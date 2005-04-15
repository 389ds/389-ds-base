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
#
# nsldap.mk: GNU Makefile for common defs used in Fedora Directory Server 
#	and related tools.
# 									

#
# Set the global directory points
#

# This stuff is for UNIX--we wire in absolute paths
# because it makes the tar'ing easier.
# On NT we don't bother with this trick.
# Note that we're setting BUILD_ROOT again,
# having set it to a relative path above, so
# we can find the include files.
ifneq ($(ARCH), WINNT)
# This seems useless to check for a path of the form word:word if the arch
# is neq winnt . . .
BUILD_TMP = $(subst :, , $(shell cd ../../..;pwd))
BUILD_WORDS = $(words $(BUILD_TMP))
# convert BUILD_ROOT from relative path to absolute
#BUILD_ROOT = $(word $(BUILD_WORDS), $(BUILD_TMP))
ifneq ($(BUILD_WORDS), 1)
BUILD_DRIVE = $(word 1, $(BUILD_TMP)):
endif
endif

RELTOP=$(BUILD_ROOT)/built/release
OBJDIR_BASE = $(notdir $(OBJDIR))
OBJDIR_BASE_32 = $(notdir $(OBJDIR_32))
# Release directory for Directory Server
RELDIR = $(BUILD_DRIVE)$(RELTOP)/$(DIR)/$(OBJDIR_BASE)
RELDIR_32 = $(BUILD_DRIVE)$(RELTOP)/$(DIR)/$(OBJDIR_BASE_32)
RELDIR_UNSTRIP = $(BUILD_DRIVE)$(RELTOP)/$(DIR)/$(ARCHPROCESSOR)$(NS64TAG)-$(SECURITY)$(SSL_PREFIX)-$(DEBUG)$(RTSUFFIX)-unstripped-$(BUILD_FORTEZZA)$(BUILD_PTHREADS)-$(DIR)

# this is the place libraries and plugins go which are used by other
# components i.e. not specific to slapd and its programs
LIB_RELDIR = $(RELDIR)/lib
# Release path definitions for software components
# This is the base path for directory server specific components
LDAP_BASE_RELDIR = $(RELDIR)/bin/slapd
# This is the base path for the slapd program and other related programs
LDAP_SERVER_RELDIR = $(LDAP_BASE_RELDIR)/server
# This is the path for administrative programs, installers, CGIs, etc.
LDAP_ADMIN_BIN_RELDIR = $(LDAP_BASE_RELDIR)/admin/bin
# This is the path for other programs, perf counters, etc.
LDAP_INSTALL_BIN_RELDIR = $(LDAP_BASE_RELDIR)/install/bin
# This is the base path for directory server specific dlls
LDAP_LIB_RELDIR = $(LDAP_BASE_RELDIR)/lib
# This is the primary location for the dsadmin dll
LDAP_ADMDLLDIR = $(LDAP_LIB_RELDIR)
# This is the location for the dsadmin export and/or static library,
# for those platforms which separate them from the dll (like NT :-( )
ifeq ($(ARCH), WINNT)
LDAP_ADMLIBDIR = $(LDAP_ADMROOT)/lib
# This is a list of other files (for NT) the dsadmin dll needs to be
# copied to
LDAP_ADMDLL_RELDLLS = $(LDAP_ADMIN_BIN_RELDIR)/libds_admin$(DLL_PRESUF).$(DLL_SUFFIX) $(LDAP_SERVER_RELDIR)/libds_admin$(DLL_PRESUF).$(DLL_SUFFIX)
LDAP_ADMDLL_RELDIRS = $(LDAP_ADMIN_BIN_RELDIR) $(LDAP_SERVER_RELDIR)
else # same place as dll
LDAP_ADMLIBDIR = $(LDAP_ADMDLLDIR)
endif

LDAP_SRC = $(BUILD_ROOT)/ldap

LDAP_INSTROOT= $(OBJDIR)

LDAP_LIBDIR = $(LDAP_INSTROOT)/lib
LDAP_OBJDIR = $(LDAP_INSTROOT)/servers/obj
LDAP_MANDIR = $(LDAP_INSTROOT)/man
LDAP_BINDIR = $(LDAP_INSTROOT)/bin
LDAP_INCLUDEDIR = $(LDAP_INSTROOT)/include
LDAP_ETCDIR = $(LDAP_INSTROOT)/etc

LDAP_ADMROOT = $(LDAP_INSTROOT)/dsadmin
LDAP_ADMINCDIR = $(LDAP_ADMROOT)/include
LDAP_ADMOBJDIR = $(LDAP_ADMROOT)/obj
LDAP_ADMPERLDIR = $(LDAP_ADMROOT)/perl

LDAP_HDIR = $(LDAP_SRC)/include

# set up a target for all directories which are used as dependencies so that the
# directory will be created if it is needed
DEPENDENCY_DIRS = $(RELDIR) $(LDAP_SERVER_RELDIR) $(LDAP_ADMIN_BIN_RELDIR) \
	$(LDAP_LIB_RELDIR) $(LDAP_ADMROOT)/lib $(OBJDIR) $(LDAP_LIBDIR) $(LDAP_OBJDIR) \
	$(LDAP_MANDIR) $(LDAP_BINDIR) $(LDAP_INCLUDEDIR) $(LDAP_ETCDIR) $(LIB_RELDIR) \
	$(LDAP_ADMINCDIR) $(LDAP_ADMOBJDIR) $(LDAP_ADMPERLDIR) $(LDAP_INSTALL_BIN_RELDIR)

$(DEPENDENCY_DIRS):
	$(MKDIR) $@

# On AIX, include _shr in shared library names.  This
# is done because the suffix .a is used with both static and dynamic libs
# and we need some way to distinguish the two.  You gotta love AIX....
ifeq ($(ARCH), AIX)
ifdef OLD_AIX_LINKING
DLL_PRESUFFIX=_shr
endif
else
DLL_PRESUFFIX=
endif

# warnings as errors
# FIXME
#ifeq ($(ARCH), Linux)
#CFLAGS += -Werror
#endif
#ifeq ($(ARCH), SOLARIS)
#CFLAGS += -xwe
#endif

# turn on convidl: new idl upgrade tool
CFLAGS+=-DUPGRADEDB

#
# Dynamic library for LDAP Server Admin interface
#
ifeq ($(ARCH), WINNT)
LDAP_ADMLIB_DEP = $(LDAP_ADMDLLDIR)/libds_admin$(DLL_PRESUF).$(DLL_SUFFIX) $(LDAP_ADMLIBDIR)/libds_admin.$(LIB_SUFFIX)
LDAP_ADMLIB = $(LDAP_ADMLIBDIR)/libds_admin.$(LIB_SUFFIX)
else
LDAP_ADMLIB_DEP = $(LDAP_ADMLIBDIR)/libds_admin$(DLL_PRESUF).$(DLL_SUFFIX)
ifeq ($(ARCH), UnixWare)
#add (COMMON_OBJDIR) to (LDAP_ADMLIB) so $(LD) can find ns-dshttpd.so
LDAP_ADMLIB = -L$(COMMON_OBJDIR) -lds_admin$(DLL_PRESUF)
else
LDAP_ADMLIB = -L$(LDAP_ADMDLLDIR) -lds_admin$(DLL_PRESUF)
endif # UnixWare
endif # WINNT

#
# Common LDAP static libraries.
# 
ifdef LDAP_USE_OLD_DB
ldap_extra_db_lib:=libldbm libdb
ldap_extra_db_link:=-lldbm -ldb
else
ldap_extra_db_lib:=
ldap_extra_db_link:=
endif # LDAP_USE_OLD_DB
LDAP_COMMON_LIBSLIST = libavl $(ldap_extra_db_lib) libldif liblitekey
ifeq ($(ARCH), WINNT)
LDAP_COMMON_LIBSLIST += libutil
else
LDAP_COMMON_LIBSLIST += libldif
endif # WINNT

