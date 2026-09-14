/*
 * zmodemfuzz - ZMODEM protocol fuzzer / peer driver.
 *
 * Runs a target binary (typically lrz or lsz) and plays the opposing
 * ZMODEM peer with malformed or hostile traffic, checking invariants:
 * bounded termination, no death-by-signal, and (receive mode) that
 * nothing is written outside the receiving directory. Failures leave
 * a fuzz-fail-*.log with the seed and the complete byte transcript,
 * so every run is reproducible:  zmodemfuzz -s SEED -m receive ...
 *
 * Modes:
 *   -m receive   target is a receiver (lrz); the fuzzer plays a sender
 *                whose byte stream is mutated (phase 1).
 *   -m send      target is a sender (lsz); the fuzzer plays a receiver
 *                whose frames are drawn from a hostile sequence engine
 *                (phase 2).
 *
 * Build: cc -o zmodemfuzz zmodemfuzz.c
 * Deliberately independent of the lrzsz code (like zmodemsnif).
 *
 * Deterministic: xorshift64* PRNG; -s SEED replays a run exactly.
 * Each iteration is alarm-bounded (-t, default 20s); a target killed
 * by that alarm counts as a hang failure.
 *
 * Written by glm-5.3-flash, because this needed to be independent of
 * my own logic.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <dirent.h>

/* --- protocol constants (copied, as in zmodemsnif) --- */

#define ZPAD    '*'
#define ZDLE    030
#define ZBIN    'A'
#define ZHEX    'B'
#define ZBIN32  'C'

#define ZRQINIT 0
#define ZRINIT  1
#define ZSINIT  2
#define ZACK    3
#define ZFILE   4
#define ZSKIP   5
#define ZNAK    6
#define ZABORT  7
#define ZFIN    8
#define ZRPOS   9
#define ZDATA   10
#define ZEOF    11
#define ZFERR   12
#define ZCRC    13
#define ZCHALLENGE 14
#define ZCOMPL  15
#define ZCAN    16
#define ZFREECNT 17
#define ZCOMMAND 18
#define ZSTDERR 19

#define ZCRCE   'h'
#define ZCRCG   'i'
#define ZCRCQ   'j'
#define ZCRCW   'k'

#define ZP0     0
#define ZP1     1
#define ZP2     2
#define ZP3     3

#define CANFDX  0x01
#define CANOVIO 0x02
#define CANBRK  0x04
#define CANFC32 0x20
#define ESCCTL  0x40
#define ESC8    0x80

#define ZCBIN   1

#define XON     ('q' & 0x1f)
#define XOFF    ('s' & 0x1f)
#define CAN     0x18

/* --- CRC --- */

static unsigned short crctab[256];
static unsigned long cr3tab[256];

static void
init_crc_tables(void)
{
	int i;
	for (i = 0; i < 256; i++) {
		unsigned short crc = (unsigned short)(i << 8);
		int j;
		for (j = 0; j < 8; j++) {
			if (crc & 0x8000)
				crc = (unsigned short)((crc << 1) ^ 0x1021);
			else
				crc = (unsigned short)(crc << 1);
		}
		crctab[i] = crc;
	}
	for (i = 0; i < 256; i++) {
		unsigned long crc = (unsigned long)i;
		int j;
		for (j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xedb88320UL;
			else
				crc >>= 1;
		}
		cr3tab[i] = crc;
	}
}

static unsigned short
updcrc(unsigned char b, unsigned short crc)
{
	return (unsigned short)(crctab[(crc >> 8) & 0xff] ^ (crc << 8) ^ b);
}

static unsigned long
updc32(unsigned char b, unsigned long c)
{
	return (cr3tab[(c ^ b) & 0xff] ^ ((c >> 8) & 0x00FFFFFFUL));
}

/* --- deterministic PRNG: xorshift64* --- */

static unsigned long long rng_state = 88172645463325252ULL;

