/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* dn2entry.c - given a dn return an entry */

#include "back-ldbm.h"

/*
 * Fetch the entry for this DN.
 *
 * Retuns NULL of the entry doesn't exist
 */
struct backentry *
dn2entry(
    Slapi_Backend *be,
    const Slapi_DN	*sdn,
    back_txn		*txn,
    int			*err
)
{
	ldbm_instance *inst;
	struct berval		ndnv;
	struct backentry	*e = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dn2entry \"%s\"\n", slapi_sdn_get_dn(sdn), 0, 0 );

	inst = (ldbm_instance *) be->be_instance_info;

	*err = 0;
	ndnv.bv_val = (void*)slapi_sdn_get_ndn(sdn); /* jcm - Had to cast away const */
	ndnv.bv_len = slapi_sdn_get_ndn_len(sdn);

	e = cache_find_dn(&inst->inst_cache, ndnv.bv_val, ndnv.bv_len);
	if (e == NULL)
	{
		/* convert dn to entry id */
		IDList	*idl = NULL;
		if ( (idl = index_read( be, "entrydn", indextype_EQUALITY, &ndnv, txn, err )) == NULL )
		{
			/* There's no entry with this DN. */
		}
		else
		{
			/* convert entry id to entry */
			if ( (e = id2entry( be, idl_firstid( idl ), txn, err )) != NULL )
			{
				/* Means that we found the entry OK */
			}
			else
			{
				/* Hmm. The DN mapped onto an EntryID, but that didn't map onto an Entry. */
				if ( *err != 0 && *err != DB_NOTFOUND )
				{
					/* JCM - Not sure if this is ever OK or not. */
				}
				else
				{
					/*
					 * this is pretty bad anyway. the dn was in the
					 * entrydn index, but we could not read the entry
					 * from the id2entry index. what should we do?
					 */
					LDAPDebug( LDAP_DEBUG_ANY,
					    "dn2entry: the dn was in the entrydn index (id %lu), "
					    "but it did not exist in id2entry.\n",
					    (u_long)idl_firstid( idl ), 0, 0 );
				}
			}
			slapi_ch_free((void**)&idl);
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= dn2entry %p\n", e, 0, 0 );
	return( e );
}

/*
 * dn2entry_or_ancestor - look up dn in the cache/indexes and return the
 * corresponding entry. If the entry is not found, this function returns NULL
 * and sets ancestordn to the DN of highest entry in the tree matched.
 *
 * ancestordn should be initialized before calling this function.
 * 
 * When the caller is finished with the entry returned, it should return it
 * to the cache:
 *  e = dn2entry_or_ancestor( ... );
 *  if ( NULL != e ) {
 *		cache_return( &inst->inst_cache, &e );
 *	}
 */
struct backentry *
dn2entry_or_ancestor(
    Slapi_Backend	*be,
    const Slapi_DN	*sdn,
    Slapi_DN 	*ancestordn,
    back_txn		*txn,
    int			*err
)
{
	struct backentry *e;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dn2entry_or_ancestor \"%s\"\n", slapi_sdn_get_dn(sdn), 0, 0 );

	/*
	 * Fetch the entry asked for.
	 */

	e= dn2entry(be,sdn,txn,err);

	if(e==NULL)
	{
		/*
		 * could not find the entry named. crawl back up the dn and
		 * stop at the first ancestor that does exist, or when we get
		 * to the suffix.
		 */
		e= dn2ancestor(be,sdn,ancestordn,txn,err);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= dn2entry_or_ancestor %p\n", e, 0, 0 );
	return( e );
}

/*
 * Use the DN to fetch the parent of the entry.
 * If the parent entry doesn't exist, keep working
 * up the DN until we hit "" or an backend suffix.
 *
 * ancestordn should be initialized before calling this function.
 *
 * Returns NULL for no entry found.
 *
 * When the caller is finished with the entry returned, it should return it
 * to the cache:
 *  e = dn2ancestor( ... );
 *  if ( NULL != e ) {
 *		cache_return( &inst->inst_cache, &e );
 *	}
 */
struct backentry *
dn2ancestor(
    Slapi_Backend *be,
    const Slapi_DN	*sdn,
	Slapi_DN *ancestordn,
    back_txn		*txn,
    int			*err
)
{
	struct backentry *e = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> dn2ancestor \"%s\"\n", slapi_sdn_get_dn(sdn), 0, 0 );

	/* stop when we get to "", or a backend suffix point */
	slapi_sdn_done(ancestordn);	/* free any previous contents */
    slapi_sdn_get_backend_parent(sdn,ancestordn,be);
	if ( !slapi_sdn_isempty(ancestordn) )
	{
		Slapi_DN *newsdn = slapi_sdn_dup(ancestordn);
		e = dn2entry_or_ancestor( be, newsdn, ancestordn, txn, err );
		slapi_sdn_free(&newsdn);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= dn2ancestor %p\n", e, 0, 0 );
	return( e );
}

/*
 * Use uniqueid2entry or dn2entry to fetch an entry from the cache,
 * make a copy of it, and stash it in the pblock.
 */
int
get_copy_of_entry(Slapi_PBlock *pb, const entry_address *addr, back_txn *txn, int plock_parameter, int must_exist) /* JCM - Move somewhere more appropriate */
{
	int	err= 0;
	int rc= LDAP_SUCCESS;
	backend *be;
	struct backentry *entry;

	slapi_pblock_get( pb, SLAPI_BACKEND, &be);

	if( addr->uniqueid!=NULL)
	{
		entry = uniqueid2entry(be, addr->uniqueid, txn, &err );
	}
	else
	{
		Slapi_DN sdn;
		slapi_sdn_init_dn_byref (&sdn, addr->dn); /* We assume that the DN is not normalized */
		entry = dn2entry( be, &sdn, txn, &err );
		slapi_sdn_done (&sdn);
	}
	if ( 0 != err && DB_NOTFOUND != err )
	{
		if(must_exist)
		{
			LDAPDebug( LDAP_DEBUG_ANY, "Operation error fetching %s (%s), error %d.\n", 
				       addr->dn, (addr->uniqueid==NULL?"null":addr->uniqueid), err );
		}
		rc= LDAP_OPERATIONS_ERROR;
	}
	else
	{
		/* If an entry is found, copy it into the PBlock. */
		if(entry!=NULL)
		{
			ldbm_instance *inst;
	    	slapi_pblock_set( pb, plock_parameter, slapi_entry_dup(entry->ep_entry));
			inst = (ldbm_instance *) be->be_instance_info;
		    cache_return( &inst->inst_cache, &entry );
		}
	}
	/* JCMREPL - Free the backentry? */
	return rc;
}

void
done_with_pblock_entry(Slapi_PBlock *pb, int plock_parameter) /* JCM - Move somewhere more appropriate */
{
	Slapi_Entry *entry;
	slapi_pblock_get( pb, plock_parameter, &entry);
	if(entry!=NULL)
	{
		slapi_entry_free(entry);
		entry= NULL;
		slapi_pblock_set( pb, plock_parameter, entry);
	}
}

