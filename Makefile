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
# Top-level gmake Makefile for LDAP Server builds.
#
# Execute the command:
#
#       gmake help
#
# to see a list of available targets.

# Relative path to the top of the build tree (i.e., where the DS source tree is checked out)
BUILD_ROOT=.

# define COMPONENT_DEPS here so that components are pulled in this makefile
COMPONENT_DEPS := 1

include nsdefs.mk
include nsconfig.mk

# first (default) rule: build and create a DS package
all:	buildAndPkgDirectory

help:
	@echo
	@echo The following are build targets that you can choose from:
	@echo
	@echo "   gmake buildAndPkgDirectory"
	@echo "   gmake buildDirectory"
	@echo "   gmake pkgDirectory"
	@echo "   gmake pkgDirectoryl10n"
	@echo "   gmake pkgDirectoryPseudoL10n"
	@echo "   gmake with no arguments will do buildAndPkgDirectory, which "
	@echo "         is usually what you want to do"
	@echo ""
	@echo "   The following are optional build flags which build or pull in"
	@echo "   optional components which are only available internally for"
	@echo "   now.  In the future these components may be made available"
	@echo "   externally or in an open source version."
	@echo "   USE_ADMINSERVER=1 - bundle the Admin Server (required to run Console/webapps)"
	@echo "   USE_CONSOLE=1     - bundle the Administration Console (requires Java)"
	@echo "   USE_DSMLGW=1      - build/bundle the DSMLv2 Gateway (requires Java)"
	@echo "   USE_ORGCHART=1    - build/bundle the Org Chart webapp"
	@echo "   USE_DSGW=1        - build/bundle the Phonebook/DS Gateway webapp"
	@echo "   USE_JAVATOOLS=1   - build/bundle the Java command line tools"
	@echo "   USE_SETUPSDK=1    - build/bundle programs that use Setup SDK"

###### Implementation notes:
#
# We use ../reltools/ftp_puller_new.pl to pull and maintain dependencies
# for the components (binary and header files) we use for the build.  The
# dependencies are maintained in the file ../components.OS, where OS is
# the operating system.  These files do not exist in CVS; they are created
# as needed.  We could probably make the system smarter to know the
# difference between debug/optimized and export/domestic, but for now it
# does not. 
#
# The file ./component_versions.mk contains the component version
# information.  If you want to change the version of a component used
# to build or package, this is the place to look.
#
#
# The file ./components.mk contains the information about what files and
# directories are used by the component.  It also contains the
# information about how to pull the component.  Each component defines a
# XXX_DEP macro which is the name of a file to be used for dependency
# checking.
#
#
# By default, NT uses FTP and Unix uses SYMLINK as their pull methods.
# This is controlled by the COMPONENT_PULL_METHOD macro.  For example,
# on Unix, you can use
#
#     gmake COMPONENT_PULL_METHOD=FTP buildDirectory 
#
# To force use of ftp to pull all components.  In addition, each component
# can have a XXX_PULL_METHOD macro.  If this macro is defined, it overrides
# the default.  For example, on Unix, you can use
#
#     gmake NSPR_PULL_METHOD=FTP buildDirectory
#
# if you want to get NSPR via ftp and the other components via symlink.
#
#
# By default, the components are only pulled from the top level if you do
# a gmake buildDirectory.  There is a macro called COMPONENT_DEPS.  If
# this is defined on the command line of the gmake command e.g.
#
#     gmake COMPONENT_DEPS=1 ... 
#
# this will force component checking and pulling.  This will have the
# effect of slowing down the build.
#
###### End of implementation notes.

ifeq ($(INTERNAL_BUILD), 1)
  COMPONENT_DEPENDENCIES = $(ADMINUTIL_DEP) $(NSPR_DEP) $(ARLIB_DEP) $(DBM_DEP) $(SECURITY_DEP) $(SVRCORE_DEP) \
	$(ICU_DEP) $(SETUPSDK_DEP) $(LDAPSDK_DEP) $(DB_LIB_DEP) $(SASL_DEP) $(NETSNMP_DEP) \
	$(AXIS_DEP) $(DSMLJAR_DEP) $(DSDOC_DEP) $(ADSYNC_DEP) $(NT4SYNC_DEP)
endif

# Pull WiX MSI toolkit on Windows.
ifeq ($(ARCH), WINNT)
COMPONENT_DEPENDENCIES += $(WIX_DEP)
## Only fetch the necessary components for ApacheDS when requested
ifeq ($(BUILD_NTDS),1)
COMPONENT_DEPENDENCIES += $(WRAPPER_DEP) $(SWIG_DEP) $(MAVEN_DEP) $(APACHEDS_DEP) 
endif
endif
components: $(COMPONENT_DEPENDENCIES)
	-@echo "The components are up to date"

