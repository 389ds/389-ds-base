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


/* New IDL code for new indexing scheme */

/* Note to future editors:
   This file is now full of redundant code
   (the DB_ALLIDS_ON_WRITE==true).
   It should be stripped out at the beginning of a
   major release cycle.
 */

#include "back-ldbm.h"

static char *filename = "idl_new.c";

#define DB_USE_BULK_FETCH 1
#define BULK_FETCH_BUFFER_SIZE (8 * 1024)

/* We used to implement allids for inserts, but that's a bad idea.
   Why ? Because:
   1) Allids results in hard to understand query behavior.
   2) The get() calls needed to check for allids on insert cost performance.
   3) Tests show that there is no significant performance benefit to having allids on writes,
   either for updates or searches.
   Set this to revert to that code */
/* #undef DB_ALLIDS_ON_WRITE */
/* We still enforce allids threshold on reads, to save time and space fetching vast id lists */
#define DB_ALLIDS_ON_READ 1

/* Structure used to hide private idl-specific data in the attrinfo object */
struct idl_private
{
    size_t idl_allidslimit;
    int dummy;
};

static int idl_tune = DEFAULT_IDL_TUNE; /* tuning parameters for IDL code */
/* Currently none for new IDL code */

#if defined(DB_ALLIDS_ON_WRITE)
static int idl_new_store_allids(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn);
#endif

void
idl_new_set_tune(int val)
{
    idl_tune = val;
}

int
idl_new_get_tune(void)
{
    return idl_tune;
}

size_t
idl_new_get_allidslimit(struct attrinfo *a, int allidslimit)
{
    idl_private *priv = NULL;

    if (allidslimit) {
        return (size_t)allidslimit;
    }

    PR_ASSERT(NULL != a);
    PR_ASSERT(NULL != a->ai_idl);

    priv = a->ai_idl;

    return priv->idl_allidslimit;
}

int
idl_new_exceeds_allidslimit(uint64_t count, struct attrinfo *a, int allidslimit)
{
    uint64_t limit = idl_new_get_allidslimit(a, allidslimit);
    return (limit != (uint64_t)-1) && (count > limit);
}


/* routine to initialize the private data used by the IDL code per-attribute */
int
idl_new_init_private(backend *be, struct attrinfo *a)
{
    idl_private *priv = NULL;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;

    PR_ASSERT(NULL != a);
    PR_ASSERT(NULL == a->ai_idl);

    priv = (idl_private *)slapi_ch_calloc(sizeof(idl_private), 1);
    if (NULL == priv) {
        return -1; /* Memory allocation failure */
    }
    priv->idl_allidslimit = (size_t)li->li_allidsthreshold;
    /* Initialize the structure */
    a->ai_idl = (void *)priv;
    return 0;
}

/* routine to release resources used by IDL private data structure */
int
idl_new_release_private(struct attrinfo *a)
{
    PR_ASSERT(NULL != a);
    if (NULL != a->ai_idl) {
        slapi_ch_free((void **)&(a->ai_idl));
    }
    return 0;
}

