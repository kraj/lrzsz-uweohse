/*
  lrz - receive files with x/y/zmodem
  Copyright (C) until 1988 Chuck Forsberg (Omen Technology INC)
  Copyright (C) 1994 Matt Porter, Michael D. Black
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

  originally written by Chuck Forsberg
*/

#include "zglobal.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifndef HAVE_GETOPT_LONG
#define getopt_long(argc,argv,str,longopts,longind) getopt(argc,argv,str)
#endif

#include "timing.h"

#ifndef HAVE_O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

unsigned Baudrate = 2400;

static FILE *fout;


static int Lastrx;
static int Crcflg;
static int Firstsec;
int errors;
static int Restricted=1;	/* restricted; no /.. or ../ in filenames */
static int skip_if_not_found;
static unsigned int Segmentsize = 0;

static char *Pathname;

static int MakeLCPathname=true;	/* make received pathname lower case */
int Verbose=0;
static int Quiet=0;		/* overrides logic that would otherwise set verbose */
static int Nflag = 0;		/* Don't really transfer files */
static int Rxclob=false;	/* Clobber existing file */
static int Rxbinary=false;	/* receive all files in bin mode */
static int Rxascii=false;	/* receive files in ascii (translate) mode */
static int Thisbinary;		/* current file is to be received in bin mode */
static bool try_resume=false;
static bool junk_path=false;
bool no_timeout=false;
enum zm_type_enum protocol;
bool under_rsh=false;
bool zmodem_requested=false;

static char *secbuf;

#if defined(F_GETFD) && defined(F_SETFD) && defined(O_SYNC)
static int o_sync = 0;
#endif
static int rzfiles (struct zm_fileinfo *);
static int tryz (void);
static void checkpath (const char *name);
static void chkinvok (const char *s);
static void report (int sct);
static void uncaps (char *s);
static int IsAnyLower (const char *s);
static int putsec (struct zm_fileinfo *zi, char *buf, size_t n);
enum safe_mkdir { MKDIR_NO, MKDIR_OK };
static FILE *safe_open (const char *path, const char *mode, enum safe_mkdir mkdir_ok);
static int procheader (char *name, struct zm_fileinfo *);
static int wcgetsec (size_t *Blklen, char *rxbuf, unsigned int maxtime);
static int wcrx (struct zm_fileinfo *);
static int wcrxpn (struct zm_fileinfo *, char *rpn);
static int wcreceive (int argc, char **argp);
static int rzfile (struct zm_fileinfo *);
static void usage (int exitcode, const char *what);
static int closeit (struct zm_fileinfo *);
static void ackbibi (void);
static void zmputs (const char *s);
static int install_signal_handler (int sig);
static mode_t lrz_umask = 0077;

static long buffersize=32768;
static unsigned long min_bps=0;
static long min_bps_time=120;

static char Lzmanag;		/* Local file management request */
static char zconv;		/* ZMODEM file conversion request */
static char zmanag;		/* ZMODEM file management request */
static char ztrans;		/* ZMODEM file transport request */
bool Zctlesc;		/* Encode control characters */
unsigned int Zrwindow = 1400;	/* RX window size (controls garbage count) */

static int tryzhdrtype=ZRINIT;	/* Header type to send corresponding to Last rx close */
static time_t stop_time;

int enable_syslog=true;

#define DO_SYSLOG_FNAME(message) do { \
	if (enable_syslog) { \
		const char *shortname; \
		if (!zi->fname) \
			shortname="no.name"; \
		else { \
			shortname=strrchr(zi->fname,'/'); \
			if (!shortname) \
				shortname=zi->fname; \
			else \
				shortname++; \
		} \
        lsyslog message ; \
	} \
} while(0)
#define DO_SYSLOG(message) do { \
	if (enable_syslog) { \
        lsyslog message ; \
	} \
} while(0)


/* async-signal-safe signal handler */
LRZSZ_NORETURN static void
bibi_sighandler(int n)
{
	if (zmodem_requested)
		zmputs(Attn);
	canit(STDOUT_FILENO);
	io_mode(0,0);
	_exit(128+n);
}

/* called from normal control flow (checkpath, rbsb) to clean things up */
LRZSZ_NORETURN void
bibi(int n)
{
	if (zmodem_requested)
		zmputs(Attn);
	canit(STDOUT_FILENO);
	io_mode(0,0);
	fatal_error(128+n,0,_("caught signal %d; exiting"), n);
}

static int
install_signal_handler(int sig)
{
#ifdef HAVE_SIGACTION
	struct sigaction sa, old;
	sa.sa_handler = bibi_sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(sig, &sa, &old) == 0) {
		if (old.sa_handler == SIG_IGN) {
			sigaction(sig, &old, NULL);
			return 0;
		}
		return 1;
	}
	return 0;
#else
	if (signal(sig, bibi_sighandler) == SIG_IGN) {
		signal(sig, SIG_IGN);
		return 0;
	}
	return 1;
#endif
}

#ifdef HAVE_GETOPT_LONG
static struct option const long_options[] =
{
	{"append", no_argument, NULL, '+'},
	{"ascii", no_argument, NULL, 'a'},
	{"segmentsize", no_argument, NULL, 'A'},
	{"binary", no_argument, NULL, 'b'},
	{"bufsize", required_argument, NULL, 'B'},
	{"allow-commands", no_argument, NULL, 'C'},
	{"escape", no_argument, NULL, 'e'},
	{"rename", no_argument, NULL, 'E'},
	{"help", no_argument, NULL, 'h'},
	{"crc-check", no_argument, NULL, 'H'},
	{"junk-path", no_argument, NULL, 'j'},
	{"errors", required_argument, NULL, 'F'},
	{"disable-timeouts", no_argument, NULL, 'O'},
	{"disable-timeout", no_argument, NULL, 'O'}, /* i can't get it right */
	{"min-bps", required_argument, NULL, 'm'},
	{"min-bps-time", required_argument, NULL, 'M'},
	{"newer", no_argument, NULL, 'n'},
	{"newer-or-longer", no_argument, NULL, 'N'},
	{"protect", no_argument, NULL, 'p'},
	{"resume", no_argument, NULL, 'r'},
	{"restricted", no_argument, NULL, 'R'},
	{"quiet", no_argument, NULL, 'q'},
	{"stop-at", required_argument, NULL, 's'},
	{"timesync", no_argument, NULL, 'S'},
	{"timeout", required_argument, NULL, 't'},
	{"keep-uppercase", no_argument, NULL, 'u'},
	{"unrestrict", no_argument, NULL, 'U'},
	{"verbose", no_argument, NULL, 'v'},
	{"windowsize", required_argument, NULL, 'w'},
	{"with-crc", no_argument, NULL, 'c'},
	{"xmodem", no_argument, NULL, 'X'},
	{"ymodem", no_argument, NULL, 'g'},
	{"zmodem", no_argument, NULL, 'Z'},
	{"overwrite", no_argument, NULL, 'y'},
	{"null", no_argument, NULL, 'D'},
	{"version", no_argument, NULL, 'V'},
	{"journal", required_argument, NULL, 'J'},
	{"syslog", optional_argument, NULL , 2},
	{"delay-startup", required_argument, NULL, 4},
	{"o-sync", no_argument, NULL, 5},
	{"o_sync", no_argument, NULL, 5},
	{NULL,0,NULL,0}
};
#endif

LRZSZ_NORETURN static void
show_version(void)
{
	printf ("%s (GNU %s) %s\n", lrzsz_progname, PACKAGE, VERSION);
        exit(0);
}

