/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */

#include <stdio.h>
#include "dsgw.h"

#include <ssl.h>
#include <sec.h>

main()
{
    char *p;
    time_t expires;
    char dn[ 512 ];
    char cookie[ 512 ];
    int rc;
    char *pw;
    

    printf( "Retrieve an entry from the cookie database\n" );

    printf( "cookie: " );
    gets( cookie );
    printf( "dn: " );
    gets( dn );

    rc = dsgw_ckdn2passwd( cookie, dn, &pw );
    if ( rc == 0 ) {
	printf( "Cookie valid, password is <%s>\n", pw );
    } else {
	if ( rc == DSGW_CKDB_KEY_NOT_PRESENT ) {
	    printf( "Cookie/DN pair not found in database\n" );
	} else if ( rc == DSGW_CKDB_EXPIRED ) {
	    printf( "Cookie/DN pair expired\n" );
	} else {
	    printf( "Unknown DB error\n" );
	}
    }
    if ( pw != NULL ) {
	free( pw );
    }
}
