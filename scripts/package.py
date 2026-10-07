import argparse
import re
import shutil
import zipfile
from pathlib import Path

import build_assets
import build_records


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    parser.add_argument("dll_17104", type=Path)
    args = parser.parse_args()
    if not args.dll.is_file() or not args.dll_17104.is_file():
        parser.error("Both compiled runtime DLLs are required")
    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        parser.error("VERSION must contain major.minor.patch")
    destination = ROOT / "dist" / f"ActiveQuestTrail-{version}"
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    core = destination / "Core"
    core.mkdir(exist_ok=True)
    build_assets.build(core)
    build_records.build(core)
    for runtime, dll in [("1.6.1170", args.dll), ("1.7.104", args.dll_17104)]:
        target = destination / "Runtime" / runtime / "SKSE/Plugins/ActiveQuestTrail.dll"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dll, target)
    shutil.copytree(ROOT / "installer/fomod", destination / "fomod", dirs_exist_ok=True)
    (destination / "fomod/info.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<fomod>\n    <Name>Active Quest Trail</Name>\n'
        '    <Author>Active Quest Trail contributors</Author>\n'
        f'    <Version>{version}</Version>\n'
        '    <Description>A persistent quest trail with matching destination glow and optional Community Shaders lighting.</Description>\n'
        '</fomod>\n', encoding="utf-8")
    output = destination.parent / (destination.name + ".zip")
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(destination))
    print(output)


if __name__ == "__main__":
    main()
