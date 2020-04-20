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
 * Start TLS - LDAP Extended Operation.
 *
 *
 * This plugin implements the "Start TLS (Transport Layer Security)"
 * extended operation for LDAP. The plugin function is called by
 * the server if an LDAP client request contains the OID:
 * "1.3.6.1.4.1.1466.20037".
 *
 */

#include <stdio.h>
#include <string.h>
#include <private/pprio.h>
#include <prio.h>
#include <ssl.h>
#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"


/* OID of the extended operation handled by this plug-in */
/* #define START_TLS_OID    "1.3.6.1.4.1.1466.20037" */


Slapi_PluginDesc exopdesc = {"start_tls_plugin", VENDOR, DS_PACKAGE_VERSION,
                             "Start TLS extended operation plugin"};

static int
start_tls_io_enable(Connection *c, void *data __attribute__((unused)))
{
    int secure = 1;
    PRFileDesc *newsocket;
    int rv = -1;
    int ns;

    /* So far we have set up the environment for deploying SSL. It's now time to import the socket
     * into SSL and to configure it consequently. */

    if (slapd_ssl_listener_is_initialized() != 0) {
        PRFileDesc *ssl_listensocket;

        ssl_listensocket = get_ssl_listener_fd();
        if (ssl_listensocket == (PRFileDesc *)NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "start_tls_io_enable",
                          "SSL listener socket not found.\n");
            goto done;
        }
        newsocket = slapd_ssl_importFD(ssl_listensocket, c->c_prfd);
        if (newsocket == (PRFileDesc *)NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "start_tls_io_enable",
                          "SSL socket import failed.\n");
            goto done;
        }
    } else {
        if (slapd_ssl_init2(&c->c_prfd, 1) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "start_tls_io_enable",
                          "SSL socket import or configuration failed.\n");
            goto done;
        }
        newsocket = c->c_prfd;
    }


    rv = slapd_ssl_resetHandshake(newsocket, 1);
    if (rv != SECSuccess) {
        slapi_log_err(SLAPI_LOG_ERR, "start_tls_io_enable",
                      "Unable to set socket ready for SSL handshake.\n");
        goto done;
    }


    /* From here on, messages will be sent through the SSL layer, so we need to get our
     * connection ready. */

    ns = configure_pr_socket(&newsocket, secure, 0 /*never local*/);

    c->c_flags |= CONN_FLAG_SSL;
    c->c_flags |= CONN_FLAG_START_TLS;
    c->c_sd = ns;
    c->c_prfd = newsocket;

    /* Get the effective key length */
    SSL_SecurityStatus(c->c_prfd, NULL, NULL, NULL, &(c->c_ssl_ssf), NULL, NULL);

    rv = slapd_ssl_handshakeCallback(c->c_prfd, (void *)handle_handshake_done, c);

    if (rv < 0) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "start_tls_io_enable",
                      "SSL_HandshakeCallback() %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      rv, prerr, slapd_pr_strerror(prerr));
    }

    if (config_get_SSLclientAuth() != SLAPD_SSLCLIENTAUTH_OFF) {
        rv = slapd_ssl_badCertHook(c->c_prfd, (void *)handle_bad_certificate, c);
        if (rv < 0) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "start_tls_io_enable",
                          "SSL_BadCertHook(%i) %i " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          c->c_sd, rv, prerr, slapd_pr_strerror(prerr));
        }
    }

done:
    return rv;
}


