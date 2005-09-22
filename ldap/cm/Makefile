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
#
# Makefile to Create packages for Directory Server and LDAP SDK
#

default help :
	@echo "The following targets are available:"
	@echo ""
	@echo "     releaseDirectory"
	@echo "     packageDirectory"

	@echo ""


BUILD_ROOT=../..
TREE_ROOT=$(BUILD_ROOT)

# make sure we pull the admin server component here
ADMSERV_DEPS = 1

include $(BUILD_ROOT)/nsconfig.mk
include $(BUILD_ROOT)/ldap/nsldap.mk
include $(BUILD_ROOT)/ldap/javarules.mk
include $(BUILD_ROOT)/ns_usesh.mk

NSDISTMODE = copy

ifneq ($(ARCH), WINNT)
# tar must support the -h flag to follow symlinks and not copy them
TAR=tar
endif

MMDD = $(shell date +%m.%d)

# we don't want to build with warnings-as-errors for the cm/ stuff, because
# it's crappy C++ code which is LITTERED with warnings, most of which we
# can't fix because it comes from files in dist/, etc.
ifeq ($(ARCH), Linux)
CFLAGS := $(subst -Werror,,$(CFLAGS))
endif

# Absolute path to .ldap/cm is needed for AIX packaging.  Due to NFS problems on AIX 4.2.1
# the build script RSH's to cindercone but it tries to cd to $(RELDIR) which is relative
# to the CWD of this Makefile in current build environment.  So, the cd to RELDIR fails
# since the RSH set the new CWD to /u/svbld.  By determining its location in the tree then
# cd'ing to same location on cindercone the cd to RELDIR finds the correct "release" directory.
# This hack can go away when we use a version of AIX that fixes the NFS problem(s). (tfox)
TMP_CM_PWD := $(shell pwd)
LDAP_CM_ABS_PATH := $(TMP_CM_PWD:/tmp_mnt/%=/%) # AIX prefixes /tmp_mnt to path.

ifneq ($(ARCH), WINNT)
RELTOOLS=$(RELTOOLSPATH)/ftpname_new.pl
SUF=-suf .tar
SUFEXE=-suf .tar.gz
BACKGROUND=&
else
RELTOOLSDIR=$(RELTOOLSPATH)
NT_RELTOOLS=$(RELTOOLSDIR)
RELTOOLS=perl $(RELTOOLSDIR)/ftpname.pl
SUF=-suf .zip
SUFEXE=-suf .exe
ifdef BUILD_SHIP
ifndef BuildDir
HOST=$(shell hostname)
BuildDir=$(shell cd $(RELTOOLSDIR);perl getdefaults -var BuildDir -if $(RELTOOLSDIR)/init/directory/directory5.init -machine $(HOST)) 
endif
endif
endif

ifdef USE_64
VERSION=-ver 7.1-64bit
else
VERSION=-ver 7.1
endif

ifeq ($(ARCH), HPUX)
RSH=remsh 
REMSH=$(RSH) anuurn -l root
else
RSH=rsh 
REMSH=$(RSH) anuurn -l root
endif


ifdef PRODUCT_MARKET
ifeq ($(PRODUCT_MARKET), JA)
INTL=-intl ja
INTL_INSTALL=-DJA=1
endif
ifeq ($(PRODUCT_MARKET), EU)
INTL=-intl eu 
endif
else
INTL=-intl us
endif

ifeq ($(BUILD_SECURITY), domestic)
SEC=-sec domestic
ifdef FORTEZZA
SEC=-sec fortezza
endif 
else
SEC=-sec export 
endif 

ifneq ($(DEBUG), optimize)
DBG=-debug full
else
DBG=-debug optimize
endif

FTPNAME = $(shell $(RELTOOLS) -name directory $(VERSION) $(INTL) $(SEC) $(DBG) $(SUF)) 
FTPNAMEGZ = $(shell $(RELTOOLS) -name directory $(VERSION) $(INTL) $(SEC) $(DBG) $(SUFEXE) ) 

# regular NT 

ifeq ($(ARCH), WINNT)

INSTALL = nsinstall -t
PLAT_ID = 32
CONVERTER = cp
DOTTXT = .txt

# NT Alpha 

ifeq ($(PROCESSOR_ARCHITECTURE), ALPHA)
PLAT_ID = 64
endif

else
INSTALL = $(OBJDIR)/nsinstall -t
CONVERTER = cp
endif

PROD_ID = dk

ifeq ($(DEBUG), optimize)
STRIP = strip
BLDTYPE_ID =
else
STRIP = true
BLDTYPE_ID = d
endif

DOTDLL = .$(DLL_SUFFIX)
DOTLIB = .$(LIB_SUFFIX)

ifeq ($(ARCH), WINNT)
DOTEXE = .exe
PACKAGE_STAGE_DIR=$(OBJDIR)/package
endif

SHARETOP = $(COMPONENTS_DIR)/ldapsdk
BUILD_DATE = $(shell date +%Y%m%d)
SHAREDIR = $(SHARETOP)/$(BUILD_DATE)/$(NC_BUILD_FLAVOR)
#ADM_VERSDIR = admserv40
#ADM_RELDATE = untested/19980119
#IMPORTADMINSRV = $(IMPORTADMINSRV_BASE)/$(NSOBJDIR_NAME_32)
IMPORTADMINSRVNOTARBASE = $(COMPONENTS_DIR_DEV)/$(ADM_VERSDIR)/$(ADM_VERSION)/$(NSOBJDIR_NAME)
# these are files and directories in the import adminsrv directory which we don't
# make a local copy of, we just import directly into the tar file or create a
# symlink to
ADMIN_IMPORTS=base admin
ADMIN_SERVER_TARGZ=adminserver.tar.gz
ADMIN_IMPORTS_TARGZ=$(ADMIN_SERVER_TARGZ)

# Release directory for ldapsdk
RELSDK = $(BUILD_DRIVE)$(RELTOP)/ldapsdk/$(OBJDIR_BASE)
RELJDK = $(BUILD_DRIVE)$(RELTOP)/ldapjdk

# these are files we need to put in the command line/console only package
#LDAPSDK_IMPORTS=ldapsearch ldapdelete ldapmodify

# perl script to add the slapd information to the base installer
# setup information file
FIX_SETUP_INF = $(BUILD_ROOT)/ldap/cm/fixSetupInf.pl
FIX_BASE_INF = $(BUILD_ROOT)/ldap/cm/fixBaseInf.pl

