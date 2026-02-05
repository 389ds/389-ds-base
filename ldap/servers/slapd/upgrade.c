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
 * Check if parentid indexes are missing the integerOrderingMatch
 * matching rule.
 *
 * This function logs a warning if we detect this condition, advising
 * the administrator to reindex the affected attributes.
 */
static upgrade_status
upgrade_check_id_index_matching_rule(void)
{
    struct slapi_pblock *pb = slapi_pblock_new();
    Slapi_Entry **backends = NULL;
    const char *be_base_dn = "cn=ldbm database,cn=plugins,cn=config";
    const char *be_filter = "(objectclass=nsBackendInstance)";
    const char *attrs_to_check[] = {"parentid", "ancestorid", NULL};
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
            const char *be_name = slapi_entry_attr_get_ref(backends[be_idx], "cn");
            if (!be_name) {
                continue;
            }

            /* Check each attribute that should have integerOrderingMatch */
            for (size_t attr_idx = 0; attrs_to_check[attr_idx] != NULL; attr_idx++) {
                const char *attr_name = attrs_to_check[attr_idx];
                struct slapi_pblock *idx_pb = slapi_pblock_new();
                Slapi_Entry **idx_entries = NULL;
                char *idx_dn = slapi_create_dn_string("cn=%s,cn=index,cn=%s,%s",
                                                       attr_name, be_name, be_base_dn);
                char *idx_filter = "(objectclass=nsIndex)";
                PRBool has_matching_rule = PR_FALSE;

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
                    /* Index exists, check if it has integerOrderingMatch */
                    Slapi_Attr *mr_attr = NULL;
                    if (slapi_entry_attr_find(idx_entries[0], "nsMatchingRule", &mr_attr) == 0) {
                        Slapi_Value *sval = NULL;
                        int idx;
                        for (idx = slapi_attr_first_value(mr_attr, &sval);
                             idx != -1;
                             idx = slapi_attr_next_value(mr_attr, idx, &sval)) {
                            const struct berval *bval = slapi_value_get_berval(sval);
                            if (bval && bval->bv_val &&
                                strcasecmp(bval->bv_val, "integerOrderingMatch") == 0) {
                                has_matching_rule = PR_TRUE;
                                break;
                            }
                        }
                    }

                    if (!has_matching_rule) {
                        /* Index exists but doesn't have integerOrderingMatch, log a warning */
                        slapi_log_err(SLAPI_LOG_ERR, "upgrade_check_id_index_matching_rule",
                                "Index '%s' in backend '%s' is missing 'nsMatchingRule: integerOrderingMatch'. "
                                "Incorrectly configured system indexes can lead to poor search performance, replication issues, and other operational problems. "
                                "To fix this, add the matching rule and reindex: "
                                "dsconf <instance> backend index set --add-mr integerOrderingMatch --attr %s %s && "
                                "dsconf <instance> backend index reindex --attr %s %s. "
                                "WARNING: Reindexing can be resource-intensive and may impact server performance on a live system. "
                                "Consider scheduling reindexing during maintenance windows or periods of low activity.\n",
                                attr_name, be_name, attr_name, be_name, attr_name, be_name);
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

upgrade_status
upgrade_server(void)
{
    if (upgrade_remove_index_scanlimit() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_check_id_index_matching_rule() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    return UPGRADE_SUCCESS;
}
