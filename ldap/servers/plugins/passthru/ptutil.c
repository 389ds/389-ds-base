/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ptutil.c - utility functions for Pass Through Authentication
 *
 */

#include "passthru.h"


/*
 * Convert a char * array into a struct berval * array.
 * Always succeeds.
 */
struct berval **
passthru_strs2bervals( char **ss )
{
    int			i;
    struct berval	**bvs;

    if ( ss == NULL || ss[0] == NULL ) {
	return( NULL );
    }

    for ( i = 0; ss[i] != NULL; ++i ) {
	;
    }

    bvs = (struct berval **)slapi_ch_calloc( i + 1, sizeof( struct berval * ));
    for ( i = 0; ss[i] != NULL; ++i ) {
	bvs[i] = (struct berval *)slapi_ch_malloc( sizeof( struct berval ));
	bvs[i]->bv_val = slapi_ch_strdup( ss[i] );
	bvs[i]->bv_len = strlen( ss[i] );
    }

    return( bvs );
}


/*
 * Convert a struct berval * array into a char * array.
 * Always succeeds.
 */
char **
passthru_bervals2strs( struct berval **bvs )
{
    int			i;
    char		**strs;

    if ( bvs == NULL || bvs[0] == NULL ) {
	return( NULL );
    }

    for ( i = 0; bvs[i] != NULL; ++i ) {
	;
    }

    strs = (char **)slapi_ch_calloc( i + 1, sizeof( char * ));
    for ( i = 0; bvs[i] != NULL; ++i ) {
	strs[i] = slapi_ch_strdup( bvs[i]->bv_val );
    }

    return( strs );
}


void
passthru_free_bervals( struct berval **bvs )
{
    int		i;

    if ( bvs != NULL ) {
	for ( i = 0; bvs[ i ] != NULL; ++i ) {
	    slapi_ch_free( (void **)&bvs[ i ] );
	}
    }
    slapi_ch_free( (void **)&bvs );
}


char *
passthru_urlparse_err2string( int err )
{
    char	*s;

    switch( err ) {
    case 0:
	s = "no error";
	break;
    case LDAP_URL_ERR_NOTLDAP:
	s = "missing ldap:// or ldaps://";
	break;
    case LDAP_URL_ERR_NODN:
	s = "missing suffix";
	break;
    case LDAP_URL_ERR_BADSCOPE:
	s = "invalid search scope";
	break;
    case LDAP_URL_ERR_MEM:
	s = "unable to allocate memory";
	break;
    case LDAP_URL_ERR_PARAM:
	s = "bad parameter to an LDAP URL function";
	break;
    }

    return( s );
}
