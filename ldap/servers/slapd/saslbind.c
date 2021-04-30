/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #193297
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <slap.h>
#include <fe.h>
#include <pw_verify.h>
#include <sasl/sasl.h>
#include <sasl/saslplug.h>
#include <unistd.h>

static char *serverfqdn;

/*
 * utility functions needed by the sasl library
 */
void *
nssasl_mutex_alloc(void)
{
    return PR_NewLock();
}

int
nssasl_mutex_lock(void *mutex)
{
    if (mutex) {
        PR_Lock(mutex);
    }
    return SASL_OK;
}

int
nssasl_mutex_unlock(void *mutex)
{
    if (mutex) {
        if (PR_Unlock(mutex) == PR_SUCCESS)
            return SASL_OK;
        return SASL_FAIL;
    } else {
        return SASL_OK;
    }
}

void
nssasl_mutex_free(void *mutex)
{
    if (mutex) {
        PR_DestroyLock(mutex);
    }
}

void
nssasl_free(void *ptr)
{
    slapi_ch_free(&ptr);
}

static Slapi_ComponentId *sasl_component_id = NULL;

static void
generate_component_id(void)
{
    if (NULL == sasl_component_id) {
        sasl_component_id = generate_componentid(NULL /* Not a plugin */,
                                                 COMPONENT_SASL);
    }
}

static Slapi_ComponentId *
sasl_get_component_id(void)
{
    return sasl_component_id;
}

/*
 * sasl library callbacks
 */

/*
 * We've added this auxprop stuff as a workaround for RHDS bug 166229
 * and FDS bug 166081.  The problem is that sasldb is configured and
 * enabled by default, but we don't want or need to use it.  What
 * happens after canon_user is that sasl looks up any auxiliary
 * properties of that user.  If you don't tell sasl which auxprop
 * plug-in to use, it tries all of them, including sasldb.  In order
 * to avoid this, we create a "dummy" auxprop plug-in with the name
 * "iDS" and tell sasl to use this plug-in for auxprop lookups.
 * The reason we don't need auxprops is because when we grab the user's
 * entry from the internal database, at the same time we get any other
 * properties we need - it's more efficient that way.
 */
#if SASL_AUXPROP_PLUG_VERSION > 4
static int
ids_auxprop_lookup
#else
static void
ids_auxprop_lookup
#endif
    (void *glob_context __attribute__((unused)),
     sasl_server_params_t *sparams __attribute__((unused)),
     unsigned flags __attribute__((unused)),
     const char *user __attribute__((unused)),
     unsigned ulen __attribute__((unused)))
{
/* do nothing - we don't need auxprops - we just do this to avoid
       sasldb_auxprop_lookup */
#if SASL_AUXPROP_PLUG_VERSION > 4
    return 0;
#endif
}

static sasl_auxprop_plug_t ids_auxprop_plugin = {
    0,                  /* Features */
    0,                  /* spare */
    NULL,               /* glob_context */
    NULL,               /* auxprop_free */
    ids_auxprop_lookup, /* auxprop_lookup */
    "iDS",              /* name */
    NULL                /* auxprop_store */
};

int
ids_auxprop_plug_init(const sasl_utils_t *utils __attribute__((unused)),
                      int max_version,
                      int *out_version,
                      sasl_auxprop_plug_t **plug,
                      const char *plugname __attribute__((unused)))
{
    if (!out_version || !plug)
        return SASL_BADPARAM;

    if (max_version < SASL_AUXPROP_PLUG_VERSION)
        return SASL_BADVERS;

    *out_version = SASL_AUXPROP_PLUG_VERSION;

    *plug = &ids_auxprop_plugin;

    return SASL_OK;
}

static int
ids_sasl_getopt(
    void *context __attribute__((unused)),
    const char *plugin_name,
    const char *option,
    const char **result,
    unsigned *len)
{
    unsigned tmplen;

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_getopt", "plugin=%s option=%s\n",
                  plugin_name ? plugin_name : "", option);

    if (len == NULL)
        len = &tmplen;

    *result = NULL;
    *len = 0;

    if (strcasecmp(option, "enable") == 0) {
        *result = "USERDB/DIGEST-MD5,GSSAPI/GSSAPI";
    } else if (strcasecmp(option, "has_plain_passwords") == 0) {
        *result = "yes";
    } else if (strcasecmp(option, "LOG_LEVEL") == 0) {
        if (loglevel_is_set(LDAP_DEBUG_TRACE)) {
            *result = "6"; /* SASL_LOG_TRACE */
        }
    } else if (strcasecmp(option, "auxprop_plugin") == 0) {
        *result = "iDS";
    }

    if (*result)
        *len = strlen(*result);

    return SASL_OK;
}

