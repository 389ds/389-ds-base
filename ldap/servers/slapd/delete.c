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
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"
#include "pratom.h"

/* Forward declarations */
static int delete_internal_pb(Slapi_PBlock *pb);
static void op_shared_delete(Slapi_PBlock *pb);

/* This function is called to process operation that come over external connections */
void
do_delete(Slapi_PBlock *pb)
{
    Slapi_Operation *operation;
    BerElement *ber;
    char *rawdn = NULL;
    int err = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "do_delete", "<==\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    ber = operation->o_ber;

    /* count the delete request */
    slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsRemoveEntryOps);

    /*
     * Parse the delete request.  It looks like this:
     *
     *    DelRequest := DistinguishedName
     */

    if (ber_scanf(operation->o_ber, "a", &rawdn) == LBER_ERROR) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "do_delete", "ber_scanf failed (op=Delete; params=DN)\n");
        op_shared_log_error_access(pb, "DEL", "???", "decoding error");
        send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0,
                         NULL);
        goto free_and_return;
    }
    /* Check if we should be performing strict validation. */
    if (config_get_dn_validate_strict()) {
        /* check that the dn is formatted correctly */
        err = slapi_dn_syntax_check(pb, rawdn, 1);
        if (err) { /* syntax check failed */
            op_shared_log_error_access(pb, "DEL", rawdn ? rawdn : "",
                                       "strict: invalid dn");
            send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX,
                             NULL, "invalid dn", 0, NULL);
            goto free_and_return;
        }
    }

    /*
     * in LDAPv3 there can be optional control extensions on
     * the end of an LDAPMessage. we need to read them in and
     * pass them to the backend.
     */
    if ((err = get_ldapmessage_controls(pb, ber, NULL)) != 0) {
        op_shared_log_error_access(pb, "DEL", rawdn, "decoding error");
        send_ldap_result(pb, err, NULL, NULL, 0, NULL);
        goto free_and_return;
    }

    slapi_log_err(SLAPI_LOG_ARGS, "do_delete", "dn (%s)\n", rawdn);

    slapi_pblock_set(pb, SLAPI_REQUESTOR_ISROOT, &(operation->o_isroot));
    slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, rawdn);

    op_shared_delete(pb);

free_and_return:;
    slapi_ch_free((void **)&rawdn);
}

/* This function is used to issue internal delete operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_delete_internal_pb instead */
Slapi_PBlock *
slapi_delete_internal(const char *idn, LDAPControl **controls, int dummy __attribute__((unused)))
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_PBlock *result_pb;
    int opresult;

    slapi_delete_internal_set_pb(pb, idn, controls, NULL, plugin_get_default_component_id(), 0);

    delete_internal_pb(pb);

    result_pb = slapi_pblock_new();
    if (result_pb) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    }
    slapi_pblock_destroy(pb);

    return result_pb;
}

/*     This is new style API to issue internal delete operation.
    pblock should contain the following data (can be set via call to slapi_delete_internal_set_pb):
    For uniqueid based operation:
        SLAPI_TARGET_DN set to dn that allows to select right backend, can be stale
        SLAPI_TARGET_UNIQUEID set to the uniqueid of the entry we are looking for
        SLAPI_CONTROLS_ARG set to request controls if present

    For dn based search:
        SLAPI_TARGET_DN set to the entry dn
        SLAPI_CONTROLS_ARG set to request controls if present
 */
int
slapi_delete_internal_pb(Slapi_PBlock *pb)
{
    if (pb == NULL)
        return -1;

    if (!allow_operation(pb)) {
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                               "This plugin is not configured to access operation target data", 0, NULL);
        return 0;
    }

    return delete_internal_pb(pb);
}

/* Initialize a pblock for a call to slapi_delete_internal_pb() */
void
slapi_delete_internal_set_pb(Slapi_PBlock *pb,
                             const char *rawdn,
                             LDAPControl **controls,
                             const char *uniqueid,
                             Slapi_ComponentId *plugin_identity,
                             int operation_flags)
{
    Operation *op;
    PR_ASSERT(pb != NULL);
    if (pb == NULL || rawdn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_delete_internal_set_pb",
                      "NULL parameter\n");
        return;
    }

    op = internal_operation_new(SLAPI_OPERATION_DELETE, operation_flags);
    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, (void *)rawdn);
    slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
    if (uniqueid) {
        slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void *)uniqueid);
    }
    slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

/* Helper functions */

static int
delete_internal_pb(Slapi_PBlock *pb)
{
    LDAPControl **controls;
    Operation *op;
    int opresult = 0;

    PR_ASSERT(pb != NULL);

    slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data = &opresult;
    op->o_result_handler = internal_getresult_callback;

    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);

    /* set parameters common for all internal operations */
    set_common_params(pb);

    /* set actions taken to process the operation */
    set_config_params(pb);

    /* perform delete operation */
    slapi_td_internal_op_start();
    op_shared_delete(pb);
    slapi_td_internal_op_finish();

    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);

    return 0;
}

