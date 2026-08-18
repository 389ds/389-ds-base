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
 * Perform an add operation
 *
 * Returns:
 *   0  - success
 *   <0 - fail
 *
 */

int
chaining_back_add(Slapi_PBlock *pb)
{
    cb_outgoing_conn *cnx;
    Slapi_Backend *be;
    Slapi_Entry *e;
    cb_backend_instance *cb;
    LDAPControl **serverctrls = NULL;
    LDAPControl **ctrls = NULL;
    LDAPMod **mods;
    LDAPMessage *res;
    LDAP *ld = NULL;
    Slapi_DN *sdn = NULL;
    const char *dn = NULL;
    char **referrals = NULL;
    char *matched_msg, *error_msg;
    char *cnxerrbuf = NULL;
    time_t endtime = 0;
    int rc, parse_rc, msgid, i;

    if ((rc = cb_forward_operation(pb)) != LDAP_SUCCESS) {
        cb_send_ldap_result(pb, rc, NULL, "Remote data access disabled", 0, NULL);
        return -1;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    cb = cb_get_instance(be);

    /* Update monitor info */
    cb_update_monitor_info(pb, cb, SLAPI_OPERATION_ADD);

    /* Check wether the chaining BE is available or not */
    if (cb_check_availability(cb, pb) == FARMSERVER_UNAVAILABLE) {
        return -1;
    }

    slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);

    dn = slapi_sdn_get_dn(sdn);

    /* Check local access controls */
    if (cb->local_acl && !cb->associated_be_is_disabled) {
        char *errbuf = NULL;
        rc = cb_access_allowed(pb, e, NULL, NULL, SLAPI_ACL_ADD, &errbuf);
        if (rc != LDAP_SUCCESS) {
            cb_send_ldap_result(pb, rc, NULL, errbuf, 0, NULL);
            slapi_ch_free((void **)&errbuf);
            return -1;
        }
    }

    /* Build LDAPMod from the SLapi_Entry */
    cb_eliminate_illegal_attributes(cb, e);

    if ((rc = slapi_entry2mods((const Slapi_Entry *)e, NULL, &mods)) != LDAP_SUCCESS) {
        cb_send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
        return -1;
    }

    /* Grab a connection handle */
    rc = cb_get_connection(cb->pool, &ld, &cnx, NULL, &cnxerrbuf);
    if (LDAP_SUCCESS != rc) {
        static int warned_get_conn = 0;
        if (!warned_get_conn) {
            slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                          "chaining_back_add - cb_get_connection failed (%d) %s\n",
                          rc, ldap_err2string(rc));
            warned_get_conn = 1;
        }
        cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, cnxerrbuf, 0, NULL);
        ldap_mods_free(mods, 1);
        slapi_ch_free_string(&cnxerrbuf);
        /* ping the farm.
         * If the farm is unreachable, we increment the counter */
        cb_ping_farm(cb, NULL, 0);

        return -1;
    }

    /* Control management */
    if ((rc = cb_update_controls(pb, ld, &ctrls, CB_UPDATE_CONTROLS_ADDAUTH)) != LDAP_SUCCESS) {
        cb_send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
        cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
        ldap_mods_free(mods, 1);
        return -1;
    }

    if (slapi_op_abandoned(pb)) {
        cb_release_op_connection(cb->pool, ld, 0);
        ldap_mods_free(mods, 1);
        ldap_controls_free(ctrls);
        return -1;
    }

    /*
     * Call the backend preoperation plugins
     */
    if ((rc = slapi_plugin_call_preop_be_plugins(pb, SLAPI_PLUGIN_ADD_OP))) {
        slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM, "chaining_back_add - add (%s): pre betxn failed, error (%d)\n", dn, rc);
        cb_release_op_connection(cb->pool, ld, 0);
        ldap_mods_free(mods, 1);
        ldap_controls_free(ctrls);
        return -1;
    }

    /* heart-beat management */
    if (cb->max_idle_time > 0) {
        endtime = slapi_current_rel_time_t() + cb->max_idle_time;
    }

    /* Send LDAP operation to the remote host */
    rc = ldap_add_ext(ld, dn, mods, ctrls, NULL, &msgid);

    ldap_controls_free(ctrls);

    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                      "ldap_add_ext failed -- %s\n", ldap_err2string(rc));

        cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, ENDUSERMSG, 0, NULL);
        cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
        ldap_mods_free(mods, 1);
        return -1;
    }

    /*
     * Poll the server for the results of the add operation.
     * Check for abandoned operation regularly.
     */
    while (1) {

        if (cb_check_forward_abandon(cb, pb, ld, msgid)) {
            /* connection handle released in cb_check_forward_abandon() */
            ldap_mods_free(mods, 1);
            return -1;
        }

        rc = ldap_result(ld, msgid, 0, &cb->abandon_timeout, &res);
        switch (rc) {
        case -1:
            cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, ldap_err2string(rc), 0, NULL);
            cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
            ldap_mods_free(mods, 1);
            ldap_msgfree(res);
            return -1;

        case 0:
            if ((rc = cb_ping_farm(cb, cnx, endtime)) != LDAP_SUCCESS) {
                /*
                 * does not respond. give up and return a
                 * error to the client.
                 */

                /*cb_send_ldap_result(pb,LDAP_OPERATIONS_ERROR, NULL,
                    ldap_err2string(rc), 0, NULL);*/
                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "FARM SERVER TEMPORARY UNAVAILABLE", 0, NULL);
                cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
                ldap_mods_free(mods, 1);
                ldap_msgfree(res);
                return -1;
            }
