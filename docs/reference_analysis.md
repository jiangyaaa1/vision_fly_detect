# sp_vision_25-main 自瞄与坐标变换分析

参考目录：`/home/jhh/code/sp_vision_25-main`。下述路径/行号对应本次读取的本地源码。新实现放在 `/home/jhh/code/vision_fly_detect`，没有修改参考工程。

## 1. 自瞄的处理链

主要入口是 `src/standard.cpp:41–79`：相机读图及时间戳 → 从下位机姿态队列取图像时刻的四元数 → 更新Solver旋转 → YOLO检测 → Tracker跟踪 → Aimer预测与弹道 → 向下位机发送控制量。部分入口另经 Shooter 判断开火条件。

| 模块 | 关键源码 | 具体职责 |
|---|---|---|
| 相机/姿态 | `src/standard.cpp:63`，`io/cboard.cpp:22–44` | 获取图像时间戳，寻找其前后姿态并SLERP插值 |
| YOLO选择 | `tasks/auto_aim/yolo.cpp:11–30` | 配置选择YOLOv5/v8/v11 |
| YOLOv8 | `tasks/auto_aim/yolos/yolov8.cpp:38–160` | OpenVINO推理、置信度、NMS和定制角点输出 |
| 传统Detector | `tasks/auto_aim/detector.cpp:33–119` | 灰度阈值、轮廓、灯条筛选和配对 |
| Armor | `tasks/auto_aim/armor.cpp:9–43` | 左右灯条端点构造四个物理角点 |
| Solver | `tasks/auto_aim/solver.cpp:54–90` | PnP、相机/云台/世界旋转、姿态和方位距离 |
| Tracker/Target | `tasks/auto_aim/tracker.cpp:231–289` 等 | 调用Solver，匹配装甲板并维护运动状态/EKF |
| Aimer/Shooter | `tasks/auto_aim/aimer.cpp`、`shooter.cpp` | 预测目标运动、弹道求角和射击判定 |

`src/uav.cpp:47` 当前实际使用传统 `Detector`，YOLO构造被注释，因此不能只根据文件名“uav”判断其使用了哪条检测路径。清晰的YOLO入口可看 `standard.cpp`。

本次需求是相机观测到目标三维位移，因此新项目实现到Solver/坐标输出，不迁移后半段瞄准、弹道和射击功能。

## 2. 它如何获得四角

参考 `YOLOV8` 的 `class_num_` 为2，输出按 `xywh + 2个类别分数 + 4×2个角点坐标` 解码，即14个值；见 `yolos/yolov8.hpp:36` 和 `.cpp:116–143`。这不是普通只返回bbox的YOLOv8 Detect模型。输入被缩放到416×416，角点再按缩放比例恢复，启用ROI时还需加回ROI偏移。

`yolov8.cpp:296–321` 将点排为左上TL、右上TR、右下BR、左下BL，之后可结合传统图像特征修正。传统检测路径则先找两根灯条，再用灯条端点形成同样顺序的四个点。

这两条路径都提供物体的**实际特征点**，而不是把轴对齐检测框当成物体边界。目标倾斜后，物理矩形投影通常是梯形，bbox四角一般不在物体四角上，用它做PnP会直接引入距离偏差。

新项目相应提供两种路径：

- `contour`：普通MaixCDK YOLOv8定位ROI，在ROI中找真实凸四边形，恢复原图偏移，检查面积、bbox一致性、边长、裁边、近正立方向，并做受限亚像素精化。此方案要求边缘清晰的平面矩形，不适合任意物体或严重遮挡；背景边框也可能被误选，必须看屏幕角点实测确认。
- `keypoints`：使用正确转换的四点YOLOv8-Pose，按 `keypoint_indices` 保留训练定义的物理角点顺序，拒绝缺点及低置信度负坐标。旋转目标不能按每帧的图像左上角重新排序。

无方向标志的轮廓不能唯一确定目标自身朝向；宽高对应关系也需要目标物理顶部与图像方向的假设。contour只支持大致正立的目标，旋转使用语义关键点或有方向的标靶。重投影误差小也不能证明角点对应了正确物理边界。

## 3. PnP求出的是什么

参考 `solver.cpp:12–25` 把装甲板四点定义在目标自身 `X=0` 平面，单位米：

```text
TL=(0,+w/2,+h/2), TR=(0,-w/2,+h/2)
BR=(0,-w/2,-h/2), BL=(0,+w/2,-h/2)
小装甲w=0.135，大装甲w=0.230，灯条长度h=0.056。
```

