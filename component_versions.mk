#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# This file contains the version definitions for all components used in the build.  It
# should be COMPLETELY AND TOTALLY SELF CONTAINED e.g. no references to macros defined
# outside of this file.

# The XXX_DIR OR XXX_VERSDIR or XXX_COMP or XXX_COMP_DIR macros are the name of the
# base directory for the component under the main components directory
# for example, the LDAP SDK component directory is
# $(COMPONENTS_DIR)/$(LDAPCOMP_DIR) == /share/builds/components/ldapsdk31

# the XXX_RELDATE or XXX_VERSION macros are the name of the subdirectory under
# the component directory where the specific version can be found.  This is
# usually in the form of YYYYMMDD, although NSPR et. al use a different
# naming scheme.
# NSPR
ifndef NSPR_RELDATE
  NSPR_RELDATE = DS7.0
endif

# SECURITY (NSS) LIBRARY
ifndef SECURITY_RELDATE
  SECURITY_RELDATE = DS7.0
endif

# LIBDB
DBDEFS:=
ifndef DB_MAJOR_MINOR
DB_MAJOR_MINOR:=db42
endif
ifndef DB_VERSION
  DB_VERSION:=DS7.0
endif

# DBM Library
ifndef DBM_RELDATE
  DBM_RELDATE = DS7.0
endif

# SMARTHEAP
ifndef SH_VERSION
  SH_VERSION:=v6.01
endif

# LDAP SDK
ifndef LDAP_RELDATE
  LDAP_RELDATE = DS7.0
endif
ifndef LDAPCOMP_DIR
  LDAPCOMP_DIR=ldapsdk50
endif

# DSRK
ifndef DSRK_RELDATE
  DSRK_RELDATE = 20041029
endif
ifndef DSRKCOMP_DIR
  DSRKCOMP_DIR=dsrk70
endif

# CRIMSONJAR 
ifndef CRIMSONJAR_VERSION
  CRIMSONJAR_VERSION = 1.1.3
endif
ifndef CRIMSONJAR_COMP
  CRIMSONJAR_COMP = crimson
endif

# ANT 
ifndef ANT_VERSION
  ANT_VERSION = 1.4.1
endif
ifndef ANT_COMP
  ANT_COMP = ant
endif

# Servlet SDK
ifndef SERVLET_VERSION
  SERVLET_VERSION = 2.3
endif
ifndef SERVLET_COMP
  SERVLET_COMP = javax/servlet
endif

# LDAP JDK
ifndef LDAPJDK_RELDATE
  LDAPJDK_RELDATE = v4.17
endif
ifndef LDAPJDK_COMP
  LDAPJDK_COMP = ldapjdk41
endif

# admin server

ifndef ADM_RELDATE
  ADM_RELDATE = 20041117
endif
ifndef ADM_VERSDIR
  ADM_VERSDIR = admserv62
endif

# peer
ifndef PEER_RELDATE
  PEER_RELDATE = DS7.0
endif

# setup sdk
ifndef SETUP_SDK_RELDATE
  SETUP_SDK_RELDATE = DS7.0
endif
ifndef SETUPSDK_VERSDIR
  SETUPSDK_VERSDIR = v6.2
endif

# infozip utilities
ifndef INFOZIP_RELDATE
  INFOZIP_RELDATE = CMSTOOLS_7_x
endif

# server core
ifndef SVRCORE_RELDATE
  SVRCORE_RELDATE = DS7.0
endif

# admin utility library
ifndef ADMINUTIL_VER
  ADMINUTIL_VER=62
endif
ifndef ADMINUTIL_RELDATE
  ADMINUTIL_RELDATE=20041117
endif

ifndef ADMINUTIL_VERSDIR
  ADMINUTIL_VERSDIR=adminsdk$(ADMINUTIL_VER)
endif

# MCC (Console JDK)
ifndef MCC_REL
  MCC_REL=62
endif
ifndef MCC_COMP
  MCC_COMP = consolesdk$(MCC_REL)
endif
ifndef MCC_RELDATE
  MCC_RELDATE=20041117
endif

ifndef PERLDAP_VERSION
  PERLDAP_VERSION=20041116
endif

ifndef JSS_COMP
  JSS_COMP=jss
endif

ifndef JSS_VERSION
  JSS_VERSION=DS7.0
endif

ifndef JSS_JAR_VERSION
  JSS_JAR_VERSION=3
endif

ifndef SASL_VERSDIR
  SASL_VERSDIR=sasl_1_0
endif
ifndef SASL_RELDATE
  SASL_RELDATE=20041130
endif

# jakarta/axis for DSMLGW
ifndef TOMCAT_VERSION
  TOMCAT_VERSION=5.0.27
endif
ifndef AXIS_VERSION
  AXIS_VERSION=1_2beta
endif

# JSP compiler jasper
ifndef JSPC_VERSION
  JSPC_VERSION = 4.0.3
endif
ifndef JSPC_COMP
  JSPC_COMP = javax/jasper
endif

# ICU
ifndef ICU_VERSDIR
  ICU_VERSDIR=libicu_2_4
endif
ifndef ICU_RELDATE
  ICU_RELDATE=DS7.0
endif
