/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "slapi-plugin.h"
#include "repl.h"

/* XXXggood I think we no longer need this - the mapping tree should do it for us */
int
legacy_preop_search( Slapi_PBlock *pb )
{
	int return_code = 0;
	return return_code;
}


/* XXXggood I think we no longer need this - the mapping tree should do it for us */
int
legacy_pre_entry( Slapi_PBlock *pb )
{
	int return_code = 0;
	return return_code;
}
