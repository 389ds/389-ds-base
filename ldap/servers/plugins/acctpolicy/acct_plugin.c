/******************************************************************************
Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
Copyright (C) 2023 Red Hat, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Contributors:
Hewlett-Packard Development Company, L.P.
******************************************************************************/

/* Account Policy plugin */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "acctpolicy.h"
#include "slapi-plugin.h"

static int
acct_update_login_history(const char *, char *);

/*
 * acct_policy_dn_is_config()
 *
 * Checks if dn is a plugin config entry.
 */
static int
acct_policy_dn_is_config(Slapi_DN *sdn)
{
    int ret = 0;

    slapi_log_err(SLAPI_LOG_TRACE, PLUGIN_NAME,
                  "--> acct_policy_dn_is_config\n");

    if (sdn == NULL) {
        goto bail;
    }

    /* If an alternate config area is configured, treat it's child
     * entries as config entries.  If the alternate config area is
     * not configured, treat children of the top-level plug-in
     * config entry as our config entries. */
    if (acct_policy_get_config_area()) {
        if (slapi_sdn_issuffix(sdn, acct_policy_get_config_area()) &&
            slapi_sdn_compare(sdn, acct_policy_get_config_area())) {
            ret = 1;
        }
    } else {
        if (slapi_sdn_issuffix(sdn, acct_policy_get_plugin_sdn()) &&
            slapi_sdn_compare(sdn, acct_policy_get_plugin_sdn())) {
            ret = 1;
        }
    }

bail:
    slapi_log_err(SLAPI_LOG_TRACE, PLUGIN_NAME,
                  "<-- acct_policy_dn_is_config\n");

    return ret;
}

/*
  Checks bind entry for last login state and compares current time with last
  login time plus the limit to decide whether to deny the bind.
*/
static int
acct_inact_limit(Slapi_PBlock *pb, const char *dn, Slapi_Entry *target_entry, acctPolicy *policy)
{
    char *lasttimestr = NULL;
    time_t lim_t, last_t, cur_t;
    int rc = 0; /* Optimistic default */
    acctPluginCfg *cfg;

    cur_t = slapi_current_utc_time();

    config_rd_lock();
    cfg = get_config();

    if (cfg->check_all_state_attrs) {
        /*
         * Check both state and alternate state attributes.
         */
        if ((lasttimestr = get_attr_string_val(target_entry, cfg->state_attr_name)) != NULL) {
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" login timestamp is %s (found in attribute '%s')\n",
                          dn, lasttimestr, cfg->state_attr_name);
            last_t = gentimeToEpochtime(lasttimestr);
            lim_t = policy->inactivitylimit;
            slapi_ch_free_string(&lasttimestr);

            /* Finally do the time comparison */
            if (cur_t > last_t + lim_t) {
                slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                              "acct_inact_limit - \"%s\" has exceeded inactivity limit  (%ld > (%ld + %ld))\n",
                              dn, cur_t, last_t, lim_t);
                rc = 1;
                goto done;
            }
        }

        /* Check alternate state attribute next... */
        if (cfg->alt_state_attr_name &&
                ((lasttimestr = get_attr_string_val(target_entry, cfg->alt_state_attr_name)) == NULL))
        {
            goto done;
        }
        slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                      "acct_inact_limit - \"%s\" alternate timestamp is %s (found in attribute '%s')\n",
                      dn, lasttimestr, cfg->alt_state_attr_name);
        last_t = gentimeToEpochtime(lasttimestr);
        lim_t = policy->inactivitylimit;
        slapi_ch_free_string(&lasttimestr);

        /* Finally do the time comparison */
        if (cur_t > last_t + lim_t) {
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" has exceeded inactivity limit  (%ld > (%ld + %ld))\n",
                          dn, cur_t, last_t, lim_t);
            rc = 1;
            goto done;
        }
        slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                      "acct_inact_limit - \"%s\" is within inactivity limit (%ld < (%ld + %ld))\n",
                      dn, cur_t, last_t, lim_t);
    } else {
        /*
         * Check state attribute, if not present in entry only then try
         * alternate state attribute
         */
        if ((lasttimestr = get_attr_string_val(target_entry, cfg->state_attr_name)) != NULL) {
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" login timestamp is %s (found in attribute '%s')\n",
                          dn, lasttimestr, cfg->state_attr_name);
        } else if (cfg->alt_state_attr_name &&
            ((lasttimestr = get_attr_string_val(target_entry, cfg->alt_state_attr_name)) != NULL))
        {
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" alternate timestamp is %s (found in attribute '%s')\n",
                          dn, lasttimestr, cfg->alt_state_attr_name);
        } else {
            /*
             * The primary or alternate attribute might not yet exist eg.
             * if only lastlogintime is specified and it is the first login
             */
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" has no value for stateattr or altstateattr \n", dn);
            goto done;
        }

        last_t = gentimeToEpochtime(lasttimestr);
        lim_t = policy->inactivitylimit;
        slapi_ch_free_string(&lasttimestr);

        /* Finally do the time comparison */
        if (cur_t > last_t + lim_t) {
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" has exceeded inactivity limit  (%ld > (%ld + %ld))\n",
                          dn, cur_t, last_t, lim_t);
            rc = 1;
            goto done;
        } else {
            slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                          "acct_inact_limit - \"%s\" is within inactivity limit (%ld < (%ld + %ld))\n",
                          dn, cur_t, last_t, lim_t);
        }
    }
