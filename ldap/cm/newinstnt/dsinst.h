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

//////////////////////////////////////////////////////////////////////////////
// dsinst.h - Netscape SuiteSpot Installation Plug-In Directory Server
//
//

#ifndef __DSINST_H
#define __DSINST_H



#include <regparms.h>
#include "libinst.h"
#include "plstr.h"

extern __declspec(dllexport) DSINST_ReadComponentInf(LPCSTR pszCacheFile, LPCSTR pszSection);
extern __declspec(dllexport) INT  __cdecl DSINST_AskOptions(HWND hwndParent, INT nDirection);
extern __declspec(dllexport) VOID __cdecl DSINST_GetSummary(LPSTR lpszSummary);
extern __declspec(dllexport) BOOL __cdecl DSINST_WriteCacheGlobal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl DSINST_WriteCacheLocal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl DSINST_ReadCacheGlobal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl DSINST_ReadCacheLocal(LPCSTR lpszCacheFileName, LPCSTR lpszSection);
extern __declspec(dllexport) BOOL __cdecl DSINST_PreInstall(LPCSTR lpszInstallPath);
extern __declspec(dllexport) BOOL __cdecl DSINST_Install(void);
extern __declspec(dllexport) BOOL __cdecl DSINST_PostInstall(void);
extern __declspec(dllexport) BOOL __cdecl DSINST_PreUnInstall(LPCSTR pszServerRoot);
extern __declspec(dllexport) BOOL __cdecl DSINST_PostUnInstall(LPCSTR pszServerRoot);

/* no attr we add uses more the 5 */
#define MAX_LDAP_ATTR_VALUES 5

/* shutdown tries * shutdown time should == 10 minutes */
#define MAX_SLAPD_SHUTDOWN_TRIES 2400
#define SLAPD_SHUTDOWN_TIME_MILLISECONDS 15000

/* stevross: GLOBAL_INF_ only here to speed integration until
	admin header file gets updated */
#define GLOBAL_INF_LDAP_INSTALL_DN "InstallationRootDN"

#define LOCAL_INF_HOST					"InstanceHost"
#define LOCAL_INF_PORT					"InstancePort"
#define LOCAL_INF_SUFFIX				"InstanceSuffix"
#define LOCAL_INF_ROOTDN                "RootDN"
#define LOCAL_INF_ROOTDN_PASSWD         "RootDNPwd"
#define LOCAL_INF_CONFIG_SSPT           "ConfigSspt"

#define LOCAL_INF_CONFIG_CONSUMER_DN    "ConfigConsumerDN"
#define LOCAL_INF_SNMP_ON				"SNMPServiceOn"
#define NETSCAPEROOT                    "NetscapeRoot"

/* for now admin wants suffix always to be o=netscaperoot,
   have it defined here in case it changes */
#define CONFIG_DIR_SUFFIX					"o=netscaperoot"

/* default settings */
#define DEFAULT_UNRESTRICTED_USER   "cn=Directory Manager"
#define DEFAULT_SERVER_PORT         389
#define DEFAULT_SECURITY_ON         0
#define DEFAULT_CONFIG_SSPT         1
#define DEFAULT_START_SERVER        1
#define DEFAULT_LDAP_INSTALL_DN     "ou=mcom.com, o=NetscapeRoot"
#define DEFAULT_SSPT_USER           "admin"
#define DEFAULT_SUPPLIER_DN         "cn=Replication Manager"
#define DEFAULT_CHANGELOGDIR    "logs\\changelogdb"
#define DEFAULT_CHANGELOGSUFFIX    "cn=changelog"
#define DEFAULT_ADD_SAMPLE_ENTRIES  0
#define DEFAULT_ADD_ORG_ENTRIES     0
#define DEFAULT_SETUP_CONSUMER      0
#define DEFAULT_CIR_HOST            "\0"
#define DEFAULT_CIR_PORT            389
#define DEFAULT_CIR_SUFFIX          "\0"
#define DEFAULT_CIR_BINDDN          "cn=Replication Consumer"
#define DEFAULT_CIR_BINDDN_PWD       "\0"
#define DEFAULT_CIR_INTERVAL        10 
#define DEFAULT_CIR_DAYS            "0123456"
#define DEFAULT_CIR_TIMES           "0000-2359"
#define DEFAULT_REPLICATION_DN           "cn=supplier"
#define DEFAULT_REPLICATION_PWD          "\0"
#define DEFAULT_SETUP_SUPPLIER      0
#define DEFAULT_SIR_HOST            "\0"
#define DEFAULT_SIR_PORT            389
#define DEFAULT_SIR_SUFFIX          "\0"
#define DEFAULT_SIR_BINDDN          "cn=supplier"
#define DEFAULT_SIR_BINDDN_PWD       "\0"
#define DEFAULT_SIR_DAYS            "0123456"
#define DEFAULT_SIR_TIMES           "0000-2359"
#define DEFAULT_CONFIG_CONSUMER_DN  0
#define DEFAULT_CONSUMER_DN         "cn=Replication Consumer"
#define DEFAULT_CONSUMER_PWD        "\0"
#define DEFAULT_INF_POP_LDIF_FILE   "\0"
#define DEFAULT_POPULATE_SAMPLE_ENTRIES	0
#define DEFAULT_DISABLE_SCHEMA_CHECKING 0
#define DEFAULT_SNMP_ON             0
#define DEFAULT_ADMIN_PORT          80

