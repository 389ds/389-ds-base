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

/* modify.c - ldbm backend modify routine */

#include "back-ldbm.h"

extern char *numsubordinates;
extern char *hassubordinates;

static void remove_illegal_mods(LDAPMod **mods);
static int mods_have_effect (Slapi_Entry *entry, Slapi_Mods *smods);

/* Modify context structure constructor, sans allocation */
void modify_init(modify_context *mc,struct backentry *old_entry)
{
	/* Store the old entry */
	PR_ASSERT(NULL == mc->old_entry);
	PR_ASSERT(NULL == mc->new_entry);

	mc->old_entry = old_entry;
        mc->new_entry_in_cache = 0;
}

int modify_apply_mods(modify_context *mc, Slapi_Mods *smods)
{
	int ret = 0;
	/* Make a copy of the entry */
	PR_ASSERT(mc->old_entry != NULL);
	PR_ASSERT(mc->new_entry == NULL);
	mc->new_entry = backentry_dup(mc->old_entry);
	PR_ASSERT(smods!=NULL);
	if ( mods_have_effect (mc->new_entry->ep_entry, smods) ) {
		ret = entry_apply_mods( mc->new_entry->ep_entry, slapi_mods_get_ldapmods_byref(smods));
	}
	mc->smods= smods;
	return ret;
}

/* Modify context structure destructor */
int modify_term(modify_context *mc,struct backend *be)
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;

    slapi_mods_free(&mc->smods);
    /* Unlock and return entries */
    if (NULL != mc->old_entry)  {
        cache_unlock_entry(&inst->inst_cache, mc->old_entry);
        cache_return( &(inst->inst_cache), &(mc->old_entry) );
        mc->old_entry= NULL;
    }
    if (mc->new_entry_in_cache) {
        cache_return( &(inst->inst_cache), &(mc->new_entry) );
    } else {
        backentry_free(&(mc->new_entry));
    }
    mc->new_entry= NULL;
    return 0;
}

/* Modify context structure member to switch entries in the cache */
int modify_switch_entries(modify_context *mc,backend *be)
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	int ret = 0;
	if (mc->old_entry!=NULL && mc->new_entry!=NULL) {
	    ret = cache_replace(&(inst->inst_cache), mc->old_entry, mc->new_entry);
            if (ret == 0) mc->new_entry_in_cache = 1;
	}
	return ret;
}

/* This routine does that part of a modify operation which involves
   updating the on-disk data: updates idices, id2entry. 
   Copes properly with DB_LOCK_DEADLOCK. The caller must be able to cope with 
   DB_LOCK_DEADLOCK returned.
   The caller is presumed to proceed as follows: 
	Find the entry you want to modify;
	Lock it for modify;
	Make a copy of it; (call backentry_dup() )
	Apply modifications to the copy in memory (call entry_apply_mods() )
	begin transaction;
	Do any other mods to on-disk data you want
	Call this routine;
	Commit transaction;
   You pass it environment data: struct ldbminfo, pb (not sure why, but the vlv code seems to need it)
   the copy of the entry before modfication, the entry after modification;
   an LDAPMods array containing the modifications performed
*/
int modify_update_all(backend *be, Slapi_PBlock *pb,
					  modify_context *mc,
					  back_txn *txn)
{
	static char *function_name = "modify_update_all";
	Slapi_Operation *operation;
	int is_ruv = 0;				 /* True if the current entry is RUV */
	int retval = 0;

	if (pb) { /* pb could be NULL if it's called from import */
		slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
		is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
	}
	/*
	 * Update the ID to Entry index. 
	 * Note that id2entry_add replaces the entry, so the Entry ID stays the same.
	 */
	retval = id2entry_add( be, mc->new_entry, txn );
	if ( 0 != retval ) {
		if (DB_LOCK_DEADLOCK != retval)
		{
			ldbm_nasty(function_name,66,retval);
		}
		goto error;
	}
	retval = index_add_mods( be, (const LDAPMod **)slapi_mods_get_ldapmods_byref(mc->smods), mc->old_entry, mc->new_entry, txn );
	if ( 0 != retval ) {
		if (DB_LOCK_DEADLOCK != retval)
		{
			ldbm_nasty(function_name,65,retval);
		}
		goto error;
	}
	/*
	 * Remove the old entry from the Virtual List View indexes.
	 * Add the new entry to the Virtual List View indexes.
	 * Because the VLV code calls slapi_filter_test(), which requires a pb (why?),
	 * we allow the caller sans pb to get everything except vlv indexing.
	 */
	if (NULL != pb && !is_ruv) {
		retval= vlv_update_all_indexes(txn, be, pb, mc->old_entry, mc->new_entry);
		if ( 0 != retval ) {
			if (DB_LOCK_DEADLOCK != retval)
			{
				ldbm_nasty(function_name,64,retval);
			}
			goto error;
		}
	}
error:
	return retval;
}

