#!/bin/sh
set -eu

SRC=${1:?source root required}
WORKROOT=${2:?build root required}
VARIANT=${3:?variant required}
IMAGE=ghcr.io/ylarod/ddk-min:$VARIANT-20260828
WORK=$WORKROOT/build-$VARIANT
OUT=$WORKROOT/ko/susfs_guard_lkm-$VARIANT.ko

: "${SUSFS_BUILD_ROOT:?set SUSFS_BUILD_ROOT to the build root}"
[ "$SUSFS_BUILD_ROOT" = "$WORKROOT" ] || { echo "refusing build root: $WORKROOT" >&2; exit 1; }

mkdir -p "$WORKROOT/ko"
rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$SRC" "$WORK/src"
sed -i 's/static __nocfi long fn(/static __nocfi __attribute__((target("branch-protection=bti"))) long fn(/' "$WORK/src/sources/lkm/kernel/susfs_kstat.c" "$WORK/src/sources/lkm/kernel/susfs_opath.c"
case "$VARIANT" in
    android16-6.12|android17-6.18) ;;
    *) python3 "$SRC/tools/patch-lkm.py" "$WORK/src" ;;
esac

docker run --rm --privileged -v "$WORK:/work" -w /work "$IMAGE" sh -c 'make -C "$KDIR" M=/work/src/sources/lkm/kernel ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 KCFLAGS="-mbranch-protection=standard" modules -j"$(nproc)"'
cp "$WORK/src/sources/lkm/kernel/susfs_guard_lkm.ko" "$OUT"
