#!/usr/bin/env bash
set -euo pipefail
task_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$task_script_dir/maixcdk_env.sh"
task_project_dir="$(cd -- "$task_script_dir/.." && pwd)"
if [[ "${1:-}" == "preview" ]]; then
  task_project_dir="$task_project_dir/examples/camera_preview"
elif [[ -n "${1:-}" ]]; then
  printf '用法：%s [preview]\n' "$0" >&2
  exit 2
fi
cd -- "$task_project_dir"
maixcdk build -p maixcam --toolchain-id musl_t-head
maixcdk release -p maixcam
if [[ "${1:-}" != "preview" ]]; then
  python3 "$task_script_dir/package_maixcam.py"
fi
