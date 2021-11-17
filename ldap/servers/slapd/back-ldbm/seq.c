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

/* seq.c - ldbm backend sequential access function */

#include "back-ldbm.h"

#define SEQ_LITTLE_BUFFER_SIZE 100

/*
 * Access the database sequentially.
 * There are 4 ways to call this routine.  In each case, the equality index
 * for "attrname" is consulted:
 *  1) If the SLAPI_SEQ_TYPE parameter is SLAPI_SEQ_FIRST, then this routine
 *     will find the smallest key greater than or equal to the SLAPI_SEQ_VAL
 *     parameter, and return all entries that key's IDList.  If SLAPI_SEQ_VAL
 *     is NULL, then the smallest key is retrieved and the associaated
 *     entries are returned.
 *  2) If the SLAPI_SEQ_TYPE parameter is SLAPI_SEQ_NEXT, then this routine
 *     will find the smallest key strictly greater than the SLAPI_SEQ_VAL
 *     parameter, and return all entries that key's IDList.
 *  3) If the SLAPI_SEQ_TYPE parameter is SLAPI_SEQ_PREV, then this routine
 *     will find the greatest key strictly less than the SLAPI_SEQ_VAL
 *     parameter, and return all entries that key's IDList.
 *  4) If the SLAPI_SEQ_TYPE parameter is SLAPI_SEQ_LAST, then this routine
 *     will find the largest equality key in the index and return all entries
 *     which match that key.  The SLAPI_SEQ_VAL parameter is ignored.
 */
int
ldbm_back_seq(Slapi_PBlock *pb)
{
    backend *be;
    ldbm_instance *inst;
    struct ldbminfo *li;
    IDList *idl = NULL;
    back_txn txn = {NULL};
    back_txnid parent_txn;
    struct attrinfo *ai = NULL;
    dbi_db_t *db;
    dbi_cursor_t dbc = {0};
    char *attrname, *val;
    int err = LDAP_SUCCESS;
    int return_value = -1;
    int nentries = 0;
    int retry_count = 0;
    int isroot;
    int type;

    /* Decode arguments */
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &type);
    slapi_pblock_get(pb, SLAPI_SEQ_ATTRNAME, &attrname);
    slapi_pblock_get(pb, SLAPI_SEQ_VAL, &val);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&parent_txn);

    inst = (ldbm_instance *)be->be_instance_info;

    dblayer_txn_init(li, &txn);
    if (!parent_txn) {
        parent_txn = txn.back_txn_txn;
        slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    }

    /* Validate arguments */
    if (type != SLAPI_SEQ_FIRST &&
        type != SLAPI_SEQ_LAST &&
        type != SLAPI_SEQ_NEXT &&
        type != SLAPI_SEQ_PREV) {
        slapi_send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                               "Bad seq access type", 0, NULL);
        return (-1);
    }

    /* get a database */

    ainfo_get(be, attrname, &ai);
    slapi_log_err(SLAPI_LOG_ARGS,
                  "ldbm_back_seq", "indextype: %s indexmask: 0x%x seek type: %d\n",
                  ai->ai_type, ai->ai_indexmask, type);
    if (!(INDEX_EQUALITY & ai->ai_indexmask)) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "ldbm_back_seq", "caller specified un-indexed attribute %s\n",
                      attrname ? attrname : "");
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                               "Unindexed seq access type", 0, NULL);
        return -1;
    }

    if ((return_value = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_back_seq", "Could not open index file for attribute %s\n",
                      attrname);
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return -1;
    }
