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
 * Copyright (C) 2011 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Auto Membership Plug-in
 */
#include "automember.h"


/*
 * Plug-in globals
 */
static PRCList *g_automember_config = NULL;
static PRRWLock *g_automember_config_lock;

static void *_PluginID = NULL;
static Slapi_DN *_PluginDN = NULL;
static Slapi_DN *_ConfigAreaDN = NULL;
static int g_plugin_started = 0;

static Slapi_PluginDesc pdesc = { AUTOMEMBER_FEATURE_DESC,
                                  VENDOR,
                                  DS_PACKAGE_VERSION,
                                  AUTOMEMBER_PLUGIN_DESC };

/*
 * Plug-in management functions
 */
int automember_init(Slapi_PBlock * pb);
static int automember_start(Slapi_PBlock * pb);
static int automember_close(Slapi_PBlock * pb);
static int automember_postop_init(Slapi_PBlock * pb);
static int automember_internal_postop_init(Slapi_PBlock *pb);

/*
 * Operation callbacks (where the real work is done)
 */
static int automember_mod_post_op(Slapi_PBlock *pb);
static int automember_add_post_op(Slapi_PBlock *pb);
static int automember_del_post_op(Slapi_PBlock *pb);
static int automember_modrdn_post_op(Slapi_PBlock *pb);
static int automember_pre_op(Slapi_PBlock *pb, int modop);
static int automember_mod_pre_op(Slapi_PBlock *pb);
static int automember_add_pre_op(Slapi_PBlock *pb);

/*
 * Config cache management functions
 */
static int automember_load_config();
static void automember_delete_config();
static int automember_parse_config_entry(Slapi_Entry * e, int apply);
static void automember_free_config_entry(struct configEntry ** entry);

/*
 * helpers
 */
static char *automember_get_dn(Slapi_PBlock * pb);
static Slapi_DN *automember_get_config_area();
static void automember_set_config_area(Slapi_DN *sdn);
static int automember_dn_is_config(char *dn);
static int automember_oktodo(Slapi_PBlock *pb);
static int automember_isrepl(Slapi_PBlock *pb);
static void automember_parse_regex_entry(struct configEntry *config, Slapi_Entry *e);
static struct automemberRegexRule *automember_parse_regex_rule(char *rule_string);
static void automember_free_regex_rule(struct automemberRegexRule *rule);
static int automember_parse_grouping_attr(char *value, char **grouping_attr,
    char **grouping_value);
static void automember_update_membership(struct configEntry *config, Slapi_Entry *e);
static void automember_add_member_value(Slapi_Entry *member_e, const char *group_dn,
    char *grouping_attr, char *grouping_value);

/*
 * Config cache locking functions
 */
void
automember_config_read_lock()
{
    PR_RWLock_Rlock(g_automember_config_lock);
}

void
automember_config_write_lock()
{
    PR_RWLock_Wlock(g_automember_config_lock);
}

void
automember_config_unlock()
{
    PR_RWLock_Unlock(g_automember_config_lock);
}


/*
 * Plugin identity functions
 */
void
automember_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
automember_get_plugin_id()
{
    return _PluginID;
}

void
automember_set_plugin_sdn(Slapi_DN *pluginDN)
{
    _PluginDN = pluginDN;
}

Slapi_DN *
automember_get_plugin_sdn()
{
    return _PluginDN;
}


/*
 * Plug-in initialization functions
 */
int
automember_init(Slapi_PBlock *pb)
{
    int status = 0;
    char *plugin_identity = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_init\n");

    /* Store the plugin identity for later use.
     * Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    automember_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *) automember_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *) automember_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODIFY_FN,
                         (void *) automember_mod_pre_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ADD_FN,
                         (void *) automember_add_pre_op) != 0 ||
        slapi_register_plugin("internalpostoperation",         /* op type */
                              1,                               /* Enabled */
                              "automember_init",               /* this function desc */
                              automember_internal_postop_init, /* init func */
                              AUTOMEMBER_INT_POSTOP_DESC,      /* plugin desc */
                              NULL,                            /* ? */
                              plugin_identity                  /* access control */
        ) ||
        slapi_register_plugin("postoperation",        /* op type */
                              1,                      /* Enabled */
                              "automember_init",      /* this function desc */
                              automember_postop_init, /* init func for post op */
                              AUTOMEMBER_POSTOP_DESC, /* plugin desc */
                              NULL,                   /* ? */
                              plugin_identity         /* access control */
        )
        ) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_init: failed to register plugin\n");
        status = -1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_init\n");
    return status;
}

static int
automember_internal_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *) automember_add_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *) automember_del_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *) automember_mod_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *) automember_modrdn_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_internal_postop_init: failed to register plugin\n");
        status = -1;
    }
 
    return status;
}

static int
automember_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN,
                         (void *) automember_add_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN,
                         (void *) automember_del_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN,
                         (void *) automember_mod_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN,
                         (void *) automember_modrdn_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_postop_init: failed to register plugin\n");
        status = -1;
    }

    return status;
}


/* Stash the config area in the pblock for start functions? */
/*
 * automember_start()
 *
 * Creates config lock and loads config cache.
 */
static int
automember_start(Slapi_PBlock * pb)
{
    char *plugindn = NULL;
    char *config_area = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_start\n");

    /* Check if we're already started */
    if (g_plugin_started) {
        goto done;
    }

    g_automember_config_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "automember_config");

    if (!g_automember_config_lock) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_start: lock creation failed\n");

        return -1;
    }

    /*
     * Get the plug-in target dn from the system
     * and store it for future use. */
    slapi_pblock_get(pb, SLAPI_TARGET_DN, &plugindn);
    if (NULL == plugindn || 0 == strlen(plugindn)) {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_start: unable to retrieve plugin dn\n");
        return -1;
    }

    automember_set_plugin_sdn(slapi_sdn_new_dn_byref(plugindn));

    /* Set the alternate config area if one is defined. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_AREA, &config_area);
    if (config_area) {
        automember_set_config_area(slapi_sdn_new_dn_byval(config_area));
    }

    /*
     * Load the config cache
     */
    g_automember_config = (PRCList *)slapi_ch_calloc(1, sizeof(struct configEntry));
    PR_INIT_CLIST(g_automember_config);

    if (automember_load_config() != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_start: unable to load plug-in configuration\n");
        return -1;
    }

    g_plugin_started = 1;
    slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "auto membership plug-in: ready for service\n");
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_start\n");

