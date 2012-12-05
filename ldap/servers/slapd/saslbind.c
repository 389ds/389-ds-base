/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #193297
 *
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <slap.h>
#include <fe.h>
#include <sasl.h>
#include <saslplug.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static char *serverfqdn;

/*
 * utility functions needed by the sasl library
 */
void *nssasl_mutex_alloc(void)
{
    return PR_NewLock();
}

int nssasl_mutex_lock(void *mutex)
{
    PR_Lock(mutex);
    return SASL_OK;
}

int nssasl_mutex_unlock(void *mutex)
{
    if (PR_Unlock(mutex) == PR_SUCCESS) return SASL_OK;
    return SASL_FAIL;
}

void nssasl_mutex_free(void *mutex)
{
    PR_DestroyLock(mutex);
}

void nssasl_free(void *ptr)
{
    slapi_ch_free(&ptr);
}

static Slapi_ComponentId *sasl_component_id = NULL;

static void generate_component_id()
{
    if (NULL == sasl_component_id) {
        sasl_component_id = generate_componentid(NULL /* Not a plugin */,
                                                 COMPONENT_SASL);
    }
}

static Slapi_ComponentId *sasl_get_component_id()
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
static void ids_auxprop_lookup(void *glob_context,
				  sasl_server_params_t *sparams,
				  unsigned flags,
				  const char *user,
				  unsigned ulen) 
{
    /* do nothing - we don't need auxprops - we just do this to avoid
       sasldb_auxprop_lookup */
}

static sasl_auxprop_plug_t ids_auxprop_plugin = {
    0,           		/* Features */
    0,           		/* spare */
    NULL,        		/* glob_context */
    NULL,        		/* auxprop_free */
    ids_auxprop_lookup,	/* auxprop_lookup */
    "iDS",			/* name */
    NULL	/* auxprop_store */
};

int ids_auxprop_plug_init(const sasl_utils_t *utils,
                          int max_version,
                          int *out_version,
                          sasl_auxprop_plug_t **plug,
                          const char *plugname) 
{
    if(!out_version || !plug) return SASL_BADPARAM;

    if(max_version < SASL_AUXPROP_PLUG_VERSION) return SASL_BADVERS;
    
    *out_version = SASL_AUXPROP_PLUG_VERSION;

    *plug = &ids_auxprop_plugin;

    return SASL_OK;
}

static int ids_sasl_getopt(
    void *context, 
    const char *plugin_name,
    const char *option,
    const char **result, 
    unsigned *len
)
{
    unsigned tmplen;

    LDAPDebug(LDAP_DEBUG_TRACE, "ids_sasl_getopt: plugin=%s option=%s\n",
              plugin_name ? plugin_name : "", option, 0);

    if (len == NULL) len = &tmplen;

    *result = NULL;
    *len = 0;

    if (strcasecmp(option, "enable") == 0) {
        *result = "USERDB/DIGEST-MD5,GSSAPI/GSSAPI";
    } else if (strcasecmp(option, "has_plain_passwords") == 0) {
        *result = "yes";
    } else if (strcasecmp(option, "LOG_LEVEL") == 0) {
        if (LDAPDebugLevelIsSet(LDAP_DEBUG_TRACE)) {
            *result = "6"; /* SASL_LOG_TRACE */
        }
    } else if (strcasecmp(option, "auxprop_plugin") == 0) {
        *result = "iDS";
    } else if (strcasecmp(option, "mech_list") == 0){
        *result = config_get_allowed_sasl_mechs();
    }

    if (*result) *len = strlen(*result);

    return SASL_OK;
}

static int ids_sasl_log(
    void       *context,
    int        level,
    const char *message
)
{
    switch (level) {
    case SASL_LOG_ERR:          /* log unusual errors (default) */
        slapi_log_error(SLAPI_LOG_FATAL, "sasl", "%s\n", message);
        break;

    case SASL_LOG_FAIL:         /* log all authentication failures */
    case SASL_LOG_WARN:         /* log non-fatal warnings */
    case SASL_LOG_NOTE:         /* more verbose than LOG_WARN */
    case SASL_LOG_DEBUG:        /* more verbose than LOG_NOTE */
    case SASL_LOG_TRACE:        /* traces of internal protocols */
    case SASL_LOG_PASS:         /* traces of internal protocols, including
                                 * passwords */
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl(%d): %s\n", level, message, 0);
        break;

    case SASL_LOG_NONE:         /* don't log anything */
    default:
        break;
    }
    return SASL_OK;
}

