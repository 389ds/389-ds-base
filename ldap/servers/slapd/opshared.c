/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* opshared.c - functions shared between regular and internal operations */

#include "log.h"
#include "slap.h"

#ifdef SYSTEMTAP
#include <sys/sdt.h>
#endif

#define PAGEDRESULTS_PAGE_END 1
#define PAGEDRESULTS_SEARCH_END 2

/* helper functions */
static void compute_limits(Slapi_PBlock *pb);

/* attributes that no clients are allowed to add or modify */
static char *protected_attrs_all[] = {PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
                                      NULL};
static char *pwpolicy_lock_attrs_all[] = {"passwordRetryCount",
                                          "retryCountResetTime",
                                          "accountUnlockTime",
                                          NULL};
/* Forward declarations */
static void compute_limits(Slapi_PBlock *pb);
static int send_results_ext(Slapi_PBlock *pb, int send_result, int *nentries, int pagesize, unsigned int *pr_stat);
static int process_entry(Slapi_PBlock *pb, Slapi_Entry *e, int send_result);
static void send_entry(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Operation *operation, char **attrs, int attrsonly, int *pnentries);

int
op_shared_is_allowed_attr(const char *attr_name, int replicated_op)
{
    int i;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* ONREPL - should allow backends to plugin here to specify
                attributes that are not allowed */

    if (!replicated_op) {
        struct asyntaxinfo *asi;
        int no_user_mod = 0;

        /* check list of attributes that no client is allowed to specify */
        for (i = 0; protected_attrs_all[i]; i++) {
            if (strcasecmp(attr_name, protected_attrs_all[i]) == 0) {
                /* this attribute is not allowed */
                return 0;
            }
        }
        /*
         * check to see if attribute is marked as one clients can't modify
         */

        asi = attr_syntax_get_by_name(attr_name, 0);
        if (NULL != asi &&
            0 != (asi->asi_flags & SLAPI_ATTR_FLAG_NOUSERMOD)) {
            /* this attribute is not allowed */
            no_user_mod = 1;
        }
        attr_syntax_return(asi);

        if (no_user_mod) {
            return (0);
        }
    } else if (!slapdFrontendConfig->pw_is_global_policy) {
        /* check list of password policy attributes for locking accounts */
        for (i = 0; pwpolicy_lock_attrs_all[i]; i++) {
            if (strcasecmp(attr_name, pwpolicy_lock_attrs_all[i]) == 0) {
                /* this attribute is not allowed */
                return 0;
            }
        }
    }

    /* this attribute is ok */
    return 1;
}


static ps_service_fn_ptr ps_service_fn = NULL;

void
do_ps_service(Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype, ber_int_t chgnum)
{
    if (NULL == ps_service_fn) {
        if (get_entry_point(ENTRY_POINT_PS_SERVICE, (caddr_t *)(&ps_service_fn)) < 0) {
            return;
        }
    }
    (ps_service_fn)(e, eprev, chgtype, chgnum);
}

void
modify_update_last_modified_attr(Slapi_PBlock *pb, Slapi_Mods *smods)
{
    char buf[SLAPI_TIMESTAMP_BUFSIZE];
    char *plugin_dn = NULL;
    char *binddn = NULL;
    struct berval bv;
    struct berval *bvals[2];
    Operation *op;
    struct slapdplugin *plugin = NULL;
    struct slapi_componentid *cid = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    slapi_log_err(SLAPI_LOG_TRACE, "modify_update_last_modified_attr", "=>\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    bvals[0] = &bv;
    bvals[1] = NULL;

    if (slapdFrontendConfig->plugin_track) {
        /* plugin bindDN tracking is enabled, grab the bind dn from thread local storage */
        if (slapi_sdn_isempty(&op->o_sdn)) {
            bv.bv_val = "";
            bv.bv_len = 0;
        } else {
            slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &cid);
            if (cid) {
                plugin = (struct slapdplugin *)cid->sci_plugin;
            } else {
                slapi_pblock_get(pb, SLAPI_PLUGIN, &plugin);
            }
            if (plugin)
                plugin_dn = plugin_get_dn(plugin);
            if (plugin_dn) {
                bv.bv_val = plugin_dn;
                bv.bv_len = strlen(bv.bv_val);
            } else {
                bv.bv_val = (char *)slapi_sdn_get_dn(&op->o_sdn);
                bv.bv_len = strlen(bv.bv_val);
            }
        }
        slapi_mods_add_modbvps(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES,
                               "internalModifiersName", bvals);
        slapi_ch_free_string(&plugin_dn);

        /* Grab the thread data(binddn) */
        slapi_td_get_dn(&binddn);

        if (binddn == NULL) {
            /* anonymous bind */
            bv.bv_val = "";
            bv.bv_len = 0;
        } else {
            bv.bv_val = binddn;
            bv.bv_len = strlen(bv.bv_val);
        }
    } else {
        /* fill in modifiersname */
        if (slapi_sdn_isempty(&op->o_sdn)) {
            bv.bv_val = "";
            bv.bv_len = 0;
        } else {
            bv.bv_val = (char *)slapi_sdn_get_dn(&op->o_sdn);
            bv.bv_len = strlen(bv.bv_val);
        }
    }

    slapi_mods_add_modbvps(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES,
                           "modifiersname", bvals);

    /* fill in modifytimestamp */
    slapi_timestamp_utc_hr(buf, SLAPI_TIMESTAMP_BUFSIZE);

    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    slapi_mods_add_modbvps(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES,
                           "modifytimestamp", bvals);
}

/*
 * If the attribute is one of the last mod attributes return 1,
 * otherwise return 0;
 */
int
slapi_attr_is_last_mod(char *attr)
{
    if (strcasecmp(attr, "modifytimestamp") == 0 ||
        strcasecmp(attr, "modifiersname") == 0 ||
        strcasecmp(attr, "internalmodifytimestamp") == 0 ||
        strcasecmp(attr, "internalmodifiersname") == 0) {
        return 1;
    }
    return 0;
}

/* The reference on the target_entry (base search) is stored in the operation
 * This is to prevent additional cache find/return that require cache lock.
 *
 * The target entry is acquired during be->be_search (when building the candidate list).
 * and is returned once the operation completes (or fail).
 *
 * The others entries sent back to the client have been acquired/returned during send_results_ext.
 * If the target entry is sent back to the client it is not returned (refcnt--) during the send_results_ext.
 *
 * This function only returns (refcnt-- in the entry cache) the target_entry (base search).
 * It is called at the operation level (op_shared_search)
 *
 */
static void
cache_return_target_entry(Slapi_PBlock *pb, Slapi_Backend *be, Slapi_Operation *operation)
{
    if (operation_get_target_entry(operation) && be->be_entry_release) {
        (*be->be_entry_release)(pb, operation_get_target_entry(operation));
        operation_set_target_entry(operation, NULL);
        operation_set_target_entry_id(operation, 0);
    }
}
/*
 * Returns: 0    - if the operation is successful
 *        < 0    - if operation fails.
 * Note that an operation is considered "failed" if a result is sent
 * directly to the client when send_result is 0.
 */
