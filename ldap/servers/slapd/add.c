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
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"
#include "pratom.h"
#include "csngen.h"

/* Forward declarations */
static int add_internal_pb (Slapi_PBlock *pb);
static void op_shared_add (Slapi_PBlock *pb);
static int add_created_attrs(Operation *op, Slapi_Entry *e);
static int check_rdn_for_created_attrs(Slapi_Entry *e);
static void handle_fast_add(Slapi_PBlock *pb, Slapi_Entry *entry);
static int add_uniqueid (Slapi_Entry *e);
static PRBool check_oc_subentry(Slapi_Entry *e, struct berval	**vals, char *normtype);

/* This function is called to process operation that come over external connections */
void
do_add( Slapi_PBlock *pb )
{
	Slapi_Operation *operation;
	BerElement		*ber;
	char			*last;
	ber_len_t		len = -1;
	ber_tag_t		tag;
	Slapi_Entry		*e = NULL;
	int			err;
	int			rc;
	char			ebuf[ BUFSIZ ];
	PRBool  searchsubentry=PR_TRUE;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_add\n", 0, 0, 0 );

	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
    ber = operation->o_ber;

	/* count the add request */
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsAddEntryOps);

	/*
	 * Parse the add request.  It looks like this:
	 *
	 *	AddRequest := [APPLICATION 14] SEQUENCE {
	 *		name	DistinguishedName,
	 *		attrs	SEQUENCE OF SEQUENCE {
	 *			type	AttributeType,
	 *			values	SET OF AttributeValue
	 *		}
	 *	}
	 */
	/* get the name */
	{
		char *rawdn = NULL;
		char *dn = NULL;
		size_t dnlen = 0;
		if ( ber_scanf( ber, "{a", &rawdn ) == LBER_ERROR ) {
			slapi_ch_free_string(&rawdn);
			LDAPDebug( LDAP_DEBUG_ANY,
				"ber_scanf failed (op=Add; params=DN)\n", 0, 0, 0 );
			op_shared_log_error_access (pb, "ADD", "???", "decoding error");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
				"decoding error", 0, NULL );
			return;
		}
		/* Check if we should be performing strict validation. */
		if (config_get_dn_validate_strict()) {
			/* check that the dn is formatted correctly */
			rc = slapi_dn_syntax_check(pb, rawdn, 1);
			if (rc) { /* syntax check failed */
				op_shared_log_error_access(pb, "ADD", rawdn?rawdn:"",
										   "strict: invalid dn");
				send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
								 NULL, "invalid dn", 0, NULL);
				slapi_ch_free_string(&rawdn);
				return;
			}
		}
		rc = slapi_dn_normalize_ext(rawdn, 0, &dn, &dnlen);
		if (rc < 0) {
			op_shared_log_error_access(pb, "ADD", rawdn?rawdn:"", "invalid dn");
			send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
							 NULL, "invalid dn", 0, NULL);
			slapi_ch_free_string(&rawdn);
			return;
		} else if (rc > 0) {
			slapi_ch_free_string(&rawdn);
		} else { /* rc == 0; rawdn is passed in; not null terminated */
			*(dn + dnlen) = '\0';
		}
		e = slapi_entry_alloc();
		slapi_entry_init(e,dn,NULL); /* Responsibility for DN is passed to the Entry. */
	}
	LDAPDebug( LDAP_DEBUG_ARGS, "	do_add: dn (%s)\n", slapi_entry_get_dn_const(e), 0, 0 );

	/* get the attrs */
	for ( tag = ber_first_element( ber, &len, &last );
	      tag != LBER_DEFAULT && tag != LBER_END_OF_SEQORSET;
	      tag = ber_next_element( ber, &len, last ) ) {
		char *type = NULL, *normtype = NULL;
		struct berval	**vals = NULL;
		len = -1; /* reset - not used in loop */
		if ( ber_scanf( ber, "{a{V}}", &type, &vals ) == LBER_ERROR ) {
			op_shared_log_error_access (pb, "ADD", slapi_sdn_get_dn (slapi_entry_get_sdn_const(e)), "decoding error");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
			    "decoding error", 0, NULL );
            slapi_ch_free_string(&type);
            ber_bvecfree( vals );
			goto free_and_return;
		}

		if ( vals == NULL ) {
			LDAPDebug( LDAP_DEBUG_ANY, "no values for type %s\n", type, 0, 0 );
			op_shared_log_error_access (pb, "ADD", slapi_sdn_get_dn (slapi_entry_get_sdn_const(e)), "null value");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL,
			    0, NULL );
            slapi_ch_free_string(&type);
			goto free_and_return;
		}

		normtype = slapi_attr_syntax_normalize(type);
		if ( !normtype || !*normtype ) {
			rc = LDAP_INVALID_SYNTAX;
			PR_snprintf (ebuf, BUFSIZ, "invalid type '%s'", type);
			op_shared_log_error_access (pb, "ADD", slapi_sdn_get_dn (slapi_entry_get_sdn_const(e)), ebuf);
			send_ldap_result( pb, rc, NULL, ebuf, 0, NULL );
            slapi_ch_free_string(&type);
			slapi_ch_free( (void**)&normtype );
			ber_bvecfree( vals );
			goto free_and_return;
		}
		slapi_ch_free_string(&type);
	
       /* for now we just ignore attributes that client is not allowed
          to modify so not to break existing clients */
		if (op_shared_is_allowed_attr (normtype, pb->pb_conn->c_isreplication_session)){		
			if (( rc = slapi_entry_add_values( e, normtype, vals ))
				!= LDAP_SUCCESS ) {
				slapi_log_access( LDAP_DEBUG_STATS, 
					"conn=%" NSPRIu64 " op=%d ADD dn=\"%s\", add values for type %s failed\n",
					pb->pb_conn->c_connid, operation->o_opid, 
					escape_string( slapi_entry_get_dn_const(e), ebuf ), normtype );
				send_ldap_result( pb, rc, NULL, NULL, 0, NULL );

				slapi_ch_free( (void**)&normtype );
				ber_bvecfree( vals );
				goto free_and_return;
			}

			/* if this is uniqueid attribute, set uniqueid field of the entry */
			if (strcasecmp (normtype, SLAPI_ATTR_UNIQUEID) == 0)
			{
				e->e_uniqueid = slapi_ch_strdup (vals[0]->bv_val);
			}
			if(searchsubentry) searchsubentry=check_oc_subentry(e,vals,normtype);
		}

		slapi_ch_free( (void**)&normtype );
		ber_bvecfree( vals );
	}

	/* Ensure that created attributes are not used in the RDN. */
	if (check_rdn_for_created_attrs(e)) {
		op_shared_log_error_access (pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), "invalid DN");
		send_ldap_result( pb, LDAP_INVALID_DN_SYNTAX, NULL, "illegal attribute in RDN", 0, NULL );
		goto free_and_return;
	}

	if ( (tag != LBER_END_OF_SEQORSET) && (len != -1) ) {
		op_shared_log_error_access (pb, "ADD", slapi_sdn_get_dn (slapi_entry_get_sdn_const(e)), "decoding error");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
		    "decoding error", 0, NULL );
		goto free_and_return;
	}

	/*
	 * in LDAPv3 there can be optional control extensions on
	 * the end of an LDAPMessage. we need to read them in and
	 * pass them to the backend.
	 */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 ) {
		op_shared_log_error_access (pb, "ADD", slapi_sdn_get_dn (slapi_entry_get_sdn_const(e)), 
								    "failed to decode LDAP controls");
		send_ldap_result( pb, err, NULL, NULL, 0, NULL );
		goto free_and_return;
	}

	slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &operation->o_isroot );
	slapi_pblock_set( pb, SLAPI_ADD_ENTRY, e );

        if (pb->pb_conn->c_flags & CONN_FLAG_IMPORT) {
            /* this add is actually part of a bulk import -- punt */
            handle_fast_add(pb, e);
        } else {
            op_shared_add ( pb );
        }

	/* make sure that we don't free entry if it is successfully added */
	e = NULL;

