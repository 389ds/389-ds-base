#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for ldap/include
#

LDAP_SRC = ..
BUILD_ROOT = ../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifneq ($(ARCH), WINNT)
PROT= -m 644
endif

all:	FORCE

clientSDK: $(LDAP_INCLUDEDIR) all FORCE
		$(INSTALL) $(INSTALLFLAGS) $(PROT) ldap.h $(LDAP_INCLUDEDIR)
		$(INSTALL) $(INSTALLFLAGS) $(PROT) lber.h $(LDAP_INCLUDEDIR)
ifneq ($(LDAP_NO_LIBLCACHE),1)
		$(INSTALL) $(INSTALLFLAGS) $(PROT) lcache.h $(LDAP_INCLUDEDIR)
endif

		$(INSTALL) $(INSTALLFLAGS) $(PROT) ldap_ssl.h $(LDAP_INCLUDEDIR)

install: $(LDAP_INCLUDEDIR) clientSDK FORCE
		$(INSTALL) $(INSTALLFLAGS) $(PROT) disptmpl.h $(LDAP_INCLUDEDIR)
		$(INSTALL) $(INSTALLFLAGS) $(PROT) srchpref.h $(LDAP_INCLUDEDIR)
		$(INSTALL) $(INSTALLFLAGS) $(PROT) portable.h $(LDAP_INCLUDEDIR)

depend:	FORCE

clean:	FORCE
	-$(RM) db.h sys btree.h extern.h mpool.h

veryclean:	clean

FORCE:
