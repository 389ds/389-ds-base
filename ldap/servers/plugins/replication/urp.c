/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * urp.c - Update Resolution Procedures
 */

#include "slapi-plugin.h"
#include "repl.h"
#include "repl5.h"
#include "urp.h"

extern int slapi_log_urp;

static int urp_add_resolve_parententry (Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, Slapi_Entry *parententry, CSN *opcsn);
static int urp_annotate_dn (char *sessionid, Slapi_Entry *entry, CSN *opcsn, const char *optype);
static int urp_naming_conflict_removal (Slapi_PBlock *pb, char *sessionid, CSN *opcsn, const char *optype);
static int mod_namingconflict_attr (const char *uniqueid, const char*entrydn, const char *conflictdn, CSN *opcsn);
static int del_replconflict_attr (Slapi_Entry *entry, CSN *opcsn, int opflags);
static char *get_dn_plus_uniqueid(char *sessionid,const char *olddn,const char *uniqueid);
static char *get_rdn_plus_uniqueid(char *sessionid,const char *olddn,const char *uniqueid);
static void set_pblock_dn (Slapi_PBlock* pb,int pblock_parameter,char *newdn);
static int is_suffix_entry (Slapi_PBlock *pb, Slapi_Entry *entry, Slapi_DN **parenddn);

/*
 * Return 0 for OK, -1 for Error.
 */
int
urp_modify_operation( Slapi_PBlock *pb )
{
	Slapi_Entry *modifyentry= NULL;
	int op_result= 0;
    int rc= 0; /* OK */

	if ( slapi_op_abandoned(pb) )
	{
		return rc;
	}

   	slapi_pblock_get( pb, SLAPI_MODIFY_EXISTING_ENTRY, &modifyentry );

	if(modifyentry!=NULL)
	{
	    /*
	     * The entry to be modified exists.
		 * - the entry could be a tombstone... but that's OK.
		 * - the entry could be glue... that may not be OK. JCMREPL
		 */
		rc= 0; /* OK, Modify the entry */
		PROFILE_POINT; /* Modify Conflict; Entry Exists; Apply Modification */
	}
	else
	{
	    /*
	     * The entry to be modified could not be found.
	     */ 
		op_result= LDAP_NO_SUCH_OBJECT;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc= -1; /* Must discard this Modification */
		PROFILE_POINT; /* Modify Conflict; Entry Does Not Exist; Discard Modification */
	}
	return rc;
}

/*
 * Return 0 for OK,
 *       -1 for Ignore or Error depending on SLAPI_RESULT_CODE,
 *       >0 for action code
 * Action Code Bit 0: Fetch existing entry.
 * Action Code Bit 1: Fetch parent entry.
 * The function is called as a be pre-op on consumers.
 */
int 
urp_add_operation( Slapi_PBlock *pb )
{
	Slapi_Entry	*existing_uniqueid_entry;
	Slapi_Entry	*existing_dn_entry;
	Slapi_Entry	*addentry;
	const char *adduniqueid;
	CSN *opcsn;
	const char *basedn;
	char sessionid[REPL_SESSION_ID_SIZE];
	int r;
	int op_result= 0;
    int rc= 0; /* OK */

	if ( slapi_op_abandoned(pb) )
	{
		return rc;
	}

	slapi_pblock_get( pb, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY, &existing_uniqueid_entry );
	if (existing_uniqueid_entry!=NULL)
	{
		/* 
		 * An entry with this uniqueid already exists.
		 * - It could be a replay of the same Add, or
		 * - It could be a UUID generation collision, or
		 */
		op_result = LDAP_SUCCESS;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc= -1; /* Ignore this Operation */
		PROFILE_POINT; /* Add Conflict; UniqueID Exists;  Ignore */
		goto bailout;
	}

	get_repl_session_id (pb, sessionid, &opcsn);
	slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &addentry );
	slapi_pblock_get( pb, SLAPI_ADD_EXISTING_DN_ENTRY, &existing_dn_entry );
	if (existing_dn_entry==NULL) /* The target DN does not exist */
	{
		/* Check for parent entry... this could be an orphan. */
		Slapi_Entry *parententry;
		slapi_pblock_get( pb, SLAPI_ADD_PARENT_ENTRY, &parententry );
		rc = urp_add_resolve_parententry (pb, sessionid, addentry, parententry, opcsn);
		PROFILE_POINT; /* Add Entry */
		goto bailout;
	}

	/*
	 * Naming conflict: an entry with the target DN already exists.
	 * Compare the DistinguishedNameCSN of the existing entry
	 * and the OperationCSN. The smaller CSN wins. The loser changes
	 * its RDN to uniqueid+baserdn, and adds operational attribute
	 * ATTR_NSDS5_REPLCONFLIC.
	 */
	basedn = slapi_entry_get_ndn (addentry);
	adduniqueid = slapi_entry_get_uniqueid (addentry);
	r = csn_compare (entry_get_dncsn(existing_dn_entry), opcsn);
	if (r<0)
	{
		/* Entry to be added is a loser */
		char *newdn= get_dn_plus_uniqueid (sessionid, basedn, adduniqueid);
		if(newdn==NULL)
		{
			op_result= LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
			rc= -1; /* Abort this Operation */
			PROFILE_POINT; /* Add Conflict; Entry Exists; Unique ID already in RDN - Abort this update. */
		}
		else
		{
			/* Add the nsds5ReplConflict attribute in the mods */
			Slapi_Attr *attr = NULL;
			Slapi_Value **vals = NULL;
			Slapi_RDN *rdn;
			char buf[BUFSIZ];

			sprintf(buf, "%s %s", REASON_ANNOTATE_DN, basedn);
			if (slapi_entry_attr_find (addentry, ATTR_NSDS5_REPLCONFLICT, &attr) == 0)
			{
				/* ATTR_NSDS5_REPLCONFLICT exists */
				slapi_log_error (SLAPI_LOG_FATAL, sessionid, "New entry has nsds5ReplConflict already\n");
				vals = attr_get_present_values (attr); /* this returns a pointer to the contents */
			}
			if ( vals == NULL || *vals == NULL )
			{
				/* Add new attribute */
				slapi_entry_add_string (addentry, ATTR_NSDS5_REPLCONFLICT, buf);
			}
			else
			{
				/*
				 * Replace old attribute. We don't worry about the index
				 * change here since the entry is yet to be added.
				 */
				slapi_value_set_string (*vals, buf);
			}
			slapi_entry_set_dn (addentry,slapi_ch_strdup(newdn));
			set_pblock_dn(pb,SLAPI_ADD_TARGET,newdn); /* consumes newdn */

			rdn = slapi_rdn_new_sdn ( slapi_entry_get_sdn_const(addentry) );
			slapi_log_error (slapi_log_urp, sessionid,
					"Naming conflict ADD. Add %s instead\n", slapi_rdn_get_rdn(rdn) );
			slapi_rdn_free(&rdn);

			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			PROFILE_POINT; /* Add Conflict; Entry Exists; Rename Operation Entry */
		}
	}
	else if(r>0)
	{
		/* Existing entry is a loser */
		if (!urp_annotate_dn(sessionid, existing_dn_entry, opcsn, "ADD"))
		{
			op_result= LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
			rc= -1; /* Ignore this Operation */
		}
		else
		{
			/* The backend add code should now search for the existing entry again. */
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
		}
		PROFILE_POINT; /* Add Conflict; Entry Exists; Rename Existing Entry */
	}
	else /* r==0 */
	{
		/* The CSN of the Operation and the Entry DN are the same.
		 * This could only happen if:
		 * a) There are two replicas with the same ReplicaID.
		 * b) We've seen the Operation before.
		 * Let's go with (b) and ignore the little bastard.
		 */
		op_result= LDAP_SUCCESS;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc= -1; /* Ignore this Operation */
		PROFILE_POINT; /* Add Conflict; Entry Exists; Same CSN */
	}

