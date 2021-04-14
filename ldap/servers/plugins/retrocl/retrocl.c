/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
 * Requires that create_instance.c have added a plugin entry similar to:

dn: cn=Retrocl Plugin,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: RetroCL Plugin
nsslapd-pluginpath: /export2/servers/Hydra-supplier/lib/retrocl-plugin.so
nsslapd-plugininitfunc: retrocl_plugin_init
nsslapd-plugintype: object
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-pluginid: retrocl
nsslapd-pluginversion: 5.0b2
nsslapd-pluginvendor: Sun Microsystems, Inc.
nsslapd-plugindescription: Retrocl Plugin

 *
 */

#include "retrocl.h"


void *g_plg_identity[PLUGIN_MAX];
Slapi_Backend *retrocl_be_changelog = NULL;
PRLock *retrocl_internal_lock = NULL;
Slapi_RWLock *retrocl_cn_lock;
int retrocl_nattributes = 0;
char **retrocl_attributes = NULL;
char **retrocl_aliases = NULL;
int retrocl_log_deleted = 0;
int retrocl_nexclude_attrs = 0;

static Slapi_DN **retrocl_includes = NULL;
static Slapi_DN **retrocl_excludes = NULL;
static char **retrocl_exclude_attrs = NULL;

/* ----------------------------- Retrocl Plugin */

static Slapi_PluginDesc retrocldesc = {"retrocl", VENDOR, DS_PACKAGE_VERSION, "Retrocl Plugin"};
static Slapi_PluginDesc retroclpostopdesc = {"retrocl-postop", VENDOR, DS_PACKAGE_VERSION, "retrocl post-operation plugin"};
static Slapi_PluginDesc retroclinternalpostopdesc = {"retrocl-internalpostop", VENDOR, DS_PACKAGE_VERSION, "retrocl internal post-operation plugin"};
static int legacy_initialised = 0;

/*
 * Function: retrocl_*
 *
 * Returns: LDAP_
 *
 * Arguments: Pb of operation
 *
 * Description: wrappers around retrocl_postob registered as callback
 *
 */

int
retrocl_postop_add(Slapi_PBlock *pb)
{
    return retrocl_postob(pb, OP_ADD);
}
int
retrocl_postop_delete(Slapi_PBlock *pb)
{
    return retrocl_postob(pb, OP_DELETE);
}
int
retrocl_postop_modify(Slapi_PBlock *pb)
{
    return retrocl_postob(pb, OP_MODIFY);
}
int
retrocl_postop_modrdn(Slapi_PBlock *pb)
{
    return retrocl_postob(pb, OP_MODRDN);
}

/*
 * Function: retrocl_postop_init
 *
 * Returns: 0/-1
 *
 * Arguments: Pb
 *
 * Description: callback function
 *
 */

int
retrocl_postop_init(Slapi_PBlock *pb)
{
    int rc = 0; /* OK */
    Slapi_Entry *plugin_entry = NULL;
    const char *plugin_type = NULL;
    int postadd = SLAPI_PLUGIN_POST_ADD_FN;
    int postmod = SLAPI_PLUGIN_POST_MODIFY_FN;
    int postmdn = SLAPI_PLUGIN_POST_MODRDN_FN;
    int postdel = SLAPI_PLUGIN_POST_DELETE_FN;

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        postadd = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
        postmod = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
        postmdn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
        postdel = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
    }

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&retroclpostopdesc) != 0 ||
        slapi_pblock_set(pb, postadd, (void *)retrocl_postop_add) != 0 ||
        slapi_pblock_set(pb, postdel, (void *)retrocl_postop_delete) != 0 ||
        slapi_pblock_set(pb, postmod, (void *)retrocl_postop_modify) != 0 ||
        slapi_pblock_set(pb, postmdn, (void *)retrocl_postop_modrdn) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_postop_init failed\n");
        rc = -1;
    }

    return rc;
}

/*
 * Function: retrocl_internalpostop_init
 *
 * Returns: 0/-1
 *
 * Arguments: Pb
 *
 * Description: callback function
 *
 */

int
retrocl_internalpostop_init(Slapi_PBlock *pb)
{
    int rc = 0; /* OK */

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&retroclinternalpostopdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN, (void *)retrocl_postop_add) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN, (void *)retrocl_postop_delete) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN, (void *)retrocl_postop_modify) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN, (void *)retrocl_postop_modrdn) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_internalpostop_init failed\n");
        rc = -1;
    }

    return rc;
}