static int
ids_sasl_log(
    void *context __attribute__((unused)),
    int level,
    const char *message)
{
    switch (level) {
    case SASL_LOG_ERR: /* log unusual errors (default) */
        slapi_log_err(SLAPI_LOG_ERR, "ids_sasl_log", "%s\n", message);
        break;

    case SASL_LOG_FAIL:  /* log all authentication failures */
    case SASL_LOG_WARN:  /* log non-fatal warnings */
    case SASL_LOG_NOTE:  /* more verbose than LOG_WARN */
    case SASL_LOG_DEBUG: /* more verbose than LOG_NOTE */
    case SASL_LOG_TRACE: /* traces of internal protocols */
    case SASL_LOG_PASS:  /* traces of internal protocols, including
                                 * passwords */
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_log", "(%d): %s\n", level, message);
        break;

    case SASL_LOG_NONE: /* don't log anything */
    default:
        break;
    }
    return SASL_OK;
}

static void
ids_sasl_user_search(
    char *basedn,
    int scope,
    char *filter,
    LDAPControl **ctrls,
    char **attrs,
    int attrsonly,
    Slapi_Entry **ep,
    int *foundp)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *pb = NULL;
    int i, ret;

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_search",
                  "sasl user search basedn=\"%s\" filter=\"%s\"\n", basedn, filter);

    /* TODO: set size and time limits */
    pb = slapi_pblock_new();
    if (!pb) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_search", "NULL pblock for search_internal_pb\n");
        goto out;
    }

    slapi_search_internal_set_pb(pb, basedn, scope, filter, attrs, attrsonly, ctrls,
                                 NULL, sasl_get_component_id(), 0);

    slapi_search_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_search",
                      "sasl user search failed basedn=\"%s\" filter=\"%s\": %s\n",
                      basedn, filter, ldap_err2string(ret));
        goto out;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL)) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_search",
                      "sasl user search found no entries\n");
        goto out;
    }

    for (i = 0; entries[i]; i++) {
        (*foundp)++;
        if (*ep != NULL) {
            slapi_entry_free(*ep);
        }
        *ep = slapi_entry_dup(entries[i]);
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_search",
                      "sasl user search found dn=%s\n", slapi_entry_get_dn(*ep));
    }

out:

    if (pb) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        pb = NULL;
    }
    return;
}

/*
 * Search for an entry representing the sasl user.
 */
static Slapi_Entry *
ids_sasl_user_to_entry(
    sasl_conn_t *conn __attribute__((unused)),
    void *context __attribute__((unused)),
    const char *user,
    const char *user_realm)
{
    LDAPControl **ctrls = NULL;
    sasl_map_data *map = NULL;
    Slapi_Entry *entry = NULL;
    char **attrs = NULL;
    char *base = NULL;
    char *filter = NULL;
    int attrsonly = 0, scope = LDAP_SCOPE_SUBTREE;
    int regexmatch = 0;
    int found = 0;

    /* Check for wildcards in the authid and realm. If we encounter one,
     * just fail the mapping without performing a costly internal search. */
    if (user && strchr(user, '*')) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_to_entry",
                      "sasl user search encountered a wildcard in "
                      "the authid.  Not attempting to map to entry. (authid=%s)\n",
                      user);
        return NULL;
    } else if (user_realm && strchr(user_realm, '*')) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_to_entry",
                      "sasl user search encountered a wildcard in "
                      "the realm.  Not attempting to map to entry. (realm=%s)\n",
                      user_realm);
        return NULL;
    }

    /* New regex-based identity mapping */
    sasl_map_read_lock();
    while (1) {
        regexmatch = sasl_map_domap(&map, (char *)user, (char *)user_realm, &base, &filter);
        if (regexmatch) {
            ids_sasl_user_search(base, scope, filter,
                                 ctrls, attrs, attrsonly,
                                 &entry, &found);
            if (found == 1) {
                slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_to_entry",
                              "sasl user search found this entry: dn:%s, matching filter=%s\n",
                              entry->e_sdn.dn, filter);
            } else if (found == 0) {
                slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_to_entry",
                              "sasl user search found no entries matchingfilter=%s\n", filter);
            } else {
                slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_user_to_entry",
                              "sasl user search found more than one entry matching filter=%s\n", filter);
                if (entry) {
                    slapi_entry_free(entry);
                    entry = NULL;
                }
            }

            /* Free the filter etc */
            slapi_ch_free_string(&base);
            slapi_ch_free_string(&filter);

            /* If we didn't find an entry, look at the other maps */
            if (found) {
                break;
            }
        }
        /* break if the next map is NULL, or we are not checking all the mappings */
        if (map == NULL || !config_get_sasl_mapping_fallback()) {
            break;
        }
    }
    sasl_map_read_unlock();

    return entry;
}

static char *
buf2str(const char *buf, unsigned buflen)
{
    char *ret;

    ret = (char *)slapi_ch_malloc(buflen + 1);
    memcpy(ret, buf, buflen);
    ret[buflen] = '\0';

    return ret;
}

