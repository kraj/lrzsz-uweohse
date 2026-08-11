dnl Process this file with autoconf to produce a configure script.
AC_INIT([lrzsz],[0.13.0-alpha])
AC_CONFIG_SRCDIR([src/crctab.c])
AM_INIT_AUTOMAKE(foreign)
AC_CONFIG_HEADERS(config.h)
AC_USE_SYSTEM_EXTENSIONS


AC_PREREQ([2.72])

PACKAGE_VERSION="$PACKAGE-$VERSION"
AC_DEFINE_UNQUOTED([PACKAGE], ["$PACKAGE"], [Name of package])
AC_DEFINE_UNQUOTED([VERSION], ["$VERSION"], [Version number of package])
#AC_DEFINE_UNQUOTED([PACKAGE_VERSION], ["$PACKAGE_VERSION"], [The concatenation of the strings PACKAGE, "-", and VERSION.])
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)
#AC_SUBST(PACKAGE_VERSION)

ALL_LINGUAS="de"

AS_CASE([$VERSION],
  [[0-9]*.[0-9]*.[0-9]*], [README_ALPHA="README-alpha"],
  [*], [README_ALPHA=])


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
AC_PROG_RANLIB
AC_SEARCH_LIBS([strerror],[cposix])
AC_C_CONST
AC_C_INLINE
AX_COMPILER_FLAGS_CFLAGS()
AX_APPEND_COMPILE_FLAGS([ -Wunused-result -Wpendantic -Wcast-qual -Wmissing-braces -Wcomments], [WARN_CFLAGS])
AX_APPEND_COMPILE_FLAGS([ -std=c11 -fforce-addr -falign-loops=2 -falign-jumps=2 -falign-functions=2 -g3], [LRZSZ_CFLAGS])

echo "T0: $WARN_CFLAGS"
echo "T1: $LRZSZ_CFLAGS"


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

AC_CHECK_FUNCS(gettimeofday sigaction nogetopt_long)
AC_REPLACE_FUNCS(stpcpy vasprintf)

dnl O_NOFOLLOW is POSIX.1-2008; this is to providea fallback for ancient systems.
AH_TEMPLATE([HAVE_O_NOFOLLOW],
  [Define to 1 if your fcntl.h defines O_NOFOLLOW.])
AC_MSG_CHECKING([for O_NOFOLLOW])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <fcntl.h>
]], [[int x = O_NOFOLLOW; return x;]])],
  [AC_DEFINE([HAVE_O_NOFOLLOW], [1])
   AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])])


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
AC_SUBST(LRZSZ_CFLAGS)
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