bailout:
    return rc;
}

/*
 * Return 0 for OK, -1 for Error, >0 for action code
 * Action Code Bit 0: Fetch existing entry.
 * Action Code Bit 1: Fetch parent entry.
 */
int
urp_modrdn_operation( Slapi_PBlock *pb )
{
	slapi_operation_parameters *op_params = NULL;
	Slapi_Entry *parent_entry;
    Slapi_Entry *new_parent_entry;
	Slapi_DN *newsuperior = NULL;
	char *newsuperiordn;
	Slapi_DN *parentdn = NULL;
	Slapi_Entry *target_entry;
    Slapi_Entry *existing_entry;
	const CSN *target_entry_dncsn;
	CSN *opcsn= NULL;
	char *op_uniqueid = NULL;
	const char *existing_uniqueid = NULL;
	const char *target_dn;
	const char *existing_dn;
	char *newrdn;
	char sessionid[REPL_SESSION_ID_SIZE];
	int r;
	int op_result= 0;
    int rc= 0; /* OK */
	int del_old_replconflict_attr = 0;

	if ( slapi_op_abandoned(pb) )
	{
		return rc;
	}

   	slapi_pblock_get (pb, SLAPI_MODRDN_TARGET_ENTRY, &target_entry);
	if(target_entry==NULL)
	{
		/* An entry can't be found for the Unique Identifier */
		op_result= LDAP_NO_SUCH_OBJECT;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc= -1; /* No entry to modrdn */
		PROFILE_POINT; /* ModRDN Conflict; Entry does not Exist; Discard ModRDN */
		goto bailout;
	}

   	get_repl_session_id (pb, sessionid, &opcsn);
	target_entry_dncsn = entry_get_dncsn (target_entry);
	if ( csn_compare (target_entry_dncsn, opcsn) >= 0 )
	{
		/*
		 * The Operation CSN is not newer than the DN CSN.
		 * Either we're beaten by another ModRDN or we've applied the op.
		 */
		op_result= LDAP_SUCCESS;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc= -1; /* Ignore the modrdn */
		PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; OPCSN is not newer. */
		goto bailout;
	}

	/* The DN CSN is older than the Operation CSN. Apply the operation */
	target_dn = slapi_entry_get_dn_const ( target_entry);
	slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
	slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &op_uniqueid);
   	slapi_pblock_get(pb, SLAPI_MODRDN_PARENT_ENTRY, &parent_entry);
   	slapi_pblock_get(pb, SLAPI_MODRDN_NEWPARENT_ENTRY, &new_parent_entry);
	slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR, &newsuperiordn);

	if ( is_tombstone_entry (target_entry) )
	{
		/*
		 * It is a non-trivial task to rename a tombstone.
		 * This op has been ignored so far by 
		 * setting SLAPI_RESULT_CODE to LDAP_NO_SUCH_OBJECT
		 * and rc to -1.
		 */

		/* Turn the tombstone to glue before rename it */
		/*
		op_result = tombstone_to_glue (pb, sessionid, target_entry,
			slapi_entry_get_sdn (target_entry), "renameTombstone", opcsn);
		*/
		op_result = LDAP_NO_SUCH_OBJECT;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		if (op_result == 0)
		{
			/*
			 * Remember to turn this entry back to tombstone in post op.
			 * We'll just borrow an obsolete pblock type here.
			 */
			slapi_pblock_set (pb, SLAPI_URP_TOMBSTONE_UNIQUEID, strdup(op_uniqueid));
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_TARGET_ENTRY);
			rc = 0;
		}
		else
		{
			rc = -1;
		}
		PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; OPCSN is not newer. */
		goto bailout;
	}

   	slapi_pblock_get(pb, SLAPI_MODRDN_EXISTING_ENTRY, &existing_entry);
    if(existing_entry!=NULL) 
	{
	    /*
	     * An entry with the target DN already exists.
		 * The smaller dncsn wins. The loser changes its RDN to
		 * uniqueid+baserdn, and adds operational attribute
		 * ATTR_NSDS5_REPLCONFLIC
	     */

		existing_uniqueid = slapi_entry_get_uniqueid (existing_entry);
		existing_dn = slapi_entry_get_dn_const ( existing_entry);

		/*
		 * Dismiss the operation if the existing entry is the same as the target one.
		 */
		if (strcmp(op_uniqueid, existing_uniqueid) == 0) {
			op_result= LDAP_SUCCESS;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
			rc = -1; /* Ignore the op */
			PROFILE_POINT; /* ModRDN Replay */
			goto bailout;
		}

		r= csn_compare ( entry_get_dncsn (existing_entry), opcsn);
		if (r == 0)
		{
			/*
			 * The CSN of the Operation and the Entry DN are the same
			 * but the uniqueids are not.
			 * There might be two replicas with the same ReplicaID.
			 */
			slapi_log_error(SLAPI_LOG_FATAL, sessionid,
				"Duplicated CSN for different uniqueids [%s][%s]",
				existing_uniqueid, op_uniqueid);
			op_result= LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
			rc= -1; /* Abort */
			PROFILE_POINT; /* ModRDN Conflict; Duplicated CSN for Different Entries */
			goto bailout;
		}

		if(r<0)
		{
			/* The target entry is a loser */

			char *newrdn_with_uniqueid;
			newrdn_with_uniqueid= get_rdn_plus_uniqueid (sessionid, newrdn, op_uniqueid);
			if(newrdn_with_uniqueid==NULL)
			{
				op_result= LDAP_OPERATIONS_ERROR;
				slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
				rc= -1; /* Ignore this Operation */
				PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists;
								  Unique ID already in RDN - Change to Lost and Found entry */
				goto bailout;
			}
			mod_namingconflict_attr (op_uniqueid, target_dn, existing_dn, opcsn);
			set_pblock_dn (pb, SLAPI_MODRDN_NEWRDN, newrdn_with_uniqueid); 
			slapi_log_error(slapi_log_urp, sessionid,
					"Naming conflict MODRDN. Rename target entry to %s\n",
					newrdn_with_uniqueid );

			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; Rename Operation Entry */
			goto bailout;
		}

		if ( r>0 )
		{
			/* The existing entry is a loser */

			int resolve = urp_annotate_dn (sessionid, existing_entry, opcsn, "MODRDN");
			if(!resolve)
			{
				op_result= LDAP_OPERATIONS_ERROR;
				slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
				rc= -1; /* Abort this Operation */
				goto bailout;
			}
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY);
			if (LDAP_NO_SUCH_OBJECT == resolve) {
				/* This means that existing_dn_entry did not really exist!!!
				 * This indicates that a get_copy_of_entry -> dn2entry returned 
				 * an entry (existing_dn_entry) that was already removed from the ldbm.
				 * This is bad, because it indicates a dn cache or DB corruption.
				 * However, as far as the conflict is concerned, this error is harmless:
				 * if the existing_dn_entry did not exist in the first place, there was no
				 * conflict!! Return 0 for success to break the ldbm_back_modrdn loop 
				 * and get out of this inexistent conflict resolution ASAP.
				 */
				rc = 0;
			}
			/* Set flag to remove possible old naming conflict */
			del_old_replconflict_attr = 1;
			PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; Rename Entry with Target DN */
			goto bailout;
		}
	}
	else
	{
		/*
		 * No entry with the target DN exists.
		 */

		/* Set flag to remove possible old naming conflict */
		del_old_replconflict_attr = 1;

		if(new_parent_entry!=NULL)
		{
			/* The new superior entry exists */
			rc= 0; /* OK, Apply the ModRDN */
			PROFILE_POINT; /* ModRDN Conflict; OK */
			goto bailout;
		}

		/* The new superior entry doesn't exist */

		slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR, &newsuperiordn);
		if(newsuperiordn == NULL)
		{
			/* (new_parent_entry==NULL && newsuperiordn==NULL)
			 * This is ok - SLAPI_MODRDN_NEWPARENT_ENTRY will
			 * only be set if SLAPI_MODRDN_NEWSUPERIOR was
			 * suplied by the client. If it wasn't, we're just
			 * changing the RDN of the entry. In that case,
			 * if the entry exists, its parent won't change
			 * when it's renamed, and therefore we can assume
			 * its parent exists.
			 */
			rc=0;
			PROFILE_POINT; /* ModRDN OK */
			goto bailout;
		}

		newsuperior= slapi_sdn_new_dn_byval(newsuperiordn);

		if((0 == slapi_sdn_compare (slapi_entry_get_sdn(parent_entry), newsuperior)) || 
				is_suffix_dn (pb, newsuperior, &parentdn) )
		{
			/*
			 * The new superior is the same as the current one, or
			 * this entry is a suffix whose parent can be absent.
			 */ 
			rc= 0; /* OK, Move the entry */
			PROFILE_POINT; /* ModRDN Conflict; Absent Target Parent; Create Suffix Entry */
			goto bailout;
		}

		/*
		 * This entry is not a suffix entry, so the parent entry should exist.
		 * (This shouldn't happen in a ds5 server)
		 */
		slapi_pblock_get ( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
		op_result = create_glue_entry (pb, sessionid, newsuperior,
			op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid, opcsn);
		if (LDAP_SUCCESS != op_result)
		{
			/* 
			 * FATAL ERROR 
			 * We should probably just abort the rename
			 * this will cause replication divergence requiring
			 * admin intercession
			 */
			slapi_log_error( SLAPI_LOG_FATAL, sessionid,
				 "Parent %s couldn't be found, nor recreated as a glue entry\n", newsuperiordn );
			op_result= LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
			rc = -1;
			PROFILE_POINT;
			goto bailout;
		}

		/* The backend add code should now search for the parent again. */
		rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY);
		PROFILE_POINT; /* ModRDN Conflict; Absent Target Parent - Change to Lost and Found entry */
		goto bailout;
	}

