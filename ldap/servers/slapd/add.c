/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"
#include "pratom.h"
#include "csngen.h"

/* Forward declarations */
static int add_internal_pb(Slapi_PBlock *pb);
static void op_shared_add(Slapi_PBlock *pb);
static int add_created_attrs(Slapi_PBlock *pb, Slapi_Entry *e);
static int check_rdn_for_created_attrs(Slapi_Entry *e);
static void handle_fast_add(Slapi_PBlock *pb, Slapi_Entry *entry);
static int add_uniqueid(Slapi_Entry *e);
static PRBool check_oc_subentry(Slapi_Entry *e, struct berval **vals, char *normtype);

/* This function is called to process operation that come over external connections */
void
do_add(Slapi_PBlock *pb)
{
    Slapi_Operation *operation;
    BerElement *ber;
    char *last;
    ber_len_t len = LBER_ERROR;
    ber_tag_t tag;
    Slapi_Entry *e = NULL;
    int err;
    int rc;
    PRBool searchsubentry = PR_TRUE;
    Connection *pb_conn = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "do_add", "==>\n");

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);


    if (operation == NULL || pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "do_add", "NULL param: pb_conn (0x%p) pb_op (0x%p)\n",
                      pb_conn, operation);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "param error", 0, NULL);
        return;
    }
    ber = operation->o_ber;

    /* count the add request */
    slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsAddEntryOps);

    /*
     * Parse the add request.  It looks like this:
     *
     *    AddRequest := [APPLICATION 14] SEQUENCE {
     *        name    DistinguishedName,
     *        attrs    SEQUENCE OF SEQUENCE {
     *            type    AttributeType,
     *            values    SET OF AttributeValue
     *        }
     *    }
     */
    /* get the name */
    {
        char *rawdn = NULL;
        Slapi_DN mysdn;
        if (ber_scanf(ber, "{a", &rawdn) == LBER_ERROR) {
            slapi_ch_free_string(&rawdn);
            slapi_log_err(SLAPI_LOG_ERR, "do_add",
                          "ber_scanf failed (op=Add; params=DN)\n");
            op_shared_log_error_access(pb, "ADD", "???", "decoding error");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                             "decoding error", 0, NULL);
            return;
        }
        /* Check if we should be performing strict validation. */
        if (config_get_dn_validate_strict()) {
            /* check that the dn is formatted correctly */
            rc = slapi_dn_syntax_check(pb, rawdn, 1);
            if (rc) { /* syntax check failed */
                op_shared_log_error_access(pb, "ADD", rawdn ? rawdn : "",
                                           "strict: invalid dn");
                send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX,
                                 NULL, "invalid dn", 0, NULL);
                slapi_ch_free_string(&rawdn);
                return;
            }
        }
        slapi_sdn_init_dn_passin(&mysdn, rawdn);
        if (rawdn && (strlen(rawdn) > 0) &&
            (NULL == slapi_sdn_get_dn(&mysdn))) {
            /* normalization failed */
            op_shared_log_error_access(pb, "ADD", rawdn, "invalid dn");
            send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                             "invalid dn", 0, NULL);
            slapi_sdn_done(&mysdn);
            return;
        }
        e = slapi_entry_alloc();
        /* Responsibility for DN is passed to the Entry. */
        slapi_entry_init_ext(e, &mysdn, NULL);
        slapi_sdn_done(&mysdn);
    }
    slapi_log_err(SLAPI_LOG_ARGS, "do_add", "dn (%s)\n", (char *)slapi_entry_get_dn_const(e));

    /* get the attrs */
    for (tag = ber_first_element(ber, &len, &last);
         tag != LBER_DEFAULT && tag != LBER_END_OF_SEQORSET;
         tag = ber_next_element(ber, &len, last)) {
        char *type = NULL, *normtype = NULL;
        struct berval **vals = NULL;
        len = -1; /* reset - not used in loop */
        if (ber_scanf(ber, "{a{V}}", &type, &vals) == LBER_ERROR) {
            op_shared_log_error_access(pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), "decoding error");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                             "decoding error", 0, NULL);
            slapi_ch_free_string(&type);
            ber_bvecfree(vals);
            goto free_and_return;
        }

        if (vals == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "do_add", "no values for type %s\n", type);
            op_shared_log_error_access(pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), "null value");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL,
                             0, NULL);
            slapi_ch_free_string(&type);
            goto free_and_return;
        }

        normtype = slapi_attr_syntax_normalize(type);
        if (!normtype || !*normtype) {
            char ebuf[SLAPI_DSE_RETURNTEXT_SIZE];
            rc = LDAP_INVALID_SYNTAX;
            slapi_create_errormsg(ebuf, sizeof(ebuf), "invalid type '%s'", type);
            op_shared_log_error_access(pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), ebuf);
            send_ldap_result(pb, rc, NULL, ebuf, 0, NULL);
            slapi_ch_free_string(&type);
            slapi_ch_free((void **)&normtype);
            ber_bvecfree(vals);
            goto free_and_return;
        }
        slapi_ch_free_string(&type);

        /* for now we just ignore attributes that client is not allowed
          to modify so not to break existing clients */
        if (op_shared_is_allowed_attr(normtype, pb_conn->c_isreplication_session)) {
            if ((rc = slapi_entry_add_values(e, normtype, vals)) != LDAP_SUCCESS) {
                slapi_log_access(LDAP_DEBUG_STATS,
                                 "conn=%" PRIu64 " op=%d ADD dn=\"%s\", add values for type %s failed\n",
                                 pb_conn->c_connid, operation->o_opid,
                                 slapi_entry_get_dn_const(e), normtype);
                send_ldap_result(pb, rc, NULL, NULL, 0, NULL);

                slapi_ch_free((void **)&normtype);
                ber_bvecfree(vals);
                goto free_and_return;
            }

            /* if this is uniqueid attribute, set uniqueid field of the entry */
            if (strcasecmp(normtype, SLAPI_ATTR_UNIQUEID) == 0) {
                e->e_uniqueid = slapi_ch_strdup(vals[0]->bv_val);
            }
            if (searchsubentry)
                searchsubentry = check_oc_subentry(e, vals, normtype);
        }

        slapi_ch_free((void **)&normtype);
        ber_bvecfree(vals);
    }

    /* Ensure that created attributes are not used in the RDN. */
    if (check_rdn_for_created_attrs(e)) {
        op_shared_log_error_access(pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), "invalid DN");
        send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL, "illegal attribute in RDN", 0, NULL);
        goto free_and_return;
    }

    /* len, is ber_len_t, which is uint. Can't be -1. May be better to remove (len != 0) check */
    if ((tag != LBER_END_OF_SEQORSET) && (len != -1)) {
        op_shared_log_error_access(pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), "decoding error");
        send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                         "decoding error", 0, NULL);
        goto free_and_return;
    }

    /*
     * in LDAPv3 there can be optional control extensions on
     * the end of an LDAPMessage. we need to read them in and
     * pass them to the backend.
     */
    if ((err = get_ldapmessage_controls(pb, ber, NULL)) != 0) {
        op_shared_log_error_access(pb, "ADD", slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)),
                                   "failed to decode LDAP controls");
        send_ldap_result(pb, err, NULL, NULL, 0, NULL);
        goto free_and_return;
    }

    slapi_pblock_set(pb, SLAPI_REQUESTOR_ISROOT, &operation->o_isroot);
    slapi_pblock_set(pb, SLAPI_ADD_ENTRY, e);

    if (pb_conn->c_flags & CONN_FLAG_IMPORT) {
        /* this add is actually part of a bulk import -- punt */
        handle_fast_add(pb, e);
    } else {
        op_shared_add(pb);
    }

    /* make sure that we don't free entry if it is successfully added */
    e = NULL;

