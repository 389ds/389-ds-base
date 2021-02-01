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


#include "back-ldbm.h"
#include "import.h"

static char *sourcefile = LDBM_ANCESTORID_STR;

/* Start of definitions for a simple cache using a hash table */

typedef struct id2idl
{
    ID keyid;
    IDList *idl;
    struct id2idl *next;
} id2idl;

static void id2idl_free(id2idl **ididl);
static int id2idl_same_key(const void *ididl, const void *k);

typedef Hashtable id2idl_hash;

#define id2idl_new_hash(size) new_hash(size, HASHLOC(id2idl, next), NULL, id2idl_same_key)
#define id2idl_hash_lookup(ht, key, he) find_hash(ht, key, sizeof(ID), (void **)(he))
#define id2idl_hash_add(ht, key, he, alt) add_hash(ht, key, sizeof(ID), he, (void **)(alt))
#define id2idl_hash_remove(ht, key) remove_hash(ht, key, sizeof(ID))

static void id2idl_hash_destroy(id2idl_hash *ht);

/* End of definitions for a simple cache using a hash table */

static int ldbm_parentid(backend *be, DB_TXN *txn, ID id, ID *ppid);
static int check_cache(id2idl_hash *ht);
static IDList *idl_union_allids(backend *be, struct attrinfo *ai, IDList *a, IDList *b);
static int ldbm_ancestorid_default_create_index(backend *be, ImportJob *job);
static int ldbm_ancestorid_new_idl_create_index(backend *be, ImportJob *job);

static int
ldbm_get_nonleaf_ids(backend *be, DB_TXN *txn, IDList **idl, ImportJob *job)
{
    int ret = 0;
    DB *db = NULL;
    DBC *dbc = NULL;
    DBT key = {0};
    DBT data = {0};
    struct attrinfo *ai = NULL;
    IDList *nodes = NULL;
    ID id;
    int started_progress_logging = 0;
    int key_count = 0;

    /* Open the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai);

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_get_nonleaf_ids", sourcefile, 13010, ret);
        goto out;
    }

    /* Get a cursor so we can walk through the parentid */
    ret = db->cursor(db, txn, &dbc, 0);
    if (ret != 0) {
        ldbm_nasty("ldbm_get_nonleaf_ids", sourcefile, 13020, ret);
        goto out;
    }
    import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids", "Gathering ancestorid non-leaf IDs...");
    /* For each key which is an equality key */
    do {
        ret = dbc->c_get(dbc, &key, &data, DB_NEXT_NODUP);
        if ((ret == 0) && (*(char *)key.data == EQ_PREFIX)) {
            id = (ID)strtoul((char *)key.data + 1, NULL, 10);
            /*
             * TEL 20180711 - switch to idl_append instead of idl_insert because there is no
             * no need to keep the list constantly sorted, which can be very expensive with
             * large databases (exacerbated by the fact that the parentid btree ordering is
             * lexical, but the idl_insert ordering is numeric).  It is enough to gather them
             * all together and sort them once at the end.
             */
            idl_append_extend(&nodes, id);
        }
        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
                                  "Gathering ancestorid non-leaf IDs: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
                                  "Gathering ancestorid non-leaf IDs: processed %d ancestors...",
                                  key_count);
            }
            started_progress_logging = 1;
        }
    } while (ret == 0 && !(job->flags & FLAG_ABORT));

    if (started_progress_logging) {
        /* finish what we started logging */
        if (job->numsubordinates) {
            import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
                              "Gathering ancestorid non-leaf IDs: processed %d%% (ID count %d)",
                              (key_count * 100 / job->numsubordinates), key_count);
        } else {
            import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
                              "Gathering ancestorid non-leaf IDs: processed %d ancestors",
                              key_count);
        }
    }
    import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
                      "Finished gathering ancestorid non-leaf IDs.");
    /* Check for success */
    if (ret == DB_NOTFOUND)
        ret = 0;
    if (ret != 0)
        ldbm_nasty("ldbm_get_nonleaf_ids", sourcefile, 13030, ret);

    if (ret == 0 && nodes) {
        /* now sort it */
        import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
            "Starting sort of ancestorid non-leaf IDs...");

        qsort((void *)&nodes->b_ids[0], nodes->b_nids, (size_t)sizeof(ID), idl_sort_cmp);

        import_log_notice(job, SLAPI_LOG_INFO, "ldbm_get_nonleaf_ids",
            "Finished sort of ancestorid non-leaf IDs.");
    }

