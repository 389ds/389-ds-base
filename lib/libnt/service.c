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

#include <windows.h>
#include "nt/ntos.h"

#define SERVRET_ERROR     0
#define SERVRET_INSTALLED 1
#define SERVRET_STARTING  2
#define SERVRET_STARTED   3
#define SERVRET_STOPPING  4
#define SERVRET_REMOVED   5


DWORD NS_WINAPI
SERVICE_GetNTServiceStatus(LPCTSTR szServiceName, LPDWORD lpLastError )
{
	SERVICE_STATUS ServiceStatus;
    SC_HANDLE   schService = NULL;
    SC_HANDLE   schSCManager = NULL;
    DWORD lastError = 0;
    int ret = 0;

	//ereport(LOG_INFORM, "open SC Manager");
	if ((schSCManager = OpenSCManager(
                        NULL,                   // machine (NULL == local)
                        NULL,                   // database (NULL == default)
                        SC_MANAGER_ALL_ACCESS   // access required
                        )) == NULL ) {
        lastError = GetLastError();
        ret = SERVRET_ERROR;
        goto finish;
	}

    schService = OpenService(schSCManager, szServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL ) {
        lastError = GetLastError();
		if (lastError == ERROR_SERVICE_DOES_NOT_EXIST) {
            lastError = 0;
            ret = SERVRET_REMOVED;
        } else
            ret = SERVRET_ERROR;
        goto finish;
    }

    ret = ControlService(schService, SERVICE_CONTROL_INTERROGATE, &ServiceStatus);

    if ( !ret ) {
        lastError = GetLastError();
        if ( lastError == ERROR_SERVICE_NOT_ACTIVE ) {
            lastError = 0;
    	    ret = SERVRET_INSTALLED;
        } else
            ret = SERVRET_ERROR;
        goto finish;
	}

    switch ( ServiceStatus.dwCurrentState ) {
        case SERVICE_STOPPED: ret = SERVRET_INSTALLED; break;
        case SERVICE_START_PENDING: ret = SERVRET_STARTING; break;
        case SERVICE_STOP_PENDING: ret = SERVRET_STOPPING; break;
        case SERVICE_RUNNING: ret = SERVRET_STARTED; break;
        case SERVICE_CONTINUE_PENDING: ret = SERVRET_STARTED; break;
        case SERVICE_PAUSE_PENDING: ret = SERVRET_STARTED; break;
        case SERVICE_PAUSED: ret = SERVRET_STARTED; break;
        default: ret = SERVRET_ERROR; break;
    }

finish:
    if ( schService)
	    CloseServiceHandle(schService);
    if ( schSCManager)
	    CloseServiceHandle(schSCManager);
    if ( lpLastError )
        *lpLastError = lastError;
    return ret;
}

DWORD NS_WINAPI
SERVICE_InstallNTService(LPCTSTR szServiceName, LPCTSTR szServiceDisplayName, LPCTSTR szServiceExe )
{
    LPCTSTR lpszBinaryPathName = szServiceExe;
    SC_HANDLE   schService = NULL;
    SC_HANDLE   schSCManager = NULL;
    int lastError = 0;
    int ret = 0;


	//ereport(LOG_INFORM, "open SC Manager");
	if ((schSCManager = OpenSCManager(
                        NULL,                   // machine (NULL == local)
                        NULL,                   // database (NULL == default)
                        SC_MANAGER_ALL_ACCESS   // access required
                        )) == NULL ) {
        lastError = GetLastError();
        goto finish;
	}

    /* check if service already exists */
 	schService = OpenService(   schSCManager,
                                szServiceName,
                        		SERVICE_ALL_ACCESS
                        		);
	if (schService) {
        lastError = ERROR_SERVICE_EXISTS;
        goto finish;
	}

    schService = CreateService(
        schSCManager,               // SCManager database
        szServiceName,                // name of service
        szServiceDisplayName,         // name to display
        SERVICE_ALL_ACCESS,         // desired access
        SERVICE_WIN32_OWN_PROCESS |
            SERVICE_INTERACTIVE_PROCESS,  // service type
        SERVICE_AUTO_START, //SERVICE_DEMAND_START,       // start type
        SERVICE_ERROR_NORMAL,       // error control type
        lpszBinaryPathName,         // service's binary
        NULL,                       // no load ordering group
        NULL,                       // no tag identifier
        NULL,                       // no dependencies
        NULL,                   // LocalSystem account
        NULL);                  // no password

    if (schService == NULL) {
		lastError = GetLastError();
    }

    // successfully installed service

finish:
    if ( schService)
	    CloseServiceHandle(schService);
    if ( schSCManager)
	    CloseServiceHandle(schSCManager);
    return lastError;
}


DWORD NS_WINAPI
SERVICE_RemoveNTService(LPCTSTR szServiceName)
{
    SC_HANDLE   schService = NULL;
    SC_HANDLE   schSCManager = NULL;
    int lastError = 0;
    int ret = 0;

	//ereport(LOG_INFORM, "open SC Manager");
	if ((schSCManager = OpenSCManager(
                        NULL,                   // machine (NULL == local)
                        NULL,                   // database (NULL == default)
                        SC_MANAGER_ALL_ACCESS   // access required
                        )) == NULL ) {
        lastError = GetLastError();
        goto finish;
	}

    schService = OpenService(schSCManager, szServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL ) {
        lastError = GetLastError();
        goto finish;
    }

    ret = DeleteService(schService);

    if ( !ret) {
        lastError = GetLastError();
        goto finish;
	}

    // successfully removed service

finish:
    if ( schService)
	    CloseServiceHandle(schService);
    if ( schSCManager)
	    CloseServiceHandle(schSCManager);
    return lastError;
}