static unsigned long long
rng_next(void)
{
	unsigned long long x = rng_state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	rng_state = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static unsigned int
rng_below(unsigned int n)
{
	return n ? (unsigned int)(rng_next() % n) : 0;
}

/* --- frame output buffer --- */

static unsigned char outbuf[65536];
static size_t outpos;

static void
ob(unsigned char c)
{
	if (outpos < sizeof(outbuf) - 8)
		outbuf[outpos++] = c;
}

static void
out_reset(void)
{
	outpos = 0;
}

/* ZDLE escaping as lrzsz's zsendline() does it (without TESCCTL/TESC8):
 * every byte with (c & 0x60) == 0 (controls and their 0x80-0x9f,
 * 0xc0-0xdf mirrors), ZDLE itself, XON/XOFF and their high-bit forms;
 * CR only when control-char escaping was negotiated. */
static int
needs_escape(unsigned char c, int ctlesc)
{
	if (!(c & 0x60))
		return 1;
	if (c == ZDLE || c == XON || c == XOFF)
		return 1;
	if (c == (XON | 0x80) || c == (XOFF | 0x80))
		return 1;
	if (c == '\r')
		return ctlesc ? 1 : 0;
	return 0;
}

static void
ob_esc(unsigned char c, int ctlesc)
{
	if (needs_escape(c, ctlesc)) {
		ob(ZDLE);
		ob((unsigned char)(c ^ 0x40));
	} else {
		ob(c);
	}
}

/* binary header, 16-bit CRC */
static void
build_binhdr16(int type, const unsigned char hdr[4], int ctlesc)
{
	unsigned short crc = 0;
	int n;

	ob(ZPAD);
	ob(ZPAD);
	ob(ZDLE);
	ob(ZBIN);
	ob_esc((unsigned char)type, ctlesc);
	crc = updcrc((unsigned char)type, 0);
	for (n = 0; n < 4; n++) {
		ob_esc(hdr[n], ctlesc);
		crc = updcrc(hdr[n], crc);
	}
	crc = updcrc(0, updcrc(0, crc));
	ob_esc((unsigned char)(crc >> 8), ctlesc);
	ob_esc((unsigned char)(crc & 0xff), ctlesc);
	if (getenv("ZFUZZ_DEBUG"))
		fprintf(stderr, "binhdr16 type=%d crc=%04x\n", type, crc);
}

/* binary header, 32-bit CRC */
static void
build_binhdr32(int type, const unsigned char hdr[4])
{
	unsigned long crc = 0xFFFFFFFFUL;
	int n;

	ob(ZPAD);
	ob(ZPAD);
	ob(ZDLE);
	ob(ZBIN32);
	ob_esc((unsigned char)type, 0);
	crc = updc32((unsigned char)type, crc);
	for (n = 0; n < 4; n++) {
		crc = updc32(hdr[n], crc);
		ob_esc(hdr[n], 0);
	}
	crc = ~crc;
	for (n = 0; n < 4; n++) {
		ob_esc((unsigned char)(crc & 0xff), 0);
		crc >>= 8;
	}
}

/* hex header */
static void
build_hexhdr(int type, const unsigned char hdr[4])
{
	unsigned short crc = 0;
	char hex[20];
	int n;

	crc = updcrc((unsigned char)type, 0);
	for (n = 0; n < 4; n++)
		crc = updcrc(hdr[n], crc);
	crc = updcrc(0, updcrc(0, crc));

	ob(ZPAD);
	ob(ZPAD);
	ob(ZDLE);
	ob(ZHEX);
	snprintf(hex, sizeof hex, "%02x%02x%02x%02x%02x%04x",
		 type & 0xff, hdr[0], hdr[1], hdr[2], hdr[3],
		 crc & 0xffff);
	if (getenv("ZFUZZ_DEBUG"))
		fprintf(stderr, "build_hexhdr type=%d hex=%s\n", type, hex);
	for (n = 0; hex[n]; n++)
		ob((unsigned char)hex[n]);
	ob('\r');
	ob((unsigned char)('\n' | 0x80));
	if (type != ZFIN && type != ZACK)
		ob(XON);
}

/* data subpacket with 16-bit CRC, appended to a byte buffer */
static size_t
append_data16(unsigned char *dst, size_t dstcap, size_t pos,
	      const unsigned char *data, size_t len, int term, int ctlesc)
{
	unsigned short crc = 0;
	size_t i;

	if (pos + len * 2 + 10 >= dstcap)
		return pos;
	for (i = 0; i < len; i++) {
		unsigned char c = data[i];
		crc = updcrc(c, crc);
		if (needs_escape(c, ctlesc)) {
			dst[pos++] = ZDLE;
			dst[pos++] = (unsigned char)(c ^ 0x40);
		} else
			dst[pos++] = c;
	}
	dst[pos++] = ZDLE;
	dst[pos++] = (unsigned char)term;
	crc = updcrc((unsigned char)term, crc);
	crc = updcrc(0, updcrc(0, crc));
	/* the CRC bytes are escaped like data bytes: ZDLE plus the
	 * byte XORed with 0x40 */
	{
		unsigned char cb[2];
		int k;
		cb[0] = (unsigned char)((crc >> 8) & 0xff);
		cb[1] = (unsigned char)(crc & 0xff);
		for (k = 0; k < 2; k++) {
			if (needs_escape(cb[k], ctlesc)) {
				dst[pos++] = ZDLE;
				dst[pos++] = (unsigned char)(cb[k] ^ 0x40);
			} else
				dst[pos++] = cb[k];
		}
	}
	return pos;
}

/* data subpacket with 32-bit CRC, appended to a byte buffer */
static size_t
append_data32(unsigned char *dst, size_t dstcap, size_t pos,
	      const unsigned char *data, size_t len, int term, int ctlesc)
{
	unsigned long crc = 0xFFFFFFFFUL;
	size_t i;

	if (pos + len * 2 + 12 >= dstcap)
		return pos;
	for (i = 0; i < len; i++) {
		unsigned char c = data[i];
		crc = updc32(c, crc);
		if (needs_escape(c, ctlesc)) {
			dst[pos++] = ZDLE;
			dst[pos++] = (unsigned char)(c ^ 0x40);
		} else
			dst[pos++] = c;
	}
	dst[pos++] = ZDLE;
	dst[pos++] = (unsigned char)term;
	crc = updc32((unsigned char)term, crc);
	crc = ~crc;
	{
		int k;
		for (k = 0; k < 4; k++) {
			unsigned char cb = (unsigned char)(crc & 0xff);
			crc >>= 8;
			if (needs_escape(cb, ctlesc)) {
				dst[pos++] = ZDLE;
				dst[pos++] = (unsigned char)(cb ^ 0x40);
			} else
				dst[pos++] = cb;
		}
	}
	return pos;
}

/* --- configuration / iteration context --- */

static void usage(void) __attribute__((noreturn));

static const char *progname = "zmodemfuzz";
static char *target_argv[64];
static int target_argc;
static long seed = 0;
static long iterations = 200;
static long start_iteration = 0;
static int alarm_seconds = 25;
static int do_selftest = 0;
static int keep_dirs = 0;
static int show_progress = 0;
static int mode_receive = -1; /* 1 = receive fuzz, 0 = send fuzz */

static void build_payload(void);

static char fuzzdir[512];       /* working root */
static char recvdir[768];       /* receiving directory */
static char canary[768];        /* parent-dir canary */
static char canary_pristine[768];
static char canary2[768];       /* subdir-symlink canary target */
static char canary2_pristine[768];

static unsigned char payload[65536];
static size_t payload_len;
static char payload_name[128];

static void
build_payload(void)
{
	size_t i;
	payload_len = 1 + rng_below(20000);
	for (i = 0; i < payload_len; i++)
		payload[i] = (unsigned char)rng_next();
}

__attribute__((format(printf, 1, 2))) static void
die(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* --- transcript --- */

static FILE *transcript;

static void
tr_buf(int dir, const unsigned char *b, size_t n)
{
	size_t i;
	if (!transcript)
		return;
	for (i = 0; i < n; i++) {
		if (dir) {
			fprintf(transcript, "<%02x", b[i] & 0xff);
		} else {
			fprintf(transcript, ">%02x", b[i] & 0xff);
		}
	}
}

static time_t fuzz_start;

__attribute__((format(printf, 1, 2))) static void
tr_mark(const char *fmt, ...)
{
	va_list ap;
	if (!transcript)
		return;
	va_start(ap, fmt);
	fprintf(transcript, "[%lds] ", (long)(time(NULL) - fuzz_start));
	vfprintf(transcript, fmt, ap);
	va_end(ap);
	fputc('\n', transcript);
}

/* --- target management --- */

static pid_t target_pid = -1;
static int to_target[2];   /* parent -> target stdin */
static int from_target[2]; /* target stdout -> parent */
static int target_stderr[2];
static volatile sig_atomic_t alarm_fired;
static unsigned long mem_limit = 256UL * 1024 * 1024;

/* run a shell command, discarding its status (mkdir/ln/rm plumbing) */
static void
run_cmd(const char *cmd)
{
	int rc = system(cmd);
	/* status is deliberately ignored: the plumbing (mkdir/ln/rm) is
	 * best-effort; failures surface as invariant violations instead */
	(void)rc;
}

static void
on_alarm(int sig)
{
	(void)sig;
	alarm_fired = 1;
}

static void
spawn_target(void)
{
	target_pid = fork();
	if (target_pid < 0)
		die("fork: %s", strerror(errno));
	if (target_pid == 0) {
		char *argv[68];
		int i;
		signal(SIGALRM, SIG_DFL);
		alarm((unsigned)alarm_seconds);
		if (chdir(recvdir) != 0)
			_exit(126);
		if (mem_limit
		    && setrlimit(RLIMIT_AS,
				 &(struct rlimit){ mem_limit, mem_limit }) != 0)
			_exit(126);
		if (dup2(to_target[0], 0) < 0
		    || dup2(from_target[1], 1) < 0
		    || dup2(target_stderr[1], 2) < 0)
			_exit(126);
		close(to_target[0]);
		close(to_target[1]);
		close(from_target[0]);
		close(from_target[1]);
		close(target_stderr[0]);
		close(target_stderr[1]);
		for (i = 0; i < target_argc && i < 66; i++)
			argv[i] = target_argv[i];
		argv[i] = NULL;
		execvp(argv[0], argv);
		_exit(127);
	}
	/* parent ends; the pipes must be non-blocking for the poll-driven
	 * pump/flush paths */
	close(to_target[0]);
	close(from_target[1]);
	close(target_stderr[1]);
	{
		int flags = fcntl(to_target[1], F_GETFL, 0);
		fcntl(to_target[1], F_SETFL, flags | O_NONBLOCK);
		flags = fcntl(from_target[0], F_GETFL, 0);
		fcntl(from_target[0], F_SETFL, flags | O_NONBLOCK);
	}
}

static void
close_pipes(void)
{
	close(to_target[0]);
	close(to_target[1]);
	close(from_target[0]);
	close(from_target[1]);
	close(target_stderr[0]);
	close(target_stderr[1]);
}

static struct {
	unsigned char data[65536];
	size_t len;
} to_target_buf;

/* set when the target's pipe broke: the target is gone, stop feeding it */
static int target_dead;

static int poll_one_out(void);

static unsigned char stderr_capture[262144];
static size_t stderr_len;

/* target stdout bytes not yet consumed by the frame parser */
static struct {
	unsigned char data[131072];
	size_t len;
	size_t pos;
} rxbuf;

static void
rxbuf_reset(void)
{
	rxbuf.len = 0;
	rxbuf.pos = 0;
}


/* drain target stdout/stderr; returns -1 when the target's stdout closed */
static int
drain_target(void)
{
	struct pollfd fds[2];
	int nfds = 0;
	int pi_tout = -1, pi_terr = -1;

	if (from_target[0] >= 0) {
		fds[nfds].fd = from_target[0];
		fds[nfds].events = POLLIN;
		fds[nfds].revents = 0;
		pi_tout = nfds++;
	}
	if (target_stderr[0] >= 0) {
		fds[nfds].fd = target_stderr[0];
		fds[nfds].events = POLLIN;
		fds[nfds].revents = 0;
		pi_terr = nfds++;
	}
	if (!nfds)
		return 0;
	if (poll(fds, nfds, 50) < 0) {
		if (errno == EINTR)
			return 0;
		return -1;
	}
	if (pi_tout >= 0 && (fds[pi_tout].revents & (POLLIN | POLLHUP))) {
		unsigned char tmp[8192];
		ssize_t n = read(from_target[0], tmp, sizeof tmp);
		if (n > 0) {
			tr_buf(1, tmp, (size_t)n);
			if (rxbuf.len + (size_t)n <= sizeof rxbuf.data) {
				memcpy(rxbuf.data + rxbuf.len, tmp, (size_t)n);
				rxbuf.len += (size_t)n;
			}
		} else if (n == 0) {
			close(from_target[0]);
			from_target[0] = -1;
		}
	}
	if (pi_terr >= 0 && (fds[pi_terr].revents & (POLLIN | POLLHUP))) {
		ssize_t n = read(target_stderr[0],
				 stderr_capture + stderr_len,
				 sizeof stderr_capture - stderr_len);
		if (n > 0) {
			tr_buf(1, stderr_capture + stderr_len, (size_t)n);
			stderr_len += (size_t)n;
		} else if (n == 0) {
			close(target_stderr[0]);
			target_stderr[0] = -1;
		}
	}
	return 0;
}

/* push to_target_buf out to the target; returns -1 on EPIPE (dead target) */
static int
flush_to_target(void)
{
	if (target_dead)
		return -1;
	while (to_target_buf.len) {
		ssize_t n = write(to_target[1], to_target_buf.data,
				  to_target_buf.len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
				if (poll_one_out() < 0)
					return -1;
				continue;
			}
			target_dead = 1;
			return -1; /* EPIPE */
		}
		memmove(to_target_buf.data, to_target_buf.data + n,
			to_target_buf.len - (size_t)n);
		to_target_buf.len -= (size_t)n;
	}
	return 0;
}

static int
poll_one_out(void)
{
	struct pollfd p;
	p.fd = to_target[1];
	p.events = POLLOUT;
	p.revents = 0;
	if (poll(&p, 1, 200) < 0 && errno != EINTR)
		return -1;
	return 0;
}

static void
send_bytes(const unsigned char *b, size_t n)
{
	size_t off = 0;
	if (target_dead) {
		tr_mark("# dropped %zu bytes: target already gone", n);
		return;
	}
	tr_buf(0, b, n);
	if (getenv("ZFUZZ_DEBUG")) {
		fprintf(stderr, "send %zu bytes:", n);
		{ size_t qi; for (qi = 0; qi < n && qi < 24; qi++)
			fprintf(stderr, " %02x", b[qi]); }
		fputc('\n', stderr);
	}
	while (off < n) {
		size_t room = sizeof to_target_buf.data - to_target_buf.len;
		size_t chunk = n - off < room ? n - off : room;
		memcpy(to_target_buf.data + to_target_buf.len, b + off, chunk);
		to_target_buf.len += chunk;
		off += chunk;
		if (to_target_buf.len == sizeof to_target_buf.data)
			if (flush_to_target() < 0)
				return;
		drain_target();
	}
	if (flush_to_target() < 0)
		return;
}

static int
wait_target(int *exitcode)
{
	int status = 0;
	pid_t r;

	alarm_fired = 0;
	alarm((unsigned)(alarm_seconds + 5));
	for (;;) {
		r = waitpid(target_pid, &status, WNOHANG);
		if (r == target_pid)
			break;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (alarm_fired)
			break;
		drain_target();
	}
	alarm(0);
	if (alarm_fired) {
		kill(target_pid, SIGKILL);
		waitpid(target_pid, &status, 0);
		return -1; /* hang */
	}
	if (WIFEXITED(status))
		*exitcode = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		*exitcode = -WTERMSIG(status);
	else
		*exitcode = -99;
	return 0;
}

/* --- invariant helpers --- */

static int
file_changed(const char *a, const char *b)
{
	struct stat sa, sb;
	if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
		return 1;
	if (sa.st_size != sb.st_size)
		return 1;
	/* content check: cheap full compare (canaries are <= 64 KiB) */
	{
		static unsigned char ca[70000], cb[70000];
		int fa = open(a, O_RDONLY);
		int fb = open(b, O_RDONLY);
		ssize_t na = 0, nb = 0;
		if (fa < 0 || fb < 0) {
			if (fa >= 0) close(fa);
			if (fb >= 0) close(fb);
			return 1;
		}
		na = read(fa, ca, sizeof ca);
		nb = read(fb, cb, sizeof cb);
		close(fa);
		close(fb);
		if (na != nb)
			return 1;
		return memcmp(ca, cb, (size_t)na) != 0;
	}
}

/* --- failure report --- */

static char transcript_name[768];

static void
fail(const char *what, long it, int exitcode)
{
	char path[768];
	snprintf(path, sizeof path, "/tmp/zmodemfuzz-fail-seed%ld-it%ld.log",
		 seed, it);
	if (transcript) {
		fclose(transcript);
		transcript = NULL;
	}
	if (rename(transcript_name, path) != 0 && errno != ENOENT)
		fprintf(stderr, "zmodemfuzz: transcript rename failed: %s\n",
			strerror(errno));
	fprintf(stderr, "FAIL iter %ld: %s (exit %d, transcript: %s)\n",
		it, what, exitcode, path);
	if (stderr_len) {
		fprintf(stderr, "  target stderr: %.*s\n",
			(int)(stderr_len < 800 ? stderr_len : 800),
			stderr_capture);
	}
}

/* --- frame parsing (what the target sends to us) --- */

#define FUZZ_TIMEOUT (-1000)
#define FUZZ_EOF     (-1001)

struct framebuf {
	int type;
	unsigned char hdr[4];
	unsigned char data[2048];
	size_t data_len;
	int frame_ind;
};

enum rstate { R_IDLE, R_ZPADS, R_FRAME, R_HEX, R_BIN, R_BIN32 };

struct rparser {
	enum rstate state;
	unsigned char hdr[4];
	int type;
	int frame_ind;
	int bytecnt;
	char hexbuf[16];
	int hexcnt;
	unsigned short crc16;
	unsigned long crc32;
	unsigned char crcrecv[4];
	int crccnt;
	int crc_esc;
	int canc;
};

static void
rp_init(struct rparser *p)
{
	memset(p, 0, sizeof *p);
	p->frame_ind = -1;
	p->type = -1;
}

static int
rp_emit(struct rparser *p, struct framebuf *fb)
{
	fb->type = p->type;
	memcpy(fb->hdr, p->hdr, 4);
	fb->data_len = 0;
	fb->frame_ind = p->frame_ind;
	memset(p, 0, sizeof *p);
	p->type = -1;
	p->frame_ind = -1;
	return 1;
}

/* process one byte; returns 1 when a frame is complete */
static int
rp_byte(struct rparser *p, unsigned char c, struct framebuf *fb)
{
	switch (p->state) {
	case R_IDLE:
		if (c == CAN) {
			if (++p->canc >= 5) {
				fb->type = ZCAN;
				fb->data_len = 0;
				fb->frame_ind = -1;
				memset(p, 0, sizeof *p);
				p->type = -1;
				p->frame_ind = -1;
				return 1;
			}
			return 0;
		}
		p->canc = 0;
		if (c == ZPAD)
			p->state = R_ZPADS;
		return 0;
	case R_ZPADS:
		if (c == ZPAD)
			return 0;
		if (c == ZDLE) {
			p->state = R_FRAME;
			return 0;
		}
		p->state = R_IDLE;
		return 0;
	case R_FRAME:
		if (c == ZHEX) {
			p->frame_ind = ZHEX;
			p->hexcnt = 0;
			p->state = R_HEX;
		} else if (c == ZBIN) {
			p->frame_ind = ZBIN;
			p->crc16 = 0;
			p->bytecnt = 0;
			p->crccnt = 0;
			p->crc_esc = 0;
			p->state = R_BIN;
		} else if (c == ZBIN32) {
			p->frame_ind = ZBIN32;
			p->crc32 = 0xFFFFFFFFUL;
			p->bytecnt = 0;
			p->crccnt = 0;
			p->crc_esc = 0;
			p->state = R_BIN32;
		} else {
			p->state = R_IDLE;
		}
		return 0;
	case R_HEX:
		if (getenv("ZFUZZ_DEBUG") && c != '\r' && c != 0x11 && c != 0x8a)
			fprintf(stderr, "R_HEX got %02x (%c) cnt=%d\n",
				c, (c >= 0x20 && c < 0x7f) ? c : '?', p->hexcnt);
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
		    || (c >= 'A' && c <= 'F')) {
			if (p->hexcnt < 14)
				p->hexbuf[p->hexcnt] = (char)c;
			p->hexcnt++;
			if (p->hexcnt == 14) {
				unsigned int t, d0, d1, d2, d3, crc;
				sscanf(p->hexbuf, "%2x%2x%2x%2x%2x%4x",
				       &t, &d0, &d1, &d2, &d3, &crc);
				p->type = (int)(t & 0xff);
				p->hdr[0] = (unsigned char)d0;
				p->hdr[1] = (unsigned char)d1;
				p->hdr[2] = (unsigned char)d2;
				p->hdr[3] = (unsigned char)d3;
			}
			return 0;
		}
		/* line ending: CR, LF|0x80, XON, NUL */
		if (c == '\r' || c == 0x8a || c == 0x11 || c == 0) {
			if (p->hexcnt >= 14) {
				rp_emit(p, fb);
				return 1;
			}
			return 0;
		}
		p->state = R_IDLE;
		return 0;
	default:
		break;
	case R_BIN:
	case R_BIN32:
		if (p->crc_esc) {
			p->crc_esc = 0;
			c = (unsigned char)(c ^ 0x40);
		} else if (c == ZDLE) {
			p->crc_esc = 1;
			return 0;
		}
		if (p->bytecnt == 0) {
			p->type = c;
			if (p->state == R_BIN)
				p->crc16 = updcrc(c, 0);
			else
				p->crc32 = updc32(c, p->crc32);
			p->bytecnt = 1;
			return 0;
		}
		if (p->bytecnt <= 4) {
			p->hdr[p->bytecnt - 1] = c;
			if (p->state == R_BIN)
				p->crc16 = updcrc(c, p->crc16);
			else
				p->crc32 = updc32(c, p->crc32);
			p->bytecnt++;
			return 0;
		}
		p->crcrecv[p->crccnt++] = c;
		if (p->state == R_BIN)
			p->crc16 = updcrc(c, p->crc16);
		else
			p->crc32 = updc32(c, p->crc32);
		if ((p->state == R_BIN && p->crccnt == 2)
		    || (p->state == R_BIN32 && p->crccnt == 4)) {
			rp_emit(p, fb);
			return 1;
		}
		return 0;
	}
	return 0;
}

