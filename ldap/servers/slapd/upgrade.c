/* BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
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
static char *old_repl_plugin_name = NULL;

static upgrade_status
upgrade_entry_exists_or_create(char *upgrade_id, char *filter, char *dn, char *entry)
{
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
static upgrade_status
upgrade_AES_reverpwd_plugin(void)
{
    struct slapi_pblock *search_pb = slapi_pblock_new();
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

static upgrade_status
upgrade_143_entryuuid_exists(void)
{
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

static upgrade_status
upgrade_144_remove_http_client_presence(void)
{
    struct slapi_pblock *delete_pb = slapi_pblock_new();
    slapi_delete_internal_set_pb(delete_pb, "cn=HTTP Client,cn=plugins,cn=config",
            NULL, NULL, plugin_get_default_component_id(), 0);
    slapi_delete_internal_pb(delete_pb);
    slapi_pblock_destroy(delete_pb);
    return UPGRADE_SUCCESS;
}

static upgrade_status
upgrade_201_remove_des_rever_pwd_scheme(void)
{
    struct slapi_pblock *delete_pb = slapi_pblock_new();
    slapi_delete_internal_set_pb(delete_pb, "cn=DES,cn=Password Storage Schemes,cn=plugins,cn=config",
            NULL, NULL, plugin_get_default_component_id(), 0);
    slapi_delete_internal_pb(delete_pb);
    slapi_pblock_destroy(delete_pb);
    return UPGRADE_SUCCESS;
}

/*
 * Add the required "nsslapd-securitylog" attribute to cn=config
 */
static upgrade_status
upgrade_enable_security_logging(void)
{
    struct slapi_pblock *pb = slapi_pblock_new();
    Slapi_Entry **entries = NULL;

    slapi_search_internal_set_pb(
            pb, "cn=config", LDAP_SCOPE_BASE,
            "objectclass=*", NULL, 0, NULL, NULL,
            plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    /* coverity[dereference] */
    if (entries &&
        strcasecmp(slapi_entry_attr_get_ref(entries[0], "nsslapd-securitylog"), "") == 0)
    {
        slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
        Slapi_PBlock *mod_pb = slapi_pblock_new();
        LDAPMod mod_replace;
        LDAPMod *mods[2];
        char *replace_val[2];
        char security_log[BUFSIZ] = {0};
        int log_len = strlen(slapdFrontendConfig->errorlog);

        /* build the security log name based off error log location */
        memcpy(security_log, slapdFrontendConfig->errorlog, log_len - 6);
        strcat(security_log, "security");

        replace_val[0] = security_log;
        replace_val[1] = 0;
        mod_replace.mod_op = LDAP_MOD_REPLACE;
        mod_replace.mod_type = "nsslapd-securitylog";
        mod_replace.mod_values = replace_val;
        mods[0] = &mod_replace;
        mods[1] = 0;

        /* Update security logging */
        slapi_modify_internal_set_pb(mod_pb, "cn=config",
                                     mods, 0, 0, plugin_get_default_component_id(),
                                     SLAPI_OP_FLAG_FIXUP);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_destroy(mod_pb);

        slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_enable_security_logging",
                "Upgrade task: enabled security audit log\n");
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return UPGRADE_SUCCESS;
}

/*
 * The replication plugin was renamed, this function cleans up all the
 * dependencies in the other plugins
 */
static upgrade_status
upgrade_205_fixup_repl_dep(void)
{
    if (old_repl_plugin_name) {
        struct slapi_pblock *pb = slapi_pblock_new();
        Slapi_Entry **entries = NULL;
        char *filter = slapi_ch_smprintf("(nsslapd-plugin-depends-on-named=%s)",
                                         old_repl_plugin_name);

        slapi_search_internal_set_pb(
                pb, "cn=config", LDAP_SCOPE_SUBTREE,
                filter, NULL, 0, NULL, NULL,
                plugin_get_default_component_id(), 0);
        slapi_search_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries) {
            /*
             * We do have plugins that need their dependency updated
             */
            LDAPMod mod_delete;
            LDAPMod mod_add;
            LDAPMod *mods[3];
            char *delete_val[2];
            char *add_val[2];
             /* delete the old value */
            delete_val[0] = (char *)old_repl_plugin_name;
            delete_val[1] = 0;
            mod_delete.mod_op = LDAP_MOD_DELETE;
            mod_delete.mod_type = "nsslapd-plugin-depends-on-named";
            mod_delete.mod_values = delete_val;
             /* add the new value */
            add_val[0] = "Multisupplier Replication Plugin";
            add_val[1] = 0;
            mod_add.mod_op = LDAP_MOD_ADD;
            mod_add.mod_type = "nsslapd-plugin-depends-on-named";
            mod_add.mod_values = add_val;
            mods[0] = &mod_delete;
            mods[1] = &mod_add;
            mods[2] = 0;
            for (; *entries; entries++) {
                Slapi_PBlock *mod_pb = slapi_pblock_new();
                Slapi_PBlock *entry_pb = NULL;
                Slapi_DN *edn = slapi_sdn_new_dn_byval(slapi_entry_get_dn(*entries));
                Slapi_Entry *plugin_e = NULL;

                /* Update plugin entry */
                slapi_modify_internal_set_pb(mod_pb, slapi_entry_get_dn(*entries),
                                             mods, 0, 0, plugin_get_default_component_id(),
                                             SLAPI_OP_FLAG_FIXUP);
                slapi_modify_internal_pb(mod_pb);
                slapi_pblock_destroy(mod_pb);

                /*
                 * Get a fresh copy of the new entry, and update the global
                 * plugin dependencies list
                 */
                if (slapi_search_get_entry(&entry_pb, edn, NULL, &plugin_e, NULL) == LDAP_SUCCESS) {
                    plugin_update_dep_entries(plugin_e);
                }
                slapi_search_get_entry_done(&entry_pb);
                slapi_sdn_free(&edn);

                slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_205_fixup_repl_dep",
                        "Upgrade task: updated plugin dependencies for (%s)\n",
                        slapi_entry_get_dn(*entries));
            }
        }
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        slapi_ch_free_string(&old_repl_plugin_name);
        slapi_ch_free_string(&filter);
    }

    return UPGRADE_SUCCESS;
}

