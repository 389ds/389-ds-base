#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for shared components for Directory Server plugins
#
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

SHARED=shared

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
#NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/$(SHARED)
LIBDIR = $(LDAP_LIBDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

LOCAL_OBJS=  utils.o

OBJS = $(addprefix $(OBJDEST)/, $(LOCAL_OBJS)) 

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS += $(LIBSLAPD)
endif

all:	$(OBJDEST) $(OBJS)

veryclean: clean

clean:
	$(RM) $(OBJS)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(OBJS): plugin-utils.h

