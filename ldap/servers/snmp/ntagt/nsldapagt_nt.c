/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*-------------------------------------------------------------------------
 *
 * nsldapagt_nt.c - SNMP Extension Agent for Directory Server on NT.
 *                  Provides SNMP data to NT SNMP Service on behalf of the
 *                  Directory Server installed on the current system.  SNMP
 *                  data is collected from the following sources:
 *                    1. config file (static data)
 *                    2. daemonstats file (dynamic data)
 * 
 * Revision History:
 * 07/25/1997		Steve Ross	Created
 *
 *
 *-----------------------------------------------------------------------*/


#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <windows.h>
#include <winsock.h>
#include <time.h>
#include <snmp.h>
#include "nt/regparms.h"
#include "agtmmap.h"
#include "nslagtcom_nt.h"  
#include "nsldapmib_nt.h"
#include "nsldapagt_nt.h"
#include "ldap.h"

/*-------------------------------------------------------------------------
 *
 * Defines
 *
 *-----------------------------------------------------------------------*/

#define REPLACE(x, y) do {if ((x)) SNMP_free((x));\
                          (x) = SNMP_malloc(strlen((y)) + 1);\
	                         if ((x)) strcpy((x), (y));} while(0)

#define export extern "C"

/*-------------------------------------------------------------------------
 *
 * Globals
 *
 *-----------------------------------------------------------------------*/

instance_list_t *pInstanceList = NULL;

/*
 * Extension Agent DLLs need access to elapsed time agent has been active.
 * This is implemented by initializing the Extension Agent with a time zero
 * reference, and allowing the agent to compute elapsed time by subtracting
 * the time zero reference from the current system time.  This Extension
 * Agent implements this reference with dwTimeZero.
 */
DWORD dwTimeZero = 0;

/*-------------------------------------------------------------------------
 *
 * Externs
 *
 *-----------------------------------------------------------------------*/

extern AsnObjectIdentifier MIB_OidPrefix;
extern UINT MIB_num_vars;

/*------------------------ prototypes -----------------------------------*/
// char *getRootDirFromConfFile(char *filename, char *szLogPath);
char *getRootDirFromConfFile(char *filename);

/*-------------------------------------------------------------------------
 *
 * MagtInitInstance:  initializes entry in instance list
 *                   
 *
 * 
 *          
 *
 *-----------------------------------------------------------------------*/

 int MagtInitInstance(instance_list_t *pInstance)
 {
	
   
	pInstance->ghTrapEvent          = NULL;
	pInstance->pOpsStatInfo         = NULL;
    pInstance->pEntriesStatInfo     = NULL;
    pInstance->ppIntStatInfo        = NULL;
    pInstance->pMibInfo             = NULL;
    pInstance->oldUpdateTime        = 0;
    pInstance->oldStartTime         = 0;
    pInstance->graceCycles          = MAGT_TIME_QUANTUM;
    pInstance->mmapStale            = MAGT_FALSE;
    pInstance->mmapOk               = MAGT_FALSE;
    pInstance->serverUp             = MAGT_FALSE;
    pInstance->downTrapSent         = MAGT_FALSE;
    pInstance->trapType             = MAGT_TRAP_NONE;

	return 0;
 }
 
 int MagtInitStats(MagtOpsTblInfo_t    *OpsTblInfo,
                   MagtEntriesTblInfo_t *EntriesTblInfo,
                   MagtIntTblInfo_t     **IntTblInfo  )
 {
	 int i;

     if (OpsTblInfo != NULL)
	 {
         OpsTblInfo->AnonymousBinds        = 0;
         OpsTblInfo->UnAuthBinds           = 0;
         OpsTblInfo->SimpleAuthBinds       = 0;
         OpsTblInfo->StrongAuthBinds       = 0;
         OpsTblInfo->BindSecurityErrors    = 0;
         OpsTblInfo->InOps                 = 0;
         OpsTblInfo->ReadOps               = 0;
         OpsTblInfo->CompareOps            = 0;
         OpsTblInfo->AddEntryOps           = 0;
         OpsTblInfo->RemoveEntryOps        = 0;
         OpsTblInfo->ModifyEntryOps        = 0;
         OpsTblInfo->ModifyRDNOps          = 0;
         OpsTblInfo->ListOps               = 0;
         OpsTblInfo->SearchOps             = 0;
         OpsTblInfo->OneLevelSearchOps     = 0;
         OpsTblInfo->WholeSubtreeSearchOps = 0;
         OpsTblInfo->Referrals             = 0;
         OpsTblInfo->Chainings             = 0;
         OpsTblInfo->SecurityErrors        = 0;
         OpsTblInfo->Errors                = 0;
      }
      
	  if(EntriesTblInfo != NULL)
	  {
          EntriesTblInfo->MasterEntries = 0;
          EntriesTblInfo->CopyEntries   = 0;
          EntriesTblInfo->CacheEntries  = 0;
          EntriesTblInfo->CacheHits     = 0;
          EntriesTblInfo->SlaveHits     = 0;
      }

      if(IntTblInfo != NULL)
	  {
	      for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
	      {
	            strcpy(IntTblInfo[i]->DsName.val, "Not Available");
                IntTblInfo[i]->DsName.len               = strlen("Not Available"); 
	      
                IntTblInfo[i]->TimeOfCreation           = 0;
                IntTblInfo[i]->TimeOfLastAttempt        = 0;
                IntTblInfo[i]->TimeOfLastSuccess        = 0;
                IntTblInfo[i]->FailuresSinceLastSuccess = 0;
                IntTblInfo[i]->Failures                 = 0;
                IntTblInfo[i]->Successes                = 0;
                
				strcpy(IntTblInfo[i]->URL.val, "Not Available");
                IntTblInfo[i]->URL.len                  = strlen("Not Available"); 
 	      }
	  }

	  return 0;
 }

/*-------------------------------------------------------------------------
 *
 * MagtCheckServer:  Checks the Server's status and indicates
 *                   which trap is to be generated if necessary.
 *
 * Returns:  MAGT_TRAP_NONE - No trap to be generated
 *           Trap # - Id of trap to be generated
 *
 *-----------------------------------------------------------------------*/

