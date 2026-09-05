bin_PROGRAMS=lrz lsz
lrz_SOURCES=lrz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c lsyslog.c canit.c stats.c util.c
lsz_SOURCES=lsz.c timing.c zperr.c zreadline.c crctab.c rbsb.c zm.c protname.c lsyslog.c canit.c stats.c util.c
noinst_HEADERS = timing.h zglobal.h zmodem.h stats.h util.h crctab.h
noinst_PROGRAMS = zmodemsnif linesimulator timestamp

LDADD = @LIBINTL@
AM_CPPFLAGS = -I.. -I$(srcdir) -I$(top_srcdir)/src -DLOCALEDIR=\"$(localedir)\" -D_FORTIFY_SOURCE=2 -D_POSIX_C_SOURCE=@MIN_POSIX_C_STD@
AM_CFLAGS = @WARN_CFLAGS@ -Werror @LRZSZ_CFLAGS@  
#-Weverything -Wno-disabled-macro-expansion -Wno-reserved-macro-identifier -Wno-used-but-marked-unused -Wno-padded -Wno-unsafe-buffer-usage -Wno-implicit-int-conversion -Wno-sign-conversion -Wno-implicit-fallthrough -Wno-unreachable-code-break -Wno-implicit-int-float-conversion -Wno-float-equal -Wno-covered-switch-default -Wno-unused-macros
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

tidy: $(SOURCES) $(HEADERS)
	clang-tidy --checks='-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' $(SOURCES) -- $(AM_CPPFLAGS) $(AM_CFLAGS)
