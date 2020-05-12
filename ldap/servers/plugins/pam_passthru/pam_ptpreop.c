/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * pamptpreop.c - bind pre-operation plugin for Pass Through Authentication to PAM
 *
 */

#include "pam_passthru.h"

static Slapi_PluginDesc pdesc = {"pam_passthruauth", VENDOR, DS_PACKAGE_VERSION,
                                 "PAM pass through authentication plugin"};

static void *pam_passthruauth_plugin_identity = NULL;
static const Slapi_DN *pam_passthruauth_plugin_sdn = NULL;
static Slapi_RWLock *g_pam_config_lock = NULL;

/*
 * Plug-in globals
 */
PRCList *pam_passthru_global_config = NULL;

/*
 * function prototypes
 */
static int pam_passthru_bindpreop(Slapi_PBlock *pb);
static int pam_passthru_bindpreop_start(Slapi_PBlock *pb);
static int pam_passthru_bindpreop_close(Slapi_PBlock *pb);
static int pam_passthru_preop(Slapi_PBlock *pb, int modtype);
static int pam_passthru_add_preop(Slapi_PBlock *pb);
static int pam_passthru_mod_preop(Slapi_PBlock *pb);
static int pam_passthru_del_preop(Slapi_PBlock *pb);
static int pam_passthru_modrdn_preop(Slapi_PBlock *pb);
static int pam_passthru_postop(Slapi_PBlock *pb);
static int pam_passthru_internal_postop_init(Slapi_PBlock *pb);
static int pam_passthru_postop_init(Slapi_PBlock *pb);
static int pam_passthru_preop_init(Slapi_PBlock *pb);

/*
** Plugin identity mgmt
*/

void
pam_passthruauth_set_plugin_identity(void *identity)
{
    pam_passthruauth_plugin_identity = identity;
}

void *
pam_passthruauth_get_plugin_identity()
{
    return pam_passthruauth_plugin_identity;
}

void
pam_passthruauth_set_plugin_sdn(const Slapi_DN *plugin_sdn)
{
    pam_passthruauth_plugin_sdn = plugin_sdn;
}

const Slapi_DN *
pam_passthruauth_get_plugin_sdn()
{
    return pam_passthruauth_plugin_sdn;
}

const char *
pam_passthruauth_get_plugin_dn()
{
    return slapi_sdn_get_ndn(pam_passthruauth_plugin_sdn);
}

/*
 * Plugin initialization function (which must be listed in the appropriate
 * slapd config file).
 */
