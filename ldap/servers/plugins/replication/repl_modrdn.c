/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "slapi-plugin.h"
#include "repl.h"

/* The modrdn plugin points for the legacy replication plugin */

int
legacy_preop_modrdn( Slapi_PBlock *pb )
{
	return legacy_preop(pb, "legacy_preop_modrdn", OP_MODDN);
}

int
legacy_bepreop_modrdn( Slapi_PBlock *pb )
{
    return 0; /* OK */
}

int
legacy_postop_modrdn( Slapi_PBlock *pb )
{
	return legacy_postop(pb, "legacy_postop_modrdn", OP_MODDN);
}
