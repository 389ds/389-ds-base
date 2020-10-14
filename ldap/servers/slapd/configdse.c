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

/* configdse.c - routines to manage the config DSE */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/param.h>
#include "log.h"
#include "slap.h"
#include "pw.h"

static int check_all_maxdiskspace_and_mlogsize(Slapi_PBlock *pb, LDAPMod **mod, char *returntext);
static int is_delete_a_replace(LDAPMod **mods, int mod_count);
static void get_log_max_size(LDAPMod *mod,
                             char *maxdiskspace_str,
                             char *mlogsize_str,
                             int *maxdiskspace,
                             int *mlogsize);

/* List of attributes which require server restart to take effect */
static const char *requires_restart[] = {
    "cn=config:nsslapd-port",
    "cn=config:nsslapd-secureport",
    "cn=config:" CONFIG_LDAPI_FILENAME_ATTRIBUTE,
    "cn=config:" CONFIG_LDAPI_SWITCH_ATTRIBUTE,
    "cn=config:nsslapd-workingdir",
    "cn=config:nsslapd-plugin",
    "cn=config:nsslapd-sslclientauth",
    "cn=config:nsslapd-changelogdir",
    "cn=config:nsslapd-changelogsuffix",
    "cn=config:nsslapd-changelogmaxentries",
    "cn=config:nsslapd-changelogmaxage",
    "cn=config:nsslapd-db-locks",
    "cn=config:nsslapd-maxdescriptors",
    "cn=config:" CONFIG_RETURN_EXACT_CASE_ATTRIBUTE,
    "cn=config:" CONFIG_SCHEMA_IGNORE_TRAILING_SPACES,
    "cn=config,cn=ldbm:nsslapd-idlistscanlimit",
    "cn=config,cn=ldbm:nsslapd-parentcheck",
    "cn=config,cn=ldbm:nsslapd-dbcachesize",
    "cn=config,cn=ldbm:nsslapd-dbncache",
    "cn=config,cn=ldbm:nsslapd-cachesize",
    "cn=config,cn=ldbm:nsslapd-plugin",
    "cn=encryption,cn=config:nssslsessiontimeout",
    "cn=encryption,cn=config:nssslclientauth",
    "cn=encryption,cn=config:nsssl2",
    "cn=encryption,cn=config:nsssl3"};

static int
isASyntaxOrMrPluginOrPss(Slapi_Entry *e)
{
    char *ptype = (char *)slapi_entry_attr_get_ref(e, ATTR_PLUGIN_TYPE);
    int retval = (ptype && !strcasecmp(ptype, "syntax"));
    if (!retval)
        retval = (ptype && !strcasecmp(ptype, "matchingrule"));
    if (!retval)
        retval = (ptype && !strcasecmp(ptype, "pwdstoragescheme"));
    if (!retval)
        retval = (ptype && !strcasecmp(ptype, "reverpwdstoragescheme"));
    return retval;
}

/* these attr types are ignored for the purposes of configuration search/modify */
static int
ignore_attr_type(const char *attr_type)
{
    if (!attr_type ||
        (strcasecmp(attr_type, "cn") == 0) ||
        (strcasecmp(attr_type, "aci") == 0) ||
        (strcasecmp(attr_type, "objectclass") == 0) ||
        (strcasecmp(attr_type, "numsubordinates") == 0) ||
        (strcasecmp(attr_type, "internalCreatorsname") == 0) ||
        slapi_attr_is_last_mod((char *)attr_type)) {
        return 1;
    }

    return 0;
}


/* These trigger rejections for config modify! */
/*
 * So why does this function exist? Well, when we run modify_config_dse, this
 * is about intercepting the change and reloading values live. It actually has
 * nothing to do with the operation on the back-ldif, dse.ldif.
 * When a change occurs to modify_config_dse, this happens *before* the write
 * to dse.ldif. Sometimes, because of the extensibleObject, we can't really
 * validate the schema. We need to protect certain attributes.
 *
 * Take CN. If we ignore_attr_type cn, we skip over it in modify_config_dse, but
 * we still delete it from cn=config in dse.ldif. The server then explodes!
 * So this is a barrier to *reject* changes that would trash your server, rather
 * than allowing them to propogate into dse.ldif.
 *
 * SUMMARY: If it's an attribute that is structurally important to cn=config,
 * and IS NOT a configuration in libglobs.c, PUT IT HERE.
 */

