#include "reimpl/fortify.h"

#include <fcntl.h>
#include <math.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libc_bridge/libc_bridge.h>
#include <math_neon.h>

#include "reimpl/io.h"
#include "reimpl/math_bridge.h"

extern void debug_log(const char *msg);

static void bounds_or_abort(size_t requested, size_t available) {
    if (requested > available) {
        char line[160];
        snprintf(line, sizeof(line),
                 "Caustic abort: fortify bounds requested=%lu available=%lu",
                 (unsigned long)requested, (unsigned long)available);
        debug_log(line);
        abort();
    }
}

void *__memcpy_chk_bridge(void *d, const void *s, size_t n, size_t ds) { bounds_or_abort(n, ds); return memcpy(d, s, n); }
void *__memmove_chk_bridge(void *d, const void *s, size_t n, size_t ds) { bounds_or_abort(n, ds); return memmove(d, s, n); }
void *__memset_chk_bridge(void *d, int c, size_t n, size_t ds) { bounds_or_abort(n, ds); return memset(d, c, n); }
char *__strcpy_chk_bridge(char *d, const char *s, size_t ds) { bounds_or_abort(strlen(s) + 1, ds); return strcpy(d, s); }
char *__strncpy_chk_bridge(char *d, const char *s, size_t n, size_t ds) { bounds_or_abort(n, ds); return strncpy(d, s, n); }
char *__strncpy_chk2_bridge(char *d, const char *s, size_t n, size_t ds, size_t ss) { bounds_or_abort(n, ds); bounds_or_abort(strnlen(s, n), ss); return strncpy(d, s, n); }
char *__strcat_chk_bridge(char *d, const char *s, size_t ds) { bounds_or_abort(strlen(d) + strlen(s) + 1, ds); return strcat(d, s); }
char *__strncat_chk_bridge(char *d, const char *s, size_t n, size_t ds) { bounds_or_abort(strlen(d) + strnlen(s, n) + 1, ds); return strncat(d, s, n); }
size_t __strlen_chk_bridge(const char *s, size_t ss) { size_t n = strnlen(s, ss); if (n == ss) abort(); return n; }
char *__strchr_chk_bridge(const char *s, int c, size_t ss) { if (strnlen(s, ss) == ss) abort(); return strchr(s, c); }
char *__strrchr_chk_bridge(const char *s, int c, size_t ss) { if (strnlen(s, ss) == ss) abort(); return strrchr(s, c); }
ssize_t __read_chk_bridge(int fd, void *b, size_t n, size_t bs) { bounds_or_abort(n, bs); return read_soloader(fd, b, n); }
ssize_t __write_chk_bridge(int fd, const void *b, size_t n, size_t bs) { bounds_or_abort(n, bs); return write_soloader(fd, b, (int)n); }
size_t __fread_chk_bridge(void *b, size_t s, size_t n, FILE *f, size_t bs) { if (s && n > SIZE_MAX / s) abort(); bounds_or_abort(s * n, bs); return sceLibcBridge_fread(b, s, n, f); }
size_t __fwrite_chk_bridge(const void *b, size_t s, size_t n, FILE *f, size_t bs) { if (s && n > SIZE_MAX / s) abort(); bounds_or_abort(s * n, bs); return sceLibcBridge_fwrite(b, s, n, f); }
int __open_2_bridge(const char *p, int f) { return open_soloader((char *)p, f); }
/* Bionic's 32-bit ARM va_list is passed as a pointer to an array of argument
 * words.  It is not ABI-compatible with Vita newlib's va_list, so forwarding
 * it to vsnprintf silently formats empty/garbled strings. */
