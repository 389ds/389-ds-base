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
	int method;
    
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
	slapi_log_error(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM, "legacy_preop_bind: begin\n");
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
