/*
 * dynlib.c
 *
 * Resolving dynamic imports of the .so.
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

// Disable IDE complaints about _identifiers and global interfaces
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
#pragma ide diagnostic ignored "cppcoreguidelines-interfaces-global-init"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include "dynlib.h"

#include <libc_bridge/libc_bridge.h>
#include <math.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/appmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/unistd.h>

#include <so_util/so_util.h>
#include <vitaGL.h>

#include "utils/utils.h"
#include "log.h"

extern so_module so_mod;
extern int caustic_preset_io_active;

typedef struct {
    uint32_t f_type;
    uint32_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    uint32_t f_namelen;
    uint32_t f_frsize;
    uint32_t f_flags;
    uint32_t f_spare[4];
} android_statfs;

static int statfs_soloader(const char *path, android_statfs *out) {
    (void)path;
    if (!out)
        return -1;
    uint64_t total = 0;
    uint64_t free_space = 0;
    int result = sceAppMgrGetDevInfo("ux0:", &total, &free_space);
    if (result < 0 || total == 0) {
        /* Saving is safer than falsely reporting a full disk. I/O still
         * returns the real device error if space is genuinely exhausted. */
        total = 4ULL * 1024 * 1024 * 1024;
        free_space = 1024ULL * 1024 * 1024;
    }
    const uint32_t block_size = 4096;
    memset(out, 0, sizeof(*out));
    out->f_type = 0x2011BAB0;
    out->f_bsize = block_size;
    out->f_frsize = block_size;
    out->f_blocks = total / block_size;
    out->f_bfree = free_space / block_size;
    out->f_bavail = out->f_bfree;
    out->f_namelen = 255;
    return 0;
}

static size_t fread_debug(void *ptr, size_t size, size_t count, FILE *stream) {
    size_t result = sceLibcBridge_fread(ptr, size, count, stream);
    if (caustic_preset_io_active) {
        char line[160];
        snprintf(line, sizeof(line), "Caustic preset fread: size=%lu count=%lu result=%lu",
                 (unsigned long)size, (unsigned long)count, (unsigned long)result);
        debug_log(line);
    }
    return result;
}

static int fseek_debug(FILE *stream, long offset, int origin) {
    int result = sceLibcBridge_fseek(stream, offset, origin);
    if (caustic_preset_io_active) {
        char line[160];
        snprintf(line, sizeof(line), "Caustic preset fseek: offset=%ld origin=%d result=%d",
                 offset, origin, result);
        debug_log(line);
    }
    return result;
}

static long ftell_debug(FILE *stream) {
    long result = sceLibcBridge_ftell(stream);
    if (caustic_preset_io_active) {
        char line[128];
        snprintf(line, sizeof(line), "Caustic preset ftell: result=%ld", result);
        debug_log(line);
    }
    return result;
}

static void *malloc_debug(size_t size) {
    void *result = malloc(size);
    if (!result || size >= 4 * 1024 * 1024) {
        char line[144];
        snprintf(line, sizeof(line), "Caustic malloc: size=%lu result=0x%08lX",
                 (unsigned long)size, (unsigned long)(uintptr_t)result);
        debug_log(line);
    }
    return result;
}

static void *calloc_debug(size_t count, size_t size) {
    void *result = calloc(count, size);
    if (!result || (size && count >= (4 * 1024 * 1024) / size)) {
        char line[160];
        snprintf(line, sizeof(line), "Caustic calloc: count=%lu size=%lu result=0x%08lX",
                 (unsigned long)count, (unsigned long)size,
                 (unsigned long)(uintptr_t)result);
        debug_log(line);
    }
    return result;
}

static void *realloc_debug(void *pointer, size_t size) {
    void *result = realloc(pointer, size);
    if (!result || size >= 4 * 1024 * 1024) {
        char line[176];
        snprintf(line, sizeof(line), "Caustic realloc: ptr=0x%08lX size=%lu result=0x%08lX",
                 (unsigned long)(uintptr_t)pointer, (unsigned long)size,
                 (unsigned long)(uintptr_t)result);
        debug_log(line);
    }
    return result;
}

static void glLineWidth_debug(GLfloat width) {
    static unsigned calls;
    if (calls < 32) {
        char line[112];
        snprintf(line, sizeof(line), "Caustic glLineWidth[%u]: width_bits=0x%08X width=%d.%03d",
                 calls, *(uint32_t *)&width, (int)width,
                 abs((int)(width * 1000.0f)) % 1000);
        debug_log(line);
        calls++;
    }
    glLineWidth(width);
}
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/settings.h"
#include "reimpl/io.h"
#include "reimpl/log.h"
#include "reimpl/env.h"
#include "reimpl/mem.h"
#include "reimpl/math_bridge.h"
#include <math_neon.h>
#include <sys/socket.h>
#include <netdb.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <dirent.h>
#include <locale.h>
#include <zlib.h>
#include "reimpl/pthr.h"
#include "reimpl/fortify.h"
#include "log.h"

#if 0
int gethostname(char *name, size_t len) { return 0; }
#endif
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

extern void *__aeabi_atexit;
extern void *__cxa_pure_virtual;
extern void __stack_chk_fail(void);
static uintptr_t android_stack_chk_guard = 0xC0A57C3Du;

extern void * __aeabi_d2f;
extern void * __aeabi_d2iz;
extern void * __aeabi_d2uiz;
extern void * __aeabi_dcmpge;
extern void * __aeabi_dcmple;
extern void * __aeabi_fcmpun;
extern void * __cxa_guard_acquire;
extern void * __aeabi_dadd;
extern void * __aeabi_dcmpeq;
extern void * __aeabi_dcmpgt;
extern void * __aeabi_dcmplt;
extern void * __aeabi_ddiv;
extern void * __aeabi_dmul;
extern void * __aeabi_dsub;
extern void * __aeabi_f2d;
extern void * __aeabi_f2iz;
extern void * __aeabi_f2lz;
extern void * __aeabi_f2ulz;
extern void * __aeabi_fadd;
extern void * __aeabi_fcmpeq;
extern void * __aeabi_fcmpge;
extern void * __aeabi_fcmpgt;
extern void * __aeabi_fcmple;
extern void * __aeabi_fcmplt;
extern void * __aeabi_fdiv;
extern void * __aeabi_fmul;
extern void * __aeabi_fsub;
extern void * __aeabi_i2d;
extern void * __aeabi_i2f;
extern void * __aeabi_idiv;
extern void * __aeabi_idivmod;
extern void * __aeabi_l2f;
extern void * __aeabi_ldivmod;
extern void * __aeabi_lmul;
extern void * __aeabi_ui2d;
extern void * __aeabi_ui2f;
extern void * __aeabi_uidiv;
extern void * __aeabi_uidivmod;
extern void * __aeabi_ul2f;
extern void * __aeabi_uldivmod;
extern void * __dso_handle;
extern void * __swbuf;
extern void * _ZdaPv;
extern void * _ZdlPv;
extern void * _Znaj;
extern void * __aeabi_d2lz;
extern void * __aeabi_ul2d;
extern void * __cxa_guard_release;
extern void * _Znwj;
extern void * _ZSt7nothrow;
extern void * _ZnajRKSt9nothrow_t;
extern void * __srget;

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

