/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009, 2010 Hewlett-Packard Development Company, L.P.
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"
#include "pratom.h"
#if defined(irix) || defined(aix)
#include <time.h>
#endif

/* Forward declarations */
static int modify_internal_pb(Slapi_PBlock *pb);
static void op_shared_modify(Slapi_PBlock *pb, int pw_change, char *old_pw);
static int op_shared_allow_pw_change(Slapi_PBlock *pb, LDAPMod *mod, char **old_pw, Slapi_Mods *smods);
static int hash_rootpw(LDAPMod **mods);
static int valuearray_init_bervalarray_unhashed_only(struct berval **bvals, Slapi_Value ***cvals);
static void optimize_mods(Slapi_Mods *smods);

#ifdef LDAP_ERROR_LOGGING
static const char *
mod_op_image(int op)
{
    switch (op & ~LDAP_MOD_BVALUES) {
    case LDAP_MOD_ADD:
        return "add";
    case LDAP_MOD_DELETE:
        return "delete";
    case LDAP_MOD_REPLACE:
        return "replace";
    default:
        break;
    }
    return "???";
}
#endif

/* an AttrCheckFunc function should return an LDAP result code (LDAP_SUCCESS if all goes well). */
typedef int (*AttrCheckFunc)(const char *attr_name, char *value, long minval, long maxval, char *errorbuf, size_t ebuflen);

static struct attr_value_check
{
    const char *attr_name; /* the name of the attribute */
    AttrCheckFunc checkfunc;
    long minval;
    long maxval;
} AttrValueCheckList[] = {
    {CONFIG_PW_SYNTAX_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_CHANGE_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_LOCKOUT_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_MUSTCHANGE_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_EXP_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_UNLOCK_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_HISTORY_ATTRIBUTE, attr_check_onoff, 0, 0},
    {CONFIG_PW_MINAGE_ATTRIBUTE, check_pw_duration_value, -1, -1},
    {CONFIG_PW_WARNING_ATTRIBUTE, check_pw_duration_value, 0, -1},
    {CONFIG_PW_MINLENGTH_ATTRIBUTE, attr_check_minmax, 2, 512},
    {CONFIG_PW_MAXFAILURE_ATTRIBUTE, attr_check_minmax, 1, 32767},
    {CONFIG_PW_INHISTORY_ATTRIBUTE, attr_check_minmax, 1, 24},
    {CONFIG_PW_LOCKDURATION_ATTRIBUTE, check_pw_duration_value, -1, -1},
    {CONFIG_PW_RESETFAILURECOUNT_ATTRIBUTE, check_pw_resetfailurecount_value, -1, -1},
    {CONFIG_PW_GRACELIMIT_ATTRIBUTE, attr_check_minmax, 0, -1},
    {CONFIG_PW_STORAGESCHEME_ATTRIBUTE, check_pw_storagescheme_value, -1, -1},
    {CONFIG_PW_MAXAGE_ATTRIBUTE, check_pw_duration_value, -1, -1}};

