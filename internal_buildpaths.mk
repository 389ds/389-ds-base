#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# This file is where you tell the build process where to find the
# various components used during the build process.

# You can either use components built locally from source or
# pre-built components.  The reason for the different macros
# for SOURCE and BUILD is that the locations for the libs, includes,
# etc. are usually different for packages built from source vs.
# pre-built packages.  As an example, when building NSPR from
# source, the includes are in mozilla/dist/$(OBJDIR_NAME)/include
# where OBJDIR_NAME includes the OS, arch, compiler, thread model, etc.
# When using the pre-built NSPR from Mozilla FTP, the include files
# are just in nsprdir/include.  This is why we have to make the
# distinction between a SOURCE component and a BUILD (pre-built)
# component.  See components.mk for the gory details.

# For each component, specify the source root OR the pre-built
# component directory.  If both a SOURCE_ROOT and a BUILD_DIR are
# defined for a component, the SOURCE_ROOT will be used - don't do
# this, it's confusing.

# For the Mozilla components, if using source for all of them,
# you can just define MOZILLA_SOURCE_ROOT - the build will
# assume all of them have been built in that same directory
# (as per the recommended build instructions)

# For all components, the recommended way is to put each
# component in a subdirectory of the parent directory of
# BUILD_ROOT, both with pre-built and source components

# work around vsftpd -L problem
ifeq ($(COMPONENT_PULL_METHOD), FTP)
ifdef USING_VSFTPD
VSFTPD_HACK=1
endif
endif

#MOZILLA_SOURCE_ROOT = $(BUILD_ROOT)/../mozilla

#NSPR_SOURCE_ROOT = $(MOZILLA_SOURCE_ROOT)
ifndef NSPR_SOURCE_ROOT
NSPR_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/nspr
# NSPR also needs a build dir with a full, absolute path for some reason
NSPR_ABS_BUILD_DIR = $(NSCP_ABS_DISTDIR_FULL_RTL)/nspr
endif # NSPR_SOURCE_ROOT

#DBM_SOURCE_ROOT = $(MOZILLA_SOURCE_ROOT)
ifndef DBM_SOURCE_ROOT
DBM_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/dbm
endif # DBM_SOURCE_ROOT

#SECURITY_SOURCE_ROOT = $(MOZILLA_SOURCE_ROOT)
ifndef SECURITY_SOURCE_ROOT
SECURITY_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/nss
endif # SECURITY_SOURCE_ROOT

#SVRCORE_SOURCE_ROOT = $(MOZILLA_SOURCE_ROOT)
ifndef SVRCORE_SOURCE_ROOT
SVRCORE_BUILD_DIR = $(NSCP_DISTDIR)/svrcore
endif # SVRCORE_SOURCE_ROOT

#LDAPSDK_SOURCE_ROOT = $(MOZILLA_SOURCE_ROOT)
ifndef LDAPSDK_SOURCE_ROOT
LDAP_ROOT = $(NSCP_DISTDIR_FULL_RTL)/ldapsdk
endif # LDAPSDK_SOURCE_ROOT

#SASL_SOURCE_ROOT = $(BUILD_ROOT)/../cyrus-sasl-2.1.20
ifndef SASL_SOURCE_ROOT
SASL_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/sasl
endif # SASL_SOURCE_ROOT

#NETSNMP_SOURCE_ROOT = $(BUILD_ROOT)/../net-snmp
ifndef NETSNMP_SOURCE_ROOT
NETSNMP_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/net-snmp
endif # NETSNMP_SOURCE_ROOT

#ICU_SOURCE_ROOT = $(BUILD_ROOT)/../icu
ifndef ICU_SOURCE_ROOT
ICU_BUILD_DIR = $(NSCP_DISTDIR_FULL_RTL)/libicu
endif # ICU_SOURCE_ROOT

#DB_SOURCE_ROOT = $(BUILD_ROOT)/../db-4.2.52.NC
# DB_MAJOR_MINOR is the root name for the db shared library
# source builds use db-4.2 - uncomment this if using source
#DB_MAJOR_MINOR := db-4.2
ifndef DB_SOURCE_ROOT
DB_MAJOR_MINOR := db42
db_component_name=$(DB_MAJOR_MINOR)
db_path_config :=$(NSCP_DISTDIR)/$(db_component_name)
endif # DB_SOURCE_ROOT
