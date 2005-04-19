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
/**********************************************************************
 *  ntOS.h - functionality used bt NT Operating System
 *
 **********************************************************************/

#ifndef _ntos_h
#define _ntos_h


#ifdef __cplusplus
extern "C" {            /* Assume C declarations for C++ */
#endif  /* __cplusplus */

#ifdef ISHIELD_DLL
#define NS_WINAPI WINAPI
#else
#define NS_WINAPI 
#endif

/* prototypes for info.c */
typedef enum {
    OS_WIN95,
    OS_WINNT,
    OS_WIN32S,
    OS_UNKNOWN
} OS_TYPE;

typedef enum {
    PROCESSOR_I386,
    PROCESSOR_ALPHA,
    PROCESSOR_MIPS,
    PROCESSOR_PPC,
    PROCESSOR_UNKNOWN
} PROCESSOR_TYPE;
    
OS_TYPE NS_WINAPI INFO_GetOperatingSystem (); 
DWORD NS_WINAPI INFO_GetOSMajorVersion (); 
DWORD NS_WINAPI INFO_GetOSMinorVersion (); 
void NS_WINAPI OS_GetComputerName  (LPTSTR computerName, int nComputerNameLength ); 
PROCESSOR_TYPE NS_WINAPI OS_GetProcessor (); 
DWORD NS_WINAPI INFO_GetOSServicePack (); 


/* prototypes for path.c */
DWORD NS_WINAPI PATH_RemoveRelative ( char * path );
DWORD NS_WINAPI PATH_ConvertNtSlashesToUnix( LPCTSTR  lpszNtPath, LPSTR lpszUnixPath );
DWORD NS_WINAPI PATH_GetNextFileInDirectory ( long hFile, char * path, char * lpFileName );
DWORD NS_WINAPI PATH_GetNextSubDirectory( long hFile, char * path, char * lpSubDirectoryName, char * lpSubDirectoryPrefix );
DWORD NS_WINAPI PATH_DeleteRecursively ( char * path );


/* prototypes for registry.c */
BOOL NS_WINAPI REG_CheckIfKeyExists( HKEY hKey, LPCTSTR registryKey );
BOOL NS_WINAPI REG_CreateKey( HKEY hKey, LPCTSTR registryKey );
BOOL NS_WINAPI REG_DeleteKey( HKEY hKey, LPCTSTR registryKey );
BOOL NS_WINAPI REG_DeleteValue( HKEY hKey, LPCTSTR registryKey, LPCSTR valueName );
		 	
BOOL NS_WINAPI 
REG_GetRegistryParameter(
    HKEY hKey, 
	LPCTSTR registryKey, 
	LPTSTR QueryValueName,
	LPDWORD ValueType,
	LPBYTE ValueBuffer, 
	LPDWORD ValueBufferSize
	);
		 	
BOOL NS_WINAPI 
REG_SetRegistryParameter(
    HKEY hKey, 
	LPCTSTR registryKey, 
	LPTSTR valueName,
	DWORD valueType,
	LPCTSTR ValueString, 
	DWORD valueStringLength
	);

BOOL NS_WINAPI 
REG_GetSubKeysInfo( 
    HKEY hKey, 
    LPCTSTR registryKey, 
    LPDWORD lpdwNumberOfSubKeys, 
    LPDWORD lpdwMaxSubKeyLength 
    );

BOOL NS_WINAPI 
REG_GetSubKey( HKEY hKey, 
    LPCTSTR registryKey, 
    DWORD nSubKeyIndex, 
    LPTSTR registrySubKeyBuffer, 
    DWORD subKeyBufferSize 
    );

/* prototypes for service.c */
#define SERVRET_ERROR     0
#define SERVRET_INSTALLED 1
#define SERVRET_STARTING  2
#define SERVRET_STARTED   3
#define SERVRET_STOPPING  4
#define SERVRET_REMOVED   5

DWORD NS_WINAPI SERVICE_GetNTServiceStatus(LPCTSTR szServiceName, LPDWORD lpLastError );
DWORD NS_WINAPI SERVICE_InstallNTService(LPCTSTR szServiceName, LPCTSTR szServiceDisplayName, LPCTSTR szServiceExe );
DWORD NS_WINAPI SERVICE_ReinstallNTService(LPCTSTR szServiceName, LPCTSTR szServiceDisplayName, LPCTSTR szServiceExe );
DWORD NS_WINAPI SERVICE_RemoveNTService(LPCTSTR szServiceName);
DWORD NS_WINAPI SERVICE_StartNTService(LPCTSTR szServiceName);
DWORD NS_WINAPI SERVICE_StartNTServiceAndWait(LPCTSTR szServiceName, LPDWORD lpdwLastError);
DWORD NS_WINAPI SERVICE_StopNTService(LPCTSTR szServiceName);
DWORD NS_WINAPI SERVICE_StopNTServiceAndWait(LPCTSTR szServiceName, LPDWORD lpdwLastError);


/* prototypes for pmddeml.c */
DWORD PMDDEML_Open ( void );
BOOL PMDDEML_Close ( DWORD idInst );
BOOL PMDDEML_CreateProgramManagerGroup ( DWORD idInst, LPCTSTR lpszGroupName );
BOOL PMDDEML_DeleteProgramManagerGroup ( DWORD idInst, LPCTSTR lpszGroupName );
BOOL PMDDEML_ShowProgramManagerGroup ( DWORD idInst, LPCTSTR lpszGroupName );
BOOL PMDDEML_AddIconToProgramManagerGroup ( DWORD idInst, LPCTSTR lpszCmdLine,
                 LPCTSTR lpszTitle, LPCTSTR lpszIconPath, LPCTSTR lpszWorkingDir,
                 BOOL bReplace );
BOOL PMDDEML_CreateProgramManagerCommonGroup ( DWORD idInst,
											   LPCTSTR lpszGroupName );
BOOL PMDDEML_DeleteProgramManagerCommonGroup ( DWORD idInst,
											   LPCTSTR lpszGroupName );
BOOL PMDDEML_ShowProgramManagerCommonGroup ( DWORD idInst,
											 LPCTSTR lpszGroupName );
BOOL PMDDEML_DeleteIconInProgramManagerGroup ( DWORD idInst, LPCTSTR lpszTitle );
BOOL PMDDEML_GetProgramGroupInfo(DWORD idInst, LPSTR lpProgramGroup, char *szBuffer, DWORD cbBuffer);

/* prototypes for tcpip.c */
#define TCPIP_NO_ERROR     		0
#define TCPIP_UNSUPPORTED_OS	1
#define TCPIP_NO_WINSOCK_DLL	2
#define TCPIP_NO_TCPIP          3
#define TCPIP_NETWORK_DOWN      4   /*	The Windows Sockets implementation has detected that the network subsystem has failed. */
#define TCPIP_NETWORK_ERROR     5
#define TCPIP_HOST_NOT_FOUND    6   /*	Authoritative Answer Host not found. */
#define TCPIP_HOST_SERVER_DOWN  7   /*	Non-Authoritative Host not found, or SERVERFAIL */
#define TCPIP_HOST_VALID_NAME   8  /*	Valid name, no data record of requested type. */

DWORD NS_WINAPI
TCPIP_GetDefaultHostName( LPTSTR lpszFullHostName, LPTSTR lpszHostName, LPTSTR lpszDomainName );
DWORD NS_WINAPI TCPIP_VerifyHostName( LPCTSTR lpszHostName );


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif
