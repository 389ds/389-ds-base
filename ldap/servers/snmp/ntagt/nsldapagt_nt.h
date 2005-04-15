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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*-------------------------------------------------------------------------
 *
 * nsldapagt_nt.h - Definitions for NS Directory Server's SNMP subagent on
 *                  NT.
 * 
 * Revision History:
 * 07/25/1997		Steve Ross	Created
 *
 *
 *-----------------------------------------------------------------------*/

#ifndef __NSLDAPAGT_NT_H_
#define __NSLDAPAGT_NT_H_

/*-------------------------------------------------------------------------
 *
 * Defines 
 *
 *-----------------------------------------------------------------------*/

#define MAGT_MAX_LINELEN 255
#define MAGT_CONFFILE "snmp.conf"
#define MAGT_CONFDIR  "config"
#define DSE_LDIF "dse.ldif"
#define MAGT_LOGFILE "nsldapagt.log"
#define MAGT_TIME_QUANTUM 10

#define MAGT_TRAP_NONE 0
#define MAGT_TRAP_SERVER_DOWN 7001
#define MAGT_TRAP_SERVER_START 7002

#define LDAP_CONFIG_DN "cn=SNMP,cn=config"
#define BASE_OBJECTCLASS_SEARCH "objectclass=*"

#define LDAP_ATTR_SNMP_ENABLED			"nssnmpenabled"
#define LDAP_ATTR_SNMP_DESCRIPTION		"nssnmpdescription"
#define LDAP_ATTR_SNMP_ORGANIZATION		"nssnmporganization"
#define LDAP_ATTR_SNMP_LOCATION			"nssnmplocation"
#define LDAP_ATTR_SNMP_CONTACT			"nssnmpcontact"

#define SNMP_ON "ON"

/*-------------------------------------------------------------------------
 *
 * Types
 *
 *-----------------------------------------------------------------------*/
 
typedef enum
{
  MAGT_FALSE = 0,
  MAGT_TRUE
} MagtBool_t;

typedef enum
{
  MAGT_TRAP_GENERATION,
  MAGT_TRAP_CLEANUP
} MagtTrapTask_t;

typedef struct MagtDispStr
{
  int len;
  unsigned char *val;
} MagtDispStr_t;

typedef struct MagtStaticInfo
{
  MagtDispStr_t entityDescr;
  MagtDispStr_t entityVers;
  MagtDispStr_t entityOrg;
  MagtDispStr_t entityLocation;
  MagtDispStr_t entityContact;
  MagtDispStr_t entityName;
  int ApplIndex;

} MagtStaticInfo_t;

typedef struct MagtLDAPInfo
{
  char *host;
  int port;
  char *rootdn;
  char *rootpw;


} MagtLDAPInfo_t;

typedef struct MagtHdrInfo
{
/* versMajor and versMinor are no longer used. <03/04/05> */
//  int versMajor;
//  int versMinor;
  int restarted;
  time_t startTime;
  time_t updateTime;
} MagtHdrInfo_t;

typedef struct MagtOpsTblInfo
{
  int AnonymousBinds;
  int UnAuthBinds;
  int SimpleAuthBinds;
  int StrongAuthBinds;
  int BindSecurityErrors;
  int InOps;
  int ReadOps;
  int CompareOps;
  int AddEntryOps;
  int RemoveEntryOps;
  int ModifyEntryOps;
  int ModifyRDNOps;
  int ListOps;
  int SearchOps;
  int OneLevelSearchOps;
  int WholeSubtreeSearchOps;
  int Referrals;
  int Chainings;
  int SecurityErrors;
  int Errors;
} MagtOpsTblInfo_t;

typedef struct MagtEntriesTblInfo
{
  int MasterEntries;
  int CopyEntries;
  int CacheEntries;
  int CacheHits;
  int SlaveHits;
} MagtEntriesTblInfo_t;

typedef struct MagtIntTblInfo
{
  MagtDispStr_t DsName;
  time_t        TimeOfCreation;
  time_t        TimeOfLastAttempt;
  time_t        TimeOfLastSuccess;
  int           FailuresSinceLastSuccess;
  int           Failures;
  int           Successes;
  MagtDispStr_t URL;
}MagtIntTblInfo_t;

typedef struct instance_list_t {
	PERF_INSTANCE_DEFINITION	instance;
	PWSTR						pInstanceName;
	PWSTR						pConfPath;
	int							Handle;
	struct agt_stats_t *		pData;
	HANDLE                      ghTrapEvent;
	MagtOpsTblInfo_t *          pOpsStatInfo;
    MagtEntriesTblInfo_t *      pEntriesStatInfo;
    MagtIntTblInfo_t **         ppIntStatInfo;
    MagtStaticInfo_t *          pCfgInfo;
    MagtMibEntry_t *            pMibInfo;
    char *                      szRootDir;
    char                        szLogPath[MAX_PATH];
    char                        szStatsPath[MAX_PATH];
    int SNMPOff;


	/* trap stuff */
	time_t                      oldUpdateTime;
    time_t                      oldStartTime;
    int                         graceCycles;
    MagtBool_t                  mmapStale; 
    MagtBool_t                  mmapOk;
    MagtBool_t                  serverUp;
    MagtBool_t                  downTrapSent;
    int                         trapType;
    MagtHdrInfo_t               hdrInfo;

	struct instance_list_t *	pNext;
} instance_list_t;

/*-------------------------------------------------------------------------
 *
 * Prototypes
 *
 *-----------------------------------------------------------------------*/

int MagtCheckServer(instance_list_t *pInstance);

void MagtCleanUp();

void MagtConfProcess(char *line, int lineLen, 
                     MagtLDAPInfo_t *info);

char *MagtGetRootDir(void);


void MagtInitMibStorage(MagtMibEntry_t *        MibInfo, 
						MagtOpsTblInfo_t *      pOpsStatInfo,
						MagtEntriesTblInfo_t *  pEntriesStatInfo, 
						MagtIntTblInfo_t **     ppIntStatInfo,
						MagtStaticInfo_t *      pCfgInfo);


int MagtLoadStaticInfo(MagtStaticInfo_t *staticInfo, char *pszRootDir, int *SNMPOff, char *pszLogPath);

void MagtLog(char *logMsg, char *pszLogPath);

char *MagtLogTime();

void MagtOpenLog(char *pszRootDir, char *pszLogPath);

int MagtReadLine(char *buf, 
                 int n, 
                 FILE *fp);

int MagtReadStats(MagtHdrInfo_t *hdrInfo, 
                  MagtOpsTblInfo_t *OpsTblInfo,
                  MagtEntriesTblInfo_t *EntriesTblInfo,
                  MagtIntTblInfo_t **IntTblInfo,
				  char * pszStatsPath,
				  char * pszLogPath);

DWORD _FindNetscapeServers();

extern instance_list_t *pInstanceList;


#endif					/* __NSLDAPAGT_NT_H_ */
