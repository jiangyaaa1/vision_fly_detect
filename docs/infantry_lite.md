# 深大 Infantry-v8n 在 MaixCAM Lite 独立运行

最新：默认模型已升级为640×480 INT8主体＋BF16完整角点回归，真实相机处理约19FPS，目前按用户要求取消预览限速（此前约19FPS是在预览限10时测得）；下文原BF16实测保留作历史对照。完整基准口径及回退步骤见[优化说明](optimization.md)。

本适配器只对应用户提供的 `Infantry-v8n-fp16-20260726-D1.8w-B16.onnx`，不是标准Ultralytics Pose。源模型SHA256见 `models/infantry/manifest.json`。ONNX原文件保留在桌面，不修改。

## 使用

在电脑终端执行：

```bash
cd /home/jhh/code/vision_fly_detect
python3 tools/infantry_lite.py start
```

该命令上传整个发布目录、停止本工程原始相机预览，然后启动板端C++程序。打开 `http://10.154.34.1:8000/` 查看检测框和四角点。当前默认只发送640×480叠加图；`debug_layout: sheet`可恢复三联图。关键点模式不运行轮廓提取。

```bash
python3 tools/infantry_lite.py status
python3 tools/infantry_lite.py stop
```

停止后可用 `./tools/preview_camera.sh start` 恢复原始相机预览。运行模型与预览不能同时占用相机。

部署目录：`/root/vision_fly_detect_infantry`。这是Lite本身的C++程序在推理，电脑只负责上传、启动、查看画面；上传启动后关闭终端不停止推理。断开USB前须保证板子有独立供电，USB断开会失去当前网络预览。现已增加独立供电开机自启动配置，见[自启动说明](autostart.md)。

## 选择目标与定位

编辑电脑上的 `configs/maixcam.yaml`，修改后重新打包与启动：

```bash
./tools/build_maixcam.sh
python3 tools/infantry_lite.py start
```

- `class_id`：固定选择一种已知角点实际宽高的装甲板。当前默认-1为全部；0为哨兵，3为3号步兵；0哨兵、1英雄、2工程、4步兵4、6前哨站、7基地底部、8基地顶部；本模型不再识别5号步兵。
- `armor_color`：-1同时显示蓝/红/白，0只蓝、1只红、2只白。表示要识别的颜色，不是己方颜色。紫色始终过滤。此实现不包含原仓库的白色装甲历史颜色判定。
- `confidence`：类别最大概率阈值，默认0.60。颜色采用最大分支选择，不与类别概率相乘。
- `model_type`：保持 `szurpv_infantry_v8n`；不能当标准YOLOv8模型加载。
- `image_width/image_height`：固定640/480，避免额外缩放或padding改变角点坐标。
- `corner_mode: "keypoints"`、`keypoint_indices: [0,3,2,1]`：原模型顺序TL/BL/BR/TR，转换为本工程PnP的TL/TR/BR/BL；不能按照每帧图像方向重新排序。

初始 `localization_enabled: 0`，仅检测，输出JSON的三维有效标志为false。这不是完整距离定位验收。要开启PnP，必须填写该镜头640×480的真实内参、畸变、相机到机体外参，以及与模型标注角点对应的实际尺寸；核实后将 `calibration_valid/extrinsics_valid/localization_enabled` 设为1。大装甲板和小装甲板不能共用未经核实的尺寸。目前一次只处理 `class_id` 指定的一类；模型原仓库把1、7视为大装甲，其余视为小装甲，但仍应实际核对角点物理间距。

相机坐标和机体相对坐标可在标定后得到；世界NED坐标还需要真实机体姿态。`pose_mode: none` 时世界坐标保持无效。默认主配置的输出位置日志为板端工作目录的 `target_positions.jsonl`，不会控制无人机或发射机构。

## 实现

