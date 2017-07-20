/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/* repl_session_plugin.c */

#include "repl5.h"
#include "slap.h"
#include "slapi-plugin.h"
#include "repl-session-plugin.h"

/* an array of function pointers */
static void **_ReplSessionAPI = NULL;

void
repl_session_plugin_init()
{
    /* If the function pointer array is null, get the functions.
     * We will only grab the api once. */
    if ((NULL == _ReplSessionAPI) &&
        (slapi_apib_get_interface(REPL_SESSION_v1_0_GUID, &_ReplSessionAPI) ||
         (NULL == _ReplSessionAPI))) {
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                      "repl_session_plugin_init - No replication session"
                      " plugin API registered for GUID [%s] -- end\n",
                      REPL_SESSION_v1_0_GUID);
    }

    return;
}

void
repl_session_plugin_call_agmt_init_cb(Repl_Agmt *ra)
{
    void *cookie = NULL;
    Slapi_DN *replarea = NULL;
    repl_session_plugin_agmt_init_cb initfunc = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                  "repl_session_plugin_call_agmt_init_cb - Begin\n");

    if (_ReplSessionAPI) {
        initfunc = (repl_session_plugin_agmt_init_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_AGMT_INIT_CB];
    }
    if (initfunc) {
        replarea = agmt_get_replarea(ra);
        if (!replarea) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "repl_session_plugin_call_agmt_init_cb- Aborted - No replication area\n");
            return;
        }
        cookie = (*initfunc)(replarea);
        slapi_sdn_free(&replarea);
    }

    agmt_set_priv(ra, cookie);

    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name, "repl_session_plugin_call_agmt_init_cb - End\n");

    return;
}

int
repl_session_plugin_call_pre_acquire_cb(const Repl_Agmt *ra, int is_total, char **data_guid, struct berval **data)
{
    int rc = 0;
    Slapi_DN *replarea = NULL;

    repl_session_plugin_pre_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_PRE_ACQUIRE_CB]) ? (repl_session_plugin_pre_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_PRE_ACQUIRE_CB] : NULL;

    if (thefunc) {
        replarea = agmt_get_replarea(ra);
        if (!replarea) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "repl_session_plugin_call_pre_acquire_cb "
                          "- Aborted - No replication area\n");
            return 1;
        }
        rc = (*thefunc)(agmt_get_priv(ra), replarea, is_total, data_guid, data);
        slapi_sdn_free(&replarea);
    }

    return rc;
}

int
repl_session_plugin_call_post_acquire_cb(const Repl_Agmt *ra, int is_total, const char *data_guid, const struct berval *data)
{
    int rc = 0;
    Slapi_DN *replarea = NULL;

    repl_session_plugin_post_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_POST_ACQUIRE_CB]) ? (repl_session_plugin_post_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_POST_ACQUIRE_CB] : NULL;

    if (thefunc) {
        replarea = agmt_get_replarea(ra);
        if (!replarea) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "repl_session_plugin_call_post_acquire_cb - Aborted - No replication area\n");
            return 1;
        }
        rc = (*thefunc)(agmt_get_priv(ra), replarea, is_total, data_guid, data);
        slapi_sdn_free(&replarea);
    }

    return rc;
}

int
repl_session_plugin_call_recv_acquire_cb(const char *repl_area, int is_total, const char *data_guid, const struct berval *data)
{
    int rc = 0;

    repl_session_plugin_recv_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_RECV_ACQUIRE_CB]) ? (repl_session_plugin_recv_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_RECV_ACQUIRE_CB] : NULL;

    if (thefunc) {
        rc = (*thefunc)(repl_area, is_total, data_guid, data);
    }

    return rc;
}

int
repl_session_plugin_call_reply_acquire_cb(const char *repl_area, int is_total, char **data_guid, struct berval **data)
{
    int rc = 0;

    repl_session_plugin_reply_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_REPLY_ACQUIRE_CB]) ? (repl_session_plugin_reply_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_REPLY_ACQUIRE_CB] : NULL;

    if (thefunc) {
        rc = (*thefunc)(repl_area, is_total, data_guid, data);
    }

    return rc;
}

void
repl_session_plugin_call_destroy_agmt_cb(const Repl_Agmt *ra)
{
    Slapi_DN *replarea = NULL;

    repl_session_plugin_destroy_agmt_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_DESTROY_AGMT_CB]) ? (repl_session_plugin_destroy_agmt_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_DESTROY_AGMT_CB] : NULL;

    if (thefunc) {
        replarea = agmt_get_replarea(ra);
        if (!replarea) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "repl_session_plugin_call_destroy_agmt_cb - Aborted - No replication area\n");
            return;
        }
        (*thefunc)(agmt_get_priv(ra), replarea);
        slapi_sdn_free(&replarea);
    }

    return;
}
