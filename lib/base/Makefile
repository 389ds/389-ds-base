#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# The base library contains the base set of functionality for ldapserver
# servers. Cross-platform I/O and other generic functions are
# provided here.


BUILD_ROOT=../..
MODULE=LibBase

include $(BUILD_ROOT)/nsdefs.mk

OBJDEST=$(OBJDIR)/lib/base

ifeq ($(ARCH), WINNT)
LIBS=$(OBJDIR)/lib/libbase.lib
ifeq ($(BSCINFO), yes)
BSCS=$(OBJDIR)/lib/libbase.bsc
endif
else
LIBS=$(OBJDIR)/lib/libbase.a
endif

include $(BUILD_ROOT)/nsconfig.mk

MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

LOCAL_DEPS = $(NSPR_DEP) $(ADMINUTIL_DEP) $(SECURITY_DEP) $(DBM_DEP)

all: $(LOCAL_DEPS) $(OBJDEST) $(LIBS) $(BSCS)

$(OBJDEST):
	mkdir -p $(OBJDEST)

ifeq ($(ARCH), WINNT)
OSOBJS = nterrors.o eventlog.o
else
OSOBJS = 
endif

OBJS = $(addprefix $(OBJDEST)/,	\
				shexp.o \
				plist.o \
				file.o \
				util.o \
				net.o \
				fsmutex.o \
				systhr.o \
				crit.o \
				rwlock.o \
				dns.o \
				dnsdmain.o \
				lexer.o \
				pool.o \
				system.o \
				nscperror.o \
				ereport.o \
				$(OSOBJS) \
				) 


MODULE_CFLAGS=-I$(BUILD_ROOT)/include/base -I$(BUILD_ROOT)/ldap/servers/slapd

$(LIBS): $(OBJS)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

ifeq ($(ARCH), WINNT)
ifeq ($(BSCINFO), yes)
$(BSCS): $(OBJS)
	$(BSCMAKE) $(OBJDEST)/*.sbr
endif
endif

include $(INCLUDE_DEPENDS)