/* This function is called to process operation that come over external connections */
void
do_modify(Slapi_PBlock *pb)
{
    Slapi_Operation *operation = NULL;
    Connection *pb_conn = NULL;
    Slapi_Mods smods;
    BerElement *ber;
    ber_tag_t tag;
    ber_len_t len;
    LDAPMod **normalized_mods = NULL;
    LDAPMod *mod;
    LDAPMod **mods;
    char *last, *type = NULL;
    char *old_pw = NULL; /* remember the old password */
    char *rawdn = NULL;
    int minssf_exclude_rootdse = 0;
    int ignored_some_mods = 0;
    int has_password_mod = 0; /* number of password mods */
    int pw_change = 0;        /* 0 = no password change */
    int err;

    slapi_log_err(SLAPI_LOG_TRACE, "do_modify", "=>\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    if (operation == NULL || pb_conn == NULL) {
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR,
                         NULL, "operation is NULL parameter", 0, NULL);
        slapi_log_err(SLAPI_LOG_ERR, "do_modify",
            "NULL param:  pb_conn (0x%p) operation (0x%p)\n", pb_conn, operation);
        return;
    }

    ber = operation->o_ber;

    /* count the modify request */
    slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsModifyEntryOps);

    /*
     * Parse the modify request.  It looks like this:
     *
     *    ModifyRequest := [APPLICATION 6] SEQUENCE {
     *        name    DistinguishedName,
     *        mods    SEQUENCE OF SEQUENCE {
     *            operation    ENUMERATED {
     *                add    (0),
     *                delete    (1),
     *                replace    (2)
     *            },
     *            modification    SEQUENCE {
     *                type    AttributeType,
     *                values    SET OF AttributeValue
     *            }
     *        }
     *    }
     */

    {
        int rc = 0;
        if (ber_scanf(ber, "{a", &rawdn) == LBER_ERROR) {
            slapi_log_err(SLAPI_LOG_ERR, "do_modify",
                          "ber_scanf failed (op=Modify; params=DN)\n");
            op_shared_log_error_access(pb, "MOD", "???", "decoding error");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
            slapi_ch_free_string(&rawdn);
            return;
        }
        /* Check if we should be performing strict validation. */
        if (config_get_dn_validate_strict()) {
            /* check that the dn is formatted correctly */
            rc = slapi_dn_syntax_check(pb, rawdn, 1);
            if (rc) { /* syntax check failed */
                op_shared_log_error_access(pb, "MOD", rawdn ? rawdn : "",
                                           "strict: invalid dn");
                send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX,
                                 NULL, "invalid dn", 0, NULL);
                slapi_ch_free((void **)&rawdn);
                return;
            }
        }
    }

    slapi_log_err(SLAPI_LOG_ARGS, "do_modify", "dn (%s)\n", rawdn);

    /*
     * If nsslapd-minssf-exclude-rootdse is on, the minssf check has been
     * postponed until here.  We should do it now.
     */
    minssf_exclude_rootdse = config_get_minssf_exclude_rootdse();
    if (minssf_exclude_rootdse) {
        int minssf = 0;
        /* Check if the minimum SSF requirement has been met. */
        minssf = config_get_minssf();
        if ((pb_conn->c_sasl_ssf < minssf) &&
            (pb_conn->c_ssl_ssf < minssf) &&
            (pb_conn->c_local_ssf < minssf)) {
            op_shared_log_error_access(pb, "MOD", rawdn ? rawdn : "",
                                       "Minimum SSF not met");
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "Minimum SSF not met.", 0, NULL);
            slapi_ch_free((void **)&rawdn);
            return;
        }
    }

    slapi_pblock_set(pb, SLAPI_REQUESTOR_ISROOT, &(operation->o_isroot));
    slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, rawdn);

    /* collect modifications & save for later */
    slapi_mods_init(&smods, 0);
    len = -1;
    for (tag = ber_first_element(ber, &len, &last);
         tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
         tag = ber_next_element(ber, &len, last)) {
        ber_int_t mod_op;
        mod = (LDAPMod *)slapi_ch_malloc(sizeof(LDAPMod));
        mod->mod_bvalues = NULL;
        len = -1; /* reset - len is not used */

        if (ber_scanf(ber, "{i{a[V]}}", &mod_op, &type,
                      &mod->mod_bvalues) == LBER_ERROR) {
            op_shared_log_error_access(pb, "MOD", rawdn, "decoding error");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                             "decoding error", 0, NULL);
            ber_bvecfree(mod->mod_bvalues);
            slapi_ch_free((void **)&mod);
            slapi_ch_free_string(&type);
            goto free_and_return;
        }
        mod->mod_op = mod_op;
        mod->mod_type = slapi_attr_syntax_normalize(type);
        if (!mod->mod_type || !*mod->mod_type) {
            char ebuf[SLAPI_DSE_RETURNTEXT_SIZE];
            PR_snprintf(ebuf, sizeof(ebuf), "invalid type '%s'", type);
            op_shared_log_error_access(pb, "MOD", rawdn, ebuf);
            send_ldap_result(pb, LDAP_INVALID_SYNTAX, NULL, ebuf, 0, NULL);
            slapi_ch_free((void **)&type);
            ber_bvecfree(mod->mod_bvalues);
            slapi_ch_free_string(&mod->mod_type);
            slapi_ch_free((void **)&mod);
            goto free_and_return;
        }
        slapi_ch_free((void **)&type);

        if (mod->mod_op != LDAP_MOD_ADD &&
            mod->mod_op != LDAP_MOD_DELETE &&
            mod->mod_op != LDAP_MOD_REPLACE) {
            op_shared_log_error_access(pb, "MOD", rawdn, "unrecognized modify operation");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                             "unrecognized modify operation", 0, NULL);
            ber_bvecfree(mod->mod_bvalues);
            slapi_ch_free((void **)&(mod->mod_type));
            slapi_ch_free((void **)&mod);
            goto free_and_return;
        }

        if (mod->mod_bvalues == NULL && mod->mod_op != LDAP_MOD_DELETE && mod->mod_op != LDAP_MOD_REPLACE) {
            op_shared_log_error_access(pb, "MOD", rawdn, "no values given");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                             "no values given", 0, NULL);
            ber_bvecfree(mod->mod_bvalues);
            slapi_ch_free((void **)&(mod->mod_type));
            slapi_ch_free((void **)&mod);
            goto free_and_return;
        }

        /* check if user is allowed to modify the specified attribute */
        if (!op_shared_is_allowed_attr(mod->mod_type, pb_conn->c_isreplication_session)) {
            /*
             * For now we just ignore attributes that client is not allowed
             * to modify so not to break existing clients
             */
            ++ignored_some_mods;
            ber_bvecfree(mod->mod_bvalues);
            slapi_ch_free((void **)&(mod->mod_type));
            slapi_ch_free((void **)&mod);
            continue;
        }

        /* check for password change (including deletion) */
        if (strcasecmp(mod->mod_type, SLAPI_USERPWD_ATTR) == 0) {
            has_password_mod++;
        }

        mod->mod_op |= LDAP_MOD_BVALUES;
        slapi_mods_add_ldapmod(&smods, mod);
    }

    if (ignored_some_mods && (0 == smods.num_elements)) {
        if (pb_conn->c_isreplication_session) {
            uint64_t connid;
            int32_t opid;
            slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
            slapi_pblock_get(pb, SLAPI_OPERATION_ID, &opid);
            slapi_log_err(SLAPI_LOG_ERR, "do_modify",
                          "Rejecting replicated password policy operation(conn=%"PRIu64" op=%d) for "
                          "entry %s.  To allow these changes to be accepted, set passwordIsGlobalPolicy to 'on' in "
                          "cn=config.\n",
                          connid, opid, rawdn);
        }
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL);
        goto free_and_return;
    }

    /* check for decoding error */
    /* will return LBER_DEFAULT in either case
        if there was at least one element read, len will be -1
        if there were no elements read (empty modify) len will be 0
    */
    if (tag != LBER_END_OF_SEQORSET) {
        if ((len == 0) && (0 == smods.num_elements) && !ignored_some_mods) {
            /* ok - empty modify - allow empty modifies */
        } else if (len != -1) {
            op_shared_log_error_access(pb, "MOD", rawdn, "decoding error");
            send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0, NULL);
            goto free_and_return;
        }
        /* else ok */
    }

    /* decode the optional controls - put them in the pblock */
    if ((err = get_ldapmessage_controls(pb, ber, NULL)) != 0) {
        op_shared_log_error_access(pb, "MOD", rawdn, "failed to decode LDAP controls");
        send_ldap_result(pb, err, NULL, NULL, 0, NULL);
        goto free_and_return;
    }

    /* if there are any password mods, see if they are allowed */
    if (has_password_mod) {
        /* iterate through the mods looking for password mods */
        for (mod = slapi_mods_get_first_mod(&smods);
             mod;
             mod = slapi_mods_get_next_mod(&smods)) {
            /* check for password change (including deletion) */
            if (strcasecmp(mod->mod_type, SLAPI_USERPWD_ATTR) == 0) {
                /* assumes controls have already been decoded and placed
                   in the pblock */
                pw_change = op_shared_allow_pw_change(pb, mod, &old_pw, &smods);
                if (pw_change == -1) {
                    goto free_and_return;
                }
            }
        }
    }

    if (!pb_conn->c_isreplication_session &&
        pb_conn->c_needpw && pw_change == 0) {
        (void)slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
        op_shared_log_error_access(pb, "MOD", rawdn, "need new password");
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL);
        goto free_and_return;
    }

#ifdef LDAP_ERROR_LOGGING
    slapi_log_err(SLAPI_LOG_ARGS, "do_modify", "modifications:\n");
    for (mod = slapi_mods_get_first_mod(&smods); mod != NULL;
         mod = slapi_mods_get_next_mod(&smods)) {
        slapi_log_err(SLAPI_LOG_ARGS, "do_modify", "\t%s: %s\n",
                      mod_op_image(mod->mod_op), mod->mod_type);
    }