IDList *
idl_new_fetch(
    backend *be,
    dbi_db_t *db,
    dbi_val_t *inkey,
    dbi_txn_t *txn,
    struct attrinfo *a,
    int *flag_err,
    int allidslimit)
{
    int ret = 0;
    int ret2 = 0;
    int idl_rc = 0;
    dbi_cursor_t cursor = {0};
    IDList *idl = NULL;
    dbi_val_t key = {0};
    dbi_bulk_t bulkdata = {0};
    ID id = 0;
    uint64_t count = 0;
    /* beware that a large buffer on the stack might cause a stack overflow on some platforms */
    char buffer[BULK_FETCH_BUFFER_SIZE];
    dbi_val_t dataret = {0};
    back_txn s_txn;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    char *index_id = "unknown";

    if (a && a->ai_type) {
        index_id = a->ai_type;
    } else if (dblayer_get_db_id(be, db)) {
        index_id = dblayer_get_db_id(be, db);
    }

    if (NEW_IDL_NOOP == *flag_err) {
        *flag_err = 0;
        return NULL;
    }

    dblayer_bulk_set_buffer(be, &bulkdata, buffer, sizeof(buffer), DBI_VF_BULK_DATA);
    memset(&dataret, 0, sizeof(dataret));

    dblayer_txn_init(li, &s_txn);
    if (txn) {
        dblayer_read_txn_begin(be, txn, &s_txn);
    }

    /* Make a cursor */
    ret = dblayer_new_cursor(be, db, txn, &cursor);
    if (0 != ret) {
        ldbm_nasty("idl_new_fetch", filename, 1, ret);
        goto error;
    }

    /*
     * We're not expecting the key to change in value
     * so we can just use the input key as a buffer.
     * This avoids memory management of the key.
     */
    dblayer_value_set_buffer(be, &key, inkey->data, inkey->size);

    /* Position cursor at the first matching key */
    ret = dblayer_cursor_bulkop(&cursor, DBI_OP_MOVE_TO_KEY, &key, &bulkdata);
    if (0 != ret) {
        if (DBI_RC_NOTFOUND != ret) {
            if (ret == DBI_RC_BUFFER_SMALL) {
                slapi_log_err(SLAPI_LOG_ERR, "idl_new_fetch",
                        "Database index is corrupt (attribute: %s); "
                        "data item for key %s is too large for our buffer "
                        "(need=%ld actual=%ld)\n",
                        index_id, (char *)key.data, bulkdata.v.size, bulkdata.v.ulen);
            }
            ldbm_nasty("idl_new_fetch", filename, 2, ret);
        }
        goto error; /* Not found is OK, return NULL IDL */
    }

    /* Allocate an idlist to populate into */
    idl = idl_alloc(IDLIST_MIN_BLOCK_SIZE);

    /* Iterate over the duplicates, amassing them into an IDL */
    for (;;) {
        ID lastid = 0;
        for (dblayer_bulk_start(&bulkdata); DBI_RC_SUCCESS == dblayer_bulk_nextdata(&bulkdata, &dataret);) {
            if (dataret.size != sizeof(ID)) {
                slapi_log_err(SLAPI_LOG_ERR, "idl_new_fetch",
                        "Database index is corrupt; "
                        "(attribute: %s) key %s has a data item with the wrong size (%ld)\n",
                        index_id, (char *)key.data, dataret.size);
                goto error;
            }
            memcpy(&id, dataret.data, sizeof(ID));
            if (id == lastid) { /* dup */
                slapi_log_err(SLAPI_LOG_TRACE, "idl_new_fetch",
                        "Detected duplicate id %d due to DB_MULTIPLE error - skipping (attribute: %s)\n",
                              id, index_id);
                continue; /* get next one */
            }
            /* note the last id read to check for dups */
            lastid = id;
            /* we got another ID, add it to our IDL */
            idl_rc = idl_append_extend(&idl, id);
            if (idl_rc) {
                slapi_log_err(SLAPI_LOG_ERR, "idl_new_fetch",
                        "Unable to extend id list for attribute (%s) (err=%d)\n",
                        index_id, idl_rc);
                idl_free(&idl);
                goto error;
            }

            count++;
        }

        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_fetch",
                "bulk fetch buffer nids=%" PRIu64 " attribute: %s\n",
                count, index_id);
#if defined(DB_ALLIDS_ON_READ)
        /* enforce the allids read limit */
        if ((NEW_IDL_NO_ALLID != *flag_err) && (NULL != a) &&
            (idl != NULL) && idl_new_exceeds_allidslimit(count, a, allidslimit)) {
            idl->b_nids = 1;
            idl->b_ids[0] = ALLID;
            ret = DBI_RC_NOTFOUND; /* fool the code below into thinking that we finished the dups */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "idl_new_fetch",
                    "Search for key for attribute index %s exceeded allidslimit %d - count is %" PRIu64 "\n",
                    index_id, allidslimit, count);
            break;
        }
#endif
        ret = dblayer_cursor_bulkop(&cursor, DBI_OP_NEXT_DATA, &key, &bulkdata);
        if (0 != ret) {
            break;
        }
    }

    if (ret != DBI_RC_NOTFOUND) {
        idl_free(&idl);
        ldbm_nasty("idl_new_fetch", filename, 59, ret);
        goto error;
    }

    ret = 0;

    /* check for allids value */
    if (idl != NULL && idl->b_nids == 1 && idl->b_ids[0] == ALLID) {
        idl_free(&idl);
        idl = idl_allids(be);
        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_fetch", "%s returns allids (attribute: %s)\n",
                      (char *)key.data, index_id);
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_fetch", "%s returns nids=%lu (attribute: %s)\n",
                      (char *)key.data, (u_long)IDL_NIDS(idl), index_id);
    }

