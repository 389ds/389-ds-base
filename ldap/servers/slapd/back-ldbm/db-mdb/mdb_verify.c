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

/* dbverify.c - verify database files */

#include "mdb_layer.h"

int
dbmdb_verify(Slapi_PBlock *pb)
{
    return 0;  /* Fonction useless with lmdb - as we can verify the db when doing a backup */
}
