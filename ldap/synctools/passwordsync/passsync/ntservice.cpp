/***********************************************************************
**
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
/*
**
** NAME
**  NTService.cpp
**
** DESCRIPTION
**  Base class for NT Service app
**
** AUTHOR
**   Rob Weltman <rweltman@netscape.com>
**
***********************************************************************/

/***********************************************************************
** Includes
***********************************************************************/
// Removed: 2-8-2005
//#include "sysplat.h"
// Added: 2-8-2005
#include <stdio.h>
// End Change

#include <tchar.h>
#include <time.h>
#include "NTService.h"
// Remove: 2-8-2005
//#include "uniutil.h"
// End Change
#include "dssynchmsg.h"

// static variables
CNTService* CNTService::m_pThis = NULL;

CNTService::CNTService(const TCHAR* szServiceName)
{
    // copy the address of the current object so we can access it from
    // the static member callback functions. 
    // WARNING: This limits the application to only one CNTService object. 
    m_pThis = this;
    
    // Set the default service name and version
    _tcsncpy(m_szServiceName, szServiceName, sizeof(m_szServiceName)-1);
    m_iMajorVersion = 1;
    m_iMinorVersion = 0;
    m_hEventSource = NULL;

    // set up the initial service status 
    m_hServiceStatus = NULL;
    m_Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    m_Status.dwCurrentState = SERVICE_STOPPED;
    m_Status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    m_Status.dwWin32ExitCode = 0;
    m_Status.dwServiceSpecificExitCode = 0;
    m_Status.dwCheckPoint = 0;
    m_Status.dwWaitHint = 0;
    m_bIsRunning = FALSE;
}

CNTService::~CNTService()
{
    DebugMsg(_T("CNTService::~CNTService()"));
    if (m_hEventSource) {
        ::DeregisterEventSource(m_hEventSource);
		m_hEventSource = NULL;
    }
}

////////////////////////////////////////////////////////////////////////////////////////
// Default command line argument parsing

