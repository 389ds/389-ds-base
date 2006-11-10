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


#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include "ldap.h"
#include "regparms.h"
#include "nspr.h"
#include "plstr.h"

HANDLE hSlapdEventSource;
LPTSTR	pszServerName;

void ReportSlapdEvent(WORD wEventType, DWORD dwIdEvent, WORD wNumInsertStrings, 
						char *pszStrings)
{
	LPCTSTR	lpszStrings[64];
	BOOL bSuccess;

	if( hSlapdEventSource )
	{
		if( pszServerName )
			lpszStrings[0] = (LPCTSTR)pszServerName;

		if( pszStrings != NULL)
            lpszStrings[1] = (LPCTSTR)pszStrings;

		wNumInsertStrings++;

		/* Now report the event, which will add this event to the event log */
		bSuccess = ReportEvent(hSlapdEventSource,   /* event-log handle */
							wEventType,				/* event type       */
							0,                      /* category zero    */
							dwIdEvent,              /* event ID         */
							NULL,                   /* no user SID      */
							wNumInsertStrings,      /* number of substr */
							0,                      /* no binary data   */
							lpszStrings,            /* string array     */
							NULL);                  /* address of data  */
	}
	
} /* ReportSlapdEvent */

BOOL ReportSlapdStatusToSCMgr(
					SERVICE_STATUS *serviceStatus,
					SERVICE_STATUS_HANDLE serviceStatusHandle,
					HANDLE Event,
					DWORD dwCurrentState,
                    DWORD dwWin32ExitCode,
                    DWORD dwCheckPoint,
                    DWORD dwWaitHint)
{
    /*  Disable control requests until the service is started. */
    if (dwCurrentState == SERVICE_START_PENDING)
        serviceStatus->dwControlsAccepted = 0;
    else
        serviceStatus->dwControlsAccepted = SERVICE_ACCEPT_STOP |
            SERVICE_ACCEPT_PAUSE_CONTINUE;

    serviceStatus->dwCurrentState = dwCurrentState;
    serviceStatus->dwWin32ExitCode = dwWin32ExitCode;
    serviceStatus->dwCheckPoint = dwCheckPoint;

    serviceStatus->dwWaitHint = dwWaitHint;

    /* Report the status of the service to the service control manager. */
    return SetServiceStatus( serviceStatusHandle, serviceStatus);

}	/* ReportSlapdStatusToSCMgr */

// This is a routine that we use to check for multiple instances of a server with
// the same id. We cannot use a shared data section to keep count of instances since
// there will be multiple instances of the server running. MS recommends using a
// sync object to do this. Thus we attempt to create an object with same NAME 
// but different TYPE as the server "Done" event.We have a small race condition
// between the check and the creation of the "Done" event.

BOOL
MultipleInstances()
{
    HANDLE hServDoneSemaphore;
    DWORD result;
    CHAR ErrMsg[1024];
    char szDoneEvent[256];

	if( !pszServerName )
		return FALSE;

	PR_snprintf(szDoneEvent, sizeof(szDoneEvent), "NS_%s", pszServerName);

    hServDoneSemaphore = CreateSemaphore(
        NULL,   // security attributes    
        0,      // initial count for semaphore
        1,      // maximum count for semaphore
        szDoneEvent);

    if ( hServDoneSemaphore == NULL) {

        result = GetLastError();
        if (result == ERROR_INVALID_HANDLE) {

            PR_snprintf(ErrMsg, sizeof(ErrMsg), "Server %s is already"
            " running. Terminating this instance.", pszServerName);

            MessageBox(GetDesktopWindow(), ErrMsg,
                "SERVER ALREADY RUNNING", MB_ICONEXCLAMATION | MB_OK);
            return TRUE;

        } else {
            /* We aren't too interested in why the creation failed
             * if it is not because of another instance */

            return FALSE;
        }
    }  // hServDoneSemaphore == NULL

    CloseHandle(hServDoneSemaphore);
    return FALSE; 
}

BOOL SlapdIsAService()
{
	// May change in V2.0
	return FALSE;
}

BOOL SlapdGetServerNameFromCmdline(char *szServerName, char *szCmdLine, int dirname)
{
	BOOL bReturn = FALSE;
	char *szChar = NULL;
	char szCmdCopy[_MAX_PATH]; 

	if( szCmdLine )
	{
		memset(szCmdCopy, 0, _MAX_PATH );
		PL_strncpyz( szCmdCopy, szCmdLine , sizeof(szCmdCopy) );
	}
	else
		return(bReturn);

	// szCmdCopy should be something like 
	//		c:\navgold\server\slapd-kennedy\config\slapd.conf
	// unless dirname is TRUE in which case it should be
	//		c:\navgold\server\slapd-kennedy
	if(szChar = strrchr(szCmdCopy, '\\'))
	{
		*szChar = 0;
		if(dirname)
		{
			strcpy(szServerName, szChar+1);
			bReturn = TRUE;
		}
		else if(szChar = strrchr(szCmdCopy, '\\'))
		{
			// szCmdCopy should be c:\navgold\server\slapd-kennedy\config
			*szChar = 0;
			// szCmdCopy should be c:\navgold\server\slapd-kennedy
			if(szChar = strrchr(szCmdCopy, '\\'))
			{
				szChar++;
		   		// szChar should point to slapd-kennedy
				strcpy(szServerName, szChar);
				bReturn = TRUE;
			}
		}
	}
	else
	{
		// szCmdCopy should be something like slapd-kennedy
		strcpy(szServerName, szCmdCopy);
		bReturn = TRUE;
	}

	if(strlen(szServerName) == 0)
		bReturn = FALSE;

	return(bReturn);
}

#endif _WIN32
