/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
// ERROR.C
//
//      This file contains the functions needed to install the httpd server.
// They are as follows.
//
// getreg.c
//
// This file has the function needed to get a particular value of a key from the registry...
// 1/16/95 aruna
//

#include <windows.h>
#include "nt/ntos.h"

BOOL NS_WINAPI
REG_CheckIfKeyExists( HKEY hKey, LPCTSTR key )
{
	HKEY hQueryKey;

	if (RegOpenKeyEx(hKey, key, 0, KEY_ALL_ACCESS,
			&hQueryKey) != ERROR_SUCCESS) {
		return FALSE;
	}

	RegCloseKey(hQueryKey);
	return TRUE;
}
		 	
BOOL NS_WINAPI
REG_GetRegistryParameter(
    HKEY hKey, 
	LPCTSTR registryKey, 
	LPTSTR QueryValueName,
	LPDWORD ValueType,
	LPBYTE ValueBuffer, 
	LPDWORD ValueBufferSize
	)
{
	HKEY hQueryKey;

	if (RegOpenKeyEx(hKey, registryKey, 0, KEY_ALL_ACCESS,
			&hQueryKey) != ERROR_SUCCESS) {
			
		return FALSE;
	}
	  
	if (RegQueryValueEx(hQueryKey, QueryValueName, 0, 
		ValueType, ValueBuffer, ValueBufferSize) != ERROR_SUCCESS) {
		RegCloseKey(hQueryKey);
		return FALSE;
	}

	RegCloseKey(hQueryKey);
	return TRUE;
}
		 	
BOOL NS_WINAPI
REG_CreateKey( HKEY hKey, LPCTSTR registryKey )
{
	HKEY hNewKey;

	if ( RegCreateKey (hKey, registryKey, &hNewKey) != ERROR_SUCCESS) {
		return FALSE;
	}

	RegCloseKey(hNewKey);
	return TRUE;
}

BOOL NS_WINAPI
REG_DeleteKey( HKEY hKey, LPCTSTR registryKey )
{
	HKEY hQueryKey;
    DWORD dwNumberOfSubKeys;
    char  registrySubKey[256];
    DWORD i;

    /* if key does not exist, then consider it deleted */
    if ( !REG_CheckIfKeyExists( hKey, registryKey ) )
        return TRUE;

    if ( !REG_GetSubKeysInfo( hKey, registryKey, &dwNumberOfSubKeys, NULL ) )
        return FALSE;

    if ( dwNumberOfSubKeys ) {
    	if (RegOpenKeyEx(hKey, registryKey, 0, KEY_ALL_ACCESS,
    			&hQueryKey) != ERROR_SUCCESS) {
    		return FALSE;
    	}

        // loop through all sub keys and delete the subkeys (recursion)
        for ( i=0; i<dwNumberOfSubKeys; i++ ) {
         	if ( RegEnumKey( hQueryKey, 0, registrySubKey, sizeof(registrySubKey) ) != ERROR_SUCCESS) {		
        		RegCloseKey(hQueryKey);
        		return FALSE;
        	}
            if ( !REG_DeleteKey( hQueryKey, registrySubKey ) ) {
        		RegCloseKey(hQueryKey);
        		return FALSE;
        	}
        }
	    RegCloseKey(hQueryKey);
    }

	if ( RegDeleteKey (hKey, registryKey) != ERROR_SUCCESS) {
		return FALSE;
	}

	return TRUE;
}

BOOL NS_WINAPI
REG_GetSubKey( HKEY hKey, LPCTSTR registryKey, DWORD nSubKeyIndex, LPTSTR registrySubKeyBuffer, DWORD subKeyBufferSize )
{
	HKEY hQueryKey;

	if (RegOpenKeyEx(hKey, registryKey, 0, KEY_ALL_ACCESS,
			&hQueryKey) != ERROR_SUCCESS) {
		return FALSE;
	}

 	if ( RegEnumKey( hQueryKey, nSubKeyIndex, registrySubKeyBuffer, subKeyBufferSize ) != ERROR_SUCCESS) {		
		RegCloseKey(hQueryKey);
		return FALSE;
	}

	RegCloseKey(hQueryKey);
	return TRUE;
}

