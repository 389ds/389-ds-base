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

/* cleanup.c - cleans up ldbm backend */

#include "back-ldbm.h"

int ldbm_back_cleanup( Slapi_PBlock *pb )
{
	struct ldbminfo	*li;
    Slapi_Backend *be;

	LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend cleaning up\n", 0, 0, 0 );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_BACKEND, &be );

	if (be->be_state != BE_STATE_STOPPED &&
		be->be_state != BE_STATE_DELETED)
	{
		LDAPDebug( LDAP_DEBUG_TRACE, 
				  "ldbm_back_cleanup: warning - backend is in a wrong state - %d\n", 
				  be->be_state, 0, 0 );
		return 0;
	}

	PR_Lock (be->be_state_lock);

	if (be->be_state != BE_STATE_STOPPED &&
		be->be_state != BE_STATE_DELETED)
	{
		LDAPDebug( LDAP_DEBUG_TRACE, 
				  "ldbm_back_cleanup: warning - backend is in a wrong state - %d\n", 
				  be->be_state, 0, 0 );
		PR_Unlock (be->be_state_lock);
		return 0;
	}
    
	dblayer_terminate( li );

/* JCM I tried adding this to tidy up memory on shutdown. */
/* JCM But, the result was very messy. */
/* JCM objset_delete(&li->li_instance_set); */

	be->be_state = BE_STATE_CLEANED;

	PR_Unlock (be->be_state_lock);

	return 0;
}
