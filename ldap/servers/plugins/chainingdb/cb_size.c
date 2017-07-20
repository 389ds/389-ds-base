/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cb.h"

int
cb_db_size(Slapi_PBlock *pb)
{

    /*
    ** Return the size in byte of the local database storage
    ** Size is 0 for a chaining backend
    */

    unsigned int size = 0;

    slapi_pblock_set(pb, SLAPI_DBSIZE, &size);
    return 0;
}