done:
    config_unlock();
    /* Deny bind; the account has exceeded the inactivity limit */
    if (rc == 1) {
        slapi_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                               "Account inactivity limit exceeded."
                               " Contact system administrator to reset.",
                               0, NULL);
    }

    return (rc);
}

/*
  Preserve bind time stamps in virtual attribute
*/
int
acct_update_login_history(const char *dn, char *timestr)
{
    void *plugin_id = NULL;
    int rc = -1;
    int num_entries = 0;
    size_t i = 0;
    char **login_hist = NULL;
    Slapi_PBlock *entry_pb = NULL;
    Slapi_PBlock *mod_pb;
    Slapi_Entry *e = NULL;
    Slapi_DN *sdn = NULL;
    acctPluginCfg *cfg;
    LDAPMod attribute;
    LDAPMod *list_of_mods[2];

    plugin_id = get_identity();

    sdn = slapi_sdn_new_normdn_byref(dn);
    slapi_search_get_entry(&entry_pb, sdn, NULL, &e, plugin_id);

    if (!timestr) {
        return (rc);
    }

    config_rd_lock();
    cfg = get_config();

    /* get login history */
    login_hist = slapi_entry_attr_get_charray_ext(e, cfg->login_history_attr, &num_entries);

    /* first time round */
    if (!login_hist || !num_entries) {
        login_hist = (char **)slapi_ch_calloc(2, sizeof(char *));
    }

    /* Do we need to resize login_hist array */
    if (num_entries >= cfg->login_history_size) {
        int diff = (num_entries - cfg->login_history_size);
        /* free times we dont need */
        for (i = 0; i <= diff; i++) {
            slapi_ch_free_string(&login_hist[i]);
        }
        /* remap array*/
        for (i = 0; i < (cfg->login_history_size - 1); i++) {
            login_hist[i] = login_hist[(diff + 1) + i];
        }
        /* expand array and add current time string at the end */
        login_hist = (char **)slapi_ch_realloc((char *)login_hist, sizeof(char *) * (cfg->login_history_size + 1));
        login_hist[i] = slapi_ch_smprintf("%s", timestr);
        login_hist[i + 1] = NULL;
    } else {
        /* expand array and add current time string at the end */
        login_hist = (char **)slapi_ch_realloc((char *)login_hist, sizeof(char *) * (num_entries + 2));
        login_hist[num_entries] = slapi_ch_smprintf("%s", timestr);
        login_hist[num_entries + 1] = NULL;
    }

    /* modify the attribute */
    attribute.mod_type = cfg->login_history_attr;
    attribute.mod_op = LDAP_MOD_REPLACE;
    attribute.mod_values = login_hist;

    list_of_mods[0] = &attribute;
    list_of_mods[1] = NULL;

    mod_pb = slapi_pblock_new();
    slapi_modify_internal_set_pb(mod_pb, dn, list_of_mods, NULL, NULL, plugin_id, 0);
    slapi_modify_internal_pb(mod_pb);
    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "acct_update_login_history", "Modify error %d on entry '%s'\n", rc, dn);
    }

    config_unlock();

    slapi_ch_array_free(login_hist);
    slapi_search_get_entry_done(&entry_pb);
    slapi_sdn_free(&sdn);
    slapi_pblock_destroy(mod_pb);
    slapi_pblock_destroy(entry_pb);

    return (rc);
}

