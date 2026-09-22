dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.72])
AC_INIT([lrzsz],[0.13.0-beta2])
AC_CONFIG_SRCDIR([src/crctab.c])
AM_INIT_AUTOMAKE(foreign)
AC_CONFIG_HEADERS(config.h)
AC_USE_SYSTEM_EXTENSIONS

dnl Enable 64-bit file offsets on 32-bit platforms (noop if off_t is already 64-bit)
AC_SYS_LARGEFILE

dnl Policy: about 20 years. Rounded to the nearest _POSIX_C_SOURCE. Not below 15 years.
supported_c_stds="c23 c17 c11 c99"
AC_SUBST(MIN_POSIX_C_STD, "200809L")
AC_SUBST(SUPPORTED_C_STDS, "$supported_c_stds")

dnl AM_INIT_AUTOMAKE AC_DEFINEs and AC_SUBSTs PACKAGE and VERSION.
dnl Tarball name (Specfile.in); must not shadow autoconf's plain PACKAGE_VERSION.
AC_SUBST([DIST_TARBALL], ["$PACKAGE-$VERSION"])


AC_ARG_ENABLE([mkdir],
	AS_HELP_STRING([--disable-mkdir], [disable support for creating directories (lrz)]))
AS_IF([test "x$enable_mkdir" != xno],
	[AC_DEFINE([ENABLE_MKDIR], [1], [Define to 1 if lrz shall create directories if needed.])])
AC_ARG_WITH([syslog-facility],
	AS_HELP_STRING([--with-syslog-facility=FACILITY], [syslog facility (default: LOG_UUCP)]),
	[AS_CASE([$withval],
		[yes|no|""], [syslog_facility=LOG_UUCP],
		[syslog_facility=$withval])],
	[syslog_facility=LOG_UUCP])
AC_DEFINE_UNQUOTED([SYSLOG_FACILITY], [$syslog_facility],
	[Define to the syslog facility constant to use for openlog().])
have_std=
AC_ARG_ENABLE([std],
              AS_HELP_STRING([--enable-std=STD], [use a specific C std (c23 c17 c11 c99)]),
              [have_std=$enableval],
              [have_std="no"])
AC_ARG_ENABLE([asan],
              AS_HELP_STRING([--enable-asan], [enable the address sanitizer (if available)]),
              [want_asan=$enableval],
              [want_asan="no"])
AC_ARG_ENABLE([lsan],
              AS_HELP_STRING([--enable-lsan], [enable the leak sanitizer (if available)]),
              [want_lsan=$enableval],
              [want_lsan="no"])
AC_ARG_ENABLE([msan],
              AS_HELP_STRING([--enable-msan], [enable the memory sanitizer (if available)]),
              [want_msan=$enableval],
              [want_msan="no"])
AC_ARG_ENABLE([ubsan],
              AS_HELP_STRING([--enable-ubsan], [enable the undefined behavior sanitizer (if available)]),
              [want_ubsan=$enableval],
              [want_ubsan="no"])
AC_ARG_ENABLE([coverage],
              AS_HELP_STRING([--enable-coverage], [compile with coverage information]),
              [want_coverage=$enableval],
              [want_coverage="no"])


if test "x$CFLAGS" = x ; then old_cflags_empty=1; fi

dnl Checks for programs.
AC_PROG_CC
AX_COMPILER_FLAGS_CFLAGS()

for i in $supported_c_stds ; do
    if test "$have_std" = "no" ; then AX_CHECK_COMPILE_FLAG(-std=$i, [have_std=$i]) fi
done
if test "$have_std" != "no" ; then
    AX_APPEND_COMPILE_FLAGS(-std=$have_std, [LRZSZ_CFLAGS])
fi

AX_APPEND_COMPILE_FLAGS([ -g3], [LRZSZ_CFLAGS])
AX_APPEND_COMPILE_FLAGS([ -Wunused-result -Wpedantic -Wcast-qual -Wmissing-braces -Wcomments -Wformat], [WARN_CFLAGS])

