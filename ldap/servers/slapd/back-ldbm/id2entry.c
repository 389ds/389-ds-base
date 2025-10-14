/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* id2entry.c - routines to deal with the id2entry index */

#include "back-ldbm.h"

#define ID2ENTRY "id2entry"

/*
 * The caller MUST check for DBI_RC_RETRY and DBI_RC_RUNRECOVERY returned
 * If cache_res is not NULL, it stores the result of CACHE_ADD of the
 * entry cache.
 */
int
id2entry_add_ext(backend *be, struct backentry *e, back_txn *txn, int encrypt, int *cache_res)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    dbi_db_t *db = NULL;
    dbi_txn_t *db_txn = NULL;
    dbi_val_t data = {0};
    dbi_val_t key = {0};
    int len, rc;
    char temp_id[sizeof(ID)];
    struct backentry *encrypted_entry = NULL;
    char *entrydn = NULL;
    uint32_t esize;

    slapi_log_err(SLAPI_LOG_TRACE, "id2entry_add_ext", "=> ( %lu, \"%s\" )\n",
                  (u_long)e->ep_id, backentry_get_ndn(e));

    if ((rc = dblayer_get_id2entry(be, &db)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "id2entry_add_ext",
                      "Could not open/create id2entry\n");
        rc = -1;
        goto done;
    }

    id_internal_to_stored(e->ep_id, temp_id);

    memset(&key, 0, sizeof(key));
    key.dptr = temp_id;
    key.dsize = sizeof(temp_id);

    /* Encrypt attributes in this entry if necessary */
    if (encrypt) {
        rc = attrcrypt_encrypt_entry(be, e, &encrypted_entry);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "id2entry_add_ext",
                          "attrcrypt_encrypt_entry failed\n");
            rc = -1;
            goto done;
        }
    }

    {
        int options = SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID;
        Slapi_Entry *entry_to_use = encrypted_entry ? encrypted_entry->ep_entry : e->ep_entry;
        memset(&data, 0, sizeof(data));
        entrydn = slapi_entry_get_dn(entry_to_use);
        slapi_entry_attr_set_charptr(entry_to_use, SLAPI_ATTR_DS_ENTRYDN,
                entrydn);

        struct backdn *oldbdn = NULL;
        Slapi_DN *sdn =
            slapi_sdn_dup(slapi_entry_get_sdn_const(entry_to_use));
        struct backdn *bdn = backdn_init(sdn, e->ep_id, 0);
        options |= SLAPI_DUMP_RDN_ENTRY;

        /* If the ID already exists in the DN cache && the DNs do not match,
         * replace it. */
        if (CACHE_ADD(&inst->inst_dncache, bdn, &oldbdn) == 1) {
            if (slapi_sdn_compare(sdn, oldbdn->dn_sdn)) {
                if (cache_replace(&inst->inst_dncache, oldbdn, bdn) != 0) {
                    /* The entry was not in the cache for some reason (this
                     * should not happen since CACHE_ADD said it existed above). */
                    slapi_log_err(SLAPI_LOG_WARNING, "id2entry_add_ext",
                                  "Entry disappeared from cache (%s)\n",
                                  slapi_sdn_get_dn(oldbdn->dn_sdn));
                }
            }
            CACHE_RETURN(&inst->inst_dncache, &oldbdn); /* to free oldbdn */
        }

        CACHE_RETURN(&inst->inst_dncache, &bdn);
        slapi_log_err(SLAPI_LOG_TRACE,
                      "id2entry_add_ext", "(dncache) ( %lu, \"%s\" )\n",
                      (u_long)e->ep_id, slapi_entry_get_dn_const(entry_to_use));

        data.dptr = slapi_entry2str_with_options(entry_to_use, &len, options);
        data.dsize = len + 1;
    }

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }

    /* call pre-entry-store plugin */
    esize = (uint32_t)data.dsize;
    plugin_call_entrystore_plugins((char **)&data.dptr, &esize);
    data.dsize = esize;

    /* store it  */
    if (txn && txn->back_special_handling_fn) {
        rc = txn->back_special_handling_fn(be, BTXNACT_ID2ENTRY_ADD, db, &key, &data, txn);
    } else {
        rc = dblayer_db_op(be, db, db_txn, DBI_OP_PUT, &key, &data);
    }
    /* DBDB looks like we're freeing memory allocated by another DLL, which is bad */
    slapi_ch_free(&(data.dptr));

    dblayer_release_id2entry(be, db);

    if (0 == rc) {
        int cache_rc = 0;
        /* Putting the entry into the entry cache.
         * We don't use the encrypted entry here. */

        struct backentry *parententry = NULL;
        ID parentid = slapi_entry_attr_get_ulong(e->ep_entry, "parentid");
        const char *myrdn = slapi_entry_get_rdn_const(e->ep_entry);
        const char *parentdn = NULL;
        char *myparentdn = NULL;
        Slapi_Attr *eattr = NULL;
        /* If the parent is in the cache, check the parent's DN and
            * adjust to it if they don't match. (bz628300) */
        if (parentid && myrdn) {
            parententry = cache_find_id(&inst->inst_cache, parentid);
            if (parententry) {
                parentdn = slapi_entry_get_dn_const(parententry->ep_entry);
                if (parentdn) {
                    int is_tombstone = slapi_entry_flag_is_set(e->ep_entry,
                                                               SLAPI_ENTRY_FLAG_TOMBSTONE);
                    myparentdn = slapi_dn_parent_ext(
                        slapi_entry_get_dn_const(e->ep_entry),
                        is_tombstone);
                    if (myparentdn && PL_strcasecmp(parentdn, myparentdn)) {
                        Slapi_DN *sdn = slapi_entry_get_sdn(e->ep_entry);
                        char *newdn = NULL;
                        CACHE_LOCK(&inst->inst_cache);
                        slapi_sdn_done(sdn);
                        newdn = slapi_ch_smprintf("%s,%s", myrdn, parentdn);
                        slapi_sdn_init_dn_passin(sdn, newdn);
                        slapi_sdn_get_ndn(sdn); /* to set ndn */
                        CACHE_UNLOCK(&inst->inst_cache);
                    }
                    slapi_ch_free_string(&myparentdn);
                }
                CACHE_RETURN(&inst->inst_cache, &parententry);
            }
        }
        /*
         * Adding entrydn attribute value to the entry,
         * which should be done before adding the entry to the entry cache.
         * Note: since we removed entrydn from the entry before writing
         * it to the database, it is guaranteed not in the entry.
         */
        /* slapi_ch_strdup and slapi_dn_ignore_case never returns NULL */
        entrydn = slapi_ch_strdup(slapi_entry_get_dn_const(e->ep_entry));
        entrydn = slapi_dn_ignore_case(entrydn);
        slapi_entry_attr_set_charptr(e->ep_entry,
                                     LDBM_ENTRYDN_STR, entrydn);
        if (0 == slapi_entry_attr_find(e->ep_entry,
                                       LDBM_ENTRYDN_STR, &eattr)) {
            /* now entrydn should exist in the entry */
            /* Set it to operational attribute */
            eattr->a_flags = SLAPI_ATTR_FLAG_OPATTR;
        }
        slapi_ch_free_string(&entrydn);

        /*
         * For ldbm_back_add and ldbm_back_modify, this entry had been already
         * reserved as a tentative entry.  So, it should be safe.
         * For ldbm_back_modify, the original entry having the same dn/id
         * should be in the cache.  Thus, this entry e won't be put into the
         * entry cache.  It'll be added by cache_replace.
         */
        cache_rc = CACHE_ADD(&inst->inst_cache, e, NULL);
        if (cache_res) {
            *cache_res = cache_rc;
        }
    }