done:
    return 0;
}

/*
 * automember_close()
 *
 * Cleans up the config cache.
 */
static int
automember_close(Slapi_PBlock * pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_close\n");

    if (!g_plugin_started) {
        goto done;
    }

    automember_config_write_lock();
    g_plugin_started = 0;
    automember_delete_config();
    automember_config_unlock();

    slapi_ch_free((void **)&g_automember_config);
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
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_close\n");

    return 0;
}

/*
 * automember_get_config()
 *
 * Get the config list.
 */
PRCList *
automember_get_config()
{
    return g_automember_config;
}

/*
 * automember_load_config()
 *
 * Parse and load the config entries.
 */
static int
automember_load_config()
{
    int status = 0;
    int result;
    int i;
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_load_config\n");

    /* Clear out any old config. */
    automember_config_write_lock();
    automember_delete_config();

    search_pb = slapi_pblock_new();

    /* If an alternate config area is configured, find
     * the config entries that are beneath it, otherwise
     * we load the entries beneath our top-level plug-in
     * config entry. */
    if (automember_get_config_area()) {
        /* Find the config entries beneath the alternate config area. */
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_load_config: Looking for config entries "
                        "beneath \"%s\".\n", slapi_sdn_get_ndn(automember_get_config_area()));

        slapi_search_internal_set_pb(search_pb, slapi_sdn_get_ndn(automember_get_config_area()),
                                     LDAP_SCOPE_SUBTREE, AUTOMEMBER_DEFINITION_FILTER,
                                     NULL, 0, NULL, NULL, automember_get_plugin_id(), 0);
    } else {
        /* Find the config entries beneath our plugin entry. */
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_load_config: Looking for config entries "
                        "beneath \"%s\".\n", slapi_sdn_get_ndn(automember_get_plugin_sdn()));

        slapi_search_internal_set_pb(search_pb, slapi_sdn_get_ndn(automember_get_plugin_sdn()),
                                     LDAP_SCOPE_SUBTREE, AUTOMEMBER_DEFINITION_FILTER,
                                     NULL, 0, NULL, NULL, automember_get_plugin_id(), 0);
    }

    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    if (LDAP_SUCCESS != result) {
        if (automember_get_config_area() && (result == LDAP_NO_SUCH_OBJECT)) {
            slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_load_config: Config container \"%s\" does "
                            "not exist.\n", slapi_sdn_get_ndn(automember_get_config_area()));
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
         slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                         "automember_load_config: parsing config entry "
                         "\"%s\".\n", slapi_entry_get_dn(entries[i]));
        /* We don't care about the status here because we may have
         * some invalid config entries, but we just want to continue
         * looking for valid ones. */
        automember_parse_config_entry(entries[i], 1);
    }

  cleanup:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    automember_config_unlock();
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_load_config\n");

    return status;
}

/*
 * automember_parse_config_entry()
 *
 * Parses a single config entry.  If apply is non-zero, then
 * we will load and start using the new config.  You can simply
 * validate config without making any changes by setting apply
 * to 0.
 *
 * Returns 0 if the entry is valid and -1 if it is invalid.
 */
