#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Config stuff for Linux2.4
#

#include $(NSPRDEPTH)/config/UNIX.mk

CC 		= gcc
CCC 		= g++

CPU_ARCH	= x86
GFX_ARCH	= x

RANLIB 		= ranlib

ifdef SERVER_BUILD
# see sun-java/config/config.mk
STATIC_JAVA = yes
endif

NEED_XMOS = 1

# fixme OS_CFLAGS = -m486 -ansi -Wall -pipe -MDupdate $(DEPENDENCIES)
OS_CFLAGS = -m486 -ansi -Wall -pipe

OS_CFLAGS += -DLINUX -DLINUX2_4 -D_POSIX_SOURCE -D_BSD_SOURCE -DHAVE_STRERROR -D_REENTRANT
OS_LIBS = -L /lib -lc -ldl -lpthread

ARCH = linux

EN_LOCALE = C
DE_LOCALE = de_DE.ISO8859-1
FR_LOCALE = fr_FR.ISO8859-1
JP_LOCALE = ja
SJIS_LOCALE = ja_JP.SJIS
KR_LOCALE = ko_KR.EUC
CN_LOCALE = zh
TW_LOCALE = zh
I2_LOCALE = i2

BUILD_UNIX_PLUGINS = 1

ifeq ($(OS_RELEASE),2.4)
OS_REL_CFLAGS         += -DLINUX2_4
MKSHLIB			= $(LD) -shared
endif

XINC = /usr/X11R6/include
INCLUDES += -I$(XINC)

BSDECHO	= echo

