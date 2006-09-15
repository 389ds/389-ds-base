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
  NSPR_RELDATE = v4.6.2-dstest
endif

# SECURITY (NSS) LIBRARY
ifndef SECURITY_RELDATE
  SECURITY_RELDATE = NSS_3_11_1_RTM
endif

# LIBDB
DBDEFS:=
ifndef DB_MAJOR_MINOR
DB_MAJOR_MINOR:=db-4.2
endif
ifndef DB_VERSION
  DB_VERSION:=20060308
endif

# SMARTHEAP
ifndef SH_VERSION
  SH_VERSION:=v6.01
endif

# LDAP SDK
ifndef LDAP_RELDATE
  LDAP_RELDATE = v5.17-sun-merge
endif
ifndef LDAPCOMP_DIR
  LDAPCOMP_DIR=ldapcsdk
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
  ANT_VERSION = 1.6.2
endif
ifndef ANT_COMP
  ANT_COMP = ant
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
  ADM_RELDATE = 20060619
endif
ifndef ADM_VERSDIR
  ADM_VERSDIR = adminserver/1.0
endif

# Net-SNMP
ifndef NETSNMP_VER
  NETSNMP_VER = v5.2.1
endif

# setuputil
ifndef SETUPUTIL_RELDATE
  SETUPUTIL_RELDATE = 20060615
endif
ifndef SETUPUTIL_VER
  SETUPUTIL_VER = 10
  SETUPUTIL_DOT_VER = 1.0
endif

ifndef SETUPUTIL_VERSDIR
  SETUPUTIL_VERSDIR=setuputil/$(SETUPUTIL_DOT_VER)
endif

# server core
ifndef SVRCORE_RELDATE
  SVRCORE_RELDATE = SVRCORE_4_0_1_RTM
endif

# admin utility library
ifndef ADMINUTIL_VER
  ADMINUTIL_VER=10
  ADMINUTIL_DOT_VER=1.0
endif
ifndef ADMINUTIL_RELDATE
  ADMINUTIL_RELDATE=20060615
endif

ifndef ADMINUTIL_VERSDIR
  ADMINUTIL_VERSDIR=adminutil/$(ADMINUTIL_DOT_VER)
endif

# LDAP Console
ifndef LDAPCONSOLE_GENREL
  LDAPCONSOLE_GENREL=1.0
endif
ifndef LDAPCONSOLE_REL
  LDAPCONSOLE_REL=1.0.2
endif
ifndef LDAPCONSOLE_COMP
  LDAPCONSOLE_COMP = directoryconsole
endif
ifndef LDAPCONSOLE_RELDATE
  LDAPCONSOLE_RELDATE=$(LDAPCONSOLE_GENREL)/20060323
endif

ifndef PERLDAP_VERSION
  PERLDAP_VERSION=1.5/20060915
endif

ifndef JSS_COMP
  JSS_COMP=jss
endif

ifndef JSS_VERSION
  JSS_VERSION=JSS_3_7_RTM
endif

ifndef JSS_JAR_VERSION
  JSS_JAR_VERSION=3
endif

ifndef SASL_VERSDIR
  SASL_VERSDIR=cyrus
endif
ifndef SASL_RELDATE
  SASL_RELDATE=v2.1.20.2
endif

# jakarta/axis for DSMLGW
ifndef AXIS_VERSION
  AXIS_VERSION=1.2rc3
endif

# ICU
ifndef ICU_VERSDIR
  ICU_VERSDIR=libicu_3_4
endif
ifndef ICU_RELDATE
  ICU_RELDATE=
endif

# DOC
ifndef DSDOC_RELDATE
  DSDOC_RELDATE = 20050311
endif

ifndef ADSYNC_VERSION
	ADSYNC_VERSION=20060330
endif

ifndef NT4SYNC_VERSION
	NT4SYNC_VERSION=20060330
endif