static int android_vformat(char *d, size_t capacity, const char *fmt,
                           const uintptr_t *args) {
    size_t used = 0;
    unsigned arg = 0;
    if (!d || !fmt || capacity == 0)
        return -1;

#define APPEND_CHAR(ch) do { \
    if (used + 1 < capacity) d[used] = (char)(ch); \
    used++; \
} while (0)

    while (*fmt) {
        if (*fmt != '%') {
            APPEND_CHAR(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            APPEND_CHAR('%');
            fmt++;
            continue;
        }

        char spec[32] = "%";
        size_t spec_len = 1;
        while (*fmt && strchr("-+ #0.123456789hlzL", *fmt) && spec_len + 2 < sizeof(spec))
            spec[spec_len++] = *fmt++;
        char conversion = *fmt ? *fmt++ : '\0';
        spec[spec_len++] = conversion;
        spec[spec_len] = '\0';

        char temp[256];
        int count;
        if (conversion == 's') {
            const char *value = (const char *)args[arg++];
            count = snprintf(temp, sizeof(temp), spec, value ? value : "(null)");
        } else if (conversion == 'c' || conversion == 'd' || conversion == 'i') {
            count = snprintf(temp, sizeof(temp), spec, (int)args[arg++]);
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'X' ||
                   conversion == 'o') {
            count = snprintf(temp, sizeof(temp), spec, (unsigned)args[arg++]);
        } else if (conversion == 'p') {
            count = snprintf(temp, sizeof(temp), spec, (void *)args[arg++]);
        } else if (strchr("aAeEfFgG", conversion)) {
            /* Variadic float arguments are promoted to double and aligned to
             * 8 bytes by the 32-bit ARM AAPCS. */
            uintptr_t address = (uintptr_t)&args[arg];
            uintptr_t aligned = (address + 7u) & ~(uintptr_t)7u;
            uint64_t bits;
            memcpy(&bits, (const void *)aligned, sizeof(bits));
            double value;
            memcpy(&value, &bits, sizeof(value));
            arg = (unsigned)(((aligned - (uintptr_t)args) / sizeof(uintptr_t)) + 2);
            count = snprintf(temp, sizeof(temp), spec, value);
        } else {
            temp[0] = '%';
            temp[1] = conversion;
            temp[2] = '\0';
            count = 2;
        }
        if (count < 0)
            return count;
        size_t available = (size_t)count < sizeof(temp) ? (size_t)count : sizeof(temp) - 1;
        for (size_t i = 0; i < available; i++)
            APPEND_CHAR(temp[i]);
    }
    d[used < capacity ? used : capacity - 1] = '\0';
#undef APPEND_CHAR
    return (int)used;
}

int __vsnprintf_chk_bridge(char *d, size_t n, int flags, size_t ds,
                           const char *fmt, const uintptr_t *ap) {
    (void)flags;
    bounds_or_abort(n, ds);
    return android_vformat(d, n, fmt, ap);
}

int __vsprintf_chk_bridge(char *d, int flags, size_t ds, const char *fmt,
                          const uintptr_t *args) {
    (void)flags;
    return android_vformat(d, ds == SIZE_MAX ? 4096 : ds, fmt, args);
}
void __FD_SET_chk_bridge(int fd, fd_set *set, size_t size) { if (size < sizeof(*set)) abort(); FD_SET(fd, set); }
int __FD_ISSET_chk_bridge(int fd, const fd_set *set, size_t size) { if (size < sizeof(*set)) abort(); return FD_ISSET(fd, set); }
ssize_t __sendto_chk_bridge(int fd, const void *b, size_t n, size_t bs, int flags, const struct sockaddr *a, socklen_t al) { bounds_or_abort(n, bs); return sendto(fd, b, n, flags, a, al); }
int __register_atfork_bridge(void *prepare, void *parent, void *child, void *dso) { (void)prepare; (void)parent; (void)child; (void)dso; return 0; }
void android_set_abort_message_bridge(const char *message) {
    char line[512];
    snprintf(line, sizeof(line), "Caustic Android abort message: %s",
             message ? message : "(null)");
    debug_log(line);
}
char *setlocale_bridge(int category, const char *locale) {
    (void)category;
    (void)locale;
    /* Caustic only requires the invariant C locale. Returning NULL here
     * reports locale initialization failure to libc++ and can leave its
     * parsing facets absent. */
    static char c_locale[] = "C";
    return c_locale;
}
void openlog_bridge(const char *ident, int option, int facility) { (void)ident; (void)option; (void)facility; }
void closelog_bridge(void) {}
void syslog_bridge(int priority, const char *format, ...) { (void)priority; (void)format; }

void __aeabi_memcpy_bridge(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memmove_bridge(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset_bridge(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr_bridge(void *d, size_t n) { memset(d, 0, n); }

void sincos_bridge(double x, double *s, double *c) { bridge_sincos(x, s, c); }
void sincosf_bridge(float x, float *s, float *c) { bridge_sincosf(x, s, c); }
size_t __ctype_get_mb_cur_max_bridge(void) { return MB_CUR_MAX; }
int posix_memalign_bridge(void **out, size_t alignment, size_t size) {
    if (!out || alignment < sizeof(void *) || (alignment & (alignment - 1))) return 22;
    *out = memalign(alignment, size);
    return *out ? 0 : 12;
}
unsigned int umask_bridge(unsigned int mask) { (void)mask; return 0; }
