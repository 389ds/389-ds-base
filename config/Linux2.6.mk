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

