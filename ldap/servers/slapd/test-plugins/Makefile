#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server test-plugin
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true     # probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/servers/obj
BINDIR = $(OBJDIR)/bin
LIBDIR = $(OBJDIR)/lib

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(BUILD_ROOT)/ns_usepurify.mk


EXTRA_INCLUDES=-I.. -I../../../include $(NSPR_INCLUDE) -I$(LDAP_INCLUDE) -I$(DB_INCLUDE) -I../../../servers/slapd -g
INCLUDE_FLAGS=-I../include $(EXTRA_INCLUDES)
EXTRA_LIBS = $(DB_LIB)

ifdef USE_64
MAKEFILE_FILE=Makefile.$(BUILD_ARCH)64
else
MAKEFILE_FILE=Makefile.$(BUILD_ARCH)
endif

all:
	$(MAKE) -f $(MAKEFILE_FILE) all INCLUDE_FLAGS="$(INCLUDE_FLAGS)" EXTRA_LIBS="$(EXTRA_LIBS)"

libtest-plugin.so:
	$(MAKE) -f $(MAKEFILE_FILE) libtest-plugin.so \
	    INCLUDE_FLAGS="$(INCLUDE_FLAGS)"

clean:
	$(MAKE) -f $(MAKEFILE_FILE) clean INCLUDE_FLAGS="$(INCLUDE_FLAGS)"

veryclean:		clean

