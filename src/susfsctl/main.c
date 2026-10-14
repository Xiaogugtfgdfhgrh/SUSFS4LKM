
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/sysmacros.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <utime.h>

#include "cJSON.h"

#define TAG "susfsctl"
#define MODULE_ROOT "/data/adb/modules/susfs4ksu_unified"
#define PERSISTENT_DIR "/data/adb/susfs4ksu_unified"
#define STATE_DIR "/data/adb/susfs4ksu_unified/state"

#define KERNEL_SU_OPTION    0xDEADBEEF

#define KSU_INSTALL_MAGIC1  0xDEADBEEF
#define SUSFS_MAGIC         0xFAFAFAFA

#define CMD_SUSFS_ADD_SUS_PATH                   0x55550
#define CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH     0x55551
#define CMD_SUSFS_SET_SDCARD_ROOT_PATH           0x55552
#define CMD_SUSFS_ADD_SUS_PATH_LOOP              0x55553
#define CMD_SUSFS_ADD_SUS_MOUNT                  0x55560
#define CMD_SUSFS_HIDE_SUS_MNTS                  0x55561
#define CMD_SUSFS_UMOUNT_FOR_ZYGOTE_ISO_SERVICE  0x55562
#define CMD_SUSFS_ADD_SUS_KSTAT                  0x55570
#define CMD_SUSFS_UPDATE_SUS_KSTAT               0x55571
#define CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY       0x55572
#define CMD_SUSFS_ADD_TRY_UMOUNT                 0x55580
#define CMD_SUSFS_SET_UNAME                      0x55590
#define CMD_SUSFS_ENABLE_LOG                     0x555a0
#define CMD_SUSFS_SET_BOOTCONFIG                 0x555b0
#define CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG      0x555b0
#define CMD_SUSFS_ADD_OPEN_REDIRECT              0x555c0
#define CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS  0x555d0
#define CMD_SUSFS_SHOW_VERSION                   0x555e1
#define CMD_SUSFS_SHOW_ENABLED_FEATURES          0x555e2
#define CMD_SUSFS_SHOW_VARIANT                   0x555e3
#define CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE       0x555e4
#define CMD_SUSFS_IS_SUS_SU_READY                0x555f0
#define CMD_SUSFS_SUS_SU                         0x60000
#define CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING        0x60010
#define CMD_SUSFS_ADD_SUS_MAP                    0x60020

#define KSTAT_SPOOF_INO (1 << 0)
#define KSTAT_SPOOF_DEV (1 << 1)
#define KSTAT_SPOOF_NLINK (1 << 2)
#define KSTAT_SPOOF_SIZE (1 << 3)
#define KSTAT_SPOOF_ATIME_TV_SEC (1 << 4)
#define KSTAT_SPOOF_ATIME_TV_NSEC (1 << 5)
#define KSTAT_SPOOF_MTIME_TV_SEC (1 << 6)
#define KSTAT_SPOOF_MTIME_TV_NSEC (1 << 7)
#define KSTAT_SPOOF_CTIME_TV_SEC (1 << 8)
#define KSTAT_SPOOF_CTIME_TV_NSEC (1 << 9)
#define KSTAT_SPOOF_BLOCKS (1 << 10)
#define KSTAT_SPOOF_BLKSIZE (1 << 11)
#define KSTAT_AUTO_SPOOF (KSTAT_SPOOF_INO | KSTAT_SPOOF_DEV | KSTAT_SPOOF_ATIME_TV_SEC | KSTAT_SPOOF_ATIME_TV_NSEC | \
		    KSTAT_SPOOF_MTIME_TV_SEC | KSTAT_SPOOF_MTIME_TV_NSEC | KSTAT_SPOOF_CTIME_TV_SEC | KSTAT_SPOOF_CTIME_TV_NSEC | \
		    KSTAT_SPOOF_BLKSIZE | KSTAT_SPOOF_BLOCKS)
#define KSTAT_AUTO_SPOOF_FULL_CLONE (KSTAT_AUTO_SPOOF | KSTAT_SPOOF_NLINK | KSTAT_SPOOF_SIZE)


#define ERR_v2000_CMD_NOT_SUPPORTED  255

#define SUS_PATH_LAYOUT_CACHE_DIR     STATE_DIR
#define SUS_PATH_LAYOUT_OLD_MARKER    STATE_DIR "/using_old_sus_path_layout"

#define SUSFS_MAX_LEN_PATHNAME   256
#ifndef __NEW_UTS_LEN
#define __NEW_UTS_LEN            64
#endif

#define SUS_SU_DISABLED       0
#define SUS_SU_WITH_OVERLAY   1
#define SUS_SU_WITH_HOOKS     2
#define SUS_SU_CONF_FILE_PATH "/data/adb/ksu/bin/sus_su_drv_path"


#define ABI_UNKNOWN  0
#define ABI_PRCTL    1
#define ABI_v2000     2

#define PROBE_UNKNOWN      0
#define PROBE_NATIVE       1
#define PROBE_LKM          2
#define PROBE_NONE         3
#define PROBE_UNSUPPORTED  4
#define PROBE_PERMISSION   5
#define PROBE_PROTOCOL     6

static int g_abi     = ABI_UNKNOWN;
static int g_version = 0;
static char *g_version_string = NULL;
static int g_backend_state = PROBE_UNKNOWN;
static int g_last_errno = 0;

#define V2000_SUS_PATH_LAYOUT_UNKNOWN 0
#define V2000_SUS_PATH_LAYOUT_OLD     1
#define V2000_SUS_PATH_LAYOUT_NEW     2
static int g_v2000_sus_path_layout = V2000_SUS_PATH_LAYOUT_UNKNOWN;

#define HAVE(v)  (g_version >= (v))



struct sus_path_v1 {
    unsigned long   target_ino;
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
};

struct sus_path_v154 {
    unsigned long   target_ino;
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned int    i_uid;
};

struct sus_mount_v1 {
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned long   target_dev;
};

struct sus_kstat_v1 {
    bool                is_statically;
    unsigned long       target_ino;
    char                target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned long       spoofed_ino;
    unsigned long       spoofed_dev;
    unsigned int        spoofed_nlink;
    long long           spoofed_size;
    long                spoofed_atime_tv_sec;
    long                spoofed_mtime_tv_sec;
    long                spoofed_ctime_tv_sec;
    long                spoofed_atime_tv_nsec;
    long                spoofed_mtime_tv_nsec;
    long                spoofed_ctime_tv_nsec;
    unsigned long       spoofed_blksize;
    unsigned long long  spoofed_blocks;
};

struct try_umount_v1 {
    char    target_pathname[SUSFS_MAX_LEN_PATHNAME];
    int     mnt_mode;
};

struct uname_v1 {
    char release[__NEW_UTS_LEN+1];
    char version[__NEW_UTS_LEN+1];
};

struct open_redirect_v1 {
    unsigned long   target_ino;
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    char            redirected_pathname[SUSFS_MAX_LEN_PATHNAME];
};

struct sus_su_v152 {
    int     mode;
    char    drv_path[256];
    int     maj_dev_num;
};

struct sus_su_v153 {
    int     mode;
};

struct sus_map_v1 {
    char    target_pathname[SUSFS_MAX_LEN_PATHNAME];
};


struct sus_path_v2000 {
    unsigned long   target_ino;
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned int    i_uid;
    int             err;
};

struct sus_path_v2000_new {
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    int             err;
};

static void load_sus_path_layout_cache(void) {
    if (g_abi != ABI_v2000)
        return;
    if (access(SUS_PATH_LAYOUT_OLD_MARKER, F_OK) == 0)
        g_v2000_sus_path_layout = V2000_SUS_PATH_LAYOUT_OLD;
}

static void persist_old_sus_path_layout_cache(void) {
    int fd;
    (void)mkdir(SUS_PATH_LAYOUT_CACHE_DIR, 0755);
    fd = open(SUS_PATH_LAYOUT_OLD_MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open(layout_cache)");
        return;
    }
    close(fd);
}

static void clear_old_sus_path_layout_cache(void) {
    (void)unlink(SUS_PATH_LAYOUT_OLD_MARKER);
}

struct external_dir_v2000 {
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    bool            is_inited;
    int             cmd;
    int             err;
};

struct sus_mount_v2000 {
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned long   target_dev;
    int             err;
};

struct hide_sus_mnts_v2000 {
    bool            enabled;
    int             err;
};

struct umount_zygote_v2000 {
    bool            enabled;
    int             err;
};

struct sus_kstat_v2000 {
    bool                is_statically;
    unsigned long       target_ino;
    char                target_pathname[SUSFS_MAX_LEN_PATHNAME];
    unsigned long       spoofed_ino;
    unsigned long       spoofed_dev;
    unsigned int        spoofed_nlink;
    long long           spoofed_size;
    long                spoofed_atime_tv_sec;
    long                spoofed_mtime_tv_sec;
    long                spoofed_ctime_tv_sec;
    long                spoofed_atime_tv_nsec;
    long                spoofed_mtime_tv_nsec;
    long                spoofed_ctime_tv_nsec;
    unsigned long       spoofed_blksize;
    unsigned long long  spoofed_blocks;
    int                 err;
};

struct sus_kstat_v2100 {
    bool                    is_statically;
	unsigned long           target_ino;
	char                    target_pathname[SUSFS_MAX_LEN_PATHNAME];
	unsigned long           spoofed_ino;
	unsigned long           spoofed_dev;
	unsigned int            spoofed_nlink;
	long long               spoofed_size;
	long                    spoofed_atime_tv_sec;
	unsigned long           spoofed_atime_tv_nsec;
	long                    spoofed_mtime_tv_sec;
	unsigned long           spoofed_mtime_tv_nsec;
	long                    spoofed_ctime_tv_sec;
	unsigned long           spoofed_ctime_tv_nsec;
	long long               spoofed_blocks;
	long                    spoofed_blksize;
	int                     flags;
	int                     err;
};

struct try_umount_v2000 {
    char    target_pathname[SUSFS_MAX_LEN_PATHNAME];
    int     mnt_mode;
    int     err;
};

struct uname_v2000 {
    char    release[__NEW_UTS_LEN+1];
    char    version[__NEW_UTS_LEN+1];
    int     err;
};

struct log_v2000 {
    bool    enabled;
    int     err;
};

struct spoof_cmdline_v2000 {
    char    fake_cmdline_or_bootconfig[8192];
    int     err;
};

struct open_redirect_v2000 {
    unsigned long   target_ino;
    char            target_pathname[SUSFS_MAX_LEN_PATHNAME];
    char            redirected_pathname[SUSFS_MAX_LEN_PATHNAME];
    int             err;
};

enum UID_SCHEME {
	UID_NON_APP_PROC = 0,
	UID_ROOT_PROC_EXCEPT_SU_PROC,
	UID_NON_SU_PROC,
	UID_UMOUNTED_APP_PROC,
	UID_UMOUNTED_PROC,
};

struct open_redirect_v2100 {
	char                    target_pathname[SUSFS_MAX_LEN_PATHNAME];
	char                    redirected_pathname[SUSFS_MAX_LEN_PATHNAME];
	int                     uid_scheme;
	int                     err;
};

struct sus_map_v2000 {
    char    target_pathname[SUSFS_MAX_LEN_PATHNAME];
    int     err;
};

struct avc_log_v2000 {
    bool    enabled;
    int     err;
};

struct enabled_features_v2000 {
    char    enabled_features[8192];
    int     err;
};

struct variant_v2000 {
    char    susfs_variant[16];
    int     err;
};

struct version_v2000 {
    char    susfs_version[16];
    int     err;
};

_Static_assert(sizeof(struct sus_kstat_v2100) == 376, "sus_kstat_v2100 size");
_Static_assert(offsetof(struct sus_kstat_v2100, flags) == 368, "sus_kstat_v2100 flags");
_Static_assert(offsetof(struct sus_kstat_v2100, err) == 372, "sus_kstat_v2100 err");
_Static_assert(sizeof(struct open_redirect_v2100) == 520, "open_redirect_v2100 size");
_Static_assert(offsetof(struct open_redirect_v2100, uid_scheme) == 512, "open_redirect_v2100 uid_scheme");
_Static_assert(offsetof(struct open_redirect_v2100, err) == 516, "open_redirect_v2100 err");
_Static_assert(sizeof(struct open_redirect_v2000) == 528, "open_redirect_v2000 size");
_Static_assert(offsetof(struct open_redirect_v2000, err) == 520, "open_redirect_v2000 err");


