/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
        /* ONREPL - how do we publish changenumbers now with multiple changelogs?
		sprintf( buf, "%s:%lu %s% lu", get_localhost_DNS(), port, sdv, ldapi_get_last_changenumber());
        */
    	val.bv_val = buf;
    	val.bv_len = strlen( buf );
    	slapi_entry_attr_replace( e, attr_dataversion, vals );
    }
	return SLAPI_DSE_CALLBACK_OK;
}

