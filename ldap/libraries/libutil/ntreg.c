/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include "ldap.h"

int SlapdGetRegSZ( LPTSTR lpszRegKey, LPSTR lpszValueName, LPTSTR lpszValue )
{
	HKEY hKey;
	DWORD dwType, dwNumBytes;
	LONG lResult;

	/* Open the registry, get the required key handle. */	
	lResult = RegOpenKeyEx( HKEY_LOCAL_MACHINE, lpszRegKey, 
				0L,	KEY_QUERY_VALUE, &hKey );
	if (lResult == ERROR_SUCCESS) 
	{ 
		dwNumBytes = sizeof( DWORD );
		lResult = RegQueryValueEx( hKey, lpszValueName, 0, 
					&dwType, NULL, &dwNumBytes );
		if( lResult == ERROR_SUCCESS ) 
		{
			RegQueryValueEx( hKey, lpszValueName, 0, &dwType, 
							(LPBYTE)lpszValue, &dwNumBytes );
			*(lpszValue+dwNumBytes) = 0;

			/*  Close the Registry. */
			RegCloseKey(hKey);
			return 0;
		}
		else
		{
			/* No config file location stored in the Registry. */
			RegCloseKey(hKey);
			return 1;
		}
	}
	else
	{
  		return 1;
	}
}	/* SlapdGetRegSZ */


int SlapdSetRegSZ( LPTSTR lpszKey, LPSTR lpszValueName, LPTSTR lpszValue )
{
	HKEY hKey;
	LONG lResult;

	/* Open the registry, get a handle to the desired key. */	
	lResult = RegOpenKeyEx( HKEY_LOCAL_MACHINE, lpszKey, 0, 
				KEY_ALL_ACCESS, &hKey );
	if (lResult == ERROR_SUCCESS) 
	{ 
		/* Set the value to the value-name at the key location. */
		RegSetValueEx( hKey, lpszValueName, 0, REG_SZ, 
					   (CONST BYTE*)lpszValue, strlen(lpszValue) );

		/* Close the registry */
		RegCloseKey(hKey);
		return 0;
	} 
	else 
	{
		return 1;
	}
}	/* SlapdSetRegSZ */

/* converts '/' chars to '\' */
void
unixtodospath(char *szText)
{
    if(szText)
    {
        while(*szText)
        {
            if( *szText == '/' )
                *szText = '\\';
            szText++;
        }
    }
}

/* converts '\' chars to '/' */
void
dostounixpath(char *szText)
{
    if(szText)
    {
        while(*szText)
        {
            if( *szText == '\\' )
                *szText = '/';
            szText++;
        }
    }
}

#endif /* _WIN32 */
