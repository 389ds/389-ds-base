ASAN_CFLAGS = @asan_cflags@
DEBUG_DEFINES = @debug_defs@
DEBUG_CFLAGS = @debug_cflags@

AM_CPPFLAGS = $(DEBUG_DEFINES)
AM_CFLAGS = $(DEBUG_CFLAGS) $(ASAN_CFLAGS)

EXTRA_DIST = key.ico \
	     logo.ico \
	     manifest.mn \
	     Makefile.win \
	     ntgetpin.rc

lib_LTLIBRARIES = libsvrcore.la
include_HEADERS = svrcore.h
libsvrcore_la_SOURCES =	alt.c \
		      	cache.c \
		      	errors.c \
		      	file.c \
			ntgetpin.c \
			ntresource.h \
			pin.c \
			pk11.c \
			std.c \
			systemd-ask-pass.c \
			std-systemd.c \
			user.c

libsvrcore_la_LDFLAGS = -version-info 0:0:0 @NSS_LIBS@ @NSPR_LIBS@
libsvrcore_la_CFLAGS = @NSS_CFLAGS@ @NSPR_CFLAGS@ @SYSTEMD_CFLAGS@
