# --- BEGIN COPYRIGHT BLOCK ---
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
# --- END COPYRIGHT BLOCK ---
# 
#
# GNU Makefile for Directory Server distribution plugin
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/libdistrib
LIBDIR = $(LDAP_PLUGIN_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libdistrib.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

DIS_OBJS= \
	distrib.o


OBJS = $(addprefix $(OBJDEST)/, $(DIS_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBDIS_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

# The sample distribution plugin is not part of DS.
# So we generate the shared library outside of $(LIBDIR)
# so that it's not retreived by the packaging makefiles.
#LIBDIS =	$(addprefix $(LIBDIR)/, $(DIS_DLL).$(DLL_SUFFIX))
LIBDIS =	$(addprefix $(OBJDEST)/, $(DIS_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += \
	$(LIBSLAPD_DEP) \
	$(LDAP_LIBUTIL_DEP) \
	$(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += \
	$(LDAPSDK_DEP) \
	$(SECURITY_DEP)
EXTRA_LIBS += \
	$(LIBSLAPD) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LIBUTIL) \
	$(NSPRLINK) \
	$(LDAP_COMMON_LIBS)
endif
ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += \
	$(LIBSLAPD_DEP) \
	$(LDAP_LIBUTIL_DEP) \
	$(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += \
	$(LDAPSDK_DEP)
EXTRA_LIBS += \
	$(LIBSLAPDLINK) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LIBUTIL) \
	$(NSPRLINK) \
	$(LDAP_COMMON_LIBS)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LDAPLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libdistrib.def"
CFLAGS+= /WX 
endif # WINNT

ifeq ($(ARCH), AIX)
LD=ld
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBDIS)

$(LIBDIS): $(OBJS) $(LIBDIS_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBDIS_DLL_OBJ) $(EXTRA_LIBS) 

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBDIS_DLL_OBJ)
endif
	$(RM) $(LIBDIS)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