static FILE __sF_fake[3];
static FILE *stderr_fake;

extern void *_Unwind_DeleteException;
extern void *_Unwind_GetLanguageSpecificData;
extern void *_Unwind_GetRegionStart;
extern void *_Unwind_RaiseException;
extern void *_Unwind_Resume;
extern void *_Unwind_VRS_Get;
extern void *_Unwind_VRS_Set;
extern void *__gnu_unwind_frame;

/* Bionic's armeabi-v7a timeval is two signed 32-bit fields. Use Vita's
   monotonic microsecond counter instead of newlib's wall-clock shim: Caustic
   calls this in a busy frame limiter and will spin forever if gettimeofday
   fails or leaves its stack timeval unchanged. */
typedef struct {
    int32_t tv_sec;
    int32_t tv_usec;
} android_timeval;

static int gettimeofday_soloader(android_timeval *tv, void *timezone) {
    (void)timezone;
    if (!tv)
        return -1;
    uint64_t micros = sceKernelGetProcessTimeWide();
    tv->tv_sec = (int32_t)(micros / 1000000ULL);
    tv->tv_usec = (int32_t)(micros % 1000000ULL);
    return 0;
}

static void abort_debug(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    char line[160];
    if (caller >= so_mod.text_base && caller < so_mod.text_base + 0x05000000) {
        snprintf(line, sizeof(line),
                 "Caustic abort: Android abort() invoked by module+0x%08lX (PC=0x%08lX)",
                 (unsigned long)(caller - so_mod.text_base),
                 (unsigned long)caller);
    } else {
        snprintf(line, sizeof(line),
                 "Caustic abort: Android abort() invoked by PC=0x%08lX",
                 (unsigned long)caller);
    }
    debug_log(line);

    /* terminate()/abort() hides the original C++ throw site. Preserve likely
     * module return addresses from this thread's stack for symbolization. */
    volatile uintptr_t *stack = &caller;
    char trace[320];
    size_t used = (size_t)snprintf(trace, sizeof(trace), "Caustic abort stack module offsets:");
    unsigned found = 0;
    for (unsigned i = 0; i < 160 && found < 12 && used < sizeof(trace); ++i) {
        uintptr_t value = stack[i];
        if (value >= so_mod.text_base && value < so_mod.text_base + 0x00310000) {
            int wrote = snprintf(trace + used, sizeof(trace) - used,
                                 " %08lX", (unsigned long)(value - so_mod.text_base));
            if (wrote > 0)
                used += (size_t)wrote;
            found++;
        }
    }
    debug_log(trace);
    abort();
}

static GLuint glCreateShader_debug(GLenum type) {
    debug_log("Caustic3 Vita: glCreateShader begin");
    GLuint shader = glCreateShader(type);
    debug_log("Caustic3 Vita: glCreateShader returned");
    return shader;
}

static GLuint glCreateProgram_debug(void) {
    debug_log("Caustic3 Vita: glCreateProgram begin");
    GLuint program = glCreateProgram();
    debug_log("Caustic3 Vita: glCreateProgram returned");
    return program;
}

static void glLinkProgram_debug(GLuint program) {
    debug_log("Caustic3 Vita: glLinkProgram begin");
    glLinkProgram(program);
    debug_log("Caustic3 Vita: glLinkProgram returned");
}

static int first_clear_color = 1, first_clear = 1, first_viewport = 1;
static int first_uniform_matrix = 1, first_texture_upload = 1;

