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

/* extendedop.c - handle an LDAPv3 extended operation */

#include <stdio.h>
#include "slap.h"

static const char *extended_op_oid2string(const char *oid);


/********** this stuff should probably be moved when it's done **********/

static void
extop_handle_import_start(Slapi_PBlock *pb, char *extoid __attribute__((unused)), struct berval *extval)
{
    char *orig = NULL;
    const char *suffix = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Backend *be = NULL;
    struct berval bv;
    int ret;
    Operation *pb_op = NULL;
    Connection *pb_conn = NULL;

    if (extval == NULL || extval->bv_val == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "extop_handle_import_start", "no data supplied\n");
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                         "no data supplied", 0, NULL);
        return;
    }
    orig = slapi_ch_malloc(extval->bv_len + 1);
    strncpy(orig, extval->bv_val, extval->bv_len);
    orig[extval->bv_len] = 0;
    /* Check if we should be performing strict validation. */
    if (config_get_dn_validate_strict()) {
        /* check that the dn is formatted correctly */
        ret = slapi_dn_syntax_check(pb, orig, 1);
        if (ret) { /* syntax check failed */
            slapi_log_err(SLAPI_LOG_ERR, "extop_handle_import_start",
                          "strict: invalid suffix (%s)\n", orig);
            send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                             "invalid suffix", 0, NULL);
            return;
        }
    }
    sdn = slapi_sdn_new_dn_passin(orig);
    if (!sdn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "extop_handle_import_start", "Out of memory\n");
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return;
    }
    suffix = slapi_sdn_get_dn(sdn);
    /*    be = slapi_be_select(sdn); */
    be = slapi_mapping_tree_find_backend_for_sdn(sdn);
    if (be == NULL || be == defbackend_get_backend()) {
        /* might be instance name instead of suffix */
        be = slapi_be_select_by_instance_name(suffix);
    }
    if (be == NULL || be == defbackend_get_backend()) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "extop_handle_import_start", "invalid suffix or instance name '%s'\n",
                      suffix);
        send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL,
                         "invalid suffix or instance name", 0, NULL);
        goto out;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_REQUESTOR_ISROOT, &(pb_op->o_isroot));

    {
        /* Access Control Check to see if the client is
         *  allowed to use task import
         */
        char *dummyAttr = "dummy#attr";
        char *dummyAttrs[2] = {NULL, NULL};
        int rc = 0;
        char dn[128];
        Slapi_Entry *feature;

        /* slapi_str2entry modify its dn parameter so we must copy
         * this string each time we call it !
         */
        /* This dn is no need to be normalized. */
        PR_snprintf(dn, sizeof(dn), "dn: oid=%s,cn=features,cn=config",
                    EXTOP_BULK_IMPORT_START_OID);

        dummyAttrs[0] = dummyAttr;
        feature = slapi_str2entry(dn, 0);
        rc = plugin_call_acl_plugin(pb, feature, dummyAttrs, NULL,
                                    SLAPI_ACL_WRITE, ACLPLUGIN_ACCESS_DEFAULT, NULL);
        slapi_entry_free(feature);
        if (rc != LDAP_SUCCESS) {
            /* Client isn't allowed to do this. */
            send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
            goto out;
        }
    }

    if (be->be_wire_import == NULL) {
        /* not supported by this backend */
        slapi_log_err(SLAPI_LOG_ERR, "extop_handle_import_start",
                      "bulk import attempted on '%s' (not supported)\n", suffix);
        send_ldap_result(pb, LDAP_NOT_SUPPORTED, NULL, NULL, 0, NULL);
        goto out;
    }

    ret = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
    slapi_pblock_set(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &ret);
    ret = SLAPI_BI_STATE_START;
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &ret);
    ret = (*be->be_wire_import)(pb);
    if (ret != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "extop_handle_import_start",
                      "error starting import (%d)\n", ret);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        goto out;
    }

    /* okay, the import is starting now -- save the backend in the
     * connection block & mark this connection as belonging to a bulk import
     */
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    if (pb_conn) {
        PR_EnterMonitor(pb_conn->c_mutex);
        pb_conn->c_flags |= CONN_FLAG_IMPORT;
        pb_conn->c_bi_backend = be;
        PR_ExitMonitor(pb_conn->c_mutex);
    }

    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, EXTOP_BULK_IMPORT_START_OID);
    bv.bv_val = NULL;
    bv.bv_len = 0;
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, &bv);
    send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    slapi_log_err(SLAPI_LOG_INFO, "extop_handle_import_start",
                  "Bulk import begin import on '%s'.\n", suffix);

