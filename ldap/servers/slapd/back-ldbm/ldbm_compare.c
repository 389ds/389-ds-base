/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* compare.c - ldbm backend compare routine */

#include "back-ldbm.h"

int
ldbm_back_compare( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	struct ldbminfo		*li;
	struct backentry	*e;
	int			err;
	char			    *type;
	struct berval		*bval;
	entry_address *addr;
	Slapi_Value compare_value;
	int result;
	int ret = 0;
	Slapi_DN *namespace_dn;


	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr);
	slapi_pblock_get( pb, SLAPI_COMPARE_TYPE, &type );
	slapi_pblock_get( pb, SLAPI_COMPARE_VALUE, &bval );
	
	inst = (ldbm_instance *) be->be_instance_info;
	/* get the namespace dn */
	namespace_dn = (Slapi_DN*)slapi_be_getsuffix(be, 0);

	if ( (e = find_entry( pb, be, addr, NULL )) == NULL ) {
		return( -1 );	/* error result sent by find_entry() */
	}

	err = slapi_access_allowed (pb, e->ep_entry, type, bval, SLAPI_ACL_COMPARE);
	if ( err != LDAP_SUCCESS ) {
		slapi_send_ldap_result( pb, err, NULL, NULL, 0, NULL );								
		ret = 1;
	} else {

		slapi_value_init_berval(&compare_value,bval);

		err = slapi_vattr_namespace_value_compare(e->ep_entry,namespace_dn,type,&compare_value,&result,0);

		if (0 != err) {
			/* Was the attribute not found ? */
			if (SLAPI_VIRTUALATTRS_NOT_FOUND == err) {
				slapi_send_ldap_result( pb, LDAP_NO_SUCH_ATTRIBUTE, NULL, NULL,0, NULL );
				ret = 1;
			} else {
				/* Some other problem, call it an operations error */
				slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL,0, NULL );	
				ret = -1;
			}
		} else {
			/* Interpret the result */
			if (result) {
				/* Compare true */
					slapi_send_ldap_result( pb, LDAP_COMPARE_TRUE, NULL, NULL, 0, NULL );
			} else {
				/* Compare false */
					slapi_send_ldap_result( pb, LDAP_COMPARE_FALSE, NULL, NULL, 0, NULL );
			}
			ret = 0;
		}
		value_done(&compare_value);
	}

	cache_return( &inst->inst_cache, &e );
	return( ret );
}
