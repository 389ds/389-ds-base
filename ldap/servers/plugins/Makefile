#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server and LDAP SDK libraries
#

BUILD_ROOT = ../../..
LDAP_SRC = $(BUILD_ROOT)/ldap

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifneq ($(ARCH), WINNT)
all: _referint _collation _syntaxes _passthru _utils _uiduniq _roles _acl _replication _cos _pwdstorage _rever _chainingdb _distrib _retrocl _statechange _http _views _pam_passthru
else
all: _referint _collation _syntaxes _passthru _utils _uiduniq _roles _acl _replication _cos _pwdstorage _rever _chainingdb _distrib _retrocl _statechange _http _views
endif

_utils:
	cd shared; $(MAKE) $(MFLAGS) all
_rever:  
	cd rever; $(MAKE) $(MFLAGS) all

_chainingdb:  
	cd chainingdb; $(MAKE) $(MFLAGS) all

_referint:  
	cd referint; $(MAKE) $(MFLAGS) all

_collation:  
	cd collation; $(MAKE) $(MFLAGS) all

_syntaxes:  
	cd syntaxes; $(MAKE) $(MFLAGS) all

_passthru:
	cd passthru; $(MAKE) $(MFLAGS) all

ifneq ($(ARCH), WINNT)
_pam_passthru:
	cd pam_passthru; $(MAKE) $(MFLAGS) all
endif

_uiduniq:
	cd uiduniq; $(MAKE) $(MFLAGS) all

_replication:
	cd replication; $(MAKE) $(MFLAGS) all

_acl:
	cd acl; $(MAKE) $(MFLAGS) all

_pwdstorage:
	cd pwdstorage; $(MAKE) $(MFLAGS) all

_distrib:
	cd distrib; $(MAKE) $(MFLAGS) all

_roles:
	cd roles; $(MAKE) $(MFLAGS) all

_cos:
	cd cos; $(MAKE) $(MFLAGS) all

_statechange:
	cd statechange; $(MAKE) $(MFLAGS) all

_retrocl:
	cd retrocl; $(MAKE) $(MFLAGS) all

_http:
	cd http; $(MAKE) $(MFLAGS) all

# Note: stops building/packaging presence from 7.1
_presence:
	cd presence; $(MAKE) $(MFLAGS) all

_views:
	cd views; $(MAKE) $(MFLAGS) all

clean: 
	cd rever; $(MAKE) $(MFLAGS) clean
	cd referint; $(MAKE) $(MFLAGS) clean
	cd collation; $(MAKE) $(MFLAGS) clean
	cd syntaxes; $(MAKE) $(MFLAGS) clean
	cd passthru; $(MAKE) $(MFLAGS) clean
ifneq ($(ARCH), WINNT)
	cd pam_passthru; $(MAKE) $(MFLAGS) clean
endif
	cd shared; $(MAKE) $(MFLAGS) clean
	cd uiduniq; $(MAKE) $(MFLAGS) clean
	cd replication; $(MAKE) $(MFLAGS) clean
	cd acl; $(MAKE) $(MFLAGS) clean
	cd cos; $(MAKE) $(MFLAGS) clean
	cd pwdstorage; $(MAKE) $(MFLAGS) clean
	cd roles; $(MAKE) $(MFLAGS) clean
	cd chainingdb; $(MAKE) $(MFLAGS) clean
	cd distrib; $(MAKE) $(MFLAGS) clean
	cd retrocl; $(MAKE) $(MFLAGS) clean
	cd statechange; $(MAKE) $(MFLAGS) clean
	cd http; $(MAKE) $(MFLAGS) clean
	cd views; $(MAKE) $(MFLAGS) clean
	#cd presence; $(MAKE) $(MFLAGS) clean

veryclean:	clean