/* Note that in this sasl1 API, when it says 'authid' it really means 'authzid'. */
static int
ids_sasl_canon_user(
    sasl_conn_t *conn,
    void *context,
    const char *userbuf,
    unsigned ulen,
    unsigned flags __attribute__((unused)),
    const char *user_realm,
    char *out_user,
    unsigned out_umax,
    unsigned *out_ulen)
{
    struct propctx *propctx = sasl_auxprop_getctx(conn);
    Slapi_Entry *entry = NULL;
    Slapi_DN *sdn = NULL;
    char *pw = NULL;
    char *user = NULL;
    char *mech = NULL;
    const char *dn;
    int isroot = 0;
    char *clear = NULL;
    int returnvalue = SASL_FAIL;

    user = buf2str(userbuf, ulen);
    if (user == NULL) {
        goto fail;
    }
    slapi_log_err(SLAPI_LOG_CONNS,
                  "ids_sasl_canon_user", "(user=%s, realm=%s)\n",
                  user, user_realm ? user_realm : "");

    sasl_getprop(conn, SASL_MECHNAME, (const void **)&mech);
    if (mech == NULL) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_canon_user",
                      "Unable to read SASL mechanism while canonifying user.\n");
        goto fail;
    }

    if (strncasecmp(user, "dn:", 3) == 0) {
        sdn = slapi_sdn_new();
        slapi_sdn_set_dn_byval(sdn, user + 3);
        isroot = slapi_dn_isroot(slapi_sdn_get_ndn(sdn));
    }

    if (isroot) {
        /* special case directory manager */
        dn = slapi_sdn_get_ndn(sdn);
        pw = config_get_rootpw();
        *out_ulen = PR_snprintf(out_user, out_umax, "dn: %s", dn);
    } else if (strcasecmp(mech, "ANONYMOUS") == 0) {
        /* SASL doesn't allow us to set the username to an empty string,
         * so we just set it to anonymous. */
        dn = "anonymous";
        PL_strncpyz(out_user, dn, out_umax);
        /* the length of out_user needs to be set for Cyrus SASL */
        *out_ulen = strlen(out_user);
    } else {
        /* map the sasl username into an entry */
        entry = ids_sasl_user_to_entry(conn, context, user, user_realm);
        if (entry == NULL) {
            /* Specific return value is supposed to be set instead of
               an generic error (SASL_FAIL) for Cyrus SASL */
            returnvalue = SASL_NOAUTHZ;
            goto fail;
        }
        dn = slapi_entry_get_ndn(entry);
        pw = slapi_entry_attr_get_charptr(entry, "userpassword");
        *out_ulen = PR_snprintf(out_user, out_umax, "dn: %s", dn);
    }

    /* Need to set dn property to an empty string for the ANONYMOUS mechanism.  This
     * property determines what the bind identity will be if authentication succeeds. */
    if (strcasecmp(mech, "ANONYMOUS") == 0) {
        if (prop_set(propctx, "dn", "", -1) != 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_canon_user", "prop_set(dn) failed\n");
            goto fail;
        }
    } else if (prop_set(propctx, "dn", dn, -1) != 0) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_canon_user", "prop_set(dn) failed\n");
        goto fail;
    }

    /* We need to check if the first character of pw is an opening
     * brace since strstr will simply return it's first argument if
     * it is an empty string. */
    if (pw && (*pw == '{')) {
        if (strchr(pw, '}')) {
            /* This password is stored in a non-cleartext format.
             * Any SASL mechanism that actually needs the
             * password is going to fail.  We should print a warning
             * to aid in troubleshooting. */
            slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_canon_user",
                          "Warning: Detected a sasl bind attempt by an "
                          "entry whose password is stored in a non-cleartext format.  This "
                          "will not work for mechanisms which require a cleartext password "
                          "such as DIGEST-MD5 and CRAM-MD5.\n");
        } else {
            /* This password doesn't have a storage prefix but
             * just happens to start with the '{' character.  We'll
             * assume that it's just a cleartext password without
             * the proper storage prefix. */
            clear = pw;
        }
    } else {
        /* This password has no storage prefix, or the password is empty */
        clear = pw;
    }

    if (clear) {
/* older versions of sasl do not have SASL_AUX_PASSWORD_PROP, so omit it */
#ifdef SASL_AUX_PASSWORD_PROP
        if (prop_set(propctx, SASL_AUX_PASSWORD_PROP, clear, -1) != 0) {
            /* Failure is benign here because some mechanisms don't support this property */
            /*slapi_log_err(SLAPI_LOG_CONNS, "prop_set(userpassword) failed\n", 0, 0, 0);
            goto fail */;
        }
#endif /* SASL_AUX_PASSWORD_PROP */
        if (prop_set(propctx, SASL_AUX_PASSWORD, clear, -1) != 0) {
            /* Failure is benign here because some mechanisms don't support this property */
            /*slapi_log_err(SLAPI_LOG_CONNS, "prop_set(userpassword) failed\n", 0, 0, 0);
            goto fail */;
        }
    }

    slapi_entry_free(entry);
    slapi_ch_free((void **)&user);
    slapi_ch_free((void **)&pw);
    slapi_sdn_free(&sdn);

    return SASL_OK;

