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
ldbm_back_seq( Slapi_PBlock *pb )
{
	backend         *be;
	ldbm_instance   *inst;
	struct ldbminfo *li;
	IDList          *idl = NULL;
	back_txn        txn = {NULL};
	struct attrinfo	*ai = NULL;
	DB              *db;
	DBC             *dbc = NULL;
	char *attrname, *val;
	int err = LDAP_SUCCESS;
	int return_value = -1;
	int nentries = 0;
	int retry_count = 0;
	int isroot;
	int type;

	/* Decode arguments */
	slapi_pblock_get( pb, SLAPI_BACKEND, &be);
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_SEQ_TYPE, &type );
	slapi_pblock_get( pb, SLAPI_SEQ_ATTRNAME, &attrname );
	slapi_pblock_get( pb, SLAPI_SEQ_VAL, &val );
	slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
	slapi_pblock_get( pb, SLAPI_TXN, &txn.back_txn_txn );

	inst = (ldbm_instance *) be->be_instance_info;

	if ( !txn.back_txn_txn ) {
		dblayer_txn_init( li, &txn );
		slapi_pblock_set( pb, SLAPI_TXN, txn.back_txn_txn );
	}

	/* Validate arguments */
	if ( type != SLAPI_SEQ_FIRST &&
	     type != SLAPI_SEQ_LAST &&
	     type != SLAPI_SEQ_NEXT &&
	     type != SLAPI_SEQ_PREV )
	{
	    slapi_send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
		    "Bad seq access type", 0, NULL );
	    return( -1 );
	}

	/* get a database */

	ainfo_get( be, attrname, &ai );
	LDAPDebug( LDAP_DEBUG_ARGS,
	    "   seq: indextype: %s indexmask: 0x%x seek type: %d\n",
	    ai->ai_type, ai->ai_indexmask, type );
	if ( ! (INDEX_EQUALITY & ai->ai_indexmask) ) {
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "seq: caller specified un-indexed attribute %s\n",
			   attrname ? attrname : "", 0, 0 );
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
		    "Unindexed seq access type", 0, NULL );
		return -1;
	}

	if ( (return_value = dblayer_get_index_file( be, ai, &db, DBOPEN_CREATE )) != 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "<= ldbm_back_seq NULL (could not open index file for attribute %s)\n",
		    attrname, 0, 0 );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
		return -1;
	}

	/* First, get a database cursor */

	return_value = db->cursor(db, txn.back_txn_txn, &dbc, 0);

	if (0 == return_value)
	{
		DBT data = {0};
		DBT key = {0};
		char little_buffer[SEQ_LITTLE_BUFFER_SIZE];
		char *big_buffer = NULL;
		char keystring = EQ_PREFIX;

		/* Set data */
		data.flags = DB_DBT_MALLOC;

		/* Set up key */
		key.flags = DB_DBT_MALLOC;
		if (NULL == val)
		{
			/* this means, goto the first equality key */
			/* seek to key >= "=" */
			key.data = &keystring;
			key.size = 1;
		}
		else
		{
			size_t key_length = strlen(val) + 2;
			if (key_length <= SEQ_LITTLE_BUFFER_SIZE) {
				key.data = &little_buffer;
			} else {
				big_buffer = slapi_ch_malloc(key_length);
				if (NULL == big_buffer) {
					/* memory allocation failure */
					dblayer_release_index_file( be, ai, db );
					return -1;
				}
				key.data = big_buffer;
			}
			key.size = sprintf(key.data,"%c%s",EQ_PREFIX,val);
		}

		/* decide which type of operation we're being asked to do and do the db bit */
		/* The c_get call always mallocs memory for data.data */
		/* The c_get call mallocs memory for key.data, except for DB_SET */
		/* after this, we leave data containing the retrieved IDL, or NULL if we didn't get it */

		switch (type) {
		case SLAPI_SEQ_FIRST:
			/* if (NULL == val) goto the first equality key ( seek to key >= "=" ) */
			/* else goto the first equality key >= val ( seek to key >= "=val"  )*/
			return_value = dbc->c_get(dbc,&key,&data,DB_SET_RANGE);
			break;
		case SLAPI_SEQ_NEXT:
			/* seek to the indicated =value, then seek to the next entry, */
			return_value = dbc->c_get(dbc,&key,&data,DB_SET);
			if (0 == return_value)
			{
				slapi_ch_free(&(data.data));
				return_value = dbc->c_get(dbc,&key,&data,DB_NEXT);
			}
			else
			{
				/* DB_SET doesn't allocate key data.  Make sure we don't try to free it... */
				key.data= NULL;
			}
			break;
		case SLAPI_SEQ_PREV:
			/* seek to the indicated =value, then seek to the previous entry, */
			return_value = dbc->c_get(dbc,&key,&data,DB_SET);
			if (0 == return_value )
			{
				slapi_ch_free(&(data.data));
				return_value = dbc->c_get(dbc,&key,&data,DB_PREV);
			}
			else
			{
				/* DB_SET doesn't allocate key data.  Make sure we don't try to free it... */
				key.data= NULL;
			}
			break;
		case SLAPI_SEQ_LAST:
			/* seek to the first possible key after all the equality keys (">"), then seek back one */
			{
				keystring = EQ_PREFIX + 1;
				key.data = &keystring;
				key.size = 1;
				return_value = dbc->c_get(dbc,&key,&data,DB_SET_RANGE);
				if (0 == return_value || DB_NOTFOUND == return_value)
				{
					slapi_ch_free(&(data.data));
					return_value = dbc->c_get(dbc,&key,&data,DB_PREV); 
				}
			}
			break;
		}

		dbc->c_close(dbc);

		if (0 == return_value && key.data!=NULL)
		{

			/* Now check that the key we eventually settled on was an equality key ! */
			if (*((char*)key.data) == EQ_PREFIX)
			{
				/* Retrieve the idlist for this key */
				key.flags = 0;
				for (retry_count = 0; retry_count < IDL_FETCH_RETRY_COUNT; retry_count++) {
					err = NEW_IDL_DEFAULT;
					idl = idl_fetch( be, db, &key, txn.back_txn_txn, ai, &err );
					if(err == DB_LOCK_DEADLOCK) {
						ldbm_nasty("ldbm_back_seq deadlock retry", 1600, err);
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to retry the entire transaction
#endif
						continue;
					} else {
						break;
					}
				}
			}
		}
		if(retry_count == IDL_FETCH_RETRY_COUNT) {
			ldbm_nasty("ldbm_back_seq retry count exceeded",1645,err);
		} else if ( err != 0 && err != DB_NOTFOUND ) {
			ldbm_nasty("ldbm_back_seq database error", 1650, err);
		}
		slapi_ch_free( &(data.data) );
		if ( key.data != little_buffer && key.data != &keystring ) {
		    slapi_ch_free( &(key.data) );
		}
		slapi_ch_free_string( &big_buffer );
	}

	/* null idlist means there were no matching keys */
	if ( idl != NULL )
	{
		/*
		 * Step through the IDlist.  For each ID, get the entry
		 * and send it.
		 */
		ID id;
		struct backentry	*e;
		for ( id = idl_firstid( idl ); id != NOID;
			id = idl_nextid( idl, id ))
		{
		    if (( e = id2entry( be, id, &txn, &err )) == NULL )
		    {
				if ( err != LDAP_SUCCESS )
				{
				    LDAPDebug( LDAP_DEBUG_ANY, "seq id2entry err %d\n", err, 0, 0 );
				}
				LDAPDebug( LDAP_DEBUG_ARGS,
					"ldbm_back_seq: candidate %lu not found\n",
					(u_long)id, 0, 0 );
				continue;
		    }
		    if ( slapi_send_ldap_search_entry( pb, e->ep_entry, NULL, NULL, 0 ) == 0 )
		    {
				nentries++;
		    }
		    CACHE_RETURN( &inst->inst_cache, &e );
		}
		idl_free( idl );
	}

	dblayer_release_index_file( be, ai, db );

	slapi_send_ldap_result( pb, LDAP_SUCCESS == err ? LDAP_SUCCESS : LDAP_OPERATIONS_ERROR, NULL, NULL, nentries, NULL );

	return 0;
}