ABSBUILD_ROOT = $(shell cd $(BUILD_ROOT); pwd)
ABSRELDIR = $(ABSBUILD_ROOT)/built/release
GENRPMPATCH = $(ABSBUILD_ROOT)/ldap/cm/genRpmPatch.pl
PATCHINF = $(ABSBUILD_ROOT)/ldap/cm/fedora-patch.inf
DATETIME = $(shell date +%Y%m%d-%H%M%S)
SPEXT = .SP.$(DATETIME)

# This is the directory where we put what we're making: the files which go on the CD.
ifndef INSTDIR
ifeq ($(ARCH), WINNT)
INSTDIR = $(TREE_ROOT)/../$(MMDD)/$(NS_BUILD_FLAVOR)
else
INSTDIR = $(BUILD_DRIVE)$(BUILD_ROOT)/../$(MMDD)/$(NS_BUILD_FLAVOR)
endif
endif
ABS_INSTDIR = $(shell cd $(INSTDIR); pwd)

ifdef BUILD_PATCH
PATCHINSTDIR = $(ABS_INSTDIR)-SP
SLAPDSP = slapd-71
endif

INST_TARGET_RESKIT=$(INSTDIR)/reskit
INST_TARGET_INTL=./$(PRODUCT_MARKET)dir

LDAPDIR = $(BUILD_ROOT)/ldap

NSDIST = $(BUILD_ROOT)/../dist

DS_JAR_SRC_PATH = $(NSDIST)/$(BUILD_DEBUG)
DS_JAR_DEST_PATH = java/jars
XMLTOOLS_JAR_FILE = xmltools.jar

UNZIP=unzip -o
UNZIPNOPATHS=$(UNZIP) -j
ZIP=zip
ZIP_FLAGS=-r -T
EXCLUDED_FILES=-x lib/aolsnauth-plugin.*
# Linux Zip has problems zipping gif file over NFS (??)
ifeq ($(ARCH), Linux)
ZIP_FLAGS=-r -T -n .gif
endif

ifeq ($(USE_64), 1)
MAKEARCH=$(ARCH)64
else
MAKEARCH=$(ARCH)
endif

PACKAGE_SETUP_LIBS_32=$(subst $(NS64TAG),,$(PACKAGE_SETUP_LIBS))

# set the values of the macros used by rpmbuild
ifdef BUILD_RPM
# name and version of RPM - must correspond to the spec file - these get branded
	RPM_BASE_NAME=fedora
	RPM_VERSION=7.1
	RPM_FILE_BASE=$(RPM_BASE_NAME)-ds-$(RPM_VERSION)
	RPM_ARCH = $(shell uname -i)
# root dir for RPM built and temp files
	ABS_TOPDIR = $(shell cd $(INSTDIR)/.. ; pwd)
	RPM_TOPDIR = --define "_topdir $(ABS_TOPDIR)"
# location of source tarball
	RPM_SOURCEDIR = --define "_sourcedir $(ABS_TOPDIR)"
# location of staging area built into RPM
	RPM_BUILDDIR = --define "_builddir $(ABS_INSTDIR)"
# location of final RPM file
	RPM_RPMDIR = --define "_rpmdir $(ABS_TOPDIR)"
# location of source RPM
	RPM_SRPMDIR = --define "_srcrpmdir $(ABS_TOPDIR)"
ifneq ($(BUILD_DEBUG), optimize)
	RPM_FLAVOR = dbg
else
	RPM_FLAVOR = opt
endif
endif

# Borland libraries are build on NT only

dummy:
	-@echo SITEHACK = $(SITEHACK)
	-@echo PACKAGE_SRC_DEST = $(PACKAGE_SRC_DEST)

importAdmin:

releaseDirectory:
# LIBS_TO_PKG is defined in components.mk - these are component files (not directories) to install
# with the other component files that we don't necessarily pick up from the admin server build
# see below for windows packaging
ifneq ($(ARCH), WINNT)
	for file in $(LIBS_TO_PKG) ; \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/bin/slapd/lib ; \
	fi ; \
	done
# these are files to copy to the shared/bin directory - ldap cmd line tools, sec tools, etc.
	for file in $(BINS_TO_PKG_SHARED); \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/shared/bin ; \
	fi ; \
	done
# these are files to copy to the shared/lib directory - ldap cmd line tools, sec tools, etc.
	for file in $(LIBS_TO_PKG_SHARED); \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/shared/lib ; \
	fi ; \
	done
# these are libs to copy to clients/lib on unix to support dsgw & pb
ifdef USE_DSGW
	for file in $(LIBS_TO_PKG_CLIENTS); \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/clients/lib ; \
	fi ; \
	done
endif
endif

# PACKAGE_SRC_DEST is defined in components.mk - these are component files and directories to install
# with the other component files that we don't necessarily pick up from the admin server build
# these can go in any directory
	for dest in $(PACKAGE_SRC_DEST) ; \
	do if [ "$$src" ] ; \
	then if [ ! -d $(RELDIR)/$$dest ] ; then mkdir -p $(RELDIR)/$$dest ; fi ; \
		if [ -d $$src ] ; \
		then bs=`basename $$src` ; \
			if [ -d $(RELDIR)/$$dest/$$bs ] ; then rm -rf $(RELDIR)/$$dest/$$bs ; fi ; \
			cp -r $$src $(RELDIR)/$$dest ; \
		else $(INSTALL) -m 755 $$src $(RELDIR)/$$dest ; \
		fi ; \
		src= ; \
	else src=$$dest ; \
	fi ; \
	done

