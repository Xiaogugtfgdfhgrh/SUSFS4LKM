#!/system/bin/sh
set -eu

TARGET=/data/local/tmp/susfs-redirect-target
DST=/data/local/tmp/susfs-redirect-dst
BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl

printf ORIGINAL > "$TARGET"
printf REDIRECTED > "$DST"
before=$(su -Z u:r:untrusted_app:s0 10000 -c "cat $TARGET")
"$BIN" add_open_redirect "$TARGET" "$DST" 2
app=$(su -Z u:r:untrusted_app:s0 10000 -c "cat $TARGET")
root=$(cat "$TARGET")
"$BIN" --json '{"operation":"redirect.clear"}' >/dev/null
restored=$(su -Z u:r:untrusted_app:s0 10000 -c "cat $TARGET")

test "$before" = ORIGINAL
test "$app" = REDIRECTED
test "$root" = ORIGINAL
test "$restored" = ORIGINAL
rm -f "$TARGET" "$DST"
echo "open-redirect: PASS before=$before app=$app root=$root restored=$restored"