static int ids_sasl_proxy_policy(
    sasl_conn_t *conn,
    void *context,
    const char *requested_user, int rlen,
    const char *auth_identity, int alen,
    const char *def_realm, int urlen,
    struct propctx *propctx
)
{
    int retVal = SASL_OK;
    /* do not permit sasl proxy authorization */
    /* if the auth_identity is null or empty string, allow the sasl request to go thru */    
    if ( (auth_identity != NULL ) && ( strlen(auth_identity) > 0 ) ) {
        Slapi_DN authId , reqUser;
        slapi_sdn_init_dn_byref(&authId,auth_identity);
        slapi_sdn_init_dn_byref(&reqUser,requested_user);
        if (slapi_sdn_compare((const Slapi_DN *)&reqUser,(const Slapi_DN *) &authId) != 0) {
            LDAPDebug(LDAP_DEBUG_TRACE, 
                  "sasl proxy auth not permitted authid=%s user=%s\n",
                  auth_identity, requested_user, 0);
            retVal =  SASL_NOAUTHZ;
        }
        slapi_sdn_done(&authId);
        slapi_sdn_done(&reqUser); 
    }
    return retVal;
}

static void ids_sasl_user_search(
    char *basedn,
    int scope,
    char *filter,
    LDAPControl **ctrls,
    char **attrs,
    int attrsonly,
    Slapi_Entry **ep,
    int *foundp
)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *pb = NULL;
    int i, ret;

    LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search basedn=\"%s\" filter=\"%s\"\n", basedn, filter, 0);

    /* TODO: set size and time limits */
    pb = slapi_pblock_new();
    if (!pb) {
        LDAPDebug(LDAP_DEBUG_TRACE, "null pblock for search_internal_pb\n", 0, 0, 0);
        goto out;
    }

    slapi_search_internal_set_pb(pb, basedn, scope, filter, attrs, attrsonly, ctrls, 
                                 NULL, sasl_get_component_id(), 0); 

    slapi_search_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search failed basedn=\"%s\" "
                  "filter=\"%s\": %s\n", 
                  basedn, filter, ldap_err2string(ret));
        goto out;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL)) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found no entries\n",
                  0, 0, 0);
        goto out;
    }

    for (i = 0; entries[i]; i++) {
        (*foundp)++;
        if (*ep != NULL) {
            slapi_entry_free(*ep);
        }
        *ep = slapi_entry_dup(entries[i]);
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found dn=%s\n",
                  slapi_entry_get_dn(*ep), 0, 0);
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
static Slapi_Entry *ids_sasl_user_to_entry(
    sasl_conn_t *conn,
    void *context,
    const char *user,
    const char *user_realm
)
{
    int found = 0;
    int attrsonly = 0, scope = LDAP_SCOPE_SUBTREE;
    LDAPControl **ctrls = NULL;
    Slapi_Entry *entry = NULL;
    char **attrs = NULL;
    int regexmatch = 0;
    char *base = NULL;
    char *filter = NULL;

    /* Check for wildcards in the authid and realm. If we encounter one,
     * just fail the mapping without performing a costly internal search. */
    if (user && strchr(user, '*')) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search encountered a wildcard in "
            "the authid.  Not attempting to map to entry. (authid=%s)\n", user, 0, 0);
        return NULL;
    } else if (user_realm && strchr(user_realm, '*')) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search encountered a wildcard in "
            "the realm.  Not attempting to map to entry. (realm=%s)\n", user_realm, 0, 0);
        return NULL;
    }

    /* New regex-based identity mapping */
    regexmatch = sasl_map_domap((char*)user, (char*)user_realm, &base, &filter);
    if (regexmatch) {
        ids_sasl_user_search(base, scope, filter, 
                             ctrls, attrs, attrsonly,
                             &entry, &found);

        if (found == 1) {
            LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found this entry: dn:%s, "
                "matching filter=%s\n", entry->e_sdn.dn, filter, 0);
        } else if (found == 0) {
            LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found no entries matching "
                "filter=%s\n", filter, 0, 0);
        } else {
            LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found more than one entry "
                "matching filter=%s\n", filter, 0, 0);
            if (entry) {
                slapi_entry_free(entry);
                entry = NULL;
            }
        }

        /* Free the filter etc */
        slapi_ch_free_string(&base);
        slapi_ch_free_string(&filter);
    }

    return entry;
}

