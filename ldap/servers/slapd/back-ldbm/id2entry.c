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

/* id2entry.c - routines to deal with the id2entry index */

#include "back-ldbm.h"

#define ID2ENTRY "id2entry"

/* 
 * The caller MUST check for DB_LOCK_DEADLOCK and DB_RUNRECOVERY returned
 */
int
id2entry_add_ext( backend *be, struct backentry *e, back_txn *txn, int encrypt  )
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    DB     *db = NULL;
    DB_TXN *db_txn = NULL;
    DBT    data;
    DBT    key;
    int    len, rc;
    char   temp_id[sizeof(ID)];
    struct backentry *encrypted_entry = NULL;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> id2entry_add( %lu, \"%s\" )\n",
                                 (u_long)e->ep_id, backentry_get_ndn(e), 0 );

    if ( (rc = dblayer_get_id2entry( be, &db )) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open/create id2entry\n",
            0, 0, 0 );
        rc = -1;
        goto done;
    }

    id_internal_to_stored(e->ep_id,temp_id);

    memset(&key, 0, sizeof(key));
    key.dptr = temp_id;
    key.dsize = sizeof(temp_id);

    /* Encrypt attributes in this entry if necessary */
    if (encrypt) {
        rc = attrcrypt_encrypt_entry(be, e, &encrypted_entry);
        if (rc) {
            LDAPDebug( LDAP_DEBUG_ANY, "attrcrypt_encrypt_entry failed in id2entry_add\n",
                0, 0, 0 );
            rc = -1;
            goto done;
        }
    }

    {
        int options = SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID;
        Slapi_Entry *entry_to_use = encrypted_entry ? encrypted_entry->ep_entry : e->ep_entry;
        memset(&data, 0, sizeof(data));
        if (entryrdn_get_switch())
        {
            struct backdn *oldbdn = NULL;
            Slapi_DN *sdn =
                          slapi_sdn_dup(slapi_entry_get_sdn_const(e->ep_entry));
            struct backdn *bdn = backdn_init(sdn, e->ep_id, 0);
            options |= SLAPI_DUMP_RDN_ENTRY;

            /* If the ID already exists in the DN cache && the DNs do not match,
             * replace it. */
            if (CACHE_ADD( &inst->inst_dncache, bdn, &oldbdn ) == 1) {
                if (slapi_sdn_compare(sdn, oldbdn->dn_sdn)) {
                    if (cache_replace( &inst->inst_dncache, oldbdn, bdn ) != 0) {
                        /* The entry was not in the cache for some reason (this
                         * should not happen since CACHE_ADD said it existed above). */
			LDAPDebug( LDAP_DEBUG_ANY, "id2entry_add_ext(): Entry disappeared "
                                   "from cache (%s)\n", oldbdn->dn_sdn, 0, 0 );
                    }
                }
                CACHE_RETURN(&inst->inst_dncache, &oldbdn); /* to free oldbdn */
            }

            CACHE_RETURN(&inst->inst_dncache, &bdn);
            LDAPDebug( LDAP_DEBUG_TRACE,
                   "=> id2entry_add (dncache) ( %lu, \"%s\" )\n",
                   (u_long)e->ep_id, slapi_entry_get_dn_const(e->ep_entry), 0 );
        }
        data.dptr = slapi_entry2str_with_options(entry_to_use, &len, options);
        data.dsize = len + 1;
    }

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }

    /* call pre-entry-store plugin */
    plugin_call_entrystore_plugins( (char **) &data.dptr, &data.dsize );

    /* store it  */
    rc = db->put( db, db_txn, &key, &data, 0);
    /* DBDB looks like we're freeing memory allocated by another DLL, which is bad */
    slapi_ch_free( &(data.dptr) );

    dblayer_release_id2entry( be, db );

    if (0 == rc)
    {
        if (entryrdn_get_switch()) {
            struct backentry *parententry = NULL;
            ID parentid = slapi_entry_attr_get_ulong(e->ep_entry, "parentid");
            const char *myrdn = slapi_entry_get_rdn_const(e->ep_entry);
            const char *parentdn = NULL;
            char *myparentdn = NULL;
            /* If the parent is in the cache, check the parent's DN and 
             * adjust to it if they don't match. (bz628300) */
            if (parentid && myrdn) {
                parententry = cache_find_id(&inst->inst_cache, parentid);
                if (parententry) {
                    parentdn = slapi_entry_get_dn_const(parententry->ep_entry);
                    if (parentdn) {
                        myparentdn = 
                         slapi_dn_parent(slapi_entry_get_dn_const(e->ep_entry));
                        if (myparentdn && PL_strcmp(parentdn, myparentdn)) {
                            Slapi_DN *sdn = slapi_entry_get_sdn(e->ep_entry);
                            char *newdn = NULL;
                            slapi_sdn_done(sdn);
                            newdn = slapi_ch_smprintf("%s,%s", myrdn, parentdn);
                            slapi_sdn_init_dn_passin(sdn, newdn);
                            slapi_sdn_get_ndn(sdn); /* to set ndn */
                        }
                        slapi_ch_free_string(&myparentdn);
                    }
                    CACHE_RETURN(&inst->inst_cache, &parententry);
                }
            }
        }
        /* 
         * For ldbm_back_add and ldbm_back_modify, this entry had been already
         * reserved as a tentative entry.  So, it should be safe.
         * For ldbm_back_modify, the original entry having the same dn/id
         * should be in the cache.  Thus, this entry e won't be put into the
         * entry cache.  It'll be added by cache_replace.
         */
        (void) CACHE_ADD( &inst->inst_cache, e, NULL );
    }