fail:
    slapi_entry_free(entry);
    slapi_ch_free((void **)&user);
    slapi_ch_free((void **)&pw);
    slapi_sdn_free(&sdn);

    return returnvalue;
}

static int
ids_sasl_getpluginpath(sasl_conn_t *conn __attribute__((unused)), const char **path)
{
    /* Try to get path from config, otherwise check for SASL_PATH environment
     * variable.  If neither of these are set, default to /usr/lib64/sasl2 on
     * 64-bit Linux machines, and /usr/lib/sasl2 on all other platforms.
     */
    char *pluginpath = config_get_saslpath();
    if ((!pluginpath) || (*pluginpath == '\0')) {
        slapi_ch_free_string(&pluginpath);
        pluginpath = ldaputil_get_saslpath();
    }
    *path = pluginpath;
    return SASL_OK;
}

static int
ids_sasl_userdb_checkpass(sasl_conn_t *conn,
                          void *context __attribute__((unused)),
                          const char *user,
                          const char *pass,
                          unsigned passlen,
                          struct propctx *propctx __attribute__((unused)))
{
    /*
     * Based on the mech
     */
    char *mech = NULL;
    int isroot = 0;
    int bind_result = SLAPI_BIND_FAIL;
    struct berval cred;

    sasl_getprop(conn, SASL_MECHNAME, (const void **)&mech);
    if (mech == NULL) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_userdb_checkpass", "Unable to read SASL mechanism while verifying userdb password.\n");
        goto out;
    }

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_userdb_checkpass", "Using mech %s", mech);
    if (passlen == 0) {
        goto out;
    }

    if (strncasecmp(user, "dn: ", 4) == 0) {
        isroot = slapi_dn_isroot(user + 4);
    } else {
        /* The sasl request probably didn't come from us ... */
        goto out;
    }

    /* Both types will need the creds. */
    cred.bv_len = passlen;
    cred.bv_val = (char *)pass;

    if (isroot) {
        Slapi_Value sv_cred;
        /* Turn the creds into a Slapi Value */
        slapi_value_init_berval(&sv_cred, &cred);
        bind_result = pw_verify_root_dn(user + 4, &sv_cred);
        value_done(&sv_cred);
    } else {
        /* Convert the dn char str to an SDN */
        ber_tag_t method = LDAP_AUTH_SIMPLE;
        Slapi_Entry *referral = NULL;
        Slapi_DN *sdn = slapi_sdn_new();
        slapi_sdn_set_dn_byval(sdn, user + 4);
        /* Create a pblock */
        Slapi_PBlock *pb = slapi_pblock_new();
        /* We have to make a fake operation for the targetsdn spec. */
        /* This is used within the be_dn function */
        Slapi_Operation *op = operation_new(OP_FLAG_INTERNAL);
        operation_set_type(op, SLAPI_OPERATION_BIND);
        /* For mapping tree to work */
        operation_set_target_spec(op, sdn);
        slapi_pblock_set(pb, SLAPI_OPERATION, op);
        /* Equivalent to SLAPI_BIND_TARGET_SDN
         * Used by ldbm bind to know who to bind to.
         */
        slapi_pblock_set(pb, SLAPI_TARGET_SDN, (void *)sdn);
        slapi_pblock_set(pb, SLAPI_BIND_CREDENTIALS, &cred);
        /* To make the ldbm-bind code work, we pretend to be a simple auth right now. */
        slapi_pblock_set(pb, SLAPI_BIND_METHOD, &method);
        /* Feed it to pw_verify_be_dn */
        bind_result = pw_verify_be_dn(pb, &referral);
        /* Now check the result. */
        if (bind_result == SLAPI_BIND_REFERRAL) {
            /* If we have a referral do we ignore it for sasl? */
            slapi_entry_free(referral);
        }
        /* Free everything */
        slapi_sdn_free(&sdn);
        slapi_pblock_destroy(pb);
    }

out:
    if (bind_result == SLAPI_BIND_SUCCESS) {
        return SASL_OK;
    }
    return SASL_FAIL;
}

static sasl_callback_t ids_sasl_callbacks[] =
    {
        {SASL_CB_GETOPT,
         (IFP)ids_sasl_getopt,
         NULL},
        {SASL_CB_LOG,
         (IFP)ids_sasl_log,
         NULL},
        {SASL_CB_CANON_USER,
         (IFP)ids_sasl_canon_user,
         NULL},
        {SASL_CB_GETPATH,
         (IFP)ids_sasl_getpluginpath,
         NULL},
        {SASL_CB_SERVER_USERDB_CHECKPASS,
         (IFP)ids_sasl_userdb_checkpass,
         NULL},
        {SASL_CB_LIST_END,
         (IFP)NULL,
         NULL}};

static const char *dn_propnames[] = {"dn", 0};

/*
 * initialize the sasl library
 */
