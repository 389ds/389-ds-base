/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( ultrix ) || defined( nextstep )

#include <string.h>


char *strdup( char *s )
{
        char    *p;

        if ( (p = (char *) malloc( strlen( s ) + 1 )) == NULL )
                return( NULL );

        strcpy( p, s );

        return( p );
}

#else
typedef int SHUT_UP_DAMN_COMPILER;
#endif /* ultrix || nextstep */
