#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server Admin components
#

BUILD_ROOT = ../..
LDAP_SRC = ../

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

all:
	cd include; $(MAKE) $(MFLAGS) all
	cd lib; $(MAKE) $(MFLAGS) all
	cd src; $(MAKE) $(MFLAGS) all

veryclean: clean

clean:
	cd include; $(MAKE) $(MFLAGS) clean
	cd lib; $(MAKE) $(MFLAGS) clean
	cd src; $(MAKE) $(MFLAGS) clean