int MagtCheckServer(instance_list_t *pInstance)
{
  int err;

  if (pInstance->mmapStale == MAGT_TRUE)
    pInstance->mmapOk = MAGT_FALSE;			/* Ensure open of mmap */

  err = MagtReadStats(&(pInstance->hdrInfo), 
					    NULL, 
						NULL, 
						NULL, 
	                    pInstance->szStatsPath, 
						pInstance->szLogPath);		/* Find times info in hdr */

  if (pInstance->mmapOk == MAGT_FALSE)
  {
      if (err != 0)				/* Cannot open mmap file */
      {
            if ((pInstance->serverUp == MAGT_TRUE) ||		/* Server status changes */
                (pInstance->downTrapSent == MAGT_FALSE))		/* Down trap was not sent */
            {
                pInstance->serverUp = MAGT_FALSE;
                pInstance->downTrapSent = MAGT_TRUE;
                pInstance->trapType = MAGT_TRAP_SERVER_DOWN;
            }
       }  
       else
       {
           pInstance->mmapOk = MAGT_TRUE;

           /*
            * Since mmapOk was false, it means the mmap file couldn't be
            * opened before.  Now it is opened ok, so it will be assumed
            * that the server has gone down and up and a start trap may need
            * to be sent.
            */
            if (pInstance->mmapStale == MAGT_FALSE)
                pInstance->serverUp = MAGT_FALSE;
            else
                pInstance->mmapStale = MAGT_FALSE;			/* Not stale anymore */
        }
  }

  if (pInstance->trapType == MAGT_TRAP_NONE)
  {
      if (err != 0)
      {
          pInstance->mmapOk = MAGT_FALSE;

          /*
           * If the mmap file does not exist, assume server has gone down.
           */
          if (err == ENOENT)
          {
              if((pInstance->serverUp == MAGT_TRUE) ||		/* Server status changes */
                 (pInstance->downTrapSent == MAGT_FALSE))	/* Down trap was not sent */
              {
                  pInstance->serverUp = MAGT_FALSE;
                  pInstance->downTrapSent = MAGT_TRUE;
                  pInstance->trapType = MAGT_TRAP_SERVER_DOWN;
              }
          }
      }   
      else					/* Got hdr info ok */
      {

          /*
           * The fact that header info can be read will be taken as the
           * server is up.  If it was not up before, a server start trap
           * will need to be sent.
           */
          if (((pInstance->hdrInfo.restarted) || (pInstance->hdrInfo.startTime > pInstance->oldStartTime))
			   && (pInstance->hdrInfo.updateTime > pInstance->oldUpdateTime))
          {
              pInstance->oldUpdateTime = pInstance->hdrInfo.updateTime;
              pInstance->oldStartTime  = pInstance->hdrInfo.startTime;
              pInstance->graceCycles   = MAGT_TIME_QUANTUM;
              pInstance->serverUp      = MAGT_TRUE;
              pInstance->downTrapSent  = MAGT_FALSE;
              pInstance->trapType      = MAGT_TRAP_SERVER_START;
          }
          else
          {
              if (pInstance->hdrInfo.updateTime > pInstance->oldUpdateTime)
              {
                  pInstance->oldUpdateTime = pInstance->hdrInfo.updateTime;
                  if (pInstance->graceCycles == 0)
                  {
            
                      /*
                       * The server has probably been stuck and has been restarted.
                       */
                      pInstance->serverUp     = MAGT_TRUE;
                      pInstance->downTrapSent = MAGT_FALSE;
                      pInstance->trapType     = MAGT_TRAP_SERVER_START;
                  }

                  /*
                   * Reset grace cycles in either case because server is healthy.
                   */
                  pInstance->graceCycles = MAGT_TIME_QUANTUM;
              }
              else					/* Mmap file not updated */
              {
                  pInstance->mmapStale = MAGT_TRUE;

                  /*
                   * The server is not responding, send trap if one has not
                   * been sent yet.
                   */
                  if (pInstance->graceCycles > 0)
                  {
                      pInstance->graceCycles--;
                      if (pInstance->graceCycles == 0)
                      {
                          pInstance->trapType = MAGT_TRAP_SERVER_DOWN;
                      }
                  }
              }
          }
      }
  }

  return pInstance->trapType;
}


/*-------------------------------------------------------------------------
 *
 * MagtCleanUp:  Cleans up any allocated global resources.
 *
 * Returns:  None
 *
 *-----------------------------------------------------------------------*/

void MagtCleanUp()
{
    instance_list_t *pInstance;

	for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext) 
	{
        if (pInstance->pCfgInfo != NULL)
            GlobalFree(pInstance->pCfgInfo);
        
		if (pInstance->pMibInfo != NULL)
            GlobalFree(pInstance->pMibInfo);
        
		if (pInstance->szRootDir != NULL)
            GlobalFree(pInstance->szRootDir);
        
		if (pInstance->ghTrapEvent != NULL)
            CloseHandle(pInstance->ghTrapEvent);
	}

}

/*-------------------------------------------------------------------------
 *
 * MagtConfProcess:  Processes a configuration entry and updates the
 *                   corresponding static info field.
 *
 * Returns:  None
 *
 *-----------------------------------------------------------------------*/

void MagtConfProcess(char *line, int lineLen, 
                     MagtLDAPInfo_t *info)
					 
{
  char keyWord[MAGT_MAX_LINELEN + 1];
  char *val, *p;

  if (line == NULL)				/* Shouldn't happen */
    return;

  if ((*line) == '#')				/* Comment - Ignore */
    return;

  keyWord[0] = '\0';

  if (sscanf(line, "%s", keyWord) != 1)		/* Partial entry */
    return;

  val = line;

  /*
   * Go past any spaces preceding the keyword.
   */
  while ((*val) && (isspace(*val)))
    ++val;

  if (!(*val))
    return;

  /*
   * Go past the keyword.
   */
  for (; (*val) && !(isspace(*val)); ++val);

  if (!(*val))
    return;

  /*
   * Go past the spaces that follow the key word.
   */
  while ((*val) && (isspace(*val)))
    ++val;

  if (!(*val))
    return;

  /*
   * Strip CRLF characters.
   */
  if ((p = strchr(val, '\r')) != NULL)
    *p = '\0';
  if ((p = strchr(val, '\n')) != NULL)
    *p = '\0';

  /*
   * Now val points to the value and keyWord points to the key word.
   */
 
  if (!stricmp(keyWord, "nsslapd-port:"))
  {
    info->port = atoi(val);
    return;
  }

  if (!stricmp(keyWord, "nsslapd-localhost:"))
  {
    REPLACE(info->host, val);
    info->host[strlen(info->host)] = '\0';
    return;
  }

  if (!stricmp(keyWord, "nsslapd-rootdn:"))
  {
    REPLACE(info->rootdn, val);
    info->rootdn[strlen(info->rootdn)] = '\0';
    return;
  }

   if (!stricmp(keyWord, "nsslapd-rootpw:"))
  {
    REPLACE(info->rootpw, val);
    info->rootpw[strlen(info->rootpw)] = '\0';
    return;
  }

  /*
   * None of the above?  Invalid keyword.  Just return.
   */
  return;
}

 
char *getRootDirFromConfFile(char *confpath)
{
 char *rootDir      = NULL;
 const char *config = "\\config\0" ;
 char instanceDir[MAGT_MAX_LINELEN + 1] = "";
 size_t len ;

 if (confpath) {
	len = strlen(confpath) - strlen(config) ;
	strncpy(instanceDir, confpath, len);
	rootDir = _strdup(instanceDir) ; // allocate memory for rootDir and set up to value pointed by instanceDir
	return rootDir ;
	}
 else return NULL ;
}


/*-------------------------------------------------------------------------
 *
 * MagtInitMibStorage:  Initializes the storage pointers of MIB variables.
 *
 * Returns:  None
 *
 *-----------------------------------------------------------------------*/

