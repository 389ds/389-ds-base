/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * the "new" ("deluxe") backend import code
 *
 * please make sure you use 4-space indentation on this file.
 */

#include "mdb_layer.h"
#include "../vlv_srch.h"

#define ERR_IMPORT_ABORTED -23
#define NEED_DN_NORM -24
#define NEED_DN_NORM_SP -25
#define NEED_DN_NORM_BT -26

static char *sourcefile = "dbmdb_import.c";

static int dbmdb_ancestorid_create_index(backend *be, ImportJob *job);
static int dbmdb_ancestorid_default_create_index(backend *be, ImportJob *job);
static int dbmdb_ancestorid_new_idl_create_index(backend *be, ImportJob *job);

/* Start of definitions for a simple cache using a hash table */

typedef struct id2idl
{
    ID keyid;
    IDList *idl;
    struct id2idl *next;
} dbmdb_id2idl;

static void dbmdb_id2idl_free(dbmdb_id2idl **ididl);
static int dbmdb_id2idl_same_key(const void *ididl, const void *k);

typedef Hashtable id2idl_hash;

#define dbmdb_id2idl_new_hash(size) new_hash(size, HASHLOC(dbmdb_id2idl, next), NULL, dbmdb_id2idl_same_key)
#define dbmdb_id2idl_hash_lookup(ht, key, he) find_hash(ht, key, sizeof(ID), (void **)(he))
#define dbmdb_id2idl_hash_add(ht, key, he, alt) add_hash(ht, key, sizeof(ID), he, (void **)(alt))
#define dbmdb_id2idl_hash_remove(ht, key) remove_hash(ht, key, sizeof(ID))

static void dbmdb_id2idl_hash_destroy(id2idl_hash *ht);
/* End of definitions for a simple cache using a hash table */

static int dbmdb_parentid(backend *be, dbi_txn_t *txn, ID id, ID *ppid);
static int dbmdb_check_cache(id2idl_hash *ht);
static IDList *dbmdb_idl_union_allids(backend *be, struct attrinfo *ai, IDList *a, IDList *b);

/********** routines to manipulate the entry fifo **********/

/* this is pretty bogus -- could be a HUGE amount of memory */
/* Not anymore with the Import Queue Adaptative Algorithm (Regulation) */
#define MAX_FIFO_SIZE 8000

static int
dbmdb_import_fifo_init(ImportJob *job)
{
    ldbm_instance *inst = job->inst;

    /* Work out how big the entry fifo can be */
    if (inst->inst_cache.c_maxentries > 0)
        job->fifo.size = inst->inst_cache.c_maxentries;
    else
        job->fifo.size = inst->inst_cache.c_maxsize / 1024; /* guess */

    /* byte limit that should be respected to avoid memory starvation */
    /* Rather than cachesize * .8, we set it to cachesize for clarity */
    job->fifo.bsize = inst->inst_cache.c_maxsize;

    job->fifo.c_bsize = 0;

    if (job->fifo.size > MAX_FIFO_SIZE)
        job->fifo.size = MAX_FIFO_SIZE;
    /* has to be at least 1 or 2, and anything less than about 100 destroys
     * the point of doing all this optimization in the first place. */
    if (job->fifo.size < 100)
        job->fifo.size = 100;

    /* Get memory for the entry fifo */
    /* This is used to keep a ref'ed pointer to the last <cachesize>
     * processed entries */
    PR_ASSERT(NULL == job->fifo.item);
    job->fifo.item = (FifoItem *)slapi_ch_calloc(job->fifo.size,
                                                 sizeof(FifoItem));
    if (NULL == job->fifo.item) {
        /* Memory allocation error */
        return -1;
    }
    return 0;
}

/*
 * import_fifo_validate_capacity_or_expand
 *
 * This is used to check if the capacity of the fifo is able to accomodate
 * the entry of the size entrysize. If it is enable to hold the entry the
 * fifo buffer is automatically expanded.
 *
 * \param job The ImportJob queue
 * \param entrysize The size to check for
 *
 * \return int: If able to hold the entry, returns 0. If unable to, but resize was sucessful, so now able to hold the entry, 0. If unable to hold the entry and unable to resize, 1.
 */
int
dbmdb_import_fifo_validate_capacity_or_expand(ImportJob *job, size_t entrysize)
{
    int result = 1;
    /* We shoot for four times as much to start with. */
    uint64_t request = entrysize * 4;
    util_cachesize_result sane;

    if (entrysize > job->fifo.bsize) {
        /* Check the amount of memory on the system */
        slapi_pal_meminfo *mi = spal_meminfo_get();
        sane = util_is_cachesize_sane(mi, &request);
        spal_meminfo_destroy(mi);
        if (sane == UTIL_CACHESIZE_REDUCED && entrysize <= request) {
            /* Did the amount cachesize set still exceed entrysize? It'll do ... */
            job->fifo.bsize = request;
            result = 0;
        } else if (sane != UTIL_CACHESIZE_VALID) {
            /* Can't allocate! No!!! */
            result = 1;
        } else {
            /* Our request was okay, go ahead .... */
            job->fifo.bsize = request;
            result = 0;
        }
    } else {
        result = 0;
    }
    return result;
}

FifoItem *
dbmdb_import_fifo_fetch(ImportJob *job, ID id, int worker)
{
    int idx = id % job->fifo.size;
    FifoItem *fi;

    if (job->fifo.item) {
        fi = &(job->fifo.item[idx]);
    } else {
        return NULL;
    }
    if (fi->entry) {
        if (worker) {
            if (fi->bad) {
                if (fi->bad == FIFOITEM_BAD) {
                    fi->bad = FIFOITEM_BAD_PRINTED;
                    if (!(job->flags & FLAG_UPGRADEDNFORMAT_V1)) {
                        import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_fifo_fetch",
                                          "Bad entry: ID %d", id);
                    }
                }
                return NULL;
            }
            PR_ASSERT(fi->entry->ep_refcnt > 0);
        }
    }
    return fi;
}

static void
dbmdb_import_fifo_destroy(ImportJob *job)
{
    /* Free any entries in the fifo first */
    struct backentry *be = NULL;
    size_t i = 0;

    for (i = 0; i < job->fifo.size; i++) {
        be = job->fifo.item[i].entry;
        backentry_free(&be);
        job->fifo.item[i].entry = NULL;
        job->fifo.item[i].filename = NULL;
    }
    slapi_ch_free((void **)&job->fifo.item);
    job->fifo.item = NULL;
}


/********** logging stuff **********/

#define LOG_BUFFER 512

/* this changes the 'nsTaskStatus' value, which is transient (anything logged
 * here wipes out any previous status)
 */
static void
dbmdb_import_log_status_start(ImportJob *job)
{
    if (!job->task_status)
        job->task_status = (char *)slapi_ch_malloc(10 * LOG_BUFFER);
    if (!job->task_status)
        return; /* out of memory? */

    job->task_status[0] = 0;
}

static void
dbmdb_import_log_status_add_line(ImportJob *job, char *format, ...)
{
    va_list ap;
    int len = 0;

    if (!job->task_status)
        return;
    len = strlen(job->task_status);
    if (len + 5 > (10 * LOG_BUFFER))
        return; /* no room */

    if (job->task_status[0])
        strcat(job->task_status, "\n");

    va_start(ap, format);
    PR_vsnprintf(job->task_status + len, (10 * LOG_BUFFER) - len, format, ap);
    va_end(ap);
}

static void
dbmdb_import_log_status_done(ImportJob *job)
{
    if (job->task) {
        slapi_task_log_status(job->task, "%s", job->task_status);
    }
}

static void
dbmdb_import_task_destroy(Slapi_Task *task)
{
    ImportJob *job = (ImportJob *)slapi_task_get_data(task);

    if (!job) {
        return;
    }

    while (task->task_state == SLAPI_TASK_RUNNING) {
        /* wait for the job to finish before freeing it */
        DS_Sleep(PR_SecondsToInterval(1));
    }
    if (job->task_status) {
        slapi_ch_free((void **)&job->task_status);
        job->task_status = NULL;
    }
    FREE(job);
    slapi_task_set_data(task, NULL);
}

static void
dbmdb_import_task_abort(Slapi_Task *task)
{
    ImportJob *job;

    /* don't log anything from here, because we're still holding the
     * DSE lock for modify...
     */

    if (slapi_task_get_state(task) == SLAPI_TASK_FINISHED) {
        /* too late */
    }

    /*
     * Race condition.
     * If the import thread happens to finish right now we're in trouble
     * because it will free the job.
     */

    job = (ImportJob *)slapi_task_get_data(task);

    import_abort_all(job, 0);
    while (slapi_task_get_state(task) != SLAPI_TASK_FINISHED)
        DS_Sleep(PR_MillisecondsToInterval(100));
}


/********** helper functions for importing **********/

/*
 * Get parentid of an id by reading the operational attr from id2entry.
 */
