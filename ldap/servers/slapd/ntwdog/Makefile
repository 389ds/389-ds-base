#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for LDAP Server NT Service watchdog
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/ntwdog/obj
SLAPD_OBJDEST= $(OBJDIR)/obj
BINDIR = $(LDAP_SERVER_RELDIR)
LDAP_LIBDIR = $(OBJDIR)/lib

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
SUBSYSTEM=windows
endif

#INCLUDES += -I$(BUILD_ROOT)/include/libadmin 

DS_SERVER_DEFS = -DNS_DS

NTWDOG_OBJS=	ntwatchdog.o 

OBJS = $(addprefix $(OBJDEST)/, $(NTWDOG_OBJS)) 

LIBS_DEP = $(LDAP_LIBUTIL_DEP) $(LIBADMIN_DEP)

LIBS = $(LDAP_LIBUTIL)

EXTRA_LIBS += $(LIBS) 

NTWDOG = $(addprefix $(BINDIR)/, ns-slapd.exe) 

all:	$(OBJDEST) $(BINDIR) $(NTWDOG)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(NTWDOG):	$(OBJS) $(LIBS_DEP)
	$(LINK_EXE) 

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(NTWDOG)