int
ldbm_back_modify( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	struct ldbminfo		*li;
	struct backentry	*e = NULL, *ec = NULL;
	Slapi_Entry		*postentry = NULL;
	LDAPMod			**mods;
	Slapi_Mods smods = {0};
	back_txn txn;
	back_txnid		parent_txn;
	int			retval = -1;
	char			*msg;
	char			*errbuf = NULL;
	int retry_count = 0;
	int disk_full = 0;
	int ldap_result_code= LDAP_SUCCESS;
	char *ldap_result_message= NULL;
	int rc = 0;
	Slapi_Operation *operation;
	int dblock_acquired= 0;
	entry_address *addr;
	int change_entry = 0;
	int ec_in_cache = 0;
	int is_fixup_operation= 0;
	int is_ruv = 0;                 /* True if the current entry is RUV */
	CSN *opcsn = NULL;
	int i = 0;

	slapi_pblock_get( pb, SLAPI_BACKEND, &be);
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr );
	slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
	slapi_pblock_get( pb, SLAPI_PARENT_TXN, (void**)&parent_txn );
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
	is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
	inst = (ldbm_instance *) be->be_instance_info;

	if (NULL == addr)
	{
		goto error_return;
	}
	ldap_result_code = slapi_dn_syntax_check(pb, addr->dn, 1);
	if (ldap_result_code)
	{
		ldap_result_code = LDAP_INVALID_DN_SYNTAX;
		slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
		goto error_return;
	}
	dblayer_txn_init(li,&txn);

	/* The dblock serializes writes to the database,
	 * which reduces deadlocking in the db code,
	 * which means that we run faster.
	 *
	 * But, this lock is re-enterant for the fixup
	 * operations that the URP code in the Replication
	 * plugin generates.
	 */
	if(SERIALLOCK(li) && !operation_is_flag_set(operation,OP_FLAG_REPL_FIXUP)) {
		dblayer_lock_backend(be);
		dblock_acquired= 1;
	}

	/* find and lock the entry we are about to modify */
	if ( (e = find_entry2modify( pb, be, addr, NULL )) == NULL ) {
		ldap_result_code= -1;
		goto error_return;	  /* error result sent by find_entry2modify() */
	}

	if ( !is_fixup_operation )
	{
		opcsn = operation_get_csn (operation);
		if (NULL == opcsn && operation->o_csngen_handler)
		{
			/*
			 * Current op is a user request. Opcsn will be assigned
			 * if the dn is in an updatable replica.
			 */
			opcsn = entry_assign_operation_csn ( pb, e->ep_entry, NULL );
		}
		if (opcsn)
		{
			entry_set_maxcsn (e->ep_entry, opcsn);
		}
	}

	/* Save away a copy of the entry, before modifications */
	slapi_pblock_set( pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup( e->ep_entry ));
	
	if ( (ldap_result_code = plugin_call_acl_mods_access( pb, e->ep_entry, mods, &errbuf)) != LDAP_SUCCESS ) {
		ldap_result_message= errbuf;
		goto error_return;
	}

	/* create a copy of the entry and apply the changes to it */
	if ( (ec = backentry_dup( e )) == NULL ) {
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	remove_illegal_mods(mods);

	/* ec is the entry that our bepreop should get to mess with */
	slapi_pblock_set( pb, SLAPI_MODIFY_EXISTING_ENTRY, ec->ep_entry );
	slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
	plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_MODIFY_FN);
	slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
	/* The Plugin may have messed about with some of the PBlock parameters... ie. mods */
	slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
	slapi_mods_init_byref(&smods,mods);

	{
		CSN *csn = operation_get_csn(operation);
		if ( (change_entry = mods_have_effect (ec->ep_entry, &smods)) ) {
			ldap_result_code = entry_apply_mods_wsi(ec->ep_entry, &smods, csn, operation_is_flag_set(operation,OP_FLAG_REPLICATED));
			/*
			 * XXXmcs: it would be nice to get back an error message from
			 * the above call so we could pass it along to the client, e.g.,
			 * "duplicate value for attribute givenName."
			 */
		} else {
			 /* If the entry was not actually changed, we still need to
			 * set the SLAPI_ENTRY_POST_OP field in the pblock (post-op
			 * plugins expect that field to be present for all modify
			 * operations that return LDAP_SUCCESS).
			 */
			postentry = slapi_entry_dup( e->ep_entry );
			slapi_pblock_set ( pb, SLAPI_ENTRY_POST_OP, postentry );
			postentry = NULL;	/* avoid removal/free in error_return code */
		}
		if ( !change_entry || ldap_result_code != 0 ) {
			/* change_entry == 0 is not an error, but we need to free lock etc */
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
			goto error_return;
		}
	}

	/*
	 * If the objectClass attribute type was modified in any way, expand
	 * the objectClass values to reflect the inheritance hierarchy.
	 */
	for ( i = 0; mods[i] != NULL; ++i ) {
		if ( 0 == strcasecmp( SLAPI_ATTR_OBJECTCLASS, mods[i]->mod_type )) {
			slapi_schema_expand_objectclasses( ec->ep_entry );
			break;
		}
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

	/* check that the entry still obeys the schema */
	if ( (operation_is_flag_set(operation,OP_FLAG_ACTION_SCHEMA_CHECK)) && 
		  slapi_entry_schema_check( pb, ec->ep_entry ) != 0 ) {
		ldap_result_code= LDAP_OBJECT_CLASS_VIOLATION;
		slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
		goto error_return;
	}

	/* check attribute syntax for the new values */
	if (slapi_mods_syntax_check(pb, mods, 0) != 0)
	{
		ldap_result_code = LDAP_INVALID_SYNTAX;
		slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
		goto error_return;
	}

	/*
	 * make sure the entry contains all values in the RDN.
	 * if not, the modification must have removed them.
	 */
	if ( ! slapi_entry_rdn_values_present( ec->ep_entry ) ) {
		ldap_result_code= LDAP_NOT_ALLOWED_ON_RDN;
		goto error_return;
	}

	for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {

		if (retry_count > 0) {
			dblayer_txn_abort(li,&txn);
			LDAPDebug( LDAP_DEBUG_TRACE, "Modify Retrying Transaction\n", 0, 0, 0 );
#ifndef LDBM_NO_BACKOFF_DELAY
			{
			PRIntervalTime interval;
			interval = PR_MillisecondsToInterval(slapi_rand() % 100);
			DS_Sleep(interval);
			}
#endif
		}

		/* Nothing above here modifies persistent store, everything after here is subject to the transaction */
		retval = dblayer_txn_begin(li,parent_txn,&txn);

		if (0 != retval) {
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			goto error_return;
		}

		/*
		 * Update the ID to Entry index. 
		 * Note that id2entry_add replaces the entry, so the Entry ID stays the same.
		 */
		retval = id2entry_add( be, ec, &txn ); 
		if (DB_LOCK_DEADLOCK == retval)
		{
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
		ec_in_cache = 1;
		retval = index_add_mods( be, (const LDAPMod**)mods, e, ec, &txn );
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Abort and re-try */
			continue;
		}
		if (0 != retval) {
			LDAPDebug( LDAP_DEBUG_ANY, "index_add_mods failed, err=%d %s\n",
				  retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			goto error_return;
		}
		/*
		 * Remove the old entry from the Virtual List View indexes.
		 * Add the new entry to the Virtual List View indexes.
		 * If the entry is ruv, no need to update vlv.
		 */
		if (!is_ruv) {
			retval= vlv_update_all_indexes(&txn, be, pb, e, ec);
			if (DB_LOCK_DEADLOCK == retval)
			{
				/* Abort and re-try */
				continue;
			}
			if (0 != retval) {
				LDAPDebug( LDAP_DEBUG_ANY, 
					"vlv_update_index failed, err=%d %s\n",
					retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				ldap_result_code= LDAP_OPERATIONS_ERROR;
				goto error_return;
			}

		}
		if (0 == retval) {
			break;
		}
	}
	if (retry_count == RETRY_TIMES) {
		LDAPDebug( LDAP_DEBUG_ANY, "Retry count exceeded in modify\n", 0, 0, 0 );
	   	ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}
	
	if (cache_replace( &inst->inst_cache, e, ec ) != 0 ) {
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	postentry = slapi_entry_dup( ec->ep_entry );
	slapi_pblock_set( pb, SLAPI_ENTRY_POST_OP, postentry );

	/* invalidate virtual cache */
	ec->ep_entry->e_virtual_watermark = 0;

	/* we must return both e (which has been deleted) and new entry ec */
	cache_unlock_entry( &inst->inst_cache, e );
	cache_return( &inst->inst_cache, &e );
	/* 
	 * LP Fix of crash when the commit will fail:
	 * If the commit fail, the common error path will
	 * try to unlock the entry again and crash (PR_ASSERT
	 * in debug mode.
	 * By just setting e to NULL, we avoid this. It's OK since
	 * we don't use e after that in the normal case.
	 */
	e = NULL;
	
	retval = dblayer_txn_commit(li,&txn);
	if (0 != retval) {
		if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	rc= 0;
	goto common_return;

error_return:
	if (ec_in_cache)
	{
		cache_remove( &inst->inst_cache, ec );
	}
	else
	{
		backentry_free(&ec);
	}
	if ( postentry != NULL ) 
	{
		slapi_entry_free( postentry );
		postentry = NULL;
		slapi_pblock_set( pb, SLAPI_ENTRY_POST_OP, NULL );
	}
	
	if (e!=NULL) {
	    cache_unlock_entry( &inst->inst_cache, e);
	    cache_return( &inst->inst_cache, &e);
	}

	if (retval == DB_RUNRECOVERY) {
	  dblayer_remember_disk_filled(li);
	  ldbm_nasty("Modify",81,retval);
	  disk_full = 1;
	}

	if (disk_full)
	    rc= return_on_disk_full(li);
	else if (ldap_result_code != LDAP_SUCCESS) {
		if (retry_count > 0) {
			/* It is safer not to abort when the transaction is not started. */
			dblayer_txn_abort(li,&txn); /* abort crashes in case disk full */
		}
	    rc= SLAPI_FAIL_GENERAL;
	}

	
common_return:
	slapi_mods_done(&smods);
	
	if (ec_in_cache)
	{
		cache_return( &inst->inst_cache, &ec );
	}
	/* JCMREPL - The bepostop is called even if the operation fails. */
	if (!disk_full)
		plugin_call_plugins (pb, SLAPI_PLUGIN_BE_POST_MODIFY_FN);

	if(dblock_acquired)
	{
		dblayer_unlock_backend(be);
	}
	if(ldap_result_code!=-1)
	{
    	slapi_send_ldap_result( pb, ldap_result_code, NULL, ldap_result_message, 0, NULL );
	}
	
    slapi_ch_free( (void**)&errbuf);
	return rc;
}

/* Function removes mods which are not allowed over-the-wire */
static void
remove_illegal_mods(LDAPMod **mods)
{
	int		i, j;
	LDAPMod		*tmp;

	/* remove any attempts by the user to modify these attrs */
	for ( i = 0; mods[i] != NULL; i++ ) {
		if ( strcasecmp( mods[i]->mod_type, numsubordinates ) == 0
		    || strcasecmp( mods[i]->mod_type, hassubordinates ) == 0 )
		{
			tmp = mods[i];
			for ( j = i; mods[j] != NULL; j++ ) {
				mods[j] = mods[j + 1];
			}
			slapi_ch_free( (void**)&(tmp->mod_type) );
			if ( tmp->mod_bvalues != NULL ) {
				ber_bvecfree( tmp->mod_bvalues );
			}
			slapi_ch_free( (void**)&tmp );
			i--;
		}
	}
}

/* A mod has no effect if it is trying to replace a non-existing
 * attribute with null value
 */ 
static int
mods_have_effect (Slapi_Entry *entry, Slapi_Mods *smods)
{
	LDAPMod *mod;
	Slapi_Attr *attr;
	int have_effect = 1;
	int j;

	/* Mods have effect if there is at least a non-replace mod or
	 * a non-null-value mod.
	 */
	for ( j = 0; j < smods->num_mods - 1; j++ ) {
		if ( (mod = smods->mods[j]) != NULL ) {
			if ( ((mod->mod_op & LDAP_MOD_REPLACE) == 0) ||
				 (mod->mod_vals.modv_bvals &&
				  strcasecmp (mod->mod_type, "modifiersname") &&
				  strcasecmp (mod->mod_type, "modifytime") ) ) {
				goto done;
			}
		}
	}

	if ( entry && entry->e_sdn.dn ) {
		for ( j = 0; j < smods->num_mods - 1; j++ ) {
			if ( ((mod = smods->mods[j]) != NULL) &&
				 strcasecmp (mod->mod_type, "modifiersname") &&
				 strcasecmp (mod->mod_type, "modifytime") ) {
				for ( attr = entry->e_attrs; attr; attr = attr->a_next ) {
					/* Mods have effect if at least a null-value-mod is
					 * to actually remove an existing attribute
					 */
					if ( strcasecmp ( mod->mod_type, attr->a_type ) == 0 ) {
						have_effect = 1; /* found one - mod has effect */
						goto done;
					}
					/* this mod type was not found in the entry - if we don't
					   find one of the other mod types, or if there are no more
					   mod types to look for, this mod does not apply */
					have_effect = 0;
				}
			}
		}
	}

done:

	/* Return true would let the flow continue along the old path before
	 * this function was added
	 */
	return have_effect;
}
