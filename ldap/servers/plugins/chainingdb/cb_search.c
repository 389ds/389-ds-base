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
 * Build a candidate list for this backentry and scope.
 * Could be a BASE, ONELEVEL, or SUBTREE search.
 *
 * Returns:
 *   0  - success
 *   <0 - fail
 *
 */

int
chainingdb_build_candidate_list(Slapi_PBlock *pb)
{
    cb_backend_instance *cb = NULL;
    cb_outgoing_conn *cnx;
    cb_searchContext *ctx = NULL;
    Slapi_Backend *be;
    Slapi_Operation *op;
    LDAPControl **serverctrls = NULL;
    LDAPControl **controls = NULL;
    LDAPControl **ctrls = NULL;
    LDAPMessage *res = NULL;
    LDAP *ld = NULL;
    Slapi_DN *target_sdn = NULL;
    const char *target = NULL;
    char *filter;
    char **attrs = NULL;
    struct timespec expire_time;
    struct timeval timeout;
    time_t endtime = 0;
    char *matched_msg, *error_msg;
    char **referrals = NULL;
    char *cnxerrbuf = NULL;
    int scope, attrsonly, sizelimit, timelimit, searchreferral;
    int rc, parse_rc, doit;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    cb = cb_get_instance(be);

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &filter);
    slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &target_sdn);

    target = slapi_sdn_get_dn(target_sdn);

    if (LDAP_SUCCESS != (parse_rc = cb_forward_operation(pb))) {
        /* Don't return errors */
        if (cb_debug_on()) {
            slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                          "chainingdb_build_candidate_list - local search: base:<%s> scope:<%s> filter:<%s>\n", target,
                          scope == LDAP_SCOPE_SUBTREE ? "SUBTREE" : scope == LDAP_SCOPE_ONELEVEL ? "ONE-LEVEL" : "BASE", filter);
        }

        ctx = (cb_searchContext *)slapi_ch_calloc(1, sizeof(cb_searchContext));
        ctx->type = CB_SEARCHCONTEXT_ENTRY;
        ctx->data = NULL;
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);

        return 0;
    }

    cb_update_monitor_info(pb, cb, SLAPI_OPERATION_SEARCH);

    /* Check wether the chaining BE is available or not */
    if (cb_check_availability(cb, pb) == FARMSERVER_UNAVAILABLE) {
        return -1;
    }

    if (cb_debug_on()) {
        slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                      "chainingdb_build_candidate_list - chained search: base:<%s> scope:<%s> filter:<%s>\n", target,
                      scope == LDAP_SCOPE_SUBTREE ? "SUBTREE" : scope == LDAP_SCOPE_ONELEVEL ? "ONE-LEVEL" : "BASE",
                      filter);
    }

    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &controls);
    slapi_pblock_get(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);
    slapi_pblock_get(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);
    slapi_operation_time_expiry(op, (time_t)timelimit, &expire_time);
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);

    if ((scope != LDAP_SCOPE_BASE) && (scope != LDAP_SCOPE_ONELEVEL) && (scope != LDAP_SCOPE_SUBTREE)) {
        cb_send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, "Bad scope", 0, NULL);
        return 1;
    }

    searchreferral = cb->searchreferral;

    if ((scope != LDAP_SCOPE_BASE) && (searchreferral)) {
        int i;
        struct berval bv, *bvals[2];
        Slapi_Entry **aciArray = (Slapi_Entry **)slapi_ch_malloc(2 * sizeof(Slapi_Entry *));
        Slapi_Entry *anEntry = slapi_entry_alloc();

        slapi_entry_set_sdn(anEntry, target_sdn);

        bvals[1] = NULL;
        bvals[0] = &bv;
        bv.bv_val = "referral";
        bv.bv_len = strlen(bv.bv_val);
        slapi_entry_add_values(anEntry, "objectclass", bvals);

        slapi_rwlock_rdlock(cb->rwl_config_lock);
        for (i = 0; cb->url_array && cb->url_array[i]; i++) {
            char *anUrl = slapi_ch_smprintf("%s%s", cb->url_array[i], target);
            bv.bv_val = anUrl;
            bv.bv_len = strlen(bv.bv_val);
            slapi_entry_attr_merge(anEntry, "ref", bvals);
            slapi_ch_free((void **)&anUrl);
        }
        slapi_rwlock_unlock(cb->rwl_config_lock);

        aciArray[0] = anEntry;
        aciArray[1] = NULL;

        ctx = (cb_searchContext *)slapi_ch_calloc(1, sizeof(cb_searchContext));
        ctx->type = CB_SEARCHCONTEXT_ENTRY;
        ctx->data = aciArray;
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);
        return 0;
    }

    /*
    ** Time limit management.
    ** Make sure the operation has not expired
    */
    if (slapi_timespec_expire_check(&expire_time) == TIMER_EXPIRED) {
        cb_send_ldap_result(pb, LDAP_TIMELIMIT_EXCEEDED, NULL, NULL, 0, NULL);
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
        return 1;
    }
    /* Set the timeout for ldap_search_ext */
    /* for some reasons, it is an error to pass in a zero'd timeval */
    /* to ldap_search_ext()                            */
    if (timelimit == -1 || timelimit == 0) {
        timeout.tv_sec = -1;
        timeout.tv_usec = -1;
    } else {
        /* Get the elapsed op time to fix our time limit */
        struct timespec elapsed;
        slapi_operation_time_elapsed(op, &elapsed);
        timeout.tv_sec = ((time_t)timelimit) - elapsed.tv_sec;
        timeout.tv_usec = 0;
    }


    /* Operational attribute support for internal searches:         */
    /* The front-end relies on the fact that operational attributes */
    /* are returned along with standard attrs when the attr list is */
    /* NULL. To make it work, we need to explicitly request for all*/
    /* possible operational attrs. Too bad.             */

    if ((attrs == NULL) && operation_is_flag_set(op, OP_FLAG_INTERNAL)) {
        attrs = cb->every_attribute;
    } else {
        int i;
        if (attrs != NULL) {
            for (i = 0; attrs[i] != NULL; i++) {
                if (strcasecmp("nsrole", attrs[i]) == 0) {
                    attrs = cb->every_attribute;
                    break;
                }
            }
        }
    }

    /* Grab a connection handle */
    rc = cb_get_connection(cb->pool, &ld, &cnx, &expire_time, &cnxerrbuf);
    if (LDAP_SUCCESS != rc) {
        static int warned_get_conn = 0;
        if (!warned_get_conn) {
            slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                          "chainingdb_build_candidate_list - cb_get_connection failed (%d) %s\n",
                          rc, ldap_err2string(rc));
            warned_get_conn = 1;
        }
        if (rc == LDAP_TIMELIMIT_EXCEEDED) {
            cb_send_ldap_result(pb, rc, NULL, cnxerrbuf, 0, NULL);
        } else {
            cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                cnxerrbuf, 0, NULL);
        }
        slapi_ch_free_string(&cnxerrbuf);
        /* ping the farm.
         * If the farm is unreachable, we increment the counter */
        cb_ping_farm(cb, NULL, 0);
        return 1;
    }

    /*
     * Control management
     */

    if (LDAP_SUCCESS != (rc = cb_update_controls(pb, ld, &ctrls, CB_UPDATE_CONTROLS_ADDAUTH))) {
        cb_send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
        cb_release_op_connection(cb->pool, ld, 0);
        return 1;
    }

    if (slapi_op_abandoned(pb)) {
        cb_release_op_connection(cb->pool, ld, 0);
        ldap_controls_free(ctrls);
        return 1;
    }

    ctx = (cb_searchContext *)slapi_ch_calloc(1, sizeof(cb_searchContext));

    /*
    ** We need to store the connection handle in the search context
    ** to make sure we reuse it in the next_entry iteration
    ** Indeed, if another thread on this connection detects a problem
    ** on this connection, it may reallocate a new connection and
    ** a call to get_connection may return a new cnx. Too bad.
    */

    ctx->ld = ld;
    ctx->cnx = cnx;

    /* heart-beat management */
    if (cb->max_idle_time > 0) {
        endtime = slapi_current_utc_time() + cb->max_idle_time;
    }

    rc = ldap_search_ext(ld, target, scope, filter, attrs, attrsonly,
                         ctrls, NULL, &timeout, sizelimit, &(ctx->msgid));

    ldap_controls_free(ctrls);

    if (LDAP_SUCCESS != rc) {
        cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, ldap_err2string(rc), 0, NULL);
        cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
        slapi_ch_free((void **)&ctx);
        return 1;
    }

    /*
    ** Need to get the very first result to handle
    ** errors properly, especially no search base.
    */

    doit = 1;
    while (doit) {
        if (cb_check_forward_abandon(cb, pb, ctx->ld, ctx->msgid)) {
            slapi_ch_free((void **)&ctx);
            return 1;
        }

        rc = ldap_result(ld, ctx->msgid, LDAP_MSG_ONE, &cb->abandon_timeout, &res);
        switch (rc) {
        case -1:
            /* An error occurred. return now */
            rc = slapi_ldap_get_lderrno(ld, NULL, NULL);
            /* tuck away some errors in a OPERATION_ERROR */
            if (CB_LDAP_CONN_ERROR(rc)) {
                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                    ldap_err2string(rc), 0, NULL);
            } else {
                cb_send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
            }
            cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
            ldap_msgfree(res);
            slapi_ch_free((void **)&ctx);
            return 1;

        case 0:
            /* Local timeout management */
            if (slapi_timespec_expire_check(&expire_time) == TIMER_EXPIRED) {
                slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                              "chainingdb_build_candidate_list - Local timeout expiration\n");
                cb_send_ldap_result(pb, LDAP_TIMELIMIT_EXCEEDED,
                                    NULL, NULL, 0, NULL);
                /* Force connection close */
                cb_release_op_connection(cb->pool, ld, 1);
                ldap_msgfree(res);
                slapi_ch_free((void **)&ctx);
                return 1;
            }
            /* heart-beat management */
            if ((rc = cb_ping_farm(cb, cnx, endtime)) != LDAP_SUCCESS) {
                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                    ldap_err2string(rc), 0, NULL);
                cb_release_op_connection(cb->pool, ld, CB_LDAP_CONN_ERROR(rc));
                ldap_msgfree(res);
                slapi_ch_free((void **)&ctx);
                return 1;
            }

