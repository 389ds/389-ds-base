/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "dsalib.h"
#include "prthread.h"
#include "plstr.h"

/*
 * Function: adjustFile
 * Property: Adjust the file offset to the "tail" of the file
 * Called by: DisplayTail
 * Return: -1 for error, else file size
 */	
static int
adjustFile(FILE *fp, int curSize)
{
    struct stat	statBuf;
    int		fd = fileno(fp);

    if ( fstat(fd, &statBuf) == -1 )
	return(-1);
    if ( statBuf.st_size < curSize )	/* file has shrunk! */
    {
        if ( fseek(fp, 0L, 0) == -1 )	/* get back to the beginning */
	    return(-1);
    }
    curSize = (int) statBuf.st_size;
    if ( !curSize )
	curSize = 1;
    return(curSize);
}

/*
 * Function: wrapLines
 * Property: wrap lines at 50 characters.  When a wrap point is encountered,
 *           insert the string "\n", since the buffer is going to be placed
 *           inside a JavaScript alert() call.
 * Called by: ds_display_tail
 * Return: pointer to wrapped buffer.  Caller should free.
 */
static char *
wrapLines( char *buf )
{
    char *src = buf;
    char *obuf, *dst;
    int lwidth = 0;

    obuf = malloc( strlen( buf ) * 2 ); /* conservative */
    if ( obuf == NULL ) {
	return NULL;
    }
    dst = obuf;
    while ( *src != '\0' ) {
	if (( ++lwidth > 50 ) && isspace( *src )) {
	    *dst++ = '\\';
	    *dst++ = 'n';
	    lwidth = 0;
	    src++;
	} else {
	    *dst++ = *src++;
	}
    }
    *dst = '\0';
    return obuf;
}

DS_EXPORT_SYMBOL int
ds_get_file_size(char *fileName)
{
    struct stat	statBuf;

    if ( fileName == NULL )
        return(0);

    if ( stat(fileName, &statBuf) == -1 )
	return(0);

    return(statBuf.st_size);
}

/*
 * Function: ds_display_tail
 * Property: follow the tail and display it for timeOut secs or until the line
 * read from the file contains the string doneMsg; the lastLine, if not null,
 * will be filled in with the last line read from the file; this is useful
 * for determining why the server failed to start e.g. port in use, ran out
 * of semaphores, database is corrupted, etc.
 * Calls:    adjustFile
 */
DS_EXPORT_SYMBOL void 
ds_display_tail(char *fileName, int timeOut, int startSeek, char *doneMsg,
		char *lastLine)
{
	FILE    	*fp = NULL;
	int		fd;
	char		msgBuf[BIG_LINE];
	struct stat	statBuf;
	int		curSize;
	int		i = timeOut;

	if (lastLine != NULL)
	    lastLine[0] = 0;

	if ( fileName == NULL )
	    return;
	/*
	 * Open the file.
	 * Try to keep reading it assuming that it may get truncated.
	 */
	while (i && !fp)
	{
	    fp = fopen(fileName, "r");
	    if (!fp)
	    {
		PR_Sleep(PR_SecondsToInterval(1));
		--i;
		/* need to print something so http connection doesn't
		   timeout and also to let the user know something is
		   happening . . .
		*/
		if (!(i % 10))
		{
		    ds_send_status("Attempting to obtain server status . . .");
		}
	    }
	}

	if (!i || !fp)
	    return;

	fd = fileno(fp);
	if ( fstat(fd, &statBuf) == -1 ) {
	    (void) fclose(fp);
	    return;
	}
	curSize = (int) statBuf.st_size;
	if ( startSeek < curSize )
	    curSize = startSeek;
	if ( curSize > 0 )
	    if ( fseek(fp, curSize, SEEK_SET) == -1 ) {
		(void) fclose(fp);
		return;
	    }
	if ( !curSize )
	    curSize = 1;		/* ensure minimum */

	while ( i )
	{
	    int	newCurSize;

	    newCurSize = curSize = adjustFile(fp, curSize);
	    if ( curSize == -1 ) {
		(void) fclose(fp);
		return;
	    }
	    while ( fgets(msgBuf, sizeof(msgBuf), fp) )
	    {
		char *tmp;
		if (lastLine != NULL)
		    PL_strncpyz(lastLine, msgBuf, BIG_LINE);
		if ( (tmp = strchr(msgBuf, ((int) '\n'))) != NULL )
		    *tmp = '\0';	/* strip out real newlines from here */
		ds_send_status(msgBuf);
		if ( (strstr(msgBuf, "WARNING: ") != NULL) ||
		     (strstr(msgBuf, "ERROR: ") != NULL) ) {
		    char *wrapBuf;

		    wrapBuf = wrapLines( msgBuf );
		    if ( wrapBuf != NULL ) {
			ds_send_error(wrapBuf, 5);
		    } else {
			ds_send_error(msgBuf, 5);
		    }
		}
		if ( (doneMsg != NULL) && (strstr(msgBuf, doneMsg)) ) {
		    (void) fclose(fp);
		    return;
		}
	        newCurSize = adjustFile(fp, newCurSize);
	        if ( newCurSize == -1 ) {
		    (void) fclose(fp);
		    return;
	        }
	    }
	    if ( ferror(fp) ) {
		(void) fclose(fp);
		return;
	    }
	    clearerr(fp);	/* clear eof condition */
	    PR_Sleep(PR_SecondsToInterval(1));
	    if ( newCurSize != curSize )
		i = timeOut;	/* keep going till no more changes */
	    else
	        i--;
	}
	(void) fclose(fp);
}