LDAP_COMMON_LIBS_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, $(LDAP_COMMON_LIBSLIST)))
ifeq ($(ARCH), WINNT)
LDAP_COMMON_LIBS = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, $(LDAP_COMMON_LIBSLIST)))
LDAP_COMMON_LINK = /LIBPATH:$(LDAP_LIBDIR) \
	$(addsuffix .$(LIB_SUFFIX), $(LDAP_COMMON_LIBSLIST))
else
LDAP_COMMON_LIBS = -lavl $(ldap_extra_db_link) -lldif
LDAP_COMMON_LINK = $(LDAP_COMMON_LIBS)
endif

#
# Individual LDAP libraries and dependancies
#

LDAP_LIBAVL_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libavl))
ifeq ($(ARCH), WINNT)
LDAP_LIBAVL = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libavl))
else
LDAP_LIBAVL = -lavl
endif

ifdef LDAP_USE_OLD_DB
LDAP_LIBLDBM_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libldbm))
ifeq ($(ARCH), WINNT)
LDAP_LIBLDBM = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libldbm))
else
LDAP_LIBLDBM = -lldbm
endif
else
LDAP_LIBLDBM_DEP:=
LDAP_LIBLDBM:=
endif

# dboreham: changed for new db regime
ifdef LDAP_USE_OLD_DB
LDAP_LIBDB_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libdb))
ifeq ($(ARCH), WINNT)
LDAP_LIBDB = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libdb))
else
LDAP_LIBDB = -ldb
endif
else
LDAP_LIBDB_DEP:=
LDAP_LIBDB:=DONT USE THIS ANYMORE
endif

LDAP_LIBLBER_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, liblber))
ifeq ($(ARCH), WINNT)
LDAP_LIBLBER = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, liblber))
else
LDAP_LIBLBER = -llber
endif

LDAP_LIBUTIL_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libutil))
ifeq ($(ARCH), WINNT)
LDAP_LIBUTIL = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libutil))
else
LDAP_LIBUTIL = -lutil
endif

LDAP_LIBLDIF_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libldif))
ifeq ($(ARCH), WINNT)
LDAP_LIBLDIF = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libldif))
else
LDAP_LIBLDIF = -lldif
endif

LDAP_LIBLITEKEY_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, liblitekey))
ifeq ($(ARCH), WINNT)
LDAP_LIBLITEKEY = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, liblitekey))
else
LDAP_LIBLITEKEY = -llitekey
endif

ifneq ($(LDAP_NO_LIBLCACHE),1)
LDAP_SLIBLCACHE_DEP = $(addsuffix .$(LIB_SUFFIX), \
      $(addprefix $(LDAP_LIBDIR)/, $(LIBLCACHE_LIB)))
ifeq ($(ARCH), WINNT)
LDAP_SLIBLCACHE = $(addsuffix .$(LIB_SUFFIX), \
      $(addprefix $(LDAP_LIBDIR)/, $(LIBLCACHE_LIB)))
else
# XXXmcs: on UNIX we actually use the DLL (?)
LDAP_SLIBLCACHE = $(LDAP_SDK_LIBLCACHE_DLL)
endif
endif

# dynamic libs that we ship will be put in <reldir>/lib and
# static libs that we use to build other ds components will
# be put in <builddir>/lib; this is mostly for NT and other
# platforms that separate the static and dynamic code
ifeq ($(ARCH), WINNT)
LDAP_LIBBACK_LDBM_LIBDIR = $(LDAP_LIBDIR)
LDAP_LIBBACK_LDBM_DLLDIR = $(LIB_RELDIR)
LDAP_LIBBACK_LDBM = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, $(LIBBACK_LDBM_LIB)))
else
LDAP_LIBBACK_LDBM = -lback-ldbm
LDAP_LIBBACK_LDBM_LIBDIR = $(LDAP_LIBDIR)
LDAP_LIBBACK_LDBM_DLLDIR = $(LIB_RELDIR)
endif
LDAP_LIBBACK_LDBM_DEP = $(addsuffix .$(DLL_SUFFIX), \
    $(addprefix $(LDAP_LIBBACK_LDBM_LIBDIR)/, $(LIBBACK_LDBM_DLL)))

#
# Libldapu
#
LIBLDAPU_DEP = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libldapu))
ifeq ($(ARCH), WINNT)
LIBLDAPU = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libldapu))
else
LIBLDAPU = -lldapu
endif

#
# Libadmin
#
LIBADMIN_DEP_OLD = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libadmin))
ifeq ($(ARCH), WINNT)
LIBADMIN_OLD = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libadmin))
else
LIBADMIN_OLD = -ladmin
endif

LIBADMIN=$(LIBADMIN_OLD)
LIBADMIN_DEP=$(LIBADMIN_DEP_OLD)

#
# Shared library for slapd objects---this contains
# everything prototyped in backendext.h, and 
# in slapd-proto.h The latter routines are not
# for public consumption, but live in the library
# used by 3rd party backends.
# On NT, the libslapd dll is packaged in the same directory as the server
# On Unix, the libslapd dll is packaged in the <server root>/lib directory
ifeq ($(ARCH), WINNT)
LIBSLAPD_DEP = $(addsuffix .$(DLL_SUFFIX), \
    $(addprefix $(LDAP_SERVER_RELDIR)/, libslapd$(DLL_PRESUFFIX)))
LIBSLAPD_DLL = $(addsuffix .$(DLL_SUFFIX), \
    $(addprefix $(LDAP_SERVER_RELDIR)/, libslapd$(DLL_PRESUFFIX)))
LIBSLAPD = $(addsuffix .$(LIB_SUFFIX), \
    $(addprefix $(LDAP_LIBDIR)/, libslapd))
LIBSLAPDLINK = /LIBPATH:$(LDAP_LIBDIR) libslapd.$(LIB_SUFFIX)
# This is a list of other files (for NT) the dsadmin dll needs to be
# copied to
LIBSLAPD_RELDLLS = $(LDAP_SERVER_RELDIR)/libslapd$(DLL_PRESUF).$(DLL_SUFFIX)
LIBSLAPD_RELDIRS = $(LDAP_SERVER_RELDIR)
else
# libslapd is now in $(RELDIR)/bin/slapd/server
LIBSLAPD_DEP = $(addsuffix .$(DLL_SUFFIX), \
    $(addprefix $(LDAP_SERVER_RELDIR)/, libslapd$(DLL_PRESUFFIX)))
LIBSLAPD_DLL = $(LIBSLAPD_DEP)
LIBSLAPD = -L$(LDAP_SERVER_RELDIR) -lslapd$(DLL_PRESUFFIX)
LIBSLAPDLINK = $(LIBSLAPD)
endif

#
# XP
#
LIBXP_DEP = $(NSCP_DISTDIR)/lib/libxp.$(LIB_SUFFIX)
ifeq ($(ARCH), WINNT)
LIBXP = $(NSCP_DISTDIR)/lib/libxp.$(LIB_SUFFIX)
else
LIBXP = -lxp
endif

#
# SSLIO
#
LIBSSLIO_DEP = $(NSCP_DISTDIR)/lib/libsslio.$(LIB_SUFFIX)
ifeq ($(ARCH), WINNT)
LIBSSLIO = $(NSCP_DISTDIR)/lib/libsslio.$(LIB_SUFFIX)
else
LIBSSLIO = -lsslio
endif

#
# Libsec
#
LIBSEC_DEP = $(NSCP_DISTDIR)/lib/libsec-$(SECURITY_EXTN).$(LIB_SUFFIX)
LIBSEC = $(NSCP_DISTDIR)/lib/libsec-$(SECURITY_EXTN).$(LIB_SUFFIX)
ifdef FORTEZZA
LIBSEC += $(FORTEZZA_DRIVER)
endif

#
# Libdb
#
LIBDB_DEP = $(NSCP_DISTDIR)/lib/libdbm.$(LIB_SUFFIX)
ifeq ($(ARCH), WINNT)
LIBDB = $(NSCP_DISTDIR)/lib/libdbm.$(LIB_SUFFIX)
else
LIBDB = -ldbm
endif

#
# ACL library, Libaccess
#
LIBACCESS_DEP = $(LDAP_LIBDIR)/libaccess.$(LIB_SUFFIX)
ifeq ($(ARCH), WINNT)
LIBACCESS = $(LDAP_LIBDIR)/libaccess.lib
else
LIBACCESS = -laccess
endif

