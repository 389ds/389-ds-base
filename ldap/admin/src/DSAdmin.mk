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
# Makefile for the DSAdmin dynamic loaded perl module

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

BINDIR=$(LDAP_ADMIN_BIN_RELDIR)
OBJDEST=$(LDAP_ADMOBJDIR)

INCLUDES += -I$(LDAP_SRC)/admin/include

DS_SERVER_DEFS = -DNS_DS

INFO = $(OBJDIR)/$(DIR)

ifneq ($(ARCH), WINNT)
EXTRALDFLAGS += $(SSLLIBFLAG)
endif

EXTRA_LIBS += $(LIBPERL_A) $(SETUPSDK_S_LINK) $(LDAP_ADMLIB) \
        $(LDAPLINK) $(DEPLINK) $(ADMINUTIL_LINK) \
        $(NSPRLINK) $(NLSLINK) \
        $(NLSLINK_CONV_STATIC)
 
# the security libs are statically linked into libds_admin.so; osf doesn't like
# to link against them again, it thinks they are multiply defined
ifneq ($(ARCH), OSF1)
EXTRA_LIBS += $(SECURITYLINK) $(DBMLINK)
else
#DLL_LDFLAGS=-shared -all -error_unresolved -taso
#EXTRA_LIBS += -lcxx -lcxxstd -lcurses -lc
endif

ifeq ($(ARCH), WINNT)
PLATFORM_INCLUDE = -I$(BUILD_ROOT)/include/nt
SUBSYSTEM=console
EXTRA_LIBS+=comctl32.lib $(LDAP_SDK_LIBLDAP_DLL) $(LDAP_LIBUTIL) 
EXTRA_LIBS_DEP+=$(LDAP_LIBUTIL_DEP)
DEF_FILE:=DSAdmin.def
LINK_DLL += /NODEFAULTLIB:msvcrtd.lib
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS += $(DLL_EXTRA_LIBS) -lbsd
LD=ld
endif

# absolutely do not try to build perl-stuff with warnings-as-errors.
# (duh.)
ifeq ($(ARCH), Linux)
CFLAGS := $(subst -Werror,,$(CFLAGS))
endif

DSADMIN_OBJS = DSAdmin.o
DSADMIN_BASENAME = DSAdmin$(DLL_PRESUFFIX).$(DLL_SUFFIX)

OBJS= $(addprefix $(OBJDEST)/, $(DSADMIN_OBJS)) 
DSADMIN_SO = $(addprefix $(BINDIR)/, $(DSADMIN_BASENAME))

EXTRA_LIBS_DEP = $(SETUPSDK_DEP)

# for Solaris, our most common unix build platform, we check for undefined
# symbols at link time so we don't catch them at run time.  To do this, we
# set the -z defs flag.  We also have to add explicitly link with the C and
# C++ runtime libraries (e.g., -lc) because, even though ld and CC link
# with them implicitly, -z defs will throw errors if we do not link with
# them explicitly.
ifeq ($(ARCH), SOLARIS)
LINK_DLL += -z defs
# removed -lcx from the following line
EXTRA_LIBS += -lCstd -lCrun -lm -lw -lc
# with the Forte 6 and later compilers, we must use CC to link
LD=CC
endif

all: $(OBJDEST) $(BINDIR) $(DSADMIN_SO)

dummy:
	-@echo PERL_EXE = $(PERL_EXE)
	-@echo PERL_EXENT = $(PERL_EXENT)
	-@echo PERL_BASEDIR = $(PERL_BASEDIR)
	-@echo PERL_ROOT = $(PERL_ROOT)
	-@echo IS_ACTIVESTATE = $(IS_ACTIVESTATE)
	-@echo PERL_CONFIG = $(PERL_CONFIG)
	-@echo PERL_ROOT = $(PERL_ROOT)
	-@echo PERL = $(PERL)
	-@echo PERL_LIB = $(PERL_LIB)
	-@echo PERL_ARCHLIB = $(PERL_ARCHLIB)
	-@echo EXTRA_LIBS_DEP = $(EXTRA_LIBS_DEP)
	abort

ifeq ($(ARCH), WINNT)
$(DSADMIN_SO): $(OBJS) $(EXTRA_LIBS_DEP) $(DEF_FILE)
	$(LINK_DLL) /DEF:$(DEF_FILE)
# linking this file causes a .exp and a .lib file to be generated which don't seem
# to be required while running, so I get rid of them
	$(RM) $(subst .dll,.exp,$@) $(subst .dll,.lib,$@)
else
$(DSADMIN_SO): $(OBJS)
	$(LINK_DLL) $(EXTRA_LIBS)
endif

$(OBJDEST)/DSAdmin.o: $(OBJDEST)/DSAdmin.c
ifeq ($(ARCH), WINNT)
	$(CC) -c $(CFLAGS) $(PERL_CFLAGS) $(MCC_INCLUDE) $(SETUPSDK_INCLUDE) $(PERL_INC) $< $(OFFLAG)$@
else
	$(CXX) $(EXCEPTIONS) -c $(CFLAGS) $(PERL_CFLAGS) $(MCC_INCLUDE) $(SETUPSDK_INCLUDE) $(PERL_INC) $< $(OFFLAG)$@
endif

$(OBJDEST)/DSAdmin.c: DSAdmin.xs
	$(PERL) -w -I$(PERL_ARCHLIB) -I$(PERL_LIB) $(XSUBPP) $(XSPROTOARG) $(XSUBPPARGS) $< > $@

#MYCMD := "Mksymlists('NAME' => 'DSAdmin', 'DLBASE' => 'DSAdmin');"
#$(DEF_FILE): DSAdmin.xs
#	$(PERL) -w "-I$(PERL_ARCHLIB)" "-I$(PERL_LIB)" -MExtUtils::Mksymlists -e "$(MYCMD)"

clean:
	-$(RM) $(OBJDEST)/DSAdmin.c $(OBJS) $(DSADMIN_SO)
