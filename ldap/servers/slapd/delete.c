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
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"
#include "pratom.h"

/* Forward declarations */
static int delete_internal_pb (Slapi_PBlock *pb); 
static void op_shared_delete (Slapi_PBlock *pb);

/* This function is called to process operation that come over external connections */
void
do_delete( Slapi_PBlock *pb )
{
	Slapi_Operation *operation;
	BerElement	*ber;
	char	    *dn = NULL;
	int			err;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_delete\n", 0, 0, 0 );
	
	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
	ber = operation->o_ber;

	/* count the delete request */
	snmp_increment_counter(g_get_global_snmp_vars()->ops_tbl.dsRemoveEntryOps);

	/*
	 * Parse the delete request.  It looks like this:
	 *
	 *	DelRequest := DistinguishedName
	 */

	if ( ber_scanf( pb->pb_op->o_ber, "a", &dn ) == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ber_scanf failed (op=Delete; params=DN)\n", 0, 0, 0 );
		op_shared_log_error_access (pb, "DEL", "???", "decoding error");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0,
		    NULL );
		goto free_and_return;
	}

	/*
	 * in LDAPv3 there can be optional control extensions on
	 * the end of an LDAPMessage. we need to read them in and
	 * pass them to the backend.
	 */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 ) {
		op_shared_log_error_access (pb, "DEL", dn, "decoding error");
		send_ldap_result( pb, err, NULL, NULL, 0, NULL );
		goto free_and_return;
	}

	LDAPDebug( LDAP_DEBUG_ARGS, "do_delete: dn (%s)\n", dn, 0, 0 );
			
	slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &pb->pb_op->o_isroot );
	slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET, dn);

	op_shared_delete (pb);

free_and_return:;
	slapi_ch_free ((void**)&dn);
}

/* This function is used to issue internal delete operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_delete_internal_pb instead */
Slapi_PBlock *
slapi_delete_internal(const char *idn, LDAPControl **controls, int dummy)
{
    Slapi_PBlock	pb;
    Slapi_PBlock    *result_pb;
    int             opresult;

    pblock_init (&pb);
    	
    slapi_delete_internal_set_pb (&pb, idn, controls, NULL, plugin_get_default_component_id(), 0);

	delete_internal_pb (&pb);
	
	result_pb = slapi_pblock_new();
	if (result_pb)
	{
		slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);	
		slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
	}
	pblock_done(&pb);
    
    return result_pb;
}

/* 	This is new style API to issue internal delete operation.
	pblock should contain the following data (can be set via call to slapi_delete_internal_set_pb):
	For uniqueid based operation:
		SLAPI_TARGET_DN set to dn that allows to select right backend, can be stale
		SLAPI_TARGET_UNIQUEID set to the uniqueid of the entry we are looking for
		SLAPI_CONTROLS_ARG set to request controls if present

	For dn based search:
		SLAPI_TARGET_DN set to the entry dn
		SLAPI_CONTROLS_ARG set to request controls if present		 				
 */
int slapi_delete_internal_pb (Slapi_PBlock *pb)
{
	if (pb == NULL)
		return -1;

	if (!allow_operation (pb))
	{
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
						 "This plugin is not configured to access operation target data", 0, NULL );
		return 0;
	}

	return delete_internal_pb (pb);
}

/* Initialize a pblock for a call to slapi_delete_internal_pb() */
void slapi_delete_internal_set_pb (Slapi_PBlock *pb, const char *dn, LDAPControl **controls, const char *uniqueid, 
								   Slapi_ComponentId *plugin_identity, int operation_flags)
{  
	Operation *op;
	PR_ASSERT (pb != NULL);
	if (pb == NULL || dn == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"slapi_delete_internal_set_pb: NULL parameter\n");
		return;
	}

    op = internal_operation_new(SLAPI_OPERATION_DELETE,operation_flags);
	slapi_pblock_set(pb, SLAPI_OPERATION, op);
	slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, (void*)dn);
    slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
	if (uniqueid)
	{
		slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void*)uniqueid);
	}
	slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

/* Helper functions */

