#!/usr/bin/env python3
"""电脑端：通过SSH部署、启动和停止MaixCAM实时预览，不修改开机启动配置。"""

import argparse
import base64
import errno
import hashlib
import json
import os
from pathlib import Path
import shlex
import sys
import time
import urllib.request
import webbrowser

import paramiko
from connection_check import ensure_usb_route

PROJECT = Path(__file__).resolve().parents[1]
REMOTE = "/root/vision_fly_detect_preview"
CPP_RELEASE = PROJECT / "examples/camera_preview/dist/camera_preview_release"


def run(client, command, timeout=20):
    _, output, error = client.exec_command(command, timeout=timeout)
    text = output.read().decode("utf-8", errors="replace")
    diagnostic = error.read().decode("utf-8", errors="replace")
    code = output.channel.recv_exit_status()
    if code:
        raise RuntimeError(f"开发板操作失败（{code}）：{text}{diagnostic}")
    return text


def remote_python(client, source):
    return run(client, "python3 -c " + shlex.quote(source))


# 损坏或丢失的PID文件不能直接转整数；先精确查找本工程进程。
REMOTE_PID_HELPER = f"""
from pathlib import Path
def preview_pid(directory):
    try:
        value = (directory / 'preview.pid').read_text().strip()
    except FileNotFoundError:
        value = ''
    if value.isascii() and value.isdecimal() and 0 < int(value) <= 2147483647:
        return int(value)
    found = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            argv = (proc / 'cmdline').read_bytes().split(b'\\0')
            if ({str(REMOTE + '/camera_preview.py').encode()!r} in argv or
                    argv[0] == {str(REMOTE + '/camera_preview').encode()!r}):
                found.append(int(proc.name))
        except (FileNotFoundError, ProcessLookupError):
            pass
    if len(found) > 1:
        raise RuntimeError('发现多个本项目预览进程，请先检查后再启动')
    return found[0] if found else None
"""


def stop_preview(client):
    # 只停止PID文件指向的本工程进程，绝不按python进程名批量终止。
    return remote_python(client, REMOTE_PID_HELPER + f"""
import os, signal, time
from pathlib import Path
directory = Path({REMOTE!r})
pidfile = directory / 'preview.pid'
pid = preview_pid(directory)
if pid is not None:
    command = Path('/proc/%d/cmdline' % pid)
    if command.exists():
        def state():
            try:
                status = Path('/proc/%d/status' % pid).read_text()
                return next(line.split()[1] for line in status.splitlines() if line.startswith('State:'))
            except FileNotFoundError:
                return None
        argv = command.read_bytes().split(b'\\0')
        if state() == 'Z':
            pass
        elif argv == [b'']:
            raise RuntimeError('相机任务尚在内核等待，不能视为已退出；请检查驱动并重启MaixCAM恢复')
        elif ({str(REMOTE + '/camera_preview.py').encode()!r} not in argv and
              argv[0] != {str(REMOTE + '/camera_preview').encode()!r}):
            raise RuntimeError('PID属于其他程序，拒绝停止')
        else:
            # SDK默认信号处理只保证SIGINT清理资源，不能用TERM直接杀死旧版C++应用。
            os.kill(pid, signal.SIGINT)
            for _ in range(100):
                if state() in (None, 'Z'):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError('预览未正常退出，请检查preview.log')
pidfile.unlink(missing_ok=True)
print('PREVIEW_STOPPED')
""")


def pause_launcher(client):
    # 官方要求释放Launcher；兼容旧launcher_daemon及新launcher daemon两种固件。
    return remote_python(client, f"""
import os, signal, time
from pathlib import Path
directory = Path({REMOTE!r})
directory.mkdir(parents=True, exist_ok=True)
launcher = b'/maixapp/apps/launcher/launcher'
daemon = b'/maixapp/apps/launcher/launcher_daemon'
def active(proc):
    try:
        return not any(v.startswith('State:') and v.split()[1]=='Z' for v in (proc/'status').read_text().splitlines())
    except FileNotFoundError:
        return False
def matches():
    result = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit(): continue
        try:
            argv = (proc/'cmdline').read_bytes().split(b'\\0')
            if argv and argv[0] in (launcher, daemon) and active(proc): result.append((proc, argv))
        except (FileNotFoundError, ProcessLookupError): pass
    return result
def stop_group(items, timeout):
    for proc, argv in items:
        try:
            # SDK相机/串口使用SIGINT正常清理；旧独立守护程序沿用SIGTERM。
            os.kill(int(proc.name), signal.SIGTERM if argv[0] == daemon else signal.SIGINT)
        except ProcessLookupError: pass
    deadline = time.monotonic() + timeout
    while any(active(proc) for proc, _ in items):
        if time.monotonic() >= deadline:
            raise RuntimeError('Launcher尚未正常退出，相机或串口仍可能占用；未启动新的识别进程')
        time.sleep(0.1)
items = matches()
if items:
    (directory/'launcher_paused').write_text('1')
# 先停止守护进程，防止随后停止界面时又被拉起。
stop_group([(p,a) for p,a in items if a[0]==daemon or b'daemon' in a[1:]], 5)
stop_group(matches(), 7)
if matches(): raise RuntimeError('Launcher被重新拉起，不能确认串口已释放')
print('LAUNCHER_RELEASED')
""")