#endif

    mods = slapi_mods_get_ldapmods_passout(&smods);

    /* normalize the mods */
    if (mods) {
        normalized_mods = normalize_mods2bvals((const LDAPMod **)mods);
        ldap_mods_free(mods, 1 /* Free the Array and the Elements */);
        if (normalized_mods == NULL) {
            /* NOTE: normalize_mods2bvals only handles DN syntax currently */
            op_shared_log_error_access(pb, "MOD", rawdn ? rawdn : "",
                                       "mod includes invalid dn format");
            send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                             "mod includes invalid dn format", 0, NULL);
            goto free_and_return;
        }
        slapi_pblock_set(pb, SLAPI_MODIFY_MODS, normalized_mods);
    }

    op_shared_modify(pb, pw_change, old_pw);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &normalized_mods);
    ldap_mods_free(normalized_mods, 1 /* Free the Array and the Elements */);

free_and_return:;
    slapi_ch_free_string(&old_pw);
    slapi_ch_free_string(&rawdn);
    slapi_mods_done(&smods);
}

/* This function is used to issue internal modify operation
   This is an old style API. Its use is discoraged because it is not extendable and
   because it does not allow to check whether plugin has right to access part of the
   tree it is trying to modify. Use slapi_modify_internal_pb instead */
Slapi_PBlock *
slapi_modify_internal(const char *idn,
                      LDAPMod **mods,
                      LDAPControl **controls,
                      int dummy __attribute__((unused)))
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_PBlock *result_pb = NULL;
    int opresult;

    slapi_modify_internal_set_pb(pb, idn, (LDAPMod **)mods, controls, NULL,
                                 (void *)plugin_get_default_component_id(), 0);

    modify_internal_pb(pb);

    result_pb = slapi_pblock_new();
    if (result_pb) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        slapi_pblock_set(result_pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    }
    slapi_pblock_destroy(pb);

    return result_pb;
}

/*  This is new style API to issue internal modify operation.
    pblock should contain the following data (can be set via call to slapi_modify_internal_set_pb):
    For uniqueid based operation:
        SLAPI_TARGET_DN set to dn that allows to select right backend, can be stale
        SLAPI_TARGET_UNIQUEID set to the uniqueid of the entry we are looking for
        SLAPI_MODIFY_MODS set to the mods
        SLAPI_CONTROLS_ARG set to request controls if present

    For dn based search:
        SLAPI_TARGET_DN set to the entry dn
        SLAPI_MODIFY_MODS set to the mods
        SLAPI_CONTROLS_ARG set to request controls if present
 */
int
slapi_modify_internal_pb(Slapi_PBlock *pb)
{
    if (pb == NULL)
        return -1;

    if (!allow_operation(pb)) {
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                               "This plugin is not configured to access operation target data", 0, NULL);
        return 0;
    }

    return modify_internal_pb(pb);
}

/* Initialize a pblock for a call to slapi_modify_internal_pb() */
void
slapi_modify_internal_set_pb(Slapi_PBlock *pb, const char *dn, LDAPMod **mods, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags)
{
    Operation *op;
    PR_ASSERT(pb != NULL);
    if (pb == NULL || dn == NULL || mods == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_modify_internal_set_pb",
                      "NULL parameter\n");
        return;
    }

    op = internal_operation_new(SLAPI_OPERATION_MODIFY, operation_flags);
    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, (void *)dn);
    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
    slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
    if (uniqueid) {
        slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void *)uniqueid);
    }
    slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

/* Initialize a pblock for a call to slapi_modify_internal_pb() */
void
slapi_modify_internal_set_pb_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, LDAPMod **mods, LDAPControl **controls, const char *uniqueid, Slapi_ComponentId *plugin_identity, int operation_flags)
{
    Operation *op;
    PR_ASSERT(pb != NULL);
    if (pb == NULL || sdn == NULL || mods == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_modify_internal_set_pb_ext",
                      "NULL parameter\n");
        return;
    }

    op = internal_operation_new(SLAPI_OPERATION_MODIFY, operation_flags);
    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    slapi_pblock_set(pb, SLAPI_ORIGINAL_TARGET, (void *)slapi_sdn_get_dn(sdn));
    slapi_pblock_set(pb, SLAPI_TARGET_SDN, (void *)sdn);
    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
    slapi_pblock_set(pb, SLAPI_CONTROLS_ARG, controls);
    if (uniqueid) {
        slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, (void *)uniqueid);
    }
    slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, plugin_identity);
}

/* Helper functions */

static int
modify_internal_pb(Slapi_PBlock *pb)
{
    LDAPControl **controls;
    int pwpolicy_ctrl = 0;
    Operation *op;
    int opresult = 0;
    LDAPMod **normalized_mods = NULL;
    LDAPMod **mods;
    LDAPMod **mod;
    Slapi_Mods smods;
    int pw_change = 0;
    char *old_pw = NULL;

    PR_ASSERT(pb != NULL);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_pblock_get(pb, SLAPI_CONTROLS_ARG, &controls);

    /* See if pwpolicy control is present.  We need to do
     * this before we call op_shared_allow_pw_change() since
     * it looks for SLAPI_PWPOLICY in the pblock to determine
     * if the response contorl is needed. */
    pwpolicy_ctrl = slapi_control_present(controls,
                                          LDAP_X_CONTROL_PWPOLICY_REQUEST, NULL, NULL);
    slapi_pblock_set(pb, SLAPI_PWPOLICY, &pwpolicy_ctrl);

    if (mods == NULL) {
        opresult = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        return 0;
    }

    /* first normalize the mods so they are bvalue
     * Note: We don't add any special
     * attributes such as "creatorsname".
     * for CIR we don't want to change them, for other
     * plugins the writer should change these if it wants too by explicitly
     * adding them to the mods
     */
    normalized_mods = normalize_mods2bvals((const LDAPMod **)mods);
    if (normalized_mods == NULL) {
        opresult = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
        return 0;
    }

    /* check for password change */
    mod = normalized_mods;
    while (*mod) {
        if ((*mod)->mod_bvalues != NULL && strcasecmp((*mod)->mod_type, SLAPI_USERPWD_ATTR) == 0) {
            slapi_mods_init_passin(&smods, mods);
            pw_change = op_shared_allow_pw_change(pb, *mod, &old_pw, &smods);
            if (pw_change == -1) {
                /* The internal result code will already have been set by op_shared_allow_pw_change() */
                ldap_mods_free(normalized_mods, 1);
                return 0;
            }
        }

        mod++;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op->o_handler_data = &opresult;
    op->o_result_handler = internal_getresult_callback;

    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, normalized_mods);
    slapi_pblock_set(pb, SLAPI_REQCONTROLS, controls);

    /* set parameters common for all internal operations */
    set_common_params(pb);

    /* set actions taken to process the operation */
    set_config_params(pb);

    /* perform modify operation */
    slapi_td_internal_op_start();
    op_shared_modify(pb, pw_change, old_pw);
    slapi_td_internal_op_finish();

    /* free the normalized_mods don't forget to add this*/
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &normalized_mods);
    if (normalized_mods != NULL) {
        ldap_mods_free(normalized_mods, 1);
    }

    /* return original mods here */
    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
    /* set result */
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);

    return 0;
}

