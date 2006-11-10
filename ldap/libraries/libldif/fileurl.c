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

/*
 *  LDIF tools fileurl.c -- functions for handling file URLs.
 *  Used by ldif_parse_line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fileurl.h"
#include <ctype.h>	/* for isalpha() */

static int str_starts_with( char *s, char *prefix );
static void hex_unescape( char *s );
static int unhex( char c );
static void strcpy_escaped_and_convert( char *s1, char *s2 );

/*
 * Convert a file URL to a local path.
 *
 * If successful, LDIF_FILEURL_SUCCESS is returned and *localpathp is
 * set point to an allocated string.  If not, an different LDIF_FILEURL_
 * error code is returned.
 *
 * See RFCs 1738 and 2396 for a specification for file URLs... but
 * Netscape Navigator seems to be a bit more lenient in what it will
 * accept, especially on Windows).
 *
 * This function parses file URLs of these three forms:
 *
 *    file:///path
 *    file:/path
 *    file://localhost/path
 *    file://host/path		(rejected with a ...NONLOCAL error)
 *
 * On Windows, we convert leading drive letters of the form C| to C:
 * and if a drive letter is present we strip off the slash that precedes
 * path.  Otherwise, the leading slash is returned.
 *
 */
int
ldif_fileurl2path( char *fileurl, char **localpathp )
{
    char	*path;

    /*
     * Make sure this is a file name or URL we can handle.
     */
    if ( *fileurl == '/' || 
	 ( isalpha( fileurl[0] ) && ( fileurl[1] == '|' || fileurl[1] == ':' ) ) ) {
        path = fileurl;
	goto path_ready;
    } else if ( !str_starts_with( fileurl, "file:" )) {
	return( LDIF_FILEURL_NOTAFILEURL );
    }

    path = fileurl + 5;		/* skip past "file:" scheme prefix */

    if ( *path != '/' ) {
	return( LDIF_FILEURL_MISSINGPATH );
    }

    ++path;			/* skip past '/' at end of "file:/" */

    if ( *path == '/' ) {
	++path;			/* remainder is now host/path or /path */
	if ( *path != '/' ) {
	    /*
	     * Make sure it is for the local host.
	     */
	    if ( str_starts_with( path, "localhost/" )) {
		path += 9;
	    } else {
		return( LDIF_FILEURL_NONLOCAL );
	    }
	}
    } else {		/* URL is of the form file:/path */
	--path;
    }

    /*
     * The remainder is now of the form /path.  On Windows, skip past the
     * leading slash if a drive letter is present.
     */
#ifdef _WIN32
    if ( isalpha( path[1] ) && ( path[2] == '|' || path[2] == ':' )) {
	++path;
    }
#endif /* _WIN32 */


 path_ready:
    /*
     * Duplicate the path so we can safely alter it.
     * Unescape any %HH sequences.
     */
    if (( path = strdup( path )) == NULL ) {
	return( LDIF_FILEURL_NOMEMORY );
    }
    hex_unescape( path );

#ifdef _WIN32
    /*
     * Convert forward slashes to backslashes for Windows.  Also,
     * if we see a drive letter / vertical bar combination (e.g., c|)
     * at the beginning of the path, replace the '|' with a ':'.
     */
    {
	char	*p;

	for ( p = path; *p != '\0'; ++p ) {
	    if ( *p == '/' ) {
		*p = '\\';
	    }
	}
    }

    if ( isalpha( path[0] ) && path[1] == '|' ) {
	path[1] = ':';
    }
#endif /* _WIN32 */

    *localpathp = path;
    return( LDIF_FILEURL_SUCCESS );
}


/*
 * Convert a local path to a file URL.
 *
 * If successful, LDIF_FILEURL_SUCCESS is returned and *urlp is
 * set point to an allocated string.  If not, an different LDIF_FILEURL_
 * error code is returned.  At present, the only possible error is
 * LDIF_FILEURL_NOMEMORY.
 *
 * This function produces file URLs of the form file:path.
 *
 * On Windows, we convert leading drive letters to C|.
 *
 */
