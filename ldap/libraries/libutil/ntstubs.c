/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/******************************************************
 *
 *  ntstubs.c - Stubs needed on NT when linking in
 *  the SSL code. If these stubs were not here, the 
 *  named functions below would not be located at link
 *  time, because there is no implementation of the 
 *  functions for Win32 in cross-platform libraries.
 *
 ******************************************************/

#if defined( _WIN32 ) && defined ( NET_SSL )

#include <windows.h>
#include <nspr.h>

/*
char* XP_FileName (const char* name, XP_FileType type)
{
    return NULL;
}

XP_File XP_FileOpen(const char* name, XP_FileType type, 
		    const XP_FilePerm permissions)
{
    return NULL;
}
*/

char *
WH_FileName (const char *name, PRFileType type)
{
	return NULL;
}
#endif /* WIN32 && NET_SSL */

