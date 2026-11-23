# 验证记录

日期：2026-11-07。环境：Linux x86_64、GCC 11.4.0、CMake 3.22.1、OpenCV 4.5.4。

## 可重复的主机验证

```bash
cd /home/jhh/code/vision_fly_detect
cmake -S tests -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

四个测试均通过，核心代码使用 `-Wall -Wextra -Wpedantic -Werror` 编译。

| 测试 | 内容 |
|---|---|
| solver | OpenCV生成带Brown5畸变的投影，与已知平移及OpenCV IPPE比较；坏点、无标定、距离范围、NED旋转、杆臂和绝对位置 |
| detector | 透视矩形及反相/内部纹理、ROI偏移、空图/圆/裁边拒绝、旋转语义关键点、NaN/越界/自交/缺点 |
| io | ASCII姿态解析、SLERP与q/-q、位置插值、乱序/重复/缺双侧/大时间间隔拒绝、中文YAML、配置检查和输出有效性 |
| pipeline | 合成真实矩形图像→轮廓角点→PnP→机体FRD→NED方向相对位移 |

PnP固定25组案例（5个姿态×0.9/2/5/10/20m）中，最大位置误差约 `1.48e-5 m`，与OpenCV IPPE最大差约 `3.57e-6 m`；3m、约0.2像素确定性扰动案例位置误差约 `0.00176 m`。这些只反映合成数据与数学实现，不代表实机误差或性能。测试在Release模式仍执行检查，没有用会被NDEBUG禁用的assert。

核心库仅链接OpenCV core/imgproc，calib3d仅在测试参考计算中使用。离线照片工具额外使用主机imgcodecs。

后续已加入YOLO/OpenCV三联网页调试：叠加图、实际ROI边缘和二值化结果，以及不输出三维坐标的仅检测模式。四组主机测试再次通过，验证诊断开关不会改变角点、绘图不修改计算输入、调试配置可在未标定时加载。更新后的主程序也已通过MaixCAM RISC-V交叉编译、链接和发布；本轮未替换正在运行的原始预览，尚需用户目标模型完成YOLO调试实机验收。见 [调试说明](debugging.md)。

## 真实MaixCDK头文件检查

核对官方提交 `b7f57665309084082f56a1e79a9d9c114eedd4dc`，执行：

```bash
g++ -std=c++17 -fsyntax-only -DPLATFORM_MAIXCAM=1 \
  -I. -I/usr/include/opencv4 \
  -I/tmp/maix_research/generated \
  -I/tmp/maix_research/MaixCDK-b7f57665309084082f56a1e79a9d9c114eedd4dc/components/basic/include \
  -I/tmp/maix_research/MaixCDK-b7f57665309084082f56a1e79a9d9c114eedd4dc/components/vision/include \
  -I/tmp/maix_research/MaixCDK-b7f57665309084082f56a1e79a9d9c114eedd4dc/components/vision/include/base \
  -I/tmp/maix_research/MaixCDK-b7f57665309084082f56a1e79a9d9c114eedd4dc/components/nn/include \
  src/main.cpp tasks/target_position/yolo.cpp
```

检查退出码0、无诊断。仅构建时生成的 `global_config.h/global_build_info_version.h` 使用各一行 `#pragma once` 的空配置文件；相机、显示、YOLO和图像转换等API全部使用真实官方头文件，没有模拟API。临时SDK源码不是本工程依赖，正式编译用实际安装的MaixCDK。

## 尚未验证的部分

2026-11-07验证时，电脑没有已安装的MaixCDK交叉工具链，也没有提供已连接的MaixCAM、目标模型、实测标定和飞控数据。该轮未验证RISC-V交叉链接、真实相机/NPU推理、UDP板端传输、飞控时钟映射、运动时同步误差与实机帧率。姿态测试通过直接注入时间序列完成，验证解析和插值算法，不代表硬件链路已联调。

2026-11-08已连接MaixCAM并启动真实相机屏幕/网页预览。本机MaixCDK视觉依赖已安装缓存，定位主项目和独立C++预览例程均已通过RISC-V MUSL交叉编译与链接；C++预览已上传当前MaixCAM运行并通过真实JPEG首帧检查，见 [相机预览记录](camera_preview.md)。相机预览成功不等于YOLO/PnP主定位程序已完成实机标定和验证。

上板验收建议按顺序进行：填写真实配置 → 静止已知距离检查四角覆盖及深度 → 检查安装轴与杆臂 → 固定姿态检查NED符号 → 接入实时姿态并校准曝光延迟 → 在工作距离和运动范围内统计定位误差。普通轮廓在复杂背景中的误匹配不能仅靠重投影阈值排除。

设备随后确认为无屏MaixCAM Lite：连接配置默认关闭LCD，预览传入--no-display，主程序YAML默认display=0。电脑端原始JPEG拍照与2秒MP4录像已实机验证并回读成功，5项采集单元测试通过。详见 [Lite无屏采集](lite_capture.md)。

## 深大 Infantry-v8n / Lite 实机验证（2026-11-09）

