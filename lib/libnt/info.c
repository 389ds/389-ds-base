/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <windows.h>
#include "nt/ntos.h"

OS_TYPE NS_WINAPI INFO_GetOperatingSystem () 
{
	OSVERSIONINFO versionInfo;
	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx( &versionInfo );

	switch ( versionInfo.dwPlatformId ) {
	case VER_PLATFORM_WIN32s:
		return OS_WIN32S;
	case VER_PLATFORM_WIN32_WINDOWS:
		return OS_WIN95;
	case VER_PLATFORM_WIN32_NT:
		return OS_WINNT;
	default:
		break;
	} 
    return OS_UNKNOWN;
}
DWORD NS_WINAPI INFO_GetOSMajorVersion () 
{
	OSVERSIONINFO versionInfo;
	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx( &versionInfo );

    return versionInfo.dwMajorVersion;
}
DWORD NS_WINAPI INFO_GetOSMinorVersion () 
{
	OSVERSIONINFO versionInfo;
	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx( &versionInfo );

    return versionInfo.dwMinorVersion;
}
DWORD NS_WINAPI INFO_GetOSServicePack () 
{
	OSVERSIONINFO versionInfo;
    char * servicePackString = "Service Pack ";
    int servicePackStringLng  = strlen(servicePackString);
    int servicePackNumber = 0;
    
	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx( &versionInfo );
    if ( strncmp ( versionInfo.szCSDVersion, servicePackString, servicePackStringLng ) == 0 )
        servicePackNumber = atoi ( &versionInfo.szCSDVersion[servicePackStringLng] );

    return servicePackNumber;
}
void NS_WINAPI OS_GetComputerName  (LPTSTR computerName, int nComputerNameLength ) 
{
	DWORD computerNameLength = nComputerNameLength; 
	GetComputerName( computerName, &computerNameLength );
}

PROCESSOR_TYPE NS_WINAPI OS_GetProcessor () 
{
	SYSTEM_INFO systemInfo;
	GetSystemInfo( &systemInfo);

	switch ( systemInfo.wProcessorArchitecture ) {
	case PROCESSOR_ARCHITECTURE_INTEL:
		return PROCESSOR_I386;
	case PROCESSOR_ARCHITECTURE_MIPS:
		return PROCESSOR_MIPS;
	case PROCESSOR_ARCHITECTURE_ALPHA:
		return PROCESSOR_ALPHA;
	case PROCESSOR_ARCHITECTURE_PPC:
		return PROCESSOR_PPC;
	default:
        break;
	}
    return PROCESSOR_UNKNOWN;
}
