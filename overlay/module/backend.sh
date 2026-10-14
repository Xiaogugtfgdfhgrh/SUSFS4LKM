#!/bin/sh

MODDIR=/data/adb/modules/susfs4ksu_unified
SUSFS_BIN=$MODDIR/bin/susfsctl
PERSISTENT_DIR=/data/adb/susfs4ksu_unified
STATE_DIR=$PERSISTENT_DIR/state
LOCK_DIR=$STATE_DIR/backend.lock
BACKEND_FILE=$STATE_DIR/backend
BACKEND_LOG=$STATE_DIR/backend.log
KO=

. "$MODDIR/kmi.sh"

backend_probe() {
    "$SUSFS_BIN" probe
}

backend_init() {
    mkdir -p "$STATE_DIR"
    if ! mkdir "$LOCK_DIR" 2>/dev/null; then
        i=0
        while [ $i -lt 20 ]; do
            [ ! -d "$LOCK_DIR" ] && break
            sleep 1
            i=$((i + 1))
        done
        if [ -d "$LOCK_DIR" ]; then
            owner=$(cat "$LOCK_DIR/pid" 2>/dev/null || echo 0)
            case "$owner" in *[!0-9]*|'') owner=0 ;; esac
            if [ "$owner" -gt 0 ] && kill -0 "$owner" 2>/dev/null; then
                echo "backend: lock held by live pid $owner" >> "$BACKEND_LOG"
                return 6
            fi
            rm -f "$LOCK_DIR/pid"
            rmdir "$LOCK_DIR" 2>/dev/null || return 6
        fi
        mkdir "$LOCK_DIR" 2>/dev/null || return 6
    fi
    echo $$ > "$LOCK_DIR/pid"
    trap 'if [ "$(cat "$LOCK_DIR/pid" 2>/dev/null)" = "$$" ]; then rm -f "$LOCK_DIR/pid"; rmdir "$LOCK_DIR" 2>/dev/null; fi' EXIT HUP INT TERM

    output=$(backend_probe 2>/dev/null)
    rc=$?
    if [ $rc -eq 0 ]; then
        printf '%s\n' "$output" > "$BACKEND_FILE"
        return 0
    fi
    if [ $rc -ne 3 ]; then
        echo "backend: probe failed rc=$rc" >> "$BACKEND_LOG"
        return $rc
    fi
    REL=$(cat /proc/sys/kernel/osrelease) || {
        echo "backend: cannot read kernel release" >> "$BACKEND_LOG"
        return 6
    }
    KMI=$(select_kmi "$REL" "$MODDIR") || {
        echo "backend: no packaged LKM for $REL" >> "$BACKEND_LOG"
        return 4
    }
    KO=$MODDIR/lib/susfs_guard_lkm-$KMI.ko
    if [ ! -f "$KO" ]; then
        echo "backend: missing $KO" >> "$BACKEND_LOG"
        return 7
    fi

    echo "backend: loading $KO" >> "$BACKEND_LOG"
    if [ -x "$MODDIR/bin/susfs_insmod" ]; then
        "$MODDIR/bin/susfs_insmod" "$KO" expose_proc=1 >> "$BACKEND_LOG" 2>&1
        rc=$?
    else
        /data/adb/ksu/bin/ksud insmod "$KO" expose_proc=1 >> "$BACKEND_LOG" 2>&1
        rc=$?
    fi
    if [ $rc -ne 0 ]; then
        echo "backend: load failed rc=$rc" >> "$BACKEND_LOG"
        return 7
    fi

    i=0
    while [ $i -lt 10 ]; do
        output=$(backend_probe 2>/dev/null)
        rc=$?
        if [ $rc -eq 0 ]; then
            printf '%s\n' "$output" > "$BACKEND_FILE"
            return 0
        fi
        sleep 1
        i=$((i + 1))
    done
    echo "backend: loaded module did not become ready" >> "$BACKEND_LOG"
    return 7
}

backend_version() {
    "$SUSFS_BIN" show version
}

backend_features() {
    "$SUSFS_BIN" show enabled_features
}

backend_is_lkm() {
    grep -q '^lkm ' "$BACKEND_FILE" 2>/dev/null
}