#ifdef CB_YIELD
            DS_Sleep(PR_INTERVAL_NO_WAIT);
#endif
            break;

        default:
            serverctrls = NULL;
            matched_msg = error_msg = NULL;
            referrals = NULL;

            parse_rc = ldap_parse_result(ld, res, &rc, &matched_msg,
                                         &error_msg, &referrals, &serverctrls, 1);

            if (parse_rc != LDAP_SUCCESS) {
                static int warned_parse_rc = 0;
                if (!warned_parse_rc) {
                    slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                                  "chaining_back_add - %s%s%s\n",
                                  matched_msg ? matched_msg : "",
                                  (matched_msg && (*matched_msg != '\0')) ? ": " : "",
                                  ldap_err2string(parse_rc));
                    warned_parse_rc = 1;
                }
                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, ENDUSERMSG, 0, NULL);
                cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(parse_rc));
                ldap_mods_free(mods, 1);
                slapi_ch_free_string(&matched_msg);
                slapi_ch_free_string(&error_msg);
                ldap_controls_free(serverctrls);
                charray_free(referrals);
                return -1;
            }

            if (rc != LDAP_SUCCESS) {
                struct berval **refs = referrals2berval(referrals);
                static int warned_rc = 0;
                if (!warned_rc && error_msg) {
                    slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                                  "chaining_back_add - %s%s%s\n",
                                  matched_msg ? matched_msg : "",
                                  (matched_msg && (*matched_msg != '\0')) ? ": " : "",
                                  error_msg);
                    warned_rc = 1;
                }
                cb_send_ldap_result(pb, rc, matched_msg, ENDUSERMSG, 0, refs);
                cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
                ldap_mods_free(mods, 1);
                slapi_ch_free_string(&matched_msg);
                slapi_ch_free_string(&error_msg);
                if (refs)
                    ber_bvecfree(refs);
                charray_free(referrals);
                ldap_controls_free(serverctrls);
                return -1;
            }

            /* Success */
            ldap_mods_free(mods, 1);
            cb_release_op_connection(cb->pool, ld, 0);

            /* Call the backend postoperation plugins */
            if ((rc = slapi_plugin_call_postop_be_plugins(pb, SLAPI_PLUGIN_ADD_OP))) {
                slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM, "chaining_back_add - add (%s): post betxn failed, error (%d)\n", dn, rc);
            }

            /* Add control response sent by the farm server */
            for (i = 0; serverctrls && serverctrls[i]; i++)
                slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, serverctrls[i]);
            if (serverctrls)
                ldap_controls_free(serverctrls);
            slapi_ch_free_string(&matched_msg);
            slapi_ch_free_string(&error_msg);
            charray_free(referrals);
            cb_send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
            if (rc == LDAP_SUCCESS) {
                slapi_entry_free(e);
                slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
                return 0;
            } else {
                return -1;
            }
        }
    }
    /* Never reached */
}
