/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

/*
** generic function to send back results
** Turn off acl eval on front-end when needed
*/

void cb_set_acl_policy(Slapi_PBlock *pb) {
 
        Slapi_Backend *be;
        cb_backend_instance *cb;
        int noacl;

        slapi_pblock_get( pb, SLAPI_BACKEND, &be );
        cb = cb_get_instance(be);

		/* disable acl checking if the local_acl flag is not set
		   or if the associated backend is disabled */
        noacl=!(cb->local_acl) || cb->associated_be_is_disabled;
 
        if (noacl) {
                slapi_pblock_set(pb, SLAPI_PLUGIN_DB_NO_ACL, &noacl);
	} else {
                /* Be very conservative about acl evaluation */
                slapi_pblock_set(pb, SLAPI_PLUGIN_DB_NO_ACL, &noacl);
        }
}

int cb_access_allowed(
        Slapi_PBlock        *pb,
        Slapi_Entry         *e,                 /* The Slapi_Entry */
        char                *attr,              /* Attribute of the entry */
        struct berval       *val,               /* value of attr. NOT USED */
        int                 access,              /* access rights */
	char 		    **errbuf
        )

{

switch (access) {

	case SLAPI_ACL_ADD:
	case SLAPI_ACL_DELETE:
	case SLAPI_ACL_COMPARE:
	case SLAPI_ACL_WRITE:
	case SLAPI_ACL_PROXY:

		/* Keep in mind some entries are NOT */
		/* available for acl evaluation      */

		return slapi_access_allowed(pb,e,attr,val,access);
	default:
		return LDAP_INSUFFICIENT_ACCESS;
}
}