free_and_return:;
    if (e)
        slapi_entry_free(e);
}

/* This function is used to issue internal add operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_add_internal_pb instead */
Slapi_PBlock *
slapi_add_internal(const char *idn,
                   LDAPMod **iattrs,
                   LDAPControl **controls,
                   int dummy)
{
    Slapi_Entry *e;
    Slapi_PBlock *result_pb = NULL;
    int opresult = -1;

    if (iattrs == NULL) {
        opresult = LDAP_PARAM_ERROR;
        goto done;
    }

    opresult = slapi_mods2entry(&e, (char *)idn, iattrs);
    if (opresult != LDAP_SUCCESS) {
        goto done;
    }
    result_pb = slapi_add_entry_internal(e, controls, dummy);

done:
    if (result_pb == NULL) {
        result_pb = slapi_pblock_new();
        slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    }

    return result_pb;
}

/* This function is used to issue internal add operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_add_internal_pb instead
   Beware: The entry is consumed. */
Slapi_PBlock *
slapi_add_entry_internal(Slapi_Entry *e, LDAPControl **controls, int dummy __attribute__((unused)))
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_PBlock *result_pb = NULL;
    int opresult;

    slapi_add_entry_internal_set_pb(pb, e, controls, plugin_get_default_component_id(), 0);

    add_internal_pb(pb);

    result_pb = slapi_pblock_new();
    if (result_pb) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    }
    slapi_pblock_destroy(pb);

    return result_pb;
}