static int
automember_parse_config_entry(Slapi_Entry * e, int apply)
{
    char *value = NULL;
    char **values = NULL;
    struct configEntry *entry = NULL;
    struct configEntry *config_entry;
    PRCList *list;
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **rule_entries = NULL;
    char *filter_str = NULL;
    Slapi_Filter *filter = NULL;
    int result;
    int entry_added = 0;
    int i = 0;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_parse_config_entry\n");

    /* If this is the main plug-in config entry or
     * the config area container, just bail. */
    if ((slapi_sdn_compare(automember_get_plugin_sdn(), slapi_entry_get_sdn(e)) == 0) ||
        (automember_get_config_area() && (slapi_sdn_compare(automember_get_config_area(),
        slapi_entry_get_sdn(e)) == 0))) {
        goto bail;
    }

    /* If this entry is not an automember config definition entry, just bail. */
    filter_str = slapi_ch_strdup(AUTOMEMBER_DEFINITION_FILTER);
    filter = slapi_str2filter(filter_str);
    if (slapi_filter_test_simple(e, filter) != 0) {
        goto bail;
    }

    /* If marked as disabled, just bail. */
    value = slapi_entry_attr_get_charptr(e, AUTOMEMBER_DISABLED_TYPE);
    if (value) {
        slapi_ch_free_string(&value);
        goto bail;
    }

    entry = (struct configEntry *)slapi_ch_calloc(1, sizeof(struct configEntry));
    if (NULL == entry) {
        ret = -1;
        goto bail;
    }

    value = slapi_entry_get_ndn(e);
    if (value) {
        entry->dn = slapi_ch_strdup(value);
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_config_entry: Error "
                        "reading dn from config entry\n");
        ret = -1;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "----------> dn [%s]\n", entry->dn);

    /* Load the scope */
    value = slapi_entry_attr_get_charptr(e, AUTOMEMBER_SCOPE_TYPE);
    if (value) {
        entry->scope = value;
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        AUTOMEMBER_SCOPE_TYPE, entry->dn);
        ret = -1;
        goto bail;
    }

    /* Load the filter */
    value = slapi_entry_attr_get_charptr(e, AUTOMEMBER_FILTER_TYPE);
    if (value) {
        /* Convert to a Slapi_Filter to improve performance. */
        if (NULL == (entry->filter = slapi_str2filter(value))) {
            slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM ,
                "automember_parse_config_entry: Invalid search filter in "
                "%s config setting for config entry \"%s\" "
                "(filter = \"%s\").\n", AUTOMEMBER_FILTER_TYPE, entry->dn, value);
            ret = -1;
        }

        slapi_ch_free_string(&value);

        if (ret != 0) {
            goto bail;
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        AUTOMEMBER_FILTER_TYPE, entry->dn);
        ret = -1;
        goto bail;
    }

    /* Load the default groups */
    values = slapi_entry_attr_get_charray(e, AUTOMEMBER_DEFAULT_GROUP_TYPE);
    if (values) {
        /* Just hand off the values */
        entry->default_groups = values;
        values = NULL;
    }

    /* Load the grouping attr */
    value = slapi_entry_attr_get_charptr(e, AUTOMEMBER_GROUPING_ATTR_TYPE);
    if (value) {
        if (automember_parse_grouping_attr(value, &(entry->grouping_attr),
            &(entry->grouping_value)) != 0) {
            slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_parse_config_entry: Invalid "
                            "%s config setting for config entry \"%s\" "
                            "(value: \"%s\").\n", AUTOMEMBER_GROUPING_ATTR_TYPE,
                            entry->dn, value);
            ret = -1;
        }

        slapi_ch_free_string(&value);
        if (ret != 0) {
            goto bail;
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_config_entry: The %s config "
                        "setting is required for config entry \"%s\".\n",
                        AUTOMEMBER_GROUPING_ATTR_TYPE, entry->dn);
        ret = -1;
        goto bail;
    }

    /* Find all child regex rule entries */
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, entry->dn, LDAP_SCOPE_SUBTREE,
                                 AUTOMEMBER_REGEX_RULE_FILTER, NULL, 0, NULL,
                                 NULL, automember_get_plugin_id(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    /* If this is a new config entry being added, it won't exist yet
     * when we are simply validating config.  We can just ignore no
     * such object errors. */
    if ((LDAP_SUCCESS != result) && (LDAP_NO_SUCH_OBJECT != result)) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_config_entry: Error searching "
                        "for child rule entries for config \"%s\" (err=%d).",
                        entry->dn, result);
        ret = -1;
        goto bail;
    }

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &rule_entries);

    /* Go through each child rule entry and parse it. */
    for (i = 0; rule_entries && (rule_entries[i] != NULL); i++) {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_config_entry: parsing regex rule entry "
                        "\"%s\".\n", slapi_entry_get_dn(rule_entries[i]));
        automember_parse_regex_entry(entry, rule_entries[i]);
    }


    /* If we were only called to validate config, we can
     * just bail out before applying the config changes */
    if (apply == 0) {
        goto bail;
    }

    /* Add the config object to the list.  We order by scope. */
    if (!PR_CLIST_IS_EMPTY(g_automember_config)) {
        list = PR_LIST_HEAD(g_automember_config);
        while (list != g_automember_config) {
            config_entry = (struct configEntry *) list;

            /* If the config entry we are adding has a scope that is
             * a child of the scope of the current list item, we insert
             * the entry before that list item. */
            if (slapi_dn_issuffix(entry->scope, config_entry->scope)) {
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                                "store [%s] before [%s] \n", entry->dn,
                                config_entry->dn);

                entry_added = 1;
                break;
            }

            list = PR_NEXT_LINK(list);

            /* If we hit the end of the list, add to the tail. */
            if (g_automember_config == list) {
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                                "store [%s] at tail\n", entry->dn);

                entry_added = 1;
                break;
            }
        }
    } else {
        /* first entry */
        PR_INSERT_LINK(&(entry->list), g_automember_config);
        slapi_log_error(SLAPI_LOG_CONFIG, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "store [%s] at head \n", entry->dn);

        entry_added = 1;
    }

  bail:
    if (0 == entry_added) {
        /* Don't log error if we weren't asked to apply config */
        if ((apply != 0) && (entry != NULL)) {
            slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_parse_config_entry: Invalid config entry "
                            "[%s] skipped\n", entry->dn);
        }
        automember_free_config_entry(&entry);
    } else {
        ret = 0;
    }

    slapi_ch_free_string(&filter_str);
    slapi_filter_free(filter, 1);
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_parse_config_entry\n");

    return ret;
}

static void
automember_free_config_entry(struct configEntry ** entry)
{
    struct configEntry *e = *entry;

    if (e == NULL)
        return;

    if (e->dn) {
        slapi_log_error(SLAPI_LOG_CONFIG, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "freeing config entry [%s]\n", e->dn);
        slapi_ch_free_string(&e->dn);
    }

    if (e->scope) {
        slapi_ch_free_string(&e->scope);
    }

    if (e->filter) {
        slapi_filter_free(e->filter, 1);
    }

    if (e->exclusive_rules) {
        PRCList *list;

        /* Clear out the list contents. */
        while (!PR_CLIST_IS_EMPTY((PRCList *)e->exclusive_rules)) {
            list = PR_LIST_HEAD((PRCList *)e->exclusive_rules);
            PR_REMOVE_LINK(list);
            automember_free_regex_rule((struct automemberRegexRule *)list);
        }

        /* Free the list itself. */
        slapi_ch_free((void **)&(e->exclusive_rules));
    }

    /* Clear out the list contents. */
    if (e->inclusive_rules) {
        PRCList *list;

        while (!PR_CLIST_IS_EMPTY((PRCList *)e->inclusive_rules)) {
            list = PR_LIST_HEAD((PRCList *)e->inclusive_rules);
            PR_REMOVE_LINK(list);
            automember_free_regex_rule((struct automemberRegexRule *)list);
        }

        /* Free the list itself. */
        slapi_ch_free((void **)&(e->inclusive_rules));
    }

    if (e->default_groups) {
        slapi_ch_array_free(e->default_groups);
    }

    if (e->grouping_attr) {
        slapi_ch_free_string(&e->grouping_attr);
    }

    if (e->grouping_value) {
        slapi_ch_free_string(&e->grouping_value);
    }

    slapi_ch_free((void **)entry);
}

static void
automember_delete_configEntry(PRCList *entry)
{
    PR_REMOVE_LINK(entry);
    automember_free_config_entry((struct configEntry **) &entry);
}

static void
automember_delete_config()
{
    PRCList *list;

    /* Delete the config cache. */
    while (!PR_CLIST_IS_EMPTY(g_automember_config)) {
        list = PR_LIST_HEAD(g_automember_config);
        automember_delete_configEntry(list);
    }

    return;
}


/*
 * Helper functions
 */
