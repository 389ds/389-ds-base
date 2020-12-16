/** BEGIN COPYRIGHT BLOCK
        * Copyright (C) 2020 Red Hat, Inc.
        * All rights reserved.
        *
        * License: GPL (version 3 or any later version).
        * See LICENSE for details.
        * END COPYRIGHT BLOCK **/

#ifndef _DBIMPL_H
#define _DBIMPL_H

/* Temporary wrapup toward libdb */
#include <db.h>

#define MEM_FOR_DB_PLUGINS      (8*(sizeof (long)))

typedef enum {
    DBI_RC_SUCCESS,
    DBI_RC_UNSUPPORTED = 389000, /* Operation non supporte par cette implementation */
    DBI_RC_BUFFER_SMALL,
    DBI_RC_KEYEXIST,
    DBI_RC_NOTFOUND,
    DBI_RC_RUNRECOVERY,
    DBI_RC_RETRY,
    DBI_RC_OTHER
} dbi_error_t;


typedef enum {
    DBI_VF_NONE        = 0,
    DBI_VF_PROTECTED   = 0x01,  /* data should not be freed */
    DBI_VF_DONTGROW    = 0x02,  /* data should not be realloced */
    DBI_VF_READONLY    = 0x04,  /* data should not be modified */
    DBI_VF_BULK_DATA   = 0x08,  /* Bulk operation on data only */
    DBI_VF_BULK_RECORD = 0x10,  /* Bulk operation on key+data */
} dbi_valflags_t;               /* Should not be used in backend except within dbimpl.c */

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
    DBI_BULK_DATA,
    DBI_BULK_RECORDS
} dbi_bulk_mode_t;

typedef enum {
    DBI_DUP_CMP_NONE,
    DBI_DUP_CMP_ENTRYRDN
} dbi_dup_cmp_t;

/* Opaque struct definition used by database implementation plugins */

typedef void dbi_env_t;         /* the global database framework context */
typedef void dbi_db_t;          /* A database instance context */
typedef void dbi_txn_t;         /* A transaction context */

/* semi opaque definition  (members may be used by dbimpl.c and implementation plugins */

typedef struct {
    struct backend  *be;
    dbi_txn_t       *txn;
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


#endif /* _DBIMPL_H */