error:
    /* Close the cursor */
    ret2 = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (ret2) {
        ldbm_nasty("idl_new_fetch", filename, 3, ret2);
        if (!ret) {
            /* if cursor close returns DEADLOCK, we must bubble that up
               to the higher layers for retries */
            ret = ret2;
        }
    }
    if (ret) {
        dblayer_read_txn_abort(be, &s_txn);
    } else {
        dblayer_read_txn_commit(be, &s_txn);
    }
    dblayer_bulk_free(&bulkdata);
    *flag_err = ret;
    return idl;
}



/* This function compares two index keys.  It is assumed
   that the values are already normalized, since they should have
   been when the index was created (by int_values2keys).

   richm - actually, the current syntax compare functions
   always normalize both arguments.  We need to add an additional
   syntax compare function that does not normalize or takes
   an argument like value_cmp to specify to normalize or not.

   More fun - this function is used to compare both raw database
   keys (e.g. with the prefix '=' or '+' or '*' etc.) and without
   (in the case of two equality keys, we want to strip off the
   leading '=' to compare the actual values).  We only use the
   value_compare function if both keys are equality keys with
   some data after the equality prefix.  In every other case,
   we will just use a standard berval cmp function.

   see also dblayer_bt_compare
*/
int
keycmp(dbi_val_t *L, dbi_val_t *R, value_compare_fn_type cmp_fn)
{
    struct berval Lv;
    struct berval Rv;

    if ((L->data && (L->size > 1) && (*((char *)L->data) == EQ_PREFIX)) &&
        (R->data && (R->size > 1) && (*((char *)R->data) == EQ_PREFIX))) {
        Lv.bv_val = (char *)L->data + 1;
        Lv.bv_len = (ber_len_t)L->size - 1;
        Rv.bv_val = (char *)R->data + 1;
        Rv.bv_len = (ber_len_t)R->size - 1;
        /* use specific compare fn, if any */
        cmp_fn = (cmp_fn ? cmp_fn : slapi_berval_cmp);
    } else {
        Lv.bv_val = (char *)L->data;
        Lv.bv_len = (ber_len_t)L->size;
        Rv.bv_val = (char *)R->data;
        Rv.bv_len = (ber_len_t)R->size;
        /* just compare raw bervals */
        cmp_fn = slapi_berval_cmp;
    }
    return cmp_fn(&Lv, &Rv);
}





typedef struct _range_id_pair
{
    ID key;
    ID id;
} idl_range_id_pair;
/*
 * Perform the range search in the idl layer instead of the index layer
 * to improve the performance.
 */
/*
 * NOTE:
 * In the total update (bulk import), an entry requires its ancestors already added.
 * To guarantee it, the range search with parentid is used with setting the flag
 * SLAPI_OP_RANGE_NO_IDL_SORT in operator.
 * In bulk import the range search is parentid>=1 to retrieve all the entries
 * But we need to order the IDL with the parents first => retrieve the suffix entry ID
 * to store the children
 *
 * If the flag is set,
 * 1. the IDList is not sorted by the ID.
 * 2. holding to add an ID to the IDList unless the key is found in the IDList.
 */