static int
reject_attr_type(const char *attr_type)
{
    if (!attr_type || (strcasecmp(attr_type, "cn") == 0)) {
        return 1;
    }
    return 0;
}

int
read_config_dse(Slapi_PBlock *pb __attribute__((unused)),
                Slapi_Entry *e,
                Slapi_Entry *entryAfter __attribute__((unused)),
                int *returncode,
                char *returntext __attribute__((unused)),
                void *arg __attribute__((unused)))
{
    struct berval *vals[2];
    struct berval val;
    Slapi_Backend *be;
    slapdFrontendConfig_t *slapdFrontendConfig;
    struct slapdplugin *pPlugin;
    char *cookie;
    size_t i;

    slapdFrontendConfig = getFrontendConfig();

    vals[0] = &val;
    vals[1] = NULL;

    /*
     * We can skip using the config accessor functions here because we're holding
     * the read lock explicitly
     */
    CFG_LOCK_READ(slapdFrontendConfig);

    /* show backend config */
    attrlist_delete(&e->e_attrs, "nsslapd-backendconfig");
    for (i = 0;
         slapdFrontendConfig->backendconfig &&
         slapdFrontendConfig->backendconfig[i];
         i++) {
        val.bv_val = slapdFrontendConfig->backendconfig[i];
        val.bv_len = strlen(val.bv_val);
        attrlist_merge(&e->e_attrs, "nsslapd-backendconfig", vals);
    }

    CFG_UNLOCK_READ(slapdFrontendConfig);

    /* show other config entries */
    attrlist_delete(&e->e_attrs, "nsslapd-backendconfig");
    cookie = NULL;
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (!be->be_private) {
            Slapi_DN dn;
            slapi_sdn_init(&dn);
            be_getconfigdn(be, &dn);
            val.bv_val = (char *)slapi_sdn_get_ndn(&dn);
            val.bv_len = strlen(val.bv_val);
            attrlist_merge(&e->e_attrs, "nsslapd-backendconfig", vals);
            slapi_sdn_done(&dn);
        }

        be = slapi_get_next_backend(cookie);
    }

    slapi_ch_free_string(&cookie);

    /* show be_type */
    attrlist_delete(&e->e_attrs, "nsslapd-betype");
    cookie = NULL;
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (!be->be_private) {
            val.bv_val = be->be_type;
            val.bv_len = strlen(be->be_type);
            attrlist_replace(&e->e_attrs, "nsslapd-betype", vals);
        }

        be = slapi_get_next_backend(cookie);
    }

    slapi_ch_free_string(&cookie);

    /* show private suffixes */
    attrlist_delete(&e->e_attrs, "nsslapd-privatenamespaces");
    cookie = NULL;
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (be->be_private) {
            const Slapi_DN *base = NULL;
            base = slapi_be_getsuffix(be);
            if (base != NULL) {
                val.bv_val = (void *)slapi_sdn_get_dn(base); /* jcm: had to cast away const */
                val.bv_len = strlen(val.bv_val);
                attrlist_merge(&e->e_attrs, "nsslapd-privatenamespaces", vals);
            }
        }
        be = slapi_get_next_backend(cookie);
    }

    slapi_ch_free_string(&cookie);

    /* show syntax plugins */
    attrlist_delete(&e->e_attrs, CONFIG_PLUGIN_ATTRIBUTE);
    for (pPlugin = slapi_get_global_syntax_plugins(); pPlugin != NULL;
         pPlugin = pPlugin->plg_next) {
        val.bv_val = pPlugin->plg_dn;
        val.bv_len = strlen(val.bv_val);
        attrlist_merge(&e->e_attrs, CONFIG_PLUGIN_ATTRIBUTE, vals);
    }

    /* show matching rule plugins */
    for (pPlugin = slapi_get_global_mr_plugins(); pPlugin != NULL;
         pPlugin = pPlugin->plg_next) {
        val.bv_val = pPlugin->plg_dn;
        val.bv_len = strlen(val.bv_val);
        attrlist_merge(&e->e_attrs, CONFIG_PLUGIN_ATTRIBUTE, vals);
    }

    /* show requiresrestart */
    attrlist_delete(&e->e_attrs, "nsslapd-requiresrestart");
    for (i = 0; i < sizeof(requires_restart) / sizeof(requires_restart[0]); i++) {
        val.bv_val = (char *)requires_restart[i];
        val.bv_len = strlen(val.bv_val);
        attrlist_merge(&e->e_attrs, "nsslapd-requiresrestart", vals);
    }

    /* show the rest of the configuration parameters */
    *returncode = config_set_entry(e);

    return SLAPI_DSE_CALLBACK_OK;
}