static int
dbmdb_parentid(backend *be, dbi_txn_t *txn, ID id, ID *ppid)
{
    int ret = 0;
    dbmdb_dbi_t*db = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    ID stored_id;
    char *p;

    /* Open the id2entry file */
    ret = dblayer_get_id2entry(be, (dbi_db_t**)&db);
    if (ret != 0) {
        ldbm_nasty("dbmdb_parentid", sourcefile, 13100, ret);
        goto out;
    }

    /* Initialize key and data MDB_vals */
    id_internal_to_stored(id, (char *)&stored_id);
    key.mv_data = (char *)&stored_id;
    key.mv_size = sizeof(stored_id);

    /* Read id2entry */
    ret = mdb_get(TXN(txn), db->dbi, &key, &data);
    if (ret != 0) {
        ldbm_nasty("dbmdb_parentid", sourcefile, 13110, ret);
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_parentid",
                      "Unable to find entry id [" ID_FMT "] (original [" ID_FMT "])"
                      " in id2entry\n",
                      stored_id, id);
        goto out;
    }

/* Extract the parentid value */
#define PARENTID_STR "\nparentid:"
    p = strstr(data.mv_data, PARENTID_STR);
    if (p == NULL) {
        *ppid = NOID;
        goto out;
    }
    *ppid = strtoul(p + strlen(PARENTID_STR), NULL, 10);

out:

    /* Release the id2entry file */
    if (db != NULL) {
        dblayer_release_id2entry(be, db);
    }
    return ret;
}

static void
dbmdb_id2idl_free(dbmdb_id2idl **ididl)
{
    idl_free(&((*ididl)->idl));
    slapi_ch_free((void **)ididl);
}

static int
dbmdb_id2idl_same_key(const void *ididl, const void *k)
{
    return (((dbmdb_id2idl *)ididl)->keyid == *(ID *)k);
}

static int
dbmdb_check_cache(id2idl_hash *ht)
{
    dbmdb_id2idl *e;
    u_long i, found = 0;
    int ret = 0;

    if (ht == NULL)
        return 0;

    for (i = 0; i < ht->size; i++) {
        e = (dbmdb_id2idl *)ht->slot[i];
        while (e) {
            found++;
            e = e->next;
        }
    }

    if (found > 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_check_cache",
                      "parentid index is not complete (%lu extra keys in ancestorid cache)\n", found);
        ret = -1;
    }

    return ret;
}

static void
dbmdb_id2idl_hash_destroy(id2idl_hash *ht)
{
    u_long i;
    dbmdb_id2idl *e, *next;

    if (ht == NULL)
        return;

    for (i = 0; i < ht->size; i++) {
        e = (dbmdb_id2idl *)ht->slot[i];
        while (e) {
            next = e->next;
            dbmdb_id2idl_free(&e);
            e = next;
        }
    }
    slapi_ch_free((void **)&ht);
}

/*
 * dbmdb_idl_union_allids - return a union b
 * takes attr index allids setting into account
 */
static IDList *
dbmdb_idl_union_allids(backend *be, struct attrinfo *ai, IDList *a, IDList *b)
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
dbmdb_get_nonleaf_ids(backend *be, dbi_txn_t *txn, IDList **idl, ImportJob *job)
{
    int ret = 0;
    dbmdb_dbi_t*db = NULL;
    MDB_cursor *dbc = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    struct attrinfo *ai = NULL;
    IDList *nodes = NULL;
    ID id;
    int started_progress_logging = 0;
    int key_count = 0;
    dbmdb_cursor_t cursor = {0};
    struct ldbminfo *li = (struct ldbminfo*)be->be_database->plg_private;

    /* Open the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai);

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai, (dbi_db_t**)&db, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("dbmdb_get_nonleaf_ids", sourcefile, 13010, ret);
        goto out;
    }

    /* Get a cursor so we can walk through the parentid */
    ret = dbmdb_open_cursor(&cursor, MDB_CONFIG(li) ,db ,0);
    if (ret != 0) {
        ldbm_nasty("dbmdb_get_nonleaf_ids", sourcefile, 13020, ret);
        goto out;
    }
     dbc = cursor.cur;
     txn = cursor.txn;
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_get_nonleaf_ids", "Gathering ancestorid non-leaf IDs...");
    /* For each key which is an equality key */
    ret = MDB_CURSOR_GET(dbc, &key, &data, MDB_FIRST);
    while (ret == 0 && !(job->flags & FLAG_ABORT)) {
        if (*(char *)key.mv_data == EQ_PREFIX) {
            id = (ID)strtoul((char *)key.mv_data + 1, NULL, 10);
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
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_get_nonleaf_ids",
                                  "Gathering ancestorid non-leaf IDs: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_get_nonleaf_ids",
                                  "Gathering ancestorid non-leaf IDs: processed %d ancestors...",
                                  key_count);
            }
            started_progress_logging = 1;
        }
        ret = MDB_CURSOR_GET(dbc, &key, &data, MDB_NEXT_NODUP);
    }

    if (started_progress_logging) {
        /* finish what we started logging */
        if (job->numsubordinates) {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_get_nonleaf_ids",
                              "Gathering ancestorid non-leaf IDs: processed %d%% (ID count %d)",
                              (key_count * 100 / job->numsubordinates), key_count);
        } else {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_get_nonleaf_ids",
                              "Gathering ancestorid non-leaf IDs: processed %d ancestors",
                              key_count);
        }
    }
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_get_nonleaf_ids",
                      "Finished gathering ancestorid non-leaf IDs.");
    /* Check for success */
    if (ret == MDB_NOTFOUND)
        ret = 0;
    if (ret != 0)
        ldbm_nasty("dbmdb_get_nonleaf_ids", sourcefile, 13030, ret);

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
    dbmdb_close_cursor(&cursor, ret);

    /* Release the parentid file */
    if (db != NULL) {
        dblayer_release_index_file(be, ai, db);
    }

    /* Return the idlist */
    if (ret == 0) {
        *idl = nodes;
        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_get_nonleaf_ids", "Found %lu nodes for ancestorid\n",
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
static int
dbmdb_ancestorid_create_index(backend *be, ImportJob *job)
{
    return (idl_get_idl_new()) ? dbmdb_ancestorid_new_idl_create_index(be, job) : dbmdb_ancestorid_default_create_index(be, job);
}

/*
 * Create the ancestorid index.  This version is safe to
 * use whichever IDL mode is active.  However, it may be
 * quite a bit slower than dbmdb_ancestorid_new_idl_create_index()
 * when the new mode is used, particularly with large databases.
 */
static int
dbmdb_ancestorid_default_create_index(backend *be, ImportJob *job)
{
    int key_count = 0;
    int ret = 0;
    dbmdb_dbi_t *db_pid = NULL;
    dbmdb_dbi_t *db_aid = NULL;
    dbi_val_t key = {0};
    struct attrinfo *ai_pid = NULL;
    struct attrinfo *ai_aid = NULL;
    char keybuf[24];
    IDList *nodes = NULL;
    IDList *children = NULL, *descendants = NULL;
    NIDS nids;
    ID id, parentid;
    id2idl_hash *ht = NULL;
    dbmdb_id2idl *ididl;
    int started_progress_logging = 0;
    dbi_txn_t * txn = NULL;

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
    ret = dbmdb_get_nonleaf_ids(be, txn, &nodes, job);
    if (ret != 0)
        return ret;

    /* Get the ancestorid index */
    ainfo_get(be, LDBM_ANCESTORID_STR, &ai_aid);

    /* Prevent any other use of the index */
    ai_aid->ai_indexmask |= INDEX_OFFLINE;

    /* Open the ancestorid index file */
    ret = dblayer_get_index_file(be, ai_aid, (dbi_db_t**)&db_aid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("dbmdb_ancestorid_default_create_index", sourcefile, 13050, ret);
        goto out;
    }

    /* Maybe nothing to do */
    if (nodes == NULL || nodes->b_nids == 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_default_create_index",
                      "Nothing to do to build ancestorid index\n");
        goto out;
    }

    /* Create an ancestorid cache */
    ht = dbmdb_id2idl_new_hash(nodes->b_nids);

    /* Get the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai_pid);

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai_pid, (dbi_db_t**)&db_pid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("dbmdb_ancestorid_default_create_index", sourcefile, 13060, ret);
        goto out;
    }

    /* Initialize key MDB_val */
    dblayer_value_set_buffer(be, &key, keybuf, sizeof(keybuf));

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_default_create_index",
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
            ldbm_nasty("dbmdb_ancestorid_default_create_index", sourcefile, 13070, ret);
            break;
        }

        /* check if we need to abort */
        if (job->flags & FLAG_ABORT) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_ancestorid_default_create_index",
                              "ancestorid creation aborted.");
            ret = -1;
            break;
        }

        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d ancestors...",
                                  key_count);
            }
            started_progress_logging = 1;
        }

        /* Insert into ancestorid for this node */
        if (dbmdb_id2idl_hash_lookup(ht, &id, &ididl)) {
            descendants = dbmdb_idl_union_allids(be, ai_aid, ididl->idl, children);
            idl_free(&children);
            if (dbmdb_id2idl_hash_remove(ht, &id) == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_default_create_index",
                              "dbmdb_id2idl_hash_remove() failed\n");
            } else {
                dbmdb_id2idl_free(&ididl);
            }
        } else {
            descendants = children;
        }
        ret = idl_store_block(be, db_aid, &key, descendants, txn, ai_aid);
        if (ret != 0)
            break;

        /* Get parentid for this entry */
        ret = dbmdb_parentid(be, txn, id, &parentid);
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
        if (dbmdb_id2idl_hash_lookup(ht, &parentid, &ididl)) {
            IDList *idl = dbmdb_idl_union_allids(be, ai_aid, ididl->idl, descendants);
            idl_free(&descendants);
            idl_free(&(ididl->idl));
            ididl->idl = idl;
        } else {
            ididl = (dbmdb_id2idl *)slapi_ch_calloc(1, sizeof(dbmdb_id2idl));
            ididl->keyid = parentid;
            ididl->idl = descendants;
            if (dbmdb_id2idl_hash_add(ht, &parentid, ididl, NULL) == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_default_create_index ",
                              "dbmdb_id2idl_hash_add failed\n");
            }
        }

    } while (nids > 0);

    if (ret != 0) {
        goto out;
    }

    /* We're expecting the cache to be empty */
    ret = dbmdb_check_cache(ht);