void
op_shared_search(Slapi_PBlock *pb, int send_result)
{
    char *base = NULL;
    const char *normbase = NULL;
    char *fstr;
    int scope;
    Slapi_Backend *be = NULL;
    Slapi_Backend *be_single = NULL;
    Slapi_Backend *be_list[BE_LIST_SIZE + 1];
    Slapi_Entry *referral_list[BE_LIST_SIZE + 1];
    char attrlistbuf[1024], *attrliststr, **attrs = NULL;
    int rc = 0;
    int internal_op;
    Slapi_DN *basesdn = NULL;
    Slapi_DN monitorsdn = {0};
    Slapi_DN *sdn = NULL;
    Slapi_Operation *operation = NULL;
    Slapi_Entry *referral = NULL;
    char *proxydn = NULL;
    char *proxystr = NULL;
    int proxy_err = LDAP_SUCCESS;
    char *errtext = NULL;
    int nentries, pnentries;
    int flag_search_base_found = 0;
    int flag_no_such_object = 0;
    int flag_referral = 0;
    int flag_psearch = 0;
    int err_code = LDAP_SUCCESS;
    LDAPControl **ctrlp;
    struct berval *ctl_value = NULL;
    int iscritical = 0;
    char *be_name = NULL;
    int index = 0;
    int sent_result = 0;
    unsigned int pr_stat = 0;
    Connection *pb_conn;

    ber_int_t pagesize = -1;
    ber_int_t estimate = 0; /* estimated search result set size */
    int curr_search_count = 0;
    Slapi_Backend *pr_be = NULL;
    void *pr_search_result = NULL;
    int pr_idx = -1;
    Slapi_DN *orig_sdn = NULL;
    int free_sdn = 0;

    be_list[0] = NULL;
    referral_list[0] = NULL;

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, op_shared_search__entry);
#endif

    /* get search parameters */
    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &base);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);

    if (NULL == sdn) {
        sdn = slapi_sdn_new_dn_byval(base);
        slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, sdn);
        free_sdn = 1;
    } else {
        /* save it so we can restore it later - may have to replace it internally
       e.g. for onelevel and subtree searches, but need to restore it */
        orig_sdn = sdn;
    }
    normbase = slapi_sdn_get_dn(sdn);

    if (base && (strlen(base) > 0) && (NULL == normbase)) {
        /* normalization failed */
        op_shared_log_error_access(pb, "SRCH", base, "invalid dn");
        send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL, "invalid dn", 0, NULL);
        rc = -1;
        goto free_and_return_nolock;
    }
    basesdn = slapi_sdn_dup(sdn);

    slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
    slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &fstr);
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);

    if (operation == NULL) {
        op_shared_log_error_access(pb, "SRCH", base, "NULL operation");
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "NULL operation", 0, NULL);
        rc = -1;
        goto free_and_return_nolock;
    }
    
    /* Set the time we actually started the operation */
    slapi_operation_set_time_started(operation);

    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);
    flag_psearch = operation_is_flag_set(operation, OP_FLAG_PS);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    /* get the proxy auth dn if the proxy auth control is present */
    proxy_err = proxyauth_get_dn(pb, &proxydn, &errtext);

    if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
        char fmtstr[SLAPI_ACCESS_LOG_FMTBUF];
        uint64_t connid;
        int32_t op_id;
        int32_t op_internal_id;
        int32_t op_nested_count;

        PR_ASSERT(fstr);
        if (internal_op) {
            get_internal_conn_op(&connid, &op_id, &op_internal_id, &op_nested_count);
        }

        if (NULL == attrs) {
            attrliststr = "ALL";
        } else {
            strarray2str(attrs, attrlistbuf, sizeof(attrlistbuf),
                         1 /* include quotes */);
            attrliststr = attrlistbuf;
        }

        if (proxydn) {
            proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
        }

#define SLAPD_SEARCH_FMTSTR_CONN_OP "conn=%" PRIu64 " op=%d"
#define SLAPD_SEARCH_FMTSTR_CONN_OP_INT_INT "conn=Internal(%" PRIu64 ") op=%d(%d)(%d)"
#define SLAPD_SEARCH_FMTSTR_CONN_OP_EXT_INT "conn=%" PRIu64 " (Internal) op=%d(%d)(%d)"
#define SLAPD_SEARCH_FMTSTR_REMAINDER "%s%s\n"

#define SLAPD_SEARCH_BUFPART 512
#define LOG_ACCESS_FORMAT_BUFSIZ(arg, logstr, bufsiz) ((strlen(arg)) < (bufsiz) ? (logstr "%s") : \
                                                                       (logstr "%." STRINGIFYDEFINE(bufsiz) "s..."))