/*  This is new style API to issue internal add operation.
    pblock should contain the following data (can be set via call to slapi_add_internal_set_pb):
    SLAPI_TARGET_SDN    set to sdn of the new entry
    SLAPI_CONTROLS_ARG    set to request controls if present
    SLAPI_ADD_ENTRY        set to Slapi_Entry to add
    Beware: The entry is consumed. */
int
slapi_add_internal_pb(Slapi_PBlock *pb)
{
    if (pb == NULL)
        return -1;

    if (!allow_operation(pb)) {
        /* free the entry as it's expected to be consumed */
        Slapi_Entry *e;
        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
        slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
        slapi_entry_free(e);

        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                               "This plugin is not configured to access operation target data", 0, NULL);
        return 0;
    }

    return add_internal_pb(pb);
}

int
slapi_add_internal_set_pb(Slapi_PBlock *pb, const char *dn, LDAPMod **attrs, LDAPControl **controls, Slapi_ComponentId *plugin_identity, int operation_flags)
{
    Slapi_Entry *e;
    int rc;

    if (pb == NULL || dn == NULL || attrs == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, NULL, "slapi_add_internal_set_pb: invalid argument\n");
        return LDAP_PARAM_ERROR;
    }

    rc = slapi_mods2entry(&e, dn, attrs);
    if (rc == LDAP_SUCCESS) {
        slapi_add_entry_internal_set_pb(pb, e, controls, plugin_identity, operation_flags);
    }

    return rc;
}

/* Note: Passed entry e is going to be consumed. */
/* Initialize a pblock for a call to slapi_add_internal_pb() */
void
slapi_add_entry_internal_set_pb(Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **controls, Slapi_ComponentId *plugin_identity, int operation_flags)
{
    Operation *op;
    PR_ASSERT(pb != NULL);
    if (pb == NULL || e == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, NULL, "slapi_add_entry_internal_set_pb: invalid argument\n");
        return;
    }

    op = internal_operation_new(SLAPI_OPERATION_ADD, operation_flags);
    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    slapi_pblock_set(pb, SLAPI_ADD_ENTRY, e);
    slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
    slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

int
slapi_exists_or_add_internal(
    Slapi_DN *dn, const char *filter, const char *entry, const char *modifier_name
) {
    /* Search */
    Slapi_PBlock *search_pb = slapi_pblock_new();
    int search_result = 0;
    int search_nentries = 0;

    slapi_search_internal_set_pb_ext(search_pb,
        dn,
        LDAP_SCOPE_BASE,
        filter,
        NULL,
        0,
        NULL,
        NULL,
        plugin_get_default_component_id(),
        0);

    slapi_search_internal_pb(search_pb);

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
    if (search_result == LDAP_SUCCESS) {
        slapi_pblock_get(search_pb, SLAPI_NENTRIES, &search_nentries);
    }
    slapi_pblock_destroy(search_pb);

    slapi_log_error(SLAPI_LOG_DEBUG, "slapi_exists_or_add_internal", "search_internal result -> %d, %d\n", search_result, search_nentries);

    if (search_result != LDAP_SUCCESS) {
        return search_result;
    }

    /* Did it exist? */
    if (search_nentries == 0) {
        int create_result = 0;
        /* begin the create */
        slapi_log_error(SLAPI_LOG_DEBUG, "slapi_exists_or_add_internal", "creating entry:\n%s\n", entry);
        Slapi_Entry *s_entry = slapi_str2entry((char *)entry, 0);

        if (s_entry == NULL) {
            slapi_log_error(SLAPI_LOG_ERR, "slapi_exists_or_add_internal", "failed to parse entry\n");
            return -1;
        }

        /* Set modifiers name */
        slapi_entry_attr_set_charptr(s_entry, "internalModifiersname", modifier_name);

        /* do the add */
        Slapi_PBlock *add_pb = slapi_pblock_new();

        slapi_add_entry_internal_set_pb(add_pb, s_entry, NULL, plugin_get_default_component_id(), 0);
        slapi_add_internal_pb(add_pb);

        slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &create_result);
        slapi_pblock_destroy(add_pb);

        slapi_log_error(SLAPI_LOG_DEBUG, "slapi_exists_or_add_internal", "add_internal result -> %d\n", create_result);

        return create_result;
    }
    /* No action was taken */
    return LDAP_SUCCESS;
}

