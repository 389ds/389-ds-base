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

 
/* delete.c - ldbm backend delete routine */

#include "back-ldbm.h"

int
ldbm_back_delete( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	struct ldbminfo	*li = NULL;
	struct backentry *e = NULL;
	struct backentry *tombstone = NULL;
	char *dn = NULL;
	back_txn txn;
	back_txnid parent_txn;
	int retval = -1;
	char *msg;
	char *errbuf = NULL;
	int retry_count = 0;
	int disk_full = 0;
	int parent_found = 0;
	modify_context parent_modify_c = {0};
	int rc;
	int ldap_result_code= LDAP_SUCCESS;
	char *ldap_result_message= NULL;
	Slapi_DN sdn;
	char *e_uniqueid = NULL;
	Slapi_DN *nscpEntrySDN = NULL;
	int dblock_acquired= 0;
	Slapi_Operation *operation;
	CSN *opcsn = NULL;
	int is_fixup_operation = 0;
	int is_ruv = 0;                 /* True if the current entry is RUV */
	int is_replicated_operation= 0;
	int	is_tombstone_entry = 0;		/* True if the current entry is alreday a tombstone		*/
	int delete_tombstone_entry = 0;	/* We must remove the given tombstone entry from the DB	*/
	int create_tombstone_entry = 0;	/* We perform a "regular" LDAP delete but since we use	*/
									/* replication, we must create a new tombstone entry	*/
	int tombstone_in_cache = 0;
	entry_address *addr;
	int addordel_flags = 0; /* passed to index_addordel */

	slapi_pblock_get( pb, SLAPI_BACKEND, &be);
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_DELETE_TARGET, &dn );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr);
	slapi_pblock_get( pb, SLAPI_PARENT_TXN, (void**)&parent_txn );
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation );
	
	if (pb->pb_conn)
	{
		slapi_log_error (SLAPI_LOG_TRACE, "ldbm_back_delete", "enter conn=%" PRIu64 " op=%d\n", pb->pb_conn->c_connid, operation->o_opid);
	}

	is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
	is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
	delete_tombstone_entry = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_ENTRY);
	
	inst = (ldbm_instance *) be->be_instance_info;

	slapi_sdn_init_dn_byref(&sdn,dn);

	dblayer_txn_init(li,&txn);

	/* The dblock serializes writes to the database,
	 * which reduces deadlocking in the db code,
	 * which means that we run faster.
	 *
	 * But, this lock is re-enterant for the fixup
	 * operations that the URP code in the Replication
	 * plugin generates.
	 */
	if(SERIALLOCK(li) && !operation_is_flag_set(operation,OP_FLAG_REPL_FIXUP))
	{
		dblayer_lock_backend(be);
		dblock_acquired= 1;
	}

	/*
	 * We are about to pass the last abandon test, so from now on we are
	 * committed to finish this operation. Set status to "will complete"
	 * before we make our last abandon check to avoid race conditions in
	 * the code that processes abandon operations.
	 */
	if (operation) {
		operation->o_status = SLAPI_OP_STATUS_WILL_COMPLETE;
	}
	if ( slapi_op_abandoned( pb ) ) {
		goto error_return;
	}

	/* Don't call pre-op for Tombstone entries */
	if (!delete_tombstone_entry)
	{
		/* 
		 * Some present state information is passed through the PBlock to the
		 * backend pre-op plugin. To ensure a consistent snapshot of this state
		 * we wrap the reading of the entry with the dblock.
		 */
		ldap_result_code= get_copy_of_entry(pb, addr, &txn, SLAPI_DELETE_EXISTING_ENTRY, !is_replicated_operation);
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
		if(plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_DELETE_FN)==-1)
		{
			/* 
			 * Plugin indicated some kind of failure,
			 * or that this Operation became a No-Op.
			 */
			slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			goto error_return;
		}
	}
	

	/* find and lock the entry we are about to modify */
	if ( (e = find_entry2modify( pb, be, addr, NULL )) == NULL )
	{
    	ldap_result_code= -1; 
		goto error_return; /* error result sent by find_entry2modify() */
	}

	if ( slapi_entry_has_children( e->ep_entry ) )
	{
		ldap_result_code= LDAP_NOT_ALLOWED_ON_NONLEAF;
		goto error_return;
	}

	/*
	 * Sanity check to avoid to delete a non-tombstone or to tombstone again
	 * a tombstone entry. This should not happen (see bug 561003).
	 */
	is_tombstone_entry = slapi_entry_flag_is_set(e->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
	if (delete_tombstone_entry) {
		PR_ASSERT(is_tombstone_entry);
		if (!is_tombstone_entry) {
			slapi_log_error(SLAPI_LOG_FATAL, "ldbm_back_delete",
					"Attempt to delete a non-tombstone entry %s\n", dn);
			delete_tombstone_entry = 0;
		}
	} else {
		PR_ASSERT(!is_tombstone_entry);
		if (is_tombstone_entry) { 
				slapi_log_error(SLAPI_LOG_FATAL, "ldbm_back_delete",
						"Attempt to Tombstone again a tombstone entry %s\n", dn);
			delete_tombstone_entry = 1;
		}
	}

	/*
	 * If a CSN is set, we need to tombstone the entry,
	 * rather than deleting it outright.
	 */
	opcsn = operation_get_csn (operation);
	if (!delete_tombstone_entry)
	{
		if (opcsn == NULL && !is_fixup_operation && operation->o_csngen_handler)
		{
			/*
			 * Current op is a user request. Opcsn will be assigned
			 * by entry_assign_operation_csn() if the dn is in an
			 * updatable replica.
			 */
			opcsn = entry_assign_operation_csn ( pb, e->ep_entry, NULL );
		}
		if (opcsn != NULL)
		{
			if (!is_fixup_operation)
			{
				entry_set_maxcsn (e->ep_entry, opcsn);
			}
			/*
			 * We are dealing with replication and if we haven't been called to
			 * remove a tombstone, then it's because  we want to create a new one.
			 */
			if ( slapi_operation_get_replica_attr (pb, operation, "nsds5ReplicaTombstonePurgeInterval", &create_tombstone_entry) == 0)
			{
				create_tombstone_entry = (create_tombstone_entry < 0) ? 0 : 1;
			}
		}
	}

#if DEBUG
	slapi_log_error(SLAPI_LOG_REPL, "ldbm_back_delete",
			"entry: %s  - flags: delete %d is_tombstone_entry %d create %d \n",
			dn, delete_tombstone_entry, is_tombstone_entry, create_tombstone_entry);
#endif

	/* Save away a copy of the entry, before modifications */
	slapi_pblock_set( pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup( e->ep_entry ));

    /* JCMACL - Shouldn't the access check be before the has children check... 	
	 * otherwise we're revealing the fact that an entry exists and has children */
	ldap_result_code = plugin_call_acl_plugin (pb, e->ep_entry, NULL, NULL, SLAPI_ACL_DELETE, 
					ACLPLUGIN_ACCESS_DEFAULT, &errbuf );
	if ( ldap_result_code != LDAP_SUCCESS )
	{
		ldap_result_message= errbuf;
		goto error_return;
	}

	/*
	 * Get the entry's parent. We do this here because index_read
	 * seems to deadlock the database when dblayer_txn_begin is
	 * called.
	 */
	if (!delete_tombstone_entry)
	{
	    Slapi_DN parentsdn;

		slapi_sdn_init(&parentsdn);
		slapi_sdn_get_backend_parent(&sdn,&parentsdn,pb->pb_backend);
    	if ( !slapi_sdn_isempty(&parentsdn) )
		{
    		struct backentry *parent = NULL;
			entry_address parent_addr;

			parent_addr.dn = (char*)slapi_sdn_get_dn (&parentsdn);
			parent_addr.uniqueid = NULL;
    		parent = find_entry2modify_only(pb,be,&parent_addr,&txn);
    		if (NULL != parent) {
				int isglue;
				size_t haschildren = 0;

    			/* Unfortunately findentry doesn't tell us whether it just didn't find the entry, or if
    			   there was an error, so we have to assume that the parent wasn't found */
    			parent_found = 1;

				/* Modify the parent in memory */
    			modify_init(&parent_modify_c,parent);
    			retval = parent_update_on_childchange(&parent_modify_c,2,&haschildren); /* 2==delete */\
    			/* The modify context now contains info needed later */
    			if (0 != retval) {
    				ldap_result_code= LDAP_OPERATIONS_ERROR;
    				goto error_return;
    			}
				
				/*
				 * Replication urp_post_delete will delete the parent entry
				 * if it is a glue entry without any more children.
				 * Those urp condition checkings are done here to
				 * save unnecessary entry dup.
				 */
				isglue = slapi_entry_attr_hasvalue (parent_modify_c.new_entry->ep_entry,
							SLAPI_ATTR_OBJECTCLASS, "glue");
				if ( opcsn && parent_modify_c.new_entry && !haschildren && isglue)
				{
					slapi_pblock_set ( pb, SLAPI_DELETE_GLUE_PARENT_ENTRY,
						slapi_entry_dup (parent_modify_c.new_entry->ep_entry) );
				}
    		}		
    	}
		slapi_sdn_done(&parentsdn);
	}
    
	if(create_tombstone_entry)
	{
		/*
		 * The entry is not removed from the disk when we tombstone an
		 * entry. We change the DN, add objectclass=tombstone, and record
		 * the UniqueID of the parent entry.
		 */
		const char *childuniqueid= slapi_entry_get_uniqueid(e->ep_entry);
		const char *parentuniqueid= NULL;
		char *tombstone_dn = compute_entry_tombstone_dn(slapi_entry_get_dn(e->ep_entry),
			childuniqueid);
		Slapi_Value *tomb_value;

		nscpEntrySDN = slapi_entry_get_sdn(e->ep_entry);

		/* Copy the entry unique_id for URP conflict checking */
		e_uniqueid = slapi_ch_strdup(childuniqueid);

		if(parent_modify_c.old_entry!=NULL)
		{
			/* The suffix entry has no parent */
			parentuniqueid= slapi_entry_get_uniqueid(parent_modify_c.old_entry->ep_entry);
		}
		tombstone = backentry_dup( e );
		slapi_entry_set_dn(tombstone->ep_entry,tombstone_dn); /* Consumes DN */
		/* Set tombstone flag on ep_entry */
		slapi_entry_set_flag(tombstone->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
		
		if(parentuniqueid!=NULL)
		{
			/* The suffix entry has no parent */
			slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID, parentuniqueid);
		}
		if(nscpEntrySDN!=NULL)
		{
			slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN, slapi_sdn_get_ndn(nscpEntrySDN));
		}
		tomb_value = slapi_value_new_string(SLAPI_ATTR_VALUE_TOMBSTONE);
		value_update_csn(tomb_value, CSN_TYPE_VALUE_UPDATED,
			operation_get_csn(operation));
		slapi_entry_add_value(tombstone->ep_entry, SLAPI_ATTR_OBJECTCLASS, tomb_value);
		slapi_value_free(&tomb_value);
		
		/* XXXggood above used to be: slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE); */
		/* JCMREPL - Add a description of what's going on? */
	}

	/*
	 * So, we believe that no code up till here actually added anything
	 * to the persistent store. From now on, we're transacted
	 */
	
	for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
		if (retry_count > 0) {
			dblayer_txn_abort(li,&txn);
			/* We're re-trying */
			LDAPDebug( LDAP_DEBUG_TRACE, "Delete Retrying Transaction\n", 0, 0, 0 );
#ifndef LDBM_NO_BACKOFF_DELAY
            {
	        PRIntervalTime interval;
			interval = PR_MillisecondsToInterval(slapi_rand() % 100);
			DS_Sleep(interval);
			}
#endif
		}
		retval = dblayer_txn_begin(li,parent_txn,&txn);
		if (0 != retval) {
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			goto error_return;
		}
		if(create_tombstone_entry)
		{
			/*
			 * The entry is not removed from the disk when we tombstone an
			 * entry. We change the DN, add objectclass=tombstone, and record
			 * the UniqueID of the parent entry.
			 */
			retval = id2entry_add( be, tombstone, &txn );
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 1 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Abort and re-try */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_ANY, "id2entry_add failed, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
	        	ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
			tombstone_in_cache = 1;
		}
		else
		{
			/* delete the entry from disk */
			retval = id2entry_delete( be, e, &txn );
			if (DB_LOCK_DEADLOCK == retval)
			{
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 2 DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (retval  != 0 ) {
			  	if (retval == DB_RUNRECOVERY || 
				    LDBM_OS_ERR_IS_DISKFULL(retval)) {
				    disk_full = 1;
				}
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
		}
		/* delete from attribute indexes */
		addordel_flags = BE_INDEX_DEL|BE_INDEX_PRESENCE|BE_INDEX_EQUALITY;
		if (delete_tombstone_entry)
		{
			addordel_flags |= BE_INDEX_TOMBSTONE; /* tell index code we are deleting a tombstone */
		}
		retval = index_addordel_entry( be, e, addordel_flags, &txn );
		if (DB_LOCK_DEADLOCK == retval)
		{
			LDAPDebug( LDAP_DEBUG_ARGS, "delete 1 DEADLOCK\n", 0, 0, 0 );
			/* Retry txn */
			continue;
		}
		if (retval != 0) {
			LDAPDebug( LDAP_DEBUG_TRACE, "index_del_entry failed\n", 0, 0, 0 );
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			goto error_return;
		}
		if(create_tombstone_entry)
		{
			/*
			 * The tombstone entry is removed from all attribute indexes
			 * above, but we want it to remain in the nsUniqueID and nscpEntryDN indexes
			 * and for objectclass=tombstone.
			 */
			retval = index_addordel_string(be,SLAPI_ATTR_OBJECTCLASS,SLAPI_ATTR_VALUE_TOMBSTONE,tombstone->ep_id,BE_INDEX_ADD,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 4 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "delete 1 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
    			ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
			retval = index_addordel_string(be,SLAPI_ATTR_UNIQUEID,slapi_entry_get_uniqueid(tombstone->ep_entry),tombstone->ep_id,BE_INDEX_ADD,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 5 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "delete 2 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
    			ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
			retval = index_addordel_string(be,SLAPI_ATTR_NSCP_ENTRYDN, slapi_sdn_get_ndn(nscpEntrySDN),tombstone->ep_id,BE_INDEX_ADD,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 6 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "delete 3 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
    			ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
		} /* create_tombstone_entry */
		else if (delete_tombstone_entry)
		{
			/* 
			 * We need to remove the Tombstone entry from the remaining indexes:
			 * objectclass=nsTombstone, nsUniqueID, nscpEntryDN
			 */
			char *nscpedn = NULL;

			retval = index_addordel_string(be,SLAPI_ATTR_OBJECTCLASS,SLAPI_ATTR_VALUE_TOMBSTONE,e->ep_id,BE_INDEX_DEL,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 4 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "delete 1 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
    			ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
			retval = index_addordel_string(be,SLAPI_ATTR_UNIQUEID,slapi_entry_get_uniqueid(e->ep_entry),e->ep_id,BE_INDEX_DEL,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "delete 5 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "delete 2 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
    			ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}

			nscpedn = slapi_entry_attr_get_charptr(e->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN);
			if (nscpedn) {
				retval = index_addordel_string(be,SLAPI_ATTR_NSCP_ENTRYDN, nscpedn, e->ep_id,BE_INDEX_DEL,&txn);
				slapi_ch_free((void **)&nscpedn);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug( LDAP_DEBUG_ARGS, "delete 6 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug( LDAP_DEBUG_TRACE, "delete 3 BAD, err=%d %s\n",
							   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					ldap_result_code= LDAP_OPERATIONS_ERROR;
					goto error_return;
				}
			}
		} /* delete_tombstone_entry */
		
		if (parent_found) {
			/* Push out the db modifications from the parent entry */
			retval = modify_update_all(be,pb,&parent_modify_c,&txn);
			if (DB_LOCK_DEADLOCK == retval)
			{
				LDAPDebug( LDAP_DEBUG_ARGS, "del 4 DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "delete 3 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
		}
		/* 
		 * first check if searchentry needs to be removed
         * Remove the entry from the Virtual List View indexes.
		 */
		if (!delete_tombstone_entry && !is_ruv &&
		    !vlv_delete_search_entry(pb,e->ep_entry,inst)) {
			retval = vlv_update_all_indexes(&txn, be, pb, e, NULL);

			if (DB_LOCK_DEADLOCK == retval)
			{
				LDAPDebug( LDAP_DEBUG_ARGS, "delete DEADLOCK vlv_update_index\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (retval  != 0 ) {
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
		}
		if (retval == 0 ) {
			break;
		}
	}
	if (retry_count == RETRY_TIMES) {
		/* Failed */
		LDAPDebug( LDAP_DEBUG_ANY, "Retry count exceeded in delete\n", 0, 0, 0 );
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	retval = dblayer_txn_commit(li,&txn);
	if (0 != retval)
	{
		if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	/* delete from cache and clean up */
	cache_remove(&inst->inst_cache, e);
	cache_unlock_entry( &inst->inst_cache, e );
	cache_return( &inst->inst_cache, &e );
	if (parent_found)
	{
		 /* Replace the old parent entry with the newly modified one */
		modify_switch_entries( &parent_modify_c,be);
	}
	

	rc= 0;
	goto common_return;

error_return:
	if (e!=NULL) {
	    cache_unlock_entry( &inst->inst_cache, e );
	    cache_return( &inst->inst_cache, &e );
	}
	if (tombstone_in_cache)
	{
		cache_remove( &inst->inst_cache, tombstone );
	}
	else
	{
		backentry_free( &tombstone );
	}

	if (retval == DB_RUNRECOVERY) {
	    dblayer_remember_disk_filled(li);
	    ldbm_nasty("Delete",79,retval);
	    disk_full = 1;
	}

	if (disk_full) {
	    rc= return_on_disk_full(li);
	    goto diskfull_return;
	}
	else
	    rc= SLAPI_FAIL_GENERAL;

	/* It is specifically OK to make this call even when no transaction was in progress */
	dblayer_txn_abort(li,&txn); /* abort crashes in case disk full */
	
common_return:
	if (tombstone_in_cache)
	{
		cache_return( &inst->inst_cache, &tombstone );
	}
	
	/*
	 * The bepostop is called even if the operation fails,
	 * but not if the operation is purging tombstones.
	 */
	if (!delete_tombstone_entry) {
		plugin_call_plugins (pb, SLAPI_PLUGIN_BE_POST_DELETE_FN);
	}

diskfull_return:
    if(ldap_result_code!=-1)
	{
    	slapi_send_ldap_result( pb, ldap_result_code, NULL, ldap_result_message, 0, NULL );
	}
	modify_term(&parent_modify_c,be);
	if(dblock_acquired)
	{
		dblayer_unlock_backend(be);
	}
	if (rc == 0 && opcsn && !is_fixup_operation && !delete_tombstone_entry)
	{
		/* URP Naming Collision
		 * When an entry is deleted by a replicated delete operation
		 * we must check for entries that have had a naming collision
		 * with this entry. Now that this name has been given up, one
		 * of those entries can take over the name. 
		 */
		slapi_pblock_set(pb, SLAPI_URP_NAMING_COLLISION_DN, slapi_ch_strdup (dn));
	}
	done_with_pblock_entry(pb, SLAPI_DELETE_EXISTING_ENTRY);
	slapi_ch_free((void**)&errbuf);
	slapi_sdn_done(&sdn);
	slapi_ch_free_string(&e_uniqueid);
	if (pb->pb_conn)
	{
		slapi_log_error (SLAPI_LOG_TRACE, "ldbm_back_delete", "leave conn=%" PRIu64 " op=%d\n", pb->pb_conn->c_connid, operation->o_opid);
	}
	return rc;
}