int
pam_passthruauth_init(Slapi_PBlock *pb)
{
    int status = 0;
    Slapi_Entry *plugin_entry = NULL;
    const char *plugin_type = NULL;
    int is_betxn = 0;
    int preadd = SLAPI_PLUGIN_PRE_ADD_FN;
    int premod = SLAPI_PLUGIN_PRE_MODIFY_FN;
    int predel = SLAPI_PLUGIN_PRE_DELETE_FN;
    int premdn = SLAPI_PLUGIN_PRE_MODRDN_FN;

    PAM_PASSTHRU_ASSERT(pb != NULL);

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "=> pam_passthruauth_init\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &pam_passthruauth_plugin_identity);
    PR_ASSERT(pam_passthruauth_plugin_identity);

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        is_betxn = 1;
        preadd = SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN;
        premod = SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN;
        predel = SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN;
        premdn = SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN;
    }

    if (is_betxn) {
        if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                             (void *)SLAPI_PLUGIN_VERSION_01) ||
            slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) ||
            slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                             (void *)pam_passthru_bindpreop_start) ||
            slapi_pblock_set(pb, preadd, (void *)pam_passthru_add_preop) ||
            slapi_pblock_set(pb, premod, (void *)pam_passthru_mod_preop) ||
            slapi_pblock_set(pb, predel, (void *)pam_passthru_del_preop) ||
            slapi_pblock_set(pb, premdn, (void *)pam_passthru_modrdn_preop)) {
            slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthruauth_init - Failed\n");
            status = -1;
            goto bail;
        }

        /* Register preop functions for the betxn enabled case */
        if (slapi_register_plugin("preoperation",                  /* op type */
                                  1,                               /* Enabled */
                                  "pam_passthruauth_init",         /* this function desc */
                                  pam_passthru_preop_init,         /* init func for pre op */
                                  PAM_PASSTHRU_PREOP_DESC,         /* plugin desc */
                                  NULL,                            /* ? */
                                  pam_passthruauth_plugin_identity /* access control */
                                  )) {
            slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthruauth_init - "
                          "Failed to register preop plugin\n");
            status = -1;
            goto bail;
        }
    } else {
        if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                             (void *)SLAPI_PLUGIN_VERSION_01) ||
            slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) ||
            slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                             (void *)pam_passthru_bindpreop_start) ||
            slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_BIND_FN,
                             (void *)pam_passthru_bindpreop) ||
            slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                             (void *)pam_passthru_bindpreop_close) ||
            slapi_pblock_set(pb, preadd, (void *)pam_passthru_add_preop) ||
            slapi_pblock_set(pb, premod, (void *)pam_passthru_mod_preop) ||
            slapi_pblock_set(pb, predel, (void *)pam_passthru_del_preop) ||
            slapi_pblock_set(pb, premdn, (void *)pam_passthru_modrdn_preop)) {
            slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthruauth_init - Failed\n");
            status = -1;
            goto bail;
        }
        /* Register internal postop functions. */
        /* If betxn is enabled, internal op is a part of betxn */
        if (slapi_register_plugin("internalpostoperation",           /* op type */
                                  1,                                 /* Enabled */
                                  "pam_passthruauth_init",           /* this function desc */
                                  pam_passthru_internal_postop_init, /* init func */
                                  PAM_PASSTHRU_INT_POSTOP_DESC,      /* plugin desc */
                                  NULL,                              /* ? */
                                  pam_passthruauth_plugin_identity   /* access control */
                                  )) {
            slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthruauth_init - "
                          "Failed to register internal postop plugin\n");
            status = -1;
            goto bail;
        }
    }

    /* Register postop functions */
    if (slapi_register_plugin(is_betxn ? "postoperation" : "betxnpostoperation", /* op type */
                              1,                                                 /* Enabled */
                              "pam_passthruauth_init",                           /* this function desc */
                              pam_passthru_postop_init,                          /* init func for post op */
                              PAM_PASSTHRU_POSTOP_DESC,                          /* plugin desc */
                              NULL,                                              /* ? */
                              pam_passthruauth_plugin_identity                   /* access control */
                              )) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthruauth_init - "
                      "Failed to register (%s) plugin\n",
                      is_betxn ? "postoperation" : "betxnpostoperation");
        status = -1;
        goto bail;
    }

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "<= pam_passthruauth_init\n");

bail:
    return status;
}

/*
 * Only if betxn is on, register just pre bind and close as perop operation.
 * The other preops (add/del/mod/mdn) are registered as betxn pre ops.
 */
static int
pam_passthru_preop_init(Slapi_PBlock *pb)
{
    int status = 0;
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_BIND_FN,
                         (void *)pam_passthru_bindpreop) ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)pam_passthru_bindpreop_close)) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_preop_init - Failed\n");
        status = -1;
    }
    return status;
}

static int
pam_passthru_internal_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *)pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *)pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *)pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *)pam_passthru_postop) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_internal_postop_init: failed to register plugin\n");
        status = -1;
    }

    return status;
}

static int
pam_passthru_postop_init(Slapi_PBlock *pb)
{
    int status = 0;
    Slapi_Entry *plugin_entry = NULL;
    const char *plugin_type = NULL;
    int is_betxn = 0;
    int postadd = SLAPI_PLUGIN_POST_ADD_FN;
    int postmod = SLAPI_PLUGIN_POST_MODIFY_FN;
    int postmdn = SLAPI_PLUGIN_POST_MODRDN_FN;
    int postdel = SLAPI_PLUGIN_POST_DELETE_FN;

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        postadd = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
        postmod = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
        postmdn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
        postdel = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
        is_betxn = 1;
    }

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) ||
        slapi_pblock_set(pb, postadd, (void *)pam_passthru_postop) ||
        slapi_pblock_set(pb, postdel, (void *)pam_passthru_postop) ||
        slapi_pblock_set(pb, postmod, (void *)pam_passthru_postop) ||
        slapi_pblock_set(pb, postmdn, (void *)pam_passthru_postop)) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_postop_init - "
                      "Failed to register (%s) plugin\n",
                      is_betxn ? "betxn postop" : "postop");
        status = -1;
    }

    return status;
}