def restore_launcher(client):
    return remote_python(client, f"""
import subprocess
from pathlib import Path
marker = Path({REMOTE!r}) / 'launcher_paused'
if marker.exists():
    already_running = False
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if (proc / 'cmdline').read_bytes().split(b'\\0')[0] == b'/maixapp/apps/launcher/launcher_daemon':
                already_running = True
        except FileNotFoundError:
            pass
    if not already_running:
        with open('/tmp/vision_fly_launcher.log', 'ab') as log:
            subprocess.Popen(['/maixapp/apps/launcher/launcher_daemon'],
                stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)
    marker.unlink()
    print('LAUNCHER_RESTORED')
""")


def upload_tree(sftp, source, destination):
    try:
        sftp.mkdir(destination)
    except OSError:
        sftp.stat(destination)
    for item in source.iterdir():
        target = destination + "/" + item.name
        if item.is_dir():
            upload_tree(sftp, item, target)
        else:
            local_stat = item.stat()
            try:
                remote_stat = sftp.stat(target)
            except OSError as error:
                if error.errno != errno.ENOENT:
                    raise
            else:
                if (remote_stat.st_size == local_stat.st_size and
                        int(remote_stat.st_mtime) == int(local_stat.st_mtime)):
                    continue
            sftp.put(str(item), target)
            sftp.chmod(target, local_stat.st_mode & 0o777)
            sftp.utime(target, (local_stat.st_atime, local_stat.st_mtime))


def wait_ready(client, url):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    for _ in range(30):
        state = remote_python(client, REMOTE_PID_HELPER + f"""
from pathlib import Path
directory = Path({REMOTE!r})
pid = preview_pid(directory)
command = Path('/proc/%d/cmdline' % pid) if pid is not None else None
if command is None or not command.exists() or not command.read_bytes():
    print('EXITED')
else:
    log = (directory / 'preview.log').read_text(errors='replace')
    print('READY' if 'PREVIEW_READY' in log and 'PREVIEW_FIRST_FRAME' in log else 'STARTING')
""").strip()
        if state == "EXITED":
            break
        if state == "READY":
            try:
                with opener.open(url, timeout=2) as response:
                    # read1不等待EOF，兼容SDK未发送Content-Length的长连接网页。
                    # TCP可能将<html>分成多块，累积到标记出现再判断。
                    marker = b'data-app="vision-fly-camera-preview"'
                    html = b""
                    deadline = time.monotonic() + 3
                    while len(html) < 16384 and time.monotonic() < deadline:
                        block = response.read1(4096)
                        if not block:
                            break
                        html += block
                        if marker in html or b"</html>" in html:
                            break
                    if response.status != 200 or marker not in html:
                        raise RuntimeError("预览端口被其他服务占用，返回的不是本项目网页")
                with opener.open(url + "stream", timeout=3) as response:
                    frame = b""
                    for _ in range(32):
                        block = response.read1(65536)
                        if not block:
                            break
                        frame += block
                        start = frame.find(b"\xff\xd8")
                        if start >= 0 and frame.find(b"\xff\xd9", start + 2) >= 0:
                            return
            except OSError:
                pass
        time.sleep(0.5)
    print(run(client, f"tail -n 35 {REMOTE}/preview.log"))
    raise RuntimeError("未收到相机首帧，请检查以上相机日志")


