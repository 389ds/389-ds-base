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
#if defined( XP_WIN32 )
#include <windows.h>
#include <process.h>
#include "regparms.h"
#else
#include <signal.h>
#include <sys/signal.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "dsalib.h"
#include <string.h>
#include "nspr.h"

#if defined( XP_WIN32 )
SC_HANDLE schService;
SC_HANDLE schSCManager;

int StartServer(); 
int StopandRestartServer();
int StopServer();
 
int StopNetscapeProgram();
int StartNetscapeProgram();

int StopNetscapeService();
int StartNetscapeService();
void WaitForServertoStop();
#endif

/*
 * Get status for the Directory Server.
 * 0 -- down
 * 1 -- up
 * -1 -- unknown
 */
#if !defined( XP_WIN32 )
static pid_t       server_pid;

DS_EXPORT_SYMBOL int
ds_get_updown_status()
{
    char	pid_file_name[BIG_LINE];
    char        *root;
    FILE        *pidfile;
    int         ipid = -1;
	int status = 0;
 
    if ( (root = ds_get_install_root()) == NULL ) {
		fprintf(stderr, "ds_get_updown_status: could not get install root\n");
        return(DS_SERVER_UNKNOWN);
	}
    PR_snprintf(pid_file_name, BIG_LINE, "%s/logs/pid", root);
    pidfile = fopen(pid_file_name, "r");
    if ( pidfile == NULL ) {
/*
		fprintf(stderr,
				"ds_get_updown_status: could not open pid file=%s errno=%d\n",
				pid_file_name, errno);
*/
        return(DS_SERVER_DOWN);
	}
	status = fscanf(pidfile, "%d\n", &ipid);
	fclose(pidfile);
    if ( status == -1 ) {
		fprintf(stderr,
				"ds_get_updown_status: pidfile=%s server_pid=%d errno=%d\n",
				pid_file_name, ipid, errno);
        unlink(pid_file_name);     /* junk in file? */
        return(DS_SERVER_DOWN);
    }
    server_pid = (pid_t) ipid;
    if ( (status = kill(server_pid, 0)) != 0 && errno != EPERM ) {
		/* we should get ESRCH if the server is down, anything else may be
		   a real problem */
		if (errno != ESRCH) {
			fprintf(stderr,
					"ds_get_updown_status: pidfile=%s server_pid=%d status=%d errno=%d\n",
					pid_file_name, server_pid, status, errno);
		}
        unlink(pid_file_name);     /* pid does not exist! */
        return(DS_SERVER_DOWN);
    }
    return(DS_SERVER_UP);
}
#else
DS_EXPORT_SYMBOL int
ds_get_updown_status()
{
    char	*ds_name = ds_get_server_name();
    HANDLE hServerDoneEvent = NULL;

    /* watchdog.c creates a global event of this same name */
    if((hServerDoneEvent = OpenEvent(EVENT_ALL_ACCESS, TRUE, ds_name)) != NULL) 
	{
       CloseHandle(hServerDoneEvent);
       return(DS_SERVER_UP);
    }
    if(GetLastError() == ERROR_ACCESS_DENIED)  /* it exists */
       return(DS_SERVER_UP);

    /* assume it's not running. */
    return(DS_SERVER_DOWN);
}
#endif