IDList *
idl_new_range_fetch(
    backend *be,
    dbi_db_t *db,
    dbi_val_t *lowerkey,
    dbi_val_t *upperkey,
    dbi_txn_t *txn,
    struct attrinfo *ai,
    int *flag_err,
    int allidslimit,
    int sizelimit,
    struct timespec *expire_time,
    int lookthrough_limit,
    int
    operator)
{
    int ret = 0;
    int ret2 = 0;
    int idl_rc = 0;
    dbi_cursor_t cursor = {0};
    IDList *idl = NULL;
    dbi_val_t cur_key = {0};
    dbi_bulk_t bulkdata = {0};
    ID id = 0;
    uint64_t count = 0;
    /* beware that a large buffer on the stack might cause a stack overflow on some platforms */
    char buffer[BULK_FETCH_BUFFER_SIZE];
    dbi_val_t dataret = {0};
    back_txn s_txn;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    int coreop = operator&SLAPI_OP_RANGE;
    ID key = 0xff; /* random- to suppress compiler warning */
    ID suffix = 0; /* random- to suppress compiler warning */
    idl_range_id_pair *leftover = NULL;
    size_t leftoverlen = 32;
    size_t leftovercnt = 0;

    if (NULL == flag_err) {
        return NULL;
    }
    if (operator & SLAPI_OP_RANGE_NO_IDL_SORT) {
            struct _back_info_index_key bck_info;
            int rc;
            /* We are doing a bulk import
             * try to retrieve the suffix entry id from the index
             */

            bck_info.index = SLAPI_ATTR_PARENTID;
            bck_info.key = "0";

            if ((rc = slapi_back_get_info(be, BACK_INFO_INDEX_KEY, (void **)&bck_info))) {
                slapi_log_err(SLAPI_LOG_WARNING, "idl_new_range_fetch", "Total update: fail to retrieve suffix entryID, continue assuming it is the first entry\n");
            }
            if (bck_info.key_found) {
                suffix = bck_info.id;
            }
    }

    if (NEW_IDL_NOOP == *flag_err) {
        return NULL;
    }

    dblayer_txn_init(li, &s_txn);
    if (txn) {
        dblayer_read_txn_begin(be, txn, &s_txn);
    }

    /* Make a cursor */
    ret = dblayer_new_cursor(be, db, txn, &cursor);
    if (0 != ret) {
        ldbm_nasty("idl_new_range_fetch", filename, 1, ret);
        goto error;
    }
    memset(&dataret, 0, sizeof(dataret));

    dblayer_bulk_set_buffer(be, &bulkdata, buffer, sizeof(buffer), DBI_VF_BULK_DATA);
    /*
     * We're not expecting the key to change in value
     * so we can just use the input key as a buffer.
     * This avoids memory management of the key.
     */
    dblayer_value_set_buffer(be, &cur_key, lowerkey->data, lowerkey->size);

    /* Position cursor at the first matching key */
    ret = dblayer_cursor_bulkop(&cursor, DBI_OP_MOVE_TO_KEY, &cur_key, &bulkdata);
    if (0 != ret) {
        if (DBI_RC_NOTFOUND != ret) {
            if (ret == DBI_RC_BUFFER_SMALL) {
                slapi_log_err(SLAPI_LOG_ERR, "idl_new_range_fetch", "Database index is corrupt; "
                                                                    "data item for key %s is too large for our buffer (need=%ld actual=%ld)\n",
                              (char *)cur_key.data, bulkdata.v.size, bulkdata.v.ulen);
            }
            ldbm_nasty("idl_new_range_fetch", filename, 2, ret);
        }
        goto error; /* Not found is OK, return NULL IDL */
    }

    /* Allocate an idlist to populate into */
    idl = idl_alloc(IDLIST_MIN_BLOCK_SIZE);

    /* Iterate over the duplicates, amassing them into an IDL */
    while (cur_key.data &&
           (upperkey && upperkey->data ? ((coreop == SLAPI_OP_LESS) ? keycmp(&cur_key, upperkey, ai->ai_key_cmp_fn) < 0 : keycmp(&cur_key, upperkey, ai->ai_key_cmp_fn) <= 0) : PR_TRUE /* e.g., (x > a) */)) {
        ID lastid = 0;

        dblayer_bulk_start(&bulkdata);

        /* lookthrough limit & sizelimit check */
        if (idl) {
            if ((lookthrough_limit != -1) &&
                (idl->b_nids > (ID)lookthrough_limit)) {
                idl_free(&idl);
                idl = idl_allids(be);
                slapi_log_err(SLAPI_LOG_TRACE,
                              "idl_new_range_fetch", "lookthrough_limit exceeded\n");
                *flag_err = LDAP_ADMINLIMIT_EXCEEDED;
                goto error;
            }
            if ((sizelimit > 0) && (idl->b_nids > (ID)sizelimit)) {
                slapi_log_err(SLAPI_LOG_TRACE,
                              "idl_new_range_fetch", "sizelimit exceeded\n");
                *flag_err = LDAP_SIZELIMIT_EXCEEDED;
                goto error;
            }
        }
        /* timelimit check */
        /*
         * A future improvement could be to check this only every X iterations
         * to prevent overwhelming the clock?
         */
        if (slapi_timespec_expire_check(expire_time) == TIMER_EXPIRED) {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "idl_new_range_fetch", "timelimit exceeded\n");
            *flag_err = LDAP_TIMELIMIT_EXCEEDED;
            goto error;
        }
        if (operator & SLAPI_OP_RANGE_NO_IDL_SORT) {
            key = (ID)strtol((char *)cur_key.data + 1, (char **)NULL, 10);
        }
        while (DBI_RC_SUCCESS == dblayer_bulk_nextdata(&bulkdata, &dataret)) {
            if (dataret.size != sizeof(ID)) {
                slapi_log_err(SLAPI_LOG_ERR, "idl_new_range_fetch", "Database index is corrupt; "
                                                                    "key %s has a data item with the wrong size (%ld)\n",
                              (char *)cur_key.data, dataret.size);
                goto error;
            }
            memcpy(&id, dataret.data, sizeof(ID));
            if (id == lastid) { /* dup */
                slapi_log_err(SLAPI_LOG_TRACE, "idl_new_range_fetch",
                              "Detected duplicate id %d due to DB_MULTIPLE error - skipping\n", id);
                continue; /* get next one */
            }
            /* note the last id read to check for dups */
            lastid = id;
            /* we got another ID, add it to our IDL */
            if (operator & SLAPI_OP_RANGE_NO_IDL_SORT) {
                if ((count == 0) && (suffix == 0)) {
                    /* First time.  Keep the suffix ID. 
                     * note that 'suffix==0' mean we did not retrieve the suffix entry id
                     * from the parentid index (key '=0'), so let assume the first
                     * found entry is the one from the suffix
                     */
                    suffix = key;
                    idl_rc = idl_append_extend(&idl, id);
                } else if ((key == suffix) || idl_id_is_in_idlist(idl, key)) {
                    /* the parent is the suffix or already in idl. */
                    idl_rc = idl_append_extend(&idl, id);
                } else {
                    /* Otherwise, keep the {key,id} in leftover array */
                    if (!leftover) {
                        leftover = (idl_range_id_pair *)slapi_ch_calloc(leftoverlen, sizeof(idl_range_id_pair));
                    } else if (leftovercnt == leftoverlen) {
                        leftover = (idl_range_id_pair *)slapi_ch_realloc((char *)leftover, 2 * leftoverlen * sizeof(idl_range_id_pair));
                        memset(leftover + leftovercnt, 0, leftoverlen);
                        leftoverlen *= 2;
                    }
                    leftover[leftovercnt].key = key;
                    leftover[leftovercnt].id = id;
                    leftovercnt++;
                }
            } else {
                idl_rc = idl_append_extend(&idl, id);
            }
            if (idl_rc) {
                slapi_log_err(SLAPI_LOG_ERR, "idl_new_range_fetch",
                              "Unable to extend id list (err=%d)\n", idl_rc);
                idl_free(&idl);
                goto error;
            }

            count++;
        }

        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_range_fetch",
                      "Bulk fetch buffer nids=%" PRIu64 "\n", count);