out:
    /* Close the cursor */
    if (dbc != NULL) {
        if (ret == 0) {
            ret = dbc->c_close(dbc);
            if (ret != 0)
                ldbm_nasty("ldbm_get_nonleaf_ids", sourcefile, 13040, ret);
        } else {
            (void)dbc->c_close(dbc);
        }
    }

    /* Release the parentid file */
    if (db != NULL) {
        dblayer_release_index_file(be, ai, db);
    }

    /* Return the idlist */
    if (ret == 0) {
        *idl = nodes;
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_get_nonleaf_ids", "Found %lu nodes for ancestorid\n",
                      (u_long)IDL_NIDS(nodes));
    } else {
        idl_free(&nodes);
        *idl = NULL;
    }

    return ret;
}

/*
 * XXX: This function creates ancestorid index, which is a sort of hack.
 *      This function handles idl directly,
 *      which should have been implemented in the idl file(s).
 *      When the idl code would be updated in the future,
 *      this function may also get affected.
 *      (see also bug#: 605535)
 *
 * Construct the ancestorid index. Requirements:
 * - The backend is read only.
 * - The parentid index is accurate.
 * - Non-leaf entries have IDs less than their descendants
 *   (guaranteed after a database import but not after a subtree move)
 *
 */
int
ldbm_ancestorid_create_index(backend *be, ImportJob *job)
{
    return (idl_get_idl_new()) ? ldbm_ancestorid_new_idl_create_index(be, job) : ldbm_ancestorid_default_create_index(be, job);
}

/*
 * Create the ancestorid index.  This version is safe to
 * use whichever IDL mode is active.  However, it may be
 * quite a bit slower than ldbm_ancestorid_new_idl_create_index()
 * when the new mode is used, particularly with large databases.
 */
