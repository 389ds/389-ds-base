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

/*
 * dbsize.c - ldbm backend routine which returns the size (in bytes)
 * that the database occupies on disk.
 */

#include "back-ldbm.h"
#include "dblayer.h"

/* TODO: make this a 64-bit return value */
int
ldbm_db_size(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    dblayer_private *priv;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    priv = (dblayer_private *)li->li_dblayer_private;
    return priv->dblayer_db_size_fn(pb);

}
