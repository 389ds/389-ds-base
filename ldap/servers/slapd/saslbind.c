/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#define CYRUS_SASL 1

#include <slap.h>
#include <fe.h>
#include <sasl.h>
#include <saslplug.h>
#ifndef CYRUS_SASL
#include <saslmod.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

/* No GSSAPI on Windows */
#if !defined(_WIN32)
#define BUILD_GSSAPI 1
#endif

static char *serverfqdn;

/*
 * utility functions needed by the sasl library
 */

int sasl_os_gethost(char *buf, int len)
{
    int rc;

    rc = gethostname(buf, len);
    LDAPDebug(LDAP_DEBUG_TRACE, "sasl_os_gethost %s\n", buf, 0, 0);
    return ( rc == 0 ? SASL_OK : SASL_FAIL );
}

void *sasl_mutex_alloc(void)
{
    return PR_NewLock();
}

int sasl_mutex_lock(void *mutex)
{
    PR_Lock(mutex);
    return SASL_OK;
}

int sasl_mutex_unlock(void *mutex)
{
    if (PR_Unlock(mutex) == PR_SUCCESS) return SASL_OK;
    return SASL_FAIL;
}

void sasl_mutex_free(void *mutex)
{
    PR_DestroyLock(mutex);
}

/* 
 * sasl library callbacks
 */

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
    slapi_log_error(SLAPI_LOG_FATAL, "sasl", "%s", message);
    break;

    case SASL_LOG_FAIL:         /* log all authentication failures */
    case SASL_LOG_WARN:         /* log non-fatal warnings */
    case SASL_LOG_NOTE:         /* more verbose than LOG_WARN */
    case SASL_LOG_DEBUG:        /* more verbose than LOG_NOTE */
    case SASL_LOG_TRACE:        /* traces of internal protocols */
    case SASL_LOG_PASS:         /* traces of internal protocols, including
                                 * passwords */
        LDAPDebug(LDAP_DEBUG_ANY, "sasl(%d): %s", level, message, 0);
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
    Slapi_PBlock *pb;
    int i, ret;

    LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search basedn=\"%s\" filter=\"%s\"\n", basedn, filter, 0);

    /* TODO: set size and time limits */

    pb = slapi_search_internal(basedn, scope, filter, 
                               ctrls, attrs, attrsonly);
    if (pb == NULL) {
        LDAPDebug(LDAP_DEBUG_TRACE, "null pblock from slapi_search_internal\n", 0, 0, 0);
        goto out;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search failed basedn=\"%s\" "
                  "filter=\"%s\": %s\n", 
                  basedn, filter, ldap_err2string(ret));
        goto out;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (entries == NULL) goto out;

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

    if (pb) slapi_free_search_results_internal(pb);
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
    unsigned fsize = 0, ulen, rlen = 0;
    int attrsonly = 0, scope = LDAP_SCOPE_SUBTREE;
    char filter[1024], *fptr = filter;
    LDAPControl **ctrls = NULL;
    Slapi_Entry *entry = NULL;
    Slapi_DN *sdn;
    char **attrs = NULL;
    char *userattr = "uid", *realmattr = NULL, *ufilter = NULL;
    void *node;
    int regexmatch = 0;
    char *regex_ldap_search_base = NULL;
    char *regex_ldap_search_filter = NULL;

    /* TODO: userattr & realmattr should be configurable */

    /*
     * Check for dn: prefix. See RFC 2829 section 9.
     */
    if (strncasecmp(user, "dn:", 3) == 0) {
        sprintf(fptr, "(objectclass=*)");
        scope = LDAP_SCOPE_BASE;
        ids_sasl_user_search((char*)user+3, scope, filter, 
                             ctrls, attrs, attrsonly,
                             &entry, &found);
    } else {
        int offset = 0; 
        if (strncasecmp(user,"u:",2) == 0 )
            offset = 2;
        /* TODO: quote the filter values */

        /* New regex-based identity mapping : we call it here before the old code.
         * If there's a match, we skip the old way, otherwise we plow ahead for backwards compatibility reasons
         */

        regexmatch = sasl_map_domap((char*)user, (char*)user_realm, &regex_ldap_search_base, &regex_ldap_search_filter);
        if (regexmatch) {

            ids_sasl_user_search(regex_ldap_search_base, scope, regex_ldap_search_filter, 
                                 ctrls, attrs, attrsonly,
                                 &entry, &found);

            /* Free the filter etc */
            slapi_ch_free((void**)&regex_ldap_search_base);
            slapi_ch_free((void**)&regex_ldap_search_filter);
            } else {
    
            /* Ensure no buffer overflow. */
            /* We don't know what the upper limits on username and
             * realm lengths are. There don't seem to be any defined
             * in the relevant standards. We may find in the future
             * that a 1K buffer is insufficient for some mechanism,
             * but it seems unlikely given that the values are exposed 
             * to the end user.
             */
            ulen = strlen(user+offset);
            fsize += strlen(userattr) + ulen;
            if (realmattr && user_realm) {
                rlen = strlen(user_realm);
                fsize += strlen(realmattr) + rlen;
            }
            if (ufilter) fsize += strlen(ufilter);
            fsize += 100;            /* includes a good safety margin */
            if (fsize > 1024) {
                LDAPDebug(LDAP_DEBUG_ANY, "sasl user name and/or realm too long"
                          " (ulen=%u, rlen=%u)\n", ulen, rlen, 0);
                return NULL;
            }
    
            /* now we can safely write the filter */
            sprintf(fptr, "(&(%s=%s)", userattr, user+offset);
            fptr += strlen(fptr);
            if (realmattr && user_realm) {
                sprintf(fptr, "(%s=%s)", realmattr, user_realm);
                fptr += strlen(fptr);
            }
            if (ufilter) {
                if (*ufilter == '(') {
                    sprintf(fptr, "%s", ufilter);
                } else {
                    sprintf(fptr, "(%s)", ufilter);
                }
                fptr += strlen(fptr);
            }
            sprintf(fptr, ")");
    
            /* iterate through the naming contexts */
            for (sdn = slapi_get_first_suffix(&node, 0); sdn != NULL;
                 sdn = slapi_get_next_suffix(&node, 0)) {
    
                ids_sasl_user_search((char*)slapi_sdn_get_dn(sdn), scope, filter, 
                                     ctrls, attrs, attrsonly,
                                     &entry, &found);
            }
        }
    }

    if (found == 1) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found this entry: dn:%s, matching filter=%s\n", entry->e_sdn.dn, filter, 0);
        return entry;
    }

    if (found == 0) {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found no entries matching filter=%s\n", filter, 0, 0);
    } else {
        LDAPDebug(LDAP_DEBUG_TRACE, "sasl user search found more than one entry matching filter=%s\n", filter, 0, 0);
    }

    if (entry) slapi_entry_free(entry);
    return NULL;
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
#ifndef CYRUS_SASL
    const char *authidbuf, unsigned alen,
