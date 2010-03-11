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

/* modrdn.c - ldbm backend modrdn routine */

#include "back-ldbm.h"

static const char *moddn_get_newdn(Slapi_PBlock *pb, Slapi_DN *dn_olddn, Slapi_DN *dn_newrdn, Slapi_DN *dn_newsuperiordn);
static void moddn_unlock_and_return_entries(backend *be,struct backentry **targetentry, struct backentry **existingentry);
static int moddn_newrdn_mods(Slapi_PBlock *pb, const char *olddn, struct backentry *ec, Slapi_Mods *smods, Slapi_Mods *smods_wsi, int is_repl_op);
static IDList *moddn_get_children(back_txn *ptxn, Slapi_PBlock *pb, backend *be, struct backentry *parententry, Slapi_DN *parentdn, struct backentry ***child_entries, struct backentry ***child_entry_copies);
static int moddn_rename_children(back_txn *ptxn, Slapi_PBlock *pb, backend *be, IDList *children, Slapi_DN *dn_parentdn, Slapi_DN *dn_newsuperiordn, struct backentry *child_entries[], struct backentry *child_entry_copies[]);
static int modrdn_rename_entry_update_indexes(back_txn *ptxn, Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *e, struct backentry *ec, Slapi_Mods *smods1, Slapi_Mods *smods2, Slapi_Mods *smods3);
static void mods_remove_nsuniqueid(Slapi_Mods *smods);