bailout:
	if ( del_old_replconflict_attr && rc == 0 )
	{
		del_replconflict_attr (target_entry, opcsn, 0);
	}
	if ( parentdn )
		slapi_sdn_free(&parentdn);
	if ( newsuperior )
		slapi_sdn_free(&newsuperior);
    return rc;
}

/*
 * Return 0 for OK, -1 for Error
 */
int 
urp_delete_operation( Slapi_PBlock *pb )
{
  	Slapi_Entry *deleteentry;
	CSN *opcsn= NULL;
	char sessionid[REPL_SESSION_ID_SIZE];
	int op_result= 0;
    int rc= 0; /* OK */

	if ( slapi_op_abandoned(pb) )
	{
		return rc;
	}

   	slapi_pblock_get(pb, SLAPI_DELETE_EXISTING_ENTRY, &deleteentry);

	if(deleteentry==NULL) /* uniqueid can't be found */
	{
		op_result= LDAP_NO_SUCH_OBJECT;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc= -1; /* Don't apply the Delete */
		PROFILE_POINT; /* Delete Operation; Entry not exist. */
	}
	else if(is_tombstone_entry(deleteentry))
	{
		/* The entry is already a Tombstone, ignore this delete. */
		op_result= LDAP_SUCCESS;
		slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
		rc = -1; /* Don't apply the Delete */
		PROFILE_POINT; /* Delete Operation; Already a Tombstone. */
	}
	else /* The entry to be deleted exists and is not a tombstone */
	{
		get_repl_session_id (pb, sessionid, &opcsn);

		/* Check if the entry has children. */
		if(!slapi_entry_has_children(deleteentry))
		{
			/* Remove possible conflict attributes */
			del_replconflict_attr (deleteentry, opcsn, 0);
			rc= 0; /* OK, to delete the entry */
			PROFILE_POINT; /* Delete Operation; OK. */
		}
		else
		{
			/* Turn this entry into a glue_absent_parent entry */
			entry_to_glue(sessionid, deleteentry, REASON_RESURRECT_ENTRY, opcsn);

			/* Turn the Delete into a No-Op */
			op_result= LDAP_SUCCESS;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
			rc = -1; /* Don't apply the Delete */
		    PROFILE_POINT; /* Delete Operation; Entry has children. */
		}
	}
	return rc;
}

