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
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# This file defines dependencies for components and 
# tells how to satisfy thoes dependencies

# For internal components, we use ftp_puller_new.pl
# We should consider using wget or something like that
# in the future.

BUILD_MODE = ext

ifdef BUILD_PUMPKIN
PUMPKIN_AGE := 120
#BUILD_BOMB=
BUILD_BOMB=-DPUMPKIN_HOUR=$(shell cat $(BUILD_ROOT)/pumpkin.dat)
BOMB=$(BUILD_BOMB)
endif # BUILD_PUMPKIN

ifndef NSPR_SOURCE_ROOT
NSPR_IMPORT = $(COMPONENTS_DIR_DEV)/nspr/$(NSPR_RELDATE)/$(FULL_RTL_OBJDIR)
NSPR_DEP = $(NSPR_LIBPATH)/libnspr4.$(LIB_SUFFIX)

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
endif # NSPR_SOURCE_ROOT

ifndef SECURITY_SOURCE_ROOT
SECURITY_IMPORT = $(COMPONENTS_DIR)/nss/$(SECURITY_RELDATE)/$(FULL_RTL_OBJDIR)
ifeq ($(ARCH), WINNT)
  SECURITY_DEP = $(SECURITY_LIBPATH)/ssl3.$(DLL_SUFFIX)
else
  SECURITY_DEP = $(SECURITY_LIBPATH)/libssl3.$(DLL_SUFFIX)
endif

ifdef VSFTPD_HACK
SECURITY_FILES=lib,bin/$(subst $(SPACE),$(COMMA)bin/,$(SECURITY_TOOLS))
else
SECURITY_FILES=lib,include,bin/$(subst $(SPACE),$(COMMA)bin/,$(SECURITY_TOOLS))
endif

ifndef SECURITY_PULL_METHOD
SECURITY_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SECURITY_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	$(RM) -rf $(SECURITY_BINPATH)
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
endif # COMPONENT_DEPS
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component NSS file $@" ; \
	fi
endif # SECURITY_SOURCE_ROOT

ifndef SVRCORE_SOURCE_ROOT
SVRCORE_IMPORT = $(COMPONENTS_DIR)/svrcore/$(SVRCORE_RELDATE)/$(NSOBJDIR_NAME)
#SVRCORE_IMPORT = $(COMPONENTS_DIR_DEV)/svrcore/$(SVRCORE_RELDATE)/$(NSOBJDIR_NAME)
ifeq ($(ARCH), WINNT)
  SVRCORE_DEP = $(SVRCORE_LIBPATH)/svrcore.$(LIB_SUFFIX)
else
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
endif # SVRCORE_SOURCE_ROOT

ifndef LDAPSDK_SOURCE_ROOT
ifndef LDAP_VERSION
  LDAP_VERSION = $(LDAP_RELDATE)
endif
ifndef LDAP_SBC
LDAP_SBC = $(COMPONENTS_DIR_DEV)
#LDAP_SBC = $(COMPONENTS_DIR)
endif
LDAPOBJDIR = $(FULL_RTL_OBJDIR)
# LDAP does not have PTH version, so here is the hack which treat non PTH
# version as PTH version
ifeq ($(USE_PTHREADS), 1)
  LDAP_RELEASE = $(LDAP_SBC)/$(LDAPCOMP_DIR)/$(LDAP_VERSION)/$(NSOBJDIR_NAME1)
else
  LDAP_RELEASE = $(LDAP_SBC)/$(LDAPCOMP_DIR)/$(LDAP_VERSION)/$(LDAPOBJDIR)