void MagtInitMibStorage(MagtMibEntry_t *        MibInfo, 
						MagtOpsTblInfo_t *      pOpsStatInfo,
						MagtEntriesTblInfo_t *  pEntriesStatInfo, 
						MagtIntTblInfo_t **     ppIntStatInfo,
						MagtStaticInfo_t *      pCfgInfo)
{
  switch(MibInfo->uId)
  {
    case MAGT_ID_DESC:
      MibInfo->Storage = pCfgInfo->entityDescr.val;
      break;
    case MAGT_ID_VERS:
      MibInfo->Storage = pCfgInfo->entityVers.val;
      break;
    case MAGT_ID_ORG:
      MibInfo->Storage = pCfgInfo->entityOrg.val;
      break;
    case MAGT_ID_LOC:
      MibInfo->Storage = pCfgInfo->entityLocation.val;
      break;
    case MAGT_ID_CONTACT:
      MibInfo->Storage = pCfgInfo->entityContact.val; 
      break;
    case MAGT_ID_NAME:
      MibInfo->Storage = pCfgInfo->entityName.val;
      break;
    case MAGT_ID_APPLINDEX:
      MibInfo->Storage = &pCfgInfo->ApplIndex;
      break;

      /* operations table attrs */
    case MAGT_ID_ANONYMOUS_BINDS:
        MibInfo->Storage = &pOpsStatInfo->AnonymousBinds;
    break;
    case MAGT_ID_UNAUTH_BINDS:
        MibInfo->Storage = &pOpsStatInfo->UnAuthBinds;
    break;
    case MAGT_ID_SIMPLE_AUTH_BINDS:  
        MibInfo->Storage = &pOpsStatInfo->SimpleAuthBinds;
    break;
    case MAGT_ID_STRONG_AUTH_BINDS:
        MibInfo->Storage = &pOpsStatInfo->StrongAuthBinds;
    break;
    case MAGT_ID_BIND_SECURITY_ERRORS:
        MibInfo->Storage = &pOpsStatInfo->BindSecurityErrors;
    break;
    case MAGT_ID_IN_OPS:
        MibInfo->Storage = &pOpsStatInfo->InOps;
    break;
    case MAGT_ID_READ_OPS:
        MibInfo->Storage = &pOpsStatInfo->ReadOps;
    break;
    case MAGT_ID_COMPARE_OPS:
        MibInfo->Storage = &pOpsStatInfo->CompareOps;    
    break;
    case MAGT_ID_ADD_ENTRY_OPS:
        MibInfo->Storage = &pOpsStatInfo->AddEntryOps;
    break;
    case MAGT_ID_REMOVE_ENTRY_OPS:
        MibInfo->Storage = &pOpsStatInfo->RemoveEntryOps;
    break;
    case MAGT_ID_MODIFY_ENTRY_OPS:
        MibInfo->Storage = &pOpsStatInfo->ModifyEntryOps;
    break;
    case MAGT_ID_MODIFY_RDN_OPS:
        MibInfo->Storage = &pOpsStatInfo->ModifyRDNOps;
    break;
    case MAGT_ID_LIST_OPS:
        MibInfo->Storage = &pOpsStatInfo->ListOps;
    break;
    case MAGT_ID_SEARCH_OPS:
        MibInfo->Storage = &pOpsStatInfo->SearchOps;
    break;
    case MAGT_ID_ONE_LEVEL_SEARCH_OPS:
        MibInfo->Storage = &pOpsStatInfo->OneLevelSearchOps;
    break;
    case MAGT_ID_WHOLE_SUBTREE_SEARCH_OPS:
        MibInfo->Storage = &pOpsStatInfo->WholeSubtreeSearchOps;
    break;
    case MAGT_ID_REFERRALS:
        MibInfo->Storage = &pOpsStatInfo->Referrals;
    break;
    case MAGT_ID_CHAININGS:
        MibInfo->Storage = &pOpsStatInfo->Chainings;
    break;
    case MAGT_ID_SECURITY_ERRORS:
        MibInfo->Storage = &pOpsStatInfo->SecurityErrors;
    break;
    case MAGT_ID_ERRORS:
        MibInfo->Storage = &pOpsStatInfo->Errors;
    break;
      /* entries table attrs */
    case MAGT_ID_MASTER_ENTRIES:
        MibInfo->Storage = &pEntriesStatInfo->MasterEntries;
    break;
    case MAGT_ID_COPY_ENTRIES:
        MibInfo->Storage = &pEntriesStatInfo->CopyEntries;
    break;
    case MAGT_ID_CACHE_ENTRIES:
        MibInfo->Storage = &pEntriesStatInfo->CacheEntries;
    break;
    case MAGT_ID_CACHE_HITS:
        MibInfo->Storage = &pEntriesStatInfo->CacheHits;
    break;
    case MAGT_ID_SLAVE_HITS:
        MibInfo->Storage = &pEntriesStatInfo->SlaveHits;
    break;
      /* interaction table entries 
        *---------------------------------
	   * a little different because table of N entries, we can get current value of n 
	   * from MibInfo->Oid.ids[MibInfo->Oid.idLength] because dsIntIndex is last,
	   *  subtract 1 from it because oids go from 1 to NUM_SNMP_INT_TBL_ROWS array goes
	   *    from 0 to NUM_SNMP_INT_TBL_ROWS - 1
	   *	if this ever changes this logic will have to change to get it from
	   *	appropriate spot 
	   */
    case MAGT_ID_DS_NAME:
	      MibInfo->Storage = ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->DsName.val; 
	break;
    case MAGT_ID_TIME_OF_CREATION:
        MibInfo->Storage = &(ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->TimeOfCreation);
    break;
    case MAGT_ID_TIME_OF_LAST_ATTEMPT:
        MibInfo->Storage = &(ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->TimeOfLastAttempt);
    break;
    case MAGT_ID_TIME_OF_LAST_SUCCESS:
        MibInfo->Storage = &(ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->TimeOfLastSuccess);
    break;
    case MAGT_ID_FAILURES_SINCE_LAST_SUCCESS:
        MibInfo->Storage = &(ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->FailuresSinceLastSuccess);
    break;
    case MAGT_ID_FAILURES:
        MibInfo->Storage = &(ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->Failures);
    break;
    case MAGT_ID_SUCCESSES:
        MibInfo->Storage = &(ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength - 1] - 1]->Successes);
    break;
    case MAGT_ID_URL:
	    MibInfo->Storage = ppIntStatInfo[MibInfo->Oid.ids[MibInfo->Oid.idLength  - 1] - 1]->URL.val;  
    break;


   default:
      break;
  }
}

/*-------------------------------------------------------------------------
 *
 * ReadStaticSettingsOverLdap:  Reads static information from the directory server
 *                     
 *
 * Returns:  0 - No error
 *           -1 - Errors
 *
 *-----------------------------------------------------------------------*/

