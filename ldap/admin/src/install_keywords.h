/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