out:

    /* Destroy the cache */
    dbmdb_id2idl_hash_destroy(ht);

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
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_default_create_index",
                                  "Creating ancestorid index: processed %d ancestors",
                                  key_count);
            }
        }
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_default_create_index",
                          "Created ancestorid index (old idl).");
        ai_aid->ai_indexmask &= ~INDEX_OFFLINE;
    }

    return ret;
}

/*
 * Create the ancestorid index.  This version expects to use
 * idl_new_store_block() and should be used when idl_new != 0.
 * It has lower overhead and can be faster than
 * dbmdb_ancestorid_default_create_index(), particularly on
 * large databases.  Cf. bug 469800.
 */
static int
dbmdb_ancestorid_new_idl_create_index(backend *be, ImportJob *job)
{
    int key_count = 0;
    int ret = 0;
    dbmdb_dbi_t *db_pid = NULL;
    dbmdb_dbi_t *db_aid = NULL;
    dbi_val_t key = {0};
    struct attrinfo *ai_pid = NULL;
    struct attrinfo *ai_aid = NULL;
    char keybuf[24];
    IDList *nodes = NULL;
    IDList *children = NULL;
    NIDS nids;
    ID id, parentid;
    int started_progress_logging = 0;
    dbi_txn_t *txn = NULL;

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
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_new_idl_create_index",
                      "Cannot create ancestorid index.  "
                      "New IDL version called but idl_new is false!\n");
        return 1;
    }

    /* Get the non-leaf node IDs */
    ret = dbmdb_get_nonleaf_ids(be, NULL, &nodes, job);
    if (ret != 0)
        return ret;

    /* Get the ancestorid index */
    ainfo_get(be, LDBM_ANCESTORID_STR, &ai_aid);

    /* Prevent any other use of the index */
    ai_aid->ai_indexmask |= INDEX_OFFLINE;

    /* Open the ancestorid index file */
    ret = dblayer_get_index_file(be, ai_aid, (dbi_db_t**)&db_aid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("dbmdb_ancestorid_new_idl_create_index", sourcefile, 13050, ret);
        goto out;
    }

    /* Maybe nothing to do */
    if (nodes == NULL || nodes->b_nids == 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_new_idl_create_index",
                      "Nothing to do to build ancestorid index\n");
        goto out;
    }

    /* Get the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai_pid);

    /* Open the parentid index file */
    ret = dblayer_get_index_file(be, ai_pid, (dbi_db_t**)&db_pid, DBOPEN_CREATE);
    if (ret != 0) {
        ldbm_nasty("dbmdb_ancestorid_new_idl_create_index", sourcefile, 13060, ret);
        goto out;
    }

    ret = START_TXN(&txn,NULL,0);  /* Start a rw txn to update the index */
    if (ret) {
        import_log_notice(job,SLAPI_LOG_ERR,"dbmdb_public_dbmdb_import_main",
                      "Failed to begin a transaction");
        goto out;
    }

    /* Initialize key memory */
    dblayer_value_set_buffer(be, &key, keybuf, sizeof(keybuf));

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_new_idl_create_index",
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
            ldbm_nasty("dbmdb_ancestorid_new_idl_create_index", sourcefile, 13070, ret);
            break;
        }

        /* check if we need to abort */
        if (job->flags & FLAG_ABORT) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_ancestorid_new_idl_create_index",
                              "ancestorid creation aborted.");
            ret = -1;
            break;
        }

        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: progress %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: progress %d ancestors...",
                                  key_count);
            }
            started_progress_logging = 1;
        }

        /* Instead of maintaining a full accounting of IDs in a hashtable
         * as is done with dbmdb_ancestorid_default_create_index(), perform
         * incremental updates straight to the dbmdb_dbi_t with idl_new_store_block()
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
            ret = dbmdb_parentid(be, txn, id, &parentid);
            if (ret != 0) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_new_idl_create_index",
                              "Failure: dbmdb_parentid on node index [" ID_FMT "] of [" ID_FMT "]\n",
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
    if (txn) {
        ret = END_TXN(&txn, ret);
    }
    if (ret == 0) {
        if (started_progress_logging) {
            /* finish what we started logging */
            if (job->numsubordinates) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: processed %d%% (ID count %d)",
                                  (key_count * 100 / job->numsubordinates), key_count);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_new_idl_create_index",
                                  "Creating ancestorid index: processed %d ancestors",
                                  key_count);
            }
        }
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_ancestorid_new_idl_create_index",
                          "Created ancestorid index (new idl).");
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ancestorid_new_idl_create_index",
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
/* Update subordinate count in a hint list, given the parent's ID */
int
dbmdb_import_subcount_mother_init(import_subcount_stuff *mothers, ID parent_id, size_t count)
{
    PR_ASSERT(NULL == PL_HashTableLookup(mothers->hashtable, (void *)((uintptr_t)parent_id)));
    PL_HashTableAdd(mothers->hashtable, (void *)((uintptr_t)parent_id), (void *)count);
    return 0;
}

/* Update subordinate count in a hint list, given the parent's ID */
int
dbmdb_import_subcount_mother_count(import_subcount_stuff *mothers, ID parent_id)
{
    size_t stored_count = 0;

    /* Lookup the hash table for the target ID */
    stored_count = (size_t)PL_HashTableLookup(mothers->hashtable,
                                              (void *)((uintptr_t)parent_id));
    PR_ASSERT(0 != stored_count);
    /* Increment the count */
    stored_count++;
    PL_HashTableAdd(mothers->hashtable, (void *)((uintptr_t)parent_id), (void *)stored_count);
    return 0;
}

static int
dbmdb_import_update_entry_subcount(backend *be, ID parentid, size_t sub_count, int isencrypted, back_txn *txn)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int ret = 0;
    modify_context mc = {0};
    char value_buffer[22] = {0}; /* enough digits for 2^64 children */
    struct backentry *e = NULL;
    int isreplace = 0;
    char *numsub_str = numsubordinates;

    /* Get hold of the parent */
    e = id2entry(be, parentid, txn, &ret);
    if ((NULL == e) || (0 != ret)) {
        ldbm_nasty("dbmdb_import_update_entry_subcount", sourcefile, 5, ret);
        return (0 == ret) ? -1 : ret;
    }
    /* Lock it (not really required since we're single-threaded here, but
     * let's do it so we can reuse the modify routines) */
    cache_lock_entry(&inst->inst_cache, e);
    modify_init(&mc, e);
    mc.attr_encrypt = isencrypted;
    sprintf(value_buffer, "%lu", (long unsigned int)sub_count);
    /* If it is a tombstone entry, add tombstonesubordinates instead of
     * numsubordinates. */
    if (slapi_entry_flag_is_set(e->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE)) {
        numsub_str = tombstone_numsubordinates;
    }
    /* attr numsubordinates/tombstonenumsubordinates could already exist in
     * the entry, let's check whether it's already there or not */
    isreplace = (attrlist_find(e->ep_entry->e_attrs, numsub_str) != NULL);
    {
        int op = isreplace ? LDAP_MOD_REPLACE : LDAP_MOD_ADD;
        Slapi_Mods *smods = slapi_mods_new();

        slapi_mods_add(smods, op | LDAP_MOD_BVALUES, numsub_str,
                       strlen(value_buffer), value_buffer);
        ret = modify_apply_mods(&mc, smods); /* smods passed in */
    }
    if (0 == ret || LDAP_TYPE_OR_VALUE_EXISTS == ret) {
        /* This will correctly index subordinatecount: */
        ret = modify_update_all(be, NULL, &mc, txn);
        if (0 == ret) {
            modify_switch_entries(&mc, be);
        }
    }
    /* entry is unlocked and returned to the cache in modify_term */
    modify_term(&mc, be);
    return ret;
}

