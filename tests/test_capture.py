"""电脑端采集：验证multipart分段、照片原始字节和MP4可正常结束/回读。"""
import importlib.util
import io
from email.message import Message
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import cv2
import numpy as np

source = Path(__file__).resolve().parents[1] / "tools/capture_camera.py"
spec = importlib.util.spec_from_file_location("capture_camera", source)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class Response(io.BytesIO):
    def __init__(self, data):
        super().__init__(data)
        self.headers = Message()
        self.headers["Content-Type"] = "multipart/x-mixed-replace; boundary=frame"

    def read(self, size=-1):
        # 强制拆分帧体，不能假定网络read一次拿到整个JPEG。
        return super().read(min(size, 13) if size >= 0 else 13)


def jpeg():
    ok, data = cv2.imencode(".jpg", np.zeros((48, 64, 3), dtype=np.uint8))
    if not ok:
        raise RuntimeError("test JPEG encoding failed")
    return data.tobytes()


def response(image, count=1):
    block = b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " + str(len(image)).encode() + b"\r\n\r\n" + image + b"\r\n"
    return Response(block * count + b"--frame--\r\n")


class CaptureTests(unittest.TestCase):
    def test_split_body_is_not_truncated(self):
        image = jpeg()
        self.assertEqual(list(capture.frames(response(image, 2))), [image, image])

    def test_truncation_rejected(self):
        raw = b"--frame\r\nContent-Length: 500\r\n\r\n\xff\xd8"
        with self.assertRaises(OSError):
            list(capture.frames(Response(raw)))

    def test_photo_preserves_original_jpeg(self):
        image = jpeg()
        with tempfile.TemporaryDirectory() as folder:
            capture.photos(response(image), Path(folder), 1, 0)
            files = list(Path(folder).glob("*.jpg"))
            self.assertEqual(len(files), 1)
            self.assertEqual(files[0].read_bytes(), image)

    def test_mp4_closed_and_readable(self):
        with tempfile.TemporaryDirectory() as folder:
            # 3帧、10fps、0.3秒；时钟驱动，不把传感器帧率当录像帧率。
            with patch.object(capture, "frames", return_value=iter([jpeg()] * 4)), patch.object(
                capture.time, "monotonic", side_effect=[10.0, 10.11, 10.21, 10.31]
            ):
                path = capture.record(None, Path(folder), 0.3, 10)
            video = cv2.VideoCapture(str(path))
            try:
                self.assertTrue(video.isOpened())
                self.assertEqual(int(video.get(cv2.CAP_PROP_FRAME_COUNT)), 3)
                self.assertTrue(video.read()[0])
            finally:
                video.release()

    def test_interrupt_finalizes_partial_mp4(self):
        def interrupted():
            yield jpeg()
            raise KeyboardInterrupt()
        with tempfile.TemporaryDirectory() as folder:
            with patch.object(capture, "frames", return_value=interrupted()):
                path = capture.record(None, Path(folder), 30, 10)
            video = cv2.VideoCapture(str(path))
            try:
                self.assertTrue(video.read()[0])
            finally:
                video.release()
            self.assertIn('"status": "interrupted"', path.with_suffix(".json").read_text())


if __name__ == "__main__":
    unittest.main()
