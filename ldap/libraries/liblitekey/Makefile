#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for liblitekey
#

LDAP_SRC = ../..
MCOM_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/liblitekey
LIBDIR = $(LDAP_LIBDIR)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

CFLAGS +=	$(SLCFLAGS)

LIBLITEKEY_OBJS=	keycheck.o

OBJS = $(addprefix $(OBJDEST)/, $(LIBLITEKEY_OBJS)) 

LIBLITEKEY = $(addprefix $(LIBDIR)/, liblitekey.$(LIB_SUFFIX))

all:	$(OBJDEST) $(LIBDIR) $(OBJS) $(LIBLITEKEY)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)

$(LIBLITEKEY): $(OBJS)
	$(LINK_LIB)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBLITEKEY)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