/* Helper functions */

static int
add_internal_pb(Slapi_PBlock *pb)
{
    LDAPControl **controls;
    Operation *op;
    int opresult = 0;
    Slapi_Entry *e;

    PR_ASSERT(pb != NULL);

    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

    if (e == NULL) {
        opresult = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        return 0;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data = &opresult;
    op->o_result_handler = internal_getresult_callback;

    slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);

    /* set parameters common to all internal operations */
    set_common_params(pb);

    /* set actions taken to process the operation */
    set_config_params(pb);

    /* perform the add operation */
    slapi_td_internal_op_start();
    op_shared_add(pb);
    slapi_td_internal_op_finish();

    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);

    return 0;
}

/* Code shared between regular and internal add operation */
static void
op_shared_add(Slapi_PBlock *pb)
{
    Slapi_Operation *operation;
    Slapi_Entry *e, *pse;
    Slapi_Backend *be = NULL;
    int err;
    int internal_op, repl_op, lastmod;
    char *pwdtype = NULL;
    Slapi_Attr *attr = NULL;
    Slapi_Entry *referral;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    struct slapdplugin *p = NULL;
    char *proxydn = NULL;
    char *proxystr = NULL;
    int proxy_err = LDAP_SUCCESS;
    char *errtext = NULL;
    Slapi_DN *sdn = NULL;
    passwdPolicy *pwpolicy;
    Connection *pb_conn = NULL;

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);
    pwpolicy = new_passwdPolicy(pb, slapi_entry_get_dn(e));

    /* Set the time we actually started the operation */
    slapi_operation_set_time_started(operation);

    /* target spec is used to decide which plugins are applicable for the operation */
    operation_set_target_spec(operation, slapi_entry_get_sdn(e));

    if ((err = slapi_entry_add_rdn_values(e)) != LDAP_SUCCESS) {
        send_ldap_result(pb, err, NULL, "failed to add RDN values", 0, NULL);
        goto done;
    }

    /* get the proxy auth dn if the proxy auth control is present */
    proxy_err = proxyauth_get_dn(pb, &proxydn, &errtext);

    if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
        if (proxydn) {
            proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
        }

        if (!internal_op) {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ADD dn=\"%s\"%s\n",
                             pb_conn ? pb_conn->c_connid : -1,
                             operation ? operation->o_opid: -1,
                             slapi_entry_get_dn_const(e),
                             proxystr ? proxystr : "");
        } else {
            uint64_t connid;
            int32_t op_id;
            int32_t op_internal_id;
            int32_t op_nested_count;
            get_internal_conn_op(&connid, &op_id, &op_internal_id, &op_nested_count);
            slapi_log_access(LDAP_DEBUG_ARGS,
                             connid==0 ? "conn=Internal(%" PRId64 ") op=%d(%d)(%d) ADD dn=\"%s\"\n" :
                                         "conn=%" PRId64 " (Internal) op=%d(%d)(%d) ADD dn=\"%s\"\n",
                             connid,
                             op_id,
                             op_internal_id,
                             op_nested_count,
                             slapi_entry_get_dn_const(e));
        }
    }

    /* If we encountered an error parsing the proxy control, return an error
     * to the client.  We do this here to ensure that we log the operation first. */
    if (proxy_err != LDAP_SUCCESS) {
        send_ldap_result(pb, proxy_err, NULL, errtext, 0, NULL);
        goto done;
    }

    /*
     * We could be serving multiple database backends.  Select the
     * appropriate one.
     */
    if ((err = slapi_mapping_tree_select(pb, &be, &referral, errorbuf, sizeof(errorbuf))) != LDAP_SUCCESS) {
        send_ldap_result(pb, err, NULL, errorbuf, 0, NULL);
        be = NULL;
        goto done;
    }

    if (referral) {
        int managedsait;

        slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
        if (managedsait) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "cannot update referral", 0, NULL);
            slapi_entry_free(referral);
            goto done;
        }

        slapi_pblock_set(pb, SLAPI_TARGET_SDN, (void *)operation_get_target_spec(operation));
        send_referrals_from_entry(pb, referral);
        slapi_entry_free(referral);
        goto done;
    }

    if (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
        Slapi_Value **unhashed_password_vals = NULL;
        Slapi_Value **present_values = NULL;

        /* Setting unhashed password to the entry extension. */
        if (repl_op) {
            /* replicated add ==> get unhashed pw from entry, if any.
             * set it to the extension */
            slapi_entry_attr_find(e, PSEUDO_ATTR_UNHASHEDUSERPASSWORD, &attr);
            if (attr) {
                present_values = attr_get_present_values(attr);
                valuearray_add_valuearray(&unhashed_password_vals,
                                          present_values, 0);
#if !defined(USE_OLD_UNHASHED)
                /* and remove it from the entry. */
                slapi_entry_attr_delete(e, PSEUDO_ATTR_UNHASHEDUSERPASSWORD);
#endif
            }
        } else {
            /* ordinary add ==>
             * get unhashed pw from userpassword before encrypting it */
            /* look for user password attribute */
            slapi_entry_attr_find(e, SLAPI_USERPWD_ATTR, &attr);
            if (attr) {
                Slapi_Value **vals = NULL;

                /* Set the backend in the pblock.
                 * The slapi_access_allowed function
                 * needs this set to work properly. */
                slapi_pblock_set(pb, SLAPI_BACKEND,
                                 slapi_be_select(slapi_entry_get_sdn_const(e)));

                /* Check ACI before checking password syntax */
                if ((err = slapi_access_allowed(pb, e, SLAPI_USERPWD_ATTR, NULL,
                                                SLAPI_ACL_ADD)) != LDAP_SUCCESS) {
                    send_ldap_result(pb, err, NULL,
                                     "Insufficient 'add' privilege to the "
                                     "'userPassword' attribute",
                                     0, NULL);
                    goto done;
                }

                /*
                 * Check password syntax, unless this is a pwd admin/rootDN
                 */
                present_values = attr_get_present_values(attr);
                if (!pw_is_pwp_admin(pb, pwpolicy) &&
                    check_pw_syntax(pb, slapi_entry_get_sdn_const(e),
                                    present_values, NULL, e, 0) != 0) {
                    /* error result is sent from check_pw_syntax */
                    goto done;
                }
                /* pw syntax is valid */
                valuearray_add_valuearray(&unhashed_password_vals,
                                          present_values, 0);
                valuearray_add_valuearray(&vals, present_values, 0);
                if (pw_encodevals_ext(pb, slapi_entry_get_sdn(e), vals) != 0) {
                    slapi_log_err(SLAPI_LOG_CRIT, "op_shared_add", "Unable to hash userPassword attribute for %s.\n", slapi_entry_get_dn_const(e));
                    send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Unable to store attribute \"userPassword\" correctly\n", 0, NULL);
                    goto done;
                }
                add_password_attrs(pb, operation, e);
                slapi_entry_attr_replace_sv(e, SLAPI_USERPWD_ATTR, vals);
                valuearray_free(&vals);
#if defined(USE_OLD_UNHASHED)
                /* Add the unhashed password pseudo-attribute to the entry */
                pwdtype =
                    slapi_attr_syntax_normalize(PSEUDO_ATTR_UNHASHEDUSERPASSWORD);
                slapi_entry_add_values_sv(e, pwdtype, unhashed_password_vals);
#endif
            }
        }
        if (unhashed_password_vals &&
            (SLAPD_UNHASHED_PW_OFF != config_get_unhashed_pw_switch())) {
            /* unhashed_password_vals is consumed if successful. */
            err = slapi_pw_set_entry_ext(e, unhashed_password_vals,
                                         SLAPI_EXT_SET_ADD);
            if (err) {
                valuearray_free(&unhashed_password_vals);
            }
        } else {
            valuearray_free(&unhashed_password_vals);
        }

