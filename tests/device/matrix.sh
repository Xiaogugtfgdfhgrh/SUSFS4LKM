#!/system/bin/sh
BIN=/data/adb/modules/susfs4ksu_unified/bin/susfsctl
APP="su -Z u:r:untrusted_app:s0 10000 -c"
PASS=0
FAIL=0
SKIP=0

ck() {
    if [ "$2" = "$3" ]; then
        echo "PASS $1 got=$3"
        PASS=$((PASS + 1))
    else
        echo "FAIL $1 want=$2 got=$3"
        FAIL=$((FAIL + 1))
    fi
}

skip() {
    echo "SKIP $1 reason=$2"
    SKIP=$((SKIP + 1))
}

app() { $APP "$1"; }
json() { $BIN --json "$1" >/dev/null 2>&1; echo $?; }
appmount() { app "grep -q $1 /proc/self/mountinfo && echo visible || echo hidden"; }

echo "=== 0 backend ==="
ck probe.exit 0 "$($BIN probe >/dev/null 2>&1; echo $?)"
ck show.version 0 "$($BIN show version >/dev/null 2>&1; echo $?)"
ck show.features 0 "$($BIN show enabled_features >/dev/null 2>&1; echo $?)"
ck show.variant 0 "$($BIN show variant >/dev/null 2>&1; echo $?)"

echo "=== 1 add_sus_path ==="
T=/data/local/tmp/mx-path.txt
echo data >"$T"
ck path.before VISIBLE "$(app "test -e $T && echo VISIBLE || echo HIDDEN")"
$BIN add_sus_path "$T" >/dev/null 2>&1
ck path.app HIDDEN "$(app "test -e $T && echo VISIBLE || echo HIDDEN")"
ck path.root VISIBLE "$(test -e "$T" && echo VISIBLE || echo HIDDEN)"
ck path.delete.rc 0 "$(json '{"operation":"path.delete","values":{"path":"/data/local/tmp/mx-path.txt"}}')"
ck path.restore VISIBLE "$(app "test -e $T && echo VISIBLE || echo HIDDEN")"

echo "=== 2 add_sus_path_loop ==="
D=/data/local/tmp/mx-loop
mkdir -p "$D"
echo x >"$D/f"
$BIN add_sus_path_loop "$D" >/dev/null 2>&1
ck path_loop.app HIDDEN "$(app "test -e $D/f && echo VISIBLE || echo HIDDEN")"
ck path_loop.delete.rc 0 "$(json '{"operation":"path.delete","values":{"path":"/data/local/tmp/mx-loop"}}')"
ck path_loop.restore VISIBLE "$(app "test -e $D/f && echo VISIBLE || echo HIDDEN")"

echo "=== 3 kstat ==="
K=/data/local/tmp/mx-kstat.txt
printf A >"$K"
$BIN add_sus_kstat "$K" >/dev/null 2>&1
printf BBBBB >"$K"
$BIN update_sus_kstat_full_clone "$K" >/dev/null 2>&1
ck kstat.app 1 "$(app "stat -c %s $K")"
ck kstat.root 5 "$(stat -c %s "$K")"
ck kstat.clear.rc 0 "$(json '{"operation":"kstat.clear"}')"
ck kstat.restore 5 "$(app "stat -c %s $K")"

S=/data/local/tmp/mx-kstat-static.txt
printf A >"$S"
$BIN add_sus_kstat_statically "$S" default default default 4242 default default default default default default default default >/dev/null 2>&1
ck kstat_static.app 4242 "$(app "stat -c %s $S")"
ck kstat_static.root 1 "$(stat -c %s "$S")"
ck kstat_static.clear.rc 0 "$(json '{"operation":"kstat.clear"}')"
ck kstat_static.restore 1 "$(app "stat -c %s $S")"

