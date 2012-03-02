/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 *
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception.
 *
 *
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Managed Entries Plug-in
 */
#include "mep.h"
#include "slapi-private.h"


/*
 * Plug-in globals
 */
static PRCList *g_mep_config = NULL;
static Slapi_RWLock *g_mep_config_lock;

static void *_PluginID = NULL;
static Slapi_DN *_PluginDN = NULL;
static Slapi_DN *_ConfigAreaDN = NULL;
static int g_plugin_started = 0;

static Slapi_PluginDesc pdesc = { MEP_FEATURE_DESC,
                                  VENDOR,
                                  DS_PACKAGE_VERSION,
                                  MEP_PLUGIN_DESC };

/*
 * Plug-in management functions
 */
int mep_init(Slapi_PBlock * pb);
static int mep_start(Slapi_PBlock * pb);
static int mep_close(Slapi_PBlock * pb);
static int mep_postop_init(Slapi_PBlock * pb);
static int mep_internal_postop_init(Slapi_PBlock *pb);

/*
 * Operation callbacks (where the real work is done)
 */
static int mep_mod_post_op(Slapi_PBlock *pb);
static int mep_add_post_op(Slapi_PBlock *pb);
static int mep_del_post_op(Slapi_PBlock *pb);
static int mep_modrdn_post_op(Slapi_PBlock *pb);
static int mep_pre_op(Slapi_PBlock *pb, int modop);
static int mep_mod_pre_op(Slapi_PBlock *pb);
static int mep_add_pre_op(Slapi_PBlock *pb);
static int mep_del_pre_op(Slapi_PBlock *pb);
static int mep_modrdn_pre_op(Slapi_PBlock *pb);

/*
 * Config cache management functions
 */
static int mep_load_config();
static void mep_delete_config();
static int mep_parse_config_entry(Slapi_Entry * e, int apply);
static void mep_free_config_entry(struct configEntry ** entry);

/*
 * helpers
 */
static Slapi_DN *mep_get_sdn(Slapi_PBlock * pb);
static Slapi_DN *mep_get_config_area();
static void mep_set_config_area(Slapi_DN *sdn);
static int mep_dn_is_config(Slapi_DN *sdn);
static int mep_dn_is_template(Slapi_DN *dn);
static void mep_find_config(Slapi_Entry *e, struct configEntry **config);
static void mep_find_config_by_template_dn(Slapi_DN *template_dn,
    struct configEntry **config);
static int mep_oktodo(Slapi_PBlock *pb);
static int mep_isrepl(Slapi_PBlock *pb);
static Slapi_Entry *mep_create_managed_entry(struct configEntry *config,
    Slapi_Entry *origin);
static void mep_add_managed_entry(struct configEntry *config,
    Slapi_Entry *origin);
static void mep_rename_managed_entry(Slapi_Entry *origin,
    Slapi_DN *new_dn, Slapi_DN *old_dn);
static Slapi_Mods *mep_get_mapped_mods(struct configEntry *config,
    Slapi_Entry *origin, char **mapped_dn);
static int mep_parse_mapped_attr(char *mapping, Slapi_Entry *origin,
    char **type, char **value);
static int mep_is_managed_entry(Slapi_Entry *e);
static int mep_is_mapped_attr(Slapi_Entry *template, char *type);
static int mep_has_tombstone_value(Slapi_Entry * e);
static int mep_parse_mapped_origin_attr(char *mapping, char **origin_type);
static int mep_is_mapped_origin_attr(char **vals, char *type);
static char** mep_extract_origin_attrs(Slapi_Entry *entry);

/*
 * Config cache locking functions
 */
void
mep_config_read_lock()
{
    slapi_rwlock_rdlock(g_mep_config_lock);
}

void
mep_config_write_lock()
{
    slapi_rwlock_wrlock(g_mep_config_lock);
}

void
mep_config_unlock()
{
    slapi_rwlock_unlock(g_mep_config_lock);
}


/*
 * Plugin identity functions
 */
void
mep_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
mep_get_plugin_id()
{
    return _PluginID;
}

void
mep_set_plugin_sdn(Slapi_DN *pluginDN)
{
    _PluginDN = pluginDN;
}

Slapi_DN *
mep_get_plugin_sdn()
{
    return _PluginDN;
}


static int plugin_is_betxn = 0;

/*
 * Plug-in initialization functions
 */
int
mep_init(Slapi_PBlock *pb)
{
    int status = 0;
    char *plugin_identity = NULL;
    Slapi_Entry *plugin_entry = NULL;
    char *plugin_type = NULL;
    int preadd = SLAPI_PLUGIN_PRE_ADD_FN;
    int premod = SLAPI_PLUGIN_PRE_MODIFY_FN;
    int predel = SLAPI_PLUGIN_PRE_DELETE_FN;
    int premdn = SLAPI_PLUGIN_PRE_MODRDN_FN;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_init\n");

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        plugin_is_betxn = 1;
        preadd = SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN;
        premod = SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN;
        predel = SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN;
        premdn = SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN;
    }
    slapi_ch_free_string(&plugin_type);

    /* Store the plugin identity for later use.
     * Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    mep_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *) mep_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *) mep_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, premod, (void *) mep_mod_pre_op) != 0 ||
        slapi_pblock_set(pb, preadd, (void *) mep_add_pre_op) != 0 ||
        slapi_pblock_set(pb, predel, (void *) mep_del_pre_op) != 0 ||
        slapi_pblock_set(pb, premdn, (void *) mep_modrdn_pre_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_init: failed to register plugin\n");
        status = -1;
    }

    if (!status && !plugin_is_betxn &&
        slapi_register_plugin("internalpostoperation",  /* op type */
                              1,        /* Enabled */
                              "mep_init",   /* this function desc */
                              mep_internal_postop_init,  /* init func */
                              MEP_INT_POSTOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              plugin_identity   /* access control */
        )) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_init: failed to register internalpostoperation plugin\n");
        status = -1;
    }

    if (!status) {
        plugin_type = plugin_is_betxn ? "betxnpostoperation" : "postoperation";
        if (slapi_register_plugin(plugin_type,  /* op type */
                                  1,        /* Enabled */
                                  "mep_init",   /* this function desc */
                                  mep_postop_init,  /* init func for post op */
                                  MEP_POSTOP_DESC,      /* plugin desc */
                                  NULL,     /* ? */
                                  plugin_identity   /* access control */
        )) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_init: failed to register plugin\n");
            status = -1;
        }
    }

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_init\n");
    return status;
}

/* not used when using plugin as a betxn plugin - betxn plugins are called for both internal and external ops */
static int
mep_internal_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *) mep_add_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *) mep_del_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *) mep_mod_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *) mep_modrdn_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_internal_postop_init: failed to register plugin\n");
        status = -1;
    }
 
    return status;
}

static int
mep_postop_init(Slapi_PBlock *pb)
{
    int status = 0;
    int addfn = SLAPI_PLUGIN_POST_ADD_FN;
    int delfn = SLAPI_PLUGIN_POST_DELETE_FN;
    int modfn = SLAPI_PLUGIN_POST_MODIFY_FN;
    int mdnfn = SLAPI_PLUGIN_POST_MODRDN_FN;

    if (plugin_is_betxn) {
        addfn = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
        delfn = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
        modfn = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
        mdnfn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
    }

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, addfn, (void *) mep_add_post_op) != 0 ||
        slapi_pblock_set(pb, delfn, (void *) mep_del_post_op) != 0 ||
        slapi_pblock_set(pb, modfn, (void *) mep_mod_post_op) != 0 ||
        slapi_pblock_set(pb, mdnfn, (void *) mep_modrdn_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_postop_init: failed to register plugin\n");
        status = -1;
    }

    return status;
}


/*
 * mep_start()
 *
 * Creates config lock and loads config cache.
 */
static int
mep_start(Slapi_PBlock * pb)
{
    Slapi_DN *plugindn = NULL;
    char *config_area = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_start\n");

    /* Check if we're already started */
    if (g_plugin_started) {
        goto done;
    }

    g_mep_config_lock = slapi_new_rwlock();

    if (!g_mep_config_lock) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_start: lock creation failed\n");

        return -1;
    }

    /*
     * Get the plug-in target dn from the system
     * and store it for future use. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &plugindn);
    if (NULL == plugindn || 0 == slapi_sdn_get_ndn_len(plugindn)) {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_start: unable to retrieve plugin dn\n");
        return -1;
    }

    mep_set_plugin_sdn(slapi_sdn_dup(plugindn));

    /* Set the alternate config area if one is defined. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_AREA, &config_area);
    if (config_area) {
        mep_set_config_area(slapi_sdn_new_normdn_byval(config_area));
    }

    /*
     * Load the config cache
     */
    g_mep_config = (PRCList *)slapi_ch_calloc(1, sizeof(struct configEntry));
    PR_INIT_CLIST(g_mep_config);

    if (mep_load_config() != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_start: unable to load plug-in configuration\n");
        return -1;
    }

    g_plugin_started = 1;
    slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "managed entries plug-in: ready for service\n");
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_start\n");

done:
    return 0;
}

/*
 * mep_close()
 *
 * Cleans up the config cache.
 */
static int
mep_close(Slapi_PBlock * pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_close\n");

    if (!g_plugin_started) {
        goto done;
    }

    mep_config_write_lock();
    g_plugin_started = 0;
    mep_delete_config();
    mep_config_unlock();

    slapi_ch_free((void **)&g_mep_config);
    slapi_sdn_free(&_PluginDN);
    slapi_sdn_free(&_ConfigAreaDN);

    /* We explicitly don't destroy the config lock here.  If we did,
     * there is the slight possibility that another thread that just
     * passed the g_plugin_started check is about to try to obtain
     * a reader lock.  We leave the lock around so these threads
     * don't crash the process.  If we always check the started
     * flag again after obtaining a reader lock, no free'd resources
     * will be used. */

done:
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_close\n");

    return 0;
}

PRCList *
mep_get_config()
{
    return g_mep_config;
}

/* 
 * config looks like this
 * - cn=Managed Entries,cn=plugins,cn=config
 * --- cn=user private groups,...
 * --- cn=etc,...
 */
