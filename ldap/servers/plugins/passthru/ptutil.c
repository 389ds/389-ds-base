/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