int ReadStaticSettingsOverLdap(MagtLDAPInfo_t ldapInfo, MagtStaticInfo_t *staticInfo, int *SNMPoff)
{ 
 LDAP *ld;
 LDAPMessage *result, *e;
 BerElement *ber;
 char *a;
 char **vals;
 char *attrs[]={LDAP_ATTR_SNMP_ENABLED, 
			   LDAP_ATTR_SNMP_DESCRIPTION,
			   LDAP_ATTR_SNMP_ORGANIZATION,
			   LDAP_ATTR_SNMP_LOCATION,
			   LDAP_ATTR_SNMP_CONTACT,
			   NULL};
 /* set the applIndex to the ldap port */
 staticInfo->ApplIndex = ldapInfo.port;

 /* get rest of static settings from the Directory Server */
 if ( ( ld = ldap_init( ldapInfo.host, ldapInfo.port ) ) == NULL ) 
 { 
	return -1; 
 }
 
 if ( ldap_simple_bind_s( ld, NULL, NULL) != LDAP_SUCCESS ) 
 {
    return -1;
 }


 if ( ldap_search_s( ld, LDAP_CONFIG_DN, LDAP_SCOPE_BASE, BASE_OBJECTCLASS_SEARCH, 
							attrs, 0, &result ) != LDAP_SUCCESS ) 
 {
     return -1;

 }else{
	 
     e = ldap_first_entry( ld, result );
	 
	 if(e != NULL)
	 {
		for ( a = ldap_first_attribute( ld, e, &ber );
	          a != NULL; a = ldap_next_attribute( ld, e, ber ) ) 
	    {
            if ((vals = ldap_get_values( ld, e, a)) != NULL ) 
			{
			   MagtDispStr_t *pStaticAttr=NULL;
			   /* we only want the first value, ignore any others */
			   if( 0 == strcmp(LDAP_ATTR_SNMP_ENABLED, a) )
			   {
				   if(0 == stricmp(SNMP_ON, vals[0]) )
				   {
				       *SNMPoff = 0;
				   }else{
					   *SNMPoff = 1;
				   }
			   }else if( 0 == strcmp(LDAP_ATTR_SNMP_DESCRIPTION, a) ){
				   pStaticAttr = &(staticInfo->entityDescr);
			   }else if( 0 == strcmp(LDAP_ATTR_SNMP_ORGANIZATION, a) ){
				   pStaticAttr = &(staticInfo->entityOrg);
			   }else if( 0 == strcmp(LDAP_ATTR_SNMP_LOCATION, a) ){
				   pStaticAttr = &(staticInfo->entityLocation);
			   }else if( 0 == strcmp(LDAP_ATTR_SNMP_CONTACT, a) ){
				   pStaticAttr = &(staticInfo->entityContact);
			   }
			   /* stevross: missing the following for NT
						version
						DSName
			   */
	
			   /* for Unix also missing
					MasterHost, MasterPort
					*/
			   if(pStaticAttr != NULL && vals[0] != NULL)
			   {
			       REPLACE(pStaticAttr->val, vals[0]);
				   pStaticAttr->len = strlen(pStaticAttr->val);
			   }
    
                ldap_value_free( vals );

            }

            ldap_memfree( a );

        }

        if ( ber != NULL ) 
		{
             ldap_ber_free( ber, 0 );
        }
    }
 }

 ldap_msgfree( result );
 ldap_unbind( ld );

 return 0;
}

/*-------------------------------------------------------------------------
 *
 * MagtLoadStaticInfo:  Loads static information from the configuration
 *                      file.
 *
 * Returns:  0 - No error
 *           -1 - Errors
 *
 *-----------------------------------------------------------------------*/

int MagtLoadStaticInfo(MagtStaticInfo_t *staticInfo, char *pszRootDir, int *SNMPOff, char *pszLogPath)
{
  char confpath[MAX_PATH];
  FILE *fp;
  char linebuf[MAGT_MAX_LINELEN + 1];
  int lineLen;
  char logMsg[1024];
  MagtLDAPInfo_t ldapInfo;

  /*
   * Set-up default values first.
   */

  staticInfo->entityDescr.val = NULL;
  staticInfo->entityVers.val = NULL;
  staticInfo->entityOrg.val = NULL;
  staticInfo->entityLocation.val = NULL;
  staticInfo->entityContact.val = NULL;
  staticInfo->entityName.val = NULL;
 
  staticInfo->ApplIndex = 0;
 
  REPLACE(staticInfo->entityDescr.val, "Brandx Directory Server");
  staticInfo->entityDescr.len = strlen(staticInfo->entityDescr.val);
 
  REPLACE(staticInfo->entityVers.val, "7");
  staticInfo->entityVers.len = strlen(staticInfo->entityVers.val);
 
  REPLACE(staticInfo->entityOrg.val, "Not Available");
  staticInfo->entityOrg.len = strlen(staticInfo->entityOrg.val);
 
  REPLACE(staticInfo->entityLocation.val, "Not Available");
  staticInfo->entityLocation.len = strlen(staticInfo->entityLocation.val);
 
  REPLACE(staticInfo->entityContact.val, "Not Available");
  staticInfo->entityContact.len = strlen(staticInfo->entityContact.val);
 
  REPLACE(staticInfo->entityName.val, "Not Available");
  staticInfo->entityName.len = strlen(staticInfo->entityName.val);

  /*
   * Read any config info from dse.ldif (for now its just port used as 
   * applIndex
   */
  
  wsprintf(confpath, "%s/%s/%s", pszRootDir, MAGT_CONFDIR, DSE_LDIF);

  if ((fp = fopen(confpath, "r")) == (FILE *) NULL)
  {
      wsprintf(logMsg,
                 "Failed to open dse.ldif (error = %d)\n",
                  errno);
      MagtLog(logMsg, pszLogPath);
      return (-1);
  }
 
 
  while ((lineLen = MagtReadLine(linebuf, MAGT_MAX_LINELEN, fp)) > 0)
  {
      /*
       * Update the configured entries.
       */
      MagtConfProcess(linebuf, lineLen, &ldapInfo);
  }
  fclose (fp);
 
  if( 0 != ReadStaticSettingsOverLdap(ldapInfo, staticInfo, SNMPOff) < 0 )
  {
      wsprintf(logMsg,
               "Failed to read SNMP configuration over ldap\n");
      MagtLog(logMsg, pszLogPath);
      return (-1);
  } 
 
  return (0);
}

/*-------------------------------------------------------------------------
 *
 * MagtLog:  Logs the specified message into the log file.
 *           Notes:  Log file is opened and closed each time.
 *
 * Returns:  None
 *
 *-----------------------------------------------------------------------*/

void MagtLog(char *logMsg, char *pszLogPath)
{
  FILE *f;
  char *szTime;

  f = fopen(pszLogPath, "a");
  if (!f)
    return;
  szTime = MagtLogTime();
  if (szTime != NULL)
  {
    fprintf(f, "%s %s", szTime, logMsg);
    SNMP_free(szTime);
  }
  else					/* No time string returned */
  {
    fprintf(f, "%s %s", "00000000000000", logMsg);
  }
  fclose(f);
}

/*-------------------------------------------------------------------------
 *
 * MagtLogTime:  Returns time for logging purpose.
 *
 * Returns:  Formatted time string - No error
 *           "00000000000000" - Errors
 *
 *-----------------------------------------------------------------------*/

char *MagtLogTime()
{
  time_t timeNow;
  struct tm tmLocal;
  char dateBuf[64];
  char *timeStr = NULL;
  static timeZoneSet = MAGT_FALSE;

  timeNow = time(0);
  memcpy(&tmLocal, localtime(&timeNow), sizeof(tmLocal));

  /*
   * Set up the timezone information.
   */
  if (!timeZoneSet)
  {
    tzset();
    timeZoneSet = MAGT_TRUE;
  }

  /*
   * Create the date string.
   */
  if (!strftime(dateBuf, 64, "%Y%m%d%H%M%S", &tmLocal))
  {
    strcpy(dateBuf, "00000000000000");
  }
  REPLACE(timeStr, dateBuf);

  return timeStr;
}

/*-------------------------------------------------------------------------
 *
 * MagtOpenLog:  Creates and opens the log file.
 *               Backs up any old log file.
 *
 * Returns:  None
 *
 *-----------------------------------------------------------------------*/

