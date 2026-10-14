#!/usr/bin/env python3
import argparse
import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

RELEASE = "https://github.com/inforcqb/susfs4ksu-lkm/releases/download/v2.3.0-r18.1"
MIRROR = "https://ghfast.top/"
COMMIT = "9eee87c6a18ee5c769e9f1d59f496b0eb711b7c8"
TAG = "v2.3.0-r18.1"
LKM_COMMIT = "85ef920ccf27682a2a9089b4676547f691b92828"

ASSETS = {
    "android12-5.10": "b8d1d3b0de69cc6d7dc87349eb371f953989f2daf2348ff7014e831cc277f019",
    "android13-5.10": "28d1336714256deaf36b6f24ef2938f32bea3837d245582c8b793096c938cffe",
    "android13-5.15": "c17abce3f21a9edbf8647adfb438278231da1cc52ce9243125a3f3e02f41b41a",
    "android14-5.15": "a7c33d2d8b47d274bd4e86da1c646feecfdd370d146bec27e78fbbc70ebb7a23",
    "android15-6.6": "059c51bbe06267b86b403d261c9a3c983f32aab6a981a4a6e666138ab34c16c4",
    "android16-6.12": "2ef6ba774d044c29310c1239568b520a194d88329bfcd1fe85ae1210f5076d60",
    "android17-6.18": "b013587138543cfaff9a85b5e2f4aadb7c0616200745a2275e2cae79b6a99b19",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def download(variant):
    name = f"susfs_guard_lkm-{variant}.ko"
    last = None
    for url in (f"{RELEASE}/{name}", f"{MIRROR}{RELEASE}/{name}"):
        for attempt in range(2):
            try:
                request = urllib.request.Request(url, headers={"User-Agent": "susfs4lkm-fetch"})
                with urllib.request.urlopen(request, timeout=120) as response:
                    return name, response.read()
            except Exception as error:
                last = error
                time.sleep(5 * (attempt + 1))
    raise SystemExit(f"download failed for {name}: {last}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--variants", required=True)
    parser.add_argument("--skip", action="append", default=[])
    parser.add_argument("--record", action="append", default=[])
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    variants = [line.strip() for line in Path(args.variants).read_text(encoding="utf-8").splitlines() if line.strip()]
    meta_path = out / "ko-sources.json"
    meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}

    for variant in variants:
        if variant in args.skip:
            continue
        expected = ASSETS.get(variant)
        if expected is None:
            raise SystemExit(f"no pinned upstream ko for {variant}")
        name, payload = download(variant)
        actual = digest(payload)
        if actual != expected:
            raise SystemExit(f"ko hash mismatch: {variant} {actual}")
        (out / name).write_bytes(payload)
        meta[variant] = {
            "commit": COMMIT,
            "release": TAG,
            "sha256": expected.upper(),
            "source": "upstream-release",
        }
        print(f"{variant}: {expected}")

    for variant in args.record:
        built = out / f"susfs_guard_lkm-{variant}.ko"
        if not built.is_file():
            raise SystemExit(f"missing built ko: {built}")
        meta[variant] = {
            "commit": LKM_COMMIT,
            "sha256": digest(built.read_bytes()).upper(),
            "source": "current-source",
        }
        print(f"{variant}: {meta[variant]['sha256']} (current-source)")

    meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
