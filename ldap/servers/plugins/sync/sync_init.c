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
static int sync_be_postop_init(Slapi_PBlock *pb);
static int sync_betxn_preop_init(Slapi_PBlock *pb);

static PRUintn thread_primary_op;

int
sync_init(Slapi_PBlock *pb)
{
    char *plugin_identity = NULL;
    int rc = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM,
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
        char *plugin_type = "betxnpreoperation";
        /* the config change checking post op */
        if (slapi_register_plugin(plugin_type,
                                  1,                /* Enabled */
                                  "sync_init",      /* this function desc */
                                  sync_betxn_preop_init, /* init func for post op */
                                  SYNC_BETXN_PREOP_DESC, /* plugin desc */
                                  NULL,
                                  plugin_identity)) {
            slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                          "sync_init - Failed to register be_txn_pre_op plugin\n");
            rc = 1;
        }
    }
    if (rc == 0) {
        char *plugin_type = "bepostoperation";
        /* the config change checking post op */
        if (slapi_register_plugin(plugin_type,
                                  1,                /* Enabled */
                                  "sync_init",      /* this function desc */
                                  sync_be_postop_init, /* init func for be_post op */
                                  SYNC_BE_POSTOP_DESC, /* plugin desc */
                                  NULL,
                                  plugin_identity)) {
            slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                          "sync_init - Failed to register be_txn_pre_op plugin\n");
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
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_POST_SEARCH_FN, (void *)sync_srch_refresh_post_search);
    return (rc);
}

static int
sync_be_postop_init(Slapi_PBlock *pb)
{
    int rc;
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_ADD_FN, (void *)sync_add_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_DELETE_FN, (void *)sync_del_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_MODIFY_FN, (void *)sync_mod_persist_post_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_MODRDN_FN, (void *)sync_modrdn_persist_post_op);
    return (rc);
}

static int
sync_betxn_preop_init(Slapi_PBlock *pb)
{
    int rc;
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN, (void *)sync_update_persist_betxn_pre_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN, (void *)sync_update_persist_betxn_pre_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN, (void *)sync_update_persist_betxn_pre_op);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN, (void *)sync_update_persist_betxn_pre_op);
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
    Slapi_Entry *e = NULL;
    PRBool allow_openldap_compat = PR_FALSE;

    slapi_register_supported_control(LDAP_CONTROL_SYNC,
                                     SLAPI_OPERATION_SEARCH);
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM,
                  "--> sync_start\n");

    if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "    sync_start, no config found, that's okay 👍\n");
    }

    if (e) {
        /* Do we allow openldap sync? */
        Slapi_Attr *chattr = NULL;
        if (slapi_entry_attr_find(e, SYNC_ALLOW_OPENLDAP_COMPAT, &chattr) == 0) {
            Slapi_Value *sval = NULL;
            slapi_attr_first_value(chattr, &sval);

            const struct berval *value = slapi_value_get_berval(sval);
            if (NULL != value && NULL != value->bv_val && '\0' != value->bv_val[0]) {
                if (strcasecmp(value->bv_val, "on") == 0) {
                    allow_openldap_compat = PR_TRUE;
                }
            }
        }
    }

    sync_register_allow_openldap_compat(allow_openldap_compat);

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc) != 0 ||
        slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_start - Unable to get arguments\n");
        return (-1);
    }
    /* It registers a per thread 'thread_primary_op' variable that is
     * a list of pending operations. For simple operation, this list
     * only contains one operation. For nested, the list contains the operations
     * in the order that they were applied
     */
    PR_NewThreadPrivateIndex(&thread_primary_op, NULL);
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

/* Return the head of the operations list
 * the head is the primary operation.
 * The list is private to that thread and contains
 * all nested operations applied by the thread.
 */
OPERATION_PL_CTX_T *
get_thread_primary_op(void)
{
    OPERATION_PL_CTX_T *head = NULL;
    head = (OPERATION_PL_CTX_T *)PR_GetThreadPrivate(thread_primary_op);
    if (head == NULL) {
        /* if it was not initialized set it to zero */
        head = (OPERATION_PL_CTX_T *) slapi_ch_calloc(1, sizeof(OPERATION_PL_CTX_T));
        head->flags = OPERATION_PL_HEAD;
        PR_SetThreadPrivate(thread_primary_op, head);
    }

    return head->next;
}

/* It is set with a non NULL op when this is a primary operation
 * else it set to NULL when the all pending list has be flushed.
 * The list is flushed when no more operations (in that list) are
 * pending (OPERATION_PL_PENDING).
 */
void
set_thread_primary_op(OPERATION_PL_CTX_T *op)
{
    OPERATION_PL_CTX_T *head;
    head = (OPERATION_PL_CTX_T *) PR_GetThreadPrivate(thread_primary_op);
    if (head == NULL) {
        /* if it was not initialized set it to zero */
        head = (OPERATION_PL_CTX_T *) slapi_ch_calloc(1, sizeof(OPERATION_PL_CTX_T));
        head->flags = OPERATION_PL_HEAD;
        PR_SetThreadPrivate(thread_primary_op, (void *) head);
    }
    head->next = op;
}