/*
  This is called after binds, it updates an attribute in the account
  and the login time history.
*/
static int
acct_record_login(const char *dn)
{
    int ldrc;
    int rc = 0; /* Optimistic default */
    LDAPMod *mods[2];
    LDAPMod mod;
    struct berval *vals[2];
    struct berval val;
    char *timestr = NULL;
    acctPluginCfg *cfg;
    void *plugin_id;
    Slapi_PBlock *modpb = NULL;
    int skip_mod_attrs = 1; /* value doesn't matter as long as not NULL */

    config_rd_lock();
    cfg = get_config();

    /* if we are not allowed to modify the state attr we're done
         * this could be intentional, so just return
         */
    if (!update_is_allowed_attr(cfg->always_record_login_attr))
        goto done;

    plugin_id = get_identity();

    timestr = epochtimeToGentime(slapi_current_utc_time());
    val.bv_val = timestr;
    val.bv_len = strlen(val.bv_val);

    vals[0] = &val;
    vals[1] = NULL;

    mod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
    mod.mod_type = cfg->always_record_login_attr;
    mod.mod_bvalues = vals;

    mods[0] = &mod;
    mods[1] = NULL;

    modpb = slapi_pblock_new();

    slapi_modify_internal_set_pb(modpb, dn, mods, NULL, NULL,
                                 plugin_id, SLAPI_OP_FLAG_NO_ACCESS_CHECK |
                                                SLAPI_OP_FLAG_BYPASS_REFERRALS);
    slapi_pblock_set(modpb, SLAPI_SKIP_MODIFIED_ATTRS, &skip_mod_attrs);
    slapi_modify_internal_pb(modpb);

    slapi_pblock_get(modpb, SLAPI_PLUGIN_INTOP_RESULT, &ldrc);

    if (ldrc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, POST_PLUGIN_NAME,
                      "acct_record_login - Recording %s=%s failed on \"%s\" err=%d\n", cfg->always_record_login_attr,
                      timestr, dn, ldrc);
        rc = -1;
        goto done;
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, POST_PLUGIN_NAME,
                      "acct_record_login - Recorded %s=%s on \"%s\"\n", cfg->always_record_login_attr, timestr, dn);

        /* update login history */
        acct_update_login_history(dn, timestr);
    }

done:
    config_unlock();
    slapi_pblock_destroy(modpb);
    slapi_ch_free_string(&timestr);

    return (rc);
}

