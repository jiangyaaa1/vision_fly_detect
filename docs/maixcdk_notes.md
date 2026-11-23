# MaixCDK 接口与 MaixCAM 部署核实记录

核实日期：2026-11-07。硬件对象是 **MaixCAM / MaixCAM Pro 的 RISC-V 平台 `maixcam`**，不是 MaixCAM2。

本记录先阅读用户指定的 [MaixCDK 中文快速开始](https://wiki.sipeed.com/maixcdk/doc/zh/index.html)，再核对 Sipeed 官方 C++ 源码。官方 `main` 在核实时的提交为 [`b7f57665309084082f56a1e79a9d9c114eedd4dc`](https://github.com/sipeed/MaixCDK/commit/b7f57665309084082f56a1e79a9d9c114eedd4dc)，提交时间 `2026-11-04T15:16:20Z`。下文源码链接固定到该提交；安装时应优先与板上固件/MaixPy 的 `maixcdk_version_*.txt` 对齐。

## 1. C++ 相机、YOLOv8、图像转换接口

必须包含的头文件：

```cpp
#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_nn_yolov8.hpp"
#include "maix_image_cv.hpp"
#include <memory>
```

`maix_basic.hpp` 已包含 `maix_sys.hpp`、时间、异常和 app 接口。若显式包含系统接口，应写 `#include "maix_sys.hpp"`，不是 `sys.hpp`。

已核实的签名：

```cpp
maix::nn::YOLOv8(const std::string &model = "", bool dual_buff = true);

maix::nn::Objects *detect(
  maix::image::Image &img, float conf_th = 0.5, float iou_th = 0.45,
  maix::image::Fit fit = maix::image::FIT_CONTAIN,
  float keypoint_th = 0.5, int sort = 0);

maix::camera::Camera(
  int width = -1, int height = -1,
  maix::image::Format format = maix::image::FMT_RGB888,
  const char *device = nullptr, double fps = -1, int buff_num = 3,
  bool open = true, bool raw = false);

maix::image::Image *read(bool block = true, int block_ms = -1);

maix::err::Err maix::image::image2cv(
  maix::image::Image &img, cv::Mat &mat,
  bool ensure_bgr = false, bool copy = true);
```

这些签名分属不同类/命名空间，以上摘列用于查阅，不应作为一段声明代码复制。

实际调用示例：

```cpp
maix::nn::YOLOv8 detector(model_path, false);
maix::camera::Camera camera(width, height, detector.input_format(), nullptr, fps, 1);

std::unique_ptr<maix::image::Image> image(camera.read());
if (!image) return;
std::unique_ptr<maix::nn::Objects> objects(detector.detect(
  *image, 0.5f, 0.45f, maix::image::FIT_CONTAIN, 0.5f));

cv::Mat bgr;
maix::err::check_raise(maix::image::image2cv(*image, bgr, true, true));
for (auto *object : *objects) {
  // object->x/y/w/h: int，框坐标；object->class_id: int；object->score: float。
  // object->points: std::vector<int>，依次为 x0,y0,x1,y1,...。
}
```

定位流程应设置 **`dual_buff=false`**。默认双缓冲允许本次返回前一输入帧的推理结果，无法直接用于当前图像的角点精修与对应姿态变换。

所有权约定：

- `Camera::read()` 返回的新 `Image*` 由调用者释放。
- `YOLOv8::detect()` 返回的新 `Objects*` 由调用者释放；`Objects` 析构会释放内部的 `Object*` 和分割 mask，不能重复删除内部对象。
- 用 `std::unique_ptr` 包装上述返回值，可以在异常和提前退出时释放资源。
- `image2cv(..., true, true)` 创建 BGR 副本，适合 OpenCV 处理；若用 `false, false`，Mat 共享 Image 内存并保留原始 RGB/BGR 格式，Image 必须持续存活，灰度转换也要选对通道顺序。

当前实现的 `_post_process(outputs, img.width(), img.height(), fit, sort)` 会将框和关键点恢复到**传入 detect 的原图坐标**，不需要再次逆 letterbox。相机采集尺寸不必等于模型输入尺寸，但必须与相机内参标定尺寸一致；如有裁剪、缩放、镜像或翻转，须同步处理内参和像素几何。

官方源码：

- [maix_basic.hpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/basic/include/maix_basic.hpp)
- [maix_camera.hpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/vision/include/maix_camera.hpp)
- [maix_image_cv.hpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/vision/include/maix_image_cv.hpp)
- [maix_nn_yolov8.hpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/nn/include/maix_nn_yolov8.hpp)
- [YOLOv8 继承的实际解码实现 maix_nn_yolo11.hpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/nn/include/maix_nn_yolo11.hpp)
- [检测对象及容器 maix_nn_object.hpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/nn/include/maix_nn_object.hpp)

## 2. 四角点 Pose 与普通 Detect 的区别

普通 YOLOv8 Detect 输出轴对齐框，框的四角不等于透视投影后的目标四角，不能作为可靠 PnP 对应点。可以先用 Detect 限定 ROI，再从清晰、已知尺寸的平面矩形轮廓中提取真实角点；更直接的方案是训练 **4 点 YOLOv8-Pose**。

核实版本的 Pose 解码按输出通道数除以 3 推导关键点数量，没有固定为人体 17 点。四点输出通道为 12，常见 shape 为 `[1,12,N]` 或 `[1,N,12]`；每点含 x、y、置信度。MaixCDK 输出的 `points` 只保留整数像素坐标，低置信度点变为负坐标。需要检查点数、负坐标、图像边界、凸性和面积；有效点随后可做亚像素精修。

训练时必须保持固定的**物理点语义顺序**，代码按同一顺序与实际三维角点对应。不要对 Pose 输出每帧重新按图像左上角排序；目标旋转后会改变物理点对应关系。对没有方向标志的普通矩形轮廓，位置可测量，但完整目标姿态存在对称性歧义。

`.mud` 必须含匹配的 `model_type=yolov8`、`type=pose`，输出节点与该 SDK 支持的 Pose 布局一致。当前 mode 2 解码按通道数是否为 3 的倍数识别 Pose 输出；多类别模型的输出布局需要特别验证，单目标类别四点模型更容易核对。

## 3. MaixCAM 默认 OpenCV 没有 calib3d

这是影响能否上板的关键约束。核实版本的 `opencv_conf.cmake` 显式设置：

```cmake
-DBUILD_opencv_calib3d=OFF
```

默认 MaixCAM 动态库依赖也没有 `opencv_calib3d`。所以直接从参考 PC 项目迁移 `cv::solvePnP`、`cv::Rodrigues`、`cv::projectPoints`、`cv::undistortPoints`，会遇到头文件或链接问题。单纯在自己的 CMake 中加一行 `opencv` 不能补出这些函数。

本工程针对已知尺寸的平面矩形，可用 OpenCV core 的 SVD 和线性求解完成单应矩阵分解初值，再自行完成旋转、Brown-Conrady 畸变、正深度检查和重投影优化。这仍是平面 PnP 求解，且不要求重新编译 SDK 的 OpenCV。若以后改为任意非平面目标或要直接使用 IPPE/SQPnP，需要另行交叉编译带 calib3d 的 OpenCV，并完整配置依赖与部署库。

官方依据：

- [opencv_conf.cmake](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/3rd_party/opencv/opencv_conf.cmake)
- [OpenCV 组件链接配置](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/3rd_party/opencv/CMakeLists.txt)

## 4. 官方工程布局与构建

官方例程使用 `main/CMakeLists.txt` 注册主组件，源码位于 `main/src`。`maixcdk` 命令负责工程生成和构建；核实的 `camera_display` 例程没有手写根 `CMakeLists.txt` 或 `project.py`。本工程另外保留的 PC 测试构建入口属于本工程功能，不能替代 MaixCDK 组件配置。

主组件的基本配置示例：

```cmake
list(APPEND ADD_INCLUDE "include")
append_srcs_dir(ADD_SRCS "src")
list(APPEND ADD_REQUIREMENTS basic vision nn opencv)
register_component()
```

若源码放在参考项目风格的 `tasks`、`tools`、`io`、`src` 目录，应把实际文件逐一加入 `ADD_SRCS`，并把项目根加入 `ADD_INCLUDE`；不能指望 `append_srcs_dir(... "src")` 递归收集其他目录。

`app.yaml` 的有效格式示例（按自己的资源路径修改）：

```yaml
id: vision_fly_detect
name: Vision Fly Detect
name[zh]: 无人机目标定位
version: 1.0.0
author: local
desc: YOLOv8 corner detection and planar PnP localization
desc[zh]: 使用相机和无人机姿态测量目标相对位置
files:
  app.yaml: app.yaml
  configs: configs
  models: models
```

`files` 左边是工程内源路径，右边是打包后目标路径；该示例要求 `configs`、`models` 实际存在。模型体积较大时，也可通过单独复制模型并配置绝对模型路径部署。

主程序应参考官方入口：

```cpp
int main(int argc, char *argv[])
{
  maix::sys::register_default_signal_handle();
  CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
```

`_main` 内使用 `while (!maix::app::need_exit())`。异常包装可以保证栈上对象正常析构，避免相机和图像缓冲未释放。

构建流程：

```bash
# 在 Linux x86_64 开发电脑安装 MaixCDK 及其 requirements.txt 中的依赖。
# 将 MAIXCDK_PATH 设置为已安装 SDK 的实际路径。
export MAIXCDK_PATH=/absolute/path/to/MaixCDK
cd /home/jhh/code/vision_fly_detect
maixcdk menuconfig -p maixcam
maixcdk build -p maixcam
maixcdk release -p maixcam
```

以上将 `-p` 放在子命令后，与核实版本 `tools/cmake/project.py` 的实际参数解析一致。首次构建会下载 RISC-V 工具链及依赖，耗时取决于网络。`build2`/`build --no-gen` 只适合没有增删源文件的后续编译。安装包在 `dist` 中；运行时需要可执行文件、依赖库和所需配置/模型，不能只复制二进制。相机被 Launcher 或其他程序占用时，应先通过 MaixVision 正常连接退出 Launcher，再运行本程序。

官方示例与说明：

- [主组件 CMake](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/examples/camera_display/main/CMakeLists.txt)
- [app.yaml](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/examples/camera_display/app.yaml)
- [main.cpp](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/examples/camera_display/main/src/main.cpp)
- [编译与发布流程](https://wiki.sipeed.com/maixcdk/doc/zh/index.html)

## 5. 模型转换与配置要求

电脑训练的 `.pt`、`.onnx` 不能直接用于 MaixCAM NPU。MaixCAM / MaixCAM Pro 需要对应的 **`.mud` + `.cvimodel`**；同一模型包的文件应一起部署，MaixCAM 目标处理器为 `cv181x`。不要误用 MaixCAM2 的 `.axmodel`。

当前普通 Detect 官方网页转换流程支持 YOLOv8；Pose、分割和 OBB 应走对应任务的手动转换流程。核实日期的 Detect 网页教程指定 `ultralytics==8.4.104`、`opset=17`、`dynamic=False`，MaixCAM 常用示例输入为宽 320、高 224；Pose 应以自己的导出节点、shape 和对应教程为准，不能照搬 Detect 的两输出节点。

`.mud` 的输入色彩顺序、mean、scale、类别顺序和任务类型必须与训练/转换一致。相机使用 `detector.input_format()` 获取格式，可以避免模型要求 RGB 而输入 BGR。量化参考图片应覆盖真实视角、距离和光照；量化校准不等于相机内参标定。

官方入口：

- [网页转换 YOLO Detect](https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/online_converter.html)
- [手动转换给 MaixCAM 用](https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html)
- [Pose 原理、模型与转换入口](https://wiki.sipeed.com/maixpy/doc/zh/vision/body_key_points.html)
- [模型获取、上板和运行](https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/ai_model_deploy.html)

## 6. 图像与飞控姿态的时间戳限制

核实的 `Camera::read()` 返回 `Image`，没有通过公开接口返回传感器曝光时刻。官方例程只在读取前后用软件时钟计时。因此在读取前后记录 `maix::time::ticks_us()`，并减去标定的管线延时，得到的是**曝光时刻估计**，不是硬件同步。相机缓冲数量、曝光、帧率和处理负载都会影响延时，需实测；`buff_num=1` 也不代表延时自动为零。

`time::ticks_us()` 返回自启动以来的微秒数，适合间隔与同步计算。`time::time_us()` 是系统时间，可能因 NTP 调整发生跳变。若飞控使用自己的启动时间或 Unix 时间，应先映射到 MaixCAM 的单调时钟域；无法直接比较两个设备的裸时间戳。

世界系相对向量应使用图像曝光估计时刻的 `R_world_body`。必须拒绝过期、未对时或无法插值的姿态。`dual_buff=false` 解决推理帧归属，并不自动完成相机与飞控同步。

官方 [时间 API](https://github.com/sipeed/MaixCDK/blob/b7f57665309084082f56a1e79a9d9c114eedd4dc/components/basic/include/maix_time.hpp)。

## 7. 本次核实的范围

已核实官方 C++ 接口、资源所有权、工程布局、当前模型转换文档和默认 OpenCV 组件范围。官方源码已下载到本次电脑的 `/tmp/maix_research` 用于接口核对；这是临时资料，不是项目的运行依赖。

阅读文档和主机语法检查不能代替 RISC-V 交叉编译、MaixCAM 真机运行、镜头标定及飞控对时。模型准确率、帧率、采集延迟和最终定位误差需要在用户硬件及目标上测量，不能从文档直接推定。