int urp_post_modrdn_operation (Slapi_PBlock *pb)
{
	CSN *opcsn;
	char sessionid[REPL_SESSION_ID_SIZE];
	char *tombstone_uniqueid;
	Slapi_Entry *postentry;
	Slapi_Operation *op;

	/*
	 * Do not abandon the post op - the processed CSN needs to be
	 * committed to keep the consistency between the changelog
	 * and the backend DB.
	 * if ( slapi_op_abandoned(pb) ) return 0;
	 */ 

	slapi_pblock_get (pb, SLAPI_URP_TOMBSTONE_UNIQUEID, &tombstone_uniqueid );
	if (tombstone_uniqueid == NULL)
	{
		/*
		 * The entry is not resurrected from tombstone. Hence
		 * we need to check if any naming conflict with its
		 * old dn can be resolved.
		 */
		slapi_pblock_get( pb, SLAPI_OPERATION, &op);
		if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP))
		{
			get_repl_session_id (pb, sessionid, &opcsn);
			urp_naming_conflict_removal (pb, sessionid, opcsn, "MODRDN");
		}
	}
	else
	{
		/*
		 * The entry was a resurrected tombstone.
		 * This could happen when we applied a rename
		 * to a tombstone to avoid server divergence. Now
		 * it's time to put the entry back to tombstone.
		 */
		slapi_pblock_get ( pb, SLAPI_ENTRY_POST_OP, &postentry );
		if (postentry && strcmp(tombstone_uniqueid, slapi_entry_get_uniqueid(postentry)) == 0)
		{
			entry_to_tombstone (pb, postentry);
		}
		slapi_ch_free ((void**)&tombstone_uniqueid);
		slapi_pblock_set (pb, SLAPI_URP_TOMBSTONE_UNIQUEID, NULL);
	}

	return 0;
}

/*
 * Conflict removal
 */
int 
urp_post_delete_operation( Slapi_PBlock *pb )
{
	Slapi_Operation *op;
	Slapi_Entry *entry;
	CSN *opcsn;
	char sessionid[REPL_SESSION_ID_SIZE];
	int op_result;

	/*
	 * Do not abandon the post op - the processed CSN needs to be
	 * committed to keep the consistency between the changelog
	 * and the backend DB
	 * if ( slapi_op_abandoned(pb) ) return 0;
	 */ 

   	get_repl_session_id (pb, sessionid, &opcsn);

	/*
	 * Conflict removal from the parent entry:
	 * If the parent is glue and has no more children,
	 * turn the parent to tombstone
	 */
	slapi_pblock_get ( pb, SLAPI_DELETE_GLUE_PARENT_ENTRY, &entry );
	if ( entry != NULL )
	{
		op_result = entry_to_tombstone ( pb, entry );
		if ( op_result == LDAP_SUCCESS )
		{
			slapi_log_error ( slapi_log_urp, sessionid,
				"Tombstoned glue entry %s since it has no more children\n",
				slapi_entry_get_dn_const (entry) );
		}
	}

	slapi_pblock_get( pb, SLAPI_OPERATION, &op);
	if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP))
	{
		/*
		 * Conflict removal from the peers of the old dn
		 */
		urp_naming_conflict_removal (pb, sessionid, opcsn, "DEL");
	}

	return 0;
}

