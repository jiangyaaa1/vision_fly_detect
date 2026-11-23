#!/usr/bin/env bash
# 使用：source /home/jhh/code/vision_fly_detect/tools/maixcdk_env.sh
# 仅设置当前终端，不修改 ~/.bashrc。
export MAIXCDK_PATH="/home/jhh/MaixCDK"
case ":${PATH}:" in
  *":/home/jhh/.local/bin:"*) ;;
  *) export PATH="/home/jhh/.local/bin:${PATH}" ;;
esac
# 部分SDK后处理脚本调用python；本机只有python3时提供工程内别名。
# 不安装系统python、不修改系统软链接。
if ! command -v python >/dev/null 2>&1; then
  task_maix_env_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
  mkdir -p "$task_maix_env_root/.local/maixcdk-bin"
  ln -sfn "$(command -v python3)" "$task_maix_env_root/.local/maixcdk-bin/python"
  export PATH="$task_maix_env_root/.local/maixcdk-bin:${PATH}"
fi
printf 'MaixCDK 环境已启用：%s\n' "$MAIXCDK_PATH"
