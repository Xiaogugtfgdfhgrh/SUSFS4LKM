#!/usr/bin/env python3
import sys
import zipfile
from pathlib import Path

zip_path = Path(sys.argv[1])
variants = [line.strip() for line in Path(sys.argv[2]).read_text(encoding="utf-8").splitlines() if line.strip()]

with zipfile.ZipFile(zip_path) as archive:
    names = set(archive.namelist())
    required = {
        "module.prop",
        "backend.sh",
        "late-load.sh",
        "kmis",
        "bin/susfsctl",
        "bin/susfs_insmod",
        "webroot/advanced.html",
    } | {"lib/susfs_guard_lkm-%s.ko" % variant for variant in variants}
    missing = required - names
    forbidden = [
        name
        for name in names
        if "susfs-bin-update" in name or "susfs-bin-check" in name or name.startswith("tools/")
    ]
    prop = archive.read("module.prop").decode("utf-8")
    problems = []
    if b"name=SUSFS4LKM" not in prop.encode("utf-8"):
        problems.append("module name")
    if b"author=" not in prop.encode("utf-8"):
        problems.append("author")
    if b"updateJson=https://raw.githubusercontent.com/32463991/SUSFS4LKM/main/update.json" not in prop.encode("utf-8"):
        problems.append("updateJson")
    modes = {name: oct((archive.getinfo(name).external_attr >> 16) & 0o777) for name in names}
    bad_modes = [
        name
        for name, mode in modes.items()
        if (mode != "0o755" and (name.endswith(".sh") or name.startswith("bin/")))
        or (mode != "0o644" and not name.endswith(".sh") and not name.startswith("bin/"))
    ]
    suffixes = (".sh", ".prop", ".txt", ".json", ".html", ".xml", ".css", ".js", ".md")
    crlf = [name for name in names if name.endswith(suffixes) and b"\r\n" in archive.read(name)]

print("missing", sorted(missing))
print("forbidden", sorted(forbidden))
print("problems", problems)
print("bad_modes", sorted(bad_modes))
print("crlf", sorted(crlf))
if missing or forbidden or problems or bad_modes or crlf:
    sys.exit(1)
print("ZIP_LAYOUT_OK")