/*
 * Function: retrocl_rootdse_init
 *
 * Returns: LDAP_SUCCESS
 *
 * Arguments: Slapi_PBlock
 *
 * Description:   The FE DSE *must* be initialised before we get here.
 *
 */
static int
retrocl_rootdse_init(Slapi_PBlock *pb)
{

    int return_value = LDAP_SUCCESS;

    slapi_config_register_callback_plugin(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN, "",
                                          LDAP_SCOPE_BASE, "(objectclass=*)",
                                          retrocl_rootdse_search, NULL, pb);
    return return_value;
}

/*
 * Function: retrocl_select_backend
 *
 * Returns: LDAP_
 *
 * Arguments: none
 *
 * Description: simulates an add of the changelog to see if it exists.  If not,
 * creates it.  Then reads the changenumbers.  This function should be called
 * exactly once at startup.
 *
 */

static int
retrocl_select_backend(void)
{
    int err;
    Slapi_PBlock *pb;
    Slapi_Backend *be = NULL;
    Slapi_Entry *referral = NULL;
    Slapi_Operation *op = NULL;
    char errbuf[SLAPI_DSE_RETURNTEXT_SIZE];

    pb = slapi_pblock_new();

    slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, g_plg_identity[PLUGIN_RETROCL]);

    /* This is a simulated operation; no actual add is performed */
    op = operation_new(OP_FLAG_INTERNAL);
    operation_set_type(op, SLAPI_OPERATION_ADD); /* Ensure be not readonly */

    operation_set_target_spec_str(op, RETROCL_CHANGELOG_DN);

    slapi_pblock_set(pb, SLAPI_OPERATION, op);

    err = slapi_mapping_tree_select(pb, &be, &referral, errbuf, sizeof(errbuf));
    slapi_entry_free(referral);

    if (err != LDAP_SUCCESS || be == NULL || be == defbackend_get_backend()) {
        /* Could not find the backend for cn=changelog, either because
         * it doesn't exist mapping tree not registered. */
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                      "retrocl_select_backend - Mapping tree select failed (%d) %s.\n", err, errbuf);
        err = retrocl_create_config();
        if (err != LDAP_SUCCESS)
            return err;
    } else {
        retrocl_be_changelog = be;
    }

    retrocl_create_cle();
    slapi_pblock_destroy(pb);

    if (be)
        slapi_be_Unlock(be);

    return retrocl_get_changenumbers();
}

/*
 * Function: retrocl_get_config_str
 *
 * Returns: malloc'ed string which must be freed.
 *
 * Arguments: attribute type name
 *
 * Description:  reads a single-valued string attr from the plugins' own DSE.
 * This is called twice: to obtain the trim max age during startup, and to
 * obtain the change log directory.  No callback is registered; you cannot
 * change the trim max age without restarting the server.
 *
 */

char *
retrocl_get_config_str(const char *attrt)
{
    Slapi_Entry **entries;
    Slapi_PBlock *pb = NULL;
    char *ma;
    int rc = 0;
    char *dn;

    /* RETROCL_PLUGIN_DN is no need to be normalized. */
    dn = RETROCL_PLUGIN_DN;

    pb = slapi_pblock_new();

    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_BASE, "objectclass=*", NULL, 0, NULL,
                                 NULL, g_plg_identity[PLUGIN_RETROCL], 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != 0) {
        slapi_pblock_destroy(pb);
        return NULL;
    }
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    ma = slapi_entry_attr_get_charptr(entries[0], attrt);
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return ma;
}

