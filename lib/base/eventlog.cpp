/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
//                                                                          //
//  Name: EVENTLOG                                                          //
//	Platforms: WIN32                                                        //
//  ......................................................................  //
//  Revision History:                                                       //
//  01-12-95  Initial Version, Aruna Victor (aruna@netscape.com)            //
//  12-02-96  Code cleanup, Andy Hakim (ahakim@netscape.com)                //
//            - consolidated admin and http functions into one              //
//            - moved registry modification code to installer               //
//            - removed several unecessary functions                        //
//            - changed function parameters to existing functions           //
//                                                                          //
//--------------------------------------------------------------------------//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "netsite.h"
#include "base/eventlog.h"
#include <nt/regparms.h>
#include <nt/messages.h>

HANDLE ghEventSource;

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC HANDLE InitializeLogging(char *szEventLogName)
{
    ghEventSource = RegisterEventSource(NULL, szEventLogName);
    return ghEventSource;
}



NSAPI_PUBLIC BOOL TerminateLogging(HANDLE hEventSource)
{
    BOOL bReturn = FALSE;
    if(hEventSource == NULL)
        hEventSource = ghEventSource;
    if(hEventSource)
        bReturn = DeregisterEventSource(hEventSource);
    return(bReturn);
}



NSAPI_PUBLIC BOOL LogErrorEvent(HANDLE hEventSource, WORD fwEventType, WORD fwCategory, DWORD IDEvent, LPTSTR chMsg, LPTSTR lpszMsg)
{
    BOOL bReturn = FALSE;
    LPTSTR lpszStrings[2];

	lpszStrings[0] = chMsg;
    lpszStrings[1] = lpszMsg;

    if(hEventSource == NULL)
        hEventSource = ghEventSource;

    if(hEventSource)
        bReturn = ReportEvent(hEventSource, fwEventType, fwCategory,
                        IDEvent, NULL, 2, 0, (LPCTSTR *)lpszStrings, NULL);
    return(bReturn);
}

NSPR_END_EXTERN_C
