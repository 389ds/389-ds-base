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

/*
** Close a chaining backend instance
** Should be followed by a cleanup
*/


static void
free_cb_backend(cb_backend *cb)
{
    if (cb) {
        slapi_destroy_rwlock(cb->config.rwl_config_lock);
        slapi_ch_free_string(&cb->pluginDN);
        slapi_ch_free_string(&cb->configDN);
        slapi_ch_array_free(cb->config.chainable_components);
        slapi_ch_array_free(cb->config.chaining_components);
        slapi_ch_array_free(cb->config.forward_ctrls);
        slapi_ch_free((void **)&cb);
    }
}

int
cb_back_close(Slapi_PBlock *pb)
{
    Slapi_Backend *be;
    cb_backend_instance *inst;
    cb_backend *cb = cb_get_backend_type();
    char *cookie;
    int rc;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {

        CB_ASSERT(cb != NULL);

        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, cb->configDN, LDAP_SCOPE_BASE,
                                     "(objectclass=*)", cb_config_modify_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, cb->configDN, LDAP_SCOPE_BASE,
                                     "(objectclass=*)", cb_config_modify_check_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, cb->configDN, LDAP_SCOPE_BASE,
                                     "(objectclass=*)", cb_config_add_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, cb->configDN, LDAP_SCOPE_BASE,
                                     "(objectclass=*)", cb_config_add_check_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, cb->configDN, LDAP_SCOPE_BASE,
                                     "(objectclass=*)", cb_config_search_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, cb->pluginDN,
                                     LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER,
                                     cb_config_add_instance_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, cb->pluginDN,
                                     LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER,
                                     cb_config_add_instance_check_callback);

        be = slapi_get_first_backend(&cookie);
        while (be) {
            const char *betype = slapi_be_gettype(be);
            if (strcasecmp(betype, CB_CHAINING_BACKEND_TYPE) == 0) {
                inst = cb_get_instance(be);
                cb_instance_free(inst);
            }
            be = slapi_get_next_backend(cookie);
        }
        slapi_ch_free((void **)&cookie);
        free_cb_backend(cb);
        return 0;
    }

    /* XXXSD: temp fix . Sometimes, this functions */
    /* gets called with a ldbm backend instance... */

    {
        const char *betype = slapi_be_gettype(be);
        if (!betype || strcasecmp(betype, CB_CHAINING_BACKEND_TYPE)) {
            slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM, "cb_back_close - Wrong database type.\n");
            free_cb_backend(cb);
            return 0;
        }
    }

    inst = cb_get_instance(be);
    CB_ASSERT(inst != NULL);

    slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM, "cb_back_close - Stopping chaining database instance %s\n",
                  inst->configDn);
    /*
     * emulate a backend instance deletion to clean up everything
     */
    cb_instance_delete_config_callback(NULL, NULL, NULL, &rc, NULL, inst);
    free_cb_backend(cb);

    return 0;
}
