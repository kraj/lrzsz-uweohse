/*
  zm.c - zmodem protocol handling lowlevelstuff
  Copyright (C) until 1988 Chuck Forsberg (OMEN Technology Inc)
  Copyright (C) 1996-1998,2026+ Uwe Ohse

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

unsigned int Rxtimeout = 100;		/* Tenths of seconds to wait for something */

/* Globals used by ZMODEM functions */
int Rxframeind;		/* ZBIN ZBIN32, or ZHEX type of frame received */
int Rxtype;		/* Type of header received */
char Rxhdr[4];		/* Received header */
char Txhdr[4];		/* Transmitted header */
long Txpos;		/* Transmitted file position */
int Txfcs32;		/* true means send binary frames with 32 bit FCS */
int Crc32t;		/* Display flag indicating 32 bit CRC being sent */
int Crc32;		/* Display flag indicating 32 bit CRC being received */
int Znulls;		/* Number of nulls to send at beginning of ZDATA hdr */
char Attn[ZATTNLEN+1];	/* Attention string rx sends to tx on err */

static char lastsent;	/* Last char we sent */
int turbo_escape;

static const char *frametypes[] = {
	"Carrier Lost",		/* -3 */
	"TIMEOUT",		/* -2 */
	"ERROR",		/* -1 */
	"ZRQINIT",
	"ZRINIT",
	"ZSINIT",
	"ZACK",
	"ZFILE",
	"ZSKIP",
	"ZNAK",
	"ZABORT",
	"ZFIN",
	"ZRPOS",
	"ZDATA",
	"ZEOF",
	"ZFERR",
	"ZCRC",
	"ZCHALLENGE",
	"ZCOMPL",
	"ZCAN",
	"ZFREECNT",
	"ZCOMMAND",
	"ZSTDERR"
};
#define FTOFFSET 3
#define FRMAX (sizeof(frametypes) / sizeof(frametypes[0]))

static const char *
frametype_to_string(int type)
{
	static char unknown[32];
	unsigned int idx = (unsigned int)type + FTOFFSET;
	if (idx < FRMAX)
		return frametypes[idx];
	snprintf(unknown, sizeof(unknown), "unknown frame type %x", type);
	return unknown;
}

#define badcrc _("Bad CRC")
/* static char *badcrc = "Bad CRC"; */
static inline int noxrd7 (void);
static inline int zdlread (void);
static int zdlread2 (int);
static inline int zgeth1 (void);
static void zputhex (int c, unsigned char *pos);
static inline int zgethex (void);
static int zrbhdr (char *hdr);
static int zrbhdr32 (char *hdr);
static int zrhhdr (char *hdr);
static unsigned char zsendline_tab[256];
static int zrdat32 (char *buf, int length, size_t *);
static void zsbh32 (char *hdr, int type);
static inline void zsendline_s (const char *s, size_t count);

extern int zmodem_requested;

/*
 * Read a character from the modem line with timeout.
 *  Eat parity, XON and XOFF characters.
 */
static inline int
noxrd7(void)
{
	int c;

	for (;;) {
		if ((c = READLINE_PF(Rxtimeout)) < 0)
			return c;
		switch (c &= 0x7f) {
		case XON:
		case XOFF:
			continue;
		default:
			if (Zctlesc && !(c & 0x60))
				continue;
                        return c;
		case '\r':
		case '\n':
		case ZDLE:
			return c;
		}
	}
}

static inline int
zgeth1(void)
{
	int c, n;

	if ((c = noxrd7()) < 0)
		return c;
	n = c - '0';
	if (n > 9)
		n -= ('a' - ':');
	if (n & ~0xF)
		return ERROR;
	if ((c = noxrd7()) < 0)
		return c;
	c -= '0';
	if (c > 9)
		c -= ('a' - ':');
	if (c & ~0xF)
		return ERROR;
	c += (n<<4);
	return c;
}

/* Decode two lower case hex digits into an 8 bit byte value */
static inline int
zgethex(void)
{
	int c;

	c = zgeth1();
	VPRINTF(9,("zgethex: %02X", c));
	return c;
}

/*
 * Read a byte, checking for ZMODEM escape encoding
 *  including CAN*5 which represents a quick abort
 */