int
main(int argc, char *argv[])
{
	char *cp;
	int npats;
	char **patts=NULL; /* keep compiler quiet */
	int exitcode=0;
	int c;
	unsigned int startup_delay=0;
        const char *journal_filename;
    
        lrzsz_set_progname (argv[0], "lrz"); /* needed for the error handling => early */

	Rxtimeout = 100;
	setbuf(stderr, NULL);
	if ((cp=getenv("SHELL")) && cp[0]) {
		const char *base = strrchr(cp, '/');
		base = base ? base + 1 : cp;
		if (strcmp(base, "rsh") == 0 || strcmp(base, "rksh") == 0
			|| strcmp(base, "rbash") == 0 || strcmp(base, "rshell") == 0)
			under_rsh=true;
	}
	if ((getenv("ZMODEM_RESTRICTED"))!=NULL)
		Restricted=2;

	/* rights for temporary and unfinished files  */
	umask(lrz_umask);

	from_cu();
	chkinvok(argv[0]);	/* if called as [-]rzCOMMAND set flag */

	journal_filename=getenv("LRZ_JOURNAL");
        if (!journal_filename || !*journal_filename) {
	    journal_filename=getenv("LRZSZ_JOURNAL");
        }
        if (journal_filename && *journal_filename) {
            stats_journal_open(journal_filename);
        }

	openlog(lrzsz_progname,LOG_PID,SYSLOG_FACILITY);

	setlocale (LC_ALL, "");
        setlocale (LC_TIME, "C"); /* do not translate just the timestamp of a syslog message. uh. */
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
	while ((c = getopt_long (argc, argv, 
		"A:a+bB:cCDeEghHJ:m:M:nNOprRqs:St:uUvVw:XZy",
		long_options, (int *) 0)) != EOF)
	{
		switch (c)
		{
		case 0:
			break;
		case '+': Lzmanag = ZF1_ZMAPND; break;
		case 'a': Rxascii=true;  break;
                case 'A':
                        Segmentsize = (unsigned int) lrzsz_strtoul( "-A / --segmentsize", optarg, NULL, 0, 65535);
                        break;
		case 'b': Rxbinary=true; break;
		case 'B': 
			if (strcmp(optarg,"auto")==0)
				fatal_error(1,0,_("the auto buffersize option has been removed; see the manual"));
                        buffersize = lrzsz_strtoul( _("-B / --bufsize"), optarg, "km", 0, 0);
			break;
		case 'c': Crcflg=true; break;
		case 'D': Nflag = true; break;
		case 'E': Lzmanag = ZF1_ZMCHNG; break;
		case 'e': Zctlesc = 1; break;
	case 'h': usage(0,NULL); break;
	case 'J':
		stats_journal_open(optarg);
		break;
	case 'H': Lzmanag= ZF1_ZMCRC; break;
		case 'j': junk_path=true; break;
		case 'm':
			min_bps = lrzsz_strtoul( _("-m / --min-bps"), optarg, "km", 0, 0);
			break;
		case 'M':
 			min_bps_time = lrzsz_strtoul( _("-M / --min-bps-time"), optarg, "k", 1, LONG_MAX);
			break;
		case 'N': Lzmanag = ZF1_ZMNEWL;  break;
		case 'n': Lzmanag = ZF1_ZMNEW;  break;
		case 'O': no_timeout=true; break;
		case 'p': Lzmanag = ZF1_ZMPROT;  break;
		case 'q': Quiet=true; Verbose=0; break;
		case 's':
			if (isdigit((unsigned char) (*optarg))) {
				struct tm *tm;
				time_t t;
				int hh,mm;
				char *nex;
				
				hh = strtoul (optarg, &nex, 10);
				if (hh>23)
					usage(2,_("hour too large (0..23)"));
				if (*nex!=':')
					usage(2, _("unparsable stop time\n"));
                                nex++;
                                mm = strtoul(nex, &nex, 10);
                                if (mm>59)
                                        usage(2,_("minute too large (0..59)"));
                                
                                t=time(NULL);
                                tm=localtime(&t);
                                tm->tm_hour=hh;
                                tm->tm_min=mm;
				stop_time=mktime(tm);
				if (stop_time<t)
					stop_time+=86400L; /* one day more */
				if (stop_time - t <10)
					usage(2,_("stop time too small"));
			} else {
			        stop_time = (time(0)) + (time_t) lrzsz_strtoul( _("-s / --stop-at"), optarg, NULL, 1, LONG_MAX);
			}
			break;


		case 'r': 
			if (try_resume) 
				Lzmanag= ZF1_ZMCRC;
			else
				try_resume=true;  
			break;
		case 'R':
			if (Restricted < 2)
				Restricted=2;
			break;
		case 'C':
		case 'S':
			fprintf(stderr,
				_("this option has been removed. See the manual page for more information.\n"));
			exit(1);
		case 't':
                        Rxtimeout = (unsigned int) lrzsz_strtoul( "-t / --timeout", optarg, NULL, 10, 1000);
			break;
		case 'w':
                        Zrwindow = (unsigned int) lrzsz_strtoul( "-w / --windowsize", optarg, NULL, 1, 8192);
			break;
		case 'u':
			MakeLCPathname=false; break;
		case 'U':
			if (!under_rsh)
				Restricted=0;
			else  {
				DO_SYSLOG((LOG_INFO,"--unrestrict option used under restricted shell"));
				fatal_error(1,0,
	_("security violation: can't do that under restricted shell\n"));
			}
			break;
		case 'v':
			++Verbose; break;
		case 'V':
			show_version();  break;
		case 'X': protocol=ZM_XMODEM; break;
		case 'g':   protocol=ZM_YMODEM; break;
		case 'Z': protocol=ZM_ZMODEM; break;
		case 'y':
			Rxclob=true; break;
		case 2:
			if (optarg && (!strcmp(optarg,"off") || !strcmp(optarg,"no"))) {
				if (under_rsh)
					lrzsz_warning(0, _("cannot turnoff syslog"));
				else
					enable_syslog=false;
			}
			else
				enable_syslog=true;
			break;
                case 4:
                        startup_delay = (unsigned int) lrzsz_strtoul( "--delay-startup", optarg, NULL, 0, 0);
			break;
		case 5:
#if defined(F_GETFD) && defined(F_SETFD) && defined(O_SYNC)
			o_sync=1;
#else
			lrzsz_warning(0, _("O_SYNC not supported by the kernel"));
#endif
			break;
		default:
			usage(2,NULL);
		}

	}

	if (getuid()!=geteuid()) {
		fatal_error(1,0,
		_("this program was never intended to be used setuid\n"));
	}

	secbuf=xmalloc(1+LRZSZ_MAX_SUBPACKET_LEN);

	/* initialize zsendline tab */
	zsendline_init();
	if (startup_delay)
		sleep(startup_delay);

	npats = argc - optind;
	patts=&argv[optind];

	if (npats > 1)
		usage(2,_("garbage on commandline"));
	if (protocol!=ZM_XMODEM && npats)
		usage(2, _("garbage on commandline"));
	if (Fromcu && !Quiet) {
		if (Verbose == 0)
			Verbose = 2;
	}

	vfile("%s %s\n", lrzsz_progname, VERSION);

        {
            struct timespec start_timespec;
            int ret;
            ret=clock_gettime(CLOCK_MONOTONIC, &start_timespec);
            if (ret) {
                stat_for_all.start_time=0;
            } else {
                stat_for_all.start_time=start_timespec.tv_sec+((double)start_timespec.tv_nsec/1e9);
            }
        }
        stat_for_all.direction = 'r';
        stat_for_file.direction = 'r';

	io_mode(0,1);
	readline_setup(0, LRZSZ_MAX_SUBPACKET_LEN, LRZSZ_MAX_SUBPACKET_LEN*2);
	install_signal_handler(SIGINT);
	install_signal_handler(SIGTERM);
	install_signal_handler(SIGPIPE);
	if (wcreceive(npats, patts)==ERROR) {
		exitcode=0x80;
		canit(STDOUT_FILENO);
	}
	io_mode(0,0);
	if (exitcode && !zmodem_requested)	/* bellow again with all thy might. */
		canit(STDOUT_FILENO);
	if (Verbose)
	{
		fputs("\r\n",stderr);
		if (exitcode)
			fputs(_("Transfer incomplete\n"),stderr);
		else
			fputs(_("Transfer complete\n"),stderr);
	}
        stats_account_finalize_file();
        stats_log(&stat_for_all,false);
        stats_journal_close();
	exit(exitcode);
}