static char *
automember_get_dn(Slapi_PBlock * pb)
{
    char *dn = 0;
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_get_dn\n");

    if (slapi_pblock_get(pb, SLAPI_TARGET_DN, &dn)) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_get_dn: failed to get dn of changed entry");
        goto bail;
    }

  bail:
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_get_dn\n");

    return dn;
}

void
automember_set_config_area(Slapi_DN *sdn)
{
    _ConfigAreaDN = sdn;
}

Slapi_DN *
automember_get_config_area()
{
    return _ConfigAreaDN;
}

/*
 * automember_dn_is_config()
 *
 * Checks if dn is an auto membership config entry.
 */
static int
automember_dn_is_config(char *dn)
{
    int ret = 0;
    Slapi_DN *sdn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_dn_is_config\n");

    if (dn == NULL) {
        goto bail;
    }

    sdn = slapi_sdn_new_dn_byref(dn);

    /* If an alternate config area is configured, treat it's child
     * entries as config entries.  If the alternate config area is
     * not configured, treat children of the top-level plug-in
     * config entry as our config entries. */
    if (automember_get_config_area()) {
        if (slapi_sdn_issuffix(sdn, automember_get_config_area()) &&
            slapi_sdn_compare(sdn, automember_get_config_area())) {
            ret = 1;
        }
    } else {
        if (slapi_sdn_issuffix(sdn, automember_get_plugin_sdn()) &&
            slapi_sdn_compare(sdn, automember_get_plugin_sdn())) {
            ret = 1;
        }
    }

bail:
    slapi_sdn_free(&sdn);
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_dn_is_config\n");

    return ret;
}

/*
 * automember_oktodo()
 *
 * Check if we want to process this operation.  We need to be
 * sure that the operation succeeded.
 */
static int
automember_oktodo(Slapi_PBlock *pb)
{
    int ret = 1;
    int oprc = 0;

    slapi_log_error( SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                 "--> automember_oktodo\n" );

    if(slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0) {
        slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                "automember_oktodo: could not get parameters\n" );
        ret = -1;
    }

    /* This plugin should only execute if the operation succeeded. */
    if(oprc != 0) {
        ret = 0;
    }

    slapi_log_error( SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                 "<-- automember_oktodo\n" );

    return ret;
}

/*
 * automember_isrepl()
 *
 * Returns 1 if the operation associated with pb
 * is a replicated op.  Returns 0 otherwise.
 */
static int
automember_isrepl(Slapi_PBlock *pb)
{
    int is_repl = 0;

    slapi_log_error( SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                 "--> automember_isrepl\n" );

    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_repl);

    slapi_log_error( SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                 "<-- automember_isrepl\n" );

    return is_repl;
}

/*
 * automember_parse_regex_entry()
 *
 * Parses a rule entry and adds the regex rules to the
 * passed in config struct.  Invalid regex rules will
 * be skipped and logged at the fatal log level.
 */
static void
automember_parse_regex_entry(struct configEntry *config, Slapi_Entry *e)
{
    char *target_group = NULL;
    char **values = NULL;
    PRCList *list;
    int i = 0;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_parse_regex_entry\n");

    /* Make sure the target group was specified. */
    target_group = slapi_entry_attr_get_charptr(e, AUTOMEMBER_TARGET_GROUP_TYPE);
    if (!target_group) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_entry: The %s config "
                        "setting is required for rule entry \"%s\".\n",
                        AUTOMEMBER_TARGET_GROUP_TYPE, slapi_entry_get_ndn(e));
        goto bail;
    }

    /* Ensure that the target group DN is valid. */
    if (slapi_dn_syntax_check(NULL, target_group, 1) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_entry: invalid target group DN "
                        "in rule \"%s\" (dn=\"%s\").\n", slapi_entry_get_ndn(e),
                        target_group);
        goto bail;
    }

    /* Load inclusive rules */
    values = slapi_entry_attr_get_charray(e, AUTOMEMBER_INC_REGEX_TYPE);
    if (values) {
        struct automemberRegexRule *rule = NULL;

        /* If we haven't loaded any inclusive rules for
         * this config definition yet, create a new list. */
        if (config->inclusive_rules == NULL) {
            /* Create a list to hold our regex rules */
            config->inclusive_rules = (struct automemberRegexRule *)slapi_ch_calloc(1, sizeof(struct automemberRegexRule));
            PR_INIT_CLIST((PRCList *)config->inclusive_rules);
        }

        for (i = 0; values && values[i]; ++i) {
            rule = automember_parse_regex_rule(values[i]);

            if (rule) {
                /* Fill in the target group. */
                rule->target_group_dn = slapi_sdn_new_dn_byval(target_group);

                if (!PR_CLIST_IS_EMPTY((PRCList *)config->inclusive_rules)) {
                    list = PR_LIST_HEAD((PRCList *)config->inclusive_rules);
                    while (list != (PRCList *)config->inclusive_rules) {
                        struct automemberRegexRule *curr_rule = (struct automemberRegexRule *)list;
                        /* Order rules by target group DN */
                        if (slapi_sdn_compare(rule->target_group_dn, curr_rule->target_group_dn) < 0) {
                            PR_INSERT_BEFORE(&(rule->list), list);
                            break;
                        }

                        list = PR_NEXT_LINK(list);

                        /* If we hit the end of the list, add to the tail. */
                        if ((PRCList *)config->inclusive_rules == list) {
                            PR_INSERT_BEFORE(&(rule->list), list);
                            break;
                        }
                    }
                } else {
                    /* Add to head of list */
                    PR_INSERT_LINK(&(rule->list), (PRCList *)config->inclusive_rules);
                }
            } else {
                slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                                "automember_parse_regex_entry: Skipping invalid inclusive "
                                "regex rule in rule entry \"%s\" (rule = \"%s\").\n",
                                slapi_entry_get_ndn(e), values[i]);
            }
        }

        slapi_ch_array_free(values);
        values = NULL;
    }

    /* Load exclusive rules. */
    values = slapi_entry_attr_get_charray(e, AUTOMEMBER_EXC_REGEX_TYPE);
    if (values) {
        struct automemberRegexRule *rule = NULL;

        /* If we haven't loaded any exclusive rules for
         * this config definition yet, create a new list. */
        if (config->exclusive_rules == NULL) {
            /* Create a list to hold our regex rules */
            config->exclusive_rules = (struct automemberRegexRule *)slapi_ch_calloc(1, sizeof(struct automemberRegexRule));
            PR_INIT_CLIST((PRCList *)config->exclusive_rules);
        }

        for (i = 0; values && values[i]; ++i) {
            rule = automember_parse_regex_rule(values[i]);

            if (rule) { 
                /* Fill in the target group. */
                rule->target_group_dn = slapi_sdn_new_dn_byval(target_group);

                if (!PR_CLIST_IS_EMPTY((PRCList *)config->exclusive_rules)) {
                    list = PR_LIST_HEAD((PRCList *)config->exclusive_rules);
                    while (list != (PRCList *)config->exclusive_rules) {
                        struct automemberRegexRule *curr_rule = (struct automemberRegexRule *)list;
                        /* Order rules by target group DN */
                        if (slapi_sdn_compare(rule->target_group_dn, curr_rule->target_group_dn) < 0) {
                            PR_INSERT_BEFORE(&(rule->list), list);
                            break; 
                        }
    
                        list = PR_NEXT_LINK(list);
    
                        /* If we hit the end of the list, add to the tail. */
                        if ((PRCList *)config->exclusive_rules == list) {
                            PR_INSERT_BEFORE(&(rule->list), list);
                            break;
                        }
                    }
                } else {
                    /* Add to head of list */
                    PR_INSERT_LINK(&(rule->list), (PRCList *)config->exclusive_rules);
                }
            } else {
                slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                                "automember_parse_regex_entry: Skipping invalid exclusive "
                                "regex rule in rule entry \"%s\" (rule = \"%s\").\n",
                                slapi_entry_get_ndn(e), values[i]);
            }
        }

        slapi_ch_array_free(values);
        values = NULL;
    }

