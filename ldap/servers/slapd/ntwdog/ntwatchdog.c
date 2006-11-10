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

#pragma warning(disable : 4001) 
// disable warning C4001: nonstandard extension 'single line comment' was used

//                                                                          //
//  Name: NTWATCHDOG                                                        //
//	 Platforms: WIN32                                                       //
//  Description: shell for nt directory server, runs as service, launches   //
//               server, monitors it, re-launches if server crashes,        //
//  Notes:                                                                  //
//  ......................................................................  //
//  Watchdog can be run as an application or a service.  When run as a      //
//  service, it uses the service name from the SCM for the server name.     //
//  When run as an application, it uses the command line to determine       //
//  the server name.  The command line can be one of two formats:           //
//  c:\navgold\server\slapd-kennedy\config                                  //
//    or                                                                    //
//  slapd-kennedy	                                                        //
//  ......................................................................  //
//  server file "lib\base\servssl.c" was changed                            //
//  - added code to get password from WatchDog process                      //
//  ......................................................................  //
//  server file "httpd\src\ntmain.c" was changed                            //
//  - server always runs as an application                                  //
//  - changed hServerDoneEvent global name to "NS_service_name"             //
//    this was necessary so that WatchDog can trap the "service_name" event //
//  - above changes were also made in MultipleInstances()                   //
//  ......................................................................  //
//  server file "lib\libmessages\messages.mc" was changed                   //
//  - added a couple of extra messages for watchdog event logging           //
//  - watchdog is dependent on the server's eventlog source name            //
//  ......................................................................  //
//  Revision History:                                                       //
//  01-12-96  Initial Version, Andy Hakim (ahakim@netscape.com)             //
//  02-01-96  changed restart logic, now based on infant mortality time     //
//            instead of server exit code                                   //
//  07-10-96  Modified for Directory Server, pkennedy@netscape.com			//
//                                                                          //
//--------------------------------------------------------------------------//
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include "ntslapdmessages.h"           // event log msgs constants //
#include "regparms.h"                // product name, etc //
#include "ntwatchdog.h"
#include "version.h"
#include "ntresource.h"
#include "proto-ntutil.h"

#ifdef PUMPKIN_HOUR
#include <time.h>
#endif

//--------------------------------------------------------------------------//
// global variables 																			 //
//--------------------------------------------------------------------------//
SERVICE_STATUS_HANDLE gsshServiceStatus = 0L;
HWND ghWndMain = NULL;
HANDLE ghevWatchDogExit = NULL;
HINSTANCE ghInstance = NULL;
HANDLE ghdlgPassword = NULL;      // handle to password dialog window
HANDLE ghDuplicateProcess = NULL; // process handle with PROCESS_VM_READ access
HANDLE ghServerProcess = NULL;    // used by app window in TerminateProcess()
HANDLE ghServerThread0 = NULL;    // used by app window in Suspend/ResumeThread()
HANDLE ghWdogProcess = NULL;
char gszServerConfig[MAX_LINE];   // ex: c:\netscape\server\slapd-kennedy\config
char gszServerName[MAX_LINE];     // ex: slapd-kennedy
char gszServerRoot[MAX_LINE];     // ex: c:\netscape\server
char gszPassword[2048];
DWORD gdwServiceError = NO_ERROR; // return error code for service
DWORD gdwLastStatus = SERVICE_RUNNING;

//--------------------------------------------------------------------------//
// This is the shutdown handler we register via SetConsoleCtrlHandler()
// It is really the only guaranteed means we have of shutting down gracefully
// when the sytem is shutting down.  The Service Manager mechanism is not
// guaranteed to work.
//--------------------------------------------------------------------------//

BOOL WINAPI WD_ControlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_SHUTDOWN_EVENT) {
		SetEvent(ghevWatchDogExit);
		WaitForSingleObject(ghWdogProcess, 1000 * DEFAULT_KILL_TIME);
        return TRUE;
    }
    return FALSE;
}    


//--------------------------------------------------------------------------//
// calc szServerRoot given szServerName                                     //
//--------------------------------------------------------------------------//
BOOL WD_GetServerConfig(char *szServerId, char *szServerRoot, LPDWORD cbServerRoot)
{
	BOOL bReturn = FALSE;
	HANDLE hSlapdKey = 0;
	char szSlapdKey[MAX_PATH];
	DWORD dwValueType;
	DWORD dwResult = 0;

	// don't want to monitor Admin server
	if(strcmp(ADM_KEY_ROOT, szServerId) == 0)
		return(bReturn);

	// query registry key to figure out config directory
    _snprintf(szSlapdKey, sizeof(szSlapdKey), "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT,
        szServerId);
	szSlapdKey[sizeof(szSlapdKey)-1] = (char)0;

	dwResult = RegOpenKey(HKEY_LOCAL_MACHINE, szSlapdKey, &hSlapdKey);
	if(dwResult == ERROR_SUCCESS) 
	{
	 	dwResult = RegQueryValueEx(hSlapdKey, VALUE_CONFIG_PATH, NULL, 
			&dwValueType, (LPBYTE)szServerRoot, cbServerRoot);
		if(dwResult == ERROR_SUCCESS)
			bReturn = TRUE;
		RegCloseKey(hSlapdKey);
	}
	return(bReturn);
}



