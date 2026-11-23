#!/usr/bin/env python3
"""在电脑隔离工具环境内，将深大Infantry ONNX转换为Lite的BF16模型。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
for directory in (ROOT / ".local/infantry-python", ROOT / ".local/tpu-python"):
    sys.path.insert(0, str(directory))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--test-image", type=Path, required=True, help="真实装甲板场景图片")
    args = parser.parse_args()
    import cv2
    import numpy as np
    from prepare_infantry import prepare
    work = ROOT / ".local/infantry-conversion"
    prepare(args.source.resolve(), work)
    image = cv2.imread(str(args.test_image.resolve()))
    if image is None:
        raise ValueError("无法读取测试图片")
    image = cv2.resize(image, (640, 480))
    value = cv2.cvtColor(image, cv2.COLOR_BGR2RGB).transpose(2, 0, 1)[None].astype(np.float32) / 255
    np.savez(work / "test_input.npz", images=value)
    env = os.environ.copy()
    env["PYTHONPATH"] = os.pathsep.join(str(p) for p in
        (ROOT / ".local/tpu-python", ROOT / ".local/infantry-python"))
    env["OMP_NUM_THREADS"] = "4"
    env["MPLCONFIGDIR"] = str(work / "matplotlib")
    tool = ROOT / ".local/tpu-python/bin"
    def run(name, arguments):
        with (work / (name + ".log")).open("w") as log:
            subprocess.run([sys.executable, "-s", str(tool / (name + ".py")), *arguments],
                           cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    run("model_transform", ["--model_name", "infantry", "--model_def", "infantry.onnx",
        "--input_shapes", "[[1,3,480,640]]", "--mean", "0,0,0",
        "--scale", "0.00392156862745098,0.00392156862745098,0.00392156862745098",
        "--pixel_format", "rgb", "--channel_format", "nchw", "--test_input", "test_input.npz",
        "--test_result", "infantry_top_outputs.npz", "--mlir", "infantry.mlir"])
    output = ROOT / "models/infantry"
    output.mkdir(parents=True, exist_ok=True)
    model = work / "infantry_bf16.cvimodel"
    run("model_deploy", ["--mlir", "infantry.mlir", "--quantize", "BF16", "--chip", "cv181x",
        "--test_input", "test_input.npz", "--test_reference", "infantry_top_outputs.npz",
        "--model", str(model)])
    import shutil
    shutil.copy2(model, output / model.name)
    # 仅在转换、参考输出比对全部通过后发布MUD，防止把占位模型误认为可运行模型。
    (output / "infantry.mud").write_text("""[basic]
type = cvimodel
model = infantry_bf16.cvimodel

[extra]
model_type = szurpv_infantry_v8n
input_type = rgb
mean = 0,0,0
scale = 0.00392156862745098,0.00392156862745098,0.00392156862745098
labels = sentry,hero,engineer,infantry3,infantry4,infantry5,outpost,base_bottom,base_top
""")
    manifest = json.loads((work / "manifest.json").read_text())
    import hashlib
    manifest.update(quantization="BF16", chip="cv181x", converter="tpu_mlir==1.30.2",
                    model_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),
                    conversion_reference_passed=True)
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(f"转换及参考对比通过：{model}\n下一步交叉编译并上板实测。")


if __name__ == "__main__":
    main()