/*
 * "nsslapd-subtree-rename-switch" has been removed from the code
 * so cleanup the ldbm config entry
 */
static upgrade_status
upgrade_remove_subtree_rename(void)
{
    struct slapi_pblock *pb = slapi_pblock_new();
    Slapi_Entry **entries = NULL;
    const char *base_dn = "cn=config,cn=ldbm database,cn=plugins,cn=config";
    const char *filter = "(nsslapd-subtree-rename-switch=*)";
    char *attr = "nsslapd-subtree-rename-switch";

    slapi_search_internal_set_pb(
            pb, base_dn,
            LDAP_SCOPE_BASE,
            filter, NULL, 0, NULL, NULL,
            plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    if (entries && entries[0]) {
        /*
         * The attribute is present, remove it
         */
        Slapi_PBlock *mod_pb = slapi_pblock_new();
        LDAPMod mod_delete;
        LDAPMod *mods[2];
        mod_delete.mod_op = LDAP_MOD_DELETE;
        mod_delete.mod_type = attr;
        mod_delete.mod_values = NULL;
        mods[0] = &mod_delete;
        mods[1] = 0;

        /* Update ldbm config entry */
        slapi_modify_internal_set_pb(mod_pb, base_dn, mods, 0, 0,
                                     plugin_get_default_component_id(),
                                     SLAPI_OP_FLAG_FIXUP);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_destroy(mod_pb);

        slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_remove_subtree_rename",
                "Upgrade task: obsolete attribute '%s' removed from '%s'\n",
                attr, base_dn);
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return UPGRADE_SUCCESS;
}

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
                        slapi_log_err(SLAPI_LOG_WARNING, "upgrade_check_id_index_matching_rule",
                                "Index '%s' in backend '%s' is missing 'nsMatchingRule: integerOrderingMatch'. "
                                "Without it, searches may return incorrect or empty results. "
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

/*
 * Upgrade the base config of the PAM PTA plugin.
 *
 * Check the plugins base DN for any PTA configuration attributes. If any are found
 * they are migrated to a child entry under the plugins base DN, and the original attrs
 * are deleted from the base entry.
 */
static upgrade_status
upgrade_pam_pta_default_config(void)
{
    const char *parent_dn_str = "cn=PAM Pass Through Auth,cn=plugins,cn=config";
    char *child_cn_str = "default";
    const char *pam_cfg_attrs[] = {
        "pamMissingSuffix",
        "pamExcludeSuffix",
        "pamIDMapMethod",
        "pamIDAttr",
        "pamFallback",
        "pamSecure",
        "pamService",
        NULL
    };
    Slapi_Attr **attrs = NULL;
    Slapi_PBlock *add_pb = NULL;
    Slapi_PBlock *mod_pb = NULL;
    int num_attrs = 0;
    int rc = 0;
    int result = LDAP_SUCCESS;

    /* Check base entry exists */
    Slapi_Entry *base_entry = NULL;
    Slapi_DN *parent_sdn = slapi_sdn_new_dn_byval(parent_dn_str);
    rc = slapi_search_internal_get_entry(parent_sdn, NULL, &base_entry, plugin_get_default_component_id());
    if (rc != LDAP_SUCCESS || !base_entry) {
        slapi_log_error(SLAPI_LOG_ERR, "upgrade_pam_pta_default_config",
                        "Base entry not found: %s\n", parent_dn_str);
        slapi_sdn_free(&parent_sdn);
        return UPGRADE_FAILURE;
    }

    /* Check for config attributes in base entry */
    for (size_t i = 0; pam_cfg_attrs[i] != NULL; i++) {
        Slapi_Attr *attr = NULL;
        if (slapi_entry_attr_find(base_entry, pam_cfg_attrs[i], &attr) == 0) {
            Slapi_Attr *attr_copy = slapi_attr_dup(attr);
            if (attr_copy) {
                attrs = (Slapi_Attr **)slapi_ch_realloc((void *)attrs, sizeof(Slapi_Attr *) * (num_attrs + 1));
                attrs[num_attrs++] = attr_copy;
            }
        }
    }

    /* NULL terminate discovered attribute array */
    if (attrs) {
        attrs = (Slapi_Attr **)slapi_ch_realloc((void *)attrs, sizeof(Slapi_Attr *) * (num_attrs + 1));
        attrs[num_attrs] = NULL;
    }

    if (!attrs || num_attrs == 0) {
        slapi_log_error(SLAPI_LOG_PLUGIN, "upgrade_pam_pta_default_config",
                        "No config attributes found, nothing to do.\n");

        slapi_entry_free(base_entry);
        slapi_sdn_free(&parent_sdn);
        return UPGRADE_SUCCESS;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, "upgrade_pam_pta_default_config",
                    "Detected %d config attributes in %s.\n", num_attrs, parent_dn_str);

    /* Construct child entry string from attr:values discovered in the base entry */
    char *child_dn = slapi_ch_smprintf("cn=%s,%s", child_cn_str, parent_dn_str);
    Slapi_Entry *child_entry = NULL;
    char *entry_string = NULL;
    if (child_dn) {
        entry_string = slapi_ch_smprintf("dn: %s\n"
                                         "cn: %s\n"
                                         "objectClass: top\n"
                                         "objectClass: extensibleObject\n"
                                         "objectClass: pamConfig\n",
                                         child_dn, child_cn_str);

        if (entry_string) {
            for (size_t i = 0; attrs[i] != NULL; i++) {
                Slapi_Attr *attr = attrs[i];
                Slapi_Value *sval;
                struct berval *bval;
                char *attr_type = NULL;

                slapi_attr_get_type(attr, &attr_type);
                if (slapi_attr_first_value(attr, &sval) == 0) {
                    if (sval) {
                        bval = (struct berval *)slapi_value_get_berval(sval);
                        if (bval && bval->bv_val) {
                            char *tmp = slapi_ch_smprintf("%s%s: %s\n", entry_string, attr_type, bval->bv_val);
                            if (tmp) {
                                slapi_ch_free_string(&entry_string);
                                entry_string = tmp;
                            } else {
                                slapi_log_error(SLAPI_LOG_ERR, "upgrade_pam_pta_default_config",
                                                "Failed to create child config entry string for %s.\n", child_dn);
                            }
                        }
                    }
                }
            }

            child_entry = slapi_str2entry(entry_string, 0);
            slapi_ch_free_string(&entry_string);
        }
    }

    /* Create child entry */
    if (child_entry) {
        add_pb = slapi_pblock_new();
        slapi_add_entry_internal_set_pb(add_pb, child_entry, NULL, plugin_get_default_component_id(), 0);
        slapi_add_internal_pb(add_pb);
        slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
        if (result != LDAP_SUCCESS && result != LDAP_ALREADY_EXISTS) {
            slapi_log_error(SLAPI_LOG_ERR, "upgrade_pam_pta_default_config",
                            "Failed to create child entry %s: %s\n", child_dn, ldap_err2string(result));

            slapi_entry_free(child_entry);
            slapi_pblock_destroy(add_pb);
            return UPGRADE_FAILURE;
        }
        slapi_pblock_destroy(add_pb);
    } else {
        slapi_log_error(SLAPI_LOG_ERR, "upgrade_pam_pta_default_config",
                        "Failed to create child entry %s\n", child_dn);

        slapi_ch_free_string(&child_dn);
        return UPGRADE_FAILURE;
    }

    /* Construct mods for internal delete */
    LDAPMod **mods = (LDAPMod **)slapi_ch_calloc(1, ((num_attrs + 1) * sizeof(LDAPMod *)));
    for (size_t i = 0; attrs[i] != NULL; i++) {
        LDAPMod *mod = (LDAPMod *)slapi_ch_malloc(sizeof(LDAPMod));
        Slapi_Attr *attr = attrs[i];
        char *attr_type = NULL;
        slapi_attr_get_type(attr, &attr_type);
        mod->mod_op = LDAP_MOD_DELETE;
        mod->mod_type = slapi_ch_strdup(attr_type);
        mod->mod_values = NULL;
        mods[i] = mod;
    }
    mods[num_attrs] = NULL;

    mod_pb = slapi_pblock_new();
    slapi_modify_internal_set_pb(mod_pb, parent_dn_str, mods, NULL, NULL,
                                plugin_get_default_component_id(), 0);
    slapi_modify_internal_pb(mod_pb);
    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
    if (result != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_ERR, "upgrade_pam_pta_default_config",
                    "Failed to delete config attrs from:%s\n", parent_dn_str);
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, "upgrade_pam_pta_default_config",
                    "Deleted %d config attrs from: %s\n", num_attrs, parent_dn_str);
    }

    /* Clean up */
    for (size_t i = 0; i < num_attrs; i++) {
        slapi_attr_free(&attrs[i]);
        if (mods[i]) {
            if (mods[i]->mod_type)
                slapi_ch_free_string(&mods[i]->mod_type);
            slapi_ch_free((void **)&mods[i]);
        }
    }

    slapi_ch_free((void **)&attrs);
    slapi_entry_free(base_entry);
    slapi_sdn_free(&parent_sdn);
    slapi_ch_free_string(&child_dn);
    slapi_ch_free((void **)&mods);
    slapi_pblock_destroy(mod_pb);

    return UPGRADE_SUCCESS;
}


