#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Source for the install forms and CGI programs

.SUFFIXES: .cc

# We do this to force a 32-bit build of this stuff, even in a 64-bit build 
# (because we want to avoid various complexities inherenet in having a 64-bit
# installer rubbing shoulders with other 32-bit stuff at install time).
COMPONENT_DEPS := 1
override USE_64=

BUILD_ROOT = ../../..
LDAP_SRC = ../..

MODULE=slapdInstallBin
# override BUILD_MODULE = HTTP_ADMIN

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

test:
	echo $(SETUPSDK_VERSION)
	echo $(SETUPSDK_RELEASE)

# MODULE_CFLAGS = -DUSE_ADMSERV

BINDEST=$(LDAP_ADMIN_BIN_RELDIR)
OBJDEST=$(LDAP_ADMOBJDIR)

ifndef $(OFFLAG)
OFFLAG=-o 
endif

# we don't want to build with warnings-as-errors for the cm/ stuff, because
# it's crappy C++ code which is LITTERED with warnings, most of which we
# can't fix because it comes from files in dist/, etc.
ifeq ($(ARCH), Linux)
CFLAGS := $(subst -Werror,,$(CFLAGS))
endif

NOSTDCLEAN=true
NOSTDSTRIP=true

OSOBJS=
OBJS1=$(OBJDEST)/ux-dialog.o $(OBJDEST)/ux-config.o $(OBJDEST)/ux-dsalib_dn.o
OBJS1GT=$(OBJDEST)/ux-guesses.o 

PROGS= ns-config
ifeq ($(ARCH),WINNT)
BINS=
OSOBJS=
OBJS1=
OBJS2=
OSLIBS=
OSDEPLIBS=
LIBS=
else
BINS=$(addprefix $(BINDEST)/, $(PROGS))
endif
INFO= $(OBJDIR)/$(DIR)

# Source for staged installation utilities
INCDIR=$(SETUPSDK_INCLUDE) -I$(LDAP_SRC)/admin/include -I$(LDAP_SRC)/admin/lib -I$(LDAP_SRC)/admin/src

# ADM_VERSDIR = admserv40
# ADM_RELDATE = 19980112

all: $(OBJDEST) $(BINDEST) $(SETUPSDK_DEP) $(LDAPSDK_DEP) $(SECURITY_DEP)  $(NSPR_DEP) $(OSOBJS) $(OBJS1) $(OBJS2) $(BINS) $(INFO) $(BINDEST)/ns-update $(BINDEST)/uninstall
# removed ns-keygen from build - it was only used for Dir Lite
# $(BINDEST)/ns-keygen 
ifeq ($(ARCH), BSDI)
CFLAGS += -DNO_DOMAINNAME
endif

ifeq ($(ARCH), IRIX)
CFLAGS += -exceptions
EXTRA_LIBS += -lm -lc
endif

ifeq ($(ARCH), OSF1)
EXTRA_LIBS += -lm
endif

#LIBLDAPU= ldapu
#LIBLDAP= $(LDAP_LIBPATH)/libldap$(LDAP_SUF).a
#DEPLIBS=$(addprefix $(OBJDIR)/lib/lib, \
#        $(addsuffix .$(LIB_SUFFIX), $(LIBLDAPU))) $(OSDEPLIBS)

ifeq ($(ARCH), HPUX)

ifeq ($(NSOS_RELEASE),B.10.10)
CURSES=-lHcurses
endif

ifeq ($(NSOS_RELEASE),B.11.00)
	MODERNHP=1
endif

ifeq ($(NSOS_RELEASE),B.11.11)
	MODERNHP=1
endif

ifeq ($(MODERNHP),1)
CURSES=-lHcurses
else
CURSES=/usr/lib/libcurses.a
endif
ifeq ($(USE_64), 1)
CURSES=-lcurses
endif
else
CURSES=-lcurses -ltermcap
endif

ifeq ($(ARCH), AIX)
CURSES=-lcurses
endif

$(BINDEST)/ns-update : ns-update
	-@$(RM) $@
	$(CP) $< $@

$(BINDEST)/ns-keygen: ns-keygen 
	-@$(RM) $@
	$(CP) $< $@

$(BINDEST)/uninstall: uninstall
	-@$(RM) $@
	$(CP) $< $@

$(BINDEST)/ns-config: $(OBJS1) $(OBJS2)
	$(PURIFY) $(CXX) $(SHARED_FLAG) $(CFLAGS) $(MCC_INCLUDE) $(INCDIR) \
		-o $(BINDEST)/ns-config $(RPATHFLAG_PREFIX)$(RPATHFLAG)$(RPATHFLAG_EXTRAS) $(OBJS1) $(OBJS2) $(SETUPSDKLINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) \
	       $(EXTRA_LIBS) $(CURSES)

ifeq ($(ARCH), WINNT)
$(INFO):
	$(PERL) fixINF.pl $(BUILD_MODULE) $(DIR_VERSION) $(BUILD_ROOT)/$(BUILD_ARCH)/buildnum.dat slapd.inf $(SECURITY) $(PRODUCT) $(IS_DIR_LITE) $(INSTANCE_NAME_PREFIX) $@.inf $(BUILD_BOMB) "bin/admin/ns-admin,bin/admin/ns-admin.so"
else
$(INFO):
	$(PERL) fixINF.pl $(BUILD_MODULE) $(DIR_VERSION) $(BUILD_ROOT)/$(BUILD_ARCH)/buildnum.dat slapd.inf $(SECURITY) $(PRODUCT) $(IS_DIR_LITE) $(INSTANCE_NAME_PREFIX) $@.inf $(BUILD_BOMB) "$(addprefix lib/,$(LDAP_SOLIBS)) bin/admin/libnsslapd.sl bin/slapd/bin/ns-slapd bin/slapd/bin/ns-slapd.so bin/slapd/bin/libnsslapd_shr.a"
endif

$(OBJDEST)/%.o: %.c
	$(CC) $(NONSHARED) $(CFLAGS) $(MCC_INCLUDE) $(INCDIR) -c $< $(OFFLAG)$@

$(OBJDEST)/%.o: %.cc
	$(CXX) $(NONSHARED) $(CFLAGS) $(MCC_INCLUDE) $(INCDIR) -c $< $(OFFLAG)$@

ns-config-gt: $(OBJS1GT)
	$(PURIFY) $(CXX) $(NONSHARED) $(CFLAGS) $(MCC_INCLUDE) $(INCDIR) \
		-o ns-config-gt $(OBJS1GT) $(SETUPSDKLINK) $(LIBLDAP) \
		$(EXTRA_LIBS) $(CURSES)