out:
    slapi_sdn_free(&sdn);
    return;
}

static void
extop_handle_import_done(Slapi_PBlock *pb, char *extoid __attribute__((unused)), struct berval *extval __attribute__((unused)))
{
    Slapi_Backend *be;
    struct berval bv;
    int ret;
    Connection *pb_conn;

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    PR_EnterMonitor(pb_conn->c_mutex);
    pb_conn->c_flags &= ~CONN_FLAG_IMPORT;
    be = pb_conn->c_bi_backend;
    pb_conn->c_bi_backend = NULL;
    PR_ExitMonitor(pb_conn->c_mutex);

    if ((be == NULL) || (be->be_wire_import == NULL)) {
        /* can this even happen? */
        slapi_log_err(SLAPI_LOG_ERR, "extop_handle_import_done",
                      "backend not supported\n");
        send_ldap_result(pb, LDAP_NOT_SUPPORTED, NULL, NULL, 0, NULL);
        return;
    }

    /* signal "done" to the backend */
    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_ENTRY, NULL);
    ret = SLAPI_BI_STATE_DONE;
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &ret);
    ret = (*be->be_wire_import)(pb);
    if (ret != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "extop_handle_import_done",
                      "bulk import error ending import (%d)\n", ret);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return;
    }

    /* more goofiness */
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, EXTOP_BULK_IMPORT_DONE_OID);
    bv.bv_val = NULL;
    bv.bv_len = 0;
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, &bv);
    send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    slapi_log_err(SLAPI_LOG_INFO, "extop_handle_import_done",
                  "Bulk import completed successfully.\n");
    return;
}


