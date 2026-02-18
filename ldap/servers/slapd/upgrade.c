/* BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * Copyright (C) 2020 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK */

#include <slap.h>
#include <slapi-private.h>

/*
 * This is called on server startup *before* plugins start
 * but after config dse is read for operations. This allows
 * us to make internal assertions about the state of the configuration
 * at start up, enable plugins, and more.
 *
 * The functions in this file are named as:
 * upgrade_xxx_yyy, where xxx is the minimum version of the project
 * and yyy is the feature that is having it's configuration upgrade
 * or altered.
 */

/*
 * Remove nsIndexIDListScanLimit from parentid index configuration.
 *
 * This attribute was incorrectly added by a previous version and can
 * cause issues with index configuration. Remove it if present.
 */
static upgrade_status
upgrade_remove_index_scanlimit(void)
{
    struct slapi_pblock *pb = slapi_pblock_new();
    Slapi_Entry **backends = NULL;
    const char *be_base_dn = "cn=ldbm database,cn=plugins,cn=config";
    const char *be_filter = "(objectclass=nsBackendInstance)";
    const char *attrs_to_check[] = {"parentid", NULL};
    upgrade_status uresult = UPGRADE_SUCCESS;

    /* Search for all backend instances */
    slapi_search_internal_set_pb(
            pb, be_base_dn,
            LDAP_SCOPE_ONELEVEL,
            be_filter, NULL, 0, NULL, NULL,
            plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &backends);

    if (backends) {
        for (size_t be_idx = 0; backends[be_idx] != NULL; be_idx++) {
            const char *be_dn = slapi_entry_get_dn_const(backends[be_idx]);
            const char *be_name = slapi_entry_attr_get_ref(backends[be_idx], "cn");
            if (!be_dn || !be_name) {
                continue;
            }

            for (size_t attr_idx = 0; attrs_to_check[attr_idx] != NULL; attr_idx++) {
                const char *attr_name = attrs_to_check[attr_idx];
                struct slapi_pblock *idx_pb = slapi_pblock_new();
                Slapi_Entry **idx_entries = NULL;
                char *idx_dn = slapi_create_dn_string("cn=%s,cn=index,%s",
                                                       attr_name, be_dn);
                char *idx_filter = "(objectclass=nsIndex)";

                if (!idx_dn) {
                    slapi_pblock_destroy(idx_pb);
                    continue;
                }

                slapi_search_internal_set_pb(
                        idx_pb, idx_dn,
                        LDAP_SCOPE_BASE,
                        idx_filter, NULL, 0, NULL, NULL,
                        plugin_get_default_component_id(), 0);
                slapi_search_internal_pb(idx_pb);
                slapi_pblock_get(idx_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &idx_entries);

                if (idx_entries && idx_entries[0]) {
                    /* Check if nsIndexIDListScanLimit is present */
                    if (slapi_entry_attr_get_ref(idx_entries[0], "nsIndexIDListScanLimit") != NULL) {
                        /* Remove nsIndexIDListScanLimit */
                        Slapi_PBlock *mod_pb = slapi_pblock_new();
                        Slapi_Mods smods;
                        int rc;

                        slapi_mods_init(&smods, 1);
                        slapi_mods_add(&smods, LDAP_MOD_DELETE, "nsIndexIDListScanLimit", 0, NULL);

                        slapi_modify_internal_set_pb(
                                mod_pb, idx_dn,
                                slapi_mods_get_ldapmods_byref(&smods),
                                NULL, NULL,
                                plugin_get_default_component_id(), 0);
                        slapi_modify_internal_pb(mod_pb);
                        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

                        if (rc == LDAP_SUCCESS) {
                            slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_remove_index_scanlimit",
                                    "Removed 'nsIndexIDListScanLimit' from index '%s' in backend '%s'\n",
                                    attr_name, be_name);
                        } else if (rc != LDAP_NO_SUCH_ATTRIBUTE) {
                            slapi_log_err(SLAPI_LOG_ERR, "upgrade_remove_index_scanlimit",
                                    "Failed to remove 'nsIndexIDListScanLimit' from index '%s' in backend '%s': error %d\n",
                                    attr_name, be_name, rc);
                        }

                        slapi_mods_done(&smods);
                        slapi_pblock_destroy(mod_pb);
                    }
                }

                slapi_ch_free_string(&idx_dn);
                slapi_free_search_results_internal(idx_pb);
                slapi_pblock_destroy(idx_pb);
            }
        }
    }

    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return uresult;
}

