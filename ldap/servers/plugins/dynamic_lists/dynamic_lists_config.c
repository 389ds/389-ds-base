/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

 #ifdef HAVE_CONFIG_H
 #include <config.h>
 #endif

 /*
  * Dynamic Lists plug-in
  */
 #include "dynamic_lists.h"

/*
 * dynamic_lists_validate_config()
 *
 * Validates the configuration of the dynamic lists plugin.
 */
static int
dynamic_lists_validate_config(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e,
                              int *returncode, char *returntext, void *arg)
{
    const char *setting = NULL;
    char *value = NULL;

    setting = slapi_entry_attr_get_ref(e, DYNAMIC_LIST_CONFIG_OC);
    if (setting) {
        /* Check if this is a real objectclass */
        if ((value = slapi_schema_get_superior_name(setting)) == NULL) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must be set "
                        "to an existing objectclass (unknown: %s)",
                        DYNAMIC_LIST_CONFIG_OC, setting);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        } else {
            slapi_ch_free_string(&value);
        }
    }

    setting = slapi_entry_attr_get_ref(e, DYNAMIC_LIST_CONFIG_URL_ATTR);
    if (setting) {
        /* Check if this is a real attribute */
        if (!slapi_attr_syntax_exists(setting)) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must be set "
                        "to an existing attribute (unknown: %s)",
                        DYNAMIC_LIST_CONFIG_URL_ATTR, setting);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }

        /* Now make sure we are not using this attribute for the list attr */
        const char *other_setting = slapi_entry_attr_get_ref(e, DYNAMIC_LIST_CONFIG_LIST_ATTR);
        if (other_setting && strcasecmp(other_setting, setting) == 0) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must not be set "
                        "to the same attribute as the %s configuration attribute (same: %s)",
                        DYNAMIC_LIST_CONFIG_URL_ATTR, DYNAMIC_LIST_CONFIG_LIST_ATTR, other_setting);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
    }

    setting = slapi_entry_attr_get_ref(e, DYNAMIC_LIST_CONFIG_LIST_ATTR);
    if (setting) {
        /* Check if this is a real attribute with DN syntax */
        if (!slapi_attr_syntax_exists(setting)) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must be set "
                        "to an existing attribute with DN syntax (unknown %s)",
                        DYNAMIC_LIST_CONFIG_LIST_ATTR, setting);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }

        /* Now check if it has DN syntax */
        if (!slapi_attr_is_dn_syntax_type((char *)setting)) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must be set "
                        "to an attribute with DN syntax (incorrect syntax: %s)",
                        DYNAMIC_LIST_CONFIG_LIST_ATTR, setting);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }

        /* Now make sure we are not using this attribute for the URL attr */
        const char *other_setting = slapi_entry_attr_get_ref(e, DYNAMIC_LIST_CONFIG_URL_ATTR);
        if (other_setting && strcasecmp(other_setting, setting) == 0) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "The %s configuration attribute must not be set "
                        "to the same attribute as the %s configuration attribute (same: %s)",
                        DYNAMIC_LIST_CONFIG_LIST_ATTR, DYNAMIC_LIST_CONFIG_URL_ATTR, other_setting);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
    }

    return SLAPI_DSE_CALLBACK_OK;
}

static void
dynamic_lists_set_config(DynamicListsConfig *config, Slapi_Entry *config_entry)
{
    /* Future proof memory leaks for dynamic plugins */
    slapi_ch_free_string(&config->oc);
    slapi_ch_free_string(&config->URLAttr);
    slapi_ch_free_string(&config->attr);

    config->oc = slapi_entry_attr_get_charptr(config_entry, DYNAMIC_LIST_CONFIG_OC);
    if (config->oc == NULL) {
        config->oc = slapi_ch_strdup(DYNAMIC_LISTS_OBJECTCLASS);
    }

    config->URLAttr = slapi_entry_attr_get_charptr(config_entry, DYNAMIC_LIST_CONFIG_URL_ATTR);
    if (config->URLAttr == NULL) {
        config->URLAttr = slapi_ch_strdup(DYNAMIC_LISTS_URL_ATTR);
    }

    config->attr = slapi_entry_attr_get_charptr(config_entry, DYNAMIC_LIST_CONFIG_LIST_ATTR);
    if (config->attr == NULL) {
        config->attr = slapi_ch_strdup(DYNAMIC_LISTS_LIST_ATTR);
    }
}

int
dynamic_lists_load_config(Slapi_PBlock *pb, DynamicListsConfig *config)
{
    int rc = 0;
    int result;
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry *config_e = NULL;
    const char *config_area = NULL;
    const char *config_dn = NULL;

    /* Set the alternate config area if one is defined. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_AREA, &config_area);
    if (config_area) {
        search_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(search_pb, config_area, LDAP_SCOPE_BASE, "objectclass=*",
                                     NULL, 0, NULL, NULL, dynamic_lists_get_plugin_id(), 0);
        slapi_search_internal_pb(search_pb);
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
        if (LDAP_SUCCESS != result) {
            if (result == LDAP_NO_SUCH_OBJECT) {
                /* log an error and use the plugin entry for the config */
                slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                              "dynamic_lists_load_config - Config entry \"%s\" does "
                              "not exist.\n",
                              config_area);
                rc = -1;
                goto bail;
            }
        } else {
            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            if (entries && entries[0]) {
                config_e = entries[0];
            } else {
                slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                              "dynamic_lists_load_config - Config entry \"%s\" was "
                              "not located.\n",
                              config_area);
                rc = -1;
                goto bail;
            }
        }
    } else {
        /* The plugin entry itself contains the config */
        if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &config_e) != 0) {
            rc = -1;
            goto bail;
        }
    }

    /* Register callbacks to validate config changes*/
    config_dn = slapi_entry_get_dn_const(config_e);
    slapi_config_register_callback_plugin(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN,
                                          config_dn, LDAP_SCOPE_BASE, "(objectclass=*)",
                                          dynamic_lists_validate_config, NULL, pb);
    dynamic_lists_set_config(config, config_e);

bail:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);

    return rc;
}
