#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# GNU Makefile for ldap/servers
#

BUILD_ROOT = ../..
LDAP_SRC = ../

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
PRE_SLAPD_EXTRAS=_ntmsgdll
POST_SLAPD_EXTRAS=_ntwdog _ntperfdll
endif

all:	$(PRE_SLAPD_EXTRAS) _slapd $(POST_SLAPD_EXTRAS) _backends _plugins _snmp _slapdtools


ifeq ($(ARCH), HPUX)
# slapd depends on liblcoll.sl on HPUX....
_slapd:	_collation_plugin
	cd slapd; $(MAKE) $(MFLAGS) all
else
_slapd:
	cd slapd; $(MAKE) $(MFLAGS) all
endif

_collation_plugin:
	cd plugins/collation; $(MAKE) $(MFLAGS) all

_backends:
	cd slapd/back-ldbm; $(MAKE) $(MFLAGS) all
# we aren't using back-ldif yet
#	cd slapd/back-ldif; $(MAKE) $(MFLAGS) all

_plugins:
	cd plugins; $(MAKE) $(MFLAGS) all

_snmp:
ifdef still_waiting_for_net_snmp
	cd snmp; $(MAKE) $(MFLAGS) all
endif

_slapdtools:
	cd slapd/tools; $(MAKE) $(MFLAGS) all

ifeq ($(ARCH), WINNT)
_ntmsgdll:
	cd slapd/ntmsgdll; $(MAKE) $(MFLAGS) all

_ntwdog:
	cd slapd/ntwdog; $(MAKE) $(MFLAGS) all

_ntperfdll:
	cd slapd/ntperfdll; $(MAKE) $(MFLAGS) all
endif

clean:
	cd slapd; $(MAKE) $(MFLAGS) clean
ifeq ($(ARCH), WINNT)
	cd slapd/ntmsgdll; $(MAKE) $(MFLAGS) clean
	cd slapd/ntwdog; $(MAKE) $(MFLAGS) clean
	cd slapd/ntperfdll; $(MAKE) $(MFLAGS) clean
endif
	cd slapd/back-ldbm; $(MAKE) $(MFLAGS) clean
	cd slapd/back-ldif; $(MAKE) $(MFLAGS) clean
	cd plugins; $(MAKE) $(MFLAGS) clean
	cd slapd/tools; $(MAKE) $(MFLAGS) clean
