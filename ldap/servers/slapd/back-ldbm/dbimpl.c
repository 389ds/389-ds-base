/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * Abstraction layer which sits between database implemetation plugin
 * and higher layers in the directory server---typically
 * the back-end and replication changelog.
 * This module's purposes are 1) to hide messy stuff which
 * db2.0 needs, and with which we don't want to pollute the back-end
 * code. 2) Provide some degree of portability to other databases
 * Blame: progier
 */

/* Return code conventions:
 *  Unless otherwise advertised, all the functions in this module
 *  return a dbi_error_t (defined in dbimpl.h)
 */

/*
 *  Note: for historical reason part of the plugin interface wrappers
 *   are in dblayer.c ( All function defined during phase 2 )
 */

#include <sys/types.h>
#include <sys/statvfs.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdarg.h>
#include "back-ldbm.h"
#include "dblayer.h"
#include <prthread.h>
#include <prclist.h>


static inline dblayer_private *dblayer_get_priv(Slapi_Backend *be)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_private *priv = NULL;
    PR_ASSERT(NULL != li);
    priv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != priv);
    return priv;
}

static int dblayer_value_set_int(Slapi_Backend *be __attribute__((unused)), dbi_val_t *data,
    void *ptr, size_t size, size_t ulen, int flags)
{
    if (ptr != data->data) {
        dblayer_value_free(be, data);
    }
    data->flags = flags;
    data->data = ptr;
    data->size = size;
    data->ulen = ulen;
    return DBI_RC_SUCCESS;
}

char *dblayer_get_db_filename(Slapi_Backend *be, dbi_db_t *db)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_get_db_filename_fn(db);
}

/* Release bulk operation resources */
int dblayer_bulk_free(dbi_bulk_t *bulkdata)
{
    int rc = DBI_RC_SUCCESS;
    if (bulkdata->be) {
        dblayer_private *priv = dblayer_get_priv(bulkdata->be);
        if (priv->dblayer_bulk_free_fn) {
            rc = priv->dblayer_bulk_free_fn(bulkdata);
        }
        dblayer_value_free(bulkdata->be, &bulkdata->v);
        bulkdata->be = NULL;
    }
    return rc;
}

/* iterate on a bulk operation DBI_VF_BULK_DATA result */
int dblayer_bulk_nextdata(dbi_bulk_t *bulkdata, dbi_val_t *data)
{
    dblayer_private *priv = dblayer_get_priv(bulkdata->be);
    PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_DATA);
    return priv->dblayer_bulk_nextdata_fn(bulkdata, data);
}

/* iterate on a bulk operation DBI_VF_BULK_RECORD result */
int dblayer_bulk_nextrecord(dbi_bulk_t *bulkdata, dbi_val_t *key, dbi_val_t *data)
{
    dblayer_private *priv = dblayer_get_priv(bulkdata->be);
    PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_RECORD);
    int rc = priv->dblayer_bulk_nextrecord_fn(bulkdata, key, data);
    return rc;
}

/* initialze bulk operation */
int dblayer_bulk_set_buffer(Slapi_Backend *be, dbi_bulk_t *bulkdata, void *buff, size_t len, dbi_valflags_t flags)
{
    dblayer_private *priv = dblayer_get_priv(be);
    int rc = DBI_RC_SUCCESS;

    PR_ASSERT(flags == DBI_VF_BULK_DATA || flags == DBI_VF_BULK_RECORD);
    dblayer_value_set_int(be, &bulkdata->v, buff, len, len, DBI_VF_PROTECTED|DBI_VF_DONTGROW|flags);
    bulkdata->be = be;
    if (priv->dblayer_bulk_init_fn) {
        rc = priv->dblayer_bulk_init_fn(bulkdata);
    }
    return rc;
}

/* initialize iterator on a bulk operation result */
int dblayer_bulk_start(dbi_bulk_t *bulkdata)
{
    dblayer_private *priv = dblayer_get_priv(bulkdata->be);
    return priv->dblayer_bulk_start_fn(bulkdata);
}

