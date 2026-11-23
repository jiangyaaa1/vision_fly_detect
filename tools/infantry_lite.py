#!/usr/bin/env python3
"""电脑端一条命令部署、启动、停止Lite独立装甲板推理。"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import urllib.request

import paramiko
import yaml
from startup_errors import board_failure_detail
from connection_check import ensure_usb_route
from maixcam_preview import PROJECT, pause_launcher, remote_python, run, stop_preview, upload_tree

REMOTE = "/root/vision_fly_detect_infantry"


def stop(client):
    return remote_python(client, f"""
import os, signal, time
from pathlib import Path
p = Path({REMOTE!r}) / 'infantry.pid'
# 断电后PID可能为空或被其它程序复用；重新按准确可执行路径核对，不停止无关进程。
matches = []
for entry in Path('/proc').iterdir():
    if not entry.name.isdigit(): continue
    try:
        argv = (entry/'cmdline').read_bytes().split(b'\\0')
        state = (entry/'status').read_text().splitlines()
        zombie = any(v.startswith('State:') and v.split()[1]=='Z' for v in state)
        if argv and argv[0] == {str(REMOTE + '/vision_fly_detect').encode()!r} and not zombie:
            matches.append(entry.name)
    except (FileNotFoundError, ProcessLookupError, PermissionError): pass
if len(matches) > 1: raise RuntimeError('发现多个本工程进程，请检查运行状态')
if matches: p.write_text(matches[0])
elif p.exists(): p.unlink()

if p.exists():
    pid = int(p.read_text())
    proc = Path('/proc') / str(pid)
    def alive():
        if not proc.exists(): return False
        return not any(v.startswith('State:') and v.split()[1] == 'Z' for v in (proc/'status').read_text().splitlines())
    if alive():
        argv = (proc/'cmdline').read_bytes().split(b'\\0')
        if not argv or argv[0] != {str(REMOTE + '/vision_fly_detect').encode()!r}:
            raise RuntimeError('PID不属于本工程，拒绝停止')
        os.kill(pid, signal.SIGINT)
        for _ in range(100):
            if not alive(): break
            time.sleep(0.1)
        else: raise RuntimeError('推理未正常退出，请检查infantry.log')
    p.unlink()
print('INFANTRY_STOPPED')
""")


def main():
    config = json.loads((PROJECT / "configs/maixcam_connection.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["start", "stop", "status"], nargs="?", default="start")
    parser.add_argument("--host", default=config["host"])
    parser.add_argument("--config", default="configs/maixcam.yaml",
                        choices=["configs/maixcam.yaml", "configs/infantry_lite.yaml", "configs/infantry_fast.yaml", "configs/infantry_quality.yaml"])
    args = parser.parse_args()
    ensure_usb_route(args.host)
    release = PROJECT / "dist/vision_fly_detect_release"
    if args.action == "start":
        # GUI使用python -s隔离系统Qt/OpenCV；SDK打包工具在用户目录，单独子进程加载。
        package_env = os.environ.copy()
        package_env.pop("PYTHONNOUSERSITE", None)
        subprocess.run([sys.executable, str(PROJECT / "tools/package_maixcam.py")],
                       env=package_env, check=True)
        # 检查实际将上传的快照，避免用户编辑配置时，打印值与打包文件不一致。
        source = (release / args.config).read_text()
        settings = yaml.safe_load("\n".join(line for line in source.splitlines() if not line.startswith("%YAML:")))
        if not isinstance(settings, dict) or not isinstance(settings.get("model_path"), str):
            raise ValueError("配置缺少有效model_path")
        for name in ("vision_fly_detect", "models/infantry/infantry.mud",
                     "models/infantry/infantry_bf16.cvimodel", args.config):
            if not (release / name).is_file():
                raise FileNotFoundError(f"缺少发布文件{name}；请先完成转换并执行tools/build_maixcam.sh")
    client = paramiko.SSHClient()
    client.load_host_keys(str(PROJECT / ".local/maixcam_known_hosts"))
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    client.connect(args.host, port=config["port"], username=config["username"],
                   password=os.environ.get("MAIXCAM_PASSWORD", "root"), timeout=8,
                   look_for_keys=False, allow_agent=False)
    try:
        if args.action == "stop":
            print(stop(client))
            return
        if args.action == "status":
            print(remote_python(client, f"""
import json
from pathlib import Path
directory = Path({REMOTE!r})
p = directory / 'infantry.pid'
ours = False
if not p.exists() or not p.read_text().strip():
    print('INFANTRY_STOPPED')
else:
    pid = int(p.read_text()); proc = Path('/proc') / str(pid)
    command = proc / 'cmdline'
    ours = command.exists() and command.read_bytes().split(b'\\0')[0] == {str(REMOTE + '/vision_fly_detect').encode()!r}
    print(('INFANTRY_RUNNING pid=' if ours else 'INFANTRY_NOT_RUNNING old_pid=') + str(pid))
    if ours:
        print('ACTIVE_ARGV=' + command.read_bytes().replace(b'\\0', b' ').decode())
