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
# GNU Makefile for Directory Server
#

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/servers/obj
BINDIR = $(LDAP_SERVER_RELDIR)
ifndef INSTDIR
INSTDIR = /opt/servers/
endif

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(BUILD_ROOT)/ns_usepurify.mk
include $(BUILD_ROOT)/ns_usequantify.mk
include $(LDAP_SRC)/nsdeps.mk


ifdef HEAPAGENT
CFLAGS+=-DPURIFYING
LDAP_DONT_USE_SMARTHEAP=1
endif

ifdef USE_PURIFY
CFLAGS+=-DPURIFYING
LDAP_DONT_USE_SMARTHEAP=1
endif

ifdef USE_QUANTIFY
CFLAGS+=-DQUANTIFYING
LDAP_DONT_USE_SMARTHEAP=1
endif

# It looks like most of the latest versions of Unix that we ship on
# have a good enough heap implementations that they don't need 
# SmartHeap.  We still need it on NT and HPUX.
# Solaris 8 and later has mtmalloc
# By contract HPUX must be aligned with Solaris.
ifneq ($(ARCH), SOLARIS)
ifneq ($(ARCH), WINNT)
ifneq ($(ARCH), HPUX)
LDAP_DONT_USE_SMARTHEAP=1
endif
endif
endif

# Don't use smartheap for debug builds
ifeq ($(DEBUG), full)
LDAP_DONT_USE_SMARTHEAP=1
endif

ifndef LDAP_DONT_USE_SMARTHEAP
include $(BUILD_ROOT)/ns_usesh.mk
INCLUDES+=-I$(SH_INCLUDE)
else
CFLAGS+=-DLDAP_DONT_USE_SMARTHEAP
endif

ifndef LDAP_USE_OLD_DB
_ldap_db_depend:=$(DB_LIB_DEP)
INCLUDES+=-I$(DB_INCLUDE)
endif

INCLUDES += -I. -I$(ACLINC) -I$(BUILD_ROOT)/lib

#ICONS = $(addprefix $(LDAP_SRC)/servers/slapd/ntwdog/, logo.ico key.ico) 

REGULAR_SLAPD_OBJS=	abandon.o bind.o  \
	compare.o config.o connection.o daemon.o sasl_io.o \
	detach.o globals.o house.o init.o \
	monitor.o saslbind.o search.o strdup.o tempnam.o  \
	unbind.o extendop.o rootdse.o \
	configdse.o pw_mgmt.o auth.o \
	psearch.o conntable.o \
    stubs.o protect_db.o fileio.o lite_entries.o \
	getopt_ext.o start_tls_extop.o passwd_extop.o
FEDSE_OBJ= fedse.o
FEDSE_SRC= fedse.c
SLAPD_OBJS=	$(REGULAR_SLAPD_OBJS) $(FEDSE_OBJ)


ifneq ($(ARCH), WINNT)
SLAPD_OBJS	+= main.o
endif

ifeq ($(ARCH), WINNT)
LDAP_COMMON_EXTRALIBSLIST=libsi18n
LDAP_COMMON_EXTRALIBS = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, $(LDAP_COMMON_EXTRALIBSLIST)))

EXTRA_LIBS_DEP = \
	$(LDAP_SDK_LIBLDAP_DLL_DEP) \
	$(LDAP_SDK_LIBSSLDAP_LIB_DEP) $(LIBLDAPU_DEP) \
	$(_ldap_db_depend) $(LDAP_COMMON_EXTRALIBS)

EXTRA_LIBS += $(LIBSLAPD) $(LIBLDAPU) $(SVRCORELINK)\
	$(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) \
	$(LDAP_COMMON_EXTRALIBS)

# JCM - Warnings as Errors
CFLAGS += /WX
else
LDFLAGS	= $(SSLLIBFLAG)
EXTRA_LIBS_DEP	= $(SECURITY_DEP) $(NSPR_DEP) \
	$(LDAP_LIBLDBM_DEP) $(LDAP_LIBAVL_DEP) $(LDAP_LIBLDIF_DEP) \
	$(LDAPSDK_DEP) $(LIBLDAPU_DEP) \
	$(_ldap_db_depend) \
	$(SVRCORE_DEP)


#IRIX linker needs LIBSEC first, couldn't find away that would make both IRIX and
# solaris happy, hence the ifeq
ifeq ($(ARCH), IRIX)
# -llitekey is added; but it looks to me these two EXTRA_LIBS are identical...
EXTRA_LIBS = $(LIBSLAPD) \
	$(LIBLDAPU) $(LDAPLINK) \
	$(SECURITYLINK) $(NSPRLINK) $(LDAP_LIBLDBM) \
	$(DBMLINK) -lavl -lldif -llitekey \
	$(ALIBS) $(DYNALIBS) $(THREADSLIB) $(SVRCORELINK)
else
EXTRA_LIBS = $(LIBSLAPD) \
	$(LIBLDAPU) $(SECURITYLINK) $(LDAPLINK) \
	$(NSPRLINK) $(LDAP_LIBLDBM) \
	$(DBMLINK) -lavl -lldif -llitekey \
	$(ALIBS) $(DYNALIBS) $(THREADSLIB) \
	$(SVRCORELINK)
endif

endif

EXTRA_LIBS_DEP+=$(LIBSLAPD_DEP)

EXTRA_LIBS += $(SASL_LINK)

ifeq ($(ARCH), Linux)
EXTRA_LIBS += -lcrypt -lpthread
endif