static int
mep_load_config()
{
    int status = 0;
    int result;
    int i;
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_load_config\n");

    /* Clear out any old config. */
    mep_config_write_lock();
    mep_delete_config();

    search_pb = slapi_pblock_new();

    /* If an alternate config area is configured, find
     * the config entries that are beneath it, otherwise
     * we load the entries beneath our top-level plug-in
     * config entry. */
    if (mep_get_config_area()) {
        /* Find the config entries beneath the alternate config area. */
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_load_config: Looking for config entries "
                        "beneath \"%s\".\n", slapi_sdn_get_ndn(mep_get_config_area()));
        slapi_search_internal_set_pb(search_pb, slapi_sdn_get_ndn(mep_get_config_area()),
                                     LDAP_SCOPE_SUBTREE, "objectclass=*",
                                     NULL, 0, NULL, NULL, mep_get_plugin_id(), 0);
    } else {
        /* Find the config entries beneath our plugin entry. */
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_load_config: Looking for config entries "
                        "beneath \"%s\".\n",
                         slapi_sdn_get_ndn(mep_get_plugin_sdn()));
        slapi_search_internal_set_pb(search_pb,
                                     slapi_sdn_get_ndn(mep_get_plugin_sdn()),
                                     LDAP_SCOPE_SUBTREE, "objectclass=*",
                                     NULL, 0, NULL, NULL, mep_get_plugin_id(), 0);
    }

    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    if (LDAP_SUCCESS != result) {
        if (mep_get_config_area() && (result == LDAP_NO_SUCH_OBJECT)) {
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                            "mep_load_config: Config container \"%s\" does "
                            "not exist.\n", slapi_sdn_get_ndn(mep_get_config_area()));
            goto cleanup;
        } else {
            status = -1;
            goto cleanup;
        }
    }

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &entries);

    /* Loop through all of the entries we found and parse them. */
    for (i = 0; entries && (entries[i] != NULL); i++) {
        /* We don't care about the status here because we may have
         * some invalid config entries, but we just want to continue
         * looking for valid ones. */
        mep_parse_config_entry(entries[i], 1);
    }

  cleanup:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    mep_config_unlock();
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_load_config\n");

    return status;
}

/*
 * mep_parse_config_entry()
 *
 * Parses a single config entry.  If apply is non-zero, then
 * we will load and start using the new config.  You can simply
 * validate config without making any changes by setting apply
 * to 0.
 *
 * Returns 0 if the entry is valid and -1 if it is invalid.
 */
static int
mep_parse_config_entry(Slapi_Entry * e, int apply)
{
    char *value;
    struct configEntry *entry = NULL;
    struct configEntry *config_entry;
    PRCList *list;
    int entry_added = 0;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_parse_config_entry\n");

    /* If this is the main plug-in
     * config entry, just bail. */
    if ((slapi_sdn_compare(mep_get_plugin_sdn(), slapi_entry_get_sdn(e)) == 0) ||
        (mep_get_config_area() && (slapi_sdn_compare(mep_get_config_area(),
        slapi_entry_get_sdn(e)) == 0))) {
        ret = -1;
        goto bail;
    }

    entry = (struct configEntry *)slapi_ch_calloc(1, sizeof(struct configEntry));
    if (NULL == entry) {
        ret = -1;
        goto bail;
    }

    entry->sdn = slapi_sdn_dup(slapi_entry_get_sdn(e)); 
    if(entry->sdn == NULL){ 
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_config_entry: Error "
                        "reading dn from config entry\n");
        ret = -1;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, MEP_PLUGIN_SUBSYSTEM,
                    "----------> dn [%s]\n", slapi_sdn_get_dn(entry->sdn));

    /* Load the origin scope */
    value = slapi_entry_attr_get_charptr(e, MEP_SCOPE_TYPE);
    if (value) {
        entry->origin_scope = value;
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        MEP_SCOPE_TYPE, slapi_sdn_get_dn(entry->sdn));
        ret = -1;
        goto bail;
    }

    /* Load the origin filter */
    value = slapi_entry_attr_get_charptr(e, MEP_FILTER_TYPE);
    if (value) {
        /* Convert to a Slapi_Filter to improve performance. */
        if (NULL == (entry->origin_filter = slapi_str2filter(value))) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM ,
                "mep_parse_config_entry: Invalid search filter in "
                "%s config setting for config entry \"%s\" "
                "(filter = \"%s\").\n", MEP_FILTER_TYPE, slapi_sdn_get_dn(entry->sdn), value);
            ret = -1;
        }

        slapi_ch_free_string(&value);

        if (ret != 0) {
            goto bail;
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        MEP_FILTER_TYPE, slapi_sdn_get_dn(entry->sdn));
        ret = -1;
        goto bail;
    }

    /* Load the managed base */
    value = slapi_entry_attr_get_charptr(e, MEP_MANAGED_BASE_TYPE);
    if (value) {
        entry->managed_base = value; 
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        MEP_MANAGED_BASE_TYPE, slapi_sdn_get_dn(entry->sdn));
        ret = -1;
        goto bail;
    }

    /* Load the managed template */
    value = slapi_entry_attr_get_charptr(e, MEP_MANAGED_TEMPLATE_TYPE);
    if (value) {
        Slapi_Entry *test_entry = NULL;

        entry->template_sdn = slapi_sdn_new_dn_passin(value);
        value = NULL; /* entry->template_sdn owns value now */

        /*  Fetch the managed entry template */
        slapi_search_internal_get_entry(entry->template_sdn, 0,
                &entry->template_entry, mep_get_plugin_id());

        if (entry->template_entry == NULL) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_parse_config_entry: The managed entry "
                            "template \"%s\" does not exist.  Please "
                            "add it or correct the %s config setting for "
                            "config entry \"%s\"\n", slapi_sdn_get_dn(entry->template_sdn),
                            MEP_MANAGED_TEMPLATE_TYPE, slapi_sdn_get_dn(entry->sdn));
            ret = -1;
            goto bail;
        }

        /* Validate the template entry by creating a test managed
         * entry and running a schema check on it */
        test_entry = mep_create_managed_entry(entry, NULL);
        if (test_entry == NULL) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_parse_config_entry: Unable to create "
                            "a test managed entry from managed entry "
                            "template \"%s\".  Please check the template "
                            "entry for errors.\n", slapi_sdn_get_dn(entry->template_sdn));
            ret = -1;
            goto bail;
        }

        /* Check the schema */
        if (slapi_entry_schema_check(NULL, test_entry) != 0) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_parse_config_entry: Test managed "
                            "entry created from managed entry template "
                            "\"%s\" violates the schema.  Please check "
                            "the template entry for schema errors.\n",
                            slapi_sdn_get_dn(entry->template_sdn));
            slapi_entry_free(test_entry);
            ret = -1;
            goto bail;
        }

        /*
         * Extract the origin attrs from the template entry
         */
        entry->origin_attrs = mep_extract_origin_attrs(entry->template_entry);

        /* Dispose of the test entry */
        slapi_entry_free(test_entry);

    } else {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        MEP_MANAGED_TEMPLATE_TYPE, slapi_sdn_get_dn(entry->sdn));
        ret = -1;
        goto bail;
    }

    /* If we were only called to validate config, we can
     * just bail out before applying the config changes */
    if (apply == 0) {
        goto bail;
    }

    /* Add the config object to the list.  We order by scope. */
    if (!PR_CLIST_IS_EMPTY(g_mep_config)) {
        list = PR_LIST_HEAD(g_mep_config);
        while (list != g_mep_config) {
            config_entry = (struct configEntry *) list;

            /* If the config entry we are adding has a scope that is
             * a child of the scope of the current list item, we insert
             * the entry before that list item. */
            if (slapi_dn_issuffix(entry->origin_scope, config_entry->origin_scope)) {
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, MEP_PLUGIN_SUBSYSTEM,
                                "store [%s] before [%s] \n", slapi_sdn_get_dn(entry->sdn),
                                slapi_sdn_get_dn(config_entry->sdn));

                entry_added = 1;
                break;
            }

            list = PR_NEXT_LINK(list);

            /* If we hit the end of the list, add to the tail. */
            if (g_mep_config == list) {
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, MEP_PLUGIN_SUBSYSTEM,
                                "store [%s] at tail\n", slapi_sdn_get_dn(entry->sdn));

                entry_added = 1;
                break;
            }
        }
    } else {
        /* first entry */
        PR_INSERT_LINK(&(entry->list), g_mep_config);
        slapi_log_error(SLAPI_LOG_CONFIG, MEP_PLUGIN_SUBSYSTEM,
                        "store [%s] at head \n", slapi_sdn_get_dn(entry->sdn));

        entry_added = 1;
    }

  bail:
    if (0 == entry_added) {
        /* Don't log error if we weren't asked to apply config */
        if ((apply != 0) && (entry != NULL)) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_parse_config_entry: Invalid config entry "
                            "[%s] skipped\n", slapi_sdn_get_dn(entry->sdn));
        }
        mep_free_config_entry(&entry);
    } else {
        ret = 0;
    }

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_parse_config_entry\n");

    return ret;
}

static void
mep_free_config_entry(struct configEntry ** entry)
{
    struct configEntry *e = *entry;

    if (e == NULL)
        return;

    if (e->sdn) {
        slapi_log_error(SLAPI_LOG_CONFIG, MEP_PLUGIN_SUBSYSTEM,
                        "freeing config entry [%s]\n", slapi_sdn_get_dn(e->sdn));
        slapi_sdn_free(&e->sdn);
    }

    if (e->origin_scope) {
        slapi_ch_free_string(&e->origin_scope);
    }

    if (e->origin_filter) {
        slapi_filter_free(e->origin_filter, 1);
    }

    if (e->managed_base) {
        slapi_ch_free_string(&e->managed_base);
    }

    if (e->template_sdn) {
        slapi_sdn_free(&e->template_sdn);
    }

    if (e->template_entry) {
        slapi_entry_free(e->template_entry);
    }

    if(e->origin_attrs){
        slapi_ch_array_free(e->origin_attrs);
    }

    slapi_ch_free((void **) entry);
}

static void
mep_delete_configEntry(PRCList *entry)
{
    PR_REMOVE_LINK(entry);
    mep_free_config_entry((struct configEntry **) &entry);
}

static void
mep_delete_config()
{
    PRCList *list;

    /* Delete the config cache. */
    while (!PR_CLIST_IS_EMPTY(g_mep_config)) {
        list = PR_LIST_HEAD(g_mep_config);
        mep_delete_configEntry(list);
    }

    return;
}


