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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"
#include "pratom.h"
#if defined(irix) || defined(aix) || defined(_WIN32)
#include <time.h>
#endif

/* Forward declarations */
static int modify_internal_pb (Slapi_PBlock *pb);
static void op_shared_modify (Slapi_PBlock *pb, int pw_change, char *old_pw);
static void remove_mod (Slapi_Mods *smods, const char *type, Slapi_Mods *smod_unhashed);
static int op_shared_allow_pw_change (Slapi_PBlock *pb, LDAPMod *mod, char **old_pw, Slapi_Mods *smods);

#ifdef LDAP_DEBUG
static const char*
mod_op_image (int op)
{
    switch (op & ~LDAP_MOD_BVALUES) {
      case LDAP_MOD_ADD:     return "add";
      case LDAP_MOD_DELETE:  return "delete";
      case LDAP_MOD_REPLACE: return "replace";
      default: break;
    }
    return "???";
}
#endif

/* an AttrCheckFunc function should return an LDAP result code (LDAP_SUCCESS if all goes well). */
typedef int (*AttrCheckFunc)(const char *attr_name, char *value, long minval, long maxval, char *errorbuf);

static struct attr_value_check {
	const char *attr_name; /* the name of the attribute */
	AttrCheckFunc checkfunc;
	long minval;
	long maxval;
} AttrValueCheckList[] = {
	{CONFIG_PW_SYNTAX_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_CHANGE_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_LOCKOUT_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_MUSTCHANGE_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_EXP_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_UNLOCK_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_HISTORY_ATTRIBUTE, attr_check_onoff, 0, 0},
	{CONFIG_PW_MINAGE_ATTRIBUTE, check_pw_minage_value, -1, -1},
	{CONFIG_PW_WARNING_ATTRIBUTE, attr_check_minmax, 0, -1},
	{CONFIG_PW_MINLENGTH_ATTRIBUTE, attr_check_minmax, 2, 512},
	{CONFIG_PW_MAXFAILURE_ATTRIBUTE, attr_check_minmax, 1, 32767},
	{CONFIG_PW_INHISTORY_ATTRIBUTE, attr_check_minmax, 2, 24},
	{CONFIG_PW_LOCKDURATION_ATTRIBUTE, check_pw_lockduration_value, -1, -1},
	{CONFIG_PW_RESETFAILURECOUNT_ATTRIBUTE, check_pw_resetfailurecount_value, -1, -1},
	{CONFIG_PW_GRACELIMIT_ATTRIBUTE, attr_check_minmax, 0, -1},
	{CONFIG_PW_STORAGESCHEME_ATTRIBUTE, check_pw_storagescheme_value, -1, -1}
};

