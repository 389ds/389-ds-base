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

/* backentry.c - wrapper routines to deal with entries */

#include "back-ldbm.h"

void
backentry_free( struct backentry **bep )
{
	struct backentry *ep;
	if ( NULL == bep || NULL == *bep ) {
		return;
	}
	ep = *bep;
	if ( ep->ep_entry != NULL ) {
		slapi_entry_free( ep->ep_entry );
	}
	if ( ep->ep_mutexp != NULL ) {
		PR_DestroyMonitor( ep->ep_mutexp );
	}
	slapi_ch_free( (void**)&ep );
	*bep = NULL;
}

struct backentry *
backentry_alloc()
{
	struct backentry	*ec;
	ec = (struct backentry *) slapi_ch_calloc( 1, sizeof(struct backentry) ) ;
	ec->ep_state = ENTRY_STATE_NOTINCACHE;
	ec->ep_type = CACHE_TYPE_ENTRY;
#ifdef LDAP_CACHE_DEBUG
	ec->debug_sig = 0x45454545;
#endif
	return ec;
}

void backentry_clear_entry( struct backentry *ep )
{
	if (ep)
	{
		ep->ep_entry = NULL;
	}
}

struct backentry *
backentry_init( Slapi_Entry *e )
{
	struct backentry	*ep;

	ep = (struct backentry *) slapi_ch_calloc( 1, sizeof(struct backentry) );
	ep->ep_entry= e;
	ep->ep_state = ENTRY_STATE_NOTINCACHE;
	ep->ep_type = CACHE_TYPE_ENTRY;
#ifdef LDAP_CACHE_DEBUG
	ep->debug_sig = 0x23232323;
#endif

	return( ep );
}

struct backentry *
backentry_dup( struct backentry *e )
{
	struct backentry	*ec;

	if (NULL == e) {
		return NULL;
	}

	ec = (struct backentry *) slapi_ch_calloc( 1, sizeof(struct backentry) );
	ec->ep_id = e->ep_id;
	ec->ep_entry = slapi_entry_dup( e->ep_entry );
	ec->ep_state = ENTRY_STATE_NOTINCACHE;
	ec->ep_type = CACHE_TYPE_ENTRY;
#ifdef LDAP_CACHE_DEBUG
	ec->debug_sig = 0x12121212;
#endif

	return( ec );
}

char *
backentry_get_ndn(const struct backentry *e)
{
    return (char *)slapi_sdn_get_ndn(slapi_entry_get_sdn_const(e->ep_entry));
}

const Slapi_DN *
backentry_get_sdn(const struct backentry *e)
{
    return slapi_entry_get_sdn_const(e->ep_entry);
}

void
backdn_free( struct backdn **bdn )
{
	if ( NULL == bdn || NULL == *bdn ) {
		return;
	}
	slapi_sdn_free(&((*bdn)->dn_sdn));
	slapi_ch_free( (void **)bdn );
	*bdn = NULL;
}

struct backdn *
backdn_init( Slapi_DN *sdn, ID id, int to_remove_from_hash )
{
	struct backdn	*bdn;

	bdn = (struct backdn *) slapi_ch_calloc( 1, sizeof(struct backdn) );
	bdn->dn_sdn = sdn;
	bdn->ep_id = id;
	bdn->ep_size = slapi_sdn_get_size(sdn);
	bdn->ep_type = CACHE_TYPE_DN;
	if (!to_remove_from_hash) {
		bdn->ep_state = ENTRY_STATE_NOTINCACHE;
	}

	return( bdn );
}

