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
/***********************************************************************
**
** NAME
**  latest_file.c
**
** DESCRIPTION
**  Creates a batch file which assigns the latest file matching a given
**  pattern to the environment variable LATEST_FILE. For use in NT batch
**  files.
**
** AUTHOR
**   <rweltman@netscape.com>
**
***********************************************************************/

/***********************************************************************
** Includes
***********************************************************************/


/*
 * Given a pattern to match, creates a batch file with the latest full
 * file name to set to LATEST_FILE. No file is created if there are no
 * matching files.
 */
#if defined( _WIN32 )
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <io.h>


int main (int argc, char **argv)
{
	char *szWildcardFileSpec;
	char *szOutput;
	char dir[1024];
	char latest[1024];
	char *dirEnd;
	time_t latest_time = 0;
	long hFile;
	struct _finddata_t	fileinfo;
    FILE      *fBatch;

	if ( argc < 3 ) {
		fprintf( stderr, "Usage: %s PATTERN OUTPUTFILE\n", argv[0] );
		return 1;
	}

	szWildcardFileSpec = argv[1];
	szOutput = argv[2];

	/* Get directory part of path */
	strncpy( dir, szWildcardFileSpec, sizeof(dir)-1 );
	dir[sizeof(dir)-1] = (char)0;
	dirEnd = strrchr( dir, '\\' );
	if ( dirEnd != NULL ) {
		*dirEnd = 0;
	}

	/* Expand file specification */
	hFile = _findfirst( szWildcardFileSpec, &fileinfo);
	if( hFile == -1 ) {
		perror( "No matching files!" );
		return -1;
	}

	_snprintf( latest, sizeof(latest), "%s\\%s", dir, fileinfo.name );
	latest[sizeof(latest)-1] = (char)0;
	latest_time = fileinfo.time_create;

	while( _findnext( hFile, &fileinfo ) == 0 ) {
		if ( fileinfo.time_create > latest_time ) {
			_snprintf( latest, sizeof(latest), "%s\\%s", dir, fileinfo.name );
			latest[sizeof(latest)-1] = (char)0;
			latest_time = fileinfo.time_create;
		}
	}

	_findclose( hFile );

    /* create batch file */
    fBatch = fopen (szOutput, "w");
    if ( fBatch == NULL ) {
        perror ("Unable to create batch file!");
        return 1;
    }
    fprintf( fBatch, "set LATEST_FILE=%s\n", latest );
    fclose (fBatch);

	return 0;
}
#endif /* ( XP_WIN32 ) */