LRZSZ_NORETURN static void
usage(int exitcode, const char *what)
{
	FILE *f=stdout;

	if (exitcode)
	{
		if (what)
			fprintf(stderr, "%s: %s\n",lrzsz_progname,what);
		fprintf (stderr, _("Try `%s -h' for more information.\n"),
			lrzsz_progname);
		exit(exitcode);
	}

	fprintf(f, _("%s version %s\n"), lrzsz_progname,
		VERSION);

	fprintf(f,_("Usage: %s [options] [filename.if.xmodem]\n"), lrzsz_progname);
	fputs(_("Receive files with ZMODEM/YMODEM/XMODEM protocol\n"),f);
	fputs(_(
		"    (X) = option applies to XMODEM only\n"
		"    (Y) = option applies to YMODEM only\n"
		"    (Z) = option applies to ZMODEM only\n"
		),f);
	fputs(_(
"  -+, --append                append to existing files\n"
"  -a, --ascii                 ASCII transfer (change CR/LF to LF)\n"
"  -A, --segmentsize           ask for an ACK every ... bytes\n"
"  -b, --binary                binary transfer\n"
"  -B, --bufsize N             buffer N bytes before writing to disk\n"
"  -c, --with-crc              Use 16 bit CRC (X)\n"
"  -D, --null                  write all received data to /dev/null\n"
"      --delay-startup N       sleep N seconds before doing anything\n"
"  -e, --escape                Escape control characters (Z)\n"
"  -E, --rename                rename any files already existing\n"
"  -h, --help                  Help, print this usage message\n"
"  -H, --crc-check             accept file if lengths and/or CRCs differ (Z)\n"
"  -J, --journal FILE          append transfer statistics to FILE\n"
"  -m, --min-bps N             stop transmission if BPS below N\n"
"  -M, --min-bps-time N          for at least N seconds (default: 120)\n"
"  -n, --newer                 receive file if source newer (Z)\n"
"  -N, --newer-or-longer       receive file if source newer or longer (Z)\n"
"  -O, --disable-timeouts      disable timeout code, wait forever for data\n"
"      --o-sync                open output file(s) in synchronous write mode\n"
"  -p, --protect               protect existing files\n"
"  -q, --quiet                 quiet, no progress reports\n"
"  -r, --resume                try to resume interrupted file transfer (Z)\n"
"  -R, --restricted            restricted, more secure mode\n"
"  -s, --stop-at {HH:MM|+N}    stop transmission at HH:MM or in N seconds\n"
/* -S was timesync */
"      --syslog[=off]          turn syslog on or off, if possible\n"
"  -t, --timeout N             set timeout to N tenths of a second\n"
"  -u, --keep-uppercase        keep upper case filenames\n"
"  -U, --unrestrict            disable restricted mode (if allowed to)\n"
"  -v, --verbose               be verbose, provide debugging information\n"
"  -V, --version               show version\n"
"  -w, --windowsize N          Window is N bytes (Z)\n"
"  -X  --xmodem                use XMODEM protocol\n"
"  -y, --overwrite             Yes, clobber existing file if any\n"
"  -g, --ymodem                use YMODEM protocol\n"
"  -Z, --zmodem                use ZMODEM protocol\n"
"\n"
"short options use the same arguments as the long ones\n"
	),f);
#ifndef HAVE_GETOPT_LONG
        fputs(_("The long options (--...) are not available on your system (getopt_long is not available)\n"), f);
#endif
	exit(exitcode);
}

/*
 * Let's receive something already.
 */

static int 
wcreceive(int argc, char **argp)
{
	int c;
	struct zm_fileinfo zi;
	const char *shortname=NULL;
	zi.fname=NULL;
	zi.modtime=0;
	zi.mode=0;
	zi.bytes_total=0;
	zi.bytes_sent=0;
	zi.bytes_received=0;
	zi.bytes_skipped=0;
	zi.eof_seen=0;

	if (protocol!=ZM_XMODEM || argc==0) {
		Crcflg=1;
		if ( !Quiet)
			vstringf(_("%s waiting to receive."), lrzsz_progname);
		if ((c=tryz())!=0) {
			if (c == ZCOMPL)
				return OK;
			if (c == ERROR)
				goto fubar;
			c = rzfiles(&zi);

			shortname=NULL;
			if (c)
				goto fubar;
		} else {
                        /* YMODEM path. */
			for (;;) {
			if (Verbose > 1 || enable_syslog)
				timing_reset();
			shortname=NULL;
				if (wcrxpn(&zi,secbuf)== ERROR)
					goto fubar;
				if (secbuf[0]==0)
					return OK;
				if (procheader(secbuf, &zi) == ERROR)
					goto fubar;
                                stats_set_filename(zi.fname);
				shortname=strrchr(zi.fname,'/');
				if (shortname)
					shortname++;
				else
					shortname=zi.fname;
				if (wcrx(&zi)==ERROR)
					goto fubar;

				if (Verbose > 1 || enable_syslog) {
					double d;
					long bps;
					d=timing_elapsed(NULL);
					if (d==0)
						d=0.5; /* can happen if timing uses time() */
					bps=(long) ((zi.bytes_received-zi.bytes_skipped)/d);

					if (Verbose>1) {
						vstringf(
							_("\rBytes received: %7ld/%7ld   BPS:%-6ld                \r\n"),
							(long) zi.bytes_received, (long) zi.bytes_total, bps);
					}
					if (enable_syslog)
						lsyslog(LOG_INFO,"%s/%s: %zu Bytes, %ld BPS",
							shortname,protname(),zi.bytes_received, bps);
				}
                                stat_for_file.files_ok++;
                                stats_account_one();
			}
		}
	} else {
		char dummy[128] = ""; /* empty filename; procheader uses name + 1 + strlen(name) */
		zi.bytes_total = DEFBYTL;

		if (Verbose > 1 || enable_syslog)
		timing_reset();
		procheader(dummy, &zi);

		if (Pathname)
			free(Pathname);
		errno=0;
		Pathname=xstrdup(*argp);

                checkpath(Pathname);
                if (Nflag) {
                        /* -D: write to /dev/null instead (as procheader()
                         * does for ZMODEM/YMODEM) */
                        free(Pathname);
                        Pathname=xstrdup("/dev/null");
                }
                stats_set_filename(Pathname);
                shortname=strrchr(*argp,'/');
                if (shortname)
                    shortname++;
                else
                    shortname=*argp;
                if (Verbose>1) {
                    vchar('\n');
                    vstringf(_("%s: ready to receive %s"), lrzsz_progname, Pathname);
                    vstring("\r\n");
                }

                if ((fout=safe_open(Pathname, "w", MKDIR_NO)) == NULL) {
                    if (enable_syslog)
                        lsyslog(LOG_ERR,"%s/%s: cannot open: %s",
                                shortname,protname(), strerror(errno));
                    return ERROR;
                }
                if (wcrx(&zi)==ERROR) {
                    goto fubar;
                }
                /*  stat_for_file.files_ok++; done by wcrx. */
                stats_account_one();
                if (Verbose > 1 || enable_syslog) {
                    double d;
                    long bps;
                    d=timing_elapsed(NULL);
                    if (d==0)
                        d=0.5; /* can happen if timing uses time() */
                    bps=(long) ((zi.bytes_received-zi.bytes_skipped)/d);
                    if (Verbose) {
                        vstringf(
                                _("\rBytes received: %7zu   BPS:%-6ld                \r\n"),
                                zi.bytes_received, bps);
                    }
                    if (enable_syslog)
                        lsyslog(LOG_INFO,"%s/%s: %zu Bytes, %ld BPS",
                                shortname,protname(),zi.bytes_received, bps);
                }
        }
        return OK;
fubar:
        if (enable_syslog)
            lsyslog(LOG_ERR,"%s/%s: got error",
                    shortname ? shortname : "no.name", protname());
        canit(STDOUT_FILENO);
        if (fout) {
            fclose(fout);
            fout = NULL;
        }

        if (Restricted && Pathname) {
            unlink(Pathname);
            vstringf(_("\r\n%s: %s removed.\r\n"), lrzsz_progname, Pathname);
        }
        stat_for_file.files_err++;
        stats_account_one();
        return ERROR;
}


/*
 * this receives the YMODEM header.
 *
 * Fetch a pathname from the other end as a C ctyle ASCIZ string.
 * Length is indeterminate as long as less than Blklen
 * A null string represents no more files (YMODEM)
 */
static int 
wcrxpn(struct zm_fileinfo *zi, char *rpn)
{
    int c;
    size_t Blklen=0;		/* record length of received packets */

    READLINE_PF(1);

et_tu:
    Firstsec=true;
    zi->eof_seen=false;
    sendline(Crcflg?WANTCRC:NAK);
    flushmo();
    purgeline(0); /* Do read next time ... */
    while ((c = wcgetsec(&Blklen, rpn, 100)) != 0) {
        if (c == WCEOT) {
            zperr( _("Pathname fetch returned EOT"));
            stat_for_file.out_of_sync_messages+=1;
            sendline(ACK);
            flushmo();
            purgeline(0);	/* Do read next time ... */
            READLINE_PF(1);
            goto et_tu;
        }
        return ERROR;
    }
    sendline(ACK);
    flushmo();
    return OK;
}

/*
 * this is the XMODEM receiver.
 * this is the YMODEM body receiver.
 *
 * Historical note:
 * Adapted from CMODEM13.C, written by
 * Jack M. Wierda and Roderick W. Hart
 */
