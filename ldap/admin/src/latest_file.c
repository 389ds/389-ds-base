/** BEGIN COPYRIGHT BLOCK
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
	strcpy( dir, szWildcardFileSpec );
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
