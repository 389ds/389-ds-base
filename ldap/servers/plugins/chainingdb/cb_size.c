/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

int
cb_db_size( Slapi_PBlock *pb )
{

	/*
	** Return the size in byte of the local database storage
	** Size is 0 for a chaining backend
	*/

	unsigned int size=0;

        slapi_pblock_set( pb, SLAPI_DBSIZE, &size );
        return 0;
}