static char *buf2str(const char *buf, unsigned buflen)
{
    char *ret;

    ret = (char*)slapi_ch_malloc(buflen+1);
    memcpy(ret, buf, buflen);
    ret[buflen] = '\0';

    return ret;
}

/* Note that in this sasl1 API, when it says 'authid' it really means 'authzid'. */
static int ids_sasl_canon_user(
    sasl_conn_t *conn,
    void *context,
    const char *userbuf, unsigned ulen,
    unsigned flags, const char *user_realm,
    char *out_user, unsigned out_umax, unsigned *out_ulen
)
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
    LDAPDebug(LDAP_DEBUG_TRACE, 
              "ids_sasl_canon_user(user=%s, realm=%s)\n", 
              user, user_realm ? user_realm : "", 0);

    sasl_getprop(conn, SASL_MECHNAME, (const void**)&mech);
    if (mech == NULL) {
        LDAPDebug0Args(LDAP_DEBUG_TRACE, "Unable to read SASL mechanism while "
              "canonifying user.\n")
        goto fail;
    }

    if (strncasecmp(user, "dn:", 3) == 0) {
        sdn = slapi_sdn_new();
        slapi_sdn_set_dn_byval(sdn, user+3);
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
            LDAPDebug(LDAP_DEBUG_TRACE, "prop_set(dn) failed\n", 0, 0, 0);
            goto fail;
        }
    } else if (prop_set(propctx, "dn", dn, -1) != 0) {
        LDAPDebug(LDAP_DEBUG_TRACE, "prop_set(dn) failed\n", 0, 0, 0);
        goto fail;
    }

    /* We need to check if the first character of pw is an opening
     * brace since strstr will simply return it's first argument if
     * it is an empty string. */
    if (pw && (*pw == '{')) {
        if (strchr( pw, '}' )) {
            /* This password is stored in a non-cleartext format.
             * Any SASL mechanism that actually needs the
             * password is going to fail.  We should print a warning
             * to aid in troubleshooting. */
            LDAPDebug(LDAP_DEBUG_TRACE, "Warning: Detected a sasl bind attempt by an "
                      "entry whose password is stored in a non-cleartext format.  This "
                      "will not work for mechanisms which require a cleartext password "
                      "such as DIGEST-MD5 and CRAM-MD5.\n", 0, 0, 0);
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
            /*LDAPDebug(LDAP_DEBUG_TRACE, "prop_set(userpassword) failed\n", 0, 0, 0);
            goto fail */ ;
        }
#endif /* SASL_AUX_PASSWORD_PROP */
        if (prop_set(propctx, SASL_AUX_PASSWORD, clear, -1) != 0) {
            /* Failure is benign here because some mechanisms don't support this property */
            /*LDAPDebug(LDAP_DEBUG_TRACE, "prop_set(userpassword) failed\n", 0, 0, 0);
            goto fail */ ;
        }
    }

    slapi_entry_free(entry);
    slapi_ch_free((void**)&user);
    slapi_ch_free((void**)&pw);
    slapi_sdn_free(&sdn);

    return SASL_OK;

fail:
    slapi_entry_free(entry);
    slapi_ch_free((void**)&user);
    slapi_ch_free((void**)&pw);
    slapi_sdn_free(&sdn);

    return returnvalue;
}

static int ids_sasl_getpluginpath(sasl_conn_t *conn, const char **path)
{
    /* Try to get path from config, otherwise check for SASL_PATH environment
     * variable.  If neither of these are set, default to /usr/lib64/sasl2 on
     * 64-bit Linux machines, and /usr/lib/sasl2 on all other platforms.
     */
    char *pluginpath = config_get_saslpath();
    if ((!pluginpath) || (*pluginpath == '\0')) {
        if (!(pluginpath = getenv("SASL_PATH"))) {
#if defined(LINUX) && defined(__LP64__)
            pluginpath = "/usr/lib64/sasl2";
#else
            pluginpath = "/usr/lib/sasl2";
#endif
        }
    }
    *path = pluginpath;
    return SASL_OK;
}