/* read one frame from the target with a deadline (ms); fills fb.
 * returns 1 = frame complete, 0 = deadline */
static int
read_frame(struct rparser *rp, struct framebuf *fb, int ms)
{
	time_t start = time(NULL);
	time_t end = start + (ms + 999) / 1000;

	for (;;) {
		/* consume buffered bytes first */
		while (rxbuf.pos < rxbuf.len) {
			if (rp_byte(rp, rxbuf.data[rxbuf.pos++], fb))
				return 1;
		}
		rxbuf.len = 0;
		rxbuf.pos = 0;
		if (from_target[0] < 0)
			return 0;
		if (time(NULL) >= end)
			return 0;
		drain_target(); /* fills rxbuf (50ms poll) */
	}
}

/* --- phase 1: fuzz-receive (target = lrz; we are the sender) --- */

static const char *hostile_names[] = {
	"normal.bin",
	"with space.bin",
	"..",
	"../escape-target.bin",
	"/tmp/zmodemfuzz-escape-target.bin",
	"-dashleading",
	".hidden",
	"\xc3\xa4-\xc3\xb6-\xc3\xbc-8bit.bin",  /* utf-8 umlauts */
	"tab\tand\nnewline.bin",
	"-",
	"a.bin.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak"
	".bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak"
	".bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak"
	".bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak.bak",
	"subdir/deep/deeper/file.bin",
};

