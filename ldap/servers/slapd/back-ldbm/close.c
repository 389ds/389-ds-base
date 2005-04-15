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