/* Define a separate macro for attributes because when we strip it we should take care of the quotes */
#define LOG_ACCESS_FORMAT_ATTR_BUFSIZ(arg, logstr, bufsiz) ((strlen(arg)) < (bufsiz) ? (logstr "%s") : \
                                                                            (logstr "%." STRINGIFYDEFINE(bufsiz) "s...\""))

        /*
        * slapi_log_access() throws away log lines that are longer than
        * 2048 characters, so we limit the filter, base and attrs strings to 512
        * (better to log something rather than nothing)
        */
        if (!internal_op) {
            strcpy(fmtstr, SLAPD_SEARCH_FMTSTR_CONN_OP);
        } else {
            if (connid == 0) {
                strcpy(fmtstr, SLAPD_SEARCH_FMTSTR_CONN_OP_INT_INT);
            } else {
                strcpy(fmtstr, SLAPD_SEARCH_FMTSTR_CONN_OP_EXT_INT);
            }
        }
        strcat(fmtstr, LOG_ACCESS_FORMAT_BUFSIZ(normbase, " SRCH base=\"", SLAPD_SEARCH_BUFPART));
        strcat(fmtstr, LOG_ACCESS_FORMAT_BUFSIZ(fstr, "\" scope=%d filter=\"", SLAPD_SEARCH_BUFPART));
        strcat(fmtstr, LOG_ACCESS_FORMAT_ATTR_BUFSIZ(attrliststr, "\" attrs=", SLAPD_SEARCH_BUFPART));
        strcat(fmtstr, SLAPD_SEARCH_FMTSTR_REMAINDER);

        if (!internal_op) {
            slapi_log_access(LDAP_DEBUG_STATS, fmtstr,
                             pb_conn->c_connid,
                             operation->o_opid,
                             normbase,
                             scope, fstr, attrliststr,
                             flag_psearch ? " options=persistent" : "",
                             proxystr ? proxystr : "");
        } else {
            slapi_log_access(LDAP_DEBUG_ARGS, fmtstr,
                             connid,
                             op_id,
                             op_internal_id,
                             op_nested_count,
                             normbase,
                             scope, fstr, attrliststr,
                             flag_psearch ? " options=persistent" : "",
                             proxystr ? proxystr : "");
        }
    }

    /* If we encountered an error parsing the proxy control, return an error
     * to the client.  We do this here to ensure that we log the operation first.
     */
    if (proxy_err != LDAP_SUCCESS) {
        rc = -1;
        send_ldap_result(pb, proxy_err, NULL, errtext, 0, NULL);
        goto free_and_return_nolock;
    }

    /* target spec is used to decide which plugins are applicable for
     * the operation.  basesdn is duplicated and set to target spec.
     */
    operation_set_target_spec(operation, basesdn);

    /*
     * this is time to check if mapping tree specific control was used to
     * specify that we want to parse only one backend.
     */
    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
    if (ctrlp) {
        if (slapi_control_present(ctrlp, MTN_CONTROL_USE_ONE_BACKEND_EXT_OID,
                                  &ctl_value, &iscritical))
        {
            /* this control is the smart version of MTN_CONTROL_USE_ONE_BACKEND_OID,
             * it works out for itself what back end is required (thereby relieving
             * the client of working out which backend it needs) by looking at the
             * base of the search if no value is supplied
             */

            if ((ctl_value->bv_len != 0) && ctl_value->bv_val) {
                be_name = ctl_value->bv_val;
            } else {
                /* we don't need no steenkin values */
                Slapi_Backend *searchbe = slapi_be_select(sdn);

                if (searchbe && searchbe != defbackend_get_backend()) {
                    be_name = slapi_be_get_name(searchbe);
                }
            }
        } else {
            if (slapi_control_present(ctrlp, MTN_CONTROL_USE_ONE_BACKEND_OID,
                                      &ctl_value, &iscritical)) {
                if ((ctl_value->bv_len == 0) || (ctl_value->bv_val == NULL)) {
                    rc = -1;
                    if (iscritical)
                        send_ldap_result(pb, LDAP_UNAVAILABLE_CRITICAL_EXTENSION, NULL, NULL, 0, NULL);
                    else
                        send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
                    goto free_and_return_nolock;
                } else {
                    be_name = ctl_value->bv_val;
                    if (be_name == NULL) {
                        rc = -1;
                        if (iscritical)
                            send_ldap_result(pb, LDAP_UNAVAILABLE_CRITICAL_EXTENSION, NULL, NULL, 0, NULL);
                        else
                            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
                        goto free_and_return_nolock;
                    }
                }
            }
        }
    }

    if (be_name == NULL) {
        char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
        /* no specific backend was requested, use the mapping tree */
        errorbuf[0] = '\0';
        err_code = slapi_mapping_tree_select_all(pb, be_list, referral_list, errorbuf, sizeof(errorbuf));
        if (((err_code != LDAP_SUCCESS) && (err_code != LDAP_OPERATIONS_ERROR) && (err_code != LDAP_REFERRAL)) || ((err_code == LDAP_OPERATIONS_ERROR) && (be_list[0] == NULL))) {
            send_ldap_result(pb, err_code, NULL, errorbuf, 0, NULL);
            rc = -1;
            goto free_and_return;
        }
        if (be_list[0] != NULL) {
            index = 0;
            while (be_list[index] && be_list[index + 1]) {
                index++;
            }
            be = be_list[index];
        } else {
            be = NULL;
        }
    } else {
        /* specific backend be_name was requested, use slapi_be_select_by_instance_name */
        be_single = be = slapi_be_select_by_instance_name(be_name);
        if (be_single) {
            slapi_be_Rlock(be_single);
        }
        be_list[0] = NULL;
        referral_list[0] = NULL;
        referral = NULL;
    }

    /* Handle the rest of the controls. */
    if (ctrlp) {
        if (slapi_control_present(ctrlp, LDAP_CONTROL_GET_EFFECTIVE_RIGHTS,
                                  &ctl_value, &iscritical)) {
            operation->o_flags |= OP_FLAG_GET_EFFECTIVE_RIGHTS;
        }

        if (slapi_control_present(ctrlp, LDAP_CONTROL_PAGEDRESULTS,
                                  &ctl_value, &iscritical)) {
            int pr_cookie = -1;
            /* be is set only when this request is new. otherwise, prev be is honored. */
            rc = pagedresults_parse_control_value(pb, ctl_value, &pagesize, &pr_idx, be);
            /* Let's set pr_idx even if it fails; in case, pr_idx == -1. */
            slapi_pblock_set(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
            slapi_pblock_set(pb, SLAPI_PAGED_RESULTS_COOKIE, &pr_cookie);
            if ((LDAP_SUCCESS == rc) || (LDAP_CANCELLED == rc) || (0 == pagesize)) {
                op_set_pagedresults(operation);
                pr_be = pagedresults_get_current_be(pb_conn, pr_idx);
                if (be_name) {
                    if (pr_be != be_single) {
                        slapi_be_Unlock(be_single);
                        be_single = be = pr_be;
                        slapi_be_Rlock(be_single);
                    }
                } else if (be_list[0]) {
                    if (pr_be) { /* PAGED RESULT: be is found from the previous paging. */
                        /* move the index in the be_list which matches pr_be */
                        index = 0;
                        while (be_list[index] && be_list[index + 1] && pr_be != be_list[index]) {
                            index++;
                        }
                        be = be_list[index];
                    }
                }
                pr_search_result = pagedresults_get_search_result(pb_conn, operation, 0 /*not locked*/, pr_idx);
                estimate = pagedresults_get_search_result_set_size_estimate(pb_conn, operation, pr_idx);
                /* Set operation note flags as required. */
                if (pagedresults_get_unindexed(pb_conn, operation, pr_idx)) {
                    slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);
                }
                slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_SIMPLEPAGED);

                if ((LDAP_CANCELLED == rc) || (0 == pagesize)) {
                    /* paged-results-request was abandoned; making an empty cookie. */
                    pagedresults_set_response_control(pb, 0, estimate, -1, pr_idx);
                    send_ldap_result(pb, 0, NULL,
                                     "Simple Paged Results Search abandoned",
                                     0, NULL);
                    rc = LDAP_SUCCESS;
                    goto free_and_return;
                }
            } else if (LDAP_UNWILLING_TO_PERFORM == rc) {
                send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                                 "Simple Paged Results Search exceeded administration limit",
                                 0, NULL);
                goto free_and_return;
            } else {
                /* parse paged-results-control failed */
                if (iscritical) { /* return an error since it's critical */
                    send_ldap_result(pb, LDAP_UNAVAILABLE_CRITICAL_EXTENSION, NULL,
                                     "Simple Paged Results Search failed",
                                     0, NULL);
                    goto free_and_return;
                }
            }
        }
    }

    slapi_pblock_set(pb, SLAPI_BACKEND_COUNT, &index);

    if (be) {
        slapi_pblock_set(pb, SLAPI_BACKEND, be);

        /* adjust time and size limits */
        compute_limits(pb);

        /* set the timelimit to clean up the too-long-lived-paged results requests */
        if (op_is_pagedresults(operation)) {
            int32_t tlimit;
            slapi_pblock_get(pb, SLAPI_SEARCH_TIMELIMIT, &tlimit);
            pagedresults_set_timelimit(pb_conn, operation, (time_t)tlimit, pr_idx);
        }

        /*
         * call the pre-search plugins. if they succeed, call the backend
         * search function. then call the post-search plugins.
         */
        /* ONREPL - should regular plugin be called for internal searches ? */
        if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_SEARCH_FN) == 0) {
            slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
            set_db_default_result_handlers(pb);

            /* Now would be a good time to call the search rewriters for computed attrs */
            rc = compute_rewrite_search_filter(pb);
            switch (rc) {
            case 1: /* A rewriter says that we should refuse to perform this search.
                       The potential exists that we will refuse to perform a search
                       which we were going to refer, perhaps to a server which would
                       be willing to perform the search. That's bad. The rewriter
                       could be clever enough to spot this and do the right thing though. */
                send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Search not supported", 0, NULL);
                rc = -1;
                goto free_and_return;

            case -2: /* memory was allocated */
                /* take note of any changes */
                slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
                slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
                if (NULL == sdn) {
                    send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                                     "target dn is lost", 0, NULL);
                    rc = -1;
                    goto free_and_return;
                }
                if (slapi_sdn_compare(basesdn, sdn)) {
                    slapi_sdn_free(&basesdn);
                    basesdn = operation_get_target_spec(operation);
                    slapi_sdn_free(&basesdn);
                    basesdn = slapi_sdn_dup(sdn);
                    operation_set_target_spec(operation, basesdn);
                }
                break;

            case -1:
            case 0: /* OK */
                break;

            case 2: /* Operations error */
                send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "search rewriter", 0, NULL);
                rc = -1;
                goto free_and_return;
            }

        } else {
            /*
             * A pre-operation plugin handled this search. Grab the return code
             * (it may have been set by a plugin) and return.
             *
             * In DS 5.x, the following two lines of code did not exist, which
             * means a pre-search function might return a non-zero value (which
             * indicates that a result was returned to the client) but the code
             * below would execute the search anyway. This was a regression from
             * the documented plugin API behavior (and from DS 3.x and 4.x).
             */
            slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
            goto free_and_return;
        }
    }

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, op_shared_search__prepared);
#endif

    nentries = 0;
    rc = -1; /* zero backends would mean failure */
    while (be) {
        const Slapi_DN *be_suffix;
        int err = 0;
        Slapi_Backend *next_be = NULL;

        if (be->be_search == NULL) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Function not implemented", 0, NULL);
            rc = -1;
            goto free_and_return;
        }

        pnentries = 0;

        /* the backends returns no such object when a
         * search is attempted in a node above their nsslapd-suffix
         * this is correct but a bit annoying when a backends
         * is below another backend because in that case the
         * such searches should sometimes succeed
         * To allow this we therefore have to change the
         * SLAPI_SEARCH_TARGET_SDN parameter in the pblock
         *
         * Also when we climb down the mapping tree we have to
         * change ONE-LEVEL searches to BASE
         */

        /* that means we only support one suffix per backend */
        be_suffix = slapi_be_getsuffix(be, 0);

        if (be_list[0] == NULL) {
            next_be = NULL;
        } else {
            index--;
            if (index >= 0)
                next_be = be_list[index];
            else
                next_be = NULL;
        }

        if (op_is_pagedresults(operation) && pr_search_result) {
            void *sr = NULL;
            /* PAGED RESULTS and already have the search results from the prev op */
            pagedresults_lock(pb_conn, pr_idx);
            /*
             * In async paged result case, the search result might be released
             * by other theads.  We need to double check it in the locked region.
             */
            pthread_mutex_lock(&(pb_conn->c_mutex));
            pr_search_result = pagedresults_get_search_result(pb_conn, operation, 1 /*locked*/, pr_idx);
            if (pr_search_result) {
                if (pagedresults_is_abandoned_or_notavailable(pb_conn, 1 /*locked*/, pr_idx)) {
                    pagedresults_unlock(pb_conn, pr_idx);
                    /* Previous operation was abandoned and the simplepaged object is not in use. */
                    send_ldap_result(pb, 0, NULL, "Simple Paged Results Search abandoned", 0, NULL);
                    rc = LDAP_SUCCESS;
                    pthread_mutex_unlock(&(pb_conn->c_mutex));
                    goto free_and_return;
                } else {
                    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, pr_search_result);
                    rc = send_results_ext(pb, 1, &pnentries, pagesize, &pr_stat);

                    /* search result could be reset in the backend/dse */
                    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);
                    pagedresults_set_search_result(pb_conn, operation, sr, 1 /*locked*/, pr_idx);
                }
            } else {
                pr_stat = PAGEDRESULTS_SEARCH_END;
                rc = LDAP_SUCCESS;
            }
            pthread_mutex_unlock(&(pb_conn->c_mutex));
            pagedresults_unlock(pb_conn, pr_idx);

            if ((PAGEDRESULTS_SEARCH_END == pr_stat) || (0 == pnentries)) {
                /* no more entries to send in the backend */
                if (NULL == next_be) {
                    /* no more entries && no more backends */
                    curr_search_count = -1;
                } else {
                    curr_search_count = pnentries;
                }
                estimate = 0;
                pr_stat = PAGEDRESULTS_SEARCH_END; /* make sure stat is SEARCH_END */
            } else {
                curr_search_count = pnentries;
                estimate -= estimate ? curr_search_count : 0;
            }
            pagedresults_set_response_control(pb, 0, estimate,
                                              curr_search_count, pr_idx);
            if (pagedresults_get_with_sort(pb_conn, operation, pr_idx)) {
                sort_make_sort_response_control(pb, CONN_GET_SORT_RESULT_CODE, NULL);
            }
            pagedresults_set_search_result_set_size_estimate(pb_conn,
                                                             operation,
                                                             estimate, pr_idx);
            if (PAGEDRESULTS_SEARCH_END == pr_stat) {
                pagedresults_lock(pb_conn, pr_idx);
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
                if (!pagedresults_is_abandoned_or_notavailable(pb_conn, 0 /*not locked*/, pr_idx)) {
                    pagedresults_free_one(pb_conn, operation, pr_idx);
                }
                pagedresults_unlock(pb_conn, pr_idx);
                if (next_be) {
                    /* no more entries, but at least another backend */
                    if (pagedresults_set_current_be(pb_conn, next_be, pr_idx, 0) < 0) {
                        goto free_and_return;
                    }
                }
                next_be = NULL; /* to break the loop */
            } else if (PAGEDRESULTS_PAGE_END == pr_stat) {
                next_be = NULL; /* to break the loop */
            }
        } else {
            /* be_suffix null means that we are searching the default backend
             * -> don't change the search parameters in pblock
             * Also, we skip this block for 'cn=monitor' search and its subsearches
             * as they are done by callbacks from monitor.c */
            slapi_sdn_init_dn_byref(&monitorsdn, "cn=monitor");
            if (!((be_suffix == NULL) || slapi_sdn_issuffix(basesdn, &monitorsdn))) {
                if ((be_name == NULL) && (scope == LDAP_SCOPE_ONELEVEL)) {
                    /* one level searches
                     * - depending on the suffix of the backend we might have to
                     *   do a one level search or a base search
                     * - we might also have to change the search target
                     */
                    if (slapi_sdn_isparent(basesdn, be_suffix) ||
                        (slapi_sdn_get_ndn_len(basesdn) == 0)) {
                        int tmp_scope = LDAP_SCOPE_BASE;
                        slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &tmp_scope);

                        if (free_sdn) {
                            slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
                            slapi_sdn_free(&sdn);
                        }
                        sdn = slapi_sdn_dup(be_suffix);
                        slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, (void *)sdn);
                        free_sdn = 1;
                    } else if (slapi_sdn_issuffix(basesdn, be_suffix)) {
                        int tmp_scope = LDAP_SCOPE_ONELEVEL;
                        slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &tmp_scope);
                    } else {
                        slapi_sdn_done(&monitorsdn);
                        goto next_be;
                    }
                }

                /* subtree searches :
                 * if the search was started above the backend suffix
                 * - temporarily set the SLAPI_SEARCH_TARGET_SDN to the
                 *   base of the node so that we don't get a NO SUCH OBJECT error
                 * - do not change the scope
                 */
                if (scope == LDAP_SCOPE_SUBTREE) {
                    if (slapi_sdn_issuffix(be_suffix, basesdn)) {
                        if (free_sdn) {
                            slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
                            slapi_sdn_free(&sdn);
                        }
                        sdn = slapi_sdn_dup(be_suffix);
                        slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, (void *)sdn);
                        free_sdn = 1;
                    }
                }
            }
            slapi_sdn_done(&monitorsdn);
            slapi_pblock_set(pb, SLAPI_BACKEND, be);
            slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);

            /* ONREPL - we need to be able to tell the backend not to send results directly */
            rc = (*be->be_search)(pb);
            switch (rc) {
            case 1:
                /* if the backend returned LDAP_NO_SUCH_OBJECT for a SEARCH request,
                 * it will not have sent back a result - otherwise, it will have
                 * sent a result */
                rc = SLAPI_FAIL_GENERAL;
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &err);
                if (err == LDAP_NO_SUCH_OBJECT) {
                    /* may be the object exist somewhere else
                     * wait the end of the loop to send back this error
                     */
                    flag_no_such_object = 1;
                } else {
                    /* err something other than LDAP_NO_SUCH_OBJECT, so the backend will
                     * have sent the result -
                     * Set a flag here so we don't return another result. */
                    sent_result = 1;
                }
                /* fall through */

            case -1: /* an error occurred */
                /* PAGED RESULTS */
                if (op_is_pagedresults(operation)) {
                    /* cleanup the slot */
                    pthread_mutex_lock(&(pb_conn->c_mutex));
                    pagedresults_set_search_result(pb_conn, operation, NULL, 1, pr_idx);
                    rc = pagedresults_set_current_be(pb_conn, NULL, pr_idx, 1);
                    pthread_mutex_unlock(&(pb_conn->c_mutex));
                }
                if (1 == flag_no_such_object) {
                    break;
                }
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &err);
                if (err == LDAP_NO_SUCH_OBJECT) {
                    /* may be the object exist somewhere else
                     * wait the end of the loop to send back this error
                     */
                    flag_no_such_object = 1;
                    break;
                } else {
                    /* for error other than LDAP_NO_SUCH_OBJECT
                     * the error has already been sent
                     * stop the search here
                     */
                    cache_return_target_entry(pb, be, operation);
                    goto free_and_return;
                }

            /* when rc == SLAPI_FAIL_DISKFULL this case is executed */

            case SLAPI_FAIL_DISKFULL:
                operation_out_of_disk_space();
                cache_return_target_entry(pb, be, operation);
                goto free_and_return;

            case 0: /* search was successful and we need to send the result */
                flag_search_base_found++;
                rc = send_results_ext(pb, 1, &pnentries, pagesize, &pr_stat);

                /* PAGED RESULTS */
                if (op_is_pagedresults(operation)) {
                    void *sr = NULL;
                    int with_sort = operation->o_flags & OP_FLAG_SERVER_SIDE_SORTING;

                    curr_search_count = pnentries;
                    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);
                    if ((PAGEDRESULTS_SEARCH_END == pr_stat) || (0 == pnentries)) {
                        /* no more entries, but at least another backend */
                        pthread_mutex_lock(&(pb_conn->c_mutex));
                        pagedresults_set_search_result(pb_conn, operation, NULL, 1, pr_idx);
                        be->be_search_results_release(&sr);
                        rc = pagedresults_set_current_be(pb_conn, next_be, pr_idx, 1);
                        pthread_mutex_unlock(&(pb_conn->c_mutex));
                        pr_stat = PAGEDRESULTS_SEARCH_END; /* make sure stat is SEARCH_END */
                        if (NULL == next_be) {
                            /* no more entries && no more backends */
                            curr_search_count = -1;
                        } else if (rc < 0) {
                            cache_return_target_entry(pb, be, operation);
                            goto free_and_return;
                        }
                    } else {
                        curr_search_count = pnentries;
                        slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate);
                        pagedresults_lock(pb_conn, pr_idx);
                        if ((pagedresults_set_current_be(pb_conn, be, pr_idx, 0) < 0) ||
                            (pagedresults_set_search_result(pb_conn, operation, sr, 0, pr_idx) < 0) ||
                            (pagedresults_set_search_result_count(pb_conn, operation, curr_search_count, pr_idx) < 0) ||
                            (pagedresults_set_search_result_set_size_estimate(pb_conn, operation, estimate, pr_idx) < 0) ||
                            (pagedresults_set_with_sort(pb_conn, operation, with_sort, pr_idx) < 0)) {
                            pagedresults_unlock(pb_conn, pr_idx);
                            cache_return_target_entry(pb, be, operation);
                            goto free_and_return;
                        }
                        pagedresults_unlock(pb_conn, pr_idx);
                    }
                    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
                    next_be = NULL; /* to break the loop */
                    if (operation->o_status & SLAPI_OP_STATUS_ABANDONED) {
                        /* It turned out this search was abandoned. */
                        pthread_mutex_lock(&(pb_conn->c_mutex));
                        pagedresults_free_one_msgid_nolock(pb_conn, operation->o_msgid);
                        pthread_mutex_unlock(&(pb_conn->c_mutex));
                        /* paged-results-request was abandoned; making an empty cookie. */
                        pagedresults_set_response_control(pb, 0, estimate, -1, pr_idx);
                        send_ldap_result(pb, 0, NULL, "Simple Paged Results Search abandoned", 0, NULL);
                        rc = LDAP_SUCCESS;
                        cache_return_target_entry(pb, be, operation);
                        goto free_and_return;
                    }
                    pagedresults_set_response_control(pb, 0, estimate, curr_search_count, pr_idx);
                    if (curr_search_count == -1) {
                        pagedresults_free_one(pb_conn, operation, pr_idx);
                    }
                }

                /* if rc != 0 an error occurred while sending back the entries
                 * to the LDAP client
                 * LDAP error should already have been sent to the client
                 * stop the search, free and return
                 */
                if (rc != 0) {
                    cache_return_target_entry(pb, be, operation);
                    goto free_and_return;
                }
                break;
            }
            /* cache return the target_entry */
            cache_return_target_entry(pb, be, operation);
        }

        nentries += pnentries;

    next_be:
        be = next_be; /* this be won't be used for PAGED_RESULTS */
    }

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, op_shared_search__backends);
#endif

    /* if referrals were sent back by the mapping tree
   * add them to the list of referral in the pblock instead
   * of searching the backend
   */
    index = 0;
    while ((referral = referral_list[index++]) != NULL) {
        slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
        if (err_code == LDAP_REFERRAL) {
            send_referrals_from_entry(pb, referral);
            goto free_and_return;
        } else {
            if (process_entry(pb, referral, 1)) {
                flag_referral++;
            } else {
                /* Manage DSA was set, referral must be sent as an entry */
                int attrsonly;
                char **dsa_attrs = NULL;

                slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &dsa_attrs);
                slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
                slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, referral);
                switch (send_ldap_search_entry(pb, referral, NULL, dsa_attrs, attrsonly)) {
                case 0:
                    flag_search_base_found++;
                    nentries++;
                    break;
                case 1:  /* entry not sent */
                case -1: /* connection closed */
                    break;
                }
            }
        }
    }

    if (flag_search_base_found || flag_referral) {
        rc = 0;
    }

    /* ONREPL - we currently call postop only if operation is successful;
     We should always send result and pass error code to the plugin */
    if (rc == 0) {
        plugin_call_plugins(pb, SLAPI_PLUGIN_POST_SEARCH_FN);
    } else {
        plugin_call_plugins(pb, SLAPI_PLUGIN_POST_SEARCH_FAIL_FN);
    }

    if (send_result) {
        if (rc == 0) {
            /* at least one backend returned something and there was no critical error
       * from the LDAP client point of view the search was successful
       */
            struct berval **urls = NULL;

            slapi_pblock_get(pb, SLAPI_SEARCH_REFERRALS, &urls);
            send_ldap_result(pb, err_code, NULL, NULL, nentries, urls);
        } else if (flag_no_such_object) {
            /* there was at least 1 backend that was called to process
       * the operation and all backends returned NO SUCH OBJECTS.
       * Don't send the result if it's already been handled above.
       */
            if (!sent_result) {
                slapi_send_ldap_result_from_pb(pb);
            }
        } else {
            /* No backend was found in the mapping tree to process
       * the operation : return NO SUCH OBJECT
       */
            send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL);
        }
    } else {
        /* persistent search: ignore error locating base entry */
        rc = 0;
    }