- 用户ONNX固定1×3×480×640、1×21×6300；修复拓扑顺序、FP16展开FP32，裁剪原始角点+颜色+类别三输出。两组CPU输入重建完整输出最大绝对误差3.052e-5。
- TPU-MLIR1.30.2生成cv181x BF16模型，转换参考对比及模型模拟器对比通过；模型7.3MiB，SharedMem 4,915,200字节。
- MaixCDK RISC-V MUSL真实交叉编译和链接通过。SDK将输出补成[1,C,6300,1]，已按实机形状适配。
- Lite单张装甲板图：MODEL_TEST boxes=1 corners=1；class3、蓝色、score0.960938；推理及解析126.023ms。四角点相较同图FP32输出的最大位移约0.123px，仅代表这一张测试图。
- 真实GC4653相机+板端模型+HTTP首帧已验证，无LCD。完整PNP距离、真实场景识别精度、运动目标时延和世界坐标仍需实际标定与飞控姿态验收。
- 5组主机测试全部通过，新增infantry覆盖三个stride边界、分数、颜色/类别筛选、NMS、非法坐标及角点语义映射。
- 启动/停止与板端测试用法见[深大模型说明](infantry_lite.md)。

持续运行检查：最近连续约4.77秒共18帧，整条相机/推理/网页调试流程约3.56帧/秒。当前实拍画面偏暗且存在明显噪点，没有识别出目标；测试图识别已单独通过。请检查镜头遮挡、照明和对焦，真实场景精度尚未验收。

## 原生小窗口与显示优化（2026-11-09）

系统PyQt5窗口已在真实桌面启动。GUI实测JPEG接收、暂停期间持续接收及恢复时显示最新帧均通过；不使用浏览器，不保留逐帧队列。板端默认深大配置改为640×480 overlay、JPEG质量60，减少两栏空图与复制；保持模型输入与同步推理不变。五组主机测试通过，RISC-V编译与上板运行通过。多组30帧PERF记录5.18～5.43FPS，infer约130～132ms；不代表30FPS能力。

## 640×480混合精度与完整计算吞吐（2026-11-09）

最终模型采用INT8主体＋完整BF16角点回归。300帧固定真实装甲板图片，每帧执行YOLO/角点/PnP/世界变换，含异步调试编码，22.2889FPS，300/300成功，P95 47.14ms；使用模拟内参/姿态，不含相机采集，不发布导航结果。真实相机无目标状态常见19.4FPS，短时波动见优化说明；不能用它证明有效世界坐标输出频率。独立远距离视频20帧与原FP32模型匹配20/20，角点差异平均0.120px/P95 0.230px/最大0.342px，非人工真值精度。5组主机测试和真实RISC-V编译通过，新增RGB叠加颜色等价校验。模型、速度、数据来源和SAHI参考评估详见[优化说明](optimization.md)。

## 取消预览限速（2026-11-09）

用户要求预览不固定10FPS。新增debug_fps=0语义，主循环和基准均使用短路判断避免除零；默认高速配置设置为0，每个处理帧提交显示。GUI轮询缩短为10ms，仅解码新帧。5组主机测试、Python语法检查和RISC-V交叉编译通过。首次部署时USB网络断开；用户重连后已部署并验证debug_fps=0，窗口5秒收到85帧、实测约17.52FPS，板端同期处理约17～18FPS。不限速模式含每帧编码开销，不能沿用先前限10FPS时的19FPS作为结果。详见performance_live_uncapped.json。

## 主配置入口及用户标定接入（2026-11-09）

发现用户填写maixcam.yaml而启动器仍读取旧检测配置，已统一默认入口至maixcam.yaml，修正真实模型路径和深大关键点模式，保留用户K/D/R/t、class_id0、camera_fps30和最新static四元数。主机用这些参数的合成投影自洽检查误差2.93e-7米；5组测试通过。板端启动打印ACTIVE_CONFIG=.../configs/maixcam.yaml、PNP_ENABLED=1、POSE_MODE=static，真实模型文件存在；进程二进制与本工程build的SHA256一致，配置逐字节一致。最新真实相机状态为no_target，没有声称已经产生有效实物坐标。status已修复为跟随active_run.json读取实际target_positions.jsonl。

## 全类别与坐标叠加

新增class_id=-1仅用于深大模型，保留单类别兼容路径；测试同画面不同类别检测、单类过滤、9类独立物理尺寸PnP、未知尺寸拒绝、全类别不能绕过相机标定校验。5组测试通过，RGB/BGR坐标叠加颜色等价测试通过，合成坐标显示截图已视觉检查。默认主配置开启全类并沿用用户标定/外参/static设置。坐标仍为单个选中有效目标；没有声称全部目标同时输出坐标。首次部署时USB网络未连接；用户接回后已成功更新，实际进程读取configs/maixcam.yaml、class_id=-1、localization_enabled=1、pose_mode=static，真实窗口接收通过。验收镜头内暂无装甲板，当前状态no_target，未宣称已得到实际目标坐标。

## 装甲板曝光（2026-11-09）

增加MaixCDK真实自动/手动曝光与gain配置，默认主配置手动4000us、gain1024。相机读回3999us、gain1024、Manual1，设置已生效。固定目标画面观察到C0装甲板置信度约0.93、绿色四角框与有效PnP/static坐标；不将不同取景当作受控精度对比。5组主机测试和RISC-V编译通过。参数与恢复自动的方法见exposure.md。