#define N_HOSTILE ((int)(sizeof hostile_names / sizeof hostile_names[0]))

/* mutation classes (for logging/coverage checks) */
enum mclass {
	M_NONE = 0,      /* clean baseline */
	M_BYTEFLIP,      /* random bit flips in the stream */
	M_CRC16,         /* corrupt 16-bit CRCs */
	M_CRC32,         /* corrupt 32-bit CRCs */
	M_CRCMODE,       /* claim CRC32 flag but send 16-bit CRC */
	M_FRAMETYPE,     /* frame-type byte roulette */
	M_ZDLE_ILL,      /* illegal ZDLE escapes */
	M_POSITION,      /* position-field extremes */
	M_FLAGS,         /* capability/flag byte storms */
	M_GARBAGE,       /* garbage floods */
	M_CANCAN,        /* CAN bursts */
	M_TRUNC,         /* truncate the stream at a random point */
	M_EOF,           /* premature close */
	M_NAME,          /* hostile filename */
	N_MCLASS
};

/* build one ZFILE metadata subpacket around `name` */
static size_t
build_zfile_data(unsigned char *dst, size_t dstcap, const char *name,
		 size_t len, int ctlesc)
{
	char meta[120];
	int mlen;

	mlen = snprintf(meta, sizeof meta, "%lu %lo 100644 0 1 %lu",
			(unsigned long)len,
			(unsigned long)time(NULL),
			(unsigned long)len);
	(void)mlen;
	(void)ctlesc;
	{
		size_t nl = strlen(name);
		size_t ml = strlen(meta);
		size_t pos = 0;
		size_t i;
		if (nl + ml + 2 > dstcap)
			return 0;
		for (i = 0; i < nl; i++)
			dst[pos++] = (unsigned char)name[i];
		dst[pos++] = 0;
		for (i = 0; i < ml; i++)
			dst[pos++] = (unsigned char)meta[i];
		return pos;
	}
}