#define MOD_SET_ERROR(rc, error, count)                                        \
{                                                                              \
    (rc) = (error);                                                            \
    (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
}

int
ldbm_back_modrdn( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	struct ldbminfo		*li;
	struct backentry *e= NULL;
	struct backentry *ec= NULL;
	int ec_in_cache= 0;
	back_txn		txn;
	back_txnid		parent_txn;
	int			retval = -1;
	char			*msg;
	Slapi_Entry		*postentry = NULL;
	char			*errbuf = NULL;
	int disk_full = 0;
	int retry_count = 0;
	int ldap_result_code= LDAP_SUCCESS;
	char *ldap_result_message= NULL;
	char *ldap_result_matcheddn= NULL;
	struct backentry *parententry= NULL;
	struct backentry *newparententry= NULL;
	struct backentry *existingentry= NULL;
	modify_context parent_modify_context = {0};
	modify_context newparent_modify_context = {0};
	IDList *children= NULL;
	struct backentry **child_entries= NULL;
	struct backentry **child_entry_copies= NULL;
	Slapi_DN dn_olddn = {0};
	Slapi_DN dn_newdn = {0};
	Slapi_DN dn_newrdn = {0};
	Slapi_DN dn_newsuperiordn = {0};
    Slapi_DN dn_parentdn = {0};
	int rc;
	int isroot;
    LDAPMod **mods;
	Slapi_Mods smods_operation_wsi = {0};
	Slapi_Mods smods_generated = {0};
	Slapi_Mods smods_generated_wsi = {0};
	Slapi_Operation *operation;
	int dblock_acquired= 0;
	int is_replicated_operation= 0;
	int is_fixup_operation = 0;
	entry_address new_addr;
	entry_address *old_addr;
	entry_address oldparent_addr;
	entry_address *newsuperior_addr;
	char *dn;
	char ebuf[BUFSIZ];
	CSN *opcsn = NULL;

	/* sdn & parentsdn need to be initialized before "goto *_return" */
	slapi_sdn_init(&dn_newdn);
	slapi_sdn_init(&dn_parentdn);
	
    slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &dn );
	slapi_pblock_get( pb, SLAPI_BACKEND, &be);
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_PARENT_TXN, (void**)&parent_txn );
	slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation );
	is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);

	/* dblayer_txn_init needs to be called before "goto error_return" */
	dblayer_txn_init(li,&txn);

	if (pb->pb_conn)
	{
		slapi_log_error (SLAPI_LOG_TRACE, "ldbm_back_modrdn", "enter conn=%" NSPRIu64 " op=%d\n", pb->pb_conn->c_connid, operation->o_opid);
	}

	inst = (ldbm_instance *) be->be_instance_info;
	{
    	char *newrdn, *newsuperiordn;
    	slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn );
    	slapi_pblock_get( pb, SLAPI_MODRDN_NEWSUPERIOR, &newsuperiordn );
    	slapi_sdn_init_dn_byref(&dn_olddn,dn);
    	slapi_sdn_init_dn_byref(&dn_newrdn,newrdn);
    	slapi_sdn_init_dn_byref(&dn_newsuperiordn,newsuperiordn);
        slapi_sdn_get_parent(&dn_olddn,&dn_parentdn);
	}
	
	/* if old and new superior are equals, newsuperior should not be set
	 * Here we have to reset newsuperiordn in order to save processing and 
	 * avoid later deadlock when trying to fetch twice the same entry
	 */
	if (slapi_sdn_compare(&dn_newsuperiordn, &dn_parentdn) == 0)
	{
		slapi_sdn_done(&dn_newsuperiordn);
		slapi_sdn_init_dn_byref(&dn_newsuperiordn,NULL);
	}

	/* Replicated Operations are allowed to change the superior */
	if ( !is_replicated_operation && !slapi_sdn_isempty(&dn_newsuperiordn)) 
	{
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
	                  "server does not support moving of entries", 0, NULL );
		return( -1 );
	} 

	/* The dblock serializes writes to the database,
	 * which reduces deadlocking in the db code,
	 * which means that we run faster.
	 *
	 * But, this lock is re-enterant for the fixup
	 * operations that the URP code in the Replication
	 * plugin generates.
	 *
	 * Also some URP post-op operations are called after
	 * the backend has committed the change and released
	 * the dblock. Acquire the dblock again for them
	 * if OP_FLAG_ACTION_INVOKE_FOR_REPLOP is set.
	 */
	if(SERIALLOCK(li) && (!operation_is_flag_set(operation,OP_FLAG_REPL_FIXUP) || operation_is_flag_set(operation,OP_FLAG_ACTION_INVOKE_FOR_REPLOP)))
	{
		dblayer_lock_backend(be);
		dblock_acquired= 1;
	}

    /* Work out what the new name of the entry will be */
	{
	    const char *newdn= moddn_get_newdn(pb,&dn_olddn,&dn_newrdn,&dn_newsuperiordn);
    	slapi_sdn_set_dn_passin(&dn_newdn,newdn);
	}

    rc= 0;
    rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
    rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
    rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY);
    rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_TARGET_ENTRY);
    while(rc!=0)
	{
		/* JCM - copying entries can be expensive... should optimize */
		/* 
		 * Some present state information is passed through the PBlock to the
		 * backend pre-op plugin. To ensure a consistent snapshot of this state
		 * we wrap the reading of the entry with the dblock.
		 */
	    if(slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY))
		{
			const char *newdn = NULL;
			char * newrdn = NULL;

        	/* see if an entry with the new name already exists */
			done_with_pblock_entry(pb,SLAPI_MODRDN_EXISTING_ENTRY); /* Could be through this multiple times */
			slapi_sdn_done(&dn_newrdn);
			slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
			slapi_sdn_init_dn_byref(&dn_newrdn,newrdn);
			newdn= moddn_get_newdn(pb,&dn_olddn,&dn_newrdn,&dn_newsuperiordn);
			slapi_sdn_set_dn_passin(&dn_newdn,newdn);
			new_addr.dn = (char*)slapi_sdn_get_ndn (&dn_newdn);
			/* check dn syntax on newdn */
			ldap_result_code = slapi_dn_syntax_check(pb, new_addr.dn, 1);
			if (ldap_result_code)
			{
				ldap_result_code = LDAP_INVALID_DN_SYNTAX;
				slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
				goto error_return;
			}
			new_addr.uniqueid = NULL;
			ldap_result_code= get_copy_of_entry(pb, &new_addr, &txn, SLAPI_MODRDN_EXISTING_ENTRY, 0);
			if(ldap_result_code==LDAP_OPERATIONS_ERROR ||
			   ldap_result_code==LDAP_INVALID_DN_SYNTAX)
			{
				goto error_return;
			}
		}
	    if(slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY))
		{
        	/* find and lock the old parent entry */
			done_with_pblock_entry(pb,SLAPI_MODRDN_PARENT_ENTRY); /* Could be through this multiple times */
			oldparent_addr.dn = (char*)slapi_sdn_get_ndn (&dn_parentdn);
			oldparent_addr.uniqueid = NULL;			
			ldap_result_code= get_copy_of_entry(pb, &oldparent_addr, &txn, SLAPI_MODRDN_PARENT_ENTRY, !is_replicated_operation);
		}
	    if(slapi_sdn_get_ndn(&dn_newsuperiordn)!=NULL && slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY))
		{
    	    /* JCM - Could check that this really is a new superior, and not the same old one. Compare parentdn & newsuperior */
        	/* find and lock the new parent entry */
			done_with_pblock_entry(pb,SLAPI_MODRDN_NEWPARENT_ENTRY); /* Could be through this multiple times */
			/* JCMREPL - If this is a replicated operation then should fetch new superior with uniqueid */
			slapi_pblock_get (pb, SLAPI_MODRDN_NEWSUPERIOR_ADDRESS, &newsuperior_addr);
			ldap_result_code= get_copy_of_entry(pb, newsuperior_addr, &txn, SLAPI_MODRDN_NEWPARENT_ENTRY, !is_replicated_operation);
		}
	    if(slapi_isbitset_int(rc,SLAPI_RTN_BIT_FETCH_TARGET_ENTRY))
		{
        	/* find and lock the entry we are about to modify */
			done_with_pblock_entry(pb,SLAPI_MODRDN_TARGET_ENTRY); /* Could be through this multiple times */
			slapi_pblock_get (pb, SLAPI_TARGET_ADDRESS, &old_addr);
			ldap_result_code= get_copy_of_entry(pb, old_addr, &txn, SLAPI_MODRDN_TARGET_ENTRY, !is_replicated_operation);
			if(ldap_result_code==LDAP_OPERATIONS_ERROR)
			{
				/* JCM - Usually the call to find_entry2modify would generate the result code. */
				/* JCM !!! */
        	    goto error_return;
			}
		}
        /* Call the Backend Pre ModRDN plugins */
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
    	rc= plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_MODRDN_FN);
    	if(rc==-1)
    	{
			/* 
			 * Plugin indicated some kind of failure,
			 * or that this Operation became a No-Op.
			 */
			slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
    	    goto error_return;
    	}
		/*
		 * (rc!=-1) means that the plugin changed things, so we go around
		 * the loop once again to get the new present state.
		 */
		/* JCMREPL - Warning: A Plugin could cause an infinite loop by always returning a result code that requires some action. */
	}

	/* find and lock the entry we are about to modify */
	/* JCMREPL - Argh, what happens about the stinking referrals? */
	slapi_pblock_get (pb, SLAPI_TARGET_ADDRESS, &old_addr);
	e = find_entry2modify( pb, be, old_addr, NULL );
	if ( e == NULL )
	{
	    ldap_result_code= -1;
	    goto error_return; /* error result sent by find_entry2modify() */
	}
    /* Check that an entry with the same DN doesn't already exist. */
	{
		Slapi_Entry *entry;
		slapi_pblock_get( pb, SLAPI_MODRDN_EXISTING_ENTRY, &entry);
		if((entry != NULL) && 
			/* allow modrdn even if the src dn and dest dn are identical */
		   (0 != slapi_sdn_compare((const Slapi_DN *)&dn_newdn,
								   (const Slapi_DN *)&dn_olddn)))
		{
			ldap_result_code= LDAP_ALREADY_EXISTS;
			goto error_return;
		}
	}

	/* Fetch and lock the parent of the entry that is moving */
	oldparent_addr.dn = (char*)slapi_sdn_get_dn (&dn_parentdn);
	oldparent_addr.uniqueid = NULL;    		
	parententry = find_entry2modify_only( pb, be, &oldparent_addr, NULL );
	modify_init(&parent_modify_context,parententry);

	/* Fetch and lock the new parent of the entry that is moving */    		
	if(slapi_sdn_get_ndn(&dn_newsuperiordn)!=NULL)
	{
		slapi_pblock_get (pb, SLAPI_MODRDN_NEWSUPERIOR_ADDRESS, &newsuperior_addr);
		newparententry = find_entry2modify_only( pb, be, newsuperior_addr, NULL);
		modify_init(&newparent_modify_context,newparententry);
	}

	opcsn = operation_get_csn (operation);
	if (!is_fixup_operation)
	{
		if ( opcsn == NULL && operation->o_csngen_handler)
		{
			/*
			 * Current op is a user request. Opcsn will be assigned
			 * if the dn is in an updatable replica.
			 */
			opcsn = entry_assign_operation_csn ( pb, e->ep_entry, parententry ? parententry->ep_entry : NULL );
		}
		if ( opcsn != NULL )
		{
			entry_set_maxcsn (e->ep_entry, opcsn);
		}
	}

	/*
	 * Now that we have the old entry, we reset the old DN and recompute
	 * the new DN.  Why?  Because earlier when we computed the new DN, we did
	 * not have the old entry, so we used the DN that was presented as the
	 * target DN in the ModRDN operation itself, and we would prefer to
	 * preserve the case and spacing that are in the actual entry's DN
	 * instead.  Otherwise, a ModRDN operation will potentially change an
	 * entry's entire DN (at least with respect to case and spacing).
	 */
	slapi_sdn_copy( slapi_entry_get_sdn_const( e->ep_entry ), &dn_olddn );
	if (newparententry != NULL) {
		/* don't forget we also want to preserve case of new superior */
		slapi_sdn_copy(slapi_entry_get_sdn_const(newparententry->ep_entry), &dn_newsuperiordn);
	}
	slapi_sdn_set_dn_passin(&dn_newdn,
			moddn_get_newdn(pb, &dn_olddn, &dn_newrdn, &dn_newsuperiordn));

	/* Check that we're allowed to add an entry below the new superior */
	if ( newparententry == NULL )
	{
	    /* There may not be a new parent because we don't intend there to be one. */
		if(slapi_sdn_get_ndn(&dn_newsuperiordn)!=NULL)
		{
			/* If the new entry is to be a suffix, and we're root, then it's OK that the new parent doesn't exist */
			if(!(slapi_dn_isbesuffix(pb,slapi_sdn_get_ndn(&dn_newdn)) && isroot))
			{
				/* Here means that we didn't find the parent */
				int err = 0;
				Slapi_DN ancestordn = {0};
				struct backentry *ancestorentry;
				ancestorentry= dn2ancestor(be,&dn_newdn,&ancestordn,&txn,&err);
				cache_return( &inst->inst_cache, &ancestorentry );
				ldap_result_matcheddn= slapi_ch_strdup((char *) slapi_sdn_get_dn(&ancestordn));
				ldap_result_code= LDAP_NO_SUCH_OBJECT;
				LDAPDebug( LDAP_DEBUG_TRACE, "New superior does not exist matched %s, newsuperior = %s\n", 
					ldap_result_matcheddn == NULL ? "NULL" : ldap_result_matcheddn, slapi_sdn_get_ndn(&dn_newsuperiordn), 0 );
				slapi_sdn_done(&ancestordn);
				goto error_return;
       		}
		}
	}
	else
	{
		ldap_result_code= plugin_call_acl_plugin (pb, newparententry->ep_entry, NULL, NULL, SLAPI_ACL_ADD, ACLPLUGIN_ACCESS_DEFAULT, &errbuf );
		if ( ldap_result_code != LDAP_SUCCESS )
		{
		    ldap_result_message= errbuf;
			LDAPDebug( LDAP_DEBUG_TRACE, "No access to new superior.\n", 0, 0, 0 );
			goto error_return;
		}
    }

    /* Check that the target entry has a parent */
	if ( parententry == NULL )
	{
	    /* If the entry a suffix, and we're root, then it's OK that the parent doesn't exist */
		if(!(slapi_dn_isbesuffix(pb,slapi_sdn_get_ndn(&dn_olddn)) && isroot))
		{
			/* Here means that we didn't find the parent */
			if (parententry && parententry->ep_entry)
			{
				ldap_result_matcheddn = slapi_ch_strdup((char *) slapi_entry_get_dn(parententry->ep_entry));
			}
			else
			{
				ldap_result_matcheddn = "NULL";
			}
			ldap_result_code= LDAP_NO_SUCH_OBJECT;
			LDAPDebug( LDAP_DEBUG_TRACE, "Parent does not exist matched %s, parentdn = %s\n", 
				ldap_result_matcheddn, slapi_sdn_get_ndn(&dn_parentdn), 0 );
			goto error_return;
		}
	}

	/* Replicated Operations are allowed to rename entries with children */
	if ( !is_replicated_operation && slapi_entry_has_children( e->ep_entry ))
	{
	   ldap_result_code = LDAP_NOT_ALLOWED_ON_NONLEAF;
	   goto error_return;
	} 
	
	 
	/*
	 * JCM - All the child entries must be locked in the cache, so the size of
	 * subtree that can be renamed is limited by the cache size.
	 */

	/* Save away a copy of the entry, before modifications */
	slapi_pblock_set( pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup( e->ep_entry ));
	
	/* create a copy of the entry and apply the changes to it */
	if ( (ec = backentry_dup( e )) == NULL )
	{
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	/* JCMACL - Should be performed before the child check. */
	/* JCMACL - Why is the check performed against the copy, rather than the existing entry? */
	/*ldap_result_code = plugin_call_acl_plugin (pb, ec->ep_entry, NULL , NULL , SLAPI_ACL_WRITE, ACLPLUGIN_ACCESS_DEFAULT,  &errbuf );*/
	ldap_result_code = plugin_call_acl_plugin (pb, ec->ep_entry,
							NULL /*attr*/, NULL /*value*/, SLAPI_ACL_WRITE,
							ACLPLUGIN_ACCESS_MODRDN,  &errbuf );
	if ( ldap_result_code != LDAP_SUCCESS )
	{
		goto error_return;
	}

	slapi_entry_set_sdn( ec->ep_entry, &dn_newdn );

	/* create it in the cache - prevents others from creating it */
	if (( cache_add_tentative( &inst->inst_cache, ec, NULL ) != 0 ) &&
		/* allow modrdn even if the src dn and dest dn are identical */
		( 0 != slapi_sdn_compare((const Slapi_DN *)&dn_newdn,
							     (const Slapi_DN *)&dn_olddn)) )
	{
		/* somebody must've created it between dn2entry() and here */
		/* JCMREPL - Hmm... we can't permit this to happen...? */
		ldap_result_code= LDAP_ALREADY_EXISTS;
		goto error_return;
	}
	ec_in_cache= 1;

    /* Build the list of modifications required to the existing entry */
	{
		slapi_mods_init(&smods_generated,4);
		slapi_mods_init(&smods_generated_wsi,4);
		ldap_result_code = moddn_newrdn_mods(pb, slapi_sdn_get_ndn(&dn_olddn), ec, &smods_generated, &smods_generated_wsi, 
						     is_replicated_operation);
		if (ldap_result_code != LDAP_SUCCESS) {
			if (ldap_result_code == LDAP_UNWILLING_TO_PERFORM)
				ldap_result_message = "Modification of old rdn attribute type not allowed.";
			goto error_return;
		}
		/*
		 * Remove the old entrydn index entry, and add the new one.
		 */
		slapi_mods_add( &smods_generated, LDAP_MOD_DELETE, "entrydn", strlen(backentry_get_ndn(e)), backentry_get_ndn(e));
		slapi_mods_add( &smods_generated, LDAP_MOD_REPLACE, "entrydn", strlen(backentry_get_ndn(ec)), backentry_get_ndn(ec));

		/*
		 * Update parentid if we have a new superior.
		 */
		if(slapi_sdn_get_dn(&dn_newsuperiordn)!=NULL) {
			char buf[40]; /* Enough for an ID */
			
			if (parententry != NULL) {
				sprintf( buf, "%lu", (u_long)parententry->ep_id );
				slapi_mods_add_string(&smods_generated, LDAP_MOD_DELETE, "parentid", buf);
			}
			if (newparententry != NULL) {
				sprintf( buf, "%lu", (u_long)newparententry->ep_id );
				slapi_mods_add_string(&smods_generated, LDAP_MOD_REPLACE, "parentid", buf);
			}
		}
	}

   	slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
   	slapi_mods_init_byref(&smods_operation_wsi,mods);

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

	/*
     * First, we apply the generated mods that do not involve any state information.
	 */
	if ( entry_apply_mods( ec->ep_entry, slapi_mods_get_ldapmods_byref(&smods_generated) ) != 0 )
	{
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		LDAPDebug( LDAP_DEBUG_TRACE, "ldbm_modrdn: entry_apply_mods failed for entry %s\n",
				   escape_string(slapi_entry_get_dn_const(ec->ep_entry), ebuf), 0, 0);
		goto error_return;
	}

	/*
     * Now we apply the generated mods that do involve state information.
	 */
	if (slapi_mods_get_num_mods(&smods_generated_wsi)>0)
	{
		if (entry_apply_mods_wsi(ec->ep_entry, &smods_generated_wsi, operation_get_csn(operation), is_replicated_operation)!=0)
		{
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			LDAPDebug( LDAP_DEBUG_TRACE, "ldbm_modrdn: entry_apply_mods_wsi failed for entry %s\n",
					   escape_string(slapi_entry_get_dn_const(ec->ep_entry), ebuf), 0, 0);
			goto error_return;
		}
	}

	/*
     * Now we apply the operation mods that do involve state information.
	 * (Operational attributes).
	 * The following block looks redundent to the one above. But it may
	 * be necessary - check the comment for version 1.3.16.22.2.76 of
	 * this file and compare that version with its previous one.
	 */
	if (slapi_mods_get_num_mods(&smods_operation_wsi)>0)
	{
		if (entry_apply_mods_wsi(ec->ep_entry, &smods_operation_wsi, operation_get_csn(operation), is_replicated_operation)!=0)
		{
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			LDAPDebug( LDAP_DEBUG_TRACE, "ldbm_modrdn: entry_apply_mods_wsi (operational attributes) failed for entry %s\n",
					   escape_string(slapi_entry_get_dn_const(ec->ep_entry), ebuf), 0, 0);
			goto error_return;
		}
	}
	/* check that the entry still obeys the schema */
	if ( slapi_entry_schema_check( pb, ec->ep_entry ) != 0 ) {
		ldap_result_code = LDAP_OBJECT_CLASS_VIOLATION;
		slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
		goto error_return;
	}

	/* Check attribute syntax if any new values are being added for the new RDN */
	if (slapi_mods_get_num_mods(&smods_operation_wsi)>0)
	{
		if (slapi_mods_syntax_check(pb, smods_generated_wsi.mods, 0) != 0)
		{
			ldap_result_code = LDAP_INVALID_SYNTAX;
			slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
			goto error_return;
		}
	}

	/*
	 * Update the DN CSN of the entry.
	 */
	entry_add_dncsn(ec->ep_entry,operation_get_csn(operation));
	entry_add_rdn_csn(ec->ep_entry,operation_get_csn(operation));

    /*
	 * If the entry has a new superior then the subordinate count
	 * of the parents must be updated.
	 */	
	if(slapi_sdn_get_dn(&dn_newsuperiordn)!=NULL)
	{
    	/* 
		 * Update the subordinate count of the parents to reflect the moved child.
		 */
    	if ( parententry!=NULL )
		{
    		retval = parent_update_on_childchange(&parent_modify_context,2,NULL); /* 2==delete */
    		/* The parent modify context now contains info needed later */
    		if (0 != retval)
			{
    			goto error_return;
    		}
    	}
    	if ( newparententry!=NULL )
		{
    		retval = parent_update_on_childchange(&newparent_modify_context,1,NULL); /* 1==add */
    		/* The newparent modify context now contains info needed later */
    		if (0 != retval)
			{
    			goto error_return;
    		}
    	}
	}

    /*
	 * If the entry has children then we're going to have to rename them all.
	 */
	if (slapi_entry_has_children( e->ep_entry ))
	{
	    /* JCM - This is where the subtree lock will appear */
		children= moddn_get_children(&txn, pb, be, e, &dn_olddn, &child_entries, &child_entry_copies);
		/* JCM - Shouldn't we perform an access control check on all the children. */
		/* JCMREPL - But, the replication client has total rights over its subtree, so no access check needed. */
		/* JCM - A subtree move could break ACIs, static groups, and dynamic groups. */
	}

	/* 
	 * So, we believe that no code up till here actually added anything
	 * to persistent store. From now on, we're transacted
	 */
	for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++)
	{
		if (retry_count > 0)
		{
			dblayer_txn_abort(li,&txn);
			/* We're re-trying */
			LDAPDebug( LDAP_DEBUG_TRACE, "Modrdn Retrying Transaction\n", 0, 0, 0 );
		}
		retval = dblayer_txn_begin(li,parent_txn,&txn);
		if (0 != retval) {
			ldap_result_code= LDAP_OPERATIONS_ERROR;
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
			goto error_return;
		}

    	/*
    	 * Update the indexes for the entry.
    	 */
        retval = modrdn_rename_entry_update_indexes(&txn, pb, li, e, ec, &smods_generated, &smods_generated_wsi, &smods_operation_wsi);
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Retry txn */
			continue;
		}
		if (retval != 0 )
		{
			LDAPDebug( LDAP_DEBUG_TRACE, "modrdn_rename_entry_update_indexes failed, err=%d %s\n",
					   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
			MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
			goto error_return;
		}
		
		/*
		 * add new name to index
		 */
		{
		    char **rdns;
			int i;
			if ( (rdns = ldap_explode_rdn( slapi_sdn_get_dn(&dn_newrdn), 0 )) != NULL ) 
			{
				for ( i = 0; rdns[i] != NULL; i++ ) 
				{
					char *type;
					Slapi_Value *svp[2];
					Slapi_Value sv;
					memset(&sv,0,sizeof(Slapi_Value));
					if ( slapi_rdn2typeval( rdns[i], &type, &sv.bv ) != 0 ) 
					{
						char ebuf[ BUFSIZ ];
						LDAPDebug( LDAP_DEBUG_ANY, "modrdn: rdn2typeval (%s) failed\n",
								   escape_string( rdns[i], ebuf ), 0, 0 );
						if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
						MOD_SET_ERROR(ldap_result_code, 
									  LDAP_OPERATIONS_ERROR, retry_count);
						goto error_return;
					}
					svp[0] = &sv;
					svp[1] = NULL;
					retval = index_addordel_values_sv( be, type, svp, NULL, ec->ep_id, BE_INDEX_ADD, &txn );
					if (DB_LOCK_DEADLOCK == retval)
					{
						/* To retry txn, once break "for loop" */
						break;
					}
					else if (retval != 0 )
					{
						LDAPDebug( LDAP_DEBUG_ANY, "modrdn: could not add new value to index, err=%d %s\n",
								   retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
						if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
						MOD_SET_ERROR(ldap_result_code, 
									  LDAP_OPERATIONS_ERROR, retry_count);
						goto error_return;
					}
				}
				slapi_ldap_value_free( rdns );
				if (DB_LOCK_DEADLOCK == retval)
				{
					/* Retry txn */
					continue;
				}
			}
		}
    	if (slapi_sdn_get_dn(&dn_newsuperiordn)!=NULL)
    	{
    		/* Push out the db modifications from the parent entry */
    		retval = modify_update_all(be, pb, &parent_modify_context, &txn);
			if (DB_LOCK_DEADLOCK == retval)
    		{
    			/* Retry txn */
    			continue;
    		}
			else if (0 != retval)
    		{
				LDAPDebug( LDAP_DEBUG_ANY, "modrdn: "
						   "could not update parent, err=%d %s\n", retval,
						   (msg = dblayer_strerror( retval )) ? msg : "", 0 );
				if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
				MOD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
				goto error_return;
    		}
    		/* Push out the db modifications from the new parent entry */
			else if(retval==0)
    		{
    			retval = modify_update_all(be, pb, &newparent_modify_context, &txn);
				if (DB_LOCK_DEADLOCK == retval)
    			{
    				/* Retry txn */
        			continue;
    			}
    			if (0 != retval)
    			{
    				LDAPDebug( LDAP_DEBUG_TRACE, "modrdn: "
							   "could not update parent, err=%d %s\n", retval,
							   (msg = dblayer_strerror( retval )) ? msg : "",
							   0 );
					if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
					MOD_SET_ERROR(ldap_result_code, 
								  LDAP_OPERATIONS_ERROR, retry_count);
					goto error_return;
    			}
    		}
    	}

        /*
         * Update ancestorid index.
         */
    	if (slapi_sdn_get_dn(&dn_newsuperiordn)!=NULL) {
            retval = ldbm_ancestorid_move_subtree(be, &dn_olddn, &dn_newdn, e->ep_id, children, &txn);
            if (retval != 0) {
                if (retval == DB_LOCK_DEADLOCK) continue;
                if (retval == DB_RUNRECOVERY || LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
				MOD_SET_ERROR(ldap_result_code, 
							  LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        }

    	/*
    	 * If the entry has children, then rename them all.
    	 */
    	if (children!=NULL)
    	{
            retval= moddn_rename_children( &txn, pb, be, children, &dn_olddn, &dn_newdn, child_entries, child_entry_copies);
    	}
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Retry txn */
			continue;
		}
		if (retval != 0)
		{
			if (retval == DB_RUNRECOVERY || LDBM_OS_ERR_IS_DISKFULL(retval))
				disk_full = 1;
			MOD_SET_ERROR(ldap_result_code, 
						  LDAP_OPERATIONS_ERROR, retry_count);
			goto error_return;
		}

		break; /* retval==0, Done, Terminate the loop */
	}
	if (retry_count == RETRY_TIMES)
	{
		/* Failed */
		LDAPDebug( LDAP_DEBUG_ANY, "Retry count exceeded in modrdn\n", 0, 0, 0 );
		ldap_result_code= LDAP_OPERATIONS_ERROR;
		goto error_return;
	}

	postentry = slapi_entry_dup( ec->ep_entry );

    if(parententry!=NULL)
	{
    	modify_switch_entries( &parent_modify_context,be);
	}
    if(newparententry!=NULL)
	{
    	modify_switch_entries( &newparent_modify_context,be);
	}

	retval = dblayer_txn_commit(li,&txn);
	if (0 != retval)
	{
		if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
		MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
		goto error_return;
	}

    if(children!=NULL)
    {
        int i=0;
        for (; child_entries[i]!=NULL; i++) {
            cache_unlock_entry( &inst->inst_cache, child_entries[i]) ;
            cache_return( &inst->inst_cache, &(child_entries[i]) );
            cache_return( &inst->inst_cache, &(child_entry_copies[i]) );
        }
    }

    retval= 0;
#if 0 /* this new entry in the cache can be used for future; don't remove it */
    /* remove from cache so that memory can be freed by cache_return */
    if (ec_in_cache) {
        cache_remove(&inst->inst_cache, ec);
    }
#endif
    goto common_return;

error_return:
	/* result already sent above - just free stuff */
	if ( NULL != postentry )
	{
		slapi_entry_free( postentry );
		postentry= NULL;
	}
	if( ec!=NULL ) {
	    if (ec_in_cache) {
        	cache_remove(&inst->inst_cache, ec);
	    } else {
        	backentry_free( &ec );
	    }
	}
	if(children!=NULL)
	{
	    int i=0;
	    for(;child_entries[i]!=NULL;i++) {
        	cache_unlock_entry(&inst->inst_cache, child_entries[i]);
			cache_return(&inst->inst_cache, &(child_entries[i]));
			if (child_entry_copies[i] != NULL) {
				cache_remove(&inst->inst_cache, child_entry_copies[i]);
				cache_return( &inst->inst_cache, &(child_entry_copies[i]) );
			}
	    }
	}


	if (retval == DB_RUNRECOVERY) {
	    dblayer_remember_disk_filled(li);
	    ldbm_nasty("ModifyDN",82,retval);
	    disk_full = 1;
	}

	if (disk_full) 
	{
		retval = return_on_disk_full(li);
	}
	else 
	{
		/* It is safer not to abort when the transaction is not started. */
		if (retry_count > 0) {
	    	dblayer_txn_abort(li,&txn); /* abort crashes in case disk full */
		}
		retval= SLAPI_FAIL_GENERAL;
	}

common_return:

	/* Free up the resource we don't need any more */
	if(ec_in_cache) {
	    cache_return( &inst->inst_cache, &ec );
	}

	/*
	 * The bepostop is called even if the operation fails.
	 */
	plugin_call_plugins (pb, SLAPI_PLUGIN_BE_POST_MODRDN_FN);

	if (ldap_result_code!=-1)
	{
		slapi_send_ldap_result( pb, ldap_result_code, ldap_result_matcheddn,
					ldap_result_message, 0,NULL );
	}
	slapi_mods_done(&smods_operation_wsi);
	slapi_mods_done(&smods_generated);
	slapi_mods_done(&smods_generated_wsi);
    moddn_unlock_and_return_entries(be,&e,&existingentry);
    slapi_ch_free((void**)&child_entries);
    slapi_ch_free((void**)&child_entry_copies);
    if (ldap_result_matcheddn && 0 != strcmp(ldap_result_matcheddn, "NULL"))
        slapi_ch_free((void**)&ldap_result_matcheddn);
	idl_free(children);
    slapi_sdn_done(&dn_olddn);
    slapi_sdn_done(&dn_newdn);
    slapi_sdn_done(&dn_newrdn);
    slapi_sdn_done(&dn_newsuperiordn);
	slapi_sdn_done(&dn_parentdn);
	modify_term(&parent_modify_context,be);
	modify_term(&newparent_modify_context,be);
	done_with_pblock_entry(pb,SLAPI_MODRDN_EXISTING_ENTRY);
	done_with_pblock_entry(pb,SLAPI_MODRDN_PARENT_ENTRY);
	done_with_pblock_entry(pb,SLAPI_MODRDN_NEWPARENT_ENTRY);
	done_with_pblock_entry(pb,SLAPI_MODRDN_TARGET_ENTRY);
	if(dblock_acquired)
	{
		dblayer_unlock_backend(be);
	}
	slapi_ch_free((void**)&errbuf);
	if (retval == 0 && opcsn != NULL && !is_fixup_operation)
	{
		slapi_pblock_set(pb, SLAPI_URP_NAMING_COLLISION_DN, slapi_ch_strdup (dn));
	}
    slapi_pblock_set( pb, SLAPI_ENTRY_POST_OP, postentry );
	if (pb->pb_conn)
	{
		slapi_log_error (SLAPI_LOG_TRACE, "ldbm_back_modrdn", "leave conn=%" NSPRIu64 " op=%d\n", pb->pb_conn->c_connid, operation->o_opid);
	}
	return retval;
}

/*
 * Work out what the new DN of the entry will be.
 */
static const char *
moddn_get_newdn(Slapi_PBlock *pb, Slapi_DN *dn_olddn, Slapi_DN *dn_newrdn, Slapi_DN *dn_newsuperiordn)
{
    char *newdn;
	const char *newrdn= slapi_sdn_get_dn(dn_newrdn);
	const char *newsuperiordn= slapi_sdn_get_dn(dn_newsuperiordn);
	
    if( newsuperiordn!=NULL)
	{
    	/* construct the new dn */
		if(slapi_dn_isroot(newsuperiordn))
		{
		    newdn= slapi_ch_strdup(newrdn);
		}
		else
		{
            newdn= slapi_dn_plus_rdn(newsuperiordn, newrdn); /* JCM - Use Slapi_RDN */
		}
	}
	else
	{
    	/* construct the new dn */
		char *pdn;
		const char *dn= slapi_sdn_get_dn(dn_olddn);
    	pdn = slapi_dn_beparent( pb, dn );
    	if ( pdn != NULL )
    	{
            newdn= slapi_dn_plus_rdn(pdn, newrdn); /* JCM - Use Slapi_RDN */
    	}
    	else
    	{
    		newdn= slapi_ch_strdup(newrdn);
    	}
    	slapi_ch_free( (void**)&pdn );
	}
	return newdn;
}

/*
 * Return the entries to the cache.
 */
static void
moddn_unlock_and_return_entries(
	backend *be,
    struct backentry **targetentry, 
	struct backentry **existingentry)
    {
        ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;

        /* Something bad happened so we should give back all the entries */
        if ( *targetentry!=NULL ) {
            cache_unlock_entry(&inst->inst_cache, *targetentry);
            cache_return( &inst->inst_cache, targetentry );
            *targetentry= NULL;
        }
        if ( *existingentry!=NULL ) {
            cache_return( &inst->inst_cache, existingentry );
            *existingentry= NULL;
        }
    }


/*
 * JCM - There was a problem with multi-valued RDNs where
 * JCM - there was an intersection of the two sets RDN Components
 * JCM - and the deleteoldrdn flag was set. A value was deleted
 * JCM - but not re-added because the value is found to already
 * JCM - exist.
 *
 * This function returns 1 if it is necessary to add an RDN value
 * to the entry.  This is necessary if either:
 * 1 the attribute or the value is not present in the entry, or
 * 2 the attribute is present, deleteoldrdn is set, and the RDN value
 *   is in the deleted list.  
 * 
 * For example, suppose you rename cn=a to cn=a+sn=b.  The cn=a value
 * is removed from the entry and then readded.
 */

static int
moddn_rdn_add_needed (
	struct backentry *ec,
	char *type,
	struct berval *bvp,
	int deleteoldrdn,
	Slapi_Mods *smods_wsi
)
{
    Slapi_Attr *attr;
    LDAPMod *mod;

    if (slapi_entry_attr_find(ec->ep_entry, type, &attr) != 0 ||
		slapi_attr_value_find( attr, bvp ) != 0 )
	{
		return 1;
	}
    
    if (deleteoldrdn == 0) return 0;
    
    /* in a multi-valued RDN, the RDN value might have been already 
     * put on the smods_wsi list to be deleted, yet might still be 
     * in the target RDN.
     */
    
    for (mod = slapi_mods_get_first_mod(smods_wsi);
		 mod != NULL;
		 mod = slapi_mods_get_next_mod(smods_wsi)) {
		if (SLAPI_IS_MOD_DELETE(mod->mod_op) && 
			(strcasecmp(mod->mod_type, type) == 0) &&
			(mod->mod_bvalues != NULL) && 
			(slapi_attr_value_cmp(attr, *mod->mod_bvalues, bvp) == 0)) {
			return 1;
		}
    }
    
    return 0;
}

/*
 * Build the list of modifications to apply to the Existing Entry
 * With State Information:
 * - delete old rdn values from the entry if deleteoldrdn is set
 * - add new rdn values to the entry
 * Without State Information
 * - No changes
 */
static int
moddn_newrdn_mods(Slapi_PBlock *pb, const char *olddn, struct backentry *ec, Slapi_Mods *smods, Slapi_Mods *smods_wsi, int is_repl_op)
{
    char ebuf[BUFSIZ];
    char **rdns = NULL;
    char **dns = NULL;
    int deleteoldrdn;
    char *type = NULL;
    char *dn = NULL;
    char *newrdn = NULL;
    int i;
    struct berval *bvps[2];
    struct berval bv;
	
    bvps[0] = &bv;
    bvps[1] = NULL;
	
    /* slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &dn ); */
    slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn );
    slapi_pblock_get( pb, SLAPI_MODRDN_DELOLDRDN, &deleteoldrdn );
	

    /*
     * This loop removes the old RDN of the existing entry. 
     */
    if (deleteoldrdn) {
	int baddn = 0; /* set to true if could not parse dn */
	int badrdn = 0; /* set to true if could not parse rdn */
	dn = slapi_ch_strdup(olddn);
	dns = ldap_explode_dn( dn, 0 );
	if ( dns != NULL )
	{
		rdns = ldap_explode_rdn( dns[0], 0 );
		if ( rdns != NULL )
		{
			for ( i = 0; rdns[i] != NULL; i++ )
			{
				/* delete from entry attributes */
				if ( deleteoldrdn && slapi_rdn2typeval( rdns[i], &type, &bv ) == 0 )
				{
					/* check if user is allowed to modify the specified attribute */
					/*
					 * It would be better to do this check in the front end
					 * end inside op_shared_rename(), but unfortunately we
					 * don't have access to the target entry there.
					 */
					if (!op_shared_is_allowed_attr (type, is_repl_op))
					{
						slapi_ldap_value_free( rdns );
						slapi_ldap_value_free( dns );
						slapi_ch_free_string(&dn);
						return LDAP_UNWILLING_TO_PERFORM;
					}
					if (strcasecmp (type, SLAPI_ATTR_UNIQUEID) != 0)
						slapi_mods_add_modbvps( smods_wsi, LDAP_MOD_DELETE, type, bvps );
				}
			}
			slapi_ldap_value_free( rdns );
		}
	    else
	    {
			badrdn = 1;
	    }
		slapi_ldap_value_free( dns );
	}
	else
	{
	    baddn = 1;
	}
	slapi_ch_free_string(&dn);
    
	if ( baddn || badrdn )
	{
	    LDAPDebug( LDAP_DEBUG_TRACE, "moddn_newrdn_mods failed: olddn=%s baddn=%d badrdn=%d\n",
				   escape_string(olddn, ebuf), baddn, badrdn);
	    return LDAP_OPERATIONS_ERROR;
	}
    }
    /*
     * add new RDN values to the entry (non-normalized)
     */
    rdns = ldap_explode_rdn( newrdn, 0 );
    if ( rdns != NULL )
    {
        for ( i = 0; rdns[i] != NULL; i++ )
        {
            if ( slapi_rdn2typeval( rdns[i], &type, &bv ) != 0) {
                continue;
            }
            
            /* add to entry if it's not already there or if was
             * already deleted 
             */
            if (moddn_rdn_add_needed(ec, type, &bv,
                                     deleteoldrdn,
                                     smods_wsi) == 1) {
                slapi_mods_add_modbvps( smods_wsi, LDAP_MOD_ADD, type, bvps );
            }
        }
        slapi_ldap_value_free( rdns );
    }
    else
    {
	LDAPDebug( LDAP_DEBUG_TRACE, "moddn_newrdn_mods failed: could not parse new rdn %s\n",
		   escape_string(newrdn, ebuf), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }
    
    return LDAP_SUCCESS;
}

static void
mods_remove_nsuniqueid(Slapi_Mods *smods)
{
	int i;

	LDAPMod **mods = slapi_mods_get_ldapmods_byref(smods);
	for ( i = 0; mods[i] != NULL; i++ ) {
		if (!strcasecmp(mods[i]->mod_type, SLAPI_ATTR_UNIQUEID)) {
			mods[i]->mod_op = LDAP_MOD_IGNORE;
		}
	}
}


/*
 * Update the indexes to reflect the DN change made.
 * e is the entry before, ec the entry after.
 * mods contains the list of attribute change made.
 */
static int
modrdn_rename_entry_update_indexes(back_txn *ptxn, Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *e, struct backentry *ec, Slapi_Mods *smods1, Slapi_Mods *smods2, Slapi_Mods *smods3)
{
	backend *be;
	ldbm_instance *inst;
	int retval= 0;
	char *msg;
	Slapi_Operation *operation;
	int is_ruv = 0;                 /* True if the current entry is RUV */

	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
	inst = (ldbm_instance *) be->be_instance_info;

	/*
	 * Update the ID to Entry index. 
	 * Note that id2entry_add replaces the entry, so the Entry ID stays the same.
	 */
	retval = id2entry_add( be, ec, ptxn );
	if (DB_LOCK_DEADLOCK == retval)
	{
		/* Retry txn */
		goto error_return;
	}
	if (retval != 0)
	{
		LDAPDebug( LDAP_DEBUG_ANY, "id2entry_add failed, err=%d %s\n", retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
		goto error_return;
	}
	if(smods1!=NULL && slapi_mods_get_num_mods(smods1)>0)
	{
		/*
		 * update the indexes: lastmod, rdn, etc.
		 */
		retval = index_add_mods( be, (const LDAPMod **)slapi_mods_get_ldapmods_byref(smods1), e, ec, ptxn );
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Retry txn */
			goto error_return;
		}
		if (retval != 0)
		{
			LDAPDebug( LDAP_DEBUG_TRACE, "index_add_mods 1 failed, err=%d %s\n", retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			goto error_return;
		}
	}
	if(smods2!=NULL && slapi_mods_get_num_mods(smods2)>0)
	{
		/* 
		 * smods2 contains the state generated mods. One of them might be the removal of a "nsuniqueid" rdn component
		 * previously gnerated through a conflict resolution. We need to make sure we don't remove the index for "nsuniqueid"
		 * so let's get it out from the mods before calling index_add_mods... 
		 */
		mods_remove_nsuniqueid(smods2);
		/*
		 * update the indexes: lastmod, rdn, etc.
		 */
		retval = index_add_mods( be, (const LDAPMod **)slapi_mods_get_ldapmods_byref(smods2), e, ec, ptxn );
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Retry txn */
			goto error_return;
		}
		if (retval != 0)
		{
			LDAPDebug( LDAP_DEBUG_TRACE, "index_add_mods 2 failed, err=%d %s\n", retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			goto error_return;
		}
	}
	if(smods3!=NULL && slapi_mods_get_num_mods(smods3)>0)
	{
		/*
		 * update the indexes: lastmod, rdn, etc.
		 */
		retval = index_add_mods( be, (const LDAPMod **)slapi_mods_get_ldapmods_byref(smods3), e, ec, ptxn );
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Retry txn */
			goto error_return;
		}
		if (retval != 0)
		{
			LDAPDebug( LDAP_DEBUG_TRACE, "index_add_mods 3 failed, err=%d %s\n", retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			goto error_return;
		}
	}
	/*
	 * Remove the old entry from the Virtual List View indexes.
	 * Add the new entry to the Virtual List View indexes.
	 * If ruv, we don't have to update vlv.
	 */
	if (!is_ruv)
	{
		retval= vlv_update_all_indexes(ptxn, be, pb, e, ec);
		if (DB_LOCK_DEADLOCK == retval)
		{
			/* Abort and re-try */
			goto error_return;
		}
		if (retval != 0)
		{
			LDAPDebug( LDAP_DEBUG_TRACE, "vlv_update_all_indexes failed, err=%d %s\n", retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
			goto error_return;
		}
	}
	if (cache_replace( &inst->inst_cache, e, ec ) != 0 ) {
		retval= -1;
		goto error_return;
	}
error_return:
    return retval;
}


/*
 */
static int
moddn_rename_child_entry(
    back_txn *ptxn, 
	Slapi_PBlock *pb, 
	struct ldbminfo *li, 
	struct backentry *e, 
	struct backentry *ec, 
	int parentdncomps, 
	char **newsuperiordns, 
	int newsuperiordncomps,
	CSN *opcsn)
{
    /*
	 * Construct the new DN for the entry by taking the old DN
	 * excluding the old parent entry DN, and adding the new
	 * superior entry DN.
	 *
	 * ldap_explode_dn is probably a bit slow, but it knows about
	 * DN escaping which is pretty complicated, and we wouldn't
	 * want to reimplement that here.
	 *
	 * JCM - This was written before Slapi_RDN... so this could be made much neater.
	 */
    int retval;
	char *olddn;
	char *newdn;
	char **olddns;
	int olddncomps= 0;
	int need= 1; /* For the '\0' */
	int i;

	olddn = slapi_entry_get_dn(ec->ep_entry);
	olddns = ldap_explode_dn( olddn, 0 );
	for(;olddns[olddncomps]!=NULL;olddncomps++);
	for(i=0;i<olddncomps-parentdncomps;i++)
	{
	    need+= strlen(olddns[i]) + 2; /* For the ", " */
	}
	for(i=0;i<newsuperiordncomps;i++)
	{
	    need+= strlen(newsuperiordns[i]) + 2; /* For the ", " */
	}
	need--; /* We don't have a comma on the end of the last component */
	newdn= slapi_ch_malloc(need);
	newdn[0]= '\0';
	for(i=0;i<olddncomps-parentdncomps;i++)
	{
	    strcat(newdn,olddns[i]);
		strcat(newdn,", ");
	}
	for(i=0;i<newsuperiordncomps;i++)
	{
	    strcat(newdn,newsuperiordns[i]);
		if(i<newsuperiordncomps-1)
		{
			/* We don't have a comma on the end of the last component */
			strcat(newdn,", ");
		}
	}
	slapi_ldap_value_free( olddns );
	slapi_entry_set_dn( ec->ep_entry, newdn );
	add_update_entrydn_operational_attributes (ec);

	/*
	 * Update the DN CSN of the entry.
	 */
	{
		entry_add_dncsn(e->ep_entry, opcsn);
		entry_add_rdn_csn(e->ep_entry, opcsn);
		entry_set_maxcsn(e->ep_entry, opcsn);
	}
	{
		Slapi_Mods smods;
		slapi_mods_init(&smods, 2);
    	slapi_mods_add( &smods, LDAP_MOD_DELETE, "entrydn", strlen( backentry_get_ndn(e) ), backentry_get_ndn(e) );
    	slapi_mods_add( &smods, LDAP_MOD_REPLACE, "entrydn", strlen( backentry_get_ndn(ec) ), backentry_get_ndn(ec) );
		/*
		 * Update all the indexes.
		 */
        retval= modrdn_rename_entry_update_indexes(ptxn, pb, li, e, ec, &smods, NULL, NULL); /* JCMREPL - Should the children get updated modifiersname and lastmodifiedtime? */
		slapi_mods_done(&smods);
	}
    return retval;
}

/*
 * Rename all the children of an entry who's name has changed.
 */
static int
moddn_rename_children(
    back_txn *ptxn, 
	Slapi_PBlock *pb, 
	backend *be, 
	IDList *children, 
	Slapi_DN *dn_parentdn, 
	Slapi_DN *dn_newsuperiordn, 
	struct backentry *child_entries[], 
	struct backentry *child_entry_copies[])
{
    /* Iterate over the children list renaming every child */	
	struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
	Slapi_Operation *operation;
	CSN *opcsn;
    int retval= 0, i;
	char **newsuperiordns;
	int newsuperiordncomps= 0;
	int parentdncomps= 0;

    /*
	 * Break down the parent entry dn into its components.
	 */	
	{
    	char **parentdns;
       	parentdns = ldap_explode_dn( slapi_sdn_get_dn(dn_parentdn), 0 );
		for(;parentdns[parentdncomps]!=NULL;parentdncomps++);
   		slapi_ldap_value_free( parentdns );
	}

    /*
	 * Break down the new superior entry dn into its components.
	 */	
   	newsuperiordns = ldap_explode_dn( slapi_sdn_get_dn(dn_newsuperiordn), 0 );
	for(;newsuperiordns[newsuperiordncomps]!=NULL;newsuperiordncomps++);

    /*
	 * Iterate over the child entries renaming them.
	 */
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	opcsn = operation_get_csn (operation);
	for (i = 0; retval == 0 && child_entries[i] != NULL; i++) {
		retval= moddn_rename_child_entry(ptxn, pb, li, child_entries[i], child_entry_copies[i], parentdncomps, newsuperiordns, newsuperiordncomps, opcsn );
	}
	if (retval != 0) {
		while (child_entries[i] != NULL) {
			backentry_free(&(child_entry_copies[i]));
			i++;
		}
	}
	slapi_ldap_value_free( newsuperiordns );
	return retval;
}


/*
 * Get an IDList of all the children of an entry.
 */
static IDList *
moddn_get_children(back_txn *ptxn, Slapi_PBlock *pb, backend *be, struct backentry *parententry, Slapi_DN *dn_parentdn, struct backentry ***child_entries, struct backentry ***child_entry_copies)
{
    ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    int err= 0;
    IDList *candidates;
    IDList *result_idl = NULL;
    char filterstr[20];
    Slapi_Filter *filter;
    NIDS nids;
    int entrynumber= 0;
    ID id;
    idl_iterator sr_current; /* the current position in the search results */
    struct backentry *e= NULL;

    /* Fetch a candidate list of all the entries below the entry being moved */
    strcpy( filterstr, "objectclass=*" );
    filter = slapi_str2filter( filterstr );
    candidates= subtree_candidates(pb, be, slapi_sdn_get_ndn(dn_parentdn), parententry, filter, 1 /* ManageDSAIT */, NULL /* allids_before_scopingp */, &err);
    slapi_filter_free(filter,1);
	
    if (candidates!=NULL)
    {	
        sr_current = idl_iterator_init(candidates);
        result_idl= idl_alloc(candidates->b_nids);
        do
        {
            id = idl_iterator_dereference_increment(&sr_current, candidates);
            if ( id!=NOID )
            {
                int err= 0;
                e = id2entry( be, id, NULL, &err );
                if (e!=NULL)
                {
                    /* The subtree search will have included the parent entry in the result set */
                    if (e!=parententry)
                    {
                        /* Check that the candidate entry is really below the base. */
                        if(slapi_dn_issuffix( backentry_get_ndn(e), slapi_sdn_get_ndn(dn_parentdn)))
                        {
                            idl_append(result_idl,id);
                        }
                    }
                    cache_return(&inst->inst_cache, &e);
                }
            }
        } while (id!=NOID);
        idl_free(candidates);
    }
    
    nids = result_idl ? result_idl->b_nids : 0;

    *child_entries= (struct backentry**)slapi_ch_calloc(sizeof(struct backentry*),nids+1);
    *child_entry_copies= (struct backentry**)slapi_ch_calloc(sizeof(struct backentry*),nids+1);
    
    sr_current = idl_iterator_init(result_idl);
    do {
        id = idl_iterator_dereference_increment(&sr_current, result_idl);
        if ( id!=NOID ) {
            e= cache_find_id( &inst->inst_cache, id );
            if ( e != NULL ) {
                cache_lock_entry(&inst->inst_cache, e);
                (*child_entries)[entrynumber]= e;
                (*child_entry_copies)[entrynumber]= backentry_dup(e);
                entrynumber++;
            }
        }
    } while (id!=NOID);

    return result_idl;
}
