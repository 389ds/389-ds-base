/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifndef _DBIMPL_H_
#define _DBIMPL_H_

#include "../slapi-plugin.h"
#include <limits.h>

#define MEM_FOR_DB_PLUGINS      (8*(sizeof (long)))

/* Note: DBI_RC_ definition are parsed by ../mkDBErrStrs.py to generate
 * the errormap sorted error message table used by slapi_pr_strerror().
 * So:
 *  - The error code is important. value should be sorted in increasing order
 *     and should be lesser than SSL_ERROR_BASE (i.e -0x3000)
 *  - The comment format is important as it is used by ../mkDBErrStrs.py (beware to preserve the
       enum value and the comment value in sync when adding/removing error codes)
 */
typedef enum {
    DBI_RC_SUCCESS,
    DBI_RC_UNSUPPORTED = -0x3200, /* -12800, Database operation error: Operation not supported. */
    DBI_RC_BUFFER_SMALL,          /* -12799, Database operation error: Buffer is too small to store the result. */
    DBI_RC_KEYEXIST,              /* -12798, Database operation error: Key already exists. */
    DBI_RC_NOTFOUND,              /* -12797, Database operation error: Key not found (or no more keys). */
    DBI_RC_RUNRECOVERY,           /* -12796, Database operation error: Database recovery is needed. */
    DBI_RC_RETRY,                 /* -12795, Database operation error: Transient error. transaction should be retried. */
    DBI_RC_INVALID,               /* -12794, Database operation error: Invalid parameter or invalid state. */
    DBI_RC_OTHER                  /* -12793, Database operation error:  Unhandled Database operation error. See details in previous error messages. */
} dbi_error_t;


typedef enum {
    DBI_VF_NONE        = 0,
    DBI_VF_PROTECTED   = 0x01,  /* data should not be freed */
    DBI_VF_DONTGROW    = 0x02,  /* data should not be realloced */
    DBI_VF_READONLY    = 0x04,  /* data should not be modified */
    DBI_VF_BULK_DATA   = 0x08,  /* Bulk operation on data only */
    DBI_VF_BULK_RECORD = 0x10,  /* Bulk operation on key+data */
} dbi_valflags_t;               /* Should not be used in backend except within dbimpl.c */

/* Warning! any change in dbi_op_t should also be reported in dblayer_op2str() */
typedef enum {
    DBI_OP_MOVE_TO_KEY = 1001,  /* move cursor to specified key and data
                                 * then get the record.
                                 */
    DBI_OP_MOVE_NEAR_KEY,       /* move cursor to smallest key greater or equal
                                 * than specified key then get the record.
                                 */
    DBI_OP_MOVE_TO_DATA,        /* move cursor to specified key and data
                                 * then get the record.
                                 */
    DBI_OP_MOVE_NEAR_DATA,      /* move cursor to specified key and smallest
                                 * data greater or equal than specified data
                                 * then get the record.
                                 */
    DBI_OP_MOVE_TO_RECNO,       /* move cursor to specified record number */
    DBI_OP_MOVE_TO_FIRST,       /* move cursor to first key */
    DBI_OP_MOVE_TO_LAST,
    DBI_OP_GET,                 /* db operation: get record associated with key */
    DBI_OP_GET_RECNO,           /* Get current record number */
    DBI_OP_NEXT,                /* move to next record */
    DBI_OP_NEXT_DATA,           /* Move to next record having same key */
    DBI_OP_NEXT_KEY,            /* move to next record having different key */
    DBI_OP_PREV,
    DBI_OP_PUT,                 /* db operation */
    DBI_OP_REPLACE,             /* Replace value at cursor position (key is ignored) */
    DBI_OP_ADD,                 /* Add record if it does not exists */
    DBI_OP_DEL,
    DBI_OP_CLOSE,
} dbi_op_t;

typedef enum {
    DBI_DUP_CMP_NONE,
    DBI_DUP_CMP_ENTRYRDN
} dbi_dup_cmp_t;

/* Opaque struct definition used by database implementation plugins */

typedef void dbi_env_t;         /* the global database framework context */
typedef void dbi_db_t;          /* A database instance context */
typedef void dbi_txn_t;         /* A transaction context */
typedef uint32_t dbi_recno_t;   /* A record position */

/* semi opaque definition  (members may be used by dbimpl.c and implementation plugins */

typedef struct {
    struct backend  *be;
    dbi_txn_t       *txn;
    int             islocaltxn;
    void            *cur;
} dbi_cursor_t;                 /* A db cursor */