static int prctl_cmd(unsigned long cmd, void *arg) {
    int error = -1;
    prctl(KERNEL_SU_OPTION, cmd, arg, NULL, &error);
    return error;
}

static int prctl_cmd_scalar(unsigned long cmd, unsigned long val) {
    int error = -1;
    prctl(KERNEL_SU_OPTION, cmd, val, NULL, &error);
    return error;
}

static int prctl_cmd4(unsigned long cmd, void *arg3, unsigned long arg4) {
    int error = -1;
    prctl(KERNEL_SU_OPTION, cmd, arg3, (void *)arg4, &error);
    return error;
}

static void v2000_cmd(unsigned long cmd, void *info) {
    syscall(SYS_reboot, KSU_INSTALL_MAGIC1, SUSFS_MAGIC, cmd, info);
}

static void prt_not_supported(unsigned long cmd, int err) {
    bool unsupported = (g_abi == ABI_v2000)
                       ? (err == ERR_v2000_CMD_NOT_SUPPORTED)
                       : (err == -1);
    if (unsupported)
        printf("[-] CMD: '0x%lx', SUSFS operation not supported, please enable it in kernel\n", cmd);
}


static int parse_version_string(const char *s) {
    int ma = 0, mi = 0, pa = 0, used = 0;
    if (sscanf(s, "v%d.%d.%d%n", &ma, &mi, &pa, &used) != 3)
        return 0;
    if (s[used] != '\0' && s[used] != '-' && s[used] != '+')
        return 0;
    if ((ma == 2) && (mi >= 1) && (mi <= 3)) return 2100;
    if ((ma == 2) && (mi == 0)) return 2000;
    if (ma == 1) return 1500;
    if (ma > 2) return ma * 1000 + mi * 10;
    return 0;
}