`solver.cpp:61–68` 用 `cv::solvePnP(..., SOLVEPNP_IPPE)`。相机内参和五项畸变系数来自YAML。按 [OpenCV PnP定义](https://docs.opencv.org/4.x/d5/d1f/calib3d_solvePnP.html)，解是目标系到相机系的变换：

```text
P_camera = R_target2camera * P_target + t_target2camera
```

目标局部原点放在几何中心，所以该原点的相机坐标就是 `tvec`。这既不是无人机绝对世界位置，也不是相机在目标系的位置；若想求相机在目标系的坐标，需要逆变换 `-Rᵀ*tvec`。

只给像素坐标和相机内参无法决定真实尺度。必须有已知的物理角点尺寸，或其他距离/尺度观测。参考代码用装甲板固定尺寸；新项目改为YAML里填写真实矩形宽高。

## 4. 相机→云台→世界

参考源码直接对应的公式为：

```text
p_camera = tvec
p_gimbal = R_camera2gimbal * p_camera + t_camera2gimbal
p_world  = R_gimbal2world * p_gimbal
```

其中外参平移 `t_camera2gimbal` 是相机光心在云台系的位置，而不是“相机看见云台原点”的坐标；平移和旋转方向必须配套。

OpenCV光学系：x右、y下、z前。参考项目理想云台系：x前、y左、z上，可以从 `calibration/calibrate_robotworld_handeye.cpp:187` 的理想轴变换确认。理想水平前视旋转为：

```text
R_camera2gimbal = [ 0  0  1
                  -1  0  0
                   0 -1  0 ]
```

实际 `configs/uav.yaml:87–97` 还包含安装偏角和杆臂，不能把这些工业相机标定数值复制给MaixCAM。

姿态设置在 `solver.cpp:48–52`：令 `S=R_gimbal2imubody`，四元数转得 `R_imubody2imuabs` 后，使用：

```text
R_gimbal2world = Sᵀ * R_imubody2imuabs * S
```

这是把IMU旋转通过安装轴变换表达在参考工程选定的世界基底中。不能把任意飞控FRD→NED四元数未经定义就代入这个共轭公式。新项目直接约定飞控提供FRD→NED主动旋转，因此使用 `R_body2world`，不再复制含义不同的IMU共轭变换。

参考Solver还将目标旋转 `R_armor2camera` 左乘同样的外参，得到目标在云台/世界方向的旋转，再取欧拉角、yaw/pitch/distance。其后 `optimize_yaw` 假设装甲板固定±15°倾角，新项目的普通标靶不具备该先验，故不迁移这一优化。

## 5. 参考“世界”与无人机导航世界的差别

参考等式没有加机器人在导航系的平移。因此它的 `xyz_in_world` 是**以当前云台中心为原点，按世界轴方向表达的相对位置**。机器人移动时，这个原点会跟着移动，并不是固定地图坐标。

无人机需求应拆成：

```text
relative_target_NED = R_body2world * (R_camera2body*tvec + t_camera2body)
absolute_target_NED = drone_position_NED + relative_target_NED
```

第一行满足“无人机到目标的相对世界坐标”；它需要姿态但不需要无人机绝对位置。第二行才是导航世界的绝对目标坐标，需要飞控位置，而且该位置必须与姿态采用同一世界系/原点。单凭一台相机观测无定位标靶不能自动获得地理绝对坐标。

本项目机体FRD为前右下、世界NED为北东下。水平前视理想安装矩阵是 `[0,0,1;1,0,0;0,1,0]`，与参考项目的前左上矩阵不同。相机朝下时必须重新填写安装外参。未收到有效姿态时输出世界字段 `null`，不使用单位旋转伪装为世界坐标。

## 6. MaixCAM实现的调整

| 参考项目 | 本项目 |
|---|---|
| OpenVINO及工业相机 | MaixCDK `nn::YOLOv8`、`camera::Camera` |
| 装甲灯条/定制四点输出 | 真实矩形轮廓或MaixCDK四点Pose |
| YAML-CPP、Eigen | OpenCV FileStorage、Matx；减少板端依赖 |
| `cv::solvePnP(IPPE)` | Brown5去畸变→单应性初值→SO(3)修正→多初值LM平面PnP |
| 云台相对世界向量 | 明确区分FRD机体、NED相对向量和NED绝对目标位置 |
| CBoard姿态时间插值 | 带本板单调时间戳的UDP姿态队列、SLERP、缺样本拒绝 |

MaixCAM默认SDK关闭OpenCV calib3d，详见 [官方接口核实记录](maixcdk_notes.md)，所以不能直接复制PC端solvePnP调用。本实现的轻量平面PnP仅用core，已通过电脑端OpenCV投影及IPPE对照。它不等价于IPPE的解析双解求解，也不支持任意非平面目标。平面姿态弱可观测性和对称歧义仍然存在。

参考工程相机已有时间戳且 `CBoard::imu_at()` 插值匹配图像；MaixCDK公开相机接口没有曝光时间戳，本工程只能以读出时间加实测补偿估计。相机延迟、飞控对时与姿态频率将直接影响运动时误差，不能省略。见 [姿态接入协议](pose_protocol.md)。

结构延续 `tasks/.../detector/solver/target`、`io`、`tools`、`src`、`configs`；格式沿用参考Google基础、2空格、100列、头文件guard及命名习惯。没有改动参考目录，许可说明见根目录 `THIRD_PARTY_NOTICES.md`。