free_and_return:;
	if (e)
		slapi_entry_free (e);

}

/* This function is used to issue internal add operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_add_internal_pb instead */
Slapi_PBlock *
slapi_add_internal(const char *idn, 
                   LDAPMod **iattrs, 
                   LDAPControl **controls,
                   int dummy)
{
    Slapi_Entry     *e;
    Slapi_PBlock    *result_pb = NULL;
    int             opresult= -1;

    if(iattrs == NULL)
    {
        opresult = LDAP_PARAM_ERROR;
		goto done;
    }
    
    opresult = slapi_mods2entry (&e, (char*)idn, iattrs);
	if (opresult != LDAP_SUCCESS)
	{
		goto done;
	}

    result_pb= slapi_add_entry_internal(e, controls, dummy);
    
done:    
    if(result_pb==NULL)
	{
        result_pb = slapi_pblock_new();
        pblock_init(result_pb);
        slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
	}
	
    return result_pb;
}

/* This function is used to issue internal add operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_add_internal_pb instead 
   Beware: The entry is consumed. */
Slapi_PBlock *
slapi_add_entry_internal(Slapi_Entry *e, LDAPControl **controls, int dummy)
{
    Slapi_PBlock    pb;
    Slapi_PBlock    *result_pb = NULL;
    int             opresult;

	pblock_init(&pb);
	
	slapi_add_entry_internal_set_pb (&pb, e, controls, plugin_get_default_component_id(), 0);

	add_internal_pb (&pb);
	
	result_pb = slapi_pblock_new();
	if (result_pb)
	{
		slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
		slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
	}
	pblock_done(&pb);

    return result_pb;
}

