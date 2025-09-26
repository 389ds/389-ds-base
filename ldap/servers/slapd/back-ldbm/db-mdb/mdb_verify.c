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
    slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_verify", "With lmdb, db_verify feature is meaningless and is always successfull.\n");
    return 0;  /* Fonction useless with lmdb - as we can verify the db when doing a backup */
}