#
# Dynamic libraries and dependancies, LDAP SDK
#
ifeq ($(ARCH), WINNT)
LIBLDAP_DLL = nsldap32v$(DIRSDK_VERSION_DLL_SUFFIX)
LIBLDAP_LIB = nsldaps32v$(DIRSDK_VERSION_DLL_SUFFIX)
LIBSSLDAP_LIB = nsldapssl32v$(DIRSDK_VERSION_DLL_SUFFIX)
LIBLCACHE_DLL = nslch32v$(DIRSDK_VERSION_DLL_SUFFIX)
LIBLCACHE_LIB = nslchs32v$(DIRSDK_VERSION_DLL_SUFFIX)
else
LIBLDAP_DLL = libldap$(DIRSDK_VERSION_DLL_SUFFIX)$(DLL_PRESUFFIX)
LIBLCACHE_DLL = liblcache$(DIRSDK_VERSION_DLL_SUFFIX)$(DLL_PRESUFFIX)
LIBLCACHE_LIB = liblcache$(DIRSDK_VERSION_DLL_SUFFIX)
LIBLDAP_LIB = libldap$(DIRSDK_VERSION_DLL_SUFFIX)
LIBSSLDAP_LIB = libssldap$(DIRSDK_VERSION_DLL_SUFFIX)
endif

ifdef PRODUCT_IS_DIRECTORY_SERVER
# Get headers and libs from components directory
  LDAP_SDK_LIBLDAP_DLL_DEP	= $(addsuffix .$(DLL_SUFFIX), \
			    $(addprefix $(LDAP_LIBPATH)/, $(LIBLDAP_DLL)))

  ifeq ($(ARCH), WINNT)
    LDAP_SDK_LIBLDAP_DLL	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBPATH)/, $(LIBLDAP_DLL)))
  else
    LDAP_SDK_LIBLDAP_DLL	= -lldap$(DIRSDK_VERSION_DLL_SUFFIX)$(DLL_PRESUFFIX)
  endif

  LDAP_SDK_LIBSSLDAP_LIB_DEP	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBPATH)/, $(LIBSSLDAP_LIB)))

  LDAP_SDK_LIBSSLDAP_LIB	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBPATH)/, $(LIBSSLDAP_LIB)))

  ifneq ($(LDAP_NO_LIBLCACHE),1)
    LDAP_SDK_LIBLCACHE_DLL_DEP	= $(addsuffix .$(DLL_SUFFIX), \
			    $(addprefix $(LDAP_LIBPATH)/, $(LIBLCACHE_DLL)))

    ifeq ($(ARCH), WINNT)
      LDAP_SDK_LIBLCACHE_DLL	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBPATH)/, $(LIBLCACHE_DLL)))
    else
        LDAP_SDK_LIBLCACHE_DLL	= -llcache$(DIRSDK_VERSION_DLL_SUFFIX)$(DLL_PRESUFFIX)
    endif
  endif
else
# Client SDK
  LDAP_SDK_LIBLDAP_DLL_DEP	= $(addsuffix .$(DLL_SUFFIX), \
			    $(addprefix $(LDAP_LIBDIR)/, $(LIBLDAP_DLL)))

  ifeq ($(ARCH), WINNT)
    LDAP_SDK_LIBLDAP_DLL	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBDIR)/, $(LIBLDAP_DLL)))
  else
    LDAP_SDK_LIBLDAP_DLL	= -lldap$(DIRSDK_VERSION_DLL_SUFFIX)$(DLL_PRESUFFIX)
  endif

  LDAP_SDK_LIBSSLDAP_LIB_DEP	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBDIR)/, $(LIBSSLDAP_LIB)))

  LDAP_SDK_LIBSSLDAP_LIB	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBDIR)/, $(LIBSSLDAP_LIB)))

  ifneq ($(LDAP_NO_LIBLCACHE),1)
    LDAP_SDK_LIBLCACHE_DLL_DEP	= $(addsuffix .$(DLL_SUFFIX), \
			    $(addprefix $(LDAP_LIBDIR)/, $(LIBLCACHE_DLL)))

    ifeq ($(ARCH), WINNT)
      LDAP_SDK_LIBLCACHE_DLL	= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(LDAP_LIBDIR)/, $(LIBLCACHE_DLL)))
    else
        LDAP_SDK_LIBLCACHE_DLL	= -llcache$(DIRSDK_VERSION_DLL_SUFFIX)$(DLL_PRESUFFIX)
    endif
  endif
endif

#dboreham: removed LIBLCACHE from the following lines---everybody was
#linking with liblcache, which isn't right.
LDAP_SDK_LIBS_DEP	= $(LDAP_SDK_LIBSSLDAP_LIB_DEP) \
    $(LDAP_SDK_LIBLDAP_DLL_DEP)

LDAP_SDK_LIBS	= $(LDAP_SDK_LIBSSLDAP_LIB) $(LDAP_SDK_LIBLDAP_DLL)

#
# Dynamic and static libraries, BACK-LDBM 
#
ifeq ($(ARCH), WINNT)
LIBBACK_LDBM_DLL = libback-ldbm
LIBBACK_LDBM_LIB = libback-ldbms
else
LIBBACK_LDBM_DLL = libback-ldbm$(DLL_PRESUFFIX)
LIBBACK_LDBM_LIB = libback-ldbm
endif

#
# Dynamic library, BACK-LDIF
#
#ifeq ($(ARCH), WINNT)
LIBBACK_LDIF_DLL = libback-ldif
#else
LIBBACK_LDIF_DLL = libback-ldif$(DLL_PRESUFFIX)
#endif

#
# Dynamic library, REFERINT
#
ifeq ($(ARCH), WINNT)
REFERINT_DLL = referint-plugin
else
REFERINT_DLL = referint-plugin$(DLL_PRESUFFIX)
endif

#
# Dynamic library, SYNTAX
#
ifeq ($(ARCH), WINNT)
SYNTAX_DLL = syntax-plugin
else
SYNTAX_DLL = syntax-plugin$(DLL_PRESUFFIX)
endif

#
# Dynamic library, COLLATION
#
COLLATION_DLL=liblcoll$(DLL_PRESUFFIX)

#
# Dynamic library, NT Synchronization Service plugin
#
NTSYNCH_DLL=ntsynch-plugin$(DLL_PRESUFFIX)

#
# Dynamic library, PASS THROUGH AUTHENTICATION PLUGIN
#
PASSTHRU_DLL = passthru-plugin$(DLL_PRESUFFIX)

#
# Dynamic library, PAM PASS THROUGH AUTHENTICATION PLUGIN
#
PAM_PASSTHRU_DLL = pam-passthru-plugin$(DLL_PRESUFFIX)

#
# Dynamic library, UNIQUE UID CHECKING PLUGIN
#
UID_DLL = attr-unique-plugin$(DLL_RESUFFIX)

# Dynamic library, Replication Plugin
#
REPLICATION_DLL = replication-plugin$(DLL_RESUFFIX)

RETROCL_DLL = retrocl-plugin$(DLL_RESUFFIX)

#
# Dynamic library, ACL PLUGIN
#
ACL_DLL = acl-plugin$(DLL_RESUFFIX)

#
# Dynamic library, TEST-PLUGINS
#
ifeq ($(ARCH), WINNT)
TEST_PLUGIN_DLL = ns-test-plugin
else
TEST_PLUGIN_DLL = libtest-plugin
endif

#
# Dynamic library, PWDSTORAGE
#
ifeq ($(ARCH), WINNT)
PWD_DLL = pwdstorage-plugin
else
PWD_DLL = pwdstorage-plugin$(DLL_PRESUFFIX)
endif

#
# Dynamic library, DISTRIBUTION EXAMPLE
#
ifeq ($(ARCH), WINNT)
DIS_DLL = distrib-plugin
else
DIS_DLL = distrib-plugin$(DLL_PRESUFFIX)
endif

#
# Chaining backend library, CHAINING DATABASE PLUGIN
#
CB_DLL = chainingdb-plugin$(DLL_PRESUFFIX)