#define SUGGEST_LDIF "suggest"
#define SAMPLE_LDIF "bin\\slapd\\install\\ldif\\Example.ldif"
#define TEMPLATE_LDIF "bin\\slapd\\install\\ldif\\template.ldif"
#define PROCESSED_TEMPLATE_LDIF "ldif\\sample-org.ldif"
#define LDAP_MODIFY_EXE "bin\\slapd\\server\\ldapmodify.exe"
#define INSTALL_CTRS_BAT "install-nsldapctrs.bat"
#define BIN_SLAPD_INSTALL_BIN "bin\\slapd\\install\\bin"


#define ROOT_DN     "RootDN"
#define ROOT_DN_PWD "RootDNPwd"
#define SERVER_IDENTIFIER "ServerId"


#define CONSUMER_REPL_AGREE    "Consumer Replication Agreement"
#define SUPPLIER_REPL_AGREE    "Supplier Replication Agreement"
#define SERVER_MIGRATION_CLASS "com.netscape.admin.dirserv.task.MigrateCreate"
#define SERVER_CREATION_CLASS "com.netscape.admin.dirserv.task.MigrateCreate"

#define NO_REPLICATION              3
#define CONSUMER_SIR_REPLICATION    1
#define CONSUMER_CIR_REPLICATION    2

#define SUPPLIER_SIR_REPLICATION    1
#define SUPPLIER_CIR_REPLICATION    2


/// inf file stuff

#define SETUP_INF_COM_VENDOR         "Vendor"
#define SETUP_INF_COM_DESC           "Description"
#define SETUP_INF_COM_NAME           "Name"
#define SETUP_INF_COM_NICKNAME       "NickName"
#define SETUP_INF_COM_VERSION        "Version"
#define SETUP_INF_COM_BUILDNUMBER    "BuildNumber"
#define SETUP_INF_COM_REVISION       "Revision"
#define SETUP_INF_COM_CREATIONDATE   "CreationDate"
#define SETUP_INF_COM_EXPIRY         "Expires"
#define SETUP_INF_COM_SECURITY       "Security"

#define SLAPD_MIN_PW_LEN 8
#define SSPT_MIN_PW_LEN  1

#define N_DAYS   8
#define N_TIMES  10

#define SNMP_SERVICE			  "SNMP"

/* error messages */

#define ASCII_ZERO 48