static int
ldbm_ancestorid_default_create_index(backend *be, ImportJob *job)
{
    int key_count = 0;
    int ret = 0;
    DB *db_pid = NULL;
    DB *db_aid = NULL;
    DBT key = {0};
    DB_TXN *txn = NULL;
    struct attrinfo *ai_pid = NULL;
    struct attrinfo *ai_aid = NULL;
    char keybuf[24];
    IDList *nodes = NULL;
    IDList *children = NULL, *descendants = NULL;
    NIDS nids;
    ID id, parentid;
    id2idl_hash *ht = NULL;
    id2idl *ididl;
    int started_progress_logging = 0;

    /*
     * We need to iterate depth-first through the non-leaf nodes
     * in the tree amassing an idlist of descendant ids for each node.
     * We would prefer to go through the parentid keys just once from
     * highest id to lowest id but the btree ordering is by string
     * rather than number. So we go through the parentid keys in btree
     * order first of all to create an idlist of all the non-leaf nodes.
     * Then we can use the idlist to iterate through parentid in the
     * correct order.
     */

    /* Get the non-leaf node IDs */
    ret = ldbm_get_nonleaf_ids(be, txn, &nodes, job);
    if (ret != 0)
        return ret;

    /* Get the ancestorid index */
    ainfo_get(be, LDBM_ANCESTORID_STR, &ai_aid);

    /* Prevent any other use of the index */
    ai_aid->ai_indexmask |= INDEX_OFFLINE;

    /* Open the ancestorid index file */
    ret = dblayer_get_index_file(be, ai_aid, &db_aid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_ancestorid_default_create_index", sourcefile, 13050, ret);
        goto out;
    }

    /* Maybe nothing to do */
    if (nodes == NULL || nodes->b_nids == 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_default_create_index",
                      "Nothing to do to build ancestorid index\n");
        goto out;
    }

    /* Create an ancestorid cache */
    ht = id2idl_new_hash(nodes->b_nids);

    /* Get the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai_pid);

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai_pid, &db_pid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_ancestorid_default_create_index", sourcefile, 13060, ret);
        goto out;
    }

    /* Initialize key DBT */
    key.data = keybuf;
    key.ulen = sizeof(keybuf);
    key.flags = DB_DBT_USERMEM;

    import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_default_create_index",
                      "Creating ancestorid index (old idl)...");
    /* Iterate from highest to lowest ID */
    nids = nodes->b_nids;
    do {

        nids--;
        id = nodes->b_ids[nids];

        /* Get immediate children from parentid index */
        key.size = PR_snprintf(key.data, key.ulen, "%c%lu",
                               EQ_PREFIX, (u_long)id);
        key.size++; /* include the null terminator */
        ret = NEW_IDL_NO_ALLID;
        children = idl_fetch(be, db_pid, &key, txn, ai_pid, &ret);
        if (ret != 0) {
            ldbm_nasty("ldbm_ancestorid_default_create_index", sourcefile, 13070, ret);
            break;
        }

        /* check if we need to abort */
        if (job->flags & FLAG_ABORT) {
            import_log_notice(job, SLAPI_LOG_ERR, "ldbm_ancestorid_default_create_index",
                              "ancestorid creation aborted.");
            ret = -1;
            break;
        }

        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d ancestors...",
                                  key_count);
            }
            started_progress_logging = 1;
        }

        /* Insert into ancestorid for this node */
        if (id2idl_hash_lookup(ht, &id, &ididl)) {
            descendants = idl_union_allids(be, ai_aid, ididl->idl, children);
            idl_free(&children);
            if (id2idl_hash_remove(ht, &id) == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_default_create_index",
                              "id2idl_hash_remove() failed\n");
            } else {
                id2idl_free(&ididl);
            }
        } else {
            descendants = children;
        }
        ret = idl_store_block(be, db_aid, &key, descendants, txn, ai_aid);
        if (ret != 0)
            break;

        /* Get parentid for this entry */
        ret = ldbm_parentid(be, txn, id, &parentid);
        if (ret != 0) {
            idl_free(&descendants);
            break;
        }

        /* A suffix entry does not have a parent */
        if (parentid == NOID) {
            idl_free(&descendants);
            continue;
        }

        /* Insert into ancestorid for this node's parent */
        if (id2idl_hash_lookup(ht, &parentid, &ididl)) {
            IDList *idl = idl_union_allids(be, ai_aid, ididl->idl, descendants);
            idl_free(&descendants);
            idl_free(&(ididl->idl));
            ididl->idl = idl;
        } else {
            ididl = (id2idl *)slapi_ch_calloc(1, sizeof(id2idl));
            ididl->keyid = parentid;
            ididl->idl = descendants;
            if (id2idl_hash_add(ht, &parentid, ididl, NULL) == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_default_create_index ",
                              "id2idl_hash_add failed\n");
            }
        }

    } while (nids > 0);

    if (ret != 0) {
        goto out;
    }

    /* We're expecting the cache to be empty */
    ret = check_cache(ht);

out:

    /* Destroy the cache */
    id2idl_hash_destroy(ht);

    /* Free any leftover idlists */
    idl_free(&nodes);

    /* Release the parentid file */
    if (db_pid != NULL) {
        dblayer_release_index_file(be, ai_pid, db_pid);
    }

    /* Release the ancestorid file */
    if (db_aid != NULL) {
        dblayer_release_index_file(be, ai_aid, db_aid);
    }

    /* Enable the index */
    if (ret == 0) {
        if (started_progress_logging) {
            /* finish what we started logging */
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d ancestors",
                                  key_count);
            }
        }
        import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_default_create_index",
                          "Created ancestorid index (old idl).");
        ai_aid->ai_indexmask &= ~INDEX_OFFLINE;
    }

    return ret;
}

/*
 * Create the ancestorid index.  This version expects to use
 * idl_new_store_block() and should be used when idl_new != 0.
 * It has lower overhead and can be faster than
 * ldbm_ancestorid_default_create_index(), particularly on
 * large databases.  Cf. bug 469800.
 */
