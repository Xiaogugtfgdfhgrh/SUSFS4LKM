#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

EM_AARCH64 = 0xB7


def elf_machine(path):
    header = path.read_bytes()[:20]
    if header[:4] != b"\x7fELF" or header[5] != 1:
        raise SystemExit(f"not a little endian ELF: {path}")
    return struct.unpack_from("<H", header, 18)[0]


def main():
    variants_file = Path(sys.argv[1])
    ko_dir = Path(sys.argv[2])
    variants = [line.strip() for line in variants_file.read_text(encoding="utf-8").splitlines() if line.strip()]
    if not variants:
        raise SystemExit("variant list is empty")
    problems = []
    for variant in variants:
        path = ko_dir / f"susfs_guard_lkm-{variant}.ko"
        if not path.is_file():
            problems.append(f"missing {path.name}")
            continue
        if elf_machine(path) != EM_AARCH64:
            problems.append(f"{path.name}: not aarch64")
        data = path.read_bytes()
        kernel = variant.split("-", 1)[1]
        if f"vermagic={kernel}.".encode() not in data:
            problems.append(f"{path.name}: vermagic does not start with {kernel}.")
        if b"name=susfs_guard_lkm" not in data:
            problems.append(f"{path.name}: module name missing")
    print("KO_PROBLEMS", problems)
    if problems:
        sys.exit(1)
    print("KO_CHECK_OK", len(variants))


if __name__ == "__main__":
    main()