int
ids_sasl_init(void)
{
    static int inited = 0;
    int result;

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_init", "=>\n");

    PR_ASSERT(inited == 0);
    if (inited != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ids_sasl_init", "Called more than once.\n");
    }
    inited = 1;

    serverfqdn = get_localhost_DNS();

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_init", "sasl service fqdn is: %s\n",
                  serverfqdn);

    /* get component ID for internal operations */
    generate_component_id();

    /* Set SASL memory allocation callbacks */
    sasl_set_alloc(
        (sasl_malloc_t *)slapi_ch_malloc,
        (sasl_calloc_t *)slapi_ch_calloc,
        (sasl_realloc_t *)slapi_ch_realloc,
        (sasl_free_t *)nssasl_free);

    /* Set SASL mutex callbacks */
    sasl_set_mutex(
        (sasl_mutex_alloc_t *)nssasl_mutex_alloc,
        (sasl_mutex_lock_t *)nssasl_mutex_lock,
        (sasl_mutex_unlock_t *)nssasl_mutex_unlock,
        (sasl_mutex_free_t *)nssasl_mutex_free);

    result = sasl_server_init(ids_sasl_callbacks, "iDS");

    if (result != SASL_OK) {
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_init", "Failed to initialize sasl library\n");
        return result;
    }

    result = sasl_auxprop_add_plugin("iDS", ids_auxprop_plug_init);

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_init", "<= %d\n", result);

    return result;
}

/*
 * create a sasl server connection
 */
void
ids_sasl_server_new(Connection *conn)
{
    int rc;
    sasl_conn_t *sasl_conn = NULL;
    struct propctx *propctx;
    sasl_security_properties_t secprops = {0};

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_server_new",
            "=> (conn=%" PRIu64 " fqdn: %s)\n",
            conn->c_connid, serverfqdn);

    rc = sasl_server_new("ldap",
                         serverfqdn,
                         NULL, /* user_realm */
                         NULL, /* iplocalport */
                         NULL, /* ipremoteport */
                         ids_sasl_callbacks,
                         SASL_SUCCESS_DATA,
                         &sasl_conn);

    if (rc != SASL_OK) {
        slapi_log_err(SLAPI_LOG_ERR, "ids_sasl_server_new", "%s (conn=%" PRIu64 ")\n",
                      sasl_errstring(rc, NULL, NULL), conn->c_connid);
    }

    if (rc == SASL_OK) {
        propctx = sasl_auxprop_getctx(sasl_conn);
        if (propctx != NULL) {
            prop_request(propctx, dn_propnames);
        }
    }

    /* Enable security for this connection */
    secprops.maxbufsize = config_get_sasl_maxbufsize();
    secprops.max_ssf = 0xffffffff;
    secprops.min_ssf = config_get_minssf();
    /* If anonymous access is disabled, set the appropriate flag */
    if (config_get_anon_access_switch() != SLAPD_ANON_ACCESS_ON) {
        secprops.security_flags = SASL_SEC_NOANONYMOUS;
    }

    rc = sasl_setprop(sasl_conn, SASL_SEC_PROPS, &secprops);

    if (rc != SASL_OK) {
        slapi_log_err(SLAPI_LOG_ERR, "ids_sasl_server_new", "sasl_setprop: %s (conn=%" PRIu64 ")\n",
                      sasl_errstring(rc, NULL, NULL), conn->c_connid);
    }

    conn->c_sasl_conn = sasl_conn;
    conn->c_sasl_ssf = 0;

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_server_new",
            "<= (conn=%" PRIu64 ")\n", conn->c_connid);

    return;
}

/*
 * start a sasl server connection
 */
int
ids_sasl_server_start(Connection *conn, const char *mech,
                      struct berval *cred,
                      const char **sdata, unsigned int *slen)
{
    int rc;
    sasl_conn_t *sasl_conn;

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_server_start", "=> (conn=%" PRIu64 " mech=%s)\n",
                  conn->c_connid, mech);

    sasl_conn = (sasl_conn_t *)conn->c_sasl_conn;
    rc = sasl_server_start(sasl_conn, mech,
                           cred->bv_val, cred->bv_len,
                           sdata, slen);

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_server_start", "<= (conn=%" PRIu64 " rc=%s)\n",
                  conn->c_connid, sasl_errstring(rc, NULL, NULL));

    return rc;
}

/*
 * perform a sasl server step
 */
int
ids_sasl_server_step(Connection *conn, struct berval *cred,
                     const char **sdata, unsigned int *slen)
{
    int rc;
    sasl_conn_t *sasl_conn;

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_server_step",
                  "=> (conn=%" PRIu64 ")\n", conn->c_connid);

    sasl_conn = (sasl_conn_t *)conn->c_sasl_conn;
    rc = sasl_server_step(sasl_conn,
                          cred->bv_val, cred->bv_len,
                          sdata, slen);

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_server_step", "<= (conn=%" PRIu64 " rc=%s)\n",
                  conn->c_connid, sasl_errstring(rc, NULL, NULL));

    return rc;
}