void MagtOpenLog(char *pszRootDir, char *pszLogPath)
{
  char logDir[MAX_PATH];
  char oldPath[MAX_PATH];
  int  fd;

  wsprintf(logDir, "%s\\%s", pszRootDir, "logs");
  if (mkdir(logDir) != 0)
  {
    if (errno != EEXIST)
      return;
  }

  wsprintf(pszLogPath, "%s\\%s", logDir, MAGT_LOGFILE);
  wsprintf(oldPath, "%s\\%s%s", logDir, MAGT_LOGFILE, ".old");

  /*
   * Rename old log file to keep a back up.
   */
  unlink(oldPath);
  rename(pszLogPath, oldPath);
  
  /*
   * Create and open new log file.
   */
  fd = open(pszLogPath,
            _O_WRONLY | _O_CREAT | _O_TRUNC,
            _S_IWRITE);
  if (fd != -1)
    close(fd);
}

/*-------------------------------------------------------------------------
 *
 * MagtReadLine:  Reads one line of text (up to n chars) from specified
 *                file.
 *
 * Returns:  Len read - No error
 *           -1 - Errors
 *
 *-----------------------------------------------------------------------*/

int MagtReadLine(char *buf, int n, FILE *fp)
{
  if (fgets(buf, n, fp) != NULL)
  {
    return(strlen(buf));
  }
  else
  {
    return(-1);
  }
}


/*-------------------------------------------------------------------------
 *
 * MagtReadStats:  Reads statistics from stats file.  The hdr and tbl data
 *                 buffers will be filled in if they are not NULL.
 *
 * Returns:  0 - No errors
 *           errno - Errors
 *
 *-----------------------------------------------------------------------*/

int MagtReadStats(MagtHdrInfo_t *hdrInfo, 
                  MagtOpsTblInfo_t *OpsTblInfo,
                  MagtEntriesTblInfo_t *EntriesTblInfo,
                  MagtIntTblInfo_t **IntTblInfo,
				  char * pszStatsPath,
				  char * pszLogPath)
{
  int hdl;
  int err;
  int i;
  struct agt_stats_t 	*pfile_stats;

    if ((err = agt_mopen_stats(pszStatsPath, O_RDONLY, &hdl)) != 0)
    {

        /* 
		   now with multiple instances this function gets called
		   on every snmprequest. Hence 
		   logging here became too expensive, now let caller interpret
		   results and figure out if it should log something or not
		*/
		
		
        return err;
    }

  	

	if ( (hdl > 1) || (hdl < 0) )
	{
		return (EINVAL); 	/* Inavlid handle */
	}

	if ((mmap_tbl [hdl].maptype != AGT_MAP_READ) && (mmap_tbl [hdl].maptype != AGT_MAP_RDWR))
	{
		return (EINVAL); 	/* Inavlid handle */
	}

	if (mmap_tbl [hdl].fp <= (caddr_t) 0)
	{
		return (EFAULT); 	/* Something got corrupted */
	}

#if (0)
	fprintf (stderr, "%s@%d> fp = %d\n",  __FILE__, __LINE__, mmap_tbl [hdl].fp);
#endif

	pfile_stats = (struct agt_stats_t *) (mmap_tbl [hdl].fp);

  /*
   * Only fill in buffers if they are not NULL.  This way, one can choose
   * to get only the hdr info or only the tbl info.
   */
  if (hdrInfo != NULL)
  {
/* versMajor and versMinor are no longer used. <03/04/05> */ 
//	hdrInfo->versMajor  = pfile_stats->hdr_stats.hdrVersionMjr;
//  hdrInfo->versMinor  = pfile_stats->hdr_stats.hdrVersionMnr;
    hdrInfo->restarted  = pfile_stats->hdr_stats.restarted;
    hdrInfo->startTime  = pfile_stats->hdr_stats.startTime;
    hdrInfo->updateTime = pfile_stats->hdr_stats.updateTime;
  }
  if (OpsTblInfo != NULL){
    OpsTblInfo->AnonymousBinds        = pfile_stats->ops_stats.dsAnonymousBinds;
    OpsTblInfo->UnAuthBinds           = pfile_stats->ops_stats.dsUnAuthBinds;
    OpsTblInfo->SimpleAuthBinds       = pfile_stats->ops_stats.dsSimpleAuthBinds;
    OpsTblInfo->StrongAuthBinds       = pfile_stats->ops_stats.dsStrongAuthBinds;
    OpsTblInfo->BindSecurityErrors    = pfile_stats->ops_stats.dsBindSecurityErrors;
    OpsTblInfo->InOps                 = pfile_stats->ops_stats.dsInOps;
    OpsTblInfo->ReadOps               = pfile_stats->ops_stats.dsReadOps;
    OpsTblInfo->CompareOps            = pfile_stats->ops_stats.dsCompareOps;
    OpsTblInfo->AddEntryOps           = pfile_stats->ops_stats.dsAddEntryOps;
    OpsTblInfo->RemoveEntryOps        = pfile_stats->ops_stats.dsRemoveEntryOps;
    OpsTblInfo->ModifyEntryOps        = pfile_stats->ops_stats.dsModifyEntryOps;
    OpsTblInfo->ModifyRDNOps          = pfile_stats->ops_stats.dsModifyRDNOps;
    OpsTblInfo->ListOps               = pfile_stats->ops_stats.dsListOps;
    OpsTblInfo->SearchOps             = pfile_stats->ops_stats.dsSearchOps;
    OpsTblInfo->OneLevelSearchOps     = pfile_stats->ops_stats.dsOneLevelSearchOps;
    OpsTblInfo->WholeSubtreeSearchOps = pfile_stats->ops_stats.dsWholeSubtreeSearchOps;
    OpsTblInfo->Referrals             = pfile_stats->ops_stats.dsReferrals;
    OpsTblInfo->Chainings             = pfile_stats->ops_stats.dsChainings;
    OpsTblInfo->SecurityErrors        = pfile_stats->ops_stats.dsSecurityErrors;
    OpsTblInfo->Errors                = pfile_stats->ops_stats.dsErrors;
   }
  if(EntriesTblInfo != NULL){
    EntriesTblInfo->MasterEntries = pfile_stats->entries_stats.dsMasterEntries;
    EntriesTblInfo->CopyEntries   = pfile_stats->entries_stats.dsCopyEntries;
    EntriesTblInfo->CacheEntries  = pfile_stats->entries_stats.dsCacheEntries;
    EntriesTblInfo->CacheHits     = pfile_stats->entries_stats.dsCacheHits;
    EntriesTblInfo->SlaveHits     = pfile_stats->entries_stats.dsSlaveHits;
  }

  if(IntTblInfo != NULL){
	  for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
	  {

	      strcpy(IntTblInfo[i]->DsName.val, pfile_stats->int_stats[i].dsName);
          IntTblInfo[i]->DsName.len               = strlen(pfile_stats->int_stats[i].dsName); 
	      
          IntTblInfo[i]->TimeOfCreation           = pfile_stats->int_stats[i].dsTimeOfCreation;
          IntTblInfo[i]->TimeOfLastAttempt        = pfile_stats->int_stats[i].dsTimeOfLastAttempt;
          IntTblInfo[i]->TimeOfLastSuccess        = pfile_stats->int_stats[i].dsTimeOfLastSuccess;
          IntTblInfo[i]->FailuresSinceLastSuccess = pfile_stats->int_stats[i].dsFailuresSinceLastSuccess;
          IntTblInfo[i]->Failures                 = pfile_stats->int_stats[i].dsFailures;
          IntTblInfo[i]->Successes                = pfile_stats->int_stats[i].dsSuccesses;
          strcpy(IntTblInfo[i]->URL.val, pfile_stats->int_stats[i].dsURL);
          IntTblInfo[i]->URL.len                  = strlen(pfile_stats->int_stats[i].dsURL); 

	  }
  }
  
  agt_mclose_stats(hdl); 
  return 0;
}

