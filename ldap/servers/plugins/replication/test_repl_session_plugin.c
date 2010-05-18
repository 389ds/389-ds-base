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

#include "slapi-plugin.h"
#include "repl-session-plugin.h"
#include <string.h>

#define REPL_SESSION_v1_0_GUID "210D7559-566B-41C6-9B03-5523BDF30880"

static char *test_repl_session_plugin_name = "test_repl_session_api";

/*
 * Plugin identifiers
 */
static Slapi_PluginDesc test_repl_session_pdesc = {
    "test-repl-session-plugin",
    "Test Vendor",
    "1.0",
    "test replication session plugin"
};

static Slapi_ComponentId *test_repl_session_plugin_id = NULL;


/*
 * Replication Session Callbacks
 */
/*
 * This is called on a master when a replication agreement is
 * initialized at startup.  A cookie can be allocated at this
 * time which is passed to other callbacks on the master side.
 */
static void *
test_repl_session_plugin_agmt_init_cb(const Slapi_DN *repl_subtree)
{
    char *cookie = NULL;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_init_cb() called for suffix \"%s\".\n",
        slapi_sdn_get_ndn(repl_subtree));

    /* allocate a string and set as the cookie */
    cookie = slapi_ch_smprintf("cookie test");

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_init_cb(): Setting cookie: \"%s\".\n",
        cookie);

    return cookie;
}

/*
 * This is called on a master when we are about to acquire a
 * replica.  This callback can allocate some extra data to
 * be sent to the replica in the start replication request.
 * This memory will be free'd by the replication plug-in
 * after it is sent.  A guid string must be set that is to
 * be used by the receiving side to ensure that the data is
 * from the same replication session plug-in.
 *
 * Returning non-0 will abort the replication session.  This
 * results in the master going into incremental backoff mode.
 */
static int
test_repl_session_plugin_pre_acquire_cb(void *cookie, const Slapi_DN *repl_subtree,
                                        int is_total, char **data_guid, struct berval **data)
{
    int rc = 0;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_pre_acquire_cb() called for suffix \"%s\", "
        "is_total: \"%s\", cookie: \"%s\".\n", slapi_sdn_get_ndn(repl_subtree),
        is_total ? "TRUE" : "FALSE", cookie ? (char *)cookie : "NULL");

    /* allocate some data to be sent to the replica */
    *data_guid = slapi_ch_smprintf("test-guid");
    *data = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
    (*data)->bv_val = slapi_ch_smprintf("test-data");
    (*data)->bv_len = strlen((*data)->bv_val) + 1;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_pre_acquire_cb() sending data: guid: \"%s\" data: \"%s\".\n",
	*data_guid, (*data)->bv_val);

    return rc;
}

/*
 * This is called on a replica when we are about to reply to
 * a start replication request from a master.  This callback
 * can allocate some extra data to be sent to the master in
 * the start replication response.  This memory will be free'd
 * by the replication plug-in after it is sent.  A guid string
 * must be set that is to be used by the receiving side to ensure
 * that the data is from the same replication session plug-in.
 *
 * Returning non-0 will abort the replication session.  This
 * results in the master going into incremental backoff mode.
 */
static int
test_repl_session_plugin_reply_acquire_cb(const char *repl_subtree, int is_total,
                                          char **data_guid, struct berval **data)
{
    int rc = 0;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_reply_acquire_cb() called for suffix \"%s\", is_total: \"%s\".\n",
        repl_subtree, is_total ? "TRUE" : "FALSE");

    /* allocate some data to be sent to the master */
    *data_guid = slapi_ch_smprintf("test-reply-guid");
    *data = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
    (*data)->bv_val = slapi_ch_smprintf("test-reply-data");
    (*data)->bv_len = strlen((*data)->bv_val) + 1;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_reply_acquire_cb() sending data: guid: \"%s\" data: \"%s\".\n",
        *data_guid, (*data)->bv_val);

    return rc;
}

/*
 * This is called on a master when it receives a reply to a
 * start replication extop that we sent to a replica.  Any
 * extra data sent by a replication session callback on the
 * replica will be set here as the data parameter.  The data_guid
 * should be checked first to ensure that the sending side is
 * using the same replication session plug-in before making any
 * assumptions about the contents of the data parameter.  You
 * should not free data_guid or data.  The replication plug-in
 * will take care of freeing this memory.
 *
 * Returning non-0 will abort the replication session.  This
 * results in the master going into incremental backoff mode.
 */