static void
retrocl_remove_legacy_default_aci(void)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry **entries;
    char **aci_vals = NULL;
    char *attrs[] = {"aci", NULL};
    int rc;

    pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, RETROCL_CHANGELOG_DN, LDAP_SCOPE_BASE, "objectclass=*",
                                 attrs, 0, NULL, NULL, g_plg_identity[PLUGIN_RETROCL], 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc == LDAP_SUCCESS) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries && entries[0]) {
            if ((aci_vals = slapi_entry_attr_get_charray(entries[0], "aci"))) {
                if (charray_inlist(aci_vals, RETROCL_ACL)) {
                    /*
                     * Okay, we need to remove the aci
                     */
                    LDAPMod mod;
                    LDAPMod *mods[2];
                    char *val[2];
                    Slapi_PBlock *mod_pb = 0;

                    mod_pb = slapi_pblock_new();
                    mods[0] = &mod;
                    mods[1] = 0;
                    val[0] = RETROCL_ACL;
                    val[1] = 0;
                    mod.mod_op = LDAP_MOD_DELETE;
                    mod.mod_type = "aci";
                    mod.mod_values = val;

                    slapi_modify_internal_set_pb_ext(mod_pb, slapi_entry_get_sdn(entries[0]),
                                                     mods, 0, 0, g_plg_identity[PLUGIN_RETROCL], 0);
                    slapi_modify_internal_pb(mod_pb);
                    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
                    if (rc == LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_NOTICE, RETROCL_PLUGIN_NAME,
                                      "retrocl_remove_legacy_default_aci - "
                                      "Successfully removed vulnerable legacy default aci \"%s\".  "
                                      "If the aci removal was not desired please use a different \"acl "
                                      "name\" so it is not removed at the next plugin startup.\n",
                                      RETROCL_ACL);
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                                      "retrocl_remove_legacy_default_aci - "
                                      "Failed to removed vulnerable legacy default aci (%s) error %d\n",
                                      RETROCL_ACL, rc);
                    }
                    slapi_pblock_destroy(mod_pb);
                }
                slapi_ch_array_free(aci_vals);
            }
        }
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
}


/*
 * Function: retrocl_start
 *
 * Returns: 0 on success
 *
 * Arguments: Pb
 *
 * Description:
 *
 */

