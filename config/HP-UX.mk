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
ifeq ($(OS_RELEASE),B.11.23)
SERVER_CFLAGS           = +DD32 -Wl,-E,-N
else
SERVER_CFLAGS		= +DA1.0 -Wl,-E,-N
endif
STATIC_JAVA		= yes
endif