static int
ldbm_ancestorid_new_idl_create_index(backend *be, ImportJob *job)
{
    int key_count = 0;
    int ret = 0;
    DB *db_pid = NULL;
    DB *db_aid = NULL;
    DBT key = {0};
    DB_TXN *txn = NULL;
    struct attrinfo *ai_pid = NULL;
    struct attrinfo *ai_aid = NULL;
    char keybuf[24];
    IDList *nodes = NULL;
    IDList *children = NULL;
    NIDS nids;
    ID id, parentid;
    int started_progress_logging = 0;

    /*
     * We need to iterate depth-first through the non-leaf nodes
     * in the tree amassing an idlist of descendant ids for each node.
     * We would prefer to go through the parentid keys just once from
     * highest id to lowest id but the btree ordering is by string
     * rather than number. So we go through the parentid keys in btree
     * order first of all to create an idlist of all the non-leaf nodes.
     * Then we can use the idlist to iterate through parentid in the
     * correct order.
     */

    /* Bail now if we did not get here honestly. */
    if (!idl_get_idl_new()) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_new_idl_create_index",
                      "Cannot create ancestorid index.  "
                      "New IDL version called but idl_new is false!\n");
        return 1;
    }

    /* Get the non-leaf node IDs */
    ret = ldbm_get_nonleaf_ids(be, txn, &nodes, job);
    if (ret != 0)
        return ret;

    /* Get the ancestorid index */
    ainfo_get(be, LDBM_ANCESTORID_STR, &ai_aid);

    /* Prevent any other use of the index */
    ai_aid->ai_indexmask |= INDEX_OFFLINE;

    /* Open the ancestorid index file */
    ret = dblayer_get_index_file(be, ai_aid, &db_aid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_ancestorid_new_idl_create_index", sourcefile, 13050, ret);
        goto out;
    }

    /* Maybe nothing to do */
    if (nodes == NULL || nodes->b_nids == 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_new_idl_create_index",
                      "Nothing to do to build ancestorid index\n");
        goto out;
    }

    /* Get the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai_pid);

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai_pid, &db_pid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_ancestorid_new_idl_create_index", sourcefile, 13060, ret);
        goto out;
    }

    /* Initialize key DBT */
    key.data = keybuf;
    key.ulen = sizeof(keybuf);
    key.flags = DB_DBT_USERMEM;

    import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_new_idl_create_index",
                      "Creating ancestorid index (new idl)...");
    /* Iterate from highest to lowest ID */
    nids = nodes->b_nids;
    do {

        nids--;
        id = nodes->b_ids[nids];

        /* Get immediate children from parentid index */
        key.size = PR_snprintf(key.data, key.ulen, "%c%lu",
                               EQ_PREFIX, (u_long)id);
        key.size++; /* include the null terminator */
        ret = NEW_IDL_NO_ALLID;
        children = idl_fetch(be, db_pid, &key, txn, ai_pid, &ret);
        if (ret != 0) {
            ldbm_nasty("ldbm_ancestorid_new_idl_create_index", sourcefile, 13070, ret);
            break;
        }

        /* check if we need to abort */
        if (job->flags & FLAG_ABORT) {
            import_log_notice(job, SLAPI_LOG_ERR, "ldbm_ancestorid_new_idl_create_index",
                              "ancestorid creation aborted.");
            ret = -1;
            break;
        }

        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: progress %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: progress %d ancestors...",
                                  key_count);
            }
            started_progress_logging = 1;
        }

        /* Instead of maintaining a full accounting of IDs in a hashtable
         * as is done with ldbm_ancestorid_default_create_index(), perform
         * incremental updates straight to the DB with idl_new_store_block()
         * (used by idl_store_block() when idl_get_idl_new() is true).  This
         * can be a significant performance improvement with large databases,
         * where  the overhead of maintaining and copying the lists is very
         * expensive, particularly when the allids threshold is not being
         * used to provide any cut off.  Cf. bug 469800.
         * TEL 20081029 */

        /* Insert into ancestorid for this node */
        ret = idl_store_block(be, db_aid, &key, children, txn, ai_aid);
        if (ret != 0) {
            idl_free(&children);
            break;
        }

        /* Get parentid(s) for this entry */
        while (1) {
            ret = ldbm_parentid(be, txn, id, &parentid);
            if (ret != 0) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_new_idl_create_index",
                              "Failure: ldbm_parentid on node index [" ID_FMT "] of [" ID_FMT "]\n",
                              nids, nodes->b_nids);
                idl_free(&children);
                goto out;
            }

            /* A suffix entry does not have a parent */
            if (parentid == NOID) {
                idl_free(&children);
                break;
            }

            /* Reset the key to the parent id */
            key.size = PR_snprintf(key.data, key.ulen, "%c%lu",
                                   EQ_PREFIX, (u_long)parentid);
            key.size++;

            /* Insert into ancestorid for this node's parent */
            ret = idl_store_block(be, db_aid, &key, children, txn, ai_aid);
            if (ret != 0) {
                idl_free(&children);
                goto out;
            }
            id = parentid;
        }
    } while (nids > 0);

    if (ret != 0) {
        goto out;
    }

