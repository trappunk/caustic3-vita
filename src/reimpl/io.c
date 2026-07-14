/*
 * reimpl/io.c
 *
 * Wrappers and implementations for some of the IO functions.
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2022 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <psp2/kernel/threadmgr.h>
#include <libc_bridge/libc_bridge.h>

#include "reimpl/path_policy.h"
#include "utils/logger.h"

extern void debug_log(const char *msg);

#define MUSL_O_WRONLY         01
#define MUSL_O_RDWR           02
#define MUSL_O_CREAT        0100
#define MUSL_O_EXCL         0200
#define MUSL_O_TRUNC       01000
#define MUSL_O_APPEND      02000
#define MUSL_O_NONBLOCK    04000

int oflags_newlib_to_oflags_musl(int flags)
{
    int out = 0;
    if (flags & MUSL_O_RDWR)
        out |= O_RDWR;
    else if (flags & MUSL_O_WRONLY)
        out |= O_WRONLY;
    else
        out |= O_RDONLY;
    if (flags & MUSL_O_NONBLOCK)
        out |= O_NONBLOCK;
    if (flags & MUSL_O_APPEND)
        out |= O_APPEND;
    if (flags & MUSL_O_CREAT)
        out |= O_CREAT;
    if (flags & MUSL_O_TRUNC)
        out |= O_TRUNC;
    if (flags & MUSL_O_EXCL)
        out |= O_EXCL;
    return out;
}

static void dirent_newlib_to_dirent_bionic(const struct dirent *src,
                                            dirent64_bionic *dst) {
    memset(dst, 0, sizeof(*dst));
    dst->d_ino = 0;
    dst->d_off = 0;
    dst->d_reclen = sizeof(*dst);
    dst->d_type = SCE_S_ISDIR(src->d_stat.st_mode) ? DT_DIR : DT_REG;
    strncpy(dst->d_name, src->d_name, sizeof(dst->d_name) - 1);
}

void stat_newlib_to_stat_bionic(struct stat * src, stat64_bionic * dst) {
    if (!src) return;
    if (!dst) dst = malloc(sizeof(stat64_bionic));

    dst->st_dev = src->st_dev;
    dst->st_ino = src->st_ino;
    dst->st_mode = src->st_mode;
    dst->st_nlink = src->st_nlink;
    dst->st_uid = src->st_uid;
    dst->st_gid = src->st_gid;
    dst->st_rdev = src->st_rdev;
    dst->st_size = src->st_size;
    dst->st_blksize = src->st_blksize;
    dst->st_blocks = src->st_blocks;
    dst->st_atime = src->st_atime;
    dst->st_atime_nsec = 0;
    dst->st_mtime = src->st_mtime;
    dst->st_mtime_nsec = 0;
    dst->st_ctime = src->st_ctime;
    dst->st_ctime_nsec = 0;
}

dirent64_bionic *readdir_soloader(DIR *dir) {
    static dirent64_bionic converted;
    struct dirent *native = readdir(dir);
    log_debug("[io] readdir()");
    if (!native)
        return NULL;
    dirent_newlib_to_dirent_bionic(native, &converted);
    return &converted;
}

int readdir_r_soloader(DIR *dirp, dirent64_bionic *entry, dirent64_bionic **result) {
    struct dirent dirent_tmp;
    struct dirent* pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0 && pdirent_tmp != NULL) {
        dirent_newlib_to_dirent_bionic(&dirent_tmp, entry);
        *result = (pdirent_tmp != NULL) ? entry : NULL;
    } else if (ret == 0)
        *result = NULL;

    log_debug("[io] readdir_r()");
    return ret;
}

int fopenc = 0;
int caustic_preset_io_active = 0;

static int fopen_mode_writes(const char *mode) {
    return mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
}

static void log_rejected_path(const char *operation, const char *path) {
    char line[1200];
    sceClibSnprintf(line, sizeof(line),
                    "Caustic file: %s rejected path=%s errno=%d",
                    operation, path ? path : "(null)", errno);
    debug_log(line);
}

FILE *fopen_soloader(char *fname, char *mode) {
    char fopen_path_real[1024];
    if (caustic_map_path(fname, fopen_path_real, sizeof(fopen_path_real),
                         fopen_mode_writes(mode)) < 0) {
        log_rejected_path("fopen", fname);
        return NULL;
    }

    FILE* ret = sceLibcBridge_fopen(fopen_path_real, mode);
    if (ret && strstr(fopen_path_real, "/presets/"))
        caustic_preset_io_active = 1;
    if (ret) fopenc++;
    {
        char line[1200];
        sceClibSnprintf(line, sizeof(line),
                        "Caustic file: fopen path=%s mode=%s result=0x%08X",
                        fopen_path_real, mode ? mode : "(null)", (unsigned int)ret);
        debug_log(line);
    }
    logv_debug("[io] fopen:%i(%s): 0x%x", fopenc, fopen_path_real, ret);
    return ret;
}


int open_soloader(char *_fname, int flags) {
    char mapped[1024];
    flags = oflags_newlib_to_oflags_musl(flags);
    int writes = flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND);
    if (caustic_map_path(_fname, mapped, sizeof(mapped), writes) < 0) {
        log_rejected_path("open", _fname);
        return -1;
    }
    int ret = open(mapped, flags);
    {
        char line[1200];
        sceClibSnprintf(line, sizeof(line),
                        "Caustic file: open path=%s flags=0x%X result=0x%08X",
                        mapped, flags, ret);
        debug_log(line);
    }
    logv_debug("[io] open(%s, %x): %i", mapped, flags, ret);
    return ret;
}

int read_soloader(int fd, void * buf, size_t nbyte) {
    int ret = read(fd, buf, nbyte);
    logv_debug("[io] read(fd#%i, 0x%x, %i): %i", fd, buf, nbyte, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    char mapped[1024];
    if (caustic_map_path(_pathname, mapped, sizeof(mapped), 0) < 0) {
        log_rejected_path("opendir", _pathname);
        return NULL;
    }
    DIR* ret = opendir(mapped);
    {
        char line[1200];
        sceClibSnprintf(line, sizeof(line),
                        "Caustic file: opendir path=%s result=0x%08X",
                        mapped, (unsigned int)ret);
        debug_log(line);
    }
    logv_debug("[io] opendir(\"%s\"): 0x%x", mapped, ret);
    return ret;
}

int fstat_soloader(int fd, void *statbuf) {
    struct stat st;
    int res = fstat(fd, &st);
    if (res == 0)
        stat_newlib_to_stat_bionic(&st, statbuf);

    logv_debug("[io] fstat(fd#%i): %i", fd, res);
    return res;
}

int write_soloader(int fd, const void *buf, int count) {
    int ret = write(fd, buf, count);
    logv_debug("[io] write(fd#%i, 0x%x, %i): %i", fd, buf, count, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    logv_debug("[io] fcntl(fd#%i, cmd#%i)", fd, cmd);
    return 0;
}

off_t lseek_soloader(int fildes, off_t offset, int whence) {
    off_t ret = lseek(fildes, offset, whence);
    logv_debug("[io] lseek(fd#i, %i, %i): %i", fildes, offset, whence, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    logv_debug("[io] close(fd#%i): %i", fd, ret);
    return ret;
}

int fclose_soloader(FILE * f) {
    fopenc--;
    int ret = sceLibcBridge_fclose(f);
    caustic_preset_io_active = 0;
    //logv_debug("[io] fclose(0x%x): %i", f, ret);
    return ret;
}

int closedir_soloader(DIR* dir) {
    int ret = closedir(dir);
    logv_debug("[io] closedir(0x%x): %i", dir, ret);
    return ret;
}

int stat_soloader(char *_pathname, stat64_bionic *statbuf) {
    char mapped[1024];
    if (caustic_map_path(_pathname, mapped, sizeof(mapped), 0) < 0) {
        log_rejected_path("stat", _pathname);
        return -1;
    }
    struct stat st;
    int res = stat(mapped, &st);

    {
        char line[1200];
        sceClibSnprintf(line, sizeof(line),
                        "Caustic file: stat path=%s result=%d", mapped, res);
        debug_log(line);
    }

    if (res == 0)
        stat_newlib_to_stat_bionic(&st, statbuf);

    logv_debug("[io] stat(%s): %i", _pathname, res);
    return res;
}

int mkdir_soloader(const char *pathname, mode_t mode) {
    char mapped[1024];
    if (caustic_map_path(pathname, mapped, sizeof(mapped), 1) < 0) {
        log_rejected_path("mkdir", pathname);
        return -1;
    }
    return mkdir(mapped, mode);
}

int remove_soloader(const char *pathname) {
    char mapped[1024];
    if (caustic_map_path(pathname, mapped, sizeof(mapped), 1) < 0) {
        log_rejected_path("remove", pathname);
        return -1;
    }
    return remove(mapped);
}

int unlink_soloader(const char *pathname) {
    char mapped[1024];
    if (caustic_map_path(pathname, mapped, sizeof(mapped), 1) < 0) {
        log_rejected_path("unlink", pathname);
        return -1;
    }
    return unlink(mapped);
}

int rmdir_soloader(const char *pathname) {
    char mapped[1024];
    if (caustic_map_path(pathname, mapped, sizeof(mapped), 1) < 0) {
        log_rejected_path("rmdir", pathname);
        return -1;
    }
    return rmdir(mapped);
}

int chmod_soloader(const char *pathname, mode_t mode) {
    char mapped[1024];
    if (caustic_map_path(pathname, mapped, sizeof(mapped), 1) < 0) {
        log_rejected_path("chmod", pathname);
        return -1;
    }
    return chmod(mapped, mode);
}

int rename_soloader(const char *oldpath, const char *newpath) {
    char mapped_old[1024];
    char mapped_new[1024];
    if (caustic_map_path(oldpath, mapped_old, sizeof(mapped_old), 1) < 0) {
        log_rejected_path("rename-source", oldpath);
        return -1;
    }
    if (caustic_map_path(newpath, mapped_new, sizeof(mapped_new), 1) < 0) {
        log_rejected_path("rename-destination", newpath);
        return -1;
    }
    return rename(mapped_old, mapped_new);
}

int fseeko_soloader(FILE * a, off_t b, int c) {
    int ret = fseeko(a,b,c);
    logv_debug("[io] fseeko(0x%x, %i, %i): %i", a,b,c,ret);
    return ret;
}

off_t ftello_soloader(FILE * a) {
    off_t ret = ftello(a);
    logv_debug("[io] ftello(0x%x): %i", a, ret);
    return ret;
}