static int delete_internal_pb (Slapi_PBlock *pb)
{
	LDAPControl		**controls;
	Operation       *op;
    int             opresult = 0;

	PR_ASSERT (pb != NULL);

	slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data   = &opresult;
    op->o_result_handler = internal_getresult_callback;

	slapi_pblock_set(pb, SLAPI_OPERATION, op);
	slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);

	/* set parameters common for all internal operations */
	set_common_params (pb);

	/* set actions taken to process the operation */
	set_config_params (pb);

	/* perform delete operation */
	op_shared_delete (pb);

	slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);

	return 0;
}

static void op_shared_delete (Slapi_PBlock *pb)
{
	char	      	*dn;
	Slapi_Backend	*be = NULL;
	char			ebuf[ BUFSIZ ];
	int				internal_op;
	Slapi_DN		sdn;
	Slapi_Operation *operation;
	Slapi_Entry *referral;
	Slapi_Entry	*ecopy = NULL;
	char errorbuf[BUFSIZ];
	int				err;

	slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET, &dn);
	slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
	internal_op= operation_is_flag_set(operation, OP_FLAG_INTERNAL);

	slapi_sdn_init_dn_byref(&sdn,dn);
	slapi_pblock_set(pb, SLAPI_DELETE_TARGET, (void*)slapi_sdn_get_ndn (&sdn));

	/* target spec is used to decide which plugins are applicable for the operation */
	operation_set_target_spec (operation, &sdn);

	if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
	{
		if (!internal_op )
		{
			slapi_log_access(LDAP_DEBUG_STATS, "conn=%d op=%d DEL dn=\"%s\"\n",
							pb->pb_conn->c_connid, 
							pb->pb_op->o_opid,
							escape_string(dn, ebuf));
		}
		else
		{
			slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d DEL dn=\"%s\"\n",
							LOG_INTERNAL_OP_CON_ID,
							LOG_INTERNAL_OP_OP_ID,
							escape_string(dn, ebuf));
		}
	}

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one.
	 */
	if ((err = slapi_mapping_tree_select(pb, &be, &referral, errorbuf)) != LDAP_SUCCESS) {
		send_ldap_result(pb, err, NULL, errorbuf, 0, NULL);
		be = NULL;
		goto free_and_return;
	}

	if (referral)
	{
		int managedsait;

		slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
		if (managedsait)
		{
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
					"cannot delete referral", 0, NULL);
			slapi_entry_free(referral);
			goto free_and_return;
		}
	
		send_referrals_from_entry(pb,referral);
		slapi_entry_free(referral);
		goto free_and_return;
	}

	slapi_pblock_set(pb, SLAPI_BACKEND, be);			

	/*
	 * call the pre-delete plugins. if they succeed, call
	 * the backend delete function. then call the
	 * post-delete plugins.
	 */
	if (plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN : 
							SLAPI_PLUGIN_PRE_DELETE_FN) == 0)
	{
		int	rc;

		slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
		set_db_default_result_handlers(pb);
		if (be->be_delete != NULL)
		{
			if ((rc = (*be->be_delete)(pb)) == 0)
			{
				/* we don't perform acl check for internal operations */
				/* Dont update aci store for remote acis              */
				if ((!internal_op) && 
					(!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
					plugin_call_acl_mods_update (pb, SLAPI_OPERATION_DELETE);

				if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_AUDIT))
					write_audit_log_entry(pb); /* Record the operation in the audit log */

				slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &ecopy);
				do_ps_service(ecopy, NULL, LDAP_CHANGETYPE_DELETE, 0);
			}
			else
			{
				if (rc == SLAPI_FAIL_DISKFULL)
				{
					operation_out_of_disk_space();
					goto free_and_return;
				}
			}
		}

		slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
		plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN :
							SLAPI_PLUGIN_POST_DELETE_FN);
	}

free_and_return:
	if (be)
		slapi_be_Unlock(be);
	slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &ecopy);
	slapi_entry_free(ecopy);
	slapi_pblock_get ( pb, SLAPI_DELETE_GLUE_PARENT_ENTRY, &ecopy );
	if (ecopy)
	{
		slapi_entry_free (ecopy);
		slapi_pblock_set (pb, SLAPI_DELETE_GLUE_PARENT_ENTRY, NULL);
	}
	slapi_pblock_get(pb, SLAPI_URP_NAMING_COLLISION_DN, &dn);
	slapi_ch_free((void **)&dn);
	slapi_sdn_done(&sdn);
}