out:
    if (ret == 0) {
        if (started_progress_logging) {
            /* finish what we started logging */
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: processed %d ancestors",
                                  key_count);
            }
        }
        import_log_notice(job, SLAPI_LOG_INFO, "ldbm_ancestorid_new_idl_create_index",
                          "Created ancestorid index (new idl).");
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_new_idl_create_index",
                      "Failed to create ancestorid index\n");
    }

    /* Free any leftover idlists */
    idl_free(&nodes);

    /* Release the parentid file */
    if (db_pid != NULL) {
        dblayer_release_index_file(be, ai_pid, db_pid);
    }

    /* Release the ancestorid file */
    if (db_aid != NULL) {
        dblayer_release_index_file(be, ai_aid, db_aid);
    }

    /* Enable the index */
    if (ret == 0) {
        ai_aid->ai_indexmask &= ~INDEX_OFFLINE;
    }

    return ret;
}


/*
 * Get parentid of an id by reading the operational attr from id2entry.
 */
static int
ldbm_parentid(backend *be, DB_TXN *txn, ID id, ID *ppid)
{
    int ret = 0;
    DB *db = NULL;
    DBT key = {0};
    DBT data = {0};
    ID stored_id;
    char *p;

    /* Open the id2entry file */
    ret = dblayer_get_id2entry(be, &db);
    if (ret != 0) {
        ldbm_nasty("ldbm_parentid", sourcefile, 13100, ret);
        goto out;
    }

    /* Initialize key and data DBTs */
    id_internal_to_stored(id, (char *)&stored_id);
    key.data = (char *)&stored_id;
    key.size = sizeof(stored_id);
    key.flags = DB_DBT_USERMEM;
    data.flags = DB_DBT_MALLOC;

    /* Read id2entry */
    ret = db->get(db, txn, &key, &data, 0);
    if (ret != 0) {
        ldbm_nasty("ldbm_parentid", sourcefile, 13110, ret);
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_parentid",
                      "Unable to find entry id [" ID_FMT "] (original [" ID_FMT "])"
                      " in id2entry\n",
                      stored_id, id);
        goto out;
    }

/* Extract the parentid value */
#define PARENTID_STR "\nparentid:"
    p = strstr(data.data, PARENTID_STR);
    if (p == NULL) {
        *ppid = NOID;
        goto out;
    }
    *ppid = strtoul(p + strlen(PARENTID_STR), NULL, 10);

out:
    /* Free the entry value */
    slapi_ch_free(&(data.data));

    /* Release the id2entry file */
    if (db != NULL) {
        dblayer_release_id2entry(be, db);
    }
    return ret;
}

static void
id2idl_free(id2idl **ididl)
{
    idl_free(&((*ididl)->idl));
    slapi_ch_free((void **)ididl);
}

static int
id2idl_same_key(const void *ididl, const void *k)
{
    return (((id2idl *)ididl)->keyid == *(ID *)k);
}

static int
check_cache(id2idl_hash *ht)
{
    id2idl *e;
    u_long i, found = 0;
    int ret = 0;

    if (ht == NULL)
        return 0;

    for (i = 0; i < ht->size; i++) {
        e = (id2idl *)ht->slot[i];
        while (e) {
            found++;
            e = e->next;
        }
    }

    if (found > 0) {
        slapi_log_err(SLAPI_LOG_ERR, "check_cache",
                      "parentid index is not complete (%lu extra keys in ancestorid cache)\n", found);
        ret = -1;
    }

    return ret;
}

