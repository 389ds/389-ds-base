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
	PR_snprintf( buf, MAXPATHLEN, "%s%s", fname, ".lock" );
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