free_and_return:
    if ((be_list[0] != NULL) || (referral_list[0] != NULL)) {
        slapi_mapping_tree_free_all(be_list, referral_list);
    } else if (be_single) {
        slapi_be_Unlock(be_single);
    }
    if (rc > 0) {
        /* rc > 0 means a plugin generated an error and we should abort */
        send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
    }

free_and_return_nolock:
    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);

    if (free_sdn) {
        slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
        slapi_sdn_free(&sdn);
    }
    slapi_sdn_free(&basesdn);
    /* coverity false positive:
     *  var_deref_model: Passing null pointer "orig_sdn" to "slapi_pblock_set", which dereferences it.
     */
    /* coverity[var_deref_model] */
    slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, orig_sdn);

    slapi_ch_free_string(&proxydn);
    slapi_ch_free_string(&proxystr);

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, op_shared_search__return);
#endif
}

/* Returns 1 if this processing on this entry is finished
 * and doesn't need to be sent.
 */
static int
process_entry(Slapi_PBlock *pb, Slapi_Entry *e, int send_result)
{
    int managedsait;
    Slapi_Attr *a = NULL;
    int numValues = 0, i;

    if (!send_result) {
        /* server requested that we don't send results to the client,
           for instance, in case of a persistent search
         */
        return 1;
    }

    /* ONREPL - check if the entry should be referred (because of the copyingFrom) */

    /*
     * If this is a referral, and the managedsait control is not present,
     * arrange for a referral to be sent.  For v2 connections,
     * the referrals are just squirreled away and sent with the
     * final result.  For v3, the referrals are sent in separate LDAP messages.
     */
    slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
    if (!managedsait && slapi_entry_attr_find(e, "ref", &a) == 0) {
        /* to fix 522189: when rootDSE, don't interpret attribute ref as a referral entry  */

        if (slapi_is_rootdse(slapi_entry_get_dn_const(e)))
            return 0; /* more to do for this entry, e.g., send it back to the client */

        /* end fix */
        slapi_attr_get_numvalues(a, &numValues);
        if (numValues == 0) {
            slapi_log_err(SLAPI_LOG_ERR, "process_entry", "NULL ref in (%s)\n",
                          slapi_entry_get_dn_const(e));
        } else {
            Slapi_Value *val = NULL;
            struct berval **refscopy = NULL;
            struct berval **urls, **tmpUrls = NULL;
            tmpUrls = (struct berval **)slapi_ch_malloc((numValues + 1) * sizeof(struct berval *));
            for (i = slapi_attr_first_value(a, &val); i != -1;
                 i = slapi_attr_next_value(a, i, &val)) {
                tmpUrls[i] = (struct berval *)slapi_value_get_berval(val);
            }
            tmpUrls[numValues] = NULL;
            refscopy = ref_adjust(pb, tmpUrls, slapi_entry_get_sdn_const(e), 1);
            slapi_pblock_get(pb, SLAPI_SEARCH_REFERRALS, &urls);
            send_ldap_referral(pb, e, refscopy, &urls);
            slapi_pblock_set(pb, SLAPI_SEARCH_REFERRALS, urls);
            if (NULL != refscopy) {
                ber_bvecfree(refscopy);
                refscopy = NULL;
            }
            slapi_ch_free((void **)&tmpUrls);
        }

        return 1; /* done with this entry */
    }

    return 0;
}