static inline int
zdlread(void)
{
	int c;
	if ((c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	/* Quick check for non control characters */
	if (c & 0x60)
		return c;
	return zdlread2(c);
}
/* no, i don't like gotos. -- uwe */
static int
zdlread2(int c)
{
	goto jump_over; /* bad style */

again:
	/* Quick check for non control characters */
	if ((c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c & 0x60)
		return c;
jump_over:
	switch (c) {
	case ZDLE:
		break;
	case XON:
	case (XON|0x80):
	case XOFF:
	case (XOFF|0x80):
		goto again;
	default:
		if (Zctlesc && !(c & 0x60)) {
			goto again;
		}
		return c;
	}
again2:
	if ((c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	if (c == CAN && (c = READLINE_PF(Rxtimeout)) < 0)
		return c;
	switch (c) {
	case CAN:
		return GOTCAN;
	case ZCRCE:
	case ZCRCG:
	case ZCRCQ:
	case ZCRCW:
		return (c | GOTOR);
	case ZRUB0:
		return 0x7f;
	case ZRUB1:
		return 0xff;
	case XON:
	case (XON|0x80):
	case XOFF:
	case (XOFF|0x80):
		goto again2;
	default:
		if (Zctlesc && ! (c & 0x60)) {
			goto again2;
		}
		if ((c & 0x60) ==  0x40)
			return (c ^ 0x40);
		break;
	}
	VPRINTF(2,(_("Bad escape sequence %x"), c));
	return ERROR;
}



/*
 * Send character c with ZMODEM escape sequence encoding.
 *  Escape XON, XOFF. Escape CR following @ (Telenet net escape)
 */
static inline void 
zsendline(int c)
{

	switch(zsendline_tab[(unsigned) (c&=0xff)])
	{
        default: /* can't happen */
	case 0: 
		xsendline(lastsent = c); 
		break;
	case 1:
		xsendline(ZDLE);
		c ^= 0x40;
		xsendline(lastsent = c);
		break;
	case 2:
		if ((lastsent & 0x7f) != 0x40) {
			xsendline(lastsent = c);
		} else {
			xsendline(ZDLE);
			c ^= 0x40;
			xsendline(lastsent = c);
		}
		break;
	}
}

/* is this really still useful in 2026? */
static inline void 
zsendline_s(const char *s, size_t count) 
{
	const char *end=s+count;
	while(s!=end) {
		int last_esc=0;
		const char *t=s;
		while (t!=end) {
			last_esc=zsendline_tab[(unsigned) ((*t) & 0xff)];
			if (last_esc) 
				break;
			t++;
		}
		if (t!=s) {
			fwrite(s,(size_t)(t-s),1,stdout);
                        stat_for_file.net_bytes_out+=(unsigned long) (t-s);
			lastsent=t[-1];
			s=t;
		}
		if (last_esc) {
			int c=*s;
			switch(last_esc) {
                        default: /* can't happen */
			case 0: 
				xsendline(lastsent = c); 
				break;
			case 1:
				xsendline(ZDLE);
				c ^= 0x40;
				xsendline(lastsent = c);
				break;
			case 2:
				if ((lastsent & 0x7f) != 0x40) {
					xsendline(lastsent = c);
				} else {
					xsendline(ZDLE);
					c ^= 0x40;
					xsendline(lastsent = c);
				}
				break;
			}
			s++;
		}
	}
}


/* Send ZMODEM binary header hdr of type type */
void 
zsbhdr(int type, char *hdr)
{
	int n;
	unsigned short crc;

	VPRINTF(3,("zsbhdr: %s %lx", frametype_to_string(type), rclhdr(hdr)));
	if (type == ZDATA)
		for (n = Znulls; --n >=0; )
			xsendline(0);

	xsendline(ZPAD); xsendline(ZDLE);

	Crc32t=Txfcs32;
	if (Crc32t)
		zsbh32(hdr, type);
	else {
		xsendline(ZBIN); zsendline(type); crc = updcrc(type, 0);

		for (n=4; --n >= 0; ++hdr) {
			zsendline(*hdr);
			crc = updcrc((0xff& *hdr), crc);
		}
		crc = updcrc(0,updcrc(0,crc));
		zsendline(crc>>8);
		zsendline(crc);
	}
	if (type != ZDATA)
		flushmo();
}


/* Send ZMODEM binary header hdr of type type */
static void
zsbh32(char *hdr, int type)
{
	int n;
	unsigned long crc;

	xsendline(ZBIN32);  zsendline(type);
	crc = 0xFFFFFFFFL; crc = UPDC32(type, crc);

	for (n=4; --n >= 0; ++hdr) {
		crc = UPDC32((0xff & *hdr), crc);
		zsendline(*hdr);
	}
	crc = ~crc;
	for (n=4; --n >= 0;) {
		zsendline((int)crc);
		crc >>= 8;
	}
}

/* Send ZMODEM HEX header hdr of type type */
void 
zshhdr(int type, char *hdr)
{
	int n;
	unsigned short crc;
	unsigned char s[30];
	size_t len;

	VPRINTF(3,("zshhdr: %s %lx", frametype_to_string(type & 0x7f), rclhdr(hdr)));
	s[0]=ZPAD;
	s[1]=ZPAD;
	s[2]=ZDLE;
	s[3]=ZHEX;
	zputhex(type & 0x7f ,s+4);
	len=6;
	Crc32t = 0;

	crc = updcrc((type & 0x7f), 0);
	for (n=4; --n >= 0; ++hdr) {
		zputhex(*hdr,s+len); 
		len += 2;
		crc = updcrc((0xff & *hdr), crc);
	}
	crc = updcrc(0,updcrc(0,crc));
	zputhex(crc>>8,s+len); 
	zputhex(crc,s+len+2);
	len+=4;

	/* Make it printable on remote machine */
	s[len++]='\r';
	s[len++]='\n'|0x80;
	/*
	 * Uncork the remote in case a fake XOFF has stopped data flow
	 */
	if (type != ZFIN && type != ZACK)
	{
		s[len++]=XON;
	}
	fwrite(s, 1, len, stdout);
	flushmo();
}

/*
 * Send binary array buf of length length, with ending ZDLE sequence frameend
 */
static const char *Zendnames[] = { "ZCRCE", "ZCRCG", "ZCRCQ", "ZCRCW"};
void 
zsdata(const char *buf, size_t length, int frameend)
{
	unsigned short crc;

	VPRINTF(3,("zsdata: %lu %s", (unsigned long) length, 
		Zendnames[(frameend-ZCRCE)&3]));
	crc = 0;

	for( ; length; length--) {
	  zsendline(*buf); crc = updcrc((0xff & *buf), crc);
	  buf++;
	}

	xsendline(ZDLE); xsendline(frameend);
	crc = updcrc(frameend, crc);

	crc = updcrc(0,updcrc(0,crc));
	zsendline(crc>>8); zsendline(crc);
	if (frameend == ZCRCW) {
		xsendline(XON);  flushmo();
	}
}

void
zsda32(const char *buf, size_t length, int frameend)
{
	int c;
	unsigned long crc;
	int i;
	VPRINTF(3,("zsdat32: %lu %s", length, Zendnames[(frameend-ZCRCE)&3]));

	crc = 0xFFFFFFFFL;
	zsendline_s(buf,length);
	for (; length; length--) {
		c = *buf & 0xff;
		crc = UPDC32(c, crc);
		buf++;
	}
	xsendline(ZDLE); xsendline(frameend);
	crc = UPDC32(frameend, crc);

	crc = ~crc;
	for (i=4; --i >= 0;) {
		c=(int) crc;
		if (c & 0x60)
			xsendline(lastsent = c);
		else
			zsendline(c);
		crc >>= 8;
	}
	if (frameend == ZCRCW) {
		xsendline(XON);  flushmo();
	}
}

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ <= 4)
#  undef DEBUG_BLOCKSIZE
#endif

#ifdef DEBUG_BLOCKSIZE
struct debug_blocksize {
	int size;
	long count;
};
struct debug_blocksize blocksizes[]={
	{32,0},
	{64,0},
	{128,0},
	{256,0},
	{512,0},
	{1024,0},
	{2048,0},
	{4096,0},
	{8192,0},
	{0,0}
};
static inline void
count_blk(int size)
{
	int i;
	for (i=0;blocksizes[i].size;i++) {
		if (blocksizes[i].size==size) {
			blocksizes[i].count++;
			return;
		}
	}
	blocksizes[i].count++;
}

static void printout_blocksizes(void) __attribute__((__destructor__));
static void 
printout_blocksizes(void) 
{
	int i;
	for (i=0;blocksizes[i].size;i++) {
		if (blocksizes[i].count) {
			lsyslog(LOG_DEBUG,"%4d byte: %ld blocks\n",
				   blocksizes[i].size,blocksizes[i].count);
		}
	}
	if (blocksizes[i].count) {
		lsyslog(LOG_DEBUG,"unk. byte: %ld blocks",
			   blocksizes[i].count);
	}
}
#define COUNT_BLK(x) count_blk(x)
#else
#define COUNT_BLK(x)
#endif

/*
 * Receive array buf of max length with ending ZDLE sequence
 *  and CRC.  Returns the ending character or error code.
 *  NB: On errors may store length+1 bytes!
 */
int
zrdata(char *buf, int length, size_t *bytes_received)
{
	int c;
	unsigned short crc;
	char *end;
	int d;

	*bytes_received=0;
	if (Rxframeind == ZBIN32)
		return zrdat32(buf, length, bytes_received);

	crc = 0;  end = buf + length;
	while (buf <= end) {
		if ((c = zdlread()) & ~0xff) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				{ 
					d = c;
					c &= 0xff;
					crc = updcrc(c, crc);
					if ((c = zdlread()) & ~0xff)
						goto crcfoo;
					crc = updcrc(c, crc);
					if ((c = zdlread()) & ~0xff)
						goto crcfoo;
					crc = updcrc(c, crc);

                                        stat_for_file.total_message_bytes += length - (end - buf);
					if (crc & 0xFFFF) {
                                                stat_for_file.invalid_messages++;
						zperr(badcrc);
						return ERROR;
					}
                                        stat_for_file.valid_messages++;
					*bytes_received = length - (end - buf);
					COUNT_BLK(*bytes_received);
					VPRINTF(3,("zrdata: %lu  %s", (unsigned long) (*bytes_received), 
							Zendnames[(d-GOTCRCE)&3]));
					return d;
				}
			case GOTCAN:
				zperr(_("Sender Canceled"));
                                stat_for_file.cancels++;
				return ZCAN;
			case TIMEOUT:
				zperr(_("TIMEOUT"));
                                stat_for_file.timeouts++;
				return c;
			default:
				zperr(_("Bad data subpacket"));
                                stat_for_file.format_errors++;
				return c;
			}
		}
		if (buf == end) break;
		*buf++ = c;
		crc = updcrc(c, crc);
	}
	zperr(_("Data subpacket too long"));
        stat_for_file.format_errors++;
	return ERROR;
}

static int
zrdat32(char *buf, int length, size_t *bytes_received)
{
	int c;
	unsigned long crc;
	char *end;
	int d;

	crc = 0xFFFFFFFFL;  end = buf + length;
	while (buf <= end) {
		if ((c = zdlread()) & ~0xff) {
crcfoo:
			switch (c) {
			case GOTCRCE:
			case GOTCRCG:
			case GOTCRCQ:
			case GOTCRCW:
				d = c;
				c &= 0xff;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0xff)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0xff)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0xff)
					goto crcfoo;
				crc = UPDC32(c, crc);
				if ((c = zdlread()) & ~0xff)
					goto crcfoo;
				crc = UPDC32(c, crc);
                                stat_for_file.total_message_bytes += length - (end - buf);
				if (crc != 0xDEBB20E3) {
					zperr(badcrc);
                                        stat_for_file.invalid_messages++;
					return ERROR;
				}
                                stat_for_file.valid_messages++;
				*bytes_received = length - (end - buf);
				COUNT_BLK(*bytes_received);
				VPRINTF(3,("zrdat32: %lu %s", (unsigned long) *bytes_received, 
					Zendnames[(d-GOTCRCE)&3]));
				return d;
			case GOTCAN:
				zperr(_("Sender Canceled"));
                                stat_for_file.cancels++;
				return ZCAN;
			case TIMEOUT:
				zperr(_("TIMEOUT"));
                                stat_for_file.timeouts++;
				return c;
			default:
                                stat_for_file.format_errors++;
				zperr(_("Bad data subpacket"));
				return c;
			}
		}
		if (buf == end) break;
		*buf++ = c;
		crc = UPDC32(c, crc);
	}
	zperr(_("Data subpacket too long"));
        stat_for_file.format_errors++;
	return ERROR;
}

