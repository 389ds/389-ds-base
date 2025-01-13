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

/* cleanup.c - cleans up ldbm backend */

#include "back-ldbm.h"
#include "dblayer.h"
#include "vlv_srch.h"

int
ldbm_back_cleanup(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    Slapi_Backend *be;
    struct vlvSearch *nextp;

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_cleanup", "ldbm backend cleaning up\n");
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    if (be->be_state != BE_STATE_STOPPED &&
        be->be_state != BE_STATE_DELETED) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "ldbm_back_cleanup", "Warning - backend is in a wrong state - %d\n",
                      be->be_state);
        return 0;
    }

    PR_Lock(be->be_state_lock);

    if (be->be_state != BE_STATE_STOPPED &&
        be->be_state != BE_STATE_DELETED) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "ldbm_back_cleanup", "Warning - backend is in a wrong state - %d\n",
                      be->be_state);
        PR_Unlock(be->be_state_lock);
        return 0;
    }

    /* Release the vlv list */
    for (struct vlvSearch *p=be->vlvSearchList; p; p=nextp) {
        nextp = p->vlv_next;
        vlvSearch_delete(&p);
    }

    /*
     * We check if li is NULL. Because of an issue in how we create backends
     * we share the li and plugin info between many unique backends. This causes
     * be_cleanall to try to trigger this multiple times. But we don't need to!
     * the backend cleanup is sufficent to be called once for each instance of
     * ldbminfo. This protects us from heap use after frees while still cleaning
     * up. Ultimately, it's a flaw in how ldbm can have many backends, but for
     * "one" plugin.
     */
    if (li != NULL) {

        /* call the backend specific cleanup function */
        dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
        if (priv) {
            priv->dblayer_cleanup_fn(li);
        }

        ldbm_config_destroy(li);

        slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, NULL);
    }

    be->be_state = BE_STATE_CLEANED;

    PR_Unlock(be->be_state_lock);

    return 0;
}