done:
    /* If we had an encrypted entry, we no longer need it */
    if (encrypted_entry) {
        backentry_free(&encrypted_entry);
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

    if (entryrdn_get_switch())
    {
        ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
        struct backdn *bdn = dncache_find_id(&inst->inst_dncache, e->ep_id);
        if (bdn) {
            slapi_log_error(SLAPI_LOG_CACHE, ID2ENTRY,
                            "dncache_find_id returned: %s\n", 
                            slapi_sdn_get_dn(bdn->dn_sdn));
            CACHE_REMOVE(&inst->inst_dncache, bdn);
            CACHE_RETURN(&inst->inst_dncache, &bdn);
        }
    }

    rc = db->del( db,db_txn,&key,0 );
    dblayer_release_id2entry( be, db );

    LDAPDebug( LDAP_DEBUG_TRACE, "<= id2entry_delete %d\n", rc, 0, 0 );
    return( rc );
}

struct backentry *
id2entry_ext( backend *be, ID id, back_txn *txn, int *err, int flags  )
{
    ldbm_instance    *inst = (ldbm_instance *) be->be_instance_info;
    DB               *db = NULL;
    DB_TXN           *db_txn = NULL;
    DBT              key = {0};
    DBT              data = {0};
    struct backentry *e = NULL;
    Slapi_Entry      *ee;
    char             temp_id[sizeof(ID)];

    slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY,
                    "=> id2entry(%lu)\n", (u_long)id);

    if ( (e = cache_find_id( &inst->inst_cache, id )) != NULL ) {
        slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY, 
                        "<= id2entry %p, dn \"%s\" (cache)\n",
                        e, backentry_get_ndn(e));
        goto bail;
    }

    if ( (*err = dblayer_get_id2entry( be, &db )) != 0 ) {
        slapi_log_error(SLAPI_LOG_FATAL, ID2ENTRY,
                        "Could not open id2entry err %d\n", *err);
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
        if ( (0 != *err) && 
             (DB_NOTFOUND != *err) && (DB_LOCK_DEADLOCK != *err) )
        {
            slapi_log_error(SLAPI_LOG_FATAL, ID2ENTRY, "db error %d (%s)\n",
                            *err, dblayer_strerror( *err ));
        }
    }
    while ( (DB_LOCK_DEADLOCK == *err) && (txn == NULL) );

    if ( (0 != *err) && (DB_NOTFOUND != *err) && (DB_LOCK_DEADLOCK != *err) )
    {
        if ( (DB_BUFFER_SMALL == *err) && (data.dptr == NULL) )
        {
            /* 
             * Now we are setting slapi_ch_malloc and its friends to libdb
             * by ENV->set_alloc in dblayer.c.  As long as the functions are 
             * used by libdb, it won't reach here.
             */
            slapi_log_error(SLAPI_LOG_FATAL, ID2ENTRY, 
                            "malloc failed in libdb; "
                            "terminating the server; OS error %d (%s)\n",
                            *err, slapd_system_strerror( *err ));
            exit (1);
        }
        dblayer_release_id2entry( be, db );
        return( NULL );
    }

    if ( data.dptr == NULL ) {
        slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY, 
                        "<= id2entry( %lu ) not found\n", (u_long)id);
        goto bail;
    }

    /* call post-entry plugin */
    plugin_call_entryfetch_plugins( (char **) &data.dptr, &data.dsize );

    if (entryrdn_get_switch()) {
        char *rdn = NULL;
        int rc = 0;

        /* rdn is allocated in get_value_from_string */
        rc = get_value_from_string((const char *)data.dptr, "rdn", &rdn);
        if (rc) {
            /* data.dptr may not include rdn: ..., try "dn: ..." */
            ee = slapi_str2entry( data.dptr, 0 );
        } else {
            char *dn = NULL;
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache, id);
            if (bdn) {
                dn = slapi_ch_strdup(slapi_sdn_get_dn(bdn->dn_sdn));
                slapi_log_error(SLAPI_LOG_CACHE, ID2ENTRY,
                                "dncache_find_id returned: %s\n", dn);
                CACHE_RETURN(&inst->inst_dncache, &bdn);
            } else {
                Slapi_DN *sdn = NULL;
                rc = entryrdn_lookup_dn(be, rdn, id, &dn, txn);
                if (rc) {
                    slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY,
                                    "id2entry: entryrdn look up failed "
                                    "(rdn=%s, ID=%d)\n", rdn, id);
                    /* Try rdn as dn. Could be RUV. */
                    dn = slapi_ch_strdup(rdn);
                }
                sdn = slapi_sdn_new_dn_byval((const char *)dn);
                bdn = backdn_init(sdn, id, 0);
                CACHE_ADD( &inst->inst_dncache, bdn, NULL );
                CACHE_RETURN(&inst->inst_dncache, &bdn);
                slapi_log_error(SLAPI_LOG_CACHE, ID2ENTRY,
                                "entryrdn_lookup_dn returned: %s, "
                                "and set to dn cache (id %d)\n", dn, id);
            }
            ee = slapi_str2entry_ext( (const char *)dn, data.dptr, 0 );
            slapi_ch_free_string(&rdn);
            slapi_ch_free_string(&dn);
        }
    } else {
        ee = slapi_str2entry( data.dptr, 0 );
    }

    if ( ee != NULL ) {
        int retval = 0;
        struct backentry *imposter = NULL;

        /* All entries should have uniqueids */
        PR_ASSERT(slapi_entry_get_uniqueid(ee) != NULL);
        /* ownership of the entry is passed into the backentry */
        e = backentry_init( ee );
        e->ep_id = id;
        slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY, 
                        "id2entry id: %d, dn \"%s\" -- adding it to cache\n",
                        id, backentry_get_ndn(e));

        /* Decrypt any encrypted attributes in this entry, 
         * before adding it to the cache */
        retval = attrcrypt_decrypt_entry(be, e);
        if (retval) {
            slapi_log_error(SLAPI_LOG_FATAL, ID2ENTRY,
                            "attrcrypt_decrypt_entry failed in id2entry\n");
        }
        
        retval = CACHE_ADD( &inst->inst_cache, e, &imposter );
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
            slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY,
                            "id2entry: failed to put entry (id %lu, dn %s) "
                            "into entry cache\n", (u_long)id,
                            backentry_get_ndn(e));
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, ID2ENTRY,
                        "str2entry returned NULL for id %lu, string=\"%s\"\n",
                        (u_long)id, (char*)data.data);
        e = NULL;
    }