# install the DSMLGW into the client directory
ifeq ($(USE_DSMLGW), 1)
	$(MKDIR) $(RELDIR)/clients/dsmlgw
	$(CP) -R $(NSDIST)/classes/$(AXIS_REL_DIR)/webapps/axis/*  $(RELDIR)/clients/dsmlgw/

	$(INSTALL) -m 644 $(NSDIST)/dsmlgw/dsmlgw.jar $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/dsmlgw/misc/server-config.wsdd $(RELDIR)/clients/dsmlgw/WEB-INF
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/dsmlgw/misc/web-app_2_3.dtd $(RELDIR)/clients/dsmlgw/


# now time to move the necessary jars in place
	$(INSTALL) -m 644 $(NSDIST)/classes/ldapjdk.jar  $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(NSDIST)/classes/activation.jar  $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(NSDIST)/classes/jaxrpc-api.jar  $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(NSDIST)/classes/jaxrpc.jar  $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(NSDIST)/classes/saaj.jar  $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(NSDIST)/classes/xercesImpl.jar $(RELDIR)/clients/dsmlgw/WEB-INF/lib
	$(INSTALL) -m 644 $(NSDIST)/classes/xml-apis.jar $(RELDIR)/clients/dsmlgw/WEB-INF/lib
endif # USE_DSMLGW

# PACKAGE_UNDER_JAVA is defined in components.mk - these are component .jar files to install
# with the other component files that we don't necessarily pick up from the admin server build
# these go in the java/ directory
ifdef PACKAGE_UNDER_JAVA
	for file in $(PACKAGE_UNDER_JAVA) ; \
		do if [ -f $$file ] ; \
		then $(INSTALL) -m 755 $$file $(RELDIR)/$(DS_JAR_DEST_PATH) ; \
	fi ; \
	done
endif

	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/ldif/*.ldif $(RELDIR)/bin/slapd/install/ldif
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/dsml/*.dsml $(RELDIR)/bin/slapd/install/dsml

### Package up the orgchart ###
ifeq ($(USE_ORGCHART), 1)
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/*.gif $(RELDIR)/clients/orgchart/html
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/*.html $(RELDIR)/clients/orgchart/html
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/*.css $(RELDIR)/clients/orgchart/html
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/*.tmpl $(RELDIR)/clients/orgchart

ifeq ($(ARCH), WINNT)
	$(INSTALL) -m 755 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/*.bat $(RELDIR)/clients/orgchart/bin
	$(INSTALL) -m 755 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/*.pl $(RELDIR)/clients/orgchart/bin
else
	$(MKDIR) $(RELDIR)/clients/orgchart/bin
	$(CP) $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/org.pl $(RELDIR)/clients/orgchart/bin/org
	$(CP) $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/clients/orgchart/myorg.pl $(RELDIR)/clients/orgchart/bin/myorg
	chmod 755 $(RELDIR)/clients/orgchart/bin/org
	chmod 755 $(RELDIR)/clients/orgchart/bin/myorg
endif
endif # USE_ORGCHART
### end orgchart package ###

	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/schema/*.ldif $(RELDIR)/bin/slapd/install/schema
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/schema/slapd-collations.conf $(RELDIR)/bin/slapd/install/config

# libdb for windows special and smartheap
ifeq ($(ARCH), WINNT)
	$(INSTALL) -m 755 $(DB_LIBPATH)/$(DB_LIBNAME).$(DLL_SUFFIX)  $(RELDIR)/bin/slapd/server
	$(INSTALL) -m 755 $(SASL_LIBPATH)/*.$(DLL_SUFFIX)  $(RELDIR)/bin/slapd/server
#Install smartheap dll in the server binary directory
ifeq ($(DEBUG), optimize)
	$(INSTALL) -m 755 $(SH_LIBPATH)/shsmp.$(DLL_SUFFIX)  $(RELDIR)/bin/slapd/server
	$(INSTALL) -m 755 $(SH_LIBPATH)/shsmp.$(DLL_SUFFIX)  $(RELDIR)/lib
endif
endif

ifeq ($(ARCH), SOLARIS)
ifndef LDAP_DONT_USE_SMARTHEAP
	$(INSTALL) -m 755 $(SH_LIBPATH)/libsh.$(DLL_SUFFIX)  $(RELDIR)/bin/slapd/server
endif
endif

ifeq ($(ARCH), HPUX)
ifeq ($(DEBUG), optimize)
ifndef LDAP_DONT_USE_SMARTHEAP
	$(INSTALL) -m 755 $(SH_LIBPATH)/libsh.$(DLL_SUFFIX)  $(RELDIR)/bin/slapd/server
endif
endif
endif

# the plugin API
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/slapi-plugin.h $(RELDIR)/plugins/slapd/slapi/include
	$(INSTALL) -m 644 $(NSPR_INCDIR)/*.h $(RELDIR)/plugins/slapd/slapi/include
	$(INSTALL) -m 644 $(NSPR_INCDIR)/obsolete/*.h $(RELDIR)/plugins/slapd/slapi/include/obsolete
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/slapi-plugin-compat4.h $(RELDIR)/plugins/slapd/slapi/include
#	if [ -f $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/docs/plugin/README ] ; \
#	then $(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/docs/plugin/README $(RELDIR)/plugins/slapd ; \
#	fi
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/*.c $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/*.h $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(DB_INCLUDE)/db.h $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/README $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/clients/*.* $(RELDIR)/plugins/slapd/slapi/examples/clients
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/clients/README $(RELDIR)/plugins/slapd/slapi/examples/clients
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/plugins/distrib/*.c $(RELDIR)/plugins/slapd/slapi/examples/distrib
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/plugins/distrib/README $(RELDIR)/plugins/slapd/slapi/examples/distrib
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/Makefile.$(MAKEARCH) $(RELDIR)/plugins/slapd/slapi/examples
	$(MV) $(RELDIR)/plugins/slapd/slapi/examples/Makefile.$(MAKEARCH) $(RELDIR)/plugins/slapd/slapi/examples/Makefile
	$(CP) $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/plugins/distrib/Makefile.$(MAKEARCH) $(RELDIR)/plugins/slapd/slapi/examples/distrib/Makefile
ifeq ($(ARCH), WINNT)
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/testplugin.mak $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/testplugin.def $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/slapd/test-plugins/testplugin.dsp $(RELDIR)/plugins/slapd/slapi/examples
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/plugins/distrib/distrib.dsp $(RELDIR)/plugins/slapd/slapi/examples/distrib
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/plugins/distrib/libdistrib.def $(RELDIR)/plugins/slapd/slapi/examples/distrib
endif
ifeq ($(ARCH), WINNT)
	$(INSTALL) -m 755 $(LIBSLAPD) $(RELDIR)/plugins/slapd/slapi/lib
	$(INSTALL) -m 755 $(NSPR_LIBPATH)/*.lib $(RELDIR)/plugins/slapd/slapi/lib
# needed only for testdatainterop plugin in the plugins examples
	mkdir -p $(RELDIR)/plugins/slapd/slapi/examples/lib
	$(INSTALL) -m 755 $(DB_LIBPATH)/$(DB_LIBNAME).lib $(RELDIR)/plugins/slapd/slapi/examples/lib
endif

# the httpd library
ifneq ($(ARCH), WINNT)
	$(INSTALL) -m 755 $(OBJDIR)/$(NSHTTPD_DLL)$(DLL_PRESUF).$(DLL_SUFFIX)* $(RELDIR)/bin/slapd/lib
	$(INSTALL) -m 755 $(OBJDIR)/$(NSHTTPD_DLL)$(DLL_PRESUF).$(DLL_SUFFIX)* $(RELDIR)/clients/lib
endif

# install the ds jar file in the <server root>/$(DS_JAR_DEST_PATH) directory
# also install the other jar files we use
ifeq ($(USE_CONSOLE), 1)
	$(INSTALL) -m 644 $(NSDIST)/classes/$(LDAPCONSOLEJAR) $(RELDIR)/$(DS_JAR_DEST_PATH)
	$(INSTALL) -m 644 $(NSDIST)/classes/$(LDAPCONSOLEJAR_EN) $(RELDIR)/$(DS_JAR_DEST_PATH)
endif
ifeq ($(USE_JAVATOOLS), 1)
	$(INSTALL) -m 644 $(DS_JAR_SRC_PATH)/$(XMLTOOLS_JAR_FILE) $(RELDIR)/$(DS_JAR_DEST_PATH)
	$(INSTALL) -m 644 $(NSDIST)/classes/$(CRIMSONJAR) $(RELDIR)/$(DS_JAR_DEST_PATH)
	$(INSTALL) -m 644 $(NSDIST)/classes/$(CRIMSON_LICENSE) $(RELDIR)/$(DS_JAR_DEST_PATH)
endif

# Images for IM Presence plugin
ifdef BUILD_PRESENCE
	$(INSTALL) -m 644 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/servers/plugins/presence/images/*.gif $(RELDIR)/bin/slapd/install/presence
endif

# docs
	-$(RM) -r $(RELDIR)/manual/en/slapd
	-$(MKDIR) $(RELDIR)/manual/en/slapd
	if [ "$(DSDOC_DIR)" -a -d "$(DSDOC_DIR)" ] ; then \
	  $(INSTALL) -m 644 $(OBJDIR)/manual/slapd/help/*.* $(RELDIR)/manual/en/slapd/help ; \
	  $(INSTALL) -m 644 $(OBJDIR)/manual/slapd/index.map $(RELDIR)/manual/en/slapd/ ; \
	  $(INSTALL) -m 644 $(OBJDIR)/manual/slapd/index.htm $(RELDIR)/manual/en/slapd/ ; \
	  $(INSTALL) -m 644 $(OBJDIR)/manual/slapd/pixel.gif $(RELDIR)/manual/en/slapd/ ; \
	  $(INSTALL) -m 644 $(OBJDIR)/manual/slapd/topicindex.htm $(RELDIR)/manual/en/slapd/ ; \
	  $(INSTALL) -m 644 $(OBJDIR)/manual/slapd/tokens.map $(RELDIR)/manual/en/slapd/ ; \
	  cd $(RELDIR)/manual/en/slapd; \
	  $(UNZIP) $(DSDOC_DIR)/$(DSDOC_COPYRIGHT); \
	  $(UNZIP) $(DSDOC_DIR)/$(DSDOC_CLIENTS); \
	fi

ifdef USE_PURIFY
	-$(INSTALL) -m 755 $(DB_LIBPATH)/*.so_pure* $(RELDIR)/lib
	-$(INSTALL) -m 755 $(NSCP_DISTDIR)/lib/*.so_pure* $(RELDIR)/lib
	rm -f $(RELDIR)/bin/slapd/server/ns-slapd
	mv -f $(RELDIR)/bin/slapd/server/ns-slapd.pure $(RELDIR)/bin/slapd/server/ns-slapd
endif
ifdef USE_QUANTIFY
	rm -f $(RELDIR)/bin/slapd/server/ns-slapd
	mv -f $(RELDIR)/bin/slapd/server/ns-slapd.quantify $(RELDIR)/bin/slapd/server/ns-slapd
endif

# Copy db tools 
	$(INSTALL) -m 755 $(DB_BINPATH)/db_printlog* $(RELDIR)/bin/slapd/server
	$(INSTALL) -m 755 $(DB_BINPATH)/db_verify* $(RELDIR)/bin/slapd/server

	$(INSTALL) -m 755 $(OBJDIR)/lib/libsi18n/ns-slapd.properties $(RELDIR)/bin/slapd/property;

# Install LDAP Readme and License files at root of SLAPD Release sub-directory.
ifeq ($(ARCH), WINNT)
	if [ -d $(LDAPDIR)/docs ] ; then \
		cd $(LDAPDIR)/docs; \
		perl $(NT_RELTOOLS)/unx2dos.plx WINNT LICENSE.txt $(RELDIR)/bin/slapd; \
		perl $(NT_RELTOOLS)/unx2dos.plx WINNT LICENSE.txt $(RELDIR); \
		perl $(NT_RELTOOLS)/unx2dos.plx WINNT README.txt $(RELDIR)/bin/slapd; \
		perl $(NT_RELTOOLS)/unx2dos.plx WINNT README.txt $(RELDIR); \
	fi
else
	if [ -d $(LDAPDIR)/docs ] ; then \
		cd $(LDAPDIR)/docs; \
		$(INSTALL) -m 755 README.txt LICENSE.txt $(RELDIR)/bin/slapd; \
		$(INSTALL) -m 755 README.txt LICENSE.txt $(RELDIR); \
	fi
endif

# include the old configuration files in the package so we can use them
# for comparison purposes during migration
	$(INSTALL) -m 444 $(LDAPDIR)/cm/v1confs/*.* $(RELDIR)/bin/slapd/install/version1
	$(INSTALL) -m 444 $(LDAPDIR)/cm/v3confs/*.* $(RELDIR)/bin/slapd/install/version3
	$(INSTALL) -m 444 $(LDAPDIR)/cm/v4confs/40/*.*  $(RELDIR)/bin/slapd/install/version4/40
	$(INSTALL) -m 444 $(LDAPDIR)/cm/v4confs/41/*.*  $(RELDIR)/bin/slapd/install/version4/41
	$(INSTALL) -m 444 $(LDAPDIR)/cm/v4confs/411/*.* $(RELDIR)/bin/slapd/install/version4/411
	$(INSTALL) -m 444 $(LDAPDIR)/cm/v4confs/412/*.* $(RELDIR)/bin/slapd/install/version4/412

# for RPM, include the post install setup program
ifdef BUILD_RPM
	$(INSTALL) -m 755 $(BUILD_DRIVE)$(BUILD_ROOT)/ldap/cm/newinst/setup $(RELDIR)/setup
endif # BUILD_RPM

	find $(RELDIR) -exec chmod go-w {} \;
# $(RELDIR)/bin/slapd/server may host a core file.
# For security reason, it's readable only by the owner
	chmod 700 $(RELDIR)/bin/slapd/server

# this is the rule to pull the Infozip utilities
ifndef INFOZIP_PULL_METHOD
INFOZIP_PULL_METHOD = FTP
endif

$(INSTDIR)/tools/infozip.zip:
	$(RM) $@
	$(FTP_PULL) -method $(INFOZIP_PULL_METHOD) \
		-objdir $(dir $@) \
		-componentdir $(COMPONENTS_DIR)/infozip/$(INFOZIP_RELDATE)/$(NSOBJDIR_NAME_32) \
		-files infozip.zip
	@if [ ! -f $@ ] ; \
	then echo "Error: could not get component INFOZIP file $@" ; \
	exit 1 ; \
	fi

# this is the rule to pull PerLDAP
ifndef PERLDAP_PULL_METHOD
PERLDAP_PULL_METHOD = FTP
endif

$(INSTDIR)/perldap/$(PERLDAP_ZIP_FILE):
	$(RM) $@
	$(FTP_PULL) -method $(PERLDAP_PULL_METHOD) \
		-objdir $(dir $@) \
		-componentdir $(PERLDAP_COMPONENT_DIR) \
		-files $(notdir $@),perldap.inf
	@if [ ! -f $@ ] ; \
	then echo "Error: could not get component PERLDAP file $@" ; \
	exit 1 ; \
	fi
	$(PERL) -w fixPerlDAPInf.pl $(dir $@)/perldap.inf

# this is the rule to pull nsPerl
ifndef NSPERL_PULL_METHOD
NSPERL_PULL_METHOD = FTP
endif

$(INSTDIR)/nsperl/$(NSPERL_ZIP_FILE):
	$(RM) $@
	$(FTP_PULL) -method $(NSPERL_PULL_METHOD) \
		-objdir $(dir $@) \
		-componentdir $(NSPERL_COMPONENT_DIR) \
		-files $(notdir $@),nsperl.inf
	@if [ ! -f $@ ] ; \
	then echo "Error: could not get component NSPERL file $@" ; \
	exit 1 ; \
	fi
	$(PERL) -w fixNSPerlInf.pl $(dir $@)/nsperl.inf nsperl561
# one more hack to nsperl - we must remove LDIF.pm because it
# conflicts with the one in perldap - bug 600138
# SITEHACK is defined in nsperl.mk
#	$(ZIP) -d $(dir $@)/$(NSPERL_ZIP_FILE) lib/nsPerl5.6.1/$(SITEHACK)/Mozilla/LDAP/LDIF.pm

$(INSTDIR)/slapd:
	$(MKDIR) -p $@

# Packaging for UNIX is totally different than NT, so we conditionally execute here

ifneq ($(ARCH), WINNT)

# ---THE UNIX PACKAGE---
packageDirectory: $(INSTDIR)/slapd \
	$(INSTDIR)/nsperl/$(NSPERL_ZIP_FILE) \
	$(INSTDIR)/perldap/$(PERLDAP_ZIP_FILE) \
	$(INSTDIR)/tools/infozip.zip \
	$(ADMSERV_DEP)

ifdef BUILD_PATCH
ifdef BUILD_RPM
# create a patch
	$(GENRPMPATCH) -i $(RPM_BASE_NAME) -o $(NS_BUILD_FLAVOR) -r $(ABSRELDIR) -e $(SPEXT) -f $(PATCHINF) -v
	mv $(ABSRELDIR)/slapd/$(NS_BUILD_FLAVOR) $(ABSRELDIR)/slapd/$(NS_BUILD_FLAVOR).original
	ln -s $(ABSRELDIR)/slapd/$(NS_BUILD_FLAVOR)$(SPEXT)/opt/$(RPM_BASE_NAME)-ds $(ABSRELDIR)/slapd/$(NS_BUILD_FLAVOR)
endif
endif

# this gets setup, setup.inf, silent.inf, the zip wrapper, and svrcore, among others
ifeq ($(USE_SETUPUTIL),1)
	cp -R $(SETUPUTIL_BINPATH)/* $(INSTDIR)
	$(PERL) -w $(FIX_SETUP_INF) $(SETUPUTIL_BINPATH)/setup.inf $(INSTDIR)/setup.inf
endif

# copy in our product .inf files
	$(INSTALL) -m 755 $(OBJDIR_32)/*.inf $(INSTDIR)/slapd

# strip the executables in the optimized build
ifeq ($(DEBUG), optimize)
# purify doesn't like stripped executables
ifndef USE_PURIFY
ifndef USE_QUANTIFY
ifdef SAVE_UNSTRIP
	mkdir -p $(RELDIR_UNSTRIP)
	cp -R $(RELDIR)/* $(RELDIR_UNSTRIP)
endif
  ifeq ($(SECURITY_RELDATE), NSS_3_7_9_RTM)
	./unixstrip $(PERL) $(RELDIR)
  else
	./unixstrip $(PERL) $(RELDIR) $(SECURITY_BUILD_DIR)/bin/shlibsign $(SECURITY_BUILD_DIR)/lib:$(NSPR_BUILD_DIR)/lib
  endif
endif
endif
endif
# create the slapd-client.zip file, which only has the ds jar file for the console and
# the ldap client utility programs
	rm -f $(INSTDIR)/slapd/slapd-client.zip
ifdef RSH_ZIP_HOST
# Workaround for problems with ZIP and some SunOS5.6 nfs servers
# Usage : RSH_ZIP_HOST=remote host for zipping RSH_ZIP_PATH=full path on remote host for zip binary
	$(RSH) $(RSH_ZIP_HOST) "cd $(TMP_CM_PWD)/$(RELDIR); $(RSH_ZIP_PATH)/$(ZIP) $(ZIP_FLAGS) $(ABS_INSTDIR)/slapd/slapd-client.zip ./java"
else
# Normal way to ZIP the bits
	cd $(RELDIR); $(ZIP) $(ZIP_FLAGS) $(ABS_INSTDIR)/slapd/slapd-client.zip ./java
endif

#; for file in $(LDAPSDK_IMPORTS) ; \
#	do $(ZIP) $(ZIP_FLAGS) -g $(INSTDIR)/slapd/slapd-client.zip bin/slapd/server/$$file$(DOTEXE) ; \
#	done

# create the slapd zip file
	rm -f $(INSTDIR)/slapd/ns$(DIR).zip
# create installable package
ifdef RSH_ZIP_HOST
# Workaround for problems with ZIP and some SunOS5.6 nfs servers (see above)
	rsh $(RSH_ZIP_HOST) "cd $(TMP_CM_PWD)/$(RELDIR); $(RSH_ZIP_PATH)/$(ZIP) $(ZIP_FLAGS) $(ABS_INSTDIR)/slapd/ns$(DIR).zip * $(EXCLUDED_FILES) "
else
# Normal way to ZIP the bits
	cd $(RELDIR); $(ZIP) $(ZIP_FLAGS) $(ABS_INSTDIR)/slapd/ns$(DIR).zip * $(EXCLUDED_FILES)
endif

# do one last check for aix. aix zip doesn't like -r -T 
	$(ZIP) -T  $(INSTDIR)/slapd/ns$(DIR).zip
# install the ns-config file into the slapd package directory
	$(INSTALL) -m 755 $(RELDIR_32)/bin/slapd/admin/bin/ns-config $(INSTDIR)/slapd

# if we are packaging our own version of the components, we need them during setup too
	-@for file in $(PACKAGE_SETUP_LIBS_32) ; \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(INSTDIR)/slapd ; \
	fi ; \
	done

# if the untar directory is there, hooray; otherwise, we will have to unpack the
# binaries ourselves . . .
	curdir=`pwd`; cd $(INSTDIR) ; \
	if [ ! -d $(IMPORTADMINSRVNOTARBASE)/admin ] ; \
		then for file in $(ADMIN_IMPORTS_TARGZ) ; \
			do rm -rf $$file ; \
			$(GUNZIP) -c $(ADMSERV_DIR)/$$file | $(TAR) xvf - ; \
		done ; \
	else \
		for file in $(ADMIN_IMPORTS) ; \
			do rm -rf $$file ; \
			cp -r $(IMPORTADMINSRVNOTARBASE)/$$file $$file ; \
		done ; \
	fi

# we must remove the nsbase.zip file - we package those files now
	rm -f $(INSTDIR)/base/nsbase.zip
# we also need to remove the Archive directive from the [base] section of the
# base.inf file
	$(PERL) -w $(FIX_BASE_INF) $(INSTDIR)/base/base.inf

# Install LDAP Readme and License files at root of Installation (dated pre-packaging) directory.
# And, replace the License.txt file that is packaged in nssvrcore.zip.
	if [ -d $(LDAPDIR)/docs ] ; then \
		rm -rf $(INSTDIR)/LICENSE.txt $(INSTDIR)/README.txt; \
		$(INSTALL) -m 755 $(LDAPDIR)/docs/README.txt $(LDAPDIR)/docs/LICENSE.txt $(INSTDIR); \
	fi

# Install dsktune at root of Installation
	cp $(RELDIR)/bin/slapd/server/dsktune $(INSTDIR)

ifndef NO_INSTALLER_TAR_FILES
# build the slapd package tar file
	cd $(INSTDIR); $(TAR) cvf - slapd \
	| gzip -f > ../$(NS_BUILD_FLAVOR).tar.gz
# build the combined packages tar file; use h flag to follow symlinks
ifdef BUILD_SHIP
ifndef BUILD_PATCH
	cd $(INSTDIR); $(TAR) cvfh - setup.inf setup slapd nsperl \
	perldap dsktune tools $(ADMIN_IMPORTS) | gzip -f > $(BUILD_SHIP)/$(FTPNAMEGZ)
endif
ifeq ($(DEBUG), optimize)
#	$(REMSH) "/u/svbld/bin/preRtm $(BUILD_SHIP) $(FTPNAMEGZ) svbld"
endif
else
	cd $(INSTDIR); $(TAR) cvfh - setup.inf setup slapd nsperl \
	perldap dsktune tools $(ADMIN_IMPORTS) | gzip -f > ../all$(NS_BUILD_FLAVOR).tar.gz
endif # BUILD_SHIP
#cp $(INSTDIR).tar.gz  $(BUILD_SHIP)
#cp $(INSTDIR)/all$(NS_BUILD_FLAVOR).tar.gz  $(BUILD_SHIP)
# $(INSTDIR) is used to build international products.
endif
ifdef BUILD_RPM
# make the .spec file - actually lives in OBJDIR
	$(MAKE) $(MFLAGS) -C $(BUILD_ROOT) $(RPM_BASE_NAME)-ds.spec
# create the source tarball - the name must correspond to the Source: in the spec file
# they should correspond because the values come from the same source - branding
	startdir=`pwd` ; cd $(BUILD_ROOT) ; builddir=`pwd` ; \
	cd $$startdir ; cd $(INSTDIR)/.. ; \
	if [ ! -f $(RPM_FILE_BASE).tar.gz ] ; then \
		if [ ! -f $(RPM_FILE_BASE) ] ; then \
			ln -s $$builddir $(RPM_FILE_BASE) ; \
		fi ; tar cfh - --exclude \*/built --exclude \*/CVS $(RPM_FILE_BASE) --exclude \*/branding --exclude \*/RHEL* | gzip > $(RPM_FILE_BASE).tar.gz ; \
		rm $(RPM_FILE_BASE) ; \
	fi