static void
id2idl_hash_destroy(id2idl_hash *ht)
{
    u_long i;
    id2idl *e, *next;

    if (ht == NULL)
        return;

    for (i = 0; i < ht->size; i++) {
        e = (id2idl *)ht->slot[i];
        while (e) {
            next = e->next;
            id2idl_free(&e);
            e = next;
        }
    }
    slapi_ch_free((void **)&ht);
}

/*
 * idl_union_allids - return a union b
 * takes attr index allids setting into account
 */
static IDList *
idl_union_allids(backend *be, struct attrinfo *ai, IDList *a, IDList *b)
{
    if (!idl_get_idl_new()) {
        if (a != NULL && b != NULL) {
            if (ALLIDS(a) || ALLIDS(b) ||
                (IDL_NIDS(a) + IDL_NIDS(b) > idl_get_allidslimit(ai, 0))) {
                return (idl_allids(be));
            }
        }
    }
    return idl_union(be, a, b);
}

static int
ancestorid_addordel(
    backend *be,
    DB *db,
    ID node_id,
    ID id,
    DB_TXN *txn,
    struct attrinfo *ai,
    int flags,
    int *allids)
{
    DBT key = {0};
    char keybuf[24];
    int ret = 0;

    /* Initialize key DBT */
    key.data = keybuf;
    key.ulen = sizeof(keybuf);
    key.flags = DB_DBT_USERMEM;
    key.size = PR_snprintf(key.data, key.ulen, "%c%lu",
                           EQ_PREFIX, (u_long)node_id);
    key.size++; /* include the null terminator */

    if (flags & BE_INDEX_ADD) {
#if 1
        slapi_log_err(SLAPI_LOG_TRACE, "ancestorid_addordel", "Insert ancestorid %lu:%lu\n",
                      (u_long)node_id, (u_long)id);
#endif
        ret = idl_insert_key(be, db, &key, id, txn, ai, allids);
    } else {
#if 1
        slapi_log_err(SLAPI_LOG_TRACE, "ancestorid_addordel", "Delete ancestorid %lu:%lu\n",
                      (u_long)node_id, (u_long)id);
#endif
        ret = idl_delete_key(be, db, &key, id, txn, ai);
    }

    if (ret != 0) {
        ldbm_nasty("ancestorid_addordel", sourcefile, 13120, ret);
    }

    return ret;
}

/*
 * Update ancestorid index inserting or deleting depending on flags.
 * The entry ids to be indexed are given by id (a base object)
 * and optionally subtree_idl (descendants of the base object).
 * The ancestorid keys to be updated are derived from nodes
 * in the tree from low up to high. Whether the low and high nodes
 * themselves are updated is given by include_low and include_high.
 */
