/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <string.h>
#include "slapi-private.h"

#define	LS_INCRSIZE	256

/*
 * Function: addlenstr
 * Arguments: l - pointer to an allocated lenstr structure
 *            str - the (null-terminated) string to append
 * Returns: nothing
 * Description: Add "str" to the lenstr, increasing the size if needed.
 */
void
addlenstr( lenstr *l, const char *str )
{
    size_t len = strlen( str );

    if ( l->ls_buf == NULL ) {
	/* string is empty */
	l->ls_maxlen = ( len > LS_INCRSIZE ) ? len : LS_INCRSIZE;
	l->ls_len = len;
	l->ls_buf = slapi_ch_malloc( l->ls_maxlen + 1 );
	memcpy( l->ls_buf, str, len + 1 );
    } else {
	if ( l->ls_len + len > l->ls_maxlen ) {
	    l->ls_maxlen *= 2;
	    if (l->ls_maxlen < l->ls_len + len) {
		l->ls_maxlen += len;
	    }
	    l->ls_buf = slapi_ch_realloc( l->ls_buf, l->ls_maxlen + 1 );
	}
	memcpy( l->ls_buf + l->ls_len, str, len + 1 );
	l->ls_len += len;
    }
}



/*
 * Function: lenstr_free
 * Arguments: l - pointer to an allocated lenstr structure
 * Returns: nothing
 * Description: Free a lenstr.
 */
void
lenstr_free( lenstr **l )
{
    if ( NULL != l && NULL != *l ) {
	lenstr *tl = *l;
	if ( tl->ls_buf != NULL ) {
	    slapi_ch_free((void **) &tl->ls_buf );
	}
	slapi_ch_free((void **) l );
    }
}



/*
 * Function: lenstr_new
 * Returns: an empty, newly-allocated lenstr
 */
lenstr *
lenstr_new()
{
    lenstr *l;

    l = ( lenstr * ) slapi_ch_malloc( sizeof( lenstr ));
    l->ls_buf = NULL;
    l->ls_len = l->ls_maxlen = 0;
    return l;
}
    