bail:
    /* 
     * If return entry exists AND adding entrydn is requested AND
     * entryrdn switch is on, add the entrydn value.
     */
    if (e && e->ep_entry && (flags & ID2ENTRY_ADD_ENTRYDN) &&
        entryrdn_get_switch()) {
        Slapi_Attr *eattr = NULL;
        /* Check if entrydn is in the entry or not */
        if (slapi_entry_attr_find(e->ep_entry, "entrydn", &eattr)) {
            /* entrydn does not exist in the entry */
            char *entrydn = NULL;
            /* slapi_ch_strdup and slapi_dn_ignore_case never returns NULL */
            entrydn = slapi_ch_strdup(slapi_entry_get_dn_const(e->ep_entry));
            entrydn = slapi_dn_ignore_case(entrydn);
            slapi_entry_attr_set_charptr (e->ep_entry, "entrydn", entrydn);
            if (0 == slapi_entry_attr_find(e->ep_entry, "entrydn", &eattr)) {
                /* now entrydn should exist in the entry */
                /* Set it to operational attribute */
                eattr->a_flags = SLAPI_ATTR_FLAG_OPATTR;
            }
            slapi_ch_free_string(&entrydn);
        }
    }
    slapi_ch_free( &(data.data) );

    dblayer_release_id2entry( be, db );

    slapi_log_error(SLAPI_LOG_TRACE, ID2ENTRY,
                    "<= id2entry( %lu ) %p (disk)\n", (u_long)id, e);
    return( e );
}

struct backentry *
id2entry( backend *be, ID id, back_txn *txn, int *err  )
{
    return id2entry_ext(be, id, txn, err, 0);
}

