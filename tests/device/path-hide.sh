#!/system/bin/sh
set -eu

TARGET=/data/local/tmp/susfs-behavior-target
BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl

touch "$TARGET"
before=$(su -Z u:r:untrusted_app:s0 10000 -c "test -e $TARGET && echo VISIBLE || echo HIDDEN")
"$BIN" add_sus_path "$TARGET"
after=$(su -Z u:r:untrusted_app:s0 10000 -c "test -e $TARGET && echo VISIBLE || echo HIDDEN")
root=$(test -e "$TARGET" && echo VISIBLE || echo HIDDEN)
"$BIN" --json '{"operation":"path.delete","values":{"path":"/data/local/tmp/susfs-behavior-target"}}' >/dev/null
restored=$(su -Z u:r:untrusted_app:s0 10000 -c "test -e $TARGET && echo VISIBLE || echo HIDDEN")

test "$before" = VISIBLE
test "$after" = HIDDEN
test "$root" = VISIBLE
test "$restored" = VISIBLE
rm -f "$TARGET"
echo "path-hide: PASS before=$before after=$after root=$root restored=$restored"
