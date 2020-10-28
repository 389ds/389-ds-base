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

static upgrade_status
upgrade_144_remove_http_client_presence(void) {
    struct slapi_pblock *delete_pb = slapi_pblock_new();
    slapi_delete_internal_set_pb(delete_pb, "cn=HTTP Client,cn=plugins,cn=config", NULL, NULL, NULL, 0);
    slapi_delete_internal_pb(delete_pb);
    slapi_pblock_destroy(delete_pb);
    return UPGRADE_SUCCESS;
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

    if (upgrade_144_remove_http_client_presence() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }

    return UPGRADE_SUCCESS;
}

PRBool
upgrade_plugin_removed(char *plg_libpath) {
    if (strcmp("libhttp-client-plugin", plg_libpath) == 0 ||
        strcmp("libpresence-plugin", plg_libpath) == 0
    ) {
        return PR_TRUE;
    }
    return PR_FALSE;
}