/*
 * Helper functions
 */

/*
 * mep_parse_mapped_origin_attr()
 *
 * Parses a mapped attribute setting from a template and
 * grabs the attribute name and places it in origin_type.
 *
 * This is used to determine if a modify operation needs
 * to update the managed entry.
 */
static int
mep_parse_mapped_origin_attr(char *mapping, char **origin_type)
{
    int ret = 0;
    char *p = NULL;
    char *end = NULL;
    char *var_start = NULL;

    /* reset the pointer for origin_type as this func is usually in a loop */
    *origin_type = NULL;

    /* split out the type from the value (use the first ':') */
    if ((p = strchr(mapping, ':')) == NULL) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_mapped_origin_attr: Value for mapped attribute "
                        "is not in the correct format. (value: \"%s\").\n",
                        mapping);
        ret = 1;
        goto bail;
    }

    /* Ensure the type is not empty. */
    if (p == mapping) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_mapped_origin_attr: Value for mapped attribute "
                        "is not in the correct format. The type is missing. "
                        "(value: \"%s\").\n",
                        mapping);
        ret = 1;
        goto bail;
    }

    /* Terminate the type so we can use it as a string. */
    *p = '\0';

    /* Advance p to point to the beginning of the value. */
    p++;
    while (*p == ' ') {
        p++;
    }

    /* Make end point to the last character that we want in the value. */
    end = p + strlen(p) - 1;

    /* Find the variable that we need to substitute. */
    for (; p <= end; p++) {
        if (*p == '$') {
            if (p == end) {
                slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                "mep_parse_mapped_origin_attr: Invalid mapped "
                                "attribute value for type \"%s\".\n", mapping);
                ret = 1;
                goto bail;
            }

            if (*(p + 1) == '$') {
                /* This is an escaped $.  Eliminate the escape character
                 * to prevent if from being a part of the value. */
                p++;
                memmove(p, p+1, end-(p+1)+1);
                *end = '\0';
                end--;
            } else {
                int quoted = 0;

                /* We found a variable.  Terminate the pre
                 * string and process the variable. */
                *p = '\0';
                p++;

                /* Check if the variable name is quoted.  If it is, we skip past
                 * the quoting brace to avoid putting it in the mapped value. */
                if (*p == '{') {
                    quoted = 1;
                    if (p < end) {
                        p++;
                    } else {
                        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                        "mep_parse_mapped_origin_attr: Invalid mapped "
                                        "attribute value for type \"%s\".\n", mapping);
                        ret = 1;
                        goto bail;
                    }
                }

                /* We should be pointing at the variable name now. */
                var_start = p;

                /* Move the pointer to the end of the variable name.  We
                 * stop at the first character that is not legal for use
                 * in an attribute description. */
                while ((p < end) && IS_ATTRDESC_CHAR(*p)) {
                    p++;
                }

                /* If the variable is quoted and this is not a closing
                 * brace, there is a syntax error in the mapping rule. */
                if (quoted && (*p != '}')) {
                        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                        "mep_parse_mapped_origin_attr: Invalid mapped "
                                        "attribute value for type \"%s\".\n", mapping);
                        ret = 1;
                        goto bail;
                }

                /* Check for a missing variable name. */
                if (p == var_start) {
                    break;
                }

                if (p == end) {
                    /* Set the map type.  In this case, p could be
                     * pointing at either the last character of the
                     * map type, or at the first character after the
                     * map type.  If the character is valid for use
                     * in an attribute description, we consider it
                     * to be a part of the map type. */
                    if (IS_ATTRDESC_CHAR(*p)) {
                        *origin_type = strndup(var_start, p - var_start + 1);
                        /* There is no post string. */
                    } else {
                        *origin_type = strndup(var_start, p - var_start);
                    }
                } else {
                    /* Set the map type.  In this case, p is pointing
                     * at the first character after the map type. */
                    *origin_type = strndup(var_start, p - var_start);
                }

                /* We only support a single variable, so we're done. */
                break;
            }
        }
    }

  bail:
    if (ret != 0) {
        slapi_ch_free_string(origin_type);
    }

    return ret;
}

/*
 *  mep_extract_origin_attrs
 *
 *  Extract the attributes from the template that reside on
 *  the origin entry that trigger updates to the managed entry.
 */
static char **
mep_extract_origin_attrs(Slapi_Entry *template)
{
    char **vals = NULL;
    char *origin_type = NULL;
    char **origin_attrs = NULL;
    int i;

    if (template) {
        vals = slapi_entry_attr_get_charray(template, MEP_MAPPED_ATTR_TYPE);
        for (i = 0; vals && vals[i]; ++i) {
            if (mep_parse_mapped_origin_attr(vals[i], &origin_type) == 0) {
                slapi_ch_array_add(&origin_attrs,origin_type);
            }
        }
        slapi_ch_array_free(vals);
    }

    return origin_attrs;
}

/*
 * mep_is_mapped_origin_attr()
 *
 * Checks if type is a mapped origin attribute.
 */
static int
mep_is_mapped_origin_attr(char **vals, char *type)
{
    int ret = 0;
    int i;

    if (type) {
        for (i = 0; vals && vals[i]; ++i) {
            if (slapi_attr_type_cmp(vals[i], type, SLAPI_TYPE_CMP_EXACT) == 0) {
                /* Ok, we are modifying a attribute that affects the managed entry */
                ret = 1;
                break;
            }
        }
    }

    return ret;
}

static Slapi_DN *
mep_get_sdn(Slapi_PBlock * pb)
{
    Slapi_DN *sdn = 0;
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_get_sdn\n");
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_get_sdn\n");

    return sdn;
}

static void
mep_set_config_area(Slapi_DN *sdn)
{
    _ConfigAreaDN = sdn;
}

static Slapi_DN *
mep_get_config_area()
{
    return _ConfigAreaDN;
}

/*
 * mep_dn_is_config()
 *
 * Checks if dn is a managed entries config entry.
 */
static int
mep_dn_is_config(Slapi_DN *sdn)
{
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_dn_is_config\n");

    if (sdn == NULL) {
        goto bail;
    }

    /* If an alternate config area is configured, treat it's child
     * entries as config entries.  If the alternate config area is
     * not configured, treat children of the top-level plug-in
     * config entry as our config entries. */
    if (mep_get_config_area()) {
        if (slapi_sdn_issuffix(sdn, mep_get_config_area()) &&
            slapi_sdn_compare(sdn, mep_get_config_area())) {
            ret = 1;
        }
    } else {
        if (slapi_sdn_issuffix(sdn, mep_get_plugin_sdn()) &&
            slapi_sdn_compare(sdn, mep_get_plugin_sdn())) {
            ret = 1;
        }
    }

bail:
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_dn_is_config\n");

    return ret;
}

/*
 * mep_dn_is_template()
 *
 * Checks if dn is a managed entries template.
 */
static int
mep_dn_is_template(Slapi_DN *sdn)
{
    int ret = 0;
    PRCList *list = NULL;
    Slapi_DN *config_sdn = NULL;

    if (!PR_CLIST_IS_EMPTY(g_mep_config)) {
        list = PR_LIST_HEAD(g_mep_config);
        while (list != g_mep_config) {
            config_sdn = ((struct configEntry *)list)->template_sdn;

            if (slapi_sdn_compare(config_sdn, sdn) == 0) {
                ret = 1;
                break;
            } else {
                list = PR_NEXT_LINK(list);
            }
        }
    }

    return ret;
}

/*
 * mep_find_config()
 *
 * Finds the appropriate config entry for a given entry
 * by checking if the entry meets the origin scope and
 * filter requirements of any config entry.  A read lock
 * must be held on the config before calling this function.
 * The configEntry that is returned is a pointer to the
 * actual config entry in the config cache.  It should not
 * be modified in any way.  The read lock should not be
 * released until you are finished with the config entry
 * that is returned.
 *
 * Returns NULL if no applicable config entry is found.
 */
static void
mep_find_config(Slapi_Entry *e, struct configEntry **config)
{
    PRCList *list = NULL;
    char *dn = NULL;

    *config = NULL;

    if (e && !PR_CLIST_IS_EMPTY(g_mep_config)) {
        dn = slapi_entry_get_dn(e);

        list = PR_LIST_HEAD(g_mep_config);
        while (list != g_mep_config) {

            /* See if the dn is within the scope of this config entry
             * in addition to matching the origin filter. */
            if (slapi_dn_issuffix(dn, ((struct configEntry *)list)->origin_scope) &&
                (slapi_filter_test_simple(e, ((struct configEntry *)list)->origin_filter) == 0)) {
                *config = (struct configEntry *)list;
                break;
            }

            list = PR_NEXT_LINK(list);
        }
    }
}

/*
 * mep_find_config_by_template_dn()
 *
 * Finds the config entry associated with a particular
 * template dn.  A read lock must be held on the config
 * before calling this function.  The configEntry that
 * us returned is a pointer to the actual config entry
 * in the config cache.  It should not be modified in
 * any way.  The read lock should not be released until
 * you are finished with the config entry that is returned.
 *
 * Returns NULL if no applicable config entry is found.
 */
static void
mep_find_config_by_template_dn(Slapi_DN *template_sdn,
    struct configEntry **config)
{
    PRCList *list = NULL;
    Slapi_DN *config_sdn = NULL;

    *config = NULL;

    if (!PR_CLIST_IS_EMPTY(g_mep_config)) {
        list = PR_LIST_HEAD(g_mep_config);
        while (list != g_mep_config) {
            config_sdn = ((struct configEntry *)list)->template_sdn;

            if (slapi_sdn_compare(config_sdn, template_sdn) == 0) {
                *config = (struct configEntry *)list;
                break;
            } else {
                list = PR_NEXT_LINK(list);
            }
        }
    }
}

/*
 * mep_oktodo()
 *
 * Check if we want to process this operation.  We need to be
 * sure that the operation succeeded.
 */
static int
mep_oktodo(Slapi_PBlock *pb)
{
    int ret = 1;
    int oprc = 0;

    slapi_log_error( SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                 "--> mep_oktodo\n" );

    if(slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                "mep_oktodo: could not get parameters\n" );
        ret = -1;
    }

    /* This plugin should only execute if the operation succeeded. */
    if(oprc != 0) {
        ret = 0;
    }

    slapi_log_error( SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                 "<-- mep_oktodo\n" );

    return ret;
}