static int
retrocl_start(Slapi_PBlock *pb)
{
    int rc = 0;
    Slapi_Entry *e = NULL;
    char **values = NULL;
    int num_vals = 0;

    retrocl_rootdse_init(pb);

    rc = retrocl_select_backend();

    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_start - Couldn't find backend, not trimming retro changelog (%d).\n", rc);
        return rc;
    }

    /* Remove the old default aci as it exposes passwords changes to anonymous users */
    retrocl_remove_legacy_default_aci();

    retrocl_init_trimming();

    if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME, "retrocl_start - Missing config entry.\n");
        return -1;
    }

    /* Get the exclude attributes */
    values = slapi_entry_attr_get_charray_ext(e, CONFIG_CHANGELOG_EXCLUDE_ATTRS, &num_vals);
    if (values) {
        retrocl_nexclude_attrs = num_vals;
        retrocl_exclude_attrs = (char **)slapi_ch_calloc(num_vals + 1, sizeof(char *));

        for (size_t i = 0; i < num_vals; i++) {
            char *value = values[i];
            size_t length = strlen(value);

            char *pos = strchr(value, ':');
            if (pos == NULL) {
                retrocl_exclude_attrs[i] = slapi_ch_strdup(value);
            } else {
                retrocl_exclude_attrs[i] = slapi_ch_malloc(pos - value + 1);
                strncpy(retrocl_exclude_attrs[i], value, pos - value);
                retrocl_exclude_attrs[i][pos - value] = '\0';
            }
            slapi_log_err(SLAPI_LOG_INFO, RETROCL_PLUGIN_NAME,"retrocl_start - retrocl_exclude_attrs (%s).\n", retrocl_exclude_attrs[i]);
        }
        slapi_ch_array_free(values);
    }
    /* Get the exclude suffixes */
    values = slapi_entry_attr_get_charray_ext(e, CONFIG_CHANGELOG_EXCLUDE_SUFFIX, &num_vals);
    if (values) {
        /* Validate the syntax before we create our DN array */
        for (size_t i = 0; i < num_vals; i++) {
            if (slapi_dn_syntax_check(pb, values[i], 1)) {
                /* invalid dn syntax */
                slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                              "retrocl_start - Invalid DN (%s) for exclude suffix.\n", values[i]);
                slapi_ch_array_free(values);
                return -1;
            }
        }
        /* Now create our SDN array */
        retrocl_excludes = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), num_vals + 1);
        for (size_t i = 0; i < num_vals; i++) {
            retrocl_excludes[i] = slapi_sdn_new_dn_byval(values[i]);
        }
        slapi_ch_array_free(values);
    }
    /* Get the include suffixes */
    values = slapi_entry_attr_get_charray_ext(e, CONFIG_CHANGELOG_INCLUDE_SUFFIX, &num_vals);
    if (values) {
        for (size_t i = 0; i < num_vals; i++) {
            /* Validate the syntax before we create our DN array */
            if (slapi_dn_syntax_check(pb, values[i], 1)) {
                /* invalid dn syntax */
                slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                              "retrocl_start - Invalid DN (%s) for include suffix.\n", values[i]);
                slapi_ch_array_free(values);
                return -1;
            }
        }
        /* Now create our SDN array */
        retrocl_includes = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), num_vals + 1);
        for (size_t i = 0; i < num_vals; i++) {
            retrocl_includes[i] = slapi_sdn_new_dn_byval(values[i]);
        }
        slapi_ch_array_free(values);
    }
    if (retrocl_includes && retrocl_excludes) {
        /*
         * Make sure we haven't mixed the same suffix, and there are no
         * conflicts between the includes and excludes
         */
        int i = 0;

        while (retrocl_includes[i]) {
            int x = 0;
            while (retrocl_excludes[x]) {
                if (slapi_sdn_compare(retrocl_includes[i], retrocl_excludes[x]) == 0) {
                    /* we have a conflict */
                    slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                                  "retrocl_start - Include suffix (%s) is also listed in exclude suffix list\n",
                                  slapi_sdn_get_dn(retrocl_includes[i]));
                    return -1;
                }
                x++;
            }
            i++;
        }

        /* Check for parent/child conflicts */
        i = 0;
        while (retrocl_includes[i]) {
            int x = 0;
            while (retrocl_excludes[x]) {
                if (slapi_sdn_issuffix(retrocl_includes[i], retrocl_excludes[x])) {
                    /* we have a conflict */
                    slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                                  "retrocl_start - include suffix (%s) is a child of the exclude suffix(%s)\n",
                                  slapi_sdn_get_dn(retrocl_includes[i]),
                                  slapi_sdn_get_dn(retrocl_excludes[i]));
                    return -1;
                }
                x++;
            }
            i++;
        }
    }

    values = slapi_entry_attr_get_charray(e, "nsslapd-attribute");
    if (values != NULL) {
        int n = 0;

        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_start - nsslapd-attribute:\n");

        for (n = 0; values && values[n]; n++) {
            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_start - %s\n", values[n]);
        }

        retrocl_nattributes = n;

        retrocl_attributes = (char **)slapi_ch_calloc(n + 1, sizeof(char *));
        retrocl_aliases = (char **)slapi_ch_calloc(n + 1, sizeof(char *));

        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_start - Attributes:\n");

        for (size_t i = 0; i < n; i++) {
            char *value = values[i];
            size_t length = strlen(value);

            char *pos = strchr(value, ':');
            if (pos == NULL) {
                retrocl_attributes[i] = slapi_ch_strdup(value);
                retrocl_aliases[i] = NULL;

                slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, " - %s\n",
                              retrocl_attributes[i]);
            } else {
                retrocl_attributes[i] = slapi_ch_malloc(pos - value + 1);
                strncpy(retrocl_attributes[i], value, pos - value);
                retrocl_attributes[i][pos - value] = '\0';

                retrocl_aliases[i] = slapi_ch_malloc(value + length - pos);
                strcpy(retrocl_aliases[i], pos + 1);

                slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, " - %s [%s]\n",
                              retrocl_attributes[i], retrocl_aliases[i]);
            }
        }

        slapi_ch_array_free(values);
    }

    retrocl_log_deleted = 0;
    values = slapi_entry_attr_get_charray(e, "nsslapd-log-deleted");
    if (values != NULL) {
        if (values[1] != NULL) {
            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "retrocl_start - Multiple values specified for attribute: nsslapd-log-deleted\n");
        } else if (0 == strcasecmp(values[0], "on")) {
            retrocl_log_deleted = 1;
        } else if (strcasecmp(values[0], "off")) {
            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "Iretrocl_start - nvalid value (%s) specified for attribute: nsslapd-log-deleted\n", values[0]);
        }
        slapi_ch_array_free(values);
    }

    return 0;
}

/*
 * Check if an entry is in the configured scope.
 * Return 1 if entry is in the scope, or 0 otherwise.
 * For MODRDN the caller should check both the preop
 * and postop entries.  If we are moving out of, or
 * into scope, we should record it.
 */