typedef struct {
    dbi_valflags_t  flags;
    void            *data;
    size_t          size;
    size_t          ulen;
} dbi_val_t;

typedef struct {
    struct backend  *be;
    dbi_val_t       v;
    void            *it;       /* implementation plugin iterator */
} dbi_bulk_t;

/* For dblayer_list_dbs */
typedef struct {
    char filename[PATH_MAX];
    char info[PATH_MAX];
} dbi_dbslist_t;

struct attrinfo;

/*
 * dbimpl.c Function prototypes are stored here instead of in
 * proto-back-ldbm.h because this API is used by replication
 * and dbscan tools (and including proto-back-ldbm.h is painful
 * because of the complex dependency chain between slapd and backend)
 */
char *dblayer_get_filename_id(Slapi_Backend *be, dbi_env_t *env);
int dblayer_bulk_free(dbi_bulk_t *bulkdata);
int dblayer_bulk_nextdata(dbi_bulk_t *bulkdata, dbi_val_t *data);
int dblayer_bulk_nextrecord(dbi_bulk_t *bulkdata, dbi_val_t *key, dbi_val_t *data);
int dblayer_bulk_set_buffer(Slapi_Backend *be, dbi_bulk_t *bulkdata, void *buff, size_t len, dbi_valflags_t flags);
int dblayer_bulk_start(dbi_bulk_t *bulkdata);
int dblayer_cursor_bulkop(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_bulk_t *bulkdata);
int dblayer_cursor_op(dbi_cursor_t *cursor,  dbi_op_t op, dbi_val_t *key, dbi_val_t *data);
int dblayer_db_op(Slapi_Backend *be, dbi_db_t *db,  dbi_txn_t *txn, dbi_op_t op, dbi_val_t *key, dbi_val_t *data);
int dblayer_new_cursor(Slapi_Backend *be, dbi_db_t *db,  dbi_txn_t *txn, dbi_cursor_t *cursor);
int dblayer_value_free(Slapi_Backend *be, dbi_val_t *data);
int dblayer_value_init(Slapi_Backend *be, dbi_val_t *data);
int dblayer_value_protect_data(Slapi_Backend *be, dbi_val_t *data);
int dblayer_value_set_buffer(Slapi_Backend *be, dbi_val_t *data, void *buff, size_t len);
int dblayer_value_set(Slapi_Backend *be, dbi_val_t *data, void *ptr, size_t size);
int dblayer_value_strdup(Slapi_Backend *be, dbi_val_t *data, char *str);
void dblayer_value_concat(Slapi_Backend *be, dbi_val_t *data,
    void *buf, size_t buflen, const char *str1, size_t len1,
    const char *str2, size_t len2, const char *str3, size_t len3);
int dblayer_set_dup_cmp_fn(Slapi_Backend *be, struct attrinfo *a, dbi_dup_cmp_t idx);
/*
 * Note: dblayer_txn_* functions uses back_txn struct and manage backend lock.
 * while dblayer_dbi_txn_* function use dbi_txn_t opaque struct and interface
 * directly the underlying db.
 */
int dblayer_dbi_txn_begin(Slapi_Backend *be, dbi_env_t *dbenv, int flags, dbi_txn_t *parent_txn, dbi_txn_t **txn);
int dblayer_dbi_txn_commit(Slapi_Backend *be, dbi_txn_t *txn);
int dblayer_dbi_txn_abort(Slapi_Backend *be, dbi_txn_t *txn);
int dblayer_get_entries_count(Slapi_Backend *be, dbi_db_t *db, dbi_txn_t *txn, int *count);
int dblayer_cursor_get_count(dbi_cursor_t *cursor, dbi_recno_t *count);
char *dblayer_get_db_filename(Slapi_Backend *be, dbi_db_t *db);
const char *dblayer_strerror(int error);
const char *dblayer_op2str(dbi_op_t op);
int dblayer_cursor_get_count(dbi_cursor_t *cursor, dbi_recno_t *count);

int dblayer_private_open(const char *plgname, const char *dbfilename, int rw, Slapi_Backend **be, dbi_env_t **env, dbi_db_t **db);
int dblayer_private_close(Slapi_Backend **be, dbi_env_t **env, dbi_db_t **db);
dbi_dbslist_t *dblayer_list_dbs(const char *dbimpl_name, const char *dbhome);
int dblayer_db_remove(Slapi_Backend *be, dbi_db_t *db);


#endif /* _DBIMPL_H */
