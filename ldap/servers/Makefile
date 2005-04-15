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
# do so, delete this exception statement from your version. 
# 
# 
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
	cd snmp; $(MAKE) $(MFLAGS) all

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
