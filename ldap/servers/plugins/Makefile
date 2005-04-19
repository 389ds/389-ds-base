#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
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
all: _referint _collation _syntaxes _passthru _utils _uiduniq _roles _acl _replication _cos _pwdstorage _rever _chainingdb _distrib _retrocl _statechange _http _views
# richm 20050303 - do not build pam pass thru for DS 7.1
#all: _referint _collation _syntaxes _passthru _utils _uiduniq _roles _acl _replication _cos _pwdstorage _rever _chainingdb _distrib _retrocl _statechange _http _views _pam_passthru
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
