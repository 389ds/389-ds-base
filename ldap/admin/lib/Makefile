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
# GNU Makefile for Directory Server Admin DLL/SO.
#

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(LDAP_ADMOBJDIR)
LIBDIR = $(LDAP_LIBDIR)
ALIBDIR = $(LDAP_ADMLIBDIR)
BINDIR=$(LDAP_ADMIN_BIN_RELDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

SRCS	= dsalib_location.c dsalib_debug.c dsalib_updown.c dsalib_tailf.c \
	dsalib_ldif.c dsalib_db.c dsalib_conf.c dsalib_html.c \
	dsalib_filename.c dsalib_util.c dsalib_dn.c dsalib_confs.c dsalib_pw.c

OBJS = $(addprefix $(OBJDEST)/, $(subst .c,.o,$(SRCS)))

INCLUDES += -I$(LDAP_SRC)/admin/include

SECURITYLINK += -lsoftokn3

EXTRA_LIBS += $(LDAP_COMMON_LIBS) $(SECURITYLINK) $(NSPRLINK)

LIBS= $(LDAP_ADMDLLDIR)/libds_admin$(DLL_PRESUF).$(DLL_SUFFIX)
ifeq ($(ARCH), WINNT)
IMPLIB= /IMPLIB:$(LDAP_ADMLIBDIR)/libds_admin.lib
MAPFILE= /MAP:$(LDAP_ADMLIBDIR)/libds_admin.map
EXTRA_LIBS_DEP += $(LDAP_COMMON_LIBS_DEP) $(LDAP_LIBLDIF_DEP) 
#EXTRA_LIBS += $(LDAP_COMMON_LIBS) $(LDAP_LIBLDIF) $(LDAP_SDK_LIBLDAP_DLL) \
#	$(ADMINUTIL_LINK) $(SECURITYLINK) $(NSPRLINK)
endif # WINNT

ifeq ($(ARCH), Linux)
# XXXsspitzer:  we do this so that cgi's the link against libds_admin.so
# will be able to find libns-dshttpd.so at run time.  Only platforms that
# build with gcc need to do this. 
RPATHFLAG_EXTRAS+=:../..:..
endif # Linux

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP)
#EXTRA_LIBS += $(LDAP_SDK_LIBLDAP_DLL) $(SECURITYLINK) $(ADMINUTIL_LINK) $(NSPRLINK) $(DBMLINK)
LD=ld -noquiet
endif

# for Solaris, our most common unix build platform, we check for undefined symbols
# at link time so we don't catch them at run time.  To do this, we set the -z defs
# flag.  We also have to add -lc to the end because, even though ld and cc link with
# it implicitly, -z defs will throw errors if we do not link with it explicitly
ifeq ($(ARCH), SOLARIS)
LINK_DLL += -z defs
EXTRA_LIBS += -lc
endif

all:	$(LIBS) $(LDAP_ADMDLL_RELDLLS)

$(LIBS): $(OBJDEST) $(LDAP_ADMDLLDIR) $(LDAP_ADMLIBDIR) $(OBJS) $(EXTRA_LIBS_DEP)
	$(LINK_DLL) $(IMPLIB) $(MAPFILE) $(EXTRA_LIBS)

ifeq ($(ARCH), WINNT)
$(LDAP_ADMDLL_RELDLLS): $(LIBS) $(LDAP_ADMDLL_RELDIRS)
	cp $< $@

endif

veryclean: clean

clean:
	-$(RM) $(OBJS)
	-$(RM) $(LIBS)
ifeq ($(ARCH), WINNT)
	-$(RM) $(IMPLIB)
endif

$(OBJS): $(OBJDEST)/%.o: %.c 
	$(CC) -c $(NONSHARED) $(CFLAGS) $(MCC_INCLUDE) $(OFFLAG)$(OBJDEST)/$*.o $*.c
ifdef USE_LINT
	$(LINT) $(LINTCCFLAGS) $(DEFS) $(MCC_SERVER) $(INCLUDES) $(MCC_INCLUDE) $*.c > $(OBJDEST)/$*.ln 2>&1
endif