#
# Admin server dynamic library location.
#
ifeq ($(ARCH), HPUX)
ADMSONAME=ns-admin.sl
else 
ifeq ($(ARCH), SOLARIS)
ADMSONAME=ns-admin.$(DLL_SUFFIX)
else
ifeq ($(ARCH), AIX)
ADMSONAME=ns-admin$(DLL_PRESUFFIX).$(DLL_SUFFIX)
else
ifeq ($(ARCH), WINNT)
ADMSONAME=ns-admin.$(LIB_SUFFIX)
endif # WINNT
endif # AIX
endif # SOLARIS
endif # HPUX

ifeq ($(BUILD_MODULE), HTTP_ADMIN)
ADMININCLUDEDIR = $(BUILD_ROOT)/include
endif

ifndef ADMSONAME
ADMSONAME=ns-admin.so
endif

ifndef ADMSOLIB
ADMSOLIB = $(BASIC_OBJDIR)-admin/$(ADMSONAME)
endif

#
# Library path
#
ifeq ($(ARCH), WINNT)
LIBPATH=LIBPATH:
else
LIBPATH=L
endif

#
# Web server dynamic library.
#
ifeq ($(ARCH), WINNT)

NSHTTPD_DEP =	$(COMMON_OBJDIR)/$(BUILD_HTTPDLL_NAME).$(LIB_SUFFIX)
NSHTTPD =	/LIBPATH:$(COMMON_OBJDIR) $(BUILD_HTTPDLL_NAME).$(LIB_SUFFIX)
DYN_NSHTTPD=$(NSHTTPD)
NSHTTPD_DLL=$(BUILD_HTTPDLL_NAME)

else

NSHTTPD=$(COMMON_OBJDIR)/$(BUILD_HTTPDLL_NAME).$(DLL_SUFFIX)
NSHTTPD_DEP = $(NSHTTPD)
DYN_NSHTTPD=-L$(COMMON_OBJDIR) -l$(LINK_HTTPDLL_NAME)
NSHTTPD_DLL=$(BUILD_HTTPDLL_NAME)

ifeq ($(ARCH), SOLARIS)

DLLEXPORTS_PREFIX=-Blocal -M

else
ifeq ($(ARCH), SOLARISx86)

DLLEXPORTS_PREFIX=-Blocal -M

else
ifeq ($(ARCH), IRIX)

DLLEXPORTS_PREFIX=-exports_file 

else
ifeq ($(ARCH),HPUX)

else 
ifeq ($(ARCH),AIX)

NSHTTPD = $(COMMON_OBJDIR)/$(BUILD_HTTPDLL_NAME)$(DLL_PRESUF).$(DLL_SUFFIX)
DLLEXPORTS_PREFIX=-bE:
ifdef OLD_AIX_LINKING
	DL=-lsvld
else
	DL=-ldl
# flags added to every link
	PLATFORMLDFLAGS = -brtl
endif

else
ifeq ($(ARCH),OSF1)

DL=

else
ifeq ($(ARCH), Linux)

DL=-ldl

else
ifeq ($(ARCH),ReliantUNIX)

DYN_NSHTTPD=$(NSHTTPD)
DL=-ldl

else
ifeq ($(ARCH),UnixWare)

DYN_NSHTTPD=$(NSHTTPD)
DL=

else
#the previous default
#NSHTTPD=$(NSCP_DISTDIR)/lib/$(BUILD_HTTPDLL_NAME).$(DLL_SUFFIX)
#DYN_NSHTTPD=$(NSHTTPD)
#DL=-ldl
#
#the new default, which is much better when it comes to porting this product
NSHTTPD="you need to edit ldap/nsldap.mk for $(ARCH)"
DYN_NSHTTPD="you need to edit ldap/nsldap.mk for $(ARCH)"
endif # UnixWare
endif # ReliantUNIX
endif # Linux
endif # OSF1
endif # AIX
endif # HPUX
endif # IRIX
endif # SOLARISx86
endif # SOLARIS
endif # WINNT


ADMIN_SECGLUEOBJ=$(BASIC_OBJDIR)-admin/admin-lib/secglue.o

SECGLUEOBJ=$(BUILD_ROOT)/built/$(NS_BUILD_FLAVOR)/httpd-lib/secglue.o
# XXXggood need to pick up the /share/builds versions of the shared libs
# because ones we build here don't appear to be compatible with existing
# shared libs, which are used by admin server.
#SDKROOT = /share/builds/components/ldapsdk/19961107-bad/$(NC_BUILD_FLAVOR)
#SDKLDIR = $(SDKROOT)/lib
#SDKROOT = /share/builds/components/ldapsdk/latest/$(NC_BUILD_FLAVOR)
#SDKLDIR = $(SDKROOT)/lib
SDKROOT = $(OBJDIR)
SDKLDIR = $(SDKROOT)/lib
SDKHDIR = $(SDKROOT)/include
LDB_HDIR = $(LDAP_SRC)/libraries/berkeley_db/PORT/include

#
# Compiler symbol definition 
#
LDAP_REFERRALS=-DLDAP_REFERRALS
SLAPD_BACKENDS=-DLDAP_LDBM -DLDAP_LDIF
LDBMBACKEND=-DLDBM_USE_DBBTREE
SLAPD_PASSWD_HASH=-DSLAPD_PASSWD_SHA1
# all debug and server builds are done with LDAP_DEBUG defined.
# SDK builds pass LDAP_NO_LDAPDEBUG=1 which causes us not to define
# LDAP_DEBUG in optimized builds.
ifneq ($(BUILD_DEBUG), optimize)
LDAP_DEBUG=-DLDAP_DEBUG
else
ifneq ($(LDAP_NO_LDAPDEBUG),1)
LDAP_DEBUG=-DLDAP_DEBUG
endif
endif
NEEDPROTOS=-DNEEDPROTOS
WINSOCK=-DWINSOCK
USE_LOCKF=-DUSE_LOCKF
LDAP_SSLIO_HOOKS=-DLDAP_SSLIO_HOOKS
DBINTERFACE_PRIVATE=-D__DBINTERFACE_PRIVATE
NO_DOMAINNAME=-DNO_DOMAINNAME

ifeq ($(LDAP_NO_LIBLCACHE),1)
NO_LIBLCACHE=-DNO_LIBLCACHE
endif

ifeq ($(BUILD_MODULE), DIRECTORY)
NS_DIRECTORY=-DNS_DIRECTORY
endif

# uncomment this line to use soundex for approximate matches in slapd.
# the default is to use the metaphone algorithm.
#PHONETIC=-DSOUNDEX

#
# uncomment for LDAP over UDP
#CLDAP=-DCLDAP

#
# uncomment for Universty of Michigan specific things.
#UOFM=-DUOFM

#
# uncomment for elimination of local caching support in Libldap
#NO_CACHE=-DNO_CACHE

#
# If you don't want to do auto-translation of character sets, skip this.
#
# Otherwise, uncomment this line and set the following options.
#STR_TRANSLATION=-DSTR_TRANSLATION
#
# remove the defines for LDAP client library T.61 character translation
# you do not need.  If you use LDAP_CHARSET_8859, replace the '1' in "88591"
# with the number of the particular character set you use.  E.g., use "88594"
# if you use the ISO 8859-4 chracter set.
#LIBLDAP_CHARSETS=-DLDAP_CHARSET_8859="88591"
#
# uncomment one these lines to enable automatic T.61 translation by default
#LIBLDAP_DEF_CHARSET=-DLDAP_DEFAULT_CHARSET=LDAP_CHARSET_8859

#
# If you are NOT using Kerberos authentication, you can skip this section.
#
# Otherwise, to enable kerberos authentication, uncomment KERBEROS (and
# AFSKERBEROS if you are running the AFS version of kerberos).  Also
# uncomment and change the various KRB* lines to point to where the
# kerberos libraries and include files are installed at your site.
#
#KERBEROS=-DKERBEROS
#AFSKERBEROS=-DAFSKERBEROS
#KRBINCLUDEFLAG = -I/usr/local/kerberos/include
#KRBLIBFLAG = -L/usr/local/kerberos/lib
#KRBLIBS        = -lkrb -ldes

