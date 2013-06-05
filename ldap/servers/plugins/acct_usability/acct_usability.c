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
 * Copyright (C) 2011 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Account Usability Control plug-in
 */
#include <string.h>
#include "acct_usability.h"
#include <nspr.h>
#include <time.h>


/*
 * Plug-in globals
 */
static void *_PluginID = NULL;
static char *_PluginDN = NULL;
static int g_plugin_started = 0;

static Slapi_PluginDesc pdesc = { AUC_FEATURE_DESC,
                                  VENDOR,
                                  DS_PACKAGE_VERSION,
                                  AUC_PLUGIN_DESC };

/*
 * Plug-in management functions
 */
int auc_init(Slapi_PBlock * pb);
static int auc_start(Slapi_PBlock * pb);
static int auc_close(Slapi_PBlock * pb);

/*
 * Operation callbacks (where the real work is done)
 */
static int auc_pre_search(Slapi_PBlock * pb);
static int auc_pre_entry(Slapi_PBlock *pb);

/*
 * Plugin identity functions
 */
void
auc_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
auc_get_plugin_id()
{
    return _PluginID;
}

void
auc_set_plugin_dn(char *pluginDN)
{
    _PluginDN = pluginDN;
}

char *
auc_get_plugin_dn()
{
    return _PluginDN;
}

/*
 * Plug-in initialization functions
 */
int
auc_init(Slapi_PBlock *pb)
{
    int status = 0;
    char *plugin_identity = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "--> auc_init\n");

    /* Store the plugin identity for later use.
     * Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    auc_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *) auc_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *) auc_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_SEARCH_FN,
                         (void *) auc_pre_search) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ENTRY_FN,
                         (void *) auc_pre_entry) != 0
        ) {
        slapi_log_error(SLAPI_LOG_FATAL, AUC_PLUGIN_SUBSYSTEM,
                        "auc_init: failed to register plugin\n");
        status = -1;
    }

    if (status == 0) {
        slapi_register_supported_control(AUC_OID, SLAPI_OPERATION_SEARCH);
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "<-- auc_init\n");
    return status;
}


/*
 * auc_start()
 */
static int
auc_start(Slapi_PBlock * pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "--> auc_start\n");

    /* Check if we're already started */
    if (g_plugin_started) {
        goto done;
    }

    g_plugin_started = 1;
    slapi_log_error(SLAPI_LOG_PLUGIN, AUC_PLUGIN_SUBSYSTEM,
                    "account usability control plug-in: ready for service\n");
    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "<-- auc_start\n");

done:
    return 0;
}

/*
 * auc_close()
 */
static int
auc_close(Slapi_PBlock * pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "--> auc_close\n");

    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "<-- auc_close\n");

    return 0;
}


/*
 * Helper Functions
 */

/*
 * auc_incompatible_ctrl()
 *
 * Check if control oid is incompatible with the account
 * usability control.
 */
static int
auc_incompatible_ctrl(const char *oid)
{
    return 0; /* no known incompatible ctrls yet */
}

/*
 * auc_create_response_ctrl()
 *
 * Generates the response control for the passed in DN.
 *
 *     ACCOUNT_USABLE_RESPONSE ::= CHOICE {
 *       is_available           [0] INTEGER, -- Seconds before expiration --
 *       is_not_available       [1] MORE_INFO }
 *
 *     MORE_INFO ::= SEQUENCE {
 *       inactive               [0] BOOLEAN DEFAULT FALSE,
 *       reset                  [1] BOOLEAN DEFAULT FALSE,
 *       expired                [2] BOOLEAN DEFAULT_FALSE,
 *       remaining_grace        [3] INTEGER OPTIONAL,
 *       seconds_before_unlock  [4] INTEGER OPTIONAL } 
 */