static void
send_entry(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Operation *operation, char **attrs, int attrsonly, int *pnentries)
{
    /*
     * It's a regular entry, or it's a referral and
     * managedsait control is on.  In either case, send
     * the entry.
     */
    switch (send_ldap_search_entry(pb, e, NULL, attrs, attrsonly)) {
    case 0: /* entry sent ok */
        (*pnentries)++;
        slapi_pblock_set(pb, SLAPI_NENTRIES, pnentries);
        break;
    case 1: /* entry not sent */
        break;
    case -1: /* connection closed */
        /*
         * mark the operation as abandoned so the backend
         * next entry function gets called again and has
         * a chance to clean things up.
         */
        operation->o_status = SLAPI_OP_STATUS_ABANDONED;
        break;
    }
}

/* Loops through search entries and sends them to the client.
 * returns -1 on error or 1 if result packet wasn't sent.
 * This function never returns 0 because it doesn't send
 * the result packet back with the last entry like
 * iterate_with_lookahead trys to do.
 */
static int
iterate(Slapi_PBlock *pb, Slapi_Backend *be, int send_result, int *pnentries, int pagesize, unsigned int *pr_statp)
{
    int rc;
    int rval = 1; /* no error, by default */
    int attrsonly;
    int done = 0;
    Slapi_Entry *e = NULL;
    char **attrs = NULL;
    unsigned int pr_stat = 0;
    int pr_idx = -1;

    if (NULL == pb) {
        return rval;
    }
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);

    *pnentries = 0;

    while (!done) {
        Slapi_Entry *ger_template_entry = NULL;
        Slapi_Operation *operation;

        slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
        rc = be->be_next_search_entry(pb);
        if (rc < 0) {
            /*
             * Some exceptional condition occurred. Results have been sent,
             * so we're finished.
             */
            if (rc == SLAPI_FAIL_DISKFULL) {
                operation_out_of_disk_space();
            }
            pr_stat = PAGEDRESULTS_SEARCH_END;
            rval = -1;
            done = 1;
            continue;
        }

        slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &e);

        /* Check for possible get_effective_rights control */
        if (operation->o_flags & OP_FLAG_GET_EFFECTIVE_RIGHTS) {
            char *errbuf = NULL;

            if (PAGEDRESULTS_PAGE_END == pr_stat) {
                /*
                 * read ahead -- there is at least more entry.
                 * undo it and return the PAGE_END
                 */
                be->be_prev_search_results(pb);
                done = 1;
                continue;
            }
            if ( e == NULL ) {
                char **gerattrs = NULL;
                char **gerattrsdup = NULL;
                char **gap = NULL;
                char *gapnext = NULL;
                /* we have no more entries
                 * but we might create a template entry for GER
                 * so we need to continue, but make sure to stop
                 * after handling the template entry.
                 * the template entry is a temporary entry returned by the acl
                 * plugin in the pblock and will be freed
                 */
                done = 1;
                pr_stat = PAGEDRESULTS_SEARCH_END;

                slapi_pblock_get(pb, SLAPI_SEARCH_GERATTRS, &gerattrs);
                gerattrsdup = cool_charray_dup(gerattrs);
                gap = gerattrsdup;
                while (gap && *gap) {
                    gapnext = NULL;
                    if (*(gap + 1)) {
                        gapnext = *(gap + 1);
                        *(gap + 1) = NULL;
                    }
                    slapi_pblock_set(pb, SLAPI_SEARCH_GERATTRS, gap);
                    rc = plugin_call_acl_plugin(pb, e, attrs, NULL,
                                                SLAPI_ACL_ALL, ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS,
                                                &errbuf);
                    if (NULL != gapnext) {
                        *(gap + 1) = gapnext;
                    }
                    gap++;
                    /* get the template entry, if any */
                    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &e);
                    if (NULL == e) {
                        /* everything is ok - don't send the result */
                        continue;
                    }
                    ger_template_entry = e;
                    if (rc != LDAP_SUCCESS) {
                        /* Send error result and
                       abort op if the control is critical */
                        slapi_log_err(SLAPI_LOG_ERR, "iterate",
                                      "Failed to get effective rights for entry (%s), rc=%d\n",
                                      slapi_entry_get_dn_const(e), rc);
                        send_ldap_result(pb, rc, NULL, errbuf, 0, NULL);
                        rval = -1;
                    } else {
                        if (!process_entry(pb, e, send_result)) {
                            /* should send this entry now*/
                            send_entry(pb, e, operation, attrs, attrsonly, pnentries);
                        }
                    }

                    slapi_ch_free((void **)&errbuf);
                    if (ger_template_entry) {
                        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
                        slapi_entry_free(ger_template_entry);
                        ger_template_entry = e = NULL;
                    }
                } /* while ger template */
                slapi_pblock_set(pb, SLAPI_SEARCH_GERATTRS, gerattrs);
                cool_charray_free(gerattrsdup);
            } else {
                /* we are processing geteffective rights for an existing entry */
                rc = plugin_call_acl_plugin(pb, e, attrs, NULL,
                                            SLAPI_ACL_ALL, ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS,
                                             &errbuf);
                if (rc != LDAP_SUCCESS) {
                    /* Send error result and
                       abort op if the control is critical */
                    slapi_log_err(SLAPI_LOG_ERR, "iterate",
                                  "Failed to get effective rights for entry (%s), rc=%d\n",
                                  slapi_entry_get_dn_const(e), rc);
                    send_ldap_result(pb, rc, NULL, errbuf, 0, NULL);
                    rval = -1;
                } else {
                    if (!process_entry(pb, e, send_result)) {
                        /* should send this entry now*/
                        send_entry(pb, e, operation, attrs, attrsonly, pnentries);
                        if (pagesize == *pnentries) {
                            /* PAGED RESULTS: reached the pagesize */
                            /* We don't set "done = 1" here.
                             * We read ahead next entry to check whether there is
                             * more entries to return or not. */
                            pr_stat = PAGEDRESULTS_PAGE_END;
                        }
                    }
                }
                slapi_ch_free((void **)&errbuf);
            }
        /* not GET_EFFECTIVE_RIGHTS */
        } else if (e) {
                if (PAGEDRESULTS_PAGE_END == pr_stat) {
                    /*
                 * read ahead -- there is at least more entry.
                 * undo it and return the PAGE_END
                 */
                    be->be_prev_search_results(pb);
                    done = 1;
                    continue;
                }
                /* Adding shadow password attrs. */
                add_shadow_ext_password_attrs(pb, &e);
                if (!process_entry(pb, e, send_result)) {
                    /*this entry was not sent, do it now*/
                    send_entry(pb, e, operation, attrs, attrsonly, pnentries);
                    if (pagesize == *pnentries) {
                        /* PAGED RESULTS: reached the pagesize */
                        /* We don't set "done = 1" here.
                         * We read ahead next entry to check whether there is
                         * more entries to return or not. */
                        pr_stat = PAGEDRESULTS_PAGE_END;
                    }
                }
                /* cleanup pw entry . sent or not */
                struct slapi_entry *pb_pw_entry = slapi_pblock_get_pw_entry(pb);
                slapi_entry_free(pb_pw_entry);
                slapi_pblock_set_pw_entry(pb, NULL);
        } else {
            /* no more entries */
            done = 1;
            pr_stat = PAGEDRESULTS_SEARCH_END;
        }
    }

    if (pr_statp) {
        *pr_statp = pr_stat;
    }
    return rval;
}

