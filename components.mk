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
# this file contains definitions for component macros used during the build
# process.  Things like the component location in the build tree, etc.
# this file should be included by nsconfig.mk after it figures out all
# of the OS, architecture, security, and other platform and build related
# macros.  This file also contains the instructions for making the component
# up to date e.g. copying the files from their repository to an area where
# the build process has access to it.  For some components and OS's, this may
# be as simple as creating a symbolic link to the repository

# Each component should define a COMPONENT_DEP macro which can be used in
# other makefiles for dependency checking e.g.
# target: $(COMPONENT1_DEP) $(COMPONENT2_DEP) ...
# This macro should evaluate to the name of a single file which must be
# present for the package to be complete e.g. some library or include
# file name
# Each component then should define a target for that dependency which will
# bring the component up to date if that target does not exist e.g.
# $(COMPONENT1_DEP):
#	use ftp or symlinks or ??? to get the necessary files to the build
#	area

# Each component should define a COMPONENT_LINK macro which can be used to
# link the component's libraries with the target.  For NT, this will typically
# be something like
# /LIBPATH:path_to_library lib1 lib2 lib3 /LIBPATH:more_libs lib4 lib5 ...
# On Unix, this will be something like
# -Lpath_to_library -l1 -l2 -l3 -Lmore_libs -l4 -l5 ...

# Each component should define a COMPONENT_INCLUDE macro which can be used
# to compile using the component's header files e.g.
# -Ipath_to_include_files -Ipath_to_more_include_files

# Once this file is working, I will DELETE compvers.sh and ns_ftp.sh
# from the tree, so help me god.

# this macro contains a list of source files and directories to copy to
# the directory where DLLs/SOs go at runtime; each component will add the files/dirs to
# this macro that it needs to package; not all components will have
# files which need packaging
# if you need some other behavior, see PACKAGE_SRC_DEST below
LIBS_TO_PKG =

# this macro contains a list of source files and directories to copy to
# the directory where DLLs/SOs go at runtime; each component will add the files/dirs to
# this macro that it needs to package; this is for DLLs/SOs for the shared/bin
# directory where the ldap c sdk command line tools, some security tools, and
# the i18n conversion tools live
LIBS_TO_PKG_SHARED = 

# this macro contains a list of source files and directories to copy to
# the shared tools directory - things like the ldap c sdk command line
# tools, shared security tools, etc.
BINS_TO_PKG_SHARED =

# this macro contains a list of shared libraries/dlls needed during
# setup to run the setup pre-install program on unix (ns-config) or
# the slapd plugin on NT (DSINST_PreInstall)
PACKAGE_SETUP_LIBS =

# this macro contains a list of libraries/dlls to copy to the clients
# library directory
LIBS_TO_PKG_CLIENTS =

# this macro contains a list of source files and directories to copy to
# the release/java directory; usually a list of jar files
PACKAGE_UNDER_JAVA =

# this macro contains a list of pairs of source and dest files and directories
# the source is where to find the item in the build tree, and the dest is
# the place in the release to put the item, relative to the server root e.g.
# nls locale files are in libnls31/locale, but for packaging they need to
# go into lib/nls, not just lib; the destination should be a directory name;
# separate the src from the dest with a single space
PACKAGE_SRC_DEST =

# these defs are useful for doing pattern search/replace
COMMA := ,
NULLSTRING :=
SPACE := $(NULLSTRING) # the space is between the ) and the #

ifeq ($(ARCH), WINNT)
EXE_SUFFIX = .exe
else # unix - windows has no lib name prefix, except for nspr
LIB_PREFIX = lib
endif

ifeq ($(INTERNAL_BUILD), 1)
include $(BUILD_ROOT)/internal_buildpaths.mk
else
include $(BUILD_ROOT)/buildpaths.mk
endif

# NSPR20 Library
NSPR_LIBNAMES = plc4 plds4
ifeq ($(ARCH), SOLARIS)
  ifeq ($(NSPR_RELDATE), v4.2.2)
# no need after v4.4.1
NSPR_LIBNAMES += ultrasparc4 
# just need ultrasparc for now
LIBS_TO_PKG += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(NSPR_LIBPATH)/lib,ultrasparc4))
  endif
endif
NSPR_LIBNAMES += nspr4
ifdef NSPR_SOURCE_ROOT
  NSPR_LIBPATH = $(NSPR_SOURCE_ROOT)/dist/$(MOZ_OBJDIR_NAME)/lib
  NSPR_INCDIR = $(NSPR_SOURCE_ROOT)/dist/$(MOZ_OBJDIR_NAME)/include
else
  NSPR_LIBPATH = $(NSPR_BUILD_DIR)/lib
  NSPR_INCDIR = $(NSPR_BUILD_DIR)/include
endif
NSPR_INCLUDE = -I$(NSPR_INCDIR)
NSPR_LIBS_TO_PKG = $(addsuffix .$(DLL_SUFFIX),$(addprefix $(NSPR_LIBPATH)/lib,$(NSPR_LIBNAMES)))