/*
 * pam_passthru_bindpreop_start() is called before the directory server
 * is fully up.  We parse our configuration and initialize any mutexes, etc.
 */
static int
pam_passthru_bindpreop_start(Slapi_PBlock *pb)
{
    int rc = PAM_PASSTHRU_SUCCESS;
    Slapi_DN *pluginsdn = NULL;
    char *config_area = NULL;

    PAM_PASSTHRU_ASSERT(pb != NULL);

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "=> pam_passthru_bindpreop_start\n");

    /* Get the plug-in configuration DN and store it for later use. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &pluginsdn);
    if (NULL == pluginsdn || 0 == slapi_sdn_get_ndn_len(pluginsdn)) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop_start - Unable to determine plug-in config dn\n");
        rc = PAM_PASSTHRU_FAILURE;
        goto done;
    }

    /* Dup the plugin SDN to save it. */
    pam_passthruauth_set_plugin_sdn(slapi_sdn_dup(pluginsdn));

    /* Set the alternate config area if one is defined. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_AREA, &config_area);
    if (config_area) {
        pam_passthru_set_config_area(slapi_sdn_new_normdn_byval(config_area));
    } else {
        pam_passthru_set_config_area(slapi_sdn_dup(pluginsdn));
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "pam_passthru_bindpreop_start - Config at %s\n",
                  slapi_sdn_get_ndn(pam_passthru_get_config_area()));

    /* Create the lock that protects the config . */
    g_pam_config_lock = slapi_new_rwlock();

    if (!g_pam_config_lock) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop_start - Lock creation failed\n");
        rc = PAM_PASSTHRU_FAILURE;
        goto done;
    }

    /* Allocate the config list. */
    pam_passthru_global_config = (PRCList *)
        slapi_ch_calloc(1, sizeof(Pam_PassthruConfig));
    PR_INIT_CLIST(pam_passthru_global_config);

    /* Load config. */
    pam_passthru_load_config(0 /* don't skip validation */);

    if ((rc = pam_passthru_pam_init()) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop_start - Could not initialize PAM subsystem (%d)\n", rc);
        rc = PAM_PASSTHRU_FAILURE;
        goto done;
    }

done:
    if (rc != PAM_PASSTHRU_SUCCESS) {
        pam_passthru_delete_config();
        slapi_destroy_rwlock(g_pam_config_lock);
        g_pam_config_lock = NULL;
        slapi_ch_free((void **)&pam_passthru_global_config);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop_start - Ready for service\n");
    }

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "<= pam_passthru_bindpreop_start\n");

    return (rc);
}


/*
 * Called right before the Directory Server shuts down.
 */
static int
pam_passthru_bindpreop_close(Slapi_PBlock *pb)
{
    PAM_PASSTHRU_ASSERT(pb != NULL);

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "=> pam_passthru_bindpreop_close\n");

    pam_passthru_delete_config();
    pam_passthru_unlock();

    slapi_sdn_free((Slapi_DN **)&pam_passthruauth_plugin_sdn);
    pam_passthru_free_config_area();
    slapi_ch_free((void **)&pam_passthru_global_config);
    pam_passthru_pam_free();
    slapi_destroy_rwlock(g_pam_config_lock);
    g_pam_config_lock = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "<= pam_passthru_bindpreop_close\n");

    return (0);
}


