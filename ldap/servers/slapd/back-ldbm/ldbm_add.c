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


/* add.c - ldap ldbm back-end add routine */

#include "back-ldbm.h"

extern char *numsubordinates;
extern char *hassubordinates;

static void delete_update_entrydn_operational_attributes(struct backentry *ep);

#define ADD_SET_ERROR(rc, error, count)                                        \
{                                                                              \
    (rc) = (error);                                                            \
    (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
}

/* in order to find the parent, we must have either the parent dn or uniqueid
   This function will return true if either are set, or false otherwise */
static int
have_parent_address(const Slapi_DN *parentsdn, const char *parentuniqueid)
{
	if (parentuniqueid && parentuniqueid[0]) {
		return 1; /* have parent uniqueid */
	}

	if (parentsdn && !slapi_sdn_isempty(parentsdn)) {
		return 1; /* have parent dn */
	}

	return 0; /* have no address */
}

int
ldbm_back_add( Slapi_PBlock *pb )
{
	backend *be;
	struct ldbminfo *li;
	ldbm_instance *inst = NULL;
	const char *dn = NULL;
	Slapi_Entry	*e = NULL;
	struct backentry *tombstoneentry = NULL;
	struct backentry *addingentry = NULL;
	struct backentry *parententry = NULL;
	struct backentry *originalentry = NULL;
	ID pid;
	int	isroot;
	char *errbuf= NULL;
	back_txn txn;
	back_txnid parent_txn;
	int retval = -1;
	char *msg;
	int	managedsait;
	int	ldap_result_code = LDAP_SUCCESS;
	char *ldap_result_message= NULL;
	char *ldap_result_matcheddn= NULL;
	int	retry_count = 0;
	int	disk_full = 0;
	modify_context parent_modify_c = {0};
	modify_context ruv_c = {0};
	int parent_found = 0;
	int ruv_c_init = 0;
	int rc = 0;
	int addingentry_id_assigned= 0;
	int addingentry_in_cache= 0;
	int tombstone_in_cache= 0;
	Slapi_DN *sdn = NULL;
	Slapi_DN parentsdn;
	Slapi_Operation *operation;
	int is_replicated_operation= 0;
	int is_resurect_operation= 0;
	int is_tombstone_operation= 0;
	int is_fixup_operation= 0;
	int is_ruv = 0;				 /* True if the current entry is RUV */
	CSN *opcsn = NULL;
	entry_address addr = {0};
	int not_an_error = 0;

	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &e );
	slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
	slapi_pblock_get( pb, SLAPI_MANAGEDSAIT, &managedsait );
	slapi_pblock_get( pb, SLAPI_TXN, (void**)&parent_txn );
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation );
	slapi_pblock_get( pb, SLAPI_BACKEND, &be);

	is_resurect_operation= operation_is_flag_set(operation,OP_FLAG_RESURECT_ENTRY);
	is_tombstone_operation= operation_is_flag_set(operation,OP_FLAG_TOMBSTONE_ENTRY);
	is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
	is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);

	inst = (ldbm_instance *) be->be_instance_info;
	if (inst && inst->inst_ref_count) {
		slapi_counter_increment(inst->inst_ref_count);
	} else {
		LDAPDebug1Arg(LDAP_DEBUG_ANY,
		              "ldbm_add: instance \"%s\" does not exist.\n",
		              inst ? inst->inst_name : "null instance");
		goto error_return;
	}

	/* sdn & parentsdn need to be initialized before "goto *_return" */
	slapi_sdn_init(&parentsdn);
	
	/* Get rid of ldbm backend attributes that you are not allowed to specify yourself */
	slapi_entry_delete_values( e, hassubordinates, NULL );
	slapi_entry_delete_values( e, numsubordinates, NULL );

	dblayer_txn_init(li,&txn);
	/* the calls to perform searches require the parent txn if any
	   so set txn to the parent_txn until we begin the child transaction */
	if (parent_txn) {
		txn.back_txn_txn = parent_txn;
	} else {
		parent_txn = txn.back_txn_txn;
		slapi_pblock_set( pb, SLAPI_TXN, parent_txn );
	}

	/* The dblock serializes writes to the database,
	 * which reduces deadlocking in the db code,
	 * which means that we run faster.
	 *
	 * But, this lock is re-enterant for the fixup
	 * operations that the URP code in the Replication
	 * plugin generates.
	 *
	 * SERIALLOCK is moved to dblayer_txn_begin along with exposing be
	 * transaction to plugins (see slapi_back_transaction_* APIs).
	 *
	if(SERIALLOCK(li) && !is_fixup_operation)
	{
		dblayer_lock_backend(be);
		dblock_acquired= 1;
	}
	 */

	rc= 0;

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
		ldap_result_code = -1; /* needs to distinguish from "success" */
		goto error_return;
	}


	/*
	 * Originally (in the U-M LDAP 3.3 code), there was a comment near this
	 * code about a race condition.  The race was that a 2nd entry could be
	 * added between the time when we check for an already existing entry
	 * and the cache_add_entry_lock() call below.  A race condition no
	 * longer exists, because now we keep the parent entry locked for
	 * the duration of the old race condition's window of opportunity.
	 */

	/*
	 * Use transaction as a backend lock, which should be called 
	 * outside of entry lock -- find_entry* / cache_lock_entry
	 * to avoid deadlock.
	 */
	txn.back_txn_txn = NULL; /* ready to create the child transaction */
	for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
		if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
			/* Don't release SERIAL LOCK */
			dblayer_txn_abort_ext(li, &txn, PR_FALSE); 
			slapi_pblock_set(pb, SLAPI_TXN, parent_txn);

			if (addingentry_in_cache) {
				/* addingentry is in cache.  Remove it once. */
				CACHE_REMOVE(&inst->inst_cache, addingentry);
				CACHE_RETURN(&inst->inst_cache, &addingentry);
			} else {
				backentry_free(&addingentry);
			}
			slapi_pblock_set( pb, SLAPI_ADD_ENTRY, originalentry->ep_entry );
			addingentry = originalentry;
			if ( (originalentry = backentry_dup( addingentry )) == NULL ) {
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
			if (addingentry_in_cache) {
				/* Adding the resetted addingentry to the cache. */
				if (cache_add_tentative(&inst->inst_cache,
				                        addingentry, NULL) != 0) {
					LDAPDebug0Args(LDAP_DEBUG_CACHE,
					              "cache_add_tentative concurrency detected\n");
					ldap_result_code = LDAP_ALREADY_EXISTS;
					goto error_return;
				}
			}
			if (ruv_c_init) {
				/* reset the ruv txn stuff */
				modify_term(&ruv_c, be);
				ruv_c_init = 0;
			}

			/* We're re-trying */
			LDAPDebug0Args(LDAP_DEBUG_BACKLDBM, "Add Retrying Transaction\n");
#ifndef LDBM_NO_BACKOFF_DELAY
			{
				PRIntervalTime interval;
				interval = PR_MillisecondsToInterval(slapi_rand() % 100);
				DS_Sleep(interval);
			}
#endif
		}
		/* dblayer_txn_begin holds SERIAL lock, 
		 * which should be outside of locking the entry (find_entry2modify) */
		if (0 == retry_count) {
			/* First time, hold SERIAL LOCK */
			retval = dblayer_txn_begin(be, parent_txn, &txn);

			if (!is_tombstone_operation) {
				rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			}

			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY);
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
			while(rc!=0)
			{
				/* JCM - copying entries can be expensive... should optimize */
				/* 
				 * Some present state information is passed through the PBlock to the
				 * backend pre-op plugin. To ensure a consistent snapshot of this state
				 * we wrap the reading of the entry with the dblock.
				 */
				if(slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY))
				{
					/* Check if an entry with the intended uniqueid already exists. */
					done_with_pblock_entry(pb,SLAPI_ADD_EXISTING_UNIQUEID_ENTRY); /* Could be through this multiple times */
					addr.udn = NULL;
					addr.sdn = NULL;
					addr.uniqueid = (char*)slapi_entry_get_uniqueid(e); /* jcm -  cast away const */
					ldap_result_code= get_copy_of_entry(pb, &addr, &txn, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY, !is_replicated_operation);
				}
				if(slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY))
				{
					slapi_pblock_get( pb, SLAPI_ADD_TARGET_SDN, &sdn );
					if (NULL == sdn)
					{
						LDAPDebug0Args(LDAP_DEBUG_ANY,
						               "ldbm_back_add: Null target dn\n");
						goto error_return;
					}

					/* not need to check the dn syntax as this is a replicated op */
					if(!is_replicated_operation){
						dn = slapi_sdn_get_dn(sdn);
						ldap_result_code = slapi_dn_syntax_check(pb, dn, 1);
						if (ldap_result_code)
						{
							ldap_result_code = LDAP_INVALID_DN_SYNTAX;
							slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
							goto error_return;
						}
					}

					slapi_sdn_get_backend_parent(sdn, &parentsdn, pb->pb_backend);
					/* Check if an entry with the intended DN already exists. */
					done_with_pblock_entry(pb,SLAPI_ADD_EXISTING_DN_ENTRY); /* Could be through this multiple times */
					addr.sdn = sdn;
					addr.udn = NULL;
					addr.uniqueid = NULL;
					ldap_result_code= get_copy_of_entry(pb, &addr, &txn, SLAPI_ADD_EXISTING_DN_ENTRY, !is_replicated_operation);
					if(ldap_result_code==LDAP_OPERATIONS_ERROR ||
					   ldap_result_code==LDAP_INVALID_DN_SYNTAX)
					{
					    goto error_return;
					}
				}
				/* if we can find the parent by dn or uniqueid, and the operation has requested the parent
				   then get it */
				if(have_parent_address(&parentsdn, operation->o_params.p.p_add.parentuniqueid) &&
				   slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY))
				{
					done_with_pblock_entry(pb,SLAPI_ADD_PARENT_ENTRY); /* Could be through this multiple times */
					addr.sdn = &parentsdn;
					addr.udn = NULL;
					addr.uniqueid = operation->o_params.p.p_add.parentuniqueid;
					ldap_result_code= get_copy_of_entry(pb, &addr, &txn, SLAPI_ADD_PARENT_ENTRY, !is_replicated_operation);
					/* need to set parentsdn or parentuniqueid if either is not set? */
				}

				/* Call the Backend Pre Add plugins */
				slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
				rc= plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_ADD_FN);
				if (rc < 0) {
					int opreturn = 0;
					if (SLAPI_PLUGIN_NOOP == rc) {
						not_an_error = 1;
						rc = LDAP_SUCCESS;
					}
					/* 
					 * Plugin indicated some kind of failure,
					 * or that this Operation became a No-Op.
					 */
					if (!ldap_result_code) {
						slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
					}
					if (!ldap_result_code) {
						LDAPDebug0Args(LDAP_DEBUG_ANY,
							       "ldbm_back_add: SLAPI_PLUGIN_BE_PRE_ADD_FN returned error but did not set SLAPI_RESULT_CODE\n");
						ldap_result_code = LDAP_OPERATIONS_ERROR;
					}
					slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
					if (!opreturn) {
						/* make sure opreturn is set for the postop plugins */
						slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &rc);
					}
					slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
					goto error_return;
				}
				/*
				 * (rc!=-1 && rc!= 0) means that the plugin changed things, so we go around
				 * the loop once again to get the new present state.
				 */
				/* JCMREPL - Warning: A Plugin could cause an infinite loop by always returning a result code that requires some action. */
			}
		} else {
			/* Otherwise, no SERIAL LOCK */
			retval = dblayer_txn_begin_ext(li, parent_txn, &txn, PR_FALSE);
		}
		if (0 != retval) {
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
				disk_full = 1;
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto diskfull_return;
			}
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			goto error_return; 
		}

		/* stash the transaction for plugins */
		slapi_pblock_set(pb, SLAPI_TXN, txn.back_txn_txn);

		if (0 == retry_count) { /* execute just once */
			/* Nothing in this if crause modifies persistent store.
			 * it's called just once. */
			/* 
			 * Fetch the parent entry and acquire the cache lock.
			 */
			if(have_parent_address(&parentsdn, operation->o_params.p.p_add.parentuniqueid))
			{
				addr.sdn = &parentsdn;
				addr.udn = NULL;
				addr.uniqueid = operation->o_params.p.p_add.parentuniqueid;
				parententry = find_entry2modify_only(pb,be,&addr,&txn);
				if (parententry && parententry->ep_entry) {
					if (!operation->o_params.p.p_add.parentuniqueid){
						/* Set the parentuniqueid now */
						operation->o_params.p.p_add.parentuniqueid = 
						    slapi_ch_strdup(slapi_entry_get_uniqueid(parententry->ep_entry));
					}
					if (slapi_sdn_isempty(&parentsdn)) {
						/* Set the parentsdn now */
						slapi_sdn_set_dn_byval(&parentsdn, slapi_entry_get_dn_const(parententry->ep_entry));
					}
				}
				modify_init(&parent_modify_c,parententry);
			}

			/* Check if the entry we have been asked to add already exists */
			{
				Slapi_Entry *entry;
				slapi_pblock_get( pb, SLAPI_ADD_EXISTING_DN_ENTRY, &entry);
				if ( entry != NULL )
				{
					/* The entry already exists */ 
					ldap_result_code= LDAP_ALREADY_EXISTS;
					goto error_return;
				} 
				else 
				{
					/*
					 * did not find the entry - this is good, since we're
					 * trying to add it, but we have to check whether the
					 * entry we did match has a referral we should return
					 * instead. we do this only if managedsait is not on.
					 */
					if ( !managedsait && !is_tombstone_operation )
					{
						int err= 0;
						Slapi_DN ancestorsdn;
						struct backentry *ancestorentry;
						slapi_sdn_init(&ancestorsdn);
						ancestorentry= dn2ancestor(pb->pb_backend,sdn,&ancestorsdn,&txn,&err);
						slapi_sdn_done(&ancestorsdn);
						if ( ancestorentry != NULL )
						{
							int sentreferral = 
							    check_entry_for_referral(pb, ancestorentry->ep_entry,
							                             backentry_get_ndn(ancestorentry), "ldbm_back_add");
							CACHE_RETURN( &inst->inst_cache, &ancestorentry );
							if(sentreferral)
							{
								ldap_result_code= -1; /* The result was sent by check_entry_for_referral */
								goto error_return;
							}
						}
					}
				}
			}

			/* no need to check the schema as this is a replication add */
			if(!is_replicated_operation){
				if ((operation_is_flag_set(operation,OP_FLAG_ACTION_SCHEMA_CHECK))
				     && (slapi_entry_schema_check(pb, e) != 0))
			{
				LDAPDebug(LDAP_DEBUG_TRACE, "entry failed schema check\n", 0, 0, 0);
				ldap_result_code = LDAP_OBJECT_CLASS_VIOLATION;
				slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
				goto error_return;
			}

			/* Check attribute syntax */
			if (slapi_entry_syntax_check(pb, e, 0) != 0)
			{
				LDAPDebug(LDAP_DEBUG_TRACE, "entry failed syntax check\n", 0, 0, 0);
				ldap_result_code = LDAP_INVALID_SYNTAX;
				slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
				goto error_return;
			}
		}

			opcsn = operation_get_csn (operation);
			if(is_resurect_operation)
			{
				char *reason = NULL;
				/*
				 * When we resurect a tombstone we must use its UniqueID
				 * to find the tombstone entry and lock it down in the cache.
				 */
				addr.udn = NULL;
				addr.sdn = NULL;
				addr.uniqueid = (char *)slapi_entry_get_uniqueid(e); /* jcm - cast away const */
				tombstoneentry = find_entry2modify( pb, be, &addr, &txn );
				if ( tombstoneentry==NULL )
				{
					ldap_result_code= -1;
					goto error_return;	  /* error result sent by find_entry2modify() */
				}
				tombstone_in_cache = 1;

				addingentry = backentry_dup( tombstoneentry );
				if ( addingentry==NULL )
				{
					ldap_result_code= LDAP_OPERATIONS_ERROR;
					goto error_return;
				}
				/*
				 * To resurect a tombstone we must fix its DN and remove the
				 * parent UniqueID that we stashed in there.
				 *
				 * The entry comes back to life as a Glue entry, so we add the
				 * magic objectclass.
				 */
				if (NULL == sdn) {
					LDAPDebug0Args(LDAP_DEBUG_ANY, "ldbm_back_add: Null target dn\n");
					goto error_return;
				}
				dn = slapi_sdn_get_dn(sdn);
				slapi_entry_set_sdn(addingentry->ep_entry, sdn); /* The DN is passed into the entry. */
				/* LPREPL: the DN is normalized...Somehow who should get a not normalized one */
				addingentry->ep_id = slapi_entry_attr_get_ulong(addingentry->ep_entry,"entryid");
				slapi_entry_attr_delete(addingentry->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID);
				slapi_entry_delete_string(addingentry->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
				/* Now also remove the nscpEntryDN */
				if (slapi_entry_attr_delete(addingentry->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN) != 0){
					LDAPDebug(LDAP_DEBUG_REPL, "Resurrection of %s - Couldn't remove %s\n", dn, SLAPI_ATTR_NSCP_ENTRYDN, 0);
				}
				
				/* And copy the reason from e */
				reason = slapi_entry_attr_get_charptr(e, "nsds5ReplConflict");
				if (reason) {
					if (!slapi_entry_attr_hasvalue(addingentry->ep_entry, "nsds5ReplConflict", reason)) {
						slapi_entry_add_string(addingentry->ep_entry, "nsds5ReplConflict", reason);
						LDAPDebug(LDAP_DEBUG_REPL, "Resurrection of %s - Added Conflict reason %s\n", dn, reason, 0);
					}
					slapi_ch_free((void **)&reason);
				}
				/* Clear the Tombstone Flag in the entry */
				slapi_entry_clear_flag(addingentry->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);

				/* make sure the objectclass
				   - does not contain any duplicate values
				   - has CSNs for the new values we added
				*/
				{
					Slapi_Attr *sa = NULL;
					Slapi_Value sv;
					const struct berval *svbv = NULL;

					/* add the extensibleobject objectclass with csn if not present */
					slapi_entry_attr_find(addingentry->ep_entry, SLAPI_ATTR_OBJECTCLASS, &sa);
					slapi_value_init_string(&sv, "extensibleobject");
					svbv = slapi_value_get_berval(&sv);
					if (slapi_attr_value_find(sa, svbv)) { /* not found, so add it */
						if (opcsn) {
							value_update_csn(&sv, CSN_TYPE_VALUE_UPDATED, opcsn);
						}
						slapi_attr_add_value(sa, &sv);
					}
					value_done(&sv);
					
					/* add the glue objectclass with csn if not present */
					slapi_value_init_string(&sv, "glue");
					svbv = slapi_value_get_berval(&sv);
					if (slapi_attr_value_find(sa, svbv)) { /* not found, so add it */
						if (opcsn) {
							value_update_csn(&sv, CSN_TYPE_VALUE_UPDATED, opcsn);
						}
						slapi_attr_add_value(sa, &sv);
					}
					value_done(&sv);
				}
			}
			else
			{
				/*
				 * Try to add the entry to the cache, assign it a new entryid
				 * and mark it locked.  This should only fail if the entry
				 * already exists.
				 */
				/*
				 * next_id will add this id to the list of ids that are pending
				 * id2entry indexing.
				 */
				addingentry = backentry_init( e );
				if ( ( addingentry->ep_id = next_id( be ) ) >= MAXID ) {
				  LDAPDebug( LDAP_DEBUG_ANY,
						 "add: maximum ID reached, cannot add entry to "
						 "backend '%s'", be->be_name, 0, 0 );
				  ldap_result_code = LDAP_OPERATIONS_ERROR;
				  goto error_return;
				}
				addingentry_id_assigned= 1;

				if (!is_fixup_operation)
				{
					if ( opcsn == NULL && operation->o_csngen_handler )
					{
						/*
						 * Current op is a user request. Opcsn will be assigned
						 * if the dn is in an updatable replica.
						 */
						opcsn = entry_assign_operation_csn ( pb, e, parententry ? parententry->ep_entry : NULL );
					}
					if ( opcsn != NULL )
					{
						entry_set_csn (e, opcsn);
						entry_add_dncsn (e, opcsn);
						entry_add_rdn_csn (e, opcsn);
						entry_set_maxcsn (e, opcsn);
					}
				}

				if (is_tombstone_operation)
				{
					/* Directly add the entry as a tombstone */
					/*
					 * 1) If the entry has an existing DN, change it to be
					 *	"nsuniqueid=<uniqueid>, <old dn>"
					 * 2) Add the objectclass value "tombstone" and arrange for only
					 *	that value to be indexed. 
					 * 3) If the parent entry was found, set the nsparentuniqueid
					 *	attribute to be the unique id of that parent.
					 */
					char *untombstoned_dn = slapi_entry_get_dn(e);
					char *tombstoned_dn = NULL;
					if (NULL == untombstoned_dn)
					{
						untombstoned_dn = "";
					}
					tombstoned_dn = compute_entry_tombstone_dn(untombstoned_dn, addr.uniqueid);
					/*
					 * This needs to be done before slapi_entry_set_dn call,
					 * because untombstoned_dn is released in slapi_entry_set_dn.
					 */
					if (entryrdn_get_switch())
					{
						Slapi_RDN srdn = {0};
						rc = slapi_rdn_init_all_dn(&srdn, tombstoned_dn);
						if (rc) {
							LDAPDebug1Arg( LDAP_DEBUG_TRACE,
								"ldbm_back_add (tombstone_operation): failed to "
								"decompose %s to Slapi_RDN\n", tombstoned_dn);
						} else {
							slapi_entry_set_srdn(e, &srdn);
							slapi_rdn_done(&srdn);
						}
					}
					slapi_entry_set_dn(addingentry->ep_entry, tombstoned_dn);
					/* Work around pb with slapi_entry_add_string (defect 522327) 
					 * doesn't check duplicate values */
					if (!slapi_entry_attr_hasvalue(addingentry->ep_entry, 
								SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE)) {
						slapi_entry_add_string(addingentry->ep_entry, 
									SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
						slapi_entry_set_flag(addingentry->ep_entry,
									SLAPI_ENTRY_FLAG_TOMBSTONE);
					}
					if (NULL != operation->o_params.p.p_add.parentuniqueid)
					{
						slapi_entry_add_string(addingentry->ep_entry,
											SLAPI_ATTR_VALUE_PARENT_UNIQUEID,
											operation->o_params.p.p_add.parentuniqueid);
					}
				}
			}

			/*
			 * Get the parent dn and see if the corresponding entry exists.
			 * If the parent does not exist, only allow the "root" user to
			 * add the entry.
			 */
			if ( !slapi_sdn_isempty(&parentsdn) )
			{
				/* This is getting the parent */
				if (NULL == parententry)
				{
					/* Here means that we didn't find the parent */
					int err = 0;
					Slapi_DN ancestorsdn;
					struct backentry *ancestorentry;

					LDAPDebug( LDAP_DEBUG_TRACE,
						"parent does not exist, pdn = %s\n",
						slapi_sdn_get_dn(&parentsdn), 0, 0 );

					slapi_sdn_init(&ancestorsdn);
					ancestorentry = dn2ancestor(be, &parentsdn, &ancestorsdn, &txn, &err );
					CACHE_RETURN( &inst->inst_cache, &ancestorentry );

					ldap_result_code= LDAP_NO_SUCH_OBJECT;
					ldap_result_matcheddn= 
					    slapi_ch_strdup((char *)slapi_sdn_get_dn(&ancestorsdn)); /* jcm - cast away const. */
					slapi_sdn_done(&ancestorsdn);
					goto error_return;
				}
				ldap_result_code = plugin_call_acl_plugin (pb, e, NULL, NULL, SLAPI_ACL_ADD, 
								ACLPLUGIN_ACCESS_DEFAULT, &errbuf );
				if ( ldap_result_code != LDAP_SUCCESS )
				{
					LDAPDebug( LDAP_DEBUG_TRACE, "no access to parent\n", 0, 0, 0 );
					ldap_result_message= errbuf;
					goto error_return;
				}
				pid = parententry->ep_id;
			}
			else
			{	/* no parent */
				if ( !isroot && !is_replicated_operation)
				{
					LDAPDebug( LDAP_DEBUG_TRACE, "no parent & not root\n",
						0, 0, 0 );
					ldap_result_code= LDAP_INSUFFICIENT_ACCESS;
					goto error_return;
				}
				parententry = NULL;
				pid = 0;
			}
	
			if(is_resurect_operation)
			{
				add_update_entrydn_operational_attributes(addingentry);
			}
			else if (is_tombstone_operation)
			{
				/* Remove the entrydn operational attributes from the addingentry */
				delete_update_entrydn_operational_attributes(addingentry);
			}
			else
			{
				/*
				 * add the parentid, entryid and entrydn operational attributes
				 */
				add_update_entry_operational_attributes(addingentry, pid);
			}

			/* Tentatively add the entry to the cache.  We do this after adding any
			 * operational attributes to ensure that the cache is sized correctly. */
			if ( cache_add_tentative( &inst->inst_cache, addingentry, NULL )!= 0 )
			{
				LDAPDebug( LDAP_DEBUG_CACHE, "cache_add_tentative concurrency detected\n", 0, 0, 0 );
				ldap_result_code= LDAP_ALREADY_EXISTS;
				goto error_return;
			}
			addingentry_in_cache= 1;

			/*
			 * Before we add the entry, find out if the syntax of the aci
			 * aci attribute values are correct or not. We don't want to
			 * the entry if the syntax is incorrect.
			 */
			if ( plugin_call_acl_verify_syntax (pb, addingentry->ep_entry, &errbuf) != 0 ) {
				LDAPDebug( LDAP_DEBUG_TRACE, "ACL syntax error\n", 0,0,0);
				ldap_result_code= LDAP_INVALID_SYNTAX;
				ldap_result_message= errbuf;
				goto error_return;
			}

			/* Having decided that we're really going to do the operation, let's modify 
			   the in-memory state of the parent to reflect the new child (update
			   subordinate count specifically */
			if (NULL != parententry)
			{
				retval = parent_update_on_childchange(&parent_modify_c,
				                                      PARENTUPDATE_ADD, NULL);
				/* The modify context now contains info needed later */
				if (0 != retval) {
					ldap_result_code= LDAP_OPERATIONS_ERROR;
					goto error_return;
				}
				parent_found = 1;
				parententry = NULL;
			}
		
			if ( (originalentry = backentry_dup(addingentry )) == NULL ) {
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}
		} /* if (0 == retry_count) just once */

		/* call the transaction pre add plugins just after the to-be-added entry
		 * is prepared. */
		retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN);
		if (retval) {
			int opreturn = 0;
			if (SLAPI_PLUGIN_NOOP == retval) {
				not_an_error = 1;
				rc = retval = LDAP_SUCCESS;
			}
			LDAPDebug1Arg( LDAP_DEBUG_TRACE, "SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN plugin "
				       "returned error code %d\n", retval );
			if (!ldap_result_code) {
				slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			}
			if (!ldap_result_code) {
				LDAPDebug0Args( LDAP_DEBUG_ANY, "SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN plugin "
						"returned error but did not setSLAPI_RESULT_CODE \n" );
				ldap_result_code = LDAP_OPERATIONS_ERROR;
				slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			}
			slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
			if (!opreturn) {
				slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
			}
			slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
			goto error_return;
		}

		retval = id2entry_add( be, addingentry, &txn );
		if (DB_LOCK_DEADLOCK == retval)
		{
			LDAPDebug( LDAP_DEBUG_ARGS, "add 1 DEADLOCK\n", 0, 0, 0 );
			/* Retry txn */
			continue;
		}
		if (retval != 0) {
			LDAPDebug( LDAP_DEBUG_TRACE, "id2entry_add failed, err=%d %s\n",
				   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
				disk_full = 1;
				goto diskfull_return;
			}
			goto error_return; 
		}
		if(is_resurect_operation)
		{
			retval = index_addordel_string(be,SLAPI_ATTR_OBJECTCLASS,SLAPI_ATTR_VALUE_TOMBSTONE,addingentry->ep_id,BE_INDEX_DEL|BE_INDEX_EQUALITY,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "add 2 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "add 1 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				ADD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
					disk_full = 1;
					goto diskfull_return;
				}
				goto error_return; 
			}
			retval = index_addordel_string(be,SLAPI_ATTR_UNIQUEID,slapi_entry_get_uniqueid(addingentry->ep_entry),addingentry->ep_id,BE_INDEX_DEL|BE_INDEX_EQUALITY,&txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "add 3 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "add 2 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				ADD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
					disk_full = 1;
					goto diskfull_return;
				}
				goto error_return; 
			}
			retval = index_addordel_string(be,
			                               SLAPI_ATTR_NSCP_ENTRYDN,
			                               slapi_sdn_get_ndn(sdn),
			                               addingentry->ep_id,
			                               BE_INDEX_DEL|BE_INDEX_EQUALITY, &txn);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS, "add 4 DB_LOCK_DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "add 3 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				ADD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
					disk_full = 1;
					goto diskfull_return;
				}
				goto error_return; 
			}
		} 
		if (is_tombstone_operation)
		{
			retval = index_addordel_entry( be, addingentry, BE_INDEX_ADD | BE_INDEX_TOMBSTONE, &txn );
		}
		else
		{
			retval = index_addordel_entry( be, addingentry, BE_INDEX_ADD, &txn );
		}
		if (DB_LOCK_DEADLOCK == retval)
		{
			LDAPDebug( LDAP_DEBUG_ARGS, "add 5 DEADLOCK\n", 0, 0, 0 );
			/* retry txn */
			continue;
		}
		if (retval != 0) {
			LDAPDebug2Args(LDAP_DEBUG_ANY,
			               "add: attempt to index %lu failed (rc=%d)\n",
			               (u_long)addingentry->ep_id, retval);
			ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
				disk_full = 1;
				goto diskfull_return;
			}
			goto error_return; 
		}
		if (parent_found) {
			/* Push out the db modifications from the parent entry */
			retval = modify_update_all(be,pb,&parent_modify_c,&txn);
			if (DB_LOCK_DEADLOCK == retval)
			{
				LDAPDebug( LDAP_DEBUG_ARGS, "add 6 DEADLOCK\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE, "add 1 BAD, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				ADD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
					disk_full = 1;
					goto diskfull_return;
				}
				goto error_return; 
			}
		}
		/*
		 * Update the Virtual List View indexes
		 */
		if (!is_ruv)
		{
			retval= vlv_update_all_indexes(&txn, be, pb, NULL, addingentry);
			if (DB_LOCK_DEADLOCK == retval) {
				LDAPDebug( LDAP_DEBUG_ARGS,
								"add DEADLOCK vlv_update_index\n", 0, 0, 0 );
				/* Retry txn */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_TRACE,
					"vlv_update_index failed, err=%d %s\n",
				   	retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				ADD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
					disk_full = 1;
					goto diskfull_return;
				}
				goto error_return; 
			}
		}

		if (!is_ruv && !is_fixup_operation && !NO_RUV_UPDATE(li)) {
			ruv_c_init = ldbm_txn_ruv_modify_context( pb, &ruv_c );
			if (-1 == ruv_c_init) {
				LDAPDebug( LDAP_DEBUG_ANY,
					"ldbm_back_add: ldbm_txn_ruv_modify_context "
					"failed to construct RUV modify context\n",
					0, 0, 0);
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				retval = 0;
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

		if (retval == 0) {
			break;
		}
	}
	if (retry_count == RETRY_TIMES) {
		/* Failed */
		LDAPDebug( LDAP_DEBUG_ANY, "Retry count exceeded in add\n", 0, 0, 0 );
   		ldap_result_code= LDAP_BUSY;
		goto error_return;
	}

	/*
	 * At this point, everything's cool, and the only thing which
	 * can go wrong is a transaction commit failure.
	 */
	slapi_pblock_set( pb, SLAPI_ENTRY_PRE_OP, NULL );
	slapi_pblock_set( pb, SLAPI_ENTRY_POST_OP, slapi_entry_dup( addingentry->ep_entry ));

	if(is_resurect_operation)
	{
		/*
		 * We can now switch the tombstone entry with the real entry.
		 */
		if (cache_replace( &inst->inst_cache, tombstoneentry, addingentry ) != 0 )
		{
			/* This happens if the dn of addingentry already exists */
			cache_unlock_entry( &inst->inst_cache, tombstoneentry );
			ADD_SET_ERROR(ldap_result_code, LDAP_ALREADY_EXISTS, retry_count);
			goto error_return;
		}
		/*
		 * The tombstone was locked down in the cache... we can
		 * get rid of the entry in the cache now.
		 */
		cache_unlock_entry( &inst->inst_cache, tombstoneentry );
		CACHE_RETURN( &inst->inst_cache, &tombstoneentry );
		tombstone_in_cache = 0; /* deleted */
	}
	if (parent_found)
	{
		/* switch the parent entry copy into play */
		modify_switch_entries( &parent_modify_c,be);
	}

	if (ruv_c_init) {
		if (modify_switch_entries(&ruv_c, be) != 0 ) {
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			LDAPDebug( LDAP_DEBUG_ANY,
				"ldbm_back_add: modify_switch_entries failed\n", 0, 0, 0);
			goto error_return;
		}
	}

	/* call the transaction post add plugins just before the commit */
	if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_ADD_FN))) {
		int opreturn = 0;
		LDAPDebug1Arg( LDAP_DEBUG_TRACE, "SLAPI_PLUGIN_BE_TXN_POST_ADD_FN plugin "
			       "returned error code %d\n", retval );
		if (!ldap_result_code) {
			slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
		}
		if (!ldap_result_code) {
			LDAPDebug0Args( LDAP_DEBUG_ANY, "SLAPI_PLUGIN_BE_TXN_POST_ADD_FN plugin "
					"returned error but did not set SLAPI_RESULT_CODE\n" );
			ldap_result_code = LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
		}
		slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
		if (!opreturn) {
			slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
		}
		slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
		goto error_return;
	}

	/* Release SERIAL LOCK */
	retval = dblayer_txn_commit(be, &txn);
	/* after commit - txn is no longer valid - replace SLAPI_TXN with parent */
	slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
	if (0 != retval)
	{
		ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
		if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
			disk_full = 1;
			goto diskfull_return;
		}
		goto error_return; 
	}

	rc= 0;
	goto common_return;