/* This function is called to process operation that come over external connections */
void
do_modify( Slapi_PBlock *pb )
{
	Slapi_Operation *operation;
	BerElement			*ber;
	char				*last, *type = NULL;
	ber_tag_t			tag;
	ber_len_t			len;
	LDAPMod				*mod;
	LDAPMod				**mods;
	Slapi_Mods			smods;
	int				err;
	int				pw_change = 0; 	/* 0= no password change */
	int				ignored_some_mods = 0;
	int				has_password_mod = 0; /* number of password mods */
	char				*old_pw = NULL;	/* remember the old password */
	char				*dn = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_modify\n", 0, 0, 0 );

	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
	ber = operation->o_ber;

	/* count the modify request */
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsModifyEntryOps);

	/*
	 * Parse the modify request.  It looks like this:
	 *
	 *	ModifyRequest := [APPLICATION 6] SEQUENCE {
	 *		name	DistinguishedName,
	 *		mods	SEQUENCE OF SEQUENCE {
	 *			operation	ENUMERATED {
	 *				add	(0),
	 *				delete	(1),
	 *				replace	(2)
	 *			},
	 *			modification	SEQUENCE {
	 *				type	AttributeType,
	 *				values	SET OF AttributeValue
	 *			}
	 *		}
	 *	}
	 */

    {
		char *rawdn = NULL;
		size_t dnlen = 0;
		int rc = 0;
    	if ( ber_scanf( ber, "{a", &rawdn ) == LBER_ERROR )
    	{
    		LDAPDebug( LDAP_DEBUG_ANY,
    		    "ber_scanf failed (op=Modify; params=DN)\n", 0, 0, 0 );
			op_shared_log_error_access (pb, "MOD", "???", "decoding error");
    		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL );
    		slapi_ch_free_string(&rawdn);
    		return;
    	}
		/* Check if we should be performing strict validation. */
		if (config_get_dn_validate_strict()) {
			/* check that the dn is formatted correctly */
			rc = slapi_dn_syntax_check(pb, rawdn, 1);
			if (rc) { /* syntax check failed */
				op_shared_log_error_access(pb, "MOD", rawdn?rawdn:"",
								"strict: invalid dn");
				send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
								 NULL, "invalid dn", 0, NULL);
				slapi_ch_free((void **) &rawdn);
				return;
			}
		}
		rc = slapi_dn_normalize_ext(rawdn, 0, &dn, &dnlen);
		if (rc < 0) {
			op_shared_log_error_access(pb, "MOD", "???", "invalid dn");
			send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
								 NULL, "invalid dn", 0, NULL);
			slapi_ch_free((void **) &rawdn);
			return;
		} else if (rc > 0) { /* if rc == 0, rawdn is passed in */
			slapi_ch_free_string(&rawdn);
		} else { /* rc == 0; rawdn is passed in; not null terminated */
			*(dn + dnlen) = '\0';
		}
	}

	LDAPDebug( LDAP_DEBUG_ARGS, "do_modify: dn (%s)\n", dn, 0, 0 );

	slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &pb->pb_op->o_isroot);
	slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET, dn ); 

	/* collect modifications & save for later */
	slapi_mods_init(&smods, 0);
	len = -1;
	for ( tag = ber_first_element( ber, &len, &last );
	    tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
	    tag = ber_next_element( ber, &len, last ) )
	{
		ber_int_t mod_op;
		mod = (LDAPMod *) slapi_ch_malloc( sizeof(LDAPMod) );
		mod->mod_bvalues = NULL;
		len = -1; /* reset - len is not used */

		if ( ber_scanf( ber, "{i{a[V]}}", &mod_op, &type,
		    &mod->mod_bvalues ) == LBER_ERROR )
		{
			op_shared_log_error_access (pb, "MOD", dn, "decoding error");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
							  "decoding error", 0, NULL );
			ber_bvecfree(mod->mod_bvalues);
			slapi_ch_free((void **)&mod);
			slapi_ch_free_string(&type);
			goto free_and_return;
		}
		mod->mod_op = mod_op;
		mod->mod_type = slapi_attr_syntax_normalize(type);
		if ( !mod->mod_type || !*mod->mod_type ) {
			char ebuf[BUFSIZ];
			PR_snprintf (ebuf, BUFSIZ, "invalid type '%s'", type);
			op_shared_log_error_access (pb, "MOD", dn, ebuf);
			send_ldap_result( pb, LDAP_INVALID_SYNTAX, NULL, ebuf, 0, NULL );
			slapi_ch_free((void **)&type);
			ber_bvecfree(mod->mod_bvalues);
			slapi_ch_free_string(&mod->mod_type);
			slapi_ch_free((void **)&mod);
			goto free_and_return;
		}
		slapi_ch_free((void **)&type);

		if ( mod->mod_op != LDAP_MOD_ADD &&
		    mod->mod_op != LDAP_MOD_DELETE &&
		    mod->mod_op != LDAP_MOD_REPLACE )
		{
			op_shared_log_error_access (pb, "MOD", dn, "unrecognized modify operation");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
							  "unrecognized modify operation", 0, NULL );
			ber_bvecfree(mod->mod_bvalues);
			slapi_ch_free((void **)&(mod->mod_type));
			slapi_ch_free((void **)&mod);
			goto free_and_return;
		}

		if ( mod->mod_bvalues == NULL
		    && mod->mod_op != LDAP_MOD_DELETE
		    && mod->mod_op != LDAP_MOD_REPLACE )
		{
			op_shared_log_error_access (pb, "MOD", dn, "no values given");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
							  "no values given", 0, NULL );
			ber_bvecfree(mod->mod_bvalues);
			slapi_ch_free((void **)&(mod->mod_type));
			slapi_ch_free((void **)&mod);
			goto free_and_return;
		}

		/* check if user is allowed to modify the specified attribute */
		if (!op_shared_is_allowed_attr (mod->mod_type, pb->pb_conn->c_isreplication_session))
		{
            /* for now we just ignore attributes that client is not allowed
               to modify so not to break existing clients */
			++ignored_some_mods;
			ber_bvecfree(mod->mod_bvalues);
			slapi_ch_free((void **)&(mod->mod_type));
			slapi_ch_free((void **)&mod);
            continue;
			/* send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL );
			goto free_and_return; */
		}
		
		/* check for password change */
		if ( mod->mod_bvalues != NULL && 
			 strcasecmp( mod->mod_type, SLAPI_USERPWD_ATTR ) == 0 ){
			has_password_mod++;
		}

		mod->mod_op |= LDAP_MOD_BVALUES;
		slapi_mods_add_ldapmod (&smods, mod);
	}

	/* check for decoding error */
	if ( (tag != LBER_END_OF_SEQORSET) && (len != -1) )
	{
		op_shared_log_error_access (pb, "MOD", dn, "decoding error");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0, NULL );
		goto free_and_return;
	} 

	/* decode the optional controls - put them in the pblock */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 )
	{
		op_shared_log_error_access (pb, "MOD", dn, "failed to decode LDAP controls");
		send_ldap_result( pb, err, NULL, NULL, 0, NULL );
		goto free_and_return;
	}

	/* if there are any password mods, see if they are allowed */
	if (has_password_mod) {
		/* iterate through the mods looking for password mods */
		for (mod = slapi_mods_get_first_mod(&smods);
			 mod;
			 mod = slapi_mods_get_next_mod(&smods)) {
			if ( mod->mod_bvalues != NULL && 
				 strcasecmp( mod->mod_type, SLAPI_USERPWD_ATTR ) == 0 ) {
				/* assumes controls have already been decoded and placed
				   in the pblock */
				pw_change = op_shared_allow_pw_change (pb, mod, &old_pw, &smods);
				if (pw_change == -1) {
					goto free_and_return;
				}
			}
		}
	}

	if (!pb->pb_conn->c_isreplication_session &&
		pb->pb_conn->c_needpw && pw_change == 0 )
	{
		(void)slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
		op_shared_log_error_access (pb, "MOD", dn, "need new password");
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL );
		goto free_and_return;
	}

	/* see if there were actually any mods to perform */
	if ( slapi_mods_get_num_mods (&smods) == 0 )
	{
		int		lderr;
		char	*emsg;

		if ( ignored_some_mods ) {
			lderr = LDAP_UNWILLING_TO_PERFORM;
			emsg = "no modifiable attributes specified";
		} else {
			lderr = LDAP_PROTOCOL_ERROR;
			emsg = "no modifications specified";
		}
		op_shared_log_error_access (pb, "MOD", dn, emsg);
		send_ldap_result( pb, lderr, NULL, emsg, 0, NULL );
		goto free_and_return;
	}

