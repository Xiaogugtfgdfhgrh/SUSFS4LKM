#!/system/bin/sh
set -eu

TARGET=/data/local/tmp/susfs-kstat-full-target
BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl

printf A > "$TARGET"
before_size=$(su -Z u:r:untrusted_app:s0 10000 -c "stat -c %s $TARGET")
"$BIN" add_sus_kstat "$TARGET"
"$BIN" --json '{"operation":"kstat.full_clone","values":{"path":"/data/local/tmp/susfs-kstat-full-target"}}' >/dev/null
printf BBBBB > "$TARGET"
app_size=$(su -Z u:r:untrusted_app:s0 10000 -c "stat -c %s $TARGET")
root_size=$(stat -c %s "$TARGET")
"$BIN" --json '{"operation":"kstat.clear"}' >/dev/null
restored_size=$(su -Z u:r:untrusted_app:s0 10000 -c "stat -c %s $TARGET")

test "$app_size" = "$before_size"
test "$root_size" != "$before_size"
test "$restored_size" = "$root_size"
rm -f "$TARGET"
echo "kstat-full: PASS before=$before_size app=$app_size root=$root_size restored=$restored_size"