1. `tools/prepare_infantry.py` 修复源ONNX中末尾输入Cast导致的非拓扑顺序，把FP16权重等值展开为FP32，检查固定输入输出与解码常量。
2. 裁剪三输出：`/model.22/Concat_output_0`=[1,8,6300]原始角点，两个Sigmoid分别为[1,4,6300]颜色和[1,9,6300]类别。不同于标准YOLOv8 Detect，不使用DFL边框解码。
3. `tools/convert_infantry.py` 通过TPU-MLIR编译成cv181x BF16。未以空白图或无关图片伪造INT8校准；后续INT8优化需要实际装甲板校准图片及误差评估。
4. cv181x实际输出会补为[1,C,6300,1]，适配器已兼容。板端 `yolo.cpp` 用 `maix::nn::NN` 同步执行、关闭双缓冲，RGB图像按1/255归一化，输出由SDK反量化为float32。
5. `infantry.cpp` 在CPU按stride8/16/32还原角点：`pixel=(raw*2+grid_index)*stride`。该公式已逐项核对本次ONNX实际常量。筛选类别、颜色、非有限/越界/退化四边形，再通过外接框IoU去重，进入已有关键点校验和PnP。

转换副本、参考输出及转换日志位于 `.local/infantry-conversion`。模型发布目录只保留运行所需模型和说明。工具依赖隔离在 `.local/infantry-python` 与 `.local/tpu-python`，未替换系统Python。工具版本：TPU-MLIR1.30.2、ONNX1.17.0、ONNX Runtime1.23.2、PyTorch2.5.1+cpu。电脑Python3.10。

重新转换命令（隔离依赖已准备好时）：

```bash
python3 -s tools/convert_infantry.py "/home/jhh/桌面/Infantry-v8n-fp16-20260726-D1.8w-B16.onnx" \
  --test-image .local/infantry-conversion/armor_test.jpg
```

`--test-image`应是真实装甲板场景，仅用于本次转换对比，不代表完整精度评测。转换脚本任一验证失败会退出，不发布新的MUD。

## 检查与复现

```bash
cmake -S tests -B build-host
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

新增infantry测试覆盖三个stride及边界、类别分数、颜色筛选、非法坐标、NMS和角点顺序。原有四组测试仍保留。

板端单张图片测试（图片必须为640×480，在推理停止后执行）：

```bash
cd /root/vision_fly_detect_infantry
LD_LIBRARY_PATH=./dl_lib ./vision_fly_detect configs/maixcam.yaml --test-image /root/armor_test.jpg
```

该模式执行真实板端模型，不打开相机和HTTP服务；控制台输出`MODEL_TEST`、目标分数与四角点。当前配置仅选择3号步兵，未出现该类目标不代表模型加载失败。

参考：[深大仓库](https://github.com/SZURPVision/26_NNDeployment_Lib_and_Detection_Models)、[Sipeed手动转换](https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html)。

## 当前实测结果

2026-11-09，已在用户连接的MaixCAM Lite上完成模型加载、真实相机推理与HTTP首帧测试。仓库测试视频2秒位置的640×480图片识别到1块3号步兵蓝色装甲板，置信度0.960938，四个角点全部通过校验；模型推理及解析约126ms。角点相对同图FP32结果最大差异约0.123像素。这是单图一致性检查，不代表完整数据集精度，也不是相机实时链路的帧率承诺。完整定位验收需要真实标定。

持续运行检查：最近连续约4.77秒共18帧，整条相机/推理/网页调试流程约3.56帧/秒。当前实拍画面偏暗且存在明显噪点，没有识别出目标；测试图识别已单独通过。请检查镜头遮挡、照明和对焦，真实场景精度尚未验收。

电脑原生小窗口已提供：`./tools/show_camera.sh`。2026-11-09去除空面板及大图复制后，完整流程实测约5.2～5.4FPS；详见[小窗口与性能说明](camera_window.md)。

当前用户已填写主配置的内参、畸变及外参，calibration_valid/extrinsics_valid/localization_enabled均为1。pose_mode=static及四元数[1,0,0,0]为用户保留的固定姿态测试设置，不是实时飞控姿态；检测到指定类别并通过PnP后，状态标记为ok_static_pose。真正动态NED相对坐标需要后续实时姿态输入。初始检测模式与旧性能测试说明仅作为历史记录。

全类别更新：class_id=-1启用全部支持类别，使用逐类宽高数组选择PnP尺寸；窗口直接显示所选绿色目标的Cam/Body/NED坐标，详见view_corners_and_coordinates.md。历史单类别说明适用于class_id>=0。