#ifdef LDAP_DEBUG
	LDAPDebug( LDAP_DEBUG_ARGS, "modifications:\n", 0, 0, 0 );
	for (mod = slapi_mods_get_first_mod(&smods); mod != NULL; 
		 mod = slapi_mods_get_next_mod(&smods))
	{
		LDAPDebug( LDAP_DEBUG_ARGS, "\t%s: %s\n",
		mod_op_image( mod->mod_op ), mod->mod_type, 0 );
	}
#endif
	
	mods = slapi_mods_get_ldapmods_passout (&smods);

	slapi_pblock_set( pb, SLAPI_MODIFY_MODS, mods);

	op_shared_modify ( pb, pw_change, old_pw );

	slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
	ldap_mods_free (mods, 1 /* Free the Array and the Elements */);

free_and_return:;
	slapi_ch_free ((void**)&dn);
	slapi_mods_done(&smods);
}

/* This function is used to issue internal modify operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_modify_internal_pb instead */
Slapi_PBlock*
slapi_modify_internal(const char *idn, 
                      LDAPMod **mods, 
                      LDAPControl **controls,
                      int dummy)
{
    Slapi_PBlock    pb;
    Slapi_PBlock    *result_pb = NULL;
    int             opresult;

    pblock_init(&pb);    	

	slapi_modify_internal_set_pb (&pb, idn, (LDAPMod**)mods, controls, NULL, 
		(void *)plugin_get_default_component_id(), 0);

	modify_internal_pb (&pb);

    result_pb = slapi_pblock_new();
	if (result_pb)
	{
		slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);	
		slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
	}
	pblock_done(&pb);

    return result_pb;
}