/*
 * mep_isrepl()
 *
 * Returns 1 if the operation associated with pb
 * is a replicated op.  Returns 0 otherwise.
 */
static int
mep_isrepl(Slapi_PBlock *pb)
{
    int is_repl = 0;

    slapi_log_error( SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                 "--> mep_isrepl\n" );

    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_repl);

    slapi_log_error( SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                 "<-- mep_isrepl\n" );

    return is_repl;
}

/*
 * mep_create_managed_entry()
 *
 * Creates a managed entry from a specified template and origin
 * entry.  If an origin entry is not passed in, the values of
 * mapped attributes will not be filled in and the addition of
 * a backlink to the origin is not added.  This is useful for
 * creating a test managed entry for template validation.
 */
static Slapi_Entry *
mep_create_managed_entry(struct configEntry *config, Slapi_Entry *origin)
{
    Slapi_Entry *managed_entry = NULL;
    Slapi_Entry *template = NULL;
    char *rdn_type = NULL;
    char **vals = NULL;
    char *type = NULL;
    char *value = NULL;
    Slapi_Value *sval = NULL;
    int found_rdn_map = 0;
    int i = 0;
    int err = 0;

    /* If no template was supplied, there's nothing we can do. */
    if ((config == NULL) || (config->template_entry == NULL)) {
        return NULL;
    } else {
        template = config->template_entry;
    }

    /* Ensure that a RDN type was specified in the template. */
    if ((rdn_type = slapi_entry_attr_get_charptr(template, MEP_RDN_ATTR_TYPE)) == NULL) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_create_managed_entry: The %s config attribute "
                    "was not found in template \"%s\".  This attribute "
                    "is required.\n", MEP_RDN_ATTR_TYPE, slapi_sdn_get_dn(config->template_sdn));
        err = 1;
        goto done;
    }

    /* Create the entry to be returned. */
    managed_entry = slapi_entry_alloc();
    slapi_entry_init(managed_entry, NULL, NULL);

    /* Add all of the static attributes from the template to the newly
     * created managed entry. */
    vals = slapi_entry_attr_get_charray(template, MEP_STATIC_ATTR_TYPE);
    for (i = 0; vals && vals[i]; ++i) {
        struct berval bvtype = {0, NULL}, bvvalue = {0, NULL};
        int freeval = 0;
        if (slapi_ldif_parse_line(vals[i], &bvtype, &bvvalue, &freeval) != 0) {
            slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_create_managed_entry: Value for %s config setting  "
                        "is not in the correct format in template \"%s\". "
                        "(value: \"%s\")\n", MEP_STATIC_ATTR_TYPE,
                        slapi_sdn_get_dn(config->template_sdn), vals[i]);
            err = 1;
            goto done;
        } else {
            /* Create a berval and add the value to the entry. */
            sval = slapi_value_new_berval(&bvvalue);
            slapi_entry_add_value(managed_entry, bvtype.bv_val, sval);
            slapi_value_free(&sval);

            /* Set type and value to NULL so they don't get
             * free'd by mep_parse_mapped_attr(). */
            if (freeval) {
                slapi_ch_free_string(&bvvalue.bv_val);
            }
            type = NULL;
            value = NULL;
        }
    }

    /* Clear out vals so we can use them again */
    slapi_ch_array_free(vals);

    /* Add the mapped attributes to the newly created managed entry. */
    vals = slapi_entry_attr_get_charray(template, MEP_MAPPED_ATTR_TYPE);
    for (i = 0; vals && vals[i]; ++i) {
        if (mep_parse_mapped_attr(vals[i], origin, &type, &value) == 0) {
            /* Add the attribute to the managed entry. */
            slapi_entry_add_string(managed_entry, type, value);

            /* Check if this type is the RDN type. */
            if (slapi_attr_type_cmp(type, rdn_type, SLAPI_TYPE_CMP_EXACT) == 0) {
                found_rdn_map = 1;
            }

            slapi_ch_free_string(&type);
            slapi_ch_free_string(&value);
        } else {
            slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_create_managed_entry: Error parsing mapped attribute "
                    "in template \"%s\".\n", slapi_sdn_get_dn(config->template_sdn));
            err = 1;
            goto done;
        }
    }

    /* The RDN attribute must be a mapped attribute.  If we didn't find it,
     * we need to bail. */
    if (!found_rdn_map) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_create_managed_entry: The RDN type \"%s\" "
                        "was not found as a mapped attribute in template "
                        "\"%s\".  It must be a mapped attribute.\n",
                        rdn_type, slapi_sdn_get_dn(config->template_sdn));
        err = 1;
        goto done;
    } else {
        /* Build the DN and set it in the entry. */
        char *dn = NULL;
        char *rdn_val = NULL;

        /* If an origin entry was supplied, the RDN value will be
         * the mapped value.  If no origin entry was supplied, the
         * value will be the mapping rule from the template. */
        rdn_val = slapi_entry_attr_get_charptr(managed_entry, rdn_type);

        /* Create the DN using the mapped RDN value
         * and the base specified in the config. */
        dn = slapi_ch_smprintf("%s=%s,%s", rdn_type, rdn_val, config->managed_base);

        slapi_ch_free_string(&rdn_val);

        if (dn != NULL) {
            slapi_sdn_set_dn_passin(slapi_entry_get_sdn(managed_entry), dn);
        } else {
            slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_create_managed_entry: Error setting DN "
                            "in managed entry based off of template entry "
                            "\"%s\" (origin entry \"%s\").\n",
                            slapi_sdn_get_dn(config->template_sdn),
                            origin ? slapi_entry_get_dn(origin) : "NULL");
            err = 1;
            goto done;
        }
    }

    /* If an origin entry was supplied, set a backpointer to the
     * origin in the managed entry. */
    if (origin) {
        slapi_entry_add_string(managed_entry,
                SLAPI_ATTR_OBJECTCLASS, MEP_MANAGED_OC);
        slapi_entry_add_string(managed_entry, MEP_MANAGED_BY_TYPE,
                slapi_entry_get_dn(origin));
    }

  done:
    slapi_ch_array_free(vals);
    slapi_ch_free_string(&rdn_type);

    if (err != 0) {
        slapi_entry_free(managed_entry);
        managed_entry = NULL;
    }

    return managed_entry;
}

/*
 * mep_add_managed_entry()
 *
 * Creates and adds a managed entry to the database.  The
 * origin entry will also be modified to add a link to the
 * newly created managed entry.
 */
static void
mep_add_managed_entry(struct configEntry *config,
    Slapi_Entry *origin)
{
    Slapi_Entry *managed_entry = NULL;
    char *managed_dn = NULL;
    Slapi_PBlock *mod_pb = slapi_pblock_new();
    int result = LDAP_SUCCESS;

    /* Create the managed entry */
    slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "mep_add_managed_entry: Creating a managed "
                    "entry from origin entry \"%s\" using "
                    "config \"%s\".\n", slapi_entry_get_dn(origin),
                    slapi_sdn_get_dn(config->sdn));
    managed_entry = mep_create_managed_entry(config, origin);
    if (managed_entry == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_add_managed_entry: Unable to create a managed "
                    "entry from origin entry \"%s\" using config "
                    "\"%s\".\n", slapi_entry_get_dn(origin), slapi_sdn_get_dn(config->sdn));
    } else {
        /* Copy the managed entry DN to use when
         * creating the pointer attribute. */
        managed_dn = slapi_ch_strdup(slapi_entry_get_dn(managed_entry));

        /* Add managed entry to db.  The entry will be consumed. */
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "Adding managed entry \"%s\" for origin "
                    "entry \"%s\"\n.", managed_dn, slapi_entry_get_dn(origin));
        slapi_add_entry_internal_set_pb(mod_pb, managed_entry, NULL,
                                        mep_get_plugin_id(), 0);
        slapi_add_internal_pb(mod_pb);
        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

        if (result != LDAP_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_add_managed_entry: Unable to add managed "
                        "entry \"%s\" for origin entry \"%s\" (%s).\n",
                        managed_dn, slapi_entry_get_dn(origin),
                        ldap_err2string(result));
        } else {
            /* Add forward link to origin entry. */
            LDAPMod oc_mod;
            LDAPMod pointer_mod;
            LDAPMod *mods[3];
            char *oc_vals[2];
            char *pointer_vals[2];

            /* Clear out the pblock for reuse. */
            slapi_pblock_init(mod_pb);

            /* Add the origin entry objectclass. */
            oc_vals[0] = MEP_ORIGIN_OC;
            oc_vals[1] = 0;
            oc_mod.mod_op = LDAP_MOD_ADD;
            oc_mod.mod_type = SLAPI_ATTR_OBJECTCLASS;
            oc_mod.mod_values = oc_vals;

            /* Add a pointer to the managed entry. */
            pointer_vals[0] = managed_dn;
            pointer_vals[1] = 0;
            pointer_mod.mod_op = LDAP_MOD_ADD;
            pointer_mod.mod_type = MEP_MANAGED_ENTRY_TYPE;
            pointer_mod.mod_values = pointer_vals;

            mods[0] = &oc_mod;
            mods[1] = &pointer_mod;
            mods[2] = 0;

            /* Perform the modify operation. */
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "Adding %s pointer to \"%s\" in entry \"%s\"\n.",
                    MEP_MANAGED_ENTRY_TYPE, managed_dn, slapi_entry_get_dn(origin));
            slapi_modify_internal_set_pb_ext(mod_pb, 
                                            slapi_entry_get_sdn(origin),
                                            mods, 0, 0, mep_get_plugin_id(), 0);
            slapi_modify_internal_pb(mod_pb);
            slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

            if (result != LDAP_SUCCESS) {
                slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_add_managed_entry: Unable to add pointer to "
                            "managed entry \"%s\" in origin entry \"%s\" "
                            "(%s).\n", managed_dn, slapi_entry_get_dn(origin),
                            ldap_err2string(result));
            }
        }
    }

    slapi_ch_free_string(&managed_dn);
    slapi_pblock_destroy(mod_pb);
}

/* mep_rename_managed_entry()
 *
 * Renames a managed entry and updates the pointer in the
 * origin entry.
 */
