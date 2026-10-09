import argparse
import re
import shutil
import tempfile
import zipfile
from pathlib import Path

import build_archive
import build_assets
import build_records


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dll", type=Path)
    args = parser.parse_args()
    if not args.dll.is_file():
        parser.error("The compiled ActiveQuestTrail.dll is required")
    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        parser.error("VERSION must contain major.minor.patch")
    destination = ROOT / "dist" / f"ActiveQuestTrail-{version}"
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="aqt-assets-") as temporary:
        assets = Path(temporary)
        build_assets.build(assets)
        build_archive.build(assets, destination / "ActiveQuestTrail.bsa")
    build_records.build(destination)
    shutil.copytree(ROOT / "Interface", destination / "Interface")
    target = destination / "SKSE/Plugins/ActiveQuestTrail.dll"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.dll, target)
    output = destination.parent / (destination.name + ".zip")
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(destination))
    print(output)


if __name__ == "__main__":
    main()