/*  This is new style API to issue internal add operation.
	pblock should contain the following data (can be set via call to slapi_add_internal_set_pb):
	SLAPI_TARGET_DN		set to dn of the new entry
	SLAPI_CONTROLS_ARG	set to request controls if present
	SLAPI_ADD_ENTRY		set to Slapi_Entry to add
	Beware: The entry is consumed. */
int slapi_add_internal_pb (Slapi_PBlock *pb)
{
	if (pb == NULL)
		return -1;

	if (!allow_operation (pb))
	{
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
						 "This plugin is not configured to access operation target data", 0, NULL );
		return 0;
	}

	return add_internal_pb (pb);
}

int slapi_add_internal_set_pb (Slapi_PBlock *pb, const char *dn, LDAPMod **attrs, LDAPControl **controls, 
								Slapi_ComponentId *plugin_identity, int operation_flags)
{
	Slapi_Entry *e;
	int rc;

	if (pb == NULL || dn == NULL || attrs == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, NULL, "slapi_add_internal_set_pb: invalid argument\n");
		return LDAP_PARAM_ERROR;
	}

	rc = slapi_mods2entry (&e, dn, attrs);
	if (rc == LDAP_SUCCESS)
	{	
		slapi_add_entry_internal_set_pb (pb, e, controls, plugin_identity, operation_flags);
	}

	return rc;
}
	

/* Initialize a pblock for a call to slapi_add_internal_pb() */
void slapi_add_entry_internal_set_pb (Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **controls, 
								Slapi_ComponentId *plugin_identity, int operation_flags)
{
	Operation *op;
	PR_ASSERT (pb != NULL);
	if (pb == NULL || e == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, NULL, "slapi_add_entry_internal_set_pb: invalid argument\n");
		return;
	}

	op = internal_operation_new(SLAPI_OPERATION_ADD,operation_flags);
	slapi_pblock_set(pb, SLAPI_OPERATION, op);
	slapi_pblock_set(pb, SLAPI_ADD_ENTRY, e);
	slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
	slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

/* Helper functions */

static int add_internal_pb (Slapi_PBlock *pb)
{
	LDAPControl		**controls;
	Operation       *op;
	int             opresult = 0;
	Slapi_Entry		*e;

	PR_ASSERT (pb != NULL);

	slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
	slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

	if (e == NULL)
	{
		opresult = LDAP_PARAM_ERROR;
		slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
		return 0;	
	}
	
	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data   = &opresult;
    op->o_result_handler = internal_getresult_callback;

	slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);
    
	/* set parameters common to all internal operations */
	set_common_params (pb);

	/* set actions taken to process the operation */
	set_config_params (pb);

	/* perform the add operation */
    op_shared_add (pb);
	
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);

	return 0;
}

