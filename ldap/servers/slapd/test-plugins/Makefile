#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server test-plugin
#

LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true     # probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/servers/obj
BINDIR = $(OBJDIR)/bin
LIBDIR = $(OBJDIR)/lib

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(MCOM_ROOT)/ldapserver/ns_usepurify.mk
include $(MCOM_ROOT)/ldapserver/ns_usedb.mk


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

