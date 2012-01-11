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

 
/*
 * urp_tombstone.c - Update Resolution Procedures - Tombstones
 */

#include "slapi-plugin.h"
#include "repl5.h"
#include "urp.h"

extern int slapi_log_urp;

/*
 * Check if the entry is a tombstone.
 */
int
is_tombstone_entry(const Slapi_Entry* entry)
{
	int flag;

	/* LP: This doesn't work very well with entries that we tombstone ourself */
	flag = slapi_entry_flag_is_set (entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
	if (flag == 0)
	{
		/* This is slow */
		flag = slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
	}
	return flag;
}

PRBool
get_tombstone_csn(const Slapi_Entry *entry, const CSN **delcsn)
{
	PRBool ists = PR_FALSE;
	if (is_tombstone_entry(entry)) {
		ists = PR_TRUE;
		*delcsn = _get_deletion_csn((Slapi_Entry *)entry); /* cast away const */
	}

	return ists;
}

static int
tombstone_to_glue_resolve_parent (
	Slapi_PBlock *pb,
	const char *sessionid,
	const Slapi_DN *parentdn,
	const char *parentuniqueid,
	CSN *opcsn)
{
	/* Let's have a look at the parent of this entry... */
	if(!slapi_sdn_isempty(parentdn) && parentuniqueid!=NULL)
	{
		int op_result;
	    Slapi_PBlock *newpb= slapi_pblock_new();
	    void *txn = NULL;

	    slapi_pblock_get(pb, SLAPI_TXN, &txn);
	    slapi_pblock_set(newpb, SLAPI_TXN, txn);
	    slapi_search_internal_set_pb(
	    			newpb,
	    			slapi_sdn_get_dn(parentdn), /* JCM - This DN just identifies the backend to be searched. */
	    			LDAP_SCOPE_BASE,
					"objectclass=*",
					NULL, /*attrs*/
					0, /*attrsonly*/
					NULL, /*Controls*/
					parentuniqueid, /*uniqueid*/
					repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
					0);
		slapi_search_internal_pb(newpb); 
	    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
		switch(op_result)
		{
		case LDAP_SUCCESS:
			{
			Slapi_Entry **entries= NULL;
			/* OK, the tombstone entry parent exists. Is it also a tombstone? */
			slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
			if(entries!=NULL && entries[0]!=NULL)
			{
				if(is_tombstone_entry(entries[0]))
				{
					tombstone_to_glue (pb, sessionid, entries[0], parentdn, REASON_RESURRECT_ENTRY, opcsn);
				}
			}
			else
			{
				/* JCM - Couldn't find the entry! */
			}
			}
			break;
		default:
			/* So, the tombstone entry had a parent... but it's gone. */
			/* That's probably a bad thing. */
			break;
		}
		slapi_free_search_results_internal (newpb);
		slapi_pblock_destroy(newpb);
	}
	return 0;
}

/*
 * Convert a tombstone into a glue entry.
 */
int
tombstone_to_glue (
	Slapi_PBlock *pb,
	const char *sessionid,
	Slapi_Entry *tombstoneentry,
	const Slapi_DN *tombstonedn,
	const char *reason,
	CSN *opcsn)
{
	Slapi_DN *parentdn;
	char *parentuniqueid;
	const char *tombstoneuniqueid;
	Slapi_Entry *addingentry;
	const char *addingdn;
	int op_result;
	void *txn = NULL;

	/* JCMREPL
	 * Nothing logged to the 5.0 Change Log
	 * Add is logged to the 4.0 Change Log - Core server Add code
	 * must attach the entry to the Operation
	 */


	/* Resurrect the parent entry first */ 

	/* JCM - This DN calculation is odd. It could resolve to NULL
	 * which won't help us identify the correct backend to search.
	 */
	is_suffix_dn (pb, tombstonedn, &parentdn);
	parentuniqueid= slapi_entry_attr_get_charptr (tombstoneentry,
			SLAPI_ATTR_VALUE_PARENT_UNIQUEID); /* Allocated */
	tombstone_to_glue_resolve_parent (pb, sessionid, parentdn, parentuniqueid, opcsn);
	slapi_sdn_free(&parentdn);

    /* Submit an Add operation to turn the tombstone entry into glue. */
	/*
	 * The tombstone is stored with an invalid DN, we must fix this.
	 */
	addingentry = slapi_entry_dup(tombstoneentry);
	addingdn = slapi_sdn_get_dn(tombstonedn);
	slapi_entry_set_sdn(addingentry, tombstonedn);

	if (!slapi_entry_attr_hasvalue(addingentry, ATTR_NSDS5_REPLCONFLICT, reason))
	{
		/* Add the reason of turning it to glue - The backend code will use it*/
		slapi_entry_add_string(addingentry, ATTR_NSDS5_REPLCONFLICT, reason);
	}
	tombstoneuniqueid= slapi_entry_get_uniqueid(tombstoneentry);
	slapi_pblock_get (pb, SLAPI_TXN, &txn);
	op_result = urp_fixup_add_entry (addingentry, tombstoneuniqueid, parentuniqueid, opcsn, OP_FLAG_RESURECT_ENTRY, txn);
	if (op_result == LDAP_SUCCESS)
	{
		slapi_log_error (slapi_log_urp, repl_plugin_name,
			"%s: Resurrected tombstone %s to glue reason '%s'\n", sessionid, addingdn, reason);
	}
	else
	{
		slapi_log_error (SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Can't resurrect tombstone %s to glue reason '%s', error=%d\n",
			sessionid, addingdn, reason, op_result);
	}
	slapi_entry_free (addingentry);
	return op_result;
}

int
entry_to_tombstone ( Slapi_PBlock *pb, Slapi_Entry *entry )
{
	Slapi_Operation *op;
	Slapi_Mods smods;
	CSN *opcsn;
	const char *uniqueid;
	int op_result = LDAP_SUCCESS;
	void *txn = NULL;

	slapi_pblock_get ( pb, SLAPI_OPERATION, &op );
	opcsn = operation_get_csn ( op );
	uniqueid = slapi_entry_get_uniqueid ( entry );


	slapi_mods_init ( &smods, 2 );
	/* Remove objectclass=glue */
	slapi_mods_add ( &smods, LDAP_MOD_DELETE, SLAPI_ATTR_OBJECTCLASS, strlen("glue"), "glue");
	/* Remove any URP conflict since a tombstone shouldn't
	 * be retrieved later for conflict removal.
	 */
	slapi_mods_add ( &smods, LDAP_MOD_DELETE, ATTR_NSDS5_REPLCONFLICT, 0, NULL );

	slapi_pblock_get (pb, SLAPI_TXN, &txn);
	op_result = urp_fixup_modify_entry (uniqueid, 
	                                    slapi_entry_get_sdn_const (entry),
	                                    opcsn, &smods, 0, txn);
	slapi_mods_done ( &smods );

	/*
	 * Delete the entry.
	 */
	if ( op_result == LDAP_SUCCESS )
	{
		/*
		 * Using internal delete operation since it would go
		 * through the urp operations and trigger the recursive
		 * fixup if applicable.
		 */
		op_result = urp_fixup_delete_entry (uniqueid, slapi_entry_get_dn_const (entry), opcsn, 0, txn);
	}

	return op_result;
}
