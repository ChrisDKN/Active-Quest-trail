import argparse
import hashlib
import io
import json
import subprocess
import tarfile
import urllib.request
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".build-deps"
SOURCES = {
    "fmt": ("fmtlib/fmt", "1be298e1bd68957e4cd352e1f676f00e07dcfb57"),
    "spdlog": ("gabime/spdlog", "486b55554f11c9cccc913e11a87085b2a91f706f"),
    "directxtk": ("microsoft/DirectXTK", "524c655ee0b69c33f6dfec81878596b131f79671"),
    "directxmath": ("microsoft/DirectXMath", "5bbe2ceeec3236c27ad44c877715e1e0f3b9817a"),
}


def download(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def verified_download(base, name):
    destination = DEPS / name
    if not destination.exists():
        destination.write_bytes(download(base + name))
    expected = download(base + name + ".sha256").decode().split()[0]
    if hashlib.sha256(destination.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f"Checksum mismatch: {destination}")
    return destination


def sdk_manifest(xwin):
    cache = DEPS / "xwin-cache"
    subprocess.run([
        str(xwin), "--accept-license", "--manifest-version", "18",
        "--cache-dir", str(cache), "list",
    ], check=True, stdout=subprocess.DEVNULL)
    channel = json.loads((cache / "dl/manifest_18.json").read_text(encoding="utf-8-sig"))
    item = next(item for item in channel["channelItems"] if item["id"] == "Microsoft.VisualStudio.Manifests.VisualStudio")
    payload = item["payloads"][0]
    source = cache / "dl" / f"pkg_manifest_{payload['sha256']}.vsman"
    contents = source.read_bytes()
    manifest = json.loads(contents)
    for package in manifest["packages"]:
        for entry in package.get("payloads", []):
            if "url" in entry:
                entry["url"] = entry["url"].replace(" ", "%20")
    contents = json.dumps(manifest).encode()
    payload["sha256"] = hashlib.sha256(contents).hexdigest()
    payload["size"] = len(contents)
    (cache / "dl" / f"pkg_manifest_{payload['sha256']}.vsman").write_bytes(contents)
    destination = DEPS / "vs18-channel.json"
    destination.write_text(json.dumps(channel))
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--linux-sdk", action="store_true")
    args = parser.parse_args()
    DEPS.mkdir(exist_ok=True)
    for name, (repo, commit) in SOURCES.items():
        destination = DEPS / name
        if destination.exists():
            continue
        print(f"Fetching {repo} at {commit}", flush=True)
        archive = download(f"https://codeload.github.com/{repo}/tar.gz/{commit}")
        with tarfile.open(fileobj=io.BytesIO(archive)) as source:
            prefix = source.getmembers()[0].name
            source.extractall(DEPS, filter="data")
        (DEPS / prefix).rename(destination)

    simple_math = DEPS / "directxtk/Inc/SimpleMath.h"
    contents = simple_math.read_text()
    if "operator<= >" in contents:
        simple_math.write_text(contents.replace("operator<= >", "operator<=>"))

    bundle = "commonlibsse-ng-prebuilt-v11.0.0-all-msvc-cmake"
    if not (DEPS / "commonlib-prebuilt" / bundle / "lib/CommonLibSSE.lib").exists():
        archive = verified_download(
            "https://github.com/alandtse/CommonLibSSE-NG/releases/download/v11.0.0/", bundle + ".zip"
        )
        with zipfile.ZipFile(archive) as source:
            source.extractall(DEPS / "commonlib-prebuilt")

    if args.linux_sdk and not (DEPS / "xwin-v18/.complete").exists():
        name = "xwin-0.10.0-x86_64-unknown-linux-musl"
        archive = verified_download("https://github.com/Jake-Shadle/xwin/releases/download/0.10.0/", name + ".tar.gz")
        with tarfile.open(archive) as source:
            source.extractall(DEPS, filter="data")
        xwin = DEPS / name / "xwin"
        manifest = sdk_manifest(xwin)
        subprocess.run([
            str(xwin), "--accept-license", "--manifest", str(manifest), "--http-retry", "5",
            "--cache-dir", str(DEPS / "xwin-cache"), "splat", "--output", str(DEPS / "xwin-v18"),
            "--use-winsysroot-style", "--preserve-ms-arch-notation",
        ], check=True)
        (DEPS / "xwin-v18/.complete").touch()
    print("Build dependencies ready")


if __name__ == "__main__":
    main()