/*  This is new style API to issue internal modify operation.
	pblock should contain the following data (can be set via call to slapi_modify_internal_set_pb):
	For uniqueid based operation:
		SLAPI_TARGET_DN set to dn that allows to select right backend, can be stale
		SLAPI_TARGET_UNIQUEID set to the uniqueid of the entry we are looking for
		SLAPI_MODIFY_MODS set to the mods
		SLAPI_CONTROLS_ARG set to request controls if present

	For dn based search:
		SLAPI_TARGET_DN set to the entry dn
		SLAPI_MODIFY_MODS set to the mods
		SLAPI_CONTROLS_ARG set to request controls if present		 				
 */
int slapi_modify_internal_pb (Slapi_PBlock *pb)
{
	if (pb == NULL)
		return -1; 

	if (!allow_operation (pb))
	{
		slapi_send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
						 "This plugin is not configured to access operation target data", 0, NULL );
		return 0;
	}

	return modify_internal_pb (pb);
}

/* Initialize a pblock for a call to slapi_modify_internal_pb() */
void slapi_modify_internal_set_pb (Slapi_PBlock *pb, const char *dn, LDAPMod **mods, LDAPControl **controls, 
								   const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags)
{
	Operation *op;
	PR_ASSERT (pb != NULL);
	if (pb == NULL || dn == NULL || mods == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"slapi_modify_internal_set_pb: NULL parameter\n");
		return;
	}

    op= internal_operation_new(SLAPI_OPERATION_MODIFY,operation_flags);
	slapi_pblock_set(pb, SLAPI_OPERATION, op);       
	slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, (void*)dn);
    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
	slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
	if (uniqueid)
	{
		slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void*)uniqueid);
	}
	slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

/* Helper functions */

static int modify_internal_pb (Slapi_PBlock *pb)
{
	LDAPControl	**controls;
	int pwpolicy_ctrl = 0;
	Operation       *op;
	int		opresult = 0;
	LDAPMod         **normalized_mods = NULL;
	LDAPMod	        **mods;
	LDAPMod	        **mod;
	Slapi_Mods      smods;
	int		pw_change = 0;
	char		*old_pw = NULL;

	PR_ASSERT (pb != NULL);

	slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
	slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

	/* See if pwpolicy control is present.  We need to do
	 * this before we call op_shared_allow_pw_change() since
	 * it looks for SLAPI_PWPOLICY in the pblock to determine
	 * if the response contorl is needed. */
	pwpolicy_ctrl = slapi_control_present( controls,
		LDAP_X_CONTROL_PWPOLICY_REQUEST, NULL, NULL );
        slapi_pblock_set( pb, SLAPI_PWPOLICY, &pwpolicy_ctrl );

	if(mods == NULL)
    {
        opresult = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        return 0;
    }

	/* first normalize the mods so they are bvalue
     * Note: We don't add any special
     * attributes such as "creatorsname". 
     * for CIR we don't want to change them, for other
     * plugins the writer should change these if it wants too by explicitly
     * adding them to the mods
     */
    normalized_mods = normalize_mods2bvals((const LDAPMod**)mods);
    if (normalized_mods == NULL)
    {
        opresult = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        return 0;
    }

	/* check for password change */
	mod = normalized_mods;
	while (*mod)
	{
		if ((*mod)->mod_bvalues != NULL && strcasecmp((*mod)->mod_type, SLAPI_USERPWD_ATTR) == 0)
		{
			slapi_mods_init_passin(&smods, mods);
			pw_change = op_shared_allow_pw_change (pb, *mod, &old_pw, &smods);
			if (pw_change == -1)
			{
				/* The internal result code will already have been set by op_shared_allow_pw_change() */
				return 0;
			}
		}

		mod ++;
	}

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);       
    op->o_handler_data   = &opresult;
    op->o_result_handler = internal_getresult_callback;

	slapi_pblock_set(pb, SLAPI_MODIFY_MODS, normalized_mods);
	slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);
	
	/* set parameters common for all internal operations */
	set_common_params (pb);	

	/* set actions taken to process the operation */
	set_config_params (pb);

	/* perform modify operation */
	op_shared_modify (pb, pw_change, old_pw);

	/* free the normalized_mods don't forget to add this*/
	slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &normalized_mods);
    if (normalized_mods != NULL)
    {
		ldap_mods_free(normalized_mods, 1);        
    }

	/* return original mods here */
	slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods); 
	/* set result */
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);

	return 0;
}

