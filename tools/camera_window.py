#!/usr/bin/env python3
"""电脑端原生小窗口：直接接收Lite叠加图，接收线程只保留最新JPEG，不积压旧帧。"""
import argparse
from collections import deque
import json
import subprocess
import sys
from pathlib import Path
import threading
import time
import urllib.request

from PyQt5 import QtCore, QtGui, QtWidgets

from capture_camera import frames
from startup_errors import startup_error_message
from live_exposure import apply as apply_exposure
import yaml

ROOT = Path(__file__).resolve().parents[1]
VIEW_SETTINGS = ROOT / ".local/camera_window_settings.json"


class Receiver(threading.Thread):
    def __init__(self, url):
        super().__init__(daemon=True, name="MaixCAM receiver")
        self.url = url
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.response = None
        self.latest = None
        self.sequence = 0
        self.received_at = 0.0
        self.error = "正在连接相机…"
        self.arrivals = deque(maxlen=60)

    def snapshot(self):
        with self.lock:
            times = tuple(self.arrivals)
            fps = (len(times) - 1) / (times[-1] - times[0]) if len(times) > 1 and times[-1] > times[0] else 0
            return self.latest, self.sequence, self.received_at, fps, self.error

    def run(self):
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        while not self.stopped.is_set():
            try:
                with opener.open(self.url, timeout=3) as response:
                    self.response = response
                    with self.lock:
                        self.arrivals.clear()
                    for jpeg in frames(response):
                        if self.stopped.is_set(): return
                        with self.lock:
                            # 固定一个槽：GUI忙或窗口拖动时覆盖旧帧，永不创建逐帧事件队列。
                            self.latest = jpeg
                            self.sequence += 1
                            self.received_at = time.monotonic()
                            self.arrivals.append(self.received_at)
                            self.error = ""
                    if not self.stopped.is_set():
                        raise OSError("相机流结束")
            except (OSError, ValueError) as error:
                with self.lock:
                    self.error = "连接中断，正在重连：" + str(error)
                self.stopped.wait(1)
            finally:
                self.response = None

    def stop(self):
        # 网络读取最多3秒超时；daemon线程不阻塞关闭窗口，不在UI线程等待socket。
        self.stopped.set()


