# MaixCAM 无人机目标相对定位
**远距离识别：** 已增加全图/切片交替搜索及局部跟踪，配置、限制与对比方法见[远距离调试](docs/long_range.md)。

本工程位于 `/home/jhh/code/other_code/vision_fly_detect`，采用 **C++17 + MaixCDK**，面向初代 **Sipeed MaixCAM / MaixCAM Pro RISC-V**。参考 `/home/jhh/code/sp_vision_25-main` 的 Detector、Solver、配置驱动及模块划分，实现定位链路：

**已连接MaixCAM，只想查看镜头画面：** 执行 `/home/jhh/code/vision_fly_detect/tools/preview_camera.sh`，然后访问 [实时画面](http://10.154.34.1:8000/)。相机预览不需要填写PnP标定参数；详细启动/停止和本机环境说明见 [相机预览](docs/camera_preview.md)。

**当前硬件已确认是无屏MaixCAM Lite。** 已默认关闭LCD显示；电脑端拍照/录像分别使用 `tools/capture_camera.sh photo`、`tools/capture_camera.sh record --seconds 30`，文件直接保存到电脑。详见 [Lite无屏采集](docs/lite_capture.md)。

**640×480高速版：** 默认已启用INT8主体＋BF16完整角点回归，此前预览限10FPS时真实相机处理约19FPS；现已按要求取消预览限速，实际速度需以Process为准；300帧YOLO→角点→PnP→世界变换＋异步调试的模拟标定基准约22.3FPS。实际世界坐标仍需真实标定与姿态，详见[优化与验证](docs/optimization.md)。

**不用浏览器的小窗口：** 执行 `./tools/show_camera.sh`，自动启动Lite识别并打开电脑窗口。已去除空白调试面板并使用最新帧显示，旧BF16版约5.2～5.4FPS，高速版约19FPS；见[小窗口使用](docs/camera_window.md)。

**深大装甲板模型独立运行：** 已增加专用转换与板端解析，默认统一使用 `configs/maixcam.yaml`（你填写的标定、外参和姿态设置）；启动与标定步骤见 [深大模型使用说明](docs/infantry_lite.md)。

**查看YOLO框和OpenCV特征线：** 主程序已支持浏览器三联调试画面（叠加图、Canny边缘、二值图）。尚未标定时可用 `configs/debug_detection.yaml` 只调检测；具体配置、启动命令和颜色说明见 [算法调试](docs/debugging.md)。

```text
MaixCDK Camera -> YOLOv8 / 深大Infantry-v8n -> 真实目标四角 -> 平面PnP
             -> 相机坐标 -> 机体FRD坐标 -> 世界NED方向的相对位移
```

当前支持**已知实际宽高、刚性、平面的矩形目标**。普通检测模型通过 ROI 轮廓获取真实四角，也可使用自训四点 YOLOv8-Pose。普通 YOLO 检测框的角点不能代替物理角点。深大装甲板适配已随本机工程提供转换后的模型；通用检测仍需自行提供模型。本工程不含实测内外参及飞控专用桥接程序。

**角点、坐标和全类别：** 主配置 `class_id: -1` 已支持检测全部模型类别，PnP按类别选择实际尺寸；绿色目标的Cam/Body/NED相对坐标直接叠加在窗口。重编译/重启命令和坐标轴说明见[查看角点与坐标](docs/view_corners_and_coordinates.md)。

**UART0双向通信：** 主配置uart_port=0，A16发送、A17接收，115200/8N1；默认发送机体FRD相对坐标（米），并接收文本、回复PING。配置说明、带CRC的文本协议、接线与电脑测试脚本见[UART0收发说明](docs/uart0.md)。

**独立运行自启动：** 已增加SysV开机服务及Launcher永久禁用/恢复工具，通电后自动执行识别和UART0发送；安装内容、状态和恢复命令见[开机自启动说明](docs/autostart.md)。

## 先修改这个文件

正常启动已统一读取 `configs/maixcam.yaml`。电脑源工程与板端部署目录、实际模型路径及static姿态含义见[当前配置入口](docs/configuration.md)。

[configs/maixcam.yaml](configs/maixcam.yaml) 已用中文说明每项含义、单位和填写要求。启动前需填写：

1. `model_path`、`class_id`：板端 `.mud` 路径及对应目标类别；同目录放匹配的 `.cvimodel`。
2. `image_width/image_height`、`camera_matrix`、`distort_coeffs`：实际采集模式的针孔相机标定参数。标定照片与运行图像的尺寸、裁剪、镜像、焦距必须一致。仅支持 Brown5，顺序为 k1,k2,p1,p2,k3。
3. `target_width/target_height`：实际角点间宽高，单位**米**；不同尺寸的同类别物体不能混用一个尺寸。
4. `R_camera2body/t_camera2body`：真实安装外参。默认旋转只是相机水平朝正前方的理想安装示例。
5. 确认参数后，设置 `calibration_valid: 1` 和 `extrinsics_valid: 1`。占位配置会主动报错，防止生成看似有效的错误坐标。
6. 需要世界方向下的位移时，接入实时飞控姿态并设置 `pose_mode: "udp"`。`none` 模式只输出相机/机体结果；`static` 仅供固定姿态台架验证。

内参可在电脑上使用棋盘格和 OpenCV 标定获得，具体方法见 [OpenCV 官方标定教程](https://docs.opencv.org/4.x/dc/dbb/tutorial_py_calibration.html)。拍摄多种倾斜角度、不同距离并覆盖整个视野，检查单张及总体重投影误差。相机与机体的轴向和杆臂须单独测量/标定，棋盘格内参标定不会自动得到这些安装参数。

## 坐标含义

| 字段 | 原点及轴向 | 有效条件 |
|---|---|---|
| `xyz_in_camera` | 相机光心，x右/y下/z前 | 角点与PnP通过 |
| `xyz_in_body` | 无人机机体原点，FRD：x前/y右/z下 | 正确安装外参 |
| `relative_xyz_in_world` | 无人机原点到目标中心的位移，按NED：x北/y东/z下表达 | 图像时刻的有效姿态 |
| `xyz_in_world` | 飞控导航世界原点下目标的绝对位置 | 姿态包还提供同一NED系下无人机位置 |

全部长度单位为米。箭头表示**无人机到目标**，不是相反方向；相机正前方目标的 `xyz_in_camera.z` 应为正。

```text
p_body       = R_camera2body * p_camera + t_camera2body
relative_NED = R_body2world * p_body
target_NED   = drone_NED + relative_NED
```

四元数顺序为 `[w,x,y,z]`，表示主动旋转 FRD→NED。若飞控给的是逆方向、ENU或FLU，桥接端必须先转换。本版本假定相机与机体刚性连接；运动云台需要按帧更新相机到机体外参，不能沿用常量。

## 在电脑上验证

主机需要 CMake、C++17 编译器和包含 calib3d 的 OpenCV 开发包；电脑的 calib3d 仅用于测试基准，板端不依赖它。

```bash
cd /home/jhh/code/vision_fly_detect
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

测试覆盖平面PnP与 OpenCV IPPE 对照、透视轮廓角点、关键点异常、姿态插值、缺失姿态拒绝、安装偏移与坐标变换，以及合成图像到NED位移的完整几何链路。主机测试不执行真实 NPU 推理。

还可对板上采集的**原分辨率照片**测试轮廓与PnP：

```bash
# 后四项是照片中目标检测框的x,y,width,height；不在电脑上运行YOLO。
./build-host/vision_offline configs/maixcam.yaml target.png 200 140 180 120
```

本次验证结果见 [docs/validation.md](docs/validation.md)。

## 使用 MaixCDK 编译与上板

已按用户指定的 [MaixCDK 官方中文文档](https://wiki.sipeed.com/maixcdk/doc/zh/index.html) 阅读，并核对官方源码，详情见 [docs/maixcdk_notes.md](docs/maixcdk_notes.md)。首次安装 SDK 和交叉工具链按官方文档操作；建议 SDK 版本与板端固件对应。

```bash
# 在已安装MaixCDK及其Python依赖的Linux x86_64电脑上执行。
export MAIXCDK_PATH=/你的实际路径/MaixCDK
cd /home/jhh/code/vision_fly_detect
maixcdk menuconfig -p maixcam
maixcdk build -p maixcam
maixcdk release -p maixcam
```

`main/CMakeLists.txt` 注册源码与 `basic/vision/nn/opencv` 依赖；根目录 CMakeLists 由 MaixCDK 自动生成。主机测试构建在 `tests`，与板端构建分开。

将 `dist` 中生成的发布目录整体复制到板上，保留可执行文件、`dl_lib`、配置及模型的相对路径。可通过 MaixVision 或 SSH/文件传输完成。先使用 MaixVision 连接设备，使相机 Launcher 退出，避免相机资源占用。进入发布目录运行：

```bash
./vision_fly_detect configs/maixcam.yaml
```

模型使用绝对路径时，还需单独把 `.mud` 与 `.cvimodel` 放到相应路径；放在本工程 `models` 目录随包发布时，改配置为 `models/target.mud`。模型转换、Pose输出要求和SDK版本核实记录均在 [MaixCDK说明](docs/maixcdk_notes.md)。

**MaixCAM 默认 OpenCV 未启用 calib3d。** 因此本工程以 core 实现去畸变、单应性初值和多初值LM平面PnP，不直接调用参考工程的 `cv::solvePnP`。此实现不等价于 IPPE 双解穷举；远距离、小目标、近正视及对称目标存在弱可观测性或方向歧义。没有可靠角点时拒绝输出。

## 输出与飞控接入

默认每帧向运行目录的 `target_positions.jsonl` 追加一条 JSON；建议在实际任务中使用独立的可写日志路径。SDK 自身会向 stdout 打印日志，`output_path: ""` 时两种输出可能混在一起。

有效帧包含角点、相机/机体坐标、重投影误差和有效性标志。缺姿态时 `world_valid=false`，世界字段为 `null`；没有飞控位置时 `absolute_valid=false`；丢失目标时 `valid=false`，不会复用上一帧坐标。每帧选取置信度最高且通过角点/PnP检查的目标，**尚未做跨帧目标ID跟踪**。

姿态输入协议、时钟映射、SLERP和接入示例见 [docs/pose_protocol.md](docs/pose_protocol.md)。`Camera::read()` 没有公开曝光硬件时间戳，本工程使用读出时间加配置偏移估计曝光时刻，必须实测采集延迟。关闭推理双缓冲只保证检测和图像属于同一帧，不代表已完成相机/飞控硬件同步。

## 文件入口

| 文件 | 作用 |
|---|---|
| `src/main.cpp` | 相机、检测、角点、PnP、姿态与输出主循环 |
| `tasks/target_position/yolo.cpp` | MaixCDK YOLOv8适配、同帧检测 |
| `tasks/target_position/detector.cpp` | 真实轮廓角点/模型语义关键点 |
| `tasks/target_position/solver.cpp` | 轻量平面PnP、相机→机体→NED |
| `io/pose_receiver.cpp` | UDP姿态解析、时间窗口、四元数插值 |
| `tools/config.cpp` | 中文YAML读取与标定/旋转合法性检查 |
| `tools/result_writer.cpp` | JSON Lines输出 |
| `docs/reference_analysis.md` | 参考项目自瞄与坐标变换的详细分析 |

代码结构和格式借鉴参考工程，未引入其 OpenVINO、工业相机、装甲板跟踪、弹道与射击依赖。已完成主机测试、接口检查及定位主程序的真实RISC-V交叉编译和链接；纯MaixCDK C++相机预览也已在当前MaixCAM上运行。**YOLO/PnP完整定位链路仍需使用你的模型、标定参数和飞控数据完成实机验收。**
