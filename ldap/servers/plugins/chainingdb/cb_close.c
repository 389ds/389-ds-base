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

#include "cb.h"

/*
** Close a chaining backend instance
** Should be followed by a cleanup
*/


static void
free_cb_backend(cb_backend *cb)
{
    if(cb){
        slapi_destroy_rwlock(cb->config.rwl_config_lock);
        slapi_ch_free_string(&cb->pluginDN);
        slapi_ch_free_string(&cb->configDN);
        slapi_ch_array_free(cb->config.chainable_components);
        slapi_ch_array_free(cb->config.chaining_components);
        slapi_ch_array_free(cb->config.forward_ctrls);
        slapi_ch_free((void **)&cb);
    }
}

int cb_back_close( Slapi_PBlock *pb )
{
	Slapi_Backend *be;
	cb_backend_instance *inst;
	cb_backend *cb = cb_get_backend_type();
	int rc;

	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	if (be == NULL) {

		CB_ASSERT(cb!=NULL);

		slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, cb->configDN, LDAP_SCOPE_BASE,
		                             "(objectclass=*)",cb_config_modify_callback);
		slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, cb->configDN, LDAP_SCOPE_BASE,
		                             "(objectclass=*)",cb_config_modify_check_callback);
		slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, cb->configDN, LDAP_SCOPE_BASE,
		                             "(objectclass=*)",cb_config_add_callback);
		slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, cb->configDN, LDAP_SCOPE_BASE,
		                             "(objectclass=*)",cb_config_add_check_callback);
		slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, cb->configDN, LDAP_SCOPE_BASE,
		                             "(objectclass=*)",cb_config_search_callback);
		slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, cb->pluginDN,
		                             LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER,
		                             cb_config_add_instance_callback);
		slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, cb->pluginDN,
		                             LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER,
		                             cb_config_add_instance_check_callback);
		free_cb_backend(cb);

		return 0;
	}

	/* XXXSD: temp fix . Sometimes, this functions */
	/* gets called with a ldbm backend instance... */

	{
		const char *betype = slapi_be_gettype(be);
		if (!betype || strcasecmp(betype,CB_CHAINING_BACKEND_TYPE)) {
			slapi_log_error( SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM, "Wrong database type.\n");
			free_cb_backend(cb);
			return 0;
		}
	}

	inst = cb_get_instance(be);
	CB_ASSERT( inst != NULL );

	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,"Stopping chaining database instance %s\n",
			inst->configDn);
	/*
	 * emulate a backend instance deletion to clean up everything
	 */
	cb_instance_delete_config_callback(NULL, NULL,NULL, &rc, NULL, inst);
	free_cb_backend(cb);

	return 0;
}