/* Code shared between regular and internal add operation */
static void op_shared_add (Slapi_PBlock *pb)
{
	Slapi_Operation *operation;
	Slapi_Entry	*e, *pse;
	Slapi_Backend *be = NULL;
	int	err;
	char ebuf[BUFSIZ];
	int internal_op, repl_op, legacy_op, lastmod;
	char *pwdtype = NULL;
	Slapi_Value **unhashed_password_vals = NULL;
	Slapi_Attr *attr = NULL;
	Slapi_Entry *referral;
	char errorbuf[BUFSIZ];
	struct slapdplugin  *p = NULL;
	char *proxydn = NULL;
	char *proxystr = NULL;
	int proxy_err = LDAP_SUCCESS;
	char *errtext = NULL;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
	slapi_pblock_get (pb, SLAPI_ADD_ENTRY, &e);
	slapi_pblock_get (pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);	
	slapi_pblock_get (pb, SLAPI_IS_LEGACY_REPLICATED_OPERATION, &legacy_op);
	internal_op= operation_is_flag_set(operation, OP_FLAG_INTERNAL);

	/* target spec is used to decide which plugins are applicable for the operation */
	operation_set_target_spec (operation, slapi_entry_get_sdn (e));

	if ((err = slapi_entry_add_rdn_values(e)) != LDAP_SUCCESS) 
	{
	  send_ldap_result(pb, err, NULL, "failed to add RDN values", 0, NULL);
	  goto done;
	}

	/* get the proxy auth dn if the proxy auth control is present */
	proxy_err = proxyauth_get_dn(pb, &proxydn, &errtext);

	if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
	{
		if (proxydn)
		{
			proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
		}

		if ( !internal_op )
		{
			slapi_log_access(LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d ADD dn=\"%s\"%s\n",
							 pb->pb_conn->c_connid, 
							 operation->o_opid,
							 escape_string(slapi_entry_get_dn_const(e), ebuf),
							 proxystr ? proxystr : "");
		}
		else
		{
			slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d ADD dn=\"%s\"\n",
							 LOG_INTERNAL_OP_CON_ID,
							 LOG_INTERNAL_OP_OP_ID,
							 escape_string(slapi_entry_get_dn_const(e), ebuf));
		}
	}

	/* If we encountered an error parsing the proxy control, return an error
	 * to the client.  We do this here to ensure that we log the operation first. */
	if (proxy_err != LDAP_SUCCESS)
	{
		send_ldap_result(pb, proxy_err, NULL, errtext, 0, NULL);
		goto done;
	}

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one.
	 */
	if ((err = slapi_mapping_tree_select(pb, &be, &referral, errorbuf)) != LDAP_SUCCESS) {
		send_ldap_result(pb, err, NULL, errorbuf, 0, NULL);
		be = NULL;
		goto done;
	}

	if (referral)
	{
		int managedsait;

		slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
		if (managedsait)
		{
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
							 "cannot update referral", 0, NULL);
			slapi_entry_free(referral);
			goto done;
		}
	
		slapi_pblock_set(pb, SLAPI_TARGET_DN, (void*)slapi_sdn_get_ndn(operation_get_target_spec (operation)));
		send_referrals_from_entry(pb,referral);
		slapi_entry_free(referral);
		goto done;
	}

	if (!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)) {
		/* look for user password attribute */
		slapi_entry_attr_find(e, SLAPI_USERPWD_ATTR, &attr);
		if (attr && !repl_op)  
		{
			Slapi_Value **present_values;
			present_values= attr_get_present_values(attr);

			/* Set the backend in the pblock.  The slapi_access_allowed function
                         * needs this set to work properly. */
                        slapi_pblock_set( pb, SLAPI_BACKEND, slapi_be_select( slapi_entry_get_sdn_const(e) ) );

			/* Check ACI before checking password syntax */
			if ( (err = slapi_access_allowed(pb, e, SLAPI_USERPWD_ATTR, NULL,
                                     SLAPI_ACL_ADD)) != LDAP_SUCCESS) {
                                send_ldap_result(pb, err, NULL,
                                              "Insufficient 'add' privilege to the "
                                              "'userPassword' attribute", 0, NULL);
                                goto done;
			}

			/* check password syntax */
			if (check_pw_syntax(pb, slapi_entry_get_sdn_const(e), present_values, NULL, e, 0) == 0)
			{
				Slapi_Value **vals= NULL;
				valuearray_add_valuearray(&unhashed_password_vals, present_values, 0);
				valuearray_add_valuearray(&vals, present_values, 0);
				pw_encodevals_ext(pb, slapi_entry_get_sdn (e), vals);
				add_password_attrs(pb, operation, e);
				slapi_entry_attr_replace_sv(e, SLAPI_USERPWD_ATTR, vals);
				valuearray_free(&vals);
				
				/* Add the unhashed password pseudo-attribute to the entry */
				pwdtype = slapi_attr_syntax_normalize(PSEUDO_ATTR_UNHASHEDUSERPASSWORD);
				slapi_entry_add_values_sv(e, pwdtype, unhashed_password_vals);
			} else {
				/* error result is sent from check_pw_syntax */
				goto done;
			}
		}

       /* look for multiple backend local credentials or replication local credentials */
        for ( p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL;
            p = p->plg_next )
        {
            char *L_attr = NULL;
            int i=0;

            /* Get the appropriate decoding function */
            for ( L_attr = p->plg_argv[i]; i<p->plg_argc; L_attr = p->plg_argv[++i])
            {
                /* look for multiple backend local credentials or replication local credentials */
                char *L_normalized = slapi_attr_syntax_normalize(L_attr);
                slapi_entry_attr_find(e, L_normalized, &attr);
                if (attr)
                {
                    Slapi_Value **present_values = NULL;
                    Slapi_Value **vals = NULL;

                    present_values= attr_get_present_values(attr);

                    valuearray_add_valuearray(&vals, present_values, 0);
                    pw_rever_encode(vals, L_normalized);
                    slapi_entry_attr_replace_sv(e, L_normalized, vals);
                    valuearray_free(&vals);
                }
                if (L_normalized)
                    slapi_ch_free ((void**)&L_normalized);
            }
        }
    }

	slapi_pblock_set(pb, SLAPI_BACKEND, be);
	/* we set local password policy ACI for non-replicated operations only */
	if (!repl_op &&
		!operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP) &&
		!operation_is_flag_set(operation, OP_FLAG_LEGACY_REPLICATION_DN) &&
		!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA) &&
		!slapi_be_private(be) &&
		slapi_be_issuffix (be, slapi_entry_get_sdn_const(e)))
	{
		/* this is a suffix. update the pw aci */
		slapdFrontendConfig_t *slapdFrontendConfig;
		slapdFrontendConfig = getFrontendConfig();
		pw_add_allowchange_aci(e, !slapdFrontendConfig->pw_policy.pw_change &&
							   !slapdFrontendConfig->pw_policy.pw_must_change);
	}

	/* can get lastmod only after backend is selected */
	slapi_pblock_get(pb, SLAPI_BE_LASTMOD, &lastmod);
	if (!repl_op && lastmod)
	{
		if (add_created_attrs(operation, e) != 0)
		{
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
				"cannot insert computed attributes", 0, NULL);
			goto done;
		}
	}

	/* expand objectClass values to reflect the inheritance hierarchy */
	slapi_schema_expand_objectclasses( e );


    /* uniqueid needs to be generated for entries added during legacy replication */
    if (legacy_op)
	if (add_uniqueid(e) != UID_SUCCESS)
	{
		send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
			"cannot insert computed attributes", 0, NULL);
		goto done;
	}

	/*
	 * call the pre-add plugins. if they succeed, call
	 * the backend add function. then call the post-add
	 * plugins.
	 */
	
	slapi_pblock_set(pb, SLAPI_ADD_TARGET, 
					 (char*)slapi_sdn_get_ndn(slapi_entry_get_sdn_const(e)));
	if (plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN : 
							SLAPI_PLUGIN_PRE_ADD_FN) == 0)
	{
		int	rc;
		Slapi_Entry	*ec;
		char *add_target_dn;

		slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
		set_db_default_result_handlers(pb);
		/* because be_add frees the entry */
		ec = slapi_entry_dup(e);
		add_target_dn= slapi_ch_strdup(slapi_sdn_get_ndn(slapi_entry_get_sdn_const(ec)));
    	slapi_pblock_set(pb, SLAPI_ADD_TARGET, add_target_dn);
		
		if (be->be_add != NULL)
		{
			rc = (*be->be_add)(pb);
			slapi_pblock_set(pb, SLAPI_ADD_ENTRY, ec);
			if (rc == 0)
			{
				/* acl is not enabled for internal operations */
				/* don't update aci store for remote acis     */
				if ((!internal_op) && 
					(!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
				{
					plugin_call_acl_mods_update (pb, SLAPI_OPERATION_ADD);
				}

				if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_AUDIT))
				{ 
					write_audit_log_entry(pb); /* Record the operation in the audit log */
				}

				slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
				do_ps_service(pse, NULL, LDAP_CHANGETYPE_ADD, 0);

				/* If be_add succeeded, then e is consumed.  We
				 * set e to NULL to prevent freeing it ourselves. */
				e = NULL;
			}
			else
			{
				if (rc == SLAPI_FAIL_DISKFULL)
				{
					operation_out_of_disk_space();
					goto done;
				}
			}
		}
		else
		{
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
							 "Function not implemented", 0, NULL);
		}
		slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
		plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_POST_ADD_FN : 
							SLAPI_PLUGIN_POST_ADD_FN);
		slapi_entry_free(ec);
    	slapi_pblock_get(pb, SLAPI_ADD_TARGET, &add_target_dn);
		slapi_ch_free((void**)&add_target_dn);
	}

