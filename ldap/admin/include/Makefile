#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server Admin include directory.
#

MCOM_ROOT = ../../../..
LDAP_SRC = ../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

HDRDEST=$(LDAP_ADMROOT)/include

HEADERS=dsalib.h dsalib_pw.h
BINS=$(addprefix $(HDRDEST)/,$(HEADERS))

all: $(HDRDEST) $(BINS) 

$(HDRDEST):
	$(MKDIR) $(HDRDEST)

strip:
depend:

clean:
	-$(RM) $(BINS)

$(HDRDEST)/%.h: %.h
	-@$(RM) $@
	$(CP) $< $@
