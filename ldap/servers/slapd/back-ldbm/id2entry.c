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
/* id2entry.c - routines to deal with the id2entry index */

#include "back-ldbm.h"

/* 
 * The caller MUST check for DB_LOCK_DEADLOCK and DB_RUNRECOVERY returned
 */
int
id2entry_add_ext( backend *be, struct backentry *e, back_txn *txn, int encrypt  )
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    DB     *db = NULL;
    DB_TXN *db_txn = NULL;
    DBT    data = {0};
    DBT    key = {0};
    int    len, rc;
    char   temp_id[sizeof(ID)];
	struct backentry *encrypted_entry = NULL;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> id2entry_add( %lu, \"%s\" )\n",
        (u_long)e->ep_id, backentry_get_ndn(e), 0 );

    if ( (rc = dblayer_get_id2entry( be, &db )) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open/create id2entry\n",
            0, 0, 0 );
        return( -1 );
    }

    id_internal_to_stored(e->ep_id,temp_id);

    key.dptr = temp_id;
    key.dsize = sizeof(temp_id);

	/* Encrypt attributes in this entry if necessary */
	if (encrypt) {
		rc = attrcrypt_encrypt_entry(be, e, &encrypted_entry);
		if (rc) {
			LDAPDebug( LDAP_DEBUG_ANY, "attrcrypt_encrypt_entry failed in id2entry_add\n",
				0, 0, 0 );
			return ( -1 );
		}
	}

	{
		Slapi_Entry *entry_to_use = encrypted_entry ? encrypted_entry->ep_entry : e->ep_entry;
		data.dptr = slapi_entry2str_with_options( entry_to_use, &len, SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID);
		data.dsize = len + 1;
		/* If we had an encrypted entry, we no longer need it */
		if (encrypted_entry) {
			backentry_free(&encrypted_entry);
		}
	}

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }

    /* call pre-entry-store plugin */
    plugin_call_entrystore_plugins( (char **) &data.dptr, &data.dsize );

    /* store it  */
    rc = db->put( db, db_txn, &key, &data, 0);
    /* DBDB looks like we're freeing memory allocated by another DLL, which is bad */
    free( data.dptr );

    dblayer_release_id2entry( be, db );

    if (0 == rc)
    {
        /* DBDB the fact that we don't check the return code here is
         * indicitive that there may be a latent race condition lurking
         * ---what happens if the entry is already in the cache by this point?
         */
        /* 
         * For ldbm_back_add and ldbm_back_modify, this entry had been already
         * reserved as a tentative entry.  So, it should be safe.
         * For ldbm_back_modify, the original entry having the same dn/id
         * should be in the cache.  Thus, this entry e won't be put into the
         * entry cache.  It'll be added by cache_replace.
         */
        (void) cache_add( &inst->inst_cache, e, NULL );
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= id2entry_add %d\n", rc, 0, 0 );
    return( rc );
}

int
id2entry_add( backend *be, struct backentry *e, back_txn *txn  )
{
	return id2entry_add_ext(be,e,txn,1);
}

/* 
 * The caller MUST check for DB_LOCK_DEADLOCK and DB_RUNRECOVERY returned
 */
int
id2entry_delete( backend *be, struct backentry *e, back_txn *txn )
{
    DB     *db = NULL;
    DB_TXN *db_txn = NULL;
    DBT    key = {0};
    int    rc;
    char temp_id[sizeof(ID)];

    LDAPDebug( LDAP_DEBUG_TRACE, "=> id2entry_delete( %lu, \"%s\" )\n",
        (u_long)e->ep_id, backentry_get_ndn(e), 0 );

    if ( (rc = dblayer_get_id2entry( be, &db )) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open/create id2entry\n",
            0, 0, 0 );
        return( -1 );
    }

    id_internal_to_stored(e->ep_id,temp_id);

    key.dptr = temp_id;
    key.dsize = sizeof(temp_id);        

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }

    rc = db->del( db,db_txn,&key,0 );
    dblayer_release_id2entry( be, db );

    LDAPDebug( LDAP_DEBUG_TRACE, "<= id2entry_delete %d\n", rc, 0, 0 );
    return( rc );
}