endif
ifeq ($(ARCH), WINNT)
  LDAPSDK_DEP = $(LDAPSDK_LIBPATH)/nsldap32v$(LDAP_SUF).$(DLL_SUFFIX)
  LDAPSDK_PULL_LIBS = lib/nsldapssl32v$(LDAP_SUF).$(LIB_SUFFIX),lib/nsldapssl32v$(LDAP_SUF).$(LDAP_DLL_SUFFIX),lib/nsldap32v$(LDAP_SUF).$(LIB_SUFFIX),lib/nsldap32v$(LDAP_SUF).$(LDAP_DLL_SUFFIX),lib/nsldappr32v$(LDAP_SUF).$(LIB_SUFFIX),lib/nsldappr32v$(LDAP_SUF).$(LDAP_DLL_SUFFIX)
else
  LDAPSDK_DEP = $(LDAPSDK_LIBPATH)/libldap$(LDAP_SUF).$(DLL_SUFFIX)
  LDAPSDK_PULL_LIBS = lib/libssldap$(LDAP_SUF)$(LDAP_DLL_PRESUF).$(LDAP_DLL_SUFFIX),lib/libldap$(LDAP_SUF)$(LDAP_DLL_PRESUF).$(LDAP_DLL_SUFFIX),lib/libprldap$(LDAP_SUF)$(LDAP_DLL_PRESUF).$(LDAP_DLL_SUFFIX)
endif

# Solaris and HP-UX PA-RISC only #########################################
# if building 64 bit version, also need the 32 bit version of NSS and NSPR
ifeq ($(PACKAGE_LIB32), 1)
  NSPR_IMPORT_32 = $(COMPONENTS_DIR_DEV)/nspr/$(NSPR_RELDATE)/$(FULL_RTL_OBJDIR_32)
  SECURITY_IMPORT_32 = $(COMPONENTS_DIR)/nss/$(SECURITY_RELDATE)/$(FULL_RTL_OBJDIR_32)
  LDAP_RELEASE_32 = $(LDAP_SBC)/$(LDAPCOMP_DIR)/$(LDAP_VERSION)/$(FULL_RTL_OBJDIR_32)
  SECURITY_FILES_32 = $(subst $(SPACE),$(COMMA),$(SECURITY_FILES_32_TMP))
endif

