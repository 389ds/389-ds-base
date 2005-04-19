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
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Config stuff for WINNT 5.0
#

CC=cl
CCC=cl
LINK = link
RANLIB = echo
BSDECHO = echo

OTHER_DIRT = 
GARBAGE = vc20.pdb

ifdef DEBUG_RUNTIME
RTLIBFLAGS:=-MDd
else
RTLIBFLAGS:=-MD
endif

PROCESSOR := $(shell uname -p)
USE_KERNEL_THREADS=1
_PR_USECPU=1
ifeq ($(PROCESSOR), I386)
CPU_ARCH = x386
OS_CFLAGS = $(OPTIMIZER) -GT $(RTLIBFLAGS) -W3 -nologo -D_X86_ -Dx386 -D_WINDOWS -DWIN32 -DHW_THREADS
else 
ifeq ($(PROCESSOR), MIPS)
CPU_ARCH = MIPS
#OS_CFLAGS = $(OPTIMIZER) $(RTLIBFLAGS) -W3 -nologo -D_MIPS_ -D_WINDOWS -DWIN32 -DHW_THREADS
OS_CFLAGS = $(OPTIMIZER) $(RTLIBFLAGS) -W3 -nologo -D_WINDOWS -DWIN32 -DHW_THREADS
else 
ifeq ($(PROCESSOR), ALPHA)
CPU_ARCH = ALPHA
OS_CFLAGS = $(OPTIMIZER) $(RTLIBFLAGS) -W3 -nologo -D_ALPHA_=1 -D_WINDOWS -DWIN32 -DHW_THREADS
else 
CPU_ARCH = processor_is_undefined
endif
endif
endif

ifeq ($(SERVER_BUILD), 1)
OS_CFLAGS += -DSERVER_BUILD
endif

OS_DLLFLAGS = -nologo -DLL -SUBSYSTEM:WINDOWS -MAP -PDB:NONE
OS_LFLAGS = -nologo -PDB:NONE -INCREMENT:NO -SUBSYSTEM:console
OS_LIBS = kernel32.lib user32.lib gdi32.lib winmm.lib wsock32.lib advapi32.lib

OS_DEFS= SERVER_BUILD=$(SERVER_BUILD) NSPR_VERSION=$(VERSION) NS_PRODUCT=$(NS_PRODUCT)