#define FIRST_GL_VOID_WRAPPER(name, args, callargs) \
    static void name##_debug args { \
        static int first = 1; \
        if (first) debug_log("Caustic3 Vita: first " #name " begin"); \
        name callargs; \
        if (first) { debug_log("Caustic3 Vita: first " #name " returned"); first = 0; } \
    }

FIRST_GL_VOID_WRAPPER(glUseProgram, (GLuint program), (program))
FIRST_GL_VOID_WRAPPER(glUniform1i, (GLint location, GLint value), (location, value))
FIRST_GL_VOID_WRAPPER(glActiveTexture, (GLenum texture), (texture))
FIRST_GL_VOID_WRAPPER(glBindTexture, (GLenum target, GLuint texture), (target, texture))
FIRST_GL_VOID_WRAPPER(glBindBuffer, (GLenum target, GLuint buffer), (target, buffer))
FIRST_GL_VOID_WRAPPER(glEnable, (GLenum cap), (cap))
FIRST_GL_VOID_WRAPPER(glDisable, (GLenum cap), (cap))
FIRST_GL_VOID_WRAPPER(glBlendFunc, (GLenum src, GLenum dst), (src, dst))
FIRST_GL_VOID_WRAPPER(glEnableVertexAttribArray, (GLuint index), (index))
FIRST_GL_VOID_WRAPPER(glDisableVertexAttribArray, (GLuint index), (index))
FIRST_GL_VOID_WRAPPER(glVertexAttribPointer,
                      (GLuint index, GLint size, GLenum type, GLboolean normalized,
                       GLsizei stride, const GLvoid *pointer),
                      (index, size, type, normalized, stride, pointer))

static void glClearColor_debug(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (first_clear_color) debug_log("Caustic3 Vita: first glClearColor begin");
    glClearColor(r, g, b, a);
    if (first_clear_color) { debug_log("Caustic3 Vita: first glClearColor returned"); first_clear_color = 0; }
}

static void glClear_debug(GLbitfield mask) {
    if (first_clear) debug_log("Caustic3 Vita: first glClear begin");
    glClear(mask);
    if (first_clear) { debug_log("Caustic3 Vita: first glClear returned"); first_clear = 0; }
}

static void glViewport_debug(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (first_viewport) debug_log("Caustic3 Vita: first glViewport begin");
    glViewport(x, y, w, h);
    if (first_viewport) { debug_log("Caustic3 Vita: first glViewport returned"); first_viewport = 0; }
}

static void glUniformMatrix4fv_debug(GLint location, GLsizei count,
                                     GLboolean transpose, const GLfloat *value) {
    if (first_uniform_matrix) debug_log("Caustic3 Vita: first glUniformMatrix4fv begin");
    glUniformMatrix4fv(location, count, transpose, value);
    if (first_uniform_matrix) { debug_log("Caustic3 Vita: first glUniformMatrix4fv returned"); first_uniform_matrix = 0; }
}

void glDrawElementsHook(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    static int first = 1;
    if (first) debug_log("Caustic3 Vita: first glDrawElements begin");
    if (mode != GL_POINTS)
        glDrawElements(mode, count, type, indices);
    if (first) { debug_log("Caustic3 Vita: first glDrawElements returned"); first = 0; }
}

void glDrawArraysHook(GLenum mode, GLint first, GLsizei count) {
    static int first_call = 1;
    if (first_call) debug_log("Caustic3 Vita: first glDrawArrays begin");
    if (mode != GL_POINTS)
        glDrawArrays(mode, first, count);
    if (first_call) { debug_log("Caustic3 Vita: first glDrawArrays returned"); first_call = 0; }
}

int pthread_cond_timedwait_relative_np_soloader() {
    log_error("Called pthread_cond_timedwait_relative_np_soloader !!!");
    return 0;
}

int uname_fake(void *buf) {
    strcpy(buf + 195, "1.0");
    return 0;
}

int *__errno_fake() {
    log_error("Called __errno_fake !!!");
    return __errno();
}

GLint glGetUniformLocation_hook(GLuint program, const GLchar *name) {
	if (!strcmp(name, "texture"))
		return glGetUniformLocation(program, "_texture");
	return glGetUniformLocation(program, name);
}

void glGetActiveUniform_hook(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
	glGetActiveUniform(program, index, bufSize, length, size, type, name);
	if (!strcmp(name, "BoneMatrices")) {
		*type = GL_FLOAT_MAT4;
		*size = 48;
	}
	if (!strcmp(name, "_texture")) {
		strcpy(name, "texture");
		if (length)
			*length = *length - 1;
	}
}

void glTexImage2D_hook(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) {
    if (first_texture_upload) debug_log("Caustic3 Vita: first glTexImage2D begin");
    if (level == 0)
        glTexImage2D(target, level, internalFormat, width, height, border, format, type, data);
    if (first_texture_upload) { debug_log("Caustic3 Vita: first glTexImage2D returned"); first_texture_upload = 0; }
}

void glCompressedTexImage2D_hook(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) {
    if (level == 0)
        glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

so_default_dynlib default_dynlib[] = {
#ifdef CAUSTIC_FULL_322
        { "_Unwind_DeleteException", (uintptr_t)&_Unwind_DeleteException },
        { "_Unwind_GetLanguageSpecificData", (uintptr_t)&_Unwind_GetLanguageSpecificData },
        { "_Unwind_GetRegionStart", (uintptr_t)&_Unwind_GetRegionStart },
        { "_Unwind_RaiseException", (uintptr_t)&_Unwind_RaiseException },
        { "_Unwind_Resume", (uintptr_t)&_Unwind_Resume },
        { "_Unwind_VRS_Get", (uintptr_t)&_Unwind_VRS_Get },
        { "_Unwind_VRS_Set", (uintptr_t)&_Unwind_VRS_Set },
        { "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
        { "__FD_ISSET_chk", (uintptr_t)&__FD_ISSET_chk_bridge },
        { "__FD_SET_chk", (uintptr_t)&__FD_SET_chk_bridge },
        { "__aeabi_memclr", (uintptr_t)&__aeabi_memclr_bridge },
        { "__aeabi_memclr4", (uintptr_t)&__aeabi_memclr_bridge },
        { "__aeabi_memclr8", (uintptr_t)&__aeabi_memclr_bridge },
        { "__aeabi_memcpy", (uintptr_t)&__aeabi_memcpy_bridge },
        { "__aeabi_memcpy4", (uintptr_t)&__aeabi_memcpy_bridge },
        { "__aeabi_memcpy8", (uintptr_t)&__aeabi_memcpy_bridge },
        { "__aeabi_memmove", (uintptr_t)&__aeabi_memmove_bridge },
        { "__aeabi_memmove4", (uintptr_t)&__aeabi_memmove_bridge },
        { "__aeabi_memset", (uintptr_t)&__aeabi_memset_bridge },
        { "__aeabi_memset4", (uintptr_t)&__aeabi_memset_bridge },
        { "__aeabi_memset8", (uintptr_t)&__aeabi_memset_bridge },
        { "__ctype_get_mb_cur_max", (uintptr_t)&__ctype_get_mb_cur_max_bridge },
        { "__fread_chk", (uintptr_t)&__fread_chk_bridge },
        { "__fwrite_chk", (uintptr_t)&__fwrite_chk_bridge },
        { "__memcpy_chk", (uintptr_t)&__memcpy_chk_bridge },
        { "__memmove_chk", (uintptr_t)&__memmove_chk_bridge },
        { "__memset_chk", (uintptr_t)&__memset_chk_bridge },
        { "__open_2", (uintptr_t)&__open_2_bridge },
        { "__read_chk", (uintptr_t)&__read_chk_bridge },
        { "__register_atfork", (uintptr_t)&__register_atfork_bridge },
        { "__sendto_chk", (uintptr_t)&__sendto_chk_bridge },
        { "__strcat_chk", (uintptr_t)&__strcat_chk_bridge },
        { "__strchr_chk", (uintptr_t)&__strchr_chk_bridge },
        { "__strcpy_chk", (uintptr_t)&__strcpy_chk_bridge },
        { "__strlen_chk", (uintptr_t)&__strlen_chk_bridge },
        { "__strncat_chk", (uintptr_t)&__strncat_chk_bridge },
        { "__strncpy_chk", (uintptr_t)&__strncpy_chk_bridge },
        { "__strncpy_chk2", (uintptr_t)&__strncpy_chk2_bridge },
        { "__strrchr_chk", (uintptr_t)&__strrchr_chk_bridge },
        { "__umask_chk", (uintptr_t)&umask_bridge },
        { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_bridge },
        { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk_bridge },
        { "__write_chk", (uintptr_t)&__write_chk_bridge },
        { "android_set_abort_message", (uintptr_t)&android_set_abort_message_bridge },
        { "atof", (uintptr_t)&atof },
        { "btowc", (uintptr_t)&btowc },
        { "closelog", (uintptr_t)&closelog_bridge },
        { "exp2", (uintptr_t)&bridge_exp2 },
        { "exp2f", (uintptr_t)&bridge_exp2f },
        { "fileno", (uintptr_t)&ret0 },
        { "fputwc", (uintptr_t)&fputwc },
        { "freelocale", (uintptr_t)&freelocale },
        { "getwc", (uintptr_t)&getwc },
        { "iswalpha_l", (uintptr_t)&iswalpha },
        { "iswblank_l", (uintptr_t)&iswblank },
        { "iswcntrl_l", (uintptr_t)&iswcntrl },
        { "iswdigit_l", (uintptr_t)&iswdigit },
        { "iswlower_l", (uintptr_t)&iswlower },
        { "iswprint_l", (uintptr_t)&iswprint },
        { "iswpunct_l", (uintptr_t)&iswpunct },
        { "iswspace_l", (uintptr_t)&iswspace },
        { "iswupper_l", (uintptr_t)&iswupper },
        { "iswxdigit_l", (uintptr_t)&iswxdigit },
        { "localeconv", (uintptr_t)&localeconv },
        { "mbrlen", (uintptr_t)&mbrlen },
        { "mbrtowc", (uintptr_t)&mbrtowc },
        { "mbsnrtowcs", (uintptr_t)&mbsnrtowcs },
        { "mbsrtowcs", (uintptr_t)&mbsrtowcs },
        { "mbtowc", (uintptr_t)&mbtowc },
        { "newlocale", (uintptr_t)&newlocale },
        { "openlog", (uintptr_t)&openlog_bridge },
        { "posix_memalign", (uintptr_t)&posix_memalign_bridge },
        { "rand", (uintptr_t)&rand },
        { "rintf", (uintptr_t)&bridge_rintf },
        { "setbuf", (uintptr_t)&setbuf },
        { "sincos", (uintptr_t)&sincos_bridge },
        { "sincosf", (uintptr_t)&sincosf_bridge },
        { "srand", (uintptr_t)&srand },
        { "stderr", (uintptr_t)&stderr_fake },
        { "strcoll_l", (uintptr_t)&strcoll },
        { "strerror_r", (uintptr_t)&strerror_r },
        { "strftime_l", (uintptr_t)&strftime },
        { "strtof", (uintptr_t)&strtof },
        { "strtol", (uintptr_t)&strtol },
        { "strtold", (uintptr_t)&strtold },
        { "strtold_l", (uintptr_t)&strtold },
        { "strtoll", (uintptr_t)&strtoll },
        { "strtoll_l", (uintptr_t)&strtoll },
        { "strtoull", (uintptr_t)&strtoull },
        { "strtoull_l", (uintptr_t)&strtoull },
        { "strxfrm_l", (uintptr_t)&strxfrm },
        { "syslog", (uintptr_t)&syslog_bridge },
        { "towlower_l", (uintptr_t)&towlower },
        { "towupper_l", (uintptr_t)&towupper },
        { "ungetwc", (uintptr_t)&ungetwc },
        { "uselocale", (uintptr_t)&uselocale },
        { "vasprintf", (uintptr_t)&vasprintf },
        { "vfprintf", (uintptr_t)&vfprintf },
        { "vsscanf", (uintptr_t)&vsscanf },
        { "wcrtomb", (uintptr_t)&wcrtomb },
        { "wcscoll_l", (uintptr_t)&wcscoll },
        { "wcsnrtombs", (uintptr_t)&wcsnrtombs },
        { "wcstod", (uintptr_t)&wcstod },
        { "wcstof", (uintptr_t)&wcstof },
        { "wcstol", (uintptr_t)&wcstol },
        { "wcstold", (uintptr_t)&wcstold },
        { "wcstoll", (uintptr_t)&wcstoll },
        { "wcstoul", (uintptr_t)&wcstoul },
        { "wcstoull", (uintptr_t)&wcstoull },
        { "wcsxfrm_l", (uintptr_t)&wcsxfrm },
        { "wctob", (uintptr_t)&wctob },
        { "wmemchr", (uintptr_t)&wmemchr },
#endif
        { "SL_IID_BUFFERQUEUE", (uintptr_t)&ret0 },
        { "SL_IID_ENGINE", (uintptr_t)&ret0 },
        { "SL_IID_PLAY", (uintptr_t)&ret0 },
        { "SL_IID_VOLUME", (uintptr_t)&ret0 },
        { "__android_log_vprint", (uintptr_t)&android_log_vprint },
        { "__assert2", (uintptr_t)&abort },
        { "__cxa_atexit", (uintptr_t)&__aeabi_atexit },
        { "__cxa_finalize", (uintptr_t)&ret0 },
        { "__gnu_Unwind_Find_exidx", (uintptr_t)&ret0 },
        { "accept", (uintptr_t)&accept },
        { "calloc", (uintptr_t)&calloc_debug },
        { "chmod", (uintptr_t)&chmod_soloader },
        { "clearerr", (uintptr_t)&ret0 },
        { "closedir", (uintptr_t)&closedir_soloader },
        { "connect", (uintptr_t)&connect },
        { "deflate", (uintptr_t)&deflate },
        { "deflateEnd", (uintptr_t)&deflateEnd },
        { "deflateInit2_", (uintptr_t)&deflateInit2_ },
        { "dlclose", (uintptr_t)&ret0 },
        { "dlopen", (uintptr_t)&ret0 },
        { "dlsym", (uintptr_t)&ret0 },
        { "exp", (uintptr_t)&bridge_exp },
        { "fdopen", (uintptr_t)&ret0 },
        { "feof", (uintptr_t)&sceLibcBridge_feof },
        { "fseeko", (uintptr_t)&sceLibcBridge_fseek },
        { "fsync", (uintptr_t)&fsync },
        { "ftello", (uintptr_t)&sceLibcBridge_ftell },
        { "gethostbyname", (uintptr_t)&gethostbyname },
        { "getpid", (uintptr_t)&getpid },
        { "getsockname", (uintptr_t)&getsockname },
        { "inflate", (uintptr_t)&inflate },
        { "inflateEnd", (uintptr_t)&inflateEnd },
        { "inflateInit2_", (uintptr_t)&inflateInit2_ },
        { "isalpha", (uintptr_t)&isalpha },
        { "iscntrl", (uintptr_t)&iscntrl },
        { "islower", (uintptr_t)&islower },
        { "isprint", (uintptr_t)&isprint },
        { "ispunct", (uintptr_t)&ispunct },
        { "isspace", (uintptr_t)&isspace },
        { "isupper", (uintptr_t)&isupper },
        { "isxdigit", (uintptr_t)&isxdigit },
        { "kill", (uintptr_t)&kill },
        { "ldexp", (uintptr_t)&bridge_ldexp },
        { "listen", (uintptr_t)&listen },
        { "log", (uintptr_t)&bridge_log },
        { "log10f", (uintptr_t)&bridge_log10f },
        { "lrint", (uintptr_t)&bridge_lrint },
        { "lrintf", (uintptr_t)&bridge_lrintf },
        { "mkdir", (uintptr_t)&mkdir_soloader },
        { "mkstemp", (uintptr_t)&mkstemp },
        { "munlock", (uintptr_t)&ret0 },
        { "opendir", (uintptr_t)&opendir_soloader },
        { "perror", (uintptr_t)&perror },
        { "pthread_detach", (uintptr_t)&pthread_detach_soloader },
        { "pthread_exit", (uintptr_t)&pthread_exit },
        { "raise", (uintptr_t)&raise },
        { "readdir", (uintptr_t)&readdir_soloader },
        { "readdir_r", (uintptr_t)&readdir_r_soloader },
        { "recv", (uintptr_t)&recv },
        { "rint", (uintptr_t)&bridge_rint },
        { "rmdir", (uintptr_t)&rmdir_soloader },
        { "send", (uintptr_t)&send },
        { "shutdown", (uintptr_t)&shutdown },
        { "slCreateEngine", (uintptr_t)&ret0 },
        { "stat", (uintptr_t)&stat_soloader },
        { "statfs", (uintptr_t)&statfs_soloader },
        { "tolower", (uintptr_t)&tolower },
        { "toupper", (uintptr_t)&toupper },
        { "umask", (uintptr_t)&ret0 },
        { "zError", (uintptr_t)&zError },
        { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D_hook },
        { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap }, // nuke mips
        { "glTexImage2D", (uintptr_t)&glTexImage2D_hook },
        { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
        { "__aeabi_d2f", (uintptr_t)&__aeabi_d2f },
        { "__aeabi_d2iz", (uintptr_t)&__aeabi_d2iz },
        { "__aeabi_d2lz", (uintptr_t)&__aeabi_d2lz},
        { "__aeabi_d2uiz", (uintptr_t)&__aeabi_d2uiz },
        { "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
        { "__aeabi_dcmpeq", (uintptr_t)&__aeabi_dcmpeq },
        { "__aeabi_dcmpge", (uintptr_t)&__aeabi_dcmpge },
        { "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
        { "__aeabi_dcmple", (uintptr_t)&__aeabi_dcmple },
        { "__aeabi_dcmplt", (uintptr_t)&__aeabi_dcmplt },
        { "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
        { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
        { "__aeabi_dsub", (uintptr_t)&__aeabi_dsub },
        { "__aeabi_f2d", (uintptr_t)&__aeabi_f2d },
        { "__aeabi_f2iz", (uintptr_t)&__aeabi_f2iz },
        { "__aeabi_fadd", (uintptr_t)&__aeabi_fadd },
        { "__aeabi_fcmpeq", (uintptr_t)&__aeabi_fcmpeq },
        { "__aeabi_fcmpge", (uintptr_t)&__aeabi_fcmpge },
        { "__aeabi_fcmpgt", (uintptr_t)&__aeabi_fcmpgt },
        { "__aeabi_fcmple", (uintptr_t)&__aeabi_fcmple },
        { "__aeabi_fcmplt", (uintptr_t)&__aeabi_fcmplt },
        { "__aeabi_fcmpun", (uintptr_t)&__aeabi_fcmpun },
        { "__aeabi_fdiv", (uintptr_t)&__aeabi_fdiv },
        { "__aeabi_fmul", (uintptr_t)&__aeabi_fmul },
        { "__aeabi_fsub", (uintptr_t)&__aeabi_fsub },
        { "__aeabi_i2d", (uintptr_t)&__aeabi_i2d },
        { "__aeabi_i2f", (uintptr_t)&__aeabi_i2f },
        { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
        { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
        { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
        { "__aeabi_lmul", (uintptr_t)&__aeabi_lmul },
        { "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
        { "__aeabi_ui2f", (uintptr_t)&__aeabi_ui2f },
        { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
        { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
        { "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d},
        { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod},
        { "__android_log_print", (uintptr_t)&android_log_print },
        { "__android_log_write", (uintptr_t)&android_log_write },
        { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
        { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
        { "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
        { "__dso_handle", (uintptr_t)&__dso_handle },
        { "__errno", (uintptr_t)&__errno_fake },
        { "__sF", (uintptr_t)&__sF_fake },
        { "__srget", (uintptr_t)&__srget },
        { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
        { "__stack_chk_guard", (uintptr_t)&android_stack_chk_guard },
        { "_ctype_", (uintptr_t)&BIONIC_ctype_},
        { "_tolower_tab_", (uintptr_t)&BIONIC_tolower_tab_},
        { "_toupper_tab_", (uintptr_t)&BIONIC_toupper_tab_},
        { "_ZnajRKSt9nothrow_t", (uintptr_t)&_ZnajRKSt9nothrow_t },
        { "_ZSt7nothrow", (uintptr_t)&_ZSt7nothrow },
        { "abort", (uintptr_t)&abort_debug },
        { "acos", (uintptr_t)&bridge_acos },
        { "acosf", (uintptr_t)&bridge_acosf },
        { "asin", (uintptr_t)&bridge_asin },
        { "asinf", (uintptr_t)&bridge_asinf },
        { "atan", (uintptr_t)&bridge_atan },
        { "atan2", (uintptr_t)&bridge_atan2 },
        { "atan2f", (uintptr_t)&bridge_atan2f },
        { "atanf", (uintptr_t)&bridge_atanf },
        { "atoi", (uintptr_t)&atoi },
        { "bind", (uintptr_t)&bind},
        { "ceil", (uintptr_t)&bridge_ceil},
        { "ceilf", (uintptr_t)&bridge_ceilf },
        { "chdir", (uintptr_t)&chdir},
        { "clock", (uintptr_t)&clock },
        { "close", (uintptr_t)&close},
        { "cos", (uintptr_t)&bridge_cos },
        { "cosf", (uintptr_t)&bridge_cosf },
        { "cosh", (uintptr_t)&cosh},
        { "difftime", (uintptr_t)&bridge_difftime},
        { "exit", (uintptr_t)&exit },
        { "expf", (uintptr_t)&bridge_expf },
        { "fclose", (uintptr_t)&fclose_soloader },
        { "fcntl", (uintptr_t)&fcntl_soloader },
        { "ferror", (uintptr_t)&sceLibcBridge_ferror},
        { "fflush", (uintptr_t)&sceLibcBridge_fflush},
        { "fgetc", (uintptr_t)&sceLibcBridge_fgetc },
        { "fgetpos", (uintptr_t)&sceLibcBridge_fgetpos},
        { "fgets", (uintptr_t)&sceLibcBridge_fgets},
        { "floor", (uintptr_t)&bridge_floor },
        { "floorf", (uintptr_t)&bridge_floorf },
        { "fmod", (uintptr_t)&bridge_fmod },
        { "fmodf", (uintptr_t)&bridge_fmodf },
        { "fopen", (uintptr_t)&fopen_soloader },
        { "fprintf", (uintptr_t)&sceLibcBridge_fprintf },
        { "fputc", (uintptr_t)&sceLibcBridge_fputc},
        { "fputs", (uintptr_t)&sceLibcBridge_fputs},
        { "fread", (uintptr_t)&sceLibcBridge_fread },
        { "free", (uintptr_t)&free },
        { "freeaddrinfo", (uintptr_t)&freeaddrinfo},
        { "freopen", (uintptr_t)&freopen},
        { "frexpf", (uintptr_t)&frexpf},
        { "fscanf", (uintptr_t)&sceLibcBridge_fscanf},
        { "fseek", (uintptr_t)&sceLibcBridge_fseek },
        { "fsetpos", (uintptr_t)&sceLibcBridge_fsetpos},
        { "fstat", (uintptr_t)&fstat_soloader},
        { "ftell", (uintptr_t)&sceLibcBridge_ftell },
        { "fwrite", (uintptr_t)&sceLibcBridge_fwrite },
        { "getaddrinfo", (uintptr_t)&getaddrinfo},
        { "getc", (uintptr_t)&sceLibcBridge_getc},
        { "getenv", (uintptr_t)&ret0 },
        { "gethostname", (uintptr_t)&gethostname },
        { "gettimeofday", (uintptr_t)&gettimeofday_soloader },
        { "glActiveTexture", (uintptr_t)&glActiveTexture_debug },
        { "glAlphaFunc", (uintptr_t)&glAlphaFunc },
        { "glAttachShader", (uintptr_t)&glAttachShader},
        { "glBindBuffer", (uintptr_t)&glBindBuffer_debug },
        { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer},
        { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
        { "glBindTexture", (uintptr_t)&glBindTexture_debug },
        { "glBlendColor", (uintptr_t)&ret0 },
        { "glBlendEquation", (uintptr_t)&glBlendEquation },
        { "glBlendFunc", (uintptr_t)&glBlendFunc_debug },
        { "glBufferData", (uintptr_t)&glBufferData },
        { "glBufferSubData", (uintptr_t)&glBufferSubData },
        { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
        { "glClear", (uintptr_t)&glClear_debug },
        { "glClearColor", (uintptr_t)&glClearColor_debug },
        { "glClearDepthf", (uintptr_t)&glClearDepthf },
        { "glClearStencil", (uintptr_t)&glClearStencil },
        { "glClientActiveTexture", (uintptr_t)&glClientActiveTexture },
        { "glClipPlanef", (uintptr_t)&glClipPlanef },
        { "glColor4f", (uintptr_t)&glColor4f },
        { "glColor4ub", (uintptr_t)&glColor4ub },
        { "glColorMask", (uintptr_t)&glColorMask },
        { "glColorPointer", (uintptr_t)&glColorPointer },
        { "glCompileShader", (uintptr_t)&glCompileShaderHook },
        { "glCompressedTexSubImage2D", (uintptr_t)&ret0},
        { "glCopyTexImage2D", (uintptr_t)&ret0 },
        { "glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D },
        { "glCreateProgram", (uintptr_t)&glCreateProgram_debug},
        { "glCreateShader", (uintptr_t)&glCreateShader_debug },
        { "glCullFace", (uintptr_t)&glCullFace },
        { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
        { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
        { "glDeleteProgram", (uintptr_t)&glDeleteProgram},
        { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
        { "glDeleteShader", (uintptr_t)&glDeleteShader },
        { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
        { "glDepthFunc", (uintptr_t)&glDepthFunc },
        { "glDepthMask", (uintptr_t)&glDepthMask },
        { "glDepthRangef", (uintptr_t)&glDepthRangef },
        { "glDisable", (uintptr_t)&glDisable_debug },
        { "glDisableClientState", (uintptr_t)&glDisableClientState },
        { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray_debug },
        { "glDrawArrays", (uintptr_t)&glDrawArraysHook },
        { "glDrawElements", (uintptr_t)&glDrawElementsHook },
        { "glEnable", (uintptr_t)&glEnable_debug },
        { "glEnableClientState", (uintptr_t)&glEnableClientState },
        { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray_debug},
        { "glFlush", (uintptr_t)&glFlush},
        { "glFogf", (uintptr_t)&glFogf },
        { "glFogfv", (uintptr_t)&glFogfv },
        { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
        { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D},
        { "glFrontFace", (uintptr_t)&glFrontFace },
        { "glFrustumf", (uintptr_t)&glFrustumf },
        { "glGenBuffers", (uintptr_t)&glGenBuffers },
        { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers},
        { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers},
        { "glGenTextures", (uintptr_t)&glGenTextures },
        { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttrib},
        { "glGetActiveUniform", (uintptr_t)&glGetActiveUniform_hook},
        { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
        { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
        { "glGetError", (uintptr_t)&glGetError },
        { "glGetFloatv", (uintptr_t)&glGetFloatv },
        { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
        { "glGetPointerv", (uintptr_t)&glGetPointerv },
        { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog},
        { "glGetProgramiv", (uintptr_t)&glGetProgramiv},
        { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog},
        { "glGetShaderiv", (uintptr_t)&glGetShaderiv},
        { "glGetString", (uintptr_t)&glGetString },
        { "glGetTexEnviv", (uintptr_t)&glGetTexEnviv },
        { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation_hook },
        { "glHint", (uintptr_t)&glHint },
        { "glIsEnabled", (uintptr_t)&glIsEnabled },
        { "glLightf", (uintptr_t)&ret0 },
        { "glLightfv", (uintptr_t)&glLightfv },
        { "glLightModelfv", (uintptr_t)&glLightModelfv },
        { "glLineWidth", (uintptr_t)&glLineWidth_debug },
        { "glLinkProgram", (uintptr_t)&glLinkProgram_debug},
        { "glLoadIdentity", (uintptr_t)&glLoadIdentity },
        { "glLoadMatrixf", (uintptr_t)&glLoadMatrixf },
        { "glMaterialf", (uintptr_t)&ret0 },
        { "glMaterialfv", (uintptr_t)&glMaterialfv },
        { "glMatrixMode", (uintptr_t)&glMatrixMode },
        { "glMultMatrixf", (uintptr_t)&glMultMatrixf },
        { "glNormal3f", (uintptr_t)&glNormal3f },
        { "glNormalPointer", (uintptr_t)&glNormalPointer },
        { "glOrthox", (uintptr_t)&glOrthox },
        { "glPixelStorei", (uintptr_t)&glPixelStorei },
        { "glPointParameterf", (uintptr_t)&ret0 },
        { "glPointSize", (uintptr_t)&glPointSize },
        { "glPolygonOffset", (uintptr_t)&glPolygonOffset },
        { "glPopMatrix", (uintptr_t)&glPopMatrix },
        { "glPushMatrix", (uintptr_t)&glPushMatrix },
        { "glReadPixels", (uintptr_t)&glReadPixels },
        { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage},
        { "glRotatef", (uintptr_t)&glRotatef },
        { "glSampleCoverage", (uintptr_t)&ret0},
        { "glScalef", (uintptr_t)&glScalef },
        { "glScissor", (uintptr_t)&glScissor },
        { "glShadeModel", (uintptr_t)&ret0 },
        { "glShaderSource", (uintptr_t)&glShaderSourceHook },
        { "glStencilFunc", (uintptr_t)&glStencilFunc },
        { "glStencilMask", (uintptr_t)&glStencilMask },
        { "glStencilOp", (uintptr_t)&glStencilOp },
        { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer },
        { "glTexEnvf", (uintptr_t)&glTexEnvf },
        { "glTexEnvfv", (uintptr_t)&glTexEnvfv },
        { "glTexEnvi", (uintptr_t)&glTexEnvi },
        { "glTexParameterf", (uintptr_t)&glTexParameterf },
        { "glTexParameteri", (uintptr_t)&glTexParameteri },
        { "glTexParameterx", (uintptr_t)&glTexParameterx },
        { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
        { "glTranslatef", (uintptr_t)&glTranslatef },
        { "glUniform1f", (uintptr_t)&glUniform1f },
        { "glUniform1fv", (uintptr_t)&glUniform1fv},
        { "glUniform1i", (uintptr_t)&glUniform1i_debug},
        { "glUniform1iv", (uintptr_t)&glUniform1iv},
        { "glUniform2fv", (uintptr_t)&glUniform2fv },
        { "glUniform2iv", (uintptr_t)&glUniform2iv },
        { "glUniform3fv", (uintptr_t)&glUniform3fv },
        { "glUniform3iv", (uintptr_t)&glUniform3iv},
        { "glUniform4fv", (uintptr_t)&glUniform4fv},
        { "glUniform4iv", (uintptr_t)&glUniform4iv },
        { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv_debug},
        { "glUseProgram", (uintptr_t)&glUseProgram_debug },
        { "glVertexAttrib4f", (uintptr_t)&glVertexAttrib4f},
        { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer_debug},
        { "glVertexPointer", (uintptr_t)&glVertexPointer },
        { "glViewport", (uintptr_t)&glViewport_debug },
        { "gmtime", (uintptr_t)&gmtime},
        { "iswalpha", (uintptr_t)&iswalpha},
        { "iswcntrl", (uintptr_t)&iswcntrl},
        { "iswdigit", (uintptr_t)&iswdigit},
        { "iswlower", (uintptr_t)&iswlower},
        { "iswprint", (uintptr_t)&iswprint},
        { "iswpunct", (uintptr_t)&iswpunct},
        { "iswspace", (uintptr_t)&iswspace},
        { "iswupper", (uintptr_t)&iswupper},
        { "iswxdigit", (uintptr_t)&iswxdigit},
        { "ldexpf", (uintptr_t)&bridge_ldexpf},
        { "localtime", (uintptr_t)&localtime},
        { "log10", (uintptr_t)&bridge_log10},
        { "logf", (uintptr_t)&bridge_logf },
        { "longjmp", (uintptr_t)&sceLibcBridge_longjmp},
        { "lrand48", (uintptr_t)&lrand48 },
        { "lseek", (uintptr_t)&lseek},
        { "malloc", (uintptr_t)&malloc_debug },
        { "memchr", (uintptr_t)&memchr},
        { "memcmp", (uintptr_t)&memcmp },
        { "memcpy", (uintptr_t)&memcpy },
        { "memmove", (uintptr_t)&memmove },
        { "memset", (uintptr_t)&memset },
        { "mktime", (uintptr_t)&mktime},
        { "mmap", (uintptr_t)&mmap},
        { "modff", (uintptr_t)&bridge_modff},
        { "munmap", (uintptr_t)&munmap},
        { "nanosleep", (uintptr_t)&nanosleep },
        { "open", (uintptr_t)&open_soloader },
        { "pow", (uintptr_t)&bridge_pow },
        { "powf", (uintptr_t)&bridge_powf },
        { "printf", (uintptr_t)&sceClibPrintf },
        { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
        { "pthread_attr_init", (uintptr_t)&pthread_attr_init_soloader },
        { "pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate_soloader },
        { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize_soloader },
        { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_soloader},
        { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_soloader},
        { "pthread_cond_init", (uintptr_t)&pthread_cond_init_soloader},
        { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_soloader},
        { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_soloader},
        { "pthread_cond_timedwait_relative_np", (uintptr_t)&pthread_cond_timedwait_relative_np_soloader},
        { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_soloader},
        { "pthread_create", (uintptr_t)&pthread_create_soloader },
        { "pthread_getschedparam", (uintptr_t)&pthread_getschedparam_soloader },
        { "pthread_getschedparam", (uintptr_t)&pthread_getschedparam_soloader },
        { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
        { "pthread_join", (uintptr_t)&pthread_join_soloader },
        { "pthread_key_create", (uintptr_t)&pthread_key_create },
        { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
        { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_soloader },
        { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_soloader },
        { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_soloader },
        { "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_soloader },
        { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_soloader },
        { "pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy_soloader },
        { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_soloader },
        { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_soloader },
        { "pthread_once", (uintptr_t)&pthread_once_soloader },
        { "pthread_self", (uintptr_t)&pthread_self_soloader },
        { "pthread_setname_np", (uintptr_t)&pthread_setname_np_soloader },
        { "pthread_setschedparam", (uintptr_t)&pthread_setschedparam_soloader },
        { "pthread_setspecific", (uintptr_t)&pthread_setspecific },
        { "putc", (uintptr_t)&sceLibcBridge_fputc},
        { "putchar", (uintptr_t)&putchar},
        { "puts", (uintptr_t)&puts },
        { "qsort", (uintptr_t)&sceLibcBridge_qsort},
        { "read", (uintptr_t)&read},
        { "realloc", (uintptr_t)&realloc_debug},
        { "recvfrom", (uintptr_t)&recvfrom},
        { "remove", (uintptr_t)&remove_soloader },
        { "rename", (uintptr_t)&rename_soloader },
        { "sched_yield", (uintptr_t)&sched_yield},
        { "select", (uintptr_t)&select},
        { "sendto", (uintptr_t)&sendto},
        { "setjmp", (uintptr_t)&sceLibcBridge_setjmp},
        { "setlocale", (uintptr_t)&setlocale_bridge},
        { "setsockopt", (uintptr_t)&setsockopt},
        { "setvbuf", (uintptr_t)&ret0},
        { "sin", (uintptr_t)&bridge_sin },
        { "sinf", (uintptr_t)&bridge_sinf },
        { "sinh", (uintptr_t)&sinh},
        { "snprintf", (uintptr_t)&snprintf },
        { "socket", (uintptr_t)&socket},
        { "sprintf", (uintptr_t)&sprintf },
        { "sqrt", (uintptr_t)&bridge_sqrt },
        { "sqrtf", (uintptr_t)&bridge_sqrtf },
        { "srand48", (uintptr_t)&srand48 },
        { "sscanf", (uintptr_t)&sceLibcBridge_sscanf },
        { "strcasecmp", (uintptr_t)&strcasecmp },
        { "strcat", (uintptr_t)&strcat },
        { "strchr", (uintptr_t)&strchr },
        { "strcmp", (uintptr_t)&strcmp },
        { "strcoll", (uintptr_t)&strcoll},
        { "strcpy", (uintptr_t)&strcpy },
        { "strcspn", (uintptr_t)&strcspn},
        { "strdup", (uintptr_t)&strdup },
        { "strerror", (uintptr_t)&strerror},
        { "strftime", (uintptr_t)&strftime},
        { "strlen", (uintptr_t)&strlen },
        { "strncasecmp", (uintptr_t)&strncasecmp},
        { "strncat", (uintptr_t)&strncat},
        { "strncmp", (uintptr_t)&strncmp },
        { "strncpy", (uintptr_t)&strncpy},
        { "strpbrk", (uintptr_t)&strpbrk },
        { "strrchr", (uintptr_t)&strrchr},
        { "strstr", (uintptr_t)&strstr },
        { "strtod", (uintptr_t)&strtod },
        { "strtok", (uintptr_t)&strtok},
        { "strtoul", (uintptr_t)&strtoul},
        { "swprintf", (uintptr_t)&swprintf},
        { "sysconf", (uintptr_t)&ret0},
        { "system", (uintptr_t)&system},
        { "tan", (uintptr_t)&bridge_tan },
        { "tanf", (uintptr_t)&bridge_tanf },
        { "tanh", (uintptr_t)&tanh},
        { "time", (uintptr_t)&time},
        { "tmpfile", (uintptr_t)&tmpfile},
        { "tmpnam", (uintptr_t)&tmpnam},
        { "towlower", (uintptr_t)&towlower},
        { "towupper", (uintptr_t)&towupper},
        { "uname", (uintptr_t)&uname_fake },
        { "ungetc", (uintptr_t)&sceLibcBridge_ungetc},
        { "unlink", (uintptr_t)&unlink_soloader},
        { "usleep", (uintptr_t)&usleep},
        { "vsnprintf", (uintptr_t)&vsnprintf},
        { "vsprintf", (uintptr_t)&vsprintf },
        { "wcscmp", (uintptr_t)&wcscmp },
        { "wcscpy", (uintptr_t)&wcscpy },
        { "wcslen", (uintptr_t)&wcslen },
        { "wcsncpy", (uintptr_t)&wcsncpy},
        { "wmemcmp", (uintptr_t)&wmemcmp},
        { "wmemcpy", (uintptr_t)&wmemcpy },
        { "wmemmove", (uintptr_t)&wmemmove},
        { "wmemset", (uintptr_t)&wmemset},
        { "write", (uintptr_t)&write_soloader},
};

void resolve_imports(so_module* mod) {
    __sF_fake[0] = *stdin;
    __sF_fake[1] = *stdout;
    __sF_fake[2] = *stderr;

    so_resolve(mod, default_dynlib, sizeof(default_dynlib), 0);
}
