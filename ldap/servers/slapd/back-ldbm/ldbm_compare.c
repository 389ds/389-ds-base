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

/* compare.c - ldbm backend compare routine */

#include "back-ldbm.h"

int
ldbm_back_compare( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	struct ldbminfo		*li;
	struct backentry	*e;
	int			err;
	char			    *type;
	struct berval		*bval;
	entry_address *addr;
	Slapi_Value compare_value;
	int result;
	int ret = 0;
	Slapi_DN *namespace_dn;


	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr);
	slapi_pblock_get( pb, SLAPI_COMPARE_TYPE, &type );
	slapi_pblock_get( pb, SLAPI_COMPARE_VALUE, &bval );
	
	inst = (ldbm_instance *) be->be_instance_info;
	/* get the namespace dn */
	namespace_dn = (Slapi_DN*)slapi_be_getsuffix(be, 0);

	if ( (e = find_entry( pb, be, addr, NULL )) == NULL ) {
		return( -1 );	/* error result sent by find_entry() */
	}

	err = slapi_access_allowed (pb, e->ep_entry, type, bval, SLAPI_ACL_COMPARE);
	if ( err != LDAP_SUCCESS ) {
		slapi_send_ldap_result( pb, err, NULL, NULL, 0, NULL );								
		ret = 1;
	} else {

		slapi_value_init_berval(&compare_value,bval);

		err = slapi_vattr_namespace_value_compare(e->ep_entry,namespace_dn,type,&compare_value,&result,0);

		if (0 != err) {
			/* Was the attribute not found ? */
			if (SLAPI_VIRTUALATTRS_NOT_FOUND == err) {
				slapi_send_ldap_result( pb, LDAP_NO_SUCH_ATTRIBUTE, NULL, NULL,0, NULL );
				ret = 1;
			} else {
				/* Some other problem, call it an operations error */
				slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL,0, NULL );	
				ret = -1;
			}
		} else {
			/* Interpret the result */
			if (result) {
				/* Compare true */
					slapi_send_ldap_result( pb, LDAP_COMPARE_TRUE, NULL, NULL, 0, NULL );
			} else {
				/* Compare false */
					slapi_send_ldap_result( pb, LDAP_COMPARE_FALSE, NULL, NULL, 0, NULL );
			}
			ret = 0;
		}
		value_done(&compare_value);
	}

	cache_return( &inst->inst_cache, &e );
	return( ret );
}
