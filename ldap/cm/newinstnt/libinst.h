/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
//////////////////////////////////////////////////////////////////////////////
// libinst.h - Netscape SuiteSpot Installation Plug-In Directory Server
//
//
// routines common to each component install dll
#ifndef __LIBINST_H
#define __LIBINST_H

#include <windows.h>
#include <nssetup.h>

#define MAX_STR_SIZE  512

DWORD _LaunchAndWait(char *szCommandLine, DWORD dwTimeout);
int WriteSummaryStringRC(LPSTR lpsz, char *format, HINSTANCE hModule, UINT uStringID, char *value);
int WriteSummaryIntRC(LPSTR lpsz, char *format, HINSTANCE hModule, UINT uStringID, int value);
int DSMessageBox(UINT type, UINT titleKey, UINT msgKey, const char *titlearg, ...);
int DSMessageBoxOK(UINT titleKey, UINT msgKey, const char *titlearg, ...);
void DSGetHostName(char *hostname, int bufsiz);
void DSGetDefaultSuffix(char *suffix, const char *hostname);
#endif