static int
ldbm_ancestorid_index_update(
    backend *be,
    const Slapi_DN *low,
    const Slapi_DN *high,
    int include_low,
    int include_high,
    ID id,
    IDList *subtree_idl,
    int flags, /* BE_INDEX_ADD, BE_INDEX_DEL */
    back_txn *txn)
{
    DB *db = NULL;
    int allids = IDL_INSERT_NORMAL;
    Slapi_DN sdn;
    Slapi_DN nextsdn;
    struct attrinfo *ai = NULL;
    ID node_id, sub_id;
    idl_iterator iter;
    int err = 0, ret = 0;
    DB_TXN *db_txn = txn != NULL ? txn->back_txn_txn : NULL;

    slapi_sdn_init(&sdn);
    slapi_sdn_init(&nextsdn);

    /* Open the ancestorid index */
    ainfo_get(be, LDBM_ANCESTORID_STR, &ai);
    ret = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("ldbm_ancestorid_index_update", sourcefile, 13130, ret);
        goto out;
    }

    slapi_sdn_copy(low, &sdn);

    if (include_low == 0) {
        if (slapi_sdn_compare(&sdn, high) == 0) {
            goto out;
        }
        /* Get the next highest DN */
        slapi_sdn_get_parent(&sdn, &nextsdn);
        slapi_sdn_copy(&nextsdn, &sdn);
    }

    /* Iterate up through the tree */
    do {
        if (slapi_sdn_isempty(&sdn)) {
            break;
        }

        /* Have we reached the high node? */
        if (include_high == 0 && slapi_sdn_compare(&sdn, high) == 0) {
            break;
        }

        /* Get the id for that DN */
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            node_id = 0;
            err = entryrdn_index_read(be, &sdn, &node_id, txn);
            if (err) {
                if (DB_NOTFOUND != err) {
                    ldbm_nasty("ldbm_ancestorid_index_update", sourcefile, 13141, err);
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_ancestorid_index_update",
                                  "entryrdn_index_read(%s)\n", slapi_sdn_get_dn(&sdn));
                    ret = err;
                }
                break;
            }
        } else {
            IDList *idl = NULL;
            struct berval ndnv;
            ndnv.bv_val = (void *)slapi_sdn_get_ndn(&sdn);
            ndnv.bv_len = slapi_sdn_get_ndn_len(&sdn);
            err = 0;
            idl = index_read(be, LDBM_ENTRYDN_STR, indextype_EQUALITY, &ndnv, txn, &err);
            if (idl == NULL) {
                if (err != 0 && err != DB_NOTFOUND) {
                    ldbm_nasty("ldbm_ancestorid_index_update", sourcefile, 13140, err);
                    ret = err;
                }
                break;
            }
            node_id = idl_firstid(idl);
            idl_free(&idl);
        }

        /* Update ancestorid for the base entry */
        ret = ancestorid_addordel(be, db, node_id, id, db_txn, ai, flags, &allids);
        if (ret != 0)
            break;

        /*
         * If this node was already allids then all higher nodes must already
         * be at allids since the higher nodes must have a greater number
         * of descendants. Therefore no point continuing.
         */
        if (allids == IDL_INSERT_ALLIDS)
            break;

        /* Update ancestorid for any subtree entries */
        if (subtree_idl != NULL && ((flags & BE_INDEX_ADD) || (!ALLIDS(subtree_idl)))) {
            iter = idl_iterator_init(subtree_idl);
            while ((sub_id = idl_iterator_dereference_increment(&iter, subtree_idl)) != NOID) {
                ret = ancestorid_addordel(be, db, node_id, sub_id, db_txn, ai, flags, &allids);
                if (ret != 0)
                    break;
            }
            if (ret != 0)
                break;
        }

        /* Have we reached the high node? */
        if (slapi_sdn_compare(&sdn, high) == 0) {
            break;
        }

        /* Get the next highest DN */
        slapi_sdn_get_parent(&sdn, &nextsdn);
        slapi_sdn_copy(&nextsdn, &sdn);

    } while (ret == 0);

out:
    slapi_sdn_done(&sdn);
    slapi_sdn_done(&nextsdn);

    /* Release the ancestorid file */
    if (db != NULL) {
        dblayer_release_index_file(be, ai, db);
    }

    return ret;
}

/*
 * Update the ancestorid index for a single entry.
 * This function depends on the integrity of the entrydn index.
 */
int
ldbm_ancestorid_index_entry(
    backend *be,
    struct backentry *e,
    int flags, /* BE_INDEX_ADD, BE_INDEX_DEL */
    back_txn *txn)
{
    int ret = 0;

    ret = ldbm_ancestorid_index_update(be,
                                       slapi_entry_get_sdn_const(e->ep_entry),
                                       slapi_be_getsuffix(be, 0),
                                       0, 1, e->ep_id, NULL, flags, txn);

    return ret;
}

/*
 * Returns <0, 0, >0 according to whether right is a suffix of left,
 * neither is a suffix of the other, or left is a suffix of right.
 * If common is non-null then the common suffix of left and right
 * is returned in *common.
 */
