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

#include <string.h>

#include "repl.h"
#include "slapi-plugin.h"
 
/* Forward Declartions */
static int repl_monitor_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

int
repl_monitor_init()
{
    /* The FE DSE *must* be initialised before we get here */
    int return_value= LDAP_SUCCESS;
	static int initialized = 0;

	if (!initialized)
	{
		/* ONREPL - this is commented until we implement 4.0 style changelog 
        slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,"cn=monitor",LDAP_SCOPE_BASE,"(objectclass=*)",repl_monitor_search,NULL); */
		initialized = 1;
	}

    return return_value;
}

static int
repl_monitor_search(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    const char *sdv = get_server_dataversion();
    if ( sdv != NULL )
	{
	    int port;
    	char buf[BUFSIZ];
    	struct berval val;
    	struct berval *vals[2];
    	vals[0] = &val;
    	vals[1] = NULL;
    	port= config_get_port();
    	if(port==0)
    	{
    	    port= config_get_secureport();
    	}
		buf[0] = (char)0;
        /* ONREPL - how do we publish changenumbers now with multiple changelogs?
		sprintf( buf, "%s:%lu %s% lu", get_localhost_DNS(), port, sdv, ldapi_get_last_changenumber());
        */
    	val.bv_val = buf;
    	val.bv_len = strlen( buf );
    	slapi_entry_attr_replace( e, attr_dataversion, vals );
    }
	return SLAPI_DSE_CALLBACK_OK;
}

