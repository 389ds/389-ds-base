/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* close.c - close ldbm backend */

#include "back-ldbm.h"

int ldbm_back_close( Slapi_PBlock *pb )
{
	struct ldbminfo	*li;

    LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend syncing\n", 0, 0, 0 );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	
	/* Kill off any sleeping threads by setting this flag */
	PR_Lock(li->li_shutdown_mutex);
	li->li_shutdown = 1;
	PR_Unlock(li->li_shutdown_mutex);

	dblayer_flush( li );		/* just be doubly sure! */

	/* close down all the ldbm instances */
	dblayer_close( li, DBLAYER_NORMAL_MODE );

	LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend done syncing\n", 0, 0, 0 );
	return 0;
}

int ldbm_back_flush( Slapi_PBlock *pb )
{
	struct ldbminfo	*li;

	LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend flushing\n", 0, 0, 0 );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	dblayer_flush( li );
	LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend done flushing\n", 0, 0, 0 );
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
	LDAPDebug(LDAP_DEBUG_ANY, "Set of instances destroyed\n", 0, 0, 0);
}