int dblayer_cursor_bulkop(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_bulk_t *bulkdata)
{
    dblayer_private *priv = dblayer_get_priv(cursor->be);
    int rc = DBI_RC_UNSUPPORTED;
    switch (op) {
        case DBI_OP_MOVE_TO_FIRST:
        case DBI_OP_MOVE_TO_KEY:
        case DBI_OP_NEXT_DATA:
        case DBI_OP_NEXT_KEY:
            PR_ASSERT(bulkdata->v.flags & (DBI_VF_BULK_DATA|DBI_VF_BULK_RECORD));
            rc = priv->dblayer_cursor_bulkop_fn(cursor, op, key, bulkdata);
            break;
        case DBI_OP_MOVE_NEAR_KEY:
        case DBI_OP_NEXT:
            PR_ASSERT(bulkdata->v.flags & DBI_VF_BULK_RECORD);
            rc = priv->dblayer_cursor_bulkop_fn(cursor, op, key, bulkdata);
            break;
        default:
            PR_ASSERT(0);
    }
    return rc;
}

int dblayer_cursor_op(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_val_t *data)
{
    static const dbi_cursor_t cursor0 = {0};
    dblayer_private *priv;
    int rc = DBI_RC_UNSUPPORTED;
    if (op == DBI_OP_CLOSE && cursor->be == NULL) {
        return DBI_RC_SUCCESS;
    }
    priv = dblayer_get_priv(cursor->be);
    switch (op) {
        case DBI_OP_MOVE_TO_KEY:
        case DBI_OP_MOVE_NEAR_KEY:
        case DBI_OP_MOVE_TO_DATA:
        case DBI_OP_MOVE_NEAR_DATA:
        case DBI_OP_MOVE_TO_RECNO:
        case DBI_OP_MOVE_TO_FIRST:
        case DBI_OP_MOVE_TO_LAST:
        case DBI_OP_GET_RECNO:
        case DBI_OP_NEXT:
        case DBI_OP_NEXT_DATA:
        case DBI_OP_NEXT_KEY:
        case DBI_OP_PREV:
        case DBI_OP_REPLACE:
        case DBI_OP_ADD:
        case DBI_OP_DEL:
            rc = priv->dblayer_cursor_op_fn(cursor, op, key, data);
            break;
        case DBI_OP_CLOSE:
            rc = priv->dblayer_cursor_op_fn(cursor, op, key, data);
            *cursor = cursor0;
            break;
        default:
            PR_ASSERT(0);
    }
    return rc;
}

int dblayer_db_op(Slapi_Backend *be, dbi_db_t *db,  dbi_txn_t *txn, dbi_op_t op, dbi_val_t *key, dbi_val_t *data)
{
    dblayer_private *priv = dblayer_get_priv(be);
    int rc = DBI_RC_UNSUPPORTED;
    switch (op) {
        case DBI_OP_PUT:
        case DBI_OP_GET:
        case DBI_OP_DEL:
        case DBI_OP_ADD:
        case DBI_OP_CLOSE:
            rc = priv->dblayer_db_op_fn(db, txn, op, key, data);
            break;
        default:
            PR_ASSERT(0);
    }
    return rc;
}

int dblayer_new_cursor(Slapi_Backend *be, dbi_db_t *db,  dbi_txn_t *txn, dbi_cursor_t *cursor)
{
    dblayer_private *priv = dblayer_get_priv(be);
    cursor->be = be;
    cursor->txn = txn;
    return priv->dblayer_new_cursor_fn(db, cursor);
}

/* free value resources */
int dblayer_value_free(Slapi_Backend *be __attribute__((unused)), dbi_val_t *data)
{
    if (data != NULL && !(data->flags & DBI_VF_PROTECTED)) {
        slapi_ch_free(&data->data);
        data->size = data->ulen = 0;
    }
    return DBI_RC_SUCCESS;
}

/* Initialize or re-initialize the value: warning resources are not freed */
int dblayer_value_init(Slapi_Backend *be __attribute__((unused)), dbi_val_t *data)
{
    memset(data, 0, sizeof *data);
    return DBI_RC_SUCCESS;
}

int dblayer_value_protect_data(Slapi_Backend *be __attribute__((unused)), dbi_val_t *data)
{
    data->flags |= DBI_VF_PROTECTED;
    return DBI_RC_SUCCESS;
}


