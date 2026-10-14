#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path

REPO = "https://github.com/32463991/SUSFS4LKM"


def prop(root, name):
    text = (root / "overlay" / "module" / "module.prop").read_text(encoding="utf-8")
    match = re.search(rf"^{name}=(.*)$", text, re.M)
    if not match:
        raise SystemExit(f"module.prop has no {name}")
    return match.group(1).strip()


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()
    tag = sys.argv[2] if len(sys.argv) > 2 else None
    version = prop(root, "version")
    code = int(prop(root, "versionCode"))
    update = json.loads((root / "update.json").read_text(encoding="utf-8"))

    problems = []
    if update.get("version") != version:
        problems.append("update.json version")
    if int(update.get("versionCode", -1)) != code:
        problems.append("update.json versionCode")
    if update.get("zipUrl") != f"{REPO}/releases/latest/download/SUSFS4LKM-arm64.zip":
        problems.append("update.json zipUrl")
    if prop(root, "updateJson") != f"{REPO.replace('github.com', 'raw.githubusercontent.com')}/main/update.json":
        problems.append("module.prop updateJson")
    if tag is not None and tag != version:
        problems.append(f"tag {tag}")

    if problems:
        raise SystemExit("version check failed: " + ", ".join(problems))
    print(f"VERSION_OK {version} {code}")


if __name__ == "__main__":
    main()
