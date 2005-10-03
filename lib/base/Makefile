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
# The base library contains the base set of functionality for ldapserver
# servers. Cross-platform I/O and other generic functions are
# provided here.


BUILD_ROOT=../..
MODULE=LibBase

OBJDEST=$(OBJDIR)/lib/base

include $(BUILD_ROOT)/nsconfig.mk

ifeq ($(ARCH), WINNT)
LIBS=$(OBJDIR)/lib/libbase.lib
ifeq ($(BSCINFO), yes)
BSCS=$(OBJDIR)/lib/libbase.bsc
endif
else
LIBS=$(OBJDIR)/lib/libbase.a
endif

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

LOCAL_DEPS = $(NSPR_DEP) $(ADMINUTIL_DEP) $(SECURITY_DEP) $(DBM_DEP)

all: $(LOCAL_DEPS) $(OBJDEST) $(LIBS) $(BSCS)

$(OBJDEST):
	mkdir -p $(OBJDEST)

ifeq ($(ARCH), WINNT)
OSOBJS = nterrors.o eventlog.o
else
OSOBJS = 
endif

OBJS = $(addprefix $(OBJDEST)/,	\
				shexp.o \
				plist.o \
				file.o \
				util.o \
				net.o \
				fsmutex.o \
				systhr.o \
				crit.o \
				rwlock.o \
				dns.o \
				dnsdmain.o \
				lexer.o \
				pool.o \
				system.o \
				nscperror.o \
				ereport.o \
				$(OSOBJS) \
				) 


MODULE_CFLAGS=-I$(BUILD_ROOT)/include/base -I$(BUILD_ROOT)/ldap/servers/slapd

$(LIBS): $(OBJS)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

ifeq ($(ARCH), WINNT)
ifeq ($(BSCINFO), yes)
$(BSCS): $(OBJS)
	$(BSCMAKE) $(OBJDEST)/*.sbr
endif
endif

include $(INCLUDE_DEPENDS)