/*
 * return sasl mechanisms available on this connection.
 * caller must free returned charray.
 */
char **
ids_sasl_listmech(Slapi_PBlock *pb)
{
    char **ret;
    char **config_ret;
    char **sup_ret;
    char **others;
    const char *str;
    char *dupstr;
    sasl_conn_t *sasl_conn;
    Connection *pb_conn = NULL;

    PR_ASSERT(pb);

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_listmech",
                  "=> (conn=%" PRIu64 ")\n", pb_conn ? pb_conn->c_connid : 0);

    /* hard-wired mechanisms and slapi plugin registered mechanisms */
    sup_ret = slapi_get_supported_saslmechanisms_copy();

    /* If we have a connection, get the provided list from SASL */
    if (pb_conn != NULL) {
        sasl_conn = (sasl_conn_t *)pb_conn->c_sasl_conn;
        if (sasl_conn != NULL) {
            /* sasl library mechanisms are connection dependent */
            pthread_mutex_lock(&(pb_conn->c_mutex));
            if (sasl_listmech(sasl_conn,
                              NULL, /* username */
                              "", ",", "",
                              &str, NULL, NULL) == SASL_OK) {
                slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_listmech",
                              "sasl library mechs: %s (conn=%" PRIu64 ")\n",
                              str, pb_conn->c_connid);
                /* merge into result set */
                dupstr = slapi_ch_strdup(str);
                others = slapi_str2charray_ext(dupstr, ",", 0 /* don't list duplicate mechanisms */);

                charray_merge(&sup_ret, others, 1);
                charray_free(others);
                slapi_ch_free((void **)&dupstr);
            }
            pthread_mutex_unlock(&(pb_conn->c_mutex));
        }
    }

    /* Get the servers "allowed" list */
    config_ret = config_get_allowed_sasl_mechs_array();

    /* Remove any content that isn't in the allowed list */
    if (config_ret != NULL) {
        /* Get the set of supported mechs in the intersection of the two */
        ret = charray_intersection(sup_ret, config_ret);
        charray_free(sup_ret);
        charray_free(config_ret);
    } else {
        /* The allowed list was empty, just take our supported list. */
        ret = sup_ret;
    }

    /*
     * https://pagure.io/389-ds-base/issue/49231
     * Because of the way that SASL mechs are managed in bind.c and saslbind.c
     * even if EXTERNAL was *not* in the list of allowed mechs, it was allowed
     * in the bind process because it bypasses lots of our checking. As a result
     * we have to always present it.
     */
    charray_assert_present(&ret, "EXTERNAL");

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_listmech",
                  "<= (conn=%" PRIu64 ")\n",
                  pb_conn ? pb_conn->c_connid : 0);

    return ret;
}

/*
 * Determine whether a given sasl mechanism is supported by
 * this sasl connection. Returns true/false.
 * NOTE: caller must lock pb->pb_conn->c_mutex
 */
static int
ids_sasl_mech_supported(Slapi_PBlock *pb, const char *mech)
{
    Connection *pb_conn = NULL;
    char **allowed_mechs = ids_sasl_listmech(pb);

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_log_err(SLAPI_LOG_CONNS,
                  "ids_sasl_mech_supported", "=> (conn=%" PRIu64 " mech: %s)\n",
                  pb_conn ? pb_conn->c_connid : 0, mech);

    /* 0 indicates "now allowed" */
    int allowed_mech_present = 0;
    if (allowed_mechs != NULL) {
        /* Returns 1 if present and allowed. */
        allowed_mech_present = charray_inlist(allowed_mechs, (char *)mech);
        charray_free(allowed_mechs);
    }

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_mech_supported",
                  "<= (conn=%" PRIu64 " mech: %s present: %d)\n",
                  pb_conn ? pb_conn->c_connid : 0, mech, allowed_mech_present);

    return allowed_mech_present;
}

/*
 * do a sasl bind and return a result
 */
