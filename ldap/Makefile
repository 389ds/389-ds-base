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

# GNU Makefile for Directory Server and Ldap SDK
#

BUILD_ROOT = ..
LDAP_SRC = $(BUILD_ROOT)/ldap

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

all: $(LDAP_LIBDIR) $(LDAP_BINDIR) $(LDAP_OBJDIR) ldapprogs ldapdocs

ldapprogs:
	cd include; $(MAKE) $(MFLAGS) all
	cd libraries; $(MAKE) $(MFLAGS) buildDirectory
	cd servers; $(MAKE) $(MFLAGS) all
ifneq ($(ARCH), WINNT) 
	cd systools; $(MAKE) $(MFLAGS) all
	# new unix installer
ifeq ($(USE_SETUPSDK), 1)
	cd cm/newinst; $(MAKE) $(MFLAGS) all
ifeq ($(USE_64),1)
	# In 64-bit builds, we build the installer 32-bit, which has the side-effect that the uninstaller and ns-update scripts
	# get copied into the 32-bit output directory by the makefile above. However, we later want to package them and expect
	# to see them in the 64-bit output directory. So, here we copy them over.
	$(CP) $(RELDIR_32)/bin/slapd/admin/bin/ns-update $(LDAP_ADMIN_BIN_RELDIR)
	$(CP) $(RELDIR_32)/bin/slapd/admin/bin/uninstall $(LDAP_ADMIN_BIN_RELDIR)
endif # USE_64
endif # USE_SETUPSDK
else # not WINNT
ifeq ($(USE_SETUPSDK), 1)
	cd cm/newinstnt; $(MAKE) $(MFLAGS) all
endif # USE_SETUPSDK
endif # WINNT
	cd admin; $(MAKE) $(MFLAGS) all

ldapdocs:
	if [ -d docs ]; then cd docs/dirhlp; $(MAKE) $(MFLAGS) ; fi

clientSDK: $(LDAP_LIBDIR) $(LDAP_BINDIR) $(LDAP_OBJDIR) 
	cd include; $(MAKE) $(MFLAGS) clientSDK
ifeq ($(ARCH), WINNT)
	cd servers/slapd/ntmsgdll; $(MAKE) $(MFLAGS) all
endif
	cd libraries; $(MAKE) $(MFLAGS) clientSDK
	cd clients/tools; $(MAKE) $(MFLAGS) clientSDK

clean: 
	cd include; $(MAKE) $(MFLAGS) clean
	cd libraries; $(MAKE) $(MFLAGS) clean
	cd servers; $(MAKE) $(MFLAGS) clean
	cd admin; $(MAKE) $(MFLAGS) clean
ifneq ($(ARCH), WINNT) # new unix installer
	cd systools; $(MAKE) $(MFLAGS) clean
	cd cm/newinst; $(MAKE) $(MFLAGS) clean
else
	cd cm/newinstnt; $(MAKE) $(MFLAGS) clean
endif

cleanSDK:
	cd include; $(MAKE) $(MFLAGS) clean
ifeq ($(ARCH), WINNT)
	cd servers/slapd/ntmsgdll; $(MAKE) $(MFLAGS) clean
endif
	cd libraries; $(MAKE) $(MFLAGS) clean
	cd clients/tools; $(MAKE) $(MFLAGS) clean

veryclean:	clean