/*
 * Function: dbmdb_update_subordinatecounts
 *
 * Returns: Nothing
 *
 */
static int
dbmdb_update_subordinatecounts(backend *be, ImportJob *job, dbi_txn_t *txn)
{
    int isencrypted = job->encrypt;
    int started_progress_logging = 0;
    int key_count = 0;
    int ret = 0;
    dbmdb_dbi_t*db = NULL;
    MDB_cursor *dbc = NULL;
    struct attrinfo *ai = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    dbmdb_cursor_t cursor = {0};
    struct ldbminfo *li = (struct ldbminfo*)be->be_database->plg_private;
	back_txn btxn = {0};

    /* Open the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai);

    /* Open the parentid index file */
    if ((ret = dblayer_get_index_file(be, ai, (dbi_db_t**)&db, DBOPEN_CREATE)) != 0) {
        ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 67, ret);
        return (ret);
    }
    /* Get a cursor with r/w txn so we can walk through the parentid */
    ret = dbmdb_open_cursor(&cursor, MDB_CONFIG(li), db, 0);
    if (ret != 0) {
        ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 68, ret);
        dblayer_release_index_file(be, ai, db);
        return ret;
    }
    dbc = cursor.cur;
    txn = cursor.txn;
    btxn.back_txn_txn = txn;
    ret = MDB_CURSOR_GET(dbc, &key, &data, MDB_FIRST);

    /* Walk along the index */
    while (ret != MDB_NOTFOUND) {
        size_t sub_count = 0;
        ID parentid = 0;

        if (0 != ret) {
            key.mv_data=NULL;
            ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 62, ret);
            break;
        }
        /* check if we need to abort */
        if (job->flags & FLAG_ABORT) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_update_subordinatecounts",
                              "numsubordinate generation aborted.");
            break;
        }
        /*
         * Do an update count
         */
        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_update_subordinatecounts",
                              "numsubordinate generation: processed %d entries...",
                              key_count);
            started_progress_logging = 1;
        }

        if (*(char *)key.mv_data == EQ_PREFIX) {
            char *idptr = NULL;

            /* construct the parent's ID from the key */
            /* Look for the ID in the hint list supplied by the caller */
            /* If its there, we know the answer already */
            idptr = (((char *)key.mv_data) + 1);
            parentid = (ID)atol(idptr);
            PR_ASSERT(0 != parentid);
            ret = mdb_cursor_count(dbc, &sub_count);
            if (ret) {
                ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 63, ret);
                break;
            }
            PR_ASSERT(0 != sub_count);
            ret = dbmdb_import_update_entry_subcount(be, parentid, sub_count, isencrypted, &btxn);
            if (ret) {
                ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 64, ret);
                break;
            }
        }
        ret = MDB_CURSOR_GET(dbc, &key, &data, MDB_NEXT_NODUP);
    }
    if (started_progress_logging) {
        /* Finish what we started... */
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_update_subordinatecounts",
                          "numsubordinate generation: processed %d entries.",
                          key_count);
        job->numsubordinates = key_count;
    }
    if (ret == MDB_NOTFOUND) {
        ret = 0;
    }

    dbmdb_close_cursor(&cursor, ret);
    dblayer_release_index_file(be, ai, db);

    return (ret);
}

/* Function used to gather a list of indexed attrs */
static int
dbmdb_import_attr_callback(void *node, void *param)
{
    ImportJob *job = (ImportJob *)param;
    struct attrinfo *a = (struct attrinfo *)node;

    if (job->flags & FLAG_DRYRUN) { /* dryrun; we don't need the workers */
        return 0;
    }
    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        /* Bring up import workers just for indexes having DN syntax
         * attribute type. (except entrydn -- taken care below) */
        int rc = 0;
        Slapi_Attr attr = {0};

        /*
         * Treat cn and ou specially.  Bring up the import workers for
         * cn and ou even though they are not DN syntax attribute.
         * This is done because they have some exceptional case to store
         * DN format in the admin entries such as UserPreferences.
         */
        if ((0 == PL_strcasecmp("cn", a->ai_type)) ||
            (0 == PL_strcasecmp("commonname", a->ai_type)) ||
            (0 == PL_strcasecmp("ou", a->ai_type)) ||
            (0 == PL_strcasecmp("organizationalUnit", a->ai_type))) {
            ;
        } else {
            slapi_attr_init(&attr, a->ai_type);
            rc = slapi_attr_is_dn_syntax_attr(&attr);
            attr_done(&attr);
            if (0 == rc) {
                return 0;
            }
        }
    }

    /* OK, so we now have hold of the attribute structure and the job info,
     * let's see what we have.  Remember that although this function is called
     * many times, all these calls are in the context of a single thread, so we
     * don't need to worry about protecting the data in the job structure.
     */

    /* We need to specifically exclude the (entrydn, entryrdn) & parentid &
     * ancestorid indexes because we build those in the foreman thread.
     */
    if (IS_INDEXED(a->ai_indexmask) &&
        (strcasecmp(a->ai_type, LDBM_ENTRYDN_STR) != 0) &&
        (strcasecmp(a->ai_type, LDBM_ENTRYRDN_STR) != 0) &&
        (strcasecmp(a->ai_type, LDBM_PARENTID_STR) != 0) &&
        (strcasecmp(a->ai_type, LDBM_ANCESTORID_STR) != 0) &&
        (strcasecmp(a->ai_type, numsubordinates) != 0)) {
        /* Make an import_index_info structure, fill it in and insert into the
         * job's list */
        IndexInfo *info = CALLOC(IndexInfo);

        if (NULL == info) {
            /* Memory allocation error */
            return -1;
        }
        info->name = slapi_ch_strdup(a->ai_type);
        info->ai = a;
        if (NULL == info->name) {
            /* Memory allocation error */
            FREE(info);
            return -1;
        }
        info->next = job->index_list;
        job->index_list = info;
        job->number_indexers++;
    }
    return 0;
}

static void
dbmdb_import_set_index_buffer_size(ImportJob *job)
{
    IndexInfo *current_index = NULL;
    size_t substring_index_count = 0;
    size_t proposed_size = 0;

    /* Count the substring indexes we have */
    for (current_index = job->index_list; current_index != NULL;
         current_index = current_index->next) {
        if (current_index->ai->ai_indexmask & INDEX_SUB) {
            substring_index_count++;
        }
    }
    if (substring_index_count > 0) {
        /* Make proposed size such that if all substring indices were
     * reasonably full, we'd hit the target space */
        proposed_size = (job->job_index_buffer_size / substring_index_count) /
                        IMPORT_INDEX_BUFFER_SIZE_CONSTANT;
        if (proposed_size > IMPORT_MAX_INDEX_BUFFER_SIZE) {
            proposed_size = IMPORT_MAX_INDEX_BUFFER_SIZE;
        }
        if (proposed_size < IMPORT_MIN_INDEX_BUFFER_SIZE) {
            proposed_size = 0;
        }
    }

    job->job_index_buffer_suggestion = proposed_size;
}

static void
dbmdb_import_free_thread_data(ImportJob *job)
{
    /* DBMDB_dbifree the lists etc */
    ImportWorkerInfo *worker = job->worker_list;

    while (worker != NULL) {
        ImportWorkerInfo *asabird = worker;
        worker = worker->next;
        if (asabird->work_type != PRODUCER)
            slapi_ch_free((void **)&asabird);
    }
}

void
dbmdb_import_free_job(ImportJob *job)
{
    /* DBMDB_dbifree the lists etc */
    IndexInfo *index = job->index_list;

    dbmdb_import_free_thread_data(job);
    while (index != NULL) {
        IndexInfo *asabird = index;
        index = index->next;
        slapi_ch_free((void **)&asabird->name);
        slapi_ch_free((void **)&asabird);
    }
    job->index_list = NULL;
    if (NULL != job->mothers) {
        import_subcount_stuff_term(job->mothers);
        slapi_ch_free((void **)&job->mothers);
    }

    dbmdb_back_free_incl_excl(job->include_subtrees, job->exclude_subtrees);
    charray_free(job->input_filenames);
    if (job->fifo.size) {
        /* dbmdb_bulk_import_queue is startcfg, while holding the job lock.
         * dbmdb_bulk_import_queue is using the fifo queue.
         * To avoid freeing fifo queue under dbmdb_bulk_import_queue use
         * job lock to synchronize
         */
        pthread_mutex_lock(&job->wire_lock);
        dbmdb_import_fifo_destroy(job);
        pthread_mutex_unlock(&job->wire_lock);
    }

    if (NULL != job->uuid_namespace) {
        slapi_ch_free((void **)&job->uuid_namespace);
    }
    pthread_mutex_destroy(&job->wire_lock);
    pthread_cond_destroy(&job->wire_cv);
    slapi_ch_free((void **)&job->task_status);
}