//--------------------------------------------------------------------------//
// get server's id based on index value that corresponds to the order in    //
// which it is listed under the the registry \SOFTWARE\Netscape\..          //
//--------------------------------------------------------------------------//
BOOL WD_GetServerId(IN DWORD dwSubKey, OUT char *szServerId, IN OUT LPDWORD cbServerId)
{
	BOOL bReturn = FALSE;
	static HANDLE hSlapdKey = 0;
	DWORD dwResult = ERROR_SUCCESS;
	FILETIME ftLastWrite;
	char szSlapdKey[MAX_LINE];

	if(dwSubKey == 0) {
    	_snprintf(szSlapdKey, sizeof(szSlapdKey), "%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT);
		szSlapdKey[sizeof(szSlapdKey)-1] = (char)0;
		dwResult = RegOpenKey(HKEY_LOCAL_MACHINE, szSlapdKey, 
			&hSlapdKey);
	}

	if(dwResult == ERROR_SUCCESS)
	{
		dwResult = RegEnumKeyEx(hSlapdKey, dwSubKey, szServerId, 
			cbServerId, NULL, NULL, NULL, &ftLastWrite);
		if(dwResult == ERROR_SUCCESS)
		{
			bReturn = TRUE;
		}
		else
		{
			RegCloseKey(hSlapdKey);
			hSlapdKey = 0;
		}
	}
	return(bReturn);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_IsServiceRunning(char *szServerId)
{
   BOOL bReturn = FALSE;
   SC_HANDLE hscManager;
   SC_HANDLE hscService;
   SERVICE_STATUS ssServiceStatus;

   if(hscManager = OpenSCManager(NULL, NULL, GENERIC_READ))
   {
      if(hscService = OpenService(hscManager, szServerId, SERVICE_QUERY_STATUS))
      {
         if(QueryServiceStatus(hscService, &ssServiceStatus))
         {
            if(ssServiceStatus.dwCurrentState != SERVICE_STOPPED)
            {
               bReturn = TRUE;
            }
         }
         CloseServiceHandle(hscService);
      }
      CloseServiceHandle(hscManager);
   }
	return(bReturn);
}





//--------------------------------------------------------------------------//
// get a list of installed servers                                          //
//--------------------------------------------------------------------------//
int WD_GetRunningServerCount(void)
{
	int nServerCount = 0;
	int nEnumIndex = 0;
	char szServerId[MAX_PATH];
	DWORD cbServerId = sizeof(szServerId);
	char szServerRoot[MAX_PATH];
	DWORD cbServerRoot = sizeof(szServerRoot);

	while(WD_GetServerId(nEnumIndex++, szServerId, &cbServerId))
	{
		cbServerId = sizeof(szServerId);
		// we have an entry that MIGHT be a server, but check to see if it really is one
		if(WD_GetServerConfig(szServerId, szServerRoot, &cbServerRoot))
      {
         if(WD_IsServiceRunning(szServerId))
			   nServerCount++;
      }
		cbServerRoot = sizeof(szServerRoot);
	}

	return(nServerCount);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
DWORD WD_GetDefaultKeyValue(char *szServerName, char *szKeyName, DWORD dwDefault)
{
	HANDLE hSlapdKey = 0;
	char szSlapdKey[MAX_LINE];
	DWORD dwValueType;
	DWORD dwValue = dwDefault;
	DWORD cbValue = sizeof(dwValue);

	// query registry key to figure out config directory
    _snprintf(szSlapdKey, sizeof(szSlapdKey), "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT,
        szServerName);
	szSlapdKey[sizeof(szSlapdKey)-1] = (char)0;
	if(RegOpenKey(HKEY_LOCAL_MACHINE, szSlapdKey, &hSlapdKey) == ERROR_SUCCESS)
	{
	 	RegQueryValueEx(hSlapdKey, szKeyName, NULL, &dwValueType, 
			(LPBYTE)&dwValue, &cbValue);
		RegCloseKey(hSlapdKey);
	}

	return(dwValue);
}


//--------------------------------------------------------------------------//
// figure out if we are running under Windows NT                            //
//--------------------------------------------------------------------------//
BOOL WD_IsWindowsNT(void)
{
	BOOL bReturn = FALSE;
	OSVERSIONINFO osVersionInfo;

	osVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if(GetVersionEx(&osVersionInfo))
	{
		bReturn = (osVersionInfo.dwPlatformId == VER_PLATFORM_WIN32_NT);
	}
	return(bReturn);
}



//--------------------------------------------------------------------------//
// figure out if we have enough physical memory to operate server           //
//--------------------------------------------------------------------------//
BOOL WD_IsEnoughResources(void)
{
   BOOL bReturn = TRUE;
   MEMORYSTATUS ms;
   DWORD dwMinRamFree = 0;
   DWORD dwMinRamTotal = DEFAULT_MINRAMTOTAL;
   DWORD dwMinRamPerServer = DEFAULT_MINRAMPERSERVER;

   dwMinRamFree = WD_GetDefaultKeyValue(gszServerName, MINRAMFREE_KEY, DEFAULT_MINRAMFREE);
   dwMinRamTotal = WD_GetDefaultKeyValue(gszServerName, MINRAMTOTAL_KEY, DEFAULT_MINRAMTOTAL);
   dwMinRamPerServer = WD_GetDefaultKeyValue(gszServerName, MINRAMPERSERVER_KEY, DEFAULT_MINRAMPERSERVER);

   ZeroMemory((PVOID)&ms, sizeof(ms));
   GlobalMemoryStatus(&ms);

   if((ms.dwTotalPhys < (dwMinRamTotal * 1024)) || (ms.dwAvailPhys < (dwMinRamFree * 1024)))
      bReturn = FALSE;

   if(ms.dwTotalPhys < (WD_GetRunningServerCount() * dwMinRamPerServer * 1024))
      bReturn = FALSE;

   return(bReturn);
}



//--------------------------------------------------------------------------//
// write error to EventLog service                                          //
//--------------------------------------------------------------------------//
BOOL WD_SysLog(WORD fwEventType, DWORD IDEvent, char *szData)
{
	BOOL bReturn = FALSE;
	HANDLE hEventSource;
	WORD     fwCategory = 0;	        // event category 
	PSID     pUserSid = NULL;	        // user security identifier (optional) 
	WORD     cStrings = 1;	            // number of strings to merge with message  
	DWORD    cbData = 0;	            // size of binary data, in bytes
	LPCTSTR  lpszStrings[64];	        // array of strings to merge with message 
	LPVOID   lpvData = 0; 	            // address of binary data 

	hEventSource = RegisterEventSource(NULL, TEXT(EVENTLOG_APPNAME));
	if(	hEventSource != NULL)
	{
		lpszStrings[0] = (LPCTSTR)gszServerName;
		if(szData != NULL)
		{
      		lpszStrings[1] = (LPCTSTR)szData;
			cStrings++;
		}
		
		bReturn = ReportEvent(hEventSource,	fwEventType, fwCategory,
                  IDEvent, pUserSid, cStrings, cbData,
                  lpszStrings, lpvData);
		DeregisterEventSource(hEventSource);
   }

   return(bReturn);
}



//--------------------------------------------------------------------------//
// converts '/' chars to '\'                                                //
//--------------------------------------------------------------------------//
void WD_UnixToDosPath(char *szText)
{
	if(szText)
   {
   	while(*szText)
   	{
   		if(*szText == '/')
   	
			*szText = '\\';
   		szText++;
   	}
   }
}



//--------------------------------------------------------------------------//
// calc szServerRoot given szServerConfig, and store szServerRoot in        //
// SLAPD_ROOT environment variable.                                      //
//--------------------------------------------------------------------------//
BOOL WD_GetServerRoot(char *szServerRoot, char *szServerConfig)
{
	char szTemp[MAX_LINE], szServerRootEnvVar[MAX_LINE];
	BOOL bReturn = FALSE;
	char *szChar = NULL;

	strncpy(szTemp, szServerConfig, sizeof(szTemp));
	szTemp[sizeof(szTemp)-1] = (char)0;
	// szTemp should be something like c:\navgold\server\slapd-kennedy\config
	if(szChar = strrchr(szTemp,'\\'))
	{
		*szChar = 0;
	   // szTemp should be c:\navgold\server\slapd-kennedy
		if(szChar = strrchr(szTemp, '\\'))
		{
			*szChar = 0;
		   // szTemp should be c:\navgold\server
			strncpy( szServerRoot, szTemp, sizeof(gszServerRoot) );
			szServerRoot[sizeof(gszServerRoot)-1] = (char)0;
			wsprintf(szServerRootEnvVar, "%s=%s", SLAPD_ROOT, szTemp);
			putenv(szServerRootEnvVar);
			bReturn = TRUE;
		}
	}
	return(bReturn);
}



//--------------------------------------------------------------------------//
// calc szServerConfig given szServerName                                    //
//--------------------------------------------------------------------------//
BOOL WD_GetConfigFromRegistry(char *szServerConfig, char *szServerName)
{
	BOOL bReturn = FALSE;
	HANDLE hSlapdKey = 0;
	char szSlapdKey[MAX_LINE];
	DWORD dwValueType;
	char szValueString[MAX_LINE];
	DWORD cbValueString = sizeof(szValueString);
	DWORD dwResult = 0;

	// query registry key to figure out config directory
    _snprintf(szSlapdKey, sizeof(szSlapdKey), "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT,
        szServerName);
	szSlapdKey[sizeof(szSlapdKey)-1] = (char)0;

	dwResult = RegOpenKey(HKEY_LOCAL_MACHINE, szSlapdKey, &hSlapdKey);
	if(dwResult != ERROR_SUCCESS) 
	{
		WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_REGISTRY, szSlapdKey);
		return(bReturn);
	}

 	dwResult = RegQueryValueEx(hSlapdKey, VALUE_CONFIG_PATH, NULL, 
				&dwValueType, szValueString, &cbValueString);
	if(dwResult != ERROR_SUCCESS)
	{
		WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_REGISTRY, szSlapdKey);
	} 
	else
	{
		strncpy(szServerConfig, szValueString, sizeof(gszServerConfig));
		szServerConfig[sizeof(gszServerConfig)-1] = (char)0;
		WD_UnixToDosPath(szServerConfig);
		WD_GetServerRoot(gszServerRoot, szServerConfig);
		bReturn = TRUE;
	}
	RegCloseKey(hSlapdKey);
	return(bReturn);
}


//--------------------------------------------------------------------------//
// calc szServerConfig and szServerName given szCmdLine                      //
//--------------------------------------------------------------------------//
BOOL WD_GetConfigFromCmdline(char *szServerConfig, char *szServerName, char *szCmdLine)
{
	BOOL bReturn = FALSE;
	char *szChar = NULL;

	if(!szCmdLine || !(strcmp(szCmdLine, "")) ) 
	{
		WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_BADCMDLINE, szCmdLine);
		return(bReturn);
	}

	strncpy(szServerConfig, szCmdLine, sizeof(gszServerConfig));
	szServerConfig[sizeof(gszServerConfig)-1] = (char)0;
	WD_UnixToDosPath(szCmdLine);
	WD_GetServerRoot(gszServerRoot, szCmdLine);

	// szCmdLine should be something like c:\navgold\server\slapd-kennedy\config
	if(szChar = strrchr(szCmdLine, '\\'))
	{
		*szChar = 0;
	   // szCmdLine should be c:\navgold\server\slapd-kennedy
		if(szChar = strrchr(szCmdLine, '\\'))
		{
			szChar++;
		   // szChar should point to slapd-kennedy
			strncpy(szServerName, szChar, sizeof(gszServerName));
			szServerName[sizeof(gszServerName)-1] = (char)0;
			WD_GetConfigFromRegistry(szServerConfig, szServerName);
			bReturn = TRUE;

		}
	}
	else
	{
		// szCmdLine should be something like slapd-kennedy
		strncpy(szServerName, szCmdLine, sizeof(gszServerName));
		szServerName[sizeof(gszServerName)-1] = (char)0;
		bReturn = WD_GetConfigFromRegistry(szServerConfig, szServerName);
	}

	if(strlen(szServerName) == 0)
	{
		WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_BADCMDLINE, szCmdLine);
		bReturn = FALSE;
	}

	return(bReturn);
}



