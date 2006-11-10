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

BUILD_ROOT = ../../../..
LDAP_SRC = ../../..

OBJDEST = $(OBJDIR)/servers/obj
BINDIR = $(LDAP_SERVER_RELDIR)
ALLDIRS = $(BINDIR) $(OBJDEST)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

LINK32=link.exe

MC=mc	#message compiler?

DLLS = nsldapctr

# -D's get all screwed up: we need to fix them:
CFLAGS+= -UNS_PERSONAL
# this lets us pickup regparms.h
CFLAGS += -I $(BUILD_ROOT)/include

MCS = nsldapctrmc
DEPMC = $(addsuffix .h, $(MCS))

OBJS  = $(OBJDEST)/nsldapctr.o $(OBJDEST)/nsldapctrutil.o

RELFILES = nsldapctrs.ini nsldapctrdef.h

DESTRELFILES = $(addprefix $(LDAP_INSTALL_BIN_RELDIR)/, $(RELFILES))

all: $(ALLDIRS) $(addprefix $(BINDIR)/, $(addsuffix .dll, $(DLLS) ) ) $(DESTRELFILES)

nsldapctrmc.h: 
	$(MC) -s -v $*.mc

$(OBJDEST)/%.o: %.cpp
	cl -nologo -c $(CFLAGS) $(MCC_INCLUDE) /Tp $< -Fo$@

$(BINDIR)/nsldapctr.dll: $(DEPMC) $(OBJS)
	$(LINK_DLL) /OUT:$@ /DEF:exports.def

$(LDAP_INSTALL_BIN_RELDIR)/%: % $(LDAP_INSTALL_BIN_RELDIR)
	$(CP) $< $(dir $@)
