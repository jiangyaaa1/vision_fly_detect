请把为MaixCAM转换后的目标模型 `.mud` 和其引用的 `.cvimodel` 放在这里，
或在 `configs/maixcam.yaml` 中填写已经位于开发板上的模型路径。
本工程不包含训练好的目标模型，也不会将通用模型假装为你的目标模型。
`class_id` 必须与模型标签匹配，且这一类别的目标必须具有配置中的真实尺寸。

普通Detect模型使用 `corner_mode: "contour"`，需要清晰的矩形物体边界。
四角点Pose模型使用 `corner_mode: "keypoints"`，关键点按物理角点顺序映射。
具体转换要求见 [MaixCDK核对记录](../docs/maixcdk_notes.md)。
