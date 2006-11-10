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

/*
    nsctr.c

	Netscape server performance monitor hooks.


	***********************************************************************
		HOW TO ADD A NEW PERFMON STATISTIC
		1. add to StatSlot or StatHeader struct 
		2. add new counter definition to NS_DATA_DEFINITION in nsctrs.h
		3. define the offset of your new counter in nsctrdef.h
		4. add your counter initialization to NSDataDefinition in nsctr.cpp
		5. update CollectNSPerformanceData to collect your data
		6. modify nsctrs.ini to contain the text info for your counter
			these are keyed off the "tag" you used in step 3
	***********************************************************************
		HOW TO UPDATE THE REGISTRY
		1. run regini nsreg.ini
		2. run lodctr nsctrs.ini
	***********************************************************************
 */

#define UNICODE

#include <windows.h>
#include <string.h>
#include <winperf.h>
#include <stdio.h>
#include <regstr.h>
#include "nsldapctrs.h"
#include "nsldapctrmsg.h"
#include "nsldapctrutil.h"
#include "nsldapctrmc.h"
#include "nsldapctrdef.h"

#include "nt/regparms.h"

#include "../agtmmap.h"

#define NUM_INSTANCES 0
#define MAGT_MAX_LINELEN 255


/* --- Constant Performance Counter Declaration --------------------------------------------*/

NS_DATA_DEFINITION NSDataDefinition = {

    {	sizeof(NS_DATA_DEFINITION) + SIZE_OF_NS_PERFORMANCE_DATA,
		sizeof(NS_DATA_DEFINITION),
    	sizeof(PERF_OBJECT_TYPE),
    	NS_OBJ,
    	0,
		NS_OBJ,
    	0,
		PERF_DETAIL_NOVICE,
		(sizeof(NS_DATA_DEFINITION)-sizeof(PERF_OBJECT_TYPE))/
	        sizeof(PERF_COUNTER_DEFINITION),
		4L,
		NUM_INSTANCES,
		0, 
		0, 
		0
	},
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		CONN_RATE,
	    0,
		CONN_RATE,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		NUM_CONN_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		THROUGHPUT,
	    0,
	  	THROUGHPUT,
	  	0,
	    -3,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		NUM_THROUGHPUT_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		TOTAL_BYTES_WRITTEN,
	    0,
		TOTAL_BYTES_WRITTEN,
	    0,
	    -3,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_RAWCOUNT,
	    sizeof(DWORD),
		NUM_TOTAL_BYTES_WRITTEN_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		TOTAL_BYTES_READ,
	    0,
		TOTAL_BYTES_READ,
	    0,
	    -3,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_RAWCOUNT,
	    sizeof(DWORD),
		NUM_TOTAL_BYTES_READ_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		OP_RATE,
	    0,
		OP_RATE,
	    0,
	    -1,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		NUM_OP_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		TOTAL_ERRORS,
	    0,
		TOTAL_ERRORS,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_RAWCOUNT,
	    sizeof(DWORD),
		NUM_TOTAL_ERRORS_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		SEARCH_RATE,
	    0,
		SEARCH_RATE,
	    0,
	    -1,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		NUM_SEARCH_RATE_OFFSET
    } ,
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		ADD_RATE,
	    0,
		ADD_RATE,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		ADD_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		DELETE_RATE,
	    0,
		DELETE_RATE,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		DELETE_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		MODIFY_RATE,
	    0,
		MODIFY_RATE,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		MODIFY_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		COMPARE_RATE,
	    0,
		COMPARE_RATE,
	    0,
	    -1,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		COMPARE_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		MODDN_RATE,
	    0,
		MODDN_RATE,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		MODDN_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		CONNECTIONS,
	    0,
		CONNECTIONS,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_RAWCOUNT,
	    sizeof(DWORD),
		CONNECTIONS_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		BIND_RATE,
	    0,
		BIND_RATE,
	    0,
	    -1,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		BIND_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		ENTRIES_RETURNED,
	    0,
		ENTRIES_RETURNED,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_RAWCOUNT,
	    sizeof(DWORD),
		ENTRIES_RETURNED_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		ENTRIES_RETURNED_RATE,
	    0,
		ENTRIES_RETURNED_RATE,
	    0,
	    -1,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		ENTRIES_RETURNED_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		REFERRALS_RETURNED,
	    0,
		REFERRALS_RETURNED,
	    0,
	    0,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_RAWCOUNT,
	    sizeof(DWORD),
		REFERRALS_RETURNED_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		REFERRALS_RETURNED_RATE,
	    0,
		REFERRALS_RETURNED_RATE,
	    0,
	    -1,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		REFERRALS_RETURNED_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		BYTES_READ_RATE,
	    0,
		BYTES_READ_RATE,
	    0,
	    -3,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		BYTES_READ_RATE_OFFSET
    },
    {   
    	sizeof(PERF_COUNTER_DEFINITION),
		BYTES_WRITTEN_RATE,
	    0,
		BYTES_WRITTEN_RATE,
	    0,
	    -3,
		PERF_DETAIL_NOVICE,
		PERF_COUNTER_COUNTER,
	    sizeof(DWORD),
		BYTES_WRITTEN_RATE_OFFSET
    }

	
};

