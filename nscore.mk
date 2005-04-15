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
# Shared rules for modules depending on nscore

# AIX uname
AIXOS_VERSION := $(shell uname -v)
AIXOS_RELEASE := $(shell uname -r)
AIXOS = $(AIXOS_VERSION).$(AIXOS_RELEASE)

#
# Compiler
#
# Windows NT
ifeq ($(ARCH), WINNT)
  CFLAGS += -DOS_windows -DNT 
  PROCESSOR := $(shell uname -p)
  ifeq ($(PROCESSOR), I386)
    CFLAGS += -DCPU_x86
  else
  ifeq ($(PROCESSOR), ALPHA)
    CFLAGS += -DCPU_alpha
  else
  ifeq ($(PROCESSOR), MIPS)
    CFLAGS += -DCPU_mips
  endif
  endif
  endif
endif
# Solaris
ifeq ($(ARCH), SOLARIS)
  CFLAGS += -DCPU_sparc -DOS_solaris
endif
#Solarisx86
ifeq ($(ARCH), SOLARISx86)
  CFLAGS += -DOS_solaris
endif

# HP-UX
ifeq ($(ARCH), HPUX)
  CFLAGS += -DCPU_hppa -DOS_hpux
  CFLAGS += -D_NO_THREADS_
endif
# AIX
ifeq ($(ARCH), AIX)
  CFLAGS += -DCPU_powerpc -DOS_aix
endif
# IRIX
ifeq ($(ARCH), IRIX)
  CFLAGS += -DCPU_mips -DOS_irix
  CFLAGS += -D_NO_THREADS_
endif
# OSF1
ifeq ($(ARCH), OSF1)
  CFLAGS += -DCPU_alpha -DOS_osf1
endif
ifeq ($(ARCH), UNIXWARE)
  CFLAGS += -DSYSV -DSVR4 -DCPU_i486 -DOS_unixware
endif
ifeq ($(ARCH), UnixWare)
  CFLAGS += -DSYSV -DCPU_i486 -DOS_unixware
  SYSV_REL := $(shell uname -r)
ifeq ($(SYSV_REL), 5)
  CFLAGS += -DSVR5
else
  CFLAGS += -DSVR4
endif
endif
ifeq ($(ARCH), SCO)
  CFLAGS += -DSYSV -DCPU_i486 -DOS_sco
endif
ifeq ($(ARCH), NCR)
  CFLAGS += -DSYSV -DSVR4 -DCPU_i486 -DOS_ncr
endif