/*-------------------------------------------------------------------------
 *
 * DllMain:  Standard WIN32 DLL entry point.
 *
 * Returns:  TRUE
 *
 *-----------------------------------------------------------------------*/

BOOL WINAPI DllMain(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
  
  switch(dwReason)
  {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
      break;
    case DLL_PROCESS_DETACH:
      MagtCleanUp();
      break;
    default:
      break;
  }
    
  return TRUE;
}

/*-------------------------------------------------------------------------
 *
 * SnmpExtensionInit:  Entry point to coordinate the initializations of the
 *                     Extension Agent and the Extendible Agent.  The
 *                     Extendible Agent provides the Extension Agent with a
 *                     time zero reference; and the Extension Agent
 *                     provides the Extendible Agent with an Event handle
 *                     for communicating occurence of traps, and an object
 *                     identifier representing the root of the MIB subtree
 *                     that the Extension Agent supports.
 *
 * Returns:  TRUE - No error
 *           FALSE - Errors
 *
 *-----------------------------------------------------------------------*/

BOOL WINAPI SnmpExtensionInit(IN DWORD dwTimeZeroReference,
                              OUT HANDLE *hPollForTrapEvent,
                              OUT AsnObjectIdentifier *supportedView)
{
  int nMibIndex = 0;
  SECURITY_ATTRIBUTES sa;
  PSECURITY_ATTRIBUTES psa = NULL;
  SECURITY_DESCRIPTOR sd;
  char logMsg[1024];
  int i;
  
  instance_list_t *pInstance;

  /*
   * Record the time reference provided by the Extendible Agent.
   */

  dwTimeZero = dwTimeZeroReference;

  /*
   * Create a security descriptor that gives everyone access to the
   * trap event.  This is so that the SNMP process can set the event
   * when it detects that the server is up or down.  Without this
   * relaxed ACL, the SNMP process which usually runs as the Netscape
   * DS user can not set the trap event created by this DLL which is
   * loaded by the Extendible Agent, which usually runs as LocalSystem.
   */
  if (InitializeSecurityDescriptor(&sd,
        SECURITY_DESCRIPTOR_REVISION) == TRUE)
  {
    if (SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE) == TRUE)
    {
      sa.nLength = sizeof(SECURITY_ATTRIBUTES);
      sa.bInheritHandle = TRUE;
      sa.lpSecurityDescriptor = &sd;
      psa = &sa;
    }
  }
  
  /*
   * Create an event that will be used to communicate the occurence of
   * traps to the Extendible Agent.
   * The event will have a signaled initial state so that the status of
   * the server can be checked as soon as the subagent is loaded and
   * the necessary trap will be generated.
   */
   if ((*hPollForTrapEvent = CreateEvent(psa, 
                                         FALSE, 
                                         FALSE,
                                         MAGT_NSEV_SNMPTRAP)) == NULL)
   {
       return FALSE;
   }
  
  /*
   * Indicate the MIB view supported by this Extension Agent, an object
   * identifier representing the sub root of the MIB that is supported.
   */
  *supportedView = MIB_OidPrefix;


  /*
   * Initialize globals.
   */
 
  if ( !_FindNetscapeServers() )
  {
      return FALSE;
  }

  for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext) 
  {
      MagtInitInstance(pInstance);
  }

  for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext) 
  {
	  /* build the mib */

	  if ((pInstance->pOpsStatInfo = (MagtOpsTblInfo_t *) GlobalAlloc(GPTR, 
                                     sizeof(MagtOpsTblInfo_t))) == NULL)
	  {
		  wsprintf(logMsg, "Failed to allocate ops stats structure (error = %d)\n",
					GetLastError());
		  MagtLog(logMsg, pInstance->szLogPath);
		  return FALSE;
	  }

	  if ((pInstance->pEntriesStatInfo = (MagtEntriesTblInfo_t *) GlobalAlloc(GPTR, 
                                          sizeof(MagtEntriesTblInfo_t))) == NULL)
	  {
		  wsprintf(logMsg, "Failed to allocate entries stat structure (error = %d)\n",
                   GetLastError());
          MagtLog(logMsg, pInstance->szLogPath);
          return FALSE;
      }

	  if ((pInstance->ppIntStatInfo = (MagtIntTblInfo_t **) GlobalAlloc(GPTR, 
                                     NUM_SNMP_INT_TBL_ROWS * sizeof(MagtIntTblInfo_t *))) == NULL)
	  {
		  wsprintf(logMsg, "Failed to allocate interaction stat structure (error = %d)\n",
                   GetLastError());
          MagtLog(logMsg, pInstance->szLogPath);
          return FALSE;
	  }

	  for(i =0; i < NUM_SNMP_INT_TBL_ROWS; i++)
	  {
		  pInstance->ppIntStatInfo[i] = (MagtIntTblInfo_t *) GlobalAlloc(GPTR, 
		                                sizeof(MagtIntTblInfo_t));

	      /* make the static char for name and url so they have one address for later use */
	      pInstance->ppIntStatInfo[i]->DsName.val = (char *) GlobalAlloc(GPTR, 
											                  100 * sizeof(char));
	      pInstance->ppIntStatInfo[i]->URL.val =    (char *) GlobalAlloc(GPTR, 
															  100 *	sizeof(char));
	  }

	  /* initialize the stats we just allocated */
	  MagtInitStats(pInstance->pOpsStatInfo, 
		            pInstance->pEntriesStatInfo, 
					pInstance->ppIntStatInfo);
	  
	  
	  if( Mib_init(&(pInstance->pMibInfo), pInstance->pCfgInfo->ApplIndex) == -1)
	  {
		  wsprintf(logMsg, "Failed to create Mib structure (error = %d)\n",
                   GetLastError());
          MagtLog(logMsg, pInstance->szLogPath);
          return FALSE;
 	  }

	  for (nMibIndex = 0; nMibIndex < (int) MIB_num_vars; nMibIndex++)
	  {
	  	  MagtInitMibStorage(&(pInstance->pMibInfo[nMibIndex]),
							   pInstance->pOpsStatInfo,
							   pInstance->pEntriesStatInfo,
							   pInstance->ppIntStatInfo,
							   pInstance->pCfgInfo);
	  }
	

      /*
       * Construct the path to stats file.
       */
       wsprintfA(pInstance->szStatsPath, "%s/logs/%s", pInstance->szRootDir, 
		         AGT_STATS_FILE);

       if (MagtReadStats(NULL, pInstance->pOpsStatInfo, 
		                 pInstance->pEntriesStatInfo, 
						 pInstance->ppIntStatInfo,
						 pInstance->szStatsPath,
						 pInstance->szLogPath) != 0)
       {
	       wsprintf(logMsg,
                    "Failed to open Memory Mapped Stats File. Make sure ns-slapd is running\n",
                    GetLastError());
           MagtLog(logMsg, pInstance->szLogPath);
       }
 
	   	
  }
    
  /* now that all mib's set up set next pointer from last entry to first 
	 entry of next instance */

  for (nMibIndex = 0; nMibIndex < (int) MIB_num_vars; nMibIndex++)
  {

      for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext) 
	  {
	      if(pInstance->pNext != NULL)
	      {
              pInstance->pMibInfo[nMibIndex].MibNext = &(pInstance->pNext->pMibInfo[nMibIndex]);
	      }else{
		      if (nMibIndex + 1 != (int) MIB_num_vars) 
		      { 
			      pInstance->pMibInfo[nMibIndex].MibNext = &(pInstanceList->pMibInfo[nMibIndex + 1]); 
			  } 
		  }
      }  

    
  }

  /*
   * Set event to have SnmpExtensionTrap invoked for initial check of
   * Server status.
   */
  if (SetEvent(*hPollForTrapEvent) == FALSE)
  {
	/* don't have a specific instance to log it to, find something better to do later */
  }
  
  return TRUE;
}