//--------------------------------------------------------------------------//
// parse server config file to see if it security is enabled                //
//--------------------------------------------------------------------------//
BOOL WD_IsServerSecure(void)
{
	BOOL bReturn = FALSE;
	char szFileName[MAX_PATH];
	char szText[MAX_LINE];
	char szSeperators[] = " \t\n";
	char *szTemp;
	FILE *fh = NULL;
	
	_snprintf(szFileName, sizeof(szFileName), "%s\\%s", gszServerConfig, SLAPD_CONF);
	szFileName[sizeof(szFileName)-1] = (char)0;
	if(fh = fopen(szFileName, "r"))
	{
		while(!feof(fh))
		{
			if(fgets(szText, sizeof(szText), fh))
			{
				strlwr(szText);

				/* strtok() is not MT safe on Unix , but it is okay to call 
				   here because this file is NT only and strtok() is MT safe on NT */

				if(szTemp = strtok(szText, szSeperators))
				{
					if(strcmp(szTemp, "security") == 0)
					{
						if(szTemp = strtok(NULL, szSeperators))
						{
							if(strcmp(szTemp, "on") == 0)
								bReturn = TRUE;
						}
						break;
					}
				}
			}
		}
		fclose(fh);
	}

	return(bReturn);
}

//--------------------------------------------------------------------------//
// message proc window for app window                                       //
//--------------------------------------------------------------------------//
LONG APIENTRY WD_MainWndProc(HWND hWnd, UINT message, UINT wParam, LONG lParam)
{
   switch(message) 
   {
      case WM_CREATE:
         break;

      case WM_CLOSE:
			SetEvent(ghevWatchDogExit);
         break;

		case WM_COMMAND:
		{
			switch(LOWORD(wParam))
			{
				case ID_SERVER_SHUTDOWN:
				{
					HANDLE hevShutdown = NULL;
					char szShutdownEvent[MAX_LINE];

					// shutdown web server, it should exit with 0, WatchDog won't restart it
					_snprintf(szShutdownEvent, sizeof(szShutdownEvent), "NS_%s", gszServerName);
					szShutdownEvent[sizeof(szShutdownEvent)-1] = (char)0;
					hevShutdown = OpenEvent(EVENT_MODIFY_STATE, FALSE, szShutdownEvent);
					if(hevShutdown)
					{
						SetEvent(hevShutdown);  // try to exit gracefully
						CLOSEHANDLE(hevShutdown);
					}
					break;
				}

				case ID_SERVER_RESTART:
				{
					// shutdown web server, it should exit with 2, WatchDog will restart it
					if(ghServerProcess)
					{
						CLOSEHANDLE(ghServerProcess);
						TerminateProcess(ghServerProcess, 2);
					}
					break;
				}

				case ID_SERVER_SUSPEND:
				{
					if(ghServerThread0)
						SuspendThread(ghServerThread0);
					break;
				}
				
				case ID_SERVER_RESUME:
				{
					if(ghServerThread0)
						ResumeThread(ghServerThread0);
					break;
				}
				
				case ID_FILE_EXIT:
					PostMessage(hWnd, WM_CLOSE, 0, 0);
					break;
			}
			break;
		}

      default:
         return(DefWindowProc(hWnd, message, wParam, lParam));
   }
   return(0);
}



