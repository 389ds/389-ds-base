#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server Admin include directory.
#

BUILD_ROOT = ../../..
LDAP_SRC = ../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

HDRDEST=$(LDAP_ADMROOT)/include

HEADERS=dsalib.h dsalib_pw.h
BINS=$(addprefix $(HDRDEST)/,$(HEADERS))

all: $(HDRDEST) $(BINS) 

strip:
depend:

clean:
	-$(RM) $(BINS)

$(HDRDEST)/%.h: %.h
	-@$(RM) $@
	$(CP) $< $@
