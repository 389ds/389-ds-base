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