static sasl_callback_t ids_sasl_callbacks[] =
{
    {
      SASL_CB_GETOPT,
      (IFP) ids_sasl_getopt,
      NULL
    },
    {
      SASL_CB_LOG,
      (IFP) ids_sasl_log,
      NULL
    },
    {
      SASL_CB_PROXY_POLICY,
      (IFP) ids_sasl_proxy_policy,
      NULL
    },
    {
      SASL_CB_CANON_USER,
      (IFP) ids_sasl_canon_user,
      NULL
    },
    {
      SASL_CB_GETPATH,
      (IFP) ids_sasl_getpluginpath,
      NULL
    },
    {
      SASL_CB_LIST_END,
      (IFP) NULL,
      NULL
    }
};

static const char *dn_propnames[] = { "dn", 0 };

/*
 * initialize the sasl library
 */
int ids_sasl_init(void)
{
    static int inited = 0;
    int result;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_init\n", 0, 0, 0 );

    PR_ASSERT(inited == 0);
    inited = 1;

    serverfqdn = get_localhost_DNS();

    LDAPDebug(LDAP_DEBUG_TRACE, "sasl service fqdn is: %s\n", 
                  serverfqdn, 0, 0);

    /* get component ID for internal operations */
    generate_component_id();

    /* Set SASL memory allocation callbacks */
    sasl_set_alloc(
        (sasl_malloc_t *)slapi_ch_malloc,
        (sasl_calloc_t *)slapi_ch_calloc,
        (sasl_realloc_t *)slapi_ch_realloc,
        (sasl_free_t *)nssasl_free );

    /* Set SASL mutex callbacks */
    sasl_set_mutex(
        (sasl_mutex_alloc_t *)nssasl_mutex_alloc,
        (sasl_mutex_lock_t *)nssasl_mutex_lock,
        (sasl_mutex_unlock_t *)nssasl_mutex_unlock,
        (sasl_mutex_free_t *)nssasl_mutex_free);

    result = sasl_server_init(ids_sasl_callbacks, "iDS");

    if (result != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_TRACE, "failed to initialize sasl library\n", 
                  0, 0, 0);
        return result;
    }

    result = sasl_auxprop_add_plugin("iDS", ids_auxprop_plug_init);

    LDAPDebug( LDAP_DEBUG_TRACE, "<= ids_sasl_init\n", 0, 0, 0 );

    return result;
}

/*
 * create a sasl server connection
 */
void ids_sasl_server_new(Connection *conn)
{
    int rc;
    sasl_conn_t *sasl_conn = NULL;
    struct propctx *propctx;
    sasl_security_properties_t secprops = {0};

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_server_new (%s)\n", serverfqdn, 0, 0 );

    rc = sasl_server_new("ldap", 
                         serverfqdn,
                         NULL,  /* user_realm */
                         NULL,  /* iplocalport */
                         NULL,  /* ipremoteport */
                         ids_sasl_callbacks,
                         SASL_SUCCESS_DATA, 
                         &sasl_conn);

    if (rc != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_ANY, "sasl_server_new: %s\n", 
                  sasl_errstring(rc, NULL, NULL), 0, 0);
    }

    if (rc == SASL_OK) {
        propctx = sasl_auxprop_getctx(sasl_conn);
        if (propctx != NULL) {
            prop_request(propctx, dn_propnames);
        }
    }

    /* Enable security for this connection */
    secprops.maxbufsize = 2048; /* DBDB: hack */
    secprops.max_ssf = 0xffffffff;
    secprops.min_ssf = config_get_minssf();
    /* If anonymous access is disabled, set the appropriate flag */
    if (config_get_anon_access_switch() != SLAPD_ANON_ACCESS_ON) {
        secprops.security_flags = SASL_SEC_NOANONYMOUS;
    }

    rc = sasl_setprop(sasl_conn, SASL_SEC_PROPS, &secprops);

    if (rc != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_ANY, "sasl_setprop: %s\n",
                  sasl_errstring(rc, NULL, NULL), 0, 0);
    }
    
    conn->c_sasl_conn = sasl_conn;
    conn->c_sasl_ssf = 0;

    LDAPDebug( LDAP_DEBUG_TRACE, "<= ids_sasl_server_new\n", 0, 0, 0 );

    return;
}