/* determine if we are the correct backend for this entry
 * (in a distributed suffix, some entries may be for other backends).
 * if the entry's dn actually matches one of the suffixes of the be, we
 * automatically take it as a belonging one, for such entries must be
 * present in EVERY backend independently of the distribution applied.
 */
int
dbmdb_import_entry_belongs_here(Slapi_Entry *e, backend *be)
{
    Slapi_Backend *retbe;
    Slapi_DN *sdn = slapi_entry_get_sdn(e);

    if (slapi_be_issuffix(be, sdn))
        return 1;

    retbe = slapi_mapping_tree_find_backend_for_sdn(sdn);
    return (retbe == be);
}


/********** starting threads and stuff **********/

/* Solaris is weird---we need an LWP per thread but NSPR doesn't give us
 * one unless we make this magic belshe-call */
/* Fixed on Solaris 8; NSPR supports PR_GLOBAL_BOUND_THREAD */
#define CREATE_THREAD PR_CreateThread

static void
dbmdb_import_init_worker_info(ImportWorkerInfo *info, ImportJob *job)
{
    info->command = PAUSE;
    info->job = job;
    info->first_ID = job->first_ID;
    info->index_buffer_size = job->job_index_buffer_suggestion;
}

static int
dbmdb_import_start_threads(ImportJob *job)
{
    IndexInfo *current_index = NULL;
    ImportWorkerInfo *foreman = NULL, *worker = NULL;
    ImportWorkerInfo *writer = NULL;

    foreman = CALLOC(ImportWorkerInfo);
    if (!foreman)
        goto error;

    /* start the foreman */
    dbmdb_import_init_worker_info(foreman, job);
    strncpy(foreman->name, "foreman", sizeof foreman->name);
    foreman->work_type = FOREMAN;
    if (!CREATE_THREAD(PR_USER_THREAD, (VFP)dbmdb_import_foreman, foreman,
                       PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                       PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_start_threads",
                      "Unable to spawn import foreman thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        FREE(foreman);
        goto error;
    }

    foreman->next = job->worker_list;
    job->worker_list = foreman;
    writer = CALLOC(ImportWorkerInfo);
    strncpy(writer->name, "mdbimportwriter", sizeof writer->name);
    dbmdb_import_init_worker_info(writer, job);
    writer->work_type = WRITER;
    if(!CREATE_THREAD(PR_USER_THREAD, (VFP)dbmdb_import_writer, writer,
                      PR_PRIORITY_NORMAL,PR_GLOBAL_BOUND_THREAD,
                      PR_UNJOINABLE_THREAD,SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr=PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR,"dbmdb_import_start_threads",
                      "Unabletospawnimportwriterthread,"SLAPI_COMPONENT_NAME_NSPR"error%d(%s)\n",
                      prerr,slapd_pr_strerror(prerr));
        FREE(writer);
        goto error;
    }

    writer->next=job->worker_list;
    job->worker_list=writer;

    /* Start follower threads, if we are doing attribute indexing */
    current_index = job->index_list;
    if (job->flags & FLAG_INDEX_ATTRS) {
        while (current_index) {
            /* make a new thread info structure */
            worker = CALLOC(ImportWorkerInfo);
            if (!worker)
                goto error;

            /* fill it in */
            dbmdb_import_init_worker_info(worker, job);
            worker->index_info = current_index;
            worker->work_type = WORKER;
            PR_snprintf(worker->name, sizeof worker->name, "indexer%s", worker->index_info->name);

            /* Start the thread */
            if (!CREATE_THREAD(PR_USER_THREAD, (VFP)dbmdb_import_worker, worker,
                               PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                               PR_UNJOINABLE_THREAD,
                               SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_start_threads",
                              "Unable to spawn import worker thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              prerr, slapd_pr_strerror(prerr));
                FREE(worker);
                goto error;
            }

            /* link it onto the job's thread list */
            worker->next = job->worker_list;
            job->worker_list = worker;
            current_index = current_index->next;
        }
    }
    return 0;

error:
    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_start_threads", "Import thread creation failed.");
    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_start_threads", "Aborting all import threads...");
    import_abort_all(job, 1);
    import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_start_threads", "Import threads aborted.");
    return -1;
}


/********** monitoring the worker threads **********/

static void
dbmdb_import_clear_progress_history(ImportJob *job)
{
    int i = 0;

    for (i = 0; i < IMPORT_JOB_PROG_HISTORY_SIZE /*- 1*/; i++) {
        job->progress_history[i] = job->first_ID;
        job->progress_times[i] = job->start_time;
    }
    /* reset limdb cache stats */
    job->inst->inst_cache_hits = job->inst->inst_cache_misses = 0;
}

static char *
dbmdb_import_decode_worker_state(int state)
{
    switch (state) {
    case WAITING:
        return "W";
    case RUNNING:
        return "R";
    case FINISHED:
        return "F";
    case ABORTED:
        return "A";
    default:
        return "?";
    }
}

static void
dbmdb_import_print_worker_status(ImportWorkerInfo *info)
{
    char *name = (info->work_type == PRODUCER ? "Producer" : (info->work_type == FOREMAN ? "Foreman" :
                  (info->work_type == WRITER ? "Writer" : info->index_info->name)));

    dbmdb_import_log_status_add_line(info->job,
                               "%-25s %s%10ld %7.1f", name,
                               dbmdb_import_decode_worker_state(info->state),
                               info->last_ID_processed, info->rate);
}



static void
dbmdb_import_push_progress_history(ImportJob *job, ID current_id, time_t current_time)
{
    int i = 0;

    for (i = 0; i < IMPORT_JOB_PROG_HISTORY_SIZE - 1; i++) {
        job->progress_history[i] = job->progress_history[i + 1];
        job->progress_times[i] = job->progress_times[i + 1];
    }
    job->progress_history[i] = current_id;
    job->progress_times[i] = current_time;
}

static void
dbmdb_import_calc_rate(ImportWorkerInfo *info, int time_interval)
{
    size_t ids = info->last_ID_processed - info->previous_ID_counted;
    double rate = (double)ids / time_interval;

    if ((info->previous_ID_counted != 0) && (info->last_ID_processed != 0)) {
        info->rate = rate;
    } else {
        info->rate = 0;
    }
    info->previous_ID_counted = info->last_ID_processed;
}

/* find the rate (ids/time) of work from a worker thread between history
 * marks A and B.
 */
#define HISTORY(N) (job->progress_history[N])
#define TIMES(N) (job->progress_times[N])
#define PROGRESS(A, B) ((HISTORY(B) > HISTORY(A)) ? ((double)(HISTORY(B) - HISTORY(A)) / \
                                                     (double)(TIMES(B) - TIMES(A)))      \
                                                  : (double)0)