//--------------------------------------------------------------------------//
// This window serves as an IPC method with the server process.  It has     //
// pointers in it's storage area that the server uses to access the SSL     //
// password.  Quite strange, but it works perfectly well.                   //
//--------------------------------------------------------------------------//
HWND WD_CreateWindow()
{
	HWND hWndMain = NULL;
	WNDCLASS  wc;

	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)WD_MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = sizeof(LONG) * 4;
	wc.hIcon = LoadIcon(ghInstance, MAKEINTRESOURCE(IDI_LOGO));
	wc.hInstance = ghInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject(GRAY_BRUSH);
    wc.lpszMenuName =  MAKEINTRESOURCE(IDR_MENU);
    wc.lpszClassName = "slapd";

   RegisterClass(&wc);  // class may be registered if more than one instance

   hWndMain = CreateWindow(
        wc.lpszClassName,               /* See RegisterClass() call.          */
        gszServerName,                  /* Text for window title bar.         */
        WS_OVERLAPPEDWINDOW |           /* Window style.                      */
         WS_POPUP,                      /* Window style.                      */
        CW_USEDEFAULT,                  /* Default horizontal position.       */
        CW_USEDEFAULT,                  /* Default vertical position.         */
        320,                            /* Default width.                     */
        0,                              /* Default height.                    */
        NULL,                           /* Overlapped windows have no parent. */
        NULL,                           /* Use the window class menu.         */
        ghInstance,                     /* This instance owns this window.    */
        NULL                            /* Pointer not needed.                */
    );

	if(hWndMain)
	{
#ifdef SHOW_DEBUG_WINDOW
		ShowWindow(hWndMain, SW_SHOWDEFAULT);
#else
		ShowWindow(hWndMain, SW_HIDE);
#endif
		UpdateWindow(hWndMain);
	}
	return hWndMain;
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
void WD_WindowThreadProc(LPDWORD lpdwParam)
{
   HANDLE hevWindowCreated = (HANDLE)lpdwParam;
   MSG msg;

	// the ghWndMain global is used all over the place
	ghWndMain = WD_CreateWindow();
	
	// inform parent that window creation is complete because it is waiting on us
	SetEvent(hevWindowCreated);

   if(ghWndMain)
	{
	   while(GetMessage(&msg, ghWndMain, 0, 0) == TRUE)
	   {
	      TranslateMessage(&msg);		// Translates virtual key codes
	      DispatchMessage(&msg);		// Dispatches message to window
	   }
	}
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
void WD_PasswordThreadProc(LPDWORD lpdwParam)
{
	// app window must be created sometime during initialization
	if(ghWndMain)
	{
      	ZeroMemory(gszPassword, sizeof(gszPassword));
	SetWindowLong(ghWndMain, GWL_PASSWORD_ADDR, (LONG)gszPassword);
	SetWindowLong(ghWndMain, GWL_PASSWORD_LENGTH, (LONG)0);
	}
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_StartServer(PROCESS_INFORMATION *pi)
{
	BOOL bReturn = FALSE;
	char szCmdLine[MAX_LINE];
	char szServerPath[MAX_PATH];
	char szInstancePath [MAX_PATH];
	char *szChar;
	STARTUPINFO sui;
    DWORD fdwCreate = DETACHED_PROCESS;  /* flags for CreateProcess */
    int i;
    char *posfile;
    UNALIGNED long *posfhnd;
    
	if(!WD_IsEnoughResources())
    {
	    WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_STRING, MSG_RESOURCES);
       gdwServiceError = ERROR_SERVICE_NOT_ACTIVE;
       return(FALSE);
    }

	strncpy(szServerPath, gszServerConfig, sizeof(szServerPath));
	szServerPath[sizeof(szServerPath)-1] = (char)0;
	WD_UnixToDosPath(szServerPath);

	// szServerPath should now be something similar to
	//		c:\navgold\server\slapd-kennedy\config
	if(szChar = strrchr(szServerPath, '\\'))
	{
		*szChar = 0;
		strncpy (szInstancePath, szServerPath, sizeof(szInstancePath));
		szInstancePath[sizeof(szInstancePath)-1] = (char)0;
		if(szChar = strrchr(szServerPath, '\\'))
		{
			*szChar = 0;
		}
	}

	// For Directory Server, service-name is defined as slapd.exe, 
	// in ldapserver/include/nt/regpargms.h
	_snprintf( szCmdLine, sizeof(szCmdLine), "%s\\bin\\%s\\server\\%s -D \"%s\"", szServerPath, 
			 PRODUCT_NAME, SERVICE_EXE, szInstancePath );
	szCmdLine[sizeof(szCmdLine)-1] = (char)0;
	// szCmdLine ex: c:\navgold\server\bin\slapd\slapd.exe 
	//		-f c:\navgold\server\slapd-kennedy\config

	memset(&sui,0,sizeof(sui));
    sui.cb = sizeof(STARTUPINFO);

	/* All of this, to CreateProcess(), allows us to run a console
	   app (slapd.exe) from the service (ns-slapd.exe), without a 
	   new console being opened for the app. 
	   See dospawn.c in the crt src for more details.
	   */
    sui.cbReserved2 = (WORD)(sizeof( int ) + (3 *
                              (sizeof( char ) + sizeof( long ))));

    sui.lpReserved2 = calloc( sui.cbReserved2, 1 );

    *((UNALIGNED int *)(sui.lpReserved2)) = 3;

    posfile = (char *)(sui.lpReserved2 + sizeof( int ));

    posfhnd = (UNALIGNED long *)(sui.lpReserved2 + sizeof( int ) +
              (3 * sizeof( char )));

	for ( i = 0,
		  posfile = (char *)(sui.lpReserved2 + sizeof( int )),
		  posfhnd = (UNALIGNED long *)(sui.lpReserved2 + sizeof( int )
					+ (3 * sizeof( char ))) ;
		  i < 3 ;
		  i++, posfile++, posfhnd++ )
	{
		*posfile = 0;
		*posfhnd = (long)INVALID_HANDLE_VALUE;
	}

	fdwCreate |= CREATE_SUSPENDED;
	bReturn = CreateProcess(NULL, szCmdLine, NULL, NULL,
                   TRUE, fdwCreate, NULL, NULL, &sui, pi );
	if(bReturn)
	{
		ghServerProcess = pi->hProcess; // used by app window
		ghServerThread0 = pi->hThread; // used by app window
		if(DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), 
			pi->hProcess, (LPHANDLE)&ghDuplicateProcess, 
			(DWORD)PROCESS_VM_READ | PROCESS_VM_WRITE | 
			PROCESS_ALL_ACCESS, FALSE, (DWORD)0))
		{
			SetWindowLong(ghWndMain, GWL_PROCESS_HANDLE, 
					(LONG)ghDuplicateProcess);
		}
		ResumeThread(pi->hThread);
	}
	else
	{
		WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_STARTFAILED, szCmdLine);
	}
        
	free( sui.lpReserved2 );

	return(bReturn);
}