static void
op_shared_modify(Slapi_PBlock *pb, int pw_change, char *old_pw)
{
    Slapi_PBlock *entry_pb = NULL;
    Slapi_Backend *be = NULL;
    Slapi_Entry *pse;
    Slapi_Entry *referral;
    Slapi_Entry *e = NULL;
    char *dn = NULL;
    char *normdn = NULL;
    Slapi_DN *sdn = NULL;
    int passin_sdn = 0;
    LDAPMod **mods, *pw_mod, **tmpmods = NULL;
    Slapi_Mods smods;
    int repl_op, internal_op, lastmod, skip_modified_attrs;
    char *unhashed_pw_attr = NULL;
    Slapi_Operation *operation;
    Connection *pb_conn;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    int err;
    LDAPMod *lc_mod = NULL;
    struct slapdplugin *p = NULL;
    int numattr;
    char *proxydn = NULL;
    int proxy_err = LDAP_SUCCESS;
    char *errtext = NULL;

    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET, &dn);
    slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &tmpmods);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);
    slapi_pblock_get(pb, SLAPI_SKIP_MODIFIED_ATTRS, &skip_modified_attrs);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    /* Set the time we actually started the operation */
    slapi_operation_set_time_started(operation);

    if (sdn) {
        passin_sdn = 1;
    } else {
        sdn = slapi_sdn_new_dn_byval(dn);
        slapi_pblock_set(pb, SLAPI_MODIFY_TARGET_SDN, (void *)sdn);
    }
    normdn = (char *)slapi_sdn_get_dn(sdn);
    if (dn && (strlen(dn) > 0) && (NULL == normdn)) {
        /* normalization failed */
        op_shared_log_error_access(pb, "MOD", dn, "invalid dn");
        send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                         "invalid dn", 0, NULL);
        goto free_and_return;
    }

    slapi_mods_init_passin(&smods, mods);

    /* target spec is used to decide which plugins are applicable for the operation */
    operation_set_target_spec(operation, sdn);

    /* get the proxy auth dn if the proxy auth control is present */
    proxy_err = proxyauth_get_dn(pb, &proxydn, &errtext);

    if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
        char *proxystr = NULL;

        if (proxydn) {
            proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
        }

        if (!internal_op) {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d MOD dn=\"%s\"%s\n",
                             pb_conn->c_connid,
                             operation->o_opid,
                             slapi_sdn_get_dn(sdn),
                             proxystr ? proxystr : "");
        } else {
            uint64_t connid;
            int32_t op_id;
            int32_t op_internal_id;
            int32_t op_nested_count;
            get_internal_conn_op(&connid, &op_id, &op_internal_id, &op_nested_count);
            slapi_log_access(LDAP_DEBUG_ARGS,
                             connid==0 ? "conn=Internal(%" PRId64 ") op=%d(%d)(%d) MOD dn=\"%s\"%s\n" :
                                         "conn=%" PRId64 " (Internal) op=%d(%d)(%d) MOD dn=\"%s\"%s\n",
                             connid,
                             op_id,
                             op_internal_id,
                             op_nested_count,
                             slapi_sdn_get_dn(sdn),
                             proxystr ? proxystr : "");
        }

        slapi_ch_free_string(&proxystr);
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
    errorbuf[0] = '\0';
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
                             "cannot update referral", 0, NULL);
            slapi_entry_free(referral);
            goto free_and_return;
        }

        send_referrals_from_entry(pb, referral);
        slapi_entry_free(referral);
        goto free_and_return;
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);

    /* The following section checks the valid values of fine-grained
     * password policy attributes.
     * 1. First, it checks if the entry has "passwordpolicy" objectclass.
     * 2. If yes, then if the mods contain any passwdpolicy specific attributes.
     * 3. If yes, then it invokes corrosponding checking function.
     */
    if (!repl_op && !internal_op && normdn && slapi_search_get_entry(&entry_pb, sdn, NULL, &e, NULL) == LDAP_SUCCESS) {
        Slapi_Value target;
        slapi_value_init(&target);
        slapi_value_set_string(&target, "passwordpolicy");
        if ((slapi_entry_attr_has_syntax_value(e, "objectclass", &target)) == 1) {
            numattr = sizeof(AttrValueCheckList) / sizeof(AttrValueCheckList[0]);
            while (tmpmods && *tmpmods) {
                if ((*tmpmods)->mod_bvalues != NULL &&
                    !SLAPI_IS_MOD_DELETE((*tmpmods)->mod_op)) {
                    for (size_t i = 0; i < numattr; i++) {
                        if (slapi_attr_type_cmp((*tmpmods)->mod_type,
                                                AttrValueCheckList[i].attr_name, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                            /* The below function call is good for
                             * single-valued attrs only
                             */
                            if ((err = AttrValueCheckList[i].checkfunc(AttrValueCheckList[i].attr_name,
                                                                       (*tmpmods)->mod_bvalues[0]->bv_val, AttrValueCheckList[i].minval,
                                                                       AttrValueCheckList[i].maxval, errorbuf, sizeof(errorbuf))) != LDAP_SUCCESS) {
                                /* return error */
                                send_ldap_result(pb, err, NULL, errorbuf, 0, NULL);
                                goto free_and_return;
                            }
                        }
                    }
                }
                tmpmods++;
            } /* end of (while */
        }     /* end of if (found */
        value_done(&target);
    } /* end of if (!repl_op */

    /* can get lastmod only after backend is selected */
    slapi_pblock_get(pb, SLAPI_BE_LASTMOD, &lastmod);

    /* if this is replication session or the operation has been
     * flagged - leave mod attributes alone */
    if (!repl_op && !skip_modified_attrs && lastmod) {
        modify_update_last_modified_attr(pb, &smods);
        slapi_pblock_set(pb, SLAPI_MODIFY_MODS, slapi_mods_get_ldapmods_byref(&smods));
    }


    if (0 == slapi_mods_get_num_mods(&smods)) {
        /* nothing to do - no mods - this is not an error - just
           send back LDAP_SUCCESS */
        send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
        goto free_and_return;
    }

    /*
     * Add the unhashed password pseudo-attribute (for add) OR
     * Delete the unhashed password pseudo-attribute (for delete)
     * before calling the preop plugins
     */

    if (pw_change && !repl_op) {
        Slapi_Value **va = NULL;

        unhashed_pw_attr = slapi_attr_syntax_normalize(PSEUDO_ATTR_UNHASHEDUSERPASSWORD);

        for (pw_mod = slapi_mods_get_first_mod(&smods); pw_mod;
             pw_mod = slapi_mods_get_next_mod(&smods)) {
            if (strcasecmp(pw_mod->mod_type, SLAPI_USERPWD_ATTR) != 0)
                continue;

            if (SLAPI_IS_MOD_DELETE(pw_mod->mod_op)) {
                Slapi_Attr *a = NULL;
                struct pw_scheme *pwsp = NULL;
                int remove_unhashed_pw = 1;
                char *password = NULL;
                char *valpwd = NULL;

                /* if there are mod values, we need to delete a specific userpassword */
                for (size_t i = 0; pw_mod->mod_bvalues != NULL && pw_mod->mod_bvalues[i] != NULL; i++) {
                    password = slapi_ch_strdup(pw_mod->mod_bvalues[i]->bv_val);
                    pwsp = pw_val2scheme(password, &valpwd, 1);
                    if (pwsp == NULL || strcmp(pwsp->pws_name, "CLEAR") == 0) {
                        /*
                         *  CLEAR password
                         *
                         *  Ok, so now we to check the entry's userpassword values.
                         *  First, find out the password encoding of the entry's pw.
                         *  Then compare our clear text password to the encoded userpassword
                         *  using the proper scheme.  If we have a match, we know which
                         *  userpassword value to delete.
                         */
                        Slapi_Attr *pw = NULL;
                        struct berval bval, *bv[2];

                        if (slapi_entry_attr_find(e, SLAPI_USERPWD_ATTR, &pw) == 0 && pw) {
                            struct pw_scheme *pass_scheme = NULL;
                            Slapi_Value **present_values = NULL;
                            char *pval = NULL;
                            int ii;

                            present_values = attr_get_present_values(pw);
                            for (ii = 0; present_values && present_values[ii]; ii++) {
                                const char *userpwd = slapi_value_get_string(present_values[ii]);

                                pass_scheme = pw_val2scheme((char *)userpwd, &pval, 1);
                                if (pass_scheme && strcmp(pass_scheme->pws_name, "CLEAR")) {
                                    /* its encoded, so compare it */
                                    if ((*(pass_scheme->pws_cmp))(valpwd, pval) == 0) {
                                        /*
                                         *  Match, replace the mod value with the encoded password
                                         */
                                        slapi_ch_free_string(&pw_mod->mod_bvalues[i]->bv_val);
                                        pw_mod->mod_bvalues[i]->bv_val = strdup(userpwd);
                                        pw_mod->mod_bvalues[i]->bv_len = strlen(userpwd);
                                        free_pw_scheme(pass_scheme);
                                        break;
                                    }
                                } else {
                                    /* userpassword is already clear text, nothing to do */
                                    free_pw_scheme(pass_scheme);
                                    break;
                                }
                                free_pw_scheme(pass_scheme);
                            }
                        }
                        /*
                         *  Finally, delete the unhashed userpassword
                         *  (this will update the password entry extension)
                         */
                        if (SLAPD_UNHASHED_PW_OFF != config_get_unhashed_pw_switch()) {
                            bval.bv_val = password;
                            bval.bv_len = strlen(password);
                            bv[0] = &bval;
                            bv[1] = NULL;
                            valuearray_init_bervalarray(bv, &va);
                            slapi_mods_add_mod_values(&smods, pw_mod->mod_op, unhashed_pw_attr, va);
                            valuearray_free(&va);
                        }
                    } else {
                        /*
                         *  Password is encoded, try and find a matching unhashed_password to delete
                         */
                        Slapi_Value **vals;

                        /*
                         *  Grab the current unhashed passwords from the password entry extension,
                         *  as the "attribute" is no longer present in the entry.
                         */
                        if (slapi_pw_get_entry_ext(e, &vals) == LDAP_SUCCESS) {
                            int ii;

                            for (ii = 0; vals && vals[ii]; ii++) {
                                const char *unhashed_pwd = slapi_value_get_string(vals[ii]);
                                struct pw_scheme *unhashed_pwsp = NULL;
                                struct berval bval, *bv[2];

                                /* prepare the value to delete from the list of unhashed userpasswords */
                                bval.bv_val = (char *)unhashed_pwd;
                                bval.bv_len = strlen(unhashed_pwd);
                                bv[0] = &bval;
                                bv[1] = NULL;
                                /*
                                 *  Compare the clear text unhashed password, to the encoded password
                                 *  provided by the client.
                                 */
                                unhashed_pwsp = pw_val2scheme((char *)unhashed_pwd, NULL, 1);
                                if (unhashed_pwsp == NULL || strcmp(unhashed_pwsp->pws_name, "CLEAR") == 0) {
                                    if ((*(pwsp->pws_cmp))((char *)unhashed_pwd, valpwd) == 0) {
                                        /* match, add the delete mod for this particular unhashed userpassword */
                                        if (SLAPD_UNHASHED_PW_OFF != config_get_unhashed_pw_switch()) {
                                            valuearray_init_bervalarray(bv, &va);
                                            slapi_mods_add_mod_values(&smods, pw_mod->mod_op, unhashed_pw_attr, va);
                                            valuearray_free(&va);
                                            free_pw_scheme(unhashed_pwsp);
                                        }
                                        break;
                                    }
                                } else {
                                    /*
                                     *  We have a hashed unhashed_userpassword!  We must delete it.
                                     */
                                    if (SLAPD_UNHASHED_PW_OFF != config_get_unhashed_pw_switch()) {
                                        valuearray_init_bervalarray(bv, &va);
                                        slapi_mods_add_mod_values(&smods, pw_mod->mod_op, unhashed_pw_attr, va);
                                        valuearray_free(&va);
                                    }
                                }
                                free_pw_scheme(unhashed_pwsp);
                            }
                        } else {
                        }
                    }
                    remove_unhashed_pw = 0; /* mark that we already removed the unhashed userpassword */
                    slapi_ch_free_string(&password);
                    free_pw_scheme(pwsp);
                }
                if (remove_unhashed_pw && !slapi_entry_attr_find(e, unhashed_pw_attr, &a)) {
                    slapi_mods_add_mod_values(&smods, pw_mod->mod_op, unhashed_pw_attr, va);
                }
            } else if (SLAPD_UNHASHED_PW_OFF != config_get_unhashed_pw_switch()) {
                /* add pseudo password attribute */
                valuearray_init_bervalarray_unhashed_only(pw_mod->mod_bvalues, &va);
                if (va && va[0]) {
                    slapi_mods_add_mod_values(&smods, pw_mod->mod_op, unhashed_pw_attr, va);
                }
                valuearray_free(&va);
            }

            /* Init new value array for hashed value */
            valuearray_init_bervalarray(pw_mod->mod_bvalues, &va);

            /* encode password */
            if (pw_encodevals_ext(pb, sdn, va)) {
                slapi_log_err(SLAPI_LOG_CRIT, "op_shared_modify", "Unable to hash userPassword attribute for %s, "
                    "check value is utf8 string.\n", slapi_entry_get_dn_const(e));
                send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Unable to hash \"userPassword\" attribute, "
                    "check value is utf8 string.\n", 0, NULL);
                valuearray_free(&va);
                goto free_and_return;
            }

            /* remove current clear value of userpassword */
            ber_bvecfree(pw_mod->mod_bvalues);

            /* add the cipher in the structure */
            valuearray_get_bervalarray(va, &pw_mod->mod_bvalues);

            valuearray_free(&va);
        }
    }
    for (p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL && !repl_op; p = p->plg_next) {
        char *L_attr = NULL;
        size_t i = 0;

        /* Get the appropriate encoding function */
        for (L_attr = p->plg_argv[i]; i < p->plg_argc; L_attr = p->plg_argv[++i]) {
            char *L_normalized = slapi_attr_syntax_normalize(L_attr);

            for (lc_mod = slapi_mods_get_first_mod(&smods); lc_mod;
                 lc_mod = slapi_mods_get_next_mod(&smods)) {
                Slapi_Value **va = NULL;

                if (strcasecmp(lc_mod->mod_type, L_normalized) != 0)
                    continue;

                switch (lc_mod->mod_op & ~LDAP_MOD_BVALUES) {
                case LDAP_MOD_ADD:
                case LDAP_MOD_REPLACE:

                    /* Init new value array for hashed value */
                    valuearray_init_bervalarray(lc_mod->mod_bvalues, &va);
                    if (va) {
                        /* encode local credentials */
                        pw_rever_encode(va, L_normalized);
                        /* remove current clear value of userpassword */
                        ber_bvecfree(lc_mod->mod_bvalues);
                        /* add the cipher in the structure */
                        valuearray_get_bervalarray(va, &lc_mod->mod_bvalues);

                        valuearray_free(&va);
                    }
                    break;
                default:
                    /* for LDAP_MOD_DELETE, don't do anything */
                    /* for LDAP_MOD_BVALUES, don't do anything */
                    ;
                }
            }
            if (L_normalized)
                slapi_ch_free((void **)&L_normalized);
        }
    }

    /*
     * Optimize the mods - this combines sequential identical attribute modifications.
     */
    optimize_mods(&smods);

    /*
     * call the pre-mod plugins. if they succeed, call
     * the backend mod function. then call the post-mod
     * plugins.
     */
    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, (void *)slapi_mods_get_ldapmods_passout(&smods));
    if (plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN : SLAPI_PLUGIN_PRE_MODIFY_FN) == SLAPI_PLUGIN_SUCCESS) {
        int rc;

        /*
         * Hash any rootpw attribute values.  We hash them after pre-op
         * plugins are called in case any pre-op plugin needs the clear value.
         * They do need to be hashed here so they wont get audit logged in the
         * clear.  Note that config_set_rootpw will also do hashing if needed,
         * but it will detect that the password is already hashed.
         */
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
        if (hash_rootpw(mods) != 0) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "Failed to hash root user's password", 0, NULL);
            goto free_and_return;
        }

        slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
        set_db_default_result_handlers(pb);
        if (be->be_modify != NULL) {
            if ((rc = (*be->be_modify)(pb)) == 0) {
                /* acl is not used for internal operations */
                /* don't update aci store for remote acis  */
                if ((!internal_op) &&
                    (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA))) {
                    plugin_call_acl_mods_update(pb, SLAPI_OPERATION_MODIFY);
                }

                if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_AUDIT))
                    write_audit_log_entry(pb); /* Record the operation in the audit log */

                if (pw_change && (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA))) {
                    /* update the password info */
                    update_pw_info(pb, old_pw);
                }

                slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
                do_ps_service(pse, NULL, LDAP_CHANGETYPE_MODIFY, 0);
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
        } else {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "Function not implemented", 0, NULL);
        }

        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        plugin_call_plugins(pb, internal_op ? SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN : SLAPI_PLUGIN_POST_MODIFY_FN);
    }

