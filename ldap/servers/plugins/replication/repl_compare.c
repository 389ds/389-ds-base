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

 
#include "slapi-plugin.h"
#include "repl.h"

int
legacy_preop_compare( Slapi_PBlock *pb )
{
	int is_replicated_operation = 0;
	struct berval **referral = NULL;
	int return_code = 0;
	Slapi_DN *basesdn = NULL;

	slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
	slapi_pblock_get(pb, SLAPI_COMPARE_TARGET_SDN, &basesdn);
	if (NULL == basesdn) {
		slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
				               "Null target DN", 0, NULL );

		return_code = 1;	/* return 1 to prevent further search processing */
		goto bail;
	}
	referral = get_data_source(pb, basesdn, 1, NULL);
	if (NULL != referral && !is_replicated_operation)
	{
		/*
		 * There is a copyingFrom in this entry or an ancestor.
		 * Return a referral to the supplier, and we're all done.
		 */
		slapi_send_ldap_result(pb, LDAP_REFERRAL, NULL, NULL, 0, referral);
		return_code = 1;	/* return 1 to prevent further search processing */
	}
	slapi_ch_free((void**)&referral);
bail:
	return return_code;
}