class CameraWindow(QtWidgets.QMainWindow):
    def __init__(self, url, host="10.154.34.1", start_board=False):
        super().__init__()
        self.setWindowTitle("MaixCAM Lite · 装甲板识别")
        self.resize(680, 590)
        self.setMinimumSize(420, 360)
        self.sequence = -1
        self.pixmap = None
        self.display_cache_key = None
        self.display_pixmap = None
        self.jpeg = None
        self.paused = False
        self.saved_message = ""
        self.saved_until = 0
        self.host = host
        self.deploying = False
        self.startup_error = ""
        body = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(body)
        title = QtWidgets.QLabel("MaixCAM Lite　｜　Infantry-v8n")
        title.setStyleSheet("font-size:17px;font-weight:bold;padding:6px")
        layout.addWidget(title)
        self.image = QtWidgets.QLabel("正在连接相机…")
        self.image.setAlignment(QtCore.Qt.AlignCenter)
        self.image.setMinimumSize(1, 1)
        self.image.setSizePolicy(QtWidgets.QSizePolicy.Ignored, QtWidgets.QSizePolicy.Ignored)
        self.image.setStyleSheet("background:#111827;color:#cbd5e1;border-radius:6px")
        layout.addWidget(self.image, 1)
        controls = QtWidgets.QHBoxLayout()
        self.start_button = QtWidgets.QPushButton("启动板端")
        self.start_button.clicked.connect(self.start_board)
        controls.addWidget(self.start_button)
        self.pause_button = QtWidgets.QPushButton("暂停画面")
        self.pause_button.clicked.connect(self.toggle_pause)
        controls.addWidget(self.pause_button)
        save = QtWidgets.QPushButton("保存调试图")
        save.clicked.connect(self.save_frame)
        controls.addWidget(save)
        close = QtWidgets.QPushButton("关闭窗口")
        close.clicked.connect(self.close)
        controls.addWidget(close)
        layout.addLayout(controls)
        try:
            view_settings = json.loads(VIEW_SETTINGS.read_text())
            if not isinstance(view_settings, dict):
                view_settings = {}
        except (OSError, ValueError):
            view_settings = {}
        orientation = QtWidgets.QHBoxLayout()
        orientation.addWidget(QtWidgets.QLabel("画面方向"))
        self.rotation = QtWidgets.QComboBox()
        for label, angle in (("正向（0°）", 0), ("顺时针90°", 90),
                             ("旋转180°", 180), ("逆时针90°", 270)):
            self.rotation.addItem(label, angle)
        current_rotation = view_settings.get("rotation", 0)
        index = self.rotation.findData(current_rotation)
        self.rotation.setCurrentIndex(index if index >= 0 else 0)
        orientation.addWidget(self.rotation)
        self.mirror = QtWidgets.QCheckBox("左右镜像")
        self.mirror.setChecked(view_settings.get("mirror", False) is True)
        orientation.addWidget(self.mirror)
        orientation.addStretch(1)
        orientation.addWidget(QtWidgets.QLabel("仅调整显示，不改变定位坐标"))
        layout.addLayout(orientation)
        self.rotation.currentIndexChanged.connect(self.change_orientation)
        self.mirror.toggled.connect(self.change_orientation)
        exposure_controls = QtWidgets.QHBoxLayout()
        settings = yaml.safe_load("\n".join(line for line in
            (ROOT / "configs/maixcam.yaml").read_text().splitlines() if not line.startswith("%YAML:")))
        self.auto_exposure = QtWidgets.QCheckBox("自动曝光")
        self.auto_exposure.setChecked(bool(settings.get("camera_auto_exposure", 1)))
        exposure_controls.addWidget(self.auto_exposure)
        exposure_controls.addWidget(QtWidgets.QLabel("曝光"))
        self.exposure_ms = QtWidgets.QDoubleSpinBox()
        self.exposure_ms.setDecimals(2)
        self.exposure_ms.setRange(.01, 1000 / settings.get("camera_fps", 30))
        self.exposure_ms.setSingleStep(.1)
        self.exposure_ms.setSuffix(" ms")
        self.exposure_ms.setValue(settings.get("camera_exposure_us", 2000) / 1000)
        exposure_controls.addWidget(self.exposure_ms)
        self.gain = QtWidgets.QDoubleSpinBox()
        self.gain.setRange(1, 16)
        self.gain.setSingleStep(.25)
        self.gain.setSuffix(" 倍增益")
        self.gain.setValue(settings.get("camera_gain", 1024) / 1024)
        exposure_controls.addWidget(self.gain)
        self.apply_button = QtWidgets.QPushButton("实时应用并保存")
        self.apply_button.clicked.connect(self.apply_exposure)
        exposure_controls.addWidget(self.apply_button)
        layout.addLayout(exposure_controls)
        self.exposure_feedback = QtWidgets.QLabel("曝光修改后点实时应用，不重启识别。小于3毫秒可先试2.00 ms。")
        self.exposure_feedback.setWordWrap(True)
        layout.addWidget(self.exposure_feedback)
        self.exposure_busy = False
        self.exposure_result = None
        note = QtWidgets.QLabel("设置：configs/maixcam.yaml。橙色=四角，绿色=PnP成功；Cam=相机，Body=机体相对坐标，NED=世界方向相对坐标（米）。STATIC为固定姿态。")
        note.setWordWrap(True)
        layout.addWidget(note)
        self.startup_feedback = QtWidgets.QLabel()
        self.startup_feedback.setWordWrap(True)
        self.startup_feedback.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        self.startup_feedback.setStyleSheet("color:#b91c1c;padding:5px")
        self.startup_feedback.hide()
        layout.addWidget(self.startup_feedback)
        self.setCentralWidget(body)
        self.receiver = Receiver(url)
        self.receiver.start()
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.update_frame)
        self.timer.start(10)  # 及时显示新帧；没有新帧时不重复解码或播放。
        QtWidgets.QShortcut(QtGui.QKeySequence("Esc"), self, self.close)
        QtWidgets.QShortcut(QtGui.QKeySequence("Space"), self, self.toggle_pause)
        QtWidgets.QShortcut(QtGui.QKeySequence("S"), self, self.save_frame)
        if start_board:
            self.start_board()

    def apply_exposure(self):
        if self.exposure_busy or self.deploying:
            return
        self.exposure_busy = True
        self.exposure_feedback.setText("正在应用，等待传感器读回…")
        exposure = round(self.exposure_ms.value() * 1000)
        gain = round(self.gain.value() * 1024)
        automatic = self.auto_exposure.isChecked()
        def change():
            try:
                result = apply_exposure(self.host, exposure, gain, automatic)
                mode = "自动" if result["mode"] == 0 else "手动"
                message = (f"已生效并保存：{mode}，传感器读回 {result['exposure_us'] / 1000:.3f} ms，"
                           f"增益 {result['gain'] / 1024:.2f} 倍")
            except Exception as error:
                message = "应用失败：" + str(error)
            self.exposure_result = message
            self.exposure_busy = False
        threading.Thread(target=change, daemon=True, name="MaixCAM exposure").start()

    def start_board(self):
        if self.deploying or self.exposure_busy:
            return
        self.deploying = True
        self.startup_error = ""
        def deploy():
            try:
                result = subprocess.run([sys.executable, "-s", str(ROOT / "tools/infantry_lite.py"),
                                         "start", "--host", self.host], capture_output=True,
                                        text=True, timeout=100)
                if result.returncode:
                    diagnostic = ROOT / ".local/last_startup.log"
                    diagnostic.parent.mkdir(parents=True, exist_ok=True)
                    diagnostic.write_text(result.stdout + "\n" + result.stderr)
                    self.startup_error = startup_error_message(result.stdout, result.stderr)
            except (OSError, subprocess.TimeoutExpired) as error:
                self.startup_error = "启动失败：" + str(error)
            finally:
                self.deploying = False
        threading.Thread(target=deploy, daemon=True, name="MaixCAM deploy").start()

    def update_frame(self):
        jpeg, sequence, received, fps, error = self.receiver.snapshot()
        age = time.monotonic() - received if received else 0
        if not self.paused and jpeg is not None and sequence != self.sequence:
            image = QtGui.QImage.fromData(jpeg, "JPEG")
            if image.isNull():
                self.statusBar().showMessage("收到损坏图像，等待下一帧")
                return
            self.jpeg = jpeg
            self.pixmap = QtGui.QPixmap.fromImage(image)
            self.sequence = sequence
            self.draw_frame()
        status = "已暂停显示；后台仍接收最新帧" if self.paused else (
            error or ("画面已停更" if age > 2 else f"预览 {fps:.1f} 帧/秒　｜　距最近接收 {age * 1000:.0f} ms"))
        self.start_button.setEnabled(not self.deploying and not self.exposure_busy)
        self.startup_feedback.setText(self.startup_error)
        self.startup_feedback.setVisible(bool(self.startup_error))
        self.apply_button.setEnabled(not self.deploying and not self.exposure_busy)
        if self.exposure_result is not None:
            self.exposure_feedback.setText(self.exposure_result)
            self.exposure_result = None
        if self.deploying:
            status = "正在上传并启动Lite识别，请稍候…"
        elif self.startup_error and (error or age > 2):
            status = self.startup_error
        # 这里是PC端接收间隔，不能称为曝光到显示的端到端延迟。
        if time.monotonic() < self.saved_until:
            status = self.saved_message
        self.statusBar().showMessage(status)

    def oriented_pixmap(self):
        if self.pixmap is None:
            return None
        angle = int(self.rotation.currentData())
        mirror = self.mirror.isChecked()
        key = (self.pixmap.cacheKey(), angle, mirror)
        if key != self.display_cache_key:
            display = self.pixmap
            if angle:
                display = display.transformed(QtGui.QTransform().rotate(angle),
                                              QtCore.Qt.FastTransformation)
            if mirror:
                display = display.transformed(QtGui.QTransform().scale(-1, 1),
                                              QtCore.Qt.FastTransformation)
            self.display_pixmap = display
            self.display_cache_key = key
        return self.display_pixmap

    def change_orientation(self, *_):
        self.draw_frame()
        try:
            VIEW_SETTINGS.parent.mkdir(parents=True, exist_ok=True)
            VIEW_SETTINGS.write_text(json.dumps(dict(rotation=int(self.rotation.currentData()),
                                                    mirror=self.mirror.isChecked()),
                                               ensure_ascii=False, indent=2))
        except OSError as error:
            self.saved_message = "画面方向已应用，但保存设置失败：" + str(error)
            self.saved_until = time.monotonic() + 5

    def draw_frame(self):
        display = self.oriented_pixmap()
        if display is not None:
            self.image.setPixmap(display.scaled(self.image.size(), QtCore.Qt.KeepAspectRatio,
                                                QtCore.Qt.FastTransformation))

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.draw_frame()

    def toggle_pause(self):
        self.paused = not self.paused
        self.pause_button.setText("继续画面" if self.paused else "暂停画面")

    def save_frame(self):
        if self.jpeg is None:
            return
        path = ROOT / "captures/debug_snapshots"
        path.mkdir(parents=True, exist_ok=True)
        target = path / (time.strftime("debug_%Y%m%d_%H%M%S") + f"_{time.time_ns() % 1000000000:09d}.jpg")
        try:
            if int(self.rotation.currentData()) == 0 and not self.mirror.isChecked():
                target.write_bytes(self.jpeg)
            elif not self.oriented_pixmap().save(str(target), "JPEG", 90):
                raise OSError("保存旋转后的图片失败")
            self.saved_message = "已保存叠加调试图：" + str(target)
        except OSError as error:
            self.saved_message = "保存失败：" + str(error)
        self.saved_until = time.monotonic() + 5

    def closeEvent(self, event):
        self.timer.stop()
        self.receiver.stop()
        super().closeEvent(event)


def main():
    config = json.loads((ROOT / "configs/maixcam_connection.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=config["host"])
    parser.add_argument("--start-board", action="store_true", help="窗口打开后在后台部署并启动板端")
    args = parser.parse_args()
    app = QtWidgets.QApplication([])
    app.setApplicationName("MaixCAM Lite 装甲板识别")
    window = CameraWindow(f"http://{args.host}:{config['preview_port']}/stream", args.host, args.start_board)
    window.show()
    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