retry:
    if (txn.back_txn_txn) {
        dblayer_read_txn_begin(be, parent_txn, &txn);
    }
    /* First, get a database cursor */
    return_value = dblayer_new_cursor(be, db, txn.back_txn_txn, &dbc);

    if (0 == return_value) {
        dbi_val_t data = {0};
        dbi_val_t key = {0};
        char *key_val = NULL;

        /* Set data */
        dblayer_value_init(be, &data);

        /* Set up key */
        if (NULL == val) {
            /* this means, goto the first equality key */
            /* seek to key >= "=" */
            val = "";
        }
        key_val = slapi_ch_smprintf("%c%s", EQ_PREFIX, val);
        dblayer_value_set(be, &key, key_val, strlen(key_val));

        /* decide which type of operation we're being asked to do and do the db bit */
        /* The c_get call always mallocs memory for data.data */
        /* The c_get call mallocs memory for key.data, except for DB_SET */
        /* after this, we leave data containing the retrieved IDL, or NULL if we didn't get it */

        switch (type) {
        case SLAPI_SEQ_FIRST:
            /* if (NULL == val) goto the first equality key ( seek to key >= "=" ) */
            /* else goto the first equality key >= val ( seek to key >= "=val"  )*/
            return_value = dblayer_cursor_op(&dbc, DBI_OP_MOVE_NEAR_KEY, &key, &data);
            break;
        case SLAPI_SEQ_NEXT:
            /* seek to the indicated =value, then seek to the next entry, */
            return_value = dblayer_cursor_op(&dbc, DBI_OP_MOVE_TO_KEY, &key, &data);
            if (0 == return_value) {
                return_value = dblayer_cursor_op(&dbc, DBI_OP_NEXT, &key, &data);
            }
            break;
        case SLAPI_SEQ_PREV:
            /* seek to the indicated =value, then seek to the previous entry, */
            return_value = dblayer_cursor_op(&dbc, DBI_OP_MOVE_TO_KEY, &key, &data);
            if (0 == return_value) {
                return_value = dblayer_cursor_op(&dbc, DBI_OP_PREV, &key, &data);
            }
            break;
        case SLAPI_SEQ_LAST:
            /* seek to the first possible key after all the equality keys (">"), then seek back one */
            {
                sprintf(key.data, "%c", EQ_PREFIX + 1);
                key.size = 1;
                return_value = dblayer_cursor_op(&dbc, DBI_OP_MOVE_NEAR_KEY, &key, &data);
                if ((0 == return_value) || (DBI_RC_NOTFOUND == return_value)) {
                    return_value = dblayer_cursor_op(&dbc, DBI_OP_PREV, &key, &data);
                }
            }
            break;
        }

        dblayer_cursor_op(&dbc, DBI_OP_CLOSE, NULL, NULL);

        if ((0 == return_value) || (DBI_RC_NOTFOUND == return_value)) {
            /* Now check that the key we eventually settled on was an equality key ! */
            if (key.data && *((char *)key.data) == EQ_PREFIX) {
                /* Retrieve the idlist for this key */
                key.flags = 0;
                for (retry_count = 0; retry_count < IDL_FETCH_RETRY_COUNT; retry_count++) {
                    err = NEW_IDL_DEFAULT;
                    idl_free(&idl);
                    idl = idl_fetch(be, db, &key, txn.back_txn_txn, ai, &err);
                    if (err == DBI_RC_RETRY) {
                        ldbm_nasty("ldbm_back_seq", "deadlock retry", 1600, err);
                        if (txn.back_txn_txn) {
                            dblayer_read_txn_abort(be, &txn);
                            goto retry;
                        } else {
                            continue;
                        }
                    } else {
                        break;
                    }
                }
            }
        } else {
            if (txn.back_txn_txn) {
                dblayer_read_txn_abort(be, &txn);
            }
            if (DBI_RC_RETRY == return_value) {
                ldbm_nasty("ldbm_back_seq", "deadlock retry", 1601, err);
                goto retry;
            }
        }
        if (retry_count == IDL_FETCH_RETRY_COUNT) {
            ldbm_nasty("ldbm_back_seq", "Retry count exceeded", 1645, err);
        } else if (err != 0 && err != DBI_RC_NOTFOUND) {
            ldbm_nasty("ldbm_back_seq", "Database error", 1650, err);
        }
        dblayer_value_free(be, &key);
        dblayer_value_free(be, &data);
    }

    /* null idlist means there were no matching keys */
    if (idl != NULL) {
        /*
         * Step through the IDlist.  For each ID, get the entry
         * and send it.
         */
        ID id;
        struct backentry *e;
        for (id = idl_firstid(idl); id != NOID;
             id = idl_nextid(idl, id)) {
            if ((e = id2entry(be, id, &txn, &err)) == NULL) {
                if (err != LDAP_SUCCESS) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_seq", "id2entry err %d\n", err);
                }
                slapi_log_err(SLAPI_LOG_ARGS,
                              "ldbm_back_seq", "candidate %lu not found\n",
                              (u_long)id);
                continue;
            }
            if (slapi_send_ldap_search_entry(pb, e->ep_entry, NULL, NULL, 0) == 0) {
                nentries++;
            }
            CACHE_RETURN(&inst->inst_cache, &e);
        }
        idl_free(&idl);
    }
    /* if success finally commit the transaction, otherwise abort if DBI_RC_NOTFOUND */
    if (txn.back_txn_txn) {
        if (return_value == 0) {
            dblayer_read_txn_commit(be, &txn);
        } else if (DBI_RC_NOTFOUND == return_value) {
            dblayer_read_txn_abort(be, &txn);
        }
    }

    dblayer_release_index_file(be, ai, db);

    slapi_send_ldap_result(pb, LDAP_SUCCESS == err ? LDAP_SUCCESS : LDAP_OPERATIONS_ERROR, NULL, NULL, nentries, NULL);

    return 0;
}