static int
pam_passthru_bindpreop(Slapi_PBlock *pb)
{
    int rc = LDAP_SUCCESS;
    ber_tag_t method;
    const char *normbinddn;
    char *errmsg = NULL;
    Slapi_DN *bindsdn = NULL;
    Pam_PassthruConfig *cfg;
    struct berval *creds;
    int retcode = PAM_PASSTHRU_OP_NOT_HANDLED;

    PAM_PASSTHRU_ASSERT(pb != NULL);

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "=> pam_passthru_bindpreop\n");

    /*
     * retrieve parameters for bind operation
     */
    if (slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method) != 0 ||
        slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &bindsdn) != 0 ||
        slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &creds) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop - not handled (unable to retrieve bind parameters)\n");
        return retcode;
    }
    normbinddn = slapi_sdn_get_dn(bindsdn);

    /*
     * We only handle simple bind requests that include non-NULL binddn and
     * credentials.  Let the Directory Server itself handle everything else.
     */
    if (method != LDAP_AUTH_SIMPLE || normbinddn == NULL ||
        *normbinddn == '\0' || creds->bv_len == 0)
    {
        slapi_log_err(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop - Not handled (not simple bind or NULL dn/credentials)\n");
        return retcode;
    }

    /* Get the config lock.  From this point on, we must go to done
     * to be sure we unlock. */
    pam_passthru_read_lock();

    /* Bail out if the plug-in close function was just called. */
    if (!slapi_plugin_running(pb)) {
        goto done;
    }

    /* See if any of our config entries apply to this user */
    cfg = pam_passthru_get_config(bindsdn);

    if (!cfg) {
        slapi_log_err(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_bindpreop - \"%s\" Not handled (doesn't meet configuration criteria)\n", normbinddn);
        goto done;
    }

    if (cfg->pamptconfig_secure) { /* is a secure connection required? */
        int is_ssl = 0;
        slapi_pblock_get(pb, SLAPI_CONN_IS_SSL_SESSION, &is_ssl);
        if (!is_ssl) {
            slapi_log_err(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthru_bindpreop - Connection not secure (secure connection required; check config)\n");
            goto done;
        }
    }

    /*
     * We are now committed to handling this bind request.
     * Chain it off to PAM
     */
    rc = pam_passthru_do_pam_auth(pb, cfg);

    /*
     * If bind succeeded, change authentication information associated
     * with this connection.
     */
    if (rc == LDAP_SUCCESS) {
        char *ndn = slapi_ch_strdup(normbinddn);
        if ((slapi_pblock_set(pb, SLAPI_CONN_DN, ndn) != 0) ||
            (slapi_pblock_set(pb, SLAPI_CONN_AUTHMETHOD,
                              SLAPD_AUTH_SIMPLE) != 0)) {
            slapi_ch_free_string(&ndn);
            rc = LDAP_OPERATIONS_ERROR;
            errmsg = "unable to set connection DN or AUTHTYPE";
            slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthru_bindpreop - %s\n", errmsg);
        } else {
            LDAPControl **reqctrls = NULL;
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &reqctrls);
            if (slapi_control_present(reqctrls, LDAP_CONTROL_AUTH_REQUEST, NULL, NULL)) {
                slapi_add_auth_response_control(pb, ndn);
            }
        }
    }

    if (rc == LDAP_SUCCESS) {
        /* we are handling the result */
        slapi_send_ldap_result(pb, rc, NULL, errmsg, 0, NULL);
        /* tell bind code we handled the result */
        retcode = PAM_PASSTHRU_OP_HANDLED;
    } else if (!cfg->pamptconfig_fallback) {
        /* tell bind code we already sent back the error result in pam_ptimpl.c */
        retcode = PAM_PASSTHRU_OP_HANDLED;
    }

done:
    pam_passthru_unlock();

    slapi_log_err(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "pam_passthru_bindpreop - handled (error %d - %s)\n", rc, ldap_err2string(rc));

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "<= pam_passthru_bindpreop\n");

    return retcode;
}


/*
 * Pre-op callbacks for config change validation
 */