/*
 * return sasl mechanisms available on this connection.
 * caller must free returned charray.
 */
char **ids_sasl_listmech(Slapi_PBlock *pb)
{
    char **ret, **others;
    const char *str;
    char *dupstr;
    sasl_conn_t *sasl_conn;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_listmech\n", 0, 0, 0 );

    PR_ASSERT(pb);

    /* hard-wired mechanisms and slapi plugin registered mechanisms */
    ret = slapi_get_supported_saslmechanisms_copy();

    if (pb->pb_conn == NULL) return ret;

    sasl_conn = (sasl_conn_t*)pb->pb_conn->c_sasl_conn;
    if (sasl_conn == NULL) return ret;

    /* sasl library mechanisms are connection dependent */
    PR_Lock(pb->pb_conn->c_mutex);
    if (sasl_listmech(sasl_conn, 
                      NULL,     /* username */
                      "", ",", "",
                      &str, NULL, NULL) == SASL_OK) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl library mechs: %s\n", str, 0, 0);
        /* merge into result set */
        dupstr = slapi_ch_strdup(str);
        others = slapi_str2charray_ext(dupstr, ",", 0 /* don't list duplicate mechanisms */);
        charray_merge(&ret, others, 1);
        charray_free(others);
        slapi_ch_free((void**)&dupstr);
    }
    PR_Unlock(pb->pb_conn->c_mutex);

    LDAPDebug( LDAP_DEBUG_TRACE, "<= ids_sasl_listmech\n", 0, 0, 0 );

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
  int i, ret = 0;
  char **mechs;
  char *dupstr;
  const char *str;
  int sasl_result = 0;
  sasl_conn_t *sasl_conn = (sasl_conn_t *)pb->pb_conn->c_sasl_conn;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_mech_supported\n", 0, 0, 0 );


  /* sasl_listmech is not thread-safe - caller must lock pb_conn */
  sasl_result = sasl_listmech(sasl_conn, 
                    NULL,     /* username */
                    "", ",", "",
                    &str, NULL, NULL);
  if (sasl_result != SASL_OK) {
    return 0;
  }

  dupstr = slapi_ch_strdup(str);
  mechs = slapi_str2charray(dupstr, ",");

  for (i = 0; mechs[i] != NULL; i++) {
    if (strcasecmp(mech, mechs[i]) == 0) {
      ret = 1;
      break;
    }
  }

  charray_free(mechs);
  slapi_ch_free((void**)&dupstr);

  LDAPDebug( LDAP_DEBUG_TRACE, "<= ids_sasl_mech_supported\n", 0, 0, 0 );

  return ret;
}

/*
 * do a sasl bind and return a result
 */
