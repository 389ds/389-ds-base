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

#include "cb.h"

Slapi_PluginDesc chainingdbdesc = {CB_PLUGIN_NAME,
                                   VENDOR,
                                   DS_PACKAGE_VERSION,
                                   CB_PLUGIN_DESCRIPTION};


static cb_backend *cb_backend_type = NULL;

cb_backend *
cb_get_backend_type()
{
    return cb_backend_type;
}

static void
cb_set_backend_type(cb_backend *cb)
{
    cb_backend_type = cb;
}

/* Initialization function */
int
chaining_back_init(Slapi_PBlock *pb)
{

    int rc = 0;
    cb_backend *cb;
    struct slapdplugin *p;

    cb = (cb_backend *)slapi_ch_calloc(1, sizeof(cb_backend));

    /*  Record the identity of the chaining plugin. used during internal ops.*/
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &(cb->identity));

    /* keep a pointer back to the plugin */
    slapi_pblock_get(pb, SLAPI_PLUGIN, &p);
    cb->plugin = p;

    /* Initialize misc. fields */
    cb->config.rwl_config_lock = slapi_new_rwlock();
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, (void *)cb);

    /* These DNs are already normalized */
    cb->pluginDN = slapi_ch_smprintf("cn=%s,%s", CB_PLUGIN_NAME, PLUGIN_BASE_DN);

    cb->configDN = slapi_ch_smprintf("cn=config,%s", cb->pluginDN);

    /* Set backend callback functions */
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&chainingdbdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SEARCH_FN,
                           (void *)chainingdb_build_candidate_list);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN,
                           (void *)chainingdb_next_search_entry);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN,
                           (void *)chainingdb_prev_search_results);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN,
                           (void *)chaining_back_search_results_release);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                           (void *)chainingdb_start);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_BIND_FN,
                           (void *)chainingdb_bind);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ADD_FN,
                           (void *)chaining_back_add);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_DELETE_FN,
                           (void *)chaining_back_delete);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_COMPARE_FN,
                           (void *)chaining_back_compare);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_MODIFY_FN,
                           (void *)chaining_back_modify);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_MODRDN_FN,
                           (void *)chaining_back_modrdn);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ABANDON_FN,
                           (void *)chaining_back_abandon);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                           (void *)cb_back_close);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_CLEANUP_FN,
                           (void *)cb_back_cleanup);

    /****
        rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_TEST_FN,
            (void *) cb_back_test );
     ****/

    /*
    ** The following callbacks are not implemented
    ** by the chaining backend
    **     - SLAPI_PLUGIN_DB_SEQ_FN
    **     - SLAPI_PLUGIN_DB_RMDB_FN
    **     - SLAPI_PLUGIN_DB_DB2INDEX_FN
    **     - SLAPI_PLUGIN_DB_LDIF2DB_FN
    **     - SLAPI_PLUGIN_DB_DB2LDIF_FN
    **     - SLAPI_PLUGIN_DB_ARCHIVE2DB_FN
    **     - SLAPI_PLUGIN_DB_DB2ARCHIVE_FN
    **     - SLAPI_PLUGIN_DB_BEGIN_FN
    **     - SLAPI_PLUGIN_DB_COMMIT_FN
    **     - SLAPI_PLUGIN_DB_ABORT_FN
    **     - SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN
    */

    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM, "chaining_back_init - Failed\n");
        return (-1);
    }

    cb_set_backend_type(cb);

    return (0);
}
