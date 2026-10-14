#!/bin/sh

if [ -z "$KSU" ]; then
    abort "[!] SUSFS4LKM requires KernelSU"
fi

PERSISTENT_DIR=/data/adb/susfs4ksu_unified
STATE_DIR=$PERSISTENT_DIR/state
mkdir -p $PERSISTENT_DIR $STATE_DIR

files="sus_mount.txt try_umount.txt sus_path.txt sus_path_loop.txt sus_maps.txt sus_open_redirect.txt legit_mounts.txt sus_kstat_statically.json config.sh"
for i in $files; do
    if [ ! -f $PERSISTENT_DIR/$i ]; then
        cp $MODPATH/$i $PERSISTENT_DIR/$i
    fi
    rm -f $MODPATH/$i
done

chmod 755 $MODPATH/bin/susfsctl
[ -f $MODPATH/bin/susfs_insmod ] && chmod 755 $MODPATH/bin/susfs_insmod
chmod 644 $MODPATH/post-fs-data.sh $MODPATH/late-load.sh $MODPATH/post-mount.sh $MODPATH/service.sh $MODPATH/boot-completed.sh $MODPATH/action.sh $MODPATH/uninstall.sh $MODPATH/susfs_reset.sh $MODPATH/backend.sh

ui_print "[-] SUSFS4LKM installed"
rm -f $MODPATH/customize.sh
