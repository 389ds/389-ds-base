#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Config stuff for SunOS5.x
#

ifdef NS_USE_NATIVE
CC			= cc -DNS_USE_NATIVE
CCC			= CC -DNS_USE_NATIVE
ASFLAGS			+= -Wa,-P
OS_CFLAGS		= $(NOMD_OS_CFLAGS)
ifdef BUILD_OPT
OPTIMIZER   = -xcg89 -dalign -xO2
endif
else
CC			= gcc -Wall -Wno-format
CCC			= g++ -Wall -Wno-format
ASFLAGS			+= -x assembler-with-cpp
ifdef NO_MDUPDATE
OS_CFLAGS		= $(NOMD_OS_CFLAGS)
else
OS_CFLAGS		= $(NOMD_OS_CFLAGS) -MDupdate $(DEPENDENCIES)
endif
endif

RANLIB			= echo

CPU_ARCH		= sparc
GFX_ARCH		= x

MOZ_CFLAGS		= -DSVR4 -DSYSV -DNSPR -D__svr4 -D__svr4__ -DSOLARIS -DHAVE_WEAK_IO_SYMBOLS

ifeq ($(SERVER_BUILD),1)
USE_KERNEL_THREADS	= 1
endif

ifeq ($(FORCE_SW_THREADS),1)
USE_KERNEL_THREADS	= 0
endif

# Purify doesn't like -MDupdate
ifeq ($(USE_KERNEL_THREADS), 1)
ifdef NSPR20
NOMD_OS_CFLAGS		= $(MOZ_CFLAGS) -DNSPR20 -D_PR_NTHREAD -D_REENTRANT $(SOL_CFLAGS)
else
NOMD_OS_CFLAGS		= $(MOZ_CFLAGS) -DHW_THREADS -D_REENTRANT $(SOL_CFLAGS)
endif
OS_LIBS			= -lthread -lposix4 -lsocket -lnsl -ldl
else
NOMD_OS_CFLAGS		= $(MOZ_CFLAGS) -DSW_THREADS $(SOL_CFLAGS)
OS_LIBS			= -lsocket -lnsl -ldl -L/tools/ns/lib -lposix4
endif

ifeq ($(OS_RELEASE),5.3)
MOTIF			= /usr/local/Motif/opt/ICS/Motif/usr
MOTIFLIB		= $(MOTIF)/lib/libXm.a
else
MOTIF			= /usr/dt
MOTIFLIB		= -lXm
endif

INCLUDES		+= -I$(MOTIF)/include -I/usr/openwin/include

MKSHLIB			= $(LD) $(DSO_LDOPTS)
#Livewire httpdlw.so is using CC to link.
LWMKSHLIB		= $(CCC) $(DSO_LDOPTS)

HAVE_PURIFY		= 1

NOSUCHFILE		= /solaris-rm-f-sucks

LOCALE_MAP		= $(DEPTH)/cmd/xfe/intl/sunos.lm

EN_LOCALE		= en_US
DE_LOCALE		= de
FR_LOCALE		= fr
JP_LOCALE		= ja
SJIS_LOCALE		= ja_JP.SJIS
KR_LOCALE		= ko
CN_LOCALE		= zh
TW_LOCALE		= zh_TW
I2_LOCALE		= i2
IT_LOCALE		= it
SV_LOCALE		= sv
ES_LOCALE		= es
NL_LOCALE		= nl
PT_LOCALE		= pt

LOC_LIB_DIR		= /usr/openwin/lib/locale

BSDECHO			= /usr/ucb/echo

#
# These defines are for building unix plugins
#
BUILD_UNIX_PLUGINS	= 1
DSO_LDOPTS		= -G -L$(MOTIF)/lib -L/usr/openwin/lib
DSO_LDFLAGS		=
