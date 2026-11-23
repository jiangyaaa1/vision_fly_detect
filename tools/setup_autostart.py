#!/usr/bin/env python3
"""安装、查看或恢复Lite独立运行自启动及Launcher禁用配置。"""
import argparse
import json
import os
import shlex
import subprocess
import sys

import paramiko
from connection_check import ensure_usb_route
from maixcam_preview import PROJECT, pause_launcher, run

REMOTE = '/root/vision_fly_detect_infantry'
INIT = '/etc/init.d/S98vision_fly'


def main():
    config = json.loads((PROJECT / 'configs/maixcam_connection.json').read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'status', 'restore'], nargs='?', default='status')
    parser.add_argument('--host', default=config['host'])
    args = parser.parse_args()
    ensure_usb_route(args.host)
    if args.action == 'install':
        # 统一部署入口会打包board目录、正常释放旧相机和Launcher、更新本机主配置。
        subprocess.run([sys.executable, '-s', str(PROJECT / 'tools/infantry_lite.py'),
                        'start', '--host', args.host], check=True)
    client = paramiko.SSHClient()
    client.load_host_keys(str(PROJECT / '.local/maixcam_known_hosts'))
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    client.connect(args.host, port=config['port'], username=config['username'],
                   password=os.environ.get('MAIXCAM_PASSWORD', 'root'), timeout=8,
                   look_for_keys=False, allow_agent=False)
    try:
        command = 'python3 ' + shlex.quote(REMOTE + '/board/install_boot.py')
        if args.action == 'install':
            print(pause_launcher(client))
            print(run(client, command + ' install', timeout=45))
            print(run(client, INIT + ' start', timeout=20))
        elif args.action == 'restore':
            print(run(client, INIT + ' stop', timeout=20))
            print(run(client, command + ' restore', timeout=45))
        print(run(client, command + ' status'))
        if args.action != 'restore':
            print(run(client, INIT + ' status'))
    finally:
        client.close()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('自启动配置失败：' + str(error), file=sys.stderr)
        sys.exit(1)
