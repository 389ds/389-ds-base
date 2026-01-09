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

static char *modifier_name = "cn=upgrade internal,cn=config";

static upgrade_status
upgrade_entry_exists_or_create(char *upgrade_id, char *filter, char *dn, char *entry) {
    upgrade_status uresult = UPGRADE_SUCCESS;
    char *dupentry = strdup(entry);

    Slapi_DN *base_sdn = slapi_sdn_new_from_char_dn(dn);
    /* If not, create it. */
    int result = slapi_exists_or_add_internal(base_sdn, filter, dupentry, modifier_name);

    if (result != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, upgrade_id, "Failed to create entry: %"PRId32"\n", result);
        uresult = UPGRADE_FAILURE;
    }
    slapi_ch_free_string(&dupentry);
    slapi_sdn_free(&base_sdn);
    return uresult;
}

/*
 * Add the new replication bootstrap bind DN password attribute to the AES
 * reversible password plugin
 */
static int32_t
upgrade_AES_reverpwd_plugin(void)
{
    Slapi_PBlock *search_pb = slapi_pblock_new();
    Slapi_Entry *plugin_entry = NULL;
    Slapi_DN *sdn = NULL;
    const char *plugin_dn = "cn=AES,cn=Password Storage Schemes,cn=plugins,cn=config";
    char *plugin_attr = "nsslapd-pluginarg2";
    char *repl_bootstrap_val = "nsds5replicabootstrapcredentials";
    upgrade_status uresult = UPGRADE_SUCCESS;

    sdn = slapi_sdn_new_dn_byref(plugin_dn);
    slapi_search_get_entry(&search_pb, sdn, NULL, &plugin_entry, NULL);
    if (plugin_entry) {
        if (slapi_entry_attr_get_ref(plugin_entry, plugin_attr) == NULL) {
            /* The attribute is not set, add it */
            Slapi_PBlock *mod_pb = slapi_pblock_new();
            LDAPMod mod_add;
            LDAPMod *mods[2];
            char *add_val[2];
            int32_t result;

            add_val[0] = repl_bootstrap_val;
            add_val[1] = 0;
            mod_add.mod_op = LDAP_MOD_ADD;
            mod_add.mod_type = plugin_attr;
            mod_add.mod_values = add_val;
            mods[0] = &mod_add;
            mods[1] = 0;

            slapi_modify_internal_set_pb(mod_pb, plugin_dn,
                    mods, 0, 0, (void *)plugin_get_default_component_id(), 0);
            slapi_modify_internal_pb(mod_pb);
            slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
            if (result != LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "upgrade_AES_reverpwd_plugin",
                        "Failed to upgrade (%s) with new replication "
                        "bootstrap password attribute (%s), error %d\n",
                        plugin_dn, plugin_attr, result);
                uresult = UPGRADE_FAILURE;
            }
            slapi_pblock_destroy(mod_pb);
        }
    }
    slapi_search_get_entry_done(&search_pb);
    slapi_sdn_free(&sdn);

    return uresult;
}

#ifdef RUST_ENABLE
static upgrade_status
upgrade_143_entryuuid_exists(void) {
    char *entry = "dn: cn=entryuuid,cn=plugins,cn=config\n"
                  "objectclass: top\n"
                  "objectclass: nsSlapdPlugin\n"
                  "cn: entryuuid\n"
                  "nsslapd-pluginpath: libentryuuid-plugin\n"
                  "nsslapd-plugininitfunc: entryuuid_plugin_init\n"
                  "nsslapd-plugintype: betxnpreoperation\n"
                  "nsslapd-pluginenabled: on\n"
                  "nsslapd-pluginId: entryuuid\n"
                  "nsslapd-pluginVersion: none\n"
                  "nsslapd-pluginVendor: 389 Project\n"
                  "nsslapd-pluginDescription: entryuuid\n";

    return upgrade_entry_exists_or_create(
        "upgrade_143_entryuuid_exists",
        "(cn=entryuuid)",
        "cn=entryuuid,cn=plugins,cn=config",
        entry
    );
}
#endif

/*
 * Check if parentid/ancestorid indexes are missing the integerOrderingMatch
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
upgrade_server(void) {
#ifdef RUST_ENABLE
    if (upgrade_143_entryuuid_exists() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }
#endif

    if (upgrade_AES_reverpwd_plugin() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_check_id_index_matching_rule() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    return UPGRADE_SUCCESS;
}