# General non-Windows compiler options
#
# Passed to every compile (cc or gcc).  This is where you put -O or -g, etc.
ifneq ($(ARCH), WINNT)
ifdef BUILD_OPT
EXTRACFLAGS=-O
else
EXTRACFLAGS=-g
endif
endif

ifeq ($(ARCH), WINNT)
ifeq ($(DEBUG), full)
DSLDDEBUG=/debug
else
ifeq ($(DEBUG), purify)
DSLDDEBUG=/debug
endif
endif
ifndef HEAPAGENT
PDBOPT=/PDB:NONE
endif
endif

#
# SSL-related definitions
#
ifeq ($(SECURITY), export)
SECURITY_EXTN=export
endif
ifeq ($(SECURITY), domestic)
SECURITY_EXTN=us
endif

SSL = -DNET_SSL -DUSE_NSPR_MT
EXTRASSLLIBS = $(LIBARES)

ifeq ($(ARCH), WINNT)
SSLLIBS = $(EXTRASSLLIBS)
endif

ifeq ($(ARCH), WINNT)
LIBNT = $(OBJDIR)/libnt.lib
LIBNT_DEP = $(LIBNT)
endif

# If you are certain that an executable will not be using libsec, include
# the following early in the link command.  secglue.o includes "do nothing"
# shims for most libsec functions.  We do this to reduce our size.
ifeq ($(ARCH), WINNT)
# MLM
# SECGLUE= ns-admin.lib
SECGLUE = $(NSHTTPD) $(OSDEPLIBS) $(LIBNT)
NOSSLLIBS = $(LDAP_LIBDIR)/libldap.lib \
	    $(SSLLIBS) $(ALIBS) 
else
# $(ARCH) != WINNT
ifeq ($(ARCH), Linux)
# XXXsspitzer: all gcc platforms will have to do this
SECGLUE= $(SECGLUEOBJS) $(DYN_NSHTTPD)
#                                        $(LIBARES)
else # Linux
SECGLUE= $(SECGLUEOBJS) $(DYN_NSHTTPD) 
#                                        $(LIBARES)
endif # Linux
NOSSLLIBS = $(LDAP_SDK_LIBLDAP_DLL) $(LDAP_SDK_LIBLCACHE_DLL)\
     $(SECGLUE) $(ALIBS)
endif

ifeq ($(BUILD_DLL), yes)
STATIC_SECDEPS= $(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(COMMON_OBJDIR)/lib/lib, \
			    $(LIBADMIN) $(FRAME) $(LIBACCESS) $(CRYPT))) \
			    $(LIBSEC) $(LIBNSPR)

DYNAMIC_DEPLIBS=$(LDAP_COMMON_LIBS) 
DYNAMIC_DEPLINK=$(DYNAMIC_DEPLIBS)
else
STATIC_DEPLIBS=$(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(OBJDIR)/lib/lib, \
			    $(LIBADMIN) $(FRAME) $(LIBACCESS) $(CRYPT))) \
			    $(LIBNSPR)

STATIC_SECDEPS=$(addsuffix .$(LIB_SUFFIX), \
			    $(addprefix $(OBJDIR)/lib/lib, \
			    $(LIBADMIN) $(FRAME) $(LIBACCESS) $(CRYPT))) \
			    $(LIBSEC) $(LIBNSPR)

DYNAMIC_DEPLIBS=$(LDAP_COMMON_LIBS)
DYNAMIC_DEPLINK=$(LDAP_COMMON_LIBS)
endif

ifndef DEPLIBS
DEPLIBS = $(DYNAMIC_DEPLIBS)
DEPLINK = $(DYNAMIC_DEPLINK)
ifeq ($(ARCH), WINNT)
SECDEPS = $(DEPLIBS) $(SECGLUE) $(XP_OBJS)
else
SECDEPS = $(STATIC_SECDEPS)
endif
SECLINK = $(SECDEPS)
endif

HTMLDEFS=-DPRODUCT_NAME=$(PRODUCT) -D$(ARCH) -DARCH=$(PRETTY_ARCH)

#
# Windows NT platform-specifics
#
ifeq ($(ARCH), WINNT)

PLATFORM_INCLUDE = -I$(BUILD_ROOT)/include/nt \
    -I$(LDAP_SRC)/libraries/libutil

SYSERRLIST_IN_STDIO=-DSYSERRLIST_IN_STDIO

endif # WINNT

ifeq ($(ARCH), SOLARIS)
#
# SunOS5 platform-specifics
#

PLATFORM=sunos5

# ranlib not needed under SunOS5
RANLIB	= true

# be explicit about which CC to use
CC=cc -v

# gie full path to hostname since it may not be in user's path
HOSTNAME=/usr/ucb/hostname

# don't count on /usr/ucb/install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS= -KPIC

# Extra linker options needed when creating shared libraries
DYNALIBS=

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-R

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
LDRPATHFLAG_PREFIX=-R

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX)libldap.so
SONAMEFLAG_PREFIX=-h

THREADS= -DTHREAD_SUNOS5_LWP
PLAT_ADMCFLAGS=	-DSVR4 -DSOLARIS
PLAT_ADMLIBS= 
PLATFORMCFLAGS=	-D$(PLATFORM) -D_REENTRANT -DSVR4
PLATFORMLIBS= -lresolv -lsocket -lnsl -lgen -ldl -lposix4 -lw
THREADS= -DTHREAD_SUNOS5_LWP
THREADSLIB=-lthread
endif # SOLARIS

ifeq ($(ARCH), SOLARISx86)
#
# Solaris x86 platform-specifics
#

PLATFORM=sunos5x86

# ranlib not needed under sunos5x86
RANLIB	= true

# be explicit about which CC to use
CC=cc
#CC=gcc

# give full path to hostname since it may not be in user's path
HOSTNAME=/usr/ucb/hostname

# don't count on /usr/ucb/install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
ifeq ($(CC), cc)
SLCFLAGS= -KPIC
else
SLCFLAGS= -fPIC
endif

# Extra linker options needed when creating shared libraries
DYNALIBS=

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-R,

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
LDRPATHFLAG_PREFIX=-R

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX)libldap.so
SONAMEFLAG_PREFIX=-h

THREADS= -DTHREAD_SUNOS5x86_LWP
PLAT_ADMCFLAGS=	-DSVR4 -DSOLARISx86 -DSOLARIS
PLAT_ADMLIBS= 
PLATFORMCFLAGS=	-D$(PLATFORM) -D_REENTRANT -DSVR4
PLATFORMLIBS= -lresolv -lsocket -lnsl -lgen -ldl -lposix4 -lw
THREADS= -DTHREAD_SUNOS5x86_LWP
THREADSLIB=-lthread
endif # SOLARISx86

ifeq ($(ARCH), SUNOS4)
#
# SunOS 4 platform-specifics
#

5LINT	= /usr/5bin/lint

PLATFORMCFLAGS= -Dsunos4
THREADS= -DTHREAD_SUNOS4_LWP
THREADSLIB=-llwp

#
# the SunOS 4 cc compiler doesn't understand function prototypes, so we
# need the unproto preprocessor
#
NEEDUNPROTO=yes
UNPROTOCFLAGS=-Qpath $(LDAP_SRC)/build/unproto
endif # SUNOS4

ifeq ($(ARCH), IRIX)
#
# IRIX platform-specifics
#

PLAT_ADMCFLAGS=	-DSVR4 -DIRIX
PLAT_ADMLIBS= 

PLATFORM=irix
# ranlib not needed under IRIX
RANLIB	= true

# be explicit about which CC to use
CC=cc

# give full path to hostname since it may not be in user's path
HOSTNAME=/usr/bsd/hostname

# don't count on a BSD install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-Wl,-rpath,

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
LDRPATHFLAG_PREFIX=-rpath

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX)libldap.so
# Note that the definition below includes a trailing space.
SONAMEFLAG_PREFIX=-soname 