static void
op_shared_delete(Slapi_PBlock *pb)
{
    char *rawdn = NULL;
    const char *dn = NULL;
    Slapi_Backend *be = NULL;
    int internal_op;
    Slapi_DN *sdn = NULL;
    Slapi_Operation *operation;
    Slapi_Entry *referral;
    Slapi_Entry *ecopy = NULL;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    int err;
    char *proxydn = NULL;
    char *proxystr = NULL;
    int proxy_err = LDAP_SUCCESS;
    char *errtext = NULL;

    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET, &rawdn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);

    /* Set the time we actually started the operation */
    slapi_operation_set_time_started(operation);

    sdn = slapi_sdn_new_dn_byval(rawdn);
    dn = slapi_sdn_get_dn(sdn);
    slapi_pblock_set(pb, SLAPI_DELETE_TARGET_SDN, (void *)sdn);
    if (rawdn && (strlen(rawdn) > 0) && (NULL == dn)) {
        /* normalization failed */
        op_shared_log_error_access(pb, "DEL", rawdn, "invalid dn");
        send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX,
                         NULL, "invalid dn", 0, NULL);
        goto free_and_return;
    }

    /* target spec is used to decide which plugins are applicable for the operation */
    operation_set_target_spec(operation, sdn);

    /* get the proxy auth dn if the proxy auth control is present */
    proxy_err = proxyauth_get_dn(pb, &proxydn, &errtext);

    if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
        if (proxydn) {
            proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
        }

        if (!internal_op) {
            Connection *pb_conn = NULL;
            Operation *pb_op = NULL;
            slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
            slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d DEL dn=\"%s\"%s\n",
                             pb_conn ? pb_conn->c_connid : -1,
                             pb_op ? pb_op->o_opid : -1,
                             slapi_sdn_get_dn(sdn),
                             proxystr ? proxystr : "");
        } else {
            uint64_t connid;
            int32_t op_id;
            int32_t op_internal_id;
            int32_t op_nested_count;
            get_internal_conn_op(&connid, &op_id, &op_internal_id, &op_nested_count);
            slapi_log_access(LDAP_DEBUG_ARGS,
                             connid==0 ? "conn=Internal(%" PRId64 ") op=%d(%d)(%d) DEL dn=\"%s\"%s\n" :
                                         "conn=%" PRId64 " (Internal) op=%d(%d)(%d) DEL dn=\"%s\"%s\n",
                             connid,
                             op_id,
                             op_internal_id,
                             op_nested_count,
                             slapi_sdn_get_dn(sdn),
                             proxystr ? proxystr : "");
        }
    }

    /* If we encountered an error parsing the proxy control, return an error
     * to the client.  We do this here to ensure that we log the operation first. */
    if (proxy_err != LDAP_SUCCESS) {
        send_ldap_result(pb, proxy_err, NULL, errtext, 0, NULL);
        goto free_and_return;
    }

    /*
     * We could be serving multiple database backends.  Select the
     * appropriate one.
     */
    if ((err = slapi_mapping_tree_select(pb, &be, &referral, errorbuf, sizeof(errorbuf))) != LDAP_SUCCESS) {
        send_ldap_result(pb, err, NULL, errorbuf, 0, NULL);
        be = NULL;
        goto free_and_return;
    }

    if (referral) {
        int managedsait;

        slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
        if (managedsait) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "cannot delete referral", 0, NULL);
            slapi_entry_free(referral);
            goto free_and_return;
        }

        send_referrals_from_entry(pb, referral);
        slapi_entry_free(referral);
        goto free_and_return;
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);

    /*
     * call the pre-delete plugins. if they succeed, call
     * the backend delete function. then call the
     * post-delete plugins.
     */
    if (plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN : SLAPI_PLUGIN_PRE_DELETE_FN) == SLAPI_PLUGIN_SUCCESS) {
        int rc;

        slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
        set_db_default_result_handlers(pb);
        if (be->be_delete != NULL) {
            if ((rc = (*be->be_delete)(pb)) == 0) {
                /* we don't perform acl check for internal operations */
                /* Dont update aci store for remote acis              */
                if ((!internal_op) &&
                    (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)))
                    plugin_call_acl_mods_update(pb, SLAPI_OPERATION_DELETE);

                if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_AUDIT))
                    write_audit_log_entry(pb); /* Record the operation in the audit log */

                slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &ecopy);
                do_ps_service(ecopy, NULL, LDAP_CHANGETYPE_DELETE, 0);
            } else {
                if (rc == SLAPI_FAIL_DISKFULL) {
                    operation_out_of_disk_space();
                    goto free_and_return;
                }
                /* If the disk is full we don't want to make it worse ... */
                if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_AUDIT)) {
                    write_auditfail_log_entry(pb); /* Record the operation in the audit log */
                }
            }
        }

        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN : SLAPI_PLUGIN_POST_DELETE_FN);
    }

free_and_return:
    if (be) {
        slapi_be_Unlock(be);
    }
    {
        char *coldn = NULL;
        Slapi_Entry *epre = NULL, *eparent = NULL;
        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &epre);
        slapi_pblock_get(pb, SLAPI_DELETE_GLUE_PARENT_ENTRY, &eparent);
        slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, NULL);
        slapi_pblock_set(pb, SLAPI_DELETE_GLUE_PARENT_ENTRY, NULL);
        if (epre == eparent) {
            eparent = NULL;
        }
        slapi_entry_free(epre);
        slapi_entry_free(eparent);
        slapi_pblock_get(pb, SLAPI_URP_NAMING_COLLISION_DN, &coldn);
        slapi_ch_free_string(&coldn);
        slapi_pblock_set(pb, SLAPI_URP_NAMING_COLLISION_DN, NULL);
    }

    slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &sdn);
    slapi_sdn_free(&sdn);
    slapi_ch_free_string(&proxydn);
    slapi_ch_free_string(&proxystr);
}
