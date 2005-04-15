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
# GNU Makefile for LDAP Back-ldif backend
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libback-ldif
LIBDIR = $(LDAP_LIBDIR)
SERVER_OBJDEST = $(OBJDIR)/servers/obj

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

BACKLDIF_OBJS	= close.o delete.o modrdn.o unbind.o add.o \
	compare.o init.o search.o bind.o config.o modify.o monitor.o \
	start.o

OBJS = $(addprefix $(OBJDEST)/, $(BACKLDIF_OBJS)) 

SERVER_OBJS= ch_malloc.o entry.o result.o modutil.o

EXTRA_OBJS = $(addprefix $(SERVER_OBJDEST)/, $(SERVER_OBJS))

INCLUDES += -I..

ifeq ($(ARCH), WINNT)
BACKLDIF_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

LDAP_BACKLDIF=	$(addprefix $(LIBDIR)/, $(LIBBACK_LDIF_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSECURITY) $(LIBNSPR) \
	$(LDAP_COMMON_LIBS_DEP) $(LDAP_SDK_LIBS_DEP) \
	$(LIBSLAPD_DEP) $(LIBLDAPU_DEP)

EXTRA_LIBS += $(LIBSECURITY) $(LIBNSPR) \
	$(LDAP_COMMON_LIBS) $(LDAP_SDK_LIBS) \
	$(LIBSLAPD) $(THREADSLIB) $(LIBLDAPU)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS += $(DLL_EXTRA_LIBS)
endif

clientSDK:

all:	$(OBJDEST) $(LIBDIR) $(LDAP_BACKLDIF)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)

$(LDAP_BACKLDIF): $(OBJS) $(BACKLDIF_DLL_OBJ)
	$(LINK_DLL) $(BACKLDIF_DLL_OBJ) $(EXTRA_LIBS)

$(SERVER_OBJDEST)/ch_malloc.o: ../ch_malloc.c
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< $(OFFLAG)$*.o

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(BACKLDIF_DLL_OBJ)
endif
	$(RM) $(LDAP_BACKLDIF)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