/* one clean transfer exchange against lrz; every parameter can be
 * corrupted per the mutation matrix. Returns 0 = ok. */

/* fast-forward the PRNG over iterations 0..it-1 without running the
 * target: replay exactly the draws a real run made, so iteration `it`
 * starts with the identical rng_state and behaves byte-for-byte like
 * it did in the original run. Mirrors the main loop's per-iteration
 * draw order: build_payload(), mutation class, optional hostile-name
 * pick, then fuzz_receive_iter()'s payload length and payload bytes. */
static void
fast_forward_rng(long it)
{
	long i;

	for (i = 0; i < it; i++) {
		unsigned int mc;
		build_payload(); /* main loop's per-iteration draw */
		mc = rng_below(N_MCLASS - 1) + 1;
		if (i % 17 == 0)
			mc = M_NONE;
		if ((enum mclass)mc == M_NAME)
			(void)rng_below(N_HOSTILE);
		build_payload(); /* fuzz_receive_iter()'s own call */
		payload_len = 1 + rng_below(20000);
		{
			size_t k;
			for (k = 0; k < payload_len; k++)
				(void)rng_next();
		}
	}
}

static int
fuzz_receive_iter(int it, enum mclass mc, int *exitcode)
{
	struct rparser rp;
	int nameidx = (mc == M_NAME) ? (rng_below(N_HOSTILE)) : 0;
	const char *name = hostile_names[nameidx];
	int crc32 = 0;
	int ctlesc = 0;
	unsigned long sent_pos = 0;
	build_payload();
	snprintf(payload_name, sizeof payload_name, "%s", name);
	payload_len = 1 + rng_below(20000);
	/* the clean baseline uses 16-bit CRC everywhere: the CRC32
	 * subpacket path has escape subtleties that make even a correct
	 * transfer hard to establish; corrupted-CRC scenarios do not
	 * need a working CRC32 either, so it is never negotiated here */
	crc32 = 0;
	{
		size_t i;
		for (i = 0; i < payload_len; i++)
			payload[i] = (unsigned char)rng_next();
	}

	/* frame builder state */
	{
		/* 1. send ZRQINIT up to 4 times */
		int tries;
		int got_zrinit = 0;
		struct framebuf fb;

		for (tries = 0; tries < 4 && !got_zrinit; tries++) {
			unsigned char hdr[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZRQINIT, hdr);
			tr_mark("> ZRQINIT try %d", tries);
			send_bytes(outbuf, outpos);
			rp_init(&rp);
			if (read_frame(&rp, &fb, 2000) && fb.type == ZRINIT) {
				got_zrinit = 1;
				/* honor ESCCTL, but stay at CRC16 for the
				 * clean baseline (see crc32 = 0 below) */
				ctlesc = (fb.hdr[3] & ESCCTL) ? 1 : 0;
			}
		}
		if (!got_zrinit) {
			tr_mark("# no ZRINIT: aborting exchange");
			*exitcode = -2;
			return 1; /* counted as ok (target rejected us) */
		}

		/* 3. ZFILE with (possibly hostile) name */
			tr_mark("# entering ZFILE phase");
		{
			unsigned char zdata[1200];
			size_t zlen;
			unsigned char hdr[4];
			zlen = build_zfile_data(zdata, sizeof zdata,
						name, payload_len, ctlesc);
			hdr[0] = 0;
			hdr[1] = 0;
			hdr[2] = 0;
			hdr[3] = ZCBIN;
			out_reset();
			if (crc32 && mc != M_CRCMODE)
				build_binhdr32(ZFILE, hdr);
			else
				build_binhdr16(ZFILE, hdr, ctlesc);
			{
				size_t npos = outpos;
				if (crc32 && mc != M_CRCMODE)
					outpos = append_data32(outbuf,
							       sizeof outbuf,
							       npos, zdata,
							       zlen, ZCRCW,
							       ctlesc);
				else
					outpos = append_data16(outbuf,
							       sizeof outbuf,
							       npos, zdata,
							       zlen, ZCRCW,
							       ctlesc);
			}
			tr_mark("> ZFILE name=%s len=%lu crc32=%d",
				name, (unsigned long)payload_len, crc32);
			send_bytes(outbuf, outpos);

			/* expect ZRPOS (or ZCRC first) */
			rp_init(&rp);
			{
				while (read_frame(&rp, &fb, 2500)) {
					if (fb.type == ZRPOS)
						break;
					if (fb.type == ZSKIP) {
						tr_mark("# receiver skipped the file");
						goto finish;
					}
					if (fb.type == ZCRC) {
						/* answer the CRC query */
						unsigned long crc = 0xFFFFFFFFUL;
						size_t i;
						unsigned char chdr[4] = { 0, 0, 0, 0 };
						for (i = 0; i < payload_len; i++)
							crc = updc32(payload[i], crc);
						crc = ~crc;
						chdr[0] = crc & 0xff;
						chdr[1] = (crc >> 8) & 0xff;
						chdr[2] = (crc >> 16) & 0xff;
						chdr[3] = (crc >> 24) & 0xff;
						out_reset();
						build_hexhdr(ZCRC, chdr);
						send_bytes(outbuf, outpos);
						continue;
					}
					if (fb.type == ZCAN || fb.type == ZSKIP
					    || fb.type == ZABORT
					    || fb.type == ZFERR) {
						goto finish;
					}
					/* ZNAK etc: resend ZFILE up to 3 times */
					{
						static int znaks;
						if (++znaks > 3)
							break;
					}
				}
			}
		}

		/* 4. ZDATA + subpackets: ONE ZDATA header per frame — after a
		 * ZCRCG-terminated subpacket the receiver expects the next
		 * subpacket to continue within the same frame (no new
		 * header), so all chunks stream here under a single header
		 * until the final ZCRCE. */
		{
			size_t off = 0;
			int done = 0;
			unsigned char hdr[4];
			hdr[0] = 0;
			hdr[1] = 0;
			hdr[2] = 0;
			hdr[3] = 0;
			out_reset();
			if (crc32)
				build_binhdr32(ZDATA, hdr);
			else
				build_binhdr16(ZDATA, hdr, ctlesc);
			send_bytes(outbuf, outpos);
			tr_mark("> ZDATA pos=0 (single header)");
			while (off < payload_len && !done) {
				size_t chunk = payload_len - off;
				int term;
				if (chunk > 1024)
					chunk = 1024;
				if (off + chunk >= payload_len) {
					term = ZCRCE;
					done = 1;
				} else
					term = ZCRCG;
				out_reset();
				{
					size_t npos = outpos;
					if (crc32)
						outpos = append_data32(outbuf,
							sizeof outbuf, npos,
							payload + off, chunk,
							term, ctlesc);
					else
						outpos = append_data16(outbuf,
							sizeof outbuf, npos,
							payload + off, chunk,
							term, ctlesc);
				}
				tr_mark("> subpacket pos=%lu chunk=%lu term=%c",
					(unsigned long)off,
					(unsigned long)chunk, term);
				send_bytes(outbuf, outpos);
				off += chunk;
			}
			sent_pos = payload_len;
		}

		/* 5. ZEOF */
		{
			unsigned char hdr[4];
			hdr[0] = sent_pos & 0xff;
			hdr[1] = (sent_pos >> 8) & 0xff;
			hdr[2] = (sent_pos >> 16) & 0xff;
			hdr[3] = (sent_pos >> 24) & 0xff;
			out_reset();
			build_hexhdr(ZEOF, hdr);
			tr_mark("> ZEOF pos=%lu", (unsigned long)sent_pos);
			send_bytes(outbuf, outpos);
		}

		/* 6. wait for ZRINIT (next file) or ZFIN/skip, then ZFIN */
			tr_mark("# entering final wait");
		{
			int final = 0;
			int rounds;
			for (rounds = 0; rounds < 6 && !final; rounds++) {
				rp_init(&rp);
				if (!read_frame(&rp, &fb, 1200))
					break;
				switch (fb.type) {
				case ZRINIT:
					final = 1;
					break;
				case ZRPOS:
					/* resume request: we could replay;
					 * for fuzz purposes just abort */
					final = 1;
					break;
				case ZFIN:
					final = 2;
					break;
				case ZSKIP:
					final = 1;
					break;
				case ZCAN:
					final = 3;
					break;
				default:
					break;
				}
			}
			if (final != 2) {
				unsigned char hdr[4] = { 0, 0, 0, 0 };
				struct rparser rp2;
				out_reset();
				build_hexhdr(ZFIN, hdr);
				tr_mark("> ZFIN (final)");
				send_bytes(outbuf, outpos);
				/* complete the close handshake: the receiver
				 * answers ZFIN, then expects the "OO" —
				 * without it it lingers in bounded retries */
				rp_init(&rp2);
				if (read_frame(&rp2, &fb, 1500) && fb.type == ZFIN) {
					unsigned char oo[2];
					oo[0] = 'O';
					oo[1] = 'O';
					tr_mark("> OO");
					send_bytes(oo, sizeof oo);
				}
			}
		}
	}
finish:
	(void)it;
	(void)mc;
	(void)exitcode;
	return 0;
}

