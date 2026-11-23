# 飞控姿态、时钟与输出协议

## 数据方向

接收程序是 `io/pose_receiver.cpp`。在 `configs/maixcam.yaml` 设置 `pose_mode: "udp"`，默认监听本板 `127.0.0.1:5005`。飞控的UART/MAVLink/CAN数据由你的桥接程序解析后，向这个UDP端口发送ASCII数据报；本工程没有假定你使用的飞控型号和串口线序。

每个数据报一条完整消息，两种合法形式：

```text
POSE timestamp_ns qw qx qy qz
POSE timestamp_ns qw qx qy qz north east down
```

- `timestamp_ns`：姿态测量时刻，必须映射为 **MaixCAM本板CLOCK_MONOTONIC的纳秒**，不是Unix时间，也不是未经映射的飞控启动时间。
- `qw qx qy qz`：FRD机体坐标到NED世界方向的主动旋转四元数，w在前；接收端归一化，明显异常长度、零向量、NaN和格式错误会拒绝。
- 可选 `north east down`：同一时刻的无人机机体原点在同一NED导航世界中的位置，单位米。三个分量必须同时提供。经纬度不能直接填进来。

相机外参定义的机体原点必须与这里的位置原点一致。若外参相对IMU、位置相对GPS天线，应先处理两者杆臂。

## 对时与插值

1. 桥接端需要建立飞控时间到本板单调时钟的映射，处理时钟偏移和漂移。可从飞控时间同步协议获得；仅在串口收到消息时打本板时间戳会包含传输/排队延迟，只适合明确评估过延迟的调试。
2. `camera.read()` 返回后立即记录本板单调时钟；加上 `image_time_offset_ms` 得到曝光时刻估计。例：读出晚于曝光35ms，偏移填写 `-35`。相机API没有公开真实曝光硬件时间戳，这不是硬件同步。
3. 主循环在采集前、采集后和推理后排空UDP队列，保留时间有序样本。拒绝乱序、重复及明显不在本板时钟范围内的样本，最多缓存512条/3秒。
4. 用图像估计时刻的前后两个姿态SLERP；处理q与-q等价及最短弧。两侧样本总间隔超过 `max_pose_gap_ms`，或缺少一侧，世界坐标无效。**不外推，不把最新姿态直接套到旧图像上。**
5. 两侧均有位置时才插值绝对导航位置；仅有姿态时仍可输出NED相对位移。

桥接频率应足以覆盖相机帧，且推理结束前已有曝光时刻之后的样本；例如相机30fps时可先用50–100Hz姿态做台架验证，再按实际延迟、运动速度与误差要求调整。接口不会等待未来样本，缺样本帧会明确标记无效。

## 最小台架发送示例

以下在 **MaixCAM上** 用Python标准库运行，无需额外安装包。它只模拟“水平、机头向北”的固定姿态，目的是检查接口和时钟，不能作为飞行中的真实姿态。

```python
import socket
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    while True:
        # 实际桥接应替换为真实测量值和对时后的测量时间戳。
        packet = f"POSE {time.monotonic_ns()} 1 0 0 0"
        sock.sendto(packet.encode("ascii"), ("127.0.0.1", 5005))
        time.sleep(0.01)
finally:
    sock.close()
```

若从另一台电脑发送，不能直接使用那台电脑的 `time.monotonic_ns()`；必须先映射时钟，并按需将监听地址改成 `0.0.0.0`。本接口不直接发送无人机飞行控制命令。

ENU数据转换为NED时，对向量用 `N=Y_ENU, E=X_ENU, D=-Z_ENU`；姿态还必须考虑机体定义是否为FLU/FRD，不能只交换四元数两个分量。建议桥接端先形成旋转矩阵，明确左右基底变换后再转四元数。

## 输出

默认写 `target_positions.jsonl`，每帧一行JSON。`timestamp_ns` 是曝光估计时刻，`completed_ns` 是本帧处理完成时刻。长度米，坐标轴见README。

| 字段 | 含义 |
|---|---|
| `valid` | 当前帧选中目标的角点和PnP均有效 |
| `world_valid` | 当前帧还具有匹配的FRD→NED姿态 |
| `absolute_valid` | 当前帧还具有匹配的导航位置 |
| `status` | `ok`、`ok_static_pose`（固定姿态测试）、`no_target`、角点/PnP失败原因、`camera_body_only:pose_*` |
| `corners_px` | TL/TR/BR/BL对应的四个原图像素坐标 |
| `xyz_in_camera` | 目标中心在相机右下前坐标中的位置 |
| `xyz_in_body` | 目标中心在无人机前右下坐标中的位置，已含相机杆臂 |
| `relative_xyz_in_world` | 无人机原点指向目标中心、按NED表达的位移；无姿态为null |
| `xyz_in_world` | 导航世界下的绝对目标位置；无姿态/无人机位置为null |
| `reprojection_error_px` | 四个角点欧氏重投影误差的RMS，单位像素 |

只使用 `valid=true` 的当前帧几何结果；世界方向控制还需 `world_valid=true`，绝对导航还需 `absolute_valid=true`。丢失目标时不会保留上一帧坐标。单帧按置信度排序，依次尝试候选，选第一个角点/PnP有效的目标；多目标之间可能切换，尚无跨帧ID跟踪。

低重投影误差不是精度保证：错误目标尺寸、错误物理角点、外参偏差和时钟误差仍可产生错误世界坐标。请先在静止台架中以已知距离和方向检查，再测量运动中的误差。
