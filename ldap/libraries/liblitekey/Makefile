#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for liblitekey
#

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/liblitekey
LIBDIR = $(LDAP_LIBDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

CFLAGS +=	$(SLCFLAGS)

LIBLITEKEY_OBJS=	keycheck.o

OBJS = $(addprefix $(OBJDEST)/, $(LIBLITEKEY_OBJS)) 

LIBLITEKEY = $(addprefix $(LIBDIR)/, liblitekey.$(LIB_SUFFIX))

all:	$(OBJDEST) $(LIBDIR) $(OBJS) $(LIBLITEKEY)

$(LIBLITEKEY): $(OBJS)
	$(LINK_LIB)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBLITEKEY)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