# execute the RPM build
	rpmbuild $(RPM_TOPDIR) $(RPM_SOURCEDIR) $(RPM_BUILDDIR) $(RPM_RPMDIR) $(RPM_SRPMDIR) $(RPM_REQUIRES) --define "flavor $(RPM_FLAVOR)" --clean --nodeps -ba $(OBJDIR)/$(RPM_BASE_NAME)-ds.spec

ifdef BUILD_SHIP
	cp $(ABS_TOPDIR)/$(RPM_FILE_BASE)*$(NSOS_ARCH)$(NSOS_RELEASE).$(RPM_ARCH).$(RPM_FLAVOR).rpm $(BUILD_SHIP)
endif # BUILD_SHIP
endif # BUILD_RPM

ifdef BUILD_PATCH
	mkdir -p $(PATCHINSTDIR)/$(SLAPDSP)
	cp $(INSTDIR)/LICENSE.txt $(PATCHINSTDIR)
	cp $(INSTDIR)/README.txt $(PATCHINSTDIR)
# prepare the main inf file: setup.inf pointing inf file $(SLAPDS)/$(SLAPD).inf
	sed -e "s/Components.*=/Components = $(SLAPDSP)/" $(INSTDIR)/setup.inf > $(PATCHINSTDIR)/setup.inf
	echo "" >> $(PATCHINSTDIR)/setup.inf
	echo "[$(SLAPDSP)]" >> $(PATCHINSTDIR)/setup.inf
	echo "ComponentInfoFile = $(SLAPDSP)/$(SLAPDSP).inf" >> $(PATCHINSTDIR)/setup.inf
