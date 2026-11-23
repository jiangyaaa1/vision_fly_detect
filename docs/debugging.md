# 查看YOLO检测和OpenCV特征线

`tools/preview_camera.sh` 启动的是原始相机预览，不运行YOLO或轮廓提取。查看算法结果需要运行本项目主程序 `vision_fly_detect`，它在计算完成后把调试标记画到副本，再通过同一USB网络推送到浏览器。

## 先只调检测，不要求PnP标定

修改 [configs/debug_detection.yaml](../configs/debug_detection.yaml) 的 `model_path`、`class_id`，并将对应 `.mud` 和 `.cvimodel` 上传到板上。该配置关闭定位，只显示检测与角点，JSON的 `valid/world_valid/absolute_valid` 为false，不产生有效三维位置。不要为了看框就把占位标定值设为有效。

在电脑执行：

```bash
/home/jhh/code/vision_fly_detect/tools/build_maixcam.sh
/home/jhh/code/vision_fly_detect/tools/preview_camera.sh stop
scp -r /home/jhh/code/vision_fly_detect/dist/vision_fly_detect_release root@10.154.34.1:/root/
ssh root@10.154.34.1
```

登录板端后执行：

```bash
killall launcher_daemon
cd /root/vision_fly_detect_release
LD_LIBRARY_PATH=./dl_lib ./vision_fly_detect configs/debug_detection.yaml
```

打开或刷新 [算法调试画面](http://10.154.34.1:8000/)。如果仍然看到“原始相机预览”，说明还在运行独立预览程序，或网页需要刷新。相机只能由一个程序占用；不要同时运行预览和检测程序。停止当前主程序使用Ctrl+C。

## 三联画面与颜色

| 画面/标记 | 含义 |
|---|---|
| 左侧彩色图 | 原图叠加检测框、真实轮廓、提取角点和解算状态 |
| 青色框+class/分数 | YOLO输出的目标类别编号与置信度；显示配置class_id筛选后的检测 |
| 蓝色框 | 检测框扩大后实际处理的ROI |
| 紫色线 | 真实轮廓候选，已达到最小面积，但可能尚未通过四边形/方向等检查 |
| 黄色点 | Pose模型输出的有效关键点；缺失负坐标不会画成有效点 |
| 橙色四边形与红色0/1/2/3 | 角点提取已通过；橙色不代表PnP成功 |
| 绿色四边形 | 当前被选中且通过PnP的目标；仅检测模式不会显示绿色 |
| 中间黑白图 | 实际YOLO ROI内的Canny边缘，其他位置为黑色 |
| 右侧黑白图 | 同一ROI的Otsu亮色二值图；检测器也会检查其反相图 |

调试文本使用ASCII以兼容板端OpenCV内置字体，网页图例使用中文。无YOLO检测时不会扫描全图，因此没有ROI时边缘/二值图是黑色；`keypoints`模式使用模型语义关键点，不运行轮廓提取，中/右两栏也为空。

当前代码的OpenCV特征是矩形轮廓与角点，不包含参考工程的专用装甲板双灯条拟合。紫色线显示实际计算过的轮廓；不能把普通YOLO框四角当物理角点。

## 怎样定位问题

- 连青色框都没有：检查模型/类别编号、置信度、目标与训练场景、光照和镜头清晰度。
- 有框，但中间边缘很碎或缺失：检查光照、运动模糊、目标是否太小，及ROI是否覆盖完整目标。
- 有轮廓但没有橙色四边形：查看左上失败状态，检查凸四边形、面积、裁边、方向限制，以及目标是否确实有矩形外边。
- 有橙色但没有绿色：仅检测模式属于正常现象；定位模式下检查角点物理对应、真实尺寸、内参和重投影阈值。
- 绿色目标但没有世界坐标：检查图像时刻是否有有效飞控姿态；`camera_body_only:pose_*` 会说明缺失原因。

## 标定后调试PnP

在正常 [configs/maixcam.yaml](../configs/maixcam.yaml) 中填入真实标定、外参、目标尺寸和模型，保持：

```yaml
localization_enabled: 1
debug_stream: 1
debug_port: 8000
debug_fps: 10
```

板端运行 `./vision_fly_detect configs/maixcam.yaml`，网页上会额外显示相机Z距离和角点重投影RMS。距离和RMS只对PnP有效的当前目标显示。改YAML后重启主程序生效；改C++代码后需要重新编译并上传发布目录。

`debug_fps` 限制的是三联画面编码/推送频率，不是相机帧率。降低到5可减少调试编码开销；`display: 0` 可关闭板端显示；正式运行时 `debug_stream: 0` 可关闭调试网页。当前实现没有网页在线修改参数和单步暂停功能。

主机测试检查了开启诊断采集不会改变角点结果、绘图不会污染计算输入图像，以及三联画面尺寸与类型；真实目标的模型效果仍需用你的模型上板确认。
