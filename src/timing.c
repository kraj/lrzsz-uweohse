/*
  timing.c - Timing routines for computing elapsed wall time
  Copyright (C) 1994 Michael D. Black
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

  originally written by Michael D. Black, mblack@csihq.com
*/

#include "zglobal.h"

#include "timing.h"

double 
timing (int reset, time_t *nowp)
{
  static double elaptime, starttime, stoptime;
  static int initialized = 0;
  double yet;
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tv;
  gettimeofday(&tv, NULL);
  yet=tv.tv_sec + tv.tv_usec/1000000.0;
#else
  yet=(double) time(NULL);
#endif
  if (nowp)
    *nowp=(time_t) yet;
  if (reset) {
    starttime = yet;
    initialized = 1;
    return starttime;
  }
  else {
    if (!initialized) {
      starttime = yet;
      initialized = 1;
    }
    stoptime = yet;
    elaptime = stoptime - starttime;
    return elaptime;
  }
}