int
urp_fixup_add_entry (Slapi_Entry *e, const char *target_uniqueid, const char *parentuniqueid, CSN *opcsn, int opflags)
{
	Slapi_PBlock *newpb;
	Slapi_Operation *op;
	int op_result;

	newpb = slapi_pblock_new ();

	/*
	 * Mark this operation as replicated, so that the front end
	 * doesn't add extra attributes.
	 */
	slapi_add_entry_internal_set_pb (
			newpb,
			e,
			NULL, /*Controls*/
			repl_get_plugin_identity ( PLUGIN_MULTIMASTER_REPLICATION ),
			OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);
	if (target_uniqueid)
	{
		slapi_pblock_set( newpb, SLAPI_TARGET_UNIQUEID, (void*)target_uniqueid);
	}
	if (parentuniqueid)
	{
		struct slapi_operation_parameters *op_params;
		slapi_pblock_get( newpb, SLAPI_OPERATION_PARAMETERS, &op_params );
		op_params->p.p_add.parentuniqueid = (char*)parentuniqueid; /* Consumes parentuniqueid */
	}
	slapi_pblock_get ( newpb, SLAPI_OPERATION, &op );
	operation_set_csn ( op, opcsn );

	slapi_add_internal_pb ( newpb );
	slapi_pblock_get ( newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result );
	slapi_pblock_destroy ( newpb );

	return op_result;
}

int
urp_fixup_rename_entry (Slapi_Entry *entry, const char *newrdn, int opflags)
{
	Slapi_PBlock *newpb;
    Slapi_Operation *op;
	CSN *opcsn;
	int op_result;

	newpb = slapi_pblock_new();

	/*
	 * Must mark this operation as replicated,
	 * so that the frontend doesn't add extra attributes.
	 */
	slapi_rename_internal_set_pb (
					newpb,
					slapi_entry_get_dn_const (entry),
					newrdn, /*NewRDN*/
					NULL, /*NewSuperior*/
					0, /* !Delete Old RDNS */
					NULL, /*Controls*/
					slapi_entry_get_uniqueid (entry), /*uniqueid*/
					repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
					OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);

    /* set operation csn to the entry's dncsn */
	opcsn = (CSN *)entry_get_dncsn (entry);
    slapi_pblock_get (newpb, SLAPI_OPERATION, &op);
    operation_set_csn (op, opcsn);

	slapi_modrdn_internal_pb(newpb); 
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);

	slapi_pblock_destroy(newpb);
	return op_result;
}

int
urp_fixup_delete_entry (const char *uniqueid, const char *dn, CSN *opcsn, int opflags)
{
	Slapi_PBlock *newpb;
	Slapi_Operation *op;
	int op_result;

	newpb = slapi_pblock_new ();

	/*
	 * Mark this operation as replicated, so that the front end
	 * doesn't add extra attributes.
	 */
	slapi_delete_internal_set_pb (
			newpb,
			dn,
			NULL, /*Controls*/
			uniqueid, /*uniqueid*/
			repl_get_plugin_identity ( PLUGIN_MULTIMASTER_REPLICATION ),
			OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags );
	slapi_pblock_get ( newpb, SLAPI_OPERATION, &op );
	operation_set_csn ( op, opcsn );

	slapi_delete_internal_pb ( newpb );
	slapi_pblock_get ( newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result );
	slapi_pblock_destroy ( newpb );

	return op_result;
}

int
urp_fixup_modify_entry (const char *uniqueid, const char *dn, CSN *opcsn, Slapi_Mods *smods, int opflags)
{
	Slapi_PBlock *newpb;
	Slapi_Operation *op;
	int op_result;

	newpb = slapi_pblock_new();
			
	slapi_modify_internal_set_pb (
			newpb,
			dn,
			slapi_mods_get_ldapmods_byref (smods),
			NULL, /* Controls */
			uniqueid,
			repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION),
			OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);

	/* set operation csn */
	slapi_pblock_get (newpb, SLAPI_OPERATION, &op);
	operation_set_csn (op, opcsn);

	/* do modify */
	slapi_modify_internal_pb (newpb);
	slapi_pblock_get (newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
	slapi_pblock_destroy(newpb);

	return op_result;
}

