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
    
#ifdef notdef /* this was some testing code... */
{
    char *ck, *r, *d, *p;
    int rc;

    ck = dsgw_get_auth_cookie();
    rc = dsgw_parse_cookie( ck, &r, &d );
    if ( rc == 0 ) {
	(void) dsgw_ckdn2passwd( r, d, &p );
	printf( "Got pw of <%s>\n", ( p == NULL ) ? "NULL" : p );
    }
}
#endif /* notdef */
    printf( "Dump the cookie database\n" );

    dsgw_traverse_db();
}