int
load_config_dse(Slapi_PBlock *pb __attribute__((unused)),
                Slapi_Entry *e,
                Slapi_Entry *ignored __attribute__((unused)),
                int *returncode,
                char *returntext,
                void *arg __attribute__((unused)))
{
    int retval = LDAP_SUCCESS;
    Slapi_Attr *attr = 0;

    for (slapi_entry_first_attr(e, &attr); (retval == LDAP_SUCCESS) && attr;
         slapi_entry_next_attr(e, attr, &attr)) {
        char *attr_name = 0;
        struct berval **values = 0;
        int nvals = 0;

        slapi_attr_get_type(attr, &attr_name);
        if (ignore_attr_type(attr_name))
            continue;

        slapi_attr_get_numvalues(attr, &nvals);

        /* convert the values into an array of bervals */
        if (nvals) {
            Slapi_Value *v = 0;
            int index = 0;

            values = (struct berval **)slapi_ch_malloc((nvals + 1) *
                                                       sizeof(struct berval *));
            values[nvals] = 0;
            for (index = slapi_attr_first_value(attr, &v);
                 v && (index != -1);
                 index = slapi_attr_next_value(attr, index, &v)) {
                values[index] = (struct berval *)slapi_value_get_berval(v);
            }
        }

        if (attr_name) {
            retval = config_set(attr_name, values, returntext, 1 /* force apply */);
            if ((strcasecmp(attr_name, CONFIG_MAXDESCRIPTORS_ATTRIBUTE) == 0) ||
                (strcasecmp(attr_name, CONFIG_RESERVEDESCRIPTORS_ATTRIBUTE) == 0) ||
                (strcasecmp(attr_name, CONFIG_CONNTABLESIZE_ATTRIBUTE) == 0)) {
                /* We should not treat an LDAP_UNWILLING_TO_PERFORM as fatal for
                 * the these config attributes.  This error is returned when
                 * the value we are trying to set is higher than the current
                 * process limit.  The set function will auto-adjust the runtime
                 * value to the current process limit when this happens.  We want
                 * to allow the server to still start in this case. */
                if (retval == LDAP_UNWILLING_TO_PERFORM) {
                    slapi_log_err(SLAPI_LOG_WARNING, "load_config_dse", "Config Warning: - %s\n", returntext);
                    retval = LDAP_SUCCESS;
                }
            } else {
                if (((retval != LDAP_SUCCESS) &&
                     slapi_attr_flag_is_set(attr, SLAPI_ATTR_FLAG_OPATTR)) ||
                    (LDAP_NO_SUCH_ATTRIBUTE == retval)) {
                    /* ignore attempts to modify operational attrs and */
                    /* ignore attempts to modify unknown attributes for load. */
                    retval = LDAP_SUCCESS;
                }
            }
        }

        if (values) {
            /* slapi_value_get_berval returns the actual memory owned by the
               slapi attr, so we cannot free it */
            slapi_ch_free((void **)&values);
        }
    }

    *returncode = retval;
    return (retval == LDAP_SUCCESS) ? SLAPI_DSE_CALLBACK_OK
                                    : SLAPI_DSE_CALLBACK_ERROR;
}

