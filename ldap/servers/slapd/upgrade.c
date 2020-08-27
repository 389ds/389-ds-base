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

upgrade_status
upgrade_server(void) {
#ifdef RUST_ENABLE
    if (upgrade_143_entryuuid_exists() != UPGRADE_SUCCESS) {
        return UPGRADE_FAILURE;
    }
#endif

    return UPGRADE_SUCCESS;
}


