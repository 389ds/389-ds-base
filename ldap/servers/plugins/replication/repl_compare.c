/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "slapi-plugin.h"
#include "repl.h"

int
legacy_preop_compare( Slapi_PBlock *pb )
{
	int is_replicated_operation = 0;
	char *compare_base = NULL;
	struct berval **referral = NULL;
	int return_code = 0;
	Slapi_DN *basesdn;

	slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
	slapi_pblock_get(pb, SLAPI_COMPARE_TARGET, &compare_base);
	basesdn= slapi_sdn_new_dn_byref(compare_base);
    referral = get_data_source(pb, basesdn, 1, NULL);
	slapi_sdn_free(&basesdn);
	if (NULL != referral && !is_replicated_operation)
	{
		/*
		 * There is a copyingFrom in this entry or an ancestor.
		 * Return a referral to the supplier, and we're all done.
		 */
		slapi_send_ldap_result(pb, LDAP_REFERRAL, NULL, NULL, 0, referral);
		return_code = 1;	/* return 1 to prevent further search processing */
	}
	return return_code;
}
