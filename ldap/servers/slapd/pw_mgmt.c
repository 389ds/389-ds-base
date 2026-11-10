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

/* pw_mgmt.c
*/

#include <time.h>
#include <string.h>
#include "slap.h"

/****************************************************************************/
/* prototypes                                                               */
/****************************************************************************/

static int shadow_fixup_task_add(Slapi_PBlock *pb __attribute__((unused)),
                                 Slapi_Entry *e,
                                 Slapi_Entry *eAfter __attribute__((unused)),
                                 int *returncode,
                                 char *returntext __attribute__((unused)),
                                 void *arg);
static void shadow_fixup_task_thread(void *arg);
static void shadow_fixup_task_destructor(Slapi_Task *task);

typedef struct task_data {
    char *suffix;
    bool force;
} task_data;

typedef struct shadow_fixup_callback_data
{
    int32_t rc;
    uint32_t skipped;
    uint32_t stale;
    uint32_t fixed;
    Slapi_Task *task;
    task_data *task_data;
} shadow_fixup_callback_data;

/*
 * For chaining, particularly chain-on-update, we still need to check if the user's
 * password MUST be reset before proceeding.
 */
void
check_must_change_pw(Slapi_PBlock *pb, Slapi_Entry *e)
{
    passwdPolicy *pwpolicy = NULL;
    Connection *pb_conn = NULL;
    time_t pw_exp_date;
    char *passwordExpirationTime = NULL;

    if ((passwordExpirationTime = (char *)slapi_entry_attr_get_ref(e, "passwordExpirationTime"))) {
        pw_exp_date = parse_genTime(passwordExpirationTime);

        /* Check if password has been reset */
        if (pw_exp_date == NO_TIME) {
            pwpolicy = new_passwdPolicy(pb, slapi_entry_get_ndn(e));

            /* check if changing password is required */
            if (pwpolicy->pw_must_change) {
                /* set c_needpw for this connection to be true.  this client
                   now can only change its own password */
                slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
                if (pb_conn) {
                    pb_conn->c_needpw = 1;
                }
            }
        }
    }
/* covscan false positive: new_passwdPolicy anchor the policy in the pblock */
/* coverity[leaked_storage] */
}

/*
 * need_new_pw() is called when non rootdn bind operation succeeds with authentication
 *
 * Return  0 - password is okay
 * Return -1 - password is expired, abort bind
 */
int
need_new_pw(Slapi_PBlock *pb, Slapi_Entry *e, int pwresponse_req)
{
    time_t cur_time, pw_exp_date;
    Slapi_Mods smods;
    double diff_t = 0;
    char *cur_time_str = NULL;
    char *passwordExpirationTime = NULL;
    char *timestring;
    char *dn;
    const Slapi_DN *sdn;
    passwdPolicy *pwpolicy = NULL;
    int pwdGraceUserTime = 0;
    char graceUserTime[16] = {0};
    Connection *pb_conn = NULL;
    long t;
    int needpw = 0;

    if (NULL == e) {
        return (-1);
    }
    slapi_mods_init(&smods, 0);
    sdn = slapi_entry_get_sdn_const(e);
    dn = slapi_entry_get_ndn(e);
    pwpolicy = new_passwdPolicy(pb, dn);

    /* after the user binds with authentication, clear the retry count */
    if (pwpolicy->pw_lockout == 1) {
        if (slapi_entry_attr_get_int(e, "passwordRetryCount") > 0) {
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordRetryCount", "0");
        }
    }

    cur_time = slapi_current_utc_time();

    /* get passwordExpirationTime attribute */
    passwordExpirationTime = (char *)slapi_entry_attr_get_ref(e, "passwordExpirationTime");

    if (passwordExpirationTime == NULL) {
        /* password expiration date is not set.
         * This is ok for data that has been loaded via ldif2ldbm
         * Set expiration time if needed,
         * don't do further checking and return 0 */
        if (pwpolicy->pw_exp == 1) {
            pw_exp_date = time_plus_sec(cur_time, pwpolicy->pw_maxage);

            timestring = format_genTime(pw_exp_date);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestring);
            slapi_ch_free_string(&timestring);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpWarned", "0");

            pw_apply_mods(sdn, &smods);
        } else if (pwpolicy->pw_lockout == 1) {
            pw_apply_mods(sdn, &smods);
        }
        slapi_mods_done(&smods);
        /* new_passwdPolicy registers the policy in the pblock so there is no leak */
        /* coverity[leaked_storage] */
        return (0);
    }

    pw_exp_date = parse_genTime(passwordExpirationTime);

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    if (pb_conn) {
        needpw = pb_conn->c_needpw;
    }

    /* Check if password has been reset */
    if (pw_exp_date == NO_TIME) {

        /* check if changing password is required */
        if (pwpolicy->pw_must_change) {
            /* set c_needpw for this connection to be true.  this client
               now can only change its own password */
            if (pb_conn){
                pb_conn->c_needpw = needpw = 1;
            } else {
                needpw = 1;
            }
            /* We need to include "changeafterreset" error in
             * passwordpolicy response control. So, we will not be
             * done here. We remember this scenario by (c_needpw=1)
             * and check it before sending the control from various
             * places. We will also add LDAP_CONTROL_PWEXPIRED control
             * as the return value used to be (1).
             */
            goto skip;
        }
        /* Mark that first login occured */
        pw_exp_date = NOT_FIRST_TIME;
        timestring = format_genTime(pw_exp_date);
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestring);
        slapi_ch_free_string(&timestring);
    }

