#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server and LDAP SDK libraries
#

BUILD_ROOT = ../..
LDAP_SRC = $(BUILD_ROOT)/ldap

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib
LIBDIR = $(LDAP_LIBDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
# the following lines are to make this work for
# both db1.85 and db2.0
ifndef LDAP_USE_OLD_DB
include $(BUILD_ROOT)/ns_usedb.mk
ldap_db_depend:=$(DB_LIB_DEP)
CFLAGS+= -I$(DB_INCLUDE)
else
ldap_db_depend:=_berkeley_db
endif

ifeq ($(ARCH), WINNT)
LDAPRES=	$(addprefix $(LIBDIR)/, libldap/*.res)
LCACHERES=	$(addprefix $(LIBDIR)/, liblcache/*.res)
endif

########## Security #######################
##########ifneq ($(SECURITY), none)

# Where to find libsec and friends
SECDIR=$(NSCP_DISTDIR)/lib

ifeq ($(ARCH), WINNT)
# Used by libsec in Win32
SEC_SYSLIBS=rpcrt4.lib user32.lib
# When pulling in libsec/nspr20, WinSock and WinMM must be linked in
ifdef INCLUDE_SSL
EXTRA_LIBS=winmm.lib wsock32.lib
SDK_EXPORT_DEFS=ldapsdk.def
SDK_EXPORT_DEPS=msdos/winsock/nsldap32.def msdos/winsock/ldapssl.def
else
SDK_EXPORT_DEFS=msdos/winsock/nsldap32.def
endif
SDK_EXPORT_FLAGS=/DEF:$(SDK_EXPORT_DEFS)
LCACHE_EXPORT_DEFS=msdos/winsock/nslch32.def
LCACHE_EXPORT_FLAGS=/DEF:$(LCACHE_EXPORT_DEFS)
endif

EXPLDAP=libldap
EXPLCH=liblcache
EXPLDSSL=libldap_ssl

ifeq ($(ARCH), AIX)
USE_DLL_EXPORTS_FILE=1
else
ifeq ($(ARCH), SOLARIS)
USE_DLL_EXPORTS_FILE=1
else
ifeq ($(ARCH), IRIX)
USE_DLL_EXPORTS_FILE=1
endif
endif
endif


ifeq ($(USE_DLL_EXPORTS_FILE), 1)
ifdef INCLUDE_SSL
SDK_EXPORT_DEFS=$(OBJDEST)/$(EXPLDAP).exp $(OBJDEST)/$(EXPLDSSL).exp
DLL_EXPORT_FLAGS=$(SDK_EXPORT_DEFS)
else
SDK_EXPORT_DEFS=$(OBJDEST)/$(EXPLDAP).exp
endif
DLL_EXPORT_FLAGS=$(addprefix $(DLLEXPORTS_PREFIX), $(SDK_EXPORT_DEFS))
LCACHE_EXPORT_DEFS=$(OBJDEST)/$(EXPLCH).exp
DLL_EXPORT_FLAGS2=$(addprefix $(DLLEXPORTS_PREFIX), $(LCACHE_EXPORT_DEFS))
endif


EXTRA_SECLIBS=	$(STATICLIBNSPR) $(LIBSEC) $(LIBXP) $(LIBDB) $(LIBARES) \
	$(LIBSSLIO) $(SEC_SYSLIBS)
EXTRA_SECLIBS_DEP += $(STATICLIBNSPR) $(LIBXP_DEP) \
	$(LIBSSLIO_DEP)
EXTRA_SECLIBS_DEP += $(LIBSEC_DEP) $(LIBDB_DEP) $(LIBARES_DEP)

ifdef INCLUDE_SSL
#
# include security code in the LDAP DLL
#
EXTRA_LIBS += $(EXTRA_SECLIBS)
EXTRA_LIBS_DEP += $(EXTRA_SECLIBS_DEP)
ifneq ($(ARCH), WINNT)
DLL_LDFLAGS	+= -$(LIBPATH)$(SECDIR)
endif

SSLOBJS= $(addprefix $(LIBDIR)/, libssldap/*.o)
SSL_DEP= $(LDAPSDK_DEP)

endif
########## end Security ###################

# The following libraries are built by this Makefile:
# dynamic LDAP library
DLIBLDAP=	$(addprefix $(LIBDIR)/, $(LIBLDAP_DLL).$(DLL_SUFFIX))
# static LDAP library
SLIBLDAP=	$(addprefix $(LIBDIR)/, $(LIBLDAP_LIB).$(LIB_SUFFIX))
# dynamic LCACHE library
DLIBLCACHE=	$(addprefix $(LIBDIR)/, $(LIBLCACHE_DLL).$(DLL_SUFFIX))
# static LCACHE library
SLIBLCACHE=	$(addprefix $(LIBDIR)/, $(LIBLCACHE_LIB).$(LIB_SUFFIX))

# objects built by libavl/Makefile
AVLOBJS=	 $(addprefix $(LIBDIR)/, libavl/*.o)
# objects built by berkeley_db/Makefile
# the following lines pull in the new db library with the liblcache dll is built
# this is a hack, but can't see how to add it easier without a new LINK_DLL2.
ifndef LDAP_USE_OLD_DB
DLL_EXPORT_FLAGS2+=$(DB_STATIC_LIB)
else
BERKDBOBJS=	 $(addprefix $(LIBDIR)/, libdb/*.o)
endif
# objects built by liblber/Makefile
LBEROBJS=	$(addprefix $(LIBDIR)/, liblber/*.o)
# objects built by liblcache/Makefile
LCACHEOBJS=	 $(addprefix $(LIBDIR)/, liblcache/*.o)
# objects built by libldap/Makefile
LDAPOBJS=	$(addprefix $(LIBDIR)/, libldap/*.o)
# one object built by libldap/Makefile
REGEXOBJ=	$(addprefix $(LIBDIR)/, libldap/regex.o)
# objects built by libldbm/Makefile
LDBMOBJS=	 $(addprefix $(LIBDIR)/, libldbm/*.o)
# objects built by libldif/Makefile
LDIFOBJS=	 $(addprefix $(LIBDIR)/, libldif/*.o)

all: $(LDAP_LIBDIR) clientSDK

buildDirectory: $(LDAP_LIBDIR) _libavl _libldif _liblitekey _libutil

ifdef LDAP_USE_OLD_DB
_berkeley_db:
	cd berkeley_db; $(MAKE) $(MFLAGS) all
endif

_libavl:
	cd libavl; $(MAKE) $(MFLAGS) all

_liblber:
	cd liblber; $(MAKE) $(MFLAGS) all

_libldbm:
	cd libldbm; $(MAKE) $(MFLAGS) all

_libldif:
	cd libldif; $(MAKE) $(MFLAGS) all

_liblitekey:
	cd liblitekey; $(MAKE) $(MFLAGS) all

_libssldap:
	cd libssldap; $(MAKE) $(MFLAGS) all

_libutil:
ifeq ($(ARCH), WINNT)
	cd libutil; $(MAKE) $(MFLAGS) all
endif

_libldap:
	cd libldap; $(MAKE) $(MFLAGS) all

_slapd:
	cd $(LDAP_SRC)/servers/slapd; $(MAKE) $(MFLAGS) all

_back-ldbm:
	cd $(LDAP_SRC)/servers/slapd/back-ldbm; $(MAKE) $(MFLAGS) all

_back-ldif:
	cd $(LDAP_SRC)/servers/slapd/back-ldif; $(MAKE) $(MFLAGS) all

_liblcache:
	cd liblcache; $(MAKE) $(MFLAGS) clientSDK

ifdef INCLUDE_SSL
#
# There is no easy way to include libsec and friends when making static libs
# (at least on UNIX) so we don't bother making them when INCLUDE_SSL is set.
# Also if LDAP_NO_LIBLCACHE is 1, we don't build liblcache.  Simple, huh?
#
ifeq ($(LDAP_NO_LIBLCACHE),1)
LIBS2BUILD=$(DLIBLDAP)
else
LIBS2BUILD=$(DLIBLDAP) $(DLIBLCACHE)
endif
else
ifeq ($(LDAP_NO_LIBLCACHE),1)
LIBS2BUILD=$(DLIBLDAP) $(SLIBLDAP)
else
LIBS2BUILD=$(DLIBLDAP) $(SLIBLDAP) $(DLIBLCACHE) $(SLIBLCACHE)
endif

endif

clientSDK: $(LDAP_LIBDIR) _libavl _liblber _libldif \
		_libssldap _libutil $(LIBS2BUILD) 

OBJS=$(LBEROBJS) $(LDAPOBJS) $(SSLOBJS) $(LDAPRES)

$(DLIBLDAP): $(LDAP_OUT_DIR) _liblber _libldap $(EXTRA_LIBS_DEP) $(SSL_DEP) \
				$(SDK_EXPORT_DEFS) 
	$(LINK_DLL) $(SDK_EXPORT_FLAGS) $(EXTRA_LIBS)

$(SLIBLDAP): $(LDAP_OUT_DIR) _liblber _libldap $(EXTRA_LIBS_DEP) $(SSL_DEP)
	$(LINK_LIB)

OBJS2=$(LCACHEOBJS) $(AVLOBJS) $(LDBMOBJS) $(BERKDBOBJS) $(LDIFOBJS) \
	$(REGEXOBJ) $(LCACHERES)

ifeq ($(ARCH), WINNT)
# liblber is included here only to pick up ber_err_print()
# libsec and libxp are included here only to pick up SHA1_Hash()
OTHER_SECLIBS=	$(LIBSEC) $(LIBXP)
EXTRA_LCACHE_LIBS=$(LDAP_SDK_LIBLDAP_DLL) $(LDAP_LIBLBER) $(OTHER_SECLIBS)
EXTRA_LCACHE_LIBS_DEP= $(LDAP_SDK_LIBLDAP_DLL_DEP) $(LDAP_LIBLBER_DEP) \
	$(EXTRA_SECLIBS_DEP)
endif

ifeq ($(ARCH), AIX)
DLL_LDFLAGS     += -$(LIBPATH)$(SECDIR)
# extra libraries are needed on AIX as well
EXTRA_LCACHE_LIBS=$(LDAP_SDK_LIBLDAP_DLL) $(LDAP_LIBLBER) $(EXTRA_SECLIBS)
EXTRA_LCACHE_LIBS_DEP= $(LDAP_SDK_LIBLDAP_DLL_DEP) $(LDAP_LIBLBER_DEP) \
        $(EXTRA_SECLIBS_DEP)
endif

$(DLIBLCACHE): $(LDAP_OUT_DIR) $(ldap_db_depend) _liblcache _libavl _libldbm \
		_libldif $(EXTRA_LCACHE_LIBS_DEP) $(LCACHE_EXPORT_DEFS)
	$(LINK_DLL2) $(LCACHE_EXPORT_FLAGS) $(EXTRA_LCACHE_LIBS) $(EXTRA_LIBS)

$(SLIBLCACHE): $(LDAP_OUT_DIR) $(ldap_db_depend) _liblcache libavl _libldbm \
		_libldif
	$(LINK_LIB2)

# Need to add exports if building with SSL in NT
ifeq ($(ARCH), WINNT)
ifdef INCLUDE_SSL
$(SDK_EXPORT_DEFS) : $(SDK_EXPORT_DEPS)
	cat $(SDK_EXPORT_DEPS) > $(SDK_EXPORT_DEFS)
endif
endif

clean: FORCE
ifdef LDAP_USE_OLD_DB
	cd berkeley_db; $(MAKE) $(MFLAGS) clean
endif
	cd libavl; $(MAKE) $(MFLAGS) clean
	cd libldif; $(MAKE) $(MFLAGS) clean
ifeq ($(ARCH), WINNT)
	cd libutil; $(MAKE) $(MFLAGS) clean
	-(cd $(LDAP_SRC)/servers/slapd; $(MAKE) $(MFLAGS) clean)
endif
	-(cd $(LDAP_SRC)/servers/slapd/back-ldbm; $(MAKE) $(MFLAGS) clean)
	-(cd $(LDAP_SRC)/servers/slapd/back-ldif; $(MAKE) $(MFLAGS) clean)
ifeq ($(ARCH), WINNT)
	-$(RM) $(LIBLDAP_DLL_OBJ)
endif
	-$(RM) $(LIBS2BUILD)

veryclean:	clean

FORCE:

$(LDAP_OUT_DIR):
	$(MKDIR) $(LDAP_OUT_DIR)

$(LDAP_LIBDIR):
	$(MKDIR) $(LDAP_LIBDIR)


GENEXPORTS=$(PERL) $(LDAP_SRC)/build/genexports.pl
# WINDEFDIR is now defined below via a recursive make.  This is a
# bit of a hack to avoid collisions with SDK_EXPORT_DEFS (defined above).
#WINDEFDIR=msdos/winsock
MACDEFDIR=macintosh
GENEXPARGS=$(BUILD_DEBUG) $(DIRSDK_VERSION_DLL_SUFFIX) $(DIRSDK_VERSION)

DEFLDAPW32=$(WINDEFDIR)/nsldap32
DEFLDAPW16REV=$(WINDEFDIR)/nsldap
DEFLDAPW16=$(WINDEFDIR)/libldap
DEFLDAPW32BC=$(WINDEFDIR)/nsldap32bc
DEFLDSSLW32=$(WINDEFDIR)/ldapssl
DEFLDSSLW32BC=$(WINDEFDIR)/ldapsslbc
DEFLCHW32=$(WINDEFDIR)/nslch32
DEFLCHW32BC=$(WINDEFDIR)/nslch32bc
DEFLDAPMACPPC=$(MACDEFDIR)/libldap-PPC.u
DEFLDAPMACCLIENTPPC=$(MACDEFDIR)/LDAPClientPPC.prj
DEFLDAPMACCLIENT68K=$(MACDEFDIR)/LDAPClient68K.prj

# nsdefs.mk contains version number definitions....
OTHERDEFDEPS=$(BUILD_ROOT)/nsdefs.mk

exportfiles:
	$(MAKE) $(MFLAGS) WINDEFDIR=msdos/winsock doExportFiles

doExportFiles:	$(DEFLDAPW32).def $(DEFLDAPW16).def $(DEFLDAPW16REV).def \
	$(DEFLDAPW32BC).def $(DEFLDSSLW32).def $(DEFLDSSLW32BC).def \
	$(DEFLCHW32).def $(DEFLCHW32BC).def \
	$(DEFLDAPMACPPC).exp $(DEFLDAPMACCLIENTPPC).exp \
	$(DEFLDAPMACCLIENT68K).exp

#
# All the various flavors of Windows:
#
$(DEFLDAPW32).def:	$(DEFLDAPW32).tdf libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) Win32 $< libldap.ex $(GENEXPARGS) > $@

$(DEFLDAPW16).def:	$(DEFLDAPW16).tdf libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) Win16 $< libldap.ex $(GENEXPARGS) > $@

$(DEFLDAPW16REV).def:	$(DEFLDAPW16REV).tdf libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) Win16Rev $< libldap.ex $(GENEXPARGS) > $@

$(DEFLDAPW32BC).def:	$(DEFLDAPW32BC).tdf libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) WinBC $< libldap.ex $(GENEXPARGS) > $@

$(DEFLDSSLW32).def:	$(DEFLDSSLW32).tdf libldap_ssl.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) Win32 $< libldap_ssl.ex $(GENEXPARGS) > $@

$(DEFLDSSLW32BC).def:	$(DEFLDSSLW32BC).tdf libldap_ssl.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) WinBC $< libldap_ssl.ex $(GENEXPARGS) > $@

$(DEFLCHW32).def:	$(DEFLCHW32).tdf liblcache.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) Win32 $< liblcache.ex $(GENEXPARGS) > $@

$(DEFLCHW32BC).def:	$(DEFLCHW32BC).tdf liblcache.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) WinBC $< liblcache.ex $(GENEXPARGS) > $@

#
# MacOS:
#
$(DEFLDAPMACPPC).exp:		libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) MacOS Standard $< $(GENEXPARGS) > $@

$(DEFLDAPMACCLIENTPPC).exp:	libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) MacOS Standard $< $(GENEXPARGS) > $@

$(DEFLDAPMACCLIENT68K).exp:	libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) MacOS Standard $< $(GENEXPARGS) > $@

ifeq ($(USE_DLL_EXPORTS_FILE), 1)
# generic rules to create standard export/map files
$(OBJDEST)/$(EXPLDAP).exp:	libldap.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) $(ARCH) Standard $< $(GENEXPARGS) > $@

$(OBJDEST)/$(EXPLDSSL).exp:	libldap_ssl.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) $(ARCH) Standard $< $(GENEXPARGS) > $@

$(OBJDEST)/$(EXPLCH).exp:	liblcache.ex $(OTHERDEFDEPS)
	$(GENEXPORTS) $(ARCH) Standard $< $(GENEXPARGS) > $@
endif
