#!/bin/sh
MODDIR=/data/adb/modules/susfs4ksu_unified
SUSFS_BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl
. ${MODDIR}/utils.sh
. ${MODDIR}/backend.sh
PERSISTENT_DIR=/data/adb/susfs4ksu_unified
tmpfolder=/data/adb/susfs4ksu_unified/state
logfile="$tmpfolder/logs/susfs.log"
logfile1="$tmpfolder/logs/susfs1.log"
backend_init || exit $?

[ -w /mnt ] && mntfolder=/mnt/susfs4ksu
[ -w /mnt/vendor ] && mntfolder=/mnt/vendor/susfs4ksu

post_fs_data=0
[ -f $tmpfolder/logs/boot_stage_time.sh ] && . $tmpfolder/logs/boot_stage_time.sh

if grep -v "#" "$PERSISTENT_DIR/sus_mount.txt" > /dev/null; then
    grep -v "#" "$PERSISTENT_DIR/sus_mount.txt" | while read -r i; do
        if [ -z "$i" ]; then
            continue
        fi
        if backend_is_lkm; then
            echo "[sus_mount skipped on lkm]: susfs4ksu/post-mount $i" >> "$logfile1"
        else
            ${SUSFS_BIN} add_sus_mount "$i" && echo "[sus_mount]: susfs4ksu/post-mount $i" >> "$logfile1"
        fi
    done
fi

if grep -v "#" "$PERSISTENT_DIR/try_umount.txt" > /dev/null; then
    grep -v "#" "$PERSISTENT_DIR/try_umount.txt" | while read -r i; do
        if [ -z "$i" ]; then
            continue
        fi
        if backend_is_lkm; then
            echo "[try_umount deferred to boot-completed]: susfs4ksu/post-mount $i" >> "$logfile1"
        else
            ${SUSFS_BIN} add_try_umount "$i" 1 && echo "[try_umount]: susfs4ksu/post-mount $i" >> "$logfile1"
        fi
    done
fi

dmesg_snapshot=$(dmesg)
echo "$dmesg_snapshot" | sed -n "/^\[ *$post_fs_data/,\$p" | grep -iE "susfs_auto_add|ksu_susfs|susfs:" >> $logfile
endmsg=$(echo "$dmesg_snapshot" | grep -E '^\[ *[0-9]' | cut -d']' -f1 | sed 's/^\[ *//' | cut -d' ' -f1 | tail -n 1)
echo "post_mount=$endmsg" >> $tmpfolder/logs/boot_stage_time.sh
