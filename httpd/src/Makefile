#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile.cpp for the Netsite Commerce and Communications servers.

MCOM_ROOT = ../../..

MODULE=httpdBinary

ifneq ($(ARCH), WINNT)
OLD_MCOM_ROOT := $(MCOM_ROOT)
MCOM_ROOT := $(shell cd $(OLD_MCOM_ROOT); pwd)
BUILDSO=mocha admin
endif

HTTPD_DIR=$(OBJDIR)
OBJDEST=$(HTTPD_DIR)/obj

include ../../nsconfig.mk

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

ifeq ($(ARCH), WINNT)
BINS=$(HTTPD_DIR)/httpd.exe
DLLS=$(HTTPD_DIR)/$(HTTPDLL_NAME).dll
HTTPD_LIB=$(HTTPD_DIR)/$(HTTPDLL_NAME).lib
NSAPI20=$(HTTPD_DIR)/ns-httpd20.dll
LOCAL_LINK_EXE = link -OUT:"$@" /STACK:0x20000,0x2000 /MAP  $(ARCH_LINK_DEBUG) $(LCFLAGS) /NOLOGO /PDB:NONE /INCREMENTAL:NO \
                  /SUBSYSTEM:windows $(EXTRA_LIBS)
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
OS_TARGETS= admin libhttpdaemon $(DLLS)
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

HTTPD_LIBS=$(FRAME) $(HTTPDAEMON)

ifeq ($(ARCH), WINNT)
ifeq ($(NO_MOCHA), 1)
MOCHA =
MCC_SERVER += -DNO_MOCHA
else
MOCHA=$(NSCP_DISTDIR)/lib/mocha.lib
endif
OSOBJS = ntmagnus.o ntnsapi.o
HTTPD_LIBS += $(SAFS) $(LIBCRYPT)
FVERSION_FLAGS+=-s$(BUILD_SECURITY)
else
OSOBJS = main.o
endif

ifneq ($(ARCH), WINNT)
OBJS=$(addprefix $(OBJDEST)/, $(OSOBJS) index.o shtml.o upload.o \
                              cookie.o nsconfig.o)
else
EXEOBJS=$(addprefix $(OBJDEST)/, password.res ntmain.o)
OBJS=$(addprefix $(OBJDEST)/, $(OSOBJS) index.o shtml.o upload.o \
                               cookie.o nsconfig.o)
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

ifdef PRODUCT_IS_DIRECTORY_SERVER
# Primitive NT link process for nshttpd.dll requires httpdaemon
  ifeq ($(ARCH), WINNT)
    DAEMONLIB=$(OBJDIR)/lib/libhttpdaemon
  else
    DAEMONLIB=
  endif
else
  DAEMONLIB=$(OBJDIR)/lib/libhttpdaemon
endif

ifeq ($(ARCH), WINNT)
# Don't define DEPLIBS for NT because standard macros like LINK_EXE
# and LINK_DLL automatically pick up DEPLIBS which we don't always wan't.
#                    $(LIBDIRMON) (temporily removed)
DEPLIBS = $(addsuffix .$(LIB_SUFFIX), $(addprefix $(OBJDIR)/lib/lib, $(LIBADMIN) $(LIBLDAPU) $(LIBACCESS) $(HTTPD_LIBS))) \
 $(addsuffix .$(LIB_SUFFIX), $(OBJDIR)/lib/libmsgdisp)
DEPLIBS += $(LIBSECURITY) \
           $(LIBNSPR) \
           $(LIBLDAP) \
           $(LIBDBM) \
		   $(ADMINUTIL_DEP)

DEPLINK = /LIBPATH:$(OBJDIR)/lib $(addsuffix .$(LIB_SUFFIX), $(addprefix lib, $(LIBADMIN) $(LIBLDAPU) $(LIBACCESS) $(HTTPD_LIBS))) \
	libmsgdisp.$(LIB_SUFFIX) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) \
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
                    $(OBJDIR)/lib/libmsgdisp) \
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
                    $(OBJDIR)/lib/libmsgdisp) \
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

$(MOCHA):
	cd $(MCOM_ROOT)/mocha; $(MAKE) export $(NSDEFS)

$(DLLS): $(OBJS) $(DEPLIBS) $(OBJDEST)/ns-httpd.res
	-@echo EXTRA_LIBS = $(EXTRA_LIBS)
	$(PURIFY) $(LINK_DLL) $(DEPLINK) $(OBJDEST)/ns-httpd.res $(OBJS) $(EXTRA_LIBS)
	cp $(HTTPD_LIB) $(NSCP_DISTDIR)/lib
	echo $(DLLS) finished

$(BINS): $(OBJS) $(EXEOBJS) $(HTTPD_LIB)
	rm -f $@
	$(PURIFY) $(LOCAL_LINK_EXE) $(EXEOBJS) $(HTTPD_LIB)
	echo $(BINS) finished

$(NSAPI20): $(HTTPD_LIB) $(OBJDIR)/lib/frame/nsapi20.o
	$(PURIFY) $(LINK_DLL) $(LIBNSPR) $(HTTPD_LIB) $(OBJDIR)/lib/frame/nsapi20.o $(EXTRA_LIBS)

ifeq ($(BSCINFO), yes)
$(BSCS): $(OBJS)
	$(BSCMAKE) $(OBJDEST)/*.sbr
endif
endif


include $(INCLUDE_DEPENDS)