/*
 * Remove ancestorid index configuration entry if present.
 *
 * The ancestorid index is special - it has no corresponding attribute type
 * and should not have a DSE config entry. If an entry exists, remove it.
 *
 * This function removes:
 * 1. The ancestorid entry from cn=default indexes (to prevent re-creation on startup)
 * 2. The ancestorid entry from each backend's cn=index (if it exists)
 */
static upgrade_status
upgrade_remove_ancestorid_index_config(void)
{
    struct slapi_pblock *pb = slapi_pblock_new();
    Slapi_Entry **backends = NULL;
    const char *be_base_dn = "cn=ldbm database,cn=plugins,cn=config";
    const char *be_filter = "(objectclass=nsBackendInstance)";
    upgrade_status uresult = UPGRADE_SUCCESS;
    int rc;

    /*
     * First, remove ancestorid from cn=default indexes to prevent
     * ldbm_instance_create_default_user_indexes() from re-creating it.
     */
    {
        Slapi_PBlock *def_pb = slapi_pblock_new();
        char *def_idx_dn = slapi_create_dn_string(
                "cn=ancestorid,cn=default indexes,cn=config,%s", be_base_dn);

        if (def_idx_dn) {
            slapi_delete_internal_set_pb(
                    def_pb, def_idx_dn, NULL, NULL,
                    plugin_get_default_component_id(), 0);
            slapi_delete_internal_pb(def_pb);
            slapi_pblock_get(def_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

            if (rc == LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_remove_ancestorid_index_config",
                        "Removed 'ancestorid' from default indexes.\n");
            } else if (rc != LDAP_NO_SUCH_OBJECT) {
                slapi_log_err(SLAPI_LOG_ERR, "upgrade_remove_ancestorid_index_config",
                        "Failed to remove 'ancestorid' from default indexes: error %d\n", rc);
            }

            slapi_ch_free_string(&def_idx_dn);
        }
        slapi_pblock_destroy(def_pb);
    }

    /* Search for all backend instances */
    slapi_search_internal_set_pb(
            pb, be_base_dn,
            LDAP_SCOPE_ONELEVEL,
            be_filter, NULL, 0, NULL, NULL,
            plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &backends);

    if (backends) {
        for (size_t be_idx = 0; backends[be_idx] != NULL; be_idx++) {
            const char *be_dn = slapi_entry_get_dn_const(backends[be_idx]);
            const char *be_name = slapi_entry_attr_get_ref(backends[be_idx], "cn");
            if (!be_dn || !be_name) {
                continue;
            }

            struct slapi_pblock *idx_pb = slapi_pblock_new();
            Slapi_Entry **idx_entries = NULL;
            char *idx_dn = slapi_create_dn_string("cn=ancestorid,cn=index,%s",
                                                   be_dn);
            char *idx_filter = "(objectclass=nsIndex)";

            if (!idx_dn) {
                slapi_pblock_destroy(idx_pb);
                continue;
            }

            slapi_search_internal_set_pb(
                    idx_pb, idx_dn,
                    LDAP_SCOPE_BASE,
                    idx_filter, NULL, 0, NULL, NULL,
                    plugin_get_default_component_id(), 0);
            slapi_search_internal_pb(idx_pb);
            slapi_pblock_get(idx_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &idx_entries);

            if (idx_entries && idx_entries[0]) {
                /* ancestorid index entry exists - delete it */
                Slapi_PBlock *del_pb = slapi_pblock_new();

                slapi_delete_internal_set_pb(
                        del_pb, idx_dn, NULL, NULL,
                        plugin_get_default_component_id(), 0);
                slapi_delete_internal_pb(del_pb);
                slapi_pblock_get(del_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

                if (rc == LDAP_SUCCESS) {
                    slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_remove_ancestorid_index_config",
                            "Removed 'ancestorid' index config entry in backend '%s'.\n",
                            be_name);
                } else if (rc != LDAP_NO_SUCH_OBJECT) {
                    slapi_log_err(SLAPI_LOG_ERR, "upgrade_remove_ancestorid_index_config",
                            "Failed to remove 'ancestorid' index config entry in backend '%s': error %d\n",
                            be_name, rc);
                }

                slapi_pblock_destroy(del_pb);
            }

            slapi_ch_free_string(&idx_dn);
            slapi_free_search_results_internal(idx_pb);
            slapi_pblock_destroy(idx_pb);
        }
    }

    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return uresult;
}

upgrade_status
upgrade_server(void)
{
    if (upgrade_remove_index_scanlimit() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_remove_ancestorid_index_config() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    return UPGRADE_SUCCESS;
}