static bool kallsyms_has_susfs(void) {
    FILE *f = fopen("/proc/kallsyms", "r");
    if (!f)
        return false;
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "susfs_") || strstr(line, "ksu_susfs")) {
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

static bool lkm_is_loaded(void) {
    return access("/sys/module/susfs_guard_lkm", F_OK) == 0 ||
           access("/proc/susfs_opath", F_OK) == 0;
}

static void detect_susfs_version(void) {
    g_backend_state = PROBE_UNKNOWN;
    g_last_errno = 0;
    g_abi = ABI_UNKNOWN;
    g_version = 0;
    free(g_version_string);
    g_version_string = NULL;

    struct version_v2000 v2 = {0};
    v2.err = ERR_v2000_CMD_NOT_SUPPORTED;
    errno = 0;
    long v2_ret = syscall(SYS_reboot, KSU_INSTALL_MAGIC1, SUSFS_MAGIC, CMD_SUSFS_SHOW_VERSION, &v2);
    int v2_errno = errno;
    if (v2_ret == 0 && v2.err == 0 && v2.susfs_version[0]) {
        g_abi     = ABI_v2000;
        g_version = parse_version_string(v2.susfs_version);
        g_version_string = strdup(v2.susfs_version);
        if (g_version == 2000 || g_version == 2100)
            g_backend_state = lkm_is_loaded() ? PROBE_LKM : PROBE_NATIVE;
        else
            g_backend_state = PROBE_UNSUPPORTED;
        return;
    }

    char ver_buf[16] = {0};
    int error = -1;
    errno = 0;
    int pr_ret = prctl(KERNEL_SU_OPTION, CMD_SUSFS_SHOW_VERSION, ver_buf, NULL, &error);
    int pr_errno = errno;
    if (pr_ret == 0 && error == 0 && ver_buf[0]) {
        g_abi     = ABI_PRCTL;
        g_version = parse_version_string(ver_buf);
        g_version_string = strdup(ver_buf);
        g_backend_state = PROBE_UNSUPPORTED;
        return;
    }

    if (getuid() != 0) {
        g_backend_state = PROBE_PERMISSION;
        g_last_errno = EACCES;
        g_version_string = strdup("unknown");
        return;
    }

    if (lkm_is_loaded() || kallsyms_has_susfs()) {
        g_backend_state = PROBE_UNSUPPORTED;
        g_version_string = strdup("unknown");
        return;
    }

    if ((v2_ret < 0 && v2_errno != EINVAL && v2_errno != ENOSYS) ||
        (pr_ret < 0 && pr_errno != EINVAL && pr_errno != ENOSYS)) {
        g_backend_state = PROBE_PROTOCOL;
        g_last_errno = v2_errno ? v2_errno : pr_errno;
        g_version_string = strdup("unknown");
        return;
    }

    g_version_string = strdup("unknown");
    g_backend_state = PROBE_NONE;
}


static void pre_check(void) {
    if (getuid() != 0) {
        g_backend_state = PROBE_PERMISSION;
        g_last_errno = EACCES;
    }
}

static int get_file_stat(const char *pathname, struct stat *sb) {
    return stat(pathname, sb) != 0;
}

static void copy_stat_to_kstat_v1(struct sus_kstat_v1 *k, const struct stat *sb) {
    k->spoofed_ino           = sb->st_ino;
    k->spoofed_dev           = sb->st_dev;
    k->spoofed_nlink         = sb->st_nlink;
    k->spoofed_size          = sb->st_size;
    k->spoofed_atime_tv_sec  = sb->st_atime;
    k->spoofed_mtime_tv_sec  = sb->st_mtime;
    k->spoofed_ctime_tv_sec  = sb->st_ctime;
    k->spoofed_atime_tv_nsec = sb->st_atimensec;
    k->spoofed_mtime_tv_nsec = sb->st_mtimensec;
    k->spoofed_ctime_tv_nsec = sb->st_ctimensec;
    k->spoofed_blksize       = sb->st_blksize;
    k->spoofed_blocks        = sb->st_blocks;
}

static void copy_stat_to_kstat_v2000(struct sus_kstat_v2000 *k, const struct stat *sb) {
    k->spoofed_ino           = sb->st_ino;
    k->spoofed_dev           = sb->st_dev;
    k->spoofed_nlink         = sb->st_nlink;
    k->spoofed_size          = sb->st_size;
    k->spoofed_atime_tv_sec  = sb->st_atime;
    k->spoofed_mtime_tv_sec  = sb->st_mtime;
    k->spoofed_ctime_tv_sec  = sb->st_ctime;
    k->spoofed_atime_tv_nsec = sb->st_atimensec;
    k->spoofed_mtime_tv_nsec = sb->st_mtimensec;
    k->spoofed_ctime_tv_nsec = sb->st_ctimensec;
    k->spoofed_blksize       = sb->st_blksize;
    k->spoofed_blocks        = sb->st_blocks;
}

static void copy_stat_to_kstat_v2100(struct sus_kstat_v2100 *k, const struct stat *sb) {
    k->spoofed_ino           = sb->st_ino;
    k->spoofed_dev           = sb->st_dev;
    k->spoofed_nlink         = sb->st_nlink;
    k->spoofed_size          = sb->st_size;
    k->spoofed_atime_tv_sec  = sb->st_atime;
    k->spoofed_atime_tv_nsec = sb->st_atimensec;
    k->spoofed_mtime_tv_sec  = sb->st_mtime;
    k->spoofed_mtime_tv_nsec = sb->st_mtimensec;
    k->spoofed_ctime_tv_sec  = sb->st_ctime;
    k->spoofed_ctime_tv_nsec = sb->st_ctimensec;
    k->spoofed_blocks        = sb->st_blocks;
    k->spoofed_blksize       = sb->st_blksize;
}

static int v2000_cmd_sus_path(unsigned long cmd, const char *path, const struct stat *sb, int layout) {
    if (layout == V2000_SUS_PATH_LAYOUT_NEW) {
        struct sus_path_v2000_new info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.err = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(cmd, &info);
        return info.err;
    }

    struct sus_path_v2000 info = {0};
    strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
    info.target_ino = sb->st_ino;
    info.i_uid      = sb->st_uid;
    info.err        = ERR_v2000_CMD_NOT_SUPPORTED;
    v2000_cmd(cmd, &info);
    return info.err;
}

static int read_file_to_buf(const char *path, char **out, long *out_size) {
    char abs[PATH_MAX];
    if (!realpath(path, abs)) { perror("realpath"); return 1; }
    FILE *f = fopen(abs, "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); perror("malloc"); return 1; }
    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        fclose(f); free(buf); perror("fread"); return 1;
    }
    buf[sz] = '\0';
    fclose(f);
    *out = buf;
    if (out_size) *out_size = sz;
    return 0;
}

static void print_help(void) {
    printf("usage: %s <CMD> [CMD options]\n", TAG);
    printf("help: %s --help <CMD>  (for more details of a specific command)\n", TAG);
    if (g_abi != ABI_UNKNOWN)
        printf("  Detected kernel: susfs %s (ABI: %s)\n",
               g_version_string,
               g_abi == ABI_v2000 ? "sys_reboot" : "prctl");
    printf("\n  <CMD>:\n");
    printf("    add_sus_path </path>\n");
    if (HAVE(1510))
        printf("    add_sus_path_loop </path>\n");
    if (HAVE(158)) {
        printf("    set_android_data_root_path </path/to/Android/data>\n");
        printf("    set_sdcard_root_path </path/to/sdcard>\n");
    }
    if (HAVE(2000)) {
        printf("    add_sus_mount <mounted_path> (If CLI feature is supported)\n");
    }
    else {
        printf("    add_sus_mount <mounted_path>\n");
    }
    if (HAVE(158) && g_abi == ABI_PRCTL)
        printf("    hide_sus_mnts_for_all_procs <0|1>\n");
    if (g_abi == ABI_v2000)
        printf("    hide_sus_mnts_for_non_su_procs <0|1>\n");
    if (HAVE(1510))
        printf("    umount_for_zygote_iso_service <0|1>\n");
    printf("    add_sus_kstat_statically </path> <ino> <dev> <nlink> <size>"
           " <atime> <atime_nsec> <mtime> <mtime_nsec> <ctime> <ctime_nsec>"
           " <blocks> <blksize>  (pass 'default' for any field)\n");
    printf("    add_sus_kstat </path>\n");
    printf("    update_sus_kstat </path>\n");
    printf("    update_sus_kstat_full_clone </path>\n");
    printf("    add_try_umount </path> <mode>  (mode: 0=no flags, 1=MNT_DETACH)\n");
    if (HAVE(159) && g_abi == ABI_PRCTL)
        printf("    run_try_umount\n");
    printf("    set_uname <release> <version>  (pass 'default' for either)\n");
    printf("    enable_log <0|1>\n");
    if (g_version == 152 || g_version == 153)
        printf("    set_bootconfig </path/to/file>\n");
    if (HAVE(154))
        printf("    set_cmdline_or_bootconfig </path/to/file>\n");
    printf("    add_open_redirect </target> </redirect>\n");
    if (HAVE(1512))
        printf("    add_sus_map </path/to/library>\n");
    if (HAVE(1510))
        printf("    enable_avc_log_spoofing <0|1>\n");
    if (HAVE(153))
        printf("    show <version|enabled_features|variant>\n");
    if (g_abi == ABI_PRCTL)
        printf("    sus_su <0|2|show_working_mode>\n");
    printf("\n");
}

static int print_more_help(const char *cmd) {
    if (strcmp(cmd, "add_sus_path") == 0 ) {
        printf("    add_sus_path </path/of/file_or_directory>\n");
        printf("      |--> Added path and all its sub-paths will be hidden for umounted app process from several syscalls\n");
        printf("      |--> Please be reminded that if the target path has upper mounts then make sure the proper layer is added, otherwise it may not be effective for the target process.\n");
        printf("      * Important Notes *\n");
        printf("      - Only effective for umounted process with uid >= 10000\n");
        printf("      - On susfs version v1.5.8 - early v2.0.0, to fix the leak of app path after /sdcard/Android/data from syscall, please run ksu_susfs set_android_data_root_path </path/of/sdcard/Android/data> first\n");
        printf("    Example: %s add_sus_path /data/local/tmp/main.jar\n", TAG);
        return 0;
    }
    if (strcmp(cmd, "add_sus_path_loop") == 0 ) {
        printf("    add_sus_path_loop </path/of/file_or_directory>\n");
        printf("      |--> The only difference to add_sus_path is that the added sus_path via this cli will be flagged as SUS_PATH again for the app process when it is being spawned by zygote and marked umounted\n");
        printf("      |--> Also it does not check if the path is existed or not, instead it checks for empty string only, so be careful what to add.\n");
        printf("      * Important Notes *\n");
        printf("      - Only effective for umounted process with uid >= 10000\n");
        printf("      - On susfs version v1.5.8 - early v2.0.0, this applies to regular path (not including path in /sdcard/) only\n");
        printf("    Example: %s add_sus_path_loop /data/local/tmp/main.jar\n", TAG);
        return 0;
    }
    if (strcmp(cmd, "set_android_data_root_path") == 0) {
    	printf("    set_android_data_root_path </root/dir/of/sdcard/Android/data>\n");
        printf("      |--> To fix the leak of app path after /sdcard/Android/data/ from syscall, first you need to tell the susfs kernel where is the actual path '/sdcard/Android/data' located, as it may vary on different phones\n");
        printf("      |--> Effective for no root access granted user apps only\n");
        return 0;
    }
    if (strcmp(cmd, "set_sdcard_root_path") == 0) {
        printf("    set_sdcard_root_path </root/dir/of/sdcard>\n");
        printf("      |--> To hide paths after /sdcard/, first you need to tell the susfs kernel where is the actual path '/sdcard' located, as it may vary on different phones\n");
        printf("      |--> Warning: All no root access granted user apps cannot see any sus paths in /sdcard/ unless you grant root access for the target app\n");
        return 0;
    }
    if (strcmp(cmd, "add_sus_mount") == 0 ) {
        printf("    add_sus_mount </path/of/mount_point>\n");
	    printf("      |--> Added mounted path will be hidden from /proc/self/[mounts|mountinfo|mountstats]\n");
	    printf("      |--> Please be reminded that the target path must be added after the bind mount or overlay operation, otherwise it won't be effective\n");
        printf("    Example: %s add_sus_mount /system/etc/hosts\n", TAG);
        return 0;
    }
    if (strcmp(cmd, "hide_sus_mnts_for_all_procs") == 0) {
        printf("      |--> 0 -> Do not hide sus mounts for all processes but only non ksu process\n");
        printf("      |--> 1 -> Hide all sus mounts for all processes no matter they are ksu processes or not\n");
        printf("      * Important Notes *\n");
        printf("      - It is set to 1 in kernel by default\n");
        printf("      - It is recommended to set to 0 after screen is unlocked, or during service.sh or boot-completed.sh stage, as this should fix the issue on some rooted apps that rely on mounts mounted by ksu process\n");
        return 0;
    }
    if (strcmp(cmd, "hide_sus_mnts_for_non_su_procs") == 0) {
        printf("      |--> 0 -> DO NOT hide sus mounts for non-su processes\n");
	    printf("      |--> 1 -> hide all sus mounts for non-su processes\n");
	    printf("      * Important Notes *\n");
	    printf("      - It is set to 0 in kernel by default\n");
	    printf("      - For ReZygisk without TreatWheel module, it is recommended to set to 1 in post-fs-data.sh to prevent zygote from caching the sus mounts in memory, and revert to 0 in boot-completed.sh stage, or keep it enabled if you want to keep them hidden from /proc/self/[mounts|mountinfo|mountstat] for non-su processes\n");
        return 0;
    }
    if (strcmp(cmd, "umount_for_zygote_iso_service") == 0) {
        printf("      |--> 0 -> Do not umount for zygote spawned isolated service process\n");
        printf("      |--> 1 -> Enable to umount for zygote spawned isolated service process\n");
        printf("      * Important Notes *\n");
        printf("      - By default it is set to 0 in kernel, or create '/data/adb/susfs_umount_for_zygote_iso_service' to set it to 1 on boot\n");
        printf("      - Set to 0 if you have modules that overlay framework system files like framework.jar or other overlay apk, then atm you should let other module like zygisk and its hiding module to take care of, otherwise it may cause bootloop\n");
        printf("      - Set to 1 if you DO NOT have such modules mentioned above, otherwise sus mounts won't be umounted for zygote spawned isolated process and they will be detected\n");
        return 0;
    }
    if (strcmp(cmd, "add_sus_kstat_statically") == 0) {
        printf("    add_sus_kstat_statically </path> <ino> <dev> <nlink> <size>"
               " <atime> <atime_nsec> <mtime> <mtime_nsec> <ctime> <ctime_nsec>"
               " <blocks> <blksize>\n");
        printf("      |--> Use 'stat' tool to find the format:\n");
        printf("               ino -> %%i, dev -> %%d, nlink -> %%h, atime -> %%X, mtime -> %%Y, ctime -> %%Z\n");
        printf("               size -> %%s, blocks -> %%b, blksize -> %%B\n");
        printf("      |--> e.g., %s add_sus_kstat_statically '/system/addon.d' '1234' '1234' '2' '223344'\\\n", TAG);
        printf("                    '1712592355' '0' '1712592355' '0' '1712592355' '0' '1712592355' '0'\\\n");
        printf("                    '16' '512'\n");
        printf("      |--> Or pass 'default' to use its original value:\n");
        printf("      |--> e.g., %s add_sus_kstat_statically '/system/addon.d' 'default' 'default' 'default' 'default'\\\n", TAG);
        printf("                    '1712592355' 'default' '1712592355' 'default' '1712592355' 'default'\\\n");
        printf("                    'default' 'default'\n");
        printf("      * Important Notes *\n");
	    printf("      - Only effective for umounted process with uid >= 10000\n");
        return 0;
    }
    if (strcmp(cmd, "add_sus_kstat") == 0) {
        printf("    add_sus_kstat </path/of/file_or_directory>\n");
        printf("      |--> Add the desired path BEFORE it gets bind mounted or overlayed, this is used for storing original stat info in kernel memory\n");
        printf("      |--> This command must be completed with <update_sus_kstat> later after the added path is bind mounted or overlayed\n");
        printf("      * Important Notes *\n");
        printf("      - Only effective for umounted process with uid >= 10000\n");
        return 0;
    }
    if (strcmp(cmd, "update_sus_kstat") == 0) {
        printf("    update_sus_kstat </path/of/file_or_directory>\n");
        printf("      |--> Add the desired path you have added before via <add_sus_kstat> to complete the kstat spoofing procedure\n");
        printf("      |--> This updates the target ino, but size and blocks are remained the same as current stat\n");
        printf("      * Important Notes *\n");
        printf("      - Only effective for umounted process with uid >= 10000\n");
        return 0;
    }
    if (strcmp(cmd, "update_sus_kstat_full_clone") == 0) {
        printf("    update_sus_kstat_full_clone </path/of/file_or_directory>\n");
        printf("      |--> Add the desired path you have added before via <add_sus_kstat> to complete the kstat spoofing procedure\n");
        printf("      |--> This updates the target ino only, other stat members are remained the same as the original stat\n");
        printf("      * Important Notes *\n");
        printf("      - Only effective for umounted process with uid >= 10000\n");
        return 0;
    }
    if (strcmp(cmd, "add_try_umount") == 0) {
        printf("    add_try_umount </path/of/file_or_directory> <mode>\n");
        printf("      |--> Added path will be umounted from KSU for all UIDs that are NOT su allowed, and profile template configured with umount\n");
        printf("      |--> <mode>: 0 -> umount with no flags, 1 -> umount with MNT_DETACH\n");
        printf("      |--> NOTE: susfs umount takes precedence of ksu umount\n");
        return 0;
    }
    if (strcmp(cmd, "run_try_umount") == 0) {
        printf("        run_try_umount\n");
	    printf("         |--> Make all sus mounts to be private and umount them one by one in kernel for the mount namespace of current process\n");
        return 0;
    }
    if (strcmp(cmd, "set_uname") == 0) {
        printf("    set_uname <release> <version>\n");
        printf("      |--> Spoof uname for all processes, set string to 'default' to imply the function to use original string\n");
        printf("      |--> NOTE: only 'release' and <version> are spoofed as others are no longer needed\n");
        printf("     Example: %s set_uname '4.9.337-g3291538446b7' '#1 SMP PREEMPT Mon Oct 6 16:50:48 UTC 2025'\n", TAG);
        return 0;
    }
    if (strcmp(cmd, "enable_log") == 0) {
        printf("    enable_log <0|1>\n");
	    printf("      |--> 0: disable susfs log in kernel, 1: enable susfs log in kernel\n");
        return 0;
    }
    if (strcmp(cmd, "set_bootconfig") == 0) {
        printf("    set_bootconfig </path/to/file>\n");
        printf("      |--> Spoof the output of /proc/bootconfig from a text file\n");
        return 0;
    }
    if (strcmp(cmd, "set_cmdline_or_bootconfig") == 0) {
        printf("    set_cmdline_or_bootconfig </path/to/fake_cmdline_file/or/fake_bootconfig_file>\n");
	    printf("      |--> Spoof the output of /proc/cmdline (non-gki) or /proc/bootconfig (gki) from a text file\n");
        return 0;
    }
    if (strcmp(cmd, "add_open_redirect") == 0) {
        if (HAVE(2100)) {
            printf("    add_open_redirect </target/path> </redirected/path> <uid_scheme>\n");
            printf("      |--> Redirect the target path to be opened with user defined path and pre-defined uid scheme\n");
            printf("      |--> <uid_scheme>\n");
            printf("           |--> 0: Effective for non-app processes (uid < 10000)\n");
            printf("           |--> 1: Effective for non-su processes of which uid is 0 (All root process but not with su domain)\n");
            printf("           |--> 2: Effective for non-su processes (Use it carefully!)\n");
            printf("           |--> 3: Effective for processes that are marked umounted with uid >= 10000 (Use it carefully!)\n");
            printf("           |--> 4: Effective for processes that are marked umounted (include most of the init spawned process, use it carefully!)\n");
            printf("      * Important Notes *\n");
            printf("      - Both target_pathname and redirected_pathname must be existed before they can be added to open_redirect\n");
            printf("      - Users have to take care of the selinux permission of both target_pathname and redirected_pathname by themselves\n");
            printf("      - Only effective for current process that matches the pre-defined uid scheme\n");
        }
        else {
            printf("    add_open_redirect </target/path> </redirected/path>\n");
            printf("      |--> Redirect the target path to be opened with user defined path\n");
            printf("      * Important Notes *\n");
            printf("      - Only effective for current process with uid <= 2000 or uid <= 11000 (for modified susfs kernels)\n");
        }
        return 0;
    }
    if (strcmp(cmd, "add_sus_map") == 0) {
        printf("    add_sus_map </path/to/actual/library>\n");
        printf("      |--> added real file path which gets mmapped will be hidden from /proc/self/[maps|smaps|smaps_rollup|map_files|mem|pagemap]\n");
        printf("      |--> e.g., add_sus_map '/data/adb/modules/my_module/zygisk/arm64-v8a.so'\n");
        printf("      * Important Notes *\n");
        printf("      - It does NOT support hiding for anon memory.\n");
        printf("      - It does NOT hide any inline hooks or plt hooks cause by the injected library itself\n");
        printf("      - It may not be able to evade detections by apps that implement a good injection detection\n");
        printf("      - Only effective for umounted process with uid >= 10000\n");
        return 0;
    }
    if (strcmp(cmd, "enable_avc_log_spoofing") == 0) {
        printf("    enable_avc_log_spoofing <0|1>\n");
        printf("      |--> 0: Disable spoofing the sus 'su' tcontext shown in avc log in kernel\n");
        printf("      |--> 1: Enable spoofing the sus tcontext 'su' with 'kernel' shown in avc log in kernel\n");
        printf("      * Important Note *\n");
        printf("      - It is set to '0' by default in kernel\n");
        printf("      - Enable this will sometimes make developers hard to identify the cause when they are debugging with some permission or selinux issue, so users are advised to disable this when doing so.\n");
        return 0;
    }
    if (strcmp(cmd, "show") == 0) {
        printf("    show <version|enabled_features|variant>\n");
        printf("      |--> version: show the current susfs version implemented in kernel\n");
        printf("      |--> enabled_features: show the current implemented susfs features in kernel\n");
        printf("      |--> variant: show the current variant: GKI or NON-GKI\n");
        return 0;
    }
    if (strcmp(cmd, "sus_su") == 0) {
        printf("        sus_su <0|1|2|show_working_mode>\n");
        printf("         |--> NOTE-1:\n");
        printf("              - For mode 1: (deprecated) It disables kprobe hooks made by ksu, and instead,\n");
        printf("                a sus_su character device driver with random name will be created, and user\n");
        printf("                need to use a tool named 'sus_su' together with a path file in same current directory\n");
        printf("                named '" SUS_SU_CONF_FILE_PATH "' to get a root shell from the sus_su driver.'\n");
        printf("                ** sus_su userspace tool and an overlay mount is required **'\n");
        printf("              - For mode 2: It disables kprobe hooks made by ksu, and instead,\n");
        printf("                the non-kprobe inline hooks will be enbaled, just the same implementation for non-gki kernel without kprobe supported)\n");
        printf("                ** Needs no extra userspace tools and mounts **\n");
        printf("         |--> NOTE-2:\n");
        printf("                Please see the service.sh template from ksu_module_susfs for the usage\n");
        printf("         |--> 0: enable core ksu kprobe hooks and disable sus_su driver\n");
        printf("         |--> 1: (deprecated), disable the core ksu kprobe hooks and enable sus_su fifo driver\n");
        printf("         |--> 2: disable the core ksu kprobe hooks and enable sus_su just with non-kprobe hooks\n");
        printf("         |--> show_working_mode: show the current sus_su working mode, [0,1,2]\n");
        return 0;
    }

    printf("[-] No additional help available for command '%s'\n", cmd);
    return 1;
}


static int cmd_add_sus_path(const char *path, bool loop) {
    unsigned long cmd = loop ? CMD_SUSFS_ADD_SUS_PATH_LOOP : CMD_SUSFS_ADD_SUS_PATH;
    int ret;

    if (g_abi == ABI_v2000) {
        if (g_v2000_sus_path_layout != V2000_SUS_PATH_LAYOUT_OLD) {
            struct sus_path_v2000_new info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(cmd, &info);
            if (info.err == 0 || g_v2000_sus_path_layout == V2000_SUS_PATH_LAYOUT_NEW) {
                g_v2000_sus_path_layout = V2000_SUS_PATH_LAYOUT_NEW;
                clear_old_sus_path_layout_cache();
                prt_not_supported(cmd, info.err);
                return info.err;
            }
        }
        {
            struct stat sb;
            if (get_file_stat(path, &sb)) {
                printf("[-] Failed to stat '%s'\n", path);
                return 1;
            }
            struct sus_path_v2000 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.target_ino = sb.st_ino;
            info.i_uid      = sb.st_uid;
            info.err        = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(cmd, &info);
            if (info.err == 0) {
                g_v2000_sus_path_layout = V2000_SUS_PATH_LAYOUT_OLD;
                persist_old_sus_path_layout_cache();
            }
            prt_not_supported(cmd, info.err);
            ret = info.err;
        }
    } else {
        struct stat sb;
        if (get_file_stat(path, &sb)) {
            printf("[-] Failed to stat '%s'\n", path);
            return 1;
        }
        if (HAVE(154)) {
            struct sus_path_v154 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.target_ino = sb.st_ino;
            info.i_uid      = sb.st_uid;
            ret = prctl_cmd(cmd, &info);
            prt_not_supported(cmd, ret);
        } else {
            struct sus_path_v1 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.target_ino = sb.st_ino;
            ret = prctl_cmd(cmd, &info);
            prt_not_supported(cmd, ret);
        }
    }
    return ret;
}

static int cmd_set_external_dir(const char *path, unsigned long cmd) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct external_dir_v2000 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.cmd = (int)cmd;
        info.err = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(cmd, &info);
        prt_not_supported(cmd, info.err);
        ret = info.err;
    } else {
        ret = prctl_cmd_scalar(cmd, (unsigned long)(uintptr_t)path);
        prt_not_supported(cmd, ret);
    }
    return ret;
}