echo "=== 4 open redirect ==="
RT=/data/local/tmp/mx-redir-target
RD=/data/local/tmp/mx-redir-dst
printf ORIGINAL >"$RT"
printf REDIRECTED >"$RD"
$BIN add_open_redirect "$RT" "$RD" 2 >/dev/null 2>&1
ck redirect.app REDIRECTED "$(app "cat $RT")"
ck redirect.root ORIGINAL "$(cat "$RT")"
ck redirect.clear.rc 0 "$(json '{"operation":"redirect.clear"}')"
ck redirect.restore ORIGINAL "$(app "cat $RT")"

echo "=== 5 uname spoof (spoof applies to every process) ==="
REAL=$(app "uname -r")
$BIN set_uname 9.9.9-mx-spoofed "#1 SMP MX" >/dev/null 2>&1
ck uname.app 9.9.9-mx-spoofed "$(app "uname -r")"
ck uname.root 9.9.9-mx-spoofed "$(uname -r)"
$BIN set_uname default default >/dev/null 2>&1
ck uname.restore "$REAL" "$(app "uname -r")"

echo "=== 6 cmdline/bootconfig spoof ==="
F=/data/local/tmp/mx-cmdline
printf 'mx=fake androidboot.mx=1\n' >"$F"
$BIN set_cmdline_or_bootconfig "$F" >/dev/null 2>&1
ck cmdline.set 0 "$?"
case "$(cat /proc/bootconfig 2>/dev/null)$(cat /proc/cmdline 2>/dev/null)" in
    *mx=fake*) ck cmdline.root spoofed spoofed ;;
    *) ck cmdline.root spoofed not_spoofed ;;
esac
skip cmdline.app "untrusted_app cannot read /proc/bootconfig or /proc/cmdline on this rom"

echo "=== 7 mount hiding via prefixes ==="
P=/data/local/tmp/mx-prefix
M=$P/new
mkdir -p "$M"
$BIN --json '{"operation":"mounts.add","values":{"prefix":"/data/local/tmp/mx-prefix"}}' >/dev/null 2>&1
mount -t tmpfs tmpfs "$M" 2>/dev/null
sleep 1
ck mount.app.hidden hidden "$(appmount "$M")"
ck mount.root.visible visible "$(grep -q "$M" /proc/self/mountinfo && echo visible || echo hidden)"
ck mounts.del.rc 0 "$(json '{"operation":"mounts.del","values":{"prefix":"/data/local/tmp/mx-prefix"}}')"
umount "$M" 2>/dev/null
mount -t tmpfs tmpfs "$M" 2>/dev/null
sleep 1
ck mount.new.after_del visible "$(appmount "$M")"
umount "$M" 2>/dev/null

echo "=== 8 add_sus_mount / add_try_umount are not part of the LKM interface ==="
ck sus_mount.unsupported 4 "$($BIN add_sus_mount /data/local/tmp 2>/dev/null; echo $?)"
ck try_umount.unsupported 4 "$($BIN add_try_umount /data/local/tmp 1 2>/dev/null; echo $?)"

echo "=== 9 sus_map ==="
MM=/data/local/tmp/mx-map-target-$$.so
cp /system/lib64/libc.so "$MM" 2>/dev/null || echo mapped >"$MM"
chmod 755 /data/local/tmp/susmap_probe
BEFORE=$(app "/data/local/tmp/susmap_probe $MM" | grep -o 'MAP_RESULT [A-Z]*')
ck sus_map.before MAP_RESULT_VISIBLE "$(echo "$BEFORE" | tr ' ' '_')"
$BIN add_sus_map "$MM" >/dev/null 2>&1
AFTER=$(app "/data/local/tmp/susmap_probe $MM" | grep -o 'MAP_RESULT [A-Z]*')
ck sus_map.after MAP_RESULT_HIDDEN "$(echo "$AFTER" | tr ' ' '_')"

echo "=== 10 module name hiding ==="
ck modules.root.hidden susfs_hidden "$(grep -q susfs_guard_lkm /proc/modules && echo susfs_visible || echo susfs_hidden)"
ck kallsyms.hidden susfs_hidden "$(grep -q susfs_guard_lkm /proc/kallsyms && echo susfs_visible || echo susfs_hidden)"

