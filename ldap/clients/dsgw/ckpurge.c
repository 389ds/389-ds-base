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

main()
{
    char *p;
    time_t expires;
    char dn[ 512 ];
    char pw[ 512 ];
    char expsec[ 512 ];
    int np = 0;
    time_t last;
    FILE *fp;
    
    printf( "purge the cookie database\n" );

    fp = dsgw_opencookiedb();
    last = dsgw_getlastpurged( fp );
    dsgw_closecookiedb( fp );
    printf( "database was last purged at %s\n", ctime( &last ));
    np = dsgw_purgedatabase( NULL );
    printf( "%d records purged\n", np );
}
