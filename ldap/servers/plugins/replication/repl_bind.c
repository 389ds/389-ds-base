/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 

#include "slapi-plugin.h"
#include "repl.h"
#include "repl5.h"


int
legacy_preop_bind( Slapi_PBlock *pb )
{
    int return_value = 0;
	char *dn = NULL;
	struct berval *cred = NULL;
	int method;
	int one = 1;
    
	slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method);
	slapi_pblock_get(pb, SLAPI_BIND_TARGET, &dn);
	slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred);

	if (LDAP_AUTH_SIMPLE == method)
	{
		if (legacy_consumer_is_replicationdn(dn) && legacy_consumer_is_replicationpw(cred))
		{
			/* Successful bind as replicationdn */
			void *conn = NULL;
			consumer_connection_extension *connext = NULL;
#ifdef DEBUG
	slapi_log_error(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM, "legacy_preop_bind: begin\n");
#endif
			slapi_pblock_get( pb, SLAPI_CONNECTION, &conn );
			connext = (consumer_connection_extension*) repl_con_get_ext (REPL_CON_EXT_CONN, conn);
			if (NULL != connext)
			{
				connext->is_legacy_replication_dn = 1;
			}
			slapi_send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
			return_value = 1; /* Prevent further processing in front end */
		}
	}
	return return_value;

}