/*-------------------------------------------------------------------------
 *
 * SnmpExtensionTrap:  Entry point to communicate traps to the Extendible
 *                     Agent.  The Extendible Agent will query this entry
 *                     point when the trap event (supplied at initialization
 *                     time) is asserted, which indicates that zero or more
 *                     traps may have occured.  The Extendible Agent will
 *                     repeatedly call this entry point until FALSE is
 *                     returned, indicating that all outstanding traps have
 *                     been processed.
 *
 * Returns:  TRUE - Valid trap data
 *           FALSE - No trap data 
 *
 *-----------------------------------------------------------------------*/

BOOL WINAPI SnmpExtensionTrap(OUT AsnObjectIdentifier *enterprise,
                              OUT AsnInteger *genericTrap,
                              OUT AsnInteger *specificTrap,
                              OUT AsnTimeticks *timeStamp,
                              OUT RFC1157VarBindList *variableBindings)
{
  static UINT oidList[] = {1, 3, 6, 1, 4, 1, 1450};
  static UINT oidListLen = MAGT_OID_SIZEOF(oidList);
  static RFC1157VarBind *trapVars = NULL;
  static MagtTrapTask_t trapTask = MAGT_TRAP_GENERATION;
  int nVarLen;
  char logMsg[1024];
  instance_list_t *pInstance;
  
  
  if (trapTask == MAGT_TRAP_CLEANUP)
  {
      if (variableBindings->list != NULL)
          SNMP_FreeVarBind(variableBindings->list);
 
	  trapTask = MAGT_TRAP_GENERATION;
  }


  if (trapTask == MAGT_TRAP_GENERATION)
  {

      for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext)
	  {
          MagtCheckServer(pInstance);

          /*
           * If there is no trap to be generated for this instance keep looking at other
		   *  instances.
           */
           if (pInstance->trapType == MAGT_TRAP_NONE)
		   {
              continue;
		   }

           enterprise->ids = (UINT *) SNMP_malloc(sizeof(UINT) * oidListLen);
           if (enterprise->ids == NULL)
           {
               wsprintf(logMsg,
                        "Failed to allocate enterprise id\n");
               MagtLog(logMsg, pInstance->szLogPath);
               return FALSE;
           }
           enterprise->idLength = oidListLen;
           memcpy(enterprise->ids, oidList, sizeof(UINT) * oidListLen);

           *genericTrap = SNMP_GENERICTRAP_ENTERSPECIFIC; 
           *specificTrap = pInstance->trapType;
           *timeStamp = GetCurrentTime() - dwTimeZero;
    
           /*
            * Set up the variable binding list with variables specified in the MIB
            * for each trap.
            */
           if ((trapVars = SNMP_malloc(sizeof(RFC1157VarBind) * 4)) == NULL)
           {
               wsprintf(logMsg,
                        "Failed to allocate trap variables\n");
               MagtLog(logMsg, pInstance->szLogPath);
               SNMP_oidfree(enterprise);
               return FALSE;
		   }

           if ((nVarLen = MagtFillTrapVars(pInstance->trapType, trapVars, pInstance->pCfgInfo)) == 0)
           {
               wsprintf(logMsg,
                        "Failed to fill trap variables\n");
               MagtLog(logMsg, pInstance->szLogPath);
               SNMP_free(trapVars);
               SNMP_oidfree(enterprise);
               return FALSE;
           }

           variableBindings->list = trapVars;
           variableBindings->len = nVarLen;

           trapTask = MAGT_TRAP_CLEANUP;

           wsprintf(logMsg,
                    "Sending trap %d\n",
                    pInstance->trapType);
           MagtLog(logMsg, pInstance->szLogPath);

		   /* reset the trap type for this instance */
		   pInstance->trapType = MAGT_TRAP_NONE;

           /*
            * Indicate that a trap should be sent and parameters contain valid
            * data.
            */
            return TRUE;
        }
        

    }

    /*
     * Indicate that no more traps are available and parameters do not
     * refer to any valid data.
     */

     return FALSE;
}

/*-------------------------------------------------------------------------
 *
 * SnmpExtensionQuery:  Entry point to resolve queries for MIB variables in
 *                      their supported MIB view (supplied at
 *                      initialization time).  The supported requestType is
 *                      Get/GetNext.
 *
 * Returns:  TRUE - No error
 *           FALSE - Errors
 *
 *-----------------------------------------------------------------------*/

BOOL WINAPI SnmpExtensionQuery(IN BYTE requestType,
                               IN OUT RFC1157VarBindList *variableBindings,
                               OUT AsnInteger *errorStatus,
                               OUT AsnInteger *errorIndex)
{
  static time_t lastChkTime = 0;
  UINT i;
  HANDLE hTrapEvent;
  time_t currTime;
   
  /*
   * Check for valid input.
   */
  
  if (variableBindings == NULL ||
      errorStatus == NULL ||
      errorIndex == NULL)
  {
      return FALSE;
  }

  /*
   * Iterate through the variable bindings list to resolve individual
   * variable bindings.
   */

  for (i = 0; i < variableBindings->len; i++)
  {
    *errorStatus = MagtResolveVarBind(&variableBindings->list[i], 
                                      requestType);

    /*
     * Test and handle case where GetNext past end of MIB view supported by
     * this Extension Agent occurs.  Special processing is required to 
     * communicate this situation to the Extendible Agent so it can take
     * appropriate action.
     */
    if (*errorStatus == SNMP_ERRORSTATUS_NOSUCHNAME &&
        requestType == MAGT_MIB_ACTION_GETNEXT)
    {
      *errorStatus = SNMP_ERRORSTATUS_NOERROR;

      /*
       * Modify variable binding of such variables so the OID points just
       * outside the MIB view supported by this Extension Agent.  The
       * Extendible Agent tests for this, and takes appropriate action.
       */
      SNMP_oidfree(&variableBindings->list[i].name);
      SNMP_oidcpy(&variableBindings->list[i].name, &MIB_OidPrefix);
      variableBindings->list[i].name.ids[MAGT_MIB_PREFIX_LEN - 1]++;
    }

    /*
     * If an error was indicated, communicate error status and error index
     * to the Extendible Agent.  The Extendible Agent will ensure that the
     * original variable bindings are returned in the response packet.
     */
    if (*errorStatus != SNMP_ERRORSTATUS_NOERROR)
    {
      *errorIndex = i + 1;
      return FALSE;
    }
  }

  /* 
   * Before going back, set the trap event so server status can be checked
   * to see if a trap needs to be generated.  This is to cover the case the
   * SNMP process is unable to set the trap event because it is stuck.
   */
  currTime = time(0);

  /*
   * If just check status, do not generate event again.
   */
  if ((currTime - lastChkTime) >= MAGT_TIME_QUANTUM * 3)
  {
    if ((hTrapEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE,
                                (LPCTSTR)MAGT_NSEV_SNMPTRAP)) != NULL)
      SetEvent(hTrapEvent);

    lastChkTime = currTime;
  }

  return TRUE;
}