skip:
    /* if password never expires, don't need to go on; return 0 */
    if (pwpolicy->pw_exp == 0) {
        /* check for "changeafterreset" condition */
        if (needpw == 1) {
            if (pwresponse_req) {
                slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_CHGAFTERRESET);
            }
            slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
        }
        pw_apply_mods(sdn, &smods);
        slapi_mods_done(&smods);
        /* new_passwdPolicy registers the policy in the pblock so there is no leak */
        /* coverity[leaked_storage] */
        return (0);
    }

    /* check if password expired.  If so, abort bind. */
    cur_time_str = format_genTime(cur_time);
    if ((pw_exp_date != NO_TIME) && (pw_exp_date != NOT_FIRST_TIME) &&
        (diff_t = difftime(pw_exp_date, parse_genTime(cur_time_str))) <= 0) {
        slapi_ch_free_string(&cur_time_str); /* only need this above */
        /* password has expired. Check the value of
         * passwordGraceUserTime and compare it
         * against the value of passwordGraceLimit */
        pwdGraceUserTime = slapi_entry_attr_get_int(e, "passwordGraceUserTime");
        if (pwpolicy->pw_gracelimit > pwdGraceUserTime) {
            pwdGraceUserTime++;
            sprintf(graceUserTime, "%d", pwdGraceUserTime);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE,
                                  "passwordGraceUserTime", graceUserTime);
            pw_apply_mods(sdn, &smods);
            slapi_mods_done(&smods);
            if (pwresponse_req) {
                /* check for "changeafterreset" condition */
                if (needpw == 1) {
                    slapi_pwpolicy_make_response_control(pb, -1,
                                                         ((pwpolicy->pw_gracelimit) - pwdGraceUserTime),
                                                         LDAP_PWPOLICY_CHGAFTERRESET);
                } else {
                    slapi_pwpolicy_make_response_control(pb, -1,
                                                         ((pwpolicy->pw_gracelimit) - pwdGraceUserTime),
                                                         -1);
                }
            }
            slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                          "Password expired, but within grace limit (curr=%d limit=%d needpw=%d): Entry (%s) Policy (%s)\n",
                          pwpolicy->pw_gracelimit, pwdGraceUserTime, needpw, dn,
                          pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
            slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
            /* new_passwdPolicy registers the policy in the pblock so there is no leak */
            /* coverity[leaked_storage] */
            return (0);
        }

        /* password expired and user exceeded limit of grace attempts.
         * Send result and also the control */
        if (pwresponse_req) {
            slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_PWDEXPIRED);
        }
        slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
        if (pb_conn) {
            bind_credentials_clear(pb_conn, PR_FALSE, PR_TRUE);
        }
        slapi_send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL,
                               "password expired!", 0, NULL);
        slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                      "Password expired: Entry (%s) Policy (%s)\n",
                      dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");

        /* abort bind */
        /* pass pb to do_unbind().  pb->pb_op->o_opid and
           pb->pb_op->o_tag are not right but I don't see
           do_unbind() checking for those.   We might need to
           create a pb for unbind operation.  Also do_unbind calls
           pre and post ops.  Maybe we don't want to call them */
        if (pb_conn && (LDAP_VERSION2 == pb_conn->c_ldapversion)) {
            Operation *pb_op = NULL;
            slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
            if (pb_op) {
                /* We close the connection only with LDAPv2 connections */
                disconnect_server(pb_conn, pb_op->o_connid,
                                  pb_op->o_opid, SLAPD_DISCONNECT_UNBIND, 0);
            }
        }
        /* Apply current modifications */
        pw_apply_mods(sdn, &smods);
        slapi_mods_done(&smods);
        /* new_passwdPolicy registers the policy in the pblock so there is no leak */
        /* coverity[leaked_storage] */
        return (-1);
    }
    slapi_ch_free((void **)&cur_time_str);

    /* check if password is going to expire within "passwordWarning" */
    /* Note that if pw_exp_date is NO_TIME or NOT_FIRST_TIME,
     * we must send warning first and this changes the expiration time.
     * This is done just below since diff_t is 0
     */
    if (diff_t <= pwpolicy->pw_warning) {
        int pw_exp_warned = 0;

        pw_exp_warned = slapi_entry_attr_get_int(e, "passwordExpWarned");
        if (!pw_exp_warned) {
            /* first time send out a warning */
            /* reset the expiration time to current + warning time
             * and set passwordExpWarned to true
             */
            if (needpw != 1) {
                pw_exp_date = time_plus_sec(cur_time, pwpolicy->pw_warning);
            }

            timestring = format_genTime(pw_exp_date);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestring);
            slapi_ch_free_string(&timestring);

            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpWarned", "1");

            t = pwpolicy->pw_warning;

        } else {
            t = (long)diff_t; /* jcm: had to cast double to long */
        }

        pw_apply_mods(sdn, &smods);
        slapi_mods_done(&smods);
        if (pwresponse_req) {
            /* check for "changeafterreset" condition */
            if (needpw == 1) {
                slapi_pwpolicy_make_response_control(pb, t, -1, LDAP_PWPOLICY_CHGAFTERRESET);
            } else {
                slapi_pwpolicy_make_response_control(pb, t, -1, -1);
            }
        }

        if (needpw == 1) {
            slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
        } else {
            slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRING, t);
        }
        /* new_passwdPolicy registers the policy in the pblock so there is no leak */
        /* coverity[leaked_storage] */
        return (0);
    } else {
        if (pwresponse_req && pwpolicy->pw_send_expiring) {
            slapi_pwpolicy_make_response_control(pb, diff_t, -1, -1);
            slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRING, diff_t);
        }
    }

    pw_apply_mods(sdn, &smods);
    slapi_mods_done(&smods);
    /* Leftover from "changeafterreset" condition */
    if (needpw == 1) {
        slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
    }
    /* passes checking, return 0 */
    /* new_passwdPolicy registers the policy in the pblock so there is no leak */
    /* coverity[leaked_storage] */
    return (0);
}

