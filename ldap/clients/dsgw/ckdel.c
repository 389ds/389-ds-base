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