//------------------------------------------------------z--------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_CreatePasswordThread(void)
{
	#define NUM_WAIT_OBJECTS 2
	enum { CHILD_PROCESS, EXIT_EVENT };

	BOOL bReturn = FALSE;
	HANDLE lphObject[NUM_WAIT_OBJECTS];
	HANDLE hPasswordThread;
	DWORD dwThreadID;
	DWORD dwResult;

	lphObject[EXIT_EVENT] = ghevWatchDogExit;

    hPasswordThread = CreateThread((LPSECURITY_ATTRIBUTES)NULL, 0, 
										(LPTHREAD_START_ROUTINE)WD_PasswordThreadProc, NULL, 0, &dwThreadID);
	if(hPasswordThread)
	{
		lphObject[CHILD_PROCESS] = hPasswordThread;
		dwResult = WaitForMultipleObjects(NUM_WAIT_OBJECTS, lphObject, FALSE, INFINITE);
		CLOSEHANDLE(hPasswordThread);
		if(dwResult == WAIT_OBJECT_0 + EXIT_EVENT) // user stopped service
		{
			EndDialog(ghdlgPassword, 1);
		}
		bReturn = TRUE;
	}
	else
	{
		WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_BADPASSWORD, NULL);
	}
	return(bReturn);
}


//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_CreateWindowThread(void)
{
	BOOL bReturn = FALSE;
	DWORD dwThreadID;
	HANDLE hWindowThread;
	HANDLE hevWindowCreated = NULL;

	if(hevWindowCreated = CreateEvent(NULL, FALSE, FALSE, NULL))
	{
   	if(hWindowThread = CreateThread((LPSECURITY_ATTRIBUTES)NULL, 0, 
								 (LPTHREAD_START_ROUTINE)WD_WindowThreadProc, (LPVOID)hevWindowCreated, 0, &dwThreadID))
		{
			// make sure ghHwndMain is created otherwise
			// SetWindowLong(ghWndMain) will fail in other threads
			WaitForSingleObject(hevWindowCreated, INFINITE);
			CLOSEHANDLE(hWindowThread);
			bReturn = TRUE;
		}
	}
	return(bReturn);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_CreateCronThread(HANDLE hevWatchDogExit)
{
	BOOL bReturn = FALSE;
	DWORD dwThreadID = 0;
	HANDLE hWindowThread = NULL;
#if 0
	if(hWindowThread = CreateThread((LPSECURITY_ATTRIBUTES)NULL, 0, 
							 (LPTHREAD_START_ROUTINE)CRON_ThreadProc, (LPVOID)hevWatchDogExit, 0, &dwThreadID))
	{
		CLOSEHANDLE(hWindowThread);
		bReturn = TRUE;
	}
#endif
	return(bReturn);
}


//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_MonitorServer(void)
{
	#define NUM_WAIT_OBJECTS 2
	enum { SERVER_PROCESS, WATCHDOG_EXIT };

	BOOL bReturn = FALSE;
	HANDLE lphObject[NUM_WAIT_OBJECTS];
	DWORD dwResult = 0;
	DWORD dwExitCode = 0;
	PROCESS_INFORMATION pi;
	HANDLE hevServerDone = NULL;
	char szServerDoneEvent[MAX_LINE];
	char szText[MAX_LINE];
	DWORD dwTickCount = 0;

	lphObject[WATCHDOG_EXIT] = ghevWatchDogExit;

	while(WD_StartServer(&pi))
	{
		dwTickCount = GetTickCount();
		lphObject[SERVER_PROCESS] = pi.hProcess;
		dwResult = WaitForMultipleObjects(NUM_WAIT_OBJECTS, lphObject, FALSE, INFINITE);

      //WS_SendSNMPTrapSignal();

		if(dwResult == WAIT_OBJECT_0 + WATCHDOG_EXIT)
		{
			// shutdown web server
			//CLOSEHANDLE(pi.hProcess);  // XXXahakim close them after TerminateProcess()
			//CLOSEHANDLE(pi.hThread);
			_snprintf(szServerDoneEvent, sizeof(szServerDoneEvent), "NS_%s", gszServerName);
			szServerDoneEvent[sizeof(szServerDoneEvent)-1] = (char)0;
			hevServerDone = OpenEvent(EVENT_MODIFY_STATE, FALSE, szServerDoneEvent);
			if(hevServerDone)
			{
				SetEvent(hevServerDone);  // try to exit gracefully
				CLOSEHANDLE(hevServerDone);
				WaitForSingleObject(lphObject[SERVER_PROCESS], 1000 * DEFAULT_KILL_TIME);
			}
			// but just in case it's still alive, swat it again, harder!
			TerminateProcess(lphObject[SERVER_PROCESS], 1);
			CLOSEHANDLE(pi.hProcess);  // XXXahakim moved from above 03/06/96
			CLOSEHANDLE(pi.hThread);
			bReturn = TRUE;
		}
		else
		if(dwResult == WAIT_OBJECT_0 + SERVER_PROCESS)
		{
			// why did web server shutdown?
			// GetExitCodeProcess(lphObject[SERVER_PROCESS], &dwExitCode);
			// if(dwExitCode != 0)
			// checking the exit code is bogus because a crashed process can return
			// anything, including 0, so we use another method to determine if the
			// server shutdown legitimately, which is similar to how unix works
			// according to robm.

			// check to see if a specified amount of time has elapsed since the server
			// started.  If it's "infant mortality" don't bother restarting it
			// because chances are it will continue to fail (such as when the password
			// is bad, or if there is some other severe startup problem)
			if(GetTickCount() - dwTickCount > 1000 * WD_GetDefaultKeyValue(gszServerName, MORTALITY_KEY, DEFAULT_MORTALITY_TIME))
			{
				sprintf(szText, "%d", dwExitCode);
				WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_RESTART, szText);
				CLOSEHANDLE(pi.hProcess);
				CLOSEHANDLE(pi.hThread);
				CLOSEHANDLE(ghDuplicateProcess);
				Sleep(DEFAULT_RESTART_TIME * 1000);
            continue;
			}
			// server closed legitimately
			else  
				bReturn = TRUE;
		}
		CLOSEHANDLE(pi.hProcess);
		CLOSEHANDLE(pi.hThread);
		CLOSEHANDLE(ghDuplicateProcess);
		break;
	}

	return(bReturn);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
BOOL WD_SetServiceStatus(DWORD dwCurrentState, DWORD dwError)
{
	BOOL bReturn = FALSE;
	SERVICE_STATUS ssStatus;

	if(gsshServiceStatus)
	{
        gdwLastStatus = dwCurrentState;
		ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		ssStatus.dwCurrentState = dwCurrentState;
		ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | 
                                      SERVICE_ACCEPT_PAUSE_CONTINUE | 
                                      SERVICE_ACCEPT_SHUTDOWN;
		ssStatus.dwWin32ExitCode = dwError;
		ssStatus.dwServiceSpecificExitCode = (NO_ERROR ? 0 : 1);
		ssStatus.dwCheckPoint = 0;
		ssStatus.dwWaitHint = (1000 * ((dwCurrentState==SERVICE_STOP_PENDING)?600:DEFAULT_KILL_TIME + 1));
		bReturn = SetServiceStatus(gsshServiceStatus, &ssStatus);
	}
	return(FALSE);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
VOID WINAPI WD_ServiceHandler(DWORD fdwControl)
{
	switch(fdwControl)
	{
		case SERVICE_CONTROL_STOP:
		case SERVICE_CONTROL_SHUTDOWN:
			WD_SetServiceStatus(SERVICE_STOP_PENDING, gdwServiceError);
			SetEvent(ghevWatchDogExit);
			return;

		case SERVICE_CONTROL_PAUSE:
			if(ghServerThread0)
			{
				WD_SetServiceStatus(SERVICE_PAUSE_PENDING, gdwServiceError);
				SuspendThread(ghServerThread0);
				WD_SetServiceStatus(SERVICE_PAUSED, gdwServiceError);
				return;
			}
			break;

		case SERVICE_CONTROL_CONTINUE:
			if(ghServerThread0)
			{
				WD_SetServiceStatus(SERVICE_CONTINUE_PENDING, gdwServiceError);
				ResumeThread(ghServerThread0);
				WD_SetServiceStatus(SERVICE_RUNNING, gdwServiceError);
				return;
			}
			break;

		case SERVICE_CONTROL_INTERROGATE:
			WD_SetServiceStatus(gdwLastStatus, gdwServiceError);
			return;
            
        default:
			WD_SysLog(EVENTLOG_ERROR_TYPE, MSG_WD_RESTART, "unknown service event");
            return;
	}
	WD_SetServiceStatus(SERVICE_RUNNING, gdwServiceError);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
VOID WD_ServiceMain(DWORD dwArgc, LPTSTR  *lpszArgv)
{
	BOOL bOkToProceed = TRUE;
	// if SCM calls us lpszArgv will not be NULL
	BOOL bIsService = (lpszArgv != NULL);

    // register our custom control handler to handle shutdown
    ghWdogProcess = GetCurrentProcess();
    SetConsoleCtrlHandler(WD_ControlHandler, TRUE);
    
	if(bIsService)
	{
		gsshServiceStatus = RegisterServiceCtrlHandler(lpszArgv[0], 
									(LPHANDLER_FUNCTION)WD_ServiceHandler);
		bOkToProceed = (gsshServiceStatus != (SERVICE_STATUS_HANDLE)NULL);
		if(bOkToProceed)
		{
			strncpy(gszServerName, lpszArgv[0], sizeof(gszServerName));
			gszServerName[sizeof(gszServerName)-1] = (char)0;
			bOkToProceed = WD_GetConfigFromRegistry(gszServerConfig, 
								gszServerName);
		}
	}

	WD_SetServiceStatus(SERVICE_START_PENDING, gdwServiceError);

	if(bOkToProceed)
	{
		if(ghevWatchDogExit = CreateEvent(NULL, TRUE, FALSE, gszServerName))
		{
			WD_SetServiceStatus(SERVICE_RUNNING, gdwServiceError);
			WD_CreateWindowThread();
#if 0
			WD_CreateCronThread(ghevWatchDogExit);
#endif

			if(WD_IsServerSecure())
			{
				bOkToProceed = WD_CreatePasswordThread();
			}

			if(bOkToProceed)
			{
				WD_MonitorServer();
			}
			CLOSEHANDLE(ghevWatchDogExit);
		}
	}
	WD_SetServiceStatus(SERVICE_STOPPED, gdwServiceError);
}



//--------------------------------------------------------------------------//
//                                                                          //
//--------------------------------------------------------------------------//
WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
               int nCmdShow)
{
	SERVICE_TABLE_ENTRY steServiceTable[2];

#ifdef PUMPKIN_HOUR
    if(time(NULL) > (PUMPKIN_HOUR - 10)) 
	{
		char szMessage[256];
		sprintf( szMessage, " ** This beta software has expired **\n");
		MessageBox(GetDesktopWindow(), szMessage, 
			DS_NAME_FULL_VERSION, MB_ICONEXCLAMATION | MB_OK);
        exit(1);
    }
#endif

	if(!hPrevInstance)						  // other instances of app running?
	{                                
		ghInstance = hInstance;
		memset(gszPassword, 0, sizeof(gszPassword));
		memset(gszServerConfig, 0, sizeof(gszServerConfig));
		memset(gszServerName, 0, sizeof(gszServerName));
		if(WD_IsWindowsNT() && (lpCmdLine) && (strlen(lpCmdLine) == 0))
		{
		   // run as service
			steServiceTable[0].lpServiceName = TEXT(PRODUCT_NAME);
			steServiceTable[0].lpServiceProc = 
					(LPSERVICE_MAIN_FUNCTION)WD_ServiceMain;
			steServiceTable[1].lpServiceName = NULL;
			steServiceTable[1].lpServiceProc = NULL;
			StartServiceCtrlDispatcher(steServiceTable);
		}
		else
		{
			// run as application 
			if(WD_GetConfigFromCmdline(gszServerConfig, 
				gszServerName, lpCmdLine))
			{
				WD_ServiceMain(0, (LPTSTR *)NULL);
			}
		}
   }
   return(FALSE);
}
