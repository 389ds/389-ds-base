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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* Common IDL code, used in both old and new indexing schemes */

#include "back-ldbm.h"

size_t idl_sizeof(IDList *idl)
{
	if (NULL == idl) {
		return 0;
	}
	return (2 + idl->b_nmax) * sizeof(ID);
}

NIDS idl_length(IDList *idl)
{
	if (NULL == idl) {
		return 0;
	}
    return (idl->b_nmax == ALLIDSBLOCK) ? UINT_MAX : idl->b_nids;
}

int idl_is_allids(IDList *idl)
{
	if (NULL == idl) {
		return 0;
	}
    return (idl->b_nmax == ALLIDSBLOCK);
}

IDList *
idl_alloc( NIDS nids )
{
	IDList	*new;

	/* nmax + nids + space for the ids */
	new = (IDList *) slapi_ch_calloc( (2 + nids), sizeof(ID) );
	new->b_nmax = nids;
	new->b_nids = 0;

	return( new );
}

IDList	*
idl_allids( backend *be )
{
	IDList	*idl;

	idl = idl_alloc( 0 );
	idl->b_nmax = ALLIDSBLOCK;
	idl->b_nids = next_id_get( be );

	return( idl );
}

void
idl_free( IDList *idl ) /* JCM - pass in ** */
{
	if ( idl == NULL ) {
		return;
	}

	slapi_ch_free((void**)&idl );
}


/*
 * idl_append - append an id to an id list.
 *
 * Warning: The ID List must be maintained in order.
 * Use idl_insert if the id may not 
 * 
 * returns
 *    0 - appended
 *    1 - already in there
 *    2 - not enough room
 */

int
idl_append( IDList *idl, ID id)
{
	if (NULL == idl) {
		return 2;
	}
	if ( ALLIDS( idl ) || ( (idl->b_nids) && (idl->b_ids[idl->b_nids - 1] == id)) ) {
		return( 1 );	/* already there */
	}

	if ( idl->b_nids == idl->b_nmax ) {
		return( 2 );	/* not enough room */
	}

	idl->b_ids[idl->b_nids] = id;
	idl->b_nids++;

	return( 0 );
}

/* Append an ID to an IDL, realloc-ing the space if needs be */
/* ID presented is not to be already in the IDL. */
/* moved from idl_new.c */
int
idl_append_extend(IDList **orig_idl, ID id)
{
	IDList *idl = *orig_idl;

	if (idl == NULL) {
		idl = idl_alloc(32); /* used to be 0 */
		idl_append(idl, id);

		*orig_idl = idl;
		return 0;
	}

	if ( idl->b_nids == idl->b_nmax ) {
		/* No more room, need to extend */
		/* Allocate new IDL with twice the space of this one */
		IDList *idl_new = NULL;
		idl_new = idl_alloc(idl->b_nmax * 2);
		if (NULL == idl_new) {
			return ENOMEM;
		}
		/* copy over the existing contents */
		idl_new->b_nids = idl->b_nids;
		memcpy(idl_new->b_ids, idl->b_ids, sizeof(ID) * idl->b_nids);
		idl_free(idl);
		idl = idl_new;
	}

	idl->b_ids[idl->b_nids] = id;
	idl->b_nids++;
	*orig_idl = idl;

	return 0;
}

static IDList *
idl_dup( IDList *idl )
{
	IDList	*new;

	if ( idl == NULL ) {
		return( NULL );
	}

	new = idl_alloc( idl->b_nmax );
	SAFEMEMCPY( (char *) new, (char *) idl, (idl->b_nmax + 2)
	    * sizeof(ID) );

	return( new );
}

static IDList *
idl_min( IDList *a, IDList *b )
{
	return( a->b_nids > b->b_nids ? b : a );
}

int
idl_id_is_in_idlist(IDList *idl, ID id)
{
    NIDS i;
    if (NULL == idl || NOID == id) {
        return 0; /* not in the list */
    }
    if (ALLIDS(idl)) {
        return 1; /* in the list */
    }
    for (i = 0; i < idl->b_nids; i++) {
        if (id == idl->b_ids[i]) {
            return 1; /* in the list */
        }
    }
    return 0; /* not in the list */
}

