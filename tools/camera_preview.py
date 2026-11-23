#!/usr/bin/env python3
"""在MaixCAM板端运行：调用MaixCDK的MaixPy绑定，屏幕显示并提供浏览器实时预览。"""

import argparse
import signal
import time

from maix import app, camera, display, err, http


def main():
    parser = argparse.ArgumentParser(description="MaixCAM相机实时预览")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--no-display", action="store_true")
    args = parser.parse_args()
    running = True

    def stop(_signal, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    cam = camera.Camera(args.width, args.height)
    screen = None if args.no_display else display.Display()
    stream = http.JpegStreamer(host="0.0.0.0", port=args.port)
    stream.set_html("""<!DOCTYPE html>
<html lang="zh-CN" data-app="vision-fly-camera-preview"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MaixCAM 实时相机</title>
<style>body{margin:0;padding:28px;background:#111827;color:#e5e7eb;
font:16px system-ui;text-align:center}img{width:min(100%,960px);height:auto;
border-radius:12px;background:#000}p{color:#9ca3af}</style>
<h1>MaixCAM 实时相机</h1><p>移动镜头前的物体，即可确认实时画面。</p>
<img src="/stream" alt="MaixCAM相机实时画面">
<p>USB连接预览 · 关闭本网页不会停止相机；停止请运行电脑端 stop 命令。</p>
</html>""")
    result = stream.start()
    if result != err.Err.ERR_NONE:
        raise RuntimeError(f"启动相机网页失败: {result}")
    print(f"PREVIEW_READY port={args.port} size={cam.width()}x{cam.height()}", flush=True)
    count = 0
    first_frame = True
    last_log = time.monotonic()
    try:
        while running and not app.need_exit():
            image = cam.read()
            if image is None:
                continue
            if screen is not None:
                screen.show(image)
            jpeg = image.to_jpeg()
            result = stream.write(jpeg)
            if result != err.Err.ERR_NONE:
                raise RuntimeError(f"推送相机画面失败: {result}")
            if first_frame:
                print("PREVIEW_FIRST_FRAME", flush=True)
                first_frame = False
            count += 1
            now = time.monotonic()
            if now - last_log >= 5:
                print(f"PREVIEW_FRAMES {count} fps={count / (now - last_log):.1f}", flush=True)
                count = 0
                last_log = now
    finally:
        stream.stop()
        cam.close()
        if screen is not None:
            screen.close()


if __name__ == "__main__":
    main()
