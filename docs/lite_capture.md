# MaixCAM Lite：没有屏幕也能预览、拍照和录像

当前设备是无屏MaixCAM Lite，操作均在电脑完成。官方也提供 [无屏版本快速开始](https://wiki.sipeed.com/maixpy/doc/zh/README_no_screen.html)。本工程仍使用已经验证的 `maixcam` 构建平台和USB地址 `10.154.34.1`，不需要额外购买屏幕。

## 1. 开启无屏预览

在电脑终端执行：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh
```

打开 [实时画面](http://10.154.34.1:8000/)。`configs/maixcam_connection.json` 已设 `display: false`，脚本向板端程序传入 `--no-display`，不会创建LCD显示对象。仅用于以后接屏幕的设备才需要打开显示。

## 2. 在电脑拍一张照片

保留预览运行，在另一个电脑终端执行：

```bash
/home/jhh/code/vision_fly_detect/tools/capture_camera.sh photo
```

文件直接保存到电脑：

```text
/home/jhh/code/vision_fly_detect/captures/photos/
```

保存的是相机传来的原始JPEG字节，不包含浏览器网页和标题，也不会额外缩放照片。文件名使用UTC时间并带Z后缀，避免重复覆盖。

连续拍100张、至少每秒一张：

```bash
/home/jhh/code/vision_fly_detect/tools/capture_camera.sh photo --count 100 --interval 1
```

需要半秒一张时将 `--interval` 改成 `0.5`。按Ctrl+C结束，已保存照片保留。

## 3. 在电脑录制视频

录制30秒：

```bash
/home/jhh/code/vision_fly_detect/tools/capture_camera.sh record --seconds 30
```

文件直接保存到电脑：

```text
/home/jhh/code/vision_fly_detect/captures/videos/
```

输出为MP4，默认10fps、无音频，同时写入同名JSON元信息和 `.frames.jsonl` 帧时间记录。可用 `--fps 15` 改录像采样帧率。Ctrl+C可提前结束，脚本会完成MP4索引再退出；不要强制终止采集进程。

这是**电脑接收HTTP JPEG视频流后录制**，不是板端直接录制高码率原生H.264。录像帧率受USB传输、JPEG编码和电脑编码速度限制；短暂网络丢帧时会重复当前帧维持播放时长。原始接收时间记录在侧边文件中，不能将它当作曝光硬件同步数据。

电脑端使用已验证的系统OpenCV与NumPy，入口脚本通过 `/usr/bin/python3 -s` 避免用户目录NumPy2与系统OpenCV的冲突，不影响YOLO训练虚拟环境，不要求额外安装FFmpeg。

## 4. 停止和切换程序

先在采集终端按Ctrl+C结束录像/连拍，再停止板端预览：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh stop
```

Lite模式停止后不恢复屏幕菜单。要调试YOLO时先结束原始预览，再运行定位主程序；两者不能同时占用板端相机。

采集工具只接受本工程原始预览网页，拒绝把带YOLO/OpenCV叠加线的调试画面误保存成训练数据。它作为预览的另一个HTTP客户端，不会再创建第二个板端相机实例，浏览器可以同时观看。

## 5. 用来准备训练数据

先把未标注原始照片保存在captures目录，标注完成后再放到训练项目 `/home/jhh/code/maixcam_yolov8_train` 的images/labels镜像目录，不要把未标注图片直接当成背景训练。

采集不同距离、角度、光照和背景，检查镜头清晰度。训练集与验证集按场景或整段视频分开，避免同一段视频的近似帧分别进入两个集合。建议训练照片保持与实际检测相同的采集分辨率和传感器模式。

## 实机验证

已使用当前Lite无屏配置启动原始C++预览，试拍640×480 JPEG并录制2秒、10fps MP4。照片解码成功；视频回读成功且为20帧，元信息状态complete。样例在 `captures/lite_test`，当前原始预览保持运行。

本地5项采集测试通过：multipart帧体分段、不完整帧拒绝、照片原始字节保持、MP4正常结束/回读、Ctrl+C中断后部分MP4正常回读。可用 `/usr/bin/python3 -s -m unittest discover -s tests -p test_capture.py -v` 重跑。