PLATFORMCFLAGS=-DUSE_WAITPID -D$(PLATFORM)
PLATFORMLIBS=
THREADS= -DTHREAD_SGI_SPROC
THREADSLIB=

endif # IRIX

ifeq ($(ARCH), OSF1)

#
# OSF1 platform-specifics
#

PLATFORM=OSF1

# Even though prototypes are supported by the compiler, OSF's CC doesn't
# seem to define __STDC__ so we explicitly defined NEEDPROTOS here.
PLATFORMCFLAGS= -D$(PLATFORM) -DNEEDPROTOS -D_REENTRANT
PLATFORMLIBS=
THREADS= -DTHREAD_DCE_PTHREADS
THREADSLIB= -lpthread

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-Wl,-rpath,

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
LDRPATHFLAG_PREFIX=-rpath

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX)libldap.so
# Note that the definition below includes a trailing space.
SONAMEFLAG_PREFIX=-soname 

# the BSD-like install under OSF/1 is called installbsd
# INSTALL=installbsd
# use this shell script, instead of installbsd.  
INSTALL=$(LDAP_SRC)/build/install.sh

endif # OSF1

ifeq ($(ARCH), AIX)

#
# AIX platform-specifics
#

PLAT_ADMCFLAGS= -DAIX 
PLAT_ADMLIBS=

PLATFORM=aix

# ranlib for aix 
RANLIB=ranlib

# install with BSD semantics
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags to set runtime shared library search path.  For example:
# $(CC) $(RPATHFLAG_PREFIX)../..$(RPATHFLAG_EXTRAS)
RPATHFLAG_PREFIX=-blibpath:
RPATHFLAG_EXTRAS=:/usr/lib:/lib

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
LDRPATHFLAG_PREFIX=-blibpath:/usr/lib:/lib:

PLATFORMCFLAGS=	-D_AIX32_CURSES -DUSE_PTHREADS -DHW_THREADS \
    -DUSE_REENTRANT_LIBC -Daix -Dunix

PLATFORMLIBS= 
THREADS= -DTHREAD_AIX_PTHREADS
#SECGLUE= moresecglue.o \
#        $(BUILD_ROOT)/nspr/src/$(NC_BUILD_FLAVOR)/longlong.o \
#        $(BUILD_ROOT)/nspr/src/$(NC_BUILD_FLAVOR)/prprf.o

# JCM - Use -bnoquiet to find out which symbols can't be resolved.
DLL_LDFLAGS= -bexpall -brtl -bM:SRE -bnoentry \
    -L.:/usr/lib/threads:/usr/lpp/xlC/lib:/usr/lib:/lib
DLL_EXTRA_LIBS= -bI:/usr/lib/lowsys.exp -lC_r -lC -lpthreads -lc_r -lm \
    /usr/lib/libc.a

ifdef OLD_AIX_LINKING
EXE_EXTRA_LIBS= -bI:/usr/lib/syscalls.exp -lsvld -lpthreads 
else
EXE_EXTRA_LIBS= -bI:/usr/lib/syscalls.exp -ldl -lpthreads 
endif

endif # AIX

ifeq ($(ARCH), HPUX)
#
# HP-UX platform-specifics
#

CC=cc
PLATFORM=hpux

# ranlib not needed under HP-UX
RANLIB  = true

# install under HP-UX is not like on BSD systems, so we use our own script
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS=+Z

# we need to link a separate library to get ndbm routines under HP/UX
LDBMLIB=-lndbm

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-Wl,+s,+b,

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
LDRPATHFLAG_PREFIX=-Wl,+s,+b,

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX) libldap.so
SONAMEFLAG_PREFIX=-Wl,+h

# we need to link in the V3 library to get sigset()
# 07/03/02 - no longer needed - version 6.1
# PLATFORMLIBS= -lV3

# -Ae means 'enforce ansi BUT allow the use of long-long'.  we need this
# for 64-bit file support.
PLATFORMCFLAGS= -Dhpux -D$(PLATFORM) -D_HPUX_SOURCE -D_REENTRANT -Ae

#aCC doesn't recognize -Ae so this will be used with aCC
ACC_PLATFORMCFLAGS= -Dhpux -D$(PLATFORM) -D_HPUX_SOURCE -D_REENTRANT

endif # HPUX

# UNIXWARE || UnixWare
ifeq ($(subst nix,NIX,$(subst are,ARE,$(ARCH))), UNIXWARE)
#
# LDAP SVR4 standard cc Make-platform file
# Uses the std SVR4 stuff whenever possible.
# Some references to the BSD compatibility required.
# "bsdcompat" is an optional package, but we need it installed for other builds
#

#
# add any platform-specific overrides below here
#

# compiler to use, e.g. CC=cc or CC=gcc
ifndef CC
CC = cc
endif

# give full path to hostname since it may not be in user's path
HOSTNAME=/usr/ucb/hostname

# don't count on /usr/ucb/install being present or first in path
# INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS= -Kpic

# Flags required to cause linker to create a shared library
DYNAFLAGS= -G

# Extra linker options needed then creating shared libraries
DYNALIBS= -ldl

# Filename extension for shared libraries
DYNAEXT=so

# ndbm library, needed if not in libc (e.g. LDBMLIB=-lndbm)
LDBMLIB = -L/usr/ucblib -ldbm

# BSD-like install command; if necessary, you can use a script
INSTALL = /usr/ucb/install

# command to convert libraries for efficient random access;
RANLIB = true

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX) libldap.so
SONAMEFLAG_PREFIX=-h

USE_LD_RUN_PATH=true

# other commands  - see the file build/Make-append for a list
endif #UNIXWARE || UnixWare

ifeq ($(ARCH), UNIXWARE)

PLAT_ADMCFLAGS=  -DUNIXWARE -DSVR4 -DSYSV

# flags added to every compile
PLATFORMCFLAGS= -DUNIXWARE -DSYSV -DSVR4

# flags added to every link
PLATFORMLDFLAGS =

# extra libraries needed (added to the end of all link commands)
PLATFORMLIBS =  -lsocket -lnsl -lresolv -lgen

# other commands  - see the file build/Make-append for a list
endif #UNIXWARE

ifeq ($(ARCH), UnixWare)
# Gemini: UnixWare7 (SVR5), or UNIXWARE2.1.x (SVR4) with the UDK
SYSV_REL := $(shell $(BUILD_ROOT)/nsarch -f | sed 's/UnixWare //')
ifeq ($(SYSV_REL),5)
PLAT_ADMCFLAGS= -DUnixWare -DSVR5 -DSYSV
PLATFORMCFLAGS= -DUnixWare -DSYSV -DSVR5
else
PLAT_ADMCFLAGS= -DUNIXWARE -DSVR4 -DSYSV
PLATFORMCFLAGS= -DUNIXWARE -DSYSV -DSVR4
endif

# flags added to every link
PLATFORMLDFLAGS =

# extra libraries needed (added to the end of all link commands)
PLATFORMLIBS =  -lsocket -lnsl -lresolv -lgen

# other commands  - see the file build/Make-append for a list
endif #UnixWare

ifeq ($(ARCH), SCOOS)
#
# LDAP SVR4 standard cc Make-platform file
# Uses the std SVR4 stuff whenever possible.
# Some references to the BSD compatibility required.
#

#
# add any platform-specific overrides below here
#

# ranlib not needed under SCOOS
RANLIB  = true

USE_LD_RUN_PATH=true

CC= cc -b elf -KPIC -DSCO -DSCOOS

# don't count on /usr/ucb/install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS= -Kpic

PLATFORMCFLAGS=  -DSCO_SV -DSYSV -DHAVE_STRERROR -DSW_THREADS -DSCO_PM -DSCO -Dsco -DSCOOS

PLATFORMLIBS= -lsocket 

#-lnsl -ldl -lpmapi -lc -lPW

EXTRA_LIBS= -lsocket 
#-lnsl -ldl -lpmapi -lc -lPW

endif # SCOOS

ifeq ($(ARCH), NCR)
#
# LDAP SVR4 standard cc Make-platform file
# Uses the std SVR4 stuff whenever possible.
# Some references to the BSD compatibility required.
#

#
# add any platform-specific overrides below here
#