static int 
wcrx(struct zm_fileinfo *zi)
{
    int sectnum, sectcurr;
    char sendchar;
    size_t Blklen;

    Firstsec=true;sectnum=0; 
    zi->eof_seen=false;
    sendchar=Crcflg?WANTCRC:NAK;

    for (;;) {
        sendline(sendchar);	/* send it now, we're ready! */
        flushmo();
        purgeline(0);	/* Do read next time ... */
        sectcurr=wcgetsec(&Blklen, secbuf, 
                (unsigned int) ((sectnum&0x7f) ? 50 : 130));
        if (sectcurr==((sectnum+1) &0xff)) {
            sectnum++;
            /* report the real section/block number, not sectcurr
             * this is (a) more correct, and
             * (b) avoid overwriting 255 with 0 to create 055…
             */
            report(sectnum);
            /* if using xmodem we don't know how long a file is */
            if (zi->bytes_total && R_BYTESLEFT(zi) < Blklen)
                Blklen=R_BYTESLEFT(zi);
            zi->bytes_received+=Blklen;
            if (putsec(zi, secbuf, Blklen)==ERROR)
                return ERROR;
            sendchar=ACK;
        }
        else if (sectcurr==(sectnum&0xff)) {
            zperr( _("Received dup Sector"));
            sendchar=ACK;
            stat_for_file.out_of_sync_messages+=1;
        }
        else if (sectcurr==WCEOT) {
            if (closeit(zi)) {
                stat_for_file.local_errors+=1;
                return ERROR;
            }
            stat_for_file.files_ok+=1;
            sendline(ACK);
            flushmo();
            purgeline(0);	/* Do read next time ... */
            return OK;
        }
        else if (sectcurr==ERROR) /* wcgetsec accounted for stats. */
            return ERROR;
        else {
            zperr( _("Sync Error"));
            stat_for_file.out_of_sync_messages+=1;
            return ERROR;
        }
    }
}

/*
 * Wcgetsec fetches a Ward Christensen type sector.
 * Returns sector number encountered or ERROR if valid sector not received,
 * or CAN CAN received
 * or WCEOT if eot sector
 * time is timeout for first char, set to 4 seconds thereafter
 ***************** NO ACK IS SENT IF SECTOR IS RECEIVED OK **************
 *    (Caller must do that when he is good and ready to get next sector)
 */
static int
wcgetsec(size_t *Blklen, char *rxbuf, unsigned int maxtime)
{
    int checksum, wcj, firstch, c;
    unsigned short oldcrc;
    char *p;
    int sectcurr;

    for (Lastrx=errors=0; errors<RETRYMAX; errors++) {

        if ((firstch=READLINE_PF(maxtime))==STX) {
            *Blklen=1024; goto get2;
        }
        if (firstch==SOH) {
            *Blklen=128;
get2:
            sectcurr=READLINE_PF(1);
            if ((sectcurr+(oldcrc=READLINE_PF(1)))==0xff) {
                oldcrc=checksum=0;
                for (p=rxbuf,wcj=*Blklen; --wcj>=0; ) {
                    if ((firstch=READLINE_PF(1)) < 0)
                        goto bilge;
                    oldcrc=updcrc(firstch, oldcrc);
                    checksum += (*p++ = firstch);
                }
                if ((firstch=READLINE_PF(1)) < 0)
                    goto bilge;
                if (Crcflg) {
                    oldcrc=updcrc(firstch, oldcrc);
                    stat_for_file.total_message_bytes+=*Blklen;
                    if ((firstch=READLINE_PF(1)) < 0)
                        goto bilge;
                    oldcrc=updcrc(firstch, oldcrc);
                    if (oldcrc & 0xFFFF) {
                        stat_for_file.invalid_messages+=1;
                        zperr( _("CRC"));
                    } else {
                        Firstsec=false;
                        stat_for_file.valid_messages+=1;
                        return sectcurr;
                    }
                } else {
                    stat_for_file.total_message_bytes+=*Blklen;
                    if (((checksum-firstch)&0xff)==0) {
                        Firstsec=false;
                        stat_for_file.valid_messages+=1;
                        return sectcurr;
                    }
                    else {
                        stat_for_file.invalid_messages+=1;
                        zperr( _("Checksum"));
                    }
                }
            }
            else {
                stat_for_file.format_errors+=1;
                zperr(_("Sector number garbled"));
            }
        }
        /* make sure eot really is eot and not just mixmash */
        else if (firstch==EOT
                 && ((c = READLINE_PF(1)) == TIMEOUT || c == RCDO))
            return WCEOT;
        else if (firstch==CAN) {
            if (Lastrx==CAN) {
                zperr( _("Sender Cancelled"));
                stat_for_file.cancels+=1;
                return ERROR;
            } else {
                Lastrx=CAN;
                continue;
            }
        }
        else if (firstch==TIMEOUT) {
            if (Firstsec)
                goto humbug;
bilge:
            stat_for_file.timeouts+=1;
            zperr( _("TIMEOUT"));
        } else {
            zperr( _("Got 0x%x sector header"), (unsigned int) firstch);
        }

humbug:
        Lastrx=0;
        {
            int cnt=1000;
            int dc;
            /* drain garbage, but give up immediately if the line is gone */
            while(cnt-- && (dc=READLINE_PF(1))!=TIMEOUT && dc!=ERROR && dc!=RCDO)
                ;
        }
        if (Firstsec) {
            sendline(Crcflg?WANTCRC:NAK);
            flushmo();
            purgeline(0);	/* Do read next time ... */
        } else {
            maxtime=40;
            sendline(NAK);
            flushmo();
            purgeline(0);	/* Do read next time ... */
        }
    }
    /* try to stop the bubble machine. */
    canit(STDOUT_FILENO);
    stat_for_file.too_many_errors+=1;
    return ERROR;
}

#define ZCRC_DIFFERS (ERROR+1)
#define ZCRC_EQUAL (ERROR+2)
/*
 * do ZCRC-Check for open file f.
 * check at most check_bytes bytes (crash recovery). if 0 -> whole file.
 * remote file size is remote_bytes.
 */
static int 
do_crc_check(FILE *f, size_t remote_bytes, size_t check_bytes) 
{
    struct stat st;
    unsigned long crc;
    unsigned long rcrc;
    size_t n;
    int c;
    int t1=0,t2=0;
    if (-1==fstat(fileno(f),&st)) {
        stat_for_file.local_errors+=1;
        DO_SYSLOG((LOG_ERR,"cannot fstat open file: %s",strerror(errno)));
        return ERROR;
    }
    if (check_bytes==0 && ((size_t) st.st_size)!=remote_bytes)
        return ZCRC_DIFFERS; /* shortcut */

    crc=0xFFFFFFFFL;
    n=check_bytes;
    if (n==0)
        n=st.st_size;
    while (n-- && ((c = getc(f)) != EOF))
        crc = UPDC32(c, crc);
    crc = ~crc;
    clearerr(f);  /* Clear EOF */
    fseeko(f, 0, 0);

    while (t1<3) {
        stohdr(check_bytes);
        zshhdr(ZCRC, Txhdr);
        t2=0;
        while(t2<3) {
            size_t tmp;
            c = zgethdr(Rxhdr, 0, &tmp);
            rcrc=(unsigned long) tmp;
            switch (c) {
                case TIMEOUT:
                    stat_for_file.timeouts+=1;
                    break;
                case ERROR:
                default: /* ignore */
                    break;
                case ZFIN:
                    stat_for_file.out_of_sync_messages+=1;
                    return ERROR;
                case ZRINIT:
                    stat_for_file.out_of_sync_messages+=1;
                    return ERROR;
                case ZCAN:
                    if (Verbose)
                        vstringf(_("got ZCAN"));
                    stat_for_file.cancels+=1;
                    return ERROR;
                    break;
                case ZCRC:
                    if (crc!=rcrc)
                        return ZCRC_DIFFERS;
                    return ZCRC_EQUAL;
                    break;
            }
            t2++;
        }
        t1++;
    }
    stat_for_file.too_many_errors+=1;
    return ERROR;
}

/*
 * Open a file w/o following symlinks, resolving relative to the CWD.
 * - O_NOFOLLOW prevents the final path component from being a symlink;
 * - every intermediate directory component is opened with O_DIRECTORY|O_NOFOLLOW,
 *   so a symlinked directory component fails instead of being followed
 * - with mkdir_ok, missing intermediate directories are created via mkdirat().
 * Maps fopen-style mode strings to open() flags.
 * This is complicated, but race free.
 *
 * @return FILE *|NULL
 */
/*
 * Map fopen-style mode strings to open() flags.
 */
static int
fopen_mode_to_flags(const char *mode)
{
    int flags = 0;
    const char *p;

    for (p = mode; *p; p++) {
        switch (*p) {
            case 'r':
                flags |= O_RDONLY;
                break;
            case 'w':
                flags |= O_WRONLY | O_CREAT | O_TRUNC;
                break;
            case 'a':
                flags |= O_WRONLY | O_CREAT | O_APPEND;
                break;
            case '+':
                flags &= ~(O_RDONLY | O_WRONLY);
                flags |= O_RDWR;
                break;
            default:
                break;
        }
    }
    return flags;
}