# create a zip file based upon the $(PATCHINF) file
	cd $(ABSRELDIR)/slapd/$(NS_BUILD_FLAVOR); zip -r $(PATCHINSTDIR)/$(SLAPDSP)/ns$(SLAPDSP).zip `egrep file: $(PATCHINF) | awk -F: '{print $$3}'`
# put ns-config and needed libs in the $(PATCHINSTDIR)/$(SLAPDSP) directory
	$(INSTALL) -m 755 $(RELDIR_32)/bin/slapd/admin/bin/ns-config $(PATCHINSTDIR)/$(SLAPDSP)
	-@for file in $(PACKAGE_SETUP_LIBS_32) ; \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(PATCHINSTDIR)/$(SLAPDSP) ; \
	fi ; \
	done
# create patch inf file: $(SLAPD).inf
	cp $(OBJDIR)/slapd-patch.inf $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf
	cd $(ABSRELDIR)/slapd/$(NS_BUILD_FLAVOR); ls `egrep file: $(PATCHINF) | egrep -v "setup/setup" | awk -F: '{print $$3}'` > $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf.tmp
	echo `cat $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf.tmp` | sed -e "s/ /,/g" > $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf.tmp2
	echo "BackupFiles="`cat $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf.tmp2`>> $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf
	rm -f $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf.tmp $(PATCHINSTDIR)/$(SLAPDSP)/$(SLAPDSP).inf.tmp2
