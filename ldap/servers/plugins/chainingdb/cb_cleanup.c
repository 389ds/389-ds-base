/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