static int cmd_add_sus_mount(const char *path) {
    struct stat sb;
    if (get_file_stat(path, &sb)) {
        printf("[-] Failed to stat '%s'\n", path);
        return 1;
    }
    int ret;
    if (g_abi == ABI_v2000) {
        struct sus_mount_v2000 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.target_dev = sb.st_dev;
        info.err        = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_ADD_SUS_MOUNT, &info);
        prt_not_supported(CMD_SUSFS_ADD_SUS_MOUNT, info.err);
        ret = info.err;
    } else {
        struct sus_mount_v1 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.target_dev = sb.st_dev;
        ret = prctl_cmd(CMD_SUSFS_ADD_SUS_MOUNT, &info);
        prt_not_supported(CMD_SUSFS_ADD_SUS_MOUNT, ret);
    }
    return ret;
}

static int cmd_hide_sus_mnts(int enabled) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct hide_sus_mnts_v2000 info = {0};
        info.enabled = (bool)enabled;
        info.err     = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_HIDE_SUS_MNTS, &info);
        prt_not_supported(CMD_SUSFS_HIDE_SUS_MNTS, info.err);
        ret = info.err;
    } else {
        ret = prctl_cmd_scalar(CMD_SUSFS_HIDE_SUS_MNTS, (unsigned long)enabled);
        prt_not_supported(CMD_SUSFS_HIDE_SUS_MNTS, ret);
    }
    return ret;
}

static int cmd_umount_zygote_iso(int enabled) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct umount_zygote_v2000 info = {0};
        info.enabled = (bool)enabled;
        info.err     = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_UMOUNT_FOR_ZYGOTE_ISO_SERVICE, &info);
        prt_not_supported(CMD_SUSFS_UMOUNT_FOR_ZYGOTE_ISO_SERVICE, info.err);
        ret = info.err;
    } else {
        ret = prctl_cmd_scalar(CMD_SUSFS_UMOUNT_FOR_ZYGOTE_ISO_SERVICE, (unsigned long)enabled);
        prt_not_supported(CMD_SUSFS_UMOUNT_FOR_ZYGOTE_ISO_SERVICE, ret);
    }
    return ret;
}


static int parse_kstat_argv(char **argv, struct stat *sb, int *flags) {
    char *ep;
#define MAYBE_UL(idx, field) \
    if (strcmp(argv[idx], "default")) { \
        unsigned long _v = strtoul(argv[idx], &ep, 10); \
        if (*ep) return 1; \
        sb->field = _v; \
    }
#define MAYBE_SL(idx, field) \
    if (strcmp(argv[idx], "default")) { \
        long _v = strtol(argv[idx], &ep, 10); \
        if (*ep) return 1; \
        sb->field = _v; \
    }
    if (strcmp(argv[4], "default")) {
        MAYBE_UL(4, st_dev)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_DEV;
    }
    if (strcmp(argv[5], "default")) {
        MAYBE_UL(5, st_nlink)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_NLINK;
    }
    if (strcmp(argv[6], "default")) {
        MAYBE_UL(6, st_size)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_SIZE;
    }
    if (strcmp(argv[7], "default")) {
        MAYBE_SL(7, st_atime)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_ATIME_TV_SEC;
    }
    if (strcmp(argv[8], "default")) {
        MAYBE_UL(8, st_atimensec)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_ATIME_TV_NSEC;
    }
    if (strcmp(argv[9], "default")) {
        MAYBE_SL(9, st_mtime)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_MTIME_TV_SEC;
    }
    if (strcmp(argv[10], "default")) {
        MAYBE_UL(10, st_mtimensec)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_MTIME_TV_NSEC;
    }
    if (strcmp(argv[11], "default")) {
        MAYBE_SL(11, st_ctime)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_CTIME_TV_SEC;
    }
    if (strcmp(argv[12], "default")) {
        MAYBE_UL(12, st_ctimensec)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_CTIME_TV_NSEC;
    }
    if (strcmp(argv[13], "default")) {
        MAYBE_UL(13, st_blocks)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_BLOCKS;
    }
    if (strcmp(argv[14], "default")) {
        MAYBE_UL(14, st_blksize)
        if (flags && HAVE(2100)) *flags |= KSTAT_SPOOF_BLKSIZE;
    }
#undef MAYBE_UL
#undef MAYBE_SL
    return 0;
}

static int cmd_add_sus_kstat_statically(char **argv) {
    struct stat sb;
    if (get_file_stat(argv[2], &sb)) {
        printf("[-] Failed to stat '%s'\n", argv[2]);
        return 1;
    }
    unsigned long orig_ino = sb.st_ino;
    int flags = 0;

    if (strcmp(argv[3], "default")) {
        char *ep;
        sb.st_ino = strtoul(argv[3], &ep, 10);
        if (*ep) { print_help(); return 1; }
        if (HAVE(2100))
            flags |= KSTAT_SPOOF_INO;
    }
    if (parse_kstat_argv(argv, &sb, &flags)) { print_help(); return 1; }

    int ret;
    if (g_abi == ABI_v2000) {
        if (HAVE(2100)) {
            struct sus_kstat_v2100 info = {0};
            strncpy(info.target_pathname, argv[2], SUSFS_MAX_LEN_PATHNAME - 1);
            info.is_statically = true;
            info.target_ino    = orig_ino;
            info.flags         = flags;
            copy_stat_to_kstat_v2100(&info, &sb);
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY, &info);
            prt_not_supported(CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY, info.err);
            ret = info.err;
        } else {
            struct sus_kstat_v2000 info = {0};
            strncpy(info.target_pathname, argv[2], SUSFS_MAX_LEN_PATHNAME - 1);
            info.is_statically = true;
            info.target_ino    = orig_ino;
            copy_stat_to_kstat_v2000(&info, &sb);
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY, &info);
            prt_not_supported(CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY, info.err);
            ret = info.err;
        }
    } else {
        struct sus_kstat_v1 info = {0};
        strncpy(info.target_pathname, argv[2], SUSFS_MAX_LEN_PATHNAME - 1);
        info.is_statically = true;
        info.target_ino    = orig_ino;
        copy_stat_to_kstat_v1(&info, &sb);
        ret = prctl_cmd(CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY, &info);
        prt_not_supported(CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY, ret);
    }
    return ret;
}

static int cmd_add_sus_kstat(const char *path) {
    struct stat sb;
    if (get_file_stat(path, &sb)) { printf("[-] Failed to stat '%s'\n", path); return 1; }
    int ret;
    if (g_abi == ABI_v2000) {
        if (HAVE(2100)) {
            struct sus_kstat_v2100 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.is_statically = false;
            info.target_ino    = sb.st_ino;
            info.flags         = KSTAT_AUTO_SPOOF;
            copy_stat_to_kstat_v2100(&info, &sb);
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_ADD_SUS_KSTAT, &info);
            prt_not_supported(CMD_SUSFS_ADD_SUS_KSTAT, info.err);
            ret = info.err;
        } else {
            struct sus_kstat_v2000 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.is_statically = false;
            info.target_ino    = sb.st_ino;
            copy_stat_to_kstat_v2000(&info, &sb);
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_ADD_SUS_KSTAT, &info);
            prt_not_supported(CMD_SUSFS_ADD_SUS_KSTAT, info.err);
            ret = info.err;
        }
    } else {
        struct sus_kstat_v1 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.is_statically = false;
        info.target_ino    = sb.st_ino;
        copy_stat_to_kstat_v1(&info, &sb);
        ret = prctl_cmd(CMD_SUSFS_ADD_SUS_KSTAT, &info);
        prt_not_supported(CMD_SUSFS_ADD_SUS_KSTAT, ret);
    }
    return ret;
}

