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
LDAP_SRC= ../../..
BUILD_ROOT= ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST= $(OBJDIR)/lib/libcollation-plugin
LIBDIR = $(LDAP_PLUGIN_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

INCLUDES+= -I../../slapd  -I../../../include 
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

COLLATION_OBJS= collate.o config.o orfilter.o

ifeq ($(ARCH), WINNT)
COLLATION_OBJS+= debug.o
COLLATION_DLL_OBJ=$(addprefix $(OBJDEST)/, dllmain.o)
DEF_FILE:=./collation.def
EXTRA_LIBS+= $(NSPRLINK) $(LDAP_SDK_LIBLDAP_DLL) $(LIBSLAPD) 
EXTRA_LIBS_DEP+= $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP+=$(LDAPSDK_DEP)
endif

# INCLUDES+= -I. -I$(ACLINC) -I$(BUILD_ROOT)/lib

# ICU stuff
INCLUDES+= $(ICU_INCLUDE)
EXTRA_LIBS+=$(ICULINK) 

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS+= $(LIBSLAPDLINK) $(NSPRLINK) $(LDAPLINK)
EXTRA_LIBS_DEP+= $(LIBSLAPD_DEP) 
EXTRA_LIBS_DEP+=$(LDAPSDK_DEP)
LD=ld
endif

OBJS= $(addprefix $(OBJDEST)/, $(COLLATION_OBJS)) 
COLLATION= $(addprefix $(LIBDIR)/, $(COLLATION_DLL).$(DLL_SUFFIX))

clientSDK: 

all: $(OBJDEST) $(LIBDIR) $(COLLATION)
ifeq (0, 1)
# Where the heck did the compiler options come from?
	@echo ARCH=$(ARCH)
	@echo DEBUG=$(DEBUG)
	@echo BUILD_OPT=$(BUILD_OPT)
	@echo CFLAGS=$(CFLAGS)
	@echo "    MCC_DEBUG="$(MCC_DEBUG)
	@echo "    PLATFORMCFLAGS="$(PLATFORMCFLAGS)
	@echo "    ACFLAGS="$(ACFLAGS)
	@echo "        EXTRACFLAGS="$(EXTRACFLAGS)
	@echo "        UNPROTOCFLAGS="$(UNPROTOCFLAGS)
	@echo "    SLCFLAGS="$(SLCFLAGS)
	@echo "ALDFLAGS="$(ALDFLAGS)
	@echo "DLL_LDFLAGS="$(DLL_LDFLAGS)
	@echo "DLL_EXPORT_FLAGS="$(DLL_EXPORT_FLAGS)
endif

ifeq ($(ARCH), WINNT)
$(COLLATION): $(OBJS) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS_DEP) $(DEF_FILE)
	$(LINK_DLL) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
ifeq ($(ARCH), AIX)
$(COLLATION): $(OBJS) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS_DEP)
	$(LINK_DLL) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS)
else
$(COLLATION): $(OBJS) $(EXTRA_LIBS_DEP)
	$(LINK_DLL) $(EXTRA_LIBS)
endif
endif

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(COLLATION_DLL_OBJ)
endif
	$(RM) $(COLLATION)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
