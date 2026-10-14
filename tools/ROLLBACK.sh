#!/system/bin/sh
set -eu

MOD=/data/adb/modules/susfs4ksu_unified
[ -d "$MOD" ] || exit 3
touch "$MOD/disable"
sync
echo "SUSFS4LKM disabled; reboot to restore the pre-module boot state."