static int cmd_update_sus_kstat(const char *path, bool full_clone) {
    struct stat sb;
    if (get_file_stat(path, &sb)) { printf("[-] Failed to stat '%s'\n", path); return 1; }
    int ret;
    if (g_abi == ABI_v2000) {
        if (HAVE(2100)) {
            struct sus_kstat_v2100 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.is_statically = false;
            info.target_ino    = sb.st_ino;
            info.flags = full_clone ? KSTAT_AUTO_SPOOF_FULL_CLONE : KSTAT_AUTO_SPOOF;
            copy_stat_to_kstat_v2100(&info, &sb);
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_UPDATE_SUS_KSTAT, &info);
            prt_not_supported(CMD_SUSFS_UPDATE_SUS_KSTAT, info.err);
            ret = info.err;
        } else {
            struct sus_kstat_v2000 info = {0};
            strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
            info.is_statically = false;
            info.target_ino    = sb.st_ino;
            if (!full_clone) {
                info.spoofed_size   = sb.st_size;
                info.spoofed_blocks = sb.st_blocks;
            }
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_UPDATE_SUS_KSTAT, &info);
            prt_not_supported(CMD_SUSFS_UPDATE_SUS_KSTAT, info.err);
            ret = info.err;
        }
    } else {
        struct sus_kstat_v1 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.is_statically = false;
        info.target_ino    = sb.st_ino;
        if (!full_clone) {
            info.spoofed_size   = sb.st_size;
            info.spoofed_blocks = sb.st_blocks;
        }
        ret = prctl_cmd(CMD_SUSFS_UPDATE_SUS_KSTAT, &info);
        prt_not_supported(CMD_SUSFS_UPDATE_SUS_KSTAT, ret);
    }
    return ret;
}

static int cmd_add_try_umount(const char *path, int mnt_mode) {
    char abs[PATH_MAX];
    if (!realpath(path, abs)) { perror("realpath"); return 1; }

    const char *blocked_v2000[] = { "/odm", "/system", "/vendor", "/product",
                                   "/system_ext", "/data/adb/modules", NULL };
    const char *blocked_v1[]   = { "/system", "/vendor", "/product",
                                   "/data/adb/modules", "/debug_ramdisk", "/sbin", NULL };
    const char **bl = (g_abi == ABI_v2000) ? blocked_v2000 : blocked_v1;
    for (int i = 0; bl[i]; i++) {
        if (!strcmp(abs, bl[i])) {
            printf("[-] %s cannot be added to try_umount (managed by ksu)\n", abs);
            return -EINVAL;
        }
    }

    int ret;
    if (g_abi == ABI_v2000) {
        struct try_umount_v2000 info = {0};
        strncpy(info.target_pathname, abs, SUSFS_MAX_LEN_PATHNAME - 1);
        info.mnt_mode = mnt_mode;
        info.err      = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_ADD_TRY_UMOUNT, &info);
        prt_not_supported(CMD_SUSFS_ADD_TRY_UMOUNT, info.err);
        ret = info.err;
    } else {
        struct try_umount_v1 info = {0};
        strncpy(info.target_pathname, abs, SUSFS_MAX_LEN_PATHNAME - 1);
        info.mnt_mode = mnt_mode;
        ret = prctl_cmd(CMD_SUSFS_ADD_TRY_UMOUNT, &info);
        prt_not_supported(CMD_SUSFS_ADD_TRY_UMOUNT, ret);
    }
    return ret;
}

static int cmd_set_uname(const char *release, const char *version) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct uname_v2000 info = {0};
        strncpy(info.release, release, __NEW_UTS_LEN);
        strncpy(info.version, version, __NEW_UTS_LEN);
        info.err = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_SET_UNAME, &info);
        prt_not_supported(CMD_SUSFS_SET_UNAME, info.err);
        ret = info.err;
    } else {
        struct uname_v1 info = {0};
        strncpy(info.release, release, __NEW_UTS_LEN);
        strncpy(info.version, version, __NEW_UTS_LEN);
        ret = prctl_cmd(CMD_SUSFS_SET_UNAME, &info);
        prt_not_supported(CMD_SUSFS_SET_UNAME, ret);
    }
    return ret;
}

static int cmd_enable_log(int enabled) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct log_v2000 info = {0};
        info.enabled = (bool)enabled;
        info.err     = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_ENABLE_LOG, &info);
        prt_not_supported(CMD_SUSFS_ENABLE_LOG, info.err);
        ret = info.err;
    } else {
        ret = prctl_cmd_scalar(CMD_SUSFS_ENABLE_LOG, (unsigned long)enabled);
        prt_not_supported(CMD_SUSFS_ENABLE_LOG, ret);
    }
    return ret;
}

static int cmd_set_cmdline_or_bootconfig(const char *filepath) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct spoof_cmdline_v2000 *info = calloc(1, sizeof(*info));
        if (!info) { perror("calloc"); return -ENOMEM; }
        char abs[PATH_MAX];
        if (!realpath(filepath, abs)) { perror("realpath"); free(info); return 1; }
        FILE *f = fopen(abs, "rb");
        if (!f) { perror("fopen"); free(info); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        if (sz >= (long)sizeof(info->fake_cmdline_or_bootconfig)) {
            printf("[-] File too large (max %zu bytes)\n",
                   sizeof(info->fake_cmdline_or_bootconfig) - 1);
            fclose(f); free(info); return -EINVAL;
        }
        if ((long)fread(info->fake_cmdline_or_bootconfig, 1, (size_t)sz, f) != sz) {
            perror("fread"); fclose(f); free(info); return 1;
        }
        fclose(f);
        info->err = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG, info);
        prt_not_supported(CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG, info->err);
        ret = info->err;
        free(info);
    } else {
        char *buf;
        if (read_file_to_buf(filepath, &buf, NULL)) return 1;
        ret = prctl_cmd(CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG, buf);
        free(buf);
        prt_not_supported(CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG, ret);
    }
    return ret;
}

static int cmd_add_open_redirect(const char *target, const char *redirect) {
    char abs_t[PATH_MAX], abs_r[PATH_MAX];
    if (!realpath(target,   abs_t)) { perror("realpath(target)");   return 1; }
    if (!realpath(redirect, abs_r)) { perror("realpath(redirect)"); return 1; }
    struct stat sb;
    if (get_file_stat(abs_t, &sb)) { printf("[-] Failed to stat '%s'\n", abs_t); return 1; }
    int ret;
    if (g_abi == ABI_v2000) {
        struct open_redirect_v2000 info = {0};
        strncpy(info.target_pathname,     abs_t, SUSFS_MAX_LEN_PATHNAME - 1);
        strncpy(info.redirected_pathname, abs_r, SUSFS_MAX_LEN_PATHNAME - 1);
        info.target_ino = sb.st_ino;
        info.err        = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_ADD_OPEN_REDIRECT, &info);
        prt_not_supported(CMD_SUSFS_ADD_OPEN_REDIRECT, info.err);
        ret = info.err;
    } else {
        struct open_redirect_v1 info = {0};
        strncpy(info.target_pathname,     abs_t, SUSFS_MAX_LEN_PATHNAME - 1);
        strncpy(info.redirected_pathname, abs_r, SUSFS_MAX_LEN_PATHNAME - 1);
        info.target_ino = sb.st_ino;
        ret = prctl_cmd(CMD_SUSFS_ADD_OPEN_REDIRECT, &info);
        prt_not_supported(CMD_SUSFS_ADD_OPEN_REDIRECT, ret);
    }
    return ret;
}

static int cmd_add_open_redirect_2100(const char *target, const char *redirect, const char *uid) {
    char abs_t[PATH_MAX], abs_r[PATH_MAX];
    if (!realpath(target,   abs_t)) { perror("realpath(target)");   return 1; }
    if (!realpath(redirect, abs_r)) { perror("realpath(redirect)"); return 1; }
    struct stat sb;
    if (get_file_stat(abs_t, &sb)) { printf("[-] Failed to stat '%s'\n", abs_t); return 1; }
    int ret;
    
    struct open_redirect_v2100 info = {0};
    char *endptr;
    long uid_scheme;

    uid_scheme = strtol(uid, &endptr, 10);
    if (*endptr != '\0') {
        print_help();
        return -EINVAL;
    }

    if (uid_scheme < UID_NON_APP_PROC || uid_scheme > UID_UMOUNTED_PROC) {
        print_help();
        return -EINVAL;
    }

    info.uid_scheme = uid_scheme;
    strncpy(info.target_pathname, abs_t, SUSFS_MAX_LEN_PATHNAME - 1);
    strncpy(info.redirected_pathname, abs_r, SUSFS_MAX_LEN_PATHNAME - 1);
    info.err        = ERR_v2000_CMD_NOT_SUPPORTED;
    v2000_cmd(CMD_SUSFS_ADD_OPEN_REDIRECT, &info);
    prt_not_supported(CMD_SUSFS_ADD_OPEN_REDIRECT, info.err);
    ret = info.err;
    return ret;
}

static int cmd_add_sus_map(const char *path) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct sus_map_v2000 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        info.err = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_ADD_SUS_MAP, &info);
        prt_not_supported(CMD_SUSFS_ADD_SUS_MAP, info.err);
        ret = info.err;
    } else {
        struct sus_map_v1 info = {0};
        strncpy(info.target_pathname, path, SUSFS_MAX_LEN_PATHNAME - 1);
        ret = prctl_cmd(CMD_SUSFS_ADD_SUS_MAP, &info);
        prt_not_supported(CMD_SUSFS_ADD_SUS_MAP, ret);
    }
    return ret;
}

static int cmd_enable_avc_log_spoofing(int enabled) {
    int ret;
    if (g_abi == ABI_v2000) {
        struct avc_log_v2000 info = {0};
        info.enabled = (bool)enabled;
        info.err     = ERR_v2000_CMD_NOT_SUPPORTED;
        v2000_cmd(CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING, &info);
        prt_not_supported(CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING, info.err);
        ret = info.err;
    } else {
        ret = prctl_cmd_scalar(CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING, (unsigned long)enabled);
        prt_not_supported(CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING, ret);
    }
    return ret;
}

static int cmd_show(const char *what) {
    if (!strcmp(what, "version")) {
        if (g_abi == ABI_v2000) {
            struct version_v2000 info = {0};
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_SHOW_VERSION, &info);
            prt_not_supported(CMD_SUSFS_SHOW_VERSION, info.err);
            if (!info.err) printf("%s\n", info.susfs_version);
            return info.err;
        } else {
            char buf[16] = {0};
            int r = prctl_cmd(CMD_SUSFS_SHOW_VERSION, buf);
            prt_not_supported(CMD_SUSFS_SHOW_VERSION, r);
            if (!r) printf("%s\n", buf);
            return r;
        }

    } else if (!strcmp(what, "enabled_features")) {
        if (g_abi == ABI_v2000) {
            struct enabled_features_v2000 *info = calloc(1, sizeof(*info));
            if (!info) { perror("calloc"); return -ENOMEM; }
            info->err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_SHOW_ENABLED_FEATURES, info);
            prt_not_supported(CMD_SUSFS_SHOW_ENABLED_FEATURES, info->err);
            int r = info->err;
            if (!r) printf("%s", info->enabled_features);
            free(info);
            return r;
        } else if (HAVE(159)) {
            size_t bufsz = (size_t)getpagesize() * 2;
            char *buf = malloc(bufsz);
            if (!buf) { perror("malloc"); return -ENOMEM; }
            int r = prctl_cmd4(CMD_SUSFS_SHOW_ENABLED_FEATURES, buf, (unsigned long)bufsz);
            prt_not_supported(CMD_SUSFS_SHOW_ENABLED_FEATURES, r);
            if (!r) printf("%s", buf);
            free(buf);
            return r;
        } else {
            unsigned long mask = 0;
            int r = prctl_cmd(CMD_SUSFS_SHOW_ENABLED_FEATURES, &mask);
            prt_not_supported(CMD_SUSFS_SHOW_ENABLED_FEATURES, r);
            if (r) return r;

            const char *names_153[] = {
                "CONFIG_KSU_SUSFS_SUS_PATH",
                "CONFIG_KSU_SUSFS_SUS_MOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT",
                "CONFIG_KSU_SUSFS_SUS_KSTAT",
                "CONFIG_KSU_SUSFS_SUS_OVERLAYFS",
                "CONFIG_KSU_SUSFS_TRY_UMOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT",
                "CONFIG_KSU_SUSFS_SPOOF_UNAME",
                "CONFIG_KSU_SUSFS_ENABLE_LOG",
                "CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS",
                "CONFIG_KSU_SUSFS_SPOOF_BOOTCONFIG",
                "CONFIG_KSU_SUSFS_OPEN_REDIRECT",
                "CONFIG_KSU_SUSFS_SUS_SU",
                NULL
            };
            const char *names_154[] = {
                "CONFIG_KSU_SUSFS_SUS_PATH",
                "CONFIG_KSU_SUSFS_SUS_MOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT",
                "CONFIG_KSU_SUSFS_SUS_KSTAT",
                "CONFIG_KSU_SUSFS_SUS_OVERLAYFS",
                "CONFIG_KSU_SUSFS_TRY_UMOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT",
                "CONFIG_KSU_SUSFS_SPOOF_UNAME",
                "CONFIG_KSU_SUSFS_ENABLE_LOG",
                "CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS",
                "CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG",
                "CONFIG_KSU_SUSFS_OPEN_REDIRECT",
                "CONFIG_KSU_SUSFS_SUS_SU",
                "CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT",
                NULL
            };
            const char *names_158[] = {
                "CONFIG_KSU_SUSFS_SUS_PATH",
                "CONFIG_KSU_SUSFS_SUS_MOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT",
                "CONFIG_KSU_SUSFS_SUS_KSTAT",
                "CONFIG_KSU_SUSFS_TRY_UMOUNT",
                "CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT",
                "CONFIG_KSU_SUSFS_SPOOF_UNAME",
                "CONFIG_KSU_SUSFS_ENABLE_LOG",
                "CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS",
                "CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG",
                "CONFIG_KSU_SUSFS_OPEN_REDIRECT",
                "CONFIG_KSU_SUSFS_SUS_SU",
                "CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT",
                NULL
            };
            const char **names = (g_version == 153) ? names_153 : (g_version == 154) ? names_154 : names_158;
            for (int bit = 0; names[bit]; bit++)
                if (mask & (1UL << bit))
                    printf("%s\n", names[bit]);
            return 0;
        }

    } else if (!strcmp(what, "variant")) {
        if (g_abi == ABI_v2000) {
            struct variant_v2000 info = {0};
            info.err = ERR_v2000_CMD_NOT_SUPPORTED;
            v2000_cmd(CMD_SUSFS_SHOW_VARIANT, &info);
            prt_not_supported(CMD_SUSFS_SHOW_VARIANT, info.err);
            if (!info.err) printf("%s\n", info.susfs_variant);
            return info.err;
        } else {
            char buf[16] = {0};
            int r = prctl_cmd(CMD_SUSFS_SHOW_VARIANT, buf);
            prt_not_supported(CMD_SUSFS_SHOW_VARIANT, r);
            if (!r) printf("%s\n", buf);
            return r;
        }
    }

    print_help();
    return 1;
}