done:
    /* If we had an encrypted entry, we no longer need it.
     * Note: encrypted_entry is not in the entry cache. */
    if (encrypted_entry) {
        backentry_free(&encrypted_entry);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "id2entry_add_ext", "<= %d\n", rc);
    return (rc);
}

int
id2entry_add(backend *be, struct backentry *e, back_txn *txn)
{
    return id2entry_add_ext(be, e, txn, 1, NULL);
}

/*
 * The caller MUST check for DBI_RC_RETRY and DBI_RC_RUNRECOVERY returned
 */
int
id2entry_delete(backend *be, struct backentry *e, back_txn *txn)
{
    dbi_db_t *db = NULL;
    dbi_txn_t *db_txn = NULL;
    dbi_val_t key = {0};
    int rc;
    char temp_id[sizeof(ID)];

    slapi_log_err(SLAPI_LOG_TRACE, "id2entry_delete", "=>( %lu, \"%s\" )\n",
                  (u_long)e->ep_id, backentry_get_ndn(e));

    if ((rc = dblayer_get_id2entry(be, &db)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "id2entry_delete",
                      "Could not open/create id2entry\n");
        return (-1);
    }

    id_internal_to_stored(e->ep_id, temp_id);

    key.dptr = temp_id;
    key.dsize = sizeof(temp_id);

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }

    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    struct backdn *bdn = dncache_find_id(&inst->inst_dncache, e->ep_id);
    if (bdn) {
        slapi_log_err(SLAPI_LOG_CACHE, ID2ENTRY,
                      "dncache_find_id returned: %s\n",
                      slapi_sdn_get_dn(bdn->dn_sdn));
        CACHE_REMOVE(&inst->inst_dncache, bdn);
        CACHE_RETURN(&inst->inst_dncache, &bdn);
    }

    rc = dblayer_db_op(be, db, db_txn, DBI_OP_DEL, &key, 0);
    dblayer_release_id2entry(be, db);

    slapi_log_err(SLAPI_LOG_TRACE, "id2entry_delete", "<= %d\n", rc);
    return (rc);
}