static int
test_repl_session_plugin_post_acquire_cb(void *cookie, const Slapi_DN *repl_subtree, int is_total,
                                         const char *data_guid, const struct berval *data)
{
    int rc = 0;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_post_acquire_cb() called for suffix \"%s\", "
        "is_total: \"%s\" cookie: \"%s\".\n", slapi_sdn_get_ndn(repl_subtree),
        is_total ? "TRUE" : "FALSE", cookie ? (char *)cookie : "NULL");

    /* log any extra data that was sent from the replica */
    if (data_guid && data) {
        slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
            "test_repl_session_plugin_post_acquire_cb() received data: guid: \"%s\" data: \"%s\".\n",
            data_guid, data->bv_val);
    }

    return rc;
}

/*
 * This is called on a replica when it receives a start replication
 * extended operation from a master.  If the replication session
 * plug-in on the master sent any extra data, it will be set here
 * as the data parameter.  The data_guid should be checked first to
 * ensure that the sending side is using the same replication session
 * plug-in before making any assumptions about the contents of the
 * data parameter.  You should not free data_guid or data.  The
 * replication plug-in will take care of freeing this memory.
 *
 * Returning non-0 will abort the replication session.  This
 * results in the master going into incremental backoff mode.
 */
static int
test_repl_session_plugin_recv_acquire_cb(const char *repl_subtree, int is_total,
                                         const char *data_guid, const struct berval *data)
{
    int rc = 0;

    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_recv_acquire_cb() called for suffix \"%s\", is_total: \"%s\".\n",
        repl_subtree, is_total ? "TRUE" : "FALSE");

    /* log any extra data that was sent from the master */
    if (data_guid && data) {
        slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
            "test_repl_session_plugin_recv_acquire_cb() received data: guid: \"%s\" data: \"%s\".\n",
            data_guid, data->bv_val);
    }

    return rc;
}

/*
 * This is called on a master when a replication agreement is
 * destroyed.  Any cookie allocated when the agreement was initialized
 * should be free'd here.
 */
static void
test_repl_session_plugin_destroy_cb(void *cookie, const Slapi_DN *repl_subtree)
{
    slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
        "test_repl_session_plugin_destroy_cb() called for suffix \"%s\".\n",
        slapi_sdn_get_ndn(repl_subtree));

    /* free cookie */
    slapi_ch_free_string((char **)&cookie);

    return;
}

/*
 * Callback list for registering API
 */
static void *test_repl_session_api[] = {
    NULL, /* reserved for api broker use, must be zero */
    test_repl_session_plugin_agmt_init_cb,
    test_repl_session_plugin_pre_acquire_cb,
    test_repl_session_plugin_reply_acquire_cb,
    test_repl_session_plugin_post_acquire_cb,
    test_repl_session_plugin_recv_acquire_cb,
    test_repl_session_plugin_destroy_cb
};

/*
 * Plug-in framework functions
 */
static int
test_repl_session_plugin_start(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_repl_session_plugin_name,
                    "--> test_repl_session_plugin_start -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_repl_session_plugin_name,
                    "<-- test_repl_session_plugin_start -- end\n");
    return 0;
}

static int
test_repl_session_plugin_close(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_repl_session_plugin_name,
                    "--> test_repl_session_plugin_close -- begin\n");

    slapi_apib_unregister(REPL_SESSION_v1_0_GUID);

    slapi_log_error(SLAPI_LOG_PLUGIN, test_repl_session_plugin_name,
                    "<-- test_repl_session_plugin_close -- end\n");
    return 0;
}

int test_repl_session_plugin_init(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_repl_session_plugin_name,
                    "--> test_repl_session_plugin_init -- begin\n");

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
                           SLAPI_PLUGIN_VERSION_01 ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                          (void *) test_repl_session_plugin_start ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                          (void *) test_repl_session_plugin_close ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&test_repl_session_pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, test_repl_session_plugin_name,
                         "<-- test_repl_session_plugin_init -- failed to register plugin -- end\n");
        return -1;
    }

    if( slapi_apib_register(REPL_SESSION_v1_0_GUID, test_repl_session_api) ) {
        slapi_log_error( SLAPI_LOG_FATAL, test_repl_session_plugin_name,
                         "<-- test_repl_session_plugin_start -- failed to register repl_session api -- end\n");
        return -1;
    }


    /* Retrieve and save the plugin identity to later pass to
       internal operations */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &test_repl_session_plugin_id) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, test_repl_session_plugin_name,
                         "<-- test_repl_session_plugin_init -- failed to retrieve plugin identity -- end\n");
        return -1;
    }

    slapi_log_error( SLAPI_LOG_PLUGIN, test_repl_session_plugin_name,
                     "<-- test_repl_session_plugin_init -- end\n");
    return 0;
}

/*
dn: cn=Test Replication Session API,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: Test Replication Session API
nsslapd-pluginpath: libtestreplsession-plugin
nsslapd-plugininitfunc: test_repl_session_plugin_init
nsslapd-plugintype: preoperation
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-plugin-depends-on-named: Multimaster Replication Plugin
*/