int
load_plugin_entry(Slapi_PBlock *pb __attribute__((unused)),
                  Slapi_Entry *e,
                  Slapi_Entry *ignored __attribute__((unused)),
                  int *returncode,
                  char *returntext,
                  void *arg __attribute__((unused)))
{
    int retval = LDAP_SUCCESS;

    if (isASyntaxOrMrPluginOrPss(e)) {
        /*
         * syntax/matching/passwd storage scheme rule plugins are loaded
         * at bootstrap time, so no need to load them here.  BUT -- the
         * descriptive information that is registered by the plugin is
         * thrown away during bootstrap time, so we set it here.
         */
        (void)plugin_add_descriptive_attributes(e, NULL);

    } else {
        /*
         * Process plugins that were not loaded during bootstrap.
         */
        retval = plugin_setup(e, 0, 0, 1, returntext);

        /*
         * well this damn well sucks, but this function is used as a callback
         * and to ensure we do not continue if a plugin fails to load or init
         * properly we must exit here.
         */
        if (retval) {
            slapi_log_err(SLAPI_LOG_ERR, "load_plugin_entry",
                          "Unable to load plugin \"%s\"\n",
                          slapi_entry_get_dn_const(e));
            exit(1);
        }
    }

    *returncode = retval;
    return SLAPI_DSE_CALLBACK_OK;
}