static void
mep_rename_managed_entry(Slapi_Entry *origin,
                         Slapi_DN *new_dn, Slapi_DN *old_dn)
{
    Slapi_RDN *srdn = slapi_rdn_new();
    Slapi_PBlock *mep_pb = slapi_pblock_new();
    LDAPMod mod;
    LDAPMod *mods[2];
    char *vals[2];
    int result = LDAP_SUCCESS;

    /* Just bail if any of our parameters are NULL. */
    if (origin == NULL || new_dn == NULL || old_dn == NULL) {
        goto bail;
    }

    /* Create new RDN */
    slapi_rdn_set_dn(srdn, slapi_sdn_get_ndn(new_dn));

    /* Rename the managed entry. */
    slapi_rename_internal_set_pb_ext(mep_pb, old_dn,
                                 slapi_rdn_get_rdn(srdn),
                                 NULL, 1, NULL, NULL, mep_get_plugin_id(), 0);
    slapi_modrdn_internal_pb(mep_pb);
    slapi_pblock_get(mep_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    if (result != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_rename_managed_entry: Unable to rename managed "
                    "entry \"%s\" to \"%s\" (%s).\n", slapi_sdn_get_dn(old_dn),
                    slapi_sdn_get_dn(new_dn), ldap_err2string(result));
    } else {
        /* Clear out the pblock for reuse. */
        slapi_pblock_init(mep_pb);

        /* Update the link to the managed entry in the origin entry. */
        vals[0] = (char *)slapi_sdn_get_dn(new_dn);
        vals[1] = 0;
        mod.mod_op = LDAP_MOD_REPLACE;
        mod.mod_type = MEP_MANAGED_ENTRY_TYPE;
        mod.mod_values = vals;
        mods[0] = &mod;
        mods[1] = 0;

        /* Perform the modify operation. */
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                "mep_rename_managed_entry: Updating %s pointer to "
                "\"%s\" in entry \"%s\"\n.", MEP_MANAGED_ENTRY_TYPE,
                vals[0], slapi_entry_get_dn(origin));
        slapi_modify_internal_set_pb_ext(mep_pb, slapi_entry_get_sdn(origin),
                                         mods, 0, 0, mep_get_plugin_id(), 0);
        slapi_modify_internal_pb(mep_pb);
        slapi_pblock_get(mep_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

        if (result != LDAP_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_rename_managed_entry: Unable to update %s "
                    "pointer in entry \"%s\" (%s).\n", MEP_MANAGED_ENTRY_TYPE,
                    slapi_entry_get_dn(origin), ldap_err2string(result));
        }
    }

bail:
    slapi_rdn_free(&srdn);
    slapi_pblock_destroy(mep_pb);
}

/*
 * mep_get_mapped_mods()
 *
 * Creates the modifications needed to update the mapped values
 * in a managed entry.  It is up to the caller to free the
 * returned mods when it is finished using them.
 */
static Slapi_Mods *mep_get_mapped_mods(struct configEntry *config,
    Slapi_Entry *origin, char **mapped_dn)
{
    Slapi_Mods *smods = NULL;
    Slapi_Entry *template = NULL;
    Slapi_Attr *attr = NULL;
    char **vals = NULL;
    char *type = NULL;
    char *value = NULL;
    char *rdn_type = NULL;
    int i = 0;

    /* If no template was supplied, there's nothing we can do. */
    if (origin == NULL || config == NULL || config->template_entry == NULL) {
        return NULL;
    } else {
        template = config->template_entry;
    }

    /* See how many mods we will have and initialize the smods. */
    if (slapi_entry_attr_find(config->template_entry, MEP_MAPPED_ATTR_TYPE, &attr) == 0) {
        int numvals = 0;

        slapi_attr_get_numvalues(attr, &numvals);
        smods = slapi_mods_new();
        slapi_mods_init(smods, numvals);
    }

    /* Find the the RDN type for the managed entry. */
    if ((rdn_type = slapi_entry_attr_get_charptr(template, MEP_RDN_ATTR_TYPE)) == NULL) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                "mep_get_mapped_mods: Error getting RDN type from tempate "
                "\"%s\".\n", slapi_sdn_get_dn(config->template_sdn));
        slapi_mods_free(&smods);
        goto bail;
    }

    /* Go through the template and find the mapped attrs. */
    vals = slapi_entry_attr_get_charray(template, MEP_MAPPED_ATTR_TYPE);
    for (i = 0; vals && vals[i]; ++i) {
        if (mep_parse_mapped_attr(vals[i], origin, &type, &value) == 0) {
            /* Don't attempt to modify the RDN type, but create
             * the mapped DN if requested.  It is up to the caller
             * to free the returned DN. */
            if (slapi_attr_type_cmp(type, rdn_type, SLAPI_TYPE_CMP_EXACT) == 0) {
                if (mapped_dn) {
                    *mapped_dn = slapi_create_dn_string("%s=%s,%s", rdn_type,
                            value, config->managed_base);
                }
            } else {
                /* Add a modify that replaces all values with the new value. */
                slapi_mods_add_string(smods, LDAP_MOD_REPLACE, type, value);
            }
            slapi_ch_free_string(&type);
            slapi_ch_free_string(&value);
        } else {
            slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_get_mapped_mods: Error parsing mapped attribute "
                    "in template \"%s\".\n", slapi_sdn_get_dn(config->template_sdn));
            slapi_mods_free(&smods);
            goto bail;
        }
    }

  bail:
    slapi_ch_free_string(&rdn_type);
    slapi_ch_array_free(vals);

    return smods;
}

/*
 * mep_parse_mapped_attr()
 *
 * Parses a mapped attribute setting from a template and
 * fills in the type and value based off of the origin
 * entry.  If an origin entry is not supplied, the value
 * is simply the mapping rule.
 */
static int
mep_parse_mapped_attr(char *mapping, Slapi_Entry *origin,
    char **type, char **value)
{
    int ret = 0;
    char *p = NULL;
    char *pre_str = NULL;
    char *post_str = NULL;
    char *end = NULL;
    char *var_start = NULL;
    char *map_type = NULL;

    /* Clear out any existing type or value. */
    slapi_ch_free_string(type);
    slapi_ch_free_string(value);

    /* split out the type from the value (use the first ':') */
    if ((p = strchr(mapping, ':')) == NULL) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_mapped_attr: Value for mapped attribute "
                        "is not in the correct format. (value: \"%s\").\n", 
                        mapping);
        ret = 1;
        goto bail;
    }

    /* Ensure the type is not empty. */
    if (p == mapping) {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_parse_mapped_attr: Value for mapped attribute "
                        "is not in the correct format. The type is missing. "
                        "(value: \"%s\").\n",
                        mapping);
        ret = 1;
        goto bail;
    }

    /* Terminate the type so we can use it as a string. */
    *p = '\0';

    /* Duplicate the type to be returned. */
    *type = slapi_ch_strdup(mapping);

    /* Advance p to point to the beginning of the value. */
    p++;
    while (*p == ' ') {
        p++;
    }

    pre_str = p;

    /* Make end point to the last character that we want in the value. */
    end = p + strlen(p) - 1;

    /* Find the variable that we need to substitute. */
    for (; p <= end; p++) {
        if (*p == '$') {
            if (p == end) {
                slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                "mep_parse_mapped_attr: Invalid mapped "
                                "attribute value for type \"%s\".\n", mapping);
                ret = 1;
                goto bail;
            }

            if (*(p + 1) == '$') {
                /* This is an escaped $.  Eliminate the escape character
                 * to prevent if from being a part of the value. */
                p++;
                memmove(p, p+1, end-(p+1)+1);
                *end = '\0';
                end--;
            } else {
                int quoted = 0;

                /* We found a variable.  Terminate the pre
                 * string and process the variable. */
                *p = '\0';
                p++;

                /* Check if the variable name is quoted.  If it is, we skip past
                 * the quoting brace to avoid putting it in the mapped value. */
                if (*p == '{') {
                    quoted = 1;
                    if (p < end) {
                        p++;
                    } else {
                        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                        "mep_parse_mapped_attr: Invalid mapped "
                                        "attribute value for type \"%s\".\n", mapping);
                        ret = 1;
                        goto bail;
                    }
                }

                /* We should be pointing at the variable name now. */
                var_start = p;

                /* Move the pointer to the end of the variable name.  We
                 * stop at the first character that is not legal for use
                 * in an attribute description. */
                while ((p < end) && IS_ATTRDESC_CHAR(*p)) {
                    p++;
                }

                /* If the variable is quoted and this is not a closing
                 * brace, there is a syntax error in the mapping rule. */
                if (quoted && (*p != '}')) {
                        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                        "mep_parse_mapped_attr: Invalid mapped "
                                        "attribute value for type \"%s\".\n", mapping);
                        ret = 1;
                        goto bail;
                }

                /* Check for a missing variable name. */
                if (p == var_start) {
                    break;
                }

                if (p == end) {
                    /* Set the map type.  In this case, p could be
                     * pointing at either the last character of the
                     * map type, or at the first character after the
                     * map type.  If the character is valid for use
                     * in an attribute description, we consider it
                     * to be a part of the map type. */
                    if (IS_ATTRDESC_CHAR(*p)) {
                        map_type = strndup(var_start, p - var_start + 1);
                        /* There is no post string. */
                        post_str = NULL;
                    } else {
                        map_type = strndup(var_start, p - var_start);
                        post_str = p;
                    }
                } else {
                    /* Set the map type.  In this case, p is pointing
                     * at the first character after the map type. */
                    map_type = strndup(var_start, p - var_start);

                    /* If the variable is quoted, don't include
                     * the closing brace in the post string. */
                    if (quoted) {
                        post_str = p+1;
                    } else {
                        post_str = p;
                    }
                }

                /* Process the post string to remove any escapes. */
                for (p = post_str; p && (p <= end); p++) {
                    if (*p == '$') {
                        if ((p == end) || (*(p+1) != '$')) {
                            slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                        "mep_parse_mapped_attr: Invalid mapped "
                                        "attribute value for type \"%s\".\n", mapping);
                            ret = 1;
                            goto bail;
                        } else {
                            /* This is an escaped '$'.  Remove the escape char. */
                            p++;
                            memmove(p, p+1, end-(p+1)+1);
                            *end = '\0';
                            end--;
                        }
                    }
                }

                /* We only support a single variable, so we're done. */
                break;
            }
        }
    }

    if (map_type) {
        if (origin) {
            char *map_val = NULL;
            int freeit = 0;

            /* If the map type is dn, fetch the origin dn. */
            if (slapi_attr_type_cmp(map_type, "dn", SLAPI_TYPE_CMP_EXACT) == 0) {
                map_val = slapi_entry_get_ndn(origin);
            } else {
                map_val = slapi_entry_attr_get_charptr(origin, map_type);
                freeit = 1;
            }

            if (map_val) {
                /* Create the new mapped value. */
                *value = slapi_ch_smprintf("%s%s%s", pre_str,
                                           map_val, post_str ? post_str : "");
                if (freeit) {
                    slapi_ch_free_string(&map_val);
                }
            } else {
                slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                    "mep_parse_mapped_attr: Mapped attribute \"%s\" "
                    "is not present in origin entry \"%s\".  Please "
                    "correct template to only map attributes "
                    "required by the schema.\n", map_type,
                    slapi_entry_get_dn(origin));
                ret = 1;
                goto bail;
            }
        } else {
            /* Just use the mapping since we have no origin entry. */
            *value = slapi_ch_smprintf("%s$%s%s", pre_str, map_type,
                                      post_str);
        }
    } else {
        slapi_log_error( SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                "mep_parse_mapped_attr: No variable found in "
                "mapped attribute value for type \"%s\".\n",
                mapping);
        ret = 1;
        goto bail;
    }

  bail:
    slapi_ch_free_string(&map_type);

    if (ret != 0) {
        slapi_ch_free_string(type);
        slapi_ch_free_string(value);
    }

    return ret;
}