# In order for debugging to work properly with shared libraries on HP/UX,
# we need to link with end.o.
ifeq ($(ARCH), HPUX)
# need to add arch flags :maybe
LDFLAGS+=$(ARCH_CFLAGS)
# HPUX linker voodoo
ifeq ($(DEBUG), full)
ifeq ($(USE_64), 1)
EXTRA_LIBS_TEMP:=$(EXTRA_LIBS)
EXTRA_LIBS += /opt/langtools/lib/pa20_64/end.o
else
EXTRA_LIBS_TEMP:=$(EXTRA_LIBS)
EXTRA_LIBS += /opt/langtools/lib/end.o
endif #USE_64
endif #DEBUG
# Always put libpthread at the beginning of the library list, otherwise NSPR gets upset (very)
EXTRA_LIBS_TEMP:=$(EXTRA_LIBS)
EXTRA_LIBS = -lpthread $(EXTRA_LIBS_TEMP) 
endif #HPUX

#Put SmartHeap at the beginning of the linker library list
EXTRA_LIBS_TEMP:=$(EXTRA_LIBS)
EXTRA_LIBS=$(SH_LIB) $(EXTRA_LIBS_TEMP)

ifeq ($(ARCH), SOLARIS)
ifeq ($(USE_64), 1)
LDFLAGS+= -xarch=v9
endif
endif

ifeq ($(ARCH), SOLARIS)
  SH_LIB = -lmtmalloc
endif

ifeq ($(ARCH), SOLARISx86)
  SH_LIB= -lmtmalloc
endif

OBJS = $(addprefix $(OBJDEST)/, $(SLAPD_OBJS)) 

STUB_OBJS = $(addprefix $(OBJDEST)/, stubrepl.o) 

ifeq ($(ARCH), WINNT)
MAIN_OBJ = $(addprefix $(OBJDEST)/, main.o) 
SLAPD_RES = $(addprefix $(SVRCORE_LIBPATH)/, ntsvrcore.res)
EXTRA_OBJS = $(MAIN_OBJ) $(SLAPD_RES) 
SUBSYSTEM=console
endif

ifeq ($(ARCH), WINNT)
SLAPD =		$(addprefix $(BINDIR)/, slapd.exe)
else
ifdef USE_PURIFY
SLAPD =		$(addprefix $(BINDIR)/, ns-slapd.pure)
else
ifdef USE_QUANTIFY
SLAPD =		$(addprefix $(BINDIR)/, ns-slapd.quantify)
else
SLAPD =		$(addprefix $(BINDIR)/, ns-slapd)
endif
endif
endif

ifeq ($(ARCH), AIX)
ifdef OLD_AIX_LINKING
	CCC = svxlC_r
endif

#LDFLAGS += -bloadmap:$(BINDIR)/loadmap_slapd
# setup the bmaxdata flag to use 5 segments (1.25 GB).
# This is a trade-off that allows dbcachesize to be up to about 1GB.
LDFLAGS += -bmaxdata:0x50000000
EXTRA_LIBS += -L$(OBJDIR)  $(EXE_EXTRA_LIBS) 
LINK_EXE = $(CCC) -bautoexp -brtl $(ALDFLAGS) $(LDFLAGS) \
			$(RPATHFLAG_PREFIX)$(RPATHFLAG)$(RPATHFLAG_EXTRAS) \
			-o $@ $(OBJS) $(EXTRA_LIBS)
endif

#ifeq ($(ARCH),OSF1)
#LINK_EXE = $(CXX) $(ALDFLAGS) $(LDFLAGS) \
#		$(RPATHFLAG_PREFIX)$(RPATHFLAG)$(RPATHFLAG_EXTRAS) \
#		-o $@ $(OBJS) $(EXTRA_LIBS)
#endif # OSF1

# Special rule to compile a large source file on Win32:
#	Use the /Zm option to increase internal compiler heap size.
ifeq ($(ARCH), WINNT)
$(OBJDEST)/$(FEDSE_OBJ):	$(FEDSE_SRC)
	$(CC) -c /Zm250 $(CFLAGS) $(MCC_INCLUDE) $< -Fo$(OBJDEST)/$(FEDSE_OBJ) $(CBSCFLAGS)
endif

all:	$(OBJDEST) $(BINDIR) $(BUILD_DEP) libslapd $(SLAPD) $(STUB_OBJS)

static:	$(OBJDEST) $(LIBSLAPD)

clientSDK:	static

.PHONY: libslapd push

libslapd $(LIBSLAPD_DEP):
	$(MAKE) -f libmakefile $(MFLAGS) all

#$(SLAPD_RES): $(LDAP_SRC)/libraries/libutil/ntslapd.rc \
#		$(DIRVER_H)
#	$(RSC) -fo $(SLAPD_RES) -i. -i $(OBJDIR)/include $<  

$(SLAPD):	$(SH_LIB_DEP) $(OBJS) $(MAIN_OBJ) $(SLAPD_RES) $(EXTRA_LIBS_DEP)
	$(QUANTIFY) $(PURIFY) $(PUREOPTS) $(LINK_EXE) $(EXTRA_OBJS) $(DB_LIB)

veryclean: clean

clean:
	-$(RM) $(OBJS)
	-$(RM) $(STUB_OBJS)
ifeq ($(ARCH), WINNT)
	-$(RM) $(MAIN_OBJ)
#	-$(RM) $(SLAPD_RES)
endif
	-$(RM) $(SLAPD)
	$(MAKE) -f libmakefile clean

# Target to push the built binary to an installed server
SLAPD_PUSH = $(addprefix $(INSTDIR)/, bin/slapd/server/slapd.exe)
push: $(SLAPD_PUSH)
	$(MAKE) -f libmakefile $(MFLAGS) push

$(SLAPD_PUSH): $(SLAPD)
	cp $(SLAPD) $(SLAPD_PUSH)

