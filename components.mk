#
# BEGIN COPYRIGHT BLOCK
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
COMMA := ,
NULLSTRING :=
SPACE := $(NULLSTRING) # the space is between the ) and the #
PACKAGE_SRC_DEST =

ifeq ($(ARCH), WINNT)
EXE_SUFFIX = .exe
else # unix - windows has no lib name prefix, except for nspr
LIB_PREFIX = lib
endif

# work around vsftpd -L problem
ifeq ($(COMPONENT_PULL_METHOD), FTP)
ifdef USING_VSFTPD
VSFTPD_HACK=1
endif
endif

# ADMINUTIL library #######################################
ADMINUTIL_VERSION=$(ADMINUTIL_RELDATE)$(SEC_SUFFIX)
ADMINUTIL_BASE=$(ADMINUTIL_VERSDIR)/${ADMINUTIL_VERSION}
ADMSDKOBJDIR = $(FULL_RTL_OBJDIR)
ADMINUTIL_IMPORT=$(COMPONENTS_DIR)/${ADMINUTIL_BASE}/$(NSOBJDIR_NAME)
# this is the base directory under which the component's files will be found
# during the build process
ADMINUTIL_BUILD_DIR=$(NSCP_DISTDIR_FULL_RTL)/adminutil
ADMINUTIL_LIBPATH=$(ADMINUTIL_BUILD_DIR)/lib
ADMINUTIL_INCPATH=$(ADMINUTIL_BUILD_DIR)/include