int
modify_config_dse(Slapi_PBlock *pb,
                  Slapi_Entry *entryBefore __attribute__((unused)),
                  Slapi_Entry *e,
                  int *returncode,
                  char *returntext,
                  void *arg __attribute__((unused)))
{
    char *config_attr;
    LDAPMod **mods;
    int rc = LDAP_SUCCESS;
    int apply_mods = 0;
    char *pwd = 0;
    int checked_all_maxdiskspace_and_mlogsize = 0;

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

    returntext[0] = '\0';

    /*
     * First pass: set apply mods to 0 so only input validation will be done;
     * 2nd pass: set apply mods to 1 to apply changes to internal storage
     */
    for (apply_mods = 0; apply_mods <= 1; apply_mods++) {
        int i = 0;
        for (i = 0; mods && (mods[i] && (LDAP_SUCCESS == rc)); i++) {
            /* send all aci modifications to the backend */
            config_attr = (char *)mods[i]->mod_type;

            /*
             * See comments with function reject_attr_type for the important
             * difference between "reject" and "ignore".
             *
             * Here it is important that REJECT is FIRST!!!!
             */

            if (reject_attr_type(config_attr)) {
                slapi_log_err(SLAPI_LOG_WARNING, "modify_config_dse",
                              "Modification of attribute \"%s\" is not allowed, REJECTING!\n",
                              config_attr);
                rc = LDAP_UNWILLING_TO_PERFORM;
            } else if (ignore_attr_type(config_attr)) {
                slapi_log_err(SLAPI_LOG_CONFIG, "modify_config_dse",
                              "Modification of attribute \"%s\" is not allowed, ignoring!\n",
                              config_attr);
            } else if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
                if (apply_mods) { /* log warning once */
                    slapi_log_err(SLAPI_LOG_CONFIG, "modify_config_dse",
                                  "Adding configuration attribute \"%s\"\n",
                                  config_attr);
                }
                rc = config_set(config_attr, mods[i]->mod_bvalues,
                                returntext, apply_mods);
                if (LDAP_NO_SUCH_ATTRIBUTE == rc) {
                    /* config_set returns LDAP_NO_SUCH_ATTRIBUTE if the
                     * attr is not defined for cn=config.
                     * map it to LDAP_UNWILLING_TO_PERFORM */
                    rc = LDAP_UNWILLING_TO_PERFORM;
                }
            } else if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
                /*
                 *  Check if this delete is followed by an add of the same attribute, as some
                 *  clients do a replace by deleting and adding the attribute.
                 *  In the future when we fix https://fedorahosted.org/389/ticket/49019
                 *  This will be an important distinction as it will allow a reset to null
                 *  rather than to a value.
                 */
                if (is_delete_a_replace(mods, i)) {
                    /* Just do it and allow the null (well, in the future allow null. For
                     * now this is still going to fail :(
                     */
                    rc = config_set(config_attr, mods[i]->mod_bvalues, returntext, apply_mods);
                    if (rc != LDAP_SUCCESS && apply_mods) {
                        rc = LDAP_UNWILLING_TO_PERFORM;
                        slapi_log_err(SLAPI_LOG_WARNING, "modify_config_dse", "Deleting configuration attribute \"%s\"\n", config_attr);
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Deleting attributes is not allowed");
                    }
                } else {
                    /* Nope, this is really a delete. Let the value be reset! */
                    rc = config_set(config_attr, mods[i]->mod_bvalues, returntext, apply_mods);
                    if (rc != LDAP_SUCCESS && apply_mods) {
                        rc = LDAP_UNWILLING_TO_PERFORM;
                        slapi_log_err(SLAPI_LOG_WARNING, "modify_config_dse", "Deleting configuration attribute \"%s\"\n", config_attr);
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Deleting attributes is not allowed");
                    }
                }
            } else if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) {
                if ((checked_all_maxdiskspace_and_mlogsize == 0) &&
                    ((strcasecmp(mods[i]->mod_type, CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE) == 0) ||
                     (strcasecmp(mods[i]->mod_type, CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE) == 0) ||
                     (strcasecmp(mods[i]->mod_type, CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE) == 0) ||
                     (strcasecmp(mods[i]->mod_type, CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE) == 0) ||
                     (strcasecmp(mods[i]->mod_type, CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE) == 0) ||
                     (strcasecmp(mods[i]->mod_type, CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE) == 0))) {
                    checked_all_maxdiskspace_and_mlogsize = 1;
                    if ((rc = check_all_maxdiskspace_and_mlogsize(pb, mods, returntext)) != LDAP_SUCCESS) {
                        goto finish_and_return;
                    }
                }

                rc = config_set(config_attr, mods[i]->mod_bvalues, returntext,
                                apply_mods);
                if (LDAP_NO_SUCH_ATTRIBUTE == rc) {
                    /* config_set returns LDAP_NO_SUCH_ATTRIBUTE if the
                     * attr is not defined for cn=config.
                     * map it to LDAP_UNWILLING_TO_PERFORM */
                    rc = LDAP_UNWILLING_TO_PERFORM;
                }
            }
        }
    }

finish_and_return:
    /*
     * The DSE code will be writing the resultant entry value to the
     * dse.ldif file.  We *must*not* write plain passwords into here.
     */
    slapi_entry_attr_delete(e, CONFIG_ROOTPW_ATTRIBUTE);
    /* if the password has been set, it will be hashed */
    if ((pwd = config_get_rootpw()) != NULL) {
        slapi_entry_attr_set_charptr(e, CONFIG_ROOTPW_ATTRIBUTE, pwd);
        slapi_ch_free_string(&pwd);
    }

    *returncode = rc;
    if (LDAP_SUCCESS == rc) {
        return (SLAPI_DSE_CALLBACK_OK); /* success -- apply the mods. */
    } else {
        return (SLAPI_DSE_CALLBACK_ERROR); /* failure -- reject the mods. */
    }
}

