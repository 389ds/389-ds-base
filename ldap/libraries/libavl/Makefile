#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for libavl
#

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libavl
LIBDIR = $(LDAP_LIBDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

CFLAGS +=	$(SLCFLAGS)

LIBAVL_OBJS=	avl.o

OBJS = $(addprefix $(OBJDEST)/, $(LIBAVL_OBJS)) 

LIBAVL=	$(addprefix $(LIBDIR)/, libavl.$(LIB_SUFFIX))

clientSDK: all

all:	$(OBJDEST) $(LIBDIR) $(LIBAVL)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)

$(LIBAVL): $(OBJS)
	$(LINK_LIB)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBAVL)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

