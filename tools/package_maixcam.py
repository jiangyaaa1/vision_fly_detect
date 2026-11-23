#!/usr/bin/env python3
"""统一ZIP包和SSH发布目录，避免仅重新打ZIP却部署旧配置。"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def package_main():
    from maixtool.app_release import pack
    binary = ROOT / "build/vision_fly_detect"
    if not binary.is_file():
        raise FileNotFoundError("请先执行tools/build_maixcam.sh编译")
    result = pack(str(ROOT), str(binary), extra_files={str(ROOT / "build/dl_lib"): "dl_lib"})
    # maixtool.pack只更新dist/pack，不更新CMake旧的*_release目录，显式同步。
    shutil.copytree(ROOT / "dist/pack/vision_fly_detect", ROOT / "dist/vision_fly_detect_release", dirs_exist_ok=True)
    # SDK安装器会chmod主程序；SSH直接运行也必须显式保留执行权限。
    (ROOT / "dist/vision_fly_detect_release/vision_fly_detect").chmod(binary.stat().st_mode & 0o777)
    return result


if __name__ == "__main__":
    print(package_main())