/* --- Data structs ----------------------------------------------------------------------- */
typedef struct instance_list_t {
	PERF_INSTANCE_DEFINITION	instance;
	PWSTR						pInstanceName;
	PWSTR						pConfPath;
	agt_stats_t *				pData;
	struct instance_list_t *	pNext;
} instance_list_t;


/* --- Globals ---------------------------------------------------------------------------- */    
static BOOL bInitialized = FALSE;
static DWORD dwOpenCount = 0;			/* Count of threads holding DLL open */
static DWORD dwInstanceCount = 0;
static instance_list_t *pInstanceList = NULL;

#define export extern "C"


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



/* --- strips quotes off of a quoted string -------------------------------------- */    


char *dequote(char *quoted_string)
{	
	char *return_string = (char *)malloc((strlen(quoted_string) - 2) * sizeof(char) );
    char *pQuo = quoted_string;
    char *pRet = return_string;

	for(; *pQuo; pQuo++) {
        if (*pQuo != '\"')
            *(pRet++) = *pQuo;
	}
	*pRet = '\0';
	
	return return_string;
	
}
 
/* --- gets the instance dir from conf file ------------------------------------- */    


/*
 * The body of this function is pretty much copied from
 *   ldapserver/ldap/servers/snmp/ntagt/nsldapagt_nt.c
 *
 */
char *getRootDirFromConfFile(PWSTR confpath)
{
  char *rootDir      = NULL;
  const char *config = "\\config\0" ;
  char instanceDir[MAGT_MAX_LINELEN + 1] = "";
  size_t len ;
  char filename[256];

  if (confpath) {
    sprintf(filename, "%S", confpath);
    len = strlen(filename) - strlen(config) ;
    strncpy(instanceDir, filename, len);
    rootDir = _strdup(instanceDir) ; // allocate memory for rootDir and set up to value pointed by instanceDir
    return rootDir ;
  }
  else return NULL ;
}

static DWORD MapSharedMem(char* path, agt_stats_t **ptr)
{
	HANDLE	hFile = NULL;
	HANDLE	hMapFile = NULL;
	LPVOID	memory = NULL;
	
	*ptr = NULL;
	/* Open existing disk file for read */
	hFile = CreateFileA(path, 
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, 
				NULL, 
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, 
				NULL);

	if ( hFile == INVALID_HANDLE_VALUE || hFile == NULL ) return GetLastError();

	/* Create mapped file handle for reading */
	hMapFile = CreateFileMapping( hFile, NULL, PAGE_READONLY, 0,
				sizeof(struct agt_stats_t),
				NULL);
	if ( hMapFile == NULL ) {
		CloseHandle( hFile );
		return GetLastError();
	}

		/* Create addr ptr to the start of the file */
	memory = MapViewOfFileEx( hMapFile, FILE_MAP_READ, 0, 0,
			sizeof(struct agt_stats_t), NULL );
	CloseHandle( hMapFile );
	CloseHandle( hFile );
	if ( memory == NULL ) {
		return GetLastError();
	}
	*ptr = (agt_stats_t *)memory;
	return 0;
}

