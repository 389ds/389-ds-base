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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * pamptpreop.c - bind pre-operation plugin for Pass Through Authentication to PAM
 *
 */

#include "pam_passthru.h"

static Slapi_PluginDesc pdesc = { "pam_passthruauth",  VENDOR, DS_PACKAGE_VERSION,
	"PAM pass through authentication plugin" };

static void *pam_passthruauth_plugin_identity = NULL;
static const Slapi_DN *pam_passthruauth_plugin_sdn = NULL;
static Slapi_RWLock *g_pam_config_lock = NULL;

/*
 * Plug-in globals
 */
int g_pam_plugin_started = 0;
PRCList *pam_passthru_global_config = NULL;

/*
 * function prototypes
 */
static int pam_passthru_bindpreop( Slapi_PBlock *pb );
static int pam_passthru_bindpreop_start( Slapi_PBlock *pb );
static int pam_passthru_bindpreop_close( Slapi_PBlock *pb );
static int pam_passthru_preop(Slapi_PBlock *pb, int modtype);
static int pam_passthru_add_preop(Slapi_PBlock *pb);
static int pam_passthru_mod_preop(Slapi_PBlock *pb);
static int pam_passthru_del_preop(Slapi_PBlock *pb);
static int pam_passthru_modrdn_preop(Slapi_PBlock *pb);
static int pam_passthru_postop(Slapi_PBlock *pb);
static int pam_passthru_internal_postop_init(Slapi_PBlock *pb);
static int pam_passthru_postop_init(Slapi_PBlock *pb);

/*
** Plugin identity mgmt
*/

void pam_passthruauth_set_plugin_identity(void * identity) 
{
    pam_passthruauth_plugin_identity=identity;
}

void * pam_passthruauth_get_plugin_identity()
{
    return pam_passthruauth_plugin_identity;
}

void pam_passthruauth_set_plugin_sdn(const Slapi_DN *plugin_sdn)
{
    pam_passthruauth_plugin_sdn = plugin_sdn;
}

const Slapi_DN *pam_passthruauth_get_plugin_sdn()
{
    return pam_passthruauth_plugin_sdn;
}

const char *pam_passthruauth_get_plugin_dn()
{
    return slapi_sdn_get_ndn(pam_passthruauth_plugin_sdn);
}

/*
 * Plugin initialization function (which must be listed in the appropriate
 * slapd config file).
 */
int
pam_passthruauth_init( Slapi_PBlock *pb )
{
    int status = 0;

    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "=> pam_passthruauth_init\n" );

    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &pam_passthruauth_plugin_identity);
    PR_ASSERT (pam_passthruauth_plugin_identity);

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
            (void *)SLAPI_PLUGIN_VERSION_01 ) != 0
            || slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                    (void *)&pdesc ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
                    (void *)pam_passthru_bindpreop_start ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN,
                    (void *)pam_passthru_bindpreop ) != 0
            || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ADD_FN,
                    (void *)pam_passthru_add_preop ) != 0
            || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODIFY_FN,
                    (void *)pam_passthru_mod_preop ) != 0
            || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_DELETE_FN,
                    (void *)pam_passthru_del_preop ) != 0
            || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODRDN_FN,
                    (void *)pam_passthru_modrdn_preop ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN,
                    (void *)pam_passthru_bindpreop_close ) != 0  ) {
        slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "pam_passthruauth_init failed\n" );
        status = -1;
        goto bail;
    }

    /* Register internal postop functions. */
    if (slapi_register_plugin("internalpostoperation",  /* op type */
                              1,        /* Enabled */
                              "pam_passthruauth_init",   /* this function desc */
                              pam_passthru_internal_postop_init,  /* init func */
                              PAM_PASSTHRU_INT_POSTOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              pam_passthruauth_plugin_identity   /* access control */
    )) {
        slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "pam_passthruauth_init: failed to register plugin\n");
        status = -1;
        goto bail;
    }

    /* Register postop functions */
    if (slapi_register_plugin("postoperation",  /* op type */
                              1,        /* Enabled */
                              "pam_passthruauth_init",   /* this function desc */
                              pam_passthru_postop_init,  /* init func for post op */
                              PAM_PASSTHRU_POSTOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              pam_passthruauth_plugin_identity   /* access control */
    )) {
        slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "pam_passthruauth_init: failed to register plugin\n");
        status = -1;
        goto bail;
    }

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "<= pam_passthruauth_init\n" );

  bail:
    return status;
}