#ifdef CB_YIELD
            DS_Sleep(PR_INTERVAL_NO_WAIT);
#endif
            break;

        case LDAP_RES_SEARCH_ENTRY:
        case LDAP_RES_SEARCH_REFERENCE:
            /* Some results received   */
            /* don't parse result here */
            ctx->pending_result = res;
            ctx->pending_result_type = rc;
            doit = 0;
            break;

        case LDAP_RES_SEARCH_RESULT:
            matched_msg = NULL;
            error_msg = NULL;
            referrals = NULL;
            serverctrls = NULL;
            parse_rc = ldap_parse_result(ld, res, &rc, &matched_msg,
                                         &error_msg, &referrals, &serverctrls, 0);
            if (parse_rc != LDAP_SUCCESS) {
                static int warned_parse_rc = 0;
                if (!warned_parse_rc && error_msg) {
                    slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                                  "%s%s%s\n",
                                  matched_msg ? matched_msg : "",
                                  (matched_msg && (*matched_msg != '\0')) ? ": " : "",
                                  error_msg);
                    warned_parse_rc = 1;
                }
                cb_send_ldap_result(pb, parse_rc, NULL, ENDUSERMSG, 0, NULL);
                rc = -1;
            } else if (rc != LDAP_SUCCESS) {
                static int warned_rc = 0;
                if (!warned_rc) {
                    slapi_ldap_get_lderrno(ctx->ld, &matched_msg, &error_msg);
                    slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                                  "%s%s%s\n",
                                  matched_msg ? matched_msg : "",
                                  (matched_msg && (*matched_msg != '\0')) ? ": " : "",
                                  error_msg);
                    warned_rc = 1;
                }
                cb_send_ldap_result(pb, rc, NULL, ENDUSERMSG, 0, NULL);
                /* BEWARE: matched_msg and error_msg points */
                /* to ld fields.                */
                matched_msg = NULL;
                error_msg = NULL;
                rc = -1;
            }

            slapi_ch_free_string(&matched_msg);
            slapi_ch_free_string(&error_msg);
            ldap_controls_free(serverctrls);
            charray_free(referrals);
            if (rc != LDAP_SUCCESS) {
                cb_release_op_connection(cb->pool, ld,
                                         CB_LDAP_CONN_ERROR(rc));
                ldap_msgfree(res);
                slapi_ch_free((void **)&ctx);
                return -1;
            }

            /* Store the msg in the ctx */
            /* Parsed in iterate.       */
            ctx->pending_result = res;
            ctx->pending_result_type = LDAP_RES_SEARCH_RESULT;
            doit = 0;
        }
    }
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);

    return 0;
}