/* Called once from main */
void
pw_init(void)
{
    pw_set_componentID(generate_componentid(NULL, COMPONENT_PWPOLICY));

#if defined(USE_OLD_UNHASHED)
    slapi_add_internal_attr_syntax(PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
                                   PSEUDO_ATTR_UNHASHEDUSERPASSWORD_OID,
                                   OCTETSTRING_SYNTAX_OID, 0,
                                   /* Clients don't need to directly modify
                                     * PSEUDO_ATTR_UNHASHEDUSERPASSWORD */
                                   SLAPI_ATTR_FLAG_NOUSERMOD |
                                       SLAPI_ATTR_FLAG_NOEXPOSE);
#endif

    slapi_task_register_handler("fixup shadow attributes",
                                shadow_fixup_task_add);
}

/*
 * ShadowLastChange fixup task fucntions
 */
static void
shadow_fixup_task_destructor(Slapi_Task *task)
{
    if (task) {
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task to finish */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
        task_data *mytaskdata = (task_data *)slapi_task_get_data(task);
        slapi_ch_free_string(&mytaskdata->suffix);
        slapi_ch_free((void **)&mytaskdata);
    }
}

static int
shadow_fixup_task_add(Slapi_PBlock *pb __attribute__((unused)),
                      Slapi_Entry *e,
                      Slapi_Entry *eAfter __attribute__((unused)),
                      int *returncode,
                      char *returntext __attribute__((unused)),
                      void *arg)
{
    PRThread *thread = NULL;
    Slapi_Task *task = NULL;
    Slapi_DN *sdn = NULL;
    task_data *mytaskdata = NULL;
    char *suffix = NULL;
    bool force = false;
    mapping_tree_node *mtn = NULL;
    int rc = SLAPI_DSE_CALLBACK_OK;

    *returncode = LDAP_SUCCESS;

    /* get our suffix */
    suffix = slapi_entry_attr_get_charptr(e, "suffix");
    if (suffix == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "shadow_fixup_task_add",
            "Missing suffix attribute in task entry - aborting fix up task.\n");
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    sdn = slapi_sdn_new_dn_byval(suffix);
    if (sdn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "shadow_fixup_task_add",
            "Specified suffix is not a valid DN: '%s' - aborting fix up task.\n",
            suffix);
        slapi_sdn_free(&sdn);
        slapi_ch_free_string(&suffix);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    mtn = slapi_get_mapping_tree_node_by_dn(sdn);
    if (mtn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "shadow_fixup_task_add",
            "Failed to get mapping tree node for suffix: '%s' - aborting fix up task.\n",
            suffix);
        slapi_sdn_free(&sdn);
        slapi_ch_free_string(&suffix);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    slapi_sdn_free(&sdn);

    /* Are we forcing the fixup on all entries? */
    force = slapi_entry_attr_get_bool_ext(e, "force", PR_FALSE);

    mytaskdata = (task_data *)slapi_ch_calloc(1, sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        slapi_ch_free_string(&suffix);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    mytaskdata->suffix = suffix;
    mytaskdata->force = force;

    /* allocate new task now */
    task = slapi_plugin_new_task(slapi_entry_get_ndn(e), arg);
    slapi_task_set_destructor_fn(task, shadow_fixup_task_destructor);
    slapi_task_set_data(task, (void *)mytaskdata);

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, shadow_fixup_task_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "shadow_fixup_task_add",
                      "Unable to create task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        slapi_task_finish(task, *returncode);
        rc = SLAPI_DSE_CALLBACK_ERROR;
    } else {
        rc = SLAPI_DSE_CALLBACK_OK;
    }

    return rc;
}