int
ldif_path2fileurl( char *path, char **urlp )
{
    char	*p, *url, *prefix ="file:";

    if ( NULL == path ) {
	path = "/";
    }

    /*
     * Allocate space for the URL, taking into account that path may
     * expand during the hex escaping process.
     */
    if (( url = malloc( strlen( prefix ) + 3 * strlen( path ) + 1 )) == NULL ) {
	return( LDIF_FILEURL_NOMEMORY );
    }

    strcpy( url, prefix );
    p = url + strlen( prefix );

#ifdef _WIN32
    /*
     * On Windows, convert leading drive letters (e.g., C:) to the correct URL
     * syntax (e.g., C|).
     */
    if ( isalpha( path[0] ) && path[1] == ':' ) {
	*p++ = path[0];
	*p++ = '|';
	path += 2;
	*p = '\0';
    }
#endif /* _WIN32 */

    /*
     * Append the path, encoding any URL-special characters using the %HH
     * convention.
     * On Windows, convert backwards slashes in the path to forward ones.
     */
    strcpy_escaped_and_convert( p, path );

    *urlp = url;
    return( LDIF_FILEURL_SUCCESS );
}


/*
 * Return a non-zero value if the string s begins with prefix and zero if not.
 */
static int
str_starts_with( char *s, char *prefix )
{
    size_t	prefix_len;

    if ( s == NULL || prefix == NULL ) {
	return( 0 );
    }

    prefix_len = strlen( prefix );
    if ( strlen( s ) < prefix_len ) {
	return( 0 );
    }

    return( strncmp( s, prefix, prefix_len ) == 0 );
}


/*
 * Remove URL hex escapes from s... done in place.  The basic concept for
 * this routine is borrowed from the WWW library HTUnEscape() routine.
 *
 */
static void
hex_unescape( char *s )
{
	char	*p;

	for ( p = s; *s != '\0'; ++s ) {
		if ( *s == '%' ) {
			if ( *++s != '\0' ) {
				*p = unhex( *s ) << 4;
			}
			if ( *++s != '\0' ) {
				*p++ += unhex( *s );
			}
		} else {
			*p++ = *s;
		}
	}

	*p = '\0';
}


/*
 * Return the integer equivalent of one hex digit (in c).
 *
 */
static int
unhex( char c )
{
	return( c >= '0' && c <= '9' ? c - '0'
	    : c >= 'A' && c <= 'F' ? c - 'A' + 10
	    : c - 'a' + 10 );
}


#define HREF_CHAR_ACCEPTABLE( c )	(( c >= '-' && c <= '9' ) ||	\
					 ( c >= '@' && c <= 'Z' ) ||	\
					 ( c == '_' ) ||		\
					 ( c >= 'a' && c <= 'z' ))

/*
 * Like strcat(), except if any URL-special characters are found in s2
 * they are escaped using the %HH convention and backslash characters are
 * converted to forward slashes on Windows.
 *
 * Maximum space needed in s1 is 3 * strlen( s2 ) + 1.
 *
 */
static void
strcpy_escaped_and_convert( char *s1, char *s2 )
{
    char	*p, *q;
    char	*hexdig = "0123456789ABCDEF";

    p = s1 + strlen( s1 );
    for ( q = s2; *q != '\0'; ++q ) {
#ifdef _WIN32
	if ( *q == '\\' ) {
                *p++ = '/';
	} else
#endif /* _WIN32 */

	if ( HREF_CHAR_ACCEPTABLE( *q )) {
	    *p++ = *q;
	} else {
	    *p++ = '%';
	    *p++ = hexdig[ 0x0F & ((*(unsigned char*)q) >> 4) ];
	    *p++ = hexdig[ 0x0F & *q ];
	}
    }

    *p = '\0';
}