/*
 * Return the next entry in the result set.  The entry is returned
 * in the pblock.
 * Returns 0 normally.  If -1 is returned, it means that some
 * exceptional condition, e.g. timelimit exceeded has occurred,
 * and this routine has sent a result to the client.  If zero
 * is returned and no entry is available in the PBlock, then
 * we've iterated through all the entries.
 */

int
chainingdb_next_search_entry(Slapi_PBlock *pb)
{

    Slapi_DN *target_sdn = NULL;
    int sizelimit, timelimit;
    int rc, parse_rc, retcode;
    int i, attrsonly;
    struct timespec expire_time;
    LDAPMessage *res = NULL;
    char *matched_msg, *error_msg;
    cb_searchContext *ctx = NULL;
    Slapi_Entry *entry;
    LDAPControl **serverctrls = NULL;
    char **referrals = NULL;
    cb_backend_instance *cb = NULL;
    Slapi_Backend *be;
    time_t endtime = 0;
    Slapi_Operation *op;

    matched_msg = error_msg = NULL;

    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ctx);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);
    slapi_pblock_get(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &target_sdn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_operation_time_expiry(op, (time_t)timelimit, &expire_time);
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);

    cb = cb_get_instance(be);

    if (NULL == ctx) {
        /* End of local search */
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
        slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                      "chainingdb_next_search_entry - Unexpected NULL ctx in chainingdb_next_search_entry\n");
        return 0;
    }

    if (NULL != ctx->readahead) {
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, ctx->readahead);
        if (ctx->tobefreed != ctx->readahead) {
            slapi_entry_free(ctx->tobefreed);
        }
        ctx->tobefreed = ctx->readahead;
        ctx->readahead = NULL;
        cb_set_acl_policy(pb);
        return 0;
    }

    if (NULL != ctx->tobefreed) {
        slapi_entry_free(ctx->tobefreed);
        ctx->tobefreed = NULL;
    }

    if (ctx->type == CB_SEARCHCONTEXT_ENTRY) {

        int n;
        Slapi_Entry **ptr;
        if (slapi_timespec_expire_check(&expire_time) == TIMER_EXPIRED) {
            cb_send_ldap_result(pb, LDAP_TIMELIMIT_EXCEEDED, NULL, NULL, 0, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);

            for (n = 0, ptr = (Slapi_Entry **)ctx->data; ptr != NULL && ptr[n] != NULL; n++) {
                slapi_entry_free(ptr[n]);
            }
            if (ctx->data) {
                slapi_ch_free((void **)&ctx->data);
            }
            slapi_ch_free((void **)&ctx);
            return -1;
        }

        /*
        ** Return the Slapi_Entry of the result set one
        ** by one
        */

        for (n = 0, ptr = (Slapi_Entry **)ctx->data; ptr != NULL && ptr[n] != NULL; n++)
            ;
        if (n != 0) {
            Slapi_Entry *anEntry = ptr[n - 1];
            ptr[n - 1] = NULL;
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, anEntry);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);
            cb_set_acl_policy(pb);
            ctx->tobefreed = anEntry;
        } else {
            slapi_ch_free((void **)&ctx);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
        }
        return 0;
    }

    /*
     * Grab a connection handle. Should be the same as the one
     * used in the build_candidate list. To be certain of that, grab it from
     * the context.
     */

    /* Poll the server for the results of the search operation.
     * Passing LDAP_MSG_ONE indicates that you want to receive
     * the entries one at a time, as they come in.  If the next
     * entry that you retrieve is NULL, there are no more entries.
     */

    /* heart-beat management */
    if (cb->max_idle_time > 0) {
        endtime = slapi_current_utc_time() + cb->max_idle_time;
    }

    while (1) {

        if (cb_check_forward_abandon(cb, pb, ctx->ld, ctx->msgid)) {
            /* cnx handle released */
            ldap_msgfree(ctx->pending_result);
            slapi_ch_free((void **)&ctx);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
            return -1;
        }

        /* Check for time limit done by the remote farm server */
        /* Check for size limit done by the remote farm server */

        /* Use pending msg if one is available */
        if (ctx->pending_result) {
            res = ctx->pending_result;
            rc = ctx->pending_result_type;
            ctx->pending_result = NULL;
        } else {


            rc = ldap_result(ctx->ld, ctx->msgid,
                             LDAP_MSG_ONE, &cb->abandon_timeout, &res);
        }

        /* The server can return three types of results back to the client,
              * and the return value of ldap_result() indicates the result type:
              * LDAP_RES_SEARCH_ENTRY identifies an entry found by the search,
              * LDAP_RES_SEARCH_REFERENCE identifies a search reference returned
              * by the server, and LDAP_RES_SEARCH_RESULT is the last result
              * sent from the server to the client after the operation completes.
              * We need to check for each of these types of results.
         */

        switch (rc) {
        case -1:

            /* An error occurred. */
            rc = slapi_ldap_get_lderrno(ctx->ld, NULL, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);

            cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, ldap_err2string(rc), 0, NULL);

            ldap_msgfree(res);
            cb_release_op_connection(cb->pool, ctx->ld, CB_LDAP_CONN_ERROR(rc));
            slapi_ch_free((void **)&ctx);
            return -1;
        case 0:
            /* heart-beat management */
            if ((rc = cb_ping_farm(cb, ctx->cnx, endtime)) != LDAP_SUCCESS) {

                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);

                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                    ldap_err2string(rc), 0, NULL);

                ldap_msgfree(res);
                cb_release_op_connection(cb->pool, ctx->ld, CB_LDAP_CONN_ERROR(rc));
                slapi_ch_free((void **)&ctx);
                return -1;
            }
