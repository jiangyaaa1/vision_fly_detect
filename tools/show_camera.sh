#!/usr/bin/env bash
# 电脑小窗口：默认部署启动板端YOLO；--view-only只打开窗口，不重启相机。
set -euo pipefail
task_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == "--view-only" ]]; then
  shift
  exec /usr/bin/python3 -s "$task_script_dir/camera_window.py" "$@"
else
  exec /usr/bin/python3 -s "$task_script_dir/camera_window.py" --start-board "$@"
fi
