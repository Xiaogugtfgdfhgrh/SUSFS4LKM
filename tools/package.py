#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import zipfile
from pathlib import Path

FIXED_TIME = (2026, 1, 1, 0, 0, 0)
EXCLUDED = {".gitignore", "README.md", "CHANGELOG.md", "update.json", "susfs-bin-check.sh", "susfs-bin-update.sh"}


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_prop(path):
    values = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key.strip()] = value.strip()
    return values


def reset_tree(path, build_root):
    target = Path(path).resolve()
    root = Path(build_root).resolve()
    if root not in target.parents:
        raise RuntimeError(f"refusing to remove outside build root: {target}")
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)


def copy_base(source, target):
    for path in sorted(Path(source).rglob("*")):
        rel = path.relative_to(source)
        if rel.parts and rel.parts[0] == "tools":
            continue
        if ".github" in rel.parts:
            continue
        if rel.name in EXCLUDED:
            continue
        dest = Path(target) / rel
        if path.is_dir():
            dest.mkdir(parents=True, exist_ok=True)
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)


def copy_overlay(source, target):
    root = Path(source)
    if not root.exists():
        return
    for path in sorted(root.rglob("*")):
        if path.is_file():
            dest = Path(target) / path.relative_to(root)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)


def set_mode(path, executable=False):
    os.chmod(path, 0o755 if executable else 0o644)


def normalize_text(path):
    if path.suffix not in {".sh", ".prop", ".txt", ".json", ".html", ".xml", ".css", ".js", ".md"}:
        return
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


def make_zip(stage, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        members = [path for path in Path(stage).rglob("*") if path.is_file()]
        for path in sorted(members, key=lambda item: (item.relative_to(stage).as_posix().casefold(), item.relative_to(stage).as_posix())):
            if not path.is_file():
                continue
            rel = path.relative_to(stage).as_posix()
            executable = rel.startswith("bin/") or rel.endswith(".sh")
            info = zipfile.ZipInfo(rel, FIXED_TIME)
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = ((stat.S_IFREG | (0o755 if executable else 0o644)) << 16)
            zf.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--ko-dir", required=True)
    parser.add_argument("--variant-list", required=True)
    parser.add_argument("--webroot", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    build_root = Path(args.build_root).resolve()
    props = read_prop(root / "overlay" / "module" / "module.prop")
    stage = build_root / "stage"
    reset_tree(stage, build_root)
    copy_base(root / "sources" / "module", stage)
    copy_overlay(root / "overlay" / "module", stage)

    bin_dir = stage / "bin"
    lib_dir = stage / "lib"
    bin_dir.mkdir(parents=True, exist_ok=True)
    lib_dir.mkdir(parents=True, exist_ok=True)
    variants = [line.strip() for line in Path(args.variant_list).read_text(encoding="utf-8").splitlines() if line.strip()]
    if not variants:
        raise RuntimeError("variant list is empty")
    lkm_hashes = {}
    for variant in variants:
        source = Path(args.ko_dir) / f"susfs_guard_lkm-{variant}.ko"
        if not source.is_file():
            raise RuntimeError(f"missing LKM variant: {source}")
        target = lib_dir / source.name
        shutil.copy2(source, target)
        lkm_hashes[variant] = digest(source)
    (stage / "kmis").write_text("\n".join(variants) + "\n", encoding="utf-8", newline="\n")
    source_meta_path = Path(args.ko_dir) / "ko-sources.json"
    if not source_meta_path.is_file():
        raise RuntimeError(f"missing ko provenance: {source_meta_path}")
    lkm_sources = json.loads(source_meta_path.read_text(encoding="utf-8"))
    if set(lkm_sources) != set(variants):
        raise RuntimeError("ko provenance does not match the variant list")
    ddk_images = {}
    for path in sorted((Path(args.ko_dir)).glob("ddk-*.json")):
        image = json.loads(path.read_text(encoding="utf-8"))
        ddk_images[image["variant"]] = {"image": image["image"], "digest": image["digest"]}
    shutil.copy2(build_root / "susfsctl", bin_dir / "susfsctl")
    shutil.copy2(build_root / "susfs_insmod", bin_dir / "susfs_insmod")
    set_mode(bin_dir / "susfsctl", True)
    set_mode(bin_dir / "susfs_insmod", True)

    webroot = stage / "webroot"
    if webroot.exists():
        shutil.rmtree(webroot)
    shutil.copytree(args.webroot, webroot)

    for path in stage.rglob("*"):
        if path.is_file() and path.suffix in {".sh", ".prop", ".txt", ".json", ".html", ".xml", ".css", ".js", ".md"}:
            normalize_text(path)
        if path.is_file() and path.suffix in {".sh", ".prop"}:
            set_mode(path, path.suffix == ".sh")
    set_mode(stage / "module.prop", False)

    output = Path(args.output).resolve()
    make_zip(stage, output)
    members = {}
    with zipfile.ZipFile(output) as zf:
        for info in sorted(zf.infolist(), key=lambda x: x.filename):
            members[info.filename] = hashlib.sha256(zf.read(info.filename)).hexdigest()
    provenance = {
        "module": props["id"],
        "version": props["version"],
        "versionCode": int(props["versionCode"]),
        "zip_sha256": digest(output),
        "manifest_sha256": digest(root / "manifest.yaml"),
        "source_tree_sha256": json.loads((root / "SOURCE_LOCK.json").read_text(encoding="utf-8"))["tree_sha256"],
        "artifacts": {
            "susfsctl_sha256": digest(build_root / "susfsctl"),
            "susfs_insmod_sha256": digest(build_root / "susfs_insmod"),
            "lkm_sha256": lkm_hashes,
            "lkm_sources": lkm_sources,
            "ddk_images": ddk_images,
            "lkm_patch_sha256": digest(root / "tools" / "patch-lkm.py"),
            "lkm_build_script_sha256": digest(root / "tools" / "build-lkm.sh"),
        },
        "build": {
            "python": sys.version.split()[0],
            "ddk_image_template": "ghcr.io/ylarod/ddk-min:<variant>-20260828",
            "lkm_compile_flags": "-mbranch-protection=standard",
            "lkm_variants": variants,
        },
        "members": members,
    }
    provenance_path = output.with_name(output.stem + ".provenance.json")
    provenance_path.write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(output)
    print(provenance_path)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"pack failed: {exc}", file=sys.stderr)
        sys.exit(1)
