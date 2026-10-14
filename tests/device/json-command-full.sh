#!/system/bin/sh
set -eu

TARGET=/data/local/tmp/susfs-json-command-full-target
BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl

printf A > "$TARGET"
"$BIN" add_sus_kstat "$TARGET" >/dev/null
"$BIN" --json '{"operation":"command","values":{"argv":["update_sus_kstat_full_clone","/data/local/tmp/susfs-json-command-full-target"]}}' >/dev/null
printf BBBBB > "$TARGET"
app_size=$(su -Z u:r:untrusted_app:s0 10000 -c "stat -c %s $TARGET")
root_size=$(stat -c %s "$TARGET")
"$BIN" --json '{"operation":"kstat.clear"}' >/dev/null

test "$app_size" = 1
test "$root_size" = 5
rm -f "$TARGET"
echo "json-command-full: PASS app=$app_size root=$root_size"