ifdef BUILD_SHIP
	cd $(PATCHINSTDIR); $(TAR) cvfh - * | gzip -f > $(BUILD_SHIP)/$(FTPNAMEGZ)
else
	cd $(PATCHINSTDIR); $(TAR) cvfh - * | gzip -f > ../$(NS_BUILD_FLAVOR)-SP.tar.gz
endif
endif

else

# ---THE NT PACKAGE---
$(PACKAGE_STAGE_DIR):
	mkdir -p $@

$(INSTDIR):
	mkdir -p $@

###self-extracting EXE using EXEBUILD and MAKE the INI file on the fly ##
# and rename OBJDIR_BASE directroy to temp since EXEBUILDER chokes on LFNS 
# when building across NFS, then rename it back. davidk

make_zip:
	if [ -d $(LDAPDIR)/docs ] ; then \
		rm -rf $(INSTDIR)/license-nt.txt $(INSTDIR)/slapd.txt $(INSTDIR)/README.txt $(INSTDIR)/LICENSE.txt; \
		cd $(LDAPDIR)/docs; $(INSTALL) -m 755 README.txt LICENSE.txt $(INSTDIR); \
	fi
ifdef BUILD_SHIP
	cd $(INSTDIR); zip -r $(BUILD_SHIP)/$(FTPNAME) * 