ifndef LDAPSDK_PULL_METHOD
LDAPSDK_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(LDAPSDK_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	mkdir -p $(LDAP_LIBPATH)
	$(FTP_PULL) -method $(LDAPSDK_PULL_METHOD) \
		-objdir $(LDAP_ROOT) -componentdir $(LDAP_RELEASE) \
		-files include,$(LDAPSDK_PULL_LIBS),bin
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component LDAPSDK file $@" ; \
	fi

ifeq ($(PACKAGE_LIB32), 1)
	$(FTP_PULL) -method $(SECURITY_PULL_METHOD) \
		-objdir $(NSPR_BUILD_DIR_32) -componentdir $(NSPR_IMPORT_32) \
		-files lib
	$(RM) -rf $(SECURITY_BUILD_DIR_32)/lib
	mkdir -p $(SECURITY_BUILD_DIR_32)/lib
	$(FTP_PULL) -method $(SECURITY_PULL_METHOD) \
		-objdir $(SECURITY_BUILD_DIR_32)/lib -componentdir $(SECURITY_IMPORT_32)/lib \
		-files $(SECURITY_FILES_32)
	$(FTP_PULL) -method $(LDAPSDK_PULL_METHOD) \
		-objdir $(LDAP_ROOT_32) -componentdir $(LDAP_RELEASE_32) \
		-files lib
	-@if [ -f $(SECURITY_BUILD_DIR_32)/lib/$(NSSCKBI_FILE) ] ; then \
		mv -f $(SECURITY_BUILD_DIR_32)/lib/$(NSSCKBI_FILE) $(SECURITY_BUILD_DIR_32)/lib/$(NSSCKBI32_FILE) ; \
	fi
endif # PACKAGE_LIB32
##
endif # LDAPSDK_SOURCE_ROOT

ifndef SASL_SOURCE_ROOT
ifneq ($(ARCH), Linux)
SASL_RELEASE = $(COMPONENTS_DIR_DEV)/sasl/$(SASL_VERSDIR)/$(SASL_RELDATE)/$(NSOBJDIR_NAME)
SASL_DEP = $(SASL_INCLUDE)/sasl.h
ifndef SASL_PULL_METHOD
SASL_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SASL_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(SASL_PULL_METHOD) \
		-objdir $(SASL_BUILD_DIR) -componentdir $(SASL_RELEASE) \
		-files include,lib
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component SASL file $@" ; \
	fi
endif # not Linux
endif # SASL_SOURCE_ROOT

ifndef ICU_SOURCE_ROOT
#ICU_RELEASE = $(COMPONENTS_DIR)/libicu/$(ICU_VERSDIR)/$(ICU_RELDATE)/$(NSOBJDIR_NAME)
ICU_RELEASE = $(COMPONENTS_DIR_DEV)/libicu/$(ICU_VERSDIR)/$(ICU_RELDATE)/$(NSOBJDIR_NAME)
ICU_DEP = $(ICU_INCPATH)/unicode/ucol.h
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
endif # ICU_SOURCE_ROOT

ifndef DB_SOURCE_ROOT
#if no version specified, we'll use the latest one
ifndef DB_VERSION
  DB_VERSION=20060308
endif
# define the paths to the component parts
#db_components_share=$(COMPONENTS_DIR)/$(db_component_name)
db_components_share=$(COMPONENTS_DIR_DEV)/$(db_component_name)
MY_NSOBJDIR_TAG=$(NSOBJDIR_TAG).OBJ
db_release_config =$(db_components_share)/$(DB_VERSION)/$(NSCONFIG_NOTAG)$(MY_NSOBJDIR_TAG)
# add ",bin" to DB_FILES if you want the programs like db_verify, db_recover, etc.
DB_FILES=include,lib,bin

ifeq ($(ARCH), WINNT)
  DB_LIB_DEP =$(DB_STATIC_LIB)
else	# not WINNT
  DB_LIB_DEP =$(DB_LIBPATH)/$(DB_LIBNAME).$(DLL_SUFFIX)
endif	# not WINNT

ifndef DB_PULL_METHOD
DB_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(DB_LIB_DEP): $(NSCP_DISTDIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(DB_PULL_METHOD) \
		-objdir $(db_path_config) -componentdir $(db_release_config) \
		-files $(DB_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component $(db_component_name) file $@" ; \
	fi
endif # DB_SOURCE_ROOT

######## END OF OPEN SOURCE COMPONENTS ######################

######## The rest of these components are internal only (for now)

# ADMINUTIL library #######################################
ADMINUTIL_VERSION=$(ADMINUTIL_RELDATE)
ADMINUTIL_BASE=$(ADMINUTIL_VERSDIR)/${ADMINUTIL_VERSION}
ifeq ($(BUILD_MODE), int)
#  ADMINUTIL_IMPORT=$(COMPONENTS_DIR)/${ADMINUTIL_BASE}/$(NSOBJDIR_NAME)
  ADMINUTIL_IMPORT=$(COMPONENTS_DIR_DEV)/${ADMINUTIL_BASE}/$(NSOBJDIR_NAME)
else
# ADMINUTIL_IMPORT=$(COMPONENTS_DIR)/${ADMINUTIL_BASE}/$(NSOBJDIR_NAME)
  ADMINUTIL_IMPORT=$(FED_COMPONENTS_DIR)/${ADMINUTIL_BASE}/$(NSOBJDIR_NAME)
endif
ADMINUTIL_BUILD_DIR=$(NSCP_DISTDIR_FULL_RTL)/adminutil

#
# Libadminutil
#
ADMINUTIL_DEP = $(ADMINUTIL_LIBPATH)/libadminutil.$(DLL_SUFFIX).$(ADMINUTIL_DOT_VER)

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
# Net-SNMP

ifndef NETSNMP_SOURCE_ROOT
ifneq ($(ARCH), Linux)
#NETSNMP_RELEASE = $(COMPONENTS_DIR_DEV)/net-snmp/$(NETSNMP_VER)/$(NSOBJDIR_NAME)
NETSNMP_RELEASE = $(COMPONENTS_DIR)/net-snmp/$(NETSNMP_VER)/$(NSOBJDIR_NAME)
NETSNMP_DEP = $(NETSNMP_INCDIR)/net-snmp/net-snmp-includes.h
ifndef NETSNMP_PULL_METHOD
NETSNMP_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(NETSNMP_DEP): $(NSCP_DISTDIR_FULL_RTL)
ifneq ($(ARCH), WINNT)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(NETSNMP_PULL_METHOD) \
		-objdir $(NETSNMP_BUILD_DIR) -componentdir $(NETSNMP_RELEASE) \
		-files lib,include,bin
endif
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component NETSNMP file $@" ; \
	fi
endif # Linux
endif # NETSNMP_SOURCE_ROOT

###########################################################

### SETUPUTIL #############################
# this is where the build looks for setupsdk components
SETUPUTIL_BUILD_DIR = $(NSCP_DISTDIR)/setuputil
SETUPUTIL_VERSION = $(SETUPUTIL_RELDATE)
ifeq ($(BUILD_MODE), int)
#  SETUPUTIL_RELEASE = $(COMPONENTS_DIR)/$(SETUPUTIL_VERSDIR)/$(SETUPUTIL_VERSION)/$(NSOBJDIR_NAME)
  SETUPUTIL_RELEASE = $(COMPONENTS_DIR_DEV)/$(SETUPUTIL_VERSDIR)/$(SETUPUTIL_VERSION)/$(NSOBJDIR_NAME)
else
  SETUPUTIL_RELEASE = $(FED_COMPONENTS_DIR)/$(SETUPUTIL_VERSDIR)/$(SETUPUTIL_VERSION)/$(NSOBJDIR_NAME)
endif

ifeq ($(ARCH), WINNT)
SETUPUTIL_FILES = setuputil.tar.gz -unzip $(NSCP_DISTDIR)/setuputil
SETUPUTIL_DEP = $(SETUPUTIL_LIBPATH)/nssetup32.$(LIB_SUFFIX)
else
SETUPUTIL_FILES = bin,lib,include
SETUPUTIL_DEP = $(SETUPUTIL_LIBPATH)/libinstall.$(LIB_SUFFIX)
endif

ifndef SETUPUTIL_PULL_METHOD
SETUPUTIL_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(SETUPUTIL_DEP): $(NSCP_DISTDIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(SETUPUTIL_PULL_METHOD) \
		-objdir $(SETUPUTIL_BUILD_DIR) -componentdir $(SETUPUTIL_RELEASE) \
		-files $(SETUPUTIL_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component SETUPUTIL file $@" ; \
	fi

# apache-axis java classes #######################################
AXIS_RELEASE = $(COMPONENTS_DIR)/axis
#AXISJAR_DIR = $(AXISJAR_RELEASE)/$(AXISJAR_COMP)/$(AXISJAR_VERSION)
AXIS_DIR = $(AXIS_RELEASE)/$(AXIS_VERSION)
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
DSMLJAR_FILES = $(DSMLJAR)
DSMLJAR_RELEASE = $(COMPONENTS_DIR)
#DSMLJARJAR_DIR = $(DSMLJARJAR_RELEASE)/$(DSMLJARJAR_COMP)/$(DSMLJARJAR_VERSION)
DSMLJAR_DIR = $(DSMLJAR_RELEASE)/dsmljars
DSMLJAR_DEP = $(CLASS_DEST)/activation.jar $(CLASS_DEST)/jaxrpc-api.jar $(CLASS_DEST)/jaxrpc.jar $(CLASS_DEST)/saaj.jar $(CLASS_DEST)/xercesImpl.jar $(CLASS_DEST)/xml-apis.jar $(CLASS_DEST)/jakarta-commons-codec.jar

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
CRIMSONJAR_FILES = $(CRIMSONJAR),$(CRIMSON_LICENSE)
CRIMSONJAR_RELEASE = $(COMPONENTS_DIR)
CRIMSONJAR_DIR = $(CRIMSONJAR_RELEASE)/$(CRIMSONJAR_COMP)/$(CRIMSONJAR_VERSION)
CRIMSONJAR_DEP = $(CRIMSONJAR_FILE) $(CLASS_DEST)/$(CRIMSON_LICENSE)

# This is java, so there is only one real platform subdirectory

PACKAGE_UNDER_JAVA += $(CRIMSONJAR_FILE)

ifndef CRIMSONJAR_PULL_METHOD
CRIMSONJAR_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(CRIMSONJAR_DEP): $(CLASS_DEST)
ifdef COMPONENT_DEPS
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
ifndef GET_ANT_FROM_PATH
#  (we use ant for building some Java code)
ANTJAR = ant.jar ant-launcher.jar
#JAXPJAR = jaxp.jar # ???
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
endif # GET_ANT_FROM_PATH
###########################################################

# LDAP java classes #######################################
LDAPJDK_VERSION = $(LDAPJDK_RELDATE)
LDAPJDK_RELEASE = $(COMPONENTS_DIR)
LDAPJDK_IMPORT = $(LDAPJDK_RELEASE)/$(LDAPJDK_COMP)/$(LDAPJDK_VERSION)/$(NSOBJDIR_NAME)
# This is java, so there is only one real platform subdirectory
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
# LDAP Console java classes
###########################################################

ifeq ($(BUILD_MODE), int)
#LDAPCONSOLE_RELEASE=$(COMPONENTS_DIR)
LDAPCONSOLE_RELEASE=$(COMPONENTS_DIR_DEV)
else
LDAPCONSOLE_RELEASE=$(FED_COMPONENTS_DIR)
endif
LDAPCONSOLE_JARDIR = $(LDAPCONSOLE_RELEASE)/$(LDAPCONSOLE_COMP)/$(LDAPCONSOLE_RELDATE)/$(NSOBJDIR_NAME)
LDAPCONSOLE_DEP = $(LDAPCONSOLE_DIR)/$(LDAPCONSOLEJAR)
LDAPCONSOLE_FILES=$(LDAPCONSOLEJAR),$(LDAPCONSOLEJAR_EN)

ifndef LDAPCONSOLE_PULL_METHOD
LDAPCONSOLE_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(LDAPCONSOLE_DEP): $(LDAPCONSOLE_DIR)
ifdef COMPONENT_DEPS
	$(FTP_PULL) -method $(LDAPCONSOLE_PULL_METHOD) \
		-objdir $(LDAPCONSOLE_DIR) -componentdir $(LDAPCONSOLE_JARDIR) \
		-files $(LDAPCONSOLE_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component LDAPCONSOLE file $@" ; \
	fi

###########################################################
### Perldap package #######################################

#PERLDAP_COMPONENT_DIR = $(COMPONENTS_DIR_DEV)/perldap/$(PERLDAP_VERSION)/$(NSOBJDIR_NAME_32)
ifeq ($(BUILD_MODE), int)
  PERLDAP_COMPONENT_DIR = $(COMPONENTS_DIR_DEV)/perldap/$(PERLDAP_VERSION)/$(NSOBJDIR_NAME_32)
  ifeq ($(BUILD_ARCH), RHEL4)
    # use 64-bit perl on 64-bit RHEL4; 32-bit on 32-bit RHEL4
    PERLDAP_COMPONENT_DIR = $(COMPONENTS_DIR_DEV)/perldap/$(PERLDAP_VERSION)/$(NSOBJDIR_NAME)
  endif
  ifeq ($(BUILD_ARCH), HPUX)
    HPUX_ARCH := $(shell uname -m)
    ifeq ($(HPUX_ARCH), ia64)
      # use 64-bit perl on 64-bit IPF HP-UX
      PERLDAP_COMPONENT_DIR = $(COMPONENTS_DIR_DEV)/perldap/$(PERLDAP_VERSION)/$(NSOBJDIR_NAME)
    endif
  endif
else
PERLDAP_COMPONENT_DIR = $(FED_COMPONENTS_DIR)/perldap/$(PERLDAP_VERSION)/$(NSOBJDIR_NAME_32)
endif
PERLDAP_FILES=lib,arch
PERLDAP_DEP = $(PERLDAP_BUILT_DIR)/lib

# this is the rule to pull PerLDAP
ifndef PERLDAP_PULL_METHOD
PERLDAP_PULL_METHOD = FTP
endif

$(PERLDAP_DEP):
ifdef INTERNAL_BUILD
	$(RM) -rf $@
	$(FTP_PULL) -method $(PERLDAP_PULL_METHOD) \
		-objdir $(dir $@) \
		-componentdir $(PERLDAP_COMPONENT_DIR) \
		-files $(PERLDAP_FILES)
	@if [ ! -d $@ ] ; \
	then echo "Error: could not get component PERLDAP file $@" ; \
	exit 1 ; \
	fi
endif

###########################################################
### Admin Server package ##################################

ADMIN_REL = $(ADM_VERSDIR)
ADMIN_REL_DATE = $(ADM_VERSION)
ADMIN_FILE := $(ADMINSERVER_PKG)
#ADMIN_FILE = $(subst $(SPACE),$(COMMA),$(ADMINSERVER_SUBCOMPS))
ifeq ($(BUILD_MODE), int)
IMPORTADMINSRV_BASE=$(COMPONENTS_DIR_DEV)/$(ADMIN_REL)/$(ADMIN_REL_DATE)
else
IMPORTADMINSRV_BASE=$(FED_COMPONENTS_DIR)/$(ADMIN_REL)/$(ADMIN_REL_DATE)
endif
IMPORTADMINSRV = $(IMPORTADMINSRV_BASE)/$(NSOBJDIR_NAME)
ADMSERV_DEP = $(ADMSERV_DIR)/$(ADMINSERVER_PKG)

ADM_VERSION = $(ADM_RELDATE)

ifndef ADMSERV_PULL_METHOD
ADMSERV_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

ifndef ADMSERV_DEPS
ADMSERV_DEPS = $(COMPONENT_DEPS)
endif

$(ADMSERV_DEP): $(ABS_ROOT_PARENT)/dist/$(NSOBJDIR_NAME)
ifdef ADMSERV_DEPS
	$(FTP_PULL) -method $(ADMSERV_PULL_METHOD) \
		-objdir $(ADMSERV_DIR) -componentdir $(IMPORTADMINSRV) \
		-files $(ADMIN_FILE)
endif
	@if [ ! -f $@ ] ; \
	then echo "Error: could not get component ADMINSERV file $@" ; \
	exit 1 ; \
	fi
### Admin Server END ######################################

### DOCS #################################
# this is where the build looks for slapd docs
DSDOC_VERSDIR = $(DIR_NORM_VERSION)
ifeq ($(BUILD_MODE), int)
#DSDOC_RELEASE = $(COMPONENTS_DIR_DEV)/ldapserverdoc/$(DIR_NORM_VERSION)/$(DSDOC_RELDATE)
DSDOC_RELEASE = $(COMPONENTS_DIR)/ldapserverdoc/$(DIR_NORM_VERSION)/$(DSDOC_RELDATE)
else
DSDOC_RELEASE = $(FED_COMPONENTS_DIR)/ldapserverdoc/$(DIR_NORM_VERSION)/$(DSDOC_RELDATE)
endif

DSDOC_FILES = $(DSDOC_COPYRIGHT),$(DSDOC_CLIENTS)
DSDOC_DEP := $(DSDOC_DIR)/$(DSDOC_COPYRIGHT)

ifndef DSDOC_PULL_METHOD
DSDOC_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(DSDOC_DEP): $(NSCP_DISTDIR)
	$(FTP_PULL) -method $(DSDOC_PULL_METHOD) \
		-objdir $(DSDOC_DIR) -componentdir $(DSDOC_RELEASE) \
		-files $(DSDOC_FILES)
	@if [ ! -f $@ ] ; \
	then echo "Error: could not get component DSDOC file $@" ; \
	exit 1 ; \
	fi
### DOCS END #############################

# Windows sync component for Active Directory
ADSYNC = PassSync.msi
ADSYNC_DEST = $(NSCP_DISTDIR_FULL_RTL)/winsync
ADSYNC_FILE = $(ADSYNC_DEST)/$(ADSYNC)
ADSYNC_FILES = $(ADSYNC)
ADSYNC_RELEASE = $(COMPONENTS_DIR)/winsync/passsync
# windows make naming convention - release = optimize, debug = full
ifeq ($(BUILD_DEBUG), optimize)
	ADSYNC_DIR_SUFFIX=release
else
	ADSYNC_DIR_SUFFIX=debug
endif
ADSYNC_DIR = $(ADSYNC_RELEASE)/$(ADSYNC_VERSION)/$(ADSYNC_DIR_SUFFIX)

ADSYNC_DEP = $(ADSYNC_FILE)
PACKAGE_SRC_DEST += $(ADSYNC_FILE) winsync

ifndef ADSYNC_PULL_METHOD
ADSYNC_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(ADSYNC_DEP): $(NSCP_DISTDIR_FULL_RTL) 
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(ADSYNC_DEST) -componentdir $(ADSYNC_DIR) \
		-files $(ADSYNC_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component ADSYNC files $@" ; \
	fi
# Windows sync component for Active Directory

# Windows sync component for NT4
NT4SYNC = ntds.msi
NT4SYNC_DEST = $(NSCP_DISTDIR_FULL_RTL)/winsync
NT4SYNC_FILE = $(NT4SYNC_DEST)/$(NT4SYNC)
NT4SYNC_FILES = $(NT4SYNC)
NT4SYNC_RELEASE = $(COMPONENTS_DIR)/winsync/ntds
# windows make naming convention - release = optimize, debug = full
ifeq ($(BUILD_DEBUG), optimize)
	NT4SYNC_DIR_SUFFIX=release
else
	NT4SYNC_DIR_SUFFIX=debug
endif
NT4SYNC_DIR = $(NT4SYNC_RELEASE)/$(NT4SYNC_VERSION)/$(NT4SYNC_DIR_SUFFIX)

NT4SYNC_DEP = $(NT4SYNC_FILE)
PACKAGE_SRC_DEST += $(NT4SYNC_FILE) winsync

ifndef NT4SYNC_PULL_METHOD
NT4SYNC_PULL_METHOD = $(COMPONENT_PULL_METHOD)
endif

$(NT4SYNC_DEP): $(NSCP_DISTDIR_FULL_RTL) 
ifdef COMPONENT_DEPS
	echo "Inside ftppull"
	$(FTP_PULL) -method $(COMPONENT_PULL_METHOD) \
		-objdir $(NT4SYNC_DEST) -componentdir $(NT4SYNC_DIR) \
		-files $(NT4SYNC_FILES)
endif
	-@if [ ! -f $@ ] ; \
	then echo "Error: could not get component NT4SYNC files $@" ; \
	fi
# Windows sync component for NT4

# BUILD_BOMB stuff
PUMPKIN_TARGET = pumpkin
$(PUMPKIN_TARGET):
ifdef BUILD_PUMPKIN
	@echo NSOS_RELEASE is: $(NSOS_RELEASE)
	$(PERL) pumpkin.pl $(PUMPKIN_AGE) pumpkin.dat
endif # BUILD_PUMPKIN