bail:
    slapi_ch_free_string(&target_group);

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_parse_regex_entry\n");
}

/*
 * automember_parse_regex_rule()
 *
 * Parses a regex rule and returns a regex rule struct.  The caller
 * will need to free this struct when it is finished with it.  If
 * there is a problem parsing the regex rule, an error will be
 * logged and NULL will be returned.
 */
static struct automemberRegexRule *
automember_parse_regex_rule(char *rule_string)
{
    struct automemberRegexRule *rule = NULL;
    char *attr = NULL;
    Slapi_Regex *regex = NULL;
    const char *recomp_result = NULL;
    char *p = NULL;
    char *p2 = NULL;

    /* A rule is in the form "attr=regex". */
    /* Find the comparison attribute name. */
    if ((p = strchr(rule_string, '=')) == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_rule: Unable to parse "
                        "regex rule (missing '=' delimeter).\n");
        goto bail;
    }

    /* Make sure the attribute name is not empty. */
    if (p == rule_string) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_rule: Unable to parse "
                        " regex rule (missing comparison attribute).\n");
        goto bail;
    }

    if ((attr = strndup(rule_string, p - rule_string)) == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_rule: Unable to allocate "
                        "memory.\n");
        goto bail;
    }

    /* Validate the attribute. */
    for (p2 = attr; p2 && (*p2 != '\0'); p2++) {
        if (!IS_ATTRDESC_CHAR(*p2)) {
            slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_parse_regex_rule: Invalid comparison "
                            "attribute name \"%s\".\n", attr);
            goto bail;
        }
    }

    /* Find the regex. */
    p++;
    if (*p == '\0') {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_rule: Unable to parse "
                        "regex rule (missing regex).\n");
        goto bail;
    }

    /* Compile the regex to validate it. */
    regex = slapi_re_comp(p, &recomp_result);
    if (!regex) {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_regex_rule: Unable to parse "
                        "regex rule (invalid regex).  Error \"%s\".\n",
                        recomp_result?recomp_result:"unknown");
    }

    /* Validation has passed, so create the regex rule struct and fill it in.
     * We hand off everything we have allocated.  All of this will be free'd
     * when the rule struct itself is freed. */
    rule = (struct automemberRegexRule *)slapi_ch_calloc(1, sizeof(struct automemberRegexRule));
    rule->attr = attr;
    rule->regex_str = slapi_ch_strdup(p);
    rule->regex = regex;

bail:
    /* Cleanup if we didn't successfully create a rule. */
    if (!rule) {
        slapi_ch_free_string(&attr);
        slapi_re_free(regex);
    }

    return rule;
}

/*
 * automember_free_regex_rule()
 *
 * Frees a regex rule and all of it's contents from memory.
 */
static void
automember_free_regex_rule(struct automemberRegexRule *rule)
{
    if (rule) {
        if (rule->target_group_dn) {
            slapi_sdn_free(&(rule->target_group_dn));
        }

        if (rule->attr) {
            slapi_ch_free_string(&(rule->attr));
        }

        if (rule->regex_str) {
            slapi_ch_free_string(&(rule->regex_str));
        }

        if (rule->regex) {
            slapi_re_free(rule->regex);
        }
    }

    slapi_ch_free((void **)&rule);
}

/*
 * automember_parse_grouping_attr()
 *
 * Parses a grouping attribute and grouping value from
 * the passed in config string.  Memory will be allocated
 * for grouping_attr and grouping_value, so it is up to
 * the called to free them when they are no longer needed.
 * Returns 0 upon success and 1 upon failure.
 */
