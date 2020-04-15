/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "sync.h"

static Slapi_PluginDesc pdesc = {PLUGIN_NAME, VENDOR, DS_PACKAGE_VERSION, "Context Synchronization (RFC4533) plugin"};

static int sync_start(Slapi_PBlock *pb);
static int sync_close(Slapi_PBlock *pb);
static int sync_preop_init(Slapi_PBlock *pb);
static int sync_postop_init(Slapi_PBlock *pb);
static int sync_internal_postop_init(Slapi_PBlock *pb);

int
sync_init(Slapi_PBlock *pb)
{
    char *plugin_identity = NULL;
    int rc = 0;

    slapi_log_err(SLAPI_LOG_TRACE, SYNC_PLUGIN_SUBSYSTEM,
                  "--> sync_init\n");

    /**
     * Store the plugin identity for later use.
     * Used for internal operations
     */

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)sync_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)sync_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_init - Failed to register plugin\n");
        rc = 1;
    }

    if (rc == 0) {
        char *plugin_type = "preoperation";
        /* the config change checking post op */
        if (slapi_register_plugin(
                plugin_type,
                1,               /* Enabled */
                "sync_init",     /* this function desc */
                sync_preop_init, /* init func for post op */
                SYNC_PREOP_DESC, /* plugin desc */
                NULL,
                plugin_identity)) {
            slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                          "sync_init - Failed to register preop plugin\n");
            rc = 1;
        }
    }

    if (rc == 0) {
        char *plugin_type = "postoperation";
        /* the config change checking post op */
        if (slapi_register_plugin(plugin_type,
                                  1,                /* Enabled */
                                  "sync_init",      /* this function desc */
                                  sync_postop_init, /* init func for post op */
                                  SYNC_POSTOP_DESC, /* plugin desc */
                                  NULL,
                                  plugin_identity)) {
            slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                          "sync_init - Failed to register postop plugin\n");
            rc = 1;
        }
    }

    if (rc == 0) {
        char *plugin_type = "internalpostoperation";
        /* the config change checking post op */
        if (slapi_register_plugin(plugin_type,
                                  1,                /* Enabled */
                                  "sync_init",      /* this function desc */
                                  sync_internal_postop_init, /* init func for post op */
                                  SYNC_INT_POSTOP_DESC, /* plugin desc */
                                  NULL,
                                  plugin_identity)) {
            slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                          "sync_init - Failed to register internal postop plugin\n");
            rc = 1;
        }
    }

    return (rc);
}

static int
sync_preop_init(Slapi_PBlock *pb)
{
    int rc;
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_SEARCH_FN, (void *)sync_srch_refresh_pre_search);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ENTRY_FN, (void *)sync_srch_refresh_pre_entry);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_RESULT_FN, (void *)sync_srch_refresh_pre_result);
    rc |= sync_register_operation_extension();

    return (rc);
}

static int
sync_postop_init(Slapi_PBlock *pb)
{
    int rc;
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN, (void *)sync_add_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN, (void *)sync_del_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *)sync_mod_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN, (void *)sync_modrdn_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_SEARCH_FN, (void *)sync_srch_refresh_post_search);
    return (rc);
}

static int
sync_internal_postop_init(Slapi_PBlock *pb)
{
    int rc;
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN, (void *)sync_add_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN, (void *)sync_del_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN, (void *)sync_mod_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN, (void *)sync_modrdn_persist_post_op);
    return (rc);
}

/*
    sync_start
    --------------
    Register the Content Synchronization Control.
    Initialize locks and queues for the persitent phase.
*/
static int
sync_start(Slapi_PBlock *pb)
{
    int argc;
    char **argv;

    slapi_register_supported_control(LDAP_CONTROL_SYNC,
                                     SLAPI_OPERATION_SEARCH);
    slapi_log_err(SLAPI_LOG_TRACE, SYNC_PLUGIN_SUBSYSTEM,
                  "--> sync_start\n");

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc) != 0 ||
        slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_start - Unable to get arguments\n");
        return (-1);
    }
    sync_persist_initialize(argc, argv);

    return (0);
}

/*
    sync_close
    --------------
    Free locks and queues allocated.
*/
static int
sync_close(Slapi_PBlock *pb __attribute__((unused)))
{
    sync_persist_terminate_all();
    sync_unregister_operation_entension();

    return (0);
}