free_and_return : {
    Slapi_Entry *epre = NULL, *epost = NULL;
    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &epre);
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &epost);
    if (epre == e) {
        epre = NULL; /* to avoid possible double free below */
    }
    if (epost == e) {
        epost = NULL; /* to avoid possible double free below */
    }
    if (epre == epost) {
        epost = NULL; /* to avoid possible double free below */
    }
    slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, NULL);
    slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, NULL);
    slapi_entry_free(epre);
    slapi_entry_free(epost);
}
    slapi_search_get_entry_done(&entry_pb);

    if (be)
        slapi_be_Unlock(be);

    if (unhashed_pw_attr)
        slapi_ch_free((void **)&unhashed_pw_attr);

    slapi_ch_free_string(&proxydn);

    slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &sdn);
    if (!passin_sdn) {
        slapi_sdn_free(&sdn);
    }
}

/*
 *  Only add password mods that are in clear text.  The console likes to send two mods:
 *    - Already encoded password
 *    - Clear text password
 *
 *  We don't want to add the encoded value to the unhashed_userpassword attr
 */
static int
valuearray_init_bervalarray_unhashed_only(struct berval **bvals, Slapi_Value ***cvals)
{
    size_t n;
    for (n = 0; bvals != NULL && bvals[n] != NULL; n++)
        ;
    if (n == 0) {
        *cvals = NULL;
    } else {
        struct pw_scheme *pwsp = NULL;
        size_t p = 0;

        *cvals = (Slapi_Value **)slapi_ch_malloc((n + 1) * sizeof(Slapi_Value *));
        for (size_t i = 0; i < n; i++) {
            pwsp = pw_val2scheme(bvals[i]->bv_val, NULL, 1);
            if (pwsp == NULL || strcmp(pwsp->pws_name, "CLEAR") == 0) {
                (*cvals)[p++] = slapi_value_new_berval(bvals[i]);
            }
            free_pw_scheme(pwsp);
        }
        (*cvals)[p] = NULL;
    }
    return n;
}