error_return:
	if ( addingentry_id_assigned )
	{
		next_id_return( be, addingentry->ep_id );
	}
	if ( NULL != addingentry )
	{
		if ( addingentry_in_cache )
		{
			if (inst) {
				CACHE_REMOVE(&inst->inst_cache, addingentry);
			}
			addingentry_in_cache = 0;
		}
		backentry_clear_entry(addingentry); /* e is released in the frontend */
		backentry_free( &addingentry ); /* release the backend wrapper, here */
	}
	if(tombstone_in_cache && inst)
	{
		CACHE_RETURN(&inst->inst_cache, &tombstoneentry);
	}

	if (rc == DB_RUNRECOVERY) {
		dblayer_remember_disk_filled(li);
		ldbm_nasty("Add",80,rc);
		disk_full = 1;
	} else if (0 == rc) {
		rc = SLAPI_FAIL_GENERAL;
	}
diskfull_return:
	if (disk_full) {
		rc= return_on_disk_full(li);
	} else {
		/* It is safer not to abort when the transaction is not started. */
		if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
			/* make sure SLAPI_RESULT_CODE and SLAPI_PLUGIN_OPRETURN are set */
			int val = 0;
			slapi_pblock_get(pb, SLAPI_RESULT_CODE, &val);
			if (!val) {
				if (!ldap_result_code) {
					ldap_result_code = LDAP_OPERATIONS_ERROR;
				}
				slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			}
			slapi_pblock_get( pb, SLAPI_PLUGIN_OPRETURN, &val );
			if (!val) {
				val = -1;
				slapi_pblock_set( pb, SLAPI_PLUGIN_OPRETURN, &val );
			}
			/* call the transaction post add plugins just before the abort */
			if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_ADD_FN))) {
				int opreturn = 0;
				LDAPDebug1Arg( LDAP_DEBUG_TRACE, "SLAPI_PLUGIN_BE_TXN_POST_ADD_FN plugin "
					       "returned error code %d\n", retval );
				if (!ldap_result_code) {
					slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
				}
				slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
				if (!opreturn) {
					opreturn = -1;
					slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
				}
			}

			/* Release SERIAL LOCK */
			dblayer_txn_abort(be, &txn); /* abort crashes in case disk full */
			/* txn is no longer valid - reset the txn pointer to the parent */
			slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
		}
		if (!not_an_error) {
			rc = SLAPI_FAIL_GENERAL;
		}
	}
	