/*
  This function does not require calling ds_get_config(), but requires
  that that information be passed in.  This is very useful for starting
  the server during installation, because we already have all of the
  configuration information in memory, we don't need to read it in
*/
DS_EXPORT_SYMBOL int
ds_bring_up_server_install(int verbose, char *root, char *errorlog)
{
#if !defined( XP_WIN32 )
    char	startup_line[BIG_LINE];
    char	statfile[PATH_MAX];
    char	*tmp_dir;
#endif
    int     error = -1;
    int		status = DS_SERVER_DOWN;
    int		cur_size = 0;
    FILE	*sf = NULL;
    char	msgBuf[BIG_LINE] = {0};
	int secondsToWaitForServer = 600;
	char *serverStartupString = "slapd started.";

    status = ds_get_updown_status();
    if ( status == DS_SERVER_UP )
        return(DS_SERVER_ALREADY_UP);
    if (!root || !errorlog)
        return(DS_SERVER_UNKNOWN);
 
    if (verbose) {
    	ds_send_status("starting up server ...");
		cur_size = ds_get_file_size(errorlog);
	}

#if !defined( XP_WIN32 )
    tmp_dir = ds_get_tmp_dir();
    PR_snprintf(statfile, PATH_MAX, "%s%cstartup.%d", tmp_dir, FILE_SEP, (int)getpid());

    PR_snprintf(startup_line, BIG_LINE, "%s%c%s > %s 2>&1",
			root, FILE_SEP, START_SCRIPT, statfile);
    alter_startup_line(startup_line);
    error = system(startup_line);
    if (error == -1)
		error = DS_SERVER_DOWN; /* could not start server */
    else
		error = DS_SERVER_UP; /* started server */
#else
    error = StartServer();
#endif

	if (error != DS_SERVER_UP)
	{
#if !defined( XP_WIN32 )
		FILE* fp = fopen(statfile, "r");
		if (fp)
		{
			while(fgets(msgBuf, BIG_LINE, fp))
				ds_send_status(msgBuf);
			fclose(fp);
		}
#endif
		return DS_SERVER_COULD_NOT_START;
	}

    if (verbose)
    {
		/* 
		 * Stop in N secs or whenever the startup message comes up.
		 * Do whichever happens first.  msgBuf will contain the last
		 * line read from the errorlog.
		 */
		ds_display_tail(errorlog, secondsToWaitForServer, cur_size,
						serverStartupString, msgBuf);
    }
    if ( error != DS_SERVER_UP ) {
		int retval = DS_SERVER_UNKNOWN;
		if (strstr(msgBuf, "semget"))
			retval = DS_SERVER_MAX_SEMAPHORES;
		else if (strstr(msgBuf, "Back-End Initialization Failed"))
			retval = DS_SERVER_CORRUPTED_DB;
		else if (strstr(msgBuf, "not initialized... exiting"))
			retval = DS_SERVER_CORRUPTED_DB;
		else if (strstr(msgBuf, "address is in use"))
			retval = DS_SERVER_PORT_IN_USE;
#if defined( XP_WIN32 )
		/* on NT, if we run out of resources, there will not even be an error
		   log
		   */
		else if (msgBuf[0] == 0) {
			retval = DS_SERVER_NO_RESOURCES;
		}
#endif
		if (verbose)
			ds_send_error("error in starting server.", 1);
		return(retval);
    }
    if (verbose) {
#if !defined( XP_WIN32 )
		if( !(sf = fopen(statfile, "r")) )  {
			ds_send_error("could not read status file.", 1);
			return(DS_SERVER_UNKNOWN);
		}

		while ( fgets(startup_line, BIG_LINE, sf) )
			ds_send_error(startup_line, 0);
		fclose(sf);
		unlink(statfile);
#endif
		status = DS_SERVER_UNKNOWN;
		if (strstr(msgBuf, "semget"))
			status = DS_SERVER_MAX_SEMAPHORES;
		else if (strstr(msgBuf, "Back-End Initialization Failed"))
			status = DS_SERVER_CORRUPTED_DB;
		else if (strstr(msgBuf, "not initialized... exiting"))
			status = DS_SERVER_CORRUPTED_DB;
		else if (strstr(msgBuf, "address is in use"))
			status = DS_SERVER_PORT_IN_USE;
#if defined( XP_WIN32 )
		/* on NT, if we run out of resources, there will not even be an error
		   log
		   */
		else if (msgBuf[0] == 0) {
			status = DS_SERVER_NO_RESOURCES;
		}
#endif
    } else {
		int tries;
		for (tries = 0; tries < secondsToWaitForServer; tries++) {
			if (ds_get_updown_status() == DS_SERVER_UP) break;
			PR_Sleep(PR_SecondsToInterval(1));
		}
		if (verbose) {
			char str[100];
			PR_snprintf(str, sizeof(str), "Had to retry %d times", tries);
			ds_send_status(str);
		}
    }

    if ( (status == DS_SERVER_DOWN) || (status == DS_SERVER_UNKNOWN) )
		status = ds_get_updown_status();

    return(status);
}

/*
 * Start the Directory Server and return status.
 * Do not start if the server is already started.
 * 0 -- down
 * 1 -- up
 * -1 -- unknown
 * -2 -- already up
 */
 
DS_EXPORT_SYMBOL int
ds_bring_up_server(int verbose)
{
    char        *root;
    int		status;
    char	*errorlog;
    status = ds_get_updown_status();
    if ( status == DS_SERVER_UP )
        return(DS_SERVER_ALREADY_UP);
    if ( (root = ds_get_install_root()) == NULL )
        return(DS_SERVER_UNKNOWN);
 
    errorlog = ds_get_config_value(DS_ERRORLOG);
    if ( errorlog == NULL ) {
		errorlog = ds_get_errors_name(); /* fallback */
    }
	return ds_bring_up_server_install(verbose, root, errorlog);
}