/* --- Open Function --------------------------------------------------------------------- */    


/* _FindNetscapeServers()
 * Function to loop through registry looking for netscape servers 
 * Stores them into pInstanceList as it finds them.
 */

#define MAX_KEY_SIZE 128
DWORD
_FindNetscapeServers()
{
	LONG	regStatus,
			status;
	HKEY	hKeyNetscape = NULL,
			hKeyNetscapeConf;
	DWORD	dwKey, 
			type,
			dwServerKeySize, 
			size,
			dwServerCount = 0;
	WCHAR	szServerKeyName[MAX_KEY_SIZE],
			szPath[MAX_KEY_SIZE];
	FILETIME fileTime;
	instance_list_t *pNew;
	instance_list_t *pCurrent;
	DWORD	iUniqueID = 0;
	char logMsg[1024];
    regStatus = RegOpenKeyEx( 
		HKEY_LOCAL_MACHINE,
        TEXT(KEY_SOFTWARE_NETSCAPE) TEXT("\\") TEXT(DS_KEY_ROOT),
		0L,
		KEY_ALL_ACCESS,
		&hKeyNetscape);

	if (regStatus != ERROR_SUCCESS) {
		goto ExitPoint;
	}

	dwKey = 0;
	do {
		dwServerKeySize = MAX_KEY_SIZE;
		regStatus = RegEnumKeyEx(
			hKeyNetscape,
			dwKey,
			(char *) szServerKeyName,
			&dwServerKeySize,
			NULL,
			0,
			0,
			&fileTime);
		dwKey++;

		if (regStatus == ERROR_SUCCESS) {

			regStatus = RegOpenKeyEx( 
				hKeyNetscape,
				(char *) szServerKeyName,
				0L,
				KEY_ALL_ACCESS,
				&hKeyNetscapeConf);

			if (regStatus != ERROR_SUCCESS) {
				continue;
			}

			/* Now look for "ConfigurationPath" to find 3.0 netscape servers */
			size = MAX_KEY_SIZE;
       		status = RegQueryValueEx(
						hKeyNetscapeConf, 
						TEXT(VALUE_CONFIG_PATH),
						0L,
						&type,
						(LPBYTE)szPath,
						&size);
			if ( status == ERROR_SUCCESS ) {
				/* this is a netscape server */
				if ( (pNew = (instance_list_t *)malloc(sizeof(instance_list_t))) == NULL) {
					status = (unsigned long)-1;
					RegCloseKey(hKeyNetscapeConf);
					goto ExitPoint;
				}
				if ( (pNew->pInstanceName = (PWCH)malloc(sizeof(WCHAR) *(dwServerKeySize+1))) == NULL) {
					status = (unsigned long)-1;
					RegCloseKey(hKeyNetscapeConf);
					goto ExitPoint;
				}
				if ( (pNew->pConfPath = (PWCH)malloc(sizeof(WCHAR) *(size+1))) == NULL) {
					status = (unsigned long)-1;
					RegCloseKey(hKeyNetscapeConf);
					goto ExitPoint;
				}
				
								
				pNew->Handle = 0;
				pNew->pData = NULL;

				pNew->instance.ParentObjectTitleIndex = 0;
				pNew->instance.ParentObjectInstance = 0;
				pNew->instance.UniqueID	= -1;
				pNew->instance.NameOffset = sizeof(PERF_INSTANCE_DEFINITION);
				lstrcpy((char *) pNew->pInstanceName, (char *) szServerKeyName);

				lstrcpy((char *) pNew->pConfPath, (char *) szPath);
	
				pNew->instance.NameLength = (dwServerKeySize+1) * sizeof(WCHAR);
				pNew->instance.ByteLength = sizeof(PERF_INSTANCE_DEFINITION) + 
						(((pNew->instance.NameLength + sizeof(DWORD)-1)/sizeof(DWORD))*sizeof(DWORD));
				pNew->instance.UniqueID = iUniqueID++;
						
				wsprintf(pNew->szLogPath, "\\%s", MAGT_LOGFILE);
                if( ((char *) pNew->szRootDir = getRootDirFromConfFile(pNew->pConfPath) ) != NULL)
	            {
                    /* can only check if getRootDir */

					/* open the log */
					
 				    MagtOpenLog(pNew->szRootDir, pNew->szLogPath);

	                if ((pNew->pCfgInfo = (MagtStaticInfo_t *) GlobalAlloc(GPTR, 
                                                      sizeof(MagtStaticInfo_t))) == NULL)
	                {
						/* something fatal happened but try to free this
						   node that won't be used anyway
						 */
						if(pNew != NULL)
						{
							free(pNew);
						}
                        status = (unsigned long)-1;
					    goto ExitPoint;
                    }
	  
                    MagtLoadStaticInfo(pNew->pCfgInfo, pNew->szRootDir, &pNew->SNMPOff, pNew->szLogPath);
					
                
                    if ( pNew->SNMPOff )
                    {
                        wsprintf(logMsg,
                                 "SNMP subagent is not configured to be on\n");
                        MagtLog(logMsg, pNew->szLogPath);

						/* since not adding this to list free it */
						if(pNew != NULL)
						{
							free(pNew);
						}
                    }else{
					    /* new instance that is on to add to list */
		            	               			
				        /* if first element null or less than first element add to beginning */
				        if(   (pInstanceList == NULL) 
					       || (pNew->pCfgInfo->ApplIndex < pInstanceList->pCfgInfo->ApplIndex) )
				        {
					        pNew->pNext = pInstanceList;
					        pInstanceList = pNew;
				        }else{
					        /* must be after first element */
					        for(pCurrent= pInstanceList; pCurrent; pCurrent=pCurrent->pNext)
					        {
								if(pNew->pCfgInfo->ApplIndex == pCurrent->pCfgInfo->ApplIndex)
								{
								    /* ApplIndex must be unique, another instance on this host
								       is already configured to be on using this applIndex,
								       so I can't monitor this one. Log the error and
								       don't add this instance to the list */

								    wsprintf(logMsg,
									    "Another server instance with this ApplIndex: %d is already being" 
									    " monitored. ApplIndex must be unique. Turn off"
									    " SNMP monitoring of the other server instance or change"
									    " the ApplIndex of one of the server instances.\n", 
										pNew->pCfgInfo->ApplIndex);
                                    MagtLog(logMsg, pNew->szLogPath);

						            /* since not adding this to list free it */
						            if(pNew != NULL)
						            {
							            free(pNew);
						            }
							    }else if(   (pCurrent->pNext == NULL)
						           || (   (pNew->pCfgInfo->ApplIndex > pCurrent->pCfgInfo->ApplIndex)
						                && (pNew->pCfgInfo->ApplIndex < pCurrent->pNext->pCfgInfo->ApplIndex)) )
						        {
									/* if next is null or if greater this element and less then next one
						               add it inbetween */
							        pNew->pNext=pCurrent->pNext;
							        pCurrent->pNext=pNew;
									break;
						        }
					        }
				        }
					}
				}
				dwServerCount++;
			}

			RegCloseKey(hKeyNetscapeConf);
		}

	} while ( regStatus != ERROR_NO_MORE_ITEMS );

ExitPoint:
	if (hKeyNetscape)
		RegCloseKey (hKeyNetscape); 

	return dwServerCount;
}

