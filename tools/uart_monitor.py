#!/usr/bin/env python3
"""电脑接USB转TTL：接收并校验MaixCAM坐标，可发送PING或普通文本。"""
import argparse
import binascii
import json
import math
import time


def decode_coordinates(line):
    payload, checksum = line.strip().rsplit(b'*', 1)
    if len(checksum) != 4 or binascii.crc_hqx(payload, 0xffff) != int(checksum, 16):
        raise ValueError('CRC16校验失败')
    fields = payload.decode('ascii').split(',')
    if len(fields) != 8 or fields[0] != 'XYZ' or fields[3] not in ('0', '1'):
        raise ValueError('坐标包格式错误')
    if fields[4] not in ('FRD', 'CAM', 'NED', 'NED_STATIC'):
        raise ValueError('未知坐标系')
    sequence, stamp = int(fields[1]), int(fields[2])
    if not 0 <= sequence <= 0xffffffff or stamp < 0:
        raise ValueError('序号或时间戳错误')
    valid = fields[3] == '1'
    xyz = [float(value) for value in fields[5:]]
    if valid and not all(math.isfinite(value) for value in xyz):
        raise ValueError('有效坐标必须是有限数值')
    if not valid and not all(math.isnan(value) for value in xyz):
        raise ValueError('无效包必须使用nan坐标')
    return dict(sequence=sequence, timestamp_ns=stamp, valid=valid,
                frame=fields[4], unit='m', xyz=xyz if valid else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='USB转TTL设备，例如/dev/ttyUSB0')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--ping', action='store_true')
    parser.add_argument('--send', help='发送一行普通文本，板端记录到接收日志')
    parser.add_argument('--seconds', type=float, default=0, help='0持续运行，Ctrl+C退出')
    args = parser.parse_args()
    import serial
    with serial.Serial(args.port, args.baud, bytesize=8, parity='N', stopbits=1,
                       timeout=.1, write_timeout=1, xonxoff=False, rtscts=False) as port:
        if args.ping: port.write(b'PING\r\n')
        if args.send is not None:
            if '\n' in args.send or '\r' in args.send or len(args.send.encode()) > 256:
                raise ValueError('--send必须是最多256字节的单行文本')
            port.write(args.send.encode() + b'\r\n')
        start = time.monotonic()
        buffer = bytearray()
        discarding = False
        last_rx = start
        warned = False
        while args.seconds <= 0 or time.monotonic() - start < args.seconds:
            for value in port.read(256):
                if value == 10:
                    if not discarding and buffer:
                        line = bytes(buffer).rstrip(b'\r')
                        try:
                            if line == b'PONG': print('PONG：双向通信正常', flush=True)
                            else: print(json.dumps(decode_coordinates(line), ensure_ascii=False), flush=True)
                            last_rx = time.monotonic(); warned = False
                        except (ValueError, UnicodeError) as error:
                            print('丢弃无效行：' + str(error), flush=True)
                    buffer.clear(); discarding = False
                elif not discarding:
                    if len(buffer) >= 512: buffer.clear(); discarding = True
                    else: buffer.append(value)
            if not warned and time.monotonic() - last_rx > 1:
                print('超过1秒未收到有效串口消息，请检查连接；旧坐标已过期。', flush=True)
                warned = True


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
