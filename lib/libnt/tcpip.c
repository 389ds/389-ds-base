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
#include <windows.h>
#include "nt/ntos.h"

/*---------------------------------------------------------------------------*\
 *
 * Function:  GetServerDefaultHostName
 *
 *  Purpose:  This function gets the default host name
 *
 *    Input:
 *
 *  Returns:
 *
 * Comments:
\*---------------------------------------------------------------------------*/
DWORD NS_WINAPI
TCPIP_GetDefaultHostName( LPTSTR lpszFullHostName, LPTSTR lpszHostName, LPTSTR lpszDomainName )
{
    char * szKey;
    char * szName;
    DWORD dwValueType;
    DWORD dwIpHostSize = 256;
    char szIpHost[256];
    DWORD dwIpDomainSize = 256;
    char szIpDomain[256];
	BOOL bWinNT;

	/* get operating system */
    switch ( INFO_GetOperatingSystem() ) {
    case OS_WIN95: bWinNT = FALSE; break;
    case OS_WINNT: bWinNT = TRUE; break;
    default: return TCPIP_UNSUPPORTED_OS;
    }


#if 0
    int lastError;
    WSADATA WSAData;
    if ( WSAStartup( 0x0101, &WSAData ) != 0 ) {
        lastError = WSAGetLastError();
        m_pMainWnd->MessageBox ( "TCP/IP must be installed.\nUse the Network Icon in Control Panel" );
		return FALSE;
    }
    lastError = gethostname ( szIpHost, sizeof(szIpHost) );
#endif

	/* get list of all keys under Netscape */
	if ( bWinNT )
		szKey = "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
	else
		szKey = "SYSTEM\\CurrentControlSet\\Services\\Vxd\\MSTCP";

	if( !REG_CheckIfKeyExists( HKEY_LOCAL_MACHINE, szKey ) ) {
		return TCPIP_NO_TCPIP;
	}

	/* get host name for computer. May have to get DHCP host name if empty */
	szName = "Hostname";
	if( !REG_GetRegistryParameter( HKEY_LOCAL_MACHINE, szKey, szName, &dwValueType, (LPBYTE)szIpHost, &dwIpHostSize ) ) {
		szIpHost[0] = '\0';
	}

	/* get domain name for computer. May have to get DHCP host name if empty */
	szName = "Domain";
	if( !REG_GetRegistryParameter( HKEY_LOCAL_MACHINE, szKey, szName, &dwValueType, (LPBYTE)szIpDomain, &dwIpDomainSize ) ) {
		dwIpDomainSize = 0;
	}
	if ( dwIpDomainSize == 0 ) {
		szName = "DhcpDomain";
		if( !REG_GetRegistryParameter( HKEY_LOCAL_MACHINE, szKey, szName,  &dwValueType, (LPBYTE)szIpDomain, &dwIpDomainSize ) ) {
		    dwIpDomainSize = 0;
		}
	}

    if ( lpszHostName )
        strcpy ( lpszHostName, szIpHost );

    strcpy ( lpszFullHostName, szIpHost ); 
    if ( lpszDomainName ) {
    	if ( dwIpDomainSize == 0 )
    	    *lpszDomainName = '\0';
        else {   	     
            strcpy ( lpszDomainName, szIpDomain );
            strcat ( lpszFullHostName, "." );
            strcat ( lpszFullHostName, lpszDomainName );
        }
    }

    return TCPIP_NO_ERROR;
}
/*---------------------------------------------------------------------------*\
 *
 * Function:  TCPIP_VerifyHostName
 *
 *  Purpose:  This function validates the host name
 *
 *    Input:
 *
 *  Returns:
 *
 * Comments:
\*---------------------------------------------------------------------------*/
DWORD NS_WINAPI
TCPIP_VerifyHostName( LPCTSTR lpszHostName )
{
    struct hostent *ent;
    WSADATA wsd;
    int lastError;

    if(WSAStartup(MAKEWORD(1, 1), &wsd) != 0)
        return TCPIP_NO_WINSOCK_DLL;

    ent = gethostbyname ( lpszHostName );
    lastError = WSAGetLastError();
    WSACleanup();

    if ( ent == NULL ) {
        switch ( lastError ) {
        case WSANOTINITIALISED:     //	A successful WSAStartup must occur before using this function.
            break;            
        case WSAENETDOWN:           //	The Windows Sockets implementation has detected that the network subsystem has failed.
            return TCPIP_NETWORK_DOWN;
        case WSAHOST_NOT_FOUND:     //	Authoritative Answer Host not found.
            return TCPIP_HOST_NOT_FOUND;
        case WSATRY_AGAIN:          //	Non-Authoritative Host not found, or SERVERFAIL.
            return TCPIP_HOST_SERVER_DOWN;
        case WSANO_RECOVERY:        //	Nonrecoverable errors: FORMERR, REFUSED, NOTIMP.
            return TCPIP_NETWORK_ERROR;
        case WSANO_DATA:            //	Valid name, no data record of requested type.
            return TCPIP_HOST_VALID_NAME;
        case WSAEINPROGRESS:        //	A blocking Windows Sockets operation is in progress.
            return TCPIP_NETWORK_ERROR;
        case WSAEINTR:              //	The (blocking) call was canceled using 
            return TCPIP_NETWORK_ERROR;
        default:
            return TCPIP_NETWORK_ERROR;
        }
    }
    return TCPIP_NO_ERROR;
}
