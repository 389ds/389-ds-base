/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <string.h>
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
		while ( buf = fgets(buf, maxlen, stdin) ) {
			/* if buffer was filled, expand and keep reading unless last char
			is linefeed, in which case it is OK for buffer to be full */
			while( ((curlen = strlen(buf)) == (maxlen - 1)) && buf[curlen-1] != '\n' ) {
				maxlen *= 2;
				if( (buf = (char *)realloc(buf, maxlen)) == NULL ) {
					perror( "realloc" );
					return( 1 );
				}
				fgets(buf+curlen, maxlen/2 + 1, stdin);
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
