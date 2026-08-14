noinst_LIBRARIES=libzmodem.a
AM_CFLAGS = @WARN_CFLAGS@ @LRZSZ_CFLAGS@
AM_LDFLAGS = @LRZSZ_LDFLAGS@

EXTRA_DIST = alloca.c \
	stpcpy.c \
	vasprintf.c

libzmodem_a_SOURCES = long-options.c xstrtol.c xstrtoul.c error.c 
libzmodem_a_LIBADD  = @LIBOBJS@ @ALLOCA@
libzmodem_a_DEPENDENCIES  = @ALLOCA@ @LIBOBJS@

noinst_HEADERS = long-options.h xstrtol.h xstrtoul.h error.h