static DWORD UnmapSharedMem(agt_stats_t **ptr)
{
	return UnmapViewOfFile( (LPVOID)*ptr) ? 0 : -1;
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
			szConfKeyName[MAX_KEY_SIZE + sizeof(KEY_SOFTWARE_NETSCAPE)],
			szPath[MAX_KEY_SIZE];
	FILETIME fileTime;
	instance_list_t *pNew;
	DWORD	iUniqueID = 0;

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
			szServerKeyName,
			&dwServerKeySize,
			NULL,
			0,
			0,
			&fileTime);
		dwKey++;

		if (regStatus == ERROR_SUCCESS) {

			regStatus = RegOpenKeyEx( 
				hKeyNetscape,
				szServerKeyName,
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


				pNew->pData = NULL;

				pNew->instance.ParentObjectTitleIndex = 0;
				pNew->instance.ParentObjectInstance = 0;
				pNew->instance.UniqueID	= -1;
				pNew->instance.NameOffset = sizeof(PERF_INSTANCE_DEFINITION);
				lstrcpy(pNew->pInstanceName, szServerKeyName);
				lstrcpy(pNew->pConfPath, szPath);

				pNew->instance.NameLength = (dwServerKeySize+1) * sizeof(WCHAR);
				pNew->instance.ByteLength = sizeof(PERF_INSTANCE_DEFINITION) + 
						(((pNew->instance.NameLength + sizeof(DWORD)-1)/sizeof(DWORD))*sizeof(DWORD));
				pNew->instance.UniqueID = iUniqueID++;
								
				pNew->pNext = pInstanceList;
				pInstanceList = pNew;

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

/* _OpenNetscapeServers()
 * Once the pInstanceList has been created, this routine will open the instances 
 * of the netscape servers; 
 */
#define MAX_FILE_LEN 128
DWORD
_OpenNetscapeServers()
{
	LONG status;
	DWORD dwServerCount = 0;
	instance_list_t *pInstance;
	char *szRootDir;
	char tmpstatsfile[MAX_FILE_LEN];
	int err;

	for (pInstance = pInstanceList; pInstance; pInstance = pInstance->pNext) {

		/* open the memory map */
		
		/*
		 * Get directory for our stats file
		 */

		szRootDir = getRootDirFromConfFile(pInstance->pConfPath);
		if( szRootDir == NULL){
			status = GetLastError();
			continue ;
		}
		wsprintfA(tmpstatsfile, "%s/logs/%s", szRootDir, AGT_STATS_FILE);
		err = MapSharedMem(tmpstatsfile,&pInstance->pData);
		if ( 0 != err ) {
			REPORT_ERROR (NSPERF_UNABLE_MAP_VIEW_OF_FILE, LOG_USER);
			status = GetLastError(); // return error
			continue;
		} else {
			dwServerCount++;
		}

		if(szRootDir != NULL){
			free(szRootDir);
		}

	}

	return dwServerCount;
}

export DWORD APIENTRY
OpenNSPerformanceData(LPWSTR lpDeviceNames)
{
    LONG	status;
    TCHAR	szMappedObject[] = TEXT(SVR_ID_SERVICE) TEXT("Statistics");
    HKEY	hKeyDriverPerf;
    DWORD	size;
    DWORD	type;
    DWORD	dwFirstCounter;
    DWORD	dwFirstHelp;

    if (!dwOpenCount) {

		hEventLog = MonOpenEventLog();


		if ( !_FindNetscapeServers() ) {
			/* No netscape servers found */
			status = (unsigned long)-1;
			goto OpenExitPoint;
		}

		if ( !(dwInstanceCount = _OpenNetscapeServers()) ) {
			/* No netscape servers are active */
			status = (unsigned long)-1;
			goto OpenExitPoint;
		}

		/* Now load help keys from registry */

        status = RegOpenKeyEx (
            HKEY_LOCAL_MACHINE,
			TEXT("System\\CurrentControlSet\\Services") TEXT("\\") TEXT(SVR_ID_SERVICE) TEXT(SVR_VERSION) TEXT("\\") TEXT(KEY_PERFORMANCE),
            0L,
			KEY_ALL_ACCESS,
            &hKeyDriverPerf);

        if (status != ERROR_SUCCESS) {
            REPORT_ERROR_DATA (NSPERF_UNABLE_OPEN_DRIVER_KEY, LOG_USER,
                &status, sizeof(status));
            goto OpenExitPoint;
        }

        size = sizeof (DWORD);
        status = RegQueryValueEx(
                    hKeyDriverPerf, 
		            TEXT("First Counter"),
                    0L,
                    &type,
                    (LPBYTE)&dwFirstCounter,
                    &size);

        if (status != ERROR_SUCCESS) {
            REPORT_ERROR_DATA (NSPERF_UNABLE_READ_FIRST_COUNTER, LOG_USER,
                &status, sizeof(status));
            goto OpenExitPoint;
        }

        size = sizeof (DWORD);
        status = RegQueryValueEx(
                    hKeyDriverPerf, 
        		    TEXT("First Help"),
                    0L,
                    &type,
                    (LPBYTE)&dwFirstHelp,
		    &size);

        if (status != ERROR_SUCCESS) {
            REPORT_ERROR_DATA (NSPERF_UNABLE_READ_FIRST_HELP, LOG_USER,
                &status, sizeof(status));
            goto OpenExitPoint;
        }
 
        NSDataDefinition.NS_ObjectType.ObjectNameTitleIndex += dwFirstCounter;
        NSDataDefinition.NS_ObjectType.ObjectHelpTitleIndex += dwFirstHelp;

        NSDataDefinition.connection_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.connection_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.throughput.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.throughput.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.total_bytes_written.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.total_bytes_written.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.total_bytes_read.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.total_bytes_read.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.operation_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.operation_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.total_errors.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.total_errors.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.search_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.search_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.add_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.add_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.delete_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.delete_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.modify_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.modify_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.compare_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.compare_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.moddn_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.moddn_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.connections.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.connections.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.bind_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.bind_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.entries_returned.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.entries_returned.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.entries_returned_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.entries_returned_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.referrals_returned.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.referrals_returned.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.referrals_returned_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.referrals_returned_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.bytes_read_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.bytes_read_rate.CounterHelpTitleIndex += dwFirstHelp;
        NSDataDefinition.bytes_written_rate.CounterNameTitleIndex += dwFirstCounter;
        NSDataDefinition.bytes_written_rate.CounterHelpTitleIndex += dwFirstHelp;

        RegCloseKey (hKeyDriverPerf); 

        bInitialized = TRUE;
    }

    dwOpenCount++; 

    status = ERROR_SUCCESS;

OpenExitPoint:

    return status;
}

/* --- Close Function -------------------------------------------------------------------- */    
export DWORD APIENTRY
CloseNSPerformanceData()
{	
	instance_list_t *pInstance, *pDead;

    if (!(--dwOpenCount)) { 

		for (pDead = NULL, pInstance = pInstanceList; pInstance; pInstance=pInstance->pNext) {
			if (pDead)
				free(pDead);

   /* I probably need to free stats too... make sure to add that later */
			if (pInstance->pData)
				UnmapSharedMem(&pInstance->pData);

			free(pInstance->pInstanceName);
			free(pInstance->pConfPath);
			pDead = pInstance;
		}
		if (pDead)			/* cleanup last instance */
			free(pDead);
		
		MonCloseEventLog();

		bInitialized = FALSE;
    }

    return ERROR_SUCCESS;
}

struct _status_struct_s {
	DWORD	connection_rate;
	DWORD	throughput;
	DWORD	tot_bytes_written;
	DWORD	tot_bytes_read;
	DWORD	op_rate;
	DWORD	tot_errs;
	DWORD	search_rate;
	DWORD	add_rate;
	DWORD	delete_rate;
	DWORD	modify_rate;
	DWORD	compare_rate;
	DWORD	moddn_rate;
	DWORD	connections;
	DWORD	bind_rate;
	DWORD	entries_returned;
	DWORD	entries_returned_rate;
	DWORD	referrals_returned;
	DWORD	referrals_returned_rate;
	DWORD	bytes_read_rate;
	DWORD	bytes_written_rate;
};

void 
Get_Actual_Data(agt_stats_t *smem, 
		struct _status_struct_s *results)
{
	/* Copy over the counters from the shared memory region */
	struct ops_stats_t *pOpsStats = &(smem->ops_stats);

	results->search_rate = pOpsStats->dsSearchOps;
	results->modify_rate = pOpsStats->dsModifyEntryOps;
	results->add_rate = pOpsStats->dsAddEntryOps ;
	results->compare_rate = pOpsStats->dsCompareOps ;
	results->moddn_rate = pOpsStats->dsModifyRDNOps ;
	results->delete_rate = pOpsStats->dsRemoveEntryOps ;
	results->bind_rate = pOpsStats->dsAnonymousBinds + pOpsStats->dsStrongAuthBinds + pOpsStats->dsSimpleAuthBinds ;
	results->op_rate = results->search_rate + results->add_rate + results->delete_rate + 
		results->modify_rate + results->compare_rate + results->moddn_rate + results->bind_rate;
	results->connections = 0;
	results->tot_errs = pOpsStats->dsErrors ;
	results->connections = pOpsStats->dsConnections ;
	results->tot_bytes_written = pOpsStats->dsBytesSent ;
	results->tot_bytes_read = pOpsStats->dsBytesRecv ;
	results->throughput = pOpsStats->dsBytesSent +  pOpsStats->dsBytesRecv;
	results->connection_rate = pOpsStats->dsConnectionSeq ;
	results->entries_returned = pOpsStats->dsEntriesReturned ;	  
	results->entries_returned_rate = pOpsStats->dsEntriesReturned ;	  
	results->referrals_returned = pOpsStats->dsReferralsReturned ;	  
	results->referrals_returned_rate = pOpsStats->dsReferralsReturned ;	  
	results->bytes_read_rate = pOpsStats->dsBytesRecv ;	  
	results->bytes_written_rate = pOpsStats->dsBytesSent ;	  
	/* Still to do : connections, throughput, db hit ratio, entry cache hit ratio */
}

/* --- Collect Function ------------------------------------------------------------------- */    
export DWORD  APIENTRY
CollectNSPerformanceData(
    IN      LPWSTR  lpValueName,
    IN OUT  LPVOID  *lppData,
    IN OUT  LPDWORD lpcbTotalBytes,
    IN OUT  LPDWORD lpNumObjectTypes
)
{
    ULONG SpaceNeeded;
    PDWORD pdwCounter;
    PERF_COUNTER_BLOCK *pPerfCounterBlock;
    NS_DATA_DEFINITION *pNSDataDefinition;
    DWORD dwQueryType;
	instance_list_t *pInstance;

    if (!bInitialized) {
	    *lpcbTotalBytes = (DWORD) 0;
	    *lpNumObjectTypes = (DWORD) 0;
        return ERROR_SUCCESS; 
    }
    
    dwQueryType = GetQueryType (lpValueName);
    
    if (dwQueryType == QUERY_FOREIGN) {
        // this routine does not service requests for data from
        // Non-NT computers
	    *lpcbTotalBytes = (DWORD) 0;
	    *lpNumObjectTypes = (DWORD) 0;
        return ERROR_SUCCESS;
    }

    if (dwQueryType == QUERY_ITEMS){
	if ( !(IsNumberInUnicodeList (NSDataDefinition.NS_ObjectType.ObjectNameTitleIndex, lpValueName))) {
            // request received for data object not provided by this routine
            *lpcbTotalBytes = (DWORD) 0;
    	    *lpNumObjectTypes = (DWORD) 0;
            return ERROR_SUCCESS;
        }
    }
    /* -------- OK DO THE REAL WORK HERE ---------- */


	/* -------------------------------------------- */
    /* | PERF_DATA_BLOCK (header)                 | */
	/* -------------------------------------------- */
    /* | PERF_OBJECT_TYPE 1                       | */
	/* -------------------------------------------- */
    /* | PERF_OBJECT_TYPE 2                       | */
	/* -------------------------------------------- */
    /* |        .                                 | */
    /* |        .                                 | */
    /* |        .                                 | */
    /* |                                          | */
    /* |                                          | */
	/* -------------------------------------------- */


	/* -------------------------------------------- */
    /* | PERF_OBJECT_TYPE (header)                | */
	/* -------------------------------------------- */
    /* | PERF_COUNTER_DEFINITION 1                | */
	/* -------------------------------------------- */
    /* | PERF_COUNTER_DEFINITION 2                | */
	/* -------------------------------------------- */
    /* |        .                                 | */
    /* |        .                                 | */
    /* |        .                                 | */
    /* |                                          | */
	/* -------------------------------------------- */
    /* | PERF_INSTANCE_DEFINITION 1               | */
	/* -------------------------------------------- */
    /* | PERF_INSTANCE_DEFINITION 2               | */
	/* -------------------------------------------- */
    /* |        .                                 | */
    /* |        .                                 | */
    /* |        .                                 | */
    /* |                                          | */
    /* |                                          | */
	/* -------------------------------------------- */


	/* -------------------------------------------- */
    /* | PERF_INSTANCE_DEFINITION (header)        | */
	/* -------------------------------------------- */
    /* | Instance Name (variable)                 | */
	/* -------------------------------------------- */
    /* | PERF_COUNTER_BLOCK (header)              | */
	/* -------------------------------------------- */
    /* | Counter Data (variable)                  | */
	/* -------------------------------------------- */



	/* Check to see if there is enough space in caller's buffer */

    pNSDataDefinition = (NS_DATA_DEFINITION *) *lppData;

    SpaceNeeded = sizeof(NS_DATA_DEFINITION) + (dwInstanceCount * 
    	(SIZE_OF_NS_PERFORMANCE_DATA + MAX_KEY_SIZE + sizeof(PERF_COUNTER_BLOCK) +
    	sizeof(PERF_INSTANCE_DEFINITION)));

    if ( *lpcbTotalBytes < SpaceNeeded ) {
	    *lpcbTotalBytes = (DWORD) 0;
	    *lpNumObjectTypes = (DWORD) 0;
        return ERROR_MORE_DATA;
    }

	/* Set the PERF_OBJECT_TYPE definition and PERF_COUNTER_DEFINITIONs */
	NSDataDefinition.NS_ObjectType.NumInstances = dwInstanceCount;
    memmove(pNSDataDefinition, &NSDataDefinition, sizeof(NS_DATA_DEFINITION));

	pdwCounter = (PDWORD) &(pNSDataDefinition[1]);

	for ( pInstance = pInstanceList; pInstance; pInstance=pInstance->pNext) {

		if ( pInstance->pData ) {

			/* Set the PERF_INSTANCE_DEFINITION */
			memmove(pdwCounter, &(pInstance->instance), sizeof(PERF_INSTANCE_DEFINITION));
			pdwCounter += ((sizeof(PERF_INSTANCE_DEFINITION))/sizeof(DWORD));

			/* Set the Instance Name */
			memmove(pdwCounter, pInstance->pInstanceName, pInstance->instance.NameLength);
			pdwCounter = pdwCounter + ((pInstance->instance.NameLength + sizeof(DWORD)-1)/sizeof(DWORD));

			/* Set the PERF_COUNTER_BLOCK */
			pPerfCounterBlock = (PERF_COUNTER_BLOCK *) pdwCounter;
			pPerfCounterBlock->ByteLength = SIZE_OF_NS_PERFORMANCE_DATA + sizeof(PERF_COUNTER_BLOCK);
			pdwCounter = (PDWORD) (&pPerfCounterBlock[1]);

			/* Set the Instance Data */
			Get_Actual_Data(pInstance->pData,(struct _status_struct_s*)pdwCounter);
	
			{
			  DWORD x = (SIZE_OF_NS_PERFORMANCE_DATA) / sizeof(DWORD);
			  
			  pdwCounter += x;
			}
		}
	}
	
	*lppData = (PVOID)(pdwCounter);
    *lpNumObjectTypes = 1;
    *lpcbTotalBytes = (PBYTE) pdwCounter - (PBYTE) pNSDataDefinition;
	pNSDataDefinition->NS_ObjectType.TotalByteLength = *lpcbTotalBytes;

    return ERROR_SUCCESS;
}