// Returns TRUE if it found an arg it recognised, FALSE if not
// Note: processing some arguments causes output to stdout to be generated.
BOOL CNTService::ParseStandardArgs(int argc, char* argv[])
{
    // See if we have any command line args we recognise
    if (argc <= 1) return FALSE;

    if (_stricmp(argv[1], "-v") == 0) {

        // Spit out version info
        _tprintf(_T("%s Version %d.%d\n"),
               m_szServiceName, m_iMajorVersion, m_iMinorVersion);
        _tprintf(_T("The service is %s installed\n"),
               IsInstalled() ? _T("currently") : _T("not"));
        return TRUE; // say we processed the argument

    } else if (_stricmp(argv[1], "-i") == 0) {

        // Request to install.
        if (IsInstalled()) {
            _tprintf(_T("%s is already installed\n"), m_szServiceName);
        } else {
            // Try and install the copy that's running
            if (Install()) {
                _tprintf(_T("%s installed\n"), m_szServiceName);
            } else {
                _tprintf(_T("%s failed to install. Error %d\n"),
					m_szServiceName, GetLastError());
            }
        }
        return TRUE; // say we processed the argument

    } else if (_stricmp(argv[1], "-u") == 0) {

        // Request to uninstall.
        if (!IsInstalled()) {
            _tprintf(_T("%s is not installed\n"), m_szServiceName);
        } else {
            // Try and remove the copy that's installed
            if (Uninstall()) {
                // Get the executable file path
                TCHAR szFilePath[_MAX_PATH];
                ::GetModuleFileName(NULL, szFilePath, sizeof(szFilePath));
                _tprintf(_T("%s removed. (You must delete the file (%s) yourself.)\n"),
                       m_szServiceName, szFilePath);
            } else {
                _tprintf(_T("Could not remove %s. Error %d\n"),
					m_szServiceName, GetLastError());
            }
        }
        return TRUE; // say we processed the argument
    
    }
         
    // Don't recognise the args
    return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////
// Install/uninstall routines

// Test if the service is currently installed
BOOL CNTService::IsInstalled()
{
    BOOL bResult = FALSE;

    // Open the Service Control Manager
    SC_HANDLE hSCM = ::OpenSCManager(NULL, // local machine
                                     NULL, // ServicesActive database
                                     SC_MANAGER_ALL_ACCESS); // full access
    if (hSCM) {

        // Try to open the service
        SC_HANDLE hService = ::OpenService(hSCM,
                                           m_szServiceName,
                                           SERVICE_QUERY_CONFIG);
        if (hService) {
            bResult = TRUE;
            ::CloseServiceHandle(hService);
        }

        ::CloseServiceHandle(hSCM);
    }
    
    return bResult;
}

BOOL CNTService::Install()
{
    // Open the Service Control Manager
    SC_HANDLE hSCM = ::OpenSCManager(NULL, // local machine
                                     NULL, // ServicesActive database
                                     SC_MANAGER_ALL_ACCESS); // full access
    if (!hSCM) return FALSE;

    // Get the executable file path
    TCHAR szFilePath[_MAX_PATH];
    ::GetModuleFileName(NULL, szFilePath, sizeof(szFilePath)/sizeof(*szFilePath));

    // Create the service
    SC_HANDLE hService = ::CreateService(hSCM,
                                         m_szServiceName,
                                         m_szServiceName,
                                         SERVICE_ALL_ACCESS,
                                         SERVICE_WIN32_OWN_PROCESS,
                                         SERVICE_DEMAND_START,        // start condition
                                         SERVICE_ERROR_NORMAL,
                                         szFilePath,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL);
    if (!hService) {
        ::CloseServiceHandle(hSCM);
        return FALSE;
    }

    // make registry entries to support logging messages
    // Add the source name as a subkey under the Application
    // key in the EventLog service portion of the registry.
    TCHAR szKey[256];
    HKEY hKey = NULL;
    _tcscpy(szKey, _T("SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\"));
    _tcscat(szKey, GetEventName());
    if (::RegCreateKey(HKEY_LOCAL_MACHINE, szKey, &hKey) != ERROR_SUCCESS) {
        ::CloseServiceHandle(hService);
        ::CloseServiceHandle(hSCM);
        return FALSE;
    }

    // Add the Event ID message-file name to the 'EventMessageFile' subkey.
    ::RegSetValueEx(hKey,
                    _T("EventMessageFile"),
                    0,
                    REG_EXPAND_SZ, 
                    (CONST BYTE*)szFilePath,
                    (_tcslen(szFilePath) + 1)*sizeof(*szFilePath));     

    // Set the supported types flags.
    DWORD dwData = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
    ::RegSetValueEx(hKey,
                    _T("TypesSupported"),
                    0,
                    REG_DWORD,
                    (CONST BYTE*)&dwData,
                     sizeof(DWORD));
    ::RegCloseKey(hKey);

    LogEvent(EVENTLOG_INFORMATION_TYPE, EVMSG_INSTALLED, m_szServiceName);

    // tidy up
    ::CloseServiceHandle(hService);
    ::CloseServiceHandle(hSCM);
    return TRUE;
}

BOOL CNTService::Uninstall()
{
    // Open the Service Control Manager
    SC_HANDLE hSCM = ::OpenSCManager(NULL, // local machine
                                     NULL, // ServicesActive database
                                     SC_MANAGER_ALL_ACCESS); // full access
    if (!hSCM) return FALSE;

    BOOL bResult = FALSE;
    SC_HANDLE hService = ::OpenService(hSCM,
                                       m_szServiceName,
                                       DELETE);
    if (hService) {
		// Stop it if it is running
		SERVICE_STATUS serviceStatus;
		BOOL bStop = ControlService( hService, SERVICE_CONTROL_STOP,
					&serviceStatus );
        if (::DeleteService(hService)) {
            LogEvent(EVENTLOG_INFORMATION_TYPE, EVMSG_REMOVED, m_szServiceName);
            bResult = TRUE;
        } else {
            LogEvent(EVENTLOG_ERROR_TYPE, EVMSG_NOTREMOVED, m_szServiceName);
        }
        ::CloseServiceHandle(hService);
    }
    
    ::CloseServiceHandle(hSCM);
    return bResult;
}

///////////////////////////////////////////////////////////////////////////////////////
// Logging functions

// This function makes an entry into the application event log
void CNTService::LogEvent(WORD wType, DWORD dwID,
                          const wchar_t* pszS1,
                          const wchar_t* pszS2,
                          const wchar_t* pszS3)
{
#ifndef _DEBUG
	if ( EVMSG_DEBUG == dwID )
		return;
#endif
    const wchar_t* ps[3];
    ps[0] = pszS1;
    ps[1] = pszS2;
    ps[2] = pszS3;

    int iStr = 0;
    for (int i = 0; i < 3; i++) {
        if (ps[i] != NULL) iStr++;
    }
        
    // Check the event source has been registered and if
    // not then register it now
    if (!m_hEventSource) {
		TCHAR *name = GetEventName();
// Modification: 2-8-2005
//        m_hEventSource = ::RegisterEventSourceW(NULL,  // local machine
//                                               GetEventName()); // source name
        m_hEventSource = ::RegisterEventSource(NULL,  // local machine
                                               GetEventName()); // source name
// End Change
    }

    if (m_hEventSource) {
        ::ReportEventW(m_hEventSource,
                      wType,
                      0,
                      dwID,
                      NULL, // sid
                      iStr,
                      0,
                      ps,
                      NULL);
    }
}

// This function makes an entry into the application event log
void CNTService::LogEvent(WORD wType, DWORD dwID,
                          const char* pszS1,
                          const char* pszS2,
                          const char* pszS3)
{
	wchar_t *p1 = pszS1 ? StrToUnicode( pszS1 ) : NULL;
	wchar_t *p2 = pszS2 ? StrToUnicode( pszS2 ) : NULL;
	wchar_t *p3 = pszS3 ? StrToUnicode( pszS3 ) : NULL;
	LogEvent( wType, dwID, p1, p2, p3 );
	if ( p1 )
		free( p1 );
	if ( p2 )
		free( p2 );
	if ( p3 )
		free( p3 );
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Service startup and registration

BOOL CNTService::StartService()
{
    CNTService* pService = m_pThis;
    SERVICE_TABLE_ENTRY st[] = {
        {m_szServiceName, ServiceMain},
        {NULL, NULL}
    };

    DebugMsg(_T("Calling StartServiceCtrlDispatcher()"));
	// Fails if started from command line, but StartService
	// works any way
    BOOL b = ::StartServiceCtrlDispatcher(st);
	DWORD err = GetLastError();
    DebugMsg(_T("Returned from StartServiceCtrlDispatcher()"));
    return b;
}

BOOL CNTService::StartServiceDirect()
{
	BOOL b = FALSE;

	// Open the Service Control Manager
	SC_HANDLE hSCM = ::OpenSCManager(NULL, // local machine
                                 NULL, // ServicesActive database
                                 SC_MANAGER_ALL_ACCESS); // full access
	if (!hSCM) return FALSE;
	SC_HANDLE hService = ::OpenService(hSCM,
                                   m_szServiceName,
                                   SERVICE_START);
	if (hService)
	{
		DebugMsg(_T("Calling StartServiceDirect()"));
		b = ::StartService( hService, 0, NULL );
		::CloseServiceHandle(hService);
	}
	::CloseServiceHandle(hSCM);

	return b;
}

// static member function (callback)
void CNTService::ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    // Get a pointer to the C++ object
    CNTService* pService = m_pThis;
    
    pService->DebugMsg(_T("Entering CNTService::ServiceMain()"));
    // Register the control request handler
    pService->m_Status.dwCurrentState = SERVICE_START_PENDING;
    pService->m_hServiceStatus = RegisterServiceCtrlHandler(pService->m_szServiceName,
                                                           Handler);
    if (pService->m_hServiceStatus == NULL) {
        pService->LogEvent(EVENTLOG_ERROR_TYPE, EVMSG_CTRLHANDLERNOTINSTALLED);
        return;
    }

    // Start the initialisation
    if (pService->Initialize()) {

        // Do the real work. 
        // When the Run function returns, the service has stopped.
        pService->m_bIsRunning = TRUE;
        pService->m_Status.dwWin32ExitCode = 0;
        pService->m_Status.dwCheckPoint = 0;
        pService->m_Status.dwWaitHint = 0;
        pService->Run();
    }

    // Tell the service manager we are stopped
    pService->SetStatus(SERVICE_STOPPED);

    pService->DebugMsg(_T("Leaving CNTService::ServiceMain()"));
}

///////////////////////////////////////////////////////////////////////////////////////////
// status functions

void CNTService::SetStatus(DWORD dwState)
{
    DebugMsg(_T("CNTService::SetStatus(%lu, %lu)"), m_hServiceStatus, dwState);
    m_Status.dwCurrentState = dwState;
    ::SetServiceStatus(m_hServiceStatus, &m_Status);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Service initialization

BOOL CNTService::Initialize()
{
    DebugMsg(_T("Entering CNTService::Initialize()"));

    // Start the initialization
    SetStatus(SERVICE_START_PENDING);
    
    // Perform the actual initialization
    BOOL bResult = OnInit(); 
    
    // Set final state
    m_Status.dwWin32ExitCode = GetLastError();
    m_Status.dwCheckPoint = 0;
    m_Status.dwWaitHint = 0;
    if (!bResult) {
        LogEvent(EVENTLOG_ERROR_TYPE, EVMSG_FAILEDINIT);
        SetStatus(SERVICE_STOPPED);
        return FALSE;    
    }
    
    LogEvent(EVENTLOG_INFORMATION_TYPE, EVMSG_STARTED);
    SetStatus(SERVICE_RUNNING);

    DebugMsg(_T("Leaving CNTService::Initialize()"));
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////////////////////
// main function to do the real work of the service

// This function performs the main work of the service. 
// When this function returns the service has stopped.
void CNTService::Run()
{
    DebugMsg(_T("Entering CNTService::Run()"));

    while (m_bIsRunning) {
        DebugMsg(_T("Sleeping..."));
        Sleep(5000);
    }

    // nothing more to do
    DebugMsg(_T("Leaving CNTService::Run()"));
}

//////////////////////////////////////////////////////////////////////////////////////
// Control request handlers

// static member function (callback) to handle commands from the
// service control manager
void CNTService::Handler(DWORD dwOpcode)
{
    // Get a pointer to the object
    CNTService* pService = m_pThis;
    
    pService->DebugMsg(_T("CNTService::Handler(%lu)"), dwOpcode);
    switch (dwOpcode) {
    case SERVICE_CONTROL_STOP: // 1
        pService->SetStatus(SERVICE_STOP_PENDING);
        pService->OnStop();
        pService->m_bIsRunning = FALSE;
        pService->LogEvent(EVENTLOG_INFORMATION_TYPE, EVMSG_STOPPED);
		if (pService->m_hEventSource) {
			::DeregisterEventSource(pService->m_hEventSource);
			pService->m_hEventSource = NULL;
		}

        break;

    case SERVICE_CONTROL_PAUSE: // 2
        pService->OnPause();
        break;

    case SERVICE_CONTROL_CONTINUE: // 3
        pService->OnContinue();
        break;

    case SERVICE_CONTROL_INTERROGATE: // 4
        pService->OnInterrogate();
        break;

    case SERVICE_CONTROL_SHUTDOWN: // 5
        pService->OnShutdown();
        break;

    default:
        if (dwOpcode >= SERVICE_CONTROL_USER) {
            if (!pService->OnUserControl(dwOpcode)) {
                pService->LogEvent(EVENTLOG_ERROR_TYPE, EVMSG_BADREQUEST);
            }
        } else {
            pService->LogEvent(EVENTLOG_ERROR_TYPE, EVMSG_BADREQUEST);
        }
        break;
    }

    // Report current status
    pService->DebugMsg(_T("Updating status (%lu, %lu)"),
                       pService->m_hServiceStatus,
                       pService->m_Status.dwCurrentState);
    ::SetServiceStatus(pService->m_hServiceStatus, &pService->m_Status);
}
        
// Called when the service is first initialized
BOOL CNTService::OnInit()
{
    DebugMsg(_T("CNTService::OnInit()"));
	return TRUE;
}

// Called when the service control manager wants to stop the service
void CNTService::OnStop()
{
    DebugMsg(_T("CNTService::OnStop()"));
}

// called when the service is interrogated
void CNTService::OnInterrogate()
{
    DebugMsg(_T("CNTService::OnInterrogate()"));
}

// called when the service is paused
void CNTService::OnPause()
{
    DebugMsg(_T("CNTService::OnPause()"));
}

// called when the service is continued
void CNTService::OnContinue()
{
    DebugMsg(_T("CNTService::OnContinue()"));
}

// called when the service is shut down
void CNTService::OnShutdown()
{
    DebugMsg(_T("CNTService::OnShutdown()"));
}

// called when the service gets a user control message
BOOL CNTService::OnUserControl(DWORD dwOpcode)
{
    DebugMsg(_T("CNTService::OnUserControl(%8.8lXH)"), dwOpcode);
    return FALSE; // say not handled
}

////////////////////////////////////////////////////////////////////////////////////////////
// Debugging support

void CNTService::DebugMsg(const TCHAR* pszFormat, ...)
{
    TCHAR buf[1024];
    _stprintf(buf, _T("[%s](%lu): "), m_szServiceName, GetCurrentThreadId());
	va_list arglist;
	va_start(arglist, pszFormat);
    _vstprintf(&buf[_tcslen(buf)], pszFormat, arglist);
	va_end(arglist);
    _tcscat(buf, _T("\n"));
    OutputDebugString(buf);
}