/* Set value memory as a fixed size buffer */
int dblayer_value_set_buffer(Slapi_Backend *be, dbi_val_t *data, void *buff, size_t len)
{
    return dblayer_value_set_int(be, data, buff, len, len, DBI_VF_PROTECTED|DBI_VF_DONTGROW);
}

/* Set value memory. Note: ptr is malloc memory (or dblayer_value_protect_data should be called) */
int dblayer_value_set(Slapi_Backend *be, dbi_val_t *data, void *ptr, size_t size)
{
    return dblayer_value_set_int(be, data, ptr, size, size, DBI_VF_NONE);
}

int dblayer_value_strdup(Slapi_Backend *be, dbi_val_t *data, char *str)
{
    char *pt = slapi_ch_strdup(str);
    int len = strlen(pt);
    return dblayer_value_set_int(be, data, pt, len, len+1, DBI_VF_NONE);
}

/* Concat all data/size pairs into a dbi_val_t
 * value is either set from buffer (if it is large enough) or it is alloced
 * (Cannot use varargs here because it leads to a crash on Suze)
 */
void dblayer_value_concat(Slapi_Backend *be, dbi_val_t *data,
    void *buf, size_t buflen, const char *str1, size_t len1,
    const char *str2, size_t len2, const char *str3, size_t len3)
{
    char *pt = buf;
    size_t len;
    char lastc = '?';

    /* add space for \0 if it is missing */
    lastc = ((len3 > 0) ? str3[len3 -1] :
            ((len2 > 0) ? str2[len2 -1] :
            ((len1 > 0) ? str1[len1 -1] : '?')));
    len = len1 + len2 + len3 + (lastc ? 1 : 0);

    if (buflen >= len) {
        /* Use the provided buffer */
        dblayer_value_set_buffer(be, data, buf, buflen);
        data->size = len;
    } else {
        /* Alloc a new buffer */
        pt = slapi_ch_malloc(len);
        dblayer_value_set(be, data, pt, len);
    }

    memset(pt, 0, len);
    if (len1 > 0) {
        memcpy(pt, str1, len1);
        pt += len1;
    }
    if (len2 > 0) {
        memcpy(pt, str2, len2);
        pt += len2;
    }
    if (len3 > 0) {
        memcpy(pt, str3, len3);
        pt += len3;
    }
    if (lastc) {
        *pt = 0;
    }
}

int dblayer_set_dup_cmp_fn(Slapi_Backend *be, struct attrinfo *a, dbi_dup_cmp_t idx)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_set_dup_cmp_fn(a, idx);
}

const char *
dblayer_strerror(int error)
{
    /*
     * Cannot use slapi_pr_strerror() because dbscan is not linked with
     * libslapd so lets use dberrstrs.h
     * And table is small enough that it is not worth to use bsearch;
     */
    static const struct {
        int errcode;
        const char *errmsg;
    } errtab[] = {
#include "dberrstrs.h"
        { DBI_RC_SUCCESS, "No error." }
    }, *errpt = errtab;

    for (;errpt ->errcode; errpt++) {
        if (errpt->errcode == error) {
            return errpt->errmsg;
        }
    }
    return "Unexpected dbimpl error code";
}

int dblayer_cursor_get_count(dbi_cursor_t *cursor, dbi_recno_t *count)
{
    dblayer_private *priv;
    if (!cursor || !cursor->be) {
        return DBI_RC_INVALID;
    }
    priv = dblayer_get_priv(cursor->be);
    return priv->dblayer_cursor_get_count_fn(cursor, count);
}

int dblayer_dbi_txn_begin(Slapi_Backend *be, dbi_env_t *dbenv, PRBool readonly, dbi_txn_t *parent_txn, dbi_txn_t **txn)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_dbi_txn_begin_fn(dbenv, readonly, parent_txn, txn);
}

int dblayer_dbi_txn_commit(Slapi_Backend *be, dbi_txn_t *txn)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_dbi_txn_commit_fn(txn);
}

int dblayer_dbi_txn_abort(Slapi_Backend *be, dbi_txn_t *txn)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_dbi_txn_abort_fn(txn);
}

