#!/usr/bin/env python3
import hashlib
import json
import sys
from pathlib import Path

VARIANT_LABEL = {
    "android12-5.10": "Android 12 / 5.10",
    "android13-5.10": "Android 13 / 5.10",
    "android13-5.15": "Android 13 / 5.15",
    "android14-5.15": "Android 14 / 5.15",
    "android14-6.1": "Android 14 / 6.1",
    "android15-6.6": "Android 15 / 6.6",
    "android16-6.12": "Android 16 / 6.12",
    "android17-6.18": "Android 17 / 6.18",
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def main():
    root = Path(sys.argv[1]).resolve()
    provenance_path = Path(sys.argv[2]).resolve()
    zip_path = Path(sys.argv[3]).resolve()
    output = Path(sys.argv[4]).resolve()
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    props = {}
    for line in (root / "overlay" / "module" / "module.prop").read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            props[key.strip()] = value.strip()

    qualified = "android14-6.1"
    lines = [
        f"SUSFS4LKM {props['version']}（versionCode {props['versionCode']}）",
        "",
        "安装：KernelSU 管理器 → 模块 → 从本地安装 → 选择 `SUSFS4LKM-arm64.zip`，或",
        "",
        "```sh",
        "su -c 'ksud module install /sdcard/Download/SUSFS4LKM-arm64.zip'",
        "```",
        "",
        "安装后重启，用 `su -c '/data/adb/modules/susfs4ksu_unified/bin/susfsctl probe'` 验证，期望输出 `lkm v2.3.0`。",
        "说明书、支持范围与故障排查见仓库首页 README。",
        "",
        "### 本包内容",
        "",
        f"ZIP SHA256: `{digest(zip_path)}`",
        f"ZIP 大小: {zip_path.stat().st_size} 字节（SHA256SUMS / provenance 见同页附件，可用 `sha256sum -c SHA256SUMS` 校验）",
        "",
        "包含的 KMI 变体（全部由本仓库锁定源码构建）：",
        "",
    ]
    sources = provenance["artifacts"]["lkm_sources"]
    for variant in sorted(sources):
        mark = "（已在 OPD2404 真机验收）" if variant == qualified else "（仅构建与产物校验，未在对应设备启动验证）"
        lines.append(f"- {VARIANT_LABEL.get(variant, variant)}：`{variant}` {mark}")
    lines += [
        "",
        "### 已验证 / 未验证",
        "",
        f"- 已验证：`{qualified}` 变体在本项目维护设备上完成安装、冷启动、路径/kstat/open redirect/opath/sus_map/挂载前缀/模块名隐藏、JSON 契约、停用与恢复、卸载与重装等实机测试。",
        "- 未验证：其余 7 个 KMI 变体没有对应设备可启动验证；相同 KMI 不代表相同内核 ABI。",
        "- 未验证：native SUSFS 2.0~2.3 后端只有源码级 ABI 检查；WebUI 在 KernelSU 管理器内的渲染；受限网络下的自动下载更新。",
        "- 部分受限网络无法直连 github.com 下载，可用代理/镜像下载本 ZIP；KernelSU 更新器安装下载的 ZIP 时不做哈希校验，安装前请自行核对 SHA256SUMS。",
    ]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(output)


if __name__ == "__main__":
    main()
