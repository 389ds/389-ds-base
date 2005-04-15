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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

int 
chainingdb_start ( Slapi_PBlock *pb ) {

	cb_backend 		* cb;

  	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &cb );

	if (cb->started) {
		/* We may be called multiple times due to */
		/* plugin dependency resolution           */
		return 0;
	}

	/*
	** Reads in any configuration information held in the dse for the
	** chaining plugin. Create dse entries used to configure the 
	** chaining plugin if they don't exist. Registers plugins to maintain 
	** those dse entries.
	*/

	cb_config_load_dse_info(pb);

	/* Register new LDAPv3 controls supported by the chaining backend */

	slapi_register_supported_control( CB_LDAP_CONTROL_CHAIN_SERVER,
            SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE
            | SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE
            | SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN );

	/* register to be notified when backend state changes */
	slapi_register_backend_state_change((void *)cb_be_state_change, 
										cb_be_state_change);

	cb->started=1;
	return 0;
}
