/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
		PR_DestroyLock( ep->ep_mutexp );
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
#ifdef LDAP_CACHE_DEBUG
	ep->debug_sig = 0x23232323;
#endif

	return( ep );
}

struct backentry *
backentry_dup( struct backentry *e )
{
	struct backentry	*ec;

	ec = (struct backentry *) slapi_ch_calloc( 1, sizeof(struct backentry) );
	ec->ep_id = e->ep_id;
	ec->ep_entry = slapi_entry_dup( e->ep_entry );
	ec->ep_state = ENTRY_STATE_NOTINCACHE;
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
