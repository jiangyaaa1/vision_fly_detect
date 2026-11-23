# Infantry-v8n：18～20帧目标优化记录

## 2026-11-10：绘制并行与整帧复制优化（已更新Lite）

本次保留原640×480混合精度模型、用户标定、曝光、面积阈值15以及debug_fps=0预览不限速。实际运行UART2、相机采集、NN、几何处理和预览；用户本次没有装甲板，优化前后尾段均没有有效目标，因此下表是无目标场景的处理吞吐，不能冒充成功PnP/坐标输出率。

比较优化前后各最近20组PERF，每组30个实际处理帧：

| 项目 | 优化前中位 | 优化后中位 |
|---|---:|---:|
| 板端处理FPS | 17.01175 | 19.00425 |
| 采集及推理前准备 | 5.99 ms | 7.66 ms |
| 模型前向及输出解析总计 | 46.84 ms | 44.61 ms |
| 主线程后处理、输出和预览提交 | 5.23 ms | 0.25 ms |

中位处理帧率提高约11.7%。优化后范围18.64～19.50FPS；这些数值受场景、负载和温度影响。额外细分计时：NN forward约43.11ms（包括SDK输入预处理、NPU执行和输出取回，不能全部称作纯NPU时间）、解析约1.16ms，结果输出约0.17ms。后台绘制约1.20ms、编码及推流约15.12ms，与主线程重叠，不能直接把所有耗时相加。当前几何循环没有成功PnP，不能据其耗时推断真实目标解算速度。

实现调整：

1. 原异步模式只有编码/推流在后台。现在绘制检测框、关键点、PnP框和坐标文本也使用同一后台线程；每个任务绑定同一帧的图像、检测、角点和坐标，单槽只保留最新待显示帧。
2. 根据当前MaixCDK源码，Camera::read()未传外部缓冲时会新建Image并把采集内容复制到它的独立堆内存。主线程完成该帧计算后，把这个Image的所有权交给后台，消除第二次整帧clone。没有跨帧借用驱动DMA缓冲，编码结束前图像所有者一直存活。BGR/轮廓模式仍保留必要的转换及Mat所有权。
3. 主组件由SDK默认-Os改为-O3；未启用fast-math。六组主机测试、SDK交叉编译通过，包含绘制逐像素等价、RGB/BGR颜色、外部图像生命周期、PnP与坐标变换、UART伪终端收发。
4. 接上此前配置中已存在但未生效的pnp_min_confidence。主配置检测门槛0.40，PnP门槛0.60；低分候选仍可显示模型预测，但不执行PnP、不发送有效坐标。没有减少每帧模型推理次数，也没有使用上一帧角点代替当前帧。
5. PERF新增nn_forward_ms、decode_ms、geometry_ms、output_ms、pnp_valid_frames、async_render_ms、async_encode_ms、preview_replaced，便于继续定位瓶颈。UART仍为独立线程；20Hz发送不等于20Hz新的有效定位测量。

原始前后日志与比较JSON位于工程.local/fps-optimization/，comparison.json记录统计口径，before_binary和before_board.yaml保留了优化前板端程序与配置。以后正常执行tools/show_camera.sh即可部署本次编译的版本，不使用preview_camera.sh判断完整算法帧率。


日期：2026-11-09。设备为用户的MaixCAM Lite，SG2002，官方NPU规格为1TOPS@INT8。目标为5～10米装甲板，优先保留640×480原始输入，不通过降低相机分辨率获得帧率。

## 默认使用

```bash
/home/jhh/code/vision_fly_detect/tools/show_camera.sh
```

模型优化后的默认入口现统一为用户填写的 `configs/maixcam.yaml`，其模型路径已指向 `models/infantry_640x480_int8_mixed_regression/infantry.mud`。相机采集、YOLO、角点、PnP和坐标变换都在Lite执行；电脑只显示。

小窗口图中的 `Process` 是板端处理帧率；窗口状态栏的“预览”是接收画面帧率。当前按用户新要求使用 `debug_fps: 0` 取消预览上限，每个处理帧都提交显示；下表约19FPS的历史数据是在预览限10FPS时测得，不能当作不限速模式的保证。没有用上一帧角点或重复定位结果充数：每个处理帧都实际执行模型，检测到目标后执行当前帧角点/PnP。

原BF16模型和配置保留，可回退：

```bash
python3 /home/jhh/code/vision_fly_detect/tools/infantry_lite.py start --config configs/infantry_quality.yaml
```

再次执行普通启动命令会切回默认高速版。

## 实测口径

| 检查 | 结果 | 限制 |
| --- | --- | --- |
| 原BF16固定图定位计算基准 | 100帧，7.86FPS | 模拟内参/姿态，未含相机采集和预览 |
| INT8主体+BF16输出尾部的第一候选 | 100帧，24.27FPS | 初步候选，非最终精度方案 |
| 最终INT8主体+完整BF16角点回归分支 | 300帧，22.29FPS，300次PnP和世界变换全部完成 | 固定真实装甲板图，含异步调试编码；模拟内参/姿态，未含相机采集 |
| 上述300帧耗时 | NN平均40.17ms，角点/PnP/变换3.40ms，调试提交1.29ms，总平均44.87ms，P95 47.14ms | 是计算吞吐，不是实际导航精度 |
| 真实相机+最终模型+窗口同时运行 | 最终最近60组30帧统计，中位19.3436FPS、最高19.6643FPS、最低16.9582FPS；多数约18.9～19.6FPS | 当前实拍无装甲板，未实际执行成功PnP，不能称为实时有效世界坐标输出率 |
| 独立远距离视频20帧 | 与FP32原模型20个目标全部匹配，无额外不匹配目标；角点平均差异0.120px、P95 0.230px、最大0.342px | 教师一致性，非人工真值mAP；视频未提供实际米数，不能据此证明5～10米精度 |