#if defined(THISISTEST)
        {
            /* test code to retrieve an unhashed pw from the entry extension &
             * PSEUDO_ATTR_UNHASHEDUSERPASSWORD attribute */
            char *test_str = slapi_get_first_clear_text_pw(e);
            if (test_str) {
                slapi_log_err(SLAPI_LOG_ERR, "op_shared_add",
                              "Value from extension: %s\n", test_str);
                slapi_ch_free_string(&test_str);
            }
#if defined(USE_OLD_UNHASHED)
            test_str = (char *)slapi_entry_attr_get_ref(e, PSEUDO_ATTR_UNHASHEDUSERPASSWORD);
            if (test_str) {
                slapi_log_err(SLAPI_LOG_ERR, "op_shared_add",
                              "Value from attr: %s\n", test_str);
            }
#endif /* USE_OLD_UNHASHED */
        }
#endif /* THISISTEST */

        /* look for multiple backend local credentials or replication local credentials */
        for (p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL && !repl_op;
             p = p->plg_next) {
            char *L_attr = NULL;
            int i = 0;

            /* Get the appropriate decoding function */
            for (L_attr = p->plg_argv[i]; i < p->plg_argc; L_attr = p->plg_argv[++i]) {
                /* look for multiple backend local credentials or replication local credentials */
                char *L_normalized = slapi_attr_syntax_normalize(L_attr);
                slapi_entry_attr_find(e, L_normalized, &attr);
                if (attr) {
                    Slapi_Value **decode_present_values = NULL;
                    Slapi_Value **vals = NULL;

                    decode_present_values = attr_get_present_values(attr);

                    valuearray_add_valuearray(&vals, decode_present_values, 0);
                    pw_rever_encode(vals, L_normalized);
                    slapi_entry_attr_replace_sv(e, L_normalized, vals);
                    valuearray_free(&vals);
                }
                if (L_normalized) {
                    slapi_ch_free((void **)&L_normalized);
                }
            }
        }
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);

    if (!repl_op) {
        /* can get lastmod only after backend is selected */
        slapi_pblock_get(pb, SLAPI_BE_LASTMOD, &lastmod);

        if (lastmod && add_created_attrs(pb, e) != 0) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "cannot insert computed attributes", 0, NULL);
            goto done;
        }
        /* expand objectClass values to reflect the inheritance hierarchy */
        slapi_schema_expand_objectclasses(e);
    }

    /*
     * call the pre-add plugins. if they succeed, call
     * the backend add function. then call the post-add
     * plugins.
     */

    sdn = slapi_sdn_dup(slapi_entry_get_sdn_const(e));
    slapi_pblock_set(pb, SLAPI_ADD_TARGET_SDN, (void *)sdn);
    if (plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN : SLAPI_PLUGIN_PRE_ADD_FN) == SLAPI_PLUGIN_SUCCESS) {
        int rc;
        Slapi_Entry *ec;
        Slapi_DN *add_target_sdn = NULL;
        Slapi_Entry *save_e = NULL;

        slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
        set_db_default_result_handlers(pb);
        /* because be_add frees the entry */
        ec = slapi_entry_dup(e);
        add_target_sdn = slapi_sdn_dup(slapi_entry_get_sdn_const(ec));
        slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
        slapi_sdn_free(&sdn);
        slapi_pblock_set(pb, SLAPI_ADD_TARGET_SDN, add_target_sdn);

        if (be->be_add != NULL) {
            rc = (*be->be_add)(pb);
            /* backend may change this if errors and not consumed */
            slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &save_e);
            slapi_pblock_set(pb, SLAPI_ADD_ENTRY, ec);
            if (rc == 0) {
                /* acl is not enabled for internal operations */
                /* don't update aci store for remote acis     */
                if ((!internal_op) &&
                    (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA))) {
                    plugin_call_acl_mods_update(pb, SLAPI_OPERATION_ADD);
                }

                if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_AUDIT)) {
                    write_audit_log_entry(pb); /* Record the operation in the audit log */
                }

                slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
                do_ps_service(pse, NULL, LDAP_CHANGETYPE_ADD, 0);
                /*
                 * If be_add succeeded, then e is consumed except the resurrect case.
                 * If it is resurrect, the corresponding tombstone entry is resurrected
                 * and put into the cache.
                 * Otherwise, we set e to NULL to prevent freeing it ourselves.
                 */
                if (operation_is_flag_set(operation, OP_FLAG_RESURECT_ENTRY) && save_e) {
                    e = save_e;
                } else {
                    e = NULL;
                }
            } else {
                /* PR_ASSERT(!save_e); save_e is supposed to be freed in the backend.  */
                e = save_e;
                if (rc == SLAPI_FAIL_DISKFULL) {
                    operation_out_of_disk_space();
                    goto done;
                }
                /* If the disk is full we don't want to make it worse ... */
                if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_AUDIT)) {
                    write_auditfail_log_entry(pb); /* Record the operation in the audit log */
                }
            }
        } else {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "Function not implemented", 0, NULL);
        }
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_POST_ADD_FN : SLAPI_PLUGIN_POST_ADD_FN);
        slapi_entry_free(ec);
    }
    slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
    slapi_sdn_free(&sdn);

