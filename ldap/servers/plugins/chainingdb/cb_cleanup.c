/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h" 

/*
** cLeanup a chaining backend instance
*/ 
 
int cb_back_cleanup( Slapi_PBlock *pb )
{

	/* 
	** Connections have been closed in cb_back_close()
	** For now, don't do more
	*/

        return 0;
}

