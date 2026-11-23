# 重启、全类别检测与角点/坐标显示

## 修改后怎么启动

只修改 `configs/maixcam.yaml` 的参数：窗口中点击“启动板端”，或执行下面命令，自动打包最新参数、上传并重启：

```bash
/home/jhh/code/vision_fly_detect/tools/show_camera.sh
```

修改 `src`、`tasks`、`io`、`tools` 下的C++源码或头文件，需要先重新编译：

```bash
cd /home/jhh/code/vision_fly_detect
./tools/build_maixcam.sh
./tools/show_camera.sh
```

只修改电脑小窗口的Python显示代码：关闭旧窗口后重新执行show_camera.sh即可。`--view-only`仅打开显示，不更新板端，不能用它让新C++代码或YAML参数生效。

## 检测所有类别

主配置已设置：

```yaml
class_id: -1
armor_color: -1
```

`class_id=-1`表示深大模型支持的全部装甲板类别；0～8表示仅指定类别。`armor_color=-1`保留蓝、红、白输出，紫色按原模型逻辑过滤。这不会增加原模型没有学会的类别，当前版本不再识别5号步兵。

全类别PnP使用 `armor_width_by_class` 和 `armor_height_by_class`，顺序为0哨兵、1英雄、2工程、3步兵3、4步兵4、5步兵5、6前哨、7基地底、8基地顶。小板沿用用户的0.135×0.056米；按上游大小分类，1与7默认宽0.230米。必须核对实际物理角点间距；尺寸不确定时该类宽高都设为0，程序仍显示检测，但不解算该类距离。单类别模式继续使用原target_width/target_height。

窗口显示所有检测框，但坐标仍对应当前帧选中的一个PnP有效目标，不是同时输出每块装甲板的坐标列表。按置信度依次尝试，第一块通过校验的目标画绿色；其它已提取四角的目标画橙色。没有做跨帧目标ID锁定。

## 在窗口里看什么

- 青色矩形：YOLO检测框，旁边是类别和置信度。
- 黄色点：模型原始四角点；橙色四边形：通过角点检查；绿色四边形：当前选中的PnP成功目标。
- 红色0/1/2/3：左上、右上、右下、左下的物理角点对应顺序。

PnP成功后，画面上方显示：

```text
ok_static_pose C3 | 18.0 FPS | RMS 0.30px
Cam XYZ(m):      0.030, -0.020, 2.000
Body FRD(m):     2.000,  0.030, -0.120
NED [STATIC](m): 2.000,  0.030, -0.120
```

上面仅为坐标格式示例，不是当前相机实测值：

- `Cam XYZ`：相机光心到目标中心，X向右、Y向下、Z向前，单位米。
- `Body FRD`：无人机机体原点到目标中心，前、右、下，已应用外参和平移。
- `NED`：同一个相对位移按北、东、地方向表达。`[STATIC]`表示使用配置里的固定姿态，不能当作飞行中的实时姿态。
- 没有匹配姿态时显示 `NED unavailable`，不会把机体坐标冒充世界坐标。
- `RMS`是四角点重投影误差，单位像素。

没有目标、角点不合格或PnP失败时不显示上一帧坐标。当前主配置为static固定姿态测试；要得到动态真实世界方向，需要后续接入实时姿态。

也可以查看数值日志：

```bash
python3 /home/jhh/code/vision_fly_detect/tools/infantry_lite.py status
```

主配置结果文件为板端 `/root/vision_fly_detect_infantry/target_positions.jsonl`。`valid=true`才是当前有效PnP；世界方向还需`world_valid=true`，其中static的有效性仅针对固定姿态假设。