done:
    if (be)
        slapi_be_Unlock(be);
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
    slapi_entry_free(pse);
    slapi_ch_free((void **)&operation->o_params.p.p_add.parentuniqueid);
    slapi_entry_free(e);
    slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
    slapi_ch_free((void **)&pwdtype);
    slapi_ch_free_string(&proxydn);
    slapi_ch_free_string(&proxystr);
}

static int
add_created_attrs(Slapi_PBlock *pb, Slapi_Entry *e)
{
    char buf[SLAPI_TIMESTAMP_BUFSIZE];
    char *binddn = NULL;
    char *plugin_dn = NULL;
    struct berval bv;
    struct berval *bvals[2];
    Operation *op;
    struct slapdplugin *plugin = NULL;
    struct slapi_componentid *cid = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    slapi_log_err(SLAPI_LOG_TRACE, "add_created_attrs", "==>\n");

    bvals[0] = &bv;
    bvals[1] = NULL;
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (slapdFrontendConfig->plugin_track) {
        /* plugin bindDN tracking is enabled, grab the dn from thread local storage */
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
        slapi_entry_attr_replace(e, "internalCreatorsName", bvals);
        slapi_entry_attr_replace(e, "internalModifiersName", bvals);
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
        if (slapi_sdn_isempty(&op->o_sdn)) {
            bv.bv_val = "";
            bv.bv_len = 0;
        } else {
            bv.bv_val = (char *)slapi_sdn_get_dn(&op->o_sdn);
            bv.bv_len = strlen(bv.bv_val);
        }
    }

    slapi_entry_attr_replace(e, "creatorsname", bvals);
    slapi_entry_attr_replace(e, "modifiersname", bvals);

    slapi_timestamp_utc_hr(buf, SLAPI_TIMESTAMP_BUFSIZE);

    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    slapi_entry_attr_replace(e, "createtimestamp", bvals);

    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    slapi_entry_attr_replace(e, "modifytimestamp", bvals);

    if (add_uniqueid(e) != UID_SUCCESS) {
        return (-1);
    }

    return (0);
}