/*
  Handles bind preop callbacks
*/
int
acct_bind_preop(Slapi_PBlock *pb)
{
    Slapi_PBlock *entry_pb = NULL;
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Entry *target_entry = NULL;
    int rc = 0; /* Optimistic default */
    int ldrc;
    acctPolicy *policy = NULL;
    void *plugin_id;

    slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                  "=> acct_bind_preop\n");

    plugin_id = get_identity();

    /* This does not give a copy, so don't free it */
    if (slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &sdn) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PRE_PLUGIN_NAME,
                      "acct_bind_preop - Error retrieving target DN\n");
        rc = -1;
        goto done;
    }
    dn = slapi_sdn_get_dn(sdn);

    /* The plugin wouldn't get called for anonymous binds but let's check */
    if (dn == NULL) {
        goto done;
    }

    ldrc = slapi_search_get_entry(&entry_pb, sdn, NULL, &target_entry, plugin_id);

    /* There was a problem retrieving the entry */
    if (ldrc != LDAP_SUCCESS) {
        if (ldrc != LDAP_NO_SUCH_OBJECT) {
            /* The problem is not a bad bind or virtual entry; halt bind */
            slapi_log_err(SLAPI_LOG_ERR, PRE_PLUGIN_NAME,
                          "acct_bind_preop - Failed to retrieve entry \"%s\": %d\n", dn, ldrc);
            rc = -1;
        }
        goto done;
    }

    if (get_acctpolicy(pb, target_entry, plugin_id, &policy)) {
        slapi_log_err(SLAPI_LOG_ERR, PRE_PLUGIN_NAME,
                      "acct_bind_preop - Account Policy object for \"%s\" is missing\n", dn);
        rc = -1;
        goto done;
    }

    /* Null policy means target isnt's under the influence of a policy */
    if (policy == NULL) {
        slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                      "acct_bind_preop - \"%s\" is not governed by an account policy\n", dn);
        goto done;
    }

    /* Check whether the account is in violation of inactivity limit */
    rc = acct_inact_limit(pb, dn, target_entry, policy);

/* ...Any additional account policy enforcement goes here... */

done:
    /* Internal error */
    if (rc == -1) {
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL);
    }

    slapi_search_get_entry_done(&entry_pb);

    free_acctpolicy(&policy);

    slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                  "<= acct_bind_preop\n");

    return (rc == 0 ? CALLBACK_OK : CALLBACK_ERR);
}

/*
  This is called after binds, it updates an attribute in the entry that the
  bind DN corresponds to with the current time if it has an account policy
  specifier.
*/
int
acct_bind_postop(Slapi_PBlock *pb)
{
    Slapi_PBlock *entry_pb = NULL;
    char *dn = NULL;
    int ldrc, tracklogin = 0;
    int rc = 0; /* Optimistic default */
    Slapi_DN *sdn = NULL;
    Slapi_Entry *target_entry = NULL;
    acctPluginCfg *cfg;
    void *plugin_id;

    slapi_log_err(SLAPI_LOG_PLUGIN, POST_PLUGIN_NAME,
                  "=> acct_bind_postop\n");

    plugin_id = get_identity();

    /* Retrieving SLAPI_CONN_DN from the pb gives a copy */
    if (slapi_pblock_get(pb, SLAPI_CONN_DN, &dn) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, POST_PLUGIN_NAME,
                      "acct_bind_postop - Error retrieving bind DN\n");
        rc = -1;
        goto done;
    }

    /* Client is anonymously bound */
    if (dn == NULL) {
        goto done;
    }

    config_rd_lock();
    cfg = get_config();
    tracklogin = cfg->always_record_login;

    /* We're not always tracking logins, so check whether the entry is
       covered by an account policy to decide whether we should track */
    if (tracklogin == 0) {
        sdn = slapi_sdn_new_normdn_byref(dn);
        ldrc = slapi_search_get_entry(&entry_pb, sdn, NULL, &target_entry, plugin_id);

        if (ldrc != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, POST_PLUGIN_NAME,
                          "acct_bind_postop - Failed to retrieve entry \"%s\": %d\n", dn, ldrc);
            rc = -1;
            goto done;
        } else {
            if (target_entry && has_attr(target_entry, cfg->spec_attr_name, NULL)) {
                tracklogin = 1;
            }
        }
    }
    config_unlock();

    if (tracklogin) {
        rc = acct_record_login(dn);
    }

/* ...Any additional account policy postops go here... */

done:
    if (rc == -1) {
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL);
    }

    slapi_search_get_entry_done(&entry_pb);

    slapi_sdn_free(&sdn);

    slapi_ch_free_string(&dn);

    slapi_log_err(SLAPI_LOG_PLUGIN, POST_PLUGIN_NAME,
                  "<= acct_bind_postop\n");

    return (rc == 0 ? CALLBACK_OK : CALLBACK_ERR);
}