DWORD NS_WINAPI
SERVICE_StartNTService(LPCTSTR szServiceName)
{
    SC_HANDLE   schService = NULL;
    SC_HANDLE   schSCManager = NULL;
    int lastError = 0;
    int ret = 0;

	//ereport(LOG_INFORM, "open SC Manager");
	if ((schSCManager = OpenSCManager(
                        NULL,                   // machine (NULL == local)
                        NULL,                   // database (NULL == default)
                        SC_MANAGER_ALL_ACCESS   // access required
                        )) == NULL ) {
        lastError = GetLastError();
        goto finish;
	}

    schService = OpenService(schSCManager, szServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL ) {
        lastError = GetLastError();
        goto finish;
    }

    ret = StartService(schService, 0, NULL);

    if ( !ret ) {
        lastError = GetLastError();
        goto finish;
	}

	// successfully started service


finish:
    if ( schService)
	    CloseServiceHandle(schService);
    if ( schSCManager)
	    CloseServiceHandle(schSCManager);
    return lastError;
}

DWORD NS_WINAPI 
SERVICE_StartNTServiceAndWait(LPCTSTR szServiceName, LPDWORD lpdwLastError)
{
    DWORD dwLastError;
    DWORD dwStatus;
    int i;

    /* check if service is running */
    dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
    if ( dwStatus == SERVRET_STARTED )
            return TRUE;

    dwLastError = SERVICE_StartNTService( szServiceName );
    if ( dwLastError != 0 ) {
        goto errorExit;
    }

    for ( i=0; i<5; i++ ) {
        // make sure the service got installed
        dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
        if ( dwStatus == SERVRET_ERROR) {
            if ( dwLastError != ERROR_SERVICE_CANNOT_ACCEPT_CTRL )
                goto errorExit;
        } else if ( dwStatus == SERVRET_STARTED )
            return TRUE;

        Sleep ( 1000 );
    }

    dwLastError = 0;

errorExit:
    if ( lpdwLastError )
        *lpdwLastError = dwLastError;
    return FALSE;
}

DWORD NS_WINAPI
SERVICE_StopNTService(LPCTSTR szServiceName)
{
    SC_HANDLE   schService = NULL;
    SC_HANDLE   schSCManager = NULL;
    int lastError = 0;
    int ret = 0;
	SERVICE_STATUS ServiceStatus;

	//ereport(LOG_INFORM, "open SC Manager");
	if ((schSCManager = OpenSCManager(
                        NULL,                   // machine (NULL == local)
                        NULL,                   // database (NULL == default)
                        SC_MANAGER_ALL_ACCESS   // access required
                        )) == NULL ) {
        lastError = GetLastError();
        goto finish;
	}

    schService = OpenService(schSCManager, szServiceName, SERVICE_ALL_ACCESS);

    if (schService == NULL ) {
        lastError = GetLastError();
        goto finish;
    }

    ret = ControlService(schService, SERVICE_CONTROL_STOP, &ServiceStatus);

    if ( !ret ) {
        lastError = GetLastError();
        goto finish;
	}

    // server is stopping

finish:
    if ( schService)
	    CloseServiceHandle(schService);
    if ( schSCManager)
	    CloseServiceHandle(schSCManager);
    return lastError;
}

DWORD NS_WINAPI 
SERVICE_StopNTServiceAndWait(LPCTSTR szServiceName, LPDWORD lpdwLastError)
{
    DWORD dwLastError;
    DWORD dwStatus;
    int i;

    /* check if service is running */
    dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
    if ( dwStatus != SERVRET_STARTED )
            return TRUE;

    for ( i=0; i<30; i++ ) {
        dwLastError = SERVICE_StopNTService( szServiceName );
        Sleep ( 1000 );

        // make sure the service is stoppped and just installed
        dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
        Sleep ( 1000 );
        if ( dwStatus == SERVRET_INSTALLED ) {
            Sleep ( 1000 );
            return TRUE;
        }            
    }

    if ( lpdwLastError )
        *lpdwLastError = dwLastError;
    return FALSE;
}

DWORD NS_WINAPI 
SERVICE_ReinstallNTService(LPCTSTR szServiceName, LPCTSTR szServiceDisplayName, LPCTSTR szServiceExe )
{
    DWORD dwLastError;
    DWORD dwStatus;
    int i;

    for ( i=0; i< 5; i++ ) {

        /* if service is running, stop it */
        dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
        if ( dwStatus == SERVRET_STARTED )
            SERVICE_StopNTServiceAndWait( szServiceName, &dwLastError );

        /* if service is installed, remove it */
        dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
        if ( dwStatus == SERVRET_INSTALLED )
            SERVICE_RemoveNTService( szServiceName );

        /* try and install the service again */
        dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
        if ( dwStatus == SERVRET_REMOVED )
           SERVICE_InstallNTService( szServiceName, szServiceDisplayName, szServiceExe );

        /* try and start the service again */
        dwStatus = SERVICE_GetNTServiceStatus( szServiceName, &dwLastError );
        if ( dwStatus == SERVRET_INSTALLED ) {
            return NO_ERROR;
        }
    }

    /* if no error reported, force an error */
    if ( dwLastError == NO_ERROR )
        dwLastError = (DWORD)-1;

    return dwLastError;
}