# compiler to use, e.g. CC=cc or CC=gcc
ifndef CC
CC = gcc
endif

# give full path to hostname since it may not be in user's path
HOSTNAME=/usr/ucb/hostname

# don't count on /usr/ucb/install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS= -fpic

PLAT_ADMCFLAGS=  -DNCR -Di386 -DSVR4 -DSYSV -DHAVE_STRERROR -DSW_THREADS

# flags added to every compile
PLATFORMCFLAGS=  -DNCR -Di386 -DSVR4 -DSYSV -DHAVE_STRERROR -DSW_THREADS 

# flags added to every link
PLATFORMLDFLAGS =

# extra libraries needed (added to the end of all link commands)
PLATFORMLIBS =  -lsocket -lnsl -lgen 

EXTRA_LIBS = -lsocket -lnsl -lgen -ldl -lc /usr/ucblib/libucb.a

# Flags required to cause linker to create a shared library
DYNAFLAGS= -G

# Extra linker options needed then creating shared libraries
DYNALIBS= -ldl

# Filename extension for shared libraries
DYNAEXT=so

# ndbm library, needed if not in libc (e.g. LDBMLIB=-lndbm)
LDBMLIB = -L/usr/ucblib -ldbm

# command to convert libraries for efficient random access;
RANLIB = true

USE_LD_RUN_PATH=true

# other commands  - see the file build/Make-append for a list
endif #NCR

ifeq ($(ARCH), ReliantUNIX)
#
# ReliantUNIX platform-specifics
#
PLATFORM=reliantunix

# ranlib not needed under ReliantUNIX
RANLIB  = true

# be explicit about which CC to use
CC=cc

# gie full path to hostname since it may not be in user's path
HOSTNAME=/usr/ucb/hostname

# don't count on /usr/ucb/install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS=

# Extra linker options needed when creating shared libraries
DYNALIBS=

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-Wl,-R,
USE_LD_RUN_PATH=true

# flag to pass to ld to set a shared library's "internal name"
# this is used like this, for example: $(SONAMEFLAG_PREFIX)libldap.so
SONAMEFLAG_PREFIX=-h

THREADS= -DTHREAD_SUNOS5_LWP
PLAT_ADMCFLAGS= -DSVR4 -DSNI -DRELIANTUNIX
PLAT_ADMLIBS=
PLATFORMCFLAGS= -D$(PLATFORM) -DSVR4 -DSNI -DRELIANTUNIX
#libc_r.so.1 for strtok_r? talk to ckaiser.  maybe libsni_r.a?
#right now, check out ns/nspr20/pr/include/md/_reliantunix.cfg and
#ns/nspr20/pr/src/md/unix/reliantunix.c 
PLATFORMLIBS= -lresolv -lsocket -lnsl -lgen -ldl
THREADS=
THREADSLIB=

endif # ReliantUNIX

ifeq ($(ARCH), Linux)
#
# add any platform-specific overrides below here
#

# compiler to use, e.g. CC=cc or CC=gcc
CC=/usr/bin/gcc -fwritable-strings

# give full path to hostname since it may not be in user's path
HOSTNAME=/bin/hostname

# don't count on /usr/ucb/install being present or first in path
INSTALL=$(LDAP_SRC)/build/install.sh

# flag to pass to cc when linking to set runtime shared library search path
# this is used like this, for example:   $(RPATHFLAG_PREFIX)../..
RPATHFLAG_PREFIX=-Wl,-rpath,

# flag to pass to ld when linking to set runtime shared library search path
# this is used like this, for example:   $(LDRPATHFLAG_PREFIX)../..
# note, there is a trailing space
LDRPATHFLAG_PREFIX=-rpath 

# Flags required to cause compiler to generate code suitable for use in
# a shared library.
SLCFLAGS= -fpic

PLAT_ADMCFLAGS=  -DLINUX -DLINUX2_0 -DLINUX2_2 -DLinux
 
# flags added to every compile
PLATFORMCFLAGS=  -DLINUX -DLINUX2_0 -DLINUX2_2 -DLinux

# flags added to every link
PLATFORMLDFLAGS =

# extra libraries needed (added to the end of all link commands)
PLATFORMLIBS =  

EXTRA_LIBS = -ldl

# Flags required to cause linker to create a shared library
DYNAFLAGS= -shared

# Extra linker options needed then creating shared libraries
DYNALIBS= -ldl

# Filename extension for shared libraries
DYNAEXT=so

# ndbm library, needed if not in libc (e.g. LDBMLIB=-lndbm)
LDBMLIB = -L/usr/ucblib -ldbm

# command to convert libraries for efficient random access;
RANLIB = ranlib

# other commands  - see the file build/Make-append for a list
endif # Linux

#
# DEFS are included in CFLAGS
#
DEFS	= $(PLATFORMCFLAGS) $(LDAP_DEBUG) $(KERBEROS) $(AFSKERBEROS) \
	    $(UOFM) $(NO_USERINTERFACE) $(CLDAP) $(NO_CACHE) $(DBDEFS) \
	    $(LDAP_REFERRALS) $(LDAP_DNS) $(STR_TRANSLATION) \
	    $(LIBLDAP_CHARSETS) $(LIBLDAP_DEF_CHARSET) \
	    $(SLAPD_BACKENDS) $(LDBMBACKEND) $(LDBMINCLUDE) $(PHONETIC) \
	    $(SLAPD_PASSWD_HASH) $(LDAP_SSLIO_HOOKS) $(DBINTERFACE_PRIVATE) \
	    $(NO_LIBLCACHE) $(SYSERRLIST_IN_STDIO) \
	    $(NS_DIRECTORY)

ifeq ($(ARCH), WINNT)
DEFS += $(NEEDPROTOS) $(NO_DOMAINNAME) 
endif

# DEFS += $(USE_LOCKF)

# ACFLAGS are added to CFLAGS but not passed to mkdep, lint, etc
ACFLAGS		= $(EXTRACFLAGS) $(UNPROTOCFLAGS)

# ALDFLAGS are always placed near the beginning of all linker (cc -o) commands
ifneq ($(ARCH), WINNT)
# Passed to every link (ld).  Include -g here if you did in EXTRACFLAGS.
EXTRALDFLAGS=-$(LIBPATH)$(LDAP_LIBDIR)
#EXTRALDFLAGS=-$(LIBPATH)$(LDAP_LIBDIR) -$(LIBPATH)$(LDAP_LIBPATH)
endif

ifeq ($(ARCH), IRIX)
 ifeq ($(USE_N32), 1)
  PLATFORMLDFLAGS=-n32 -mips3
 endif
endif

ALDFLAGS	= $(EXTRALDFLAGS) $(PLATFORMLDFLAGS)

# ALIBS are always placed at the end of all linker (cc -o) commands
ALIBS		= $(PLATFORMLIBS)

INCLUDES += -I$(LDAP_HDIR) $(PLATFORM_INCLUDE) -I$(DIRVERDIR)

CFLAGS	+=	$(DEFS) $(ACFLAGS) $(INCLUDES)

# default definitions for utilities

ifneq ($(ARCH), WINNT)
SHELL	= /bin/sh
endif

AR	= ar cq
RM	= rm -f
MV	= mv -f

CP	= cp

CHMOD	= chmod
CAT	= cat

ifneq ($(ARCH), WINNT)
LN	= ln -s
HARDLN	= ln
endif

TAIL	= tail.exe
SED	= sed
LINT	= lint
5LINT	= lint
MKDIR	= mkdir -p

ifneq ($(ARCH), WINNT)
ifndef RANLIB
RANLIB = ranlib
endif
ifndef INSTALL
INSTALL	= install
endif
ifndef INSTALLFLAGS
INSTALLFLAGS = -c
endif
ifndef USE_LD_RUN_PATH
ifndef RPATHFLAG_PREFIX
RPATHFLAG_PREFIX="XXX Please define a platform-specific RPATHFLAG_PREFIX in nsldap.mk XXX"
endif
ifndef LDRPATHFLAG_PREFIX
LDRPATHFLAG_PREFIX="XXX Please define a platform-specific LDRPATHFLAG_PREFIX in nsldap.mk XXX"
endif
endif
BASENAME= basename
DIRNAME	= dirname
else
INSTALL = cp.exe -prv
RM	= rm.exe -rf
MV	= mv.exe -f

