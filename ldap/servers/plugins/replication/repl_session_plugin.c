/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl_session_plugin.c */

#include "repl.h"
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
    if((NULL == _ReplSessionAPI) &&
       (slapi_apib_get_interface(REPL_SESSION_v1_0_GUID, &_ReplSessionAPI) ||
       (NULL == _ReplSessionAPI))) {
            LDAPDebug1Arg( LDAP_DEBUG_PLUGIN,
                   "<-- repl_session_plugin_init -- no replication session"
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

    LDAPDebug0Args( LDAP_DEBUG_PLUGIN, "--> repl_session_plugin_call_agmt_init_cb -- begin\n");

    if (_ReplSessionAPI) {
        initfunc = (repl_session_plugin_agmt_init_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_AGMT_INIT_CB];
    }
    if (initfunc) {
        replarea = agmt_get_replarea(ra);
        cookie = (*initfunc)(replarea);
        slapi_sdn_free(&replarea);
    }

    agmt_set_priv(ra, cookie);

    LDAPDebug0Args( LDAP_DEBUG_PLUGIN, "<-- repl_session_plugin_call_agmt_init_cb -- end\n");

    return;
}

int
repl_session_plugin_call_pre_acquire_cb(const Repl_Agmt *ra, int is_total,
                                        char **data_guid, struct berval **data)
{
    int rc = 0;
    Slapi_DN *replarea = NULL;

    repl_session_plugin_pre_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_PRE_ACQUIRE_CB]) ?
        (repl_session_plugin_pre_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_PRE_ACQUIRE_CB] :
        NULL;

    if (thefunc) {
        replarea = agmt_get_replarea(ra);
        rc = (*thefunc)(agmt_get_priv(ra), replarea, is_total,
                data_guid, data);
        slapi_sdn_free(&replarea);
    }

    return rc;
}

int
repl_session_plugin_call_post_acquire_cb(const Repl_Agmt *ra, int is_total,
                                         const char *data_guid, const struct berval *data)
{
    int rc = 0;
    Slapi_DN *replarea = NULL;

    repl_session_plugin_post_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_POST_ACQUIRE_CB]) ?
        (repl_session_plugin_post_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_POST_ACQUIRE_CB] :
        NULL;

    if (thefunc) {
        replarea = agmt_get_replarea(ra);
        rc = (*thefunc)(agmt_get_priv(ra), replarea,
		is_total, data_guid, data);
        slapi_sdn_free(&replarea);
    }

    return rc;
}

int
repl_session_plugin_call_recv_acquire_cb(const char *repl_area, int is_total,
                                         const char *data_guid, const struct berval *data)
{
    int rc = 0;

    repl_session_plugin_recv_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_RECV_ACQUIRE_CB]) ?
        (repl_session_plugin_recv_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_RECV_ACQUIRE_CB] :
        NULL;

    if (thefunc) {
        rc = (*thefunc)(repl_area, is_total, data_guid, data);
    }

    return rc;
}

int
repl_session_plugin_call_reply_acquire_cb(const char *repl_area, int is_total,
                                          char **data_guid, struct berval **data)
{
    int rc = 0;

    repl_session_plugin_reply_acquire_cb thefunc =
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_REPLY_ACQUIRE_CB]) ?
        (repl_session_plugin_reply_acquire_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_REPLY_ACQUIRE_CB] :
        NULL;

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
        (_ReplSessionAPI && _ReplSessionAPI[REPL_SESSION_PLUGIN_DESTROY_AGMT_CB]) ?
        (repl_session_plugin_destroy_agmt_cb)_ReplSessionAPI[REPL_SESSION_PLUGIN_DESTROY_AGMT_CB] :
        NULL;

    if (thefunc) {
        replarea = agmt_get_replarea(ra);
        (*thefunc)(agmt_get_priv(ra), replarea);
        slapi_sdn_free(&replarea);
    }

    return;
}
