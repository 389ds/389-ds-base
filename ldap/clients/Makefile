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

all: _ldaptags _online _dsgw _dsmlgw

_ldaptags:
	cd ldaptags; $(MAKE)

_online:
	cd online; $(MAKE)

_dsgw:
	cd dsgw; $(MAKE)

_dsmlgw:
	cd dsmlgw; $(MAKE)

clean: 
	cd dsgw; $(MAKE) $(MFLAGS) clean
	cd dsmlgw; $(MAKE) clean