ifeq ($(BUILD_JAVA_CODE),1)
DS_CONSOLE_COMPONENT_DEP  = $(LDAPJDK_DEP) $(LDAPCONSOLE_DEP)
DS_CONSOLE_COMPONENT_DEP += $(JAVASSL_DEP) $(JSS_DEP) $(CRIMSONJAR_DEP)
java_platform_check:

else

DS_CONSOLE_COMPONENT_DEP  =
java_platform_check:
	-@echo "Note: Java code is not built on this platform ($(ARCH))."
	-@echo "      Use 'gmake BUILD_JAVA_CODE=1 ...' to override."
endif

consoleComponents: $(DS_CONSOLE_COMPONENT_DEP)

pumpkin:
	@echo NSOS_RELEASE is: $(NSOS_RELEASE)
	$(PERL) pumpkin.pl $(PUMPKIN_AGE) pumpkin.dat

buildnum:
	if test ! -d $(BUILD_ARCH); then mkdir $(BUILD_ARCH); fi;
	$(PERL) buildnum.pl -p $(BUILD_ARCH)

nsCommon:
	cd config;		$(MAKE) export $(NSDEFS)
# XXXsspitzer:  for UNIXWARE and UnixWare
ifeq ($(subst nix,NIX,$(subst are,ARE,$(ARCH))), UNIXWARE)
	- mkdir built/$(NS_BUILD_FLAVOR)/obj
	cd built/$(NS_BUILD_FLAVOR)/obj && ar xv ../../../$(LIBNSPR) uxwrap.o
endif
ifeq ($(ARCH), WINNT)
	cd lib/libnt;	$(MAKE) $(MFLAGS) export $(NSDEFS)
	cd lib/libnt;	$(MAKE) $(MFLAGS) libs $(NSDEFS)
endif

#
# Notice that BUILD_MODULE is not supplied directly on this target.
# It either inherits from the calling target or from the default in
# nsdefs.mk.  Therefore if you need to perform 'gmake httpdlib', be sure
# that BUILD_MODULE is set to whatever target release that you need.
#
httpdLib:
	@echo 
	@echo 
	@echo 
	@echo ==== Starting Server LIBS for: $(BUILD_MODULE) ==========
	@echo
	cd lib/base; 		$(MAKE) $(MFLAGS)
	cd lib/ldaputil;	$(MAKE) $(MFLAGS)
	cd lib/libaccess; 	$(MAKE) $(MFLAGS)
	cd lib/libadmin; 	$(MAKE) $(MFLAGS)
	cd lib/libsi18n; 	$(MAKE) $(MFLAGS)
	@echo ==== Finished Server LIBS for: $(BUILD_MODULE) ==========
	@echo 

brandDirectory: $(RELTOOLSPATH)/brandver.pl
	@echo ==== Branding LDAP Server ==========
	$(RELTOOLSPATH)/brandver.pl -i branding/rhds/brandver.dat -s redhat-ds.spec
	@echo ==== Finished Branding LDAP Server ==========

normalizeDirectory: $(RELTOOLSPATH)/brandver.pl
	@echo ==== Normalizing LDAP Server ==========
	$(RELTOOLSPATH)/brandver.pl -i branding/rhds/normalize.dat -s redhat-ds.spec
	@echo ==== Normalizing Branding LDAP Server ==========

buildAndPkgDirectory:	buildDirectory pkgDirectory

buildDirectory: buildnum pumpkin $(OBJDIR) $(DIRVER_H) $(SDKVER_H) components 
	@echo 
	@echo 
	@echo ==== Starting LDAP Server ==========
	@echo
	$(MAKE) $(MFLAGS) nsCommon
	cd config;           $(MAKE) $(MFLAGS) install $(NSDEFS)
	$(MAKE) $(MFLAGS) BUILD_MODULE=DIRECTORY LDAP_NO_LIBLCACHE=1 httpdLib
ifeq ($(ARCH), WINNT)
	$(PERL) ntversion.pl $(BUILD_ROOT) $(MAJOR_VERSION) $(MINOR_VERSION)
endif
	cd httpd/src; $(MAKE) $(MFLAGS) LDAP_NO_LIBLCACHE=1 BUILD_MODULE=DIRECTORY all
	cd ldap; $(MAKE) $(MFLAGS) LDAP_NO_LIBLCACHE=1 BUILD_MODULE=DIRECTORY all
	@echo ==== Finished LDAP Server ==========
	@echo
	@echo ==== Starting LDAP Server Console ==========
	@echo
	$(MAKE) $(MFLAGS) buildDirectoryConsole
	@echo
	@echo ==== Finished LDAP Server Console ==========
	@echo
	@echo ==== Starting LDAP Server Clients ==========
	@echo
	$(MAKE) $(MFLAGS) buildDirectoryClients
	@echo
	@echo ==== Finished LDAP Server Clients ==========
	@echo

