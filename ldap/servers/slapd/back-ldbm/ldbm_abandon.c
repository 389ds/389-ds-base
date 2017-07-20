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

/* abandon.c - ldbm backend abandon routine */

#include "back-ldbm.h"

int
ldbm_back_abandon(Slapi_PBlock *pb __attribute__((unused)))
{
    /* DBDB need to implement this */
    return 0;
}
