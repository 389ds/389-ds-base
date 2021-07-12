/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2019 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* upgrade.c --- upgrade from a previous version of the database */

#include "mdb_layer.h"


/* Do the work to upgrade a database if needed */
/* When we're called, the database files have been opened, and any
recovery needed has been performed. */
int
dbmdb_ldbm_upgrade(ldbm_instance *inst, int action)
{
    int rval = 0;

    if (0 == action) {
        return rval;
    }

    return rval;
}
