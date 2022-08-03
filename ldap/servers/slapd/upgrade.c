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

#ifdef RUST_ENABLE
static char *modifier_name = "cn=upgrade internal,cn=config";
#endif
static char *old_repl_plugin_name = NULL;

#ifdef RUST_ENABLE
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
#endif

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

#ifdef RUST_ENABLE
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
#endif

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

upgrade_status
upgrade_server(void)
{
#ifdef RUST_ENABLE
    if (upgrade_143_entryuuid_exists() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }
#endif

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