static int
dbmdb_import_monitor_threads(ImportJob *job, int *status)
{
    PRIntervalTime tenthsecond = PR_MillisecondsToInterval(100);
    ImportWorkerInfo *current_worker = NULL;
    ImportWorkerInfo *producer = NULL, *foreman = NULL;
    ImportWorkerInfo *writer = NULL;
    int finished = 0;
    int giveup = 0;
    int count = 1; /* 1 to prevent premature status report */
    int producer_done = 0;
    int writer_done=0;
    const int display_interval = 200;
    time_t time_now = 0;
    time_t last_time = 0;
    time_t time_interval = 0;
    int rc = 0;
    int corestate = 0;

    for (current_worker = job->worker_list; current_worker != NULL;
         current_worker = current_worker->next) {
        current_worker->command = RUN;
        if (current_worker->work_type == PRODUCER)
            producer = current_worker;
        if (current_worker->work_type == FOREMAN)
            foreman = current_worker;
        if (current_worker->work_type == WRITER)
            writer = current_worker;
    }


    if (job->flags & FLAG_USE_FILES)
        PR_ASSERT(producer != NULL);

    PR_ASSERT(foreman != NULL);

    if (!foreman) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_monitor_threads", "Noforeman==>Abortingimport.\n");
        goto error_abort;
    }
    if (!writer) {
        import_log_notice(job,SLAPI_LOG_ERR,"dbmdb_import_monitor_threads","Nowriter==>Abortingimport.\n");
        goto error_abort;
    }

    last_time = slapi_current_rel_time_t();
    job->start_time = last_time;
    dbmdb_import_clear_progress_history(job);

    while (!finished) {
        ID trailing_ID = NOID;

        DS_Sleep(tenthsecond);
        finished = 1;

        /* First calculate the time interval since last reported */
        if (0 == (count % display_interval)) {
            time_now = slapi_current_rel_time_t();
            time_interval = time_now - last_time;
            last_time = time_now;
            /* Now calculate our rate of progress overall for this chunk */
            if (time_now != job->start_time) {
                /* log a cute chart of the worker progress */
                dbmdb_import_log_status_start(job);
                dbmdb_import_log_status_add_line(job,
                                           "Index status for import of %s:", job->inst->inst_name);
                dbmdb_import_log_status_add_line(job,
                                           "-------Index Task-------State---Entry----Rate-");

                dbmdb_import_push_progress_history(job, foreman->last_ID_processed,
                                             time_now);
                job->average_progress_rate =
                    (double)(HISTORY(IMPORT_JOB_PROG_HISTORY_SIZE - 1) + 1 - foreman->first_ID) /
                    (double)(TIMES(IMPORT_JOB_PROG_HISTORY_SIZE - 1) - job->start_time);
                job->recent_progress_rate =
                    PROGRESS(0, IMPORT_JOB_PROG_HISTORY_SIZE - 1);
                job->cache_hit_ratio = dbmdb_writer_get_progress(job);
            }
        }

        for (current_worker = job->worker_list; current_worker != NULL;
             current_worker = current_worker->next) {
            /* Calculate the ID at which the slowest worker is currently
             * processing */
            if ((trailing_ID > current_worker->last_ID_processed) &&
                (current_worker->work_type == WORKER)) {
                trailing_ID = current_worker->last_ID_processed;
            }
            if (0 == (count % display_interval) && time_interval) {
                dbmdb_import_calc_rate(current_worker, time_interval);
                dbmdb_import_print_worker_status(current_worker);
            }
            corestate = current_worker->state & CORESTATE;
            if (current_worker->state == ABORTED) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_monitor_threads", "worker %s aborted ==> Aborting import.\n", current_worker->name);
                goto error_abort;
            } else if ((corestate == QUIT) || (corestate == FINISHED)) {
                if (DN_NORM_BT == (DN_NORM_BT & current_worker->state)) {
                    /* upgrading dn norm (both) is needed */
                    rc = NEED_DN_NORM_BT; /* Set the RC; Don't abort now;
                                           * We have to stop other
                                           * threads */
                } else if (DN_NORM == (DN_NORM_BT & current_worker->state)) {
                    /* upgrading dn norm is needed */
                    rc = NEED_DN_NORM; /* Set the RC; Don't abort now;
                                        * We have to stop other threads
                                        */
                } else if (DN_NORM_SP == (DN_NORM_BT & current_worker->state)) {
                    /* upgrading spaces in dn norm is needed */
                    rc = NEED_DN_NORM_SP; /* Set the RC; Don't abort now;
                                           * We have to stop other
                                           * threads */
                }
                current_worker->state = corestate;
            } else if (current_worker->state != FINISHED) {
                finished = 0;
            }
        }

        if (finished && writer->state != FINISHED) {
            finished=0;
        }

        if ((0 == (count % display_interval)) &&
            (job->start_time != time_now)) {
            char buffer[256], *p = buffer;

            dbmdb_import_log_status_done(job);
            p += sprintf(p, "Processed %lu entries ", (u_long)job->ready_ID);
            if (job->total_pass > 1)
                p += sprintf(p, "(pass %d) ", job->total_pass);

            p += sprintf(p, "-- average rate %.1f/sec, ",
                         job->average_progress_rate);
            p += sprintf(p, "recent rate %.1f/sec, ",
                         job->recent_progress_rate);
            p += sprintf(p, "recent writer progress %.0f%%", job->cache_hit_ratio * 100.0);
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads", "%s", buffer);
        }

        /* if the producer is finished, and the foreman has caught up... */
        if (producer) {
            producer_done = (producer->state == FINISHED) ||
                            (producer->state == QUIT);
        } else {
            /* set in dbmdb_ldbm_back_wire_import */
            producer_done = (job->flags & FLAG_PRODUCER_DONE);
        }
        if (producer_done && (job->lead_ID == job->ready_ID)) {
            /* tell the foreman to stop if he's still working. */
            if (foreman->state != FINISHED)
                foreman->command = STOP;

        writer_done = (writer->state == FINISHED) || (writer->state == QUIT);
            /* if writer and all the workers are caught up too, we're done */
        if (trailing_ID == job->lead_ID && writer_done)
                break;
        }

        /* if the foreman is done (end of pass) and the worker threads
         * have caught up...
         */
        if ((foreman->state == FINISHED) && (job->ready_ID == trailing_ID)) {
            break;
        }

        count++;
    }

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads",
                      "Workers finished; cleaning up...");

    /* Now tell all the workers to stop */
    for (current_worker = job->worker_list; current_worker != NULL;
         current_worker = current_worker->next) {
        if (current_worker->work_type != PRODUCER)
            current_worker->command = STOP;
    }

    /* Having done that, wait for them to say that they've stopped */
    for (current_worker = job->worker_list; current_worker != NULL;) {
        if ((current_worker->state != FINISHED) &&
            (current_worker->state != ABORTED) &&
            (current_worker->state != QUIT) &&
            (current_worker->work_type != PRODUCER)) {
            DS_Sleep(tenthsecond); /* Only sleep if we hit a thread that is still not done */
            continue;
        } else {
            current_worker = current_worker->next;
        }
    }
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads", "Workers cleaned up.");

    /* If we're here and giveup is true, and the primary hadn't finished
     * processing the input files, we need to return IMPORT_INCOMPLETE_PASS */
    if (giveup && (job->input_filenames || (job->flags & FLAG_ONLINE) ||
                   (job->flags & FLAG_REINDEXING /* support multi-pass */))) {
        if (producer_done && (job->ready_ID == job->lead_ID)) {
            /* foreman caught up with the producer, and the producer is
             * done.
             */
            *status = IMPORT_COMPLETE_PASS;
        } else {
            *status = IMPORT_INCOMPLETE_PASS;
        }
    } else {
        *status = IMPORT_COMPLETE_PASS;
    }
    return rc;

error_abort:
    return ERR_IMPORT_ABORTED;
}


/********** startcfg passes **********/

static int
dbmdb_import_run_pass(ImportJob *job, int *status)
{
    int ret = 0;

    /* Start the threads startcfg */
    ret = dbmdb_import_start_threads(job);
    if (ret != 0) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_run_pass", "Starting threads failed: %d\n", ret);
        goto error;
    }

    /* Monitor the threads until we're done or fail */
    ret = dbmdb_import_monitor_threads(job, status);
    if ((ret == ERR_IMPORT_ABORTED) || (ret == NEED_DN_NORM) ||
        (ret == NEED_DN_NORM_SP) || (ret == NEED_DN_NORM_BT)) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_run_pass", "Thread monitoring returned: %d\n", ret);
        goto error;
    } else if (ret != 0) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_run_pass", "Thread monitoring aborted: %d\n", ret);
        goto error;
    }

error:
    return ret;
}

static void
dbmdb_import_set_abort_flag_all(ImportJob *job, int wait_for_them)
{

    ImportWorkerInfo *worker;

    /* tell all the worker threads to abort */
    job->flags |= FLAG_ABORT;
    dbmdb_writer_wakeup(job);

    /* setting of the flag in the job will be detected in the worker, foreman
     * threads and if there are any threads which have a sleeptime  200 msecs
     * = import_sleep_time; after that time, they will examine the condition
     * (job->flags & FLAG_ABORT) which will unblock the thread to proceed to
     * abort. Hence, we will sleep here for atleast 3 sec to make sure clean
     * up occurs */
    /* allow all the aborts to be processed */
    DS_Sleep(PR_MillisecondsToInterval(3000));

    if (wait_for_them) {
        /* Having done that, wait for them to say that they've stopped */
        for (worker = job->worker_list; worker != NULL;) {
            DS_Sleep(PR_MillisecondsToInterval(100));
            if ((worker->state != FINISHED) && (worker->state != ABORTED) &&
                (worker->state != QUIT)) {
                continue;
            } else {
                worker = worker->next;
            }
        }
    }
}

/* Helper function to make up filenames */

/* when the import is done, this function is called to bring stuff back up.
 * returns 0 on success; anything else is an error
 */
static int
dbmdb_import_all_done(ImportJob *job, int ret)
{
    ldbm_instance *inst = job->inst;

    if ((job->task != NULL) && (0 == slapi_task_get_refcount(job->task))) {
        slapi_task_finish(job->task, ret);
    }

    if (job->flags & FLAG_ONLINE) {
        /* make sure the indexes are online as well */
        /* richm 20070919 - if index entries are added online, they
           are created and marked as INDEX_OFFLINE, in anticipation
           of someone doing a db2index.  In this case, the db2index
           code will correctly unset the INDEX_OFFLINE flag.
           However, if import is used to create the indexes, the
           INDEX_OFFLINE flag will not be cleared.  So, we do that
           here
        */
        IndexInfo *index = job->index_list;
        while (index != NULL) {
            index->ai->ai_indexmask &= ~INDEX_OFFLINE;
            index = index->next;
        }
        /* start up the instance */
        ret = dbmdb_instance_start(job->inst->inst_be, DBLAYER_NORMAL_MODE);
        if (ret != 0)
            return ret;

        /* Reset USN slapi_counter with the last key of the entryUSN index */
        ldbm_set_last_usn(inst->inst_be);

        /* bring backend online again */
        slapi_mtn_be_enable(inst->inst_be);
    }

    return ret;
}