struct backentry *
id2entry( backend *be, ID id, back_txn *txn, int *err  )
{
    ldbm_instance    *inst = (ldbm_instance *) be->be_instance_info;
    DB               *db = NULL;
    DB_TXN           *db_txn = NULL;
    DBT              key = {0};
    DBT              data = {0};
    struct backentry *e;
    Slapi_Entry      *ee;
    char             temp_id[sizeof(ID)];

    LDAPDebug( LDAP_DEBUG_TRACE, "=> id2entry( %lu )\n", (u_long)id, 0, 0 );

    if ( (e = cache_find_id( &inst->inst_cache, id )) != NULL ) {
        LDAPDebug( LDAP_DEBUG_TRACE, "<= id2entry %p (cache)\n", e, 0,
            0 );
        return( e );
    }

    if ( (*err = dblayer_get_id2entry( be, &db )) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open id2entry err %d\n",
            *err, 0, 0 );
        return( NULL );
    }


    id_internal_to_stored(id,temp_id);

    key.data = temp_id;
    key.size = sizeof(temp_id);

    /* DBDB need to improve this, we're mallocing, freeing, all over the place here */
    data.flags = DB_DBT_MALLOC;

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }
    do {
        *err = db->get( db, db_txn, &key, &data, 0 );
        if ( 0 != *err && 
             DB_NOTFOUND != *err && DB_LOCK_DEADLOCK != *err )
        {
            LDAPDebug( LDAP_DEBUG_ANY, "id2entry error %d\n",
                *err, 0, 0 );
        }
    }
    while ( DB_LOCK_DEADLOCK == *err && txn == NULL );

    if ( 0 != *err && DB_NOTFOUND != *err && DB_LOCK_DEADLOCK != *err )
    {
        LDAPDebug( LDAP_DEBUG_ANY, "id2entry get error %d\n",
            *err, 0, 0 );
        dblayer_release_id2entry( be, db );
        return( NULL );
    }

    if ( data.dptr == NULL ) {
        LDAPDebug( LDAP_DEBUG_TRACE, "<= id2entry( %lu ) not found\n",
            (u_long)id, 0, 0 );
        dblayer_release_id2entry( be, db );
        return( NULL );
    }

    /* call post-entry plugin */
    plugin_call_entryfetch_plugins( (char **) &data.dptr, &data.dsize );

    if ( (ee = slapi_str2entry( data.dptr, 0 )) != NULL ) {
        int retval = 0;
        struct backentry *imposter = NULL;

        PR_ASSERT(slapi_entry_get_uniqueid(ee) != NULL); /* All entries should have uniqueids */
        e = backentry_init( ee ); /* ownership of the entry is passed into the backentry */
        e->ep_id = id;

		/* Decrypt any encrypted attributes in this entry, before adding it to the cache */
		retval = attrcrypt_decrypt_entry(be, e);
		if (retval) {
			LDAPDebug( LDAP_DEBUG_ANY, "attrcrypt_decrypt_entry failed in id2entry\n",
            0, 0, 0 );
		}
		
		retval = cache_add( &inst->inst_cache, e, &imposter );
        if (1 == retval) {
            /* This means that someone else put the entry in the cache
            while we weren't looking ! So, we need to use the pointer
            returned and free the one we made earlier */
            if (imposter)
            {
                backentry_free(&e);
                e = imposter;
            }
        } else if (-1 == retval) {
            /* the entry is in idtable but not in dntable, i.e., the entry
             * could have been renamed */
            LDAPDebug( LDAP_DEBUG_TRACE, 
                "id2entry: failed to put entry (id %lu, dn %s) into entry cache\n",
                (u_long)id, backentry_get_ndn(e), 0 );
        }
    } else {
        LDAPDebug( LDAP_DEBUG_ANY, "str2entry returned NULL for id %lu, string=\"%s\"\n", (u_long)id, (char*)data.data, 0);
        e = NULL;
    }

    free( data.data );

    dblayer_release_id2entry( be, db );

    LDAPDebug( LDAP_DEBUG_TRACE, "<= id2entry( %lu ) %p (disk)\n", (u_long)id, e,
        0 );
    return( e );
}

