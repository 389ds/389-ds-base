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


#include <string.h>

#include "repl.h"
#include "slapi-plugin.h"
 
#ifdef FOR_40_STYLE_CHANGELOG
/* Forward Declartions */
static int repl_monitor_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
#endif

int
repl_monitor_init()
{
    /* The FE DSE *must* be initialised before we get here */
    int return_value= LDAP_SUCCESS;
	static int initialized = 0;

	if (!initialized)
	{
#ifdef FOR_40_STYLE_CHANGELOG
		/* ONREPL - this is commented until we implement 4.0 style changelog */
        slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,"cn=monitor",LDAP_SCOPE_BASE,"(objectclass=*)",repl_monitor_search,NULL);
#endif
		initialized = 1;
	}

    return return_value;
}

#ifdef FOR_40_STYLE_CHANGELOG
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
#endif