int
dbmdb_public_dbmdb_import_main(void *arg)
{
    ImportJob *job = (ImportJob *)arg;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    int ret = 0;
    time_t beginning = 0;
    time_t end = 0;
    int finished = 0;
    int status = 0;
    int verbose = 1;
    int aborted = 0;
    ImportWorkerInfo *producer = NULL;
    char *opstr = "Import";

    if (job->task) {
        slapi_task_inc_refcount(job->task);
    }

    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        if (job->flags & FLAG_DRYRUN) {
            opstr = "Upgrade Dn Dryrun";
        } else if ((job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) == (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
            opstr = "Upgrade Dn (Full)";
        } else if (job->flags & FLAG_UPGRADEDNFORMAT_V1) {
            opstr = "Upgrade Dn (Spaces)";
        } else {
            opstr = "Upgrade Dn (RFC 4514)";
        }
    } else if (job->flags & FLAG_REINDEXING) {
        opstr = "Reindexing";
    }
    PR_ASSERT(inst != NULL);
    beginning = slapi_current_rel_time_t();

    /* Decide which indexes are needed */
    if (job->flags & FLAG_INDEX_ATTRS) {
        /* Here, we get an AVL tree which contains nodes for all attributes
         * in the schema.  Given this tree, we need to identify those nodes
         * which are marked for indexing. */
        avl_apply(job->inst->inst_attrs, (IFP)dbmdb_import_attr_callback,
                  (caddr_t)job, -1, AVL_INORDER);
        vlv_getindices((IFP)dbmdb_import_attr_callback, (void *)job, be);
    }

    /* insure all dbi get open */
    dbmdb_open_all_files(NULL, job->inst->inst_be);

    /* Determine how much index buffering space to allocate to each index */
    dbmdb_import_set_index_buffer_size(job);

    /* initialize the entry FIFO */
    ret = dbmdb_import_fifo_init(job);
    if (ret) {
        if (!(job->flags & FLAG_USE_FILES)) {
            pthread_mutex_lock(&job->wire_lock);
            pthread_cond_signal(&job->wire_cv);
            pthread_mutex_unlock(&job->wire_lock);
        }
        goto error;
    }
    /* Init writer thread job context as it is needed by foreman */
    dbmdb_writer_init(job);

    if (job->flags & FLAG_USE_FILES) {
        /* importing from files: start up a producer thread to read the
         * files and queue them
         */
        producer = CALLOC(ImportWorkerInfo);
        if (!producer)
            goto error;

        /* start the producer */
        dbmdb_import_init_worker_info(producer, job);
        producer->work_type = PRODUCER;
        if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
            if (!CREATE_THREAD(PR_USER_THREAD, (VFP)dbmdb_upgradedn_producer,
                               producer, PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                               PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "Unable to spawn upgrade dn producer thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              prerr, slapd_pr_strerror(prerr));
                goto error;
            }
        } else if (job->flags & FLAG_REINDEXING) {
            if (!CREATE_THREAD(PR_USER_THREAD, (VFP)dbmdb_index_producer, producer,
                               PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                               PR_UNJOINABLE_THREAD,
                               SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "Unable to spawn index producer thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              prerr, slapd_pr_strerror(prerr));
                goto error;
            }
        } else {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Beginning import job...");
            if (!CREATE_THREAD(PR_USER_THREAD, (VFP)dbmdb_import_producer, producer,
                               PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                               PR_UNJOINABLE_THREAD,
                               SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "Unable to spawn import producer thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              prerr, slapd_pr_strerror(prerr));
                goto error;
            }
        }

        if (0 == job->job_index_buffer_suggestion)
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Index buffering is disabled.");
        else
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                              "Index buffering enabled with bucket size %lu",
                              (long unsigned int)job->job_index_buffer_suggestion);

        job->worker_list = producer;
    } else {
        /* release the startup lock and let the entries start queueing up
         * in for import */
        pthread_mutex_lock(&job->wire_lock);
        pthread_cond_signal(&job->wire_cv);
        pthread_mutex_unlock(&job->wire_lock);
    }

    /* Run as many passes as we need to complete the job or die honourably in
     * the attempt */
    while (!finished) {
        job->current_pass++;
        job->total_pass++;
        ret = dbmdb_import_run_pass(job, &status);
        /* The following could have happened:
         *     (a) Some error happened such that we're hosed.
         *         This is indicated by a non-zero return code.
         *     (b) We finished the complete file without needing a second pass
         *         This is indicated by a zero return code and a status of
         *         IMPORT_COMPLETE_PASS and current_pass == 1;
         *     (c) We completed a pass and need at least another one
         *         This is indicated by a zero return code and a status of
         *         IMPORT_INCOMPLETE_PASS
         *     (d) We just completed what turned out to be the last in a
         *         series of passes
         *         This is indicated by a zero return code and a status of
         *         IMPORT_COMPLETE_PASS and current_pass > 1
         */
        if (ret == ERR_IMPORT_ABORTED) {
            /* at least one of the threads has aborted -- shut down ALL
             * of the threads */
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "Aborting all %s threads...", opstr);
            /* this abort sets the  abort flag on the threads and will block for
             * the exit of all threads
             */
            dbmdb_import_set_abort_flag_all(job, 1);
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "%s threads aborted.", opstr);
            aborted = 1;
            goto error;
        }
        if ((ret == NEED_DN_NORM) || (ret == NEED_DN_NORM_SP) ||
            (ret == NEED_DN_NORM_BT)) {
            goto error;
        } else if (0 != ret) {
            /* Some horrible fate has befallen the import */
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "Fatal pass error %d", ret);
            goto error;
        }

        /* No error, but a number of possibilities */
        if (IMPORT_COMPLETE_PASS == status) {
            PR_ASSERT(1==job->current_pass); /* Only a single pass with lmdb */
                /* We're done !!!! */;
            finished = 1;
        }
    }

    /* kill the producer now; we're done */
    if (producer) {
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Cleaning up producer thread...");
        producer->command = STOP;
        /* wait for the lead thread to stop */
        while (producer->state != FINISHED) {
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
    }

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Indexing complete.  Post-processing...");
    /* Now do the numsubordinates attribute */
    /* [610066] reindexed db cannot be used in the following backup/restore */
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                      "Generating numsubordinates (this may take several minutes to complete)...");
    if ((!(job->flags & FLAG_REINDEXING) || (job->flags & FLAG_DN2RDN)) &&
        (ret = dbmdb_update_subordinatecounts(be, job, NULL)) != 0) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "Failed to update numsubordinates attributes");
        goto error;
    }
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                      "Generating numSubordinates complete.");

    if (!entryrdn_get_noancestorid()) {
        /* And the ancestorid index */
        /* Creating ancestorid from the scratch; delete the index file first. */
        struct attrinfo *ai = NULL;

        ainfo_get(be, "ancestorid", &ai);
        dblayer_erase_index_file(be, ai, PR_TRUE, 0);
        if ((ret = dbmdb_ancestorid_create_index(be, job)) != 0) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main", "Failed to create ancestorid index");
            goto error;
        }
    }

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Flushing caches...");

/* New way to exit the routine: check the return code.
     * If it's non-zero, delete the database files.
     * Otherwise don't, but always close the database layer properly.
     * Then return. This ensures that we can't make a half-good/half-bad
     * Database. */

