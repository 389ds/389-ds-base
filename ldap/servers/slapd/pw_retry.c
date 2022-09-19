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

/* pw_retry.c
*/

#include <time.h>
#include "slap.h"

/****************************************************************************/
/* prototypes                                                               */
/****************************************************************************/
/* Slapi_Entry *get_entry ( Slapi_PBlock *pb, const char *dn ); */
static int set_retry_cnt(Slapi_PBlock *pb, int count);
static int set_retry_cnt_and_time(Slapi_PBlock *pb, int count, time_t cur_time);
static int set_tpr_usecount(Slapi_PBlock *pb, int count);

/*
 * update_pw_retry() is called when bind operation fails
 * with LDAP_INVALID_CREDENTIALS (in backend bind.c ).
 * It checks to see if the retry count can be reset,
 * increments retry count, and then check if need to lock the acount.
 * To have a global password policy, these mods should be chained to the
 * master, and not applied locally. If they are applied locally, they should
 * not get replicated from master...
 */

int
update_pw_retry(Slapi_PBlock *pb)
{
    Slapi_Entry *e;
    int retry_cnt = 0;
    time_t reset_time;
    time_t cur_time;
    char *cur_time_str = NULL;
    char *retryCountResetTime;
    int passwordRetryCount;
    int rc = 0;

    /* get the entry */
    e = get_entry(pb, NULL);
    if (e == NULL) {
        return (1);
    }

    cur_time = slapi_current_utc_time();

    /* check if the retry count can be reset. */
    retryCountResetTime = (char *)slapi_entry_attr_get_ref(e, "retryCountResetTime");
    if (retryCountResetTime != NULL) {
        reset_time = parse_genTime(retryCountResetTime);
        cur_time_str = format_genTime(cur_time);
        if (difftime(parse_genTime(cur_time_str), reset_time) >= 0) {
            /* set passwordRetryCount to 1 */
            /* reset retryCountResetTime */
            rc = set_retry_cnt_and_time(pb, 1, cur_time);
            slapi_ch_free((void **)&cur_time_str);
            slapi_entry_free(e);
            return (rc); /* success */
        } else {
            slapi_ch_free((void **)&cur_time_str);
        }
    } else {
        /* initialize passwordRetryCount and retryCountResetTime */
        rc = set_retry_cnt_and_time(pb, 1, cur_time);
        slapi_entry_free(e);
        return (rc); /* success */
    }
    passwordRetryCount = slapi_entry_attr_get_int(e, "passwordRetryCount");
    if (passwordRetryCount >= 0) {
        retry_cnt = passwordRetryCount + 1;
        if (retry_cnt == 1) {
            /* set retryCountResetTime */
            rc = set_retry_cnt_and_time(pb, retry_cnt, cur_time);
        } else {
            /* set passwordRetryCount to retry_cnt */
            rc = set_retry_cnt(pb, retry_cnt);
        }
    }
    slapi_entry_free(e);
    return rc;
}

/*
 * update_tpr_pw_usecount() is called during a bind operation.
 * The bind may later succeeds or fails, this function just record
 * an additional access to TPR userpassword
 * Returns
 *   LDAP_CONSTRAINT_VIOLATION if pwdTPRUseCount overpass TPR maxuse
 *   0 else
 */
int
update_tpr_pw_usecount(Slapi_PBlock *pb, Slapi_Entry *e, int32_t use_count)
{
    int rc = 0;

    if (e == NULL) {
        return (1);
    }

    if (slapi_entry_attr_hasvalue(e, "pwdTPRReset", "TRUE")) {
        /* This entry contains a OneTimePassword userpassword
         * as the bind failed, increase the passwordTPRRetryCount
         * and return a failure if the retryCount exceed the limit
         * set in the password policy
         */
        if (use_count >= 0) {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "update_tpr_pw_usecount",
                          "update pwdTPRUseCount=%d on entry (%s).\n",
                           use_count, slapi_entry_get_ndn(e));
            rc = set_tpr_usecount(pb, use_count);
        }
    }
    return rc;
}
static int
set_retry_cnt_and_time(Slapi_PBlock *pb, int count, time_t cur_time)
{
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Mods smods;
    time_t reset_time;
    char *timestr;
    passwdPolicy *pwpolicy = NULL;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    dn = slapi_sdn_get_dn(sdn);
    pwpolicy = new_passwdPolicy(pb, dn);
    slapi_mods_init(&smods, 0);

    reset_time = time_plus_sec(cur_time,
                               pwpolicy->pw_resetfailurecount);

    timestr = format_genTime(reset_time);
    slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "retryCountResetTime", timestr);
    slapi_ch_free((void **)&timestr);

    rc = set_retry_cnt_mods(pb, &smods, count);

    pw_apply_mods(sdn, &smods);
    slapi_mods_done(&smods);

    return rc;
}

/* update pwdTPRUseCount=count of the target entry
 * Returns
 *   LDAP_CONSTRAINT_VIOLATION if pwdTPRUseCount overpass TPR maxuse
 *   0 else
 */
int
set_tpr_usecount_mods(Slapi_PBlock *pb, Slapi_Mods *smods, int count)
{
    char retry_cnt[16] = {0}; /* 1-65535 */
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    passwdPolicy *pwpolicy = NULL;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    dn = slapi_sdn_get_dn(sdn);
    pwpolicy = new_passwdPolicy(pb, dn);

    if (smods) {
        sprintf(retry_cnt, "%d", count);
        slapi_mods_add_string(smods, LDAP_MOD_REPLACE, "pwdTPRUseCount", retry_cnt);
                slapi_log_err(SLAPI_LOG_TRACE,
                          "set_tpr_retry_cnt_mods",
                          "Unsuccessfull bind, increase pwdTPRUseCount = %d.\n", count);
        /* return a failure if it reaches the retry limit */
        if (count > pwpolicy->pw_tpr_maxuse) {
            slapi_log_err(SLAPI_LOG_INFO,
                          "set_tpr_retry_cnt_mods",
                          "Unsuccessfull bind, LDAP_CONSTRAINT_VIOLATION pwdTPRUseCount %d > %d.\n",
                          count,
                          pwpolicy->pw_tpr_maxuse);
            rc = LDAP_CONSTRAINT_VIOLATION;
        }
    }
    /* covscan false positive: new_passwdPolicy anchor the policy in the pblock */
    /* coverity[leaked_storage] */
    return rc;
}

