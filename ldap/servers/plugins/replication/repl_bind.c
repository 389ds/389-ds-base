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
#include "repl5.h"


int
legacy_preop_bind( Slapi_PBlock *pb )
{
    int return_value = 0;
	const char *dn = NULL;
	Slapi_DN *sdn = NULL;
	struct berval *cred = NULL;
	ber_tag_t method;
    
	slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method);
	slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &sdn);
	slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred);
	dn = slapi_sdn_get_dn(sdn);

	if (LDAP_AUTH_SIMPLE == method)
	{
		if (legacy_consumer_is_replicationdn(dn) && legacy_consumer_is_replicationpw(cred))
		{
			/* Successful bind as replicationdn */
			void *conn = NULL;
			consumer_connection_extension *connext = NULL;
#ifdef DEBUG
	slapi_log_error(SLAPI_LOG_REPL, LOG_DEBUG, REPLICATION_SUBSYSTEM, "legacy_preop_bind: begin\n");
#endif
			slapi_pblock_get( pb, SLAPI_CONNECTION, &conn );
			/* TEL 20120529 - Is there any reason we must protect this connext access? */
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