def main():
    config = json.loads((PROJECT / "configs/maixcam_connection.json").read_text())
    parser = argparse.ArgumentParser(description="一条命令开启MaixCAM实时相机")
    parser.add_argument("action", nargs="?", choices=["start", "stop", "status"], default="start")
    parser.add_argument("--host", default=config["host"])
    parser.add_argument("--backend", choices=["auto", "cpp", "python"], default="auto",
                        help="auto优先使用已编译的MaixCDK C++，否则使用官方MaixPy绑定")
    parser.add_argument("--no-open", action="store_true", help="不自动打开浏览器")
    args = parser.parse_args()
    ensure_usb_route(args.host)
    local = PROJECT / ".local"
    local.mkdir(exist_ok=True)
    known_hosts = local / "maixcam_known_hosts"
    if not known_hosts.exists():
        known_hosts.touch(mode=0o600)
    client = paramiko.SSHClient()
    client.load_host_keys(str(known_hosts))
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    # 官方初代MaixCAM出厂密码root；修改过密码时通过MAIXCAM_PASSWORD环境变量传入。
    password = os.environ.get("MAIXCAM_PASSWORD", "root")
    client.connect(args.host, port=config["port"], username=config["username"], password=password,
                   timeout=6, banner_timeout=6, auth_timeout=6, look_for_keys=False, allow_agent=False)
    key = client.get_transport().get_remote_server_key()
    fingerprint = base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    print(f"已连接 MaixCAM {args.host}（SSH SHA256:{fingerprint}）")
    try:
        if args.action == "stop":
            print(stop_preview(client))
            if config.get("display", False):
                print(restore_launcher(client))
            print("相机预览已停止。")
            return
        if args.action == "status":
            print(remote_python(client, REMOTE_PID_HELPER + f"""
from pathlib import Path
directory = Path({REMOTE!r})
pidfile = directory / 'preview.pid'
pid = preview_pid(directory)
if pid is not None:
    command = Path('/proc/%d/cmdline' % pid)
    argv = command.read_bytes().split(b'\\0') if command.exists() else []
    ours = ({str(REMOTE + '/camera_preview.py').encode()!r} in argv or
            bool(argv) and argv[0] == {str(REMOTE + '/camera_preview').encode()!r})
    print(('PREVIEW_RUNNING pid=' if ours else 'PREVIEW_NOT_RUNNING old_pid=') + str(pid))
else:
    print('PREVIEW_NOT_RUNNING')
log = directory / 'preview.log'
if log.exists():
    print('\\n'.join(log.read_text(errors='replace').splitlines()[-8:]))
"""))
            return
        for key in ["width", "height"]:
            if not 1 <= int(config[key]) <= 4096:
                raise ValueError("相机宽高应在1到4096之间")
        if not 1 <= int(config["preview_port"]) <= 65535:
            raise ValueError("预览端口应在1到65535之间")
        backend = args.backend
        if backend == "auto":
            backend = "cpp" if (CPP_RELEASE / "camera_preview").is_file() else "python"
        if backend == "cpp" and not (CPP_RELEASE / "camera_preview").is_file():
            raise RuntimeError("请先执行 ./tools/build_maixcam.sh preview 编译C++预览")
        run(client, "mkdir -p " + REMOTE)
        try:
            stop_preview(client)
            sftp = client.open_sftp()
            try:
                if backend == "cpp":
                    print("正在同步 MaixCDK C++ 预览及依赖库……", flush=True)
                    upload_tree(sftp, CPP_RELEASE, REMOTE)
                    executable = f"env LD_LIBRARY_PATH={REMOTE}/dl_lib {REMOTE}/camera_preview"
                else:
                    sftp.put(str(PROJECT / "tools/camera_preview.py"), REMOTE + "/camera_preview.py")
                    executable = f"python3 -u {REMOTE}/camera_preview.py"
            finally:
                sftp.close()
            pause_launcher(client)
            command = (
                f"(nohup {executable} --width {int(config['width'])} "
                f"--height {int(config['height'])} --port {int(config['preview_port'])} "
                + ("" if config.get("display", False) else "--no-display ") +
                f"</dev/null >{REMOTE}/preview.log 2>&1 & "
                f"preview_pid=$!; test -n \"$preview_pid\" && "
                f"printf '%s\n' \"$preview_pid\" >{REMOTE}/preview.pid.tmp && "
                f"mv {REMOTE}/preview.pid.tmp {REMOTE}/preview.pid)"
            )
            run(client, command)
            url = f"http://{args.host}:{int(config['preview_port'])}/"
            wait_ready(client, url)
        except BaseException:
            try:
                stop_preview(client)
                if config.get("display", False):
                    restore_launcher(client)
            except Exception as cleanup_error:
                print(f"恢复启动器时遇到问题：{cleanup_error}", file=sys.stderr)
            raise
        print("运行方式：" + ("MaixCDK C++" if backend == "cpp" else "官方MaixPy（MaixCDK绑定）"))
        print(f"相机已启动，请打开：{url}")
        print("电脑浏览器显示画面（Lite无屏模式）。停止：./tools/preview_camera.sh stop"
              if not config.get("display", False) else
              "开发板屏幕同步显示。停止：./tools/preview_camera.sh stop")
        if not args.no_open:
            webbrowser.open(url)
    finally:
        client.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"相机预览操作失败：{error}", file=sys.stderr)
        sys.exit(1)