typedef struct tagMODULEINFO {
  HINSTANCE m_hModule;
  HWND      m_hwndParent;
  INT       m_nResult;
  INT		m_nReInstall;
  CHAR      *m_szMCCBindAs;
  INT       m_nInstanceServerPort;
  INT       m_nAdminServerPort;
  INT       m_nCfgSspt;
  INT       m_nPopulateSampleEntries;
  INT       m_nPopulateSampleOrg;
  INT       m_nSetupConsumerReplication;
  INT       m_nSetupSupplierReplication;
  INT       m_nMaxChangeLogRecords;
  INT       m_nMaxChangeLogAge;
  INT       m_nChangeLogAgeMagnitude;
  INT       m_nConsumerSSL;
  INT       m_nSupplierSSL;
  INT       m_nUseSupplierSettings;
  INT       m_nUseChangeLogSettings;
  INT       m_nCIRInterval;
  INT       m_nConsumerPort;
  INT       m_nSupplierPort;
  INT       m_nMCCPort;
  INT       m_nExistingMCC;
  INT		m_nUGPort;
  INT		m_nExistingUG;
  INT		m_nDisableSchemaChecking;
  INT       m_nSNMPOn;
  INT       m_nConfigConsumerDN;
  CHAR      m_szMCCPw[MAX_STR_SIZE];
  CHAR      m_szMCCHost[MAX_STR_SIZE];
  CHAR      m_szMCCSuffix[MAX_STR_SIZE];
  CHAR		m_szUGPw[MAX_STR_SIZE];
  CHAR		m_szUGHost[MAX_STR_SIZE];
  CHAR		m_szUGSuffix[MAX_STR_SIZE];
  CHAR		m_szAdminDomain[MAX_STR_SIZE];
  CHAR		m_szLdapURL[MAX_STR_SIZE];
  CHAR		m_szUserGroupURL[MAX_STR_SIZE];
  CHAR		m_szUserGroupAdmin[MAX_STR_SIZE];
  CHAR		m_szUserGroupAdminPW[MAX_STR_SIZE];
  CHAR      m_szInstallDN[MAX_STR_SIZE];
  CHAR      m_szSsptUid[MAX_STR_SIZE];
  CHAR      m_szSsptUidPw[MAX_STR_SIZE];
  CHAR      m_szSsptUidPwAgain[MAX_STR_SIZE];
  CHAR      m_szSsptUser[MAX_STR_SIZE];
  CHAR      m_szServerIdentifier[MAX_STR_SIZE];
  CHAR      m_szInstanceSuffix[MAX_STR_SIZE];
  CHAR      m_szInstanceUnrestrictedUser[MAX_STR_SIZE];
  CHAR      m_szInstancePassword[MAX_STR_SIZE];
  CHAR      m_szInstancePasswordAgain[MAX_STR_SIZE];
  CHAR      m_szInstanceHostName[MAX_STR_SIZE];
  CHAR      m_szSupplierDN[MAX_STR_SIZE];
  CHAR      m_szSupplierPW[MAX_STR_SIZE];
  CHAR      m_szSupplierPWAgain[MAX_STR_SIZE];
  CHAR      m_szSSLClients[MAX_STR_SIZE];
  CHAR      m_szChangeLogDbDir[MAX_STR_SIZE];
  CHAR      m_szChangeLogSuffix[MAX_STR_SIZE];
  CHAR      m_szConsumerDN[MAX_STR_SIZE];
  CHAR      m_szConsumerPW[MAX_STR_SIZE];
  CHAR      m_szConsumerPWAgain[MAX_STR_SIZE];
  CHAR      m_szConsumerHost[MAX_STR_SIZE];
  CHAR      m_szConsumerRoot[MAX_STR_SIZE];
  CHAR      m_szConsumerBindAs[MAX_STR_SIZE];
  CHAR      m_szConsumerPw[MAX_STR_SIZE];
  CHAR      m_szSupplierHost[MAX_STR_SIZE];
  CHAR      m_szSupplierRoot[MAX_STR_SIZE];
  CHAR      m_szSupplierBindAs[MAX_STR_SIZE];
  CHAR      m_szSupplierPw[MAX_STR_SIZE];
  CHAR      m_szPopLdifFile[MAX_STR_SIZE];
  CHAR      m_szCIRDays[N_DAYS];
  CHAR      m_szCIRTimes[N_TIMES];
  CHAR      m_szSIRDays[N_DAYS];
  CHAR      m_szSIRTimes[N_TIMES];
} MODULEINFO;

typedef struct tagINFDATA
{
  char* szVendor;
  char* szDescription;
  char* szName;
  char* szNickname;
  char* szVersion;
  char* szBuildNumber;
  char* szRevision;
  char* szTimeStamp;
  char* szExpireDate;
  char* szSecurity;
} INFDATA;

typedef struct tagShutdownArgs
{
	HWND hwnd;
	char* pszServiceName;

} ShutdownArg;

#endif // __DSINST_H
