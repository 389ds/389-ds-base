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
    char cookie[ 512 ];
    int rc;
    
    printf( "Remove an entry to the cookie database\n" );

    printf( "cookie: " );
    gets( cookie );

    rc = dsgw_delcookie( cookie );
    if ( rc == 0 ) {
	printf( "Cookie deleted\n" );
    } else {
	printf( "Failed, rc = %d\n", rc );
    }
}
