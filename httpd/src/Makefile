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
# Makefile.cpp for the Netsite Commerce and Communications servers.

BUILD_ROOT = ../..

MODULE=httpdBinary

ifneq ($(ARCH), WINNT)
OLD_BUILD_ROOT := $(BUILD_ROOT)
BUILD_ROOT := $(shell cd $(OLD_BUILD_ROOT); pwd)
BUILDSO=admin
endif

HTTPD_DIR=$(OBJDIR)
OBJDEST=$(HTTPD_DIR)/obj

include $(BUILD_ROOT)/nsconfig.mk

ifeq ($(ARCH), WINNT)
DLLS=$(HTTPD_DIR)/$(HTTPDLL_NAME).dll
HTTPD_LIB=$(HTTPD_DIR)/$(HTTPDLL_NAME).lib
EXTRA_LIBS+=$(NSPRLINK)

ifeq ($(BSCINFO), yes)
BSCS=$(HTTPD_DIR)/httpd.bsc
endif

else
BINS=$(HTTPD_DIR)/ns-httpd
endif

ifeq ($(ARCH), SCO)
EXTRA_OPTS := -Wl,-Bexport
endif

ifeq ($(ARCH), UNIXWARE)
EXTRA_OPTS := -W l,-Bexport
endif

ifeq ($(ARCH), NCR)
EXTRA_OPTS := -Wl,-Bexport
endif

ifeq ($(ARCH), SUNOS4)
EXTRA_LIBS=-nostdlib /usr/lib/libc.a
endif

ifeq ($(ARCH), AIX)
EXTRA_OPTS = -blibpath:../../bin/https:$(DEF_LIBPATH)
endif

ifeq ($(ARCH), SOLARIS)
EXTRA_LIBS=$(GCCLIBS)
endif

ifeq ($(ARCH), WINNT)
OS_TARGETS= admin $(DLLS)
endif

all: $(OBJDEST) $(OS_TARGETS) $(BINS) $(BSCS)

include unixso.mk

ifeq ($(ARCH), HPUX)
comma:=,
#EXTRA_LIBS +=/opt/CC/lib/cxxshl.o
EXTRA_LIBS += $(NSCP_DISTDIR)/lib/libprstrms.$(LIB_SUFFIX)
EXTRA_LIBS := $(subst -E,-E$(comma)+s,$(EXTRA_LIBS))
EXTRA_OPTS := -Wl,+s,-E
#The extra library below is required to debug shared libraries.
ifeq ($(BUILD_DEBUG), full)
EXTRA_LIBS += /opt/langtools/lib/end.o
endif
# THIS IS A TEMPORARY SOLUTION TO THE HPUX COMPILER LIMITATION.  WE
# NEED TO INCLUDE ALL OF THE eh/LIBC object files into our main executable
# to avoid having undefined symbols with plugins.
LIBCARCHIVE = /opt/CC/lib/eh/libC.a
endif

$(OBJDEST):
	mkdir -p $(OBJDEST)

HTTPD_LIBS=

ifeq ($(ARCH), WINNT)
OSOBJS = ntnsapi.o
FVERSION_FLAGS+=-s$(BUILD_SECURITY)
else
OSOBJS =
endif

ifneq ($(ARCH), WINNT)
OBJS=
else
OBJS=$(addprefix $(OBJDEST)/, $(OSOBJS) )
endif

MODULE_CFLAGS=

# Unixware linker braindamage.  The libnspr.so won't override select
# at link time
ifeq ($(ARCH), UNIXWARE)
OBJS+=$(HTTPD_DIR)/obj/uxwrap.o
endif

ifeq ($(ARCH), NCR)
#OBJS+=$(HTTPD_DIR)/httpd-lib/nspr20/uxwrap.o
endif

ifeq ($(ARCH), WINNT)
# Don't define DEPLIBS for NT because standard macros like LINK_EXE
# and LINK_DLL automatically pick up DEPLIBS which we don't always wan't.
#                    $(LIBDIRMON) (temporily removed)
DEPLIBS = $(addsuffix .$(LIB_SUFFIX), $(addprefix $(OBJDIR)/lib/lib, $(LIBADMIN) $(LIBLDAPU) $(LIBACCESS) $(HTTPD_LIBS))) 
DEPLIBS += $(LIBSECURITY) \
           $(LIBNSPR) \
           $(LIBLDAP)

DEPLINK = /LIBPATH:$(OBJDIR)/lib $(addsuffix .$(LIB_SUFFIX), $(addprefix lib, $(LIBADMIN) $(LIBLDAPU) $(LIBACCESS) $(HTTPD_LIBS))) \
	$(LDAPLINK) $(SECURITYLINK) \
	$(NSPRLINK)
else
# unixso.mk may define DEPLIBS
ifndef DEPLIBS
DEPLIBS = $(addsuffix .$(LIB_SUFFIX), \
                    $(addprefix $(OBJDIR)/lib/lib,$(HTTPD_LIBS)) \
                    $(OBJDIR)/lib/libaccess \
                    $(OBJDIR)/lib/libbase \
                    $(OBJDIR)/lib/libsi18n \
                    $(LIBSSLIO)
DEPLIBS += $(SECURITY_DEP) \
           $(NSPR_DEP)

DEPLINK = $(addsuffix .$(LIB_SUFFIX), \
                    $(addprefix $(OBJDIR)/lib/lib,$(HTTPD_LIBS)) \
                    $(OBJDIR)/lib/libaccess \
                    $(OBJDIR)/lib/libbase \
                    $(OBJDIR)/lib/libsi18n \
                    $(SECURITYLINK) \
                    $(NSPRLINK) \
                    $(LIBSSLIO)
endif

endif

ifndef PRODUCT_IS_DIRECTORY_SERVER
ifeq ($(ARCH), AIX)
ifdef OLD_AIX_LINKING
# why is this redefined here?
CCC = svxlC_r
endif
endif
endif
ifneq ($(ARCH), WINNT)
$(BINS): $(DEPLIBS) $(OBJS)
ifndef PRODUCT_IS_DIRECTORY_SERVER
	echo "why are we doing this for directory server?"
	cd $(HTTPD_DIR); $(PURIFY) $(CCC) -o ns-httpd $(OBJS) \
            $(EXTRA_OPTS) $(DEPLINK) $(EXTRA_LIBS)
endif # Directory Server
else # WINNT

$(DLLS): $(OBJS) $(DEPLIBS)
	-@echo EXTRA_LIBS = $(EXTRA_LIBS)
	$(PURIFY) $(LINK_DLL) $(DEPLINK) $(OBJS) $(EXTRA_LIBS)
	cp $(HTTPD_LIB) $(NSCP_DISTDIR)/lib
	echo $(DLLS) finished

$(BINS): $(OBJS) $(EXEOBJS) $(HTTPD_LIB)
	rm -f $@
	echo $(BINS) finished

ifeq ($(BSCINFO), yes)
$(BSCS): $(OBJS)
	$(BSCMAKE) $(OBJDEST)/*.sbr
endif
endif


include $(INCLUDE_DEPENDS)