LIBS_TO_PKG += $(NSPR_LIBS_TO_PKG)
LIBS_TO_PKG_SHARED += $(NSPR_LIBS_TO_PKG) # needed for cmd line tools
ifeq ($(USE_SETUPUTIL), 1)
  PACKAGE_SETUP_LIBS += $(NSPR_LIBS_TO_PKG)
endif
ifeq ($(USE_DSGW), 1)
  LIBS_TO_PKG_CLIENTS += $(NSPR_LIBS_TO_PKG) # for dsgw
endif

ifeq ($(ARCH), WINNT)
  NSPRDLL_NAME = $(addprefix lib, $(NSPR_LIBNAMES))
  NSPROBJNAME = $(addsuffix .lib, $(NSPRDLL_NAME))
  NSPRLINK = /LIBPATH:$(NSPR_LIBPATH) $(NSPROBJNAME)
  LIBNSPRDLL_NAMES = $(addsuffix .dll, $(addprefix $(NSPR_LIBPATH)/, \
	$(addprefix lib, $(NSPR_LIBNAMES))))
else
  NSPR_SOLIBS = $(addsuffix .$(DLL_SUFFIX),  $(addprefix $(LIB_PREFIX), $(NSPR_LIBNAMES)))
  NSPROBJNAME = $(addsuffix .a, $(addprefix $(LIB_PREFIX), $(NSPR_LIBNAMES))
  LIBNSPR = $(addprefix $(NSPR_LIBPATH)/, $(NSPR_SOLIBS))
  NSPRLINK = -L$(NSPR_LIBPATH) $(addprefix -l, $(NSPR_LIBNAMES))
endif

### DBM #############################

ifdef DBM_SOURCE_ROOT
  DBM_LIBPATH = $(DBM_SOURCE_ROOT)/dist/$(MOZ_OBJDIR_NAME)/lib
  DBM_INCDIR = $(DBM_SOURCE_ROOT)/dist/public/dbm
else
  DBM_LIBPATH = $(DBM_BUILD_DIR)/lib
  DBM_INCDIR = $(DBM_BUILD_DIR)/include
endif
DBM_INCLUDE = -I$(DBM_INCDIR)
DBM_LIBNAMES = dbm

ifeq ($(ARCH), WINNT)
  DBMOBJNAME = $(addsuffix .lib, $(DBM_LIBNAMES))
  LIBDBM = $(addprefix $(DBM_LIBPATH)/, $(DBMOBJNAME))
  DBMLINK = /LIBPATH:$(DBM_LIBPATH) $(DBMOBJNAME)
else
  DBM_SOLIBS = $(addsuffix .$(DLL_SUFFIX),  $(addprefix $(LIB_PREFIX), $(DBM_LIBNAMES)))
  DBMROBJNAME = $(addsuffix .a, $(addprefix $(LIB_PREFIX), $(DBM_LIBNAMES)))
  LIBDBM = $(addprefix $(DBM_LIBPATH)/, $(DBMROBJNAME))
  DBMLINK = -L$(DBM_LIBPATH) $(addprefix -l, $(DBM_LIBNAMES))
endif

### DBM END #############################

### SECURITY #############################
ifdef SECURITY_SOURCE_ROOT
  SECURITY_LIBPATH = $(SECURITY_SOURCE_ROOT)/dist/$(MOZ_OBJDIR_NAME)/lib
  SECURITY_BINPATH = $(SECURITY_SOURCE_ROOT)/dist/$(MOZ_OBJDIR_NAME)/bin
  SECURITY_INCDIR = $(SECURITY_SOURCE_ROOT)/dist/public/nss
else
  SECURITY_LIBPATH = $(SECURITY_BUILD_DIR)/lib
  SECURITY_BINPATH = $(SECURITY_BUILD_DIR)/bin
  SECURITY_INCDIR = $(SECURITY_BUILD_DIR)/include
endif
SECURITY_INCLUDE = -I$(SECURITY_INCDIR)
# add crlutil and ocspclnt when we support CRL and OCSP cert checking in DS
SECURITY_BINNAMES = certutil derdump pp pk12util ssltap modutil shlibsign
SECURITY_LIBNAMES = ssl3 nss3 softokn3
# these libs have a corresponding .chk file
SECURITY_NEED_CHK = softokn3

SECURITY_LIBNAMES.pkg = $(SECURITY_LIBNAMES) smime3

# these are only needed on 32 bit Solaris and HP-UX
ifneq ($(USE_64), 1)
ifeq ($(ARCH), SOLARIS)
SECURITY_LIBNAMES.pkg += freebl_hybrid_3 freebl_pure32_3
# these libs have a corresponding .chk file
SECURITY_NEED_CHK += freebl_hybrid_3 freebl_pure32_3
endif
ifeq ($(ARCH), HPUX)
SECURITY_LIBNAMES.pkg += freebl_hybrid_3 freebl_pure32_3
# these libs have a corresponding .chk file
SECURITY_NEED_CHK += freebl_hybrid_3 freebl_pure32_3
endif
endif # USE_64

SECURITY_TOOLS = $(addsuffix $(EXE_SUFFIX),$(SECURITY_BINNAMES))
SECURITY_TOOLS_FULLPATH = $(addprefix $(SECURITY_BINPATH)/, $(SECURITY_TOOLS))

SECURITY_LIBS_TO_PKG = $(addsuffix .$(DLL_SUFFIX),$(addprefix $(SECURITY_LIBPATH)/$(LIB_PREFIX),$(SECURITY_LIBNAMES.pkg)))
SECURITY_LIBS_TO_PKG += $(addsuffix .chk,$(addprefix $(SECURITY_LIBPATH)/$(LIB_PREFIX),$(SECURITY_NEED_CHK)))
LIBS_TO_PKG += $(SECURITY_LIBS_TO_PKG)
LIBS_TO_PKG_SHARED += $(SECURITY_LIBS_TO_PKG) # for cmd line tools
ifeq ($(USE_SETUPUTIL), 1)
  PACKAGE_SETUP_LIBS += $(SECURITY_LIBS_TO_PKG)
endif
ifeq ($(USE_DSGW), 1)
  LIBS_TO_PKG_CLIENTS += $(SECURITY_LIBS_TO_PKG) # for dsgw
endif

ifeq ($(ARCH), WINNT)
  SECURITYOBJNAME = $(addsuffix .$(LIB_SUFFIX), $(SECURITY_LIBNAMES))
  LIBSECURITY = $(addprefix $(SECURITY_LIBPATH)/, $(SECURITYOBJNAME))
  SECURITYLINK = /LIBPATH:$(SECURITY_LIBPATH) $(SECURITYOBJNAME)
else
  SECURITYOBJNAME = $(addsuffix .$(DLL_SUFFIX), $(addprefix $(LIB_PREFIX), $(SECURITY_LIBNAMES)))
  LIBSECURITY = $(addprefix $(SECURITY_LIBPATH)/, $(SECURITYOBJNAME))
  SECURITYLINK = -L$(SECURITY_LIBPATH) $(addprefix -l, $(SECURITY_LIBNAMES))
endif

# we need to package the root cert file in the alias directory
PACKAGE_SRC_DEST += $(SECURITY_LIBPATH)/$(LIB_PREFIX)nssckbi.$(DLL_SUFFIX) alias

# need to package the sec tools in shared/bin
BINS_TO_PKG_SHARED += $(SECURITY_TOOLS_FULLPATH)

### SECURITY END #############################

### SVRCORE #############################
ifdef SVRCORE_SOURCE_ROOT
  SVRCORE_LIBPATH = $(SVRCORE_SOURCE_ROOT)/dist/$(MOZ_OBJDIR_NAME)/lib
  SVRCORE_INCDIR = $(SVRCORE_SOURCE_ROOT)/dist/public/svrcore
else
  SVRCORE_LIBPATH = $(SVRCORE_BUILD_DIR)/lib
  SVRCORE_INCDIR = $(SVRCORE_BUILD_DIR)/include
endif
SVRCORE_INCLUDE = -I$(SVRCORE_INCDIR)
SVRCORE_LIBNAMES = svrcore

ifeq ($(ARCH), WINNT)
  SVRCOREOBJNAME = $(addsuffix .lib, $(SVRCORE_LIBNAMES))
  LIBSVRCORE = $(addprefix $(SVRCORE_LIBPATH)/, $(SVRCOREOBJNAME))
  SVRCORELINK = /LIBPATH:$(SVRCORE_LIBPATH) $(SVRCOREOBJNAME)
else
  SVRCOREOBJNAME = $(addsuffix .a, $(addprefix $(LIB_PREFIX), $(SVRCORE_LIBNAMES)))
  LIBSVRCORE = $(addprefix $(SVRCORE_LIBPATH)/, $(SVRCOREOBJNAME))
  SVRCORELINK = -L$(SVRCORE_LIBPATH) $(addprefix -l, $(SVRCORE_LIBNAMES))
endif

### SVRCORE END #############################

####################################################
# LDAP SDK
###################################################

ifdef LDAPSDK_SOURCE_ROOT
  LDAPSDK_LIBPATH = $(LDAPSDK_SOURCE_ROOT)/dist/lib
  LDAPSDK_INCDIR = $(LDAPSDK_SOURCE_ROOT)/dist/public/ldap
  LDAPSDK_BINPATH = $(LDAPSDK_SOURCE_ROOT)/dist/bin
else
  LDAPSDK_LIBPATH = $(LDAP_ROOT)/lib
  LDAPSDK_INCDIR = $(LDAP_ROOT)/include
  LDAPSDK_BINPATH = $(LDAP_ROOT)/tools
endif
LDAPSDK_INCLUDE = -I$(LDAPSDK_INCDIR)

# package the command line programs
LDAPSDK_TOOLS = $(wildcard $(LDAPSDK_BINPATH)/ldap*$(EXE_SUFFIX))
BINS_TO_PKG_SHARED += $(LDAPSDK_TOOLS)
# package the include files - needed for the plugin API
LDAPSDK_INCLUDE_FILES = $(wildcard $(LDAPSDK_INCDIR)/*.h)
PACKAGE_SRC_DEST += $(subst $(SPACE),$(SPACE)plugins/slapd/slapi/include$(SPACE),$(LDAPSDK_INCLUDE_FILES))
PACKAGE_SRC_DEST += plugins/slapd/slapi/include

ifeq ($(ARCH), WINNT)
  LDAP_LIBNAMES = ldapssl32v$(LDAP_SUF) ldap32v$(LDAP_SUF) ldappr32v$(LDAP_SUF)
  LDAPDLL_NAME = $(addprefix ns, $(LDAP_LIBNAMES))
  LDAPOBJNAME = $(addsuffix .$(LIB_SUFFIX), $(LDAPDLL_NAME))
  LDAPLINK = /LIBPATH:$(LDAPSDK_LIBPATH) $(LDAPOBJNAME)
  LDAP_NOSSL_LINK = /LIBPATH:$(LDAPSDK_LIBPATH) nsldap32v$(LDAP_SUF).$(LIB_SUFFIX)
  LIBLDAPDLL_NAMES = $(addsuffix .dll, $(addprefix $(LDAP_LIBPATH)/, $(LDAPDLL_NAME)))

  LIBS_TO_PKG += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  LIBS_TO_PKG_SHARED += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  ifeq ($(USE_SETUPUTIL), 1)
    PACKAGE_SETUP_LIBS += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  endif
  ifeq ($(USE_DSGW), 1)
    LIBS_TO_PKG_CLIENTS += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  endif
else # not WINNT
  LDAP_SOLIB_NAMES = ssldap$(LDAP_SUF)$(LDAP_DLL_PRESUF) ldap$(LDAP_SUF)$(LDAP_DLL_PRESUF) prldap$(LDAP_SUF)$(LDAP_DLL_PRESUF)
  ifndef LDAP_NO_LIBLCACHE
	LDAP_SOLIB_NAMES += lcache30$(LDAP_DLL_PRESUF)
  endif
  LDAP_DOTALIB_NAMES =
  LDAP_LIBNAMES = $(LDAP_DOTALIB_NAMES) $(LDAP_SOLIB_NAMES)
  LDAP_SOLIBS = $(addsuffix .$(LDAP_DLL_SUFFIX), $(addprefix $(LIB_PREFIX), $(LDAP_SOLIB_NAMES)))
  LDAPOBJNAME = $(addsuffix .$(LIB_SUFFIX), $(addprefix $(LIB_PREFIX), $(LDAP_DOTALIB_NAMES))) \
		$(LDAP_SOLIBS)
  LDAPLINK = -L$(LDAPSDK_LIBPATH) $(addprefix -l,$(LDAP_SOLIB_NAMES))
  LDAP_NOSSL_LINK = -L$(LDAPSDK_LIBPATH) -lldap$(LDAP_SUF)$(LDAP_DLL_PRESUF)

  LIBS_TO_PKG += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  LIBS_TO_PKG_SHARED += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  ifeq ($(USE_SETUPUTIL), 1)
    PACKAGE_SETUP_LIBS += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  endif
  ifeq ($(USE_DSGW), 1)
    LIBS_TO_PKG_CLIENTS += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  endif
endif

LDAP_LIBPATH = $(LDAPSDK_LIBPATH)
LDAP_INCLUDE = $(LDAPSDK_INCDIR)
LDAP_TOOLDIR = $(LDAPSDK_BINPATH)
LIBLDAP = $(addprefix $(LDAP_LIBPATH)/, $(LDAPOBJNAME))

### SASL package ##########################################

ifeq ($(ARCH), Linux)
  ifeq ($(BUILD_ARCH), RHEL3)
    SASL_LIBPATH = /usr/kerberos/lib
  else
    SASL_LIBPATH = /usr/lib
  endif
  SASL_INCDIR = /usr/include/sasl
else
  ifdef SASL_SOURCE_ROOT
    SASL_LIBPATH = $(SASL_SOURCE_ROOT)/lib
    SASL_INCDIR = $(SASL_SOURCE_ROOT)/include
  else
    SASL_LIBPATH = $(SASL_BUILD_DIR)/lib
    SASL_INCDIR = $(SASL_BUILD_DIR)/include
  endif
endif
SASL_INCLUDE = $(SASL_INCDIR)

ifeq ($(ARCH), WINNT)
  SASL_LIB_ROOT_NAME = sasl
  SASL_LINK = /LIBPATH:$(SASL_LIBPATH) lib$(SASL_LIB_ROOT_NAME).lib
  SASL_LIBS = lib$(SASL_LIB_ROOT_NAME).lib,lib$(SASL_LIB_ROOT_NAME).dll,saslDIGESTMD5.dll
else
  # for cyrus it's sasl2
  SASL_LIB_ROOT_NAME = sasl2
  SASL_LIBS = lib$(SASL_LIB_ROOT_NAME).a
  ifeq ($(ARCH), Linux)
    GSSAPI_LIBS=-lgssapi_krb5
  endif
  ifeq ($(ARCH), SOLARIS)
    GSSAPI_LIBS=-lgss
  endif
  ifeq ($(ARCH), HPUX)
      GSSAPI_LIBS=-lgss
      ifeq ($(USE_64),1)
        GSSAPI_LIBS=-L/usr/lib/pa20_64 -lgss
      endif
  endif

  SASL_LINK = -L$(SASL_LIBPATH) -l$(SASL_LIB_ROOT_NAME) $(GSSAPI_LIBS)
endif
###########################################################

### Net-SNMP package ######################################
ifdef NETSNMP_SOURCE_ROOT
  NETSNMP_LIBPATH = $(NETSNMP_SOURCE_ROOT)/built/lib
  NETSNMP_INCDIR = $(NETSNMP_SOURCE_ROOT)/built/include
  NETSNMP_BINDIR = $(NETSNMP_SOURCE_ROOT)/built/bin
else
  NETSNMP_LIBPATH = $(NETSNMP_BUILD_DIR)/lib
  NETSNMP_INCDIR = $(NETSNMP_BUILD_DIR)/include
  NETSNMP_BINDIR = $(NETSNMP_BUILD_DIR)/bin
endif

NETSNMP_INCLUDE = -I$(NETSNMP_INCDIR)
NETSNMP_LIBNAMES = netsnmp netsnmpagent netsnmpmibs netsnmphelpers
NETSNMP_LINK = -L$(NETSNMP_LIBPATH) $(addprefix -l, $(NETSNMP_LIBNAMES))
ifneq ($(ARCH), WINNT)
  ifeq ($(ARCH), HPUX)
    NETSNMP_SOLIBS = $(addsuffix .$(DLL_SUFFIX).7, $(addprefix $(LIB_PREFIX), $(NETSNMP_LIBNAMES)))
  else
    NETSNMP_SOLIBS = $(addsuffix .$(DLL_SUFFIX).5, $(addprefix $(LIB_PREFIX), $(NETSNMP_LIBNAMES)))
  endif
  LIBS_TO_PKG += $(addprefix $(NETSNMP_LIBPATH)/,$(NETSNMP_SOLIBS))
endif
###########################################################

### ICU package ##########################################

ICU_LIB_VERSION = 24
ifdef ICU_SOURCE_ROOT
  ICU_LIBPATH = $(ICU_SOURCE_ROOT)/built/lib
  ICU_BINPATH = $(ICU_SOURCE_ROOT)/built/bin
  ICU_INCPATH = $(ICU_SOURCE_ROOT)/built/include
else
  ICU_LIBPATH = $(ICU_BUILD_DIR)/lib
  ICU_BINPATH = $(ICU_BUILD_DIR)/bin
  ICU_INCPATH = $(ICU_BUILD_DIR)/include
endif
ICU_INCLUDE = -I$(ICU_INCPATH)
ifeq ($(ARCH), WINNT)
  ifeq ($(BUILD_DEBUG), optimize)
    ICU_LIB_SUF=
  else
    ICU_LIB_SUF=d
  endif
  ICU_LIBNAMES = icuin$(ICU_LIB_SUF) icuuc$(ICU_LIB_SUF) icudata
  ICU_DLLNAMES = icuin$(ICU_LIB_VERSION)$(ICU_LIB_SUF) icuuc$(ICU_LIB_VERSION)$(ICU_LIB_SUF) icudt$(ICU_LIB_VERSION)l
  ICULINK = /LIBPATH:$(ICU_LIBPATH) $(addsuffix .$(LIB_SUFFIX),$(ICU_LIBNAMES))
  LIBS_TO_PKG += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_BINPATH)/,$(ICU_DLLNAMES)))
  LIBS_TO_PKG_SHARED += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_BINPATH)/,$(ICU_DLLNAMES)))
  ifeq ($(USE_DSGW), 1)
    LIBS_TO_PKG_CLIENTS += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_BINPATH)/,$(ICU_DLLNAMES)))
  endif
else
  ICU_LIBNAMES = icui18n icuuc icudata
  ICULINK = -L$(ICU_LIBPATH) $(addprefix -l, $(ICU_LIBNAMES))
  LIBS_TO_PKG += $(addsuffix .$(ICU_LIB_VERSION),$(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_LIBPATH)/,$(addprefix lib,$(ICU_LIBNAMES)))))
  LIBS_TO_PKG_SHARED += $(addsuffix .$(ICU_LIB_VERSION),$(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_LIBPATH)/,$(addprefix lib,$(ICU_LIBNAMES)))))
  ifeq ($(USE_DSGW), 1)
    LIBS_TO_PKG_CLIENTS += $(addsuffix .$(ICU_LIB_VERSION),$(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_LIBPATH)/,$(addprefix lib,$(ICU_LIBNAMES)))))
  endif
#LIBS_TO_PKG = $(addsuffix $(addprefix lib,$(ICU_LIBNAMES))
endif

BINS_TO_PKG_SHARED += $(ICU_BINPATH)/uconv$(EXE_SUFFIX)

###########################################################

### DB component (Berkeley DB) ############################
DB_LIBNAME=lib$(DB_MAJOR_MINOR)
ifdef DB_SOURCE_ROOT
  DB_INCLUDE =$(DB_SOURCE_ROOT)/built
  DB_LIBPATH =$(DB_SOURCE_ROOT)/built/.libs
  DB_BINPATH =$(DB_SOURCE_ROOT)/built
else
  DB_INCLUDE =$(db_path_config)/include
  DB_LIBPATH =$(db_path_config)/lib
  DB_BINPATH =$(db_path_config)/bin
endif
ifeq ($(ARCH), WINNT)
  db_import_lib_suffix =$(LIB_SUFFIX)
  DB_LIB =$(DB_LIBPATH)/$(DB_LIBNAME).$(db_import_lib_suffix)
  DB_STATIC_LIB =$(DB_LIBPATH)/$(DB_LIBNAME).$(LIB_SUFFIX)
else	# not WINNT
  db_import_lib_suffix =$(DLL_SUFFIX)
  DB_LIB =-L$(DB_LIBPATH) -l$(DB_MAJOR_MINOR)
# XXXsspitzer: we need the spinlock symbols staticly linked in to libdb
  DB_STATIC_LIB =-L$(DB_LIBPATH) -ldbs
endif	# not WINNT

# libdb only needs to be in the server directory since only the server uses it
PACKAGE_SRC_DEST += $(wildcard $(DB_LIBPATH)/*.$(DLL_SUFFIX)) bin/slapd/server

### DB component (Berkeley DB) ############################


###########################################
# SETUPUTIL
##########################################

ifdef SETUPUTIL_SOURCE_ROOT
  SETUPUTIL_LIBPATH = $(SETUPUTIL_SOURCE_ROOT)/built/package/$(COMPONENT_OBJDIR)/lib
  SETUPUTIL_INCDIR = $(SETUPUTIL_SOURCE_ROOT)/built/package/$(COMPONENT_OBJDIR)/include
  SETUPUTIL_BINPATH = $(SETUPUTIL_SOURCE_ROOT)/built/package/$(COMPONENT_OBJDIR)/bin
else
  SETUPUTIL_LIBPATH = $(SETUPUTIL_BUILD_DIR)/lib
  SETUPUTIL_INCDIR = $(SETUPUTIL_BUILD_DIR)/include
  SETUPUTIL_BINPATH = $(SETUPUTIL_BUILD_DIR)/bin
endif
SETUPUTIL_INCLUDE = -I$(SETUPUTIL_INCDIR)

ifeq ($(ARCH), WINNT)
SETUPUTILLINK = /LIBPATH:$(SETUPUTIL_LIBPATH) nssetup32.$(LIB_SUFFIX)
SETUPUTIL_S_LINK = /LIBPATH:$(SETUPUTIL_LIBPATH) nssetup32_s.$(LIB_SUFFIX)
else
SETUPUTILLINK = -L$(SETUPUTIL_LIBPATH) -linstall
SETUPUTIL_S_LINK = $(SETUPUTILLINK)
endif

# this is the base directory under which the component's files will be found
# during the build process
ifdef ADMINUTIL_SOURCE_ROOT
  ADMINUTIL_LIBPATH = $(ADMINUTIL_SOURCE_ROOT)/built/adminutil/$(COMPONENT_OBJDIR)/lib
  ADMINUTIL_INCPATH = $(ADMINUTIL_SOURCE_ROOT)/built/adminutil/$(COMPONENT_OBJDIR)/include
else
  ADMINUTIL_LIBPATH = $(ADMINUTIL_BUILD_DIR)/lib
  ADMINUTIL_INCPATH = $(ADMINUTIL_BUILD_DIR)/include
endif

PACKAGE_SRC_DEST += $(ADMINUTIL_LIBPATH)/property bin/slapd/lib
LIBS_TO_PKG += $(wildcard $(ADMINUTIL_LIBPATH)/*.$(DLL_SUFFIX))
LIBS_TO_PKG_CLIENTS += $(wildcard $(ADMINUTIL_LIBPATH)/*.$(DLL_SUFFIX))

ifeq ($(ARCH),WINNT)
ADMINUTIL_LINK = /LIBPATH:$(ADMINUTIL_LIBPATH) libadminutil$(ADMINUTIL_VER).$(LIB_SUFFIX)
ADMINUTIL_S_LINK = /LIBPATH:$(ADMINUTIL_LIBPATH) libadminutil_s$(ADMINUTIL_VER).$(LIB_SUFFIX)
LIBADMINUTILDLL_NAMES = $(ADMINUTIL_LIBPATH)/libadminutil$(ADMINUTIL_VER).$(DLL_SUFFIX)
else
ADMINUTIL_LINK=-L$(ADMINUTIL_LIBPATH) -ladminutil$(ADMINUTIL_VER)
endif
ADMINUTIL_INCLUDE=-I$(ADMINUTIL_INCPATH) -I$(ADMINUTIL_INCPATH)/libadminutil \
	-I$(ADMINUTIL_INCPATH)/libadmsslutil

#########################################
# LDAPJDK
#########################################

LDAPJDK = ldapjdk.jar
ifdef LDAPJDK_SOURCE_DIR
  LDAPJDK_DIR = $(LDAPJDK_SOURCE_DIR)/directory/java-sdk/dist/packages
endif
ifndef LDAPJDK_DIR
  LDAPJDK_DIR = $(CLASS_DEST)
endif
LDAPJARFILE=$(LDAPJDK_DIR)/ldapjdk.jar

AXIS = axis-$(AXIS_VERSION).zip
AXIS_FILES = $(AXIS)
AXIS_FILE = $(CLASS_DEST)/$(AXIS)

DSMLJAR = activation.jar,jaxrpc-api.jar,jaxrpc.jar,saaj.jar,xercesImpl.jar,xml-apis.jar
DSMLJAR_FILE = $(CLASS_DEST)

CRIMSON_LICENSE = LICENSE.crimson
CRIMSONJAR = crimson.jar
ifdef CRIMSON_SOURCE_DIR
  CRIMSONJAR_BUILD_DIR = $(CRIMSON_SOURCE_DIR)
endif
ifndef CRIMSONJAR_BUILD_DIR
  CRIMSONJAR_BUILD_DIR = $(CLASS_DEST)
endif
CRIMSONJAR_FILE = $(CRIMSONJAR_BUILD_DIR)/$(CRIMSONJAR)

ifdef ADMINSERVER_SOURCE_ROOT
  ADMSERV_DIR = $(ADMINSERVER_SOURCE_ROOT)/built/package/$(COMPONENT_OBJDIR)
# else set in internal_buildpaths.mk
endif
# these are the only two subcomponents we use from the adminserver package
ADMINSERVER_SUBCOMPS=admin base

ifdef LDAPCONSOLE_SOURCE_ROOT
  LDAPCONSOLE_DIR = $(LDAPCONSOLE_SOURCE_ROOT)/built/package
else
  LDAPCONSOLE_DIR = $(CLASS_DEST)
endif
LDAPCONSOLEJAR = ds$(LDAPCONSOLE_REL).jar
LDAPCONSOLEJAR_EN = ds$(LDAPCONSOLE_REL)_en.jar

#### online help docs ######
ifndef ONLINEHELP_SOURCE_ROOT
  DSDOC_DIR = $(ABS_ROOT)/../dist/dsdoc
else
  DSDOC_DIR = $(ONLINEHELP_SOURCE_ROOT)
endif
DSDOC_CLIENTS = slapd_clients.zip
DSDOC_COPYRIGHT = slapd_copyright.zip

########### PerLDAP #############
ifdef PERLDAP_SOURCE_ROOT
  PERLDAP_BUILT_DIR = $(PERLDAP_SOURCE_ROOT)/directory/perldap/blib
# else set in internal_buildpaths.mk and pulled in internal_comp_deps.mk
endif

PERLDAP_ARCHLIB_DIR = $(PERLDAP_BUILT_DIR)/arch
PERLDAP_LIB_DIR = $(PERLDAP_BUILT_DIR)/lib/Mozilla
PERLDAP_AUTOLIB_DIR = $(PERLDAP_BUILT_DIR)/lib/auto
# under the serverroot/lib directory, we should have a perl directory which contains arch/, auto/, and Mozilla/
PACKAGE_SRC_DEST += $(PERLDAP_ARCHLIB_DIR) lib/perl
PACKAGE_SRC_DEST += $(PERLDAP_LIB_DIR) lib/perl
PACKAGE_SRC_DEST += $(PERLDAP_AUTOLIB_DIR) lib/perl

# must define dependencies last because they depend on the definitions above
ifeq ($(INTERNAL_BUILD), 1)
include $(BUILD_ROOT)/internal_comp_deps.mk
endif

#################################################
# User Sync Components
#################################################

# WIX MSI Tool kit #####################
WIX = wix-$(WIX_VERSION).zip
WIX_DEST = $(NSCP_DISTDIR_FULL_RTL)/wix
WIX_FILE = $(WIX_DEST)/$(WIX)
WIX_FILES = $(WIX)
WIX_RELEASE = $(COMPONENTS_DIR)/wix
WIX_DIR = $(WIX_RELEASE)/$(WIX_VERSION)
WIX_DEP = $(WIX_FILE)
WIX_REL_DIR=$(subst -bin,,$(subst .zip,,$(WIX)))

ifndef WIX_PULL_METHOD
WIX_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(WIX_DEP): $(NSCP_DISTDIR_FULL_RTL) 
ifeq ($(ARCH), WINNT)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(WIX_DEST) -componentdir $(WIX_DIR) \
		-files $(WIX_FILES) -unzip $(WIX_DEST)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component WIX files $@" ; \
	fi
else
	-@echo "WIX is not required except on Windows."
endif #WINNT

# java service wrapper for Password Sync #####################
WRAPPER = wrapper_win32_$(WRAPPER_VERSION).zip
WRAPPER_DEST = $(NSCP_DISTDIR_FULL_RTL)/wrapper
WRAPPER_FILE = $(WRAPPER_DEST)/$(WRAPPER)
WRAPPER_FILES = $(WRAPPER)
WRAPPER_RELEASE = $(COMPONENTS_DIR)/wrapper
WRAPPER_DIR = $(WRAPPER_RELEASE)/$(WRAPPER_VERSION)
WRAPPER_DEP = $(WRAPPER_FILE)
WRAPPER_REL_DIR=$(subst -bin,,$(subst .zip,,$(WRAPPER)))

ifndef WRAPPER_PULL_METHOD
WRAPPER_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(WRAPPER_DEP): $(NSCP_DISTDIR_FULL_RTL) 
ifeq ($(ARCH), WINNT)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(WRAPPER_DEST) -componentdir $(WRAPPER_DIR) \
		-files $(WRAPPER_FILES) -unzip $(WRAPPER_DEST)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component WRAPPER files $@" ; \
	fi
else
	-@echo "WRAPPER is not required except on Windows."
endif #WINNT


# swig for Password Sync #####################
SWIG = swigwin-$(SWIG_VERSION).zip
SWIG_DEST = $(NSCP_DISTDIR_FULL_RTL)/swig
SWIG_FILE = $(SWIG_DEST)/$(SWIG)
SWIG_FILES = $(SWIG)
SWIG_RELEASE = $(COMPONENTS_DIR)/swig
SWIG_DIR = $(SWIG_RELEASE)/$(SWIG_VERSION)
SWIG_DEP = $(SWIG_FILE)
SWIG_REL_DIR=$(subst -bin,,$(subst .zip,,$(SWIG)))
SWIG_EXE = $(SWIG_DEST)/SWIG-$(SWIG_VERSION)/swig.exe

ifndef SWIG_PULL_METHOD
SWIG_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SWIG_DEP): $(NSCP_DISTDIR_FULL_RTL) 
ifeq ($(ARCH), WINNT)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(SWIG_DEST) -componentdir $(SWIG_DIR) \
		-files $(SWIG_FILES) -unzip $(SWIG_DEST)

endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component SWIG files $@" ; \
	fi
else
	-@echo "SWIG is not required except on Windows."
endif #WINNT

# apache ds for Password Sync #####################
APACHEDS = apacheds-main-$(APACHEDS_VERSION).jar
APACHEDSSRC = apacheds-$(APACHEDS_VERSION)-src.zip
APACHEDS_DEST = $(NSCP_DISTDIR_FULL_RTL)/apacheds
APACHEDS_FILE = $(APACHEDS_DEST)/$(APACHEDS)
APACHEDS_FILES = $(APACHEDS)
APACHEDSSRC_FILES = $(APACHEDSSRC)
APACHEDS_RELEASE = $(COMPONENTS_DIR)/apacheds
APACHEDS_DIR = $(APACHEDS_RELEASE)/$(APACHEDS_VERSION)
APACHEDS_DEP = $(APACHEDS_FILE)
APACHEDS_REL_DIR=$(subst -bin,,$(subst .jar,,$(APACHEDS)))
APACHEDSSOURCE = $(NSCP_DISTDIR_FULL_RTL)/apacheds/apacheds-$(APACHEDS_VERSION)


ifndef APACHEDS_PULL_METHOD
APACHEDS_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(APACHEDS_DEP) : $(NSCP_DISTDIR_FULL_RTL) 
ifeq ($(ARCH), WINNT)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(APACHEDS_DEST) -componentdir $(APACHEDS_DIR) \
		-files $(APACHEDS_FILES)
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(APACHEDS_DEST) -componentdir $(APACHEDS_DIR) \
		-files $(APACHEDSSRC_FILES) -unzip $(APACHEDS_DEST)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component APACHEDS files $@" ; \
	fi


else
	-@echo "APACHEDS is not required except on Windows."
endif #WINNT


# MAVEN for Password Sync #####################
MAVEN = maven-$(MAVEN_VERSION).zip
MAVEN_DEST = $(NSCP_DISTDIR_FULL_RTL)/maven
MAVEN_FILE = $(MAVEN_DEST)/$(MAVEN)
MAVEN_FILES = $(MAVEN)
MAVEN_RELEASE = $(COMPONENTS_DIR)/maven
MAVEN_DIR = $(MAVEN_RELEASE)/$(MAVEN_VERSION)
MAVEN_DEP = $(MAVEN_FILE)
MAVEN_REL_DIR=$(subst -bin,,$(subst .zip,,$(MAVEN)))

# CMD does not like the '/' in NSCP_DISTDIR_FULL_RTL, therefore the
# slashes need swapped to '\\'
MAVEN_EXE=cmd /c `echo $(NSCP_DISTDIR_FULL_RTL) | sed 's/\//\\\\/g'`\\maven\\$(MAVEN_REL_DIR)\\bin\\maven.bat
MAVEN_HOME=$(NSCP_DISTDIR_FULL_RTL)/maven/$(MAVEN_REL_DIR)

ifndef MAVEN_PULL_METHOD
MAVEN_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(MAVEN_DEP): $(NSCP_DISTDIR_FULL_RTL) 
ifeq ($(ARCH), WINNT)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(MAVEN_DEST) -componentdir $(MAVEN_DIR) \
		-files $(MAVEN_FILES) -unzip $(MAVEN_DEST)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component MAVEN files $@" ; \
	fi
else
	-@echo "MAVEN is not required except on Windows."
endif #WINNT
