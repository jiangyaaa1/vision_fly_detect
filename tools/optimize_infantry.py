#!/usr/bin/env python3
"""使用上游真实视频帧校准深大模型，保留原BF16发布版本，输出独立INT8候选。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
for directory in (ROOT / ".local/infantry-python", ROOT / ".local/tpu-python"):
    sys.path.insert(0, str(directory))


def letterbox(image, width, height):
    import cv2
    ratio = min(width / image.shape[1], height / image.shape[0])
    rw, rh = round(image.shape[1] * ratio), round(image.shape[0] * ratio)
    x, y = (width - rw) // 2, (height - rh) // 2
    small = cv2.resize(image, (rw, rh), interpolation=cv2.INTER_LINEAR)
    return cv2.copyMakeBorder(small, y, height - rh - y, x, width - rw - x,
                             cv2.BORDER_CONSTANT, value=(124, 124, 124))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--quantize", choices=["INT8", "BF16"], default="INT8")
    parser.add_argument("--mixed-heads", action="store_true", help="最终回归卷积及Sigmoid保留BF16")
    parser.add_argument("--reuse-calibration", action="store_true")
    parser.add_argument("--cali-method", choices=["max", "kl", "mse"], default="max")
    parser.add_argument("--tolerance", default="0.90,0.80", help="候选张量比对阈值；必须另做目标/角点验证")
    parser.add_argument("--precision-policy", choices=["last", "regression"], default="last")
    args = parser.parse_args()
    import cv2
    import numpy as np
    import onnx
    import onnxruntime as ort
    if args.width % 32 or args.height % 32:
        raise ValueError("模型输入宽高必须为32的倍数")
    work = ROOT / f".local/infantry-fast/{args.width}x{args.height}"
    work.mkdir(parents=True, exist_ok=True)
    model = onnx.load(str(ROOT / ".local/infantry-conversion/infantry.onnx"))
    dims = model.graph.input[0].type.tensor_type.shape.dim
    dims[2].dim_value, dims[3].dim_value = args.height, args.width
    del model.graph.value_info[:]
    count = sum((args.width // s) * (args.height // s) for s in (8, 16, 32))
    for output, channels in zip(model.graph.output, (8, 4, 9)):
        for d, value in zip(output.type.tensor_type.shape.dim, (1, channels, count)):
            d.dim_value = value
    model = onnx.shape_inference.infer_shapes(model)
    onnx.checker.check_model(model)
    onnx.save(model, str(work / "infantry.onnx"))
    # 校准只使用原装甲板视频+中距离视频；远距离视频留作独立一致性验证。
    sources = [(ROOT / ".local/infantry-conversion/armor_test.mp4", 24),
               (ROOT / ".local/infantry-fast/videos/中距离陀螺.avi", 40)]
    dataset = work / "calibration"
    dataset.mkdir(exist_ok=True)
    records = []
    sample = None
    for number, (source, amount) in enumerate(sources):
        cap = cv2.VideoCapture(str(source))
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        if total < amount:
            raise ValueError(f"校准视频不可用：{source}")
        for i, frame_id in enumerate(np.linspace(0, total - 1, amount, dtype=int)):
            cap.set(cv2.CAP_PROP_POS_FRAMES, int(frame_id))
            ok, image = cap.read()
            if not ok:
                raise ValueError(f"读取校准视频帧失败：{source}:{frame_id}")
            canvas = letterbox(image, args.width, args.height)
            value = cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB).transpose(2, 0, 1)[None].astype(np.float32) / 255
            path = dataset / f"clip{number}_{i:03d}.npz"
            np.savez(path, images=value)
            records.append(dict(video=str(source), frame=int(frame_id), input_sha256=hashlib.sha256(value.tobytes()).hexdigest()))
            if number == 0 and i == 5:
                sample = value
        cap.release()
    np.savez(work / "test_input.npz", images=sample)
    (work / "calibration_manifest.json").write_text(json.dumps(records, ensure_ascii=False, indent=2) + "\n")
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    session = ort.InferenceSession(str(work / "infantry.onnx"), options, providers=["CPUExecutionProvider"])
    result = session.run(None, {"images": sample})
    if [list(v.shape) for v in result] != [[1, c, count] for c in (8, 4, 9)]:
        raise ValueError("修改输入后输出shape不一致")
    env = os.environ.copy()
    env["PYTHONPATH"] = os.pathsep.join(str(p) for p in (ROOT / ".local/tpu-python", ROOT / ".local/infantry-python"))
    env["OMP_NUM_THREADS"] = "4"
    env["MPLCONFIGDIR"] = str(work / "matplotlib")
    tool = ROOT / ".local/tpu-python/bin"
    def run(name, arguments, log=None):
        print("RUN", name, flush=True)
        with (work / ((log or name) + ".log")).open("w") as output:
            subprocess.run([sys.executable, "-s", str(tool / (name + ".py")), *arguments],
                           cwd=work, env=env, stdout=output, stderr=subprocess.STDOUT, check=True)
    if not args.reuse_calibration or not (work / "infantry.mlir").exists():
        run("model_transform", ["--model_name", "infantry", "--model_def", "infantry.onnx",
            "--input_shapes", f"[[1,3,{args.height},{args.width}]]", "--mean", "0,0,0",
            "--scale", ",".join([str(1 / 255)] * 3), "--pixel_format", "rgb", "--channel_format", "nchw",
            "--test_input", "test_input.npz", "--test_result", "infantry_top_outputs.npz", "--mlir", "infantry.mlir"])
    calibration_table = "calibration_" + args.cali_method + ".table"
    if args.quantize == "INT8" and (not args.reuse_calibration or not (work / calibration_table).exists()):
        run("run_calibration", ["infantry.mlir", "--dataset", str(dataset), "--input_num", str(len(records)),
            "--chip", "cv181x", "--tune_num", "0", "--cali_method", args.cali_method, "-o", calibration_table])
    tag = args.quantize.lower() + ("_mixed" if args.mixed_heads else "")
    if args.cali_method != "max": tag += "_" + args.cali_method
    if args.precision_policy == "regression": tag += "_regression"
    destination = ROOT / f"models/infantry_{args.width}x{args.height}_{tag}"
    destination.mkdir(exist_ok=True)
    arguments = ["--mlir", "infantry.mlir", "--quantize", args.quantize, "--chip", "cv181x",
        "--test_input", "test_input.npz", "--test_reference", "infantry_top_outputs.npz", "--model", str(work / f"infantry_{tag}.cvimodel")]
    if args.quantize == "INT8":
        arguments += ["--calibration_table", calibration_table, "--quant_input", "--tolerance", args.tolerance]
    if args.mixed_heads:
        heads = [f"/model.22/{branch}.{i}/{branch}.{i}.2/Conv_output_0_Conv BF16"
                 for branch in ("cv4", "cvcolor", "cvarmor") for i in range(3)]
        heads += [f"/model.22/Reshape{('_' + str(i)) if i else ''}_output_0_Reshape BF16"
                  for i in range(9)]
        heads += [f"/model.22/Concat{('_' + str(i)) if i else ''}_output_0_Concat BF16"
                  for i in range(3)]
        heads += [f"/model.22/{n}_output_0_Sigmoid BF16" for n in ("Sigmoid", "Sigmoid_1")]
        if args.precision_policy == "regression":
            for i in range(3):
                for layer in range(2):
                    stem = f"/model.22/cv4.{i}/cv4.{i}.{layer}"
                    heads += [stem + "/conv/Conv_output_0_Conv BF16", stem + "/act/Mul_output_0_Mul BF16"]
        (work / "heads.table").write_text("\n".join(heads) + "\n")
        arguments += ["--quantize_table", "heads.table"]
    run("model_deploy", arguments, "deploy_" + tag)
    target = destination / "infantry.cvimodel"
    shutil.copy2(work / f"infantry_{tag}.cvimodel", target)
    mud = (ROOT / "models/infantry/infantry.mud").read_text().replace("infantry_bf16.cvimodel", "infantry.cvimodel")
    (destination / "infantry.mud").write_text(mud)
    manifest = dict(width=args.width, height=args.height, candidate_count=count, quantization=tag,
                    calibration_method=args.cali_method, tensor_tolerance=args.tolerance,
                    precision_policy=args.precision_policy,
                    input_preprocess="RGB /255, centered letterbox fill124", calibration_frames=len(records),
                    calibration_source="public upstream armor and medium-range video; not deployment-camera calibration",
                    sha256=hashlib.sha256(target.read_bytes()).hexdigest(), board_validated=False)
    (destination / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print("CANDIDATE", destination, flush=True)


if __name__ == "__main__":
    main()
