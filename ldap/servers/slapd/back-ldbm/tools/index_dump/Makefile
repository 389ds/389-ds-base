#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server libback-ldbm
#

LDAP_SRC = ../../../../..
BUILD_ROOT = ../../../../../..

OBJDEST = $(OBJDIR)/lib/libback-ldbm
LIBDIR = $(LDAP_LIBDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(BUILD_ROOT)/ns_usedb.mk

INCLUDES += -I$(LDAP_SRC)/servers/slapd

INDEX_DUMP_OBJS= index_dump.o

OBJS = $(addprefix $(OBJDEST)/, $(INDEX_DUMP_OBJS))

all:	$(OBJDEST) $(LIBDIR) $(SLIBBACK_LDBM) $(LIBBACK_LDBM)

veryclean: clean

clean:

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(BINDIR):
	$(MKDIR) $(LIBDIR)

