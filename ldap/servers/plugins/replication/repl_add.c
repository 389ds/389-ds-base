/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "slapi-plugin.h"
#include "repl.h"


/* Add Operation Plugin Functions for legacy replication plugin */

int
legacy_preop_add( Slapi_PBlock *pb )
{
    return legacy_preop( pb, "legacy_preop_add", OP_ADD );
}

int
legacy_bepreop_add( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	return rc;
}

int
legacy_postop_add( Slapi_PBlock *pb )
{
    return legacy_postop( pb, "legacy_postop_add", OP_ADD );
}