static int
op_shared_allow_pw_change(Slapi_PBlock *pb, LDAPMod *mod, char **old_pw, Slapi_Mods *smods)
{
    int isroot, internal_op, repl_op, pwresponse_req = 0;
    int res = 0;
    char *dn;
    char *errtxt = NULL;
    Slapi_DN sdn;
    Slapi_Entry *e = NULL;
    passwdPolicy *pwpolicy;
    int rc = 0;
    Slapi_Value **values = NULL;
    Slapi_Operation *operation;
    Connection *pb_conn = NULL;
    int proxy_err = LDAP_SUCCESS;
    char *proxydn = NULL;
    char *proxystr = NULL;
    char *errtext = NULL;
    int32_t needpw = 0;

    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
    if (repl_op) {
        /* Treat like there's no password */
        return (0);
    }

    *old_pw = NULL;

    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET, &dn);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_PWPOLICY, &pwresponse_req);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_sdn_init_dn_byref(&sdn, dn);

    if (operation == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "op_shared_allow_pw_change", "NULL operation\n");
        rc = -1;
        goto done;
    }
    if (pb_conn) {
        needpw = pb_conn->c_needpw;
    }

    /* get the proxy auth dn if the proxy auth control is present */
    if ((proxy_err = proxyauth_get_dn(pb, &proxydn, &errtext)) != LDAP_SUCCESS) {
        if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d MOD dn=\"%s\"\n",
                             pb_conn ? pb_conn->c_connid: -1, operation->o_opid,
                             slapi_sdn_get_dn(&sdn));
        }

        send_ldap_result(pb, proxy_err, NULL, errtext, 0, NULL);
        rc = -1;
        goto done;
    }

    pwpolicy = new_passwdPolicy(pb, (char *)slapi_sdn_get_ndn(&sdn));
    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);

    /* internal operation has root permissions for subtrees it is allowed to access */
    if (!internal_op) {
        /* slapi_acl_check_mods needs an array of LDAPMods, but
         * we're really only interested in the one password mod. */
        Slapi_PBlock *entry_pb = NULL;
        LDAPMod *mods[2];
        mods[0] = mod;
        mods[1] = NULL;

        /* We need to actually fetch the target here to use for ACI checking. */
        slapi_search_get_entry(&entry_pb, &sdn, NULL, &e, NULL);

        /* Create a bogus entry with just the target dn if we were unable to
         * find the actual entry.  This will only be used for checking the ACIs. */
        if (e == NULL) {
            e = slapi_entry_alloc();
            slapi_entry_init(e, NULL, NULL);
            slapi_sdn_set_dn_byref(slapi_entry_get_sdn(e), dn);
        }

        /* Set the backend in the pblock.  The slapi_access_allowed function
         * needs this set to work properly. */
        slapi_pblock_set(pb, SLAPI_BACKEND, slapi_be_select(&sdn));

        /* Check if ACIs allow password to be changed */
        if (!pw_is_pwp_admin(pb, pwpolicy) && (res = slapi_acl_check_mods(pb, e, mods, &errtxt)) != LDAP_SUCCESS) {
            if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
                if (proxydn) {
                    proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
                }
                slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d MOD dn=\"%s\"%s\n",
                                 pb_conn ? pb_conn->c_connid : -1, operation->o_opid,
                                 slapi_sdn_get_dn(&sdn), proxystr ? proxystr : "");
            }

            /* Write access is denied to userPassword by ACIs */
            if (pwresponse_req == 1) {
                slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_PWDMODNOTALLOWED);
            }
            send_ldap_result(pb, res, NULL, errtxt, 0, NULL);
            slapi_ch_free_string(&errtxt);
            slapi_search_get_entry_done(&entry_pb);
            rc = -1;
            goto done;
        }
        /* done with slapi entry e */
        slapi_search_get_entry_done(&entry_pb);

        /*
         * If this mod is being performed by a password administrator/rootDN,
         * just return success.
         */
        if (pw_is_pwp_admin(pb, pwpolicy)) {
            if (!SLAPI_IS_MOD_DELETE(mod->mod_op) && pwpolicy->pw_history) {
                /* Updating pw history, get the old password */
                get_old_pw(pb, &sdn, old_pw);
            }
            rc = 1;
            goto done;
        }

        /* Check if password policy allows users to change their passwords.*/
        if (!operation->o_isroot && slapi_sdn_compare(&sdn, &operation->o_sdn) == 0 &&
            !needpw && !pwpolicy->pw_change) {
            if (pwresponse_req == 1) {
                slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_PWDMODNOTALLOWED);
            }
            slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                          "user is not allowed to change password.  Entry (%s) Policy (%s)\n",
                          slapi_sdn_get_dn(&sdn),
                          pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "user is not allowed to change password", 0, NULL);

            if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
                if (proxydn) {
                    proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
                }

                slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d MOD dn=\"%s\"%s, %s\n",
                                 pb_conn ? pb_conn->c_connid : -1, operation->o_opid,
                                 slapi_sdn_get_dn(&sdn),
                                 proxystr ? proxystr : "",
                                 "user is not allowed to change password");
            }

            rc = -1;
            goto done;
        }
    }

    /* check if password is within password minimum age;
       error result is sent directly from check_pw_minage */
    if (!needpw && check_pw_minage(pb, &sdn, mod->mod_bvalues) == 1) {
        if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
            if (proxydn) {
                proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
            }

            if (!internal_op) {
                slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d MOD dn=\"%s\"%s, %s\n",
                                 pb_conn ? pb_conn->c_connid : -1,
                                 operation->o_opid,
                                 slapi_sdn_get_dn(&sdn),
                                 proxystr ? proxystr : "",
                                 "within password minimum age");
            } else {
                slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d MOD dn=\"%s\"%s, %s\n",
                                 LOG_INTERNAL_OP_CON_ID,
                                 LOG_INTERNAL_OP_OP_ID,
                                 slapi_sdn_get_dn(&sdn),
                                 proxystr ? proxystr : "",
                                 "within password minimum age");
            }
        }
        rc = -1;
        goto done;
    }

    /* check password syntax; remember the old password;
       error sent directly from check_pw_syntax function */
    valuearray_init_bervalarray(mod->mod_bvalues, &values);
    switch (check_pw_syntax_ext(pb, &sdn, values, old_pw, NULL, mod->mod_op, smods)) {
    case 0: /* success */
        rc = 1;
        break;

    case 1: /* failed checking */
        if (operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {
            if (proxydn) {
                proxystr = slapi_ch_smprintf(" authzid=\"%s\"", proxydn);
            }

            if (!internal_op) {
                slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d MOD dn=\"%s\"%s, %s\n",
                                 pb_conn ? pb_conn->c_connid : -1,
                                 operation->o_opid,
                                 slapi_sdn_get_dn(&sdn),
                                 proxystr ? proxystr : "",
                                 "invalid password syntax");
            } else {
                slapi_log_access(LDAP_DEBUG_ARGS, "conn=%s op=%d MOD dn=\"%s\"%s, %s\n",
                                 LOG_INTERNAL_OP_CON_ID,
                                 LOG_INTERNAL_OP_OP_ID,
                                 slapi_sdn_get_dn(&sdn),
                                 proxystr ? proxystr : "",
                                 "invalid password syntax");
            }
        }
        rc = -1;
        break;

    case -1: /* The entry is not found.  No password checking is done.  Countinue execution
                    and it should get caught later and send "no such object back. */
        rc = 0;
        break;

    default:
        break;
    }
    valuearray_free(&values);

