dnl Process this file with autoconf to produce a configure script.
AC_INIT([lrzsz],[0.12.21.rc])
AC_CONFIG_SRCDIR([src/crctab.c])
AM_INIT_AUTOMAKE(foreign)
AC_CONFIG_HEADERS(config.h)
AC_USE_SYSTEM_EXTENSIONS


dnl  is in AC_PROG_CC since 2.12
AC_PREREQ([2.72])

PACKAGE_VERSION="$PACKAGE-$VERSION"
AC_DEFINE_UNQUOTED([PACKAGE], ["$PACKAGE"], [Name of package])
AC_DEFINE_UNQUOTED([VERSION], ["$VERSION"], [Version number of package])
AC_DEFINE_UNQUOTED([PACKAGE_VERSION], ["$PACKAGE_VERSION"], [The concatenation of the strings PACKAGE, "-", and VERSION.])
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)
AC_SUBST(PACKAGE_VERSION)

ALL_LINGUAS="de"

AS_CASE([$VERSION],
  [[0-9]*.[0-9]*.[0-9]*], [README_ALPHA="README-alpha"],
  [*], [README_ALPHA=])


AC_ARG_ENABLE(timesync,
	[--disable-timesync   exclude support for timesync protocol],
	[if test "$enableval" = "no" ; then 
		:
	  else
		AC_DEFINE([ENABLE_TIMESYNC], [], [Define to 1 if you want support for the timesync protocol.])
	  fi],
	[AC_DEFINE([ENABLE_TIMESYNC], [], [Define to 1 if you want support for the timesync protocol.])])
AC_SUBST(ENABLE_TIMESYNC)
AC_ARG_ENABLE(mkdir,
	[--disable-mkdir   disable support for creating directories (lrz)],
	[if test "$enableval" = "no" ; then 
		:
	  else
	  	AC_DEFINE([ENABLE_MKDIR], [], [Define to 1 if lrz shall create directories if needed.])
	  fi],
	[AC_DEFINE([ENABLE_MKDIR], [], [Define to 1 if lrz shall create directories if needed.])])
AC_ARG_ENABLE(syslog,
	[--enable-syslog=FACILITY,{force,default,optional} include syslogsupport],
	[
	  if test "$enableval" = "no" ; then 
		:
	  else
	    case "$enableval" in
	    *,*)
			level=`echo $enableval|sed -e 's/^.*,//'`
			fac=`echo $enableval|sed -e 's/,.*$//'`
			AC_DEFINE_UNQUOTED([ENABLE_SYSLOG],$fac,[Define to LOG_xxx (a syslog facility) if syslog() shall be used.])
			;;
		*)
			lookup_facility=LOG_UUCP
			level="$enableval"
			;;
		esac
	  	if test "$level" = "force" ; then
		  AC_DEFINE([ENABLE_SYSLOG_FORCE], [], [Define to 1 if syslogging shall be forced.])
	  	else 
	  	  if test "$level" = "optional" ; then
	  	    :
	  	  else
		    AC_DEFINE([ENABLE_SYSLOG_DEFAULT], [], [Define to 1 if syslogging shall be default.])
		  fi
	  	fi
	  fi],[
		AC_DEFINE([ENABLE_SYSLOG_DEFAULT], [], [Define to 1 if syslogging shall be default.])
		lookup_facility=LOG_UUCP
	  ])

if test "x$CFLAGS" = x ; then old_cflags_empty=1; fi

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CPP
if test "x$GCC" = xyes ; then
    if test "x$old_cflags_empty" = x1 ; then
        if test "x$_CFLAGS_OWG" != x  ; then
            # each environment should contain at least one helpful hack :-)
            CFLAGS="$_CFLAGS_OWG"
        fi
    fi
fi
AC_PROG_RANLIB
AC_SEARCH_LIBS([strerror],[cposix])
AC_C_CONST
AC_C_INLINE

if test `hostname` = tirka.ohse.de -a "$cross_compiling" = no ; then
    test $prefix = NONE && prefix=/usr
	if test -z "$CFLAGS" ; then 
	    case "$CC" in 
		*gcc*)
			CFLAGS="-Wall -Wstrict-prototypes -Wmissing-prototypes"
			# -Wnested_externs entfernt wegen dcgettext()
			CFLAGS="$CFLAGS -Wpointer-arith -Wcast-qual -Wcast-align"
			CFLAGS="$CFLAGS -Winline -Wwrite-strings -Wshadow -Wmissing-braces"
			CFLAGS="$CFLAGS -Wcomments -fforce-mem -fforce-addr -O2 -m486 -pipe"
			CFLAGS="$CFLAGS -malign-loops=2 -malign-jumps=2 -malign-functions=2 -g3"
			;;
		esac
	fi
fi
case "$CFLAGS" in
*-Wstrict-prototypes*)
	AC_DEFINE([STRICT_PROTOTYPES], [], [Define to 1 if you want strict ANSI prototypes.])
	;;
esac

dnl Checks for libraries.
dnl SCO needs this.
AC_CHECK_LIB(socket,syslog,,,)
dnl BeOS needs this
AC_CHECK_LIB(be,syslog,,,)
dnl slowlartis needs this
AC_CHECK_LIB(nsl,gethostbyname,,,)

AC_CHECK_INCLUDES_DEFAULT