error:
    /* If we fail, the database is now in a mess, so we delete it
       except dry run mode */
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Closing files...");
    cache_clear(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
    if (entryrdn_get_switch()) {
        cache_clear(&job->inst->inst_dncache, CACHE_TYPE_DN);
    }
    if (aborted) {
        /* If aborted, it's safer to rebuild the caches. */
        cache_destroy_please(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            cache_destroy_please(&job->inst->inst_dncache, CACHE_TYPE_DN);
        }
        /* initialize the entry cache */
        if (!cache_init(&(inst->inst_cache), inst->inst_cache.c_maxsize,
                        DEFAULT_CACHE_ENTRIES, CACHE_TYPE_ENTRY)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "cache_init failed.  Server should be restarted.\n");
        }

        /* initialize the dn cache */
        if (!cache_init(&(inst->inst_dncache), inst->inst_dncache.c_maxsize,
                        DEFAULT_DNCACHE_MAXCOUNT, CACHE_TYPE_DN)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "dn cache_init failed.  Server should be restarted.\n");
        }
    }
    if (0 != ret) {
        dblayer_instance_close(job->inst->inst_be);
        if (!(job->flags & (FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1))) {
            /* If not dryrun NOR upgradedn space */
            /* if startcfg in the dry run mode, don't touch the db */
            dbmdb_delete_instance_dir(be);
        }
    } else {
        if (0 != (ret = dblayer_instance_close(job->inst->inst_be))) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_public_dbmdb_import_main", "Failed to close database");
        }
    }
    end = slapi_current_rel_time_t();
    if (verbose && (0 == ret)) {
        int seconds_to_import = end - beginning;
        size_t entries_processed = job->lead_ID - (job->starting_ID - 1);
        double entries_per_second =
            seconds_to_import ? (double)entries_processed / (double)seconds_to_import : 0;

        if (job->not_here_skipped) {
            if (job->skipped) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "(%d bad entries were skipped, "
                                  "%d entries were skipped because they don't "
                                  "belong to this database) in %d seconds. "
                                  "(%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  job->skipped, job->not_here_skipped,
                                  seconds_to_import, entries_per_second);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "(%d entries were skipped because they don't "
                                  "belong to this database) "
                                  "in %d seconds. (%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  job->not_here_skipped, seconds_to_import,
                                  entries_per_second);
            }
        } else {
            if (job->skipped) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "(%d were skipped) in %d seconds. "
                                  "(%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  job->skipped, seconds_to_import,
                                  entries_per_second);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "in %d seconds. (%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  seconds_to_import, entries_per_second);
            }
        }
    }

    if (job->flags & (FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1)) {
        if (0 == ret) {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "%s complete.  %s is up-to-date.",
                              opstr, job->inst->inst_name);
            ret = 0;
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
        } else if (NEED_DN_NORM_BT == ret) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_public_dbmdb_import_main",
                              "%s complete. %s needs upgradednformat all.",
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
            ret |= WARN_UPGRADE_DN_FORMAT_ALL;
        } else if (NEED_DN_NORM == ret) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_public_dbmdb_import_main",
                              "%s complete. %s needs upgradednformat.",
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
            ret |= WARN_UPGRADE_DN_FORMAT;
        } else if (NEED_DN_NORM_SP == ret) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_public_dbmdb_import_main",
                              "%s complete. %s needs upgradednformat spaces.",
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
            ret |= WARN_UPGRADE_DN_FORMAT_SPACE;
        } else {
            ret = -1;
            if (job->task != NULL) {
                slapi_task_finish(job->task, ret);
            }
        }
    } else if (0 != ret) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main", "%s failed.", opstr);
        if (job->task != NULL) {
            slapi_task_finish(job->task, ret);
        }
    } else {
        if (job->task) {
            slapi_task_dec_refcount(job->task);
        }
        dbmdb_import_all_done(job, ret);
    }

    /* set task warning if there are no errors */
    if((!ret) && (job->skipped)) {
        ret |= WARN_SKIPPED_IMPORT_ENTRY;
    }
    dbmdb_clear_dirty_flags(be);

    /* This instance isn't busy anymore */
    instance_set_not_busy(job->inst);

    dbmdb_writer_cleanup(job);
    dbmdb_import_free_job(job);
    if (!job->task) {
        FREE(job);
    }
    if (producer)
        FREE(producer);

    return (ret);
}

/*
 * to be called by online import using PR_CreateThread()
 * offline import directly calls import_main_offline()
 *
 */
void
dbmdb_import_main(void *arg)
{
    /* For online import tasks increment/decrement the global thread count */
    g_incr_active_threadcnt();
    import_main_offline(arg);
    g_decr_active_threadcnt();
}

int
dbmdb_back_ldif2db(Slapi_PBlock *pb)
{
    backend *be = NULL;
    int noattrindexes = 0;
    ImportJob *job = NULL;
    char **name_array = NULL;
    int total_files, i;
    int up_flags = 0;
    PRThread *thread = NULL;
    int ret = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_back_ldif2db", "Backend is not set\n");
        return -1;
    }
    job = CALLOC(ImportJob);
    job->inst = (ldbm_instance *)be->be_instance_info;
    slapi_pblock_get(pb, SLAPI_LDIF2DB_NOATTRINDEXES, &noattrindexes);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_FILE, &name_array);
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &up_flags); /* For upgrade dn and
                                                        dn2rdn */

    /* the removedupvals field is blatantly overloaded here to mean
     * the chunk size too.  (chunk size = number of entries that should
     * be imported before starting a new pass.  usually for debugging.)
     */
    slapi_pblock_get(pb, SLAPI_LDIF2DB_REMOVEDUPVALS, &job->merge_chunk_size);
    if (job->merge_chunk_size == 1)
        job->merge_chunk_size = 0;
    /* get list of specifically included and/or excluded subtrees from
     * the front-end */
    dbmdb_back_fetch_incl_excl(pb, &job->include_subtrees,
                              &job->exclude_subtrees);
    /* get cn=tasks info, if any */
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &job->task);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_ENCRYPT, &job->encrypt);
    /* get uniqueid info */
    slapi_pblock_get(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &job->uuid_gen_type);
    if (job->uuid_gen_type == SLAPI_UNIQUEID_GENERATE_NAME_BASED) {
        char *namespaceid;

        slapi_pblock_get(pb, SLAPI_LDIF2DB_NAMESPACEID, &namespaceid);
        job->uuid_namespace = slapi_ch_strdup(namespaceid);
    }

    job->flags = FLAG_USE_FILES;
    if (NULL == name_array) { /* no ldif file is given -> reindexing or
                                                             upgradedn */
        if (up_flags & (SLAPI_UPGRADEDNFORMAT | SLAPI_UPGRADEDNFORMAT_V1)) {
            if (up_flags & SLAPI_UPGRADEDNFORMAT) {
                job->flags |= FLAG_UPGRADEDNFORMAT;
            }
            if (up_flags & SLAPI_UPGRADEDNFORMAT_V1) {
                job->flags |= FLAG_UPGRADEDNFORMAT_V1;
            }
            if (up_flags & SLAPI_DRYRUN) {
                job->flags |= FLAG_DRYRUN;
            }
        } else {
            job->flags |= FLAG_REINDEXING; /* call dbmdb_index_producer */
            if (up_flags & SLAPI_UPGRADEDB_DN2RDN) {
                if (entryrdn_get_switch()) {
                    job->flags |= FLAG_DN2RDN; /* migrate to the rdn format */
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_back_ldif2db",
                                  "DN to RDN option is specified, "
                                  "but %s is not enabled\n",
                                  CONFIG_ENTRYRDN_SWITCH);
                    dbmdb_import_free_job(job);
                    FREE(job);
                    return -1;
                }
            }
        }
    }
    if (!noattrindexes) {
        job->flags |= FLAG_INDEX_ATTRS;
    }
    for (i = 0; name_array && name_array[i] != NULL; i++) {
        charray_add(&job->input_filenames, slapi_ch_strdup(name_array[i]));
    }
    job->starting_ID = 1;
    job->first_ID = 1;
    job->mothers = CALLOC(import_subcount_stuff);

    /* how much space should we allocate to index buffering? */
    job->job_index_buffer_size = dbmdb_import_get_index_buffer_size();
    if (job->job_index_buffer_size == 0) {
        /* 10% of the allocated cache size + one meg */
        PR_Lock(job->inst->inst_li->li_config_mutex);
        job->job_index_buffer_size =
            (job->inst->inst_li->li_import_cachesize / 10) + (1024 * 1024);
        PR_Unlock(job->inst->inst_li->li_config_mutex);
    }
    import_subcount_stuff_init(job->mothers);

    if (job->task != NULL) {
        /* count files, use that to track "progress" in cn=tasks */
        total_files = 0;
        while (name_array && name_array[total_files] != NULL)
            total_files++;
        /* add 1 to account for post-import cleanup (which can take a
         * significant amount of time)
         */
        /* NGK - This should eventually be cleaned up to use the public
         * task API. */
        if (0 == total_files) { /* reindexing */
            job->task->task_work = 2;
        } else {
            job->task->task_work = total_files + 1;
        }
        job->task->task_progress = 0;
        job->task->task_state = SLAPI_TASK_RUNNING;
        slapi_task_set_data(job->task, job);
        slapi_task_set_destructor_fn(job->task, dbmdb_import_task_destroy);
        slapi_task_set_cancel_fn(job->task, dbmdb_import_task_abort);
        job->flags |= FLAG_ONLINE;

        /* create thread for dbmdb_import_main, so we can return */
        thread = PR_CreateThread(PR_USER_THREAD, dbmdb_import_main, (void *)job,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD,
                                 SLAPD_DEFAULT_THREAD_STACKSIZE);
        if (thread == NULL) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_back_ldif2db",
                          "Unable to spawn import thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
            dbmdb_import_free_job(job);
            FREE(job);
            return -2;
        }
        return 0;
    }

    /* old style -- do it all synchronously (THIS IS GOING AWAY SOON) */
    ret = import_main_offline((void *)job);

    /* no error just warning, reset ret */
    if(ret &= WARN_SKIPPED_IMPORT_ENTRY) {
        slapi_pblock_set_task_warning(pb, WARN_SKIPPED_IMPORT_ENTRY);
        ret = 0;
    }

    return ret;
}
