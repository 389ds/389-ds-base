/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include <string.h>

#include "repl.h"
#include "cl4.h"
#include "slapi-plugin.h"
 
/* Forward Declartions */
static int repl_rootdse_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

int
repl_rootdse_init()
{
    /* The FE DSE *must* be initialised before we get here */
    int return_value= LDAP_SUCCESS;
	
	slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,"",LDAP_SCOPE_BASE,"(objectclass=*)",repl_rootdse_search,NULL); 

    return return_value;
}

static int
repl_rootdse_search(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{

#if 0
	struct berval val;
	struct berval *vals[2];
	vals[0] = &val;
	vals[1] = NULL;
 
	/* machine data suffix */
	val.bv_val = REPL_CONFIG_TOP;
	val.bv_len = strlen( val.bv_val );
	slapi_entry_attr_replace( e, ATTR_NETSCAPEMDSUFFIX, vals );

	/* Changelog information */
/* ONREPL because we now support multiple 4.0 changelogs we no longer publish
   info in the rootdse */
	if ( get_repl_backend() != NULL )
	{
		char buf[BUFSIZ];
	    changeNumber cnum;

	    /* Changelog suffix */
	    val.bv_val = changelog4_get_suffix ();
	    if ( val.bv_val != NULL )
		{
    		val.bv_len = strlen( val.bv_val );
    		slapi_entry_attr_replace( e, "changelog", vals );
	    }
		slapi_ch_free ((void **)&val.bv_val);

	    /* First change number contained in log */
	    cnum = ldapi_get_first_changenumber();
	    sprintf( buf, "%lu", cnum );
	    val.bv_val = buf;
	    val.bv_len = strlen( val.bv_val );
	    slapi_entry_attr_replace( e, "firstchangenumber", vals );

	    /* Last change number contained in log */
	    cnum = ldapi_get_last_changenumber();
	    sprintf( buf, "%lu", cnum );
	    val.bv_val = buf;
	    val.bv_len = strlen( val.bv_val );
	    slapi_entry_attr_replace( e, "lastchangenumber", vals );
	}
#endif

	return SLAPI_DSE_CALLBACK_OK;
}