static void op_shared_modify (Slapi_PBlock *pb, int pw_change, char *old_pw)
{
	Slapi_Backend *be = NULL;
	Slapi_Entry	*pse;
	Slapi_Entry *referral;
	Slapi_Entry	*ecopy = NULL;
	Slapi_Entry	*e = NULL;
	char ebuf[BUFSIZ];
	char *dn;
	Slapi_DN sdn;
	LDAPMod	**mods, *pw_mod, **tmpmods = NULL;
	Slapi_Mods smods;
	Slapi_Mods unhashed_pw_smod;	
	int repl_op, internal_op, lastmod, skip_modified_attrs;
	char *unhashed_pw_attr = NULL;
	Slapi_Operation *operation;
	char errorbuf[BUFSIZ];
	int err;
    LDAPMod *lc_mod = NULL;
	struct slapdplugin  *p = NULL;
	int numattr, i;

	slapi_pblock_get (pb, SLAPI_ORIGINAL_TARGET, &dn);
	slapi_pblock_get (pb, SLAPI_MODIFY_MODS, &mods);
	slapi_pblock_get (pb, SLAPI_MODIFY_MODS, &tmpmods);
	slapi_pblock_get (pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
	internal_op= operation_is_flag_set(operation, OP_FLAG_INTERNAL);
	slapi_pblock_get (pb, SLAPI_SKIP_MODIFIED_ATTRS, &skip_modified_attrs);

	if (dn == NULL)
	{
		slapi_sdn_init_dn_byref (&sdn, "");
	}
	else
	{
		slapi_sdn_init_dn_byref (&sdn, dn);
	}

	slapi_pblock_set(pb, SLAPI_MODIFY_TARGET, (void*)slapi_sdn_get_ndn (&sdn));

	slapi_mods_init_passin (&smods, mods);	

	slapi_mods_init(&unhashed_pw_smod, 0);

	/* target spec is used to decide which plugins are applicable for the operation */
	operation_set_target_spec (pb->pb_op, &sdn);

	if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
	{ 
		if ( !internal_op )
		{
			slapi_log_access(LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d MOD dn=\"%s\"\n",
							 pb->pb_conn->c_connid, 
							 pb->pb_op->o_opid,
							 escape_string(slapi_sdn_get_dn(&sdn), ebuf));
		}
		else
		{
			slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d MOD dn=\"%s\"\n",
							LOG_INTERNAL_OP_CON_ID,
							LOG_INTERNAL_OP_OP_ID,
							 escape_string(slapi_sdn_get_dn(&sdn), ebuf));
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
					"cannot update referral", 0, NULL);
			slapi_entry_free(referral);
			goto free_and_return;
		}
	
		send_referrals_from_entry(pb,referral);
		slapi_entry_free(referral);
		goto free_and_return;
	}

	slapi_pblock_set(pb, SLAPI_BACKEND, be);

	/* The following section checks the valid values of fine-grained
	 * password policy attributes. 
	 * 1. First, it checks if the entry has "passwordpolicy" objectclass.
	 * 2. If yes, then if the mods contain any passwdpolicy specific attributes.
	 * 3. If yes, then it invokes corrosponding checking function.
	 */
	if ( !repl_op && !internal_op && dn &&
		(e = get_entry(pb, slapi_dn_normalize(dn))) )
	{
		Slapi_Value target;
		slapi_value_init(&target);
		slapi_value_set_string(&target,"passwordpolicy");
		if ((slapi_entry_attr_has_syntax_value(e, "objectclass", &target)) == 1)
		{	
			numattr = sizeof(AttrValueCheckList)/sizeof(AttrValueCheckList[0]);
			while ( tmpmods && *tmpmods )
			{
				if ((*tmpmods)->mod_bvalues != NULL &&
				!SLAPI_IS_MOD_DELETE((*tmpmods)->mod_op))
				{
					for (i=0; i < numattr; i++)
					{
						if (slapi_attr_type_cmp((*tmpmods)->mod_type, 
							AttrValueCheckList[i].attr_name, SLAPI_TYPE_CMP_SUBTYPE) == 0)
						{
							/* The below function call is good for
							 * single-valued attrs only
							 */
							if ( (err = AttrValueCheckList[i].checkfunc (AttrValueCheckList[i].attr_name,
								(*tmpmods)->mod_bvalues[0]->bv_val, AttrValueCheckList[i].minval,
								AttrValueCheckList[i].maxval, errorbuf))
								!= LDAP_SUCCESS)
							{
								/* return error */
								send_ldap_result(pb, err, NULL, errorbuf, 0, NULL);
								goto free_and_return;
							}
						}
					}
				}
				tmpmods++;
			} /* end of (while */
		} /* end of if (found */
		value_done (&target);
	} /* end of if (!repl_op */

	/* can get lastmod only after backend is selected */	
	slapi_pblock_get(pb, SLAPI_BE_LASTMOD, &lastmod);
	
	/* if this is replication session or the operation has been
	 * flagged - leave mod attributes alone */
	if (!repl_op && !skip_modified_attrs && lastmod)
	{
		modify_update_last_modified_attr(pb, &smods);
	}			
			
	/*
	 * Add the unhashed password pseudo-attribute before
	 * calling the preop plugins
	 */

	if (pw_change)
	{
		Slapi_Value **va= NULL;

		unhashed_pw_attr = slapi_attr_syntax_normalize(PSEUDO_ATTR_UNHASHEDUSERPASSWORD);

		for ( pw_mod = slapi_mods_get_first_mod(&smods); pw_mod; 
			  pw_mod = slapi_mods_get_next_mod(&smods) )
		{
			if (strcasecmp (pw_mod->mod_type, SLAPI_USERPWD_ATTR) != 0)
				continue;

			/* add pseudo password attribute */
			valuearray_init_bervalarray(pw_mod->mod_bvalues, &va);
			slapi_mods_add_mod_values(&smods, pw_mod->mod_op, unhashed_pw_attr, va);
			valuearray_free(&va);

			/* Init new value array for hashed value */
			valuearray_init_bervalarray(pw_mod->mod_bvalues, &va);

			/* encode password */
			pw_encodevals_ext(pb, &sdn, va);

			/* remove current clear value of userpassword */
			ber_bvecfree(pw_mod->mod_bvalues);
			/* add the cipher in the structure */
			valuearray_get_bervalarray(va, &pw_mod->mod_bvalues);

			valuearray_free(&va);
		}
	}
	for ( p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL; p = p->plg_next )
    {
        char *L_attr = NULL;
        int i = 0;
 
        /* Get the appropriate encoding function */
        for ( L_attr = p->plg_argv[i]; i<p->plg_argc; L_attr = p->plg_argv[++i])
        {
            char *L_normalized = slapi_attr_syntax_normalize(L_attr);

			for ( lc_mod = slapi_mods_get_first_mod(&smods); lc_mod; 
				  lc_mod = slapi_mods_get_next_mod(&smods) )
			{
                Slapi_Value **va= NULL;

				if (strcasecmp (lc_mod->mod_type, L_normalized) != 0)
					continue;
 
                switch (lc_mod->mod_op & ~LDAP_MOD_BVALUES)
                {
                    case LDAP_MOD_ADD:
                    case LDAP_MOD_REPLACE:
 
                        /* Init new value array for hashed value */
                        valuearray_init_bervalarray(lc_mod->mod_bvalues, &va);
						if ( va )
						{	
							/* encode local credentials */
							pw_rever_encode(va, L_normalized);
							/* remove current clear value of userpassword */
							ber_bvecfree(lc_mod->mod_bvalues);
							/* add the cipher in the structure */
							valuearray_get_bervalarray(va, &lc_mod->mod_bvalues);
	 
							valuearray_free(&va);
						}
                        break;
                    default:
                        /* for LDAP_MOD_DELETE, don't do anything */
                        /* for LDAP_MOD_BVALUES, don't do anything */
                        ;
                }                          
            }
            if (L_normalized)
                slapi_ch_free ((void**)&L_normalized);
        }
	}

	/*
	 * call the pre-mod plugins. if they succeed, call
	 * the backend mod function. then call the post-mod
	 * plugins.
	 */
	slapi_pblock_set (pb, SLAPI_MODIFY_MODS, (void*)slapi_mods_get_ldapmods_passout (&smods));
	if (plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN : 
							SLAPI_PLUGIN_PRE_MODIFY_FN) == 0)
	{
		int	rc;

		slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
		set_db_default_result_handlers(pb);

		/* Remove the unhashed password pseudo-attribute prior */
		/* to db access */
		if (pw_change)
		{
			slapi_pblock_get (pb, SLAPI_MODIFY_MODS, &mods);
			slapi_mods_init_passin (&smods, mods);
			remove_mod (&smods, unhashed_pw_attr, &unhashed_pw_smod);
			slapi_pblock_set (pb, SLAPI_MODIFY_MODS, 
							  (void*)slapi_mods_get_ldapmods_passout (&smods));	
		}

		if (be->be_modify != NULL)
		{
			if ((rc = (*be->be_modify)(pb)) == 0)
			{
				/* acl is not used for internal operations */
				/* don't update aci store for remote acis  */
				if ((!internal_op) &&
					(!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
				{
					plugin_call_acl_mods_update (pb, SLAPI_OPERATION_MODIFY); 
				}

				if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_AUDIT))
					write_audit_log_entry(pb); /* Record the operation in the audit log */

				if (pw_change && (!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
				{
					/* update the password info */
					update_pw_info (pb, old_pw);
				}
				slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
				do_ps_service(pse, NULL, LDAP_CHANGETYPE_MODIFY, 0);
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
		else
		{
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
							 "Function not implemented", 0, NULL);
		}
		/* Add the pseudo-attribute prior to calling the postop plugins */
		if (pw_change)
		{
			LDAPMod *lc_mod = NULL;

			slapi_pblock_get (pb, SLAPI_MODIFY_MODS, &mods);
			slapi_mods_init_passin (&smods, mods);
			for ( lc_mod = slapi_mods_get_first_mod(&unhashed_pw_smod); lc_mod; 
				  lc_mod = slapi_mods_get_next_mod(&unhashed_pw_smod) )
			{
				Slapi_Mod lc_smod;
				slapi_mod_init_byval(&lc_smod, lc_mod); /* copies lc_mod */
				/* this extracts the copy of lc_mod and finalizes lc_smod too */
				slapi_mods_add_ldapmod(&smods,
									   slapi_mod_get_ldapmod_passout(&lc_smod));
			}
			slapi_pblock_set (pb, SLAPI_MODIFY_MODS, 
							  (void*)slapi_mods_get_ldapmods_passout (&smods));
			slapi_mods_done(&unhashed_pw_smod); /* can finalize now */
		}

		slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
		plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN :
					        SLAPI_PLUGIN_POST_MODIFY_FN);

	}

free_and_return:
	slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &ecopy);
	slapi_entry_free(ecopy);
	slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &ecopy);
	slapi_entry_free(ecopy);
	slapi_entry_free(e);
	
	if (be)
		slapi_be_Unlock(be);
    slapi_sdn_done(&sdn);

	if (unhashed_pw_attr)
		slapi_ch_free ((void**)&unhashed_pw_attr);
}

