# Build Directory client apps
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#   Name:
#   Platform: gmake
# --------------------------------------------------------------------------
override BUILD_MODULE=HTTP_ADMIN

NOSTDCLEAN=true
NO_BUILD_NUM=true

MCOM_ROOT=../../..

include ../../nsconfig.mk
include ../../ldap/javarules.mk

all: ldaptags online dsgw dsmlgw

ldaptags:
	cd ldaptags; $(MAKE)

online:
	cd online; $(MAKE)

dsgw:
	cd dsgw; $(MAKE)

dsmlgw:
	cd dsmlgw; $(MAKE)

clean: 
	cd dsgw; $(MAKE) $(MFLAGS) clean
	cd dsmlgw; $(MAKE) clean