int
set_retry_cnt_mods(Slapi_PBlock *pb, Slapi_Mods *smods, int count)
{
    char *timestr;
    time_t unlock_time;
    char retry_cnt[16] = {0}; /* 1-65535 */
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    passwdPolicy *pwpolicy = NULL;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    dn = slapi_sdn_get_dn(sdn);
    pwpolicy = new_passwdPolicy(pb, dn);

    if (smods) {
        sprintf(retry_cnt, "%d", count);
        slapi_mods_add_string(smods, LDAP_MOD_REPLACE, "passwordRetryCount", retry_cnt);
        /* lock account if reache retry limit */
        if (count >= pwpolicy->pw_maxfailure) {
            /* Remove lock_account function to perform all mods at once */
            /* lock_account ( pb ); */
            /* reach the retry limit, lock the account  */
            if (pwpolicy->pw_unlock == 0) {
                /* lock until admin reset password */
                unlock_time = NO_TIME;
            } else {
                unlock_time = time_plus_sec(slapi_current_utc_time(), pwpolicy->pw_lockduration);
            }
            timestr = format_genTime(unlock_time);
            slapi_mods_add_string(smods, LDAP_MOD_REPLACE, "accountUnlockTime", timestr);
            slapi_ch_free((void **)&timestr);
            rc = LDAP_CONSTRAINT_VIOLATION;
        }
    }
    return rc;
}

static int
set_retry_cnt(Slapi_PBlock *pb, int count)
{
    Slapi_DN *sdn = NULL;
    Slapi_Mods smods;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_mods_init(&smods, 0);
    rc = set_retry_cnt_mods(pb, &smods, count);
    pw_apply_mods(sdn, &smods);
    slapi_mods_done(&smods);
    return rc;
}

/* update pwdTPRUseCount=count of the target entry
 * Returns
 *   LDAP_CONSTRAINT_VIOLATION if pwdTPRUseCount overpass TPR maxuse
 *   0 else
 */
static int
set_tpr_usecount(Slapi_PBlock *pb, int count)
{
    Slapi_DN *sdn = NULL;
    Slapi_Mods smods;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_mods_init(&smods, 0);
    rc = set_tpr_usecount_mods(pb, &smods, count);
    pw_apply_mods(sdn, &smods);
    slapi_mods_done(&smods);
    return rc;
}


/*
 * If "dn" is passed, get_entry returns an entry which dn is "dn".
 * If "dn" is not passed, it returns an entry which dn is set in
 * SLAPI_TARGET_SDN in pblock.
 * Note: pblock is not mandatory for get_entry (e.g., new_passwdPolicy).
 */
Slapi_Entry *
get_entry(Slapi_PBlock *pb, const char *dn)
{
    int search_result = 0;
    Slapi_Entry *retentry = NULL;
    Slapi_DN *target_sdn = NULL;
    const char *target_dn = dn;
    Slapi_DN sdn;

    if (pb) {
        slapi_pblock_get(pb, SLAPI_TARGET_SDN, &target_sdn);
        if (target_dn == NULL) {
            target_dn = slapi_sdn_get_dn(target_sdn);
        }
    }

    if (target_dn == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "get_entry", "No dn specified\n");
        goto bail;
    }

    if (target_dn == dn) { /* target_dn is NOT from target_sdn */
        slapi_sdn_init_dn_byref(&sdn, target_dn);
        target_sdn = &sdn;
    }

    search_result = slapi_search_internal_get_entry(target_sdn, NULL,
                                                    &retentry,
                                                    pw_get_componentID());
    if (search_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_TRACE, "get_entry", "Can't find entry '%s', err %d\n",
                      target_dn, search_result);
    }
    if (target_dn == dn) { /* target_dn is NOT from target_sdn */
        slapi_sdn_done(&sdn);
    }
bail:
    return retentry;
}

void
pw_apply_mods(const Slapi_DN *sdn, Slapi_Mods *mods)
{
    int res;

    if (mods && (slapi_mods_get_num_mods(mods) > 0)) {
        Slapi_PBlock *pb = slapi_pblock_new();
        /* We don't want to overwrite the modifiersname, etc. attributes,
         * so we set a flag for this operation */
        slapi_modify_internal_set_pb_ext(pb, sdn,
                                         slapi_mods_get_ldapmods_byref(mods),
                                         NULL,                         /* Controls */
                                         NULL,                         /* UniqueID */
                                         pw_get_componentID(),         /* PluginID */
                                         OP_FLAG_SKIP_MODIFIED_ATTRS); /* Flags */
        slapi_modify_internal_pb(pb);

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
        if (res != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_WARNING,
                          "pw_apply_mods", "Modify error %d on entry '%s'\n",
                          res, slapi_sdn_get_dn(sdn));
        }

        slapi_pblock_destroy(pb);
    }

    return;
}

/* Handle the component ID for the password policy */

static struct slapi_componentid *pw_componentid = NULL;

void
pw_set_componentID(struct slapi_componentid *cid)
{
    pw_componentid = cid;
}

struct slapi_componentid *
pw_get_componentID()
{
    return pw_componentid;
}
