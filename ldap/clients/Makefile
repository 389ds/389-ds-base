# Build Directory client apps
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#   Name:
#   Platform: gmake
# --------------------------------------------------------------------------
override BUILD_MODULE=HTTP_ADMIN

NOSTDCLEAN=true
NO_BUILD_NUM=true

BUILD_ROOT=../..

include $(BUILD_ROOT)/nsconfig.mk
include $(BUILD_ROOT)/ldap/javarules.mk

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

