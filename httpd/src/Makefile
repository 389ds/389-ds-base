#
# BEGIN COPYRIGHT BLOCK
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

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

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

DAEMONLIB=

ifeq ($(ARCH), WINNT)
# Don't define DEPLIBS for NT because standard macros like LINK_EXE
# and LINK_DLL automatically pick up DEPLIBS which we don't always wan't.
#                    $(LIBDIRMON) (temporily removed)
DEPLIBS = $(addsuffix .$(LIB_SUFFIX), $(addprefix $(OBJDIR)/lib/lib, $(LIBADMIN) $(LIBLDAPU) $(LIBACCESS) $(HTTPD_LIBS))) 
DEPLIBS += $(LIBSECURITY) \
           $(LIBNSPR) \
           $(LIBLDAP) \
           $(LIBDBM) \
	   $(ADMINUTIL_DEP)

DEPLINK = /LIBPATH:$(OBJDIR)/lib $(addsuffix .$(LIB_SUFFIX), $(addprefix lib, $(LIBADMIN) $(LIBLDAPU) $(LIBACCESS) $(HTTPD_LIBS))) \
	$(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) \
	$(NSPRLINK) $(DBMLINK)
else
# unixso.mk may define DEPLIBS
ifndef DEPLIBS
DEPLIBS = $(addsuffix .$(LIB_SUFFIX), \
                    $(addprefix $(OBJDIR)/lib/lib,$(HTTPD_LIBS)) \
                    $(DAEMONLIB) \
                    $(OBJDIR)/lib/libaccess \
                    $(OBJDIR)/lib/libbase \
                    $(OBJDIR)/lib/libsi18n \
                    $(LIBSSLIO)
DEPLIBS += $(SECURITY_DEP) \
           $(NSPR_DEP) \
	   $(ADMINUTIL_DEP)

DEPLINK = $(addsuffix .$(LIB_SUFFIX), \
                    $(addprefix $(OBJDIR)/lib/lib,$(HTTPD_LIBS)) \
                    $(DAEMONLIB) \
                    $(OBJDIR)/lib/libaccess \
                    $(OBJDIR)/lib/libbase \
                    $(OBJDIR)/lib/libsi18n \
                    $(SECURITYLINK) \
                    $(NSPRLINK) \
                    $(LIBSSLIO) \
		    $(ADMINUTILLINK)
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
endif # Netscape Directory Server
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