#if defined(DB_ALLIDS_ON_READ)
        /* enforce the allids read limit */
        if ((NEW_IDL_NO_ALLID != *flag_err) && ai && (idl != NULL) &&
            idl_new_exceeds_allidslimit(count, ai, allidslimit)) {
            idl->b_nids = 1;
            idl->b_ids[0] = ALLID;
            ret = DBI_RC_NOTFOUND; /* fool the code below into thinking that we finished the dups */
            break;
        }
#endif
        ret = dblayer_cursor_bulkop(&cursor, DBI_OP_NEXT_KEY, &cur_key, &bulkdata);
        if (ret) {
            break;
        }
        if (upperkey && upperkey->data && KEY_EQ(&cur_key, upperkey)) {
            /* this is the last key */
            break;
        }
    }

    if (ret) {
        if (ret == DBI_RC_NOTFOUND) {
            ret = 0; /* normal case */
        } else {
            idl_free(&idl);
            ldbm_nasty("idl_new_range_fetch", filename, 59, ret);
            goto error;
        }
    }

    /* check for allids value */
    if (idl && (idl->b_nids == 1) && (idl->b_ids[0] == ALLID)) {
        idl_free(&idl);
        idl = idl_allids(be);
        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_range_fetch", "%s returns allids\n",
                      (char *)cur_key.data);
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_range_fetch", "%s returns nids=%lu\n",
                      (char *)cur_key.data, (u_long)IDL_NIDS(idl));
    }

