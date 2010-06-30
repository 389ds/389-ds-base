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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/types.h>
#if defined( _WINDOWS ) || defined( _WIN32 )
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>		/* for read() */
#include <sys/socket.h>
#endif
#include "ldap.h"
#include "ldif.h"

int	ldap_syslog;
int	ldap_syslog_level;

#if defined(USE_OPENLDAP)
static char *
ldif_type_and_value(const char *type, const char *val, int vlen)
{
    char	*buf, *p;
    int		tlen;

    tlen = strlen( type );
    if (( buf = (char *)malloc( LDIF_SIZE_NEEDED( tlen, vlen ) + 1 )) !=
	    NULL ) {
        p = buf;
        ldif_sput( &p, LDIF_PUT_VALUE, type, val, vlen );
        *p = '\0';
    }

    return( buf );
}
#endif

static void
display_usage( char *name )
{
	fprintf( stderr, "usage: %s [-b] <attrtype>\n", name );
}

int main( int argc, char **argv )
{
	char	*type, *out;
	int	binary = 0;

	if (argc < 2 || argc > 3 ) {
		display_usage( argv[0] );
		return( 1 );
	}
	if ( argc == 3 ) {
		if ( strcmp( argv[1], "-b" ) != 0 ) {
			display_usage( argv[0] );
			return( 1 );
		} else {
			binary = 1;
			type = argv[2];
		}
	} else {
		if ( strcmp( argv[1], "-b" ) == 0 ) {
			display_usage( argv[0] );
			return( 1 );
		}
		type = argv[1];
	}

	/* if the -b flag was used, read single binary value from stdin */
	if ( binary ) {
		char    buf[BUFSIZ];
		char	*val;
		int	nread, max, cur;
#if defined( _WINDOWS ) || defined( _WIN32 )
		_setmode( _fileno( stdin ), _O_BINARY );
#endif

		if (( val = (char *) malloc( BUFSIZ )) == NULL ) {
			perror( "malloc" );
			return( 1 );
		}
		max = BUFSIZ;
		cur = 0;
		while ( (nread = read( 0, buf, BUFSIZ )) != 0 ) {
			if (nread < 0) {
				perror( "read error" );
				return( 1 );
			}
			if ( nread + cur > max ) {
				max += BUFSIZ;
				if (( val = (char *) realloc( val, max )) ==
				    NULL ) {
					perror( "realloc" );
					return( 1 );
				}
			}
			memcpy( val + cur, buf, nread );
			cur += nread;
		}

		if (( out = ldif_type_and_value( type, val, cur )) == NULL ) {
		    	perror( "ldif_type_and_value" );
			return( 1 );
		}

		fputs( out, stdout );
		free( out );
		free( val );
		return( 0 );
	} else {
	/* not binary:  one value per line... */
		char *buf;
		int curlen, maxlen = BUFSIZ;

		if( (buf = malloc(BUFSIZ)) == NULL ) {
			perror( "malloc" );
		    return( 1 );
		}
		while ( (buf = fgets(buf, maxlen, stdin)) ) {
			/* if buffer was filled, expand and keep reading unless last char
			is linefeed, in which case it is OK for buffer to be full */
			while( ((curlen = strlen(buf)) == (maxlen - 1)) && buf[curlen-1] != '\n' ) {
				maxlen *= 2;
				if( (buf = (char *)realloc(buf, maxlen)) == NULL ) {
					perror( "realloc" );
					return( 1 );
				}
				(void)fgets(buf+curlen, maxlen/2 + 1, stdin);
			}
			/* we have a full line, chop potential newline and turn into ldif */
			if( buf[curlen-1] == '\n' )
				buf[curlen-1]='\0';
			if (( out = ldif_type_and_value( type, buf, strlen( buf ) ))
				== NULL ) {
				perror( "ldif_type_and_value" );
				return( 1 );
			}
			fputs( out, stdout );
			free( out );

		}
		free( buf );
	}
	return( 0 );
}