int dblayer_get_entries_count(Slapi_Backend *be, dbi_db_t *db, dbi_txn_t *txn, int *count)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_get_entries_count_fn(db, txn, count);
}

const char *dblayer_op2str(dbi_op_t op)
{
    static const char *str[] = {
        "DBI_OP_MOVE_TO_KEY",         /* move cursor to specified key and data
                                       * then get the record.
                                       */
        "DBI_OP_MOVE_NEAR_KEY",       /* move cursor to smallest key greater or equal
                                       * than specified key then get the record.
                                       */
        "DBI_OP_MOVE_TO_DATA",        /* move cursor to specified key and data
                                       * then get the record.
                                       */
        "DBI_OP_MOVE_NEAR_DATA",      /* move cursor to specified key and smallest
                                       * data greater or equal than specified data
                                       * then get the record.
                                       */
        "DBI_OP_MOVE_TO_RECNO",       /* move cursor to specified record number */
        "DBI_OP_MOVE_TO_LAST",
        "DBI_OP_GET",                 /* db operation: get record associated with key */
        "DBI_OP_GET_RECNO",           /* Get current record number */
        "DBI_OP_NEXT",                /* move to next record */
        "DBI_OP_NEXT_DATA",           /* Move to next record having same key */
        "DBI_OP_NEXT_KEY",            /* move to next record having different key */
        "DBI_OP_PREV",
        "DBI_OP_PUT",                 /* db operation */
        "DBI_OP_REPLACE",             /* Replace value at cursor position (key is ignored) */
        "DBI_OP_ADD",                 /* Add record if it does not exists */
        "DBI_OP_DEL",
        "DBI_OP_CLOSE"
    };
    int idx = op - DBI_OP_MOVE_TO_KEY;
    if (idx <0 || idx >= (sizeof str)/(sizeof str[0])) {
        return "INVALID DBI_OP";
    }
    return str[idx];
}

/* Get the li_directory directory from the database instance name -
 * Caller should free the returned value
 */
static char *
get_li_directory(const char *fname)
{
    /*
     * li_directory is an existing directory.
     * it can be fname or its parent or its greatparent
     * in case of problem returns the provided name
     */
    char *lid = slapi_ch_strdup(fname);
    struct stat sbuf = {0};
    char *pt = NULL;
    for (int count=0; count<3; count++) {
        if (stat(lid, &sbuf) == 0) {
            if (S_ISDIR(sbuf.st_mode)) {
                return lid;
            }
            /* Non directory existing file could be regular
             * at the first iteration otherwise it is an error.
             */
            if (count>0 || !S_ISREG(sbuf.st_mode)) {
                break;
            }
        }
        pt = strrchr(lid, '/');
        if (pt == NULL) {
            slapi_ch_free_string(&lid);
            return slapi_ch_strdup(".");
        }
        *pt = '\0';
    }
    /*
     * Error case. Returns a copy of the original string:
     *  and let dblayer_private_open_fn fail to open the database
     */
    slapi_ch_free_string(&lid);
    return slapi_ch_strdup(fname);
}

/* Open db env, db and db file privately (for dbscan) */
int dblayer_private_open(const char *plgname, const char *dbfilename, int rw, Slapi_Backend **be, dbi_env_t **env, dbi_db_t **db)
{
    struct ldbminfo *li;
    int rc;

    /* Setup a fake backend that supports dblayer_get_priv */
    *be = (Slapi_Backend*) slapi_ch_calloc(1, sizeof (Slapi_Backend));
    (*be)->be_database = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));
    (*be)->be_instance_info = (ldbm_instance *)slapi_ch_calloc(1, sizeof(ldbm_instance));
    li = (struct ldbminfo *)slapi_ch_calloc(1, sizeof(struct ldbminfo));
    (*be)->be_database->plg_private = li;
    li->li_plugin = (*be)->be_database;
    li->li_plugin->plg_name = (char*) "back-ldbm-dbimpl";
    li->li_plugin->plg_libpath = (char*) "libback-ldbm";
    li->li_directory = get_li_directory(dbfilename);

    /* Initialize database plugin */
    rc = dbimpl_setup(li, plgname);
    /* Then open the env database plugin */
    if (!rc) {
        dblayer_private *priv = li->li_dblayer_private;
        rc = priv->dblayer_private_open_fn(*be, dbfilename, rw, env, db);
    }
    if (rc) {
        dblayer_private_close(be, env, db);
    }
    return rc;
}