static int
urp_add_resolve_parententry (Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, Slapi_Entry *parententry, CSN *opcsn)
{
	Slapi_DN *parentdn = NULL;
	Slapi_RDN *add_rdn = NULL;
	char *newdn = NULL;
	int ldap_rc;
	int rc = 0;

	if( is_suffix_entry (pb, entry, &parentdn) )
	{
		/* It's OK for the suffix entry's parent to be absent */ 
		rc= 0;
		PROFILE_POINT; /* Add Conflict; Suffix Entry */
		goto bailout;
	}

	/* The entry is not a suffix. */
	if(parententry==NULL) /* The parent entry was not found. */
	{
		/* Create a glue entry to stand in for the absent parent */
		slapi_operation_parameters *op_params;
		slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
		ldap_rc = create_glue_entry (pb, sessionid, parentdn, op_params->p.p_add.parentuniqueid, opcsn);
		if ( LDAP_SUCCESS == ldap_rc )
		{
			/* The backend code should now search for the parent again. */
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
			PROFILE_POINT; /* Add Conflict; Orphaned Entry; Glue Parent */
		}
		else
		{
			/*
			 * Error. The parent can't be created as a glue entry.
			 * This will cause replication divergence and will
			 * require admin intercession
			 */
			ldap_rc= LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_rc);
			rc= -1; /* Abort this Operation */
			PROFILE_POINT; /* Add Conflict; Orphaned Entry; Impossible to create parent; Refuse Change. */
		}
		goto bailout;
	}

	if(is_tombstone_entry(parententry)) /* The parent is a tombstone */
	{
		/* The parent entry must be resurected from the dead. */
		ldap_rc = tombstone_to_glue (pb, sessionid, parententry, parentdn, REASON_RESURRECT_ENTRY, opcsn);
		if ( ldap_rc != LDAP_SUCCESS )
		{
			ldap_rc= LDAP_OPERATIONS_ERROR;
			slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_rc);
			rc = -1; /* Abort the operation */
		}
		else
		{
			/* The backend add code should now search for the parent again. */
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
			rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
		}
		PROFILE_POINT; /* Add Conflict; Orphaned Entry; Parent Was Tombstone */
		goto bailout;
	}

	/* The parent is healthy */
	/* Now we need to check that the parent has the correct DN */
	if (slapi_sdn_isparent(slapi_entry_get_sdn(parententry), slapi_entry_get_sdn(entry)))
	{
		rc= 0; /* OK, Add the entry */
		PROFILE_POINT; /* Add Conflict; Parent Exists */
		goto bailout;
	}

	/* 
	 * Parent entry doesn't have a DN parent to the entry.
	 * This can happen if parententry was renamed due to
	 * conflict and the child entry was created before
	 * replication occured. See defect 530942.
	 * We need to rename the entry to be child of its parent.
	 */
	add_rdn = slapi_rdn_new_dn(slapi_entry_get_dn_const (entry));
	newdn = slapi_dn_plus_rdn(slapi_entry_get_dn_const (parententry), slapi_rdn_get_rdn(add_rdn));
	slapi_entry_set_dn ( entry,slapi_ch_strdup(newdn));
	set_pblock_dn (pb,SLAPI_ADD_TARGET,newdn); /* consumes newdn */
	slapi_log_error ( slapi_log_urp, sessionid,
			"Parent was renamed. Renamed the child to %s\n", newdn );
	rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
	PROFILE_POINT; /* Add Conflict; Parent Renamed; Rename Operation Entry */

bailout:
	if (parentdn)
		slapi_sdn_free(&parentdn);
	return rc;
}

/* 
 * urp_annotate_dn:
 * Returns 0 on failure
 * Returns > 0 on success (1 on general conflict resolution success, LDAP_NO_SUCH_OBJECT on no-conflict success)
 *
 * Use this function to annotate an existing entry only. To annotate
 * a new entry (the operation entry) see urp_add_operation.
 */
static int
urp_annotate_dn (char *sessionid, Slapi_Entry *entry, CSN *opcsn, const char *optype)
{
	int rc = 0; /* Fail */
	int op_result;
	char *newrdn;
	const char *uniqueid;
	const char *basedn;
	char ebuf[BUFSIZ];

	uniqueid = slapi_entry_get_uniqueid (entry);
	basedn = slapi_entry_get_ndn (entry);
	newrdn = get_rdn_plus_uniqueid ( sessionid, basedn, uniqueid );
	if(newrdn!=NULL)
	{
		mod_namingconflict_attr (uniqueid, basedn, basedn, opcsn);
		op_result = urp_fixup_rename_entry ( entry, newrdn, 0 );
		switch(op_result)
		{
		case LDAP_SUCCESS:
			slapi_log_error(slapi_log_urp, sessionid,
				"Naming conflict %s. Renamed existing entry to %s\n",
				optype, escape_string (newrdn, ebuf));
			rc = 1;
			break;
		case LDAP_NO_SUCH_OBJECT:
			/* This means that entry did not really exist!!!
			 * This is clearly indicating that there is a
			 * get_copy_of_entry -> dn2entry returned 
			 * an entry (entry) that was already removed
			 * from the ldbm database...
			 * This is bad, because it clearly indicates
			 * some kind of db or cache corruption. We need to print 
			 * this fact clearly in the errors log to try
			 * to solve this corruption one day.
			 * However, as far as the conflict is concerned,
			 * this error is completely harmless:
			 * if thew entry did not exist in the first place,
			 * there was never a room
			 * for a conflict!! After fix for 558293, this
			 * state can't be reproduced anymore (5-Oct-01)
			 */
			slapi_log_error( SLAPI_LOG_FATAL, sessionid,
				"Entry %s exists in cache but not in DB\n",
				escape_string (basedn, ebuf) );
			rc = LDAP_NO_SUCH_OBJECT;
			break;
		default:
		    slapi_log_error( slapi_log_urp, sessionid,
				"Failed to annotate %s, err=%d\n", newrdn, op_result);
		}
		slapi_ch_free ( (void**)&newrdn );
	}
	return rc;
}

/*
 * An URP Naming Collision helper function. Retreives a list of entries
 * that have the given dn excluding the unique id of the entry. Any 
 * entries returned will be entries that have been added with the same
 * dn, but caused a naming conflict when replicated. The URP to fix
 * this constraint violation is to append the unique id of the entry
 * to its RDN.
 */
