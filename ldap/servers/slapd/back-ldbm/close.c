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

/* close.c - close ldbm backend */

#include "back-ldbm.h"

int
ldbm_back_close(Slapi_PBlock *pb)
{
    struct ldbminfo *li;

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_close", "ldbm backend syncing\n");
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);

    /* Kill off any sleeping threads by setting this flag */
    PR_Lock(li->li_shutdown_mutex);
    li->li_shutdown = 1;
    PR_Unlock(li->li_shutdown_mutex);

    /* close down all the ldbm instances */
    dblayer_close(li, DBLAYER_NORMAL_MODE);

    /* Close all the entry caches for this instance */
    ldbm_instance_stopall_caches(li);

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_close", "ldbm backend done syncing\n");
    return 0;
}

void
ldbm_back_instance_set_destructor(void **arg __attribute__((unused)))
{
    /*
    Objset *instance_set = (Objset *) *arg;
    */

    /* This function is called when the instance set is destroyed.
     * I can't really think of anything we should do here, but that
     * may change in the future. */
    slapi_log_err(SLAPI_LOG_INFO, "ldbm_back_instance_set_destructor", "Set of instances destroyed\n");
}