#endif
    unsigned flags, const char *user_realm,
    char *out_user, unsigned out_umax, unsigned *out_ulen
#ifndef CYRUS_SASL
    ,char *out_authid, unsigned out_amax, unsigned *out_alen
#endif
)
{
    struct propctx *propctx = sasl_auxprop_getctx(conn);
    Slapi_Entry *entry = NULL;
    Slapi_DN *sdn = NULL;
    char *pw = NULL;
    char *user = NULL;
#ifndef CYRUS_SASL
    char *authid = NULL;
#endif
    const char *dn;
    int isroot = 0;
    char *clear = NULL;
    int returnvalue = SASL_FAIL;

    user = buf2str(userbuf, ulen);
    if (user == NULL) {
        goto fail;
    } 
#ifdef CYRUS_SASL
    LDAPDebug(LDAP_DEBUG_TRACE, 
              "ids_sasl_canon_user(user=%s, realm=%s)\n", 
              user, user_realm ? user_realm : "", 0);
#else
    authid = buf2str(authidbuf, alen);

    LDAPDebug(LDAP_DEBUG_TRACE, 
              "ids_sasl_canon_user(user=%s, authzid=%s, realm=%s)\n", 
              user, authid, user_realm ? user_realm : "");
#endif

    if (strncasecmp(user, "dn:", 3) == 0) {
        sdn = slapi_sdn_new();
        slapi_sdn_set_dn_byval(sdn, user+3);
        isroot = slapi_dn_isroot(slapi_sdn_get_ndn(sdn));
    }

    if (isroot) {
        /* special case directory manager */
        dn = slapi_sdn_get_ndn(sdn);
        pw = config_get_rootpw();
    } else {
        /* map the sasl username into an entry */
        entry = ids_sasl_user_to_entry(conn, context, user, user_realm);
        if (entry == NULL) {
#ifdef CYRUS_SASL
            /* Specific return value is supposed to be set instead of 
               an generic error (SASL_FAIL) for Cyrus SASL */
            returnvalue = SASL_NOAUTHZ;
#endif
            goto fail;
        }
        dn = slapi_entry_get_ndn(entry);
        pw = slapi_entry_attr_get_charptr(entry, "userpassword");
    }

    if (prop_set(propctx, "dn", dn, -1) != 0) {
        LDAPDebug(LDAP_DEBUG_TRACE, "prop_set(dn) failed\n", 0, 0, 0);
        goto fail;
    }

    clear = pw;
    if (clear) {
        if (prop_set(propctx, "userpassword", clear, -1) != 0) {
            /* Failure is benign here because some mechanisms don't support this property */
            /*LDAPDebug(LDAP_DEBUG_TRACE, "prop_set(userpassword) failed\n", 0, 0, 0);
            goto fail */ ;
        }
    }

    /* TODO: canonicalize */
    PL_strncpyz(out_user, dn, out_umax);
#ifdef CYRUS_SASL
    /* the length of out_user needs to be set for Cyrus SASL */
    *out_ulen = strlen(out_user);
#else
    if (authid )
    {
        int offset = 0;
        /* The authid can start with dn:. In such case remove it */    
        if (strncasecmp(authid,"dn:",3) == 0 )
            offset = 3;
        PL_strncpyz(out_authid, authid+offset, out_amax);
    }
    *out_ulen = -1;
    *out_alen = -1;
    slapi_ch_free((void**)&authid);
#endif

    slapi_entry_free(entry);
    slapi_ch_free((void**)&user);
    slapi_ch_free((void**)&pw);
    slapi_sdn_free(&sdn);

    return SASL_OK;

 fail:
    slapi_entry_free(entry);
    slapi_ch_free((void**)&user);
#ifndef CYRUS_SASL
    slapi_ch_free((void**)&authid);
#endif
    slapi_ch_free((void**)&pw);
    slapi_sdn_free(&sdn);

    return returnvalue;
}