static int timelimit_reslimit_handle = -1;
static int sizelimit_reslimit_handle = -1;
static int pagedsizelimit_reslimit_handle = -1;

/*
 * Register size and time limit with the binder-based resource limits
 * subsystem. A SLAPI_RESLIMIT_STATUS_... code is returned.
 */
int
search_register_reslimits(void)
{
    int rc1, rc2, rc3;

    rc1 = slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                  "nsSizeLimit", &sizelimit_reslimit_handle);
    rc2 = slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                  "nsTimeLimit", &timelimit_reslimit_handle);
    rc3 = slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                  "nsPagedSizeLimit", &pagedsizelimit_reslimit_handle);

    return (rc1 != SLAPI_RESLIMIT_STATUS_SUCCESS) ? rc1 : ((rc2 != SLAPI_RESLIMIT_STATUS_SUCCESS) ? rc2 : rc3);
}


/*
 * Compute size and time limits based on the connection (bind identity).
 * Binder-based resource limits get top priority, followed by those associated
 * with the backend we are using.
 *
 * If the binder is the root DN and there is nothing in the root DN's entry
 * to say otherwise, no limits are used.  Otherwise, the lower of the limit
 * that was sent in the LDAP request and that available based on the
 * connection bind identity or configured backend limit is used.
 */