common_return:
	if (inst) {
		if (addingentry_in_cache && addingentry) {
			if (entryrdn_get_switch()) { /* subtree-rename: on */
				/* since adding the entry to the entry cache was successful,
				 * let's add the dn to dncache, if not yet done. */
				struct backdn *bdn = dncache_find_id(&inst->inst_dncache,
				                                     addingentry->ep_id);
				if (bdn) { /* already in the dncache */
					CACHE_RETURN(&inst->inst_dncache, &bdn);
				} else { /* not in the dncache yet */
					Slapi_DN *addingsdn = 
					  slapi_sdn_dup(slapi_entry_get_sdn(addingentry->ep_entry));
					if (addingsdn) {
						bdn = backdn_init(addingsdn, addingentry->ep_id, 0);
						if (bdn) {
							CACHE_ADD( &inst->inst_dncache, bdn, NULL );
							CACHE_RETURN(&inst->inst_dncache, &bdn);
							slapi_log_error(SLAPI_LOG_CACHE, "ldbm_back_add",
							                "set %s to dn cache\n", dn);
						}
					}
				}
			}
			CACHE_RETURN( &inst->inst_cache, &addingentry );
		}
		if (inst->inst_ref_count) {
			slapi_counter_decrement(inst->inst_ref_count);
		}
	}
	/* bepost op needs to know this result */
	slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
	/* JCMREPL - The bepostop is called even if the operation fails. */
	plugin_call_plugins (pb, SLAPI_PLUGIN_BE_POST_ADD_FN);
	if (ruv_c_init) {
		modify_term(&ruv_c, be);
	}
	modify_term(&parent_modify_c,be);
	done_with_pblock_entry(pb,SLAPI_ADD_EXISTING_DN_ENTRY);
	done_with_pblock_entry(pb,SLAPI_ADD_EXISTING_UNIQUEID_ENTRY);
	done_with_pblock_entry(pb,SLAPI_ADD_PARENT_ENTRY);
	if(ldap_result_code!=-1)
	{
		if (not_an_error) {
			/* This is mainly used by urp.  Solved conflict is not an error.
			 * And we don't want the supplier to halt sending the updates. */
			ldap_result_code = LDAP_SUCCESS;
		}
		slapi_send_ldap_result( pb, ldap_result_code, ldap_result_matcheddn, ldap_result_message, 0, NULL );
	}
	backentry_free(&originalentry);
	slapi_sdn_done(&parentsdn);
	slapi_ch_free( (void**)&ldap_result_matcheddn );
	slapi_ch_free( (void**)&errbuf );
	return rc;
}

