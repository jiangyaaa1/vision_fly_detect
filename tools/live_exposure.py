#!/usr/bin/env python3
"""通过SSH修改运行中的曝光；等待传感器读回后，只保存曝光字段。"""
import argparse
import json
import os
from pathlib import Path
import re
import time

import paramiko
import yaml

ROOT = Path(__file__).resolve().parents[1]
REMOTE = "/root/vision_fly_detect_infantry"


def replace_fields(text, settings):
    for key, value in settings.items():
        # 保留同一行的中文注释及其它配置，避免重写标定/外参。
        pattern = rf"(?m)^({re.escape(key)}\s*:\s*)[^#\n]*"
        if re.search(pattern, text):
            text = re.sub(pattern, lambda m: m.group(1) + str(value) + "  ", text)
        else:
            text += f"\n{key}: {value}\n"
    return text


def apply(host, exposure_us, gain, automatic=False):
    connection = json.loads((ROOT / 'configs/maixcam_connection.json').read_text())
    local = ROOT / 'configs/maixcam.yaml'
    settings = yaml.safe_load('\n'.join(v for v in local.read_text().splitlines() if not v.startswith('%YAML:')))
    if not 1 <= exposure_us <= 1000000 or not 1024 <= gain <= 16384:
        raise ValueError('曝光或增益超出范围')
    if not automatic and exposure_us > 1000000 // settings['camera_fps']:
        raise ValueError('曝光不能超过一帧的时间')
    client = paramiko.SSHClient()
    client.load_host_keys(str(ROOT / '.local/maixcam_known_hosts'))
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    client.connect(host, port=connection['port'], username=connection['username'],
                   password=os.environ.get('MAIXCAM_PASSWORD', 'root'), timeout=5,
                   look_for_keys=False, allow_agent=False)
    try:
        with client.open_sftp() as sftp:
            with sftp.open(REMOTE + '/active_run.json') as f:
                active = json.load(f)
            if active.get('config') != 'configs/maixcam.yaml':
                raise RuntimeError('当前运行的不是主配置，请先点“启动板端”')
            request_id = str(time.time_ns())
            fields = dict(camera_auto_exposure=int(automatic), camera_exposure_us=int(exposure_us),
                          camera_gain=int(gain), camera_control_id='"' + request_id + '"')
            path = REMOTE + '/configs/maixcam.yaml'
            with sftp.open(path) as f:
                original = f.read().decode()
            with sftp.open(path + '.live.tmp', 'w') as f:
                f.write(replace_fields(original, fields))
            sftp.posix_rename(path + '.live.tmp', path)
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                try:
                    with sftp.open(path + '.exposure.json') as f:
                        result = json.load(f)
                    if result.get('id') == request_id:
                        # 在应用确认之后保存本机配置；重新读取以保留期间用户其它改动。
                        fields.pop('camera_control_id')
                        local.write_text(replace_fields(local.read_text(), fields))
                        return result
                except FileNotFoundError:
                    pass
                time.sleep(.15)
            raise RuntimeError('未收到板端确认；请先升级并启动支持实时曝光的程序，再重试。板端配置已写入，本机未保存。')
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default=json.loads((ROOT/'configs/maixcam_connection.json').read_text())['host'])
    parser.add_argument('--us', type=int, default=2000, help='曝光微秒，2000=2毫秒')
    parser.add_argument('--gain', type=int, default=1024, help='1024=1倍')
    parser.add_argument('--auto', action='store_true')
    args = parser.parse_args()
    print(json.dumps(apply(args.host, args.us, args.gain, args.auto), ensure_ascii=False))


if __name__ == '__main__':
    main()
