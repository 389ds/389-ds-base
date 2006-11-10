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
# GNU Makefile for Directory Server "Chaining Backend" plugin
#

LDAP_SRC = ../../..

BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/libcb
LIBDIR = $(LDAP_PLUGIN_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libcb.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

CB_OBJS= cb_temp.o cb_init.o cb_config.o cb_instance.o cb_start.o cb_search.o cb_utils.o cb_add.o cb_delete.o cb_schema.o \
cb_acl.o cb_modify.o cb_compare.o cb_modrdn.o cb_abandon.o cb_conn_stateless.o cb_bind.o cb_unbind.o cb_monitor.o \
cb_controls.o cb_size.o cb_test.o cb_close.o cb_cleanup.o cb_debug.o

OBJS = $(addprefix $(OBJDEST)/, $(CB_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBCB_DLL_OBJ = $(addprefix $(OBJDEST)/, cbdllmain.o)
endif

LIBCB=	$(addprefix $(LIBDIR)/, $(CB_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAP_LIBUTIL_DEP) $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(SECURITY_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(LDAP_LIBUTIL) $(LDAP_COMMON_LIBS) $(SECURITYLINK) $(NSPRLINK)

endif


ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libcb.def"
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAP_LIBUTIL_DEP) $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(SECURITY_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(LIBUTIL)  $(LDAP_COMMON_LIBS)  $(SECURITYLINK) $(NSPRLINK) 
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBCB)

$(LIBCB): $(OBJS) $(LIBCB_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBCB_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBCB_DLL_OBJ)
endif
	$(RM) $(LIBCB)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(OBJS):	cb.h