static int enable_sus_su_prctl(int last_mode, int target_mode) {
    struct sus_su_v153 info = {0};
    info.mode  = target_mode;
    int error = prctl_cmd(CMD_SUSFS_SUS_SU, &info);
    prt_not_supported(CMD_SUSFS_SUS_SU, error);
    if (error) {
        if (error == 1) printf("[-] sus_su is already in mode %d\n", target_mode);
        else if (error == 2) printf("[-] please set mode %d first\n", SUS_SU_DISABLED);
        return error;
    }
    printf("[+] sus_su mode %d is enabled\n", target_mode);
    return 0;
}

static int cmd_sus_su(const char *arg) {
    if (g_version == 152) {
        struct sus_su_v152 info = {0};
        info.maj_dev_num = -1;
        int error;

        if (!strcmp(arg, "1")) {
            info.mode = SUS_SU_WITH_OVERLAY;
            error = prctl_cmd(CMD_SUSFS_SUS_SU, &info);
            prt_not_supported(CMD_SUSFS_SUS_SU, error);
            if (error) return error;
            dev_t dev = makedev(info.maj_dev_num, 0);
            if (mknod(info.drv_path, S_IFCHR | 0666, dev) < 0) {
                printf("[-] mknod '%s' failed\n", info.drv_path);
                return 1;
            }
            FILE *fp = fopen(SUS_SU_CONF_FILE_PATH, "w");
            if (!fp) { printf("[-] cannot write conf\n"); return 1; }
            fputs(info.drv_path, fp);
            fclose(fp);
            if (system("export DRV_PATH=`cat " SUS_SU_CONF_FILE_PATH
                       "`; chmod 666 ${DRV_PATH} && chcon u:object_r:null_device:s0 ${DRV_PATH}")) {
                printf("[-] chmod failed for '%s'\n", info.drv_path);
                return 1;
            }
        } else if (!strcmp(arg, "0")) {
            info.mode = SUS_SU_DISABLED;
            error = prctl_cmd(CMD_SUSFS_SUS_SU, &info);
            prt_not_supported(CMD_SUSFS_SUS_SU, error);
            if (!error)
                system("export DRV_PATH=`cat " SUS_SU_CONF_FILE_PATH "`; rm -f ${DRV_PATH}");
        } else if (!strcmp(arg, "2")) {
            info.mode = SUS_SU_WITH_HOOKS;
            error = prctl_cmd(CMD_SUSFS_SUS_SU, &info);
            prt_not_supported(CMD_SUSFS_SUS_SU, error);
        } else {
            print_help(); return 1;
        }
        return error;
    }

    int last_mode = 0;
    int r = prctl_cmd(CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE, &last_mode);
    prt_not_supported(CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE, r);
    if (r) return r;

    if (!strcmp(arg, "show_working_mode")) {
        printf("%d\n", last_mode);
        return 0;
    }
    if (!strcmp(arg, "1")) {
        printf("[-] sus_su mode 1 is deprecated\n");
        return 1;
    }

    char *ep;
    int target = (int)strtol(arg, &ep, 10);
    if (*ep) { print_help(); return 1; }

    if (target == SUS_SU_WITH_HOOKS) {
        bool ready = false;
        r = prctl_cmd(CMD_SUSFS_IS_SUS_SU_READY, &ready);
        prt_not_supported(CMD_SUSFS_IS_SUS_SU_READY, r);
        if (r) return r;
        if (!ready) {
            printf("[-] sus_su mode %d must be run during or after service stage\n",
                   SUS_SU_WITH_HOOKS);
            return 1;
        }
        if (last_mode == SUS_SU_WITH_HOOKS) {
            printf("[-] sus_su is already in mode %d\n", last_mode);
            return 1;
        }
        if (last_mode != SUS_SU_DISABLED) {
            r = enable_sus_su_prctl(last_mode, SUS_SU_DISABLED);
            if (r) return r;
        }
        return enable_sus_su_prctl(last_mode, SUS_SU_WITH_HOOKS);
    }

    if (target == SUS_SU_DISABLED) {
        if (last_mode == SUS_SU_DISABLED) {
            printf("[-] sus_su is already in mode %d\n", last_mode);
            return 1;
        }
        return enable_sus_su_prctl(last_mode, SUS_SU_DISABLED);
    }

    print_help();
    return 1;
}

static int legacy_main(int argc, char *argv[]) {
    pre_check();
    detect_susfs_version();
    load_sus_path_layout_cache();

    if (argc < 2) {
        print_help();
        return 0;
    }

    const char *cmd = argv[1];

    if (!strcmp(cmd, "--help") && argc == 3) {
        print_more_help(argv[2]);
        return 0;
    }

    if (!strcmp(cmd, "add_sus_path") && argc == 3)
        return cmd_add_sus_path(argv[2], false);

    if (!strcmp(cmd, "add_sus_path_loop") && argc == 3) {
        return cmd_add_sus_path(argv[2], true);
    }

    if (!strcmp(cmd, "set_android_data_root_path") && argc == 3) {
        if (!HAVE(158)) { printf("[-] Requires susfs v1.5.8+\n"); return 1; }
        return cmd_set_external_dir(argv[2], CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH);
    }

    if (!strcmp(cmd, "set_sdcard_root_path") && argc == 3) {
        if (!HAVE(158)) { printf("[-] Requires susfs v1.5.8+\n"); return 1; }
        return cmd_set_external_dir(argv[2], CMD_SUSFS_SET_SDCARD_ROOT_PATH);
    }

    if (!strcmp(cmd, "add_sus_mount") && argc == 3)
        return cmd_add_sus_mount(argv[2]);

    if (!strcmp(cmd, "hide_sus_mnts_for_all_procs") && argc == 3) {
        if (g_abi == ABI_v2000) {
            printf("[-] v2.0.0+: use 'hide_sus_mnts_for_non_su_procs' instead\n");
            return 1;
        }
        if (!HAVE(158)) { printf("[-] Requires susfs v1.5.8+\n"); return 1; }
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { print_help(); return 1; }
        return cmd_hide_sus_mnts(atoi(argv[2]));
    }

    if (!strcmp(cmd, "hide_sus_mnts_for_non_su_procs") && argc == 3) {
        if (g_abi != ABI_v2000) {
            printf("[-] v1.5.x: use 'hide_sus_mnts_for_all_procs' instead\n");
            return 1;
        }
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { print_help(); return 1; }
        return cmd_hide_sus_mnts(atoi(argv[2]));
    }

    if (!strcmp(cmd, "umount_for_zygote_iso_service") && argc == 3) {
        if (!HAVE(1510)) { printf("[-] Requires susfs v1.5.10+\n"); return 1; }
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { print_help(); return 1; }
        return cmd_umount_zygote_iso(atoi(argv[2]));
    }

    if (!strcmp(cmd, "add_sus_kstat_statically") && argc == 15)
        return cmd_add_sus_kstat_statically(argv);

    if (!strcmp(cmd, "add_sus_kstat") && argc == 3)
        return cmd_add_sus_kstat(argv[2]);

    if (!strcmp(cmd, "update_sus_kstat") && argc == 3)
        return cmd_update_sus_kstat(argv[2], false);

    if (!strcmp(cmd, "update_sus_kstat_full_clone") && argc == 3)
        return cmd_update_sus_kstat(argv[2], true);

    if (!strcmp(cmd, "add_try_umount") && argc == 4) {
        if (strcmp(argv[3], "0") && strcmp(argv[3], "1")) { print_help(); return 1; }
        return cmd_add_try_umount(argv[2], atoi(argv[3]));
    }

    if (!strcmp(cmd, "run_try_umount") && argc == 2) {
        if (HAVE(1512)) { printf("[-] run_try_umount is not available in susfs v1.5.12+\n"); return 1; }
        int r = prctl_cmd_scalar(CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS, 0);
        prt_not_supported(CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS, r);
        return r;
    }

    if (!strcmp(cmd, "set_uname") && argc == 4)
        return cmd_set_uname(argv[2], argv[3]);

    if (!strcmp(cmd, "enable_log") && argc == 3) {
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { print_help(); return 1; }
        return cmd_enable_log(atoi(argv[2]));
    }

    if (!strcmp(cmd, "set_bootconfig") && argc == 3) {
        if (HAVE(154)) {
            printf("[-] v1.5.4+: use 'set_cmdline_or_bootconfig' instead\n");
            return 1;
        }
        return cmd_set_cmdline_or_bootconfig(argv[2]);
    }

    if (!strcmp(cmd, "set_cmdline_or_bootconfig") && argc == 3) {
        if (!HAVE(154) && g_abi != ABI_v2000) {
            printf("[-] v1.5.2/v1.5.3: use 'set_bootconfig' instead\n");
            return 1;
        }
        return cmd_set_cmdline_or_bootconfig(argv[2]);
    }

    if (!strcmp(cmd, "add_open_redirect") && argc == 4 && !HAVE(2100))
        return cmd_add_open_redirect(argv[2], argv[3]);
    if (!strcmp(cmd, "add_open_redirect") && argc == 5 && HAVE(2100))
        return cmd_add_open_redirect_2100(argv[2], argv[3], argv[4]);

    if (!strcmp(cmd, "add_sus_map") && argc == 3) {
        if (!HAVE(1512)) { printf("[-] Requires susfs v1.5.12+\n"); return 1; }
        return cmd_add_sus_map(argv[2]);
    }

    if (!strcmp(cmd, "enable_avc_log_spoofing") && argc == 3) {
        if (!HAVE(1510)) { printf("[-] Requires susfs v1.5.10+\n"); return 1; }
        if (strcmp(argv[2], "0") && strcmp(argv[2], "1")) { print_help(); return 1; }
        return cmd_enable_avc_log_spoofing(atoi(argv[2]));
    }

    if (!strcmp(cmd, "show") && argc == 3) {
        if (!HAVE(153)) { printf("[-] Requires susfs v1.5.3+\n"); return 1; }
        return cmd_show(argv[2]);
    }

    if (!strcmp(cmd, "sus_su") && argc == 3) {
        if (g_abi == ABI_v2000) {
            printf("[-] sus_su is deprecated in v2.0.0+\n");
            return 1;
        }
        return cmd_sus_su(argv[2]);
    }

    print_help();
    return 254;
}