/*
 * mep_is_managed_entry()
 *
 * Returns 1 if the entry is a managed entry, 0 otherwise.
 * The check is performed by seeing if the managed entry
 * objectclass is present.
 */
static int
mep_is_managed_entry(Slapi_Entry *e)
{
    int ret = 0;
    Slapi_Attr *attr = NULL;
    struct berval bv;

    bv.bv_val = MEP_MANAGED_OC;
    bv.bv_len = strlen(bv.bv_val);

    if (e && (slapi_entry_attr_find(e, SLAPI_ATTR_OBJECTCLASS, &attr) == 0)) {
        if (slapi_attr_value_find(attr, &bv) == 0) {
            ret = 1;
        }
    }

    return ret;
}

/*
 * mep_is_mapped_attr()
 *
 * Checks if type is defined as a mapped attribute in template.
 */
static int
mep_is_mapped_attr(Slapi_Entry *template, char *type)
{
    int ret = 0;
    int i = 0;
    char **vals = NULL;
    char *map_type = NULL;
    char *value = NULL;

    if (template && type) {
        vals = slapi_entry_attr_get_charray(template, MEP_MAPPED_ATTR_TYPE);
        for (i = 0; vals && vals[i]; ++i) {
            if (mep_parse_mapped_attr(vals[i], NULL, &map_type, &value) == 0) {
                if (slapi_attr_type_cmp(map_type, type, SLAPI_TYPE_CMP_EXACT) == 0) {
                    ret = 1;
                }

                slapi_ch_free_string(&map_type);
                slapi_ch_free_string(&value);

                /* If we found a match, we're done. */
                if (ret == 1) {
                    break;
                }
            }
        }

        slapi_ch_array_free(vals);
    }

    return ret;
}

/*
 * Operation callback functions
 */

/*
 * mep_pre_op()
 *
 * Checks if an operation is modifying the managed 
 * entries config and validates the config changes.
 */
static int
mep_pre_op(Slapi_PBlock * pb, int modop)
{
    Slapi_DN *sdn = NULL;
    Slapi_Entry *e = 0;
    Slapi_Mods *smods = 0;
    LDAPMod **mods;
    int free_entry = 0;
    char *errstr = NULL;
    struct configEntry *config = NULL;
    void *caller_id = NULL;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_pre_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started)
        goto bail;

    /* See if we're calling ourselves. */
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &caller_id);

    if (0 == (sdn = mep_get_sdn(pb)))
        goto bail;

    if (mep_dn_is_config(sdn)) {
        /* Validate config changes, but don't apply them.
         * This allows us to reject invalid config changes
         * here at the pre-op stage.  Applying the config
         * needs to be done at the post-op stage. */

        if (LDAP_CHANGETYPE_ADD == modop) {
            slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);

        } else if (LDAP_CHANGETYPE_MODIFY == modop) {
            /* Fetch the entry being modified so we can
             * create the resulting entry for validation. */
            if (sdn) {
                slapi_search_internal_get_entry(sdn, 0, &e, mep_get_plugin_id());
                free_entry = 1;
            }

            /* If the entry doesn't exist, just bail and
             * let the server handle it. */
            if (e == NULL) {
                goto bail;
            }

            /* Grab the mods. */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            smods = slapi_mods_new();
            slapi_mods_init_byref(smods, mods);

            /* Apply the  mods to create the resulting entry. */
            if (mods && (slapi_entry_apply_mods(e, mods) != LDAP_SUCCESS)) {
                /* The mods don't apply cleanly, so we just let this op go
                 * to let the main server handle it. */
                goto bailmod;
            }
        } else if (LDAP_CHANGETYPE_DELETE == modop){
            /* we allow for deletes, so goto bail to we can skip the config parsing */
            goto bail; 
        } else {
            /* Refuse other operations. */
            ret = LDAP_UNWILLING_TO_PERFORM;
            errstr = slapi_ch_smprintf("Not a valid operation.");
            goto bail;
        }

        if (mep_parse_config_entry(e, 0) != 0) {
            /* Refuse the operation if config parsing failed. */
            ret = LDAP_UNWILLING_TO_PERFORM;
            if (LDAP_CHANGETYPE_ADD == modop) {
                errstr = slapi_ch_smprintf("Not a valid managed entries configuration entry.");
            } else {
                errstr = slapi_ch_smprintf("Changes result in an invalid "
                                           "managed entries configuration.");
            }
        }
    } else {
        /* Check if an active template entry is being updated.  If so, validate it. */
        mep_config_read_lock();

        /* Bail out if the plug-in close function was just called. */
        if (!g_plugin_started) {
            mep_config_unlock();
            goto bail;
        }

        mep_find_config_by_template_dn(sdn, &config);
        if (config) {
            Slapi_Entry *test_entry = NULL;
            struct configEntry *config_copy = NULL;

            config_copy = (struct configEntry *)slapi_ch_calloc(1, sizeof(struct configEntry));

            /* Make a temporary copy of the config to use for validation. */
            config_copy->sdn = slapi_sdn_dup(config->sdn);
            config_copy->managed_base = slapi_ch_strdup(config->managed_base);
            config_copy->template_sdn = slapi_sdn_dup(config->template_sdn);

            /* Reject attempts to delete or rename an active template.
             * Validate changes to an active template. */
            switch (modop) {
            case LDAP_CHANGETYPE_DELETE:
                errstr = slapi_ch_smprintf("Deleting an active managed "
                                           "entries template is not allowed. "
                                           "Delete the associated config "
                                           "entry first.");
                ret = LDAP_UNWILLING_TO_PERFORM;
                break;
            case LDAP_CHANGETYPE_MODDN:
                errstr = slapi_ch_smprintf("Renaming an active managed "
                                           "entries template is not allowed. "
                                           "Create a new template and modify "
                                           "the associated config entry instead.");
                ret = LDAP_UNWILLING_TO_PERFORM;
                break;
            case LDAP_CHANGETYPE_MODIFY: 
                /* Fetch the existing template entry. */
                if (sdn) {
                    slapi_search_internal_get_entry(sdn, 0, &e, mep_get_plugin_id());
                    free_entry = 1;
                }

                /* If the entry doesn't exist, we just skip
                 * validation and let the server handle it. */
                if (e) {
                    /* Grab the mods. */
                    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
                    smods = slapi_mods_new();
                    slapi_mods_init_byref(smods, mods);

                    /* Apply the  mods to create the resulting entry. */
                    if (mods && (slapi_entry_apply_mods(e, mods) == LDAP_SUCCESS)) {
                        /* Set the resulting template in the config copy.
                         * The ownership of the resulting entry is handed
                         * over to the config copy. */
                        config_copy->template_entry = e;
                        e = NULL;

                        /* Validate the changed template. */
                        test_entry = mep_create_managed_entry(config_copy, NULL);
                        if (test_entry == NULL) {
                            errstr = slapi_ch_smprintf("Changes result in an invalid "
                                                       "managed entries template.");
                            ret = LDAP_UNWILLING_TO_PERFORM;
                        } else if (slapi_entry_schema_check(NULL, test_entry) != 0) {
                            errstr = slapi_ch_smprintf("Changes result in an invalid "
                                                       "managed entries template due "
                                                       "to a schema violation.");
                            ret = LDAP_UNWILLING_TO_PERFORM;
                        }
                    }
                }

                /* Dispose of the test entry */
                slapi_entry_free(test_entry);
                break;
            }

            /* Free the config copy */
            mep_free_config_entry(&config_copy);
        }
        mep_config_unlock();

        /* If replication, just bail. */
        if (mep_isrepl(pb)) {
            goto bailmod;
        }

        /* Check if a managed entry is being deleted or
         * renamed and reject if it's not being done by
         * this plugin. */
        if (((modop == LDAP_CHANGETYPE_DELETE) || (modop == LDAP_CHANGETYPE_MODDN) ||
            (modop == LDAP_CHANGETYPE_MODIFY)) && (caller_id != mep_get_plugin_id())) {
            Slapi_Entry *origin_e = NULL;
            Slapi_Mod *smod = NULL;
            Slapi_Mod *next_mod = NULL;
            char *origin_dn = NULL;
            Slapi_DN *origin_sdn = NULL;

            /* Fetch the target entry. */
            if (sdn) {
                /* Free any existing entry so we don't leak it. */
                if (e && free_entry) {
                    slapi_entry_free(e);
                }

                slapi_search_internal_get_entry(sdn, 0, &e, mep_get_plugin_id());
                free_entry = 1;
            }

            if (e && mep_is_managed_entry(e)) {
                if (modop == LDAP_CHANGETYPE_MODIFY) {
                    /* Fetch the origin entry so we can locate the config template. */
                    origin_dn = slapi_entry_attr_get_charptr(e, MEP_MANAGED_BY_TYPE);
                    if (origin_dn) {
                        origin_sdn = slapi_sdn_new_normdn_byref(origin_dn);
                        slapi_search_internal_get_entry(origin_sdn, 0,
                                &origin_e, mep_get_plugin_id());
                        slapi_sdn_free(&origin_sdn);
                    }

                    if (origin_e) {
                        /* Fetch the config. */
                        mep_config_read_lock();

                        /* Bail out if the plug-in close function was just called. */
                        if (!g_plugin_started) {
                            mep_config_unlock();
                            goto bail;
                        }

                        mep_find_config(origin_e, &config);

                        if (config) {
                            /* Get the mods if we haven't already. */
                            if (smods == NULL) {
                                slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
                                smods = slapi_mods_new();
                                slapi_mods_init_byref(smods, mods);
                            }

                            next_mod = slapi_mod_new();
                            smod = slapi_mods_get_first_smod(smods, next_mod);
                            while(smod) {
                                char *type = (char *)slapi_mod_get_type(smod);

                                /* If this is a mapped attribute, reject the op. */
                                if (mep_is_mapped_attr(config->template_entry, type)) {
                                    errstr = slapi_ch_smprintf("Modifying a mapped attribute "
                                                       " in a managed entry is not allowed. "
                                                       "The \"%s\" attribute is mapped for "
                                                       "this entry.", type);
                                    ret = LDAP_UNWILLING_TO_PERFORM;
                                    break;
                                }

                                slapi_mod_done(next_mod);
                                smod = slapi_mods_get_next_smod(smods, next_mod);
                            }

                            slapi_mod_free(&next_mod);
                        } else {
                            slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                        "mep_pre_op: Unable to fetch config for "
                                        "origin entry \"%s\".\n", origin_dn);
                        }

                        slapi_entry_free(origin_e);
                        mep_config_unlock();
                    } else {
                        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                    "mep_pre_op: Unable to fetch origin entry "
                                    "\"%s\".\n", origin_dn);
                    }

                    slapi_ch_free_string(&origin_dn);
                } else {
                    errstr = slapi_ch_smprintf("%s a managed entry is not allowed. "
                                           "It needs to be manually unlinked first.",
                                           modop == LDAP_CHANGETYPE_DELETE ? "Deleting"
                                           : "Renaming");
                    ret = LDAP_UNWILLING_TO_PERFORM;
                }
            }
        }
    }

  bailmod:
    /* Clean up smods. */
    if (LDAP_CHANGETYPE_MODIFY == modop) {
        slapi_mods_free(&smods);
    }

  bail:
    if (free_entry && e)
        slapi_entry_free(e);

    if (ret) {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_pre_op: operation failure [%d]\n", ret);
        slapi_send_ldap_result(pb, ret, NULL, errstr, 0, NULL);
        slapi_ch_free((void **)&errstr);
        ret = -1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_pre_op\n");

    return ret;
}