/* Checks if created attributes are used in the RDN.
 * Returns 1 if created attrs are in the RDN, and
 * 0 if created attrs are not in the RDN. Returns
 * -1 if an error occurred.
 */
static int
check_rdn_for_created_attrs(Slapi_Entry *e)
{
    int i, rc = 0;
    Slapi_RDN *rdn = NULL;
    char *value = NULL;
    char *type[] = {SLAPI_ATTR_UNIQUEID, "modifytimestamp", "modifiersname", "internalmodifytimestamp",
                    "internalmodifiersname", "createtimestamp", "creatorsname", 0};

    if ((rdn = slapi_rdn_new())) {
        slapi_rdn_init_dn(rdn, slapi_entry_get_dn_const(e));

        for (i = 0; type[i] != NULL; i++) {
            if (slapi_rdn_contains_attr(rdn, type[i], &value)) {
                slapi_log_err(SLAPI_LOG_TRACE, "check_rdn_for_created_attrs",
                              "Invalid DN. RDN contains %s attribute\n", type[i]);
                rc = 1;
                break;
            }
        }

        slapi_rdn_free(&rdn);
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "check_rdn_for_created_attrs", "Error allocating RDN\n");
        rc = -1;
    }

    return rc;
}


static void
handle_fast_add(Slapi_PBlock *pb, Slapi_Entry *entry)
{
    Slapi_Backend *be;
    Slapi_Operation *operation;
    Connection *pb_conn = NULL;
    int ret;

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    if (pb_conn == NULL){
        slapi_log_err(SLAPI_LOG_ERR, "handle_fast_add", "NULL param: pb_conn (0x%p)\n", pb_conn);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "param error", 0, NULL);
        return;
    }
    be = pb_conn->c_bi_backend;

    if ((be == NULL) || (be->be_wire_import == NULL)) {
        /* can this even happen? */
        slapi_log_err(SLAPI_LOG_ERR,
                      "handle_fast_add", "Backend not supported\n");
        send_ldap_result(pb, LDAP_NOT_SUPPORTED, NULL, NULL, 0, NULL);
        return;
    }

    /* ensure that the RDN values are present as attribute values */
    if ((ret = slapi_entry_add_rdn_values(entry)) != LDAP_SUCCESS) {
        send_ldap_result(pb, ret, NULL, "failed to add RDN values", 0, NULL);
        return;
    }

    /* schema check */
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    if (operation_is_flag_set(operation, OP_FLAG_ACTION_SCHEMA_CHECK) &&
        (slapi_entry_schema_check(pb, entry) != 0)) {
        char *errtext;
        slapi_log_err(SLAPI_LOG_TRACE, "handle_fast_add", "Entry failed schema check\n");
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        send_ldap_result(pb, LDAP_OBJECT_CLASS_VIOLATION, NULL, errtext, 0, NULL);
        slapi_entry_free(entry);
        return;
    }

    /* syntax check */
    if (slapi_entry_syntax_check(pb, entry, 0) != 0) {
        char *errtext;
        slapi_log_err(SLAPI_LOG_TRACE, "handle_fast_add", "Entry failed syntax check\n");
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &errtext);
        send_ldap_result(pb, LDAP_INVALID_SYNTAX, NULL, errtext, 0, NULL);
        slapi_entry_free(entry);
        return;
    }

    /* Check if the entry being added is a Tombstone. Could be if we are
     * doing a replica init. */
    if (slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS,
                                  SLAPI_ATTR_VALUE_TOMBSTONE)) {
        entry->e_flags |= SLAPI_ENTRY_FLAG_TOMBSTONE;
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_ENTRY, entry);
    ret = SLAPI_BI_STATE_ADD;
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &ret);
    ret = (*be->be_wire_import)(pb);
    if (ret != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "handle_fast_add",
                      "wire import: Error during import (%d)\n", ret);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR,
                         NULL, NULL, 0, NULL);
        /* It's our responsibility to free the entry if
         * be_wire_import doesn't succeed. */
        slapi_entry_free(entry);

        /* turn off fast replica init -- import is now aborted */
        pb_conn->c_bi_backend = NULL;
        pb_conn->c_flags &= ~CONN_FLAG_IMPORT;

        return;
    }

    send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    return;
}

