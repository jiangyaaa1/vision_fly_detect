#!/usr/bin/env python3
"""MaixCAM SysV启动入口：只管理本工程进程，不依赖电脑或第三方Python包。"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import time

APP = Path(__file__).resolve().parents[1]
CONFIG = 'configs/maixcam.yaml'


def atomic_text(path, text):
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('w') as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def process_running(pid):
    try:
        return not any(line.startswith('State:') and line.split()[1] == 'Z'
                       for line in (Path('/proc') / str(pid) / 'status').read_text().splitlines())
    except FileNotFoundError:
        return False


def app_processes():
    expected = str(APP / 'vision_fly_detect').encode()
    result = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            argv = (proc / 'cmdline').read_bytes().split(b'\0')
            if argv and argv[0] == expected and process_running(int(proc.name)):
                result.append(int(proc.name))
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    return result


def settings():
    # 只读取状态工具需要的标量；相机、内外参与完整YAML仍由C++严格校验。
    values = {}
    wanted = {'model_path', 'output_path', 'localization_enabled', 'pose_mode', 'class_id',
              'uart_enabled', 'uart_port'}
    for line in (APP / CONFIG).read_text().splitlines():
        if ':' not in line:
            continue
        key, value = line.split(':', 1)
        if key not in wanted:
            continue
        tokens = shlex.split(value, comments=True)
        if tokens:
            values[key] = tokens[0]
    if not values.get('model_path'):
        raise RuntimeError('主配置缺少model_path')
    model = Path(values['model_path'])
    if not model.is_absolute():
        model = APP / model
    if not model.is_file():
        raise RuntimeError('模型说明文件不存在: ' + str(model))
    metadata = dict(config=CONFIG, model_path=values['model_path'],
                    output_path=values.get('output_path', 'target_positions.jsonl'),
                    localization_enabled=int(values.get('localization_enabled', 1)),
                    pose_mode=values.get('pose_mode', 'none'),
                    class_id=int(values.get('class_id', 0)),
                    uart_enabled=int(values.get('uart_enabled', 0)),
                    uart_port=int(values.get('uart_port', 0)), startup='sysv_autostart')
    return metadata


def start():
    running = app_processes()
    if len(running) > 1:
        raise RuntimeError('发现多个识别进程，未启动额外实例')
    if running:
        atomic_text(APP / 'infantry.pid', str(running[0]))
        print('VISION_ALREADY_RUNNING pid=' + str(running[0]))
        return
    executable = APP / 'vision_fly_detect'
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise RuntimeError('识别程序不存在或不可执行')
    metadata = settings()
    environment = os.environ.copy()
    environment['LD_LIBRARY_PATH'] = str(APP / 'dl_lib') + ':' + environment.get('LD_LIBRARY_PATH', '')
    with (APP / 'infantry.log').open('wb') as log:
        child = subprocess.Popen([str(executable), CONFIG], cwd=APP, env=environment,
                                 stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                 start_new_session=True)
    atomic_text(APP / 'infantry.pid', str(child.pid))
    atomic_text(APP / 'active_run.json', json.dumps(metadata, ensure_ascii=False))
    # 不等待电脑/网络；若模型或相机立即报错，在启动日志明确记录。
    time.sleep(3)
    if child.poll() is not None:
        raise RuntimeError('识别启动后退出，请查看 ' + str(APP / 'infantry.log'))
    print('VISION_AUTOSTARTED pid=' + str(child.pid))


def stop():
    running = app_processes()
    for pid in running:
        try:
            os.kill(pid, signal.SIGINT)  # SDK正常清理相机、JPEG线程和UART。
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 12
    while any(process_running(pid) for pid in running):
        if time.monotonic() >= deadline:
            raise RuntimeError('识别尚未正常退出，未强制杀死相机驱动')
        time.sleep(.1)
    (APP / 'infantry.pid').unlink(missing_ok=True)
    print('VISION_STOPPED')


def status():
    print(json.dumps(dict(running=app_processes(), config=CONFIG, app=str(APP)), ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['start', 'stop', 'restart', 'status'], nargs='?', default='status')
    args = parser.parse_args()
    # 对启动/停止加锁，防止重复执行开机服务产生多个识别实例。
    with (APP / '.service.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.action in ('stop', 'restart'):
            stop()
        if args.action in ('start', 'restart'):
            start()
        if args.action == 'status':
            status()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('VISION_SERVICE_ERROR: ' + str(error), file=sys.stderr)
        sys.exit(1)