static void remove_mod (Slapi_Mods *smods, const char *type, Slapi_Mods *smod_unhashed)
{
	LDAPMod *mod;
	Slapi_Mod smod;

	for (mod = slapi_mods_get_first_mod(smods);	mod; mod = slapi_mods_get_next_mod(smods))
	{
		if (strcasecmp (mod->mod_type, type) == 0)
		{
			slapi_mod_init_byval (&smod, mod);
			slapi_mods_add_smod(smod_unhashed, &smod);
			slapi_mods_remove (smods);
		}
	}
}

static int op_shared_allow_pw_change (Slapi_PBlock *pb, LDAPMod *mod, char **old_pw, Slapi_Mods *smods)
{
	int isroot, internal_op, repl_op, pwresponse_req = 0;
	int res = 0;
	char *dn;
	char *errtxt = NULL;
	Slapi_DN sdn;
	Slapi_Entry *e = NULL;
	passwdPolicy *pwpolicy;
	int rc = 0;
	char ebuf[BUFSIZ];
	Slapi_Value **values= NULL;
	Slapi_Operation *operation;

	slapi_pblock_get (pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
	if (repl_op) {
		/* Treat like there's no password */
		return (0);
	}

	*old_pw = NULL;

	slapi_pblock_get (pb, SLAPI_ORIGINAL_TARGET, &dn);
	slapi_pblock_get (pb, SLAPI_REQUESTOR_ISROOT, &isroot);	
	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
	slapi_pblock_get (pb, SLAPI_PWPOLICY, &pwresponse_req);
	internal_op= operation_is_flag_set(operation, OP_FLAG_INTERNAL);

	
	slapi_sdn_init_dn_byref (&sdn, dn);
	pwpolicy = new_passwdPolicy(pb, (char *)slapi_sdn_get_ndn(&sdn));

	/* internal operation has root permisions for subtrees it is allowed to access */
	if (!internal_op) 
	{	                        
		/* slapi_acl_check_mods needs an array of LDAPMods, but
		 * we're really only interested in the one password mod. */
		LDAPMod *mods[2];
		mods[0] = mod;
		mods[1] = NULL;

		/* Create a bogus entry with just the target dn.  This will
		 * only be used for checking the ACIs. */
		e = slapi_entry_alloc();
		slapi_entry_init( e, NULL, NULL );
		slapi_sdn_set_dn_byref(slapi_entry_get_sdn(e), dn);

		/* Set the backend in the pblock.  The slapi_access_allowed function
		 * needs this set to work properly. */
		slapi_pblock_set( pb, SLAPI_BACKEND, slapi_be_select( &sdn ) );

		/* Check if ACIs allow password to be changed */
		if ( (res = slapi_acl_check_mods(pb, e, mods, &errtxt)) != LDAP_SUCCESS) {
			/* Write access is denied to userPassword by ACIs */
			if ( pwresponse_req == 1 ) {
                               	slapi_pwpolicy_make_response_control ( pb, -1, -1,
						LDAP_PWPOLICY_PWDMODNOTALLOWED );
                       	}

                       	send_ldap_result(pb, res, NULL, errtxt, 0, NULL);
			slapi_ch_free_string(&errtxt);
			rc = -1;
			goto done;
		}

		/* Check if password policy allows users to change their passwords.*/
		if (!pb->pb_op->o_isroot && slapi_sdn_compare(&sdn, &pb->pb_op->o_sdn)==0 &&
			!pb->pb_conn->c_needpw && !pwpolicy->pw_change)
		{
			if ( pwresponse_req == 1 ) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1, LDAP_PWPOLICY_PWDMODNOTALLOWED );
			}
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
							 "user is not allowed to change password", 0, NULL);

			if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
			{
				slapi_log_access(LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d MOD dn=\"%s\", %s\n",
	    						 pb->pb_conn->c_connid, pb->pb_op->o_opid,
	    						 escape_string(slapi_sdn_get_dn(&sdn), ebuf), 
								 "user is not allowed to change password");
			}
	
			rc = -1;
			goto done;
		}
	}
	       
	/* check if password is within password minimum age;
	   error result is sent directly from check_pw_minage */	
	if ((internal_op || !pb->pb_conn->c_needpw) && 
         check_pw_minage(pb, &sdn, mod->mod_bvalues) == 1)
	{
		if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
		{
			if ( !internal_op )
			{
				slapi_log_access(LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d MOD dn=\"%s\", %s\n",
								 pb->pb_conn->c_connid, 
								 pb->pb_op->o_opid,
								 escape_string(slapi_sdn_get_dn(&sdn), ebuf), 
								 "within password minimum age");
			}
			else
			{
				slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d MOD dn=\"%s\", %s\n",
                            LOG_INTERNAL_OP_CON_ID,
                            LOG_INTERNAL_OP_OP_ID,
								 escape_string(slapi_sdn_get_dn(&sdn), ebuf), 
								 "within password minimum age");
			}
		}

		rc = -1;
		goto done;
	}


	/* check password syntax; remember the old password;
	   error sent directly from check_pw_syntax function */
	valuearray_init_bervalarray(mod->mod_bvalues, &values);
	switch (check_pw_syntax_ext (pb, &sdn, values, old_pw, NULL, 1, smods)) 
	{
		case 0: /* success */
				rc = 1;
				break;

		case 1: /* failed checking */
				if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
				{
					if ( !internal_op )
					{
						slapi_log_access(LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d MOD dn=\"%s\", %s\n",
										 pb->pb_conn->c_connid, 
										 pb->pb_op->o_opid,
										 escape_string(slapi_sdn_get_dn(&sdn), ebuf), "invalid password syntax");
					}
					else
					{
						slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d MOD dn=\"%s\", %s\n",
										LOG_INTERNAL_OP_CON_ID,
										LOG_INTERNAL_OP_OP_ID,
										 escape_string(slapi_sdn_get_dn(&sdn), ebuf), "invalid password syntax");
					}
				}
				rc = -1;
				break;

		case -1: /* The entry is not found.  No password checking is done.  Countinue execution
				    and it should get caught later and send "no such object back. */
				rc = 0;
				break;

		default: break;
	}
	valuearray_free(&values);

done:
	slapi_entry_free( e );
	slapi_sdn_done (&sdn);
	delete_passwdPolicy(&pwpolicy);
	return rc;
}