/*
 * Read a ZMODEM header to hdr, either binary or hex.
 *  eflag controls local display of non zmodem characters:
 *	0:  no display
 *	1:  display printing characters only
 *	2:  display all non ZMODEM characters
 *  On success, set Zmodem to 1, set Rxpos and return type of header.
 *   Otherwise return negative on error.
 *   Return ERROR instantly if ZCRCW sequence, for fast error recovery.
 */
int
zgethdr(char *hdr, int eflag, size_t *Rxpos)
{
	int c, cancount;
	unsigned int max_garbage; /* Max bytes before start of frame */
	size_t rxpos=0; /* keep gcc happy */
	int garbage_count = 0;

	max_garbage = Zrwindow + Baudrate;
	Rxframeind = Rxtype = 0;

startover:
	cancount = 5;
again:
	/* Return immediate ERROR if ZCRCW sequence seen */
	switch (c = READLINE_PF(Rxtimeout)) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case CAN:
gotcan:
		if (--cancount <= 0) {
			c = ZCAN; goto fifi;
		}
		switch (c = READLINE_PF(1)) {
		case TIMEOUT:
			goto again;
		case ZCRCW:
			c = ERROR;
	        /* FALLTHROUGH */
		case RCDO:
			goto fifi;
		default:
			break;
		case CAN:
			if (--cancount <= 0) {
				c = ZCAN; goto fifi;
			}
			goto again;
		}
	/* FALLTHROUGH */
	default:
	agn2:
		garbage_count++;
		if (Verbose > 2 && (garbage_count <= 3 || garbage_count % 1000 == 0))
			vfile("zgethdr: garbage[%d]=0x%02x, max_garbage_left=%u",
			      garbage_count, c & 0xff, max_garbage);
		if ( --max_garbage == 0) {
			if (Verbose > 2)
				vfile("zgethdr: garbage count exceeded after %d bytes",
				      garbage_count);
			zperr(_("Garbage count exceeded"));
			return(ERROR);
		}
		if (eflag && ((c &= 0x7f) & 0x60) && Verbose)
			vchar(c);
		else if (eflag > 1 && Verbose)
			vchar(c);
		goto startover;
	case ZPAD|0x80:		/* This is what we want. */
	case ZPAD:		/* This is what we want. */
		break;
	}
	cancount = 5;
