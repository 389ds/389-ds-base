/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005-2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*
 * plugin_mmr.c - routines for calling mmr pre and postop plugins
 */

#include "slap.h"

int
plugin_call_mmr_plugin_preop(Slapi_PBlock *pb, Slapi_Entry *e, int flags)
{
	struct slapdplugin *p;
	int rc = LDAP_INSUFFICIENT_ACCESS;
	Operation *operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	/* we don't perform acl check for internal operations  and if the plugin has set it not to be checked
	if (operation_is_flag_set(operation, SLAPI_OP_FLAG_NO_ACCESS_CHECK|OP_FLAG_INTERNAL|OP_FLAG_REPLICATED|OP_FLAG_LEGACY_REPLICATION_DN))
		return LDAP_SUCCESS;
	*/

	/* call the global plugins first and then the backend specific */
	for ( p = get_plugin_list(PLUGIN_LIST_MMR); p != NULL; p = p->plg_next ) {
		if (plugin_invoke_plugin_sdn (p, SLAPI_PLUGIN_MMR_BETXN_PREOP, pb,
									  (Slapi_DN*)slapi_entry_get_sdn_const (e))){
			rc = (*p->plg_mmr_betxn_preop)(pb, flags);
			if ( rc != LDAP_SUCCESS ) break;
		}
	}

	return rc;
}

int
plugin_call_mmr_plugin_postop(Slapi_PBlock *pb, Slapi_Entry *e, int flags)
{
	struct slapdplugin *p;
	int rc = LDAP_INSUFFICIENT_ACCESS;
	Operation *operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	/* we don't perform acl check for internal operations  and if the plugin has set it not to be checked
	if (operation_is_flag_set(operation, SLAPI_OP_FLAG_NO_ACCESS_CHECK|OP_FLAG_INTERNAL|OP_FLAG_REPLICATED|OP_FLAG_LEGACY_REPLICATION_DN))
		return LDAP_SUCCESS;
	*/

	/* call the global plugins first and then the backend specific */
	for ( p = get_plugin_list(PLUGIN_LIST_MMR); p != NULL; p = p->plg_next ) {
		if (plugin_invoke_plugin_sdn (p, SLAPI_PLUGIN_MMR_BETXN_POSTOP, pb,
									  (Slapi_DN*)slapi_entry_get_sdn_const (e))){
			rc = (*p->plg_mmr_betxn_postop)(pb, flags);
			if ( rc != LDAP_SUCCESS ) break;
		}
	}

	return rc;
}
