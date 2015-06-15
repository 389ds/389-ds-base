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