int dblayer_private_close(Slapi_Backend **be, dbi_env_t **env, dbi_db_t **db)
{
    int rc = 0;
    if (*be) {
        struct ldbminfo *li = (struct ldbminfo *)(*be)->be_database->plg_private;
        dblayer_private *priv = li->li_dblayer_private;

        if (priv && priv->dblayer_private_close_fn) {
            rc = priv->dblayer_private_close_fn(li, env, db);
        }
        slapi_ch_free_string(&li->li_directory);
        slapi_ch_free((void**)&li->li_dblayer_private);
        slapi_ch_free((void**)&li->li_dblayer_config);
        ldbm_config_destroy(li);
        slapi_ch_free((void**)&(*be)->be_database);
        slapi_ch_free((void**)&(*be)->be_instance_info);
        slapi_ch_free((void**)be);
    }
    return rc;
}

int dblayer_show_statistics(const char *dbimpl_name, const char *dbhome, FILE *fout, FILE *ferr)
{
    struct ldbminfo *li;
    Slapi_Backend *be;
    dbi_env_t *env = NULL;
    dbi_db_t *db = NULL;
    int rc;

    /* Setup a fake backend that supports dblayer_get_priv */
    be = (Slapi_Backend*) slapi_ch_calloc(1, sizeof (Slapi_Backend));
    be->be_database = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));
    li = (struct ldbminfo *)slapi_ch_calloc(1, sizeof(struct ldbminfo));
    be->be_database->plg_private = li;
    li->li_plugin = be->be_database;
    li->li_plugin->plg_name = (char*) "back-ldbm-dbimpl";
    li->li_plugin->plg_libpath = (char*) "libback-ldbm";
    li->li_directory = get_li_directory(dbhome);

    /* Initialize database plugin */
    rc = dbimpl_setup(li, dbimpl_name);
    /* Then open the env database plugin */
    if (!rc) {
        dblayer_private *priv = li->li_dblayer_private;
        if (priv->dblayer_show_stat_fn) {
            rc = priv->dblayer_show_stat_fn(dbhome, fout, ferr);
        } else {
            fprintf(ferr, "dblayer_show_statistics not supported on %s.\n", dbimpl_name);
            rc = -1;
        }
    }
    dblayer_private_close(&be, &env, &db);
    return rc;
}

dbi_dbslist_t *dblayer_list_dbs(const char *dbimpl_name, const char *dbhome)
{
    struct ldbminfo *li;
    dbi_dbslist_t *dbs = NULL;
    Slapi_Backend *be;
    dbi_env_t *env = NULL;
    dbi_db_t *db = NULL;
    int rc;

    /* Setup a fake backend that supports dblayer_get_priv */
    be = (Slapi_Backend*) slapi_ch_calloc(1, sizeof (Slapi_Backend));
    be->be_database = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));
    li = (struct ldbminfo *)slapi_ch_calloc(1, sizeof(struct ldbminfo));
    be->be_database->plg_private = li;
    li->li_plugin = be->be_database;
    li->li_plugin->plg_name = (char*) "back-ldbm-dbimpl";
    li->li_plugin->plg_libpath = (char*) "libback-ldbm";
    li->li_directory = slapi_ch_strdup(dbhome);

    /* Initialize database plugin */
    rc = dbimpl_setup(li, dbimpl_name);
    /* Then open the env database plugin */
    if (!rc) {
        dblayer_private *priv = li->li_dblayer_private;
        if (priv->dblayer_list_dbs_fn) {
            dbs = priv->dblayer_list_dbs_fn(dbhome);
        }
    }
    dblayer_private_close(&be, &env, &db);
    return dbs;
}

int dblayer_db_remove(Slapi_Backend *be, dbi_db_t *db)
{
    dblayer_private *priv = dblayer_get_priv(be);
    return priv->dblayer_dbi_db_remove_fn(be, db);
}

