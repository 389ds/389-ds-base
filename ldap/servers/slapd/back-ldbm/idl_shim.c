/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
size_t idl_new_get_allidslimit(struct attrinfo *a, int allidslimit);
IDList * idl_new_fetch( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err, int allidslimit );
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

size_t idl_get_allidslimit(struct attrinfo *a, int allidslimit)
{
	if (idl_new) {
		return idl_new_get_allidslimit(a, allidslimit);
	} else {
		return idl_old_get_allidslimit(a);
	}
}

IDList * idl_fetch_ext( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err, int allidslimit )
{
	if (idl_new) {
		return idl_new_fetch(be,db,key,txn,a,err,allidslimit);
	} else {
		return idl_old_fetch(be,db,key,txn,a,err);
	}
}

IDList * idl_fetch( backend *be, DB* db, DBT *key, DB_TXN *txn, struct attrinfo *a, int *err )
{
    return idl_fetch_ext(be, db, key, txn, a, err, 0);
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