static int
_sdn_suffix_cmp(
    const Slapi_DN *left,
    const Slapi_DN *right,
    Slapi_DN *common)
{
    char **rdns1, **rdns2;
    int count1, count2, i, ret = 0;
    size_t len = 0;
    char *p, *ndnstr;

    rdns1 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(left), 0);
    rdns2 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(right), 0);

    if (NULL == rdns1) {
        if (NULL == rdns2) {
            ret = 0;
        } else {
            ret = 1;
        }
        goto out;
    } else {
        if (NULL == rdns2) {
            ret = -1;
            goto out;
        }
    }

    for (count1 = 0; rdns1[count1] != NULL; count1++)
        ;
    count1--;

    for (count2 = 0; rdns2[count2] != NULL; count2++)
        ;
    count2--;

    while (count1 >= 0 && count2 >= 0) {
        if (strcmp(rdns1[count1], rdns2[count2]) != 0)
            break;
        count1--;
        count2--;
    }

    count1++;
    count2++;

    if (count1 == 0 && count2 == 0) {
        /* equal */
        ret = 0;
    } else if (count1 == 0) {
        /* left is suffix of right */
        ret = 1;
    } else if (count2 == 0) {
        /* right is suffix of left */
        ret = -1;
    } else {
        /* common prefix (possibly root), not left nor right */
        ret = 0;
    }

    /* if caller does not want the common prefix then we're done */
    if (common == NULL)
        goto out;

    /* figure out how much space we need */
    for (i = count1; rdns1[i] != NULL; i++) {
        len += strlen(rdns1[i]) + 1;
    }

    /* write the string */
    p = ndnstr = slapi_ch_calloc(len + 1, sizeof(char));
    for (i = count1; rdns1[i] != NULL; i++) {
        sprintf(p, "%s%s", (p != ndnstr) ? "," : "", rdns1[i]);
        p += strlen(p);
    }

    /* return the DN */
    slapi_sdn_set_dn_passin(common, ndnstr);

    slapi_log_err(SLAPI_LOG_TRACE, "_sdn_suffix_cmp", "Common suffix <%s>\n",
                  slapi_sdn_get_dn(common));

out:
    slapi_ldap_value_free(rdns1);
    slapi_ldap_value_free(rdns2);

    slapi_log_err(SLAPI_LOG_TRACE, "_sdn_suffix_cmp", "(<%s>, <%s>) => %d\n",
                  slapi_sdn_get_dn(left), slapi_sdn_get_dn(right), ret);

    return ret;
}

int
ldbm_ancestorid_move_subtree(
    backend *be,
    const Slapi_DN *olddn,
    const Slapi_DN *newdn,
    ID id,
    IDList *subtree_idl,
    back_txn *txn)
{
    int ret = 0;
    Slapi_DN commonsdn;


    slapi_sdn_init(&commonsdn);
    /* Determine the common ancestor */
    (void)_sdn_suffix_cmp(olddn, newdn, &commonsdn);

    /* Delete from old ancestors */
    ret = ldbm_ancestorid_index_update(be,
                                       olddn,
                                       &commonsdn,
                                       0,
                                       0,
                                       id,
                                       subtree_idl,
                                       BE_INDEX_DEL,
                                       txn);
    if (ret != 0)
        goto out;

    /* Add to new ancestors */
    ret = ldbm_ancestorid_index_update(be,
                                       newdn,
                                       &commonsdn,
                                       0,
                                       0,
                                       id,
                                       subtree_idl,
                                       BE_INDEX_ADD,
                                       txn);

out:
    slapi_sdn_done(&commonsdn);
    return ret;
}

int
ldbm_ancestorid_read_ext(
    backend *be,
    back_txn *txn,
    ID id,
    IDList **idl,
    int allidslimit)
{
    int ret = 0;
    struct berval bv;
    char keybuf[24];

    bv.bv_val = keybuf;
    bv.bv_len = PR_snprintf(keybuf, sizeof(keybuf), "%lu", (u_long)id);

    *idl = index_read_ext_allids(NULL, be, LDBM_ANCESTORID_STR, indextype_EQUALITY, &bv, txn, &ret, NULL, allidslimit);

    return ret;
}

int
ldbm_ancestorid_read(
    backend *be,
    back_txn *txn,
    ID id,
    IDList **idl)
{
    return ldbm_ancestorid_read_ext(be, txn, id, idl, 0);
}