static Slapi_Entry *
urp_get_min_naming_conflict_entry ( Slapi_PBlock *pb, char *sessionid, CSN *opcsn )
{
	Slapi_PBlock *newpb = NULL;
	LDAPControl **server_ctrls = NULL;
	Slapi_Entry **entries = NULL;
	Slapi_Entry *min_naming_conflict_entry = NULL;
	const CSN *min_csn = NULL;
	char *filter = NULL;
	char *parent_dn = NULL;
	char *basedn;
	int i = 0;
	int min_i = -1;
	int op_result = LDAP_SUCCESS;

	slapi_pblock_get (pb, SLAPI_URP_NAMING_COLLISION_DN, &basedn);
	if (NULL == basedn || strncmp (basedn, SLAPI_ATTR_UNIQUEID, strlen(SLAPI_ATTR_UNIQUEID)) == 0)
		return NULL;

	slapi_log_error ( SLAPI_LOG_REPL, sessionid,
		"Enter urp_get_min_naming_conflict_entry for %s\n", basedn);

	filter = slapi_ch_malloc(50 + strlen(basedn));
	sprintf(filter, "(%s=%s %s)", ATTR_NSDS5_REPLCONFLICT, REASON_ANNOTATE_DN, basedn);

	/* server_ctrls will be freed when newpb is destroyed */
	server_ctrls = (LDAPControl **)slapi_ch_calloc (2, sizeof (LDAPControl *));
	server_ctrls[0] = create_managedsait_control();
	server_ctrls[1] = NULL;
	
	newpb = slapi_pblock_new();
	parent_dn = slapi_dn_parent (basedn);
	slapi_search_internal_set_pb(newpb,
								 parent_dn, /* Base DN */
								 LDAP_SCOPE_ONELEVEL,
								 filter,
								 NULL, /* Attrs */
								 0, /* AttrOnly */
								 server_ctrls, /* Controls */
								 NULL, /* UniqueID */
								 repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
								 0);
	slapi_search_internal_pb(newpb);
	slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
	slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	if ( (op_result != LDAP_SUCCESS) || (entries == NULL) )
	{
		/* Log a message */
		goto done;
	}
	/* For all entries, get the one with the smallest dn csn */
	for (i = 0; NULL != entries[i]; i++)
	{
		const CSN *dncsn;
		dncsn = entry_get_dncsn(entries[i]);
		if ((dncsn != opcsn) && 
			((min_csn == NULL) || (csn_compare(dncsn, min_csn) < 0)) &&
			!is_tombstone_entry (entries[i]))
		{
			min_csn = dncsn;
			min_i = i;
		}
		/*
		 * If there are too many conflicts, the current urp code has no
		 * guarantee for all servers to converge anyway, because the
		 * urp and the backend can't be done in one transaction due
		 * to either performance or the deadlock problem.
		 * Don't sacrifice the performance too much for impossible.
		 */
		if (min_csn && i > 5)
		{
			break;
		}
	}
	
	if (min_csn != NULL) {
		/* Found one entry */
		min_naming_conflict_entry = slapi_entry_dup(entries[min_i]);
	}

done:
	slapi_ch_free((void **)&parent_dn);
	slapi_ch_free((void **)&filter);
	slapi_free_search_results_internal(newpb);
	slapi_pblock_destroy(newpb);
	newpb = NULL;

	slapi_log_error ( SLAPI_LOG_REPL, sessionid,
		"Leave urp_get_min_naming_conflict_entry (found %d entries)\n", i);

	return min_naming_conflict_entry;
}

/*
 * If an entry is deleted or renamed, a new winner may be
 * chosen from its naming competitors.
 * The entry with the smallest dncsn restores its original DN.
 */
static int
urp_naming_conflict_removal ( Slapi_PBlock *pb, char *sessionid, CSN *opcsn, const char *optype )
{
	Slapi_Entry *min_naming_conflict_entry;
	Slapi_RDN *oldrdn, *newrdn;
	const char *oldrdnstr, *newrdnstr;
	int op_result;

	/*
	 * Backend op has set SLAPI_URP_NAMING_COLLISION_DN to the basedn.
	 */
	min_naming_conflict_entry = urp_get_min_naming_conflict_entry (pb, sessionid, opcsn);
	if (min_naming_conflict_entry == NULL)
	{
		return 0;
	}

	/* Step 1: Restore the entry's original DN */

	oldrdn = slapi_rdn_new_sdn ( slapi_entry_get_sdn (min_naming_conflict_entry) );
	oldrdnstr = slapi_rdn_get_rdn ( oldrdn );

	/* newrdnstr is the old rdn of the entry minus the nsuniqueid part */
	newrdn = slapi_rdn_new_rdn ( oldrdn );
	slapi_rdn_remove_attr (newrdn, SLAPI_ATTR_UNIQUEID );
	newrdnstr = slapi_rdn_get_rdn ( newrdn );		

	/*
	 * Set OP_FLAG_ACTION_INVOKE_FOR_REPLOP since this operation
	 * is done after DB lock was released. The backend modrdn
	 * will acquire the DB lock if it sees this flag.
	 */
	op_result = urp_fixup_rename_entry (min_naming_conflict_entry, newrdnstr, OP_FLAG_ACTION_INVOKE_FOR_REPLOP);
	if ( op_result != LDAP_SUCCESS )
	{
	    slapi_log_error (slapi_log_urp, sessionid,
			"Failed to restore RDN of %s, err=%d\n", oldrdnstr, op_result);
		goto bailout;
	}
	slapi_log_error (slapi_log_urp, sessionid,
		"Naming conflict removed by %s. RDN of %s was restored\n", optype, oldrdnstr);
			
	/* Step2: Remove ATTR_NSDS5_REPLCONFLICT from the winning entry */
	/*
	 * A fixup op will not invoke urp_modrdn_operation(). Even it does,
	 * urp_modrdn_operation() will do nothing because of the same CSN.
	 */
	op_result = del_replconflict_attr (min_naming_conflict_entry, opcsn, OP_FLAG_ACTION_INVOKE_FOR_REPLOP);
	if (op_result != LDAP_SUCCESS) {
		slapi_log_error(SLAPI_LOG_REPL, sessionid,
			"Failed to remove nsds5ReplConflict for %s, err=%d\n",
			newrdnstr, op_result);
	}

bailout:
	slapi_entry_free (min_naming_conflict_entry);
	slapi_rdn_free(&oldrdn);
	slapi_rdn_free(&newrdn);
	return op_result;
}

