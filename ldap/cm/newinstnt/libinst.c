/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "libinst.h"
#include <stdio.h>

//--------------------------------------------------------------------------//
// Use this instead of installer installer sdk stuff so window is hidden    //
//--------------------------------------------------------------------------//
DWORD _LaunchAndWait(char *szCommandLine, DWORD dwTimeout)
{
    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    DWORD dwExitCode = 0;

    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESHOWWINDOW;
    // si.wShowWindow = SW_HIDE;
    // show for debuggin for now
    si.dwFlags = SW_SHOW;

    if(CreateProcess(NULL, szCommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        if(WaitForSingleObject(pi.hProcess, dwTimeout) == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &dwExitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return(dwExitCode);
}


//////////////////////////////////////////////////////////////////////////////
// WriteSummaryStringRC
//
// write summary info string using resource
//
// returns number of bytes written by wsprintf 
//

int WriteSummaryStringRC(LPSTR psz, char *format, HINSTANCE hModule, UINT uStringID, char *value)
{

	char szTempString[MAX_STR_SIZE]={0};
	int nReturn= 0;

	LoadString( hModule, uStringID, szTempString, MAX_STR_SIZE);

	if(value)
	{
		nReturn = wsprintf(psz, format, szTempString, value);
	}else{
		nReturn = wsprintf(psz, format, szTempString);
	}
	
	return nReturn;
}

//////////////////////////////////////////////////////////////////////////////
// WriteSummaryStringRC
//
// write summary info integer using resource
//
// returns number of bytes written by wsprintf 
//

int WriteSummaryIntRC(LPSTR psz, char *format, HINSTANCE hModule, UINT uStringID, int value)
{

	char szTempString[MAX_STR_SIZE]={0};
	int nReturn = 0;
	

	LoadString( hModule, uStringID, szTempString, MAX_STR_SIZE);
	nReturn = wsprintf(psz, format, szTempString, value);
	
	return nReturn;
}

void
DSGetHostName(char *hostname, int bufsiz)
{
	char *setupHostname = setupGetHostName();
	if (setupHostname) {
		int len = strlen(setupHostname);
		if (len >= bufsiz)
			len = bufsiz - 1;
		strncpy(hostname, setupHostname, len);
		hostname[len] = 0;
		setupFree(setupHostname);
	} else {
		GetHostName(hostname, bufsiz);
	}
}

void
DSGetDefaultSuffix(char *suffix, const char *hostname)
{
	const char *SUF = "dc=";
	const int SUF_LEN = 3;
	char *sptr = suffix;
	const char *ptr = 0;

	if (!hostname) {
		sprintf(sptr, "%s%s", SUF, "unknown-suffix");
		return; /* bogus domain name */
	} else {
		ptr = strchr(hostname, '.'); /* skip to first . in hostname */
		if (!ptr) {
			sprintf(sptr, "%s%s", SUF, hostname);
			return; /* no domain name */
		}
		ptr++; /* skip to beginning of domain name */
	}

	*sptr = 0;
	strcat(sptr, SUF);
	sptr += SUF_LEN;
	for (; *ptr; ++ptr) {
		if (*ptr == '.') {
			strcat(sptr, ", ");
			sptr += 2;
			strcat(sptr, SUF);
			sptr += SUF_LEN;
		} else {
			*sptr++ = *ptr;
		}
	}
	*sptr = 0;

	return;
}