done:
    slapi_sdn_done(&sdn);
    slapi_ch_free_string(&proxydn);
    slapi_ch_free_string(&proxystr);
    return rc;
}

/*
 * Hashes any nsslapd-rootpw attribute values using the password storage
 * scheme specified in cn=config:nsslapd-rootpwstoragescheme.
 * Note: This is only done for modify, because rootdn's password lives
 * in cn=config, which is never added.
 */
static int
hash_rootpw(LDAPMod **mods)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (strcasecmp(slapdFrontendConfig->rootpwstoragescheme->pws_name, "clear") == 0) {
        /* No work to do if the rootpw storage scheme is clear */
        return 0;
    }

    for (size_t i = 0; (mods != NULL) && (mods[i] != NULL); i++) {
        LDAPMod *mod = mods[i];
        if (strcasecmp(mod->mod_type, CONFIG_ROOTPW_ATTRIBUTE) != 0)
            continue;

        if (mod->mod_bvalues != NULL) {
            for (size_t j = 0; mod->mod_bvalues[j] != NULL; j++) {
                char *val = mod->mod_bvalues[j]->bv_val;
                char *hashedval = NULL;
                struct pw_scheme *pws = pw_val2scheme(val, NULL, 0);
                if (pws) {
                    free_pw_scheme(pws);
                    /* Value is pre-hashed, no work to do for this value */
                    continue;
                } else if (!slapd_nss_is_initialized()) {
                    /* We need to hash a value but NSS is not initialized; bail */
                    return -1;
                }
                hashedval = (slapdFrontendConfig->rootpwstoragescheme->pws_enc)(val);
                slapi_ch_free_string(&val);
                mod->mod_bvalues[j]->bv_val = hashedval;
                mod->mod_bvalues[j]->bv_len = strlen(hashedval);
            }
        }
    }
    return 0;
}

