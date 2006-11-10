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
# GNU Makefile for Directory Server NT messages DLL
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/ntmsgdll/obj
BINDIR = $(LDAP_SERVER_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

MSGFILE = slapdmessages30

MSG_SRC = ntslapdmessages

NTMSGDLL = $(addprefix $(BINDIR)/, $(MSGFILE).$(DLL_SUFFIX))

LDAP_MSG_OBJS=	$(MSG_SRC).o $(MSG_SRC).res

OBJS = $(addprefix $(OBJDEST)/, $(LDAP_MSG_OBJS)) 

clientSDK: all

all:	$(OBJDEST) $(BINDIR) $(NTMSGDLL)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(OBJDEST)/msg00001.bin
	$(RM) $(LDAP_HDIR)/$(MSG_SRC).h
	$(RM) $(OBJDEST)/$(MSG_SRC).rc
	$(RM) $(NTMSGDLL)

$(OBJDEST):
	$(MKDIR) $@

$(NTMSGDLL): $(OBJS) $(LDAP_HDIR)/$(MSG_SRC).h
	$(LINK_DLL)

$(LDAP_HDIR)/$(MSG_SRC).h: $(MSG_SRC).mc

$(OBJDEST)/msg0001.bin: $(MSG_SRC).mc

$(OBJDEST)/$(MSG_SRC).rc: $(MSG_SRC).mc 
	$(MC) $< -h $(LDAP_HDIR) -r $(OBJDEST)

$(OBJDEST)/$(MSG_SRC).res: $(OBJDEST)/$(MSG_SRC).rc
	$(RSC) -fo $(OBJDEST)/$(MSG_SRC).res $< 
