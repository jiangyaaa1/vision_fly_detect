#!/usr/bin/env python3
"""安装/恢复本工程的SysV自启动；仅修改列出的启动文件，原件独立备份。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys

SYSTEM_ROOT = Path('/')
APP_NAME = '/root/vision_fly_detect_infantry'
BACKUP_NAME = '/root/vision_fly_boot_backup'
TARGETS = ['/etc/init.d/S06maixapp', '/maixapp/apps/launcher/launcher',
           '/maixapp/apps/launcher/launcher_daemon', '/etc/init.d/S98vision_fly']
TAG = 'vision_fly_detect standalone boot'


def path(name):
    return SYSTEM_ROOT / name.lstrip('/')


def fingerprint(file):
    if file.is_symlink():
        data = b'symlink:' + os.readlink(file).encode()
    elif file.is_file():
        data = file.read_bytes()
    elif not file.exists():
        return None
    else:
        raise RuntimeError('目标不是普通文件: ' + str(file))
    return hashlib.sha256(data).hexdigest()


def atomic_bytes(file, data, mode=0o755):
    temporary = file.with_name(file.name + '.vision-tmp')
    with temporary.open('wb') as stream:
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
    temporary.chmod(mode)
    temporary.replace(file)


def backup():
    directory = path(BACKUP_NAME)
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    manifest = directory / 'originals.json'
    if manifest.exists():
        result = json.loads(manifest.read_text())
        if result.get('tag') != TAG or [r['path'] for r in result['files']] != TARGETS:
            raise RuntimeError('备份清单不属于本工程')
        return result
    service = path(TARGETS[-1])
    if service.exists():
        raise RuntimeError('S98vision_fly已存在但没有备份清单，未覆盖')
    records = []
    for index, name in enumerate(TARGETS):
        file = path(name)
        record = dict(path=name, sha256=fingerprint(file))
        if file.is_symlink():
            record.update(kind='symlink', target=os.readlink(file))
        elif file.is_file():
            stored = directory / ('original_' + str(index))
            shutil.copy2(file, stored)
            record.update(kind='file', backup=stored.name, mode=stat.S_IMODE(file.stat().st_mode))
        else:
            record['kind'] = 'missing'
        records.append(record)
    result = dict(tag=TAG, files=records)
    atomic_bytes(manifest, json.dumps(result, indent=2).encode(), 0o600)
    config = path(APP_NAME + '/configs/maixcam.yaml')
    if config.is_file():
        shutil.copy2(config, directory / 'config_at_install.yaml')
    os.sync()
    return result


def ensure_known(manifest):
    state_file = path(BACKUP_NAME) / 'installed.json'
    installed = json.loads(state_file.read_text()) if state_file.exists() else {}
    for record in manifest['files']:
        current = fingerprint(path(record['path']))
        if current not in (record['sha256'], installed.get(record['path'])):
            raise RuntimeError('文件已被其它操作修改，保留现场，未覆盖: ' + record['path'])


def restore_record(record):
    file = path(record['path'])
    if record['kind'] == 'missing':
        file.unlink(missing_ok=True)
    elif record['kind'] == 'symlink':
        temporary = file.with_name(file.name + '.vision-tmp')
        temporary.unlink(missing_ok=True)
        temporary.symlink_to(record['target']); temporary.replace(file)
    else:
        original = path(BACKUP_NAME) / record['backup']
        if fingerprint(original) != record['sha256']:
            raise RuntimeError('原件备份校验失败: ' + record['path'])
        atomic_bytes(file, original.read_bytes(), record['mode'])


def install():
    if not path(APP_NAME + '/vision_fly_detect').is_file():
        raise RuntimeError('请先部署完整识别程序')
    template = path(APP_NAME + '/board/S98vision_fly')
    if not template.is_file():
        raise RuntimeError('开机服务模板未部署')
    manifest = backup()
    ensure_known(manifest)
    original = path(BACKUP_NAME) / manifest['files'][0]['backup']
    init = original.read_text()
    launcher_line = '(/maixapp/apps/launcher/launcher_daemon &)'
    if launcher_line not in init:
        raise RuntimeError('固件启动脚本与已检查版本不符，未修改')
    init = init.replace(launcher_line, '# ' + TAG + ': Launcher autostart disabled')
    init = init.replace('echo "serial ready" > /dev/serial0',
                        '# ' + TAG + ': no startup text on UART0')
    wrapper = ('#!/bin/sh\n# ' + TAG + '\n'
               'echo "Launcher disabled: vision_fly_detect owns UART0. Use setup_autostart.py restore to restore." >&2\n'
               'exit 0\n').encode()
    try:
        atomic_bytes(path(TARGETS[0]), init.encode(), manifest['files'][0]['mode'])
        atomic_bytes(path(TARGETS[1]), wrapper)
        atomic_bytes(path(TARGETS[2]), wrapper)
        atomic_bytes(path(TARGETS[3]), template.read_bytes())
        installed = {name: fingerprint(path(name)) for name in TARGETS}
        atomic_bytes(path(BACKUP_NAME) / 'installed.json', json.dumps(installed, indent=2).encode(), 0o600)
        os.sync()
    except Exception:
        for record in manifest['files']:
            restore_record(record)
        os.sync()
        raise
    print(json.dumps(dict(installed=True, launcher_disabled=True, backup=BACKUP_NAME), ensure_ascii=False))


def restore():
    manifest_file = path(BACKUP_NAME) / 'originals.json'
    if not manifest_file.exists():
        raise RuntimeError('没有原件备份，未修改任何文件')
    manifest = json.loads(manifest_file.read_text())
    if manifest.get('tag') != TAG or [r['path'] for r in manifest['files']] != TARGETS:
        raise RuntimeError('备份清单不属于本工程')
    ensure_known(manifest)
    for record in manifest['files']:
        restore_record(record)
    os.sync()
    print('BOOT_CONFIGURATION_RESTORED; 原识别程序与标定文件保留')


def status():
    manifest = path(BACKUP_NAME) / 'originals.json'
    init = path('/etc/init.d/S98vision_fly')
    launcher = path('/maixapp/apps/launcher/launcher')
    blocked = launcher.is_file() and TAG.encode() in launcher.read_bytes()[:1024]
    print(json.dumps(dict(autostart_installed=init.is_file(), launcher_disabled=blocked,
                          originals_backed_up=manifest.is_file()), ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'restore', 'status'])
    args = parser.parse_args()
    try:
        {'install': install, 'restore': restore, 'status': status}[args.action]()
    except Exception as error:
        print('BOOT_SETUP_ERROR: ' + str(error), file=sys.stderr)
        sys.exit(1)