static LDAPControl *auc_create_response_ctrl(Slapi_Entry *e)
{
    BerElement *ctrlber = NULL;
    LDAPControl *ctrl = NULL;
    int is_available = 0;
    int seconds_before_expiration = 0;
    int inactive = 0;
    int reset = 0;
    int expired = 0;
    int remaining_grace = 0;
    int seconds_before_unlock = 0;
    Slapi_PWPolicy *pwpolicy = NULL;
    time_t expire_time = (time_t)0;
    time_t unlock_time = (time_t)0;
    time_t now = slapi_current_time();

    if (!e) {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUC_PLUGIN_SUBSYSTEM,
                        "auc_create_response_ctrl: NULL entry specified.\n");
        goto bail;
    }

    /* Fetch password policy info */
    pwpolicy = slapi_get_pwpolicy(slapi_entry_get_sdn(e));
    if (pwpolicy) {
        expired = slapi_pwpolicy_is_expired(pwpolicy, e, &expire_time, &remaining_grace);
        inactive = slapi_pwpolicy_is_locked(pwpolicy, e, &unlock_time);
        reset = slapi_pwpolicy_is_reset(pwpolicy, e);

        slapi_pwpolicy_free(pwpolicy);
    }

    /* Calculate the seconds before expiration or unlock if needed. */
    if (!expired && !inactive && !reset) {
        is_available = 1;
        if (expire_time > 0) {
            seconds_before_expiration = expire_time - now;
        }
    } else if (inactive && unlock_time) {
        if (unlock_time > 0) {
            seconds_before_unlock = unlock_time - now;
        }
    }

    /* Create the control value */
    ctrlber = ber_alloc();

    if (is_available) {
        ber_printf(ctrlber, "ti", AUC_TAG_AVAILABLE, seconds_before_expiration);
    } else {
        /* Fill in reason account is not available */
        ber_printf(ctrlber, "t{", AUC_TAG_NOT_AVAILABLE);
        ber_printf(ctrlber, "tb", AUC_TAG_INACTIVE, inactive);
        ber_printf(ctrlber, "tb", AUC_TAG_RESET, reset); 
        ber_printf(ctrlber, "tb", AUC_TAG_EXPIRED, expired);

        if (expired) {
            ber_printf(ctrlber, "ti", AUC_TAG_GRACE, remaining_grace);
        }

        if (inactive) {
            ber_printf(ctrlber, "ti", AUC_TAG_UNLOCK, seconds_before_unlock);
        }

        ber_printf(ctrlber, "}");
    }

    slapi_build_control(AUC_OID, ctrlber, 0, &ctrl);
    ber_free(ctrlber, 1);

bail:
    return ctrl;
}

/*
 * Operation callback functions
 */

/*
 * auc_pre_search()
 *
 * See if the account usability control has been specified.
 * If so, parse it, and check to make sure it meets the
 * protocol specification (no duplicate attributes, etc.).
 * We also check to see if the requestor is allowed to use
 * the control.
 */