void
do_extended(Slapi_PBlock *pb)
{
    char *extoid = NULL, *errmsg;
    struct berval extval = {0};
    struct slapdplugin *p = NULL;
    int lderr, rc;
    ber_len_t len;
    ber_tag_t tag;
    const char *name;
    Operation *pb_op = NULL;
    Connection *pb_conn = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "->\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    /* Set the time we actually started the operation */
    slapi_operation_set_time_started(pb_op);

    if (pb_conn == NULL || pb_op == NULL) {
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "param error", 0, NULL);
        slapi_log_err(SLAPI_LOG_ERR, "do_extended",
                      "NULL param error: conn (0x%p) op (0x%p)\n", pb_conn, pb_op);
        goto free_and_return;
    }

    /*
     * Parse the extended request. It looks like this:
     *
     *  ExtendedRequest := [APPLICATION 23] SEQUENCE {
     *      requestName [0] LDAPOID,
     *      requestValue    [1] OCTET STRING OPTIONAL
     *  }
     */

    if (ber_scanf(pb_op->o_ber, "{a", &extoid) == LBER_ERROR) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "do_extended", "ber_scanf failed (op=extended; params=OID)\n");
        op_shared_log_error_access(pb, "EXT", "???", "decoding error: fail to get extension OID");
        send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0,
                         NULL);
        goto free_and_return;
    }
    tag = ber_peek_tag(pb_op->o_ber, &len);

    if (tag == LDAP_TAG_EXOP_REQ_VALUE) {
        if (ber_scanf(pb_op->o_ber, "o}", &extval) == LBER_ERROR) {
            op_shared_log_error_access(pb, "EXT", "???", "decoding error: fail to get extension value");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0,
                             NULL);
            goto free_and_return;
        }
    } else {
        if (ber_scanf(pb_op->o_ber, "}") == LBER_ERROR) {
            op_shared_log_error_access(pb, "EXT", "???", "decoding error");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0,
                             NULL);
            goto free_and_return;
        }
    }
    if (NULL == (name = extended_op_oid2string(extoid))) {
        slapi_log_err(SLAPI_LOG_ARGS, "do_extended", "oid (%s)\n", extoid);

        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d EXT oid=\"%s\"\n",
                         pb_conn->c_connid, pb_op->o_opid, extoid);
    } else {
        slapi_log_err(SLAPI_LOG_ARGS, "do_extended", "oid (%s-%s)\n",
                      extoid, name);

        slapi_log_access(LDAP_DEBUG_STATS,
                         "conn=%" PRIu64 " op=%d EXT oid=\"%s\" name=\"%s\"\n",
                         pb_conn->c_connid, pb_op->o_opid, extoid, name);
    }

    /* during a bulk import, only BULK_IMPORT_DONE is allowed!
     * (and this is the only time it's allowed)
     */
    if (pb_conn->c_flags & CONN_FLAG_IMPORT) {
        if (strcmp(extoid, EXTOP_BULK_IMPORT_DONE_OID) != 0) {
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
            goto free_and_return;
        }
        extop_handle_import_done(pb, extoid, &extval);
        goto free_and_return;
    }

    if (strcmp(extoid, EXTOP_BULK_IMPORT_START_OID) == 0) {
        extop_handle_import_start(pb, extoid, &extval);
        goto free_and_return;
    }

    if (strcmp(extoid, START_TLS_OID) != 0) {
        int minssf = config_get_minssf();

        /* If anonymous access is disabled and we haven't
         * authenticated yet, only allow startTLS. */
        if ((config_get_anon_access_switch() != SLAPD_ANON_ACCESS_ON) && ((pb_op->o_authtype == NULL) ||
                                                                          (strcasecmp(pb_op->o_authtype, SLAPD_AUTH_NONE) == 0))) {
            send_ldap_result(pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                             "Anonymous access is not allowed.", 0, NULL);
            goto free_and_return;
        }

        /* If the minssf is not met, only allow startTLS. */
        if ((pb_conn->c_sasl_ssf < minssf) && (pb_conn->c_ssl_ssf < minssf) &&
            (pb_conn->c_local_ssf < minssf)) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "Minimum SSF not met.", 0, NULL);
            goto free_and_return;
        }
    }

    /* If a password change is required, only allow the password
     * modify extended operation */
    if (!pb_conn->c_isreplication_session &&
        pb_conn->c_needpw && (strcmp(extoid, EXTOP_PASSWD_OID) != 0)) {
        char *dn = NULL;
        slapi_pblock_get(pb, SLAPI_CONN_DN, &dn);

        (void)slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
        op_shared_log_error_access(pb, "EXT", dn ? dn : "", "need new password");
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL);

        slapi_ch_free_string(&dn);
        goto free_and_return;
    }

    /* decode the optional controls - put them in the pblock */
    if ((lderr = get_ldapmessage_controls(pb, pb_op->o_ber, NULL)) != 0) {
        char *dn = NULL;
        slapi_pblock_get(pb, SLAPI_CONN_DN, &dn);

        op_shared_log_error_access(pb, "EXT", dn ? dn : "", "failed to decode LDAP controls");
        send_ldap_result(pb, lderr, NULL, NULL, 0, NULL);

        slapi_ch_free_string(&dn);
        goto free_and_return;
    }

    slapi_pblock_set(pb, SLAPI_EXT_OP_REQ_OID, extoid);
    slapi_pblock_set(pb, SLAPI_EXT_OP_REQ_VALUE, &extval);
    slapi_pblock_set(pb, SLAPI_REQUESTOR_ISROOT, &pb_op->o_isroot);

    rc = plugin_determine_exop_plugins(extoid, &p);
    slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "Plugin_determine_exop_plugins rc %d\n", rc);

    if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_EXTOP_FN) != SLAPI_PLUGIN_SUCCESS) {
        goto free_and_return;
    }

    if (rc == SLAPI_PLUGIN_EXTENDEDOP && p != NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "Calling plugin ... \n");
        /*
         * Return values:
         *  SLAPI_PLUGIN_EXTENDED_SENT_RESULT: The result is already sent to the client.
         *                                     There is nothing to do further.
         *  SLAPI_PLUGIN_EXTENDED_NOT_HANDLED: Unsupported extended operation
         *  LDAP codes (e.g., LDAP_SUCCESS): The result is not sent yet. Call send_ldap_result.
         */
        rc = plugin_call_exop_plugins(pb, p);

        slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "Called exop, got %d \n", rc);

    } else if (rc == SLAPI_PLUGIN_BETXNEXTENDEDOP && p != NULL) {

        slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "Calling betxn plugin ... \n");
        /* Look up the correct backend to use. */
        Slapi_Backend *be = plugin_extended_op_getbackend(pb, p);

        if (be == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "do_extended", "Plugin_extended_op_getbackend was unable to retrieve a backend!\n");
            rc = LDAP_OPERATIONS_ERROR;
        } else {
            /* We need to make a new be pb here because when you set SLAPI_BACKEND
             * you overwrite the plg parts of the pb. So if we re-use pb
             * you actually nuke the request, and everything hangs. (╯°□°)╯︵ ┻━┻
             */
            Slapi_PBlock *be_pb = NULL;
            be_pb = slapi_pblock_new();
            slapi_pblock_set(be_pb, SLAPI_BACKEND, be);

            int txn_rc = slapi_back_transaction_begin(be_pb);
            if (txn_rc) {
                slapi_log_err(SLAPI_LOG_ERR, "do_extended", "Failed to start be_txn for plugin_call_exop_plugins %d\n", txn_rc);
            } else {
                /*
                 * Return values:
                 *  SLAPI_PLUGIN_EXTENDED_SENT_RESULT: The result is already sent to the client.
                 *                                     There is nothing to do further.
                 *  SLAPI_PLUGIN_EXTENDED_NOT_HANDLED: Unsupported extended operation
                 *  LDAP codes (e.g., LDAP_SUCCESS): The result is not sent yet. Call send_ldap_result.
                 */
                rc = plugin_call_exop_plugins(pb, p);
                slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "Called betxn exop, got %d \n", rc);
                if (rc == LDAP_SUCCESS || rc == SLAPI_PLUGIN_EXTENDED_SENT_RESULT) {
                    /* commit */
                    txn_rc = slapi_back_transaction_commit(be_pb);
                    if (txn_rc == 0) {
                        slapi_log_err(SLAPI_LOG_TRACE, "do_extended", "Commit with result %d \n", txn_rc);
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR, "do_extended", "Unable to commit commit with result %d \n", txn_rc);
                    }
                } else {
                    /* abort */
                    txn_rc = slapi_back_transaction_abort(be_pb);
                    slapi_log_err(SLAPI_LOG_ERR, "do_extended", "Abort with result %d \n", txn_rc);
                }
            }                            /* txn_rc */
            slapi_pblock_destroy(be_pb); /* Clean up after ourselves */
        }                                /* if be */
    }

    if (plugin_call_plugins(pb, SLAPI_PLUGIN_POST_EXTOP_FN) != SLAPI_PLUGIN_SUCCESS) {
        goto free_and_return;
    }

    if (SLAPI_PLUGIN_EXTENDED_SENT_RESULT != rc) {
        if (SLAPI_PLUGIN_EXTENDED_NOT_HANDLED == rc) {
            lderr = LDAP_PROTOCOL_ERROR; /* no plugin handled the op */
            errmsg = "unsupported extended operation";
        } else {
            if (rc != LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "do_extended", "Failed with result %d \n", rc);
            }
            errmsg = NULL;
            lderr = rc;
        }
        send_ldap_result(pb, lderr, NULL, errmsg, 0, NULL);
    }
free_and_return:
    if (extoid)
        slapi_ch_free((void **)&extoid);
    if (extval.bv_val)
        slapi_ch_free((void **)&extval.bv_val);
    return;
}


static const char *
extended_op_oid2string(const char *oid)
{
    const char *rval = NULL;

    if (0 == strcmp(oid, EXTOP_BULK_IMPORT_START_OID)) {
        rval = "Bulk Import Start";
    } else if (0 == strcmp(oid, EXTOP_BULK_IMPORT_DONE_OID)) {
        rval = "Bulk Import End";
    } else {
        rval = plugin_extended_op_oid2string(oid);
    }

    return (rval);
}