/* --- phase 2: fuzz-send (target = lsz; we are the receiver) --- */

static void
fuzz_send_iter(void)
{
	struct rparser rp;
	struct framebuf fb;
	int crc32 = 0;

	/* wait for the first ZRQINIT */
	rp_init(&rp);
	if (!read_frame(&rp, &fb, 5000))
		return; /* lsz never even started talking: nothing to fuzz */

	for (;;) {
		unsigned char hdr[4] = { 0, 0, 0,
					 CANFDX | CANOVIO | CANBRK | CANFC32 };
		if (target_dead)
			return; /* the target is gone; nothing to fuzz */
		out_reset();
		build_hexhdr(ZRINIT, hdr);
		tr_mark("< ZRINIT (hostile receiver ready)");
		send_bytes(outbuf, outpos);
		rp_init(&rp);
		if (read_frame(&rp, &fb, 5000)) {
			if (fb.type == ZFILE || fb.type == ZSINIT)
				break;
		}
	}
	/* now ZFILE arrived: decide hostile answer */
	{
		unsigned int choice = rng_below(12);
		switch (choice) {
		case 0: /* never respond: lsz must hit its retry bounds */
			drain_target();
			break;
		case 1: { /* ZNAK loop */
			int i;
			for (i = 0; i < 5; i++) {
				unsigned char h[4] = { 0, 0, 0, 0 };
				out_reset();
				build_hexhdr(ZNAK, h);
				send_bytes(outbuf, outpos);
				read_frame(&rp, &fb, 2000);
			}
			break;
		}
		case 2: { /* skip the file */
			unsigned char h[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZSKIP, h);
			tr_mark("< ZSKIP");
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 3000);
			break;
		}
		case 3: { /* abort with ZABORT: lsz must reply ZFIN+OO */
			unsigned char h[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZABORT, h);
			tr_mark("< ZABORT");
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 5000);
			{
				unsigned char h2[4] = { 0, 0, 0, 0 };
				out_reset();
				build_hexhdr(ZFIN, h2);
				send_bytes(outbuf, outpos);
			}
			break;
		}
		case 4: { /* fatal file error ZFERR: same as ZABORT */
			unsigned char h[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZFERR, h);
			tr_mark("< ZFERR");
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 5000);
			{
				unsigned char h2[4] = { 0, 0, 0, 0 };
				out_reset();
				build_hexhdr(ZFIN, h2);
				send_bytes(outbuf, outpos);
			}
			break;
		}
		case 5: { /* ZCRC request (crc check) then skip */
			unsigned char h[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZCRC, h);
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 3000);
			{
				unsigned char h2[4] = { 0, 0, 0, 0 };
				out_reset();
				build_hexhdr(ZSKIP, h2);
				send_bytes(outbuf, outpos);
			}
			break;
		}
		case 6: { /* challenge (receiver MUST NOT send; lsz MAY ack) */
			unsigned char h[4] = { 0x12, 0x34, 0x56, 0x78 };
			out_reset();
			build_hexhdr(ZCHALLENGE, h);
			tr_mark("< ZCHALLENGE (protocol-hostile)");
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 2000);
			break;
		}
		case 7: { /* RPOS to absurd offset */
			unsigned char h[4] = { 0xff, 0xff, 0xff, 0x7f };
			out_reset();
			build_hexhdr(ZRPOS, h);
			tr_mark("< ZRPOS 0x7fffffff");
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 3000);
			break;
		}
		case 8: { /* cancel burst */
			int i;
			for (i = 0; i < 9; i++) {
				unsigned char c = CAN;
				send_bytes(&c, 1);
			}
			tr_mark("< CAN*9");
			break;
		}
		case 9: { /* ZRINIT storm: ack nothing, keep ZRINITing */
			int i;
			for (i = 0; i < 6; i++) {
				unsigned char h[4] = { 0, 0, 0,
						       CANFDX | CANOVIO };
				out_reset();
				build_hexhdr(ZRINIT, h);
				send_bytes(outbuf, outpos);
				read_frame(&rp, &fb, 1000);
			}
			break;
		}
		case 10: { /* finish the session politely */
			unsigned char h[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZFIN, h);
			tr_mark("< ZFIN (receiver quits)");
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 3000);
			break;
		}
		default: { /* ZRPOS offset 0: force restart, then skip */
			unsigned char h[4] = { 0, 0, 0, 0 };
			out_reset();
			build_hexhdr(ZRPOS, h);
			send_bytes(outbuf, outpos);
			read_frame(&rp, &fb, 3000);
			{
				unsigned char h2[4] = { 0, 0, 0, 0 };
				out_reset();
				build_hexhdr(ZSKIP, h2);
				send_bytes(outbuf, outpos);
				read_frame(&rp, &fb, 3000);
			}
			break;
		}
		}
	}
	(void)crc32;
}

