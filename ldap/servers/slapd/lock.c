/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* lock.c - routines to open and apply an advisory lock to a file */

#include <stdio.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/time.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#endif
#include "slap.h"
#ifdef USE_LOCKF
#include <unistd.h>
#endif

FILE *
lock_fopen( char *fname, char *type, FILE **lfp )
{
	FILE	*fp;
	char	buf[MAXPATHLEN];

	/* open the lock file */
	strcpy( buf, fname );
	strcat( buf, ".lock" );
	if ( (*lfp = fopen( buf, "w" )) == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY, "could not open \"%s\"\n", buf, 0, 0 );
		return( NULL );
	}

	/* acquire the lock */
#ifdef _WIN32
	while ( _locking( _fileno( *lfp ), LK_NBLCK, 0xFFFFFFFF ) != 0 ) {
#else
#ifdef USE_LOCKF
	while ( lockf( fileno( *lfp ), F_LOCK, 0 ) != 0 ) {
#else /* _WIN32 */
	while ( flock( fileno( *lfp ), LOCK_EX ) != 0 ) {
#endif
#endif /* _WIN32 */
		;	/* NULL */
	}

	/* open the log file */
	if ( (fp = fopen( fname, type )) == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY, "could not open \"%s\"\n", fname, 0, 0 );
#ifdef _WIN32
	        _locking( _fileno( *lfp ), LK_UNLCK, 0xFFFFFFFF );
#else /* _WIN32 */
#ifdef USE_LOCKF
		lockf( fileno( *lfp ), F_ULOCK, 0 );
#else
		flock( fileno( *lfp ), LOCK_UN );
#endif
#endif /* _WIN32 */
		return( NULL );
	}

	return( fp );
}

int
lock_fclose( FILE *fp, FILE *lfp )
{
	/* unlock */
#ifdef _WIN32
        _locking( _fileno( lfp ), LK_UNLCK, 0xFFFFFFFF );
#else /* _WIN32 */
#ifdef USE_LOCKF
	lockf( fileno( lfp ), F_ULOCK, 0 );
#else
	flock( fileno( lfp ), LOCK_UN );
#endif
#endif /* _WIN32 */
	fclose( lfp );

	return( fclose( fp ) );
}
