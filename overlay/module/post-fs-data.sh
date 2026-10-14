#!/bin/sh
MODDIR=/data/adb/modules/susfs4ksu_unified
SUSFS_BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl
. ${MODDIR}/utils.sh
. ${MODDIR}/backend.sh
PERSISTENT_DIR=/data/adb/susfs4ksu_unified
tmpfolder=/data/adb/susfs4ksu_unified/state
mkdir -p $tmpfolder/logs
mkdir -p $tmpfolder
logfile="$tmpfolder/logs/susfs.log"
logfile1="$tmpfolder/logs/susfs1.log"
backend_init || exit $?
susfs_features=$(${SUSFS_BIN} show enabled_features)
version=$(${SUSFS_BIN} show version)
SUSFS_DECIMAL_MAIN=$(echo "$version" | sed 's/^v//;' | cut -d'.' -f1)
SUSFS_DECIMAL_SUB=$(echo "$version" | sed 's/^v//;' | cut -d'.' -f2)
SUSFS_DECIMAL_PATCH=$(echo "$version" | sed 's/^v//;' | cut -d'.' -f3)

[ -w /mnt ] && mntfolder=/mnt/susfs4ksu
[ -w /mnt/vendor ] && mntfolder=/mnt/vendor/susfs4ksu
mkdir -p $mntfolder

if [ -n "$susfs_features" ]; then
	touch $tmpfolder/logs/susfs_active
else
	dmesg | grep -q "susfs:" > /dev/null && touch $tmpfolder/logs/susfs_active || rm -f $tmpfolder/logs/susfs_active
fi

[ -f $PERSISTENT_DIR/susfs_force_override ] && touch $tmpfolder/logs/susfs_active

force_hide_lsposed=0
spoof_uname=0
umount_for_zygote_iso_service=0
avc_log_spoofing=0
hide_sus_mnts_for_all_or_non_su_procs=0
[ -f $PERSISTENT_DIR/config.sh ] && . $PERSISTENT_DIR/config.sh

echo "susfs4ksu/post-fs-data: [logging_initialized]" > $logfile1

[ "$SUSFS_DECIMAL_MAIN" -ge 2 ] && [ -f /data/adb/susfs4ksu_unified/state/using_old_sus_path_layout ] && {
	echo "susfs4ksu/post-fs-data: Detected old sus path layout, removing the cache file for rechecking old and new sus_path layout" >> $logfile1
	rm -f /data/adb/susfs4ksu_unified/state/using_old_sus_path_layout
}

[ "$SUSFS_DECIMAL_MAIN" -ge 2 ] && { [ $hide_sus_mnts_for_all_or_non_su_procs -ge 1 ] && {
	${SUSFS_BIN} hide_sus_mnts_for_all_procs 1 >/dev/null && echo "[hide_sus_mnts_for_all_procs = 1]: susfs4ksu/post-fs-data" || {
		${SUSFS_BIN} hide_sus_mnts_for_non_su_procs 1 >/dev/null && echo "[hide_sus_mnts_for_non_su_procs = 1]: susfs4ksu/post-fs-data";
	}
} || {
	${SUSFS_BIN} hide_sus_mnts_for_all_procs 0 >/dev/null && echo "[hide_sus_mnts_for_all_procs = 0]: susfs4ksu/post-fs-data" || {
		${SUSFS_BIN} hide_sus_mnts_for_non_su_procs 0 >/dev/null && echo "[hide_sus_mnts_for_non_su_procs = 0]: susfs4ksu/post-fs-data";
	}
}; } >> $logfile1

[ $avc_log_spoofing = 1 ] && ${SUSFS_BIN} enable_avc_log_spoofing 1

[ $spoof_uname = 2 ] && spoof_uname
enable_sus_su_mode_1(){

  rm -rf ${MODDIR}/system 2>/dev/null
	if ! ${SUSFS_BIN} sus_su 1; then
		sed -i "s/^sus_su=.*/sus_su=-1/" ${PERSISTENT_DIR}/config.sh
		return
	fi
	sed -i "s/^sus_su=.*/sus_su=1/" ${PERSISTENT_DIR}/config.sh
	sed -i "s/^sus_su_active=.*/sus_su_active=1/" ${PERSISTENT_DIR}/config.sh
	mkdir -p ${MODDIR}/system/bin 2>/dev/null
	cp -f /data/adb/ksu/bin/sus_su ${MODDIR}/system/bin/su
	cp -f /data/adb/ksu/bin/sus_su_drv_path ${MODDIR}/system/bin/sus_su_drv_path
	echo 1 > ${MODDIR}/sus_su_mode
	return
}

if [ $force_hide_lsposed = 1 ] && echo "$susfs_features" | grep -q "CONFIG_KSU_SUSFS_TRY_UMOUNT"; then 
	echo "susfs4ksu/post-fs-data: [force_hide_lsposed]" >> $logfile1
	${SUSFS_BIN} add_try_umount /system/apex/com.android.art/bin/dex2oat 1
	${SUSFS_BIN} add_try_umount /system/apex/com.android.art/bin/dex2oat32 1
	${SUSFS_BIN} add_try_umount /system/apex/com.android.art/bin/dex2oat64 1
	${SUSFS_BIN} add_try_umount /apex/com.android.art/bin/dex2oat 1
	${SUSFS_BIN} add_try_umount /apex/com.android.art/bin/dex2oat32 1
	${SUSFS_BIN} add_try_umount /apex/com.android.art/bin/dex2oat64 1
fi

[ $umount_for_zygote_iso_service = 1 ] && {
	if backend_is_lkm; then
		echo "susfs4ksu/post-fs-data: [umount_for_zygote_iso_service skipped on lkm]" >> $logfile1
	else
		${SUSFS_BIN} umount_for_zygote_iso_service 1 && echo "susfs4ksu/post-fs-data: [umount_for_zygote_iso_service]" >> $logfile1
	fi
}


dmesg_snapshot=$(dmesg)
echo "$dmesg_snapshot" | grep -iE "susfs_auto_add|ksu_susfs|susfs:" > $logfile
endmsg=$(echo "$dmesg_snapshot" | grep -E '^\[ *[0-9]' | cut -d']' -f1 | sed 's/^\[ *//' | cut -d' ' -f1 | tail -n 1)
echo "post_fs_data=$endmsg" > $tmpfolder/logs/boot_stage_time.sh
