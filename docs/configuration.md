# 当前真正运行的工程、配置和模型

电脑源工程：`/home/jhh/code/vision_fly_detect`。MaixCDK编译的可执行文件是 `build/vision_fly_detect`，上传到板子的部署副本目录是 `/root/vision_fly_detect_infantry`。目录名不同，但不是另一个工程；本次已比对运行中二进制的SHA256与电脑build文件一致。

正常小窗口及其“启动板端”按钮统一读取电脑的 `configs/maixcam.yaml`。该文件随程序上传到板子，实际命令为：

```text
/root/vision_fly_detect_infantry/vision_fly_detect configs/maixcam.yaml
```

工作目录固定为 `/root/vision_fly_detect_infantry`。因此主配置中：

```yaml
model_path: "models/infantry_640x480_int8_mixed_regression/infantry.mud"
model_type: "szurpv_infantry_v8n"
corner_mode: "keypoints"
keypoint_indices: [0, 3, 2, 1]
```

实际对应板端：

```text
/root/vision_fly_detect_infantry/models/infantry_640x480_int8_mixed_regression/infantry.mud
/root/vision_fly_detect_infantry/models/infantry_640x480_int8_mixed_regression/infantry.cvimodel
```

两者均已检查存在，MUD确实引用这个cvimodel。原先 `/root/models/target.mud` 是通用示例路径，当前板子上不存在，已从主配置修正。历史 `infantry_lite.yaml` 和 `infantry_fast.yaml` 不是正常启动默认配置，不能在主配置填参数后再用旧检测配置判断是否开启PnP。

## 用户参数的当前状态

- 已保留用户填写的内参、五参数畸变、相机到机体外参和平移 `[0,0,-0.1]`，未用检测模板的零内参覆盖。
- `localization_enabled/calibration_valid/extrinsics_valid` 均为1，已经启用YOLO→模型四角点→PnP→机体坐标。
- 用户最新设置为 `pose_mode: static`，固定四元数 `[1,0,0,0]`。这是固定姿态台架测试，并非实时飞控姿态。PnP成功时状态为 `ok_static_pose`，世界方向按这一个常量姿态计算；要在无人机运动时得到真实NED方向，仍需接入实时姿态。
- 当前已按用户新要求改为 `class_id: -1`，检测全部支持类别；0单独表示哨兵，3表示3号步兵，4表示4号步兵。全类别使用逐类尺寸数组做PnP；只有有效目标才输出坐标。
- 保留用户的 `camera_fps: 30` 和640×480图像尺寸；标定必须匹配这个实际采集模式。没有为追求旧性能数据而更换用户的标定/采集设置。
- 预览仍为 `debug_fps: 0`，不限速。

本次使用这些参数构造合成投影点，PnP恢复位置误差约2.93e-7米，固定姿态变换通过；这是数学自洽检查，不是实物测距精度验收。板端已真实加载模型和该主配置。验收时镜头中没有指定目标，状态为 `no_target`，不能把空目标帧当作有效定位。

## 修改后生效

编辑电脑 `configs/maixcam.yaml`，然后点击窗口的“启动板端”，或执行：

```bash
/home/jhh/code/vision_fly_detect/tools/show_camera.sh
```

仅保存YAML不会修改已运行进程的内存参数。查看实际进程、配置快照及对应的最新输出：

```bash
python3 /home/jhh/code/vision_fly_detect/tools/infantry_lite.py status
```

输出包括 `ACTIVE_ARGV`、`ACTIVE_SETTINGS`。主配置当前结果写入板端 `target_positions.jsonl`，状态工具已按实际输出路径读取，不会再误读旧检测配置的 `infantry_positions.jsonl`。


## 2026-11-10性能优化参数

主配置的pnp_min_confidence现已实际接入Solver：confidence=0.40用于检测，pnp_min_confidence=0.60用于定位。低于定位门槛时状态为pnp_confidence_too_low，只显示预测，不发布有效坐标。默认值0保持旧配置行为。异步预览现同时执行绘制、编码与推流，仍为debug_fps=0不限速。性能口径见[optimization.md](optimization.md)。