error:
    dblayer_value_free(be, &cur_key);
    dblayer_bulk_free(&bulkdata);
    /* Close the cursor */
    ret2 = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (ret2) {
        ldbm_nasty("idl_new_range_fetch", filename, 3, ret2);
        if (!ret) {
            /* if cursor close returns DEADLOCK, we must bubble that up
               to the higher layers for retries */
            ret = ret2;
        }
    }
    if (ret) {
        dblayer_read_txn_abort(be, &s_txn);
    } else {
        dblayer_read_txn_commit(be, &s_txn);
    }
    *flag_err = ret;

    /* sort idl */
    if (idl && !ALLIDS(idl) && !(operator&SLAPI_OP_RANGE_NO_IDL_SORT)) {
        qsort((void *)&idl->b_ids[0], idl->b_nids, (size_t)sizeof(ID), idl_sort_cmp);
    }
    if (operator&SLAPI_OP_RANGE_NO_IDL_SORT) {
        size_t remaining = leftovercnt;

        while(remaining > 0) {
            for (size_t i = 0; i < leftovercnt; i++) {
                if (leftover[i].key > 0 && idl_id_is_in_idlist(idl, leftover[i].key) != 0) {
                    /* if the leftover key has its parent in the idl */
                    idl_rc = idl_append_extend(&idl, leftover[i].id);
                    if (idl_rc) {
                        slapi_log_err(SLAPI_LOG_ERR, "idl_new_range_fetch",
                                      "Unable to extend id list (err=%d)\n", idl_rc);
                        idl_free(&idl);
                        return NULL;
                    }
                    leftover[i].key = 0;
                    remaining--;
                }
            }
        }
        slapi_ch_free((void **)&leftover);
    }
    return idl;
}

int
idl_new_insert_key(
    backend *be __attribute__((unused)),
    dbi_db_t *db,
    dbi_val_t *key,
    ID id,
    dbi_txn_t *txn,
    struct attrinfo *a __attribute__((unused)),
    int *disposition)
{
    int ret = 0;
    dbi_val_t data = {0};

#if defined(DB_ALLIDS_ON_WRITE)
    dbi_cursor_t cursor = {0};
    db_recno_t count;
    ID tmpid = 0;
    /* Make a cursor */
    ret = dblayer_new_cursor(be, db, txn, &cursor);
    if (0 != ret) {
        ldbm_nasty("idl_new_insert_key", filename, 58, ret);
        cursor = NULL;
        goto error;
    }
    dblayer_value_set_buffer(be, &data, &tmpid, sizeof tmpid);
    ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, key, &data);
    if (0 == ret) {
        if (tmpid == ALLID) {
            if (NULL != disposition) {
                *disposition = IDL_INSERT_ALLIDS;
            }
            goto error; /* allid: don't bother inserting any more */
        }
    } else if (DBI_RC_NOTFOUND != ret) {
        ldbm_nasty("idl_new_insert_key", filename, 12, ret);
        goto error;
    }
    if (NULL != disposition) {
        *disposition = IDL_INSERT_NORMAL;
    }

    dblayer_value_set_buffer(be, &data, &id, sizeof id);

    /* insert it */
    ret = dblayer_cursor_op(&cursor, DBI_OP_ADD, key, &data);
    if (0 != ret) {
        if (DBI_RC_KEYEXIST == ret) {
            /* this is okay */
            ret = 0;
        } else {
            ldbm_nasty("idl_new_insert_key", filename, 50, ret);
        }
    } else {
        /* check for allidslimit exceeded in database */
        if (dblayer_cursor_get_count(cursor, &count) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "idl_new_insert_key", "Could not obtain count for key %s\n",
                          (char *)key->data);
            goto error;
        }
        if ((size_t)count > idl_new_get_allidslimit(a, 0)) {
            slapi_log_err(SLAPI_LOG_TRACE, "idl_new_insert_key", "allidslimit exceeded for key %s\n",
                          (char *)key->data);
            dblayer_cursor_op(cursor, DBI_OP_CLOSE, NULL, NULL);
            if ((ret = idl_new_store_allids(be, db, key, txn)) == 0) {
                if (NULL != disposition) {
                    *disposition = IDL_INSERT_NOW_ALLIDS;
                }
            }
        }
    error:
        /* Close the cursor */
        ret2 = dblayer_cursor_op(cursor, DBI_OP_CLOSE, NULL, NULL);
        if (ret2) {
            ldbm_nasty("idl_new_insert_key", filename, 56, ret2);
        }
        dblayer_bulk_free(be, &bulkdata);
    }