splat:
	switch (c = noxrd7()) {
	case ZPAD:
		goto splat;
	case RCDO:
	case TIMEOUT:
		goto fifi;
	default:
		goto agn2;
	case ZDLE:		/* This is what we want. */
		break;
	}

	switch (c = noxrd7()) {
	case RCDO:
	case TIMEOUT:
		goto fifi;
	case ZBIN:
		Rxframeind = ZBIN;
                Crc32 = false;
		c =  zrbhdr(hdr);
                if (c==ERROR) {
                    stat_for_file.invalid_messages++;
                } else {
                    stat_for_file.valid_messages++;
                }
		break;
	case ZBIN32:
                Rxframeind = ZBIN32;
		Crc32 = true;
		c =  zrbhdr32(hdr);
                if (c==ERROR) {
                    stat_for_file.invalid_messages++;
                } else {
                    stat_for_file.valid_messages++;
                }
		break;
	case ZHEX:
		Rxframeind = ZHEX;
                Crc32 = false;
		c =  zrhhdr(hdr);
                if (c==ERROR) {
                    stat_for_file.invalid_messages++;
                } else {
                    stat_for_file.valid_messages++;
                }
		break;
	case CAN:
		goto gotcan;
	default:
                stat_for_file.format_errors++;
		goto agn2;
	}
	rxpos = hdr[ZP3] & 0xff;
	rxpos = (rxpos<<8) + (hdr[ZP2] & 0xff);
	rxpos = (rxpos<<8) + (hdr[ZP1] & 0xff);
	rxpos = (rxpos<<8) + (hdr[ZP0] & 0xff);
	if (Rxpos)
		*Rxpos = rxpos;
        stat_for_file.total_message_bytes+=4;
