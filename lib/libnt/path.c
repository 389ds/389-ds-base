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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/***********************************************************
 *	Path functions - removing ../ from path
 **********************************************************/
#include <windows.h>
#include <stdio.h>    
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>    /* For _findfirst */
#include <direct.h>    /* For _rmdir */
#include <errno.h>
#include "nt/ntos.h"

DWORD NS_WINAPI
PATH_RemoveRelative ( char * path )
{
	char * src;
	char * dst;

    src = path;
    dst = path;
    while ( *src ) {
		if ( *src == '.' ) {
    		if (  *(src+1) == '.' ) {
    			/* strip off the "../" */
    			src += 2;

    			/* back off least significant directory */
                dst--;
    			if ( ( *dst == '\\' ) || ( *dst == '/' ) ) 
    				*dst--;
    			while ( dst > path ) {
    				if ( ( *dst == '\\' ) || ( *dst == '/' ) ) 
    					break;
                    dst--;
                }
			} else {
                // remove single "."
            }

		} else
			*dst++ = *src++;
	}
	*dst = '\0';						 

	return TRUE;
}
DWORD NS_WINAPI 
PATH_ConvertNtSlashesToUnix( LPCTSTR  lpszNtPath, LPSTR lpszUnixPath )
{
    if ( lpszNtPath == NULL )
        return 0;

	/* create reverse slashes and escape them */
	while ( *lpszNtPath ) {
		if ( *lpszNtPath == '\\' )
			*lpszUnixPath = '/';
        else
			*lpszUnixPath = *lpszNtPath;
		lpszNtPath++;
		lpszUnixPath++;
	}
    *lpszUnixPath = '\0';
    return 0;
}

static DWORD 
PATH_DeleteRecursivelyFoundFile ( char * fullFileName, char * path, char * fileName )
{
    if ( strcmp ( fileName, "." ) == 0)
        return TRUE;
             
    if ( strcmp ( fileName, ".." ) == 0)
        return TRUE;
             
    strcpy ( fullFileName, path );
    strcat ( fullFileName, "\\" );
    strcat ( fullFileName,  fileName );
    return PATH_DeleteRecursively ( fullFileName );
}

/* if the path specified is a file name, the file is deleted
 * If the path specifies a directory, the directory is deleted
 */
DWORD NS_WINAPI
PATH_DeleteRecursively ( char * path )
{
	int result;
	unsigned short fileStatus;
	struct _stat buf;
    struct _finddata_t fileFound;
    long hFile;
    DWORD retStatus = TRUE;
    char fullFileName[_MAX_PATH];
    int error;
    
	/* Check if statistics are valid: */
	result = _stat( path, &buf );
	if( result != 0 )
		return TRUE;					// file or directory does not exist

	fileStatus = buf.st_mode & _S_IFMT;

    /* check if regular file */
	if ( fileStatus & _S_IFREG ) {
        if ( remove ( path ) == -1 ) {
            error = errno;
            switch ( error ) {
            case ENOENT:
                break;

            case EACCES:
                break;

            default:
                break;
            }

            return FALSE;
        }
        return TRUE;
    }
    if ( (fileStatus & _S_IFDIR) == 0 )
        return FALSE; 


    /* path contains a directory, delete all files recursively */
    /* Find first .c file in current directory */
    strcpy ( fullFileName, path );
    strcat ( fullFileName, "\\*.*");
    if( (hFile = _findfirst( fullFileName, &fileFound )) != -1L ) { /* directory contain files? */
        if ( !PATH_DeleteRecursivelyFoundFile ( fullFileName, path, fileFound.name ) )
                retStatus = FALSE;
                    
        /* Find the rest of the .c files */
        while( _findnext( hFile, &fileFound ) == 0 ) {
            if ( !PATH_DeleteRecursivelyFoundFile ( fullFileName, path, fileFound.name ) )
                    retStatus = FALSE;
        }
        _findclose( hFile );
   }

    /* remove the directory, now that it is empty */
    if ( _rmdir( path ) == -1 )
        retStatus = FALSE;
    return retStatus;
}
/* GetNextFileInDirectory - gets next file in the directory
 * Set hFile to zero, when you call it. The routine returns the
 * next value for hFile. When the routine returns NULL, there is
 * no more files
 *
 */
