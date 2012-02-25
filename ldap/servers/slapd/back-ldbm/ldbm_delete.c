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
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

 
/* delete.c - ldbm backend delete routine */

#include "back-ldbm.h"

#define DEL_SET_ERROR(rc, error, count)                                        \
{                                                                              \
    (rc) = (error);                                                            \
    (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
}

int
ldbm_back_delete( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst = NULL;
	struct ldbminfo	*li = NULL;
	struct backentry *e = NULL;
	struct backentry *tombstone = NULL;
	struct backentry *original_entry = NULL;
	char *dn = NULL;
	back_txn txn;
	back_txnid parent_txn;
	int retval = -1;
	char *msg;
	char *errbuf = NULL;
	int retry_count = 0;
	int disk_full = 0;
	int parent_found = 0;
	int ruv_c_init = 0;
	modify_context parent_modify_c = {0};
	modify_context ruv_c = {0};
	int rc = 0;
	int ldap_result_code= LDAP_SUCCESS;
	char *ldap_result_message= NULL;
	Slapi_DN sdn;
	Slapi_DN *sdnp = NULL;
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
	char *entryusn_str = NULL;
	char *prev_entryusn_str = NULL;
	Slapi_Entry *orig_entry = NULL;
	Slapi_DN parentsdn;

	slapi_pblock_get( pb, SLAPI_BACKEND, &be);
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_DELETE_TARGET_SDN, &sdnp );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr);
	slapi_pblock_get( pb, SLAPI_TXN, (void**)&parent_txn );
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation );
	
	/* sdn needs to be initialized before "goto *_return */
	slapi_sdn_init(&sdn);

	/* dblayer_txn_init needs to be called before "goto error_return" */
	dblayer_txn_init(li,&txn);
	/* the calls to perform searches require the parent txn if any
	   so set txn to the parent_txn until we begin the child transaction */
	txn.back_txn_txn = parent_txn;

	if (pb->pb_conn)
	{
		slapi_log_error (SLAPI_LOG_TRACE, "ldbm_back_delete", "enter conn=%" NSPRIu64 " op=%d\n", pb->pb_conn->c_connid, operation->o_opid);
	}

	if (NULL == addr)
	{
		/* retval is -1 */
		goto error_return;
	}
	ldap_result_code = slapi_dn_syntax_check(pb, slapi_sdn_get_dn(sdnp), 1);
	if (ldap_result_code)
	{
		ldap_result_code = LDAP_INVALID_DN_SYNTAX;
		slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
		/* retval is -1 */
		goto error_return;
	}

	is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
	is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
	delete_tombstone_entry = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_ENTRY);
	
	inst = (ldbm_instance *) be->be_instance_info;

	if (NULL == sdnp) {
		slapi_sdn_init_normdn_byref(&sdn, dn);
		sdnp = &sdn;
	}

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
		/* retval is -1 */
		goto error_return;
	}

	/* find and lock the entry we are about to modify */
	if ( (e = find_entry2modify( pb, be, addr, &txn )) == NULL )
	{
		ldap_result_code= LDAP_NO_SUCH_OBJECT; 
		/* retval is -1 */
		goto error_return; /* error result sent by find_entry2modify() */
	}

	if ( slapi_entry_has_children( e->ep_entry ) )
	{
		ldap_result_code= LDAP_NOT_ALLOWED_ON_NONLEAF;
		/* retval is -1 */
		goto error_return;
	}

	/* set entry in case be-preop plugins need to work on it (e.g., USN) */
	slapi_pblock_get( pb, SLAPI_DELETE_BEPREOP_ENTRY, &orig_entry );
	slapi_pblock_set( pb, SLAPI_DELETE_BEPREOP_ENTRY, e->ep_entry );

	/* Don't call pre-op for Tombstone entries */
	if (!delete_tombstone_entry)
	{
		int rc = 0;
		/* 
		 * Some present state information is passed through the PBlock to the
		 * backend pre-op plugin. To ensure a consistent snapshot of this state
		 * we wrap the reading of the entry with the dblock.
		 */
		ldap_result_code= get_copy_of_entry(pb, addr, &txn,
						SLAPI_DELETE_EXISTING_ENTRY, !is_replicated_operation);
		if(ldap_result_code==LDAP_OPERATIONS_ERROR ||
		   ldap_result_code==LDAP_INVALID_DN_SYNTAX)
		{
			/* restore original entry so the front-end delete code can free it */
			slapi_pblock_set( pb, SLAPI_DELETE_BEPREOP_ENTRY, orig_entry );
			/* retval is -1 */
			goto error_return;
		}
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);

		rc = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_DELETE_FN);

		if (rc == -1)
		{
			/* 
			 * Plugin indicated some kind of failure,
			 * or that this Operation became a No-Op.
			 */
			slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			/* restore original entry so the front-end delete code can free it */
			slapi_pblock_set( pb, SLAPI_DELETE_BEPREOP_ENTRY, orig_entry );
			/* retval is -1 */
			goto error_return;
		}
		/* the flag could be set in a preop plugin (e.g., USN) */
		delete_tombstone_entry = operation_is_flag_set(operation,
									OP_FLAG_TOMBSTONE_ENTRY);
	}

	slapi_pblock_set( pb, SLAPI_DELETE_BEPREOP_ENTRY, orig_entry );

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
		/* If both USN and replication is enabled, csn set by replication 
		 * should be honored. */
		if ((opcsn == NULL || ldbm_usn_enabled(be)) &&
						!is_fixup_operation && operation->o_csngen_handler)
		{
			csn_free(&opcsn); /* free opcsn set by USN plugin, if any */
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
		/* retval is -1 */
		goto error_return;
	}

	/*
	 * Get the entry's parent. We do this here because index_read
	 * seems to deadlock the database when dblayer_txn_begin is
	 * called.
	 */
	slapi_sdn_init(&parentsdn);
	slapi_sdn_get_backend_parent_ext(sdnp, &parentsdn, pb->pb_backend, is_tombstone_entry);
	if ( !slapi_sdn_isempty(&parentsdn) )
	{
		struct backentry *parent = NULL;
		entry_address parent_addr;

		parent_addr.sdn = &parentsdn;
		parent_addr.uniqueid = NULL;
		parent = find_entry2modify_only_ext(pb, be, &parent_addr,
		                                    TOMBSTONE_INCLUDED, &txn);
		if (NULL != parent) {
			int isglue;
			size_t haschildren = 0;
			int op = PARENTUPDATE_DEL;

			/* Unfortunately findentry doesn't tell us whether it just 
			 * didn't find the entry, or if there was an error, so we 
			 * have to assume that the parent wasn't found */
			parent_found = 1;

			/* Modify the parent in memory */
			modify_init(&parent_modify_c,parent);
			if (create_tombstone_entry) {
				op |= PARENTUPDATE_CREATE_TOMBSTONE;
			} else if (delete_tombstone_entry) {
				op |= PARENTUPDATE_DELETE_TOMBSTONE;
			}
			retval = parent_update_on_childchange(&parent_modify_c,
			                                      op, &haschildren);
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
		if (entryrdn_get_switch()) /* subtree-rename: on */
		{
			Slapi_RDN *srdn = slapi_entry_get_srdn(tombstone->ep_entry);
			char *tombstone_rdn =
			 compute_entry_tombstone_rdn(slapi_entry_get_rdn_const(e->ep_entry),
			 childuniqueid);
			/* e_srdn has "uniaqueid=..., <ORIG RDN>" */
			slapi_rdn_replace_rdn(srdn, tombstone_rdn);
			slapi_ch_free_string(&tombstone_rdn);
		}
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
		
		/* retrieve previous entry usn value, if any */
		prev_entryusn_str = slapi_entry_attr_get_charptr(tombstone->ep_entry,
												SLAPI_ATTR_ENTRYUSN_PREV);
		if (prev_entryusn_str) {
			/* discard the previous value from the tombstone entry */
		    retval = slapi_entry_delete_string(tombstone->ep_entry,
							SLAPI_ATTR_ENTRYUSN_PREV, prev_entryusn_str);
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE,
							"delete (deleting %s) failed, err=%d\n",
							SLAPI_ATTR_ENTRYUSN, retval, 0) ;
			}
		}

		/* XXXggood above used to be: slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE); */
		/* JCMREPL - Add a description of what's going on? */
	}

	if (!is_ruv && !is_fixup_operation && !delete_tombstone_entry) {
		ruv_c_init = ldbm_txn_ruv_modify_context( pb, &ruv_c );
		if (-1 == ruv_c_init) {
			LDAPDebug( LDAP_DEBUG_ANY,
				"ldbm_back_delete: ldbm_txn_ruv_modify_context "
				"failed to construct RUV modify context\n",
				0, 0, 0);
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			retval = 0;
			goto error_return;
		}
	}

	if ( (original_entry = backentry_dup( e )) == NULL ) {
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	/*
	 * So, we believe that no code up till here actually added anything
	 * to the persistent store. From now on, we're transacted
	 */
	
	txn.back_txn_txn = NULL; /* ready to create the child transaction */
	for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
		if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
			dblayer_txn_abort(li,&txn);

			backentry_free(&e);
			slapi_pblock_set( pb, SLAPI_DELETE_EXISTING_ENTRY, original_entry->ep_entry );
			e = original_entry;
			if ( (original_entry = backentry_dup( e )) == NULL ) {
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
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

		/* stash the transaction */
		slapi_pblock_set(pb, SLAPI_TXN, txn.back_txn_txn);

		/* call the transaction pre delete plugins just after creating the transaction */
		if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN))) {
			LDAPDebug1Arg( LDAP_DEBUG_ANY, "SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN plugin "
						   "returned error code %d\n", retval );
			slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			goto error_return;
		}

		if(create_tombstone_entry)
		{
			/*
			 * The entry is not removed from the disk when we tombstone an
			 * entry. We change the DN, add objectclass=tombstone, and record
			 * the UniqueID of the parent entry.
			 */
			/* Note: cache_add (tombstone) fails since the original entry having
			 * the same ID is already in the cache.  Thus, we have to add it
			 * tentatively for now, then cache_add again when the original
			 * entry is removed from the cache.
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
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}
			if (cache_add_tentative( &inst->inst_cache, tombstone, NULL) == 0) {
				tombstone_in_cache = 1;
			} else if (!(tombstone->ep_state & ENTRY_STATE_NOTINCACHE)) {
			    LDAPDebug1Arg(LDAP_DEBUG_CACHE,
			                  "id2entry_add tombstone (%s) is in cache\n",
			                  slapi_entry_get_dn(tombstone->ep_entry));
			    tombstone_in_cache = 1;
			}
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
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
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
			DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
			goto error_return;
		}
		if(create_tombstone_entry)
		{
			/*
			 * The tombstone entry is removed from all attribute indexes
			 * above, but we want it to remain in the nsUniqueID and nscpEntryDN indexes
			 * and for objectclass=tombstone.
			 */
			retval = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS, 
							SLAPI_ATTR_VALUE_TOMBSTONE,
							tombstone->ep_id,BE_INDEX_ADD, &txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS,
							"delete (adding %s) DB_LOCK_DEADLOCK\n",
							SLAPI_ATTR_VALUE_TOMBSTONE, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE,
							"delete (adding %s) failed, err=%d %s\n",
							SLAPI_ATTR_VALUE_TOMBSTONE, retval,
							(msg = dblayer_strerror( retval )) ? msg : "" );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}
			retval = index_addordel_string(be, SLAPI_ATTR_UNIQUEID,
							slapi_entry_get_uniqueid(tombstone->ep_entry),
							tombstone->ep_id,BE_INDEX_ADD,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS,
							"delete (adding %s) DB_LOCK_DEADLOCK\n",
							SLAPI_ATTR_UNIQUEID, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE,
							"delete (adding %s) failed, err=%d %s\n",
							SLAPI_ATTR_UNIQUEID, retval,
							(msg = dblayer_strerror( retval )) ? msg : "" );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}
			retval = index_addordel_string(be, SLAPI_ATTR_NSCP_ENTRYDN,
							slapi_sdn_get_ndn(nscpEntrySDN),
							tombstone->ep_id, BE_INDEX_ADD, &txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS,
							"delete (adding %s) DB_LOCK_DEADLOCK\n",
							SLAPI_ATTR_NSCP_ENTRYDN, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, 
							"delete (adding %s) failed, err=%d %s\n",
							SLAPI_ATTR_NSCP_ENTRYDN, retval,
							(msg = dblayer_strerror( retval )) ? msg : "" );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}
			/* add a new usn to the entryusn index */
			entryusn_str = slapi_entry_attr_get_charptr(tombstone->ep_entry,
												SLAPI_ATTR_ENTRYUSN);
			if (entryusn_str) {
				retval = index_addordel_string(be, SLAPI_ATTR_ENTRYUSN,
							entryusn_str, tombstone->ep_id, BE_INDEX_ADD, &txn);
				slapi_ch_free_string(&entryusn_str);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug( LDAP_DEBUG_ARGS,
								"delete (adding %s) DB_LOCK_DEADLOCK\n",
								SLAPI_ATTR_ENTRYUSN, 0, 0 );
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug( LDAP_DEBUG_TRACE, 
								"delete (adding %s) failed, err=%d %s\n",
								SLAPI_ATTR_ENTRYUSN, retval,
								(msg = dblayer_strerror( retval )) ? msg : "" );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					DEL_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
					goto error_return;
				}
			}
			/* delete a previous value (if it exists) from the entryusn index */
			if (prev_entryusn_str) {
				retval = index_addordel_string(be, SLAPI_ATTR_ENTRYUSN,
								prev_entryusn_str, tombstone->ep_id,
								BE_INDEX_DEL|BE_INDEX_EQUALITY, &txn);
				slapi_ch_free_string(&prev_entryusn_str);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug( LDAP_DEBUG_ARGS,
								"delete (deleting %s) DB_LOCK_DEADLOCK\n",
								SLAPI_ATTR_ENTRYUSN, 0, 0 );
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug( LDAP_DEBUG_TRACE, 
								"delete (deleting %s) failed, err=%d %s\n",
								SLAPI_ATTR_ENTRYUSN, retval,
								(msg = dblayer_strerror( retval )) ? msg : "" );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					DEL_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
					goto error_return;
				}
			}
			if (entryrdn_get_switch()) /* subtree-rename: on */
			{
				Slapi_Attr *attr;
				Slapi_Value **svals;
				/* To maintain tombstonenumsubordinates,
				 * parentid is needed for tombstone, as well. */
				slapi_entry_attr_find(tombstone->ep_entry, LDBM_PARENTID_STR,
                                      &attr);
				if (attr) {
					svals = attr_get_present_values(attr);
					retval = index_addordel_values_sv(be, LDBM_PARENTID_STR, 
					                                  svals, NULL, e->ep_id, 
					                                  BE_INDEX_ADD, &txn);
					if ( retval ) {
						LDAPDebug( LDAP_DEBUG_TRACE, 
								"delete (deleting %s) failed, err=%d %s\n",
								LDBM_PARENTID_STR, retval,
								(msg = dblayer_strerror( retval )) ? msg : "" );
						if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
						DEL_SET_ERROR(ldap_result_code, 
						              LDAP_OPERATIONS_ERROR, retry_count);
						goto error_return;
					}
				}
				entryrdn_index_entry(be, e, BE_INDEX_DEL, &txn);
				retval =
				        entryrdn_index_entry(be, tombstone, BE_INDEX_ADD, &txn);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug0Args( LDAP_DEBUG_ARGS,
								"delete (adding entryrdn) DB_LOCK_DEADLOCK\n");
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug2Args( LDAP_DEBUG_TRACE, 
								"delete (adding entryrdn) failed, err=%d %s\n",
								retval,
								(msg = dblayer_strerror( retval )) ? msg : "" );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					DEL_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
					goto error_return;
				}
			}
		} /* create_tombstone_entry */
		else if (delete_tombstone_entry)
		{
			/* 
			 * We need to remove the Tombstone entry from the remaining indexes:
			 * objectclass=nsTombstone, nsUniqueID, nscpEntryDN
			 */
			char *nscpedn = NULL;

			retval = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS,
							SLAPI_ATTR_VALUE_TOMBSTONE, e->ep_id,
							BE_INDEX_DEL, &txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS,
							"delete (deleting %s) DB_LOCK_DEADLOCK\n",
							SLAPI_ATTR_VALUE_TOMBSTONE, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE,
							"delete (deleting %s) failed, err=%d %s\n",
							SLAPI_ATTR_VALUE_TOMBSTONE, retval,
							(msg = dblayer_strerror( retval )) ? msg : "" );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}
			retval = index_addordel_string(be, SLAPI_ATTR_UNIQUEID,
							slapi_entry_get_uniqueid(e->ep_entry),
							e->ep_id, BE_INDEX_DEL, &txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS,
							"delete (deleting %s) DB_LOCK_DEADLOCK\n",
							SLAPI_ATTR_UNIQUEID, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE,
							"delete (deleting %s) failed, err=%d %s\n",
							SLAPI_ATTR_UNIQUEID, retval,
							(msg = dblayer_strerror( retval )) ? msg : "" );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}

			nscpedn = slapi_entry_attr_get_charptr(e->ep_entry,
												SLAPI_ATTR_NSCP_ENTRYDN);
			if (nscpedn) {
				retval = index_addordel_string(be, SLAPI_ATTR_NSCP_ENTRYDN,
								nscpedn, e->ep_id, BE_INDEX_DEL, &txn);
				slapi_ch_free((void **)&nscpedn);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug( LDAP_DEBUG_ARGS,
								"delete (deleting %s) DB_LOCK_DEADLOCK\n",
								SLAPI_ATTR_NSCP_ENTRYDN, 0, 0 );
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug( LDAP_DEBUG_TRACE,
								"delete (deleting %s) failed, err=%d %s\n",
								SLAPI_ATTR_NSCP_ENTRYDN, retval,
								(msg = dblayer_strerror( retval )) ? msg : "" );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					DEL_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
					goto error_return;
				}
			}
			/* delete usn from the entryusn index */
			entryusn_str = slapi_entry_attr_get_charptr(e->ep_entry,
												SLAPI_ATTR_ENTRYUSN);
			if (entryusn_str) {
				retval = index_addordel_string(be, SLAPI_ATTR_ENTRYUSN,
							entryusn_str, e->ep_id,
							BE_INDEX_DEL|BE_INDEX_EQUALITY, &txn);
				slapi_ch_free_string(&entryusn_str);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug( LDAP_DEBUG_ARGS,
								"delete (deleting %s) DB_LOCK_DEADLOCK\n",
								SLAPI_ATTR_ENTRYUSN, 0, 0 );
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug( LDAP_DEBUG_TRACE, 
								"delete (deleting %s) failed, err=%d %s\n",
								SLAPI_ATTR_ENTRYUSN, retval,
								(msg = dblayer_strerror( retval )) ? msg : "" );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					DEL_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
					goto error_return;
				}
			}
			if (entryrdn_get_switch()) /* subtree-rename: on */
			{
				retval = entryrdn_index_entry(be, e, BE_INDEX_DEL, &txn);
				if (DB_LOCK_DEADLOCK == retval) {
					LDAPDebug0Args( LDAP_DEBUG_ARGS,
							"delete (deleting entryrdn) DB_LOCK_DEADLOCK\n");
					/* Retry txn */
					continue;
				}
				if (0 != retval) {
					LDAPDebug2Args( LDAP_DEBUG_TRACE, 
							"delete (deleting entryrdn) failed, err=%d %s\n",
							retval,
							(msg = dblayer_strerror( retval )) ? msg : "" );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					DEL_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
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
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
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
				DEL_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
			}
		}

		if (ruv_c_init) {
			retval = modify_update_all( be, pb, &ruv_c, &txn );
			if (DB_LOCK_DEADLOCK == retval) {
				/* Abort and re-try */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_ANY,
					"modify_update_all failed, err=%d %s\n", retval,
					(msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval))
					disk_full = 1;
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
		retval = -1;
		goto error_return;
	}

	/* call the transaction post delete plugins just before the commit */
	if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN))) {
		LDAPDebug1Arg( LDAP_DEBUG_ANY, "SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN plugin "
					   "returned error code %d\n", retval );
		slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
		goto error_return;
	}

	retval = dblayer_txn_commit(li,&txn);
	/* after commit - txn is no longer valid - replace SLAPI_TXN with parent */
	txn.back_txn_txn = NULL;
	slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
	if (0 != retval)
	{
		if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	/* delete from cache and clean up */
	CACHE_REMOVE(&inst->inst_cache, e);
	cache_unlock_entry( &inst->inst_cache, e );
	CACHE_RETURN( &inst->inst_cache, &e );
	if (tombstone_in_cache) {
		if (CACHE_ADD( &inst->inst_cache, tombstone, NULL ) == 0) {
			tombstone_in_cache = 1;
		} else {
			tombstone_in_cache = 0;
		}
	}
	if (parent_found)
	{
		 /* Replace the old parent entry with the newly modified one */
		modify_switch_entries( &parent_modify_c,be);
	}
	
	if (ruv_c_init) {
		if (modify_switch_entries(&ruv_c, be) != 0 ) {
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			LDAPDebug( LDAP_DEBUG_ANY,
				"ldbm_back_delete: modify_switch_entries failed\n", 0, 0, 0);
			retval = -1;
			goto error_return;
		}
	}

	rc= 0;
	goto common_return;

error_return:
	if (tombstone_in_cache)
	{
		CACHE_REMOVE( &inst->inst_cache, tombstone );
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

	/* It is safer not to abort when the transaction is not started. */
	if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
		dblayer_txn_abort(li,&txn); /* abort crashes in case disk full */
		/* txn is no longer valid - reset the txn pointer to the parent */
		txn.back_txn_txn = NULL;
		slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
	}
	
common_return:
	if (tombstone_in_cache)
	{
		CACHE_RETURN( &inst->inst_cache, &tombstone );
	}
	else
	{
		backentry_free( &tombstone );
	}
	
	/* result code could be used in the bepost plugin functions. */
	slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
	/*
	 * The bepostop is called even if the operation fails,
	 * but not if the operation is purging tombstones.
	 */
	if (!delete_tombstone_entry) {
		if (e) {
			/* set entry in case be-postop plugins need to work on it
			 * (e.g., USN) */
			slapi_pblock_get( pb, SLAPI_DELETE_BEPOSTOP_ENTRY, &orig_entry );
			slapi_pblock_set( pb, SLAPI_DELETE_BEPOSTOP_ENTRY, e->ep_entry );
		}
		plugin_call_plugins (pb, SLAPI_PLUGIN_BE_POST_DELETE_FN);
		/* set original entry back */
		if (e) {
			slapi_pblock_set( pb, SLAPI_DELETE_BEPOSTOP_ENTRY, orig_entry );
		}
	}

	/* Need to return to cache after post op plugins are called */
	if (retval) { /* error case */
		if (e) {
			cache_unlock_entry( &inst->inst_cache, e );
			CACHE_RETURN( &inst->inst_cache, &e );
		}
	}
	
	if (ruv_c_init) {
		modify_term(&ruv_c, be);
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
	backentry_free(&original_entry);
	slapi_ch_free((void**)&errbuf);
	slapi_sdn_done(&sdn);
	slapi_ch_free_string(&e_uniqueid);
	if (pb->pb_conn)
	{
		slapi_log_error (SLAPI_LOG_TRACE, "ldbm_back_delete", "leave conn=%" NSPRIu64 " op=%d\n", pb->pb_conn->c_connid, operation->o_opid);
	}
	return rc;
}
