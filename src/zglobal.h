#ifndef ZMODEM_GLOBAL_H
#define ZMODEM_GLOBAL_H

/* zglobal.h - prototypes etcetera for lrzsz

  Copyright (C) until 1998 Chuck Forsberg (OMEN Technology Inc)
  Copyright (C) 1994 Matt Porter
  Copyright (C) 1996, 1997 Uwe Ohse

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

#include "config.h"
#include <sys/types.h>

#ifdef __GNUC__
# define alloca __builtin_alloca
# define HAVE_ALLOCA 1
#else
# if defined HAVE_ALLOCA_H || defined _LIBC
#  include <alloca.h>
# else
#  ifdef _AIX
#pragma alloca
#  else
#   ifndef alloca
#    ifndef __GLIBC__
char *alloca ();
#    endif
#   endif
#  endif
# endif
#endif

#include <stdarg.h>
#include <stdbool.h>

#  include <sys/time.h>
#  include <time.h>
#  define INCLUDED_SYS_TIME

#ifdef SYS_TIME_WITHOUT_SYS_SELECT
# ifdef INCLUDED_SYS_TIME
#  undef HAVE_SYS_SELECT_H
# endif
#endif


#ifdef HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif
#ifdef __BEOS__
 /* BeOS 4.0: sys/select.h is almost completely #ifdefd out */
#define  HAVE_SOCKET_H
#ifdef HAVE_SOCKET_H
#include <socket.h>
#endif
#endif

#include <string.h>

#include <sys/stat.h>

#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif

#ifdef HAVE_TERMIOS_H
/* get rid of warnings on SCO ODT 3.2 */
struct termios;
# include <termios.h>
# define USE_TERMIOS
#else
# if defined(HAVE_SYS_TERMIOS_H)
#  include <sys/termios.h>
#  define USE_TERMIOS
# else
#  error neither termios.h nor sys/termios.h found. Cannot continue.
# endif
#endif

#define CANBREAK



/* Take care of NLS matters.  */
#if HAVE_LOCALE_H
# include <locale.h>
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# define bindtextdomain(Domain, Directory) /* empty */
# define textdomain(Domain) /* empty */
# define _(Text) Text
#endif

#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#elif defined HAVE_SYS_SYSLOG_H
# include <sys/syslog.h>
#endif
extern int enable_syslog;

#if defined HAVE_UNISTD_H
# include <unistd.h>
#endif
/* The following is from pathmax.h.  */
/* Non-POSIX BSD systems might have gcc's limits.h, which doesn't define
   PATH_MAX but might cause redefinition warnings when sys/param.h is
   later included (as on MORE/BSD 4.3).  */
#if defined(_POSIX_VERSION) || (defined(HAVE_LIMITS_H) && !defined(__GNUC__))
# include <limits.h>
#endif

#ifndef _POSIX_PATH_MAX
# define _POSIX_PATH_MAX 255
#endif

#if !defined(PATH_MAX) && defined(_PC_PATH_MAX)
# define PATH_MAX (pathconf ("/", _PC_PATH_MAX) < 1 ? 1024 : pathconf ("/", _PC_PATH_MAX))
#endif

/* Don't include sys/param.h if it already has been.  */
#if defined(HAVE_SYS_PARAM_H) && !defined(PATH_MAX) && !defined(MAXPATHLEN)
# include <sys/param.h>
#endif

#if !defined(PATH_MAX) && defined(MAXPATHLEN)
# define PATH_MAX MAXPATHLEN
#endif

#if !defined(LONG_MAX) && defined(HAVE_LIMITS_H)
# include <limits.h>
#endif

#ifndef PATH_MAX
# define PATH_MAX _POSIX_PATH_MAX
#endif

#include "stats.h"
#include "util.h"

#define OK 0
#define ERROR (-1)

/* Ward Christensen / CP/M parameters - Don't change these! */
#define ENQ 0x05
#define CAN ('X'&0x1f)
#define XOFF ('s'&0x1f)
#define XON ('q'&0x1f)
#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CPMEOF 0x1a
#define WANTCRC 0x43    /* send C not NAK to get crc not checksum */
#define WANTG 0x47  /* Send G not NAK to get nonstop batch xmsn */
#define TIMEOUT (-2)
#define RCDO (-3)
#define WCEOT (-10)

#define RETRYMAX 10

#define UNIXFILE 0xF000  /* The S_IFMT file mask bit for stat */