echo "=== 11 log / avc ==="
$BIN enable_log 1 >/dev/null 2>&1
ck log.enable 1 "$(cat /proc/susfs_enable_log 2>/dev/null)"
ck log.set.json 0 "$(json '{"operation":"log.set","values":{"enabled":0}}')"
ck log.off.value 0 "$(cat /proc/susfs_enable_log 2>/dev/null)"
ck avc.set.json 0 "$(json '{"operation":"avc.set","values":{"enabled":1}}')"
ck avc.on.first_token 1 "$(head -c 1 /proc/susfs_avc_spoof 2>/dev/null)"
ck avc.off.json 0 "$(json '{"operation":"avc.set","values":{"enabled":0}}')"
ck avc.off.first_token 0 "$(head -c 1 /proc/susfs_avc_spoof 2>/dev/null)"
ck log.back.on 0 "$(json '{"operation":"log.set","values":{"enabled":1}}')"

echo "=== 12 modules.set / mounts.set ==="
ck modules.set.one 0 "$(json '{"operation":"modules.set","values":{"items":["susfs_guard_lkm"]}}')"
ck modules.set.two 0 "$(json '{"operation":"modules.set","values":{"items":["susfs_guard_lkm","mx_probe_mod"]}}')"
ck modules.set.empty 0 "$(json '{"operation":"modules.set","values":{"items":[]}}')"
ck modules.clear.rc 0 "$(json '{"operation":"modules.clear"}')"
ck modules.add.rc 0 "$(json '{"operation":"modules.add","values":{"name":"susfs_guard_lkm"}}')"
ck modules.del.rc 0 "$(json '{"operation":"modules.del","values":{"name":"susfs_guard_lkm"}}')"
ck modules.set.restore 0 "$(json '{"operation":"modules.set","values":{"items":["susfs_guard_lkm"]}}')"
ck mounts.set.one 0 "$(json '{"operation":"mounts.set","values":{"items":["/data/adb"]}}')"
ck mounts.set.empty 0 "$(json '{"operation":"mounts.set","values":{"items":[]}}')"
ck mounts.reset.rc 0 "$(json '{"operation":"mounts.reset"}')"
ck mounts.set.restore 0 "$(json '{"operation":"mounts.set","values":{"items":["/data/adb"]}}')"

echo "=== 13 config / rules ==="
ck config.get 0 "$(json '{"operation":"config.get"}')"
ck config.set 0 "$(json '{"operation":"config.set","values":{"key":"susfs_log","value":"1"}}')"
ck config.badkey 2 "$(json '{"operation":"config.set","values":{"key":"not_a_key","value":"1"}}')"
ck rules.get 0 "$(json '{"operation":"rules.get","values":{"rule":"sus_path"}}')"
ck rules.badname 2 "$(json '{"operation":"rules.get","values":{"rule":"../etc/passwd"}}')"

echo "=== 14 error contract ==="
ck err.badjson 2 "$(json 'not json')"
ck err.trailing 2 "$(json '{"operation":"status"} trailing')"
ck err.nul 2 "$(json '{"operation":"rules.get","values":{"rule":"sus_path.txt\u0000evil"}}')"
ck err.unknownop 4 "$(json '{"operation":"definitely_not_an_operation"}')"
ck err.missingpath 2 "$(json '{"operation":"path.delete"}')"
ck err.missingvalues 2 "$(json '{"operation":"config.set"}')"
ck err.space_path 2 "$(json '{"operation":"path.delete","values":{"path":"/data/local/tmp/mx space"}}')"
ck err.dollar_quote_path 0 "$(json '{"operation":"path.delete","values":{"path":"/data/local/tmp/mx-quote\"inner\""}}')"
ck err.nonascii_path 0 "$(json '{"operation":"path.delete","values":{"path":"/data/local/tmp/路径-测试"}}')"
ck ok.status 0 "$(json '{"operation":"status"}')"

echo "=== 15 log.value ==="
ck log.final 1 "$(cat /proc/susfs_enable_log 2>/dev/null)"

echo "SUMMARY pass=$PASS fail=$FAIL skip=$SKIP"
[ "$FAIL" = 0 ]
