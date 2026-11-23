#!/usr/bin/env python3
"""检查本次深大模型，生成FP32三输出转换副本；不改动用户的原始ONNX。"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, numpy_helper

POINTS = "/model.22/Concat_output_0"
COLORS = "/model.22/Sigmoid_output_0"
CLASSES = "/model.22/Sigmoid_1_output_0"


def topological_sort(model):
    ready = {v.name for v in model.graph.input} | {v.name for v in model.graph.initializer} | {""}
    pending = list(model.graph.node)
    ordered = []
    while pending:
        batch = [n for n in pending if all(v in ready for v in n.input)]
        if not batch:
            raise ValueError("ONNX存在循环或缺失输入，无法排序")
        for node in batch:
            ordered.append(node)
            ready.update(node.output)
            pending.remove(node)
    del model.graph.node[:]
    model.graph.node.extend(ordered)


def prepare(source, destination):
    model = onnx.load(str(source))
    shapes = [[d.dim_value for d in v.type.tensor_type.shape.dim]
              for v in list(model.graph.input) + list(model.graph.output)]
    if shapes != [[1, 3, 480, 640], [1, 21, 6300]]:
        raise ValueError(f"不是本适配器支持的固定模型：{shapes}")
    # FP16权重转为等值FP32，便于CPU参考推理及TPU-MLIR读取。
    for tensor in model.graph.initializer:
        if tensor.data_type == TensorProto.FLOAT16:
            tensor.CopyFrom(numpy_helper.from_array(
                numpy_helper.to_array(tensor).astype(np.float32), tensor.name))
    for node in model.graph.node:
        for attribute in node.attribute:
            if node.op_type == "Cast" and attribute.name == "to" and attribute.i == TensorProto.FLOAT16:
                attribute.i = TensorProto.FLOAT
            if attribute.type == onnx.AttributeProto.TENSOR and attribute.t.data_type == TensorProto.FLOAT16:
                attribute.t.CopyFrom(numpy_helper.from_array(
                    numpy_helper.to_array(attribute.t).astype(np.float32), attribute.t.name))
    for value in list(model.graph.input) + list(model.graph.output) + list(model.graph.value_info):
        if value.type.tensor_type.elem_type == TensorProto.FLOAT16:
            value.type.tensor_type.elem_type = TensorProto.FLOAT
    topological_sort(model)
    onnx.checker.check_model(model)
    # 验证坐标解码常量，拒绝看似相同、实际输出语义不同的模型。
    values = {v.name: numpy_helper.to_array(v) for v in model.graph.initializer}
    grids, strides = [], []
    for stride in (8, 16, 32):
        y, x = np.mgrid[:480 // stride, :640 // stride]
        grids.append(np.stack((x.ravel(), y.ravel())))
        strides.append(np.full((1, x.size), stride))
    grid = np.concatenate(grids, axis=1)
    stride = np.concatenate(strides, axis=1)
    for name, expected in [("/model.22/Constant_14_output_0", np.array(2)),
                           ("/model.22/Constant_15_output_0", grid),
                           ("/model.22/Constant_16_output_0", stride)]:
        if name not in values or not np.array_equal(values[name], expected):
            raise ValueError(f"解码常量不匹配：{name}")
    destination.mkdir(parents=True, exist_ok=True)
    full = destination / "reference_fp32.onnx"
    onnx.save(model, str(full))
    inferred = onnx.shape_inference.infer_shapes(model)
    full_inferred = destination / "inferred.onnx"
    onnx.save(inferred, str(full_inferred))
    export = destination / "infantry.onnx"
    onnx.utils.extract_model(str(full_inferred), str(export), [model.graph.input[0].name],
                             [POINTS, COLORS, CLASSES])
    trimmed = onnx.load(str(export))
    for value, channels in zip(trimmed.graph.output, (8, 4, 9)):
        if [d.dim_value for d in value.type.tensor_type.shape.dim] != [1, channels, 6300]:
            raise ValueError(f"裁剪输出shape不匹配：{value.name}")
    onnx.checker.check_model(trimmed)
    full_inferred.unlink()
    # 数值对比：裁剪结果在CPU重建后应等于修复的原始完整输出。
    import onnxruntime as ort
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    sessions = [ort.InferenceSession(str(p), options, providers=["CPUExecutionProvider"])
                for p in (full, export)]
    max_error = 0.0
    rng = np.random.default_rng(42)
    for image in (np.zeros((1, 3, 480, 640), np.float32),
                  rng.random((1, 3, 480, 640), dtype=np.float32)):
        original = sessions[0].run(None, {model.graph.input[0].name: image})[0]
        points, colors, classes = sessions[1].run(None, {model.graph.input[0].name: image})
        decoded = ((points.reshape(1, 4, 2, 6300) * 2 + grid) * stride).reshape(1, 8, 6300)
        result = np.concatenate((colors, classes, decoded), axis=1)
        error = float(np.max(np.abs(result - original)))
        if not np.isfinite(result).all() or error > 0.002:
            raise ValueError(f"裁剪模型解码不等价：最大误差{error}")
        max_error = max(max_error, error)
    manifest = dict(source=str(source.resolve()), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    export_sha256=hashlib.sha256(export.read_bytes()).hexdigest(),
                    input_shape=shapes[0], outputs=[POINTS, COLORS, CLASSES],
                    reconstruction_max_abs_error=max_error, board_validated=False)
    (destination / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--out", type=Path, default=Path(".local/infantry-conversion"))
    args = parser.parse_args()
    prepare(args.source, args.out)
