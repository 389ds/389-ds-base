/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*********************************************************************
**
**
** NAME:
**   install_keywords.h
**
** DESCRIPTION:
**   Miscellaneous stuffs used by ux-update or ux-config
**
** NOTES:
**
**
*/

#ifndef _INSTALL_KEYWORDS_H_
#define _INSTALL_KEYWORDS_H_

#include "global.h"


#ifdef XP_UNIX
#define SLAPD_KEY_FULL_MACHINE_NAME MACHINE_NAME
#else   
#define SLAPD_KEY_FULL_MACHINE_NAME "FullMachineName"
#endif
#define SLAPD_KEY_SERVER_ROOT "ServerRoot"
#define SLAPD_KEY_SERVER_PORT "ServerPort"
#define SLAPD_KEY_SECURITY_ON "SecurityOn"
#define SLAPD_KEY_SECURE_SERVER_PORT "SecureServerPort"
#define SLAPD_KEY_SLAPD_CONFIG_FOR_MC "SlapdConfigForMC"

#ifdef XP_UNIX
#define SLAPD_KEY_SERVER_ADMIN_ID MC_ADMIN_ID
#define SLAPD_KEY_SERVER_ADMIN_PWD MC_ADMIN_PWD
#else
#define SLAPD_KEY_SERVER_ADMIN_ID "ConfigDirectoryAdminID"
#define SLAPD_KEY_SERVER_ADMIN_PWD "ConfigDirectoryAdminPwd"
#endif

#define SLAPD_KEY_SERVER_IDENTIFIER "ServerIdentifier"
#define SLAPD_KEY_SUITESPOT_USERID SS_USER_ID
#define SLAPD_KEY_SUFFIX "Suffix"
#define SLAPD_KEY_ROOTDN "RootDN"
#define SLAPD_KEY_ROOTDNPWD "RootDNPwd"
#define SLAPD_KEY_ADMIN_SERVER_PORT "Port"
#define SLAPD_KEY_OLD_SERVER_ROOT "OldServerRoot"

#ifdef XP_UNIX
#define SLAPD_KEY_K_LDAP_URL CONFIG_LDAP_URL 
#else
#define SLAPD_KEY_K_LDAP_URL "ConfigDirectoryLdapURL"
#endif

#define SLAPD_KEY_K_LDAP_HOST CONFIG_DS_HOST
#define SLAPD_KEY_K_LDAP_PORT CONFIG_DS_PORT
#define SLAPD_KEY_BASE_SUFFIX CONFIG_DS_SUFFIX
#define SLAPD_KEY_ADMIN_SERVER_ID "ServerAdminID"
#define SLAPD_KEY_ADMIN_SERVER_PWD "ServerAdminPwd"
#define SLAPD_KEY_ADD_SAMPLE_ENTRIES "AddSampleEntries"
#define SLAPD_KEY_ADD_ORG_ENTRIES "AddOrgEntries"
#define SLAPD_KEY_INSTALL_LDIF_FILE "InstallLdifFile"
#define SLAPD_KEY_ORG_SIZE "OrgSize"
#define SLAPD_KEY_SETUP_CONSUMER "SetupConsumer"
#define SLAPD_KEY_CIR_HOST "CIRHost"
#define SLAPD_KEY_CIR_PORT "CIRPort"
#define SLAPD_KEY_CIR_SUFFIX "CIRSuffix"
#define SLAPD_KEY_CIR_BINDDN "CIRBindDN"
#define SLAPD_KEY_CIR_BINDDNPWD "CIRBindDNPwd"
#define SLAPD_KEY_CIR_SECURITY_ON "CIRSecurityOn"
#define SLAPD_KEY_CIR_INTERVAL "CIRInterval"
#define SLAPD_KEY_CIR_DAYS "CIRDays"
#define SLAPD_KEY_CIR_TIMES "CIRTimes"
#define SLAPD_KEY_SETUP_SUPPLIER "SetupSupplier"
#define SLAPD_KEY_REPLICATIONDN "ReplicationDN"
#define SLAPD_KEY_REPLICATIONPWD "ReplicationPwd"
#define SLAPD_KEY_CHANGELOGDIR "ChangeLogDir"
#define SLAPD_KEY_CHANGELOGSUFFIX "ChangeLogSuffix"
#define SLAPD_KEY_USE_REPLICATION "UseReplication"
#define SLAPD_KEY_CONSUMERDN "ConsumerDN"
#define SLAPD_KEY_CONSUMERPWD "ConsumerPwd"
#define SLAPD_KEY_SIR_HOST "SIRHost"
#define SLAPD_KEY_SIR_PORT "SIRPort"
#define SLAPD_KEY_SIR_SUFFIX "SIRSuffix"
#define SLAPD_KEY_SIR_BINDDN "SIRBindDN"
#define SLAPD_KEY_SIR_BINDDNPWD "SIRBindDNPwd"
#define SLAPD_KEY_SIR_SECURITY_ON "SIRSecurityOn"
#define SLAPD_KEY_SIR_DAYS "SIRDays"
#define SLAPD_KEY_SIR_TIMES "SIRTimes"
#define SLAPD_KEY_USE_EXISTING_MC "UseExistingMC"
#define SLAPD_KEY_ADMIN_DOMAIN "AdminDomain"
#define SLAPD_KEY_DISABLE_SCHEMA_CHECKING "DisableSchemaChecking"
#define SLAPD_KEY_USE_EXISTING_UG "UseExistingUG"
#define SLAPD_KEY_USER_GROUP_LDAP_URL "UserDirectoryLdapURL"
#define SLAPD_KEY_UG_HOST "UGHost"
#define SLAPD_KEY_UG_PORT "UGPort"
#define SLAPD_KEY_UG_SUFFIX "UGSuffix"
#define SLAPD_KEY_USER_GROUP_ADMIN_ID "UserDirectoryAdminID"
#define SLAPD_KEY_USER_GROUP_ADMIN_PWD "UserDirectoryAdminPwd"
#define SLAPD_KEY_CONFIG_ADMIN_DN "ConfigAdminDN"
/* This is used to pass the name of the log file used in the main setup
   program to the ds_create or ds_remove (for uninstall) so that
   they can all use the same log file
*/
#define SLAPD_INSTALL_LOG_FILE_NAME "LogFileName"

#endif // _INSTALL_KEYWORDS_H_
