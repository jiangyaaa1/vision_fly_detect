#!/usr/bin/env bash
set -euo pipefail
task_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# 使用已验证的Ubuntu系统OpenCV/NumPy，避免用户目录NumPy2与系统OpenCV冲突。
exec /usr/bin/python3 -s "$task_script_dir/capture_camera.py" "$@"