static int
automember_parse_grouping_attr(char *value, char **grouping_attr, char **grouping_value)
{
    int ret = 0;
    char *p = NULL;

    /* Clear out any existing type or value. */
    slapi_ch_free_string(grouping_attr);
    slapi_ch_free_string(grouping_value);

    /* split out the type from the value (use the first ':') */
    if ((p = strchr(value, ':')) == NULL) {
        slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_grouping_attr: Value for grouping attribute "
                        "is not in the correct format. (value: \"%s\").\n", value);
        ret = 1;
        goto bail;
    }

    /* Ensure the type is not empty. */
    if (p == value) {
        slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_grouping_attr: Value for grouping attribute "
                        "is not in the correct format. The grouping attribute is missing. "
                        "(value: \"%s\").\n", value);
        ret = 1;
        goto bail;
    }

    /* Duplicate the type to be returned. */
    *grouping_attr = strndup(value, p - value);

    /* Advance p to point to the beginning of the value. */
    p++;
    while (*p == ' ') {
        p++;
    }

    /* Ensure the value is not empty. */
    if (*p == '\0') {
        slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_grouping_attr: Value for grouping attribute "
                        "is not in the correct format. The grouping value is "
                        "missing. (value: \"%s\").\n", value);
        ret = 1;
        goto bail;
    }

    /* Duplicate the value to be returned. */
    *grouping_value = slapi_ch_strdup(p);

    /* Ensure that memory was allocated successfully. */
    if ((*grouping_attr == NULL) || (*grouping_value == NULL)) {
        slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_parse_grouping_attr: Error allocating memory.\n");
        ret = 1;
        goto bail;
    }

    /* Ensure that the grouping attr is a legal attr name. */
    for (p = *grouping_attr; p && (*p != '\0'); p++) {
        if (!IS_ATTRDESC_CHAR(*p)) {
            slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_parse_grouping_attr: Invalid value for "
                            "grouping attribute.  The grouping attribute type is "
                            "illegal. (type: \"%s\").\n", *grouping_attr);
            ret = 1;
            goto bail;
        }
    }

    /* Ensure that the grouping value type is a legal attr name. */
    for (p = *grouping_value; p && (*p != '\0'); p++) {
        if (!IS_ATTRDESC_CHAR(*p)) {
            slapi_log_error( SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_parse_grouping_attr: Invalid value for "
                            "grouping attribute.  The grouping value type is "
                            "illegal. (type: \"%s\").\n", *grouping_value);
            ret = 1;
            goto bail;
        }
    }

  bail:
    if (ret != 0) {
        slapi_ch_free_string(grouping_attr);
        slapi_ch_free_string(grouping_value);
    }

    return ret;
}

/*
 * automember_update_membership()
 *
 * Determines which target groups need to be updated according to
 * the rules in config, then performs the updates.
 */
static void
automember_update_membership(struct configEntry *config, Slapi_Entry *e)
{
    PRCList *rule = NULL;
    struct automemberRegexRule *curr_rule = NULL;
    PRCList exclusions;
    PRCList targets;
    struct automemberDNListItem *dnitem = NULL;
    Slapi_DN *last = NULL;
    PRCList *curr_exclusion = NULL;
    char **vals = NULL;
    int i = 0;

    if (!config || !e) {
        return;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "automember_update_membership: Processing \"%s\" "
                    "definition entry for candidate entry \"%s\".\n",
                    config->dn, slapi_entry_get_dn(e));

    /* Initialize our lists that keep track of targets. */
    PR_INIT_CLIST(&exclusions);
    PR_INIT_CLIST(&targets);

    /* Go through exclusive rules and build an exclusion list. */
    if (config->exclusive_rules) {
        if (!PR_CLIST_IS_EMPTY((PRCList *)config->exclusive_rules)) {
            rule = PR_LIST_HEAD((PRCList *)config->exclusive_rules);
            while (rule != (PRCList *)config->exclusive_rules) {
                curr_rule = (struct automemberRegexRule *)rule;

                /* Regex rules are sorted by the target group DN.  This means
                 * we can skip all rules for the last target group DN that we
                 * added to the exclusions list. */
                if ((last == NULL) || slapi_sdn_compare(last, curr_rule->target_group_dn) != 0) {
                    /* Get comparison attr and loop through values. */
                    vals = slapi_entry_attr_get_charray(e, curr_rule->attr);
                    for (i = 0; vals && vals[i]; ++i) {
                        /* Evaluate the regex. */
                        if (slapi_re_exec(curr_rule->regex, vals[i], -1) == 1) {
                            /* Found a match.  Add to end of the exclusion list
                             * and set last as a hint to ourselves. */
                            slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                                            "automember_update_membership: Adding \"%s\" "
                                            "to list of excluded groups for \"%s\" "
                                            "(matched: \"%s=%s\").\n",
                                            slapi_sdn_get_dn(curr_rule->target_group_dn),
                                            slapi_entry_get_dn(e), curr_rule->attr,
                                            curr_rule->regex_str);
                            dnitem = (struct automemberDNListItem *)slapi_ch_calloc(1,
                                      sizeof(struct automemberDNListItem));
                            /* We are just referencing the dn from the regex rule.  We
                             * will not free it when we clean up this list.  This list
                             * is more short-lived than the regex rule list, so we can
                             * get away with this optimization. */
                            dnitem->dn = curr_rule->target_group_dn;
                            PR_APPEND_LINK(&(dnitem->list), &exclusions);
                            last = curr_rule->target_group_dn;
                        }
                    }

                    slapi_ch_array_free(vals);
                    vals = NULL;
                }

                rule = PR_NEXT_LINK(rule);
            }
        }
    }

    /* Go through inclusive rules and build the target list. */
    if (config->inclusive_rules) {
        if (!PR_CLIST_IS_EMPTY((PRCList *)config->inclusive_rules)) {
            /* Clear out our last hint from processing exclusions. */
            last = NULL;

            /* Get the first excluded target for exclusion checking. */
            if (!PR_CLIST_IS_EMPTY(&exclusions)) {
                curr_exclusion = PR_LIST_HEAD(&exclusions);
            }
            
            rule = PR_LIST_HEAD((PRCList *)config->inclusive_rules);
            while (rule != (PRCList *)config->inclusive_rules) {
                curr_rule = (struct automemberRegexRule *)rule;

                /* The excluded targets are stored in alphabetical order.  Instead
                 * of going through the entire exclusion list for each inclusive
                 * rule, we can simply go through the exclusion list once and keep
                 * track of our position.  If the curent exclusion comes after
                 * the target DN used in the current inclusive rule, it can't be
                 * excluded.  If the current exclusion comes before the target
                 * in the current rule, we need to go through the exclusion list
                 * until we find a target that is the same or comes after the 
                 * current rule. */
                if (curr_exclusion) {
                    while ((curr_exclusion != &exclusions) && (slapi_sdn_compare(
                           ((struct automemberDNListItem *)curr_exclusion)->dn,
                           curr_rule->target_group_dn) < 0)) {
                        curr_exclusion = PR_NEXT_LINK(curr_exclusion);
                    }
                }

                /* Regex rules are sorted by the target group DN.  This means
                 * we can skip all rules for the last target group DN that we
                 * added to the targets list.  We also skip any rules for
                 * target groups that have been excluded by an exclusion rule. */
                if (((curr_exclusion == NULL) || (curr_exclusion == &exclusions) ||
                    slapi_sdn_compare(((struct automemberDNListItem *)curr_exclusion)->dn,
                    curr_rule->target_group_dn) != 0) && ((last == NULL) ||
                    (slapi_sdn_compare(last, curr_rule->target_group_dn) != 0))) {
                    /* Get comparison attr and loop through values. */
                    vals = slapi_entry_attr_get_charray(e, curr_rule->attr);
                    for (i = 0; vals && vals[i]; ++i) {
                        /* Evaluate the regex. */
                        if (slapi_re_exec(curr_rule->regex, vals[i], -1) == 1) {
                            /* Found a match.  Add to the end of the targets list
                             * and set last as a hint to ourselves. */
                            slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                                            "automember_update_membership: Adding \"%s\" "
                                            "to list of target groups for \"%s\" "
                                            "(matched: \"%s=%s\").\n",
                                            slapi_sdn_get_dn(curr_rule->target_group_dn),
                                            slapi_entry_get_dn(e), curr_rule->attr,
                                            curr_rule->regex_str);
                            dnitem = (struct automemberDNListItem *)slapi_ch_calloc(1,
                                      sizeof(struct automemberDNListItem));
                            /* We are just referencing the dn from the regex rule.  We
                             * will not free it when we clean up this list.  This list
                             * is more short-lived than the regex rule list, so we can
                             * get away with this optimization. */
                            dnitem->dn = curr_rule->target_group_dn;
                            PR_APPEND_LINK(&(dnitem->list), &targets);
                            last = curr_rule->target_group_dn;
                        }
                    }

                    slapi_ch_array_free(vals);
                    vals = NULL;
                }

                rule = PR_NEXT_LINK(rule);
            }
        }
    }

    /* If no targets, update default groups if set.  Otherwise, update
     * targets.  Use a helper to do the actual updates.  We can just pass an
     * array of target group DNs along with our entry DN, the grouping attr,
     * and the grouping value. */
    if (PR_CLIST_IS_EMPTY(&targets)) {
        /* Add to each default group. */
        for (i = 0; config->default_groups && config->default_groups[i]; i++) {
            automember_add_member_value(e, config->default_groups[i],
                                        config->grouping_attr, config->grouping_value);
        }
    } else {
        /* Update the target groups. */
        dnitem = (struct automemberDNListItem *)PR_LIST_HEAD(&targets);
        while ((PRCList *)dnitem != &targets) {
            automember_add_member_value(e, slapi_sdn_get_dn(dnitem->dn),
                                        config->grouping_attr, config->grouping_value);
            dnitem = (struct automemberDNListItem *)PR_NEXT_LINK((PRCList *)dnitem);
        }
    }

    /* Free the exclusions and targets lists.  Remember that the DN's
     * are not ours, so don't free them! */
    while (!PR_CLIST_IS_EMPTY(&exclusions)) {
            dnitem = (struct automemberDNListItem *)PR_LIST_HEAD(&exclusions);
            PR_REMOVE_LINK((PRCList *)dnitem);
            slapi_ch_free((void**)&dnitem);
    }

    while (!PR_CLIST_IS_EMPTY(&targets)) {
            dnitem = (struct automemberDNListItem *)PR_LIST_HEAD(&targets);
            PR_REMOVE_LINK((PRCList *)dnitem);
            slapi_ch_free((void**)&dnitem);
    }

}