metadata = directory / 'active_run.json'
def tail(path, count):
    if not path.is_file():
        print('尚无输出文件: ' + str(path)); return
    with path.open('rb') as f:
        f.seek(max(0, path.stat().st_size - 8192))
        print('\\n'.join(f.read().decode(errors='replace').replace(chr(0), '').splitlines()[-count:]))
if metadata.is_file():
    active = json.loads(metadata.read_text())
    print('ACTIVE_SETTINGS=' + json.dumps(active, ensure_ascii=False))
    tail(directory / 'infantry.log', 12)
    if ours and active.get('output_path'):
        tail(directory / active['output_path'], 2)
    elif not ours:
        print('进程未运行，不展示旧坐标结果')
else:
    tail(directory / 'infantry.log', 12)
    print('旧进程未记录配置快照，请重启以确认当前配置和结果文件')
"""))
            return
        # 仅停止本工程的预览及推理任务，释放相机；Lite无需恢复图形Launcher。
        print(stop_preview(client))
        print(stop(client))
        print(pause_launcher(client))
        with client.open_sftp() as sftp:
            upload_tree(sftp, release, REMOTE)
            sftp.chmod(REMOTE + "/vision_fly_detect", 0o755)
        print(remote_python(client, f"""
import configparser
from pathlib import Path
p = Path({REMOTE!r})
model = (p / {settings['model_path']!r}).resolve()
if not model.is_file():
    raise RuntimeError('模型说明文件不存在: ' + str(model))
mud = configparser.ConfigParser()
mud.read(model)
weights = (model.parent / mud['basic']['model']).resolve()
if not weights.is_file():
    raise RuntimeError('实际模型不存在: ' + str(weights))
print('ACTIVE_CONFIG=' + str(p / {args.config!r}))
print('MODEL_MUD=' + str(model))
print('MODEL_WEIGHTS=' + str(weights))
print('PNP_ENABLED=' + str({int(settings.get('localization_enabled', 1))!r}))
print('POSE_MODE=' + {str(settings.get('pose_mode', 'none'))!r})
"""))
        if settings.get("localization_enabled", 1) and settings.get("pose_mode", "none") == "none":
            print("YOLO/PnP已启用；尚未接入姿态，只能输出相机/机体坐标，世界坐标为null。")
        if settings.get("pose_mode") == "static":
            print("当前为static固定姿态测试，世界方向按配置四元数计算，并非实时飞控姿态。")
        print(remote_python(client, f"""
import os, subprocess, json
from pathlib import Path
p = Path({REMOTE!r})
env = os.environ.copy()
env['LD_LIBRARY_PATH'] = str(p/'dl_lib') + ':' + env.get('LD_LIBRARY_PATH','')
with (p/'infantry.log').open('wb') as log:
    child = subprocess.Popen([str(p/'vision_fly_detect'), {args.config!r}],
        cwd=p, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)
(p/'infantry.pid').write_text(str(child.pid))
(p/'active_run.json').write_text(json.dumps({dict(config=args.config, model_path=settings['model_path'], output_path=settings.get('output_path', 'target_positions.jsonl'), localization_enabled=settings.get('localization_enabled', 1), pose_mode=settings.get('pose_mode', 'none'), class_id=settings.get('class_id', 0))!r}, ensure_ascii=False))
print('INFANTRY_STARTED pid=' + str(child.pid))
"""))
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        deadline = time.monotonic() + 60
        url = f"http://{args.host}:8000/"
        while time.monotonic() < deadline:
            try:
                with opener.open(url + "stream", timeout=5) as response:
                    data = b""
                    while len(data) < 4000000:
                        block = response.read1(65536)
                        if not block: break
                        data += block
                        start = data.find(b'\xff\xd8')
                        if start >= 0 and data.find(b'\xff\xd9', start + 2) >= 0:
                            print(f"板端推理与调试画面已启动：{url}")
                            return
            except OSError:
                pass
            state = remote_python(client, f"""
from pathlib import Path
p = Path({REMOTE!r}); pid = int((p/'infantry.pid').read_text())
s = Path('/proc')/str(pid)/'status'
print('RUNNING' if s.exists() and not any(v.startswith('State:') and v.split()[1]=='Z' for v in s.read_text().splitlines()) else 'EXITED')
""").strip()
            if state == "EXITED": break
            time.sleep(1)
        diagnostic = run(client, f"tail -n 40 {REMOTE}/infantry.log")
        print(diagnostic)
        detail = board_failure_detail(diagnostic)
        if detail:
            raise RuntimeError("板端启动失败：" + detail)
        raise RuntimeError("尚未收到推理首帧，请查看以上板端日志")
    finally:
        client.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"操作失败：{error}", file=sys.stderr)
        sys.exit(1)