int
retrocl_entry_in_scope(Slapi_Entry *e)
{
    Slapi_DN *sdn = slapi_entry_get_sdn(e);

    if (e == NULL) {
        return 1;
    }

    if (retrocl_excludes) {
        int i = 0;

        /* check the excludes */
        while (retrocl_excludes[i]) {
            if (slapi_sdn_issuffix(sdn, retrocl_excludes[i])) {
                return 0;
            }
            i++;
        }
    }
    if (retrocl_includes) {
        int i = 0;

        /* check the excludes */
        while (retrocl_includes[i]) {
            if (slapi_sdn_issuffix(sdn, retrocl_includes[i])) {
                return 1;
            }
            i++;
        }
        return 0;
    }

    return 1;
}

/*
 * Function: retrocl_stop
 *
 * Returns: 0
 *
 * Arguments: Pb
 *
 * Description: called when the server is shutting down
 *
 */

static int
retrocl_stop(Slapi_PBlock *pb __attribute__((unused)))
{
    int rc = 0;
    int i = 0;

    slapi_ch_array_free(retrocl_attributes);
    retrocl_attributes = NULL;
    slapi_ch_array_free(retrocl_aliases);
    retrocl_aliases = NULL;
    slapi_ch_array_free(retrocl_exclude_attrs);
    retrocl_exclude_attrs = NULL;

    while (retrocl_excludes && retrocl_excludes[i]) {
        slapi_sdn_free(&retrocl_excludes[i]);
        i++;
    }
    slapi_ch_free((void **)&retrocl_excludes);
    i = 0;

    while (retrocl_includes && retrocl_includes[i]) {
        slapi_sdn_free(&retrocl_includes[i]);
        i++;
    }
    slapi_ch_free((void **)&retrocl_includes);

    retrocl_stop_trimming();
    retrocl_be_changelog = NULL;
    retrocl_forget_changenumbers();
    PR_DestroyLock(retrocl_internal_lock);
    retrocl_internal_lock = NULL;
    slapi_destroy_rwlock(retrocl_cn_lock);
    retrocl_cn_lock = NULL;
    legacy_initialised = 0;

    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, "",
                                 LDAP_SCOPE_BASE, "(objectclass=*)", retrocl_rootdse_search);

    return rc;
}

/*
 * Function: retrocl_plugin_init
 *
 * Returns: 0 on successs
 *
 * Arguments: Pb
 *
 * Description: main entry point for retrocl
 *
 */

int
retrocl_plugin_init(Slapi_PBlock *pb)
{
    int rc = 0;
    int precedence = 0;
    void *identity = NULL;
    Slapi_Entry *plugin_entry = NULL;
    int is_betxn = 0;
    const char *plugintype = "postoperation";

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &identity);
    PR_ASSERT(identity);
    g_plg_identity[PLUGIN_RETROCL] = identity;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRECEDENCE, &precedence);

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry) {
        is_betxn = slapi_entry_attr_get_bool(plugin_entry, "nsslapd-pluginbetxn");
    }

    if (!legacy_initialised) {
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01);
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&retrocldesc);
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)retrocl_start);
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)retrocl_stop);

        if (is_betxn) {
            plugintype = "betxnpostoperation";
        }
        rc = slapi_register_plugin_ext(plugintype, 1 /* Enabled */, "retrocl_postop_init", retrocl_postop_init, "Retrocl postoperation plugin", NULL, identity, precedence);
        if (!is_betxn) {
            rc = slapi_register_plugin_ext("internalpostoperation", 1 /* Enabled */, "retrocl_internalpostop_init", retrocl_internalpostop_init, "Retrocl internal postoperation plugin", NULL, identity, precedence);
        }
        retrocl_cn_lock = slapi_new_rwlock();
        if (retrocl_cn_lock == NULL)
            return -1;
        retrocl_internal_lock = PR_NewLock();
        if (retrocl_internal_lock == NULL)
            return -1;
    }

    legacy_initialised = 1;
    return rc;
}

/*
 * Function: retrocl_attr_in_exclude_attrs
 *
 * Return 1 if attribute exists in the retrocl_exclude_attrs list, else return 0.
 *
 * Arguments: attribute string, attribute length.
 *
 * Description: Check if an attribute is in the global exclude attribute list.
 *
 */
int
retrocl_attr_in_exclude_attrs(char *attr, int attrlen)
{
    int i = 0;
    if (attr && attrlen > 0 && retrocl_nexclude_attrs > 0) {
        while (retrocl_exclude_attrs[i]) {
            if (strncmp(retrocl_exclude_attrs[i], attr, attrlen) == 0) {
                slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,"retrocl_attr_in_exclude_attrs - excluding attr (%s).\n", attr);
                return 1;
            }
            i++;
        }
    }
    return 0;
}