static int
add_uniqueid(Slapi_Entry *e)
{
    char *uniqueid;
    int rc;

    /* generate uniqueID for the entry */
    rc = slapi_uniqueIDGenerateString(&uniqueid);
    if (rc == UID_SUCCESS) {
        slapi_entry_set_uniqueid(e, uniqueid);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "add_uniqueid", "Uniqueid generation failed for %s; error = %d\n",
                      slapi_entry_get_dn_const(e), rc);
    }

    return (rc);
}

static PRBool
check_oc_subentry(Slapi_Entry *e, struct berval **vals, char *normtype)
{
    int n;

    PRBool subentry = PR_TRUE;
    for (n = 0; vals != NULL && vals[n] != NULL; n++) {
        if ((strcasecmp(normtype, "objectclass") == 0) && (strncasecmp((const char *)vals[n]->bv_val, "ldapsubentry", vals[n]->bv_len) == 0)) {
            e->e_flags |= SLAPI_ENTRY_LDAPSUBENTRY;
            subentry = PR_FALSE;
            break;
        }
    }
    return subentry;
}

/*
 *  Used by plugins that modify entries on add operations, otherwise the internalModifiersname
 *  would not be set the the correct plugin name.
 */
void
add_internal_modifiersname(Slapi_PBlock *pb, Slapi_Entry *e)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    struct slapi_componentid *cid = NULL;
    struct slapdplugin *plugin = NULL;
    char *plugin_dn = NULL;

    if (slapdFrontendConfig->plugin_track) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &cid);
        if (cid) {
            plugin = (struct slapdplugin *)cid->sci_plugin;
        } else {
            slapi_pblock_get(pb, SLAPI_PLUGIN, &plugin);
        }
        if (plugin)
            plugin_dn = plugin_get_dn(plugin);
        if (plugin_dn) {
            slapi_entry_attr_set_charptr(e, "internalModifiersname", plugin_dn);
            slapi_ch_free_string(&plugin_dn);
        }
    }
}