/* helper function to get max age from pwp policy (global or local) */
static int64_t
pwp_get_max_age(char *dn)
{
    if (config_get_pwpolicy_local() == 1) {
        /* Return localpwp max age (if present) */
        passwdPolicy *pwpolicy = NULL;
        int64_t maxAge = 0;

        pwpolicy = new_passwdPolicy(NULL, dn);
        maxAge = pwpolicy->pw_maxage;
        delete_passwdPolicy(&pwpolicy);
        return maxAge;
    } else {
        /* Return global pwp max age */
        return config_get_pw_maxage();
    }
}



static void
get_shadow_fixup_result(int rc, void *cb_data)
{
    /* We only record a failure so we can log that the task was incomplete */
    if (rc != 0) {
        ((shadow_fixup_callback_data *)cb_data)->rc = rc;
    }
}

static int
shadow_fixup_callback(Slapi_Entry *e, void *callback_data)
{
    Slapi_PBlock *mod_pb = NULL;
    Slapi_Mods smods;
    time_t cur_time;
    time_t ctime;
    int64_t lastChangeTime = -1;
    char *pwdExpireTime = NULL;
    char *timestr = NULL;
    int rc = 0;
    shadow_fixup_callback_data *cb_data = (shadow_fixup_callback_data *)callback_data;

    if (slapi_is_shutting_down()) {
        slapi_task_log_notice(cb_data->task,
            "ShadowLastChange fixup task aborted due to server shutdown");
        slapi_task_log_status(cb_data->task,
            "ShadowLastChange fixup task aborted due to server shutdown");
        slapi_log_err(SLAPI_LOG_ERR, "shadow_fixup_callback",
            "ShadowLastChange fixup task aborted due to server shutdown\n");
        cb_data->rc = -1;
        return -1;
    }

    if (!slapi_entry_attr_get_ref(e, "userpassword")) {
        /* No userpassword, then there is nothing to fixup */
        cb_data->skipped++;
        return 0;
    }
    pwdExpireTime = (char *)slapi_entry_attr_get_ref(e, "passwordExpirationTime");
    if (slapi_entry_attr_get_ref(e, "shadowLastChange")) {
        lastChangeTime = slapi_entry_attr_get_longlong(e, "shadowLastChange");
    }

    if (lastChangeTime != -1 && pwdExpireTime && !cb_data->task_data->force) {
        /* Check if the current ShadowlastChange time is stale. Determine
         * the password update time by using passwordExpirationTime and pwp
         * max age. If the ShadowlastChange time is stale, update it. */
        int64_t maxAge = pwp_get_max_age(slapi_entry_get_dn(e));
        time_t pwdUpdateTime = parse_genTime(pwdExpireTime);

        if (pwdUpdateTime == NO_TIME || pwdUpdateTime == NOT_FIRST_TIME ||
            (pwdUpdateTime - maxAge) / _SEC_PER_DAY == lastChangeTime)
        {
            /* ShadowLastChange time is correct, skip this entry */
            cb_data->skipped++;
            return 0;
        } else {
            timestr = slapi_ch_smprintf("%ld", (pwdUpdateTime - maxAge) / _SEC_PER_DAY);
            cb_data->stale++;
        }
    } else if (lastChangeTime != -1 && !cb_data->task_data->force) {
        /* ShadowLastChange time is present, skip this entry */
        cb_data->skipped++;
        return 0;
    } else {
        /* Build the timestamp for the shadowLastChange attribute */
        cur_time = slapi_current_utc_time();
        ctime = cur_time / _SEC_PER_DAY;
        timestr = slapi_ch_smprintf("%ld", ctime);
    }

    /* Update the entry */
    mod_pb = slapi_pblock_new();
    slapi_mods_init(&smods, 0);
    slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "shadowLastChange",
                          timestr);

    slapi_modify_internal_set_pb_ext(mod_pb, slapi_entry_get_sdn(e),
                                     slapi_mods_get_ldapmods_byref(&smods),
                                     NULL,
                                     NULL,
                                     pw_get_componentID(),
                                     OP_FLAG_SKIP_MODIFIED_ATTRS |
                                     OP_FLAG_ACTION_SKIP_PWDPOLICY);
    slapi_modify_internal_pb(mod_pb);
    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_WARNING, "shadow_fixup_task_thread",
                      "Failed to update ShadowLastChange for entry: '%s' - error: %d\n",
                      slapi_entry_get_dn(e), rc);
        cb_data->skipped++;
        cb_data->rc = rc;
    } else {
        cb_data->fixed++;
    }
    slapi_ch_free_string(&timestr);
    slapi_mods_done(&smods);
    slapi_pblock_destroy(mod_pb);

    if ((cb_data->fixed + cb_data->skipped) % 1000 == 0) {
        slapi_task_log_notice(cb_data->task,
            "ShadowLastChange fixup task processed %d entries",
            cb_data->fixed + cb_data->skipped);
    }

    return 0;
}