fifi:
	if (Verbose > 2)
		vfile("zgethdr: returning %d (%s) after %d garbage bytes, rxpos=%lx, readline_left=%d",
		      c, frametype_to_string(c),
		      garbage_count, (unsigned long)rxpos, readline_left);
        /* error accounting */
	switch (c) {
	case GOTCAN:
	case ZCAN:
            stat_for_file.cancels++;
            break;
	case TIMEOUT:
            stat_for_file.timeouts++;
            break;
        default:
            /* nothing to do */
            ;
        }

	switch (c) {
	case GOTCAN:
		c = ZCAN;
	/* FALLTHROUGH */
	case ZNAK:
	case ZCAN:
	case ERROR:
	case TIMEOUT:
	case RCDO:
	zperr(_("Got %s"), frametype_to_string(c));
	/* FALLTHROUGH */
    default:
		VPRINTF(3,("zgethdr: %s %lx", frametype_to_string(c), (unsigned long) rxpos));
	}
	return c;
}

/* Receive a binary style header (type and position) */
static int 
zrbhdr(char *hdr)
{
	int c, n;
	unsigned short crc;

	if ((c = zdlread()) & ~0xff)
		return c;
	Rxtype = c;
	crc = updcrc(c, 0);

	for (n=4; --n >= 0; ++hdr) {
		if ((c = zdlread()) & ~0xff)
			return c;
		crc = updcrc(c, crc);
		*hdr = c;
	}
	if ((c = zdlread()) & ~0xff)
		return c;
	crc = updcrc(c, crc);
	if ((c = zdlread()) & ~0xff)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		zperr(badcrc); 
		return ERROR;
	}
	protocol = ZM_ZMODEM;
	zmodem_requested=true;
	return Rxtype;
}