void
ids_sasl_check_bind(Slapi_PBlock *pb)
{
    int rc, isroot;
    sasl_conn_t *sasl_conn;
    struct propctx *propctx;
    sasl_ssf_t *ssfp;
    char *activemech = NULL, *mech = NULL;
    char *username, *dn = NULL;
    const char *normdn = NULL;
    Slapi_DN *sdn = NULL;
    const char *sdata, *errstr;
    unsigned slen;
    int continuing = 0;
    int pwresponse_requested = 0;
    LDAPControl **ctrls;
    struct berval bvr, *cred;
    struct propval dnval[2];
    char authtype[256]; /* >26 (strlen(SLAPD_AUTH_SASL)+SASL_MECHNAMEMAX+1) */
    Slapi_Entry *bind_target_entry = NULL, *referral = NULL;
    Slapi_Backend *be = NULL;
    Connection *pb_conn = NULL;

    PR_ASSERT(pb);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    PR_ASSERT(pb_conn);

    if (pb_conn == NULL){
        slapi_log_err(SLAPI_LOG_ERR, "ids_sasl_check_bind", "pb_conn is NULL\n");
        return;
    }

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                  "=> (conn=%" PRIu64 ")\n", pb_conn->c_connid);

    pthread_mutex_lock(&(pb_conn->c_mutex)); /* BIG LOCK */
    continuing = pb_conn->c_flags & CONN_FLAG_SASL_CONTINUE;
    pb_conn->c_flags &= ~CONN_FLAG_SASL_CONTINUE; /* reset flag */

    sasl_conn = (sasl_conn_t *)pb_conn->c_sasl_conn;
    if (sasl_conn == NULL) {
        pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */
        send_ldap_result(pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                         "sasl library unavailable", 0, NULL);
        return;
    }

    slapi_pblock_get(pb, SLAPI_BIND_SASLMECHANISM, &mech);
    if (NULL == mech) {
        rc = SASL_NOMECH;
        goto sasl_check_result;
    }
    slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred);
    if (NULL == cred) {
        rc = SASL_BADPARAM;
        goto sasl_check_result;
    }
    slapi_pblock_get(pb, SLAPI_PWPOLICY, &pwresponse_requested);

    /* Work around a bug in the sasl library. We've told the
     * library that CRAM-MD5 is disabled, but it gives us a
     * different error code to SASL_NOMECH.  Must be called
     * while holding the pb_conn lock
     */
    if (ids_sasl_mech_supported(pb, mech) == 0) {
        rc = SASL_NOMECH;
        goto sasl_check_result;
    }

    /* can't do any harm */
    if (cred->bv_len == 0)
        cred->bv_val = NULL;

    if (continuing) {
        /*
         * RFC 2251: a client may abort a sasl bind negotiation by
         * sending a bindrequest with a different value in the
         * mechanism field.
         */
        sasl_getprop(sasl_conn, SASL_MECHNAME, (const void **)&activemech);
        if (activemech == NULL) {
            slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                          "Could not get active sasl mechanism (conn=%" PRIu64 ")\n",
                          pb_conn->c_connid);
            goto sasl_start;
        }
        if (strcasecmp(activemech, mech) != 0) {
            slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                          "sasl mechanisms differ: active=%s current=%s (conn=%" PRIu64 ")\n",
                          activemech, mech, pb_conn->c_connid);
            goto sasl_start;
        }

        rc = ids_sasl_server_step(pb_conn, cred, &sdata, &slen);
        goto sasl_check_result;
    }

sasl_start:

    /* Check if we are already authenticated via sasl.  If so,
     * dispose of the current sasl_conn and create a new one
     * using the new mechanism.  We also need to do this if the
     * mechanism changed in the middle of the SASL authentication
     * process. */
    if ((pb_conn->c_flags & CONN_FLAG_SASL_COMPLETE) || continuing) {
        Slapi_Operation *operation;
        slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                      "cleaning up sasl IO conn=%" PRIu64 " op=%d complete=%d continuing=%d\n",
                      pb_conn->c_connid, operation->o_opid,
                      (pb_conn->c_flags & CONN_FLAG_SASL_COMPLETE), continuing);
        /* reset flag */
        pb_conn->c_flags &= ~CONN_FLAG_SASL_COMPLETE;

        /* remove any SASL I/O from the connection */
        connection_set_io_layer_cb(pb_conn, NULL, sasl_io_cleanup, NULL);

        /* dispose of sasl_conn and create a new sasl_conn */
        sasl_dispose(&sasl_conn);
        ids_sasl_server_new(pb_conn);
        sasl_conn = (sasl_conn_t *)pb_conn->c_sasl_conn;

        if (sasl_conn == NULL) {
            send_ldap_result(pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                             "sasl library unavailable", 0, NULL);
            pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */
            return;
        }
    }

    rc = ids_sasl_server_start(pb_conn, mech, cred, &sdata, &slen);

