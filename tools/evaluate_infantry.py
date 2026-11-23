#!/usr/bin/env python3
"""无人工真值时的教师一致性检查：比较FP32原模型与板端候选；不把它称为mAP。"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
for directory in (ROOT / ".local/infantry-python", ROOT / ".local/tpu-python"):
    sys.path.insert(0, str(directory))


def decode(outputs, width=640, height=480):
    import cv2
    import numpy as np
    n = sum(width // s * (height // s) for s in (8, 16, 32))
    tensors = {c: next(v.reshape(c, n) for v in outputs.values() if v.size == c * n) for c in (8, 4, 9)}
    labels = tensors[9].argmax(0)
    scores = tensors[9].max(0)
    colors = tensors[4].argmax(0)
    grids, strides = [], []
    for s in (8, 16, 32):
        y, x = np.mgrid[:height // s, :width // s]
        grids.append(np.stack((x.ravel(), y.ravel())))
        strides.append(np.full((1, x.size), s))
    points = ((tensors[8].reshape(4, 2, n) * 2 + np.concatenate(grids, 1)) * np.concatenate(strides, 1)).transpose(2, 0, 1)
    candidates = []
    for i in np.flatnonzero((scores >= .6) & (colors != 3)):
        p = points[i].astype(np.float32)
        if not np.isfinite(p).all() or np.any(p < 0) or np.any(p[:, 0] >= width) or np.any(p[:, 1] >= height): continue
        if not cv2.isContourConvex(p) or abs(cv2.contourArea(p)) < 100: continue
        candidates.append(dict(points=p, label=int(labels[i]), color=int(colors[i]), score=float(scores[i]), box=cv2.boundingRect(p)))
    result = []
    for item in sorted(candidates, key=lambda v: -v['score']):
        if not any(iou(item['box'], r['box']) > .45 for r in result): result.append(item)
    return result


def iou(a, b):
    x = max(a[0], b[0]); y = max(a[1], b[1])
    area = max(0, min(a[0] + a[2], b[0] + b[2]) - x) * max(0, min(a[1] + a[3], b[1] + b[3]) - y)
    union = a[2] * a[3] + b[2] * b[3] - area
    return area / union if union else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--frames', type=int, default=20)
    args = parser.parse_args()
    import cv2
    import numpy as np
    import onnxruntime as ort
    import tpu_mlir
    from tools.model_runner import model_inference
    from optimize_infantry import letterbox
    manifest = json.loads((args.candidate.parent / 'manifest.json').read_text())
    width, height = manifest['width'], manifest['height']
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    teacher = ort.InferenceSession(str(ROOT / '.local/infantry-conversion/infantry.onnx'), options,
                                  providers=['CPUExecutionProvider'])
    video = ROOT / '.local/infantry-fast/videos/远距离平移.avi'
    cap = cv2.VideoCapture(str(video)); count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if not count: raise ValueError('独立验证视频不可用')
    total = matched = false_extra = 0
    errors = []
    per_frame = []
    for frame_id in np.linspace(0, max(0, count - 8), args.frames, dtype=int):
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(frame_id)); ok, image = cap.read()
        if not ok: raise ValueError('视频读取失败')
        source = cv2.resize(image, (640, 480))
        value = cv2.cvtColor(source, cv2.COLOR_BGR2RGB).transpose(2, 0, 1)[None].astype(np.float32) / 255
        reference = teacher.run(None, {'images': value})
        expected = decode(dict(zip(('points', 'colors', 'classes'), reference)))
        canvas = letterbox(source, width, height)
        v = cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB).transpose(2, 0, 1)[None].astype(np.float32) / 255
        out = model_inference({'images': v}, str(args.candidate.resolve()), dump_all=False)
        observed = decode(out, width, height)
        ratio = min(width / 640, height / 480)
        pad = np.array([(width - round(640 * ratio)) // 2, (height - round(480 * ratio)) // 2])
        for item in observed:
            item['points'] = ((item['points'] - pad) / ratio).astype(np.float32)
            item['box'] = cv2.boundingRect(item['points'])
        used = set(); local = 0
        for item in expected:
            eligible = [(iou(item['box'], other['box']), j) for j, other in enumerate(observed)
                        if j not in used and item['label'] == other['label'] and item['color'] == other['color']]
            overlap, index = max(eligible, default=(0, -1))
            if overlap >= .5:
                used.add(index); local += 1
                errors.extend(np.linalg.norm(item['points'] - observed[index]['points'], axis=1).tolist())
        total += len(expected); matched += local; false_extra += len(observed) - len(used)
        per_frame.append(dict(frame=int(frame_id), teacher=len(expected), candidate=len(observed), matched=local))
        print('EVAL_FRAME', per_frame[-1], flush=True)
    cap.release()
    report = dict(reference='original640x480FP32; proxy consistency, not ground truth accuracy',
                  heldout_video=str(video), frames=args.frames, teacher_detections=total, matched=matched,
                  teacher_match_ratio=matched / total if total else None, unmatched_candidate=false_extra,
                  corner_mean_px=float(np.mean(errors)) if errors else None,
                  corner_p95_px=float(np.percentile(errors, 95)) if errors else None,
                  corner_max_px=max(errors) if errors else None, per_frame=per_frame)
    (args.candidate.parent / 'teacher_consistency.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('REPORT', json.dumps({k:v for k,v in report.items() if k!='per_frame'}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