/* Start TLS Extended operation plugin function */
int
start_tls(Slapi_PBlock *pb)
{

    char *oid;
    Connection *conn;
    Operation *pb_op;
    int ldaprc = LDAP_SUCCESS;
    char *ldapmsg = NULL;

    /* Get the pb ready for sending Start TLS Extended Responses back to the client.
     * The only requirement is to set the LDAP OID of the extended response to the START_TLS_OID. */

    if (slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, START_TLS_OID) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Could not set extended response oid.\n");
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                               "Could not set extended response oid.", 0, NULL);
        return (SLAPI_PLUGIN_EXTENDED_SENT_RESULT);
    }


    /* Before going any further, we'll make sure that the right extended operation plugin
     * has been called: i.e., the OID shipped whithin the extended operation request must
     * match this very plugin's OID: START_TLS_OID. */

    if (slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_OID, &oid) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Could not get OID value from request.\n");
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                               "Could not get OID value from request.", 0, NULL);
        return (SLAPI_PLUGIN_EXTENDED_SENT_RESULT);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Received extended operation request with OID %s\n", oid);
    }

    if (strcasecmp(oid, START_TLS_OID) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Request OID does not match Start TLS OID.\n");
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                               "Request OID does not match Start TLS OID.", 0, NULL);
        return (SLAPI_PLUGIN_EXTENDED_SENT_RESULT);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Start TLS extended operation request confirmed.\n");
    }


    /* At least we know that the request was indeed an Start TLS one. */

    slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
    pthread_mutex_lock(&(conn->c_mutex));
    /* cannot call slapi_send_ldap_result with mutex locked - will deadlock if ber_flush returns error */
    if (conn->c_prfd == (PRFileDesc *)NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Connection socket not available.\n");
        ldaprc = LDAP_UNAVAILABLE;
        ldapmsg = "Connection socket not available.";
        goto unlock_and_return;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);

    /* Check whether the Start TLS request can be accepted. */
    if (connection_operations_pending(conn, pb_op,
                                      1 /* check for ops where result not yet sent */)) {
        for (Operation *op = conn->c_ops; op != NULL; op = op->o_next) {
            if (op == pb_op) {
                continue;
            }
            if ((op->o_msgid == -1) && (op->o_tag == LBER_DEFAULT)) {
                /* while processing start-tls extop we also received a new incoming operation
                 * As this operation will not processed until start-tls completes.
                 * Be fair do not consider this operation as a pending one
                 */
                slapi_log_err(SLAPI_LOG_CONNS, "start_tls",
                              "New incoming operation blocked by start-tls, Continue start-tls (conn=%"PRIu64").\n",
                              conn->c_connid);
                continue;
            } else {
                /* It is problematic, this pending operation is processed and
                 * start-tls can push new network layer while the operation
                 * send result. Safest to abort start-tls
                 */
                slapi_log_err(SLAPI_LOG_CONNS, "start_tls",
                              "Other operations are still pending on the connection.\n");
                ldaprc = LDAP_OPERATIONS_ERROR;
                ldapmsg = "Other operations are still pending on the connection.";
                goto unlock_and_return;
            }
        }
    }


    if (!config_get_security()) {
        /* if any, here is where the referral to another SSL supporting server should be done: */
        /* slapi_send_ldap_result( pb, LDAP_REFERRAL, NULL, msg, 0, url ); */
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "SSL not supported by this server.\n");
        ldaprc = LDAP_PROTOCOL_ERROR;
        ldapmsg = "SSL not supported by this server.";
        goto unlock_and_return;
    }


    if (conn->c_flags & CONN_FLAG_SSL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "SSL connection already established.\n");
        ldaprc = LDAP_OPERATIONS_ERROR;
        ldapmsg = "SSL connection already established.";
        goto unlock_and_return;
    }

    if (conn->c_flags & CONN_FLAG_SASL_CONTINUE) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "SASL multi-stage bind in progress.\n");
        ldaprc = LDAP_OPERATIONS_ERROR;
        ldapmsg = "SASL multi-stage bind in progress.";
        goto unlock_and_return;
    }

    if (conn->c_flags & CONN_FLAG_CLOSING) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Connection being closed at this moment.\n");
        ldaprc = LDAP_UNAVAILABLE;
        ldapmsg = "Connection being closed at this moment.";
        goto unlock_and_return;
    }

    /* At first sight, there doesn't seem to be any major impediment to start TLS.
     * So, we may as well try initialising SSL. */

    if (slapd_security_library_is_initialized() == 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "NSS libraries not initialised.\n");
        ldaprc = LDAP_UNAVAILABLE;
        ldapmsg = "NSS libraries not initialised.";
        goto unlock_and_return;
    }

    /* Enable TLS I/O on the connection */
    connection_set_io_layer_cb(conn, start_tls_io_enable, NULL, NULL);

    /* Since no specific argument for denying the Start TLS request has been found,
     * we send a success response back to the client. */
    ldapmsg = "Start TLS request accepted.Server willing to negotiate SSL.";
unlock_and_return:
    pthread_mutex_unlock(&(conn->c_mutex));
    slapi_send_ldap_result(pb, ldaprc, NULL, ldapmsg, 0, NULL);

    return (SLAPI_PLUGIN_EXTENDED_SENT_RESULT);

} /* start_tls */


/* TLS Graceful Closure function.
 * The function below kind of "resets" the connection to its state previous
 * to receiving the Start TLS operation request. But it also sets the
 * authorization and authentication credentials to "anonymous". Finally,
 * it sends a closure alert message to the client.
 *
 * Note: start_tls_graceful_closure() must be called with c->c_mutex locked.
 */