CP	= cp.exe -prv
LN	= cp.exe -prv
HARDLN	= cp.exe -prv

CHMOD	= chmod
CAT	= cat.exe
MKDIR	= mkdir.exe -p
endif

MKDEP	= $(LDAP_SRC)/build/mkdep -s -f Make-template
PWD	= pwd
DATE	= date
HOSTNAME= hostname

#
# Compiler output file
#
ifeq ($(ARCH), WINNT)
EXE_SUFFIX=.exe
RSC=rc
OFFLAG=/Fo
else 
OFFLAG=-o 
endif


#
# XXX: does anyone know of a better way to solve the "LINK_LIB2" problem? -mcs
#
# Link to produce a console/windows exe on Windows
#
ifeq ($(ARCH), WINNT)
LINK_EXE	= link -OUT:"$@" $(USE_MAP) $(ALDFLAGS) $(LDFLAGS) $(ML_DEBUG) \
    $(LCFLAGS) /NOLOGO $(PDBOPT) /DEBUGTYPE:BOTH /INCREMENTAL:NO \
    /SUBSYSTEM:$(SUBSYSTEM) $(DEPLIBS) $(EXTRA_LIBS) $(OBJS)
LINK_EXE_NOLIBSOBJS	= link -OUT:"$@" $(USE_MAP) $(ALDFLAGS) $(LDFLAGS) \
    $(ML_DEBUG) $(LCFLAGS) /NOLOGO $(PDBOPT) /DEBUGTYPE:BOTH /INCREMENTAL:NO \
    /SUBSYSTEM:$(SUBSYSTEM)
LINK_LIB	= lib -OUT:"$@"  $(OBJS)
LINK_LIB2	= lib -OUT:"$@"  $(OBJS2)
LINK_DLL	= link /nologo $(USE_MAP) /DLL $(PDBOPT) /DEBUGTYPE:BOTH \
	$(ML_DEBUG) /SUBSYSTEM:WINDOWS $(LLFLAGS) $(DLL_LDFLAGS) \
	$(EXTRA_LIBS) /out:"$@" $(OBJS)
LINK_DLL2	= link /nologo $(USE_MAP) /DLL $(PDBOPT) /DEBUGTYPE:BOTH \
	$(ML_DEBUG) /SUBSYSTEM:WINDOWS $(LLFLAGS) $(DLL_LDFLAGS) \
	$(EXTRA_LIBS) /out:"$@" $(OBJS2)
else # WINNT
#
# UNIX link commands
#
LINK_LIB	= $(RM) $@; $(AR) $@ $(OBJS); $(RANLIB) $@
LINK_LIB2	= $(RM) $@; $(AR) $@ $(OBJS2); $(RANLIB) $@
ifeq ($(ARCH), OSF1)
DLL_LDFLAGS += $(LDRPATHFLAG_PREFIX) $(RPATHFLAG)$(RPATHFLAG_EXTRAS)
else
DLL_LDFLAGS += $(RPATHFLAG_PREFIX)$(RPATHFLAG)$(RPATHFLAG_EXTRAS)
endif
ifdef SONAMEFLAG_PREFIX
LINK_DLL	= $(LD) $(ALDFLAGS) $(ARCH_CFLAGS) $(DLL_LDFLAGS) $(DLL_EXPORT_FLAGS) \
			-o $@ $(SONAMEFLAG_PREFIX)$(notdir $@) $(OBJS)
LINK_DLL2	= $(LD) $(ALDFLAGS) $(DLL_LDFLAGS) $(DLL_EXPORT_FLAGS2) \
			-o $@ $(SONAMEFLAG_PREFIX)$(notdir $@) $(OBJS2)
else # SONAMEFLAG_PREFIX
LINK_DLL	= $(LD) $(ALDFLAGS) $(DLL_LDFLAGS) $(DLL_EXPORT_FLAGS) \
			-o $@ $(OBJS)
LINK_DLL2	= $(LD) $(ALDFLAGS) $(DLL_LDFLAGS) $(DLL_EXPORT_FLAGS2) \
			-o $@ $(OBJS2)
endif # SONAMEFLAG_PREFIX

ifeq ($(ARCH), HPUX)
# On HPUX, we need a couple of changes:
# 1) Use the C++ compiler for linking, which will pass the +eh flag on down to the
#    linker so the correct exception-handling-aware libC gets used (libnshttpd.sl
#    needs this).
# 2) Add a "-Wl,-E,-N" option so the linker gets a "-E,-N" flag.  This makes symbols
#    in an executable visible to shared libraries loaded at runtime and makes ns-slapd
#    'normal executable' instead of 'shared executable'.
DS_LINKEXE_EXTRA_FLAGS=-Wl,-E,-N,+k,+vshlibunsats
LD=$(CXX)

else
ifeq ($(ARCH), OSF1)
DS_LINKEXE_EXTRA_FLAGS=-taso
else
ifeq ($(ARCH), IRIX)
DS_LINKEXE_EXTRA_FLAGS=-exceptions
endif # IRIX
endif # OSF
endif # HPUX

# Define an assortment of UNIX LINK_EXE macros.
DS_LINKEXE_FLAGS=$(DS_LINKEXE_EXTRA_FLAGS) $(ALDFLAGS) $(LDFLAGS)
ifdef USE_LD_RUN_PATH
#does RPATH differently.  instead we export RPATHFLAG as LD_RUN_PATH
export LD_RUN_PATH=$(RPATHFLAG)
else # USE_LD_RUN_PATH
DS_LINKEXE_FLAGS += $(RPATHFLAG_PREFIX)$(RPATHFLAG)$(RPATHFLAG_EXTRAS)
endif # USE_LD_RUN_PATH

LINK_EXE					= $(CXX) $(DS_LINKEXE_FLAGS) -o $@ \
									$(OBJS) $(EXTRA_LIBS)
LINK_EXE_NOLIBSOBJS			= $(CXX) $(DS_LINKEXE_FLAGS) -o $@
endif # WINNT

#
# Path to platform-specific directory for berkeley db
#
ifeq ($(ARCH), SOLARIS)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/sunos.5.2
else
ifeq ($(ARCH), IRIX)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/irix.5.3
else
ifeq ($(ARCH), AIX)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/aix.4.2
else
ifeq ($(ARCH), OSF1)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/osf.2.0
else
ifeq ($(ARCH), HPUX)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/hpux.9.01
else
ifeq ($(ARCH), WINNT)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/winnt3.51
else
# UNIXWARE || UnixWare
ifeq ($(subst nix,NIX,$(subst are,ARE,$(ARCH))), UNIXWARE)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/unixware.2.1
else
ifeq ($(ARCH), SCOOS)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/scoos.5.0
else
ifeq ($(ARCH), NCR)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/ncr.3.0
else
ifeq ($(ARCH), SOLARISx86)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/sunosx86.5.2
else
ifeq ($(ARCH), ReliantUNIX)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/reliantunix.5.4
else
ifeq ($(ARCH), Linux)
LIBDB_MAKEDIR=$(LDAP_SRC)/libraries/berkeley_db/PORT/linux.2.0
else
LIBDB_MAKEDIR=XXX_UNDEFINED_XXX
endif # Linux
endif # ReliantUNIX
endif # SOLARISx86
endif # NCR
endif # SCOOS
endif # UnixWare || UNIXWARE
endif # WINNT
endif # HPUX
endif # OSF1
endif # AIX
endif # IRIX
endif # SOLARIS

#
# Add platform-specific include directory
#
# dboreham: this is bogus, take it out
ifdef LDAP_USE_OLD_DB
INCLUDES += -I$(LIBDB_MAKEDIR)/include
endif

#Changes required for ACL
ACLINC = $(BUILD_ROOT)/include/libaccess
#ACLDIR = -$(LIBPATH)$(LDAP_LIBDIR)
ACLLIB = -laccess -lbase -lsi18n
# end of changes

