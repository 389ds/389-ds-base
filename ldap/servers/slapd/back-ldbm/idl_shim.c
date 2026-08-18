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

/* Shim which forwards IDL calls to the appropriate implementation */

#include "back-ldbm.h"

static int idl_new = 0; /* non-zero if we're doing new IDL style */


void idl_old_set_tune(int val);
int idl_old_get_tune(void);
int idl_old_init_private(backend *be, struct attrinfo *a);
int idl_old_release_private(struct attrinfo *a);
size_t idl_old_get_allidslimit(struct attrinfo *a);
IDList *idl_old_fetch(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn, struct attrinfo *a, int *err);
int idl_old_insert_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, dbi_txn_t *txn, struct attrinfo *a, int *disposition);
int idl_old_delete_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, dbi_txn_t *txn, struct attrinfo *a);
int idl_old_store_block(backend *be, dbi_db_t *db, dbi_val_t *key, IDList *idl, dbi_txn_t *txn, struct attrinfo *a);


void idl_new_set_tune(int val);
int idl_new_get_tune(void);
int idl_new_init_private(backend *be, struct attrinfo *a);
int idl_new_release_private(struct attrinfo *a);
size_t idl_new_get_allidslimit(struct attrinfo *a, int allidslimit);
IDList *idl_new_fetch(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn, struct attrinfo *a, int *err, int allidslimit);
int idl_new_insert_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, dbi_txn_t *txn, struct attrinfo *a, int *disposition);
int idl_new_delete_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, dbi_txn_t *txn, struct attrinfo *a);
int idl_new_store_block(backend *be, dbi_db_t *db, dbi_val_t *key, IDList *idl, dbi_txn_t *txn, struct attrinfo *a);

int
idl_get_idl_new()
{
    return idl_new;
}

void
idl_set_tune(int val)
{
    /* Catch idl_tune requests to use new idl code */
    if (4096 == val) {
        idl_new = 1;
    } else {
        idl_new = 0;
    }
    if (idl_new) {
        idl_new_set_tune(val);
    } else {
        idl_old_set_tune(val);
    }
}

int
idl_get_tune(void)
{
    if (idl_new) {
        return idl_new_get_tune();
    } else {
        return idl_old_get_tune();
    }
}

int
idl_init_private(backend *be, struct attrinfo *a)
{
    if (idl_new) {
        return idl_new_init_private(be, a);
    } else {
        return idl_old_init_private(be, a);
    }
}

int
idl_release_private(struct attrinfo *a)
{
    if (idl_new) {
        return idl_new_release_private(a);
    } else {
        return idl_old_release_private(a);
    }
}

size_t
idl_get_allidslimit(struct attrinfo *a, int allidslimit)
{
    if (idl_new) {
        return idl_new_get_allidslimit(a, allidslimit);
    } else {
        return idl_old_get_allidslimit(a);
    }
}

IDList *
idl_fetch_ext(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn, struct attrinfo *a, int *err, int allidslimit)
{
    if (idl_new) {
        return idl_new_fetch(be, db, key, txn, a, err, allidslimit);
    } else {
        return idl_old_fetch(be, db, key, txn, a, err);
    }
}

IDList *
idl_fetch(backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *txn, struct attrinfo *a, int *err)
{
    return idl_fetch_ext(be, db, key, txn, a, err, 0);
}

int
idl_insert_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, back_txn *txn, struct attrinfo *a, int *disposition)
{
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;

    if (txn && txn->back_special_handling_fn) {
        index_update_t update;
        dbi_val_t data = {0};
        update.id = id;
        update.a = a;
        update.disposition = disposition;
        dblayer_value_set_buffer(be, &data, &update, sizeof update);
        return txn->back_special_handling_fn(be, BTXNACT_INDEX_ADD, db, key, &data, txn);
    }

    if (idl_new) {
        return idl_new_insert_key(be, db, key, id, db_txn, a, disposition);
    } else {
        return idl_old_insert_key(be, db, key, id, db_txn, a, disposition);
    }
}

int
idl_delete_key(backend *be, dbi_db_t *db, dbi_val_t *key, ID id, back_txn *txn, struct attrinfo *a)
{
    dbi_txn_t *db_txn = (txn != NULL) ? txn->back_txn_txn : NULL;

    if (txn && txn->back_special_handling_fn) {
        index_update_t update;
        dbi_val_t data = {0};
        update.id = id;
        update.a = a;
        update.disposition = NULL;
        dblayer_value_set_buffer(be, &data, &update, sizeof update);
        return txn->back_special_handling_fn(be, BTXNACT_INDEX_DEL, db, key, &data, txn);
    }

    if (idl_new) {
        return idl_new_delete_key(be, db, key, id, db_txn, a);
    } else {
        return idl_old_delete_key(be, db, key, id, db_txn, a);
    }
}

int
idl_store_block(backend *be, dbi_db_t *db, dbi_val_t *key, IDList *idl, dbi_txn_t *txn, struct attrinfo *a)
{
    if (idl_new) {
        return idl_new_store_block(be, db, key, idl, txn, a);
    } else {
        return idl_old_store_block(be, db, key, idl, txn, a);
    }
}