void ids_sasl_check_bind(Slapi_PBlock *pb)
{
    int rc, isroot;
    long t;
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
    char errorbuf[BUFSIZ];

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_check_bind\n", 0, 0, 0 );

    PR_ASSERT(pb);
    PR_ASSERT(pb->pb_conn);

    PR_Lock(pb->pb_conn->c_mutex); /* BIG LOCK */
    continuing = pb->pb_conn->c_flags & CONN_FLAG_SASL_CONTINUE;
    pb->pb_conn->c_flags &= ~CONN_FLAG_SASL_CONTINUE; /* reset flag */

    sasl_conn = (sasl_conn_t*)pb->pb_conn->c_sasl_conn;
    if (sasl_conn == NULL) {
        PR_Unlock(pb->pb_conn->c_mutex);
        send_ldap_result( pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                          "sasl library unavailable", 0, NULL );
        return;
    }

    slapi_pblock_get(pb, SLAPI_BIND_SASLMECHANISM, &mech);
    slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred);
    slapi_pblock_get(pb, SLAPI_PWPOLICY, &pwresponse_requested);
    PR_ASSERT(mech);
    PR_ASSERT(cred);

    /* Work around a bug in the sasl library. We've told the
     * library that CRAM-MD5 is disabled, but it gives us a
     * different error code to SASL_NOMECH.  Must be called
     * while holding the pb_conn lock
     */
    if (!ids_sasl_mech_supported(pb, mech)) {
      rc = SASL_NOMECH;
      goto sasl_check_result;
    }

    /* can't do any harm */
    if (cred->bv_len == 0) cred->bv_val = NULL;

    if (continuing) {
        /* 
         * RFC 2251: a client may abort a sasl bind negotiation by
         * sending a bindrequest with a different value in the
         * mechanism field.
         */
        sasl_getprop(sasl_conn, SASL_MECHNAME, (const void**)&activemech);
        if (activemech == NULL) {
            LDAPDebug(LDAP_DEBUG_TRACE, "could not get active sasl mechanism\n", 0, 0, 0);
            goto sasl_start;
        }
        if (strcasecmp(activemech, mech) != 0) {
            LDAPDebug(LDAP_DEBUG_TRACE, "sasl mechanisms differ: active=%s current=%s\n", 0, 0, 0);
            goto sasl_start;
        }

        rc = sasl_server_step(sasl_conn, 
                              cred->bv_val, cred->bv_len, 
                              &sdata, &slen);
        goto sasl_check_result;
    }

 sasl_start:

    /* Check if we are already authenticated via sasl.  If so,
     * dispose of the current sasl_conn and create a new one
     * using the new mechanism.  We also need to do this if the
     * mechanism changed in the middle of the SASL authentication
     * process. */
    if ((pb->pb_conn->c_flags & CONN_FLAG_SASL_COMPLETE) || continuing) {
        Slapi_Operation *operation;
        slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
        slapi_log_error(SLAPI_LOG_CONNS, "ids_sasl_check_bind",
                        "cleaning up sasl IO conn=%" NSPRIu64 " op=%d complete=%d continuing=%d\n",
                        pb->pb_conn->c_connid, operation->o_opid,
                        (pb->pb_conn->c_flags & CONN_FLAG_SASL_COMPLETE), continuing);
        /* reset flag */
        pb->pb_conn->c_flags &= ~CONN_FLAG_SASL_COMPLETE;

        /* remove any SASL I/O from the connection */
        connection_set_io_layer_cb(pb->pb_conn, NULL, sasl_io_cleanup, NULL);

        /* dispose of sasl_conn and create a new sasl_conn */
        sasl_dispose(&sasl_conn);
        ids_sasl_server_new(pb->pb_conn);
        sasl_conn = (sasl_conn_t*)pb->pb_conn->c_sasl_conn;

        if (sasl_conn == NULL) {
            send_ldap_result( pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                          "sasl library unavailable", 0, NULL );
            PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */
            return;
        }
    }

    rc = sasl_server_start(sasl_conn, mech, 
                           cred->bv_val, cred->bv_len, 
                           &sdata, &slen);

 sasl_check_result:

    switch (rc) {
    case SASL_OK:               /* complete */
        /* retrieve the authenticated username */
        if (sasl_getprop(sasl_conn, SASL_USERNAME,
                         (const void**)&username) != SASL_OK) {
            PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */
            send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                             "could not obtain sasl username", 0, NULL);
            break;
        }

        LDAPDebug(LDAP_DEBUG_TRACE, "sasl authenticated mech=%s user=%s\n",
                  mech, username, 0);

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
            PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */
            send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                             "could not get auth dn from sasl", 0, NULL);
            break;
        }

        /* clean up already set TARGET */
        slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &sdn);
        slapi_sdn_free(&sdn);

        sdn = slapi_sdn_new_dn_passin(dn);
        normdn = slapi_sdn_get_dn(sdn);

        slapi_pblock_set( pb, SLAPI_BIND_TARGET_SDN, sdn );

        if ((sasl_getprop(sasl_conn, SASL_SSF, 
                          (const void**)&ssfp) == SASL_OK) && (*ssfp > 0)) {
            LDAPDebug(LDAP_DEBUG_TRACE, "sasl ssf=%u\n", (unsigned)*ssfp, 0, 0);
        } else {
            *ssfp = 0;
        }

        /* Set a flag to signify that sasl bind is complete */
        pb->pb_conn->c_flags |= CONN_FLAG_SASL_COMPLETE;
        /* note - we set this here in case there are pre-bind
           plugins that want to know what the negotiated
           ssf is - but this happens before we actually set
           up the socket for SASL encryption - so one
           consequence is that we attempt to do sasl
           encryption on the connection after the pre-bind
           plugin has been called, and sasl encryption fails
           and the operation returns an error */
        pb->pb_conn->c_sasl_ssf = (unsigned)*ssfp;

        /* set the connection bind credentials */
        PR_snprintf(authtype, sizeof(authtype), "%s%s", SLAPD_AUTH_SASL, mech);
        /* normdn is consumed by bind_credentials_set_nolock */
        bind_credentials_set_nolock(pb->pb_conn, authtype, 
                                    slapi_ch_strdup(normdn), 
                                    NULL, NULL, NULL, bind_target_entry);

        PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */

        if (plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_BIND_FN ) != 0){
            break;
        }

        isroot = slapi_dn_isroot(normdn);

        if (!isroot )
        {
            /* check if the account is locked */
            bind_target_entry = get_entry(pb,  normdn);
            if ( bind_target_entry == NULL )
            {
                goto out;
            } 
            if ( slapi_check_account_lock(pb, bind_target_entry, pwresponse_requested, 1, 1) == 1) {
                goto out;
            }
        }

        /* set the auth response control if requested */
        slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrls);
        if (slapi_control_present(ctrls, LDAP_CONTROL_AUTH_REQUEST, 
                                  NULL, NULL)) {
            slapi_add_auth_response_control(pb, normdn);
        }

        if (slapi_mapping_tree_select(pb, &be, &referral, errorbuf) != LDAP_SUCCESS) {
            send_nobackend_ldap_result( pb );
            be = NULL;
            LDAPDebug( LDAP_DEBUG_TRACE, "<= ids_sasl_check_bind\n", 0, 0, 0 );
            return; 
        }

        if (referral) {
            send_referrals_from_entry(pb,referral);
            goto out;
        }

        slapi_pblock_set( pb, SLAPI_BACKEND, be );

        slapi_pblock_set( pb, SLAPI_PLUGIN, be->be_database );
        set_db_default_result_handlers(pb);

        /* check password expiry */
        if (!isroot) {
            int pwrc;

            pwrc = need_new_pw(pb, &t, bind_target_entry, pwresponse_requested);
            
            switch (pwrc) {
            case 1:
                slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
                break;
            case 2:
                slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRING, t);
                break;
            case -1:
                goto out;
            default:
                break;
            }
        }

        /* attach the sasl data */
        if (slen != 0) {
            bvr.bv_val = (char*)sdata;
            bvr.bv_len = slen;
            slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, &bvr);
        }

        /* see if we negotiated a security layer */
        if (*ssfp > 0) {
            /* Enable SASL I/O on the connection */
            PR_Lock(pb->pb_conn->c_mutex);
            connection_set_io_layer_cb(pb->pb_conn, sasl_io_enable, NULL, NULL);
            PR_Unlock(pb->pb_conn->c_mutex);
        }

        /* send successful result */
        send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

        /* remove the sasl data from the pblock */
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, NULL);

        break;

    case SASL_CONTINUE:         /* another step needed */
        pb->pb_conn->c_flags |= CONN_FLAG_SASL_CONTINUE;
        PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */

        if (plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_BIND_FN ) != 0){
            break;
        }

        /* attach the sasl data */
        bvr.bv_val = (char*)sdata;
        bvr.bv_len = slen;
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, &bvr);

        /* send continuation result */
        send_ldap_result( pb, LDAP_SASL_BIND_IN_PROGRESS, NULL, 
                          NULL, 0, NULL );

        /* remove the sasl data from the pblock */
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, NULL);

        break;

    case SASL_NOMECH:

        PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */
        send_ldap_result(pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                         "sasl mechanism not supported", 0, NULL);
        break;

    default:                    /* other error */
        errstr = sasl_errdetail(sasl_conn);

        PR_Unlock(pb->pb_conn->c_mutex); /* BIG LOCK */
        send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL,
                         (char*)errstr, 0, NULL);
        break;
    }

    out:
        if (referral)
            slapi_entry_free(referral);
        if (be)
            slapi_be_Unlock(be);
        if (bind_target_entry)
            slapi_entry_free(bind_target_entry);

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_check_bind\n", 0, 0, 0 );

    return;
}
