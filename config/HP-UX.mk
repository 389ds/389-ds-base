#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Config stuff for HP-UX.09.03
#

CC			= cc -Ae
CCC			= CC -Aa +a1 +eh
RANLIB			= echo

CPU_ARCH		= hppa
OS_LIBS			= -ldld -lm -ldce
OS_CFLAGS		= $(SERVER_CFLAGS) $(DSO_CFLAGS) -DHAVE_STRERROR -DHPUX -D$(CPU_ARCH) -DSW_THREADS -D_HPUX_SOURCE $(ADDITIONAL_CFLAGS)
ELIBS_CFLAGS		= -g -DHAVE_STRERROR

ifeq ($(OS_RELEASE),A.09.03)
OS_CFLAGS		+= -DHPUX9
endif

ifeq ($(OS_RELEASE),B.10.01)
OS_CFLAGS		+= -DRW_NO_OVERLOAD_SCHAR -DHPUX10
endif

ifeq ($(OS_RELEASE),B.10.10)
OS_CFLAGS		+= -DRW_NO_OVERLOAD_SCHAR -DHPUX10 -DHPUX10_10
endif

MKSHLIB			= $(LD) $(DSO_LDOPTS)
DLL_SUFFIX		= sl

LOCALE_MAP		= $(DEPTH)/cmd/xfe/intl/hpux.lm

EN_LOCALE		= american.iso88591
DE_LOCALE		= german.iso88591
FR_LOCALE		= french.iso88591
JP_LOCALE		= japanese.euc
SJIS_LOCALE		= japanese
KR_LOCALE		= korean
CN_LOCALE		= chinese-s
TW_LOCALE		= chinese-t.big5
I2_LOCALE		= i2
IT_LOCALE		= it
SV_LOCALE		= sv
ES_LOCALE		= es
NL_LOCALE		= nl
PT_LOCALE		= pt

LOC_LIB_DIR		= /usr/lib/X11

# HPUX doesn't have a BSD-style echo, so this home-brewed version will deal
# with '-n' instead.
BSDECHO			= /usr/local/bin/bsdecho

#
# These defines are for building unix plugins
#
BUILD_UNIX_PLUGINS	= 1
DSO_LDOPTS		= -b
DSO_LDFLAGS		=
DSO_CFLAGS		= +z

ifdef SERVER_BUILD
SERVER_CFLAGS		= +DA1.0 -Wl,-E,-N
STATIC_JAVA		= yes
endif
