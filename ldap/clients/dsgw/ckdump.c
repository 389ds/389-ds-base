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