本次可以证明：Lite已具备接近18～20帧的实时处理速度，完整定位计算有对应吞吐余量。实际有效世界坐标输出频率和5～10米测距精度，仍需要真实相机标定、安装外参、实时机体姿态与实际装甲板测试。上述历史测试当时使用 `localization_enabled: 0`；当前用户填写的主配置已启用定位，不能将旧检测配置当作当前状态。

## 修改内容

1. 模型主体使用INT8，完整角点回归分支及最终分类/颜色输出尾部保留BF16，减少角点量化步进。模型约3.6MiB，原BF16约7.3MiB。
2. 使用上游装甲板视频24帧、中距离视频40帧做量化校准；远距离平移视频20帧仅验证，不参与校准。校准NPZ按真实RGB/255生成，未用随机噪声、暗画面、重复单图代替。
3. KL全INT8、部分尾部BF16的首轮严格检查未通过，未作为默认模型。最终采用MAX校准与混合精度，张量检查使用官方转换示例的0.90/0.60阈值，并额外进行目标/角点一致性检查；不以张量阈值代替精度验收。
4. 类别固定时先检查该类别置信度，再对可能通过的候选做完整argmax和颜色解析。这与原筛选结果等价，避免逐帧对6300个背景位置重复完整解析。
5. PnP用Brown5投影解析导数代替数值差分；保留原5个初值、60轮上限、重投影和正深度检查。原有精确投影、噪声、拒绝条件及FRD/NED变换测试通过。
6. 关键点模式的几何校验不读取像素，直接引用RGB原图；叠加图保持RGB送硬件JPEG，省去RGB/BGR往返转换。轮廓模式仍使用真正的BGR，不改变算法颜色含义。RGB/BGR叠加颜色等价测试通过。
7. 调试编码使用独立低优先级线程、单个待编码槽，旧待显示帧覆盖，不阻塞定位。关闭程序时先结束编码线程，再停止流和相机。
8. 修复SDK打ZIP与SSH发布目录不同步、主程序权限丢失问题，确保小窗口启动的是最新模型和配置。

## 复现

工具依赖仍隔离在 `.local/infantry-python` 和 `.local/tpu-python`，未替换系统环境。

```bash
cd /home/jhh/code/vision_fly_detect
python3 -s tools/optimize_infantry.py --mixed-heads --precision-policy regression \
  --reuse-calibration --tolerance 0.90,0.60
python3 -s tools/evaluate_infantry.py models/infantry_640x480_int8_mixed_regression/infantry.cvimodel
./tools/build_maixcam.sh
```

所需上游视频已在 `.local/infantry-fast/videos`；校准记录和日志在 `.local/infantry-fast/640x480`；最终模型SHA256、板端基准和一致性报告在模型目录中。原始桌面ONNX未修改。脚本可生成其它输入尺寸的转换候选，但当前板端适配固定640×480，不可只改脚本宽高后直接部署。

板端300帧基准（先停止实时程序；不会写定位JSON）：

```bash
cd /root/vision_fly_detect_infantry
LD_LIBRARY_PATH=./dl_lib ./vision_fly_detect configs/infantry_fast.yaml \
  --benchmark-fixture /root/armor_test.jpg 300
```

基准画面带 `BENCHMARK - SYNTHETIC CALIBRATION` 水印。模拟标定、模拟姿态只存在于基准函数局部变量，不修改正常配置，结果明确为 `output_not_for_navigation=1`。

## 关于用户提供的SAHI切片文章

已阅读用户粘贴的全文。其方法是在多个重叠补丁上分别推理，再做坐标回填/NMS，可帮助高分辨率小目标检出；原文也说明切片越多算力需求越高、推理时间越长。因此当前不在每帧启用全图滑动多切片，以免破坏18～20帧目标。

后续可评估“单个目标ROI精检+周期全图重新搜索”。这与完整SAHI多块扫描不同，也不保证一定减少推理次数。ROI结果必须还原到原始图像的像素坐标后再使用原内参做PnP；如果改为更高分辨率采集以保留更多原生细节，还需匹配新采集模式的真实标定。当前版本未宣称已经实现或验收ROI远距离增强。

参考：[Sipeed硬件规格](https://wiki.sipeed.com/hardware/en/maixcam/maixcam.html)、[官方手动转换](https://wiki.sipeed.com/maixpy/doc/zh/ai_model_converter/maixcam.html)、[SAHI切片流程](https://github.com/obss/sahi/blob/main/docs/guides/sliced-inference.md)。

## 预览不限速更新

按用户要求，默认高速配置已改为debug_fps=0，取消10FPS限制。窗口轮询周期改为10ms，仅在收到新帧时解码和更新，不用重复旧帧制造高帧率。处理和预览的实际速度仍受NPU、CPU、编码与传输能力约束；Process和预览各自显示实测值。

重连后的不限速实测见performance_live_uncapped.json：板端处理帧率中位约17.67FPS，窗口约17.52FPS。此结果反映每个处理帧都提交预览的额外开销，不是固定17FPS限制。

配置入口修复：小窗口和普通启动默认只读取configs/maixcam.yaml，保留用户填写的内外参及最新static姿态。历史infantry_lite/fast配置只供显式选择的检测或性能回放，不会自动覆盖主配置。status读取active_run.json关联的实际结果文件，不再固定读取旧infantry_positions.jsonl。