static int
acct_pre_op(Slapi_PBlock *pb, int modop)
{
    Slapi_PBlock *entry_pb = NULL;
    Slapi_DN *sdn = 0;
    Slapi_Entry *e = 0;
    Slapi_Mods *smods = 0;
    LDAPMod **mods;
    char *errstr = NULL;
    int ret = SLAPI_PLUGIN_SUCCESS;

    slapi_log_err(SLAPI_LOG_TRACE, PRE_PLUGIN_NAME, "--> acct_pre_op\n");

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);

    if (acct_policy_dn_is_config(sdn)) {
        /* Validate config changes, but don't apply them.
         * This allows us to reject invalid config changes
         * here at the pre-op stage.  Applying the config
         * needs to be done at the post-op stage. */

        if (LDAP_CHANGETYPE_ADD == modop) {
            slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);

            /* If the entry doesn't exist, just bail and let the server handle it. */
            if (e == NULL) {
                goto bail;
            }
        } else if (LDAP_CHANGETYPE_MODIFY == modop) {
            /* Fetch the entry being modified so we can
             * create the resulting entry for validation. */
            if (sdn) {
                slapi_search_get_entry(&entry_pb, sdn, 0, &e, get_identity());
            }

            /* If the entry doesn't exist, just bail and let the server handle it. */
            if (e == NULL) {
                goto bail;
            }

            /* Grab the mods. */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            smods = slapi_mods_new();
            slapi_mods_init_byref(smods, mods);

            /* Apply the  mods to create the resulting entry. */
            if (mods && (slapi_entry_apply_mods(e, mods) != LDAP_SUCCESS)) {
                /* The mods don't apply cleanly, so we just let this op go
                 * to let the main server handle it. */
                goto bailmod;
            }
        } else if (modop == LDAP_CHANGETYPE_DELETE) {
            ret = LDAP_UNWILLING_TO_PERFORM;
            slapi_log_err(SLAPI_LOG_ERR, PRE_PLUGIN_NAME,
                          "acct_pre_op - Can not delete plugin config entry [%d]\n", ret);
        } else {
            errstr = slapi_ch_smprintf("acct_pre_op - Invalid op type %d", modop);
            ret = LDAP_PARAM_ERROR;
            goto bail;
        }
    }

bailmod:
    /* Clean up smods. */
    if (LDAP_CHANGETYPE_MODIFY == modop) {
        slapi_mods_free(&smods);
    }

bail:
    slapi_search_get_entry_done(&entry_pb);

    if (ret) {
        slapi_log_err(SLAPI_LOG_PLUGIN, PRE_PLUGIN_NAME,
                      "acct_pre_op - Operation failure [%d]\n", ret);
        slapi_send_ldap_result(pb, ret, NULL, errstr, 0, NULL);
        slapi_ch_free((void **)&errstr);
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ret);
        ret = SLAPI_PLUGIN_FAILURE;
    }

    slapi_log_err(SLAPI_LOG_TRACE, PRE_PLUGIN_NAME, "<-- acct_pre_op\n");

    return ret;
}

int
acct_add_pre_op(Slapi_PBlock *pb)
{
    return acct_pre_op(pb, LDAP_CHANGETYPE_ADD);
}

int
acct_mod_pre_op(Slapi_PBlock *pb)
{
    return acct_pre_op(pb, LDAP_CHANGETYPE_MODIFY);
}

int
acct_del_pre_op(Slapi_PBlock *pb)
{
    return acct_pre_op(pb, LDAP_CHANGETYPE_DELETE);
}

int
acct_post_op(Slapi_PBlock *pb)
{
    Slapi_DN *sdn = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, POST_PLUGIN_NAME,
                  "--> acct_policy_post_op\n");

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    if (acct_policy_dn_is_config(sdn)) {
        if (acct_policy_load_config_startup(pb, get_identity())) {
            slapi_log_err(SLAPI_LOG_ERR, PLUGIN_NAME,
                          "acct_post_op - Failed to load configuration\n");
            return (CALLBACK_ERR);
        }
    }

    slapi_log_err(SLAPI_LOG_TRACE, POST_PLUGIN_NAME,
                  "<-- acct_policy_mod_post_op\n");

    return SLAPI_PLUGIN_SUCCESS;
}