static void
compute_limits(Slapi_PBlock *pb)
{
    int timelimit, sizelimit;
    int requested_timelimit, max_timelimit, requested_sizelimit, max_sizelimit;
    int isroot;
    int isCertAuth;
    Slapi_ComponentId *component_id = NULL;
    Slapi_Backend *be;
    Slapi_Operation *op;
    Connection *pb_conn;

    slapi_pblock_get(pb, SLAPI_SEARCH_TIMELIMIT, &requested_timelimit);
    slapi_pblock_get(pb, SLAPI_SEARCH_SIZELIMIT, &requested_sizelimit);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);


    /* If the search belongs to the client authentication process, take the value at
     * nsslapd-timelimit as the actual time limit.
     */

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &component_id);
    if (component_id) {
        isCertAuth = (!strcasecmp(component_id->sci_component_name, COMPONENT_CERT_AUTH)) ? 1 : 0;
        if (isCertAuth) {
            timelimit = config_get_timelimit();
            goto set_timelimit;
        }
    }

    /*
     * Compute the time limit.
     */
    if (isroot) {
        max_timelimit = -1; /* no limit */
    } else  if (slapi_reslimit_get_integer_limit(pb_conn,
        timelimit_reslimit_handle, &max_timelimit) != SLAPI_RESLIMIT_STATUS_SUCCESS)
    {
        /*
         * no limit associated with binder/connection or some other error
         * occurred.  use the default maximum.
         */
        max_timelimit = be->be_timelimit;
    }

    if (requested_timelimit) {
        /* requested limit should be applied to all (including root) */
        if (isroot) {
            timelimit = requested_timelimit;
        } else if ((max_timelimit == -1) ||
                   (requested_timelimit < max_timelimit)) {
            timelimit = requested_timelimit;
        } else {
            timelimit = max_timelimit;
        }
    } else if (isroot) {
        timelimit = -1; /* no limit */
    } else {
        timelimit = max_timelimit;
    }