sasl_check_result:

    switch (rc) {
    case SASL_OK: /* complete */
        /* retrieve the authenticated username */
        if (sasl_getprop(sasl_conn, SASL_USERNAME,
                         (const void **)&username) != SASL_OK) {
            pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */
            send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                             "could not obtain sasl username", 0, NULL);
            break;
        }

        slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                      "sasl authenticated mech=%s user=%s conn=%" PRIu64 ")\n",
                      mech, username, pb_conn->c_connid);

        /*
         * Retrieve the DN corresponding to the authenticated user.
         * This should have been set by the user canon callback
         * in an auxiliary property called "dn".
         */
        propctx = sasl_auxprop_getctx(sasl_conn);
        if (prop_getnames(propctx, dn_propnames, dnval) == 1) {
            if (dnval[0].values && dnval[0].values[0]) {
                dn = slapi_ch_smprintf("%s", dnval[0].values[0]);
            }
        }
        if (dn == NULL) {
            pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */
            send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                             "could not get auth dn from sasl", 0, NULL);
            break;
        }

        /* clean up already set TARGET */
        slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &sdn);
        slapi_sdn_free(&sdn);

        sdn = slapi_sdn_new_dn_passin(dn);
        normdn = slapi_sdn_get_dn(sdn);

        slapi_pblock_set(pb, SLAPI_BIND_TARGET_SDN, sdn);

        if ((sasl_getprop(sasl_conn, SASL_SSF,
                          (const void **)&ssfp) == SASL_OK) &&
            (*ssfp > 0)) {
            slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                          "sasl ssf=%u conn=%" PRIu64 "\n",
                          (unsigned)*ssfp, pb_conn->c_connid);
        } else {
            *ssfp = 0;
        }

        /* Set a flag to signify that sasl bind is complete */
        pb_conn->c_flags |= CONN_FLAG_SASL_COMPLETE;
        /* note - we set this here in case there are pre-bind
           plugins that want to know what the negotiated
           ssf is - but this happens before we actually set
           up the socket for SASL encryption - so one
           consequence is that we attempt to do sasl
           encryption on the connection after the pre-bind
           plugin has been called, and sasl encryption fails
           and the operation returns an error */
        pb_conn->c_sasl_ssf = (unsigned)*ssfp;

        /* set the connection bind credentials */
        PR_snprintf(authtype, sizeof(authtype), "%s%s", SLAPD_AUTH_SASL, mech);
        /* normdn is consumed by bind_credentials_set_nolock */
        bind_credentials_set_nolock(pb_conn, authtype,
                                    slapi_ch_strdup(normdn),
                                    NULL, NULL, NULL, bind_target_entry);

        pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */

        if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_BIND_FN) != 0) {
            break;
        }

        isroot = slapi_dn_isroot(normdn);

        if (!isroot) {
            /* check if the account is locked */
            bind_target_entry = get_entry(pb, normdn);
            if (bind_target_entry == NULL) {
                goto out;
            }
            if (slapi_check_account_lock(pb, bind_target_entry, pwresponse_requested, 1, 1) == 1) {
                goto out;
            }
        }

        /* set the auth response control if requested */
        slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrls);
        if (slapi_control_present(ctrls, LDAP_CONTROL_AUTH_REQUEST,
                                  NULL, NULL)) {
            slapi_add_auth_response_control(pb, normdn);
        }

        if (slapi_mapping_tree_select(pb, &be, &referral, NULL, 0) != LDAP_SUCCESS) {
            send_nobackend_ldap_result(pb);
            be = NULL;
            slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                          "<= (conn=%" PRIu64 ")\n", pb_conn->c_connid);
            return;
        }

        if (referral) {
            send_referrals_from_entry(pb, referral);
            goto out;
        }

        slapi_pblock_set(pb, SLAPI_BACKEND, be);

        slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
        set_db_default_result_handlers(pb);

        /* check password expiry */
        if (!isroot && need_new_pw(pb, bind_target_entry, pwresponse_requested) == -1) {
            goto out;
        }

        /* attach the sasl data */
        if (slen != 0) {
            bvr.bv_val = (char *)sdata;
            bvr.bv_len = slen;
            slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, &bvr);
        }

        /* see if we negotiated a security layer */
        if (*ssfp > 0) {
            /* Enable SASL I/O on the connection */
            pthread_mutex_lock(&(pb_conn->c_mutex));
            connection_set_io_layer_cb(pb_conn, sasl_io_enable, NULL, NULL);

            /* send successful result before sasl_io_enable can be pushed by another incoming op */
            send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);

            pthread_mutex_unlock(&(pb_conn->c_mutex));
        } else {
            /* send successful result */
            send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
        }

        /* remove the sasl data from the pblock */
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, NULL);

        break;

    case SASL_CONTINUE: /* another step needed */
        pb_conn->c_flags |= CONN_FLAG_SASL_CONTINUE;
        pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */

        if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_BIND_FN) != 0) {
            break;
        }

        /* attach the sasl data */
        bvr.bv_val = (char *)sdata;
        bvr.bv_len = slen;
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, &bvr);

        /* send continuation result */
        send_ldap_result(pb, LDAP_SASL_BIND_IN_PROGRESS, NULL,
                         NULL, 0, NULL);

        /* remove the sasl data from the pblock */
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, NULL);

        break;

    case SASL_NOMECH:

        pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */
        send_ldap_result(pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                         "sasl mechanism not supported", 0, NULL);
        break;

    default: /* other error */
        errstr = sasl_errdetail(sasl_conn);

        pthread_mutex_unlock(&(pb_conn->c_mutex)); /* BIG LOCK */
        slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, (void *)errstr);
        send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL);
        break;
    }

out:
    if (referral)
        slapi_entry_free(referral);
    if (be)
        slapi_be_Unlock(be);
    if (bind_target_entry)
        slapi_entry_free(bind_target_entry);

    slapi_log_err(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                  "<= (conn=%" PRIu64 ")\n", pb_conn->c_connid);

    return;
}