static int
auc_pre_search(Slapi_PBlock *pb)
{
    int ldapcode = LDAP_SUCCESS;
    const LDAPControl **reqctrls = NULL;
    const LDAPControl *aucctrl = NULL;
    const char *ldaperrtext = "Unknown error";
    const char *incompatible = NULL;
    int isroot = 0;
    int ii;

    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "--> auc_pre_search\n");

    /* See if the requestor is the root DN. */
    slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );

    /* see if the auc request control is in the list of 
       controls - if so, validate it */
    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &reqctrls);
    for (ii = 0; (ldapcode == LDAP_SUCCESS) && reqctrls && reqctrls[ii]; ++ii) {
        const LDAPControl *ctrl = reqctrls[ii];
        if (!strcmp(ctrl->ldctl_oid, AUC_OID)) {
            if (aucctrl) { /* already specified */
                slapi_log_error(SLAPI_LOG_FATAL, AUC_PLUGIN_SUBSYSTEM,
                                "The account usability control was specified more than "
                                "once - it must be specified only once in the search request\n");
                ldapcode = LDAP_PROTOCOL_ERROR;
                ldaperrtext = "The account usability control cannot be specified more than once";
                aucctrl = NULL;
            } else if (ctrl->ldctl_value.bv_len > 0) {
                slapi_log_error(SLAPI_LOG_FATAL, AUC_PLUGIN_SUBSYSTEM,
                                "Non-null control value specified for account usability control\n");
                ldapcode = LDAP_PROTOCOL_ERROR;
                ldaperrtext = "The account usability control must not have a value";
            } else {
                aucctrl = ctrl;
            }
        } else if (auc_incompatible_ctrl(ctrl->ldctl_oid)) {
            incompatible = ctrl->ldctl_oid;
        }
    }

    if (aucctrl && incompatible) {
        slapi_log_error(SLAPI_LOG_FATAL, AUC_PLUGIN_SUBSYSTEM,
                        "Cannot use the account usability control and control [%s] for the same search operation\n",
                        incompatible);
        /* not sure if this is a hard failure - the current spec says:
           The semantics of the criticality field are specified in [RFC4511].
           In detail, the criticality of the control determines whether the
           control will or will not be used, and if it will not be used, whether
           the operation will continue without returning the control in the
           response, or fail, returning unavailableCriticalExtension.  If the
           control is appropriate for an operation and, for any reason, it
           cannot be applied in its entirety to a single SearchResultEntry
           response, it MUST NOT be applied to that specific SearchResultEntry
           response, without affecting its application to any subsequent
           SearchResultEntry response.
        */
        /* so for now, just return LDAP_SUCCESS and don't do anything else */
        aucctrl = NULL;
    }

    /* Check access control if all the above parsing went OK.
     * Skip this for the root DN. */
    if (aucctrl && (ldapcode == LDAP_SUCCESS) && !isroot) {
        char dn[128];
        Slapi_Entry *feature = NULL;

        /* Fetch the feature entry and see if the requestor is allowed access. */
        PR_snprintf(dn, sizeof(dn), "dn: oid=%s,cn=features,cn=config", AUC_OID);
        if ((feature = slapi_str2entry(dn,0)) != NULL) {
            char *dummy_attr = "1.1";

            ldapcode = slapi_access_allowed(pb, feature, dummy_attr, NULL, SLAPI_ACL_READ);
        }

        /* If the feature entry does not exist, deny use of the control.  Only
         * the root DN will be allowed to use the control in this case. */
        if ((feature == NULL) || (ldapcode != LDAP_SUCCESS)) {
            ldapcode = LDAP_INSUFFICIENT_ACCESS;
            ldaperrtext = "Insufficient access rights to use the account usability request control";
        }

        slapi_entry_free(feature);
    }

    if (ldapcode != LDAP_SUCCESS) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &ldapcode);
        slapi_send_ldap_result(pb, ldapcode, NULL, (char *)ldaperrtext, 0, NULL);
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUC_PLUGIN_SUBSYSTEM,
                    "<-- auc_pre_op\n");

    return ldapcode;
}

static int
auc_pre_entry(Slapi_PBlock *pb)
{
    int ii = 0;
    int need_response = 0;
    LDAPControl *ctrl = NULL;
    const LDAPControl **reqctrls = NULL;
    const LDAPControl **searchctrls = NULL;
    LDAPControl **newsearchctrls = NULL;

    /* See if the account usability request control was specified. */
    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &reqctrls);
    for (ii = 0; reqctrls && reqctrls[ii]; ++ii) {
        if (!strcmp(reqctrls[ii]->ldctl_oid, AUC_OID)) {
            need_response = 1;
            break;
        }
    }

    /* Generate the response control if requested. */
    if (need_response) {
        Slapi_Entry *e = NULL;

        /* grab the entry to be returned */
        slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_ORIG, &e);
        if (!e) {
            slapi_log_error(SLAPI_LOG_FATAL, AUC_PLUGIN_SUBSYSTEM,
                            "auc_pre_entry: Unable to fetch entry.\n");
            goto bail;
        }

        /* create the respose control */
        ctrl = auc_create_response_ctrl(e);
        if (!ctrl) {
            slapi_log_error(SLAPI_LOG_FATAL, AUC_PLUGIN_SUBSYSTEM,
                "auc_pre_entry: Error creating response control for entry \"%s\".\n",
                slapi_entry_get_ndn(e) ? slapi_entry_get_ndn(e) : "null");
            goto bail;
        }

        /* get the list of controls */
        slapi_pblock_get(pb, SLAPI_SEARCH_CTRLS, &searchctrls);

        /* dup them */
        slapi_add_controls(&newsearchctrls, (LDAPControl **)searchctrls, 1);

        /* add our control */
        slapi_add_control_ext(&newsearchctrls, ctrl, 0);
        ctrl = NULL; /* newsearchctrls owns it now */

        /* set the controls in the pblock */
        slapi_pblock_set(pb, SLAPI_SEARCH_CTRLS, newsearchctrls);
    }

bail:
    return 0;
}