DS_EXPORT_SYMBOL int
ds_bring_down_server()
{
    char        *root;
    int		status;
    int		cur_size;
    char *errorlog;
 
    status = ds_get_updown_status();	/* set server_pid too! */
    if ( status != DS_SERVER_UP ) {
	ds_send_error("The server is not up.", 0);
        return(DS_SERVER_ALREADY_DOWN);
    }
    if ( (root = ds_get_install_root()) == NULL ) {
	ds_send_error("Could not get the server root directory.", 0);
        return(DS_SERVER_UNKNOWN);
    }
 
    ds_send_status("shutting down server ...");
    if (!(errorlog = ds_get_errors_name())) {
	ds_send_error("Could not get the error log filename.", 0);
	return DS_SERVER_UNKNOWN;
    }

    cur_size = ds_get_file_size(errorlog);
#if !defined( XP_WIN32 )
    if ( (kill(server_pid, SIGTERM)) != 0)  {
	if (errno == EPERM) {
	    ds_send_error("Not permitted to kill server.", 0);
	    fprintf (stdout, "[%s]: kill (%li, SIGTERM) failed with errno = EPERM.<br>\n",
		     ds_get_server_name(), (long)server_pid);
	} else {
	    ds_send_error("error in killing server.", 1);
	}
        return(DS_SERVER_UNKNOWN);
    }
#else
    if( StopServer() == DS_SERVER_DOWN )  
	{
		ds_send_status("shutdown: server shut down");
	}
	else
	{
        ds_send_error("error in killing server.", 1);
        return(DS_SERVER_UNKNOWN);
	}
#endif
    /* 
     * Wait up to SERVER_STOP_TIMEOUT seconds for the stopped message to
	 * appear in the error log.
     */
    ds_display_tail(errorlog, SERVER_STOP_TIMEOUT, cur_size, "slapd stopped.", NULL);
    /* in some cases, the server will tell us it's down when it's really not,
       so give the OS a chance to remove it from the process table */
	PR_Sleep(PR_SecondsToInterval(1));
    return(ds_get_updown_status());
}

#if defined( XP_WIN32 )

static BOOLEAN
IsService()
{
#if 0
 	CHAR ServerKey[512], *ValueString;
	HKEY hServerKey;
	DWORD dwType, ValueLength, Result;

	PR_snprintf(ServerKey,sizeof(ServerKey), "%s\\%s", COMPANY_KEY, PRODUCT_KEY);

	Result = RegOpenKey(HKEY_LOCAL_MACHINE, ServerKey, &hServerKey);
	if (Result != ERROR_SUCCESS) {
		return TRUE;
	}
	ValueLength = 512;
	ValueString = (PCHAR)malloc(ValueLength);

	Result = RegQueryValueEx(hServerKey, IS_SERVICE_KEY, NULL,
		&dwType, ValueString, &ValueLength);
	if (Result != ERROR_SUCCESS) {
		return TRUE;
	}
	if (strcmp(ValueString, "yes")) {
		return FALSE;
	}
	else {
		return TRUE;
	}
#else
	return TRUE;
#endif
}

#if 0
NSAPI_PUBLIC BOOLEAN
IsAdminService()
{
 	CHAR AdminKey[512], *ValueString;
	HKEY hAdminKey;
	DWORD dwType, ValueLength, Result;

	PR_snprintf(AdminKey,sizeof(AdminKey), "%s\\%s", COMPANY_KEY, ADMIN_REGISTRY_ROOT_KEY);

	Result = RegOpenKey(HKEY_LOCAL_MACHINE, AdminKey, &hAdminKey);
	if (Result != ERROR_SUCCESS) {
		return TRUE;
	}
	ValueLength = 512;
	ValueString = (PCHAR)malloc(ValueLength);

	Result = RegQueryValueEx(hAdminKey, IS_SERVICE_KEY, NULL,
		&dwType, ValueString, &ValueLength);
	if (Result != ERROR_SUCCESS) {
		return TRUE;
	}
	if (strcmp(ValueString, "yes")) {
		return FALSE;
	} else {
		return TRUE;
	}
}
#endif