static int
pam_passthru_internal_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *) pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *) pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *) pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *) pam_passthru_postop) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "pam_passthru_internal_postop_init: failed to register plugin\n");
        status = -1;
    }

    return status;
}

static int
pam_passthru_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN,
                         (void *) pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN,
                         (void *) pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN,
                         (void *) pam_passthru_postop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN,
                         (void *) pam_passthru_postop) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "pam_passthru_postop_init: failed to register plugin\n");
        status = -1;
    }

    return status;
}

/*
 * pam_passthru_bindpreop_start() is called before the directory server
 * is fully up.  We parse our configuration and initialize any mutexes, etc.
 */
static int
pam_passthru_bindpreop_start( Slapi_PBlock *pb )
{
    int rc = PAM_PASSTHRU_SUCCESS;
    Slapi_DN *pluginsdn = NULL;
    char *config_area = NULL;

    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "=> pam_passthru_bindpreop_start\n" );

    /* Check if we're already started */
    if (g_pam_plugin_started) {
        goto done;
    }

    /* Get the plug-in configuration DN and store it for later use. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &pluginsdn);
    if (NULL == pluginsdn || 0 == slapi_sdn_get_ndn_len(pluginsdn)) {
        slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "pam_passthru_bindpreop_start: unable to determine plug-in config dn\n");
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

    slapi_log_error(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                    "pam_passthru_bindpreop_start: config at %s\n",
                    slapi_sdn_get_ndn(pam_passthru_get_config_area()));

    /* Create the lock that protects the config . */
    g_pam_config_lock = slapi_new_rwlock();

    if (!g_pam_config_lock) {
        slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "pam_passthru_bindpreop_start: lock creation failed\n");
        rc = PAM_PASSTHRU_FAILURE;
        goto done;
    }

    /* Allocate the config list. */
    pam_passthru_global_config = (PRCList *)
        slapi_ch_calloc(1, sizeof(Pam_PassthruConfig));
    PR_INIT_CLIST(pam_passthru_global_config);

    /* Load config. */
    pam_passthru_load_config(0 /* don't skip validation */);

    if (( rc = pam_passthru_pam_init()) != LDAP_SUCCESS ) {
        slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "could not initialize PAM subsystem (%d)\n", rc);
        rc =  PAM_PASSTHRU_FAILURE;
        goto done;
    }

done:
    if ( rc != PAM_PASSTHRU_SUCCESS ) {
        pam_passthru_delete_config();
        slapi_destroy_rwlock(g_pam_config_lock);
        g_pam_config_lock = NULL;
        slapi_ch_free((void **)&pam_passthru_global_config);
    } else {
        g_pam_plugin_started = 1;
        slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "pam_passthru: ready for service\n" );
    }

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "<= pam_passthru_bindpreop_start\n" );

    return( rc );
}


/*
 * Called right before the Directory Server shuts down.
 */
static int
pam_passthru_bindpreop_close( Slapi_PBlock *pb )
{
    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> pam_passthru_bindpreop_close\n" );

    if (!g_pam_plugin_started) {
        goto done;
    }

    pam_passthru_write_lock();
    g_pam_plugin_started = 0;
    pam_passthru_delete_config();
    pam_passthru_unlock();

    slapi_ch_free((void **)&pam_passthru_global_config);

    /* We explicitly don't destroy the config lock here.  If we did,
     * there is the slight possibility that another thread that just
     * passed the g_pam_plugin_started check is about to try to obtain
     * a reader lock.  We leave the lock around so these threads
     * don't crash the process.  If we always check the started
     * flag again after obtaining a reader lock, no free'd resources
     * will be used. */

done:
    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "<= pam_passthru_bindpreop_close\n" );

    return( 0 );
}


