/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

/*
** Close a chaining backend instance
** Should be followed by a cleanup
*/

int cb_back_close( Slapi_PBlock *pb )
{
	Slapi_Backend 		* be;
	cb_backend_instance 	* inst;
	int 			rc;
	
        slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	if (be == NULL) {

		cb_backend * cb = cb_get_backend_type();
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
			LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER, cb_config_add_instance_callback);

		return 0;
	}

	/* XXXSD: temp fix . Sometimes, this functions */
	/* gets called with a ldbm backend instance... */

	{
		const char * betype = slapi_be_gettype(be);
		if (!betype || strcasecmp(betype,CB_CHAINING_BACKEND_TYPE)) {

        		slapi_log_error( SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM,
				"Wrong database type.\n");
			return 0;
		}
	}

	inst = cb_get_instance(be);
	CB_ASSERT( inst!=NULL );

	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,"Stopping chaining database instance %s\n",
		inst->configDn);
	/* emulate a backend instance deletion */
	/* to clean up everything              */
	cb_instance_delete_config_callback(NULL, NULL,NULL, &rc, NULL, inst);

        return 0;
}
