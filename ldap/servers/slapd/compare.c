/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"
#include "pratom.h"


void
do_compare( Slapi_PBlock *pb )
{
	BerElement	*ber = pb->pb_op->o_ber;
	char		*dn;
	struct ava	ava;
	Slapi_Backend		*be = NULL;
	int		err;
	char		ebuf[ BUFSIZ ];
	Slapi_DN sdn;
	Slapi_Entry *referral;
	char errorbuf[BUFSIZ];

	LDAPDebug( LDAP_DEBUG_TRACE, "do_compare\n", 0, 0, 0 );

	/* count the compare request */
	PR_AtomicIncrement(g_get_global_snmp_vars()->ops_tbl.dsCompareOps);

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


	if ( ber_scanf( ber, "{a{ao}}", &dn, &ava.ava_type,
	    &ava.ava_value ) == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ber_scanf failed (op=Compare; params=DN,Type,Value)\n",
		    0, 0, 0 );
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0,
		    NULL );
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
	slapi_sdn_init_dn_passin(&sdn,dn);

	/* target spec is used to decide which plugins are applicable for the operation */
	operation_set_target_spec (pb->pb_op, &sdn);

	LDAPDebug( LDAP_DEBUG_ARGS, "do_compare: dn (%s) attr (%s)\n",
	    dn, ava.ava_type, 0 );

	slapi_log_access( LDAP_DEBUG_STATS,
	    "conn=%d op=%d CMP dn=\"%s\" attr=\"%s\"\n",
	    pb->pb_conn->c_connid, pb->pb_op->o_opid,
	    escape_string( dn, ebuf ), ava.ava_type );

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
		slapi_pblock_set( pb, SLAPI_COMPARE_TARGET, (void*)slapi_sdn_get_ndn(&sdn) );
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
