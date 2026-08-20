/*
  rbsb.c - terminal handling stuff for lrzsz
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

/*
 *  Rev 05-05-1988
 *  ============== (not quite, but originated there :-). -- uwe 
 */
#include "zglobal.h"

#include <stdio.h>
#include <errno.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#else
# ifdef MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
# endif
#endif

static const struct {
	unsigned baudr;
	speed_t speedcode;
} speeds[] = {
	{110,	B110},
	{300,	B300},
	{600,	B600},
	{1200,	B1200},
	{2400,	B2400},
	{4800,	B4800},
#ifdef B7200
        {7200,  B7200},
#endif
	{9600,	B9600},
#ifdef B14400
        {14400,  B14400},
#endif
#ifdef B19200
    {19200,  B19200},
#endif
#ifdef B28800
        {28800,  B28800},
#endif
#ifdef B38400
    {38400,  B38400},
#endif
#ifdef B57600
    {57600,  B57600},
#endif
#ifdef B76800
    {76800,  B76800},
#endif
#ifdef B115200
    {115200,  B115200},
#endif
#ifdef B230400
    {230400,  B230400},
#endif
#ifdef B460800
    {460800,  B460800},
#endif

#ifdef EXTA
	{19200,	EXTA},
#endif
#ifdef EXTB
	{38400,	EXTB},
#endif
	{0, 0}
};

unsigned
getspeed(speed_t code)
{
	int n;
	for (n=0; speeds[n].baudr; ++n)
		if (speeds[n].speedcode == code)
			return speeds[n].baudr;
	return 38400;	/* Assume fifo if ioctl failed */
}

/*
 * return 1 if stdout and stderr are different devices
 *  indicating this program operating with a modem on a
 *  different line
 */
int Fromcu;		/* Were called from cu or yam */
int 
from_cu(void)
{
	struct stat a, b;
#if defined(makedev)
	dev_t help=makedev(0,0);
#else
	int help=0;
#endif

	/* in case fstat fails */
	a.st_rdev=b.st_rdev=a.st_dev=b.st_dev=help;

	fstat(1, &a); fstat(2, &b);

#if defined(major) && defined(minor)
	if (major(a.st_rdev) != major(b.st_rdev) 
		|| minor(a.st_rdev) != minor(b.st_rdev))
		Fromcu=1;
	else if (major(a.st_dev) != major(b.st_dev) 
		|| minor(a.st_dev) != minor(b.st_dev))
		Fromcu=1;
	else
		Fromcu=0;
#else
	Fromcu = (a.st_rdev != b.st_rdev) || (a.st_dev != b.st_dev);
#endif
	return Fromcu;
}



int Twostop;		/* Use two stop bits */

struct termios oldtty, tty;

/*
 * mode(n)
 *  3: save old tty stat, set raw mode with flow control
 *  2: set XON/XOFF for sb/sz with ZMODEM or YMODEM-g
 *  1: save old tty stat, set raw mode 
 *  0: restore original tty mode
 */
int 
io_mode(int fd, int n)
{
	static int did0 = FALSE;
	static int have_oldtty = FALSE;

	vfile("mode:%d", n);

	switch(n) {
	case 2:		/* Un-raw mode used by sz, sb when -g detected */
		if(!did0) {
			did0 = TRUE;
			if (tcgetattr(fd,&oldtty) == 0)
				have_oldtty = TRUE;
			else
				vfile("tcgetattr failed: %s", strerror(errno));
		}
		tty = oldtty;

		tty.c_iflag = BRKINT|IXON;

		tty.c_oflag = 0;	/* Transparent output */

		tty.c_cflag &= ~PARENB;	/* Disable parity */
		tty.c_cflag |= CS8;	/* Set character size = 8 */
		if (Twostop)
			tty.c_cflag |= CSTOPB;	/* Set two stop bits */

		tty.c_lflag = protocol==ZM_ZMODEM ? 0 : ISIG;
		tty.c_cc[VINTR] = protocol==ZM_ZMODEM ? -1 : 030;	/* Interrupt char */
#ifdef _POSIX_VDISABLE
		if (((int) _POSIX_VDISABLE)!=(-1)) {
			tty.c_cc[VQUIT] = _POSIX_VDISABLE;		/* Quit char */
		} else {
			tty.c_cc[VQUIT] = -1;			/* Quit char */
		}
#else
		tty.c_cc[VQUIT] = -1;			/* Quit char */
#endif
		tty.c_cc[VMIN] = 1; /* 1 char satisfied reads */
		tty.c_cc[VTIME] = 1; /* or in this many tenths of seconds */

		if (tcsetattr(fd,TCSADRAIN,&tty) < 0)
			vfile("tcsetattr failed: %s", strerror(errno));

		return OK;
	case 1:
	case 3:
		if(!did0) {
			did0 = TRUE;
			if (tcgetattr(fd,&oldtty) == 0)
				have_oldtty = TRUE;
			else
				vfile("tcgetattr failed: %s", strerror(errno));
		}
		tty = oldtty;

		tty.c_iflag = IGNBRK;
		if (n==3) /* with flow control */
			tty.c_iflag |= IXOFF;

		/* Setup raw mode: no echo, noncanonical (no edit chars),
		 * no signal generating chars, and no extended chars (^V, 
		 * ^O, ^R, ^W).
		 */
		tty.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
		tty.c_oflag = 0;	/* Transparent output */

		tty.c_cflag &= ~(PARENB);	/* Same baud rate, disable parity */
		/* Set character size = 8 */
		tty.c_cflag &= ~(CSIZE);
		tty.c_cflag |= CS8;	
		if (Twostop)
			tty.c_cflag |= CSTOPB;	/* Set two stop bits */
		tty.c_cc[VMIN] = 1; /* This many chars satisfies reads */
		tty.c_cc[VTIME] = 1;	/* or in this many tenths of seconds */
		if (tcsetattr(fd,TCSADRAIN,&tty) < 0)
			vfile("tcsetattr failed: %s", strerror(errno));
		Baudrate = getspeed(cfgetospeed(&tty));
		return OK;
	case 0:
		if(!did0)
			return ERROR;
		if (have_oldtty) {
			if (tcsetattr(fd,TCSANOW,&oldtty) < 0)
				lrzsz_warning(0, _("tcsetattr failed: %s"), strerror(errno));
		}
		tcflush(fd,TCIOFLUSH); /* flush input queue */
		tcflow(fd,TCOON); /* restart output */

		return OK;
	default:
		return ERROR;
	}
}

void
sendbrk(int fd)
{
	tcsendbreak(fd,0);
}

void
purgeline(int fd)
{
	readline_purge();
#ifdef TCFLSH
	ioctl(fd, TCFLSH, 0);
#else
	lseek(fd, 0L, 2);
#endif
}

/* End of rbsb.c */