done:
	if (be)
		slapi_be_Unlock(be);
	slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
	slapi_entry_free(pse);
	slapi_ch_free((void **)&operation->o_params.p.p_add.parentuniqueid);
	slapi_entry_free(e);
	valuearray_free(&unhashed_password_vals);
	slapi_ch_free((void**)&pwdtype);
	slapi_ch_free_string(&proxydn);
	slapi_ch_free_string(&proxystr);
}

static int 
add_created_attrs(Operation *op, Slapi_Entry *e)
{
	char   buf[20];
	struct berval	bv;
	struct berval	*bvals[2];
	time_t		curtime;
	struct tm	ltm;

	LDAPDebug(LDAP_DEBUG_TRACE, "add_created_attrs\n", 0, 0, 0);

	bvals[0] = &bv;
	bvals[1] = NULL;
	
	if (slapi_sdn_isempty(&op->o_sdn)) {
		bv.bv_val = "";
		bv.bv_len = strlen(bv.bv_val);
	} else {
		bv.bv_val = (char*)slapi_sdn_get_dn(&op->o_sdn);
		bv.bv_len = strlen(bv.bv_val);
	}
	slapi_entry_attr_replace(e, "creatorsname", bvals);
	slapi_entry_attr_replace(e, "modifiersname", bvals);

	curtime = current_time();
#ifdef _WIN32
{
	struct tm *pt;
	pt = gmtime(&curtime);
	memcpy(&ltm, pt, sizeof(struct tm));
}
#else
	gmtime_r(&curtime, &ltm);
#endif
	strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", &ltm);

	bv.bv_val = buf;
	bv.bv_len = strlen(bv.bv_val);
	slapi_entry_attr_replace(e, "createtimestamp", bvals);

	bv.bv_val = buf;
	bv.bv_len = strlen(bv.bv_val);
	slapi_entry_attr_replace(e, "modifytimestamp", bvals);

	if (add_uniqueid(e) != UID_SUCCESS ) {
		return( -1 );
	}

	return( 0 );
}