/*
 * Open path (relative to the CWD) without following symlinks.
 * Walks intermediate components with openat(); the final component
 * is opened with extra_flags ored in (caller supplies O_CREAT etc.).
 * Returns an fd, or -1 with errno set.
 * - O_NOFOLLOW prevents the final path component from being a symlink;
 * - every intermediate directory component is opened with O_DIRECTORY|O_NOFOLLOW,
 *   so a symlinked directory component fails instead of being followed
 * - with mkdir_ok, missing intermediate directories are created via mkdirat().
 * This is complicated, but race free.
 *
 * @return fd, -1 on error
 */
static int
safe_open_fd(const char *path, int open_flags, enum safe_mkdir mkdir_ok)
{
    int dirfd;
    int fd;
    int saved_errno;

    open_flags |= O_NOFOLLOW;

    if (path[0] == '/')
        return -1; /* absolute paths are rejected by checkpath() */

    dirfd = open(".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dirfd < 0)
        return -1;

    {
        /* walk intermediate components with openat(), creating them if asked */
        char *tmp = xstrdup(path);
        char *start = tmp;
        char *end = tmp + strlen(tmp);
        char *slash;
        const char *last;

        while ((slash = memchr(start, '/', (size_t) (end - start))) != NULL) {
            if (slash == start) { /* leading or doubled '/' */
                start = slash + 1;
                continue;
            }
            *slash = '\0';
            fd = openat(dirfd, start, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (fd < 0 && errno == ENOENT && mkdir_ok == MKDIR_OK) {
                if (mkdirat(dirfd, start, 0700) == 0 || errno == EEXIST)
                    fd = openat(dirfd, start, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            }
            if (fd < 0) {
                close(dirfd);
                free(tmp);
                return -1; /* ELOOP: symlinked component; ENOENT; ... */
            }
            close(dirfd);
            dirfd = fd;
            start = slash + 1;
        }
        last = start; /* final component, relative to dirfd */
        if (*last == '\0') { /* path ended in '/', e.g. "sub/" */
            close(dirfd);
            free(tmp);
            errno = EISDIR;
            return -1;
        }
        if (last[0] == '.' && (last[1] == '\0' ||
                               (last[1] == '.' && last[2] == '\0'))) {
            close(dirfd);
            free(tmp);
            errno = EACCES;
            return -1;
        }
        fd = openat(dirfd, last, open_flags, 0666);
        saved_errno = errno;
        close(dirfd);
        free(tmp);
        errno = saved_errno;
        return fd;
    }
}

static FILE *
safe_open(const char *path, const char *mode, enum safe_mkdir mkdir_ok)
{
    int fd;
    FILE *fp;

    fd = safe_open_fd(path, fopen_mode_to_flags(mode), mkdir_ok);
    if (fd < 0)
        return NULL;
    fp = fdopen(fd, mode);
    if (!fp)
        close(fd);
    return fp;
}

/*
 * Atomically claim a free "name.N" (N = 0..999) for ZMCHNG rename,
 * creating it with O_CREAT|O_EXCL (race free: no stat-then-use).
 * Returns a FILE* open for writing, and the claimed name in *claimed
 * (xmalloc'ed). On failure returns NULL: errno==EEXIST means all 1000
 * names are taken (policy reject), anything else is a local error.
 */
static FILE *
claim_rename_target(const char *name, char **claimed)
{
    size_t newnamelen = strlen(name) + 5;
    char *tmpname = xmalloc(newnamelen);
    int i;
    int fd = -1;
    FILE *fp;

    for (i = 0; i < 1000; i++) {
        snprintf(tmpname, newnamelen, "%s.%d", name, i);
        fd = safe_open_fd(tmpname, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, MKDIR_NO);
        if (fd >= 0)
            break;
        if (errno != EEXIST)
            break; /* EACCES, ELOOP (symlink game), ...: give up */
        /* name.N taken, try the next suffix */
    }
    if (i == 1000) {
        errno = EEXIST; /* exhausted all suffixes */
        free(tmpname);
        return NULL;
    }
    if (fd < 0) { /* non-EEXIST error, errno preserved */
        free(tmpname);
        return NULL;
    }
    fp = fdopen(fd, "w");
    if (!fp) {
        int saved_errno = errno;
        close(fd);
        unlink(tmpname);
        errno = saved_errno;
        free(tmpname);
        return NULL;
    }
    *claimed = tmpname;
    return fp;
}

/*
 * Process incoming file information header
 * returns OK or ERROR.
 */
static int
procheader(char *name, struct zm_fileinfo *zi)
{
    const char *openmode;
    char *p;
    static char *name_static=NULL;
    char *nameend;
    stats_start_one();

    if (name_static)
        free(name_static);
    if (junk_path) {
        p=strrchr(name,'/');
        if (p) {
            p++;
            if (!*p) {
                /* alert - file name ended in with a / */
                if (Verbose)
                    vstringf(_("file name ends with a /, skipped: %s\n"),name);
                DO_SYSLOG((LOG_ERR,"file name ends with a /, skipped: %s", name));
                stat_for_file.policy_rejects+=1;
                return ERROR;
            }
            name=p;
        }
    }
    name_static=xstrdup(name);
    zi->fname=name_static;

    if (Verbose>2) {
        vstringf(_("zmanag=%d, Lzmanag=%d\n"),zmanag,Lzmanag);
        vstringf(_("zconv=%d\n"),zconv);
    }

    /* set default parameters and overrides */
    openmode = "w";
    Thisbinary = (!Rxascii) || Rxbinary;
    if (Lzmanag)
        zmanag = Lzmanag;

    /*
     *  Process ZMODEM remote file management requests
     */
    /* NOTE: zconv == ZCNL (2) is honored below for compatibility with
     * 40 year old peers, although the current ZMODEM documentation says
     * a receiver MUST assume ZCBIN for anything but ZCBIN/ZCRESUM.
     * See man page -a/--ascii: deprecated. */
    if (!Rxbinary && zconv == ZCNL)	/* Remote ASCII override */
        Thisbinary = 0;
    if (zconv == ZCBIN)	/* Remote Binary override */
        Thisbinary = true;
    if (Thisbinary && zconv == ZCBIN && try_resume)
        zconv=ZCRESUM;
    if (zmanag == ZF1_ZMAPND && zconv!=ZCRESUM)
        openmode = "a";
    if (skip_if_not_found)
        openmode="r+";

    zi->bytes_total = DEFBYTL;
    zi->mode = 0; 
    zi->eof_seen = 0; 
    zi->modtime = 0;

    nameend = name + 1 + strlen(name);
    if (*nameend) {	/* file coming from Unix or DOS system */
        unsigned long modtime = 0;
        long bytes_total = 0;
        unsigned long mode = 0;
        if (sscanf(nameend, "%ld%lo%lo", &bytes_total, &modtime, &mode) >= 1) {
            zi->modtime = (time_t) modtime;
            zi->bytes_total = (size_t) bytes_total;
            zi->mode = (mode_t) mode;
            /* NOTE: acting on the historical full-st_mode marker below is
             * a SHOULD NOT per doc/zmodem-current.txt §4 ("file access
             * rights"), kept for compatibility with old peers. See also
             * the mode checks in closeit()/S1. */
            if (zi->mode & UNIXFILE)
                ++Thisbinary;
        }
    }

    /* Check for existing file */
    if (zconv != ZCRESUM && !Rxclob && (zmanag&ZF1_ZMMASK) != ZF1_ZMCLOB
            && (zmanag&ZF1_ZMMASK) != ZF1_ZMAPND
            && (fout=safe_open(name, "r", MKDIR_NO))) {
        struct stat sta;
        char *tmpname;
        if (zmanag == ZF1_ZMNEW || zmanag==ZF1_ZMNEWL) {
            if (-1==fstat(fileno(fout),&sta)) {
                int e=errno;
                if (Verbose)
                    vstringf(_("file exists, skipped: %s\n"),name);
                DO_SYSLOG((LOG_ERR,"cannot fstat open file %s: %s",
                            name,strerror(e)));
                stat_for_file.local_errors+=1;
                fclose(fout);
                fout = NULL;
                return ERROR;
            }
            if (zmanag == ZF1_ZMNEW) {
                if (sta.st_mtime > zi->modtime) {
                    DO_SYSLOG((LOG_INFO,"skipping %s: newer file exists", name));
                    fclose(fout);
                    fout = NULL;
                    stat_for_file.skipped+=1;
                    return ERROR; /* skips file */
                }
            } else {
                /* newer-or-longer */
                if (((size_t) sta.st_size) >= zi->bytes_total 
                        && sta.st_mtime > zi->modtime) {
                    DO_SYSLOG((LOG_INFO,"skipping %s: longer+newer file exists", name));
                    fclose(fout);
                    fout = NULL;
                    stat_for_file.skipped+=1;
                    return ERROR; /* skips file */
                }
            }
            fclose(fout);
            fout = NULL;
        } else if (zmanag==ZF1_ZMCRC) {
            int r=do_crc_check(fout,zi->bytes_total,0);
            if (r==ERROR) {
                fclose(fout);
                fout = NULL;
                return ERROR;
            }
            if (r!=ZCRC_DIFFERS) {
                fclose(fout);
                fout = NULL;
                stat_for_file.skipped++;
                return ERROR; /* skips */
            }
            fclose(fout);
            fout = NULL;
        } else {
            fclose(fout);
            fout = NULL;
            if ((zmanag & ZF1_ZMMASK)!=ZF1_ZMCHNG) {
                stat_for_file.skipped+=1;
                if (Verbose)
                    vstringf(_("file exists, skipped: %s\n"),name);
                return ERROR;
            }
            /* try to rename: atomically claim "name.N" via O_CREAT|O_EXCL
             * (no stat-then-use race), keeping the claimed file open */
            fout = claim_rename_target(name, &tmpname);
            if (!fout) {
                if (errno == EEXIST) {
                    /* all 1000 names taken */
                    stat_for_file.policy_rejects+=1;
                } else {
                    stat_for_file.local_errors+=1;
                    if (Verbose)
                        vstringf(_("cannot claim %s.N: %s\n"),name,strerror(errno));
                }
                return ERROR;
            }
            free(name_static);
            name_static=tmpname;
            zi->fname=name_static;
        }
    }

    /* if a rename target was claimed, the final name is already fixed;
     * don't run the CP/M and lower-case transformations on it */
    if (!fout) {
    if (!*nameend) {		/* File coming from CP/M system */
        for (p=name_static; *p; ++p)		/* change / to _ */
            if ( *p == '/')
                *p = '_';

        if (p > name_static && *--p == '.')		/* zap trailing period */
            *p = 0;
    }

    /* NOTE: the UNIXFILE check is the same legacy marker as above (V4):
     * a full st_mode from an old sender means "Unix peer, keep case".
     * Deprecated per doc/zmodem-current.txt §4, kept for old peers. */
    if (!zmodem_requested && MakeLCPathname && !IsAnyLower(name_static)
            && !(zi->mode&UNIXFILE))
        uncaps(name_static);
    }

    if (protocol==ZM_XMODEM)
        /* we don't have the filename yet */
        return OK; /* dummy */
    if (Pathname)
        free(Pathname);

    Pathname=xstrdup(name_static);
    if (Verbose) {
        /* overwrite the "waiting to receive" line */
        vstring("\r                                                                     \r");
        vstringf(_("Receiving: %s\n"), name_static);
    }

    checkpath(name_static);
        if (Nflag)
        {
                /* write to /dev/null instead; if a rename target was claimed
                 * already, discard it */
                if (fout) {
                        fclose(fout);
                        fout=NULL;
                }
                free(name_static);
                name_static=xstrdup("/dev/null");
        }

        if (!fout && Thisbinary && zconv==ZCRESUM) {
                struct stat st;
                fout = safe_open(name_static, "r+", MKDIR_NO);
                if (fout && 0==fstat(fileno(fout),&st))
                {
                        int can_resume=true;
                        if (zmanag==ZF1_ZMCRC) {
                                int r=do_crc_check(fout,zi->bytes_total,st.st_size);
                                if (r==ERROR) {
                                        fclose(fout);
                                        fout=NULL;
                                        return ERROR;
                                }
                                if (r==ZCRC_DIFFERS) {
                                        can_resume=false;
                                }
                        }
                        if ((unsigned long)st.st_size > zi->bytes_total) {
                                can_resume=false;
                        }
                        /* retransfer whole blocks */
                        zi->bytes_skipped = st.st_size & ~(1023);
                        if (can_resume) {
                                if (fseeko(fout, (off_t) zi->bytes_skipped, SEEK_SET)) {
                                        stat_for_file.local_errors+=1;
                                        fclose(fout);
                                        fout=NULL;
                                        return ERROR;
                                }
                                goto buffer_it;
                        }
                        else {
                                /* resume impossible, file has changed:
                                 * fall through to the normal open below, which
                                 * truncates (openmode "w" -> O_TRUNC).
                                 * Overwriting the "r+" handle without
                                 * truncation would leave a stale tail of
                                 * the old file if the new one is shorter. */
                                fclose(fout);
                                fout=NULL;
                        }
                }
                zi->bytes_skipped=0;
                if (fout) {
                        fclose(fout);
                        fout=NULL;
                }
        }
        /* fout can already be open here: claim_rename_target() claimed
         * and created "name.N" atomically; reuse that file. */
#ifdef ENABLE_MKDIR
        if (!fout && Restricted < 2)
                fout = safe_open(name_static, openmode, MKDIR_OK);
        else
#endif
        if (!fout)
                fout = safe_open(name_static, openmode, MKDIR_NO);
        if ( !fout)
        {
                int e=errno;
                stat_for_file.local_errors+=1;
                zpfatal(_("cannot open %s"), name_static);
                DO_SYSLOG((LOG_ERR,"%s: cannot open: %s",
                        protname(),strerror(e)));
                return ERROR;
        }

buffer_it:
	{
		static char *s=NULL;
		static size_t last_length=0;
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_SYNC)
		if (o_sync) {
			int oldflags;
			oldflags = fcntl (fileno(fout), F_GETFL, 0);
			if (oldflags>=0 && !(oldflags & O_SYNC)) {
				oldflags|=O_SYNC;
				fcntl (fileno(fout), F_SETFL, oldflags); /* errors don't matter */
			}
		}
#endif

		if (!s && buffersize) {
			last_length=buffersize;
			/* buffer `4096' bytes pages */
			last_length=(last_length+4095)&0xfffff000;
			s=xmalloc(last_length);
		}
		if (s) {
                    setvbuf(fout,s,_IOFBF,last_length);
		}
	}
	zi->bytes_received=zi->bytes_skipped;

	return OK;
}

/*
 * Putsec writes the n characters of buf to receive file fout.
 *  If not in binary mode, carriage returns, and all characters
 *  starting with CPMEOF are discarded.
 */
static int 
putsec(struct zm_fileinfo *zi, char *buf, size_t n)
{
	char *p;

	if (n == 0)
		return OK;
	if (Thisbinary) {
		if (fwrite(buf,n,1,fout)!=1)
			return ERROR;
	}
	else {
		if (zi->eof_seen)
			return OK;
		for (p=buf; n>0; ++p,n-- ) {
			if ( *p == '\r')
				continue;
			if (*p == CPMEOF) {
				zi->eof_seen=true;
				return OK;
			}
			putc(*p ,fout);
		}
	}
	return OK;
}

/* make string s lower case */
static void
uncaps(char *s)
{
	for ( ; *s; ++s)
		if (isupper((unsigned char)(*s)))
			*s = tolower((unsigned char)(*s));
}
/*
 * IsAnyLower returns true if string s has lower case letters.
 */
static int 
IsAnyLower(const char *s)
{
	for ( ; *s; ++s)
		if (islower((unsigned char)(*s)))
			return true;
	return false;
}

static void
report(int sct)
{
	if (Verbose>1)
	{
		vstringf(_("Blocks received: %d"),sct);
		vchar('\r');
	}
}

/*
 * If called as [-][dir/../]vrz set Verbose to 1
 * If called as rb use YMODEM protocol
 */
static void
chkinvok(const char *s)
{
	const char *p;

	p = s;
	while (*p == '-')
		s = ++p;
	while (*p)
		if (*p++ == '/')
			s = p;
	if (*s == 'v') {
		Verbose=1; ++s;
	}
	if (*s == 'l') 
		s++; /* lrz -> rz */
	protocol=ZM_ZMODEM;
	if (s[0]=='r' && s[1]=='x')
		protocol=ZM_XMODEM;
	if (s[0]=='r' && (s[1]=='b' || s[1]=='y'))
		protocol=ZM_YMODEM;
}

static void
checkpath(const char *name)
{
	if (!Restricted)
		return;

	/* don't overwrite any file in very restricted mode.
	 * don't overwrite hidden files in restricted mode */
	{
		FILE *noleak = NULL;
		const char *bn;
		bn = strrchr(name, '/');
		bn = bn ? bn + 1 : name;
		if ((Restricted == 2 || *bn == '.') && (noleak = safe_open(name, "r", MKDIR_NO)) != NULL) {
			fclose(noleak);
			canit(STDOUT_FILENO);
			vstring("\r\n");
			vstringf(_("%s: %s exists\n"),
				lrzsz_progname, name);
			bibi(-1);
		}
	}

	/* deny absolute paths */
	if (name[0] == '/')
		goto violation;

	/* walk path components */
	{
		const char *cp;
		for (cp = name; *cp; ) {
			const char *slash = strchr(cp, '/');
			size_t len = slash ? (size_t)(slash - cp) : strlen(cp);
			if (len > 0) {
				if (Restricted > 1 && cp[0] == '.')
					goto violation;
				if (len == 2 && strncmp(cp, "..", 2) == 0)
					goto violation;
			}
			if (!slash)
				break;
			cp = slash + 1;
		}
	}
	return;

violation:
	canit(STDOUT_FILENO);
	vstring("\r\n");
	vstringf(_("%s:\tSecurity Violation"), lrzsz_progname);
	vstring("\r\n");
	bibi(-1);
}

/*
 * Initialize for Zmodem receive attempt, try to activate Zmodem sender
 *  Handles ZSINIT frame
 *  Return ZFILE if Zmodem filename received, -1 on error,
 *   ZCOMPL if transaction finished,  else 0
 */
static int
tryz(void)
{
	int c, n;
	int zrqinits_received=0;
	size_t bytes_in_block=0;

	if (protocol!=ZM_ZMODEM)		/* Check for "rb" program name */
		return 0;

	for (n=zmodem_requested?15:5; 
		 (--n + zrqinits_received) >=0 && zrqinits_received<10; ) {
		/* Set buffer length and capability flags */
                stohdr(0L);
                if (tryzhdrtype==ZRINIT) {
                    /* special short number */
		    Txhdr[ZP0] = Segmentsize & 0xff;
		    Txhdr[ZP1] = (Segmentsize / 256) & 0xff;
                }
		/* advertise CANBRK only if the transfer line is a terminal:
		 * a break is a termios concept, unavailable on pipes/sockets
		 * (doc/zmodem-current.txt §10.6: "if you have tcsendbreak and
		 * send via a terminal, set this") */
		if (isatty(0))
			Txhdr[ZF0] = CANFC32|CANFDX|CANOVIO|CANBRK;
		else
			Txhdr[ZF0] = CANFC32|CANFDX|CANOVIO;
		if (Zctlesc)
			Txhdr[ZF0] |= TESCCTL; /* TESCCTL == ESCCTL */
		zshhdr(tryzhdrtype, Txhdr);

		if (tryzhdrtype == ZSKIP)	/* Don't skip too far */
			tryzhdrtype = ZRINIT;	/* CAF 8-21-87 */
again:
		switch (zgethdr(Rxhdr, 0, NULL)) {
		case ZRQINIT:
			/* getting one ZRQINIT is totally ok. Normally a ZFILE follows 
			 * (and might be in our buffer, so don't purge it). But if we
			 * get more ZRQINITs the sender has started before us
			 * and sent ZRQINITs while waiting. 
			 */
			zrqinits_received++;
			continue;
		
		case ZEOF:
			continue;
		case TIMEOUT:
			continue;
		case ZFILE:
			zconv = Rxhdr[ZF0];
			if (!zconv)
				/* resume with sz -r is impossible (at least with unix sz)
				 * if this is not set */
				zconv=ZCBIN;
			if (Rxhdr[ZF1] & ZF1_ZMSKNOLOC) {
				Rxhdr[ZF1] &= ~(ZF1_ZMSKNOLOC);
				skip_if_not_found=true;
			}
			zmanag = Rxhdr[ZF1];
			ztrans = Rxhdr[ZF2];
			tryzhdrtype = ZRINIT;
			c = zrdata(secbuf, LRZSZ_MAX_SUBPACKET_LEN, &bytes_in_block);
			io_mode(0,3);
			if (c == GOTCRCW)
				return ZFILE;
			zshhdr(ZNAK, Txhdr);
			goto again;
		case ZSINIT:
			/* this once was:
			 * Zctlesc = TESCCTL & Rxhdr[ZF0];
			 * trouble: if rz get --escape flag:
			 * - it sends TESCCTL to sz, 
			 *   get a ZSINIT _without_ TESCCTL (yeah - sender didn't know), 
			 *   overwrites Zctlesc flag ...
			 * - sender receives TESCCTL and uses "|=..."
			 * so: sz escapes, but rz doesn't unescape ... not good.
			 */
 			Zctlesc |= TESCCTL & Rxhdr[ZF0];
 			if (zrdata(Attn, ZATTNLEN,&bytes_in_block) == GOTCRCW) {
 				stohdr(0L); /* ZACK to a ZSINIT must carry 0 (see zmodem-current.txt §4) */
 				zshhdr(ZACK, Txhdr);
 				goto again;
 			}
			zshhdr(ZNAK, Txhdr);
			goto again;
		case ZFREECNT:
			stohdr(0);
			zshhdr(ZACK, Txhdr);
			goto again;
		case ZCOMMAND:
			if (zrdata(secbuf, LRZSZ_MAX_SUBPACKET_LEN, &bytes_in_block) == GOTCRCW) {
				if (Verbose) {
					vstringf("%s: %s\n", lrzsz_progname,
						_("remote command execution denied"));
					vstringf("%s: %s\n", lrzsz_progname, secbuf);
				}
                                stohdr(1L); /* be nice and tell the other side that the execution failed. */
                                zshhdr(ZCOMPL, Txhdr);
                                DO_SYSLOG((LOG_INFO,"rexec denied: %s",secbuf));
                                return ZCOMPL;
			}
			zshhdr(ZNAK, Txhdr);
			goto again;
		case ZCOMPL:
			goto again;
		default:
			continue;
		case ZFIN:
			ackbibi();
			return ZCOMPL;
		case ZRINIT:
			if (Verbose)
				vstringf(_("got ZRINIT"));
			return ERROR;
		case ZCAN:
			if (Verbose)
				vstringf(_("got ZCAN"));
			return ERROR;
		}
	}
	return 0;
}


/*
 * Receive 1 or more files with ZMODEM protocol
 */
static int
rzfiles(struct zm_fileinfo *zi)
{
	int c;

	for (;;) {
		timing_reset();
		c = rzfile(zi);
		switch (c) {
		case ZEOF:
			if (Verbose > 1 || enable_syslog) {
				double d;
				long bps;
				d=timing_elapsed(NULL);
				if (d==0)
					d=0.5; /* can happen if timing uses time() */
				bps=(long)((zi->bytes_received-zi->bytes_skipped)/d);
				if (Verbose > 1) {
					vstringf(
						_("\rBytes received: %7ld/%7ld   BPS:%-6ld                \r\n"),
						(long) zi->bytes_received, (long) zi->bytes_total, bps);
				}
				DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: %ld Bytes, %ld BPS",shortname,
						   protname(), (long) zi->bytes_total,bps));
			}
                        stat_for_file.files_ok++;
                        stats_account_one();
			break;
		case ZSKIP:
                        stat_for_file.skipped++;
                        stats_account_one();
                        if (Verbose) 
                                vstringf(_("Skipped"));
                        DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: skipped",shortname,protname()));
			break;
		default: 
		case ERROR:
                        stat_for_file.files_err++;
                        stats_account_one();
			DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error",shortname,protname()));
			return ERROR;
		}
                switch (tryz()) {
                case ZCOMPL:
                        return OK;
                default:
                        return ERROR;
                case ZFILE:
                        break;
                }
	}
}

