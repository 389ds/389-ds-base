/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
