# Infantry-v8n 640×480 高速混合精度模型

网络主体INT8，完整角点回归分支及输出尾部BF16。配套使用本工程szurpv_infantry_v8n解析器。

原始模型来源、元数据和授权见source_metadata.json、UPSTREAM_LICENSE及../infantry/README.md；SHA256和转换/板端基准见manifest.json。

远距离视频20帧与FP32原模型的一致性见teacher_consistency.json。这不是人工真值mAP，也不证明实际5～10米定位精度。运行说明见../../docs/optimization.md。