static int
mep_add_pre_op(Slapi_PBlock * pb)
{
    return mep_pre_op(pb, LDAP_CHANGETYPE_ADD);
}

static int
mep_del_pre_op(Slapi_PBlock * pb)
{
    return mep_pre_op(pb, LDAP_CHANGETYPE_DELETE);
}

static int
mep_mod_pre_op(Slapi_PBlock * pb)
{
    return mep_pre_op(pb, LDAP_CHANGETYPE_MODIFY);
}

static int
mep_modrdn_pre_op(Slapi_PBlock * pb)
{
    return mep_pre_op(pb, LDAP_CHANGETYPE_MODDN);
}

static int
mep_mod_post_op(Slapi_PBlock *pb)
{
    Slapi_Mods *smods = NULL;
    Slapi_PBlock *mep_pb = NULL;
    Slapi_Entry *e = NULL;
    Slapi_DN *sdn = NULL;
    char *managed_dn = NULL;
    Slapi_DN *managed_sdn = NULL;
    char *mapped_dn = NULL;
    Slapi_DN *mapped_sdn = NULL;
    struct configEntry *config = NULL;
    int result = 0;
    LDAPMod	**mods = NULL;
    int i, abort_mod = 1;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_mod_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started)
        return 0;

    if (mep_oktodo(pb) && (sdn = mep_get_sdn(pb))) {
        /* First check if the config or a template is being modified. */
        if (mep_dn_is_config(sdn) || mep_dn_is_template(sdn)) {
            mep_load_config();
        }

        /* If replication, just bail. */
        if (mep_isrepl(pb)) {
            goto bail;
        }

        /* Fetch the modified entry.  This will not be set for a chaining
         * backend, so don't treat the message as fatal. */
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
        if (e == NULL) {
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                            "mep_mod_post_op: Unable to fetch postop entry.\n");
            goto bail;
        }

        /* If the entry is a tombstone, just bail. */
        if (mep_has_tombstone_value(e)) {
            goto bail;
        }

        /* Check if we're an origin entry.  Update the
         * mapped attrs of it's managed entry if so. */
        managed_dn = slapi_entry_attr_get_charptr(e, MEP_MANAGED_ENTRY_TYPE);
        if (managed_dn) {
            mep_config_read_lock();

            /* Bail out if the plug-in close function was just called. */
            if (!g_plugin_started) {
                mep_config_unlock();
                goto bail;
            }

            mep_find_config(e, &config);
            if (config) {
                /*
                 *  Check to see if the applied mods are mapped origin attributes.
                 *  If they are not, then we don't need to modify the mapped entry
                 *  as it has not changed.
                 */
                slapi_pblock_get (pb, SLAPI_MODIFY_MODS, &mods);
                for(i = 0; mods && mods[i]; i++){
                    if(mep_is_mapped_origin_attr(config->origin_attrs,mods[i]->mod_type)){
                        /*
                         * We are modifying a managed origin attr, so we can proceed with
                         * modifying the managed entry.  Otherwise we would modify the
                         * managed entry for no reason.
                         */    
                        abort_mod = 0;
                        break;
                    }
                }

                if(abort_mod){
                    mep_config_unlock();
                    goto bail;
                }

                smods = mep_get_mapped_mods(config, e, &mapped_dn);
                if (smods) {
                    /* Clear out the pblock for reuse. */
                    mep_pb = slapi_pblock_new();

                    /* Perform the modify operation. */
                    slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                            "mep_mod_post_op: Updating mapped attributes "
                            "in entry \"%s\"\n.", managed_dn);
                    slapi_modify_internal_set_pb(mep_pb, managed_dn,
                                    slapi_mods_get_ldapmods_byref(smods), 0, 0,
                                    mep_get_plugin_id(), 0);
                    slapi_modify_internal_pb(mep_pb);
                    slapi_pblock_get(mep_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

                    if (result != LDAP_SUCCESS) {
                        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                    "mep_mod_post_op: Unable to update mapped "
                                    "attributes from origin entry \"%s\" in managed "
                                    "entry \"%s\" (%s).\n", slapi_sdn_get_dn(sdn), managed_dn,
                                    ldap_err2string(result));
                    }

                    slapi_mods_free(&smods);
                    slapi_pblock_destroy(mep_pb);
                }

                /* Check if we need to rename the managed entry. */
                if (mapped_dn) {
                    mapped_sdn = slapi_sdn_new_normdn_passin(mapped_dn);
                    managed_sdn = slapi_sdn_new_normdn_byref(managed_dn);

                    if (slapi_sdn_compare(managed_sdn, mapped_sdn) != 0) {
                        mep_rename_managed_entry(e, mapped_sdn, managed_sdn);
                    }

                    slapi_sdn_free(&mapped_sdn);
                    slapi_sdn_free(&managed_sdn);
                }
            } else {
                slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_mod_post_op: Unable to find config for origin "
                        "entry \"%s\".\n", slapi_sdn_get_dn(sdn));
            }
            mep_config_unlock();
        }
    }

  bail:
    slapi_ch_free_string(&managed_dn);
    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_mod_post_op\n");

    return 0;
}

static int
mep_add_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    Slapi_DN *sdn = NULL;
    struct configEntry *config = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_add_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !mep_oktodo(pb))
        return 0;

    /* Reload config if a config entry was added. */
    if ((sdn = mep_get_sdn(pb))) {
        if (mep_dn_is_config(sdn)) {
            mep_load_config();
        }
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_add_post_op: Error "
                        "retrieving dn\n");
    }

    /* If replication, just bail. */
    if (mep_isrepl(pb)) {
        return 0;
    }

    /* Get the newly added entry. */
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);

    if (e) {
        /* If the entry is a tombstone, just bail. */
        if (mep_has_tombstone_value(e)) {
            return 0;
        }

        /* Check if a config entry applies
         * to the entry being added. */
        mep_config_read_lock();

        /* Bail out if the plug-in close function was just called. */
        if (!g_plugin_started) {
            mep_config_unlock();
            return 0;
        }

        mep_find_config(e, &config);
        if (config) {
            mep_add_managed_entry(config, e);
        }

        mep_config_unlock();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_add_post_op: Error "
                        "retrieving post-op entry %s\n", slapi_sdn_get_dn(sdn));
    }

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_add_post_op\n");

    return 0;
}

static int
mep_del_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    Slapi_DN *sdn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_del_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !mep_oktodo(pb)) {
        return 0;
    }

    /* Reload config if a config entry was deleted. */
    if ((sdn = mep_get_sdn(pb))) {
        if (mep_dn_is_config(sdn))
            mep_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_del_post_op: Error "
                        "retrieving dn\n");
    }

    /* If replication, just bail. */
    if (mep_isrepl(pb)) {
        return 0;
    }

    /* Get deleted entry, then go through types to find config. */
    slapi_pblock_get( pb, SLAPI_ENTRY_PRE_OP, &e );

    if (e) {
        char *managed_dn = NULL;

        /* If the entry is a tombstone, just bail. */
        if (mep_has_tombstone_value(e)) {
            return 0;
        }

        /* See if we're an origin entry . */
        managed_dn = slapi_entry_attr_get_charptr(e, MEP_MANAGED_ENTRY_TYPE);
        if (managed_dn) {
            Slapi_PBlock *mep_pb = slapi_pblock_new();

            /* Delete the managed entry. */
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                            "mep_del_post_op: Deleting managed entry "
                            "\"%s\" due to deletion of origin entry "
                            "\"%s\".\n ", managed_dn, slapi_sdn_get_dn(sdn));
            slapi_delete_internal_set_pb(mep_pb, managed_dn, NULL,
                                         NULL, mep_get_plugin_id(), 0);
            slapi_delete_internal_pb(mep_pb);

            slapi_ch_free_string(&managed_dn);
            slapi_pblock_destroy(mep_pb);
        }
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_del_post_op: Error "
                        "retrieving pre-op entry %s\n", slapi_sdn_get_dn(sdn));
    }

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_del_post_op\n");

    return 0;
}