#else
    dblayer_value_set_buffer(be, &data, &id, sizeof(id));

    if (NULL != disposition) {
        *disposition = IDL_INSERT_NORMAL;
    }

    ret = dblayer_db_op(be, db, txn, DBI_OP_ADD, key, &data);
    if (0 != ret) {
        if (DBI_RC_KEYEXIST == ret) {
            /* this is okay */
            ret = 0;
        } else {
            ldbm_nasty("idl_new_insert_key", filename, 60, ret);
        }
    }
#endif

        return ret;
    }

int idl_new_delete_key(
    backend * be,
    dbi_db_t * db,
    dbi_val_t * key,
    ID id,
    dbi_txn_t * txn,
    struct attrinfo * a __attribute__((unused)))
{
    int ret = 0;
    int ret2 = 0;
    dbi_cursor_t cursor = {0};
    dbi_val_t data = {0};

    /* Make a cursor */
    ret = dblayer_new_cursor(be, db, txn, &cursor);
    if (0 != ret) {
        ldbm_nasty("idl_new_delete_key", filename, 21, ret);
        goto error;
    }
    dblayer_value_set_buffer(be, &data, &id, sizeof(id));
    /* Position cursor at the key, value pair */
    ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_DATA, key, &data);
    if (0 == ret) {
        if (id == ALLID) {
            goto error; /* allid: never delete it */
        }
    } else {
        if (DBI_RC_NOTFOUND == ret) {
            ret = 0; /* Not Found is OK, return immediately */
        } else {
            ldbm_nasty("idl_new_delete_key", filename, 22, ret);
        }
        goto error;
    }
    /* We found it, so delete it */
    ret = dblayer_cursor_op(&cursor, DBI_OP_DEL, key, &data);
error:
    dblayer_value_free(be, &data);
    /* Close the cursor */
    ret2 = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (ret2) {
        ldbm_nasty("idl_new_delete_key", filename, 24, ret2);
        if (!ret) {
            /* if cursor close returns DEADLOCK, we must bubble that up
           to the higher layers for retries */
            ret = ret2;
        }
    }
    return ret;
}

#if defined(DB_ALLIDS_ON_WRITE)
static int idl_new_store_allids(backend * be, dbi_db_t * db, dbi_val_t * key, dbi_txn_t * txn)
{
    int ret = 0;
    int ret2 = 0;
    dbi_cursor_t cursor = {0};
    dbi_val_t data = {0};
    ID id = 0;

    /* Make a cursor */
    ret = dblayer_new_cursor(be, db, txn, &cursor);
    if (0 != ret) {
        ldbm_nasty(filename, 31, ret);
        cursor = NULL;
        goto error;
    }
    dblayer_value_set_buffer(be, &data, &id, sizeof(ID));

    /* Position cursor at the key */
    ret = dblayer_cursor_op(cursor, DBI_OP_MOVE_TO_KEY, key, &data);
    if (ret == 0) {
        /* We found it, so delete all duplicates */
        ret = dblayer_cursor_op(cursor, DBI_OP_DEL, key, &data);
        while (0 == ret) {
            ret = dblayer_cursor_op(cursor, DBI_OP_NEXT_VALUE, key, &data);
            if (0 != ret) {
                break;
            }
            ret = dblayer_cursor_op(cursor, DBI_OP_DEL, key, &data);
        }
        if (0 != ret && DBI_RC_NOTFOUND != ret) {
            ldbm_nasty("idl_new_store_allids", filename, 54, ret);
            goto error;
        } else {
            ret = 0;
        }
    } else {
        if (DBI_RC_NOTFOUND == ret) {
            ret = 0; /* Not Found is OK */
        } else {
            ldbm_nasty("idl_new_store_allids", filename, 32, ret);
            goto error;
        }
    }

    /* store the ALLID value */
    id = ALLID;
    ret = dblayer_cursor_op(cursor, DBI_OP_ADD, key, &data);
    if (0 != ret) {
        ldbm_nasty("idl_new_store_allids", filename, 53, ret);
        goto error;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "idl_new_store_allids", "Key %s has been set to allids\n",
                  (char *)key->data);

error:
    /* Close the cursor */
    dblayer_value_free(be, &data);
    ret2 = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (ret2) {
        ldbm_nasty("idl_new_store_allids", filename, 33, ret2);
    }
    return ret;
#ifdef KRAZY_K0DE
    /* If this function is called in "no-allids" mode, then it's a bug */
    ldbm_nasty(filename, 63, 0);
    return -1;
#endif
}
#endif