DWORD NS_WINAPI
PATH_GetNextFileInDirectory ( long hFile, char * path, char * lpFileName )
{
	int result;
	unsigned short fileStatus;
	struct _stat buf;
    struct _finddata_t fileFound;
    DWORD retStatus = TRUE;
    char fullFileName[_MAX_PATH];

    if ( hFile == 0 ) {    
    	/* Check if statistics are valid: */
    	result = _stat( path, &buf );
    	if( result != 0 )
    		return 0;					// file or directory does not exist

    	fileStatus = buf.st_mode & _S_IFMT;
        if ( (fileStatus & _S_IFDIR) == 0 )
            return 0; 


        /* path contains a directory, delete all files recursively */
        /* Find first .c file in current directory */
        strcpy ( fullFileName, path );
        strcat ( fullFileName, "\\*.*");
        if( (hFile = _findfirst( fullFileName, &fileFound )) == -1L )
            return 0; 
        if ( ( strcmp ( fileFound.name , "." ) != 0)
          && ( strcmp ( fileFound.name , ".." ) != 0) ) {
            strcpy ( lpFileName, fileFound.name );
            return hFile;
        }
    }
                    
    /* Find the rest of the .c files */
    while( _findnext( hFile, &fileFound ) == 0 ) {
        if ( ( strcmp ( fileFound.name , "." ) != 0)
          && ( strcmp ( fileFound.name , ".." ) != 0) ) {
            strcpy ( lpFileName, fileFound.name );
            return hFile;
        }
    }

    _findclose( hFile );
    return 0;
}
/*---------------------------------------------------------------------------*\
 *
 * Function:  PATH_GetNextSubDirectory
 *
 *  Purpose:  Gets next sub directory in the path
 *
 *    Input:
 *          hFile: set to zero first time called; use return value for subsequent calls
 *          path: directory containing sub directories
 *          lpSubDirectoryName: buffer to store sub directorie name
 *          lpSubDirectoryPrefix: chars to exactly match begining of directory name
 *
 *  Returns:
 *          hFile to be used on subsequent call (0, if no more directories)
 *
 * Comments:
\*---------------------------------------------------------------------------*/
DWORD NS_WINAPI
PATH_GetNextSubDirectory( long hFile, char * path, char * lpSubDirectoryName, char * lpSubDirectoryPrefix )
{
	int result;
	unsigned short fileStatus;
	struct _stat buf;
    char * subDirectoryPrefix;
    char * p;
    char fullFileName[_MAX_PATH];
    BOOL bSubDirectoryFound;

    do {
        hFile = PATH_GetNextFileInDirectory ( hFile, path, lpSubDirectoryName );
        if ( hFile == 0 )
            return 0;

    	/* Check if file is a directory */
        strcpy ( fullFileName, path );
        strcat ( fullFileName, "\\" );
        strcat ( fullFileName, lpSubDirectoryName );
    	result = _stat( fullFileName, &buf );
    	if( result == 0 ) {
        	fileStatus = buf.st_mode & _S_IFMT;
            if ( (fileStatus & _S_IFDIR) == _S_IFDIR )  {

                /* check if sub directory matches prefix */
                bSubDirectoryFound = TRUE;
                if ( lpSubDirectoryPrefix ) {
                    p = lpSubDirectoryName;
                    subDirectoryPrefix = lpSubDirectoryPrefix;
                    while ( *subDirectoryPrefix ) {
                        if ( *subDirectoryPrefix++ != *p++ ) {
                            bSubDirectoryFound = FALSE;
                            break;
                        }
                    }
                }
                if ( bSubDirectoryFound )
                    return hFile;
            }
        }
    } while ( hFile );

    return 0;       // no more sub directories
}

