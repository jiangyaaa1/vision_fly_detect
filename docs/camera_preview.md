# 本机 MaixCAM 连接与相机预览

**设备已确认是无屏MaixCAM Lite。** 默认关闭板端显示，请通过电脑浏览器操作；拍照、连拍、录制MP4使用 [Lite电脑端采集说明](lite_capture.md)。下文涉及板端屏幕/菜单的描述属于此前按带屏型号进行的测试记录，当前Lite配置不需要操作屏幕。

本机连接实测：2026-11-08。初代RISC-V MaixCAM，主机名 `maixcam-9a22`，系统 `maixcam-2026-01-24-maixpy-v4.12.5`。USB双网口地址为 `10.154.34.1` / `10.154.35.1`，当前使用前者；电脑对应地址为 `.100`。这台设备的实际地址与部分旧教程中的 `10.127.117.1` 不同，操作以实测为准。

## 一条命令看到镜头画面

在电脑终端执行：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh
```

命令会连接开发板、部署本工程预览程序、暂时退出Launcher以释放相机、启动640×480实时预览，并尝试打开浏览器。

默认优先使用已经编译好的MaixCDK C++预览；尚无C++发布文件时使用板上官方MaixPy绑定。电脑端会检查本工程进程、网页标记和实际JPEG首帧后才报告成功；启动失败会尝试停止本项目进程并恢复Launcher。

直接访问也可以：

[打开 MaixCAM 实时画面](http://10.154.34.1:8000/)

当前Lite只在电脑显示画面。把镜头朝向有光的场景，移动镜头前的物体检查实时性。若画面很暗，请检查镜头盖、遮挡和光照。

关闭网页不会停止相机。停止预览（Lite不恢复屏幕菜单）：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh stop
```

查看相机最近日志：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh status
```

脚本只停止本项目PID文件指向且命令匹配的进程，不按 `python` 名称批量结束程序。未设置开机自启，重启/断电后需要再次运行启动命令。预览占用相机时，不能同时运行YOLO/PnP主程序或其他相机应用。

## 连接参数

[configs/maixcam_connection.json](../configs/maixcam_connection.json) 保存USB地址、SSH端口、预览端口和分辨率，不保存密码。使用官方初代MaixCAM的出厂账号；若密码已修改，通过 `MAIXCAM_PASSWORD` 环境变量传入。首次连接的SSH主机密钥保存在工程 `.local/maixcam_known_hosts`，后续变化会被拒绝，不会关闭主机密钥检查。

如果USB地址发生变化：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh --host 新的板端IP
```

脚本的就绪检查直接访问USB地址，不走电脑HTTP代理。如果电脑浏览器打不开，检查代理是否将 `10.154.34.1` 转发到外网，并为该USB地址选择直连；也可试用另一USB地址 `10.154.35.1`。不需要连接开发板Wi-Fi。

## 本机 MaixCDK 编译环境

已检查电脑安装：

| 项目 | 实际位置/版本 |
|---|---|
| MaixCDK SDK | `/home/jhh/MaixCDK` |
| SDK提交 | `b7f57665309084082f56a1e79a9d9c114eedd4dc` |
| maixtool / maixcdk | `1.4.4`，`/home/jhh/.local/bin` |
| RISC-V MUSL工具链 | Xuantie V2.6.1，GCC10.2.0，SDK `dl/extracted/toolchains/maixcam/host-tools/gcc/riscv64-linux-musl-x86_64` |
| 主机工具 | CMake3.22.1、C/C++编译工具和SDK Python依赖已安装 |

为当前终端启用SDK路径：

```bash
source /home/jhh/code/vision_fly_detect/tools/maixcdk_env.sh
```

这只影响当前终端，不改用户 `.bashrc`。SDK某些后处理脚本使用 `python` 命令，本机只有 `python3`，环境脚本会在工程 `.local/maixcdk-bin` 提供临时兼容别名，不修改系统Python。编译纯MaixCDK C++预览例程：