static int
pam_passthru_bindpreop( Slapi_PBlock *pb )
{
    int rc = LDAP_SUCCESS;
    int method;
    const char *normbinddn;
    char *errmsg = NULL;
    Slapi_DN *bindsdn = NULL;
    Pam_PassthruConfig	*cfg;
    struct berval	*creds;
    int retcode = PAM_PASSTHRU_OP_NOT_HANDLED;

    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "=> pam_passthru_bindpreop\n" );

    /*
     * retrieve parameters for bind operation
     */
    if ( slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) != 0 ||
            slapi_pblock_get( pb, SLAPI_BIND_TARGET_SDN, &bindsdn ) != 0 ||
            slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &creds ) != 0 ) {
        slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "<= not handled (unable to retrieve bind parameters)\n" );
        return retcode;
    }
    normbinddn = slapi_sdn_get_dn(bindsdn);

    /*
     * We only handle simple bind requests that include non-NULL binddn and
     * credentials.  Let the Directory Server itself handle everything else.
     */
    if ( method != LDAP_AUTH_SIMPLE || *normbinddn == '\0' ||
            creds->bv_len == 0 ) {
        slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "<= not handled (not simple bind or NULL dn/credentials)\n" );
        return retcode;
    }

    /* Get the config lock.  From this point on, we must go to done
     * to be sure we unlock. */
    pam_passthru_read_lock();

    /* Bail out if the plug-in close function was just called. */
    if (!g_pam_plugin_started) {
        goto done;
    }

    /* See if any of our config entries apply to this user */
    cfg = pam_passthru_get_config(bindsdn);

    if (!cfg) {
        slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                         "<= \"%s\" not handled (doesn't meet configuration criteria)\n", normbinddn );
        goto done;
    }

    if (cfg->pamptconfig_secure) { /* is a secure connection required? */
        int is_ssl = 0;
        slapi_pblock_get(pb, SLAPI_CONN_IS_SSL_SESSION, &is_ssl);
        if (!is_ssl) {
            slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                             "<= connection not secure (secure connection required; check config)");
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
            slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                            "%s\n", errmsg);
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

    slapi_log_error(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                    "<= handled (error %d - %s)\n", rc, ldap_err2string(rc));

    slapi_log_error( SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                     "<= pam_passthru_bindpreop\n" );

    return retcode;
}


/*
 * Pre-op callbacks for config change validation
 */
static int
pam_passthru_preop(Slapi_PBlock *pb, int modtype)
{
    Slapi_DN *sdn = NULL;
    Slapi_Entry *e = NULL;
    LDAPMod **mods;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE];
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                    "=> pam_passthru_preop\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_pam_plugin_started) {
        goto bail;
    }

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
            slapi_search_internal_get_entry(sdn, 0, &e,
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

            /* Free the entry. */
            slapi_entry_free(e);
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
    /* If we are refusing the operation, return the result to the client. */
    if (ret) {
        slapi_send_ldap_result(pb, ret, NULL, returntext, 0, NULL);
        ret = -1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
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
    int ret = 0;
    Slapi_DN *sdn = NULL;
    Slapi_DN *new_sdn = NULL;
    Slapi_Entry *e = NULL;
    int optype = SLAPI_OPERATION_NONE;
    int oprc = -1;

    slapi_log_error(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                    "=> pam_passthru_postop\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_pam_plugin_started) {
        goto bail;
    }

    /* Make sure the operation succeeded and bail if it didn't. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc);
    if (oprc != 0) {
        goto bail;
    }

    /* Get the target SDN. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    if (!sdn) {
        slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                        "pam_passthru_postop: unale to fetch target SDN.\n");
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
            slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                            "pam_passthru_postop: unable to fetch post-op "
                            "entry for rename operation.\n");
            goto bail;
        }
    }

    /* Check if the target is a config entry.  If so, reload all of the config.
     * If this is a rename operation, we also need to see if the new DN is in
     * the config scope. */
    if (pam_passthru_dn_is_config(sdn) || (new_sdn && pam_passthru_dn_is_config(new_sdn))) {
        pam_passthru_load_config(1); /* skip validation, as it was done at preop */
    }

    slapi_log_error(SLAPI_LOG_TRACE, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
                    "<= pam_passthru_postop\n");

  bail:
    return ret;
}

/*
 *
 * Deal with config locking
 *
 */
void pam_passthru_read_lock()
{
    slapi_rwlock_rdlock(g_pam_config_lock);
}

void pam_passthru_write_lock()
{
    slapi_rwlock_wrlock(g_pam_config_lock);
}

void pam_passthru_unlock()
{
    slapi_rwlock_unlock(g_pam_config_lock);
}