set_timelimit:
    slapi_pblock_set(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);


    /*
     * Compute the size limit.
     */
    if (isroot) {
        max_sizelimit = -1; /* no limit */
    } else {
        if (slapi_reslimit_get_integer_limit(pb_conn,
            sizelimit_reslimit_handle, &max_sizelimit) != SLAPI_RESLIMIT_STATUS_SUCCESS)
        {
            /*
             * no limit associated with binder/connection or some other error
             * occurred.  use the default maximum.
             */
            max_sizelimit = be->be_sizelimit;
        }

        if (op_is_pagedresults(op)) {
            if (slapi_reslimit_get_integer_limit(pb_conn,
                pagedsizelimit_reslimit_handle, &max_sizelimit) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
                /*
                 * no limit associated with binder/connection or some other error
                 * occurred.  use the default maximum.
                 */
                if (be->be_pagedsizelimit) {
                    max_sizelimit = be->be_pagedsizelimit;
                }
                /* else was already set above */
            }
        }
    }

    if (requested_sizelimit) {
        /* requested limit should be applied to all (including root) */
        if (isroot) {
            sizelimit = requested_sizelimit;
        } else if ((max_sizelimit == -1) ||
                   (requested_sizelimit < max_sizelimit)) {
            sizelimit = requested_sizelimit;
        } else {
            sizelimit = max_sizelimit;
        }
    } else if (isroot) {
        sizelimit = -1; /* no limit */
    } else {
        sizelimit = max_sizelimit;
    }
    slapi_pblock_set(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);

    slapi_log_err(SLAPI_LOG_TRACE, "compute_limits",
                  "=> sizelimit=%d, timelimit=%d\n",
                  sizelimit, timelimit);
}

/* Iterates through results and send them to the client.
 * Returns 0 if successful and -1 otherwise
 */
static int
send_results_ext(Slapi_PBlock *pb, int send_result, int *nentries, int pagesize, unsigned int *pr_stat)
{
    Slapi_Backend *be;
    int rc;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    if (be->be_next_search_entry == NULL) {
        /* we need to send the result, but the function to iterate through
           the result set is not implemented */
        /* ONREPL - log error */
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Search not supported", 0, NULL);
        return -1;
    }

    /* Iterate through the returned result set */
    /* if (be->be_next_search_entry_ext != NULL)
    {
    */
    /* The iterate look ahead is causing a whole mess with the ACL.
    ** the entries are now visiting the ACL land in a random way
    ** and not the ordered way it was before. Until we figure out
    ** let's not change the behavior.
    **
    ** Don't use iterate_with_lookahead because it sends the result
    * in the same times as the entry and this can cause failure
    * of the mapping tree scanning algorithme
    * if (getFrontendConfig()->result_tweak)
    * {
    *    rc = iterate_with_lookahead(pb, be, send_result, nentries);
    * } else {
    */
    rc = iterate(pb, be, send_result, nentries, pagesize, pr_stat);
    /*
        }
    } else { // if (be->be_next_search_entry_ext != NULL)
        rc = iterate(pb, be, send_result, nentries, pagesize, pr_stat);
    }
    */

    switch (rc) {
    case -1: /* an error occured */

    case 0: /* everything is ok - result is sent */
        /* If this happens we are dead but hopefully iterate
                     * never sends the result itself
                     */
        break;

    case 1: /* everything is ok - don't send the result */
        rc = 0;
    }

    return rc;
}

void
op_shared_log_error_access(Slapi_PBlock *pb, const char *type, const char *dn, const char *msg)
{
    char *proxydn = NULL;
    char *proxystr = NULL;
    Slapi_Operation *operation;
    Connection *pb_conn;

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    if ((proxyauth_get_dn(pb, &proxydn, NULL) == LDAP_SUCCESS)) {
        proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
    }

    slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d %s dn=\"%s\"%s, %s\n",
                     (pb_conn ? pb_conn->c_connid : 0),
                     (operation ? operation->o_opid : 0),
                     type,
                     dn,
                     proxystr ? proxystr : "",
                     msg ? msg : "");

    slapi_ch_free_string(&proxydn);
    slapi_ch_free_string(&proxystr);
}
