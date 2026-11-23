#!/usr/bin/env python3
"""棋盘格相机标定，输入为视频文件。

用法：
    python3 tools/calibrate_camera.py calib.mp4
    python3 tools/calibrate_camera.py                # 使用下方 VIDEO 默认值

按实际棋盘格修改下面的 cols/rows/square；输出结果可直接填入 configs/maixcam.yaml。
"""
import sys

import cv2
import numpy as np

# ---- 按实际情况修改 ----
VIDEO = "video/calib.mp4"    # 视频路径，也可用命令行参数覆盖
cols, rows = 11, 8     # 棋盘格内角点数（交点个数，不是格子数）
square = 0.040        # 每格边长，米
frame_step = 3       # 每隔多少帧取一帧，避免相邻帧几乎重复
min_move_px = 1      # 新帧角点与上一张的平均位移小于该像素值则跳过
show = True           # 是否弹窗显示；无图形界面时设为 False
# --------------------------


def main() -> int:
    video = sys.argv[1] if len(sys.argv) > 1 else VIDEO
    cap = cv2.VideoCapture(video)
    if not cap.isOpened():
        print(f"无法打开视频: {video}")
        return 1

    objp = np.zeros((cols * rows, 3), np.float32)
    objp[:, :2] = np.mgrid[0:cols, 0:rows].T.reshape(-1, 2) * square

    objpoints: list = []
    imgpoints: list = []
    last_corners = None
    image_size = None
    frame_idx = 0

    while True:
        ok, frame = cap.read()
        if not ok:
            break
        if frame_idx % frame_step != 0:
            frame_idx += 1
            continue
        frame_idx += 1

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        image_size = (gray.shape[1], gray.shape[0])  # (width, height)

        found, corners = cv2.findChessboardCorners(gray, (cols, rows), None)
        if not found:
            continue
        corners = cv2.cornerSubPix(
            gray, corners, (11, 11), (-1, -1),
            (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001))

        # 跳过与上一张几乎相同的帧，保证姿态/距离/位置有变化
        if last_corners is not None:
            move = float(np.mean(np.linalg.norm(corners - last_corners, axis=1)))
            if move < min_move_px:
                continue
        last_corners = corners.copy()

        objpoints.append(objp)
        imgpoints.append(corners)
        print(f"帧 {frame_idx}: 已采集第 {len(objpoints)} 张角点")

        if show:
            cv2.drawChessboardCorners(frame, (cols, rows), corners, found)
            cv2.imshow("checkerboard", frame)
            key = cv2.waitKey(1) & 0xFF
            if key == ord("q") or key == 27:
                break

    cap.release()
    if show:
        cv2.destroyAllWindows()

    if len(objpoints) < 5:
        print(f"有效角点帧太少（{len(objpoints)} 张），建议 15~25 张")
        return 1

    ret, mtx, dist, _rvecs, _tvecs = cv2.calibrateCamera(
        objpoints, imgpoints, image_size, None, None)

    print("\n========== 标定结果 ==========")
    print(f"有效图像数: {len(objpoints)}  尺寸: {image_size}")
    print(f"ret (平均重投影误差, 像素) = {ret:.4f}")
    print("camera_matrix (3x3) =")
    print(mtx)
    print("dist (k1,k2,p1,p2,k3) =", dist.ravel())
    print("\n可直接填入 maixcam.yaml:")
    print(f"image_width: {image_size[0]}")
    print(f"image_height: {image_size[1]}")
    print("camera_matrix:", [round(float(x), 6) for x in mtx.ravel()])
    print("distort_coeffs:", [round(float(x), 6) for x in dist.ravel()])
    return 0


if __name__ == "__main__":
    sys.exit(main())