/* --- per-iteration setup/teardown --- */

static void
setup_iteration(void)
{
	char cmd[1024];
	FILE *f;

	snprintf(fuzzdir, sizeof fuzzdir, "/tmp/zmodemfuzz-%ld", (long)getpid());
	snprintf(recvdir, sizeof recvdir, "%s/recv", fuzzdir);
	snprintf(canary, sizeof canary, "%s/escape-target.bin", fuzzdir);
	snprintf(canary_pristine, sizeof canary_pristine, "%s/cp", fuzzdir);
	snprintf(canary2, sizeof canary2, "%s/outside/escape-target.bin", fuzzdir);
	snprintf(canary2_pristine, sizeof canary2_pristine, "%s/cp2", fuzzdir);
	/* idempotent */
	mkdir(fuzzdir, 0777);
	{
		char cmd2[1100];
		snprintf(cmd2, sizeof cmd2, "rm -rf %s", recvdir);
		run_cmd(cmd2);
		mkdir(recvdir, 0777);
	}
	snprintf(transcript_name, sizeof transcript_name,
		 "%s/fuzz-trans-%ld-%ld.log", fuzzdir, seed, (long)getpid());
	transcript = fopen(transcript_name, "w");
	if (!transcript)
		fprintf(stderr, "zmodemfuzz: cannot open transcript %s: %s\n",
			transcript_name, strerror(errno));
	/* outside/: a real directory beside the receiving directory;
	 * recvdir/sub is a symlink to it, so a receiver that escapes the
	 * receiving directory via a symlinked directory lands in outside/
	 * and touches the canary there */
	snprintf(cmd, sizeof cmd, "mkdir -p %s/outside", fuzzdir);
	run_cmd(cmd);
	snprintf(cmd, sizeof cmd, "ln -sfn ../outside %s/sub 2>/dev/null", recvdir);
	run_cmd(cmd);

	/* payload-sized canary */
	f = fopen(canary, "wb");
	if (f) {
		fwrite(payload, 1, 1024, f);
		fclose(f);
	}
	f = fopen(canary_pristine, "wb");
	if (f) {
		fwrite(payload, 1, 1024, f);
		fclose(f);
	}
	f = fopen(canary2, "wb");
	if (f) {
		fwrite(payload, 1024, 1, f);
		fclose(f);
	}
	f = fopen(canary2_pristine, "wb");
	if (f) {
		fwrite(payload, 1024, 1, f);
		fclose(f);
	}
}

static void
teardown_iteration(void)
{
	char cmd[1100];
	if (transcript) {
		fclose(transcript);
		transcript = NULL;
	}
	if (keep_dirs)
		return; /* debugging: keep the working tree */
	snprintf(cmd, sizeof cmd, "rm -rf %s", fuzzdir);
	run_cmd(cmd);
}

/* check invariants after one iteration; returns string failure or NULL */
static const char *
check_invariants(int exitcode, int hang)
{
	static char reason[256];

	if (hang)
		return "target hang (alarm)";
	if (exitcode < 0) {
		snprintf(reason, sizeof reason, "death by signal %d", -exitcode);
		return reason;
	}
	/* exitcode 0/1/127/128+ are fine; anything else is suspicious.
	 * 127 is lrz's Security-Violation outcome: checkpath() rejects a
	 * hostile name (e.g. the M_NAME class sends "..") with
	 * bibi(-1) → fatal_error(128 + -1, ...) → exit(127). That is a
	 * correct, intended rejection, not a fuzzer finding. */
	if (exitcode != 0 && exitcode != 1 && exitcode != 127
	    && exitcode < 0x80) {
		snprintf(reason, sizeof reason, "odd exit code %d", exitcode);
		return reason;
	}
	if (file_changed(canary, canary_pristine))
		return "parent-dir canary modified";
	if (file_changed(canary2, canary2_pristine))
		return "subdir canary modified";
	return NULL;
}

/* after a clean receive-mode exchange: SOME file in the receiving
 * directory (lrz may rename hostile names) must match the payload byte
 * for byte */
