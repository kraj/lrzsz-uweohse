#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include "zglobal.h"
#include "timing.h"

#ifndef _
# define _(String) String
#endif

const char * lrzsz_progname;

void
lrzsz_set_progname (const char * str, const char *who)
{
    const char *known[]={"lrz","lsz","lrb","lsb","lrx","lsx"};
    const char * p = strrchr(str,'/');
    char *tmp;
    size_t len;
    if (!str) {
        lrzsz_progname=xstrdup(who);
        return;
    }
    if (p && p[1])
        p=p+1;
    else if (!p)
        p=str;
    for (unsigned int i=0;i< sizeof(known) / sizeof (const char *);i++) {
        if (0==strcmp(known[i],p)) {
            lrzsz_progname=known[i];
            return;
        }
    }
    /* asprintf is C11, over our baseline. Well. */
    len=strlen(p)+strlen(who)+4; /* 4: space ( ) null */
    tmp=malloc (len);
    if (!tmp) {
        fatal_error(1,0,"out of memory");
    }
    (void) snprintf(tmp,len,"%s (%s)",p, who);
    lrzsz_progname = tmp;
    return;
}

/* Print the program name and error message MESSAGE, which is a printf-style
 * format string with optional args.
 * If ERRNUM is nonzero, print its corresponding system error message.
 */
LRZSZ_FORMAT_PRINTF(2,0)
static void
lrzsz_vwarning (int errnum, const char *format, va_list ap)
{
    fflush(stdout);

    /* Print program name (or fallback) and colon-space prefix */
    fprintf(stderr, "%s: ", lrzsz_progname && *lrzsz_progname? lrzsz_progname : "lrzsz");

    /* Format the error message with optional arguments */
    vfprintf(stderr, format, ap);

    /* Add system error if errnum is nonzero */
    if (errnum)
        fprintf(stderr, ": %s", strerror(errnum));

    /* Print newline and flush */
    putc ('\n', stderr);
    fflush(stderr);
}

void
lrzsz_warning (int errnum, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    lrzsz_vwarning(errnum, format, args);
    va_end(args);
}

/* like lrzsz_warning(), but exit with status.  */
LRZSZ_NORETURN void
fatal_error (int status, int errnum, const char *message, ...)
{
    va_list args;
    va_start(args, message);
    lrzsz_vwarning(errnum, message, args);
    va_end(args);
    exit(status);
}

/* End-of-transfer BPS summary shared by lrz and lsz. BYTES is the number
 * of bytes actually transferred; SKIPPED bytes (resume) are excluded
 * from the bps and syslog byte count. TOTAL is only printed for
 * receives (pass 0 when sending). */
void
report_transfer_result (int sending, const char *shortname,
    const char *fname, long bytes, long total, long skipped)
{
    long bps;

    if (!(Verbose > 1 || enable_syslog))
        return;
    bps = timing_bps(sending ? bytes : bytes - skipped);
    if (Verbose) {
        if (sending)
            vchar('\r');
        if (Verbose > 1) {
            if (sending)
                vstringf(_("Bytes Sent:%7ld   BPS:%-8ld                        \n"),
                    bytes, bps);
            else
                vstringf(_("\rBytes received: %7ld/%7ld   BPS:%-6ld                \r\n"),
                    bytes, total, bps);
        }
    }
    if (enable_syslog) {
        const char *s = fname ? strrchr(fname, '/') : NULL;
        s = s ? s + 1 : (fname ? fname : shortname);
        lsyslog(LOG_INFO, "%s/%s: %ld Bytes, %ld BPS", s, protname(),
            sending ? bytes : bytes - skipped, bps);
    }
}

unsigned long
lrzsz_strtoul(const char *name, const char *value, const char *valid_suffixes, unsigned long int min, unsigned long int max)
{
  char *p;
  unsigned long int tmp;
  unsigned int factor;

  errno = 0;
  tmp = strtoul (value, &p, 10);
  if (errno != 0)
    fatal_error(2,0, _("%s `%s' is larger than maximum unsigned long int"), name,value);
  if (value[0] == '-')
    fatal_error(2,0, _("%s `%s' is negative"), name,value);
  if (p == value)
    fatal_error(2,0, _("%s `%s' is invalid"), name,value);
  if (*p) {
      unsigned char c;
      if (!valid_suffixes || p[1] || !strchr (valid_suffixes, *p)) {
        fatal_error (2,0, _("%s `%s' is followed by an invalid suffix"), name,value);
      }
      c=*p;
      switch (c) {
        case 'c': factor=1; break;
        case 'k': factor=1024; break;
        case 'm': factor=1024*1024; break;
        case 'g': factor=1024*1024*1024; break;
        default:
        fatal_error (2,0, _("%s `%s' is followed by an invalid suffix"), name,value);
      }
      if (tmp > ULONG_MAX / (unsigned long) factor) {
        fatal_error(2,0, _("%s `%s' is larger than maximum unsigned long int"), name,value);
      }
      tmp *= factor;
  }
  if (tmp < (unsigned long int) min) {
    fatal_error(2,0, _("%s `%s' is smaller than the allowed minimum of `%lu'"), name,value, min);
  }
  if (max && tmp > (unsigned long int) max) {
    fatal_error(2,0, _("%s `%s' is larger than the allowed maximum of `%lu'"), name,value, max);
  }

  return tmp;
}

char *
xstrdup(const char *s)
{
    char *tmp = strdup(s);
    if (!tmp)
        fatal_error(1,errno,"strdup");
    return tmp;
}

void *
xmalloc(size_t size)
{
    char *tmp = malloc(size);
    if (!tmp)
        fatal_error(1,errno,"malloc");
    return tmp;
}

int
fd_readable(int fd)
{
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return select(fd + 1, &fds, NULL, NULL, &tv);
}

/* Log-forging protection for syslog and the transfer journal: escape C0
 * control characters (and DEL) as \xNN; printable ASCII and bytes >=0x80
 * (multi-byte UTF-8 sequences, which live entirely in 0x80-0xff) pass
 * through verbatim. Stops cleanly at an escape boundary when dst runs
 * out; dst is always NUL-terminated. Returns the written length. */
size_t
sanitize_bytes(char *dst, size_t dstcap, const char *src)
{
    size_t o = 0, i;

    for (i = 0; src[i] && o + 4 < dstcap; i++) {
        unsigned char c = (unsigned char) src[i];
        if (c >= 0x20 && c != 0x7f) {
            dst[o++] = (char) c;
        } else {
            snprintf(dst + o, 5, "\\x%02x", c);
            o += 4;
        }
    }
    dst[o] = '\0';
    return o;
}