endif

endif

## Run Acceptance Test from the auto builds
## Run it only on domestic - optimized
## davidk

ifeq ($(BUILD_SECURITY), domestic)
ifeq ($(BUILD_DEBUG), optimize)
acceptdir=../../../tetframework/testcases/DS/6.0/acceptance
longdir=../../../tetframework/testcases/DS/6.0/longduration
endif
endif

Acceptance:
ifdef BUILD_SHIP
ifeq ($(BUILD_SECURITY), domestic)
ifeq ($(BUILD_DEBUG), optimize)
ifeq ($(ARCH), HPUX)
ifeq ($(USE_64), 1)  # only run acceptance on 64-bit HPUX builds
	$(acceptdir)/accept $(BUILD_SHIP)/$(FTPNAMEGZ) &
endif  # USE_64
else
ifeq ($(ARCH), Linux)
ifdef BUILD_RPM
	$(acceptdir)/accept $(shell echo $(BUILD_SHIP)/$(RPM_FILE_BASE)*$(NSOS_ARCH)$(NSOS_RELEASE).$(RPM_ARCH).$(RPM_FLAVOR).rpm) &
endif  # BUILD_RPM
else
	$(acceptdir)/accept $(BUILD_SHIP)/$(FTPNAMEGZ) &
endif  # Linux
endif  # HPUX
endif  # optimize
endif  # domestic
endif  # BUILD_SHIP

Longduration:
ifdef BUILD_SHIP
ifeq ($(BUILD_SECURITY), domestic)
ifeq ($(BUILD_DEBUG), optimize)
ifeq ($(ARCH), HPUX)
ifeq ($(USE_64), 1)  # only run long duration on 64-bit HPUX builds
	$(longdir)/longduration $(BUILD_SHIP)/$(FTPNAMEGZ) &
endif  # USE_64
else
ifeq ($(ARCH), Linux)
ifdef BUILD_RPM
	$(longdir)/longduration $(shell echo $(BUILD_SHIP)/$(RPM_FILE_BASE)*$(NSOS_ARCH)$(NSOS_RELEASE).$(RPM_ARCH).$(RPM_FLAVOR).rpm) &
endif  # BUILD_RPM
else
	$(longdir)/longduration $(BUILD_SHIP)/$(FTPNAMEGZ) &
endif  # Linux
endif  # HPUX
endif  # optimize
endif  # domestic
endif  # BUILD_SHIP

cleanDirectory:
	cd $(LDAPDIR); $(MAKE) clean
	rm -rf $(BUILD_ROOT)/../dist/$(NC_BUILD_FLAVOR)
	rm -rf $(BUILD_ROOT)/built/$(NS_BUILD_FLAVOR)


ifeq ($(ARCH), WINNT)
copyLibslapd: $(OBJDIRLIB) 
	$(CP) $(INST_TARGET_RESKIT)/libslapd.$(DLL_SUFFIX) $(LIBSLAPD_DEP)
	$(CP) $(INST_TARGET_RESKIT)/libslapd.$(LIB_SUFFIX) $(LIBSLAPD)
else
copyLibslapd: $(OBJDIRLIB) 
	$(CP) $(INST_TARGET_RESKIT)/libslapd.$(DLL_SUFFIX) $(LIBSLAPD_DEP)
endif

packageReskit:
ifdef BUILD_SHIP
	rm -rf $(BUILD_SHIP)/$(NS_BUILD_FLAVOR)
	$(MKDIR) $(BUILD_SHIP)/$(NS_BUILD_FLAVOR)
ifeq ($(ARCH), WINNT)
	$(INSTALL) -m 755 $(LIBSLAPD) $(BUILD_SHIP)/$(NS_BUILD_FLAVOR)/lib
	$(INSTALL) -m 755 $(OBJDIRLIB)/cos-plugin.$(LIB_SUFFIX) $(BUILD_SHIP)/$(NS_BUILD_FLAVOR)/lib
endif
	$(INSTALL) -m 755 $(LIBSLAPD_DEP) $(BUILD_SHIP)/$(NS_BUILD_FLAVOR)/lib 
	$(INSTALL) -m 755 $(OBJDIRLIB)/cos-plugin.$(DLL_SUFFIX) $(BUILD_SHIP)/$(NS_BUILD_FLAVOR)/lib
