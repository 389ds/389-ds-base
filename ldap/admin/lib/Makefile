#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server Admin DLL/SO.
#

LDAP_SRC = ../..
BUILD_ROOT = ../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(LDAP_ADMOBJDIR)
LIBDIR = $(LDAP_LIBDIR)
ALIBDIR = $(LDAP_ADMLIBDIR)
BINDIR=$(LDAP_ADMIN_BIN_RELDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

SRCS	= dsalib_location.c dsalib_debug.c dsalib_updown.c dsalib_tailf.c \
	dsalib_ldif.c dsalib_db.c dsalib_conf.c dsalib_html.c \
	dsalib_filename.c dsalib_util.c dsalib_dn.c dsalib_confs.c dsalib_pw.c

OBJS = $(addprefix $(OBJDEST)/, $(subst .c,.o,$(SRCS)))

INCLUDES += -I$(LDAP_SRC)/admin/include
ifdef FORTEZZA
INCLUDES += -I$(BUILD_ROOT)/lib
endif

EXTRA_LIBS += $(LDAP_COMMON_LIBS) $(SECURITYLINK) $(NSPRLINK)

LIBS= $(LDAP_ADMDLLDIR)/libds_admin$(DLL_PRESUF).$(DLL_SUFFIX)
ifeq ($(ARCH), WINNT)
IMPLIB= /IMPLIB:$(LDAP_ADMLIBDIR)/libds_admin.lib
MAPFILE= /MAP:$(LDAP_ADMLIBDIR)/libds_admin.map
EXTRA_LIBS_DEP += $(LDAP_COMMON_LIBS_DEP) $(LDAP_LIBLDIF_DEP) 
#EXTRA_LIBS += $(LDAP_COMMON_LIBS) $(LDAP_LIBLDIF) $(LDAP_SDK_LIBLDAP_DLL) \
#	$(ADMINUTIL_LINK) $(SECURITYLINK) $(NSPRLINK)
else # WINNT
ifdef FORTEZZA
# libci.a needs to be recompiled with the -Z option on HPUX, until then, 
# we'll link libci.a with the executables which need it -atom
ifneq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(FORTEZZA_DRIVER)
EXTRA_LIBS += $(FORTEZZA_DRIVER)
endif # !HPUX
endif # FORTEZZA
endif # WINNT

ifeq ($(ARCH), Linux)
# XXXsspitzer:  we do this so that cgi's the link against libds_admin.so
# will be able to find libns-dshttpd.so at run time.  Only platforms that
# build with gcc need to do this. 
RPATHFLAG_EXTRAS+=:../..:..
endif # Linux

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP)
#EXTRA_LIBS += $(LDAP_SDK_LIBLDAP_DLL) $(SECURITYLINK) $(ADMINUTIL_LINK) $(NSPRLINK) $(DBMLINK)
LD=ld -noquiet
endif

# for Solaris, our most common unix build platform, we check for undefined symbols
# at link time so we don't catch them at run time.  To do this, we set the -z defs
# flag.  We also have to add -lc to the end because, even though ld and cc link with
# it implicitly, -z defs will throw errors if we do not link with it explicitly
ifeq ($(ARCH), SOLARIS)
LINK_DLL += -z defs
EXTRA_LIBS += -lc
endif

ifneq ($(BUILD_DEBUG), optimize)
ifeq ($(ARCH), Linux)
LINK_DLL += -Wl,--no-allow-shlib-undefined
EXTRA_LIBS += -lc -lpthread -ldl /lib/ld-linux.so.2
endif
endif

all:	$(LIBS) $(LDAP_ADMDLL_RELDLLS)

$(LIBS): $(OBJDEST) $(LDAP_ADMDLLDIR) $(LDAP_ADMLIBDIR) $(OBJS) $(EXTRA_LIBS_DEP)
	$(LINK_DLL) $(IMPLIB) $(MAPFILE) $(EXTRA_LIBS)

ifeq ($(ARCH), WINNT)
$(LDAP_ADMDLL_RELDLLS): $(LIBS) $(LDAP_ADMDLL_RELDIRS)
	cp $< $@

endif

veryclean: clean

clean:
	-$(RM) $(OBJS)
	-$(RM) $(LIBS)
ifeq ($(ARCH), WINNT)
	-$(RM) $(IMPLIB)
endif

$(OBJS): $(OBJDEST)/%.o: %.c 
	$(CC) -c $(NONSHARED) $(CFLAGS) $(MCC_INCLUDE) $(OFFLAG)$(OBJDEST)/$*.o $*.c
ifdef USE_LINT
	$(LINT) $(LINTCCFLAGS) $(DEFS) $(MCC_SERVER) $(INCLUDES) $(MCC_INCLUDE) $*.c > $(OBJDEST)/$*.ln 2>&1
endif
