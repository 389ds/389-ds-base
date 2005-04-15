#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# do so, delete this exception statement from your version. 
# 
# 
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