static int backend_exit_code(void) {
    switch (g_backend_state) {
    case PROBE_NATIVE:
    case PROBE_LKM:
        return 0;
    case PROBE_NONE:
        return 3;
    case PROBE_UNSUPPORTED:
        return 4;
    case PROBE_PERMISSION:
        return 5;
    case PROBE_PROTOCOL:
        return 6;
    default:
        return 6;
    }
}

static const char *backend_name(void) {
    switch (g_backend_state) {
    case PROBE_NATIVE:
        return "native";
    case PROBE_LKM:
        return "lkm";
    case PROBE_NONE:
        return "none";
    case PROBE_UNSUPPORTED:
        return "unsupported";
    case PROBE_PERMISSION:
        return "permission";
    case PROBE_PROTOCOL:
        return "protocol";
    default:
        return "unknown";
    }
}

static char *stream_to_string(FILE *f) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    for (;;) {
        if (len + 2048 + 1 > cap) {
            size_t next = cap * 2;
            char *grown = realloc(buf, next);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap = next;
        }
        size_t got = fread(buf + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0) {
            if (ferror(f)) {
                free(buf);
                return NULL;
            }
            break;
        }
    }
    buf[len] = '\0';
    return buf;
}

static char *read_file_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *buf = stream_to_string(f);
    fclose(f);
    return buf;
}

static int write_node(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    size_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    int saved = errno;
    close(fd);
    if (written != (ssize_t)len)
        return written < 0 ? -saved : -EIO;
    return 0;
}

static int write_file_atomic(const char *path, const char *content) {
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, getpid()) >= (int)sizeof(tmp))
        return -ENAMETOOLONG;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return -errno;
    size_t len = strlen(content);
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, content + off, len - off);
        if (n < 0) {
            int saved = errno;
            close(fd);
            unlink(tmp);
            return -saved;
        }
        off += (size_t)n;
    }
    if (fsync(fd) != 0) {
        int saved = errno;
        close(fd);
        unlink(tmp);
        return -saved;
    }
    close(fd);
    if (rename(tmp, path) != 0) {
        int saved = errno;
        unlink(tmp);
        return -saved;
    }
    return 0;
}

static int capture_legacy(int argc, char **argv, char **out, char **err) {
    FILE *fo = tmpfile();
    FILE *fe = tmpfile();
    if (!fo || !fe) {
        if (fo) fclose(fo);
        if (fe) fclose(fe);
        return 7;
    }
    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);
    if (saved_out < 0 || saved_err < 0) {
        if (saved_out >= 0) close(saved_out);
        if (saved_err >= 0) close(saved_err);
        fclose(fo);
        fclose(fe);
        return 7;
    }
    fflush(stdout);
    fflush(stderr);
    dup2(fileno(fo), STDOUT_FILENO);
    dup2(fileno(fe), STDERR_FILENO);
    int rc = legacy_main(argc, argv);
    fflush(stdout);
    fflush(stderr);
    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_err, STDERR_FILENO);
    close(saved_out);
    close(saved_err);
    fseek(fo, 0, SEEK_SET);
    fseek(fe, 0, SEEK_SET);
    *out = stream_to_string(fo);
    *err = stream_to_string(fe);
    fclose(fo);
    fclose(fe);
    if (rc == 0)
        return 0;
    if (rc == 254)
        return 2;
    if (rc < 0)
        return 7;
    return 7;
}

static cJSON *capabilities_array(void) {
    cJSON *arr = cJSON_CreateArray();
    char *argv[] = { "susfsctl", "show", "enabled_features", NULL };
    char *out = NULL;
    char *err = NULL;
    int rc = capture_legacy(3, argv, &out, &err);
    free(err);
    if (rc == 0 && out) {
        char *save = NULL;
        char *line = strtok_r(out, "\r\n", &save);
        while (line) {
            while (*line == ' ' || *line == '\t')
                line++;
            char *end = line + strlen(line);
            while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
                *--end = '\0';
            if (*line)
                cJSON_AddItemToArray(arr, cJSON_CreateString(line));
            line = strtok_r(NULL, "\r\n", &save);
        }
    }
    free(out);
    return arr;
}

static cJSON *base_response(int ok, int code, const char *error) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", ok);
    cJSON_AddNumberToObject(resp, "code", code);
    cJSON_AddStringToObject(resp, "backend", backend_name());
    cJSON_AddStringToObject(resp, "version", g_version_string ? g_version_string : "unknown");
    cJSON_AddItemToObject(resp, "capabilities", capabilities_array());
    if (error)
        cJSON_AddStringToObject(resp, "error", error);
    else
        cJSON_AddNullToObject(resp, "error");
    return resp;
}

static int emit_json(cJSON *resp, int code) {
    char *text = cJSON_PrintUnformatted(resp);
    if (!text)
        return 7;
    if (code == 0)
        printf("%s\n", text);
    else
        fprintf(stderr, "%s\n", text);
    free(text);
    cJSON_Delete(resp);
    return code;
}

static int emit_error(const char *message, int code, cJSON *data) {
    cJSON *resp = base_response(0, code, message);
    cJSON_AddItemToObject(resp, "data", data ? data : cJSON_CreateObject());
    return emit_json(resp, code);
}

static int emit_success(const char *operation, cJSON *data) {
    cJSON *resp = base_response(1, 0, NULL);
    cJSON_AddStringToObject(resp, "operation", operation);
    cJSON_AddItemToObject(resp, "data", data ? data : cJSON_CreateObject());
    return emit_json(resp, 0);
}

static cJSON *make_status_data(void) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "state", backend_name());
    cJSON_AddStringToObject(data, "version", g_version_string ? g_version_string : "unknown");
    cJSON_AddNumberToObject(data, "abi", g_abi);
    cJSON_AddNumberToObject(data, "last_errno", g_last_errno);
    const char *nodes[] = {
        "kstat", "open_redirect", "path", "hide_modules", "hide_mounts", "opath", NULL
    };
    cJSON *diagnostics = cJSON_CreateObject();
    for (int i = 0; nodes[i]; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/proc/susfs_%s", nodes[i]);
        char *text = read_file_text(path);
        if (text) {
            cJSON_AddStringToObject(diagnostics, nodes[i], text);
            free(text);
        }
    }
    cJSON_AddItemToObject(data, "diagnostics", diagnostics);
    return data;
}

static bool safe_value(const char *s) {
    if (!s || !*s)
        return false;
    if (strlen(s) > 255)
        return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == '/' || *p == ':'))
            return false;
    }
    return true;
}

static bool known_config_key(const char *key) {
    static const char *keys[] = {
        "susfs_log", "sus_su", "sus_su_active", "hide_cusrom", "hide_vendor_sepolicy",
        "hide_compat_matrix", "hide_gapps", "hide_revanced", "spoof_cmdline", "hide_loops",
        "force_hide_lsposed", "spoof_uname", "hide_sus_mnts_for_all_or_non_su_procs",
        "umount_for_zygote_iso_service", "auto_try_umount", "skip_legit_mounts",
        "avc_log_spoofing", "emulate_vold_app_data", "kernel_version", "kernel_build", NULL
    };
    for (int i = 0; keys[i]; i++)
        if (!strcmp(key, keys[i]))
            return true;
    return false;
}

static int config_set_key(const char *key, const char *value) {
    if (!known_config_key(key) || !safe_value(value))
        return -EINVAL;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/config.sh", PERSISTENT_DIR);
    char *old = read_file_text(path);
    if (!old)
        old = strdup("");
    if (!old)
        return -ENOMEM;
    size_t matches = 0;
    const char *scan = old;
    while (*scan) {
        const char *e = strchr(scan, '\n');
        size_t len = e ? (size_t)(e - scan) : strlen(scan);
        if (len > strlen(key) && !strncmp(scan, key, strlen(key)) && scan[strlen(key)] == '=')
            matches++;
        if (!e)
            break;
        scan = e + 1;
    }
    size_t cap = strlen(old) + (strlen(key) + strlen(value) + 2) * (matches ? matches : 1) + 8;
    char *next = calloc(1, cap);
    if (!next) {
        free(old);
        return -ENOMEM;
    }
    bool replaced = false;
    const char *p = old;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > strlen(key) && !strncmp(p, key, strlen(key)) && p[strlen(key)] == '=') {
            strcat(next, key);
            strcat(next, "=");
            strcat(next, value);
            replaced = true;
        } else {
            strncat(next, p, len);
        }
        strcat(next, "\n");
        if (!e)
            break;
        p = e + 1;
    }
    if (!replaced) {
        strcat(next, key);
        strcat(next, "=");
        strcat(next, value);
        strcat(next, "\n");
    }
    int rc = write_file_atomic(path, next);
    free(old);
    free(next);
    return rc;
}

static const char *rule_path(const char *name) {
    if (!strcmp(name, "sus_path")) return PERSISTENT_DIR "/sus_path.txt";
    if (!strcmp(name, "sus_path_loop")) return PERSISTENT_DIR "/sus_path_loop.txt";
    if (!strcmp(name, "sus_map")) return PERSISTENT_DIR "/sus_maps.txt";
    if (!strcmp(name, "sus_mount")) return PERSISTENT_DIR "/sus_mount.txt";
    if (!strcmp(name, "try_umount")) return PERSISTENT_DIR "/try_umount.txt";
    if (!strcmp(name, "open_redirect")) return PERSISTENT_DIR "/sus_open_redirect.txt";
    if (!strcmp(name, "kstat")) return PERSISTENT_DIR "/sus_kstat_statically.json";
    return NULL;
}

static int json_get_string(cJSON *obj, const char *name, const char **out) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(item) || !item->valuestring)
        return -EINVAL;
    *out = item->valuestring;
    return 0;
}

static bool safe_proc_path(const char *s) {
    if (!s || s[0] != '/' || strlen(s) >= SUSFS_MAX_LEN_PATHNAME || !strcmp(s, "/"))
        return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isspace(*p) || *p == '\\')
            return false;
        if (p[0] == '.' && p[1] == '.' &&
            (p == (const unsigned char *)s || p[-1] == '/') &&
            (p[2] == '\0' || p[2] == '/'))
            return false;
    }
    return true;
}

static int json_get_abs_path(cJSON *obj, const char *name, const char **out) {
    if (json_get_string(obj, name, out))
        return -EINVAL;
    if (!safe_proc_path(*out))
        return -EINVAL;
    return 0;
}

static bool safe_component(const char *s) {
    if (!s || !*s || strlen(s) >= SUSFS_MAX_LEN_PATHNAME)
        return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isspace(*p) || *p == '/' || *p == '\\')
            return false;
        if (p[0] == '.' && p[1] == '.')
            return false;
    }
    return true;
}

static int json_get_boolish(cJSON *obj, const char *name, bool *out) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return 0;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble != 0;
        return 0;
    }
    return -EINVAL;
}

static bool legacy_command_blocked(const char *cmd);