int
StartServer()
{
    CHAR ErrorString[512];
    BOOLEAN Service;

    /* Figure out if the server is a service or an exe */
    Service = IsService();

     if(Service) {
        if (!(schSCManager = OpenSCManager(
                NULL,                   // machine (NULL == local)
                NULL,                   // database (NULL == default)
                SC_MANAGER_ALL_ACCESS   // access required
                ))) {
			PR_snprintf(ErrorString, sizeof(ErrorString),
                "Error: Could not open the ServiceControlManager:%d "
                "Please restart the server %s from the Services Program Item "
                "in the Control Panel", ds_get_server_name(), GetLastError());
            ds_send_error(ErrorString, 0);
            return(DS_SERVER_UNKNOWN);  
        }
        return(StartNetscapeService());
    } else {
        return(StartNetscapeProgram());
    }
}
 
int
StopandRestartServer()
{
    CHAR ErrorString[512];
    BOOLEAN Service;


    /* First figure out if the server is a service or an exe */
    Service = IsService();
     if(Service) {
        if (!(schSCManager = OpenSCManager(
                NULL,                   // machine (NULL == local)
                NULL,                   // database (NULL == default)
                SC_MANAGER_ALL_ACCESS   // access required
                ))) {
            PR_snprintf(ErrorString, sizeof(ErrorString),
                "Error: Could not restart server."
                "Please restart the server %s from the Services Program Item "
                "in the Control Panel", ds_get_server_name());
            ds_send_error(ErrorString, 0);
            return(DS_SERVER_UNKNOWN);  
        }
        if (StopNetscapeService() != DS_SERVER_DOWN)
            return(DS_SERVER_UNKNOWN);

        return(StartNetscapeService());
    } else {
        if (StopNetscapeProgram() != DS_SERVER_DOWN)
            return(DS_SERVER_UNKNOWN);
        return(StartNetscapeProgram());
    }
 
}
 
int
StopServer()
{
    CHAR ErrorString[512];
    BOOLEAN Service;

    /* First figure out if the server is a service or an exe */
    Service = IsService();

    if(Service) {
        if (!(schSCManager = OpenSCManager(
                NULL,                   // machine (NULL == local)
                NULL,                   // database (NULL == default)
                SC_MANAGER_ALL_ACCESS   // access required
                ))) {
            PR_snprintf(ErrorString, sizeof(ErrorString),
                "Error: Could not open the ServiceControlManager:%d "
                "Please restart the server %s from the Services Program Item "
                "in the Control Panel", ds_get_server_name(), GetLastError());
			ds_send_error(ErrorString, 0);
            return(DS_SERVER_UNKNOWN);  
        }
        return(StopNetscapeService());
    } else {
        return(StopNetscapeProgram());
    }
}

int
StartNetscapeProgram()
{
    char line[BIG_LINE], cmd[BIG_LINE];
    char *tmp = ds_get_install_root();

	CHAR ErrorString[512];
	STARTUPINFO siStartInfo;
	PROCESS_INFORMATION piProcInfo;
    FILE *CmdFile;

    ZeroMemory(line, sizeof(line));

    PR_snprintf(line, BIG_LINE, "%s\\startsrv.bat", tmp);
    
    CmdFile = fopen(line, "r");
    if (!CmdFile) 
	{
        PR_snprintf(ErrorString, sizeof(ErrorString), "Error:Tried to start server %s "
            ": Could not open the startup script %s :Error %d. Please "
            "run startsrv.bat from the server's root directory.",
            ds_get_server_name(), line, errno);
        ds_send_error(ErrorString, 0);
        return(DS_SERVER_DOWN);
    }

    ZeroMemory(cmd, sizeof(cmd));
    if (!fread(cmd, 1, BIG_LINE, CmdFile)) 
	{
        PR_snprintf(ErrorString, sizeof(ErrorString), "Error:Tried to start server %s "
            ": Could not read the startup script %s :Error %d. Please "
            "run startsrv.bat from the server's root directory.",
            ds_get_server_name(), line, errno);
        ds_send_error(ErrorString, 0);
        return(DS_SERVER_DOWN);
    }
    
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.lpReserved = siStartInfo.lpReserved2 = NULL;
	siStartInfo.cbReserved2 = 0;
	siStartInfo.lpDesktop = NULL;

	if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE,
		0, NULL, NULL, &siStartInfo, &piProcInfo)) 
	{
        PR_snprintf(ErrorString, sizeof(ErrorString), "Error:Tried to start server %s "
            ": Could not start up the startup script %s :Error %d. Please "
            "run startsrv.bat from the server's root directory.",
            ds_get_server_name(), line, GetLastError());
        ds_send_error(ErrorString, 0);
        return(DS_SERVER_DOWN);
	} 

	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);
    return(DS_SERVER_UP);
}

