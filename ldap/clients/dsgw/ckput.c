/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */

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