upgrade_status
upgrade_server(void)
{
    if (upgrade_143_entryuuid_exists() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_AES_reverpwd_plugin() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_144_remove_http_client_presence() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_201_remove_des_rever_pwd_scheme() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_205_fixup_repl_dep() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_enable_security_logging() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_remove_subtree_rename() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_pam_pta_default_config() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    if (upgrade_check_id_index_matching_rule() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    return UPGRADE_SUCCESS;
}

PRBool
upgrade_plugin_removed(char *plg_libpath)
{
    if (strcmp("libhttp-client-plugin", plg_libpath) == 0 ||
        strcmp("libpresence-plugin", plg_libpath) == 0
    ) {
        return PR_TRUE;
    }
    return PR_FALSE;
}

upgrade_status
upgrade_repl_plugin_name(Slapi_Entry *plugin_entry, struct slapdplugin *plugin)
{
    /*
     * Update the replication plugin in dse and global plugin list.
     */
    if (strcmp(plugin->plg_libpath, "libreplication-plugin") == 0) {
        if (strcmp(plugin->plg_initfunc, "replication_multisupplier_plugin_init")) {
            /*
             * Update in-memory plugin entry
             */
            slapi_ch_free_string(&plugin->plg_initfunc);
            plugin->plg_initfunc = slapi_ch_strdup("replication_multisupplier_plugin_init");

            old_repl_plugin_name = plugin->plg_name; /* owns memory now, we will use this later */
            plugin->plg_name = slapi_ch_strdup("Multisupplier Replication Plugin");

            slapi_ch_free_string(&plugin->plg_dn);
            plugin->plg_dn = slapi_ch_strdup("cn=multisupplier replication plugin,cn=plugins,cn=config");

            /*
             * Update the plugin entry in cn=config
             */
            slapi_entry_set_dn(plugin_entry,
                               slapi_ch_strdup("cn=Multisupplier Replication Plugin,cn=plugins,cn=config"));
            slapi_entry_attr_set_charptr(plugin_entry, "nsslapd-pluginInitfunc",
                                         "replication_multisupplier_plugin_init");
            slapi_entry_attr_set_charptr(plugin_entry, "cn", "Multisupplier Replication Plugin");

            slapi_log_err(SLAPI_LOG_NOTICE, "upgrade_repl_plugin_name",
                          "Upgrade task: changed the replication plugin name to: %s\n",
                          slapi_entry_get_dn(plugin_entry));
        }
    }

    return UPGRADE_SUCCESS;
}