AC_CHECK_HEADERS(fcntl.h limits.h sys/ioctl.h sys/time.h unistd.h sys/times.h)
LRZSZ_HEADERS_TERM_IO
AC_CHECK_HEADERS(termios.h sys/termios.h termio.h sys/termio.h sgtty.h)
AC_CHECK_HEADERS(sys/mman.h utime.h syslog.h sys/syslog.h sys/param.h)
AC_CHECK_HEADERS(sys/select.h strings.h arpa/inet.h locale.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
LRZSZ_TYPE_SPEED_T

AC_CHECK_HEADERS_ONCE([sys/time.h])

UO_TYPE_SOCKLEN_T
lrzsz_HEADER_SYS_SELECT
AC_STRUCT_TM

dnl suggested by autoscan, but not really needed.
dnl AC_FUNC_UTIME_NULL
AC_FUNC_MMAP
AC_FUNC_ALLOCA
AC_CHECK_TYPES([struct utimbuf], [], [], [[#include <utime.h>]])

AC_CHECK_FUNCS(gettimeofday settimeofday)
AC_CHECK_FUNCS(strchr memcpy select vprintf setlocale)
AC_CHECK_FUNCS(times rdchk utime syslog siginterrupt sigaction)
AC_REPLACE_FUNCS(mkdir mktime strerror strstr strdup strtoul strtol strpbrk)
AC_REPLACE_FUNCS(stpcpy strftime vasprintf)

AC_CHECK_FUNC([getopt_long], [],
  [AC_LIBOBJ([getopt1]) AC_LIBOBJ([getopt])])




dnl special tests

dnl this is taken from taylor uucp.
dnl Check for the SCO buggy ftime; the code can cope with the bug,
dnl though it would prefer not to, so if we're cross-configuring we
dnl accept that ftime exists.
AC_CHECK_FUNC(ftime,
[AC_MSG_CHECKING(that ftime works correctly)
AC_CACHE_VAL(lrzsz_cv_sys_ftime_ok,
[AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/timeb.h>
#include <stdio.h>
main ()
{
  struct timeb s, slast;
  int c = 0;
  ftime (&slast);
  while (c < 10)
    {
      ftime (&s);
      if (s.time < slast.time
      || (s.time == slast.time && s.millitm < slast.millitm)) {
      fprintf(stderr,"s: %ld, %ld, slast: %ld, %ld\n",
      	s.time,s.millitm, slast.time,slast.millitm);
    exit (1);
	}
      if (s.time != slast.time)
    ++c;
      slast.time = s.time;
      slast.millitm = s.millitm;
    }
  exit (0);
}
]])],[lrzsz_cv_sys_ftime_ok=yes],[lrzsz_cv_sys_ftime_ok=no],[lrzsz_cv_sys_ftime_ok=runtime])])
case $lrzsz_cv_sys_ftime_ok in
yes) AC_DEFINE([HAVE_FTIME], [], [Define to 1 if you have a reliable ftime function.]) AC_MSG_RESULT(yes) ;;
no)  AC_MSG_RESULT(no)
	AC_MSG_WARN(ftime seems to be buggy) ;;
runtime) AC_DEFINE([HAVE_FTIME], [], [Define to 1 if you have a reliable ftime function.])
	AC_MSG_RESULT(will check at run time) ;;
esac
])

dnl
AC_MSG_CHECKING([for timezone variable])
AC_CACHE_VAL(libquark_cv_var_timezone,[
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <time.h>]], [[return(int)(timezone/2);]])],[libquark_cv_var_timezone=yes],[libquark_cv_var_timezone=no])
])
if test $libquark_cv_var_timezone = yes ; then
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_TIMEZONE_VAR], [], [Define to 1 if you have the timezone variable.])
else
	AC_MSG_RESULT(no)
fi
if test "x$lookup_facility" = x ; then
	:
else
	AC_MSG_CHECKING([for $lookup_facility])
	AC_CACHE_VAL(lrzsz_cv_lookup_facility,[
		AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#else
#include <sys/syslog.h>
#endif
#ifndef $lookup_facility
#error $lookup_facility not defined
#endif
		]])], [lrzsz_cv_lookup_facility=yes], [lrzsz_cv_lookup_facility=no])
	])
	if test $lrzsz_cv_lookup_facility = yes ; then
		AC_MSG_RESULT(yes)
		AC_DEFINE_UNQUOTED([ENABLE_SYSLOG],$lookup_facility,[Define to LOG_xxx (a syslog facility) if syslog() shall be used.])
	else
		AC_DEFINE([ENABLE_SYSLOG],[LOG_USER],[Define to LOG_xxx (a syslog facility) if syslog() shall be used.])
		AC_MSG_RESULT(no)
	fi
fi


AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(LIBS)

AM_GNU_GETTEXT(external)
AM_GNU_GETTEXT_REQUIRE_VERSION([0.23])

dnl AC_DEFINE_UNQUOTED(LOCALEDIR,"$prefix/$DATADIRNAME","Description… autoheader wants this")

AC_CONFIG_FILES([Makefile lib/Makefile testsuite/Makefile \
man/Makefile po/Makefile.in src/Makefile Specfile])
AC_CONFIG_COMMANDS([default],[
test -z "$CONFIG_HEADERS" || echo timestamp > stamp-h],[])
AC_OUTPUT