#define DEFBYTL 2000000000L	/* default rx file size */

enum zm_type_enum {
	ZM_XMODEM,
	ZM_YMODEM,
	ZM_ZMODEM
};

struct zm_fileinfo {
	char *fname;
	time_t modtime;
	mode_t mode;
	size_t bytes_total;
	size_t bytes_sent;
	size_t bytes_received;
	size_t bytes_skipped; /* crash recovery */
	int    eof_seen;
};

#define R_BYTESLEFT(x) ((x)->bytes_total-(x)->bytes_received)

extern enum zm_type_enum protocol;

extern const char *program_name;        /* the name by which we were called */
extern int Verbose;
extern int errors;
extern bool no_timeout;
extern bool Zctlesc;    /* Encode control characters */
extern bool under_rsh;
extern bool turbo_escape;
extern bool zmodem_requested;

void bibi (int n);

#define sendline(c) do { putchar((c) & 0377); stat_for_file.net_bytes_out++; } while (0)
#define xsendline(c) do { putchar(c) ; stat_for_file.net_bytes_out++; } while (0)
#define flushmo() do { fflush(stdout); stat_for_file.net_write_ops++;} while(0)

/* zreadline.c */
extern char *readline_ptr; /* pointer for removing chars from linbuf */
extern int readline_left; /* number of buffered chars left to read */
#define READLINE_PF(timeout) \
    (--readline_left >= 0? (*readline_ptr++ & 0377) : readline_internal(timeout))

int readline_internal (unsigned int timeout);
void readline_purge (void);
void readline_setup (int fd, size_t readnum, size_t buffer_size);


/* rbsb.c */
extern bool Fromcu;
extern bool Twostop;
extern int iofd;
extern unsigned int Baudrate;

LRZSZ_FORMAT_PRINTF(1,2) void zperr (const char *fmt, ...);
LRZSZ_FORMAT_PRINTF(1,2) void zpfatal (const char *fmt, ...);
LRZSZ_FORMAT_PRINTF(1,2) void vfile (const char *format, ...);
#define vchar(x) putc(x,stderr)
#define vstring(x) fputs(x,stderr)

#ifdef __GNUC__
#if __GNUC__ > 1
#define vstringf(...) fprintf(stderr, __VA_ARGS__)
#endif
#endif
#ifndef vstringf
LRZSZ_FORMAT_PRINTF(1,2) void vstringf (const char *format, ...);
#endif
#define VPRINTF(level,format_args) do {if ((Verbose)>=(level)) \
	vstringf format_args ; } while(0)

/* rbsb.c */
int from_cu (void);
int rdchk (int fd);
int io_mode (int fd, int n);
void sendbrk (int fd);
void purgeline (int fd);
void canit (int fd);


#include "crctab.h"

/* zm.c */
#include "zmodem.h"
extern unsigned int Rxtimeout;        /* Tenths of seconds to wait for something */

/* Globals used by ZMODEM functions */
extern int Rxframeind;     /* ZBIN ZBIN32, or ZHEX type of frame received */
extern int Rxtype;     /* Type of header received */
extern unsigned int Zrwindow;       /* RX window size (controls garbage count) */
/* extern int Rxcount; */       /* Count of data bytes received */
extern unsigned char Rxhdr[4];      /* Received header */
extern unsigned char Txhdr[4];      /* Transmitted header */
extern long Txpos;     /* Transmitted file position */
extern int Txfcs32;        /* TURE means send binary frames with 32 bit FCS */
extern int Crc32t;     /* Display flag indicating 32 bit CRC being sent */
extern int Crc32;      /* Display flag indicating 32 bit CRC being received */
extern int Znulls;     /* Number of nulls to send at beginning of ZDATA hdr */
extern char Attn[ZATTNLEN+1];  /* Attention string rx sends to tx on err */

extern void zsendline_init (void);
void zsbhdr (int type, unsigned char *hdr);
void zshhdr (int type, unsigned char *hdr);
void zsdata (const char *buf, size_t length, int frameend);
void zsda32 (const char *buf, size_t length, int frameend);
int zrdata (char *buf, int length, size_t *received);
int zgethdr (unsigned char *hdr, int eflag, size_t *);
void stohdr (size_t pos);
unsigned long rclhdr (unsigned char *hdr);

const char * protname (void);
LRZSZ_FORMAT_PRINTF(2,3) void lsyslog (int, const char *,...);

unsigned getspeed(speed_t code);


#endif