static int proc_operation(const char *operation, cJSON *values) {
    if (g_backend_state != PROBE_LKM)
        return -ENOTSUP;
    const char *path = NULL;
    const char *name = NULL;
    char command[PATH_MAX + 32];
    if (!strcmp(operation, "path.clear")) return write_node("/proc/susfs_path", "clear");
    if (!strcmp(operation, "kstat.clear")) return write_node("/proc/susfs_kstat", "clear");
    if (!strcmp(operation, "redirect.clear")) return write_node("/proc/susfs_open_redirect", "clear");
    if (!strcmp(operation, "modules.clear")) return write_node("/proc/susfs_hide_modules", "clear");
    if (!strcmp(operation, "mounts.clear")) return write_node("/proc/susfs_hide_mounts", "clear");
    if (!strcmp(operation, "mounts.reset")) return write_node("/proc/susfs_hide_mounts", "reset");
    if (!strcmp(operation, "path.delete")) {
        if (json_get_abs_path(values, "path", &path)) return -EINVAL;
        snprintf(command, sizeof(command), "del %s", path);
        return write_node("/proc/susfs_path", command);
    }
    if (!strcmp(operation, "kstat.delete")) {
        if (json_get_abs_path(values, "path", &path)) return -EINVAL;
        snprintf(command, sizeof(command), "del %s", path);
        return write_node("/proc/susfs_kstat", command);
    }
    if (!strcmp(operation, "kstat.full_clone")) {
        if (json_get_abs_path(values, "path", &path)) return -EINVAL;
        snprintf(command, sizeof(command), "update_sus_kstat_full_clone %s", path);
        return write_node("/proc/susfs_kstat", command);
    }
    if (!strcmp(operation, "redirect.delete")) {
        if (json_get_abs_path(values, "path", &path)) return -EINVAL;
        snprintf(command, sizeof(command), "del %s", path);
        return write_node("/proc/susfs_open_redirect", command);
    }
    if (!strcmp(operation, "modules.add") || !strcmp(operation, "modules.del")) {
        if (json_get_string(values, "name", &name) || !safe_component(name)) return -EINVAL;
        snprintf(command, sizeof(command), "%s %s", !strcmp(operation, "modules.add") ? "add" : "del", name);
        return write_node("/proc/susfs_hide_modules", command);
    }
    if (!strcmp(operation, "mounts.add") || !strcmp(operation, "mounts.del")) {
        if (json_get_abs_path(values, "prefix", &path)) return -EINVAL;
        snprintf(command, sizeof(command), "%s %s", !strcmp(operation, "mounts.add") ? "add" : "del", path);
        return write_node("/proc/susfs_hide_mounts", command);
    }
    if (!strcmp(operation, "modules.set") || !strcmp(operation, "mounts.set")) {
        cJSON *items = cJSON_GetObjectItemCaseSensitive(values, "items");
        if (!cJSON_IsArray(items)) return -EINVAL;
        const char *node = !strcmp(operation, "modules.set") ? "/proc/susfs_hide_modules" : "/proc/susfs_hide_mounts";
        if (cJSON_GetArraySize(items) == 0)
            return write_node(node, "clear");
        snprintf(command, sizeof(command), "set");
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, items) {
            if (!cJSON_IsString(item)) return -EINVAL;
            if (!strcmp(operation, "mounts.set")) {
                if (item->valuestring[0] != '/' || strlen(item->valuestring) >= SUSFS_MAX_LEN_PATHNAME || !strcmp(item->valuestring, "/"))
                    return -EINVAL;
            } else if (!safe_component(item->valuestring)) {
                return -EINVAL;
            }
            if (strlen(command) + strlen(item->valuestring) + 2 >= sizeof(command)) return -E2BIG;
            strcat(command, " ");
            strcat(command, item->valuestring);
        }
        return write_node(node, command);
    }
    if (!strcmp(operation, "log.set") || !strcmp(operation, "avc.set")) {
        bool enabled = false;
        if (json_get_boolish(values, "enabled", &enabled)) return -EINVAL;
        return write_node(!strcmp(operation, "log.set") ? "/proc/susfs_enable_log" : "/proc/susfs_avc_spoof", enabled ? "1" : "0");
    }
    return -ENOTSUP;
}

static int json_dispatch(cJSON *root) {
    const char *operation = NULL;
    if (json_get_string(root, "operation", &operation))
        return emit_error("operation must be a string", 2, NULL);
    cJSON *values = cJSON_GetObjectItemCaseSensitive(root, "values");
    if (!cJSON_IsObject(values))
        values = cJSON_CreateObject();

    if (!strcmp(operation, "probe") || !strcmp(operation, "status")) {
        if (g_backend_state == PROBE_NONE)
            return emit_error("no susfs backend", 3, make_status_data());
        if (g_backend_state == PROBE_UNSUPPORTED)
            return emit_error("unsupported susfs backend", 4, make_status_data());
        if (g_backend_state == PROBE_PERMISSION)
            return emit_error("root permission required", 5, make_status_data());
        if (g_backend_state == PROBE_PROTOCOL || g_backend_state == PROBE_UNKNOWN)
            return emit_error("susfs protocol error", 6, make_status_data());
        return emit_success(operation, make_status_data());
    }

    if (!strcmp(operation, "command")) {
        cJSON *items = cJSON_GetObjectItemCaseSensitive(values, "argv");
        if (!cJSON_IsArray(items))
            return emit_error("argv must be an array", 2, NULL);
        int count = cJSON_GetArraySize(items);
        if (count < 1 || count > 32)
            return emit_error("argv length invalid", 2, NULL);
        char *argv[34] = { "susfsctl" };
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) > 4096 || strchr(item->valuestring, '\0') != item->valuestring + strlen(item->valuestring))
                return emit_error("argv item invalid", 2, NULL);
            if (g_backend_state == PROBE_LKM) {
                for (const unsigned char *p = (const unsigned char *)item->valuestring; *p; p++) {
                    if (isspace(*p))
                        return emit_error("argv item contains whitespace", 2, NULL);
                }
            }
            argv[i + 1] = item->valuestring;
        }
        int backend_code = backend_exit_code();
        if (backend_code != 0)
            return emit_error("backend is not ready", backend_code, NULL);
        if (g_backend_state == PROBE_LKM && legacy_command_blocked(argv[1]))
            return emit_error("command is not supported by lkm backend", 4, NULL);
        if (g_backend_state == PROBE_LKM && count == 2 && !strcmp(argv[1], "update_sus_kstat_full_clone")) {
            if (!safe_proc_path(argv[2]))
                return emit_error("path is invalid", 2, NULL);
            char command[SUSFS_MAX_LEN_PATHNAME + 64];
            snprintf(command, sizeof(command), "update_sus_kstat_full_clone %s", argv[2]);
            if (write_node("/proc/susfs_kstat", command))
                return emit_error("full clone failed", 7, NULL);
            cJSON *data = cJSON_CreateObject();
            cJSON_AddStringToObject(data, "operation", argv[1]);
            return emit_success(operation, data);
        }
        char *out = NULL;
        char *err = NULL;
        int rc = capture_legacy(count + 1, argv, &out, &err);
        if (rc != 0) {
            cJSON *data = cJSON_CreateObject();
            if (err) cJSON_AddStringToObject(data, "stderr", err);
            if (out) cJSON_AddStringToObject(data, "stdout", out);
            free(out);
            free(err);
            return emit_error("command failed", rc, data);
        }
        cJSON *data = cJSON_CreateObject();
        if (out) cJSON_AddStringToObject(data, "stdout", out);
        if (err) cJSON_AddStringToObject(data, "stderr", err);
        free(out);
        free(err);
        return emit_success(operation, data);
    }

    if (!strcmp(operation, "config.get")) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/config.sh", PERSISTENT_DIR);
        char *text = read_file_text(path);
        cJSON *data = cJSON_CreateObject();
        if (text) {
            cJSON_AddStringToObject(data, "content", text);
            free(text);
        }
        return emit_success(operation, data);
    }

    if (!strcmp(operation, "config.set")) {
        const char *key = NULL;
        const char *value = NULL;
        if (json_get_string(values, "key", &key) || json_get_string(values, "value", &value))
            return emit_error("key and value must be strings", 2, NULL);
        int rc = config_set_key(key, value);
        if (rc)
            return emit_error("config update failed", rc == -EINVAL || rc == -E2BIG ? 2 : 7, NULL);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "key", key);
        cJSON_AddStringToObject(data, "value", value);
        return emit_success(operation, data);
    }

    if (!strcmp(operation, "rules.replace")) {
        const char *rule = NULL;
        const char *content = NULL;
        if (json_get_string(values, "rule", &rule) || json_get_string(values, "content", &content))
            return emit_error("rule and content must be strings", 2, NULL);
        const char *path = rule_path(rule);
        if (!path || strlen(content) > 1024 * 1024)
            return emit_error("rule is not allowed", 2, NULL);
        int rc = write_file_atomic(path, content);
        if (rc)
            return emit_error("rule update failed", 7, NULL);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "rule", rule);
        return emit_success(operation, data);
    }

    if (!strcmp(operation, "rules.get")) {
        const char *rule = NULL;
        if (json_get_string(values, "rule", &rule))
            return emit_error("rule must be a string", 2, NULL);
        const char *path = rule_path(rule);
        if (!path)
            return emit_error("rule is not allowed", 2, NULL);
        char *content = read_file_text(path);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "content", content ? content : "");
        free(content);
        return emit_success(operation, data);
    }

    int rc = proc_operation(operation, values);
    if (rc == -ENOTSUP) {
        if (g_backend_state == PROBE_NONE)
            return emit_error("no susfs backend", 3, make_status_data());
        if (g_backend_state == PROBE_PERMISSION)
            return emit_error("root permission required", 5, make_status_data());
        if (g_backend_state == PROBE_PROTOCOL || g_backend_state == PROBE_UNKNOWN)
            return emit_error("susfs protocol error", 6, make_status_data());
        return emit_error("operation is not available for this backend", 4, NULL);
    }
    if (rc == -EINVAL || rc == -E2BIG)
        return emit_error("invalid operation values", 2, NULL);
    if (rc)
        return emit_error("kernel operation failed", 7, NULL);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "operation", operation);
    return emit_success(operation, data);
}

static int json_main(int argc, char **argv) {
    if (argc != 3)
        return emit_error("usage: susfsctl --json <request>", 2, NULL);
    for (const char *p = argv[2]; (p = strstr(p, "\\u0000")) != NULL; p += 6) {
        return emit_error("invalid json", 2, NULL);
    }
    cJSON *root = cJSON_ParseWithOpts(argv[2], NULL, true);
    if (!root)
        return emit_error("invalid json", 2, NULL);
    int rc = json_dispatch(root);
    cJSON_Delete(root);
    return rc;
}

static bool legacy_command_blocked(const char *cmd) {
    static const char *blocked[] = {
        "add_sus_mount", "add_try_umount", "set_android_data_root_path", "set_sdcard_root_path",
        "umount_for_zygote_iso_service", "sus_su", "run_try_umount", NULL
    };
    for (int i = 0; blocked[i]; i++)
        if (!strcmp(cmd, blocked[i]))
            return true;
    return false;
}

static int run_legacy_silent(int argc, char **argv) {
    char *out = NULL;
    char *err = NULL;
    int rc = capture_legacy(argc, argv, &out, &err);
    if (rc == 0) {
        if (out)
            fputs(out, stdout);
    } else {
        if (out)
            fputs(out, stderr);
        if (err)
            fputs(err, stderr);
    }
    free(out);
    free(err);
    return rc;
}

int main(int argc, char *argv[]) {
    pre_check();
    detect_susfs_version();
    load_sus_path_layout_cache();

    if (g_backend_state == PROBE_PERMISSION) {
        fprintf(stderr, "susfsctl: root permission required\n");
        return 5;
    }

    if (argc == 3 && !strcmp(argv[1], "--json"))
        return json_main(argc, argv);

    if (argc == 2 && (!strcmp(argv[1], "probe") || !strcmp(argv[1], "status"))) {
        int code = backend_exit_code();
        if (code == 0)
            printf("%s %s\n", backend_name(), g_version_string ? g_version_string : "unknown");
        else
            fprintf(stderr, "susfsctl: %s\n", backend_name());
        return code;
    }

    if (argc >= 2 && !strcmp(argv[1], "--help"))
        return run_legacy_silent(argc, argv);

    int code = backend_exit_code();
    if (code != 0) {
        fprintf(stderr, "susfsctl: %s\n", backend_name());
        return code;
    }

    if (g_backend_state == PROBE_LKM && argc >= 2 && legacy_command_blocked(argv[1])) {
        fprintf(stderr, "susfsctl: command is not supported by lkm backend\n");
        return 4;
    }

    if (g_backend_state == PROBE_LKM && argc == 3 && !strcmp(argv[1], "update_sus_kstat_full_clone")) {
        char command[512];
        if (!safe_proc_path(argv[2]))
            return 2;
        snprintf(command, sizeof(command), "update_sus_kstat_full_clone %s", argv[2]);
        int rc = write_node("/proc/susfs_kstat", command);
        if (rc) {
            fprintf(stderr, "susfsctl: full clone failed\n");
            return 7;
        }
        return 0;
    }

    return run_legacy_silent(argc, argv);
}
