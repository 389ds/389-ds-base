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

/* close.c - close ldbm backend */

#include "back-ldbm.h"

int ldbm_back_close( Slapi_PBlock *pb )
{
	struct ldbminfo	*li;

    LDAPDebug(LDAP_DEBUG_TRACE, LOG_DEBUG, "ldbm backend syncing\n", 0, 0, 0 );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	
	/* Kill off any sleeping threads by setting this flag */
	PR_Lock(li->li_shutdown_mutex);
	li->li_shutdown = 1;
	PR_Unlock(li->li_shutdown_mutex);

	dblayer_flush( li );		/* just be doubly sure! */

	/* close down all the ldbm instances */
	dblayer_close( li, DBLAYER_NORMAL_MODE );

	LDAPDebug(LDAP_DEBUG_TRACE, LOG_DEBUG, "ldbm backend done syncing\n", 0, 0, 0 );
	return 0;
}

int ldbm_back_flush( Slapi_PBlock *pb )
{
	struct ldbminfo	*li;

	LDAPDebug(LDAP_DEBUG_TRACE, LOG_DEBUG, "ldbm backend flushing\n", 0, 0, 0 );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	dblayer_flush( li );
	LDAPDebug(LDAP_DEBUG_TRACE, LOG_DEBUG, "ldbm backend done flushing\n", 0, 0, 0 );
	return 0;
}

void ldbm_back_instance_set_destructor(void **arg)
{
    /*
	Objset *instance_set = (Objset *) *arg;
    */
	
	/* This function is called when the instance set is destroyed.
	 * I can't really think of anything we should do here, but that
	 * may change in the future. */
	LDAPDebug(LDAP_DEBUG_ANY, LOG_ERR, "Set of instances destroyed\n", 0, 0, 0);
}
