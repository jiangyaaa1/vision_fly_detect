# 深大 Infantry-v8n 20260726

`infantry.mud` + `infantry_bf16.cvimodel`是一组cv181x BF16模型。使用本工程自定义`szurpv_infantry_v8n`解析器，不兼容标准`nn::YOLOv8`后处理。

来源：https://github.com/SZURPVision/26_NNDeployment_Lib_and_Detection_Models

用户提供的原始ONNX及转换结果SHA256记录在manifest.json。源ONNX元数据标注Ultralytics及AGPL-3.0；上游仓库根目录另附MIT许可证，随本目录保留上游许可证和原始元数据供追溯，不改写模型授权声明。

运行与标定步骤见../../docs/infantry_lite.md。manifest中的board_validated仅表示板端运行检查，不能代替真实场景精度、速度与PnP标定验收。
