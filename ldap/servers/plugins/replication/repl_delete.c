/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "slapi-plugin.h"
#include "repl.h"

int
legacy_preop_delete( Slapi_PBlock *pb )
{
	return legacy_preop(pb, "legacy_preop_delete", OP_DELETE);
}

int
legacy_bepreop_delete( Slapi_PBlock *pb )
{
	return 0; /* OK */
}

int
legacy_postop_delete( Slapi_PBlock *pb )
{
	return legacy_postop(pb, "legacy_preop_delete", OP_DELETE);
}