dnl that one does not take #ifdefs into account. clang-only warning family.
dnl probe the positive form: gcc errors on unknown positive -W flags immediately,
dnl but silently accepts unknown -Wno-* forms and then prints a per-file
dnl "unrecognized command-line option" note on every file / translation unit
dnl that has any diagnostic.
AX_CHECK_COMPILE_FLAG([-Wpre-c23-compat],
	[AX_APPEND_COMPILE_FLAGS([ -Wno-pre-c23-compat], [WARN_CFLAGS])])

dnl -Werror is a development default; release builds may pass --disable-Werror.
AC_ARG_ENABLE([Werror],
	AS_HELP_STRING([--disable-Werror], [do not turn compiler warnings into errors]),
	[], [enable_Werror=yes])
AS_IF([test "x$enable_Werror" = xyes],
	[AX_APPEND_COMPILE_FLAGS([ -Werror ], [WARN_CFLAGS])])

if test "$want_coverage" != "no" ; then
    AX_APPEND_COMPILE_FLAGS([ --coverage ], [LRZSZ_CFLAGS])
    AX_APPEND_COMPILE_FLAGS([ --coverage], [LRZSZ_LDFLAGS])
    AX_APPEND_COMPILE_FLAGS([ -Wno-error=inline ], [LRZSZ_CFLAGS])
fi

if test "$want_asan" != "no" ; then
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=address ], [LRZSZ_CFLAGS])
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=address ], [LRZSZ_LDFLAGS])
fi
if test "$want_lsan" != "no" ; then
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=leak ], [LRZSZ_CFLAGS])
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=leak ], [LRZSZ_LDFLAGS])
fi
if test "$want_msan" != "no" ; then
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=memory ], [LRZSZ_CFLAGS])
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=memory ], [LRZSZ_LDFLAGS])
    AX_APPEND_COMPILE_FLAGS([ -U_FORTIFY_SOURCE ], [LRZSZ_CFLAGS])
fi
if test "$want_ubsan" != "no" ; then
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=undefined ], [LRZSZ_CFLAGS])
    AX_APPEND_COMPILE_FLAGS([ -fsanitize=undefined ], [LRZSZ_LDFLAGS])
fi

dnl Checks for libraries.

AC_CHECK_INCLUDES_DEFAULT

AC_CHECK_HEADERS(fcntl.h limits.h sys/ioctl.h sys/time.h unistd.h sys/times.h)
AC_CHECK_HEADERS(termios.h sys/termios.h)
AC_CHECK_HEADERS(utime.h syslog.h sys/syslog.h sys/param.h)
AC_CHECK_HEADERS(sys/select.h strings.h arpa/inet.h locale.h)

dnl Checks for typedefs, structures, and compiler characteristics.

AC_CHECK_FUNCS(sigaction getopt_long fseeko)

dnl O_NOFOLLOW is POSIX.1-2008; this is to provide fallback for ancient systems.
AH_TEMPLATE([HAVE_O_NOFOLLOW],
  [Define to 1 if your fcntl.h defines O_NOFOLLOW.])
AC_MSG_CHECKING([for O_NOFOLLOW])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <fcntl.h>
]], [[int x = O_NOFOLLOW; return x;]])],
  [AC_DEFINE([HAVE_O_NOFOLLOW], [1])
   AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])])


AC_SUBST(CFLAGS)
AC_SUBST(LRZSZ_CFLAGS)
AC_SUBST(LRZSZ_LDFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(LIBS)

dnl gettext infrastructure is vendored (m4/, po/, config.rpath); do not add
dnl AM_GNU_GETTEXT_VERSION / AM_GNU_GETTEXT_REQUIRE_VERSION here — autoreconf -fi
dnl would try to run autopoint (target version would be 0.23).
AM_GNU_GETTEXT(external)

dnl AC_DEFINE_UNQUOTED(LOCALEDIR,"$prefix/$DATADIRNAME","Description… autoheader wants this")

AC_CONFIG_FILES([Makefile man/Makefile po/Makefile.in src/Makefile Specfile])
AC_OUTPUT
