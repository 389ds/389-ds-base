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