/*
 * Receive a file with ZMODEM protocol
 *  Assumes file name frame is in secbuf
 * returns: ERROR ZSIKP ZEOF
 */
static int
rzfile(struct zm_fileinfo *zi)
{
	int c, n;
	long last_rxbytes=0;
	unsigned long last_bps=0;
	long not_printed=0;
	time_t low_bps=0;
	size_t bytes_in_block=0;

	zi->eof_seen=false;

	n = 20;

	if (procheader(secbuf,zi) == ERROR) {
		DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: procheader error",
				   shortname,protname()));
		return (tryzhdrtype = ZSKIP);
	}
        stats_set_filename(zi->fname);

	for (;;) {
		stohdr(zi->bytes_received);
		zshhdr(ZRPOS, Txhdr);
	nxthdr:
		c = zgethdr(Rxhdr, 0, NULL);
		switch (c) {
		default:
			DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error: zgethdr returned %d",shortname,
					   protname(),c));
			vfile("rzfile: zgethdr returned %d", c);
			return ERROR;
		case ZNAK:
		case TIMEOUT:
			if ( --n < 0) {
				DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error: zgethdr returned %s",shortname,
					   protname(),c == ZNAK ? "ZNAK" : "TIMEOUT"));
				vfile("rzfile: zgethdr returned %d", c);
				return ERROR;
			}
                        /* traditionally we fell through to the ZFILE block and did an ZRDATA,
                         * but that is wrong.
                         */
                        continue;
		case ZFILE:
			zrdata(secbuf, LRZSZ_MAX_SUBPACKET_LEN, &bytes_in_block);
			continue;
		case ZEOF:
			if (rclhdr(Rxhdr) != (unsigned long) zi->bytes_received) {
				/*
				 * Ignore eof if it's at wrong place - force
				 *  a timeout because the eof might have gone
				 *  out before we sent our zrpos.
				 */
				errors = 0;  goto nxthdr;
			}
			if (closeit(zi)) {
				tryzhdrtype = ZFERR;
				DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error: closeit return <>0",
						   shortname, protname()));
				vfile("rzfile: closeit returned <> 0");
				return ERROR;
			}
			vfile("rzfile: normal EOF");
			return c;
		case ERROR:	/* Too much garbage in header search error */
			if ( --n < 0) {
				DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error: zgethdr returned %d",
						   shortname, protname(),c));
				vfile("rzfile: zgethdr returned %d", c);
				return ERROR;
			}
			zmputs(Attn);
			continue;
		case ZSKIP:
			closeit(zi);
			DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error: sender skipped",
					   shortname, protname()));
			vfile("rzfile: Sender SKIPPED file");
			return c;
		case ZDATA:
			if (rclhdr(Rxhdr) != (unsigned long) zi->bytes_received) {
				if ( --n < 0) {
					vfile("rzfile: out of sync");
					DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: error: out of sync",
					   shortname, protname()));
					return ERROR;
				}
                                /* this is a data block with subframes. if the sender streams
                                 * fast, this is a mass of data, which can easily overflow 
                                 * the garbage max or the cancel max.
                                 * so we read and forget it.
                                 */
				zrdata(secbuf, LRZSZ_MAX_SUBPACKET_LEN, &bytes_in_block);
				zmputs(Attn);  continue;
			}
