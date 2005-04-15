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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
    

