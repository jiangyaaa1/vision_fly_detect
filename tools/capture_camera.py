#!/usr/bin/env python3
"""电脑端从MaixCAM原始MJPEG流拍照/录制MP4；不占用第二个板端相机实例。"""
import argparse
from datetime import datetime, timezone
import http.client
import json
import math
from pathlib import Path
import sys
import time
import urllib.request

PROJECT = Path(__file__).resolve().parents[1]
MAX_JPEG_BYTES = 16 * 1024 * 1024


def stamp():
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ")


def frames(response):
    """按Content-Length读取，不因TCP分段或JPEG内部字节而错误切帧。"""
    if response.headers.get_content_type() != "multipart/x-mixed-replace":
        raise ValueError("返回的不是MJPEG相机流，请确认原始预览已启动")
    boundary = response.headers.get_param("boundary")
    if not boundary:
        raise ValueError("相机流缺少multipart边界")
    boundary = b"--" + boundary.encode("ascii")
    while True:
        line = response.readline(8193)
        if not line:
            raise OSError("相机视频流断开")
        if len(line) > 8192:
            raise ValueError("相机帧头过长")
        if line.strip() == boundary + b"--":
            return
        if line.strip() != boundary:
            continue
        headers = http.client.parse_headers(response)
        size = int(headers.get("Content-Length", "0"))
        if not 0 < size <= MAX_JPEG_BYTES:
            raise ValueError("相机JPEG长度不合法")
        chunks = bytearray()
        while len(chunks) < size:
            block = response.read(size - len(chunks))
            if not block:
                raise OSError("相机JPEG接收不完整")
            chunks.extend(block)
        jpeg = bytes(chunks)
        if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
            raise ValueError("相机流包含无效JPEG")
        yield jpeg


def raw_preview(opener, url):
    # 训练照片不能误存成YOLO叠加图；要求本项目原始预览网页的固定标志。
    marker = b'data-app="vision-fly-camera-preview"'
    with opener.open(url, timeout=5) as response:
        data = b""
        deadline = time.monotonic() + 5
        while len(data) < 16384 and time.monotonic() < deadline:
            block = response.read1(4096)
            if not block:
                break
            data += block
            if marker in data:
                return
            if b"</html>" in data:
                break
    raise ValueError("当前不是本项目原始预览。请先运行preview_camera.sh，避免把算法叠加图当训练照片")


def photos(response, directory, count, interval):
    saved = 0
    next_time = 0.0
    for jpeg in frames(response):
        now = time.monotonic()
        if now < next_time:
            continue
        path = directory / ("photo_" + stamp() + ".jpg")
        with path.open("xb") as output:
            output.write(jpeg)
        saved += 1
        print(f"已拍摄 {saved}/{count}：{path}", flush=True)
        if saved >= count:
            return saved
        next_time = now + interval
    raise OSError("未完成拍照，相机流已结束")


def record(response, directory, seconds, fps):
    # 系统OpenCV已实测可写MP4。由shell入口使用/usr/bin/python3 -s避开全局NumPy版本冲突。
    import cv2
    import numpy as np

    path = directory / ("video_" + stamp() + ".mp4")
    if path.exists():
        raise FileExistsError(path)
    writer = None
    written = 0
    started = None
    size = None
    outcome = "complete"
    timing = None
    interrupted = False
    try:
        timing = path.with_suffix(".frames.jsonl").open("x", encoding="utf-8")
        for jpeg in frames(response):
            now = time.monotonic()
            if started is None:
                started = now
            elapsed = now - started
            if elapsed >= seconds:
                break
            scheduled = min(math.floor(elapsed * fps) + 1, math.ceil(seconds * fps))
            if scheduled <= written:
                continue
            image = cv2.imdecode(np.frombuffer(jpeg, dtype=np.uint8), cv2.IMREAD_COLOR)
            if image is None:
                raise ValueError("相机JPEG无法解码")
            current_size = (image.shape[1], image.shape[0])
            if writer is None:
                size = current_size
                writer = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), fps, size)
                if not writer.isOpened():
                    raise RuntimeError("系统OpenCV无法创建MP4文件")
                print(f"录像中：{path}，{seconds:g}秒，{fps:g}fps；Ctrl+C可提前结束", flush=True)
            elif size != current_size:
                raise ValueError("录像过程中相机分辨率发生变化")
            # 按电脑单调时钟采样；短时丢帧时重复当前帧以维持正确播放时长。
            # 每帧实际接收时间单独保存，不能把此MP4当作硬件同步采集。
            while written < scheduled:
                writer.write(image)
                timing.write(json.dumps({"frame": written, "video_seconds": written / fps,
                                         "received_monotonic": now}) + "\n")
                written += 1
        if started is None or not written:
            raise OSError("没有收到可录制的相机帧")
        if written < math.ceil(seconds * fps):
            outcome = "stream_ended_early"
    except KeyboardInterrupt:
        outcome = "interrupted"
        interrupted = True
    except BaseException:
        outcome = "failed_partial"
        raise
    finally:
        if writer is not None:
            writer.release()  # 包括Ctrl+C在内都写完MP4索引，不能直接kill -9。
        if timing is not None:
            timing.close()
        metadata = {"status": outcome, "source": "MaixCAM raw HTTP MJPEG",
                    "codec": "mp4v", "audio": False, "fps": fps, "frames": written,
                    "requested_seconds": seconds, "playback_seconds": written / fps,
                    "size": size, "file": str(path)}
        path.with_suffix(".json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    if not written:
        raise OSError("录像尚未收到首帧")
    print(f"已保存：{path}（{written}帧，播放时长{written / fps:.1f}秒，{outcome}）", flush=True)
    if not interrupted and outcome != "complete":
        raise OSError("相机流提前结束，已保留可用的部分录像和时间记录")
    return path


def main():
    settings = json.loads((PROJECT / "configs/maixcam_connection.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["photo", "record"])
    parser.add_argument("--host", default=settings["host"])
    parser.add_argument("--port", type=int, default=settings["preview_port"])
    parser.add_argument("--out-dir", type=Path, help="电脑上的输出目录，默认本项目captures/photos或videos")
    parser.add_argument("--count", type=int, default=1, help="连续拍照张数")
    parser.add_argument("--interval", type=float, default=1.0, help="连续拍照最小间隔秒数")
    parser.add_argument("--seconds", type=float, default=30.0, help="录像时长秒数")
    parser.add_argument("--fps", type=float, default=10.0, help="电脑端录像采样帧率，不是传感器帧率")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.count <= 100000:
        raise ValueError("端口或拍照张数不合法")
    if not math.isfinite(args.interval) or args.interval < 0:
        raise ValueError("拍照间隔应为有限非负数")
    if not math.isfinite(args.seconds) or not 0 < args.seconds <= 3600:
        raise ValueError("单段录像时长应在0到3600秒之间")
    if not math.isfinite(args.fps) or not 1 <= args.fps <= 30:
        raise ValueError("录像fps应在1到30之间")
    directory = args.out_dir or PROJECT / "captures" / ("photos" if args.action == "photo" else "videos")
    directory = directory.expanduser().resolve()
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    url = f"http://{args.host}:{args.port}/"
    raw_preview(opener, url)
    directory.mkdir(parents=True, exist_ok=True)
    with opener.open(url + "stream", timeout=5) as response:
        if args.action == "photo":
            photos(response, directory, args.count, args.interval)
        else:
            record(response, directory, args.seconds, args.fps)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit("采集已停止，已保存的文件保留。")
    except Exception as error:
        raise SystemExit(f"采集失败：{error}\n请先运行tools/preview_camera.sh启动原始预览。")