/* Checks if created attributes are used in the RDN.
 * Returns 1 if created attrs are in the RDN, and
 * 0 if created attrs are not in the RDN. Returns
 * -1 if an error occurred.
 */
static int check_rdn_for_created_attrs(Slapi_Entry *e)
{
    int i, rc = 0;
    Slapi_RDN *rdn = NULL;
    char *value = NULL;
    char *type[] = {SLAPI_ATTR_UNIQUEID, "modifytimestamp", "createtimestamp",
                   "creatorsname", "modifiersname", 0};

    if ((rdn = slapi_rdn_new())) {
        slapi_rdn_init_dn(rdn, slapi_entry_get_dn_const(e));

        for (i = 0; type[i] != NULL; i++) {
            if (slapi_rdn_contains_attr(rdn, type[i], &value)) {
                LDAPDebug(LDAP_DEBUG_TRACE, "Invalid DN. RDN contains %s attribute\n", type[i], 0, 0);
                rc = 1;
                break;
            }
        }

        slapi_rdn_free(&rdn);
    } else {
        LDAPDebug(LDAP_DEBUG_TRACE, "check_rdn_for_created_attrs: Error allocating RDN\n", 0, 0, 0);
        rc = -1;
    }

    return rc;
}


static void handle_fast_add(Slapi_PBlock *pb, Slapi_Entry *entry)
{
    Slapi_Backend *be;
    Slapi_Operation *operation;
    int ret;

    be = pb->pb_conn->c_bi_backend;

    if ((be == NULL) || (be->be_wire_import == NULL)) {
        /* can this even happen? */
        LDAPDebug(LDAP_DEBUG_ANY,
                  "handle_fast_add: backend not supported\n", 0, 0, 0);
        send_ldap_result(pb, LDAP_NOT_SUPPORTED, NULL, NULL, 0, NULL);
        return;
    }

	/* ensure that the RDN values are present as attribute values */
	if ((ret = slapi_entry_add_rdn_values(entry)) != LDAP_SUCCESS) {
		send_ldap_result(pb, ret, NULL, "failed to add RDN values", 0, NULL);
		return;
	}

    /* schema check */
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    if (operation_is_flag_set(operation, OP_FLAG_ACTION_SCHEMA_CHECK) &&
        (slapi_entry_schema_check(pb, entry) != 0)) {
	char *errtext; 
        LDAPDebug(LDAP_DEBUG_TRACE, "entry failed schema check\n", 0, 0, 0);
	slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        send_ldap_result(pb, LDAP_OBJECT_CLASS_VIOLATION, NULL, errtext, 0, NULL);
        slapi_entry_free(entry);
        return;
    }

    /* syntax check */
    if (slapi_entry_syntax_check(pb, entry, 0) != 0) {
        char *errtext;
        LDAPDebug(LDAP_DEBUG_TRACE, "entry failed syntax check\n", 0, 0, 0);
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        send_ldap_result(pb, LDAP_INVALID_SYNTAX, NULL, errtext, 0, NULL);
        slapi_entry_free(entry);
        return;
    }

    /* Check if the entry being added is a Tombstone. Could be if we are
     * doing a replica init. */
    if (slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS,
                                  SLAPI_ATTR_VALUE_TOMBSTONE)) {
        entry->e_flags |= SLAPI_ENTRY_FLAG_TOMBSTONE;
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_ENTRY, entry);
    ret = SLAPI_BI_STATE_ADD;
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &ret);
    ret = (*be->be_wire_import)(pb);
    if (ret != 0) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "wire import: error during import (%d)\n",
                  ret, 0, 0);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR,
			 NULL, NULL, 0, NULL);
        /* It's our responsibility to free the entry if
         * be_wire_import doesn't succeed. */
        slapi_entry_free(entry);

       	/* turn off fast replica init -- import is now aborted */
       	pb->pb_conn->c_bi_backend = NULL;
       	pb->pb_conn->c_flags &= ~CONN_FLAG_IMPORT;

        return;
    }

    send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    return;
}

static int
add_uniqueid (Slapi_Entry *e)
{
    char *uniqueid;
    int rc;

    /* generate uniqueID for the entry */
	rc = slapi_uniqueIDGenerateString (&uniqueid);
	if (rc == UID_SUCCESS)
	{
		slapi_entry_set_uniqueid (e, uniqueid);
	}
	else
	{
		LDAPDebug(LDAP_DEBUG_ANY, "add_created_attrs: uniqueid generation failed for %s; error = %d\n", 
				   slapi_entry_get_dn_const(e), rc, 0);
	}

	return( rc );
}

static PRBool
check_oc_subentry(Slapi_Entry *e, struct berval	**vals, char *normtype) {
  int n;

  PRBool subentry=PR_TRUE;
  for(n=0; vals != NULL && vals[n] != NULL; n++) {
    if((strcasecmp(normtype,"objectclass") == 0)  
       && (strncasecmp((const char *)vals[n]->bv_val,"ldapsubentry",vals[n]->bv_len) == 0)) {
      e->e_flags |= SLAPI_ENTRY_LDAPSUBENTRY;
      subentry=PR_FALSE;
      break;
    }
  }
  return subentry;
}