#ifdef CB_YIELD
            DS_Sleep(PR_INTERVAL_NO_WAIT);
#endif
            break;

        case LDAP_RES_SEARCH_ENTRY:

            /* heart-beat management */
            if (cb->max_idle_time > 0) {
                endtime = slapi_current_utc_time() + cb->max_idle_time;
            }

            /* The server sent one of the entries found by the search */
            if ((entry = cb_LDAPMessage2Entry(ctx->ld, res, attrsonly)) == NULL) {
                slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM, "chainingdb_next_search_entry - Invalid entry received.\n");
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);

                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);

                ldap_msgfree(res);
                cb_release_op_connection(cb->pool, ctx->ld, 0);
                slapi_ch_free((void **)&ctx);
                return -1;
            }

            ctx->tobefreed = entry;
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, entry);
            cb_set_acl_policy(pb);
            ldap_msgfree(res);
            return 0;

        case LDAP_RES_SEARCH_REFERENCE:

            /* The server sent a search reference encountered during the
             * search operation.
             */

            /* heart-beat management */
            if (cb->max_idle_time > 0) {
                endtime = slapi_current_utc_time() + cb->max_idle_time;
            }

            parse_rc = ldap_parse_reference(ctx->ld, res, &referrals, NULL, 1);
            if (parse_rc != LDAP_SUCCESS) {
                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                    ldap_err2string(parse_rc), 0, NULL);
                cb_release_op_connection(cb->pool, ctx->ld, CB_LDAP_CONN_ERROR(parse_rc));
                slapi_ch_free((void **)&ctx);

                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
                return -1;
            }

            /*
             * build a dummy entry on the fly with a ref attribute
             */
            {
                struct berval bv;
                int i;
                struct berval *bvals[2];
                Slapi_Entry *anEntry = slapi_entry_alloc();
                slapi_entry_set_sdn(anEntry, target_sdn);

                bvals[1] = NULL;
                bvals[0] = &bv;

                bv.bv_val = "referral";
                bv.bv_len = strlen(bv.bv_val);
                slapi_entry_add_values(anEntry, "objectclass", bvals);

                for (i = 0; referrals && referrals[i] != NULL; i++) {
                    bv.bv_val = referrals[i];
                    bv.bv_len = strlen(bv.bv_val);
                    slapi_entry_add_values(anEntry, "ref", bvals);
                }

                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ctx);
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, anEntry);
                cb_set_acl_policy(pb);
            }
            slapi_ldap_value_free(referrals);

            return 0;

        case LDAP_RES_SEARCH_RESULT:

            /* Parse the final result received from the server. Note the last
             * argument is a non-zero value, which indicates that the
             * LDAPMessage structure will be freed when done.
             */

            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);

            parse_rc = ldap_parse_result(ctx->ld, res,
                                         &rc, &matched_msg, &error_msg, &referrals, &serverctrls, 1);
            if (parse_rc != LDAP_SUCCESS) {
                static int warned_parse_rc = 0;
                if (!warned_parse_rc) {
                    slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                                  "%s%s%s\n",
                                  matched_msg ? matched_msg : "",
                                  (matched_msg && (*matched_msg != '\0')) ? ": " : "",
                                  ldap_err2string(parse_rc));
                    warned_parse_rc = 1;
                }
                cb_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                    ENDUSERMSG, 0, NULL);
                retcode = -1;
            } else if (rc != LDAP_SUCCESS) {
                static int warned_rc = 0;
                slapi_ldap_get_lderrno(ctx->ld, &matched_msg, &error_msg);
                if (!warned_rc) {
                    slapi_log_err(SLAPI_LOG_ERR, CB_PLUGIN_SUBSYSTEM,
                                  "%s%s%s\n",
                                  matched_msg ? matched_msg : "",
                                  (matched_msg && (*matched_msg != '\0')) ? ": " : "",
                                  error_msg);
                    warned_rc = 1;
                }
                cb_send_ldap_result(pb, rc, matched_msg, ENDUSERMSG, 0, NULL);

                /* BEWARE: Don't free matched_msg && error_msg */
                /* Points to the ld fields               */
                matched_msg = NULL;
                error_msg = NULL;
                retcode = -1;
            } else {
                /* Add control response sent by the farm server */
                for (i = 0; serverctrls && serverctrls[i]; i++)
                    slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, serverctrls[i]);
                retcode = 0;
            }

            ldap_controls_free(serverctrls);
            slapi_ch_free_string(&matched_msg);
            slapi_ch_free_string(&error_msg);
            charray_free(referrals);

            cb_release_op_connection(cb->pool, ctx->ld, 0);
            slapi_ch_free((void **)&ctx);
            return retcode;

        default:
            slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                          "chainingdb_next_search_entry - default case.\n");
        }
    }

    /* Not reached */
    /* return 0; */
}

void
chaining_back_search_results_release(void **sr)
{
    cb_searchContext *ctx = (cb_searchContext *)(*sr);

    slapi_log_err(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                  "chaining_back_search_results_release\n");
    if (ctx == NULL) {
        /* The paged search is already complete, just return */
        /* Could we have a ctx state flag instead? */
        return;
    }

    if (ctx->readahead != ctx->tobefreed) {
        slapi_entry_free(ctx->readahead);
    }
    slapi_entry_free(ctx->tobefreed);
    ctx->tobefreed = NULL;
    slapi_ch_free((void **)&ctx->data);
    slapi_ch_free((void **)&ctx);
    return;
}

void
chainingdb_prev_search_results(Slapi_PBlock *pb)
{
    cb_searchContext *ctx = NULL;
    Slapi_Entry *entry = NULL;

    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ctx);
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &entry);
    if (ctx) {
        ctx->readahead = entry;
    }
    return;
}