int
postop_modify_config_dse(Slapi_PBlock *pb,
                         Slapi_Entry *entryBefore __attribute__((unused)),
                         Slapi_Entry *e __attribute__((unused)),
                         int *returncode,
                         char *returntext,
                         void *arg __attribute__((unused)))
{
    static int num_requires_restart = sizeof(requires_restart) / sizeof(char *);
    LDAPMod **mods;
    int i, j;
    char *p;

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    returntext[0] = '\0';

    for (i = 0; mods && mods[i]; i++) {
        if (mods[i]->mod_op & LDAP_MOD_REPLACE) {
            /* Check if the server needs to be restarted */
            for (j = 0; j < num_requires_restart; j++) {
                p = strchr(requires_restart[j], ':');
                if (p == NULL)
                    continue;
                while (*(++p) == ' ' || *p == '\t')
                    ;
                if (strcasecmp(p, mods[i]->mod_type) == 0) {
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                "The change of %s will not take effect "
                                "until the server is restarted",
                                mods[i]->mod_type);
                    slapi_log_err(SLAPI_LOG_INFO, "postop_modify_config_dse", "%s\n", returntext);
                    break;
                }
            }
            if (j < num_requires_restart) {
                /* That's enough, don't check remaining mods any more */
                break;
            }
        }
    }

    *returncode = LDAP_SUCCESS;
    return *returncode;
}

static int
check_all_maxdiskspace_and_mlogsize(Slapi_PBlock *pb __attribute__((unused)),
                                    LDAPMod **mods,
                                    char *returntext)
{
    int i = 0;
    int rc = LDAP_SUCCESS;
    int errormaxdiskspace = -1;
    int errormlogsize = -1;
    int accessmaxdiskspace = -1;
    int accessmlogsize = -1;
    int auditmaxdiskspace = -1;
    int auditmlogsize = -1;

    for (i = 0; mods[i]; i++) {
        get_log_max_size(mods[i],
                         CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE,
                         CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE,
                         &errormaxdiskspace,
                         &errormlogsize);

        get_log_max_size(mods[i],
                         CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE,
                         CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE,
                         &accessmaxdiskspace,
                         &accessmlogsize);

        get_log_max_size(mods[i],
                         CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE,
                         CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE,
                         &auditmaxdiskspace,
                         &auditmlogsize);
    }

    if ((rc = check_log_max_size(
             CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE,
             CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE,
             errormaxdiskspace,
             errormlogsize,
             returntext,
             SLAPD_ERROR_LOG)) != LDAP_SUCCESS) {
        return rc;
    }
    if ((rc = check_log_max_size(
             CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE,
             CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE,
             accessmaxdiskspace,
             accessmlogsize,
             returntext,
             SLAPD_ACCESS_LOG)) != LDAP_SUCCESS) {
        return rc;
    }
    if ((rc = check_log_max_size(
             CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE,
             CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE,
             auditmaxdiskspace,
             auditmlogsize,
             returntext,
             SLAPD_AUDIT_LOG)) != LDAP_SUCCESS) {
        return rc;
    }
    return rc;
}

static void
get_log_max_size(LDAPMod *mod,
                 char *maxdiskspace_str,
                 char *mlogsize_str,
                 int *maxdiskspace,
                 int *mlogsize)
{
    if (mod->mod_bvalues != NULL &&
        (strcasecmp(mod->mod_type, maxdiskspace_str) == 0)) {
        *maxdiskspace = atoi((char *)mod->mod_bvalues[0]->bv_val);
    }

    if (mod->mod_bvalues != NULL &&
        (strcasecmp(mod->mod_type, mlogsize_str) == 0)) {
        *mlogsize = atoi((char *)mod->mod_bvalues[0]->bv_val);
    }
}

/*
 *  Loops through all the mods, if we add the attribute back, it's a replace, but we need
 *  to keep looking through the mods in case it gets deleted again.
 */
static int
is_delete_a_replace(LDAPMod **mods, int mod_count)
{
    char *del_attr = mods[mod_count]->mod_type;
    int rc = 0;
    int i;

    for (i = mod_count + 1; mods[i] != NULL; i++) {
        if (strcasecmp(mods[i]->mod_type, del_attr) == 0 && SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
            /* ok, we are adding this attribute back */
            rc = 1;
        } else if (strcasecmp(mods[i]->mod_type, del_attr) == 0 && SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
            /* whoops we deleted it again */
            rc = 0;
        }
    }

    return rc;
}