BOOL NS_WINAPI
REG_GetSubKeysInfo( HKEY hKey, LPCTSTR registryKey, LPDWORD lpdwNumberOfSubKeys, LPDWORD lpdwMaxSubKeyLength )
{
	HKEY hQueryKey;
    char  szClass[256];	// address of buffer for class string 
    DWORD  cchClass;	// address of size of class string buffer 
    DWORD  cSubKeys;	// address of buffer for number of subkeys 
    DWORD  cchMaxSubkey;	// address of buffer for longest subkey name length  
    DWORD  cchMaxClass;	// address of buffer for longest class string length 
    DWORD  cValues;	// address of buffer for number of value entries 
    DWORD  cchMaxValueName;	// address of buffer for longest value name length 
    DWORD  cbMaxValueData;	// address of buffer for longest value data length 
    DWORD  cbSecurityDescriptor;	// address of buffer for security descriptor length 
    FILETIME  ftLastWriteTime; 	// address of buffer for last write time 


	if (RegOpenKeyEx(hKey, registryKey, 0, KEY_ALL_ACCESS,
			&hQueryKey) != ERROR_SUCCESS) {
		return FALSE;
	}

    if ( RegQueryInfoKey( hQueryKey,	// handle of key to query 
            (char*)&szClass,	// address of buffer for class string 
            &cchClass,	// address of size of class string buffer 
            NULL,	// reserved 
            &cSubKeys,	// address of buffer for number of subkeys 
            &cchMaxSubkey,	// address of buffer for longest subkey name length  
            &cchMaxClass,	// address of buffer for longest class string length 
            &cValues,	// address of buffer for number of value entries 
            &cchMaxValueName,	// address of buffer for longest value name length 
            &cbMaxValueData,	// address of buffer for longest value data length 
            &cbSecurityDescriptor,	// address of buffer for security descriptor length 
            &ftLastWriteTime 	// address of buffer for last write time 
            ) != ERROR_SUCCESS) {		
		RegCloseKey(hQueryKey);
		return FALSE;
	}

    // return desired information
    if ( lpdwNumberOfSubKeys )
        *lpdwNumberOfSubKeys = cSubKeys;
    if ( lpdwMaxSubKeyLength )
        *lpdwMaxSubKeyLength = cchMaxSubkey;

	RegCloseKey(hQueryKey);
	return TRUE;
}

BOOL NS_WINAPI
REG_SetRegistryParameter(
    HKEY hKey, 
	LPCTSTR registryKey, 
	LPTSTR valueName,
	DWORD valueType,
	LPCTSTR ValueString, 
	DWORD valueStringLength
	)
{
	HKEY hQueryKey;

	if (RegOpenKeyEx(hKey, registryKey, 0, KEY_ALL_ACCESS,
			&hQueryKey) != ERROR_SUCCESS) {
		return FALSE;
	}
	  
 	if ( RegSetValueEx( hQueryKey, valueName, 0, valueType, (CONST BYTE *)ValueString,
							valueStringLength ) != ERROR_SUCCESS) {		
		RegCloseKey(hQueryKey);
		return FALSE;
	}

	RegCloseKey(hQueryKey);
	return TRUE;
}




BOOL NS_WINAPI
REG_DeleteValue( HKEY hKey, LPCTSTR registryKey, LPCSTR valueName )
{
    HKEY hQueryKey;
    DWORD  ValueBufferSize = 256;
    char ValueBuffer[256];
    DWORD i, ValueType;

    /* if key does not exist, then consider it deleted */
    if (RegOpenKeyEx(hKey, registryKey, 0, KEY_ALL_ACCESS,
		     &hQueryKey) != ERROR_SUCCESS) {
	    return FALSE;
    }

    /* if valuename does not exist, then consider it deleted */
    if (RegQueryValueEx(hQueryKey, valueName, 0, 
			&ValueType, ValueBuffer, &ValueBufferSize) != ERROR_SUCCESS) {
	    RegCloseKey(hQueryKey);
	    return TRUE;
    }

    if (RegDeleteValue(hQueryKey, valueName) != ERROR_SUCCESS) {
	    RegCloseKey(hQueryKey);
	    return FALSE;
    }

    RegCloseKey(hQueryKey);
    return TRUE;
}