PACKAGE_SRC_DEST += $(ADMINUTIL_LIBPATH)/property bin/slapd/lib
LIBS_TO_PKG += $(wildcard $(ADMINUTIL_LIBPATH)/*.$(DLL_SUFFIX))
LIBS_TO_PKG_CLIENTS += $(wildcard $(ADMINUTIL_LIBPATH)/*.$(DLL_SUFFIX))

#
# Libadminutil
#
ADMINUTIL_DEP = $(ADMINUTIL_LIBPATH)/libadminutil$(ADMINUTIL_VER).$(LIB_SUFFIX)
ifeq ($(ARCH), WINNT)
ADMINUTIL_LINK = /LIBPATH:$(ADMINUTIL_LIBPATH) libadminutil$(ADMINUTIL_VER).$(LIB_SUFFIX)
ADMINUTIL_S_LINK = /LIBPATH:$(ADMINUTIL_LIBPATH) libadminutil_s$(ADMINUTIL_VER).$(LIB_SUFFIX)
LIBADMINUTILDLL_NAMES = $(ADMINUTIL_LIBPATH)/libadminutil$(ADMINUTIL_VER).$(DLL_SUFFIX)
else
ADMINUTIL_LINK=-L$(ADMINUTIL_LIBPATH) -ladminutil$(ADMINUTIL_VER)
endif
ADMINUTIL_INCLUDE=-I$(ADMINUTIL_INCPATH) \
	-I$(ADMINUTIL_INCPATH)/libadminutil \
	-I$(ADMINUTIL_INCPATH)/libadmsslutil

ifndef ADMINUTIL_PULL_METHOD
ADMINUTIL_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(ADMINUTIL_DEP): ${NSCP_DISTDIR_FULL_RTL}
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(ADMINUTIL_PULL_METHOD) \
		-objdir $(ADMINUTIL_BUILD_DIR) \
		-componentdir $(ADMINUTIL_IMPORT) \
		-files include,lib
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component adminutil file $@" ; \
	fi
###########################################################
# Peer

PEER_BUILD_DIR = $(NSCP_DISTDIR)/peer
ifeq ($(ARCH), WINNT)
# PEER_RELEASE = $(COMPONENTS_DIR)/peer/$(PEER_RELDATE)
# PEER_FILES = include
else
PEER_RELEASE = $(COMPONENTS_DIR)/peer/$(PEER_RELDATE)/$(NSOBJDIR_NAME)
PEER_FILES = obj
PEER_DEP = $(PEER_OBJPATH)/ns-ldapagt
endif
# PEER_MGMTPATH = $(PEER_BUILD_DIR)/dev
# PEER_INCDIR = $(PEER_BUILD_DIR)/include
# PEER_BINPATH = $(PEER_BUILD_DIR)/dev
PEER_OBJPATH = $(PEER_BUILD_DIR)/obj
# PEER_INCLUDE = -I$(PEER_INCDIR)

ifndef PEER_PULL_METHOD
PEER_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(PEER_DEP): $(NSCP_DISTDIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(PEER_PULL_METHOD) \
		-objdir $(PEER_BUILD_DIR) -componentdir $(PEER_RELEASE) \
		-files $(PEER_FILES)
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component PEER file $@" ; \
	fi
endif

###########################################################

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
NSPR_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/nspr
NSPR_ABS_BUILD_DIR = $(NSCP_ABS_DISTDIR_FULL_RTL)/nspr
NSPR_LIBPATH = $(NSPR_BUILD_DIR)/lib
NSPR_INCLUDE = -I$(NSPR_BUILD_DIR)/include
NSPR_IMPORT = $(COMPONENTS_DIR)/nspr20/$(NSPR_RELDATE)/$(FULL_RTL_OBJDIR)
NSPR_LIBS_TO_PKG = $(addsuffix .$(DLL_SUFFIX),$(addprefix $(NSPR_LIBPATH)/lib,$(NSPR_LIBNAMES)))

LIBS_TO_PKG += $(NSPR_LIBS_TO_PKG)
LIBS_TO_PKG_SHARED += $(NSPR_LIBS_TO_PKG) # needed for cmd line tools
PACKAGE_SETUP_LIBS += $(NSPR_LIBS_TO_PKG)
LIBS_TO_PKG_CLIENTS += $(NSPR_LIBS_TO_PKG) # for dsgw

NSPR_DEP = $(NSPR_LIBPATH)/libnspr4.$(LIB_SUFFIX)
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

ifndef NSPR_PULL_METHOD
NSPR_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(NSPR_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(NSPR_PULL_METHOD) \
		-objdir $(NSPR_BUILD_DIR) -componentdir $(NSPR_IMPORT) \
		-files lib,include
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component NSPR file $@" ; \
	fi

### DBM #############################

DBM_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/dbm
DBM_LIBPATH = $(DBM_BUILD_DIR)/lib
DBM_INCDIR = $(DBM_BUILD_DIR)/include
DBM_DEP = $(DBM_LIBPATH)/libdbm.$(LIB_SUFFIX)
DBM_IMPORT = $(COMPONENTS_DIR)/dbm/$(DBM_RELDATE)/$(NSOBJDIR_NAME)
DBM_INCLUDE = -I$(DBM_INCDIR)
DBM_LIBNAMES = dbm

ifeq ($(ARCH), WINNT)
  DBMOBJNAME = $(addsuffix .lib, $(DBM_LIBNAMES))
  LIBDBM = $(addprefix $(DBM_LIBPATH)/, $(DBMOBJNAME))
  DBMLINK = /LIBPATH:$(DBM_LIBPATH) $(DBMOBJNAME)
  DBM_DEP = $(DBM_LIBPATH)/dbm.$(LIB_SUFFIX)
else
  DBM_SOLIBS = $(addsuffix .$(DLL_SUFFIX),  $(addprefix $(LIB_PREFIX), $(DBM_LIBNAMES)))
  DBMROBJNAME = $(addsuffix .a, $(addprefix $(LIB_PREFIX), $(DBM_LIBNAMES)))
  LIBDBM = $(addprefix $(DBM_LIBPATH)/, $(DBMROBJNAME))
  DBMLINK = -L$(DBM_LIBPATH) $(addprefix -l, $(DBM_LIBNAMES))
  DBM_DEP = $(DBM_LIBPATH)/libdbm.$(LIB_SUFFIX)
endif

ifndef DBM_PULL_METHOD
DBM_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(DBM_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(DBM_PULL_METHOD) \
		-objdir $(DBM_BUILD_DIR) -componentdir $(DBM_IMPORT)/.. \
		-files xpheader.jar -unzip $(DBM_INCDIR)
	$(FTP_PULL) -method $(DBM_PULL_METHOD) \
		-objdir $(DBM_BUILD_DIR) -componentdir $(DBM_IMPORT) \
		-files mdbinary.jar -unzip $(DBM_BUILD_DIR)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component DBM file $@" ; \
	fi

### DBM END #############################

### SECURITY #############################
SECURITY_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/nss
SECURITY_LIBPATH = $(SECURITY_BUILD_DIR)/lib
SECURITY_BINPATH = $(SECURITY_BUILD_DIR)/bin
SECURITY_INCDIR = $(SECURITY_BUILD_DIR)/include
SECURITY_INCLUDE = -I$(SECURITY_INCDIR)
# add crlutil and ocspclnt when we support CRL and OCSP cert checking in DS
ifeq ($(SECURITY_RELDATE), NSS_3_7_9_RTM)
SECURITY_BINNAMES = certutil derdump pp pk12util ssltap modutil
else
SECURITY_BINNAMES = certutil derdump pp pk12util ssltap modutil shlibsign
endif
SECURITY_LIBNAMES = ssl3 nss3 softokn3
SECURITY_IMPORT = $(COMPONENTS_DIR)/nss/$(SECURITY_RELDATE)/$(FULL_RTL_OBJDIR)

SECURITY_LIBNAMES.pkg = $(SECURITY_LIBNAMES)
SECURITY_LIBNAMES.pkg += smime3
ifeq ($(ARCH), SOLARIS)
SECURITY_LIBNAMES.pkg += freebl_hybrid_3 freebl_pure32_3 fort swft
endif
ifeq ($(ARCH), HPUX)
SECURITY_LIBNAMES.pkg += freebl_hybrid_3 freebl_pure32_3 fort swft
endif
ifeq ($(ARCH), AIX)
SECURITY_LIBNAMES.pkg += fort swft
endif
ifeq ($(ARCH), OSF1)
SECURITY_LIBNAMES.pkg += fort swft
endif
ifeq ($(ARCH), WINNT)
SECURITY_LIBNAMES.pkg += fort32 swft32
endif

SECURITY_TOOLS = $(addsuffix $(EXE_SUFFIX),$(SECURITY_BINNAMES))
SECURITY_TOOLS_FULLPATH = $(addprefix $(SECURITY_BINPATH)/, $(SECURITY_TOOLS))

SECURITY_LIBS_TO_PKG = $(addsuffix .$(DLL_SUFFIX),$(addprefix $(SECURITY_LIBPATH)/$(LIB_PREFIX),$(SECURITY_LIBNAMES.pkg)))
ifneq ($(SECURITY_RELDATE), NSS_3_7_9_RTM)
SECURITY_LIBS_TO_PKG += $(addsuffix .chk,$(addprefix $(SECURITY_LIBPATH)/$(LIB_PREFIX),$(SECURITY_LIBNAMES.pkg)))
endif
LIBS_TO_PKG += $(SECURITY_LIBS_TO_PKG)
LIBS_TO_PKG_SHARED += $(SECURITY_LIBS_TO_PKG) # for cmd line tools
PACKAGE_SETUP_LIBS += $(SECURITY_LIBS_TO_PKG)
LIBS_TO_PKG_CLIENTS += $(SECURITY_LIBS_TO_PKG) # for dsgw

ifeq ($(ARCH), WINNT)
  SECURITYOBJNAME = $(addsuffix .$(LIB_SUFFIX), $(SECURITY_LIBNAMES))
  LIBSECURITY = $(addprefix $(SECURITY_LIBPATH)/, $(SECURITYOBJNAME))
  SECURITYLINK = /LIBPATH:$(SECURITY_LIBPATH) $(SECURITYOBJNAME)
  SECURITY_DEP = $(SECURITY_LIBPATH)/ssl3.$(DLL_SUFFIX)
else
  SECURITYOBJNAME = $(addsuffix .$(DLL_SUFFIX), $(addprefix $(LIB_PREFIX), $(SECURITY_LIBNAMES)))
  LIBSECURITY = $(addprefix $(SECURITY_LIBPATH)/, $(SECURITYOBJNAME))
  SECURITYLINK = -L$(SECURITY_LIBPATH) $(addprefix -l, $(SECURITY_LIBNAMES))
  SECURITY_DEP = $(SECURITY_LIBPATH)/libssl3.$(DLL_SUFFIX)
endif

# we need to package the root cert file in the alias directory
PACKAGE_SRC_DEST += $(SECURITY_LIBPATH)/$(LIB_PREFIX)nssckbi.$(DLL_SUFFIX) alias

# need to package the sec tools in shared/bin
BINS_TO_PKG_SHARED += $(SECURITY_TOOLS_FULLPATH)

#ifeq ($(ARCH), OSF1)
#OSF1SECURITYHACKOBJ = $(OBJDIR)/osf1securityhack.o

#$(OSF1SECURITYHACKOBJ): $(BUILD_ROOT)/osf1securityhack.c
#	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< -o $@

#  SECURITYLINK += $(OSF1SECURITYHACKOBJ)
#endif

ifdef VSFTPD_HACK
SECURITY_FILES=lib,bin/$(subst $(SPACE),$(COMMA)bin/,$(SECURITY_TOOLS))
else
SECURITY_FILES=lib,include,bin/$(subst $(SPACE),$(COMMA)bin/,$(SECURITY_TOOLS))
endif

ifndef SECURITY_PULL_METHOD
SECURITY_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SECURITY_DEP): $(NSCP_DISTDIR_FULL_RTL) $(OSF1SECURITYHACKOBJ)
ifdef COMPONENT_DEPS
	mkdir -p $(SECURITY_BINPATH)
	$(FTP_PULL) -method $(SECURITY_PULL_METHOD) \
		-objdir $(SECURITY_BUILD_DIR) -componentdir $(SECURITY_IMPORT) \
		-files $(SECURITY_FILES)
ifdef VSFTPD_HACK
# work around vsftpd -L problem
	$(FTP_PULL) -method $(SECURITY_PULL_METHOD) \
		-objdir $(SECURITY_BUILD_DIR) -componentdir $(COMPONENTS_DIR)/nss/$(SECURITY_RELDATE) \
		-files include
endif
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component NSS file $@" ; \
	fi

### SECURITY END #############################

### SVRCORE #############################
SVRCORE_BUILD_DIR = $(NSCP_DISTDIR)/svrcore
SVRCORE_LIBPATH = $(SVRCORE_BUILD_DIR)/lib
SVRCORE_INCDIR = $(SVRCORE_BUILD_DIR)/include
SVRCORE_INCLUDE = -I$(SVRCORE_INCDIR)
#SVRCORE_LIBNAMES = svrplcy svrcore
SVRCORE_LIBNAMES = svrcore
#SVRCORE_IMPORT = $(COMPONENTS_DIR)/svrcore/$(SVRCORE_RELDATE)/$(NSOBJDIR_NAME)
SVRCORE_IMPORT = $(COMPONENTS_DIR_DEV)/svrcore/$(SVRCORE_RELDATE)/$(NSOBJDIR_NAME)

ifeq ($(ARCH), WINNT)
  SVRCOREOBJNAME = $(addsuffix .lib, $(SVRCORE_LIBNAMES))
  LIBSVRCORE = $(addprefix $(SVRCORE_LIBPATH)/, $(SVRCOREOBJNAME))
  SVRCORELINK = /LIBPATH:$(SVRCORE_LIBPATH) $(SVRCOREOBJNAME)
  SVRCORE_DEP = $(SVRCORE_LIBPATH)/svrcore.$(LIB_SUFFIX)
else
  SVRCOREOBJNAME = $(addsuffix .a, $(addprefix $(LIB_PREFIX), $(SVRCORE_LIBNAMES)))
  LIBSVRCORE = $(addprefix $(SVRCORE_LIBPATH)/, $(SVRCOREOBJNAME))
ifeq ($(ARCH), OSF1)
# the -all flag is used by default.  This flag causes _all_ objects in lib.a files to
# be processed and linked.  This causes problems with svrcore because there are
# several undefined symbols (notably, the JSS_xxx functions)
  SVRCORELINK = -L$(SVRCORE_LIBPATH) -none $(addprefix -l, $(SVRCORE_LIBNAMES)) -all
else
  SVRCORELINK = -L$(SVRCORE_LIBPATH) $(addprefix -l, $(SVRCORE_LIBNAMES))
endif
  SVRCORE_DEP = $(SVRCORE_LIBPATH)/libsvrcore.$(LIB_SUFFIX)
endif

ifndef SVRCORE_PULL_METHOD
SVRCORE_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SVRCORE_DEP): $(NSCP_DISTDIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(SVRCORE_PULL_METHOD) \
		-objdir $(SVRCORE_BUILD_DIR) -componentdir $(SVRCORE_IMPORT)/.. \
		-files xpheader.jar -unzip $(SVRCORE_INCDIR)
	$(FTP_PULL) -method $(SVRCORE_PULL_METHOD) \
		-objdir $(SVRCORE_BUILD_DIR) -componentdir $(SVRCORE_IMPORT) \
		-files mdbinary.jar -unzip $(SVRCORE_BUILD_DIR)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component SVRCORE file $@" ; \
	fi

### SVRCORE END #############################

### SETUPSDK #############################
# this is where the build looks for setupsdk components
SETUP_SDK_BUILD_DIR = $(NSCP_DISTDIR)/setupsdk
SETUPSDK_VERSION = $(SETUP_SDK_RELDATE)$(SEC_SUFFIX)
SETUPSDK_RELEASE = $(COMPONENTS_DIR)/setupsdk/$(SETUPSDK_VERSDIR)/$(SETUPSDK_VERSION)/$(NSOBJDIR_NAME)
SETUPSDK_LIBPATH = $(SETUP_SDK_BUILD_DIR)/lib
SETUPSDK_INCDIR = $(SETUP_SDK_BUILD_DIR)/include
SETUPSDK_BINPATH = $(SETUP_SDK_BUILD_DIR)/bin
SETUPSDK_INCLUDE = -I$(SETUPSDK_INCDIR)

ifeq ($(ARCH), WINNT)
SETUP_SDK_FILES = setupsdk.tar.gz -unzip $(NSCP_DISTDIR)/setupsdk
SETUPSDK_DEP = $(SETUPSDK_LIBPATH)/nssetup32.$(LIB_SUFFIX)
SETUPSDKLINK = /LIBPATH:$(SETUPSDK_LIBPATH) nssetup32.$(LIB_SUFFIX)
SETUPSDK_S_LINK = /LIBPATH:$(SETUPSDK_LIBPATH) nssetup32_s.$(LIB_SUFFIX)
else
SETUP_SDK_FILES = bin,lib,include
SETUPSDK_DEP = $(SETUPSDK_LIBPATH)/libinstall.$(LIB_SUFFIX)
SETUPSDKLINK = -L$(SETUPSDK_LIBPATH) -linstall
SETUPSDK_S_LINK = $(SETUPSDKLINK)
endif

ifndef SETUPSDK_PULL_METHOD
SETUPSDK_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SETUPSDK_DEP): $(NSCP_DISTDIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(SETUPSDK_PULL_METHOD) \
		-objdir $(SETUP_SDK_BUILD_DIR) -componentdir $(SETUPSDK_RELEASE) \
		-files $(SETUP_SDK_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component SETUPSDK file $@" ; \
	fi

####################################################
# LDAP SDK
###################################################
ifndef LDAP_VERSION
  LDAP_VERSION = $(LDAP_RELDATE)$(SEC_SUFFIX)
endif

LDAP_ROOT = $(NSCP_DISTDIR_FULL_RTL)/ldapsdk
LDAPSDK_LIBPATH = $(LDAP_ROOT)/lib
LDAPSDK_INCDIR = $(LDAP_ROOT)/include
LDAPSDK_INCLUDE = -I$(LDAPSDK_INCDIR)
ifndef LDAP_SBC
# LDAP_SBC = $(COMPONENTS_DIR_DEV)
LDAP_SBC = $(COMPONENTS_DIR)
endif

# package the command line programs
LDAPSDK_TOOLS = $(wildcard $(LDAP_ROOT)/tools/*$(EXE_SUFFIX))
BINS_TO_PKG_SHARED += $(LDAPSDK_TOOLS)
# package the include files - needed for the plugin API
LDAPSDK_INCLUDE_FILES = $(wildcard $(LDAPSDK_INCDIR)/*.h)
PACKAGE_SRC_DEST += $(subst $(SPACE),$(SPACE)plugins/slapd/slapi/include$(SPACE),$(LDAPSDK_INCLUDE_FILES))
PACKAGE_SRC_DEST += plugins/slapd/slapi/include

LDAPOBJDIR = $(FULL_RTL_OBJDIR)
ifeq ($(ARCH), WINNT)
  ifeq ($(PROCESSOR), ALPHA)
	ifeq ($(DEBUG), full)
	  LDAPOBJDIR = WINNTALPHA3.51_DBG.OBJ
	else
	  LDAPOBJDIR = WINNTALPHA3.51_OPT.OBJ
	endif
  endif

  LDAP_RELEASE = $(LDAP_SBC)/$(LDAPCOMP_DIR)/$(LDAP_VERSION)/$(LDAPOBJDIR)
  LDAP_LIBNAMES = ldapssl32v$(LDAP_SUF) ldap32v$(LDAP_SUF) ldappr32v$(LDAP_SUF)
  LDAPDLL_NAME = $(addprefix ns, $(LDAP_LIBNAMES))
  LDAPOBJNAME = $(addsuffix .$(LIB_SUFFIX), $(LDAPDLL_NAME))
  LDAPLINK = /LIBPATH:$(LDAPSDK_LIBPATH) $(LDAPOBJNAME)
  LIBLDAPDLL_NAMES = $(addsuffix .dll, $(addprefix $(LDAP_LIBPATH)/, $(LDAPDLL_NAME)))
  LDAPSDK_DEP = $(LDAPSDK_LIBPATH)/nsldap32v$(LDAP_SUF).$(DLL_SUFFIX)
  LDAPSDK_PULL_LIBS = lib/nsldapssl32v$(LDAP_SUF).$(LIB_SUFFIX),lib/nsldapssl32v$(LDAP_SUF).$(LDAP_DLL_SUFFIX),lib/nsldap32v$(LDAP_SUF).$(LIB_SUFFIX),lib/nsldap32v$(LDAP_SUF).$(LDAP_DLL_SUFFIX),lib/nsldappr32v$(LDAP_SUF).$(LIB_SUFFIX),lib/nsldappr32v$(LDAP_SUF).$(LDAP_DLL_SUFFIX)

  LIBS_TO_PKG += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  PACKAGE_SETUP_LIBS += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  LIBS_TO_PKG_SHARED += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
  LIBS_TO_PKG_CLIENTS += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(LDAPSDK_LIBPATH)/,$(LDAPDLL_NAME)))
endif

# override LDAP version in OS specific section
ifneq ($(ARCH), WINNT)
# LDAP Does not has PTH version, so here is the hack which treat non PTH
# version as PTH version
  ifeq ($(USE_PTHREADS), 1)
	LDAP_RELEASE = $(LDAP_SBC)/$(LDAPCOMP_DIR)/$(LDAP_VERSION)/$(NSOBJDIR_NAME1)
  else
	LDAP_RELEASE = $(LDAP_SBC)/$(LDAPCOMP_DIR)/$(LDAP_VERSION)/$(LDAPOBJDIR)
  endif
  LDAP_SOLIB_NAMES = ssldap$(LDAP_SUF)$(LDAP_DLL_PRESUF) ldap$(LDAP_SUF)$(LDAP_DLL_PRESUF) prldap$(LDAP_SUF)$(LDAP_DLL_PRESUF)
  ifndef LDAP_NO_LIBLCACHE
	LDAP_SOLIB_NAMES += lcache30$(LDAP_DLL_PRESUF)
  endif
  LDAP_DOTALIB_NAMES =
  LDAP_LIBNAMES = $(LDAP_DOTALIB_NAMES) $(LDAP_SOLIB_NAMES)
  LDAP_SOLIBS = $(addsuffix .$(LDAP_DLL_SUFFIX), $(addprefix $(LIB_PREFIX), $(LDAP_SOLIB_NAMES)))
  LDAPOBJNAME = $(addsuffix .$(LIB_SUFFIX), $(addprefix $(LIB_PREFIX), $(LDAP_DOTALIB_NAMES))) \
		$(LDAP_SOLIBS)
  LDAPSDK_DEP = $(LDAPSDK_LIBPATH)/libldap$(LDAP_SUF).$(DLL_SUFFIX)
  LDAPLINK = -L$(LDAPSDK_LIBPATH) $(addprefix -l,$(LDAP_SOLIB_NAMES))
  LDAP_NOSSL_LINK = -L$(LDAPSDK_LIBPATH) -lldap$(LDAP_SUF)$(LDAP_DLL_PRESUF)
  LDAPSDK_PULL_LIBS = lib/libssldap$(LDAP_SUF)$(LDAP_DLL_PRESUF).$(LDAP_DLL_SUFFIX),lib/libldap$(LDAP_SUF)$(LDAP_DLL_PRESUF).$(LDAP_DLL_SUFFIX),lib/libprldap$(LDAP_SUF)$(LDAP_DLL_PRESUF).$(LDAP_DLL_SUFFIX)

  LIBS_TO_PKG += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  PACKAGE_SETUP_LIBS += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  LIBS_TO_PKG_SHARED += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
  LIBS_TO_PKG_CLIENTS += $(addprefix $(LDAPSDK_LIBPATH)/,$(LDAP_SOLIBS))
endif

LDAP_LIBPATH = $(LDAP_ROOT)/lib
LDAP_INCLUDE = $(LDAP_ROOT)/include
LDAP_TOOLDIR = $(LDAP_ROOT)/tools
LIBLDAP = $(addprefix $(LDAP_LIBPATH)/, $(LDAPOBJNAME))
LDAPSDK_FILES = include,$(LDAPSDK_PULL_LIBS),tools

ifndef LDAPSDK_PULL_METHOD
LDAPSDK_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(LDAPSDK_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	mkdir -p $(LDAP_LIBPATH)
	$(FTP_PULL) -method $(LDAPSDK_PULL_METHOD) \
		-objdir $(LDAP_ROOT) -componentdir $(LDAP_RELEASE) \
		-files $(LDAPSDK_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component LDAPSDK file $@" ; \
	fi

# apache-axis java classes #######################################
AXIS = axis-bin-$(AXIS_VERSION).zip
AXIS_FILES = $(AXIS)
AXIS_RELEASE = $(COMPONENTS_DIR)/axis
#AXISJAR_DIR = $(AXISJAR_RELEASE)/$(AXISJAR_COMP)/$(AXISJAR_VERSION)
AXIS_DIR = $(AXIS_RELEASE)/$(AXIS_VERSION)
AXIS_FILE = $(CLASS_DEST)/$(AXIS)
AXIS_DEP = $(AXIS_FILE) 
AXIS_REL_DIR=$(subst -bin,,$(subst .zip,,$(AXIS)))


# This is java, so there is only one real platform subdirectory

#PACKAGE_UNDER_JAVA += $(AXIS_FILE)

ifndef AXIS_PULL_METHOD
AXIS_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(AXIS_DEP): $(CLASS_DEST) 
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(AXIS_DIR) \
		-files $(AXIS_FILES) -unzip $(CLASS_DEST)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component AXIS files $@" ; \
	fi

###########################################################


# other dsml java classes #######################################
DSMLJAR = activation.jar,jaxrpc-api.jar,jaxrpc.jar,saaj.jar,xercesImpl.jar,xml-apis.jar
DSMLJAR_FILES = $(DSMLJAR)
DSMLJAR_RELEASE = $(COMPONENTS_DIR)
#DSMLJARJAR_DIR = $(DSMLJARJAR_RELEASE)/$(DSMLJARJAR_COMP)/$(DSMLJARJAR_VERSION)
DSMLJAR_DIR = $(DSMLJAR_RELEASE)/dsmljars
DSMLJAR_FILE = $(CLASS_DEST)
DSMLJAR_DEP = $(CLASS_DEST)/activation.jar $(CLASS_DEST)/jaxrpc-api.jar $(CLASS_DEST)/jaxrpc.jar $(CLASS_DEST)/saaj.jar $(CLASS_DEST)/xercesImpl.jar $(CLASS_DEST)/xml-apis.jar

ifndef DSMLJAR_PULL_METHOD
DSMLJAR_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(DSMLJAR_DEP): $(CLASS_DEST) 
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(DSMLJAR_DIR) \
		-files $(DSMLJAR_FILES)

endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component DSMLJAR files $@" ; \
	fi

###########################################################

# XMLTOOLS java classes #######################################
CRIMSONJAR = crimson.jar
CRIMSON_LICENSE = LICENSE.crimson
CRIMSONJAR_FILES = $(CRIMSONJAR),$(CRIMSON_LICENSE)
CRIMSONJAR_RELEASE = $(COMPONENTS_DIR)
CRIMSONJAR_DIR = $(CRIMSONJAR_RELEASE)/$(CRIMSONJAR_COMP)/$(CRIMSONJAR_VERSION)
CRIMSONJAR_FILE = $(CLASS_DEST)/$(CRIMSONJAR)
CRIMSONJAR_DEP = $(CRIMSONJAR_FILE) $(CLASS_DEST)/$(CRIMSON_LICENSE)


# This is java, so there is only one real platform subdirectory

PACKAGE_UNDER_JAVA += $(CRIMSONJAR_FILE)

ifndef CRIMSONJAR_PULL_METHOD
CRIMSONJAR_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(CRIMSONJAR_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(CRIMSONJAR_DIR) \
		-files $(CRIMSONJAR_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component CRIMSONJAR files $@" ; \
	fi

###########################################################

# ANT java classes #######################################
ifeq ($(BUILD_JAVA_CODE),1)
#  (we use ant for building some Java code)
ANTJAR = ant.jar
JAXPJAR = jaxp.jar
ANT_FILES = $(ANTJAR) $(JAXPJAR)
ANT_RELEASE = $(COMPONENTS_DIR)
ANT_HOME = $(ANT_RELEASE)/$(ANT_COMP)/$(ANT_VERSION)
ANT_DIR = $(ANT_HOME)/lib
ANT_DEP = $(addprefix $(CLASS_DEST)/, $(ANT_FILES))
ANT_CP = $(subst $(SPACE),$(PATH_SEP),$(ANT_DEP))
ANT_PULL = $(subst $(SPACE),$(COMMA),$(ANT_FILES))

ifndef ANT_PULL_METHOD
ANT_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(ANT_DEP): $(CLASS_DEST) $(CRIMSONJAR_DEP)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(ANT_DIR) \
		-files $(ANT_PULL)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component ant files $@" ; \
	fi
endif
###########################################################

# Servlet SDK classes #######################################
SERVLETJAR = servlet.jar
SERVLET_FILES = $(SERVLETJAR)
SERVLET_RELEASE = $(COMPONENTS_DIR)
SERVLET_DIR = $(SERVLET_RELEASE)/$(SERVLET_COMP)/$(SERVLET_VERSION)
SERVLET_DEP = $(addprefix $(CLASS_DEST)/, $(SERVLET_FILES))
SERVLET_CP = $(subst $(SPACE),$(PATH_SEP),$(SERVLET_DEP))
SERVLET_PULL = $(subst $(SPACE),$(COMMA),$(SERVLET_FILES))

ifndef SERVLET_PULL_METHOD
SERVLET_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SERVLET_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(SERVLET_DIR) \
		-files $(SERVLET_PULL)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component servlet SDK files $@" ; \
	fi

###########################################################

# LDAP java classes #######################################
LDAPJDK = ldapjdk.jar
LDAPJDK_VERSION = $(LDAPJDK_RELDATE)
LDAPJDK_RELEASE = $(COMPONENTS_DIR)
LDAPJDK_DIR = $(LDAPJDK_RELEASE)
LDAPJDK_IMPORT = $(LDAPJDK_RELEASE)/$(LDAPJDK_COMP)/$(LDAPJDK_VERSION)/$(NSOBJDIR_NAME)
# This is java, so there is only one real platform subdirectory
LDAPJARFILE=$(CLASS_DEST)/ldapjdk.jar
LDAPJDK_DEP=$(LDAPJARFILE)

#PACKAGE_UNDER_JAVA += $(LDAPJARFILE)

ifndef LDAPJDK_PULL_METHOD
LDAPJDK_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(LDAPJDK_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(LDAPJDK_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(LDAPJDK_IMPORT) \
		-files $(LDAPJDK)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component LDAPJDK file $@" ; \
	fi

###########################################################

# MCC java classes - the Mission Control Console #########
MCC_VERSION=$(MCC_RELDATE)$(SEC_SUFFIX)
#
MCCJAR = mcc$(MCC_REL).jar
MCCJAR_EN = mcc$(MCC_REL)_en.jar
NMCLFJAR = nmclf$(MCC_REL).jar
NMCLFJAR_EN = nmclf$(MCC_REL)_en.jar
BASEJAR = base.jar
#MCC_RELEASE=$(COMPONENTS_DIR_DEV)
MCC_RELEASE=$(COMPONENTS_DIR)
MCC_JARDIR = $(MCC_RELEASE)/$(MCC_COMP)/$(MCC_VERSION)/jars
MCCJARFILE=$(CLASS_DEST)/$(MCCJAR)
NMCLFJARFILE=$(CLASS_DEST)/$(NMCLFJAR)
BASEJARFILE=$(CLASS_DEST)/$(BASEJAR)

MCC_DEP = $(BASEJARFILE)
MCC_FILES=$(MCCJAR),$(MCCJAR_EN),$(NMCLFJAR),$(NMCLFJAR_EN),$(BASEJAR)

#PACKAGE_UNDER_JAVA += $(addprefix $(CLASS_DEST)/,$(subst $(COMMA),$(SPACE),$(MCC_FILES)))

ifndef MCC_PULL_METHOD
MCC_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(MCC_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(MCC_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(MCC_JARDIR) \
		-files $(MCC_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component MCC file $@" ; \
	fi

###########################################################

###########################################################
### Perldap package #######################################

PERLDAP_COMPONENT_DIR = $(COMPONENTS_DIR)/perldap/$(PERLDAP_VERSION)/$(NSOBJDIR_NAME_32)
PERLDAP_ZIP_FILE = perldap14.zip

###########################################################

# JSS classes - for the Mission Control Console ######
JSSJAR = jss$(JSS_JAR_VERSION).jar
JSSJARFILE = $(CLASS_DEST)/$(JSSJAR)
JSS_RELEASE = $(COMPONENTS_DIR)/$(JSS_COMP)/$(JSS_VERSION)
JSS_DEP = $(JSSJARFILE)

#PACKAGE_UNDER_JAVA += $(JSSJARFILE)

ifndef JSS_PULL_METHOD
JSS_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(JSS_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
ifdef VSFTPD_HACK
# work around vsftpd -L problem
	$(FTP_PULL) -method $(JSS_PULL_METHOD) \
		-objdir $(CLASS_DEST)/jss -componentdir $(JSS_RELEASE) \
        -files xpclass.jar
	mv $(CLASS_DEST)/jss/xpclass.jar $(CLASS_DEST)/$(JSSJAR)
	rm -rf $(CLASS_DEST)/jss
else
	$(FTP_PULL) -method $(JSS_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(JSS_RELEASE) \
		-files $(JSSJAR)
endif
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component JSS file $@" ; \
	fi

###########################################################

###########################################################
### Admin Server package ##################################

ADMIN_REL = $(ADM_VERSDIR)
ADMIN_REL_DATE = $(ADM_VERSION)
ADMIN_FILE = admserv.tar.gz
ADMIN_FILE_TAR = admserv.tar
ADMSDKOBJDIR = $(NSCONFIG)$(NSOBJDIR_TAG).OBJ
IMPORTADMINSRV_BASE=$(COMPONENTS_DIR)/$(ADMIN_REL)/$(ADMIN_REL_DATE)
IMPORTADMINSRV = $(IMPORTADMINSRV_BASE)/$(NSOBJDIR_NAME_32)
ADMSERV_DIR=$(ABS_ROOT_PARENT)/dist/$(NSOBJDIR_NAME)/admserv
ADMSERV_DEP = $(ADMSERV_DIR)/setup$(EXE_SUFFIX)

ifdef FORTEZZA
  ADM_VERSION = $(ADM_RELDATE)F
else
  ifeq ($(SECURITY), domestic)
    ADM_VERSION = $(ADM_RELDATE)D
  else
    ifneq ($(ARCH), IRIX)
        ADM_VERSION = $(ADM_RELDATE)E
    else
        ADM_VERSION = $(ADM_RELDATE)D
    endif
  endif
endif

ADM_VERSION = $(ADM_RELDATE)$(SEC_SUFFIX)
ADM_RELEASE = $(COMPONENTS_DIR)/$(ADM_VERSDIR)/$(ADM_VERSION)/$(NSOBJDIR_NAME)

ifndef ADMSERV_PULL_METHOD
ADMSERV_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

ifndef ADMSERV_DEPS
ADMSERV_DEPS = $(COMPONENT_DEPS)
endif
#IMPORTADMINSRV = /share/builds/sbsrel1/admsvr/admsvr62/ships/20030702.2/spd04_Solaris8/SunOS5.8-domestic-optimize-normal
#ADM_RELEASE = /share/builds/sbsrel1/admsvr/admsvr62/ships/20030702.2/spd04_Solaris8/SunOS5.8-domestic-optimize-normal
$(ADMSERV_DEP): $(ABS_ROOT_PARENT)/dist/$(NSOBJDIR_NAME)
ifdef ADMSERV_DEPS
	$(FTP_PULL) -method $(ADMSERV_PULL_METHOD) \
		-objdir $(ADMSERV_DIR) -componentdir $(IMPORTADMINSRV) \
		-files $(ADMIN_FILE) -unzip $(ADMSERV_DIR)
endif
	@if [ ! -f $@ ] ; \
	then echo "Error: could not get component ADMINSERV file $@" ; \
	exit 1 ; \
	fi
### Admin Server END ######################################



### SASL package ##########################################

SASL_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/sasl
SASL_RELEASE = $(COMPONENTS_DIR)/sasl/$(SASL_VERSDIR)/$(SASL_RELDATE)/$(NSOBJDIR_NAME)
SASL_LIBPATH = $(SASL_BUILD_DIR)/lib
SASL_BINPATH = $(SASL_BUILD_DIR)/bin
SASL_INCLUDE = $(SASL_BUILD_DIR)/include
SASL_DEP = $(SASL_INCLUDE)/sasl.h
ifeq ($(ARCH), WINNT)
SASL_LINK = /LIBPATH:$(SASL_LIBPATH) sasl.lib
else
ifeq ($(ARCH), SOLARIS)
GSSAPI_LIBS=-lgss
endif
#ifeq ($(ARCH), HPUX)
GSSAPI_LIBS=-lgss
#endif
ifeq ($(ARCH), Linux)
GSSAPI_LIBS=-L/usr/kerberos/lib -lgssapi_krb5
endif
SASL_LINK = -L$(SASL_LIBPATH) -lsasl $(GSSAPI_LIBS)
#SASL_LINK = -L$(SASL_LIBPATH) -lsasl
endif

ifndef SASL_PULL_METHOD
SASL_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SASL_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
ifdef VSFTPD_HACK
	$(FTP_PULL) -method $(SASL_PULL_METHOD) \
		-objdir $(SASL_BUILD_DIR) -componentdir $(SASL_RELEASE) \
		-files lib
	$(FTP_PULL) -method $(SASL_PULL_METHOD) \
		-objdir $(SASL_INCLUDE) -componentdir $(SASL_RELEASE)/../public \
		-files .\*.h
else
	$(FTP_PULL) -method $(SASL_PULL_METHOD) \
		-objdir $(SASL_BUILD_DIR) -componentdir $(SASL_RELEASE) \
		-files lib,include

endif
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component SASL file $@" ; \
	fi

###########################################################

### JSP compiler package ##################################

JSPC_REL = $(JSPC_VERSDIR)
JSPC_REL_DATE = $(JSPC_VERSION)
JSPC_FILES = jasper-compiler.jar jasper-runtime.jar
JSPC_RELEASE = $(COMPONENTS_DIR)
JSPC_DIR = $(JSPC_RELEASE)/$(JSPC_COMP)/$(JSPC_VERSION)
JSPC_DEP = $(addprefix $(CLASS_DEST)/, $(JSPC_FILES))
JSPC_CP = $(subst $(SPACE),$(PATH_SEP),$(JSPC_DEP))
JSPC_PULL = $(subst $(SPACE),$(COMMA),$(JSPC_FILES))

ifndef JSPC_PULL_METHOD
JSPC_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(JSPC_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(CLASS_DEST) -componentdir $(JSPC_DIR) \
		-files $(JSPC_PULL)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component jspc files $@" ; \
	fi

###########################################################

### ICU package ##########################################

ICU_LIB_VERSION = 24
ICU_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/libicu
ICU_RELEASE = $(COMPONENTS_DIR)/libicu/$(ICU_VERSDIR)/$(ICU_RELDATE)/$(NSOBJDIR_NAME)
ICU_LIBPATH = $(ICU_BUILD_DIR)/lib
ICU_BINPATH = $(ICU_BUILD_DIR)/bin
ICU_INCPATH = $(ICU_BUILD_DIR)/include
ICU_DEP = $(ICU_INCPATH)/unicode/unicode.h
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
LIBS_TO_PKG_CLIENTS += $(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_BINPATH)/,$(ICU_DLLNAMES)))
else
ICU_LIBNAMES = icui18n icuuc icudata
ICULINK = -L$(ICU_LIBPATH) $(addprefix -l, $(ICU_LIBNAMES))
LIBS_TO_PKG += $(addsuffix .$(ICU_LIB_VERSION),$(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_LIBPATH)/,$(addprefix lib,$(ICU_LIBNAMES)))))
LIBS_TO_PKG_SHARED += $(addsuffix .$(ICU_LIB_VERSION),$(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_LIBPATH)/,$(addprefix lib,$(ICU_LIBNAMES)))))
LIBS_TO_PKG_CLIENTS += $(addsuffix .$(ICU_LIB_VERSION),$(addsuffix .$(DLL_SUFFIX),$(addprefix $(ICU_LIBPATH)/,$(addprefix lib,$(ICU_LIBNAMES)))))
#LIBS_TO_PKG = $(addsuffix $(addprefix lib,$(ICU_LIBNAMES))
endif

BINS_TO_PKG_SHARED += $(ICU_BINPATH)/uconv$(EXE_SUFFIX)

ifndef ICU_PULL_METHOD
ICU_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(ICU_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(ICU_PULL_METHOD) \
		-objdir $(ICU_BUILD_DIR) -componentdir $(ICU_RELEASE) \
		-files lib,include,bin
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component ICU file $@" ; \
	fi

###########################################################