struct backentry *
id2entry(backend *be, ID id, back_txn *txn, int *err)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    dbi_db_t *db = NULL;
    dbi_txn_t *db_txn = NULL;
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    struct backentry *e = NULL;
    Slapi_Entry *ee;
    char temp_id[sizeof(ID)];
    uint32_t esize;
    BackEntryWeightData t1 = {0};

    slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                  "=> id2entry(%lu)\n", (u_long)id);

    if ((e = cache_find_id(&inst->inst_cache, id)) != NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                      "<= id2entry %p, dn \"%s\" (cache)\n",
                      e, backentry_get_ndn(e));
        goto bail;
    }

    *err = dblayer_get_id2entry(be, &db);
    if ((*err != 0) || (NULL == db)) {
        slapi_log_err(SLAPI_LOG_ERR, ID2ENTRY,
                      "Could not open id2entry err %d\n", *err);
        return (NULL);
    }


    backentry_init_weight(&t1);
    id_internal_to_stored(id, temp_id);

    dblayer_value_set_buffer(be, &key, temp_id,  sizeof(temp_id));
    dblayer_value_init(be, &data);

    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }
    do {
        *err = dblayer_db_op(be, db, db_txn, DBI_OP_GET, &key, &data);
        if ((0 != *err) &&
            (DBI_RC_NOTFOUND != *err) && (DBI_RC_RETRY != *err)) {
            slapi_log_err(SLAPI_LOG_ERR, ID2ENTRY, "db error %d (%s)\n",
                          *err, dblayer_strerror(*err));
        }
    } while ((DBI_RC_RETRY == *err) && (txn == NULL));

    if ((0 != *err) && (DBI_RC_NOTFOUND != *err) && (DBI_RC_RETRY != *err)) {
        if ((DBI_RC_BUFFER_SMALL == *err) && (data.dptr == NULL)) {
            /*
             * Now we are setting slapi_ch_malloc and its friends to libdb
             * by ENV->set_alloc in dblayer.c.  As long as the functions are
             * used by libdb, it won't reach here.
             */
            slapi_log_err(SLAPI_LOG_CRIT, ID2ENTRY,
                          "Malloc failed in libdb; "
                          "terminating the server; OS error %d (%s)\n",
                          *err, slapd_system_strerror(*err));
            exit(1);
        }
        dblayer_release_id2entry(be, db);
        return (NULL);
    }

    if (data.dptr == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                      "<= id2entry( %lu ) not found\n", (u_long)id);
        goto bail;
    }

    /* call post-entry plugin */
    esize = (uint32_t)data.dsize;
    plugin_call_entryfetch_plugins((char **)&data.dptr, &esize);
    data.dsize = esize;

    char *rdn = NULL;
    int rc = 0;

    /* rdn is allocated in get_value_from_string */
    rc = get_value_from_string((const char *)data.dptr, "rdn", &rdn);
    if (rc) {
        /* data.dptr may not include rdn: ..., try "dn: ..." */
        ee = slapi_str2entry(data.dptr, SLAPI_STR2ENTRY_NO_ENTRYDN);
    } else {
        char *normdn = NULL;
        Slapi_RDN *srdn = NULL;
        struct backdn *bdn = dncache_find_id(&inst->inst_dncache, id);
        if (bdn) {
            normdn = slapi_ch_strdup(slapi_sdn_get_dn(bdn->dn_sdn));
            slapi_log_err(SLAPI_LOG_CACHE, ID2ENTRY,
                          "dncache_find_id returned: %s\n", normdn);
            CACHE_RETURN(&inst->inst_dncache, &bdn);
        } else {
            Slapi_DN *sdn = NULL;
            if (config_get_return_orig_dn() &&
                !get_value_from_string((const char *)data.dptr, SLAPI_ATTR_DS_ENTRYDN, &normdn))
            {
                srdn = slapi_rdn_new_all_dn(normdn);
            } else {
                rc = entryrdn_lookup_dn(be, rdn, id, &normdn, &srdn, txn);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                                  "id2entry: entryrdn look up failed "
                                  "(rdn=%s, ID=%d)\n",
                                  rdn, id);
                    /* Try rdn as dn. Could be RUV. */
                    normdn = slapi_ch_strdup(rdn);
                } else if (NULL == normdn) {
                    slapi_log_err(SLAPI_LOG_ERR, ID2ENTRY,
                                  "id2entry( %lu ) entryrdn_lookup_dn returned NULL. "
                                  "Index file may be deleted or corrupted.\n",
                                  (u_long)id);
                    goto bail;
                }
            }

            sdn = slapi_sdn_new_normdn_byval((const char *)normdn);
            bdn = backdn_init(sdn, id, 0);
            if (CACHE_ADD(&inst->inst_dncache, bdn, NULL)) {
                backdn_free(&bdn);
                slapi_log_err(SLAPI_LOG_CACHE, ID2ENTRY,
                              "%s is already in the dn cache\n", normdn);
            } else {
                CACHE_RETURN(&inst->inst_dncache, &bdn);
                slapi_log_err(SLAPI_LOG_CACHE, ID2ENTRY,
                              "entryrdn_lookup_dn returned: %s, "
                              "and set to dn cache (id %d)\n",
                              normdn, id);
            }
        }
        ee = slapi_str2entry_ext((const char *)normdn, (const Slapi_RDN *)srdn, data.dptr,
                                 SLAPI_STR2ENTRY_NO_ENTRYDN);
        slapi_ch_free_string(&rdn);
        slapi_ch_free_string(&normdn);
        slapi_rdn_free(&srdn);
    }

    if (ee != NULL) {
        int retval = 0;
        struct backentry *imposter = NULL;

        /* All entries should have uniqueids */
        PR_ASSERT(slapi_entry_get_uniqueid(ee) != NULL);

        /* ownership of the entry is passed into the backentry */
        e = backentry_init(ee);
        e->ep_id = id;
        slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                      "id2entry id: %d, dn \"%s\" -- adding it to cache\n",
                      id, backentry_get_ndn(e));

        /* Decrypt any encrypted attributes in this entry,
         * before adding it to the cache */
        retval = attrcrypt_decrypt_entry(be, e);
        if (retval) {
            slapi_log_err(SLAPI_LOG_ERR, ID2ENTRY,
                          "attrcrypt_decrypt_entry failed in id2entry\n");
        }

        /*
         * If return entry exists AND entryrdn switch is on,
         * add the entrydn value.
         */
        Slapi_Attr *eattr = NULL;
        /* Check if entrydn is in the entry or not */
        if (slapi_entry_attr_find(e->ep_entry, LDBM_ENTRYDN_STR, &eattr)) {
            /* entrydn does not exist in the entry */
            char *entrydn = NULL;
            /* slapi_ch_strdup and slapi_dn_ignore_case never returns NULL */
            entrydn = slapi_ch_strdup(slapi_entry_get_dn_const(e->ep_entry));
            entrydn = slapi_dn_ignore_case(entrydn);
            slapi_entry_attr_set_charptr(e->ep_entry,
                                         LDBM_ENTRYDN_STR, entrydn);
            if (0 == slapi_entry_attr_find(e->ep_entry,
                                           LDBM_ENTRYDN_STR, &eattr)) {
                /* now entrydn should exist in the entry */
                /* Set it to operational attribute */
                eattr->a_flags = SLAPI_ATTR_FLAG_OPATTR;
            }
            slapi_ch_free_string(&entrydn);
        }

        backentry_compute_weight(e, &t1);
        retval = CACHE_ADD(&inst->inst_cache, e, &imposter);
        if (1 == retval) {
            /* This means that someone else put the entry in the cache
            while we weren't looking ! So, we need to use the pointer
            returned and free the one we made earlier */
            if (imposter) {
                backentry_free(&e);
                e = imposter;
            }
        } else if (-1 == retval) {
            /* the entry is in idtable but not in dntable, i.e., the entry
             * could have been renamed */
            slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                          "Failed to put entry (id %lu, dn %s) "
                          "into entry cache\n",
                          (u_long)id,
                          backentry_get_ndn(e));
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, ID2ENTRY,
                      "str2entry returned NULL for id %lu, string=\"%s\"\n",
                      (u_long)id, (char *)data.data);
        e = NULL;
    }

bail:
    dblayer_value_free(be, &data);
    dblayer_release_id2entry(be, db);

    slapi_log_err(SLAPI_LOG_TRACE, ID2ENTRY,
                  "<= id2entry( %lu ) %p (disk)\n", (u_long)id, e);
    return (e);
}
