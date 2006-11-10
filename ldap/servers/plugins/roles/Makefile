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
LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/libroles
LIBDIR = $(LDAP_PLUGIN_RELDIR)
ifndef INSTDIR
INSTDIR = c:/netscape/server4/
endif

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./roles.def
endif

ROLES_OBJS = roles_plugin.o roles_cache.o
OBJS = $(addprefix $(OBJDEST)/, $(ROLES_OBJS)) 

ROLES_DLL = libroles-plugin

INCLUDES += -I../../slapd  -I../../../include
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

# DBDB this is clearly all nonsense: the libraries this thing links with should not depend on the platform.
# However, for now I make this AIX-specific change and leave the NT-specifc stuff in place (I think it came
# from the makefile I copied to make this one. After build 3, fix this.

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP +=   $(LIBSLAPD)
EXTRA_LIBS += $(NSPRLINK)  $(LIBSLAPD) $(LDAP_LIBAVL)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
ROLES_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), AIX)
LD=ld
EXTRA_LIBS += $(NSPRLINK) $(LIBSLAPD) $(LDAP_LIBAVL)
endif

ROLES=	$(addprefix $(LIBDIR)/, $(ROLES_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(ROLES)

ifeq ($(ARCH), WINNT)
$(ROLES): $(OBJS) $(ROLES_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(ROLES_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(ROLES): $(OBJS) $(ROLES_DLL_OBJ) 
	$(LINK_DLL) $(ROLES_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(ROLES_DLL_OBJ)
endif
	$(RM) $(ROLES)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

# Target to push the built binary to an installed server
#ROLES_PUSH = $(addprefix $(INSTDIR)lib/, $(notdir $(ROLES)))
#push: $(ROLES_PUSH)

#$(ROLES_PUSH): $(ROLES)
#	cp $(ROLES) $(ROLES_PUSH)
