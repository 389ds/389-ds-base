/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/******** testdbinterop.h *******************

 The header file is for access to a Berkeley DB
 that is being created by the testdbinterop.c
 and used by testdatainterop.c ( plugin ); to allow
 creation of a DB and adding DN's to the DB.
 A simple example to show how external databases can
 be accessed through the datainterop plugin of
 testdatainterop.c

**********************************************/

#include "nspr.h"

void db_put_dn(char *data);
