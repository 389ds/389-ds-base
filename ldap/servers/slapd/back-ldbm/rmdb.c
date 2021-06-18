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
 * rmdb.c - ldbm backend routine which deletes an entire database.
 * This routine is not exposed in the public SLAPI interface.  It
 * is called by the replication subsystem when then changelog must
 * be erased.
 */

#include "back-ldbm.h"
#include "dblayer.h"

int
ldbm_back_rmdb(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    dblayer_private *priv;
    /* char            *directory = NULL;*/
    int return_value = -1;
    Slapi_Backend *be;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    if (be->be_state != BE_STATE_STOPPED) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "ldbm_back_rmdb", "Warning - backend is in a wrong state - %d\n",
                      be->be_state);
        return 0;
    }

    PR_Lock(be->be_state_lock);

    if (be->be_state != BE_STATE_STOPPED) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "ldbm_back_rmdb", "Warning - backend is in a wrong state 2 - %d\n",
                      be->be_state);
        PR_Unlock(be->be_state_lock);
        return 0;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    priv = (dblayer_private *)li->li_dblayer_private;
    /*    slapi_pblock_get( pb, SLAPI_SEQ_VAL, &directory );*/
    return_value = priv->dblayer_delete_db_fn(li);

    if (return_value == 0)
        be->be_state = BE_STATE_DELETED;

    PR_Unlock(be->be_state_lock);

    return return_value;
}