/*
 *  optimize_mods()
 *
 *  If the client send a string identical modifications we might
 *  be able to optimize it for add and delete operations:
 *
 *  mods[0].mod_op: LDAP_MOD_ADD
 *  mods[0].mod_type: uniqueMember
 *  mods[0].mod_values: <value_0>
 *  mods[1].mod_op: LDAP_MOD_ADD
 *  mods[1].mod_type: uniqueMember
 *  mods[1].mod_values: <value_1>
 *         ...
 *  mods[N].mod_op: LDAP_MOD_ADD
 *  mods[N].mod_type: uniqueMember
 *  mods[N]mod_values: <value_N>
 *
 *  Optimized to:
 *
 *  mods[0].mod_op: LDAP_MOD_ADD
 *  mods[0].mod_type: uniqueMember
 *  mods[0].mod_values: <value_0>
 *                      <value_1>
 *                      ...
 *                      <value_N>
 *
 *  We only optimize operations (ADDs and DELETEs) that are sequential.  We
 *  can not look at the all mods(non-sequentially) because we need to keep
 *  the order preserved, and keep processing to a minimum.
 */
static void
optimize_mods(Slapi_Mods *smods)
{
    LDAPMod *mod, *prev_mod;
    int mod_count = 0;
    int max_vals = 0;
    size_t i = 0;

    prev_mod = slapi_mods_get_first_mod(smods);
    while ((mod = slapi_mods_get_next_mod(smods))) {
        if (prev_mod &&
            (SLAPI_IS_MOD_ADD(prev_mod->mod_op) || SLAPI_IS_MOD_DELETE(prev_mod->mod_op)) &&
            (prev_mod->mod_op == mod->mod_op) &&
            (!strcasecmp(prev_mod->mod_type, mod->mod_type)))
        {
            /* Get the current number of mod values from the previous mod.  Do it once per attr */
            if (mod_count == 0) {
                for (; prev_mod->mod_bvalues != NULL && prev_mod->mod_bvalues[mod_count] != NULL; mod_count++)
                    ;
                if (mod_count == 0) {
                    /* The previous mod did not contain any values, so lets move to the next mod */
                    prev_mod = mod;
                    continue;
                }
            }
            /* Add the values from the current mod to the prev mod */
            for (i = 0; mod->mod_bvalues != NULL && mod->mod_bvalues[i] != NULL; i++) {
                bervalarray_add_berval_fast(&(prev_mod->mod_bvalues), mod->mod_bvalues[i], mod_count, &max_vals);
                mod_count++;
            }
            if (i > 0) {
                /* Ok, we did optimize the "mod" values, so set the current mod to be ignored */
                mod->mod_op = LDAP_MOD_IGNORE;
            } else {
                /* No mod values, probably a full delete of the attribute... reset counters and move on */
                mod_count = max_vals = 0;
                prev_mod = mod;
            }
        } else {
            /* no match, reset counters and move on */
            mod_count = max_vals = 0;
            prev_mod = mod;
        }
    }
}
