#
# BEGIN COPYRIGHT BLOCK
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
	if [ -d docs ]; then cd docs/dirhlp/ag; $(MAKE) $(MFLAGS) ; fi
	if [ -d docs ]; then cd docs/dirhlp/deploy; $(MAKE) $(MFLAGS) ; fi
	if [ -d docs ]; then cd docs/dirhlp/cli; $(MAKE) $(MFLAGS)  ; fi
	if [ -d docs ]; then cd docs/dirhlp/schema; $(MAKE) $(MFLAGS)  ; fi
	if [ -d docs ]; then cd docs/dirhlp/install; $(MAKE) $(MFLAGS)  ; fi
	if [ -d docs ]; then cd docs/dirhlp/gwcust; $(MAKE) $(MFLAGS) ; fi
	if [ -d docs ]; then cd docs/dirhlp/plugin; $(MAKE) $(MFLAGS) ; fi
	if [ -d docs ]; then cd docs/dirhlp/orgchart; $(MAKE) $(MFLAGS) ; fi
	if [ -d docs ]; then cd docs/dirhlp/dsmlgw; $(MAKE) $(MFLAGS) ; fi

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