endif 
### END OF DS4.x RESOURCE KIT STUFF
#####################################################

ifeq ($(ARCH), WINNT)

SLAPD_DIR=slapd

_perl: $(INSTDIR)/nsperl/$(NSPERL_ZIP_FILE) \
	$(INSTDIR)/perldap/$(PERLDAP_ZIP_FILE)

# ------------------------- all below this line is packageDirectory --------------------------

packageDirectory: $(INSTDIR)/$(SLAPD_DIR) _admserv_files _perl _slapd_files _setup_files make_zip

#--------------------------  admserv rules ----------------------------------------------------
_admserv_files: $(INSTDIR) $(ADMSERV_DEP)
# we don't want to copy the tarball with the other setup files . . .
	mv $(ADMSERV_DIR)/$(ADMIN_SERVER_TARGZ) $(ADMSERV_DIR)/..
# copy in the setup.exe, setup.inf, dlls, etc.
	cp $(ADMSERV_DIR)/*.* $(INSTDIR)
# move the tarball back after the copy
	mv $(ADMSERV_DIR)/../$(ADMIN_SERVER_TARGZ) $(ADMSERV_DIR)
# make sure our packages are in the setup.inf file
	$(PERL) -w $(FIX_SETUP_INF) $(ADMSERV_DIR)/setup.inf $(INSTDIR)/setup.inf


#----------------------------  slapd rules --------------------------------------------------

# Files which go in the distibution and which we make ourselves
SLAPD_ZIPFILE=$(ABS_ROOT_PARENT)/$(MMDD)/$(NS_BUILD_FLAVOR)/$(SLAPD_DIR)/slapd.z
DSJARS_ZIPFILE=$(ABS_ROOT_PARENT)/$(MMDD)/$(NS_BUILD_FLAVOR)/$(SLAPD_DIR)/dsjars.z

.PHONY: _slapd_files

_slapd_files: $(INSTDIR)/$(SLAPD_DIR)  \
			   $(INSTDIR)/$(SLAPD_DIR)/slapd.z \
			   $(INSTDIR)/$(SLAPD_DIR)/dsjars.z

$(INSTDIR)/$(SLAPD_DIR)/dsjars.z: $(CLASS_DEST)/$(LDAPCONSOLEJAR) \
	$(CLASS_DEST)/$(LDAPCONSOLEJAR_EN)  $(DS_JAR_SRC_PATH)/$(XMLTOOLS_JAR_FILE)
	rm -f $(DSJARS_ZIPFILE); cd $(RELDIR); zip -r $(DSJARS_ZIPFILE) java

$(INSTDIR)/$(SLAPD_DIR)/slapd.z: 
# see components.mk for a description of LIBS_TO_PKG
	-@for file in $(LIBS_TO_PKG) ; \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/bin/slapd/server ; \
	     $(INSTALL) -m 755 $$file $(RELDIR)/bin/slapd/admin/bin ; \
	fi ; \
	done
# these are files to copy to the shared/bin directory - ldap cmd line tools, sec tools, etc.
	for file in $(BINS_TO_PKG_SHARED) $(LIBS_TO_PKG_SHARED); \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/shared/bin ; \
	fi ; \
	done
# these are dlls to copy to the clients/dsgw/bin directory to support dsgw & pb
	for file in $(LIBS_TO_PKG_CLIENTS); \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(RELDIR)/clients/dsgw/bin ; \
	fi ; \
	done
	$(INSTALL) -m 755 $(OBJDIR)/$(BUILD_HTTPDLL_NAME).dll $(RELDIR)/bin/slapd/server
	$(INSTALL) -m 755 $(OBJDIR)/$(BUILD_HTTPDLL_NAME).dll $(RELDIR)/clients/dsgw/bin
	rm -f $(SLAPD_ZIPFILE); cd $(RELDIR); zip -r $(SLAPD_ZIPFILE) *

#----------------------------- setup rules ---------------------------------------------------

_setup_files: $(INSTDIR)/$(SLAPD_DIR)/dsinst.dll \
			  $(INSTDIR)/$(SLAPD_DIR)/slapd.inf \
			  $(INSTDIR)/admin $(INSTDIR)/base \
			  $(INSTDIR)/svrcore $(INSTDIR)/tools
# see components.mk for a description of PACKAGE_SETUP_LIBS
	-@for file in $(PACKAGE_SETUP_LIBS) ; \
	do if [ -f $$file ] ; \
	then $(INSTALL) -m 755 $$file $(INSTDIR) ; \
	fi ; \
	done

$(INSTDIR)/$(SLAPD_DIR)/dsinst.dll: $(OBJDIR)/setup/dsinst.dll
	cp $< $@

$(INSTDIR)/$(SLAPD_DIR)/slapd.inf: $(OBJDIR)/setup/slapd.inf
	cp $< $@

$(INSTDIR)/admin: $(ADMSERV_DIR)/admin
	cp -R $< $@

$(INSTDIR)/base: $(ADMSERV_DIR)/base
	cp -R $< $@
	rm -f $@/base.z
	rm -f $@/basesys.z
# we need to remove the jre per Red Hat.  Users will download their own jre
	rm -f $(INSTDIR)/base/jre.z
# we also need to remove the Archive directive from the [base] section of the
# base.inf file
	$(PERL) -w $(FIX_BASE_INF) $@/base.inf

$(INSTDIR)/svrcore: $(ADMSERV_DIR)/svrcore
	cp -R $< $@

$(INSTDIR)/tools: $(INSTDIR)/tools/infozip.zip
	$(UNZIP) -j $(INSTDIR)/nsperl/$(NSPERL_ZIP_FILE) \
	lib/nsPerl5.6.1/bin/perl$(DOTEXE) -d $@
	$(UNZIP) -j $(INSTDIR)/nsperl/$(NSPERL_ZIP_FILE) \
	lib/nsPerl5.6.1/bin/perl56.dll -d $@
# We need to pull out the perl lib directory for perl to work 
	mkdir $@/tmp
	$(UNZIP) $(INSTDIR)/nsperl/$(NSPERL_ZIP_FILE) \
	lib/nsPerl5.6.1/lib/\* -d $@/tmp
	cp -R $@/tmp/lib/nsPerl5.6.1/lib $@
	rm -rf $@/tmp
	$(UNZIP) -j $< -d $@
	rm -f $<

endif

$(OBJDIR)/lib/libsi18n/ns-slapd.properties:
	cd $(BUILD_ROOT)/lib/libsi18n ; $(MAKE) $(MFLAGS) all