/*
 * automember_add_member_value()
 *
 * Adds a member entry to a group.
 */
static void
automember_add_member_value(Slapi_Entry *member_e, const char *group_dn,
                            char *grouping_attr, char *grouping_value)
{
    Slapi_PBlock *mod_pb = slapi_pblock_new();
    int result = LDAP_SUCCESS;
    LDAPMod mod;
    LDAPMod *mods[2];
    char *vals[2];
    char *member_value = NULL;
    int freeit = 0;

    /* If grouping_value is dn, we need to fetch the dn instead. */
    if (slapi_attr_type_cmp(grouping_value, "dn", SLAPI_TYPE_CMP_EXACT) == 0) {
        member_value = slapi_entry_get_ndn(member_e);
    } else {
        member_value = slapi_entry_attr_get_charptr(member_e, grouping_value);
        freeit = 1;
    }

    if (member_value) {
        /* Set up the operation. */
        vals[0] = member_value;
        vals[1] = 0;
        mod.mod_op = LDAP_MOD_ADD;
        mod.mod_type = grouping_attr;
        mod.mod_values = vals;
        mods[0] = &mod;
        mods[1] = 0;

        /* Perform the modify operation. */
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_add_member_value: Adding \"%s\" as "
                        "a \"%s\" value to group \"%s\".\n",
                        member_value, grouping_attr, group_dn);

        slapi_modify_internal_set_pb(mod_pb, group_dn,
                                     mods, 0, 0, automember_get_plugin_id(), 0);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

        if ((result != LDAP_SUCCESS) && (result != LDAP_TYPE_OR_VALUE_EXISTS)) {
            slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                            "automember_add_member_value: Unable to add \"%s\" as "
                            "a \"%s\" value to group \"%s\" (%s).\n", 
                            member_value, grouping_attr, group_dn,
                            ldap_err2string(result));
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_add_member_value: Unable to find grouping "
                        "value attribute \"%s\" in entry \"%s\".\n",
                        grouping_value, slapi_entry_get_dn(member_e));
    }

    /* Cleanup */
    if (freeit) {
        slapi_ch_free_string(&member_value);
    }

    slapi_pblock_destroy(mod_pb);
}


/*
 * Operation callback functions
 */

/*
 * automember_pre_op()
 *
 * Checks if an operation affects the auto membership
 * config and validates the config changes.
 */