static int
pam_passthru_preop(Slapi_PBlock *pb, int modtype)
{
	Slapi_PBlock *entry_pb = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Entry *e = NULL;
    LDAPMod **mods;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE];
    int ret = SLAPI_PLUGIN_SUCCESS;

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "=> pam_passthru_preop\n");

    /* Get the target SDN. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    if (!sdn) {
        goto bail;
    }

    /* If this is a config entry, we need to validate it. */
    if (pam_passthru_dn_is_config(sdn)) {
        switch (modtype) {
        case LDAP_CHANGETYPE_ADD:
            slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
            /* Validate the entry being added. */
            if (PAM_PASSTHRU_FAILURE == pam_passthru_validate_config(e, returntext)) {
                ret = LDAP_UNWILLING_TO_PERFORM;
                goto bail;
            }
            break;
        case LDAP_CHANGETYPE_MODIFY:
            /* Fetch the entry being modified so we can
             * create the resulting entry for validation. */
            slapi_search_get_entry(&entry_pb, sdn, 0, &e,
                                   pam_passthruauth_get_plugin_identity());

            /* If the entry doesn't exist, just bail and
             * let the server handle it. */
            if (e == NULL) {
                goto bail;
            }

            /* Grab the mods. */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

            /* Apply the  mods to create the resulting entry.  If the mods
             * don't apply cleanly, we just let the main server code handle it. */
            if (mods && (slapi_entry_apply_mods(e, mods) == LDAP_SUCCESS)) {
                /* Validate the resulting entry. */
                if (PAM_PASSTHRU_FAILURE == pam_passthru_validate_config(e, returntext)) {
                    ret = LDAP_UNWILLING_TO_PERFORM;
                    /* Don't bail here, as we need to free the entry. */
                }
            }
            break;
        case LDAP_CHANGETYPE_DELETE:
        case LDAP_CHANGETYPE_MODDN:
            /* Don't allow the plug-in container in DSE to be deleted or renamed. */
            if (slapi_sdn_compare(sdn, pam_passthruauth_get_plugin_sdn()) == 0) {
                ret = LDAP_UNWILLING_TO_PERFORM;
            }
            break;
        }
    }

bail:
    slapi_search_get_entry_done(&entry_pb);
    /* If we are refusing the operation, return the result to the client. */
    if (ret) {
        slapi_send_ldap_result(pb, ret, NULL, returntext, 0, NULL);
        ret = SLAPI_PLUGIN_FAILURE;
    }
    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "<= pam_passthru_preop\n");

    return ret;
}

static int
pam_passthru_add_preop(Slapi_PBlock *pb)
{
    return pam_passthru_preop(pb, LDAP_CHANGETYPE_ADD);
}

static int
pam_passthru_mod_preop(Slapi_PBlock *pb)
{
    return pam_passthru_preop(pb, LDAP_CHANGETYPE_MODIFY);
}

static int
pam_passthru_del_preop(Slapi_PBlock *pb)
{
    return pam_passthru_preop(pb, LDAP_CHANGETYPE_DELETE);
}

static int
pam_passthru_modrdn_preop(Slapi_PBlock *pb)
{
    return pam_passthru_preop(pb, LDAP_CHANGETYPE_MODDN);
}

/*
 * Post-op callback for dynamic config loading.
 */
static int
pam_passthru_postop(Slapi_PBlock *pb)
{
    int ret = SLAPI_PLUGIN_SUCCESS;
    Slapi_DN *sdn = NULL;
    Slapi_DN *new_sdn = NULL;
    Slapi_Entry *e = NULL;
    int optype = SLAPI_OPERATION_NONE;
    int oprc = -1;

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "=> pam_passthru_postop\n");

    /* Make sure the operation succeeded and bail if it didn't. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc);
    if (oprc != 0) {
        ret = oprc;
        goto bail;
    }

    /* Get the target SDN. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    if (!sdn) {
        slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                      "pam_passthru_postop - Unable to fetch target SDN.\n");
        ret = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }

    /* Check if this is a rename operation.
     * If so, we need to get the new DN. */
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
    if (optype == SLAPI_OPERATION_MODDN) {
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
        if (e) {
            new_sdn = slapi_entry_get_sdn(e);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                          "pam_passthru_postop - Unable to fetch post-op "
                          "entry for rename operation.\n");
            ret = SLAPI_PLUGIN_FAILURE;
            goto bail;
        }
    }

    /* Check if the target is a config entry.  If so, reload all of the config.
     * If this is a rename operation, we also need to see if the new DN is in
     * the config scope. */
    if (pam_passthru_dn_is_config(sdn) || (new_sdn && pam_passthru_dn_is_config(new_sdn))) {
        pam_passthru_load_config(1); /* skip validation, as it was done at preop */
    }

    slapi_log_err(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                  "<= pam_passthru_postop\n");

bail:

    return ret;
}

/*
 *
 * Deal with config locking
 *
 */
void
pam_passthru_read_lock()
{
    slapi_rwlock_rdlock(g_pam_config_lock);
}

void
pam_passthru_write_lock()
{
    slapi_rwlock_wrlock(g_pam_config_lock);
}

void
pam_passthru_unlock()
{
    slapi_rwlock_unlock(g_pam_config_lock);
}
