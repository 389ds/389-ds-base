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

/* bind.c  - ldbm backend bind and unbind routines */

#include "back-ldbm.h"

int
ldbm_back_bind( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	ber_tag_t			method;
	struct berval		*cred;
	struct ldbminfo		*li;
	struct backentry	*e;
	Slapi_Attr		*attr;
	Slapi_Value **bvals;
	entry_address *addr;
	back_txn txn = {NULL};
	int rc = SLAPI_BIND_SUCCESS;

	/* get parameters */
	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr );
	slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method );
	slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &cred );
	slapi_pblock_get( pb, SLAPI_TXN, &txn.back_txn_txn );

	if ( !txn.back_txn_txn ) {
		dblayer_txn_init( li, &txn );
		slapi_pblock_set( pb, SLAPI_TXN, txn.back_txn_txn );
	}
	
	inst = (ldbm_instance *) be->be_instance_info;
	if (inst->inst_ref_count) {
		slapi_counter_increment(inst->inst_ref_count);
	} else {
		LDAPDebug1Arg(LDAP_DEBUG_ANY,
		              "ldbm_bind: instance %s does not exist.\n",
		              inst->inst_name);
		return( SLAPI_BIND_FAIL );
	}

	/* always allow noauth simple binds (front end will send the result) */
	if ( method == LDAP_AUTH_SIMPLE && cred->bv_len == 0 ) {
		rc = SLAPI_BIND_ANONYMOUS;
		goto bail;
	}

	/*
	 * find the target entry.  find_entry() takes care of referrals
	 *   and sending errors if the entry does not exist.
	 */
	if (( e = find_entry( pb, be, addr, &txn )) == NULL ) {
		rc = SLAPI_BIND_FAIL;
		goto bail;
	}

	switch ( method ) {
	case LDAP_AUTH_SIMPLE:
		{
		Slapi_Value cv;
		if ( slapi_entry_attr_find( e->ep_entry, "userpassword", &attr ) != 0 ) {
			slapi_send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
			    NULL, 0, NULL );
			CACHE_RETURN( &inst->inst_cache, &e );
			rc = SLAPI_BIND_FAIL;
			goto bail;
		}
		bvals= attr_get_present_values(attr);
		slapi_value_init_berval(&cv,cred);
		if ( slapi_pw_find_sv( bvals, &cv ) != 0 ) {
			slapi_send_ldap_result( pb, LDAP_INVALID_CREDENTIALS, NULL,
			    NULL, 0, NULL );
			CACHE_RETURN( &inst->inst_cache, &e );
			value_done(&cv);
			rc = SLAPI_BIND_FAIL;
			goto bail;
		}
		value_done(&cv);
		}
		break;

	default:
		slapi_send_ldap_result( pb, LDAP_STRONG_AUTH_NOT_SUPPORTED, NULL,
		    "auth method not supported", 0, NULL );
		CACHE_RETURN( &inst->inst_cache, &e );
		rc = SLAPI_BIND_FAIL;
		goto bail;
	}

	CACHE_RETURN( &inst->inst_cache, &e );
bail:
	if (inst->inst_ref_count) {
		slapi_counter_decrement(inst->inst_ref_count);
	}
	/* success:  front end will send result */
	return rc;
}
