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

if [ -n "${SUSFS_DDK_DIGEST:-}" ]; then
    IMAGE="ghcr.io/ylarod/ddk-min@$SUSFS_DDK_DIGEST"
fi
docker pull "$IMAGE"
DIGEST=$(docker image inspect --format '{{range .RepoDigests}}{{println .}}{{end}}' "$IMAGE" | head -n 1)

mkdir -p "$WORKROOT/ko"
rm -rf "$WORK"
mkdir -p "$WORK"
mkdir -p "$WORK/src"
tar -C "$SRC" --exclude=./.git -cf - . | tar -C "$WORK/src" -xf -
sed -i 's/static __nocfi long fn(/static __nocfi __attribute__((target("branch-protection=bti"))) long fn(/' "$WORK/src/sources/lkm/kernel/susfs_kstat.c" "$WORK/src/sources/lkm/kernel/susfs_opath.c"
case "$VARIANT" in
    android16-6.12|android17-6.18) ;;
    *) python3 "$SRC/tools/patch-lkm.py" "$WORK/src" ;;
esac

docker run --rm --privileged -v "$WORK:/work" -w /work "$IMAGE" sh -c 'make -C "$KDIR" M=/work/src/sources/lkm/kernel ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 KCFLAGS="-mbranch-protection=standard" modules -j"$(nproc)"'
cp "$WORK/src/sources/lkm/kernel/susfs_guard_lkm.ko" "$OUT"
if [ -n "${SUSFS_DDK_DIGEST:-}" ]; then
    REQUESTED="\"$SUSFS_DDK_DIGEST\""
else
    REQUESTED=null
fi
printf '{\n  "variant": "%s",\n  "image": "%s",\n  "digest": "%s",\n  "requested_digest": %s\n}\n' \
    "$VARIANT" "$IMAGE" "${DIGEST#*@}" "$REQUESTED" > "$WORKROOT/ko/ddk-$VARIANT.json"
