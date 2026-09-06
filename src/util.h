#ifndef LRZSZ_UTIL_H
#define LRZSZ_UTIL_H

#include "config.h" /* noreturn and format_printf */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202000L
#  define LRZSZ_NORETURN [[noreturn]]
#elif defined(__GNUC__) || defined(__clang__)
#  define LRZSZ_NORETURN __attribute__((__noreturn__))
#else
#  define LRZSZ_NORETURN
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202000L
#  define LRZSZ_FORMAT_PRINTF(fmtidx, argidx) [[gnu::format(printf, fmtidx, argidx)]]
#elif defined(__GNUC__) || defined(__clang__)
#  define LRZSZ_FORMAT_PRINTF(fmtidx, argidx) __attribute__((format(printf, fmtidx, argidx)))
#else
#  define LRZSZ_FORMAT_PRINTF(fmt, arg)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define LRZSZ_ALWAYS_INLINE __attribute__((always_inline))
#else
#  define LRZSZ_ALWAYS_INLINE
#endif

extern const char * lrzsz_progname;
void lrzsz_set_progname (const char * str, const char *who);

LRZSZ_FORMAT_PRINTF(2,3)
void lrzsz_warning(int errnum, const char *format, ...);

LRZSZ_NORETURN LRZSZ_FORMAT_PRINTF(3,4)
void fatal_error (int status, int errnum, const char *message, ...);

unsigned long lrzsz_strtoul(const char *name, const char *value, const char *valid_suffixes, unsigned long int min, unsigned long int max);

char * xstrdup(const char *s);
void * xmalloc(size_t size);

size_t sanitize_bytes(char *dst, size_t dstcap, const char *src);

void report_transfer_result (int sending, const char *shortname,
    const char *fname, long bytes, long total, long skipped);

int fd_readable(int fd);

#endif
