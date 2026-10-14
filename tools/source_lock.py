#!/usr/bin/env python3
import hashlib
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
write = len(sys.argv) > 2 and sys.argv[2] == "--write"


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_hash(name):
    base = root / name
    h = hashlib.sha256()
    files = []
    for path in sorted(base.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if "__pycache__" in path.parts or path.suffix == ".pyc":
            continue
        digest = file_hash(path)
        files.append({"path": rel, "sha256": digest})
        h.update(rel.encode("utf-8") + b"\0" + digest.encode("ascii") + b"\n")
    return {"files": len(files), "sha256": h.hexdigest()}, files


categories = {}
entries = []
for name in ["sources", "overlay", "src", "tools", "tests"]:
    category, files = tree_hash(name)
    categories[name] = category
    entries.extend(files)
entries.append({"path": "manifest.yaml", "sha256": file_hash(root / "manifest.yaml")})

overall = hashlib.sha256()
for entry in sorted(entries, key=lambda item: item["path"]):
    overall.update(entry["path"].encode("utf-8") + b"\0" + entry["sha256"].encode("ascii") + b"\n")

result = {
    "schema": 1,
    "tree_sha256": overall.hexdigest(),
    "categories": categories,
    "files": sorted(entries, key=lambda item: item["path"]),
}
text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
lock = root / "SOURCE_LOCK.json"

if write:
    lock.write_text(text, encoding="utf-8", newline="\n")
    print(lock)
else:
    if not lock.exists() or lock.read_text(encoding="utf-8") != text:
        raise SystemExit("source lock mismatch")
    print("SOURCE_LOCK_OK " + result["tree_sha256"])
