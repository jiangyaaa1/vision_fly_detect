# MaixCDK C++ 实时相机预览

独立例程，不加载检测模型。默认读取 640×480 RGB 相机画面，使用官方自动帧率和三缓冲，同时显示在开发板屏幕，并在端口 8000 提供 HTTP JPEG 预览。GC4653 在该输出尺寸下选择 720p60 传感器模式，再缩放为 640×480，与默认 MaixPy 预览一致。

```bash
export MAIXCDK_PATH=/home/jhh/MaixCDK
# SDK 的打包步骤会调用 python；本机只有 python3 时可使用临时命令别名。
mkdir -p /tmp/maix_preview_build_bin
ln -sfn /usr/bin/python3 /tmp/maix_preview_build_bin/python
export PATH=/tmp/maix_preview_build_bin:$PATH
cd /home/jhh/code/vision_fly_detect/examples/camera_preview
maixcdk build -p maixcam --toolchain-id musl_t-head
maixcdk release -p maixcam --toolchain-id musl_t-head
```

把 `dist/camera_preview_release` 整个目录复制到开发板后，在该目录运行：

```bash
LD_LIBRARY_PATH=./dl_lib ./camera_preview
```

浏览器打开 `http://开发板IP:8000/`，视频流地址为 `/stream`。USB 网络连接且板子地址为 `10.154.34.1` 时，打开 `http://10.154.34.1:8000/`。

可选参数：`--width 640 --height 480 --port 8000 --no-display`。使用 `Ctrl+C` 或 `SIGTERM` 正常退出；切换其他相机程序前先停止本例程，避免同时占用相机。关闭浏览器不停止相机。

本例程额外处理 `SIGTERM`，并忽略 HTTP 连接断开可能产生的 `SIGPIPE`。正常退出按“停止推流 → 关闭相机 → 关闭显示”顺序释放资源，完成后输出 `PREVIEW_STOPPED`。若进程阻塞在底层驱动调用中，软件退出标志不能保证立即生效；电脑端工具应核对进程及线程确实退出后再启动其他相机程序。

首帧编码并成功写入推流服务后输出一次 `PREVIEW_FIRST_FRAME`。网页有 `data-app="vision-fly-camera-preview"` 标志，可供电脑端工具核对服务归属。

SDK 默认打包会省略板上系统已有的 MaixCAM、OpenCV、FFmpeg 库；运行时应在开发板检查库依赖是否齐全，且与 SDK/固件版本匹配。本例程已使用 MUSL 工具链完成交叉编译并打包；真机验证由部署流程完成。
