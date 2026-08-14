bin_PROGRAMS=lrz lsz
lrz_SOURCES=lrz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c lsyslog.c canit.c
lsz_SOURCES=lsz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c lsyslog.c canit.c
noinst_HEADERS = timing.h zglobal.h zmodem.h
noinst_PROGRAMS = zmodemsnif

LDADD = ../lib/libzmodem.a @LIBINTL@
AM_CPPFLAGS = -I.. -I$(srcdir) -I$(top_srcdir)/src -I$(top_srcdir)/lib -DLOCALEDIR=\"$(localedir)\" -D_FORTIFY_SOURCE=2
AM_CFLAGS = @WARN_CFLAGS@ -Werror @LRZSZ_CFLAGS@
AM_LDFLAGS = @LRZSZ_LDFLAGS@

# The hard links below point at the installed binaries, so those must exist
# first. Order it explicitly, otherwise FTBFS with make --shuffle=reverse.
install-exec-local: install-binPROGRAMS
	rm -f $(DESTDIR)/$(bindir)/`echo lsb | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lsz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lsb |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lsx | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lsz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lsx |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lrb | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lrz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lrb |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lrx | sed -e '$(transform)'`
	ln $(DESTDIR)/$(bindir)/`echo lrz |sed -e '$(transform)'` \
		$(DESTDIR)/$(bindir)/`echo lrx |sed -e '$(transform)'` 

uninstall-local:
	rm -f $(DESTDIR)/$(bindir)/`echo lsb |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lsx |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lrb |sed -e '$(transform)'` 
	rm -f $(DESTDIR)/$(bindir)/`echo lrx |sed -e '$(transform)'` 
