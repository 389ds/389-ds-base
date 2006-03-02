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
 * Use the DN to fetch the parent of the entry.
 * If the parent entry doesn't exist, keep working
 * up the DN until we hit "" or an backend suffix.
 *
 * ancestordn should be initialized before calling this function, and
 * should be empty
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

    /* first, check to see if the given sdn is empty or a root suffix of the
       given backend - if so, it has no parent */
    if (!slapi_sdn_isempty(sdn) && !slapi_be_issuffix( be, sdn )) {
        Slapi_DN ancestorndn;
        const char *ptr;

        /* assign ancestordn to the parent of the given dn - ancestordn will contain
           the "raw" unnormalized DN from the caller, so we can give back the DN
           in the same format as we received it */
        ptr = slapi_dn_find_parent(slapi_sdn_get_dn(sdn));
        /* assign the ancestordn dn pointer to the parent of dn from sdn - sdn "owns"
           the memory, but ancestordn points to it */
        slapi_sdn_set_dn_byref(ancestordn, ptr); /* free any previous contents */
        /* now, do the same for the normalized version */
        /* ancestorndn holds the normalized version for iteration purposes and
           because dn2entry needs the normalized dn */
        ptr = slapi_dn_find_parent(slapi_sdn_get_ndn(sdn));
        slapi_sdn_init_ndn_byref(&ancestorndn, ptr);

        /*
          At this point you may be wondering why I need both ancestorndn and
          ancestordn.  Because, with the slapi_sdn interface, you cannot set both
          the dn and ndn byref at the same time.  Whenever you call set_dn or set_ndn,
          it calls slapi_sdn_done which wipes out the previous contents.  I suppose I
          could have added another API to allow you to pass them both in.  Also, using
          slapi_sdn_get_ndn(ancestordn) every time would result in making a copy then
          normalizing the copy every time - not efficient.
          So, why not just use a char* for the ancestorndn?  Because dn2entry requires
          a Slapi_DN with the normalized dn.
        */

        /* stop when we get to "", or a backend suffix point */
        while (!e && !slapi_sdn_isempty(&ancestorndn) && !slapi_be_issuffix( be, &ancestorndn )) {
            /* find the entry - it uses the ndn, so no further conversion is necessary */
            e= dn2entry(be,&ancestorndn,txn,err);
            if (!e) {
                /* not found, so set ancestordn to its parent and try again */
                ptr = slapi_dn_find_parent(slapi_sdn_get_ndn(&ancestorndn));
                /* keep in mind that ptr points to the raw ndn pointer inside
                   ancestorndn which is still the ndn string "owned" by sdn, the
                   original dn we started with - we are careful not to touch
                   or change it */
                slapi_sdn_set_ndn_byref(&ancestorndn, ptr); /* wipe out the previous contents */
                /* now do the same for the unnormalized one */
                ptr = slapi_dn_find_parent(slapi_sdn_get_dn(ancestordn));
                slapi_sdn_set_dn_byref(ancestordn, ptr); /* wipe out the previous contents */
            }
        }

        slapi_sdn_done(&ancestorndn);
    }

    /* post conditions:
       e is the entry of the ancestor of sdn OR e is the suffix entry
       OR e is NULL
       ancestordn contains the unnormalized DN of e or is empty */
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