static sasl_callback_t ids_sasl_callbacks[5] =
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
#ifdef CYRUS_SASL
      SASL_CB_CANON_USER,
#else
      SASL_CB_SERVER_CANON_USER,
#endif
      (IFP) ids_sasl_canon_user,
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

    result = sasl_server_init(ids_sasl_callbacks, "iDS");

    if (result != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_TRACE, "failed to initialize sasl library\n", 
                  0, 0, 0);
        return result;
    }

#ifndef CYRUS_SASL
    result = sasl_server_add_plugin("USERDB", sasl_userdb_init);

    if (result != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_TRACE, "failed to add LDAP sasl plugin\n",
                  0, 0, 0);
        return result;
    }

#if defined(BUILD_GSSAPI)
    result = sasl_server_add_plugin("GSSAPI", sasl_gssapi_init);

    if (result != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_TRACE, "failed to add LDAP gssapi plugin\n",
                  0, 0, 0);
    }
#endif
#endif

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
    rc = sasl_setprop(sasl_conn, SASL_SEC_PROPS, &secprops);
    if (rc != SASL_OK) {
        LDAPDebug(LDAP_DEBUG_ANY, "sasl_setprop: %s\n",
                  sasl_errstring(rc, NULL, NULL), 0, 0);
    }
    
    conn->c_sasl_conn = sasl_conn;

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
        others = str2charray(dupstr, ",");
        charray_merge(&ret, others, 1);
        charray_free(others);
        slapi_ch_free((void**)&dupstr);
    }
    PR_Unlock(pb->pb_conn->c_mutex);

    LDAPDebug( LDAP_DEBUG_TRACE, ">= ids_sasl_listmech\n", 0, 0, 0 );

    return ret;
}

/*
 * Determine whether a given sasl mechanism is supported by
 * this sasl connection. Returns true/false.
 */
