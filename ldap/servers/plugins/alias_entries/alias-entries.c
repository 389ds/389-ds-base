/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 anilech
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "alias-entries.h"

static Slapi_ComponentId *plugin_id = NULL;
Slapi_PluginDesc srchpdesc = { PLUGINNAME, PLUGINVNDR, PLUGINVERS, PLUGINDESC };

static Slapi_DN*
alias_get_next_dn(Slapi_DN *sdn, int *rc)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry **e;
    Slapi_DN *alias_dn = NULL;
    int derefNever = LDAP_DEREF_NEVER;
    char *attrs[] = { "aliasedObjectName", NULL };

    /* search dn to check if it is an alias */
    pb = slapi_pblock_new();
    slapi_search_internal_set_pb_ext(pb, sdn, LDAP_SCOPE_BASE, ALIASFILTER, attrs, 0, NULL, NULL, plugin_id, 0);
    slapi_pblock_set(pb, SLAPI_SEARCH_DEREF, (void *)&derefNever);

    if (slapi_search_internal_pb(pb) == 0) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, rc);
        if (*rc == LDAP_SUCCESS) {
            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &e);
            if (e && e[0]) {
                alias_dn = slapi_sdn_new_dn_byval(slapi_entry_attr_get_ref(e[0], attrs[0]));
            }
        }
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return alias_dn;
}

int
alias_entry_init(Slapi_PBlock *pb)
{
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_03) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&srchpdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_SEARCH_FN, (void *)alias_entry_srch) != 0  ||
        slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_id) != 0)
    {
        slapi_log_error(SLAPI_LOG_ERR, PLUGINNAME,
                        "alias_entry_init: Error registering the plug-in.\n");
        return -1;
    }

    return 0;
}

int
alias_entry_srch(Slapi_PBlock *pb)
{
    Slapi_DN *search_target = NULL;
    Slapi_DN *dn1 = NULL;
    Slapi_DN *dn2 = NULL;
    char *str_filter = NULL;
    int scope = 0;
    int deref_enabled = 0;
    size_t i = 0;
    int rc = 0;

    /* Skip reserved operations */
    if (slapi_op_reserved(pb)) {
        return 0;
    }

    /* Base dn should be provided */
    if (slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &search_target) != 0 || search_target == NULL) {
        return 0;
    }

    /* deref must be enabled */
    if (slapi_pblock_get(pb, SLAPI_SEARCH_DEREF, &deref_enabled) != 0) {
        return 0;
    }
    if (deref_enabled != LDAP_DEREF_FINDING && deref_enabled != LDAP_DEREF_ALWAYS) {
        return 0;
    }

    /* Only base search is supported */
    if (slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope) != 0 || scope != LDAP_SCOPE_BASE) {
        char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
        slapi_create_errormsg(errorbuf, sizeof(errorbuf),
                "Only base level scoped searches are allowed to dereference alias entries");
        rc = LDAP_UNWILLING_TO_PERFORM;
        slapi_send_ldap_result(pb, rc, NULL, errorbuf, 0, NULL);
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        return SLAPI_PLUGIN_FAILURE;
    }

    /* Skip our own requests */
    if (slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &str_filter) != 0 ||
        str_filter == NULL || strcmp(str_filter, ALIASFILTER) == 0)
    {
        return 0;
    }

    /* Follow the alias chain */
    dn2 = search_target;
    do {
        dn1 = dn2;
        dn2 = alias_get_next_dn(dn1, &rc);
        if (i > 0 && dn2 != NULL) {
            slapi_sdn_free(&dn1);
        }
        if (rc != LDAP_SUCCESS) {
            char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
            slapi_create_errormsg(errorbuf, sizeof(errorbuf),
                    "Failed to dereference alias object name (%s) error %d",
                    slapi_sdn_get_dn(dn1), rc);
            slapi_log_error(SLAPI_LOG_PLUGIN, PLUGINNAME,
                            "alias_entry_srch - %s\n", errorbuf);

            slapi_send_ldap_result(pb, rc, NULL, errorbuf, 0, NULL);
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
            return SLAPI_PLUGIN_FAILURE;
        }
    } while (dn2 != NULL && i++ < MAXALIASCHAIN);

    if (dn1 == search_target) {
        /* Source dn is not an alias */
        return 0;
    }

    if (dn2 == NULL) {
        /* alias resolved, set new base */
        slapi_sdn_free(&search_target);
        slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, dn1);
    } else {
        /* Here we hit an alias chain longer than MAXALIASCHAIN */
        slapi_sdn_free(&dn2);
    }

    return 0;
}
