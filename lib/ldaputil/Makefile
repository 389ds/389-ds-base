#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Makefile for libldapu.a (ldaputil library)
#
BUILD_ROOT=../..
MODULE=LibLdapUtil

OBJDEST=$(OBJDIR)/lib/ldaputil

include $(BUILD_ROOT)/nsconfig.mk

ifeq ($(ARCH), WINNT)
LIBS=$(OBJDIR)/lib/libldapu.lib
MODULE_CFLAGS+= -DWINSOCK
else
LIBS=$(OBJDIR)/lib/libldapu.a
endif

LOCAL_DEPS = $(LDAPSDK_DEP)

MCC_INCLUDE=-I$(BUILD_ROOT)/include \
			 $(DBM_INCLUDE) $(LDAPSDK_INCLUDE) \
			 $(SECURITY_INCLUDE) $(NSPR_INCLUDE) 

all: $(OBJDEST) $(LOCAL_DEPS) $(LIBS)

$(OBJDEST):
	mkdir -p $(OBJDEST)

OSOBJS = 

OBJS=$(addprefix $(OBJDEST)/, \
	ldapauth.o \
	ldapdb.o \
	dbconf.o \
	certmap.o \
	cert.o \
	init.o \
	encode.o \
	errors.o \
	vtable.o \
	)

MODULE_CFLAGS+= $(TESTFLAGS)
MODULE_CFLAGS+= -DLDAPDB_THREAD_SAFE -I.

ifeq ($(LDAP_NO_LIBLCACHE),1)
MODULE_CFLAGS+=-DNO_LIBLCACHE
endif


$(LIBS):  $(OBJS)
	rm -f $@
	$(AR) $(OBJS)
	$(RANLIB) $@

include $(INCLUDE_DEPENDS)