int
start_tls_graceful_closure(Connection *c, Slapi_PBlock *pb, int is_initiator)
{
    int ns;
    Slapi_PBlock *pblock = pb;
    struct slapdplugin *plugin;
    int secure = 0;
    PRFileDesc *ssl_fd;
    Operation *pb_op = NULL;

    if (pblock == NULL) {
        pblock = slapi_pblock_new();
        plugin = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));
        slapi_pblock_set(pb, SLAPI_PLUGIN, plugin);
        slapi_pblock_set(pb, SLAPI_CONNECTION, c);
        slapi_pblock_set(pb, SLAPI_OPERATION, c->c_ops);
        set_db_default_result_handlers(pblock);
        if (slapi_pblock_set(pblock, SLAPI_EXT_OP_RET_OID, START_TLS_OID) != 0) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                          "Could not set extended response oid.\n");
            slapi_send_ldap_result(pblock, LDAP_OPERATIONS_ERROR, NULL,
                                   "Could not set extended response oid.", 0, NULL);
            return (SLAPI_PLUGIN_EXTENDED_SENT_RESULT);
        }
        slapi_ch_free((void **)&plugin);
    }

    slapi_pblock_get(pblock, SLAPI_OPERATION, &pb_op);
    /* First thing to do is to finish with whatever operation may be hanging on the
     * encrypted session.
     */
    while (connection_operations_pending(c, pb_op,
                                         0 /* wait for all other ops to full complete */)) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls",
                      "Still %d operations to be completed before closing the SSL connection.\n",
                      c->c_refcnt - 1);
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls_graceful_closure", "SSL_CLOSE_NOTIFY_ALERT\n");

    /* An SSL close_notify alert should be sent to the client. However, the NSS API
     * doesn't provide us with anything alike.
     */
    slapi_send_ldap_result(pblock, LDAP_OPERATIONS_ERROR, NULL,
                           "SSL_CLOSE_NOTIFY_ALERT", 0, NULL);

    if (is_initiator) {
        /* if this call belongs to the initiator of the SSL connection closure, it must first
       * wait for the peer to send another close_notify alert back.
       */
    }

    pthread_mutex_lock(&(c->c_mutex));

    /* "Unimport" the socket from SSL, i.e. get rid of the upper layer of the
     * file descriptor stack, which represents SSL.
     * The ssl socket assigned to c->c_prfd should also be closed and destroyed.
     * Should find a way of getting that ssl socket.
     */
    /*
    rc = strcasecmp( "SSL", PR_GetNameForIdentity( c->c_prfd->identity ) );
    if ( rc == 0 ) {
      sslSocket * ssl_socket;

      ssl_socket = (sslSocket *) c->c_prfd->secret;
      ssl_socket->fd = NULL;
    }
    */
    ssl_fd = PR_PopIOLayer(c->c_prfd, PR_TOP_IO_LAYER);
    ssl_fd->dtor(ssl_fd);

    secure = 0;
    ns = configure_pr_socket(&(c->c_prfd), secure, 0 /*never local*/);
    c->c_sd = ns;
    c->c_flags &= ~CONN_FLAG_SSL;
    c->c_flags &= ~CONN_FLAG_START_TLS;
    c->c_ssl_ssf = 0;

    /*  authentication & authorization credentials must be set to "anonymous". */

    bind_credentials_clear(c, PR_FALSE, PR_TRUE);

    pthread_mutex_unlock(&(c->c_mutex));

    return (SLAPI_PLUGIN_EXTENDED_SENT_RESULT);
}


static char *start_tls_oid_list[] = {
    START_TLS_OID,
    NULL};
static char *start_tls_name_list[] = {
    "startTLS",
    NULL};

int
start_tls_register_plugin()
{
    slapi_register_plugin("extendedop", 1 /* Enabled */, "start_tls_init",
                          start_tls_init, "Start TLS extended operation",
                          start_tls_oid_list, NULL);

    return 0;
}


/* Initialization function */

int
start_tls_init(Slapi_PBlock *pb)
{
    char **argv;
    char *oid;

    /* Get the arguments appended to the plugin extendedop directive. The first argument
     * (after the standard arguments for the directive) should contain the OID of the
     * extended operation.
     */

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls_init", "Could not get argv\n");
        return (-1);
    }

    /* Compare the OID specified in the configuration file against the Start TLS OID. */

    if (argv == NULL || strcmp(argv[0], START_TLS_OID) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls_init",
                      "OID is missing or is not %s\n", START_TLS_OID);
        return (-1);
    } else {
        oid = slapi_ch_strdup(argv[0]);
        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls_init",
                      "Registering plug-in for Start TLS extended op %s.\n", oid);
        slapi_ch_free_string(&oid);
    }

    /* Register the plug-in function as an extended operation
     * plug-in function that handles the operation identified by
     * OID 1.3.6.1.4.1.1466.20037.  Also specify the version of the server
     * plug-in */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&exopdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)start_tls) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, start_tls_oid_list) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, start_tls_name_list) != 0) {

        slapi_log_err(SLAPI_LOG_PLUGIN, "start_tls_init",
                      "Failed to set plug-in version, function, and OID.\n");
        return (-1);
    }

    return (0);
}
