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
# GNU Makefile for Directory Server libback-ldbm
#
LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/libback-ldbm

ifndef INSTDIR
INSTDIR = /netscape/server4/
endif

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
ifndef LDAP_USE_OLD_DB
INCLUDES+=-I$(DB_INCLUDE)
else
CFLAGS+=-DLDAP_USE_DB185
endif
include $(LDAP_SRC)/nsdeps.mk

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

LIBBACK_LDBM_OBJS=	idl.o idl_shim.o idl_new.o idl_common.o cache.o dn2entry.o \
	id2entry.o index.o haschildren.o nextid.o init.o  \
	filterindex.o close.o backentry.o monitor.o seq.o start.o \
	rmdb.o ldif2ldbm.o dblayer.o findentry.o archive.o \
	sort.o dbsize.o dbtest.o vlv.o vlv_key.o \
	vlv_srch.o matchrule.o entrystore.o parents.o misc.o \
	upgrade.o dbversion.o cleanup.o uniqueid2entry.o \
	perfctrs.o instance.o import-threads.o import.o import-merge.o \
	ldbm_config.o ldbm_instance_config.o ldbm_index_config.o ldbm_attrcrypt_config.o \
	ldbm_attr.o \
	ldbm_abandon.o \
	ldbm_compare.o \
	ldbm_add.o \
	ldbm_search.o \
	ldbm_modify.o \
	ldbm_modrdn.o \
	ldbm_delete.o \
	ldbm_bind.o \
	ldbm_unbind.o \
	ancestorid.o \
	ldbm_attrcrypt.o \
	dbhelp.o

OBJS = $(addprefix $(OBJDEST)/, $(LIBBACK_LDBM_OBJS))

ifeq ($(ARCH), WINNT)
LIBBACK_LDBM_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

LIBBACK_LDBM=	$(addprefix $(LDAP_LIBBACK_LDBM_DLLDIR)/, $(LIBBACK_LDBM_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP +=  \
	$(LDAPSDK_DEP) \
	$(LDAP_LIBLDIF_DEP) \
	$(LDAP_LIBAVL_DEP)
EXTRA_LIBS += \
	$(NSPRLINK) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LDAP_LIBLDIF) \
	$(LDAP_LIBAVL)
CFLAGS+= /WX 
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP +=  \
	$(LDAP_SDK_LIBLDAP_DLL_DEP) \
	$(LDAP_LIBLDIF_DEP) \
	$(LDAP_LIBAVL_DEP)
EXTRA_LIBS += \
	$(NSPRLINK) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LDAP_LIBLDIF) \
	$(LDAP_LIBAVL) \
	$(DLL_EXTRA_LIBS)
LD=ld
endif

ifeq ($(ARCH), SOLARIS)
EXTRA_LIBS_DEP +=  \
	$(LDAPSDK_DEP) \
	$(LDAP_LIBLDIF_DEP) \
	$(LDAP_LIBAVL_DEP)
EXTRA_LIBS += \
	$(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) \
	$(LDAP_LIBLDIF) \
	$(LDAP_LIBAVL) \
	$(DLL_EXTRA_LIBS) -lc
# All the dependent libraries may not be linked with. (e.g., Cyrus SASL)
# LINK_DLL += -z defs
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += \
	$(LDAPSDK_DEP) \
	$(LDAP_LIBLDIF_DEP) \
	$(LDAP_LIBAVL_DEP)
EXTRA_LIBS += \
	$(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) \
	$(LDAP_LIBLDIF) \
	$(LDAP_LIBAVL) \
	$(DLL_EXTRA_LIBS) -lc
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libback-ldbm.def"
IMPLIB= /IMPLIB:$(LDAP_LIBBACK_LDBM_LIBDIR)/$(LIBBACK_LDBM_DLL).lib
MAPFILE= /MAP:$(LDAP_LIBBACK_LDBM_LIBDIR)/$(LIBBACK_LDBM_DLL).map
endif # WINNT

ifeq ($(ARCH), UnixWare)
EXTRA_LIBS_DEP += $(LDAP_LIBAVL_DEP)
EXTRA_LIBS += $(LDAP_LIBAVL)
endif # UnixWare

ifeq ($(ARCH), Linux)
EXTRA_LIBS_DEP += $(LDAP_LIBLDBM_DEP) $(LDAP_LIBAVL_DEP) $(LDAP_LIBLDIF_DEP)
EXTRA_LIBS += $(LDAP_LIBLDBM) $(LDAP_LIBAVL) $(LDAP_LIBLDIF)
EXTRA_LIBS += $(DLL_EXTRA_LIBS)
endif # Linux

EXTRA_LIBS_DEP += $(DB_LIB_DEP)

clientSDK: 

all:	$(OBJDEST) $(LDAP_LIBBACK_LDBM_LIBDIR) $(LDAP_LIBBACK_LDBM_DLLDIR) \
	$(BUILD_DEP) $(LIBBACK_LDBM)
ifeq ($(ARCH), WINNT)
	cd ntdbperfdll; $(MAKE) $(MFLAGS) all
endif

dummy:
	-@echo LDAP_LIBDIR = $(LDAP_LIB_RELDIR)
	-@echo LDAP_LIBBACK_LDBM_LIBDIR = $(LDAP_LIBBACK_LDBM_LIBDIR)
	-@echo LIB_RELDIR = $(LIB_RELDIR)
	-@echo LDAP_LIBBACK_LDBM_DLLDIR = $(LDAP_LIBBACK_LDBM_DLLDIR)
	-@echo LDAP_LIBBACK_LDBM = $(LDAP_LIBBACK_LDBM)
	-@echo LIBBACK_LDBM = $(LIBBACK_LDBM)
	abort

$(LIBBACK_LDBM): $(OBJS) $(LIBBACK_LDBM_DLL_OBJ) $(EXTRA_LIBS_DEP) $(LIBSLAPD_DEP)
	$(LINK_DLL)  $(IMPLIB) $(MAPFILE) $(LIBBACK_LDBM_DLL_OBJ) $(EXTRA_LIBS) $(DB_LIB) $(LIBSLAPD)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBBACK_LDBM_DLL_OBJ)
endif
	$(RM) $(LIBBACK_LDBM)

$(OBJDEST) $(LIBBACK_LDBM_LIBDIR):
	$(MKDIR) $@

ifeq ($(ARCH), AIX)
ifeq ($(DEBUG), optimize)

# For some reason compiling ldif2ldbm.c with the -O flag on AIX causes
# the new import code to hang.  For now we will avoid the -O flag.

TEMP_CFLAGS = $(subst -O,,$(CFLAGS))

$(OBJDEST)/ldif2ldbm.o: ldif2ldbm.c
	$(CC) -o $(OBJDEST)/ldif2ldbm.o -c $(TEMP_CFLAGS) $(MCC_INCLUDE) ldif2ldbm.c
endif
endif

# Target to push the built binary to an installed server
LDBM_PUSH = $(addprefix $(INSTDIR)/, lib/libback-ldbm.dll)
push: $(LDBM_PUSH)

$(LDBM_PUSH): $(LIBBACK_LDBM)
	cp $(LIBBACK_LDBM) $(LDBM_PUSH)