moredata:
			if ((Verbose>1 || min_bps || stop_time)
				&& (not_printed > (min_bps ? 3 : 7) 
					|| zi->bytes_received > last_bps / 2 + last_rxbytes)) {
				int minleft =  0;
				int secleft =  0;
				time_t now;
				double d;
				d=timing_elapsed(&now);
				if (d==0)
					d=0.5; /* timing_elapsed() might use time() */
				last_bps=(unsigned long) (zi->bytes_received/d);
				if (last_bps > 0) {
					minleft =  (R_BYTESLEFT(zi))/last_bps/60;
					secleft =  ((R_BYTESLEFT(zi))/last_bps)%60;
				}
				if (min_bps) {
					if (low_bps) {
						if (last_bps<min_bps) {
							if (now-low_bps>=min_bps_time) {
								/* too bad */
								vfile(_("rzfile: bps rate %lu below min %lu"), 
									  last_bps, min_bps);
								DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: bps rate low: %lu < %lu",
										   shortname, protname(), last_bps, min_bps));
								return ERROR;
							}
						}
						else
							low_bps=0;
					} else if (last_bps<min_bps) {
						low_bps=now;
					}
				}
				if (stop_time && now>=stop_time) {
					/* too bad */
					vfile(_("rzfile: reached stop time"));
					DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: reached stop time",
							   shortname, protname()));
					return ERROR;
				}
				
				if (Verbose > 1) {
					vstringf(_("\rBytes received: %7ld/%7ld   BPS:%-6lu ETA %02d:%02d  "),
						(long) zi->bytes_received, (long) zi->bytes_total, 
						last_bps, minleft, secleft);
					last_rxbytes=zi->bytes_received;
					not_printed=0;
				}
			} else if (Verbose)
				not_printed++;
			switch (c = zrdata(secbuf, LRZSZ_MAX_SUBPACKET_LEN, &bytes_in_block))
			{
			case ZCAN:
				vfile("rzfile: zrdata returned %d", c);
				DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: zrdata returned ZCAN",
						   shortname, protname()));
				return ERROR;
			case ERROR:	/* CRC error */
				if ( --n < 0) {
					vfile("rzfile: zgethdr returned %d", c);
					DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: zrdata returned ERROR",
							   shortname, protname()));
					return ERROR;
				}
				zmputs(Attn);
				continue;
			case TIMEOUT:
				if ( --n < 0) {
					DO_SYSLOG_FNAME((LOG_INFO, "%s/%s: zrdata returned TIMEOUT",
							   shortname, protname()));
					vfile("rzfile: zgethdr returned %d", c);
					return ERROR;
				}
				continue;
			case GOTCRCW:
				n = 20;
				putsec(zi, secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				vfile("GOTCRCW: bytes_received=%ld, block=%zu, readline_left=%d",
				      (long)zi->bytes_received, bytes_in_block, readline_left);
				stohdr(zi->bytes_received);
				zshhdr(ZACK | 0x80, Txhdr);
				goto nxthdr;
			case GOTCRCQ:
				n = 20;
				putsec(zi, secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				vfile("GOTCRCQ: bytes_received=%ld, block=%zu, readline_left=%d",
				      (long)zi->bytes_received, bytes_in_block, readline_left);
				stohdr(zi->bytes_received);
				zshhdr(ZACK, Txhdr);
				goto moredata;
			case GOTCRCG:
				n = 20;
				putsec(zi, secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				goto moredata;
			case GOTCRCE:
				n = 20;
				putsec(zi, secbuf, bytes_in_block);
				zi->bytes_received += bytes_in_block;
				vfile("GOTCRCE: bytes_received=%ld, block=%zu, readline_left=%d",
				      (long)zi->bytes_received, bytes_in_block, readline_left);
				goto nxthdr;
                        default:
                                ;
			}
		}
	}
}