/*
 * idl_intersection - return a intersection b
 */
IDList *
idl_intersection(
    backend *be,
    IDList		*a,
    IDList		*b
)
{
	NIDS	ai, bi, ni;
	IDList	*n;

	if ( a == NULL || b == NULL ) {
		return( NULL );
	}
	if ( ALLIDS( a ) ) {
		slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
		return( idl_dup( b ) );
	}
	if ( ALLIDS( b ) ) {
		slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
		return( idl_dup( a ) );
	}

	n = idl_dup( idl_min( a, b ) );

	for ( ni = 0, ai = 0, bi = 0; ai < a->b_nids; ai++ ) {
		for ( ; bi < b->b_nids && b->b_ids[bi] < a->b_ids[ai]; bi++ )
			;	/* NULL */

		if ( bi == b->b_nids ) {
			break;
		}

		if ( b->b_ids[bi] == a->b_ids[ai] ) {
			n->b_ids[ni++] = a->b_ids[ai];
		}
	}

	if ( ni == 0 ) {
		idl_free( n );
		return( NULL );
	}
	n->b_nids = ni;

	return( n );
}

/*
 * idl_union - return a union b
 */

IDList *
idl_union(
    backend *be,
    IDList		*a,
    IDList		*b
)
{
	NIDS	ai, bi, ni;
	IDList	*n;

	if ( a == NULL ) {
		return( idl_dup( b ) );
	}
	if ( b == NULL ) {
		return( idl_dup( a ) );
	}
	if ( ALLIDS( a ) || ALLIDS( b ) ) {
		return( idl_allids( be ) );
	}

	if ( b->b_nids < a->b_nids ) {
		n = a;
		a = b;
		b = n;
	}

	n = idl_alloc( a->b_nids + b->b_nids );

	for ( ni = 0, ai = 0, bi = 0; ai < a->b_nids && bi < b->b_nids; ) {
		if ( a->b_ids[ai] < b->b_ids[bi] ) {
			n->b_ids[ni++] = a->b_ids[ai++];
		} else if ( b->b_ids[bi] < a->b_ids[ai] ) {
			n->b_ids[ni++] = b->b_ids[bi++];
		} else {
			n->b_ids[ni++] = a->b_ids[ai];
			ai++, bi++;
		}
	}

	for ( ; ai < a->b_nids; ai++ ) {
		n->b_ids[ni++] = a->b_ids[ai];
	}
	for ( ; bi < b->b_nids; bi++ ) {
		n->b_ids[ni++] = b->b_ids[bi];
	}
	n->b_nids = ni;

	return( n );
}

/*
 * idl_notin - return a intersection ~b (or a minus b)
 * DB --- changed the interface of this function (no code called it),
 * such that it can modify IDL a in place (it'll always be the same
 * or smaller than the a passed in if not allids). 
 * If a new list is generated, it's returned in new_result and the function
 * returns 1. Otherwise the result remains in a, and the function returns 0.
 * The intention is to optimize for the interesting case in filterindex.c
 * where we are computing foo AND NOT bar, and both foo and bar are not allids.
 */