static int
automember_pre_op(Slapi_PBlock * pb, int modop)
{
    char *dn = 0;
    Slapi_Entry *e = 0;
    Slapi_Mods *smods = 0;
    LDAPMod **mods;
    int free_entry = 0;
    char *errstr = NULL;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_pre_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started)
        goto bail;

    if (0 == (dn = automember_get_dn(pb)))
        goto bail;

    if (automember_dn_is_config(dn)) {
        /* Validate config changes, but don't apply them.
         * This allows us to reject invalid config changes
         * here at the pre-op stage.  Applying the config
         * needs to be done at the post-op stage. */

        if (LDAP_CHANGETYPE_ADD == modop) {
            slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);

        } else if (LDAP_CHANGETYPE_MODIFY == modop) {
            /* Fetch the entry being modified so we can
             * create the resulting entry for validation. */
            Slapi_DN *tmp_dn = slapi_sdn_new_dn_byref(dn);
            if (tmp_dn) {
                slapi_search_internal_get_entry(tmp_dn, 0, &e, automember_get_plugin_id());
                slapi_sdn_free(&tmp_dn);
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
        }

        if (automember_parse_config_entry(e, 0) != 0) {
            /* Refuse the operation if config parsing failed. */
            ret = LDAP_UNWILLING_TO_PERFORM;
            if (LDAP_CHANGETYPE_ADD == modop) {
                errstr = slapi_ch_smprintf("Not a valid auto membership configuration entry.");
            } else {
                errstr = slapi_ch_smprintf("Changes result in an invalid "
                                           "auto membership configuration.");
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
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_pre_op: operation failure [%d]\n", ret);
        slapi_send_ldap_result(pb, ret, NULL, errstr, 0, NULL);
        slapi_ch_free((void **)&errstr);
        ret = -1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_pre_op\n");

    return ret;
}

static int
automember_add_pre_op(Slapi_PBlock * pb)
{
    return automember_pre_op(pb, LDAP_CHANGETYPE_ADD);
}

static int
automember_mod_pre_op(Slapi_PBlock * pb)
{
    return automember_pre_op(pb, LDAP_CHANGETYPE_MODIFY);
}

/*
 * automember_mod_post_op()
 *
 * Reloads the auto membership config
 * if config changes were made.
 */
static int
automember_mod_post_op(Slapi_PBlock *pb)
{
    char *dn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_mod_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started) {
        goto bail;
    }

    if (automember_oktodo(pb) && (dn = automember_get_dn(pb))) {
        /* Check if the config is being modified and reload if so. */
        if (automember_dn_is_config(dn)) {
            automember_load_config();
        }
    }

  bail:
    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_mod_post_op\n");

    return 0;
}

static int
automember_add_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    char *dn = NULL;
    struct configEntry *config = NULL;
    PRCList *list = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_add_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !automember_oktodo(pb))
        return 0;

    /* Reload config if a config entry was added. */
    if ((dn = automember_get_dn(pb))) {
        if (automember_dn_is_config(dn)) {
            automember_load_config();
        }
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_add_post_op: Error "
                        "retrieving dn\n");
    }

    /* If replication, just bail. */
    if (automember_isrepl(pb)) {
        return 0;
    }

    /* Get the newly added entry. */
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);

    if (e) {
        /* If the entry is a tombstone, just bail. */
        Slapi_Value *tombstone =
                        slapi_value_new_string(SLAPI_ATTR_VALUE_TOMBSTONE);
        int rc = slapi_entry_attr_has_syntax_value(e, SLAPI_ATTR_OBJECTCLASS,
                                                   tombstone);
        slapi_value_free(&tombstone);
        if (rc) {
            return 0;
        }

        /* Check if a config entry applies
         * to the entry being added. */
        automember_config_read_lock();

        /* Bail out if the plug-in close function was just called. */
        if (!g_plugin_started) {
            automember_config_unlock();
            return 0;
        }

        if (!PR_CLIST_IS_EMPTY(g_automember_config)) {
            list = PR_LIST_HEAD(g_automember_config);
            while (list != g_automember_config) {
                config = (struct configEntry *)list;

                /* Does the entry meet scope and filter requirements? */
                if (slapi_dn_issuffix(dn, config->scope) &&
                    (slapi_filter_test_simple(e, config->filter) == 0)) {
                    /* Find out what membership changes are needed and make them. */
                    automember_update_membership(config, e);
                }

                list = PR_NEXT_LINK(list);
            }
        }

        automember_config_unlock();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_add_post_op: Error "
                        "retrieving post-op entry %s\n", dn);
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_add_post_op\n");

    return 0;
}

/*
 * automember_del_post_op()
 *
 * Removes deleted config.
 */
static int
automember_del_post_op(Slapi_PBlock *pb)
{
    char *dn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_del_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !automember_oktodo(pb)) {
        return 0;
    }

    /* Reload config if a config entry was deleted. */
    if ((dn = automember_get_dn(pb))) {
        if (automember_dn_is_config(dn))
            automember_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_del_post_op: Error "
                        "retrieving dn\n");
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_del_post_op\n");

    return 0;
}

/*
 * automember_modrdn_post_op()
 *
 * Reloads config if config entries move
 * into or out of our config scope.
 */
static int
automember_modrdn_post_op(Slapi_PBlock *pb)
{
    char *old_dn = NULL;
    char *new_dn = NULL;
    Slapi_Entry *post_e = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "--> automember_modrdn_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !automember_oktodo(pb))
        return 0;

    /* Reload config if an existing config entry was renamed,
     * or if the new dn brings an entry into the scope of the
     * config entries. */
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_e);
    if (post_e) {
        new_dn = slapi_entry_get_ndn(post_e);
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_modrdn_post_op: Error "
                        "retrieving post-op entry\n");
        return 0;
    }

    if ((old_dn = automember_get_dn(pb))) {
        if (automember_dn_is_config(old_dn) || automember_dn_is_config(new_dn))
            automember_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                        "automember_modrdn_post_op: Error "
                        "retrieving dn\n");
    }

    slapi_log_error(SLAPI_LOG_TRACE, AUTOMEMBER_PLUGIN_SUBSYSTEM,
                    "<-- automember_modrdn_post_op\n");

    return 0;
}

