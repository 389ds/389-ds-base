/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* Shim which forwards IDL calls to the appropriate implementation */

#include "back-ldbm.h"

static int idl_new = 0; /* non-zero if we're doing new IDL style */


void idl_old_set_tune(int val);
int idl_old_get_tune();
int idl_old_init_private(backend *be, struct attrinfo *a);
int idl_old_release_private(struct attrinfo *a);
size_t idl_old_get_allidslimit(struct attrinfo *a);
IDList * idl_old_fetch( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err );
int idl_old_insert_key( backend *be, DB* db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a,int *disposition );
int idl_old_delete_key( backend *be, DB *db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a );
int idl_old_store_block( backend *be,DB *db,DBT *key,IDList *idl,DB_TXN *txn,struct attrinfo *a);


void idl_new_set_tune(int val);
int idl_new_get_tune();
int idl_new_init_private(backend *be, struct attrinfo *a);
int idl_new_release_private(struct attrinfo *a);
size_t idl_new_get_allidslimit(struct attrinfo *a);
IDList * idl_new_fetch( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err );
int idl_new_insert_key( backend *be, DB* db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a,int *disposition );
int idl_new_delete_key( backend *be, DB *db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a );
int idl_new_store_block( backend *be,DB *db,DBT *key,IDList *idl,DB_TXN *txn,struct attrinfo *a);

int idl_get_idl_new()
{
	return idl_new;
}

void idl_set_tune(int val)
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

int idl_get_tune()
{
	if (idl_new) {
		return idl_new_get_tune();
	} else {
		return idl_old_get_tune();
	}
}

int idl_init_private(backend *be, struct attrinfo *a)
{
	if (idl_new) {
		return idl_new_init_private(be,a);
	} else {
		return idl_old_init_private(be,a);
	}
}

int idl_release_private(struct attrinfo *a)
{
	if (idl_new) {
		return idl_new_release_private(a);
	} else {
		return idl_old_release_private(a);
	}
}

size_t idl_get_allidslimit(struct attrinfo *a)
{
	if (idl_new) {
		return idl_new_get_allidslimit(a);
	} else {
		return idl_old_get_allidslimit(a);
	}
}

IDList * idl_fetch( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err )
{
	if (idl_new) {
		return idl_new_fetch(be,db,key,txn,a,err);
	} else {
		return idl_old_fetch(be,db,key,txn,a,err);
	}
}

int idl_insert_key( backend *be, DB* db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a,int *disposition )
{
	if (idl_new) {
		return idl_new_insert_key(be,db,key,id,txn,a,disposition);
	} else {
		return idl_old_insert_key(be,db,key,id,txn,a,disposition);
	}
}

int idl_delete_key(backend *be, DB *db, DBT *key, ID id, DB_TXN *txn, struct attrinfo *a )
{
	if (idl_new) {
		return idl_new_delete_key(be,db,key,id,txn,a);
	} else {
		return idl_old_delete_key(be,db,key,id,txn,a);
	}
}

int idl_store_block(backend *be,DB *db,DBT *key,IDList *idl,DB_TXN *txn,struct attrinfo *a)
{
	if (idl_new) {
		return idl_new_store_block(be,db,key,idl,txn,a);
	} else {
		return idl_old_store_block(be,db,key,idl,txn,a);
	}
}
