/***********************************************************************
**
** Copyright 1996 - Netscape Communications Corporation
**
** NAME
**  NTService.h
**
** DESCRIPTION
**
**
** AUTHOR
**   Rob Weltman <rweltman@netscape.com>
**
***********************************************************************/

#ifndef _NTSERVICE_H_
#define _NTSERVICE_H_

// Added:  2-8-2005
#include <windows.h>
#include "subuniutil.h"
// End Change

// #include "dssynchmsg.h" // Event message ids

#define SERVICE_CONTROL_USER 128

class CNTService
{
public:
    CNTService(const TCHAR* szServiceName);
    virtual ~CNTService();
    BOOL ParseStandardArgs(int argc, char* argv[]);
    BOOL IsInstalled();
    BOOL Install();
    BOOL Uninstall();
    void LogEvent(WORD wType, DWORD dwID,
                  const wchar_t* pszS1 = NULL,
                  const wchar_t* pszS2 = NULL,
                  const wchar_t* pszS3 = NULL);
    void LogEvent(WORD wType, DWORD dwID,
                  const char* pszS1,
                  const char* pszS2 = NULL,
                  const char* pszS3 = NULL);
    BOOL StartService();
    BOOL StartServiceDirect();
    void SetStatus(DWORD dwState);
    BOOL Initialize();
    virtual void Run();
	virtual BOOL OnInit();
    virtual void OnStop();
    virtual void OnInterrogate();
    virtual void OnPause();
    virtual void OnContinue();
    virtual void OnShutdown();
    virtual BOOL OnUserControl(DWORD dwOpcode);
	virtual TCHAR *GetEventName() { return m_szServiceName; }
    void DebugMsg(const TCHAR* pszFormat, ...);
    
    // static member functions
    static void WINAPI ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv);
    static void WINAPI Handler(DWORD dwOpcode);

    // data members
    TCHAR m_szServiceName[64];
    int m_iMajorVersion;
    int m_iMinorVersion;
    SERVICE_STATUS_HANDLE m_hServiceStatus;
    SERVICE_STATUS m_Status;
    BOOL IsRunning() { return m_bIsRunning; }

    // static data
    static CNTService* m_pThis; // nasty hack to get object ptr

private:
    HANDLE m_hEventSource;
    BOOL m_bIsRunning;
};

#endif // _NTSERVICE_H_
