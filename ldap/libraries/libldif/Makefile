#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for libldif
#

LDAP_SRC = ../..
MCOM_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libldif
LIBDIR = $(LDAP_LIBDIR)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

CFLAGS +=	$(SLCFLAGS)

LIBLDIF_OBJS=	line64.o fileurl.o

OBJS = $(addprefix $(OBJDEST)/, $(LIBLDIF_OBJS)) 

LIBLDIF=	$(addprefix $(LIBDIR)/, libldif.$(LIB_SUFFIX))

clientSDK: all

all:	$(OBJDEST) $(LIBDIR) $(OBJS) $(LIBLDIF)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)

$(LIBLDIF): $(OBJS)
	$(LINK_LIB)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBLDIF)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

