/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */

#include <stdio.h>
#include "dsgw.h"

#include <ssl.h>
#include <sec.h>

main( int argc, char **argv)
{
    char *p;
    char dn[ 512 ];
    char pw[ 512 ];
    char lifesec[ 512 ];
    int rc;
    int c;
    extern char *optarg;
    time_t lifetime;
    
    printf( "Add an entry to the cookie database\n" );

    SEC_Init();
    SEC_RNGInit();
    SEC_SystemInfoForRNG();

    if ( argc > 1 ) {
	while (( c = getopt( argc, argv, "d:l:p:" )) != EOF ) {
	    switch ( c ) {
	    case 'd':
		strcpy( dn, optarg );
		break;
	    case 'l':
		strcpy( lifesec, optarg );
		break;
	    case 'p':
		strcpy( pw, optarg );
		break;
	    }
	}
    }

    if ( strlen( dn ) == 0 || strlen( pw ) == 0 || strlen( lifesec ) == 0 ) {
	printf( "dn: " );
	gets( dn );
	printf( "passwd: " );
	gets( pw );
	printf( "expires in how many seconds? " );
	gets( lifesec );
    }

    lifetime = atol( lifesec );
    p = dsgw_mkcookie( dn, pw, lifetime, &rc );
    if ( p == NULL ) {
	fprintf( stderr, "Error storing cookie: error %d\n", rc );
    } else {
	printf( "success, cookie is %s\n", p );
    }
}