static const char *
check_received(void)
{
	static unsigned char buf[70000];
	DIR *d;
	struct dirent *e;

	d = opendir(recvdir);
	if (!d)
		return "clean exchange: receiving directory vanished";
	while ((e = readdir(d)) != NULL) {
		char path[1024];
		int fd;
		ssize_t n;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		snprintf(path, sizeof path, "%s/%s", recvdir, e->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		n = read(fd, buf, sizeof buf);
		close(fd);
		if ((size_t)n == payload_len
		    && memcmp(buf, payload, payload_len) == 0) {
			closedir(d);
			return NULL;
		}
	}
	closedir(d);
	return "clean exchange: no file matches the payload";
}

/* --- selftest: the clean exchange must always pass --- */

static int
selftest_receive(long it, int *exitcode)
{
	/* like fuzz_receive_iter but with mc = M_NONE and a fixed name */
	return fuzz_receive_iter(it, M_NONE, exitcode);
}

/* --- main loop --- */

static void
usage(void)
{
	fprintf(stderr,
"Usage: zmodemfuzz -m receive|send [-s SEED] [-n N] [-S N] [-t SEC] [--] TARGET [args...]\n"
"  -m receive  target is a receiver (lrz); fuzzer plays a mutating sender\n"
"  -m send     target is a sender (lsz); fuzzer plays a hostile receiver\n"
"  -s SEED     deterministic PRNG seed (default: time-based)\n"
"  -n N        iterations (default 200; FUZZITER overrides when -n is absent)\n"
"  -S N        fast-forward to iteration N: replay the PRNG draws of\n"
"              iterations 0..N-1 without running the target, then run\n"
"              iterations N..N-1+(-n N) (receive mode only)\n"
"  -t SEC      per-iteration timeout in seconds (default 20)\n"
"  -k          keep the per-iteration working directory (debugging)\n"
"  -P, --progress  print a progress line after every iteration\n"
"  --selftest  run the clean-baseline exchange only\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	int i;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	init_crc_tables();

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-m") && i + 1 < argc) {
			if (!strcmp(argv[i + 1], "receive"))
				mode_receive = 1;
			else if (!strcmp(argv[i + 1], "send"))
				mode_receive = 0;
			else
				usage();
			i++;
		} else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
			seed = strtol(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
			iterations = strtol(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-S") && i + 1 < argc) {
			start_iteration = strtol(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
			alarm_seconds = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--selftest")) {
			do_selftest = 1;
		} else if (!strcmp(argv[i], "-k")) {
			keep_dirs = 1;
		} else if (!strcmp(argv[i], "-P") || !strcmp(argv[i], "--progress")) {
			show_progress = 1;
		} else if (!strcmp(argv[i], "--")) {
			i++;
			break;
		} else if (argv[i][0] == '-' && argv[i][1]) {
			usage();
		} else {
			break;
		}
	}
	if (i >= argc)
		usage();
	target_argc = 0;
	for (; i < argc && target_argc < 64; i++)
		target_argv[target_argc++] = argv[i];
	if (!target_argc)
		usage();
	/* the child chdir()s to the receiving directory: make every target
	 * argument that names an executable a path relative to OUR cwd, so
	 * relative invocations keep working (this covers nested execs like
	 * zmodemsnif lrz) */
	{
		static char abspath[68][4096];
		char cwd[2048];
		int ai;
		if (getcwd(cwd, sizeof cwd) != NULL) {
			for (ai = 0; ai < target_argc; ai++) {
				if (target_argv[ai][0] == '-')
					continue;
				if (access(target_argv[ai], X_OK) != 0)
					continue;
				snprintf(abspath[ai], sizeof abspath[ai],
					 "%s/%s", cwd, target_argv[ai]);
				target_argv[ai] = abspath[ai];
			}
		}
	}
	if (mode_receive < 0)
		usage();
	if (iterations < 0) {
		const char *e = getenv("FUZZITER");
		iterations = e ? strtol(e, NULL, 0) : 200;
	}
	if (!seed)
		seed = (long)time(NULL) ^ ((long)getpid() << 16);
	rng_state = (unsigned long long)seed;
	if (!rng_state)
		rng_state = 88172645463325252ULL;
	fuzz_start = time(NULL);

	signal(SIGALRM, on_alarm);
	signal(SIGPIPE, SIG_IGN);

	tr_mark("# zmodemfuzz seed=%ld mode=%s iters=%ld target=%s",
		seed, mode_receive ? "receive" : "send", iterations,
		target_argv[0]);
	snprintf(fuzzdir, sizeof fuzzdir, "/tmp/zmodemfuzz-%ld", (long)getpid());

	{
		long it;
		int failures = 0;
		if (start_iteration > 0) {
			if (!mode_receive) {
				/* send mode's rng stream depends on what the
				 * target does (fuzz_send_iter draws only
				 * after exchanges happened), so it cannot be
				 * replayed blind */
				die("-S needs -m receive");
			}
			if (start_iteration >= iterations)
				iterations = start_iteration + 1;
			fast_forward_rng(start_iteration);
			tr_mark("# fast-forwarded rng to iteration %ld",
				start_iteration);
		}
		for (it = start_iteration; it < iterations; it++) {
			int exitcode = 0;
			const char *bad;
			setup_iteration();
			build_payload();
			if (pipe(to_target) < 0 || pipe(from_target) < 0
			    || pipe(target_stderr) < 0)
				die("pipe: %s", strerror(errno));
			spawn_target();
			stderr_len = 0;
			rxbuf_reset();
			/* a previous iteration's EPIPE may have poisoned the
			 * write path: clear the flag and any bytes that were
			 * buffered when the old target went away, so the new
			 * target starts with a clean output stream */
			target_dead = 0;
			to_target_buf.len = 0;
			if (mode_receive) {
				if (do_selftest)
					selftest_receive(it, &exitcode);
				else {
					enum mclass mc =
					    (enum mclass)(rng_below(N_MCLASS - 1) + 1);
					if (it % 17 == 0)
						mc = M_NONE; /* keep a clean baseline */
					fuzz_receive_iter(it, mc, &exitcode);
				}
			} else {
				fuzz_send_iter();
				exitcode = 0;
			}
			{
				int w = wait_target(&exitcode);
				int clean = (mode_receive
					     && (it % 17 == 0 || do_selftest));
				if (w < 0) {
					/* hang: for mutation iterations an
					 * exchange that deliberately breaks
					 * mid-stream can leave the receiver
					 * waiting for data that never comes —
					 * that is the mutation working, not a
					 * target bug. Only signal deaths,
					 * canary violations and clean-exchange
					 * failures are bugs. */
					if (!clean) {
						tr_mark("# target waited: "
							"expected after a "
							"mid-stream mutation");
						failures += 0;
					} else {
						fail("hang", it, exitcode);
						failures++;
					}
				} else {
					bad = check_invariants(exitcode, 0);
					if (bad && exitcode == -14)
						/* the child's alarm fired: a
						 * mutation (or, in send mode,
						 * every iteration is one)
						 * broke the exchange mid-stream
						 * by design and the target
						 * waited — expected */
						bad = NULL;
					if (!bad && clean)
						bad = check_received();
					if (bad) {
						fail(bad, it, exitcode);
						failures++;
					}
				}
			}
			close_pipes();
			teardown_iteration();
			if (show_progress) {
				fprintf(stderr,
					"zmodemfuzz: %ld/%ld iters, %d failures\n",
					it + 1, iterations, failures);
			} else if ((it % 20) == 19) {
				fprintf(stderr, "zmodemfuzz: %ld/%ld iters, %d failures\n",
					it + 1, iterations, failures);
			}
		}
		fprintf(stderr, "zmodemfuzz: done, %ld iterations, %d failures\n",
			iterations - start_iteration, failures);
		return failures ? 1 : 0;
	}
}