int idl_new_store_block(
    backend * be __attribute__((unused)),
    dbi_db_t * db,
    dbi_val_t * key,
    IDList * idl,
    dbi_txn_t * txn,
    struct attrinfo * a __attribute__((unused)))
{
    int ret = 0;
    int ret2 = 0;
    dbi_cursor_t cursor = {0};
    dbi_val_t data = {0};
    ID id = 0;
    size_t x = 0;
#if defined(DB_ALLIDS_ON_WRITE)
    db_recno_t count;
#endif

    if (NULL == idl) {
        return ret;
    }

/*
 * Really we need an extra entry point to the dbi_db_t here, which
 * inserts a list of duplicate keys. In the meantime, we'll
 * just do it by brute force.
 */
#if defined(DB_ALLIDS_ON_WRITE)
    /* allids check on input idl */
    if (ALLIDS(idl) || (idl->b_nids > (ID)idl_new_get_allidslimit(a, 0))) {
        return idl_new_store_allids(be, db, key, txn);
    }
#endif

    /* Make a cursor */
    ret = dblayer_new_cursor(be, db, txn, &cursor);
    if (0 != ret) {
        ldbm_nasty("idl_new_store_block", filename, 41, ret);
        goto error;
    }

    /* initialize data dbi_val_t */
    dblayer_value_set_buffer(be, &data, &id, sizeof(id));

    /* Position cursor at the key, value pair */
    ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_DATA, key, &data);
    if (ret == DBI_RC_NOTFOUND) {
        ret = 0;
    } else if (ret != 0) {
        ldbm_nasty("idl_new_store_block", filename, 47, ret);
        goto error;
    }

    /* Iterate over the IDs in the idl */
    for (x = 0; x < idl->b_nids; x++) {
        /* insert an id */
        id = idl->b_ids[x];
        ret = dblayer_cursor_op(&cursor, DBI_OP_ADD, key, &data);
        if (0 != ret) {
            if (DBI_RC_KEYEXIST == ret) {
                ret = 0; /* exist is okay */
            } else {
                ldbm_nasty("idl_new_store_block", filename, 48, ret);
                goto error;
            }
        }
    }
#if defined(DB_ALLIDS_ON_WRITE)
    /* check for allidslimit exceeded in database */
    if (dblayer_cursor_get_count(&cursor, &count) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "idl_new_store_block",
                      "could not obtain count for key %s\n", (char *)key->data);
        goto error;
    }
    if ((size_t)count > idl_new_get_allidslimit(a, 0)) {
        slapi_log_err(SLAPI_LOG_TRACE, "idl_new_store_block",
                      "allidslimit exceeded for key %s\n", (char *)key->data);
        dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
        ret = idl_new_store_allids(be, db, key, txn);
    }
#endif

error:
    /* Close the cursor */
    ret2 = dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (ret2) {
        ldbm_nasty("idl_new_store_block", filename, 49, ret2);
        if (!ret) {
            /* if cursor close returns DEADLOCK, we must bubble that up
               to the higher layers for retries */
            ret = ret2;
        }
    }
    return ret;
}