cleanDirectory:
	@echo
	@echo
	@echo ==== Cleaning LDAP Server on $(ARCH) ====
	@echo
	rm -rf $(ARCH)
	rm -rf built/$(NS_BUILD_FLAVOR)
	rm -rf built/release/slapd/$(NS_BUILD_FLAVOR)
	rm -rf ../dist/$(NSOBJDIR_NAME)
	rm -rf ../dist/full
	rm -rf $(CLASS_DEST)	# ../dist/classes
	@echo
	@echo ==== All done ===
	@echo

buildDirectoryConsole: consoleComponents java_platform_check
ifeq ($(BUILD_JAVA_CODE),1)
#	cd ldap/admin/src/java/com/netscape/admin/dirserv; $(MAKE) $(MFLAGS) package
    ifeq ($(USE_JAVATOOLS), 1)
		cd ldap/admin/src/java/com/netscape/xmltools; $(MAKE) $(MFLAGS) package
    endif
endif

buildDirectoryClients: $(ANT_DEP) java_platform_check
ifeq ($(BUILD_JAVA_CODE),1)
    ifeq ($(USE_DSMLGW), 1)
		cd ldap/clients; $(MAKE) _dsmlgw
    endif
endif
ifeq ($(USE_DSGW), 1)
	cd ldap/clients; $(MAKE) _dsgw
endif

$(OBJDIR):
	if test ! -d $(OBJDIR); then mkdir -p $(OBJDIR); fi;

$(RELTOOLSPATH)/brandver.pl:
	cd $(ABS_ROOT)/.. ; cvs co RelToolsLite

$(SDKVER_H):
	if test ! -d $(DIRVERDIR); then mkdir -p $(DIRVERDIR); fi;
	$(PERL) dirver.pl -v "$(DIRSDK_VERSION)" -o $@

$(DIRVER_H):
	if test ! -d $(DIRVERDIR); then mkdir -p $(DIRVERDIR); fi;
	$(PERL) dirver.pl -v "$(DIR_VERSION)" -o $@

pkgLdapSDK: setupLdapSDK
	@echo
	@echo =========== Finished - LDAP SDK  Package Build ============

setupLdapSDK:
	@echo =========== Starting - LDAP SDK  Package Build ============
	@echo	
	cd ldap/cm; $(MAKE) $(MAKEFLAGS) releaseLdapSDK 
	cd ldap/cm; $(MAKE) $(MAKEFLAGS) packageLdapSDK 


pkgDirectory:	setupDirectory
	@echo
	@echo =========== Finished - LDAP Server Package Build ============

Acceptance:
	cd ldap/cm; $(MAKE) Acceptance $(MFLAGS)

Longduration:
	cd ldap/cm; $(MAKE) Longduration $(MFLAGS)

setupDirectory:
	cd ldap/cm; $(MAKE) $(MFLAGS) releaseDirectory;
ifeq ($(USE_SETUPSDK), 1)
	cd ldap/cm; $(MAKE) $(MFLAGS) packageDirectory;
endif

pkgDirectoryJars:
	cd ldap/cm; $(MAKE) $(MFLAGS) packageJars 

pkgDirectoryl10n:
	@echo =========== Starting - LDAP Server International Package Build ============
	cd ldap/cm; $(MAKE) $(MFLAGS) l10nRePackage
	@echo =========== Finished - LDAP Server International Package Build ============

pkgDirectoryPseudoL10n:
	@echo =========== Starting - LDAP Server L10N Package Build ============
ifeq ($(BUILD_SECURITY),export)
ifeq ($(BUILD_DEBUG),optimize)
	cd i18npkg/apollo; $(MAKE) $(MFLAGS)
else
	@echo skipping pkgDirectoryPseudoL10n
endif
else
	@echo skipping pkgDirectoryPseudoL10n
endif
	@echo =========== Finished - LDAP Server L10N Package Build ============

redhat-ds.spec: ldapserver.spec.tmpl branding/rhds/brandver.dat $(RELTOOLSPATH)/brandver.pl
	sed -e s/@PLATFORM@/$(BUILD_ARCH)/g ldapserver.spec.tmpl > $@
	$(RELTOOLSPATH)/brandver.pl -i branding/rhds/brandver.dat $@
	mv $@ $(OBJDIR)

fedora-ds.spec: ldapserver.spec.tmpl branding/fedora/brandver.dat $(RELTOOLSPATH)/brandver.pl
	sed -e s/@PLATFORM@/$(BUILD_ARCH)/g ldapserver.spec.tmpl > $@
	$(RELTOOLSPATH)/brandver.pl -i branding/fedora/brandver.dat $@
	mv $@ $(OBJDIR)