/* Receive a binary style header (type and position) with 32 bit FCS */
static int
zrbhdr32(char *hdr)
{
	int c, n;
	unsigned long crc;

	if ((c = zdlread()) & ~0xff)
		return c;
	Rxtype = c;
	crc = 0xFFFFFFFFL; crc = UPDC32(c, crc);

	for (n=4; --n >= 0; ++hdr) {
		if ((c = zdlread()) & ~0xff)
			return c;
		crc = UPDC32(c, crc);
		*hdr = c;
	}
	for (n=4; --n >= 0;) {
		if ((c = zdlread()) & ~0xff)
			return c;
		crc = UPDC32(c, crc);
	}
	if (crc != 0xDEBB20E3) {
		zperr(badcrc);
		return ERROR;
	}
	protocol = ZM_ZMODEM;
	zmodem_requested=true;
	return Rxtype;
}


/* Receive a hex style header (type and position) */
static int 
zrhhdr(char *hdr)
{
	int c;
	unsigned short crc;
	int n;

	if ((c = zgethex()) < 0)
		return c;
	Rxtype = c;
	crc = updcrc(c, 0);

	for (n=4; --n >= 0; ++hdr) {
		if ((c = zgethex()) < 0)
			return c;
		crc = updcrc(c, crc);
		*hdr = c;
	}
	if ((c = zgethex()) < 0)
		return c;
	crc = updcrc(c, crc);
	if ((c = zgethex()) < 0)
		return c;
	crc = updcrc(c, crc);
	if (crc & 0xFFFF) {
		zperr(badcrc); return ERROR;
	}
	switch ( c = READLINE_PF(1)) {
	case '\r'|0x80:
		/* **** FALL THRU TO **** */
	case '\r':
	 	/* Throw away possible cr/lf */
		READLINE_PF(1);
		break;
        default:
                ;
	}
	protocol = ZM_ZMODEM;
	zmodem_requested=true;
	return Rxtype;
}

/* Write a byte as two hex digits */
static void 
zputhex(int c, unsigned char *pos)
{
	static char	digits[]	= "0123456789abcdef";

	VPRINTF(9,("zputhex: %02X", c));
	pos[0]=digits[(c&0xF0)>>4];
	pos[1]=digits[c&0x0F];
}

void
zsendline_init(void)
{
	int i;
	for (i=0;i<256;i++) {	
		if (i & 0x60)
			zsendline_tab[i]=0;
		else {
			switch(i)
			{
			case ZDLE:
			case XOFF: /* ^Q */
			case XON: /* ^S */
			case (XOFF | 0x80):
			case (XON | 0x80):
				zsendline_tab[i]=1;
				break;
			case 0x10: /* ^P */
			case 0x10|0x80:
				if (turbo_escape)
					zsendline_tab[i]=0;
				else
					zsendline_tab[i]=1;
				break;
			case '\r':
			case '\r'|0x80:
				if (Zctlesc)
					zsendline_tab[i]=1;
				else if (!turbo_escape)
					zsendline_tab[i]=2;
				else 
					zsendline_tab[i]=0;
				break;
			default:
				if (Zctlesc)
					zsendline_tab[i]=1;
				else
					zsendline_tab[i]=0;
			}
		}
	}
}



/* Store pos in Txhdr */
void 
stohdr(size_t pos)
{
	unsigned long lpos=(unsigned long) pos;
	Txhdr[ZP0] = lpos;
	Txhdr[ZP1] = lpos>>8;
	Txhdr[ZP2] = lpos>>16;
	Txhdr[ZP3] = lpos>>24;
}

/* Recover a long integer from a header */
unsigned long
rclhdr(char *hdr)
{
	unsigned long l;

	l = (hdr[ZP3] & 0xff);
	l = (l << 8) | (hdr[ZP2] & 0xff);
	l = (l << 8) | (hdr[ZP1] & 0xff);
	l = (l << 8) | (hdr[ZP0] & 0xff);
	return l;
}

/* End of zm.c */