int
StopNetscapeProgram()
{
    HANDLE hEvent;
    CHAR ErrorString[512];
    char *servid = ds_get_server_name();

    hEvent = CreateEvent(NULL, TRUE, FALSE, servid);  
    if(!SetEvent(hEvent)) 
	{
        PR_snprintf(ErrorString, sizeof(ErrorString), "Tried to stop existing server %s"
            ": Could not signal it to stop :Error %d",
            servid, GetLastError());
        ds_send_error(ErrorString, 0);
        return(DS_SERVER_UNKNOWN);
    }

    return(DS_SERVER_DOWN);  
}

int
StopNetscapeService()
{
    BOOL ret;
    SERVICE_STATUS ServiceStatus;
    DWORD Error;
    CHAR ErrorString[512];
    char *serviceName = ds_get_server_name();
 
    schService = OpenService(schSCManager, serviceName, SERVICE_ALL_ACCESS);
 
    if (schService == NULL) 
	{
        PR_snprintf(ErrorString, sizeof(ErrorString), "Tried to open service"
            " %s: Error %d (%s). Please"
            " stop the server from the Services Item in the Control Panel",
            serviceName, GetLastError(), ds_system_errmsg());
            ds_send_error(ErrorString, 0);
            return(DS_SERVER_UP);
    }
 
    ret = ControlService(schService, SERVICE_CONTROL_STOP, &ServiceStatus);
    Error = GetLastError();
    /* if ControlService returns with ERROR_SERVICE_CANNOT_ACCEPT_CTRL and
       the server status indicates that it is either shutdown or in the process
        of shutting down, then just wait for it to stop as usual */
    if (ret ||
        ((Error == ERROR_SERVICE_CANNOT_ACCEPT_CTRL) &&
         ((ServiceStatus.dwCurrentState == SERVICE_STOPPED) ||
          (ServiceStatus.dwCurrentState == SERVICE_STOP_PENDING))))
    {
        CloseServiceHandle(schService);
        /* We make sure that the service is stopped */
        WaitForServertoStop();
        return(DS_SERVER_DOWN);
    } 
    else if (Error != ERROR_SERVICE_NOT_ACTIVE) 
	{
        PR_snprintf(ErrorString, sizeof(ErrorString), "Tried to stop service"
            " %s: Error %d (%s)."
            " Please stop the server from the Services Item in the"
            " Control Panel", serviceName, Error, ds_system_errmsg());
        ds_send_error(ErrorString, 0);
        return(DS_SERVER_UNKNOWN);
    }
    return(DS_SERVER_DOWN);
}
 
 
int
StartNetscapeService()
{
    CHAR ErrorString[512];
    int retries = 0;
    char *serviceName = ds_get_server_name();
 
    schService = OpenService(
                    schSCManager,               // SCManager database
                    serviceName,                // name of service
                    SERVICE_ALL_ACCESS); 
    if (schService == NULL) 
	{
        CloseServiceHandle(schService);
        PR_snprintf(ErrorString, sizeof(ErrorString),"Tried to start"
            " the service %s: Error %d. Please"
            " start the server from the Services Item in the Control Panel",
            serviceName, GetLastError());
            ds_send_error(ErrorString, 0);
        return(DS_SERVER_DOWN);
    }
 
    if (!StartService(schService, 0, NULL)) 
	{
        CloseServiceHandle(schService);
        PR_snprintf(ErrorString, sizeof(ErrorString), "StartService:Could not start "
            "the Directory service %s: Error %d. Please restart the server "
            "from the Services Item in the Control Panel",
            serviceName, GetLastError());
        ds_send_error(ErrorString, 0);
        return(DS_SERVER_DOWN);
    }

    CloseServiceHandle(schService);
    return(DS_SERVER_UP);
}

void
WaitForServertoStop()
{
    HANDLE hServDoneSemaphore;
    int result,retries = 0;
    char *serviceName = ds_get_server_name();
    char *newServiceName;

RETRY:

	newServiceName = PR_smprintf("NS_%s", serviceName);

    hServDoneSemaphore = CreateSemaphore(
        NULL,   // security attributes    
        0,      // initial count for semaphore
        1,      // maximum count for semaphore
        newServiceName);

    PR_smprintf_free(newServiceName);

    if ( hServDoneSemaphore == NULL) {
        result = GetLastError();
        if (result == ERROR_INVALID_HANDLE) {
             if (retries < SERVER_STOP_TIMEOUT) {
                retries++;
                Sleep(1000);
                goto RETRY;
             }
       } else {
            /* We aren't too interested in why the creation failed
             * if it is not because of another instance */
            return;
       }
    }  // hServDoneSemaphore == NULL
    CloseHandle(hServDoneSemaphore);
    return;
}
#endif