/*
 * Send a string to the modem, processing for \336 (sleep 1 sec)
 *   and \335 (break signal)
 */
static void
zmputs(const char *s)
{
	const char *p;

	while (s && *s)
	{
		p=strpbrk(s,"\335\336");
		if (!p)
		{
			ssize_t off=0;
			size_t len=strlen(s);
			while ((size_t)off < len) {
				ssize_t n=write(1,s+off,len-off);
				if (n < 0) {
					if (errno == EINTR)
						continue;
					return;
				}
				off += n;
			}
			return;
		}
		if (p!=s)
		{
			ssize_t off=0;
			size_t len=(size_t) (p-s);
			while ((size_t)off < len) {
				ssize_t n=write(1,s+off,len-off);
				if (n < 0) {
					if (errno == EINTR)
						continue;
					return;
				}
				off += n;
			}
			s=p;
		}
		if (*p=='\336')
			sleep(1);
		else
			sendbrk(0);
		p++;
	}
}

/*
 * Close the receive dataset, return OK or ERROR
 */
static int
closeit(struct zm_fileinfo *zi)
{
	int ret;
	ret=fclose(fout);
        fout = NULL;
	if (ret) {
		zpfatal(_("file close error"));
		/* this may be any sort of error, including random data corruption */

		unlink(Pathname);
		return ERROR;
	}
	if (zi->modtime) {
#ifdef HAVE_STRUCT_UTIMBUF
		struct utimbuf timep;
		timep.actime = time(NULL);
		timep.modtime = zi->modtime;
		utime(Pathname, &timep);
#else
		time_t timep[2];
		timep[0] = time(NULL);
		timep[1] = zi->modtime;
		utime(Pathname, timep);
#endif
	}
#ifdef S_ISREG
	if (S_ISREG(zi->mode)) {
#else
	if ((zi->mode&S_IFMT) == S_IFREG) {
#endif
		/* we must not make this program executable if running 
		 * under rsh, because the user might have uploaded an
		 * unrestricted shell.
		 */
		if (under_rsh)
			chmod(Pathname, (0666 & zi->mode) & ~lrz_umask);
		else
			chmod(Pathname, (0777 & zi->mode) & ~lrz_umask);
	}
	return OK;
}

/*
 * Ack a ZFIN packet, let byegones be byegones
 */
static void
ackbibi(void)
{
	int n;

	vfile("ackbibi:");
	stohdr(0L);
	for (n=3; --n>=0; ) {
		purgeline(0);
		zshhdr(ZFIN, Txhdr);
		switch (READLINE_PF(100)) {
		case 'O':
			READLINE_PF(1);	/* Discard 2nd 'O' */
			vfile("ackbibi complete");
			return;
		case RCDO:
			return;
		case TIMEOUT:
		default:
			break;
		}
	}
}


/* End of lrz.c */