```bash
/home/jhh/code/vision_fly_detect/tools/build_maixcam.sh preview
```

编译主项目则去掉 `preview` 参数。主项目的模型、目标尺寸、标定和外参仍需按实际填写；仅查看镜头画面不要求这些参数，所以预览例程独立于定位程序。

显式选择实现：

```bash
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh --backend cpp
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh --backend python
```

电脑端脚本使用当前已经安装的 `paramiko` 通过SSH传输文件；不要求安装MaixVision。C++发布目录整体上传，保留 `dl_lib` 依赖库，不单独只复制可执行文件。

## 预览实现

纯C++例程位于 `examples/camera_preview`，使用 `maix::camera::Camera`、`maix::display::Display` 和 `maix::http::JpegStreamer`。一键脚本在本机存在 `dist/camera_preview_release/camera_preview` 时优先部署它以及依赖库。

备用源文件 `tools/camera_preview.py` 使用上述接口的官方MaixPy绑定 `maix.camera.Camera`、`maix.display.Display` 和 `maix.http.JpegStreamer`，利用板上已安装的运行库即可实机显示，无需等待首次视觉依赖下载。

电脑端通过SSH上传预览程序到 `/root/vision_fly_detect_preview`，日志位于其 `preview.log`。串流只在当前开发板网络上提供，不上传到外部服务器。

官方依据：[MaixCDK快速开始](https://wiki.sipeed.com/maixcdk/doc/zh/index.html)、[JPEG网页串流](https://wiki.sipeed.com/maixpy/doc/zh/video/jpeg_streaming.html)。

## 已完成的实机检查

SSH连接、系统/传感器识别、640×480采集、开发板屏幕输出、HTTP页面、实际JPEG首帧、一键停止与恢复Launcher、重新启动及PID状态查询均已通过。传感器日志识别为GC4653。Python绑定预览运行日志中采集/推送约30–40fps，这不是浏览器显示帧率或网络时延保证。

纯MaixCDK C++例程已经完成真实RISC-V MUSL交叉编译、链接和发布，并上传到当前MaixCAM实机成功运行，电脑端已收到其实际JPEG首帧。当前默认一键命令会运行这一C++版本；Python绑定保留作为可选备用。

本次还修复了停止后重开的问题：该版SDK默认只处理SIGINT，直接用SIGTERM结束旧程序会跳过相机清理。预览例程已补充SIGTERM正常退出、忽略连接断开引起的SIGPIPE，并按“停推流→关闭相机→关闭显示”的顺序清理；电脑端停止命令使用SIGINT且确认进程真正退出。已在驱动恢复后实机完成C++正常停止、菜单恢复、重新启动和首帧验证。预览使用自动帧率/三缓冲，与此前成功的Python预览传感器模式保持一致。

首次编译的20项视觉依赖已缓存到 `/home/jhh/MaixCDK/dl` 并按SDK清单验证SHA256。部分官方直链返回504时改用对应项目的官方发布API下载同一资源，没有修改SDK源码。后续构建可复用这些缓存。

定位主程序仍需要你的目标模型、内参、目标尺寸、安装外参和飞控实时姿态；本次相机连通验证不会自动补出这些标定参数。


## SSH提示Error reading SSH protocol banner

此错误发生在SSH握手阶段，未进入相机启动。先确认电脑能枚举到MaixCAM USB设备，并出现10.154.34.100或10.154.35.100等直连地址。若USB接口不存在，10.154.34.1可能被VPN虚拟网卡Meta等接管，连接到的并不是板子。

目前preview_camera.sh和infantry_lite.py会在访问默认USB地址前检查本机接口和实际路由，分别提示“未检测到USB网络”或“目标被VPN/其它网卡路由”。不会自动改电脑VPN设置。请使用数据USB线并确认稳定供电，可先直连电脑USB口、绕开扩展坞，再等待板子开机。若直连USB已正常而SSH仍握手失败，才继续检查板端SSH服务和开机状态。自定义--host的Wi-Fi/以太网地址不套用此USB检查。
