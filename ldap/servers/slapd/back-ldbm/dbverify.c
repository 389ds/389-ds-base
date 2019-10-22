/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* dbverify.c - verify database files */

#include "back-ldbm.h"
#include "dblayer.h"

int
ldbm_back_dbverify(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    dblayer_setup(li);
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;

    return priv->dblayer_verify_fn(pb);;
}