int
idl_notin(
    backend *be,
    IDList 		*a,
    IDList 		*b,
    IDList **new_result
)
{
	NIDS	ni, ai, bi;
	IDList	*n;
	*new_result = NULL;

	if ( a == NULL ) {
		return( 0 );
	}
	if ( b == NULL || ALLIDS( b ) ) {
		*new_result = idl_dup( a );
		return( 1 );
	}

	if ( ALLIDS( a ) ) { /* Not convinced that this code is really worth it */
		/* It's trying to do allids notin b, where maxid is smaller than some size */
		n = idl_alloc( SLAPD_LDBM_MIN_MAXIDS );
		ni = 0;

		for ( ai = 1, bi = 0; ai < a->b_nids && ni < n->b_nmax &&
		    bi < b->b_nmax; ai++ ) {
			if ( b->b_ids[bi] == ai ) {
				bi++;
			} else {
				n->b_ids[ni++] = ai;
			}
		}

		for ( ; ai < a->b_nids && ni < n->b_nmax; ai++ ) {
			n->b_ids[ni++] = ai;
		}

		if ( ni == n->b_nmax ) {
			idl_free( n );
			*new_result = idl_allids( be );
		} else {
			n->b_nids = ni;
			*new_result = n;
		}
		return( 1 );
	}

	/* This is the case we're interested in, we want to detect where a and b don't overlap */
	{
		size_t ahii, aloi, bhii, bloi;
		size_t ahi, alo, bhi, blo;
		int aloblo, ahiblo, alobhi, ahibhi;

		aloi = bloi = 0;
		ahii = a->b_nids - 1;
		bhii = b->b_nids - 1;

		ahi = a->b_ids[ahii];
		alo = a->b_ids[aloi];
		bhi = b->b_ids[bhii];
		blo = b->b_ids[bloi];
		/* if the ranges don't overlap, we're done, current a is the result */
		aloblo = alo < blo;
		ahiblo = ahi < blo;
		alobhi = ahi > bhi;
		ahibhi = alo > bhi;
		if ( (aloblo & ahiblo) || (alobhi & ahibhi) ) {
			return 0;
		} else {
			/* Do what we did before */
			n = idl_dup( a );

			ni = 0;
			for ( ai = 0, bi = 0; ai < a->b_nids; ai++ ) {
					for ( ; bi < b->b_nids && b->b_ids[bi] < a->b_ids[ai];
						bi++ ) {
							;       /* NULL */
					}

					if ( bi == b->b_nids ) {
							break;
					}

					if ( b->b_ids[bi] != a->b_ids[ai] ) {
							n->b_ids[ni++] = a->b_ids[ai];
					}
			}

			for ( ; ai < a->b_nids; ai++ ) {
					n->b_ids[ni++] = a->b_ids[ai];
			}
			n->b_nids = ni;

			*new_result =  n;
			return( 1 );
		}
	}
}

ID
idl_firstid( IDList *idl )
{
	if ( idl == NULL || idl->b_nids == 0 ) {
		return( NOID );
	}

	if ( ALLIDS( idl ) ) {
		return( idl->b_nids == 1 ? NOID : 1 );
	}

	return( idl->b_ids[0] );
}

ID
idl_nextid( IDList *idl, ID id )
{
	NIDS	i;

	if (NULL == idl) {
		return NOID;
	}
	if ( ALLIDS( idl ) ) {
		return( ++id < idl->b_nids ? id : NOID );
	}

	for ( i = 0; i < idl->b_nids && idl->b_ids[i] < id; i++ ) {
		;	/* NULL */
	}
	i++;

	if ( i >= idl->b_nids ) {
		return( NOID );
	} else {
		return( idl->b_ids[i] );
	}
}

/* Make an ID list iterator */
idl_iterator idl_iterator_init(const IDList *idl)
{
	return (idl_iterator) 0;
}

idl_iterator idl_iterator_increment(idl_iterator *i)
{
	size_t t = (size_t) *i;
	t += 1;
	*i = (idl_iterator) t;
	return *i;
}

idl_iterator idl_iterator_decrement(idl_iterator *i)
{
	size_t t = (size_t) *i;
	if (t > 0) {
		t -= 1;
	}
	*i = (idl_iterator) t;
	return *i;
}

ID idl_iterator_dereference(idl_iterator i, const IDList *idl)
{
	if ( (NULL == idl) || (i >= idl->b_nids)) {
		return NOID;
	}
	if (ALLIDS(idl)) {
		return (ID) i + 1;
	} else {
		return idl->b_ids[i];
	}
}

ID idl_iterator_dereference_increment(idl_iterator *i, const IDList *idl)
{
	ID t = idl_iterator_dereference(*i,idl);
	idl_iterator_increment(i);
	return t;
}

