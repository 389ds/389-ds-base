# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# --- END COPYRIGHT BLOCK ---
# 
#
# GNU Makefile for Directory Server distribution plugin
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/libdistrib
LIBDIR = $(LDAP_PLUGIN_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

DIS_OBJS= \
	distrib.o


OBJS = $(addprefix $(OBJDEST)/, $(DIS_OBJS)) 

# The sample distribution plugin is not part of DS.
# So we generate the shared library outside of $(LIBDIR)
# so that it's not retrieved by the packaging makefiles.
#LIBDIS =	$(addprefix $(LIBDIR)/, $(DIS_DLL).$(DLL_SUFFIX))
LIBDIS =	$(addprefix $(OBJDEST)/, $(DIS_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LDAPLINK) $(NSPRLINK) $(ICULINK)
endif


all:	$(OBJDEST) $(LIBDIR) $(LIBDIS)

$(LIBDIS): $(OBJS) $(DEF_FILE)
	$(LINK_DLL) $(EXTRA_LIBS) 

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBDIS)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