static int
ids_sasl_mech_supported(Slapi_PBlock *pb, sasl_conn_t *sasl_conn, const char *mech)
{
  int i, ret = 0;
  char **mechs;
  char *dupstr;
  const char *str;
  int sasl_result = 0;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_mech_supported\n", 0, 0, 0 );


  /* sasl_listmech is not thread-safe, so we lock here */
  PR_Lock(pb->pb_conn->c_mutex);
  sasl_result = sasl_listmech(sasl_conn, 
                    NULL,     /* username */
                    "", ",", "",
                    &str, NULL, NULL);
  PR_Unlock(pb->pb_conn->c_mutex);
  if (sasl_result != SASL_OK) {
    return 0;
  }

  dupstr = slapi_ch_strdup(str);
  mechs = str2charray(dupstr, ",");

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

    continuing = pb->pb_conn->c_flags & CONN_FLAG_SASL_CONTINUE;
    pb->pb_conn->c_flags &= ~CONN_FLAG_SASL_CONTINUE; /* reset flag */

    sasl_conn = (sasl_conn_t*)pb->pb_conn->c_sasl_conn;
    if (sasl_conn == NULL) {
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
     * different error code to SASL_NOMECH.
     */
    if (!ids_sasl_mech_supported(pb, sasl_conn, mech)) {
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

    rc = sasl_server_start(sasl_conn, mech, 
                           cred->bv_val, cred->bv_len, 
                           &sdata, &slen);

 sasl_check_result:

    switch (rc) {
    case SASL_OK:               /* complete */

        /* retrieve the authenticated username */
        if (sasl_getprop(sasl_conn, SASL_USERNAME,
                         (const void**)&username) != SASL_OK) {
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
                dn = slapi_ch_strdup(dnval[0].values[0]);
            }
        }
        if (dn == NULL) {
            send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                             "could not get auth dn from sasl", 0, NULL);
            break;
        }

        isroot = slapi_dn_isroot(dn);

        if (!isroot )
        {
            /* check if the account is locked */
            bind_target_entry = get_entry(pb,  dn);
            if ( bind_target_entry == NULL )
            {
                break;
            } 
            if ( check_account_lock(pb, bind_target_entry, pwresponse_requested) == 1) {
                slapi_entry_free(bind_target_entry);
                break;
            }
        }

        /* see if we negotiated a security layer */
        if ((sasl_getprop(sasl_conn, SASL_SSF, 
                          (const void**)&ssfp) == SASL_OK) && (*ssfp > 0)) {
            LDAPDebug(LDAP_DEBUG_TRACE, "sasl ssf=%u\n", (unsigned)*ssfp, 0, 0);

            if (pb->pb_conn->c_flags & CONN_FLAG_SSL) {
                send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                 "sasl encryption not supported over ssl", 
                                 0, NULL);
                if ( bind_target_entry != NULL )
                    slapi_entry_free(bind_target_entry);
                break;
            } else {
                /* Enable SASL I/O on the connection now */
                /* Note that this doesn't go into effect until the next _read_ operation is done */
                if (0 != sasl_io_enable(pb->pb_conn) ) {
                    send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
                                 "failed to enable sasl i/o",
                                 0, NULL);
                }
            }
        }

        /* set the connection bind credentials */
        PR_snprintf(authtype, sizeof(authtype), "%s%s", SLAPD_AUTH_SASL, mech);
        bind_credentials_set(pb->pb_conn, authtype, dn, 
                             NULL, NULL, NULL, bind_target_entry);

        /* set the auth response control if requested */
        slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrls);
        if (slapi_control_present(ctrls, LDAP_CONTROL_AUTH_REQUEST, 
                                  NULL, NULL)) {
            slapi_add_auth_response_control(pb, dn);
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
            if ( bind_target_entry != NULL ) {
                slapi_entry_free(bind_target_entry);
                bind_target_entry = NULL;
            }
            
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

        /* send successful result */
        send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

        /* TODO: enable sasl security layer */

        /* remove the sasl data from the pblock */
        slapi_pblock_set(pb, SLAPI_BIND_RET_SASLCREDS, NULL);

        break;

    case SASL_CONTINUE:         /* another step needed */
        pb->pb_conn->c_flags |= CONN_FLAG_SASL_CONTINUE;

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

        send_ldap_result(pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                         "sasl mechanism not supported", 0, NULL);
        break;

    default:                    /* other error */
        errstr = sasl_errdetail(sasl_conn);

        send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL,
                         (char*)errstr, 0, NULL);
        break;
    }

    out:
        if (referral)
            slapi_entry_free(referral);
        if (be)
            slapi_be_Unlock(be);

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ids_sasl_check_bind\n", 0, 0, 0 );

    return;
}