static int
mep_modrdn_post_op(Slapi_PBlock *pb)
{
    const char *new_dn = NULL;
    Slapi_DN *old_sdn = NULL;
    Slapi_DN *new_sdn = NULL;
    Slapi_Entry *post_e = NULL;
    char *managed_dn = NULL;
    struct configEntry *config = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "--> mep_modrdn_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !mep_oktodo(pb))
        return 0;;

    /* Reload config if an existing config entry was renamed,
     * or if the new dn brings an entry into the scope of the
     * config entries. */
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_e);
    if (post_e) {
        new_sdn = slapi_entry_get_sdn(post_e);
        new_dn = slapi_sdn_get_dn(new_sdn);
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_modrdn_post_op: Error "
                        "retrieving post-op entry\n");
        return 0;
    }

    if ((old_sdn = mep_get_sdn(pb))) {
        if (mep_dn_is_config(old_sdn) || mep_dn_is_config(new_sdn))
            mep_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                        "mep_modrdn_post_op: Error "
                        "retrieving dn\n");
    }

    /* If replication, just bail. */
    if (mep_isrepl(pb)) {
        return 0;
    }

    /* If the entry is a tombstone, just bail. */
    if (mep_has_tombstone_value(post_e)) {
        return 0;
    }

    /* See if we're an origin entry . */
    managed_dn = slapi_entry_attr_get_charptr(post_e, MEP_MANAGED_ENTRY_TYPE);
    if (managed_dn) {
        LDAPMod mod;
        LDAPMod *mods[3];
        char *vals[2];
        int result = LDAP_SUCCESS;
        Slapi_PBlock *mep_pb = slapi_pblock_new();
        Slapi_Entry *new_managed_entry = NULL;
        Slapi_DN *managed_sdn = NULL;
        Slapi_Mods *smods = NULL;
        int free_managed_dn = 1;

        mep_config_read_lock();

        /* Bail out if the plug-in close function was just called. */
        if (!g_plugin_started) {
            mep_config_unlock();
            slapi_pblock_destroy(mep_pb);
            return 0;
        }

        mep_find_config(post_e, &config);
        if (!config) {
            LDAPMod mod2;
            char *vals2[2];

            /* Delete the associated managed entry. */
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "mep_modrdn_post_op: Removing managed entry \"%s\" "
                    "since origin entry \"%s\" was moved out of scope.\n",
                    managed_dn, slapi_sdn_get_dn(old_sdn));
            slapi_delete_internal_set_pb (mep_pb, managed_dn, NULL, NULL,
                                          mep_get_plugin_id(), 0);
            slapi_delete_internal_pb(mep_pb);

            /* Clear out the pblock for reuse. */
            slapi_pblock_init(mep_pb);

            /* Remove the pointer from the origin entry. */
            vals[0] = 0;
            mod.mod_op = LDAP_MOD_DELETE;
            mod.mod_type = MEP_MANAGED_ENTRY_TYPE;
            mod.mod_values = vals;

            /* Remove the origin objectclass. */
            vals2[0] = MEP_ORIGIN_OC;
            vals2[1] = 0;
            mod2.mod_op = LDAP_MOD_DELETE;
            mod2.mod_type = SLAPI_ATTR_OBJECTCLASS;
            mod2.mod_values = vals2;

            mods[0] = &mod;
            mods[1] = &mod2;
            mods[2] = 0;

            /* Perform the modify operation. */
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "mep_modrdn_post_op: Removing %s pointer and %s "
                    "objectclass from entry \"%s\".\n",
                    MEP_MANAGED_ENTRY_TYPE, MEP_ORIGIN_OC, new_dn);
            slapi_modify_internal_set_pb_ext(mep_pb, new_sdn, mods, 0, 0,
                                             mep_get_plugin_id(), 0);
            slapi_modify_internal_pb(mep_pb);
            slapi_pblock_get(mep_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

            if (result != LDAP_SUCCESS) {
                slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                "mep_modrdn_post_op: Unable to remove %s "
                                "pointer and %s objectclass from entry "
                                "\"%s\".\n", MEP_MANAGED_ENTRY_TYPE,
                                MEP_ORIGIN_OC, new_dn);
            }
        } else {
            /* Update backlink to new origin DN in managed entry. */
            vals[0] = (char *)new_dn;
            vals[1] = 0;
            mod.mod_op = LDAP_MOD_REPLACE;
            mod.mod_type = MEP_MANAGED_BY_TYPE;
            mod.mod_values = vals;

            mods[0] = &mod;
            mods[1] = 0;

            /* Create a new managed entry to determine what changes
             * we need to make to the existing managed entry. */
            new_managed_entry = mep_create_managed_entry(config, post_e);
            if (new_managed_entry == NULL) {
                slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                        "mep_modrdn_post_op: Unable to create in-memory "
                        "managed entry from origin entry \"%s\".\n", new_dn);
                goto bailmod;
            }

            /* Check if the managed entry exists.  It is possible that
             * it has already been renamed by another plug-in.  If it
             * has already been renamed, we need to use the new DN to
             * perform our updates. */
            managed_sdn = slapi_sdn_new_normdn_byref(managed_dn);

            if (slapi_search_internal_get_entry(managed_sdn, 0,
                    NULL, mep_get_plugin_id()) == LDAP_NO_SUCH_OBJECT) {
                slapi_ch_free_string(&managed_dn);
                /* This DN is not a copy, so we don't want to free it later. */
                managed_dn = slapi_entry_get_dn(new_managed_entry);
                slapi_sdn_set_normdn_byref(managed_sdn, managed_dn);
                free_managed_dn = 0;
            }

            /* Perform the modify operation. */
            slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                    "mep_modrdn_post_op: Updating %s pointer to \"%s\" "
                    "in entry \"%s\".\n", MEP_MANAGED_BY_TYPE, new_dn, managed_dn);
            slapi_modify_internal_set_pb(mep_pb, managed_dn, mods, 0, 0,
                                         mep_get_plugin_id(), 0);
            slapi_modify_internal_pb(mep_pb);
            slapi_pblock_get(mep_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

            if (result != LDAP_SUCCESS) {
                slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                            "mep_modrdn_post_op: Unable to update pointer to "
                            "origin entry \"%s\" in managed entry \"%s\" "
                            "(%s).\n", new_dn, managed_dn, ldap_err2string(result));
            } else {
                /* See if we need to rename the managed entry. */
                if (slapi_sdn_compare(slapi_entry_get_sdn(new_managed_entry), managed_sdn) != 0) {
                    /* Rename the managed entry. */
                    slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                                    "mep_modrdn_post_op: Renaming managed entry "
                                    "\"%s\" to \"%s\" due to rename of origin "
                                    "entry \"%s\".\n ", managed_dn,
                                    slapi_entry_get_dn(new_managed_entry),
                                    slapi_sdn_get_dn(old_sdn));
                    mep_rename_managed_entry(post_e,
                                    slapi_entry_get_sdn(new_managed_entry),
                                    managed_sdn);
                }

                /* Update all of the mapped attributes
                 * to be sure they are up to date. */
                smods = mep_get_mapped_mods(config, post_e, NULL);
                if (smods) {
                    /* Clear out the pblock for reuse. */
                    slapi_pblock_init(mep_pb);

                    /* Perform the modify operation. */
                    slapi_log_error(SLAPI_LOG_PLUGIN, MEP_PLUGIN_SUBSYSTEM,
                            "mep_modrdn_post_op: Updating mapped attributes "
                            "in entry \"%s\"\n.", managed_dn);
                    slapi_modify_internal_set_pb_ext(mep_pb,
                                    slapi_entry_get_sdn(new_managed_entry),
                                    slapi_mods_get_ldapmods_byref(smods), 0, 0,
                                    mep_get_plugin_id(), 0);
                    slapi_modify_internal_pb(mep_pb);
                    slapi_pblock_get(mep_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

                    if (result != LDAP_SUCCESS) {
                        slapi_log_error(SLAPI_LOG_FATAL, MEP_PLUGIN_SUBSYSTEM,
                                    "mep_modrdn_post_op: Unable to update mapped "
                                    "attributes from origin entry \"%s\" in managed "
                                    "entry \"%s\" (%s).\n", new_dn,
                                    slapi_entry_get_dn(new_managed_entry),
                                    ldap_err2string(result));
                    }

                    slapi_mods_free(&smods);
                }

            }
bailmod:
            slapi_entry_free(new_managed_entry);
            slapi_sdn_free(&managed_sdn);
        }

        slapi_pblock_destroy(mep_pb);

        if (free_managed_dn) {
            slapi_ch_free_string(&managed_dn);
        }

        mep_config_unlock();
    } else {
        /* Was this entry moved into scope of a config entry?
         * If so, treat like an add and create the new managed
         * entry and links. */
        mep_config_read_lock();

        /* Bail out if the plug-in close function was just called. */
        if (!g_plugin_started) {
            mep_config_unlock();
            return 0;
        }

        mep_find_config(post_e, &config);
        if (config) {
            mep_add_managed_entry(config, post_e);
        }

        mep_config_unlock();
    }

    slapi_log_error(SLAPI_LOG_TRACE, MEP_PLUGIN_SUBSYSTEM,
                    "<-- mep_modrdn_post_op\n");

    return 0;
}

static int
mep_has_tombstone_value(Slapi_Entry * e)
{
    Slapi_Value *tombstone = slapi_value_new_string(SLAPI_ATTR_VALUE_TOMBSTONE);
    int rc = slapi_entry_attr_has_syntax_value(e, SLAPI_ATTR_OBJECTCLASS,
                                               tombstone);
    slapi_value_free(&tombstone);
    return rc;
}