/*
 * add the parentid, entryid and entrydn, operational attributes.
 *
 * Note: This is called from the ldif2ldbm code.
 */
void
add_update_entry_operational_attributes(struct backentry *ep, ID pid)
{
	struct berval bv;
	struct berval *bvp[2];
	char buf[40]; /* Enough for an EntryID */

	bvp[0] = &bv;
	bvp[1] = NULL;

	/* parentid */
	/* If the pid is 0, then the entry does not have a parent.  It
	 * may be the case that the entry is a suffix.  In any case,
	 * the parentid attribute should only be added if the entry 
	 * has a parent. */
	if (pid != 0) {
		sprintf( buf, "%lu", (u_long)pid );
		bv.bv_val = buf;
		bv.bv_len = strlen( buf );
		entry_replace_values( ep->ep_entry, LDBM_PARENTID_STR, bvp );
	}

	/* entryid */
	sprintf( buf, "%lu", (u_long)ep->ep_id );
	bv.bv_val = buf;
	bv.bv_len = strlen( buf );
	entry_replace_values( ep->ep_entry, "entryid", bvp );

	/* add the entrydn operational attribute to the entry. */
	add_update_entrydn_operational_attributes(ep);
}

/*
 * add the entrydn operational attribute to the entry.
 */
void
add_update_entrydn_operational_attributes(struct backentry *ep)
{
    struct berval bv;
    struct berval *bvp[2];

    /* entrydn */
    bvp[0] = &bv;
    bvp[1] = NULL;
    bv.bv_val = (void*)backentry_get_ndn(ep);
    bv.bv_len = strlen( bv.bv_val );
    entry_replace_values_with_flags( ep->ep_entry, LDBM_ENTRYDN_STR, bvp,
                                     SLAPI_ATTR_FLAG_NORMALIZED_CIS );
}

/*
 * delete the entrydn operational attributes from the entry.
 */
static void
delete_update_entrydn_operational_attributes(struct backentry *ep)
{
    /* entrydn */
    slapi_entry_attr_delete( ep->ep_entry, LDBM_ENTRYDN_STR);
}

