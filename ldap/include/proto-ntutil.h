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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/******************************************************
 *
 *
 *  proto-ntutil.h - Prototypes for utility functions used
 *  throughout slapd on NT.
 *
 ******************************************************/
#if defined( _WINDOWS )

#ifndef _PROTO_NTUTIL
#define _PROTO_NTUTIL

#ifdef __cplusplus
extern "C" {
#endif

/* 
 *
 * ntreg.c  
 *
 */
extern int SlapdGetRegSZ( LPTSTR lpszRegKey, LPSTR lpszValueName, LPTSTR lpszValue );
extern void unixtodospath(char *szText);
extern void dostounixpath(char *szText);


/* 
 *
 * getopt.c  
 *
 */
extern int getopt (int argc, char *const *argv, const char *optstring);
extern char    *optarg;
/* 
 *
 * ntevent.c  
 *
 */
extern BOOL MultipleInstances();
extern BOOL SlapdIsAService();
extern void InitializeSlapdLogging( LPTSTR lpszRegLocation, LPTSTR lpszEventLogName, LPTSTR lpszMessageFile );
extern void ReportSlapdEvent(WORD wEventType, DWORD dwIdEvent, WORD wNumInsertStrings, 
						char *pszStrings);
extern BOOL ReportSlapdStatusToSCMgr(
					SERVICE_STATUS *serviceStatus,
					SERVICE_STATUS_HANDLE serviceStatusHandle,
					HANDLE Event,
					DWORD dwCurrentState,
                    DWORD dwWin32ExitCode,
                    DWORD dwCheckPoint,
                    DWORD dwWaitHint);
extern void WINAPI SlapdServiceCtrlHandler(DWORD dwOpcode);
extern BOOL SlapdGetServerNameFromCmdline(char *szServerName, char *szCmdLine, int dirname);

/* 
 *
 * ntgetpassword.c  
 *
 */
#ifdef NET_SSL
extern char *Slapd_GetPassword();
#ifdef FORTEZZA
extern char *Slapd_GetFortezzaPIN();
#endif
extern void CenterDialog(HWND hwndParent, HWND hwndDialog);
#endif /* NET_SSL */

#ifdef __cplusplus
}
#endif

#endif /* _PROTO_NTUTIL */

#endif /* _WINDOWS */