/* The returned value is either null or "uniqueid=<uniqueid>+<basedn>" */
static char *
get_dn_plus_uniqueid(char *sessionid, const char *olddn, const char *uniqueid)
{
	Slapi_DN *sdn= slapi_sdn_new_dn_byval(olddn);
	Slapi_RDN *rdn= slapi_rdn_new();
	char *newdn;

	PR_ASSERT(uniqueid!=NULL);

	/* Check if the RDN already contains the Unique ID */
	slapi_sdn_get_rdn(sdn,rdn);
	if(slapi_rdn_contains(rdn,SLAPI_ATTR_UNIQUEID,uniqueid,strlen(uniqueid)))
	{
		/* The Unique ID is already in the RDN.
		 * This is a highly improbable collision.
		 * It suggests that a duplicate UUID was generated.
		 * This will cause replication divergence and will
		 * require admin intercession
		 */
		slapi_log_error(SLAPI_LOG_FATAL, sessionid,
				"Annotated DN %s has naming conflict\n", olddn );
		newdn= NULL;
	}
	else
	{
		slapi_rdn_add(rdn,SLAPI_ATTR_UNIQUEID,uniqueid);
		slapi_sdn_set_rdn(sdn, rdn);
		newdn= slapi_ch_strdup(slapi_sdn_get_dn(sdn));
	}
	slapi_sdn_free(&sdn);
	slapi_rdn_free(&rdn);
	return newdn;
}

static char *
get_rdn_plus_uniqueid(char *sessionid, const char *olddn, const char *uniqueid)
{
	char *newrdn;
	/* Check if the RDN already contains the Unique ID */
	Slapi_DN *sdn= slapi_sdn_new_dn_byval(olddn);
	Slapi_RDN *rdn= slapi_rdn_new();
	slapi_sdn_get_rdn(sdn,rdn);
	PR_ASSERT(uniqueid!=NULL);
	if(slapi_rdn_contains(rdn,SLAPI_ATTR_UNIQUEID,uniqueid,strlen(uniqueid)))
	{
		/* The Unique ID is already in the RDN.
		 * This is a highly improbable collision.
		 * It suggests that a duplicate UUID was generated.
		 * This will cause replication divergence and will
		 * require admin intercession
		 */
		slapi_log_error(SLAPI_LOG_FATAL, sessionid,
				"Annotated DN %s has naming conflict\n", olddn );
		newrdn= NULL;
	}
	else
	{
		slapi_rdn_add(rdn,SLAPI_ATTR_UNIQUEID,uniqueid);
		newrdn= slapi_ch_strdup(slapi_rdn_get_rdn(rdn));
	}
	slapi_sdn_free(&sdn);
	slapi_rdn_free(&rdn);
	return newrdn;
}

static void
set_pblock_dn (Slapi_PBlock* pb,int pblock_parameter,char *newdn)
{
	char *olddn;
	slapi_pblock_get( pb, pblock_parameter, &olddn );
	slapi_ch_free((void**)&olddn);
	slapi_pblock_set( pb, pblock_parameter, newdn );
}

static int
is_suffix_entry ( Slapi_PBlock *pb, Slapi_Entry *entry, Slapi_DN **parentdn )
{
	return is_suffix_dn ( pb, slapi_entry_get_sdn(entry), parentdn );
}

int
is_suffix_dn ( Slapi_PBlock *pb, const Slapi_DN *dn, Slapi_DN **parentdn )
{
	Slapi_Backend *backend;
	int rc;

	*parentdn = slapi_sdn_new();
	slapi_pblock_get( pb, SLAPI_BACKEND, &backend );
	slapi_sdn_get_backend_parent (dn, *parentdn, backend);

	/* A suffix entry doesn't have parent dn */
	rc = slapi_sdn_isempty (*parentdn) ? 1 : 0;

	return rc;
}

static int
mod_namingconflict_attr (const char *uniqueid, const char *entrydn, const char *conflictdn, CSN *opcsn)
{
	Slapi_Mods smods;
	char buf[BUFSIZ];
	int op_result;

	sprintf (buf, "%s %s", REASON_ANNOTATE_DN, conflictdn);
	slapi_mods_init (&smods, 2);
	if ( strncmp (entrydn, SLAPI_ATTR_UNIQUEID, strlen(SLAPI_ATTR_UNIQUEID)) != 0 )
	{
		slapi_mods_add (&smods, LDAP_MOD_ADD, ATTR_NSDS5_REPLCONFLICT, strlen(buf), buf);
	}
	else
	{
		/*
		 * If the existing entry is already a naming conflict loser,
		 * the following replace operation should result in the
		 * replace of the ATTR_NSDS5_REPLCONFLICT index as well.
		 */
		slapi_mods_add (&smods, LDAP_MOD_REPLACE, ATTR_NSDS5_REPLCONFLICT, strlen(buf), buf);
	}
	op_result = urp_fixup_modify_entry (uniqueid, entrydn, opcsn, &smods, 0);
	slapi_mods_done (&smods);
	return op_result;
}

static int
del_replconflict_attr (Slapi_Entry *entry, CSN *opcsn, int opflags)
{
	Slapi_Attr *attr;
	int op_result = 0;

	if (slapi_entry_attr_find (entry, ATTR_NSDS5_REPLCONFLICT, &attr) == 0)
	{
		Slapi_Mods smods;
		const char *uniqueid;
		const char *entrydn;

		uniqueid = slapi_entry_get_uniqueid (entry);
		entrydn = slapi_entry_get_dn_const (entry);
		slapi_mods_init (&smods, 2);
		slapi_mods_add (&smods, LDAP_MOD_DELETE, ATTR_NSDS5_REPLCONFLICT, 0, NULL);
		op_result = urp_fixup_modify_entry (uniqueid, entrydn, opcsn, &smods, opflags);
		slapi_mods_done (&smods);
	}
	return op_result;
}
