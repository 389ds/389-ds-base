/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
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
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"
#include "pratom.h"


void
do_compare( Slapi_PBlock *pb )
{
	BerElement	*ber = pb->pb_op->o_ber;
	char		*rawdn = NULL;
	const char	*dn = NULL;
	struct ava	ava = {0};
	Slapi_Backend		*be = NULL;
	int		err;
	Slapi_DN sdn;
	Slapi_Entry *referral = NULL;
	char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];

	LDAPDebug( LDAP_DEBUG_TRACE, "do_compare\n", 0, 0, 0 );

	/* count the compare request */
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsCompareOps);

    /* have to init this here so we can "done" it below if we short circuit */
    slapi_sdn_init(&sdn);

	/*
	 * Parse the compare request.  It looks like this:
	 *
	 *	CompareRequest := [APPLICATION 14] SEQUENCE {
	 *		entry	DistinguishedName,
	 *		ava	SEQUENCE {
	 *			type	AttributeType,
	 *			value	AttributeValue
	 *		}
	 *	}
	 */

	if ( ber_scanf( ber, "{a{ao}}", &rawdn, &ava.ava_type,
	    &ava.ava_value ) == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ber_scanf failed (op=Compare; params=DN,Type,Value)\n",
		    0, 0, 0 );
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0,
			NULL );
		goto free_and_return;
	}
	/* Check if we should be performing strict validation. */
	if (config_get_dn_validate_strict()) {
		/* check that the dn is formatted correctly */
		err = slapi_dn_syntax_check(pb, rawdn, 1);
		if (err) { /* syntax check failed */
			op_shared_log_error_access(pb, "CMP",
							rawdn?rawdn:"", "strict: invalid dn");
			send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
							 NULL, "invalid dn", 0, NULL);
			slapi_ch_free((void **) &rawdn);
			return;
		}
	}
	slapi_sdn_init_dn_passin(&sdn, rawdn);
	dn = slapi_sdn_get_dn(&sdn);
    if (rawdn && (strlen(rawdn) > 0) && (NULL == dn)) {
        /* normalization failed */
        op_shared_log_error_access(pb, "CMP", rawdn, "invalid dn");
        send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                         "invalid dn", 0, NULL);
        slapi_sdn_done(&sdn);
        return;
    }
	/*
	 * in LDAPv3 there can be optional control extensions on
	 * the end of an LDAPMessage. we need to read them in and
	 * pass them to the backend.
	 */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 ) {
		send_ldap_result( pb, err, NULL, NULL, 0, NULL );
		goto free_and_return;
	}

	/* target spec is used to decide which plugins are applicable for the operation */
	operation_set_target_spec (pb->pb_op, &sdn);

	LDAPDebug( LDAP_DEBUG_ARGS, "do_compare: dn (%s) attr (%s)\n",
	    rawdn, ava.ava_type, 0 );

	slapi_log_access( LDAP_DEBUG_STATS,
	    "conn=%" NSPRIu64 " op=%d CMP dn=\"%s\" attr=\"%s\"\n",
	    pb->pb_conn->c_connid, pb->pb_op->o_opid, dn, ava.ava_type );

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one.
	 */
	if ((err = slapi_mapping_tree_select(pb, &be, &referral, errorbuf, sizeof(errorbuf))) != LDAP_SUCCESS) {
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
					"cannot compare referral", 0, NULL);
			slapi_entry_free(referral);
			goto free_and_return;
		}
	
		send_referrals_from_entry(pb,referral);
		slapi_entry_free(referral);
		goto free_and_return;
	}

	if ( be->be_compare != NULL ) {
		int		isroot;
		    
		slapi_pblock_set( pb, SLAPI_BACKEND, be );
		isroot = pb->pb_op->o_isroot;

		slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
		/* EXCEPTION: compare target does not allocate memory. */
		/* target never be modified by plugins. */
		slapi_pblock_set( pb, SLAPI_COMPARE_TARGET_SDN, (void*)&sdn );
		slapi_pblock_set( pb, SLAPI_COMPARE_TYPE, ava.ava_type);
		slapi_pblock_set( pb, SLAPI_COMPARE_VALUE, &ava.ava_value );
		/*
		 * call the pre-compare plugins. if they succeed, call
		 * the backend compare function. then call the
		 * post-compare plugins.
		 */
		if ( plugin_call_plugins( pb,
				SLAPI_PLUGIN_PRE_COMPARE_FN ) == 0 ) {
			int	rc;

			slapi_pblock_set( pb, SLAPI_PLUGIN, be->be_database );
			set_db_default_result_handlers(pb);
			rc = (*be->be_compare)( pb );

			slapi_pblock_set( pb, SLAPI_PLUGIN_OPRETURN, &rc );
			plugin_call_plugins( pb, SLAPI_PLUGIN_POST_COMPARE_FN );
		}
	} else {
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
		    "Function not implemented", 0, NULL );
	}

free_and_return:;
	if (be)
		slapi_be_Unlock(be);
	slapi_sdn_done(&sdn);
	ava_done( &ava );
}