static void
shadow_fixup_task_thread(void *arg)
{
    slapi_set_thread_name("shadow-fixup");
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Task *task = (Slapi_Task *)arg;
    task_data *mytaskdata = NULL;
    shadow_fixup_callback_data callback_data = {0};
    char *attrs[] = {"shadowLastChange", "passwordExpirationTime", "userpassword", NULL};
    int fixup_count = 0;
    int skip_count = 0;
    int stale_count = 0;
    int result = 0;

    slapi_task_inc_refcount(task);

    /* Fetch our task data from the task */
    mytaskdata = (task_data *)slapi_task_get_data(task);
    callback_data.task = task;
    callback_data.task_data = mytaskdata;

    /* Log start message. */
    slapi_task_begin(task, 1);
    slapi_task_log_notice(task, "ShadowLastChange fixup task starting (suffix: \"%s\"%s) ...",
                          mytaskdata->suffix, mytaskdata->force ? " (force)" : "");
    slapi_log_err(SLAPI_LOG_INFO, "shadow_fixup_task_thread",
                  "ShadowLastChange fixup task starting (suffix: \"%s\"%s) ...\n",
                  mytaskdata->suffix, mytaskdata->force ? " (force)" : "");

    /* Search for entries that contain objectclass ShadowAccount */
    slapi_search_internal_set_pb(pb, mytaskdata->suffix, LDAP_SCOPE_SUBTREE,
                                 "(objectclass=ShadowAccount)", attrs, 0, 0, 0,
                                 (void *)pw_get_componentID(), 0);

    slapi_search_internal_callback_pb(pb, &callback_data,
                                      get_shadow_fixup_result,
                                      shadow_fixup_callback, 0);

    /* Gather our callback data */
    result = callback_data.rc;
    fixup_count = callback_data.fixed;
    skip_count = callback_data.skipped;
    stale_count = callback_data.stale;

    if (mytaskdata->force) {
        slapi_task_log_notice(task,
                "ShadowLastChange fixup task finished. Updated %d entries, skipped %d entries.",
                fixup_count, skip_count);
        slapi_task_log_status(task,
                "ShadowLastChange fixup task finished. Updated %d entries, skipped %d entries.",
                fixup_count, skip_count);
        slapi_log_err(SLAPI_LOG_INFO, "shadow_fixup_task_thread",
                "ShadowLastChange fixup task finished.  Updated %d entries, skipped %d entries.\n",
                fixup_count, skip_count);
    } else {
        slapi_task_log_notice(task,
                "ShadowLastChange fixup task finished. Updated %d entries (%d stale shadowLastChange, %d missing shadowLastChange), skipped %d entries.",
                fixup_count, stale_count, fixup_count - stale_count, skip_count);
        slapi_task_log_status(task,
                "ShadowLastChange fixup task finished. Updated %d entries (%d stale shadowLastChange, %d missing shadowLastChange), skipped %d entries.",
                fixup_count, stale_count, fixup_count - stale_count, skip_count);
        slapi_log_err(SLAPI_LOG_INFO, "shadow_fixup_task_thread",
                "ShadowLastChange fixup task finished.  Updated %d entries (%d stale shadowLastChange, %d missing shadowLastChange), skipped %d entries.\n",
                fixup_count, stale_count, fixup_count - stale_count, skip_count);
    }
    if (result != LDAP_SUCCESS) {
        slapi_task_log_notice(task,
                "ShadowLastChange fixup task was incomplete and may need to be rerun");
        slapi_task_log_status(task,
                "ShadowLastChange fixup task was incomplete and may need to be rerun");
        slapi_log_err(SLAPI_LOG_ERR, "shadow_fixup_task_thread",
                "ShadowLastChange fixup task was incomplete and may need to be rerun\n");
    }
    slapi_task_inc_progress(task);

    slapi_pblock_destroy(pb);
    slapi_task_finish(task, result);
    slapi_task_dec_refcount(task);
}
