#ifndef CAUSTIC_FORTIFY_H
#define CAUSTIC_FORTIFY_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

void *__memcpy_chk_bridge(void *, const void *, size_t, size_t);
void *__memmove_chk_bridge(void *, const void *, size_t, size_t);
void *__memset_chk_bridge(void *, int, size_t, size_t);
char *__strcpy_chk_bridge(char *, const char *, size_t);
char *__strncpy_chk_bridge(char *, const char *, size_t, size_t);
char *__strncpy_chk2_bridge(char *, const char *, size_t, size_t, size_t);
char *__strcat_chk_bridge(char *, const char *, size_t);
char *__strncat_chk_bridge(char *, const char *, size_t, size_t);
size_t __strlen_chk_bridge(const char *, size_t);
char *__strchr_chk_bridge(const char *, int, size_t);
char *__strrchr_chk_bridge(const char *, int, size_t);
ssize_t __read_chk_bridge(int, void *, size_t, size_t);
ssize_t __write_chk_bridge(int, const void *, size_t, size_t);
size_t __fread_chk_bridge(void *, size_t, size_t, FILE *, size_t);
size_t __fwrite_chk_bridge(const void *, size_t, size_t, FILE *, size_t);
int __open_2_bridge(const char *, int);
int __vsnprintf_chk_bridge(char *, size_t, int, size_t, const char *, const uintptr_t *);
int __vsprintf_chk_bridge(char *, int, size_t, const char *, const uintptr_t *);
void __FD_SET_chk_bridge(int, fd_set *, size_t);
int __FD_ISSET_chk_bridge(int, const fd_set *, size_t);
ssize_t __sendto_chk_bridge(int, const void *, size_t, size_t, int,
                            const struct sockaddr *, socklen_t);
int __register_atfork_bridge(void *, void *, void *, void *);
void android_set_abort_message_bridge(const char *);
char *setlocale_bridge(int category, const char *locale);
void openlog_bridge(const char *, int, int);
void closelog_bridge(void);
void syslog_bridge(int, const char *, ...);

void __aeabi_memcpy_bridge(void *, const void *, size_t);
void __aeabi_memmove_bridge(void *, const void *, size_t);
void __aeabi_memset_bridge(void *, size_t, int);
void __aeabi_memclr_bridge(void *, size_t);

void sincos_bridge(double, double *, double *);
void sincosf_bridge(float, float *, float *);
size_t __ctype_get_mb_cur_max_bridge(void);
int posix_memalign_bridge(void **, size_t, size_t);
unsigned int umask_bridge(unsigned int);

#endif
