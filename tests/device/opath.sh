#!/system/bin/sh
set -eu

TARGET=/data/local/tmp/susfs-opath-target
BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl
TEST=/data/local/tmp/opath_test

touch "$TARGET"
chcon u:object_r:system_file:s0 "$TEST"
before=$(su -Z u:r:untrusted_app:s0 10000 -c "$TEST $TARGET" 2>&1 || true)
"$BIN" add_sus_path "$TARGET"
after=$(su -Z u:r:untrusted_app:s0 10000 -c "$TEST $TARGET" 2>&1 || true)
root=$("$TEST" "$TARGET" 2>&1 || true)
"$BIN" --json '{"operation":"path.delete","values":{"path":"/data/local/tmp/susfs-opath-target"}}' >/dev/null
restored=$(su -Z u:r:untrusted_app:s0 10000 -c "$TEST $TARGET" 2>&1 || true)

case "$before" in OPEN_OK*) ;; *) exit 1;; esac
case "$after" in *"No such file"*) ;; *) exit 1;; esac
case "$root" in OPEN_OK*) ;; *) exit 1;; esac
case "$restored" in OPEN_OK*) ;; *) exit 1;; esac
rm -f "$TARGET"
echo "opath: PASS before=$before after=$after root=$root restored=$restored"
