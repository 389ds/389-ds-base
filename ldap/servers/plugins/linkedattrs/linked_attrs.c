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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Linked attributes plug-in
 */
#include "linked_attrs.h"


/*
 * Plug-in globals
 */
static PRCList *g_link_config = NULL;
static PRCList *g_managed_config_index = NULL;
static Slapi_RWLock *g_config_lock;

static void *_PluginID = NULL;
static char *_PluginDN = NULL;
static int g_plugin_started = 0;

static Slapi_PluginDesc pdesc = { LINK_FEATURE_DESC,
                                  VENDOR,
                                  DS_PACKAGE_VERSION,
                                  LINK_PLUGIN_DESC };

/*
 * Plug-in management functions
 */
int linked_attrs_init(Slapi_PBlock * pb);
static int linked_attrs_start(Slapi_PBlock * pb);
static int linked_attrs_close(Slapi_PBlock * pb);
static int linked_attrs_postop_init(Slapi_PBlock * pb);
static int linked_attrs_internal_postop_init(Slapi_PBlock *pb);

/*
 * Operation callbacks (where the real work is done)
 */
static int linked_attrs_mod_post_op(Slapi_PBlock *pb);
static int linked_attrs_add_post_op(Slapi_PBlock *pb);
static int linked_attrs_del_post_op(Slapi_PBlock *pb);
static int linked_attrs_modrdn_post_op(Slapi_PBlock *pb);
static int linked_attrs_pre_op(Slapi_PBlock *pb, int modop);
static int linked_attrs_mod_pre_op(Slapi_PBlock *pb);
static int linked_attrs_add_pre_op(Slapi_PBlock *pb);

/*
 * Config cache management functions
 */
static int linked_attrs_load_config();
static void linked_attrs_delete_config();
static int linked_attrs_parse_config_entry(Slapi_Entry * e, int apply);
static void linked_attrs_insert_config_index(struct configEntry *entry);
static void linked_attrs_free_config_entry(struct configEntry ** entry);

/*
 * helpers
 */
static char *linked_attrs_get_dn(Slapi_PBlock * pb);
static Slapi_DN *linked_attrs_get_sdn(Slapi_PBlock * pb);
static int linked_attrs_dn_is_config(char *dn);
static void linked_attrs_find_config(const char *dn, const char *type,
    struct configEntry **config);
static void linked_attrs_find_config_reverse(const char *dn,
    const char *type, struct configEntry **config);
static int linked_attrs_config_index_has_type(char *type);
static int linked_attrs_config_exists(struct configEntry *entry);
static int linked_attrs_config_exists_reverse(struct configEntry *entry);
static int linked_attrs_oktodo(Slapi_PBlock *pb);
void linked_attrs_load_array(Slapi_Value **array, Slapi_Attr *attr);
int linked_attrs_compare(const void *a, const void *b);
static void linked_attrs_add_backpointers(char *linkdn, struct configEntry *config,
    Slapi_Mod *smod);
static void linked_attrs_del_backpointers(Slapi_PBlock *pb, char *linkdn,
    struct configEntry *config, Slapi_Mod *smod);
static void linked_attrs_replace_backpointers(Slapi_PBlock *pb, char *linkdn,
    struct configEntry *config, Slapi_Mod *smod);
static void linked_attrs_mod_backpointers(char *linkdn, char *type, char *scope,
    int modop, Slapi_ValueSet *targetvals);

/*
 * Config cache locking functions
 */
void
linked_attrs_read_lock()
{
    slapi_rwlock_rdlock(g_config_lock);
}

void
linked_attrs_write_lock()
{
    slapi_rwlock_wrlock(g_config_lock);
}

void
linked_attrs_unlock()
{
    slapi_rwlock_unlock(g_config_lock);
}


/*
 * Plugin identity functions
 */
void
linked_attrs_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
linked_attrs_get_plugin_id()
{
    return _PluginID;
}

void
linked_attrs_set_plugin_dn(const char *pluginDN)
{
    _PluginDN = (char *)pluginDN;
}

char *
linked_attrs_get_plugin_dn()
{
    return _PluginDN;
}


/*
 * Plug-in initialization functions
 */
int
linked_attrs_init(Slapi_PBlock *pb)
{
    int status = 0;
    char *plugin_identity = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_init\n");

    /* Store the plugin identity for later use.
     * Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    linked_attrs_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *) linked_attrs_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *) linked_attrs_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODIFY_FN,
                         (void *) linked_attrs_mod_pre_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ADD_FN,
                         (void *) linked_attrs_add_pre_op) != 0 ||
        slapi_register_plugin("internalpostoperation",  /* op type */
                              1,        /* Enabled */
                              "linked_attrs_init",   /* this function desc */
                              linked_attrs_internal_postop_init,  /* init func */
                              LINK_INT_POSTOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              plugin_identity   /* access control */
        ) ||
        slapi_register_plugin("postoperation",  /* op type */
                              1,        /* Enabled */
                              "linked_attrs_init",   /* this function desc */
                              linked_attrs_postop_init,  /* init func for post op */
                              LINK_POSTOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              plugin_identity   /* access control */
        )
        ) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_init: failed to register plugin\n");
        status = -1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_init\n");
    return status;
}

static int
linked_attrs_internal_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *) linked_attrs_add_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *) linked_attrs_del_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *) linked_attrs_mod_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *) linked_attrs_modrdn_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_internal_postop_init: failed to register plugin\n");
        status = -1;
    }
 
    return status;
}

static int
linked_attrs_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN,
                         (void *) linked_attrs_add_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN,
                         (void *) linked_attrs_del_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN,
                         (void *) linked_attrs_mod_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN,
                         (void *) linked_attrs_modrdn_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_postop_init: failed to register plugin\n");
        status = -1;
    }

    return status;
}


/*
 * linked_attrs_start()
 *
 * Creates config lock and loads config cache.
 */
static int
linked_attrs_start(Slapi_PBlock * pb)
{
    Slapi_DN *plugindn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_start\n");

    /* Check if we're already started */
    if (g_plugin_started) {
        goto done;
    }

    g_config_lock = slapi_new_rwlock();

    if (!g_config_lock) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_start: lock creation failed\n");

        return -1;
    }

    /*
     * Get the plug-in target dn from the system
     * and store it for future use. */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &plugindn);
    if (NULL == plugindn || 0 == slapi_sdn_get_ndn_len(plugindn)) {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_start: unable to retrieve plugin dn\n");
        return -1;
    }

    linked_attrs_set_plugin_dn(slapi_sdn_get_dn(plugindn));

    /*
     * Load the config cache
     */
    g_link_config = (PRCList *)slapi_ch_calloc(1, sizeof(struct configEntry));
    PR_INIT_CLIST(g_link_config);
    g_managed_config_index = (PRCList *)slapi_ch_calloc(1, sizeof(struct configIndex));
    PR_INIT_CLIST(g_managed_config_index);

    if (linked_attrs_load_config() != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_start: unable to load plug-in configuration\n");
        return -1;
    }

    /*
     * Register our task callback
     */
    slapi_task_register_handler("fixup linked attributes", linked_attrs_fixup_task_add);

    g_plugin_started = 1;
    slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                    "linked attributes plug-in: ready for service\n");
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_start\n");

done:
    return 0;
}

/*
 * linked_attrs_close()
 *
 * Cleans up the config cache.
 */
static int
linked_attrs_close(Slapi_PBlock * pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_close\n");

    if (!g_plugin_started) {
        goto done;
    }

    linked_attrs_write_lock();
    g_plugin_started = 0;
    linked_attrs_delete_config();
    linked_attrs_unlock();

    slapi_ch_free((void **)&g_link_config);
    slapi_ch_free((void **)&g_managed_config_index);

    /* We explicitly don't destroy the config lock here.  If we did,
     * there is the slight possibility that another thread that just
     * passed the g_plugin_started check is about to try to obtain
     * a reader lock.  We leave the lock around so these threads
     * don't crash the process.  If we always check the started
     * flag again after obtaining a reader lock, no free'd resources
     * will be used. */

done:
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_close\n");

    return 0;
}

PRCList *
linked_attrs_get_config()
{
    return g_link_config;
}

/*
 * config looks like this
 * - cn=myplugin
 * --- cn=manager link
 * --- cn=owner link
 * --- cn=etc
 */
static int
linked_attrs_load_config()
{
    int status = 0;
    int result;
    int i;
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_load_config\n");

    /* Clear out any old config. */
    linked_attrs_write_lock();
    linked_attrs_delete_config();

    /* Find the config entries beneath our plugin entry. */
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, linked_attrs_get_plugin_dn(),
                                 LDAP_SCOPE_SUBTREE, "objectclass=*",
                                 NULL, 0, NULL, NULL, linked_attrs_get_plugin_id(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    if (LDAP_SUCCESS != result) {
        status = -1;
        goto cleanup;
    }

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &entries);
    if (NULL == entries || NULL == entries[0]) {
        /* If there are no config entries, we're done. */
        goto cleanup;
    }

    /* Loop through all of the entries we found and parse them. */
    for (i = 0; (entries[i] != NULL); i++) {
        /* We don't care about the status here because we may have
         * some invalid config entries, but we just want to continue
         * looking for valid ones. */
        linked_attrs_parse_config_entry(entries[i], 1);
    }

  cleanup:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    linked_attrs_unlock();
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_load_config\n");

    return status;
}

/*
 * linked_attrs_parse_config_entry()
 *
 * Parses a single config entry.  If apply is non-zero, then
 * we will load and start using the new config.  You can simply
 * validate config without making any changes by setting apply
 * to 0.
 *
 * Returns 0 if the entry is valid and -1 if it is invalid.
 */
static int
linked_attrs_parse_config_entry(Slapi_Entry * e, int apply)
{
    char *value;
    struct configEntry *entry = NULL;
    struct configEntry *config_entry;
    PRCList *list;
    int entry_added = 0;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_parse_config_entry\n");

    /* If this is the main plug-in
     * config entry, just bail. */
    if (strcasecmp(linked_attrs_get_plugin_dn(), slapi_entry_get_ndn(e)) == 0) {
        ret = -1;
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
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_parse_config_entry: Error "
                        "reading dn from config entry\n");
        ret = -1;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                    "----------> dn [%s]\n", entry->dn);

    value = slapi_entry_attr_get_charptr(e, LINK_LINK_TYPE);
    if (value) {
        int not_dn_syntax = 0;
        char *syntaxoid = NULL;
        Slapi_Attr *attr = slapi_attr_new();

        /* Set this first so we free it if we encounter an error */
        entry->linktype = value;

        /* Gather some information about this attribute. */
        slapi_attr_init(attr, value);
        slapi_attr_get_syntax_oid_copy(attr, &syntaxoid );
        not_dn_syntax = strcmp(syntaxoid, DN_SYNTAX_OID);
        slapi_ch_free_string(&syntaxoid);
        slapi_attr_free(&attr);

        /* Check if the link type's syntax is Distinguished Name.
         * We only treat this as a warning. */
        if (not_dn_syntax) {
            slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                            "linked_attrs_parse_config_entry: The %s config "
                            "setting must be set to an attribute with the "
                            "Distinguished Name syntax for linked attribute "
                            "pair \"%s\".\n", LINK_LINK_TYPE, entry->dn);
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_parse_config_entry: The %s config "
                        "setting is required for linked attribute pair \"%s\".\n",
                        LINK_LINK_TYPE, entry->dn);
        ret = -1;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", LINK_LINK_TYPE, entry->linktype);

    value = slapi_entry_attr_get_charptr(e, LINK_MANAGED_TYPE);
    if (value) {
        int single_valued = 0;
        int not_dn_syntax = 0;
        char *syntaxoid = NULL;
        Slapi_Attr *attr = slapi_attr_new();

        /* Set this first so we free it if we encounter an error */
        entry->managedtype = value;

        /* Gather some information about this attribute. */
        slapi_attr_init(attr, value);
        slapi_attr_get_syntax_oid_copy(attr, &syntaxoid );
        not_dn_syntax = strcmp(syntaxoid, DN_SYNTAX_OID);
        single_valued = slapi_attr_flag_is_set(attr, SLAPI_ATTR_FLAG_SINGLE);
        slapi_ch_free_string(&syntaxoid);
        slapi_attr_free(&attr);

        /* Ensure that the managed type is a multi-valued attribute. */
        if (single_valued) {
            slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                            "linked_attrs_parse_config_entry: The %s config "
                            "setting must be set to a multi-valued attribute "
                            "for linked attribute pair \"%s\".\n",
                            LINK_MANAGED_TYPE, entry->dn);
            ret = -1;
            goto bail;
        /* Check if the link type's syntax is Distinguished Name.
         * We only treat this as a warning. */
        } else if (not_dn_syntax) {
            slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                            "linked_attrs_parse_config_entry: The %s config "
                            "setting must be set to an attribute with the "
                            "Distinguished Name syntax for linked attribute "
                            "pair \"%s\".\n", LINK_MANAGED_TYPE, entry->dn);
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_parse_config_entry: The %s config "
                        "setting is required for linked attribute pair \"%s\".\n",
                        LINK_MANAGED_TYPE, entry->dn);
        ret = -1;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", LINK_MANAGED_TYPE,
                    entry->managedtype);

    /* A scope is not required.  No scope means it
     * applies to any part of the DIT. */
    value = slapi_entry_attr_get_charptr(e, LINK_SCOPE);
    if (value) {
        entry->scope = value;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", LINK_SCOPE,
                    entry->scope ? entry->scope : "NULL");

    /* Check if config already exists for
     * the link type at the same scope. */
    if (linked_attrs_config_exists(entry)) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_parse_config_entry: A config "
                        "entry for the link attribute %s already "
                        "exists at a scope of \"%s\".\n", entry->linktype,
                        entry->scope);
        ret = -1;
        goto bail;
    }

    /* Check if config already exists for
     * the managed type at the same scope. */
    if (linked_attrs_config_exists_reverse(entry)) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_parse_config_entry: A config "
                        "entry for the managed attribute %s already "
                        "exists at a scope of \"%s\".\n", entry->managedtype,
                        entry->scope);
        ret = -1;
        goto bail;
    }

    /* If we were only called to validate config, we can
     * just bail out before applying the config changes */
    if (apply == 0) {
        goto bail;
    }

    /* Create a lock for this attribute pair. */
    entry->lock = slapi_new_mutex();
    if (!entry->lock) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_parse_config_entry: Unable to create "
                        "lock for linked attribute pair \"%s\".\n", entry->dn);
        ret = -1;
        goto bail;
    }

    /* Add the entry to the list.  We group by link type. We
     * also maintain a reverse list grouped by managed type. */
    if (!PR_CLIST_IS_EMPTY(g_link_config)) {
        list = PR_LIST_HEAD(g_link_config);
        while (list != g_link_config) {
            config_entry = (struct configEntry *) list;

            /* See if the types match.  We want to group
             * entries for the same link type together. */
            if (slapi_attr_type_cmp(config_entry->linktype, entry->linktype, 1) == 0) {
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                                "store [%s] before [%s] \n", entry->dn,
                                config_entry->dn);

                /* add to managed type index */
                linked_attrs_insert_config_index(entry);

                entry_added = 1;
                break;
            }

            list = PR_NEXT_LINK(list);

            if (g_link_config == list) {
                /* add to tail */
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                                "store [%s] at tail\n", entry->dn);

                /* add to managed type index */
                linked_attrs_insert_config_index(entry);

                entry_added = 1;
                break;
            }
        }
    } else {
        /* first entry */
        PR_INSERT_LINK(&(entry->list), g_link_config);
        slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                        "store [%s] at head \n", entry->dn);

        /* add to managed type index */
        linked_attrs_insert_config_index(entry);

        entry_added = 1;
    }

  bail:
    if (0 == entry_added) {
        /* Don't log error if we weren't asked to apply config */
        if ((apply != 0) && (entry != NULL)) {
            slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                            "linked_attrs_parse_config_entry: Invalid config entry "
                            "[%s] skipped\n", entry->dn);
        }
        linked_attrs_free_config_entry(&entry);
    } else {
        ret = 0;
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_parse_config_entry\n");

    return ret;
}

/*
 * linked_attrs_insert_config_index()
 *
 * Adds an entry to the ordered config index.  We maintain
 * an list of pointers to the cached config entries that is
 * grouped by managed type. We use this index to find the
 * appropriate config entry when given a backpointer.  This
 * is useful for the case where an entry with backpointers
 * is renamed and we need to updated the forward link.
 */
static void 
linked_attrs_insert_config_index(struct configEntry *entry)
{
    struct configEntry *config_entry = NULL;
    struct configIndex *index_entry = NULL;
    PRCList *list = PR_LIST_HEAD(g_managed_config_index);

    index_entry = (struct configIndex *)slapi_ch_calloc(1, sizeof(struct configIndex));
    index_entry->config = entry;

    if (!PR_CLIST_IS_EMPTY(g_managed_config_index)) {
        while (list != g_managed_config_index) {
            config_entry = ((struct configIndex *)list)->config;
    
            /* See if the types match. */
            if (slapi_attr_type_cmp(config_entry->managedtype, entry->managedtype, 1) == 0) {
                PR_INSERT_BEFORE(&(index_entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                                "store [%s] before [%s] \n", entry->dn,
                                config_entry->dn);
                break;
            }
    
            list = PR_NEXT_LINK(list);

            if (g_managed_config_index == list) {
                /* add to tail */
                PR_INSERT_BEFORE(&(index_entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                                "store [%s] at tail\n", entry->dn);
                break;
            }
        }
    } else {
        /* first entry */
        slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                        "store [%s] at head \n", entry->dn);
        PR_INSERT_LINK(&(index_entry->list), g_managed_config_index);
    }
}

static void
linked_attrs_free_config_entry(struct configEntry ** entry)
{
    struct configEntry *e = *entry;

    if (e == NULL)
        return;

    if (e->dn) {
        slapi_log_error(SLAPI_LOG_CONFIG, LINK_PLUGIN_SUBSYSTEM,
                        "freeing config entry [%s]\n", e->dn);
        slapi_ch_free_string(&e->dn);
    }

    if (e->linktype)
        slapi_ch_free_string(&e->linktype);

    if (e->managedtype)
        slapi_ch_free_string(&e->managedtype);

    if (e->scope)
        slapi_ch_free_string(&e->scope);

    if (e->lock)
        slapi_destroy_mutex(e->lock);

    slapi_ch_free((void **) entry);
}

static void
linked_attrs_delete_configEntry(PRCList *entry)
{
    PR_REMOVE_LINK(entry);
    linked_attrs_free_config_entry((struct configEntry **) &entry);
}

static void
linked_attrs_delete_config()
{
    PRCList *list;

    /* Delete the config cache. */
    while (!PR_CLIST_IS_EMPTY(g_link_config)) {
        list = PR_LIST_HEAD(g_link_config);
        linked_attrs_delete_configEntry(list);
    }

    /* Delete the reverse index. */
    while (!PR_CLIST_IS_EMPTY(g_managed_config_index)) {
        list = PR_LIST_HEAD(g_managed_config_index);
        PR_REMOVE_LINK(list);
        slapi_ch_free((void **)&list);
    }

    return;
}


/*
 * Helper functions
 */
static char *
linked_attrs_get_dn(Slapi_PBlock * pb)
{
    const char *dn = 0;
    Slapi_DN *sdn = NULL;
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_get_dn\n");

    if (slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn)) {
        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_get_dn: failed to get dn of changed entry");
        goto bail;
    }
    dn = slapi_sdn_get_dn(sdn);

  bail:
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_get_dn\n");

    return (char *)dn;
}

static Slapi_DN *
linked_attrs_get_sdn(Slapi_PBlock * pb)
{
    Slapi_DN *sdn = 0;
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_get_sdn\n");
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_get_sdn\n");

    return sdn;
}

/*
 * linked_attrs_dn_is_config()
 *
 * Checks if dn is a linked attribute config entry.
 */
static int
linked_attrs_dn_is_config(char *dn)
{
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_dn_is_config\n");

    /* Return 1 if the passed in dn is a child of the main
     * plugin config entry. */
    if (slapi_dn_issuffix(dn, linked_attrs_get_plugin_dn()) &&
        strcasecmp(dn, linked_attrs_get_plugin_dn())) {
        ret = 1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_dn_is_config\n");

    return ret;
}

/*
 * linked_attrs_find_config()
 *
 * Finds the appropriate config entry for a given dn and
 * link type.  A read lock must be held on the config
 * before calling this function.  The configEntry that is
 * returned is a pointer to the actual config entry in
 * the config cache.  It should not be modified in any
 * way.  The read lock should not be released until you
 * are finished with the config entry that is returned.

 * Returns NULL if no applicable config entry is found.
 */
static void
linked_attrs_find_config(const char *dn,
    const char *type, struct configEntry **config)
{
    int found_type = 0;
    PRCList *list = NULL;

    *config = NULL;

    if (!PR_CLIST_IS_EMPTY(g_link_config)) {
        list = PR_LIST_HEAD(g_link_config);
        while (list != g_link_config) {
            if (slapi_attr_type_cmp(((struct configEntry *)list)->linktype,
                type, 1) == 0) {
                /* Set a flag indicating that we found a config entry
                 * for this type. We use this flag so we can stop
                 * processing early if we don't find a matching scope. */
                found_type = 1;

                /* Check if the dn is in the scope of this config
                 * entry.  If the config entry doesn't have a scope
                 * (global), consider it a match.  If we have a match,
                 * we can stop processing the config. */
                if ((((struct configEntry *)list)->scope == NULL) ||
                    (slapi_dn_issuffix(dn, ((struct configEntry *)list)->scope))) {
                    *config = (struct configEntry *)list;
                    break;
                }
            } else {
                /* If flag is set, we're done.  We have configured links
                 * for this type, but none of the scopes match. */
                if (found_type) {
                    break;
                }
            }

            list = PR_NEXT_LINK(list);
        }
    }
}

/*
 * linked_attrs_find_config_reverse()
 *
 * Finds the appropriate config entry for a given dn and
 * managed type.  A read lock must be held on the config 
 * before calling this function.  The configEntry that is
 * returned is a pointer to the actual config entry in
 * the config cache.  It should not be modified in any
 * way.  The read lock should not be released until you
 * are finished with the config entry that is returned.

 * Returns NULL if no applicable config entry is found.
 */ 
static void
linked_attrs_find_config_reverse(const char *dn,
    const char *type, struct configEntry **config)
{   
    int found_type = 0;
    PRCList *list = NULL;

    *config = NULL;

    if (!PR_CLIST_IS_EMPTY(g_managed_config_index)) {
        list = PR_LIST_HEAD(g_managed_config_index);
        while (list != g_managed_config_index) {
            if (slapi_attr_type_cmp(((struct configIndex *)list)->config->managedtype,
                                    type, 1) == 0) {
                /* Set a flag indicating that we found a config entry
                 * for this type. We use this flag so we can stop
                 * processing early if we don't find a matching scope. */
                found_type = 1;

                /* Check if the dn is in the scope of this config
                 * entry.  If the config entry doesn't have a scope
                 * (global), consider it a match.  If we have a match,
                 * we can stop processing the config. */
                if ((((struct configIndex *)list)->config->scope == NULL) ||
                    (slapi_dn_issuffix(dn, ((struct configIndex *)list)->config->scope))) {
                    *config = ((struct configIndex *)list)->config;
                    break;
                }
            } else {
                /* If flag is set, we're done.  We have configured links
                 * for this type, but none of the scopes match. */
                if (found_type) {
                    break;
                }
            }

            list = PR_NEXT_LINK(list);
        }
    }
}

/*
 * linked_attrs_config_index_has_type()
 *
 * Returns 1 if a config entry exists with the passed
 * in managed type.
 *
 * A read lock on the config must be held before calling
 * this function.
 */
static int
linked_attrs_config_index_has_type(char *type)
{
    int rc = 0;
    PRCList *list = NULL;

    if (!PR_CLIST_IS_EMPTY(g_managed_config_index)) {
        list = PR_LIST_HEAD(g_managed_config_index);
        while (list != g_managed_config_index) {
            if (slapi_attr_type_cmp(((struct configIndex *)list)->config->managedtype,
                                    type, 1) == 0) {
                rc = 1;
                break;
            }

            list = PR_NEXT_LINK(list);
        }
    }

    return rc;
}

/*
 * linked_attrs_config_exists()
 *
 * Returns 1 if a config entry exists in the cache
 * already for the given link type at the given scope.
 * This will detect if the cached config entry is really
 * the same one as the passed in entry by comparing the
 * dn of the config entry.  We will still return 0 in
 * this case as it's one and the same config entry. We
 * really want to use this to prevent multiple config
 * entries for the same link type at the same scope.
 *
 * A read lock on the config must be held before calling
 * this function.
 */
static int
linked_attrs_config_exists(struct configEntry *entry)
{
    int rc = 0;
    int found_type = 0;
    PRCList *list = NULL;

    if (!PR_CLIST_IS_EMPTY(g_link_config)) {
        list = PR_LIST_HEAD(g_link_config);
        while (list != g_link_config) {
            if (slapi_attr_type_cmp(((struct configEntry *)list)->linktype,
                                    entry->linktype, 1) == 0) {
                found_type = 1;
                /* We don't allow nested config for the same type.  We
                 * need to check for nesting in both directions here. 
                 * If no scope is set, we consider the entry global. */
                if ((((struct configEntry *)list)->scope == NULL) ||
                    slapi_dn_issuffix(entry->scope, ((struct configEntry *)list)->scope) ||
                    slapi_dn_issuffix(((struct configEntry *)list)->scope, entry->scope)) {
                    /* Make sure that this isn't the same exact entry
                     * in the list already.  This can happen if a config
                     * entry is being modified.  Both of these were already
                     * normalized when the config struct was filled in. */
                    if (strcasecmp(entry->dn, ((struct configEntry *)list)->dn) != 0) {
                        rc = 1;
                        break;
                    }
                }
            } else {
                if (found_type) {
                    /* Since the list is sorted by link type, we
                     * are finished if we get here since we found
                     * the type but didn't match the scope. */
                    break;
                }
            }

            list = PR_NEXT_LINK(list);
        }
    }

    return rc;
}

/*
 * linked_attrs_config_exists_reverse()
 *
 * Returns 1 if a config entry exists in the cache
 * already for the given managed type at the given scope.
 * This will detect if the cached config entry is really
 * the same one as the passed in entry by comparing the
 * dn of the config entry.  We will still return 0 in
 * this case as it's one and the same config entry. We
 * really want to use this to prevent multiple config
 * entries for the same managed type at the same scope.
 *
 * A read lock on the config must be held before calling
 * this function.
 */
static int
linked_attrs_config_exists_reverse(struct configEntry *entry)
{
    int rc = 0;
    int found_type = 0;
    PRCList *list = NULL;

    if (!PR_CLIST_IS_EMPTY(g_managed_config_index)) {
        list = PR_LIST_HEAD(g_managed_config_index);
        while (list != g_managed_config_index) {
            if (slapi_attr_type_cmp(((struct configIndex *)list)->config->managedtype,
                                    entry->managedtype, 1) == 0) {
                found_type = 1;
                /* We don't allow nested config for the same type.  We
                 * need to check for nesting in both directions here. */
                if ((((struct configIndex *)list)->config->scope == NULL) ||
                    slapi_dn_issuffix(entry->scope,
                    ((struct configIndex *)list)->config->scope) ||
                    slapi_dn_issuffix(((struct configIndex *)list)->config->scope,
                    entry->scope)) {
                    /* Make sure that this isn't the same exact entry
                     * in the list already.  This can happen if a config
                     * entry is being modified.  Both of these were already
                     * normalized when the config struct was filled in. */
                    if (strcasecmp(entry->dn, ((struct configIndex *)list)->config->dn) != 0) {
                        rc = 1;
                        break;
                    }
                }
            } else {
                if (found_type) {
                    /* Since the list is sorted by link type, we
                     * are finished if we get here since we found
                     * the type but didn't match the scope. */
                    break;
                }
            }

            list = PR_NEXT_LINK(list);
        }
    }

    return rc;
}

/*
 * linked_attrs_oktodo()
 *
 * Check if we want to process this operation.  We need to be
 * sure that the operation succeeded.  We also respond to replicated
 * ops so we don't test for that.  This does require that the managed
 * attributes not be replicated.
 */
static int
linked_attrs_oktodo(Slapi_PBlock *pb)
{
        int ret = 1;
        int oprc = 0;

        slapi_log_error( SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                     "--> linked_attrs_oktodo\n" );

        if(slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0)
        {
                slapi_log_error( SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_oktodo: could not get parameters\n" );
                ret = -1;
        }

        /* This plugin should only execute if the operation succeeded. */
        if(oprc != 0)
        {
                ret = 0;
        }

        slapi_log_error( SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                     "<-- linked_attrs_oktodo\n" );

        return ret;
}

/* linked_attrs_load_array()
 * 
 * put attribute values in array structure
 */
void
linked_attrs_load_array(Slapi_Value **array, Slapi_Attr *attr)
{
        Slapi_Value *val = 0;
        int hint = slapi_attr_first_value(attr, &val);

        while(val)
        {
                *array = val;
                array++;
                hint = slapi_attr_next_value(attr, hint, &val);
        }
}

/* linked_attrs_compare()
 * 
 * Compare two attr values using the DN syntax.
 */
int
linked_attrs_compare(const void *a, const void *b)
{
        int rc = 0;
        Slapi_Value *val1 = *((Slapi_Value **)a);
        Slapi_Value *val2 = *((Slapi_Value **)b);
        Slapi_Attr *linkattr = slapi_attr_new();

        slapi_attr_init(linkattr, "distinguishedName");

        rc = slapi_attr_value_cmp(linkattr,
                slapi_value_get_berval(val1),
                slapi_value_get_berval(val2));

        slapi_attr_free(&linkattr);

        return rc;
}

/*
 * linked_attrs_add_backpointers()
 *
 * Adds backpointers pointing to dn to the entries referred to
 * by the values in smod.
 */
static void
linked_attrs_add_backpointers(char *linkdn, struct configEntry *config,
    Slapi_Mod *smod)
{
    Slapi_ValueSet *vals = slapi_valueset_new();

    slapi_valueset_set_from_smod(vals, smod);
    linked_attrs_mod_backpointers(linkdn, config->managedtype, config->scope,
                                  LDAP_MOD_ADD, vals);

    slapi_valueset_free(vals);
}

/*
 * linked_attrs_del_backpointers()
 *
 * Remove backpointers pointing to linkdn in the entries referred
 * to by the values in smod.
 */
static void
linked_attrs_del_backpointers(Slapi_PBlock *pb, char *linkdn,
    struct configEntry *config, Slapi_Mod *smod)
{
    Slapi_ValueSet *vals = NULL;

    /* If no values are listed in the smod, we need to get
     * a list of all of the values that were deleted by
     * looking at the pre-op copy of the entry. */
    if (slapi_mod_get_num_values(smod) == 0) {
        Slapi_Entry *pre_e = NULL;
        Slapi_Attr *pre_attr = NULL;

        slapi_pblock_get( pb, SLAPI_ENTRY_PRE_OP, &pre_e );
        slapi_entry_attr_find( pre_e, config->linktype, &pre_attr );
        slapi_attr_get_valueset(pre_attr, &vals);
    } else {
        vals = slapi_valueset_new();
        slapi_valueset_set_from_smod(vals, smod);
    }

    linked_attrs_mod_backpointers(linkdn, config->managedtype, config->scope,
                                  LDAP_MOD_DELETE, vals);

    slapi_valueset_free(vals);
}

/*
 * linked_attrs_replace_backpointers()
 *
 * Remove backpointers pointing to linkdn from the entries
 * whose values were deleted in smod and add backpointers
 * for any new values that were added as a part of the
 * replace operation.
 */
static void
linked_attrs_replace_backpointers(Slapi_PBlock *pb, char *linkdn,
    struct configEntry *config, Slapi_Mod *smod)
{
    Slapi_Entry *pre_e = NULL;
    Slapi_Entry *post_e = NULL;
    Slapi_Attr *pre_attr = 0;
    Slapi_Attr *post_attr = 0;

    /* Get the pre and post copy of the entry to see
     * what values have been added and removed. */
    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &pre_e);
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_e);

    if(pre_e && post_e) {
        slapi_entry_attr_find(pre_e, config->linktype, &pre_attr);
        slapi_entry_attr_find(post_e, config->linktype, &post_attr);
    }

    if(pre_attr || post_attr) {
        int pre_total = 0;
        int post_total = 0;
        Slapi_Value **pre_array = 0;
        Slapi_Value **post_array = 0;
        int pre_index = 0;
        int post_index = 0;
        Slapi_ValueSet *addvals = NULL;
        Slapi_ValueSet *delvals = NULL;

        /* create arrays of values */
        if(pre_attr) {
            slapi_attr_get_numvalues(pre_attr, &pre_total);
        }

        if(post_attr) {
            slapi_attr_get_numvalues(post_attr, &post_total);
        }

        if(pre_total) {
            pre_array = (Slapi_Value**) slapi_ch_malloc(sizeof(Slapi_Value*)*pre_total);
            linked_attrs_load_array(pre_array, pre_attr);
            qsort(pre_array, pre_total, sizeof(Slapi_Value*), linked_attrs_compare);
        }

        if(post_total) {
            post_array = (Slapi_Value**) slapi_ch_malloc(sizeof(Slapi_Value*)*post_total);
            linked_attrs_load_array(post_array, post_attr);
            qsort(post_array, post_total, sizeof(Slapi_Value*), linked_attrs_compare);
        }

        /* Work through arrays, following these rules:
         *   - in pre, in post, do nothing
         *   - in pre, not in post, delete from entry
         *   - not in pre, in post, add to entry
         */
        while(pre_index < pre_total || post_index < post_total) {
            if(pre_index == pre_total) {
                /* add the rest of post */
                if (addvals == NULL) {
                    addvals = slapi_valueset_new();
                }

                slapi_valueset_add_value(addvals, post_array[post_index]);
                post_index++;
            } else if(post_index == post_total) {
                /* delete the rest of pre */
                if (delvals == NULL) {
                    delvals = slapi_valueset_new();
                }

                slapi_valueset_add_value(delvals, pre_array[pre_index]);
                pre_index++;
            } else {
                /* decide what to do */
                int cmp = linked_attrs_compare(&(pre_array[pre_index]),
                                               &(post_array[post_index]));

                if(cmp < 0) {
                    /* delete pre array */
                    if (delvals == NULL) {
                        delvals = slapi_valueset_new();
                    }

                    slapi_valueset_add_value(delvals, pre_array[pre_index]);
                    pre_index++;
                } else if(cmp > 0) {
                    /* add post array */
                    if (addvals == NULL) {
                        addvals = slapi_valueset_new();
                    }

                    slapi_valueset_add_value(addvals, post_array[post_index]);
                    post_index++;
                } else {
                    /* do nothing, advance */
                    pre_index++;
                    post_index++;
                }
            }
        }

        /* Perform the actual updates to the target entries. */
        if (delvals) {
            linked_attrs_mod_backpointers(linkdn, config->managedtype,
                                          config->scope, LDAP_MOD_DELETE, delvals);
            slapi_valueset_free(delvals);
        }

        if (addvals) {
            linked_attrs_mod_backpointers(linkdn, config->managedtype,
                                          config->scope, LDAP_MOD_ADD, addvals);
            slapi_valueset_free(addvals);
        }

        slapi_ch_free((void **)&pre_array);
        slapi_ch_free((void **)&post_array);
    }
}

/*
 * linked_attrs_mod_backpointers()
 *
 * Performs backpointer management.
 */
static void
linked_attrs_mod_backpointers(char *linkdn, char *type,
    char *scope, int modop, Slapi_ValueSet *targetvals)
{
    char *val[2];
    int i = 0;
    Slapi_PBlock *mod_pb = slapi_pblock_new();
    LDAPMod mod;
    LDAPMod *mods[2];
    Slapi_Value *targetval = NULL;

    /* Setup the modify operation.  Only the target will
     * change, so we only need to do this once. */
    val[0] = linkdn;
    val[1] = 0;

    mod.mod_op = modop;
    mod.mod_type = type;
    mod.mod_values = val;

    mods[0] = &mod;
    mods[1] = 0;

    i = slapi_valueset_first_value(targetvals, &targetval);
    while(targetval)
    {
        int perform_update = 0;
        const char *targetdn = slapi_value_get_string(targetval);
        Slapi_DN *targetsdn = slapi_sdn_new_dn_byref(targetdn);

        /* If we have a scope, only update the target if it is within
         * the scope.  If we don't have a scope, only update the target
         * if it is in the same backend as the linkdn. */
        if (scope) {
            perform_update = slapi_dn_issuffix(targetdn, scope);
        } else {
            Slapi_Backend *be = NULL;
            Slapi_DN *linksdn = slapi_sdn_new_dn_byref(linkdn);

            if ((be = slapi_be_select(linksdn))) {
                perform_update = slapi_sdn_issuffix(targetsdn, slapi_be_getsuffix(be, 0));
            }

            slapi_sdn_free(&linksdn);
        }

        if (perform_update) {
            slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                            "%s backpointer (%s) in entry (%s)\n",
                            (modop == LDAP_MOD_ADD) ? "Adding" : "Removing",
                            linkdn, targetdn);

            /* Perform the modify operation. */
            slapi_modify_internal_set_pb_ext(mod_pb, targetsdn, mods, 0, 0,
                                             linked_attrs_get_plugin_id(), 0);
            slapi_modify_internal_pb(mod_pb);

            /* Initialize the pblock so we can reuse it. */
            slapi_pblock_init(mod_pb);
        }
        slapi_sdn_free(&targetsdn);

        i = slapi_valueset_next_value(targetvals, i, &targetval);
    }

    slapi_pblock_destroy(mod_pb);
}


/*
 * Operation callback functions
 */

/*
 * linked_attrs_pre_op()
 *
 * Checks if an operation is modifying the linked
 * attribute config and validates the config changes.
 */
static int
linked_attrs_pre_op(Slapi_PBlock * pb, int modop)
{
    char *dn = 0;
    Slapi_Entry *e = 0;
    LDAPMod **mods = NULL;
    int free_entry = 0;
    char *errstr = NULL;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_pre_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started)
        goto bail;

    if (0 == (dn = linked_attrs_get_dn(pb)))
        goto bail;

    if (linked_attrs_dn_is_config(dn)) {
        /* Validate config changes, but don't apply them.
         * This allows us to reject invalid config changes
         * here at the pre-op stage.  Applying the config
         * needs to be done at the post-op stage. */

        if (LDAP_CHANGETYPE_ADD == modop) {
            slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
        } else {
            /* Fetch the entry being modified so we can
             * create the resulting entry for validation. */
            /* int free_sdn = 0; */
            Slapi_DN *tmp_dn = linked_attrs_get_sdn(pb);
            if (tmp_dn) {
                slapi_search_internal_get_entry(tmp_dn, 0, &e, linked_attrs_get_plugin_id());
                free_entry = 1;
            }

            /* If the entry doesn't exist, just bail and
             * let the server handle it. */
            if (e == NULL) {
                goto bail;
            }

            /* Grab the mods. */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            /* Apply the  mods to create the resulting entry. */
            if (mods && (slapi_entry_apply_mods(e, mods) != LDAP_SUCCESS)) {
                /* The mods don't apply cleanly, so we just let this op go
                 * to let the main server handle it. */
                goto bail;
            }
        }

        if (linked_attrs_parse_config_entry(e, 0) != 0) {
            /* Refuse the operation if config parsing failed. */
            ret = LDAP_UNWILLING_TO_PERFORM;
            if (LDAP_CHANGETYPE_ADD == modop) {
                errstr = slapi_ch_smprintf("Not a valid linked attribute configuration entry.");
            } else {
                errstr = slapi_ch_smprintf("Changes result in an invalid "
                                           "linked attribute configuration.");
            }
        }
    }

  bail:
    if (free_entry && e)
        slapi_entry_free(e);

    if (ret) {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_pre_op: operation failure [%d]\n", ret);
        slapi_send_ldap_result(pb, ret, NULL, errstr, 0, NULL);
        slapi_ch_free((void **)&errstr);
        ret = -1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_pre_op\n");

    return ret;
}

static int
linked_attrs_add_pre_op(Slapi_PBlock * pb)
{
    return linked_attrs_pre_op(pb, LDAP_CHANGETYPE_ADD);
}

static int
linked_attrs_mod_pre_op(Slapi_PBlock * pb)
{
    return linked_attrs_pre_op(pb, LDAP_CHANGETYPE_MODIFY);
}

static int
linked_attrs_mod_post_op(Slapi_PBlock *pb)
{
    Slapi_Mods *smods = NULL;
    Slapi_Mod *smod = NULL;
    LDAPMod **mods;
    Slapi_Mod *next_mod = NULL;
    char *dn = NULL;
    struct configEntry *config = NULL;
    void *caller_id = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_mod_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started)
        return 0;

    /* We don't want to process internal modify
     * operations that originate from this plugin.
     * Doing so could cause a deadlock. */
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &caller_id);

    if (caller_id == linked_attrs_get_plugin_id()) {
        /* Just return without processing */
        return 0;
    }

    if (linked_attrs_oktodo(pb) &&
        (dn = linked_attrs_get_dn(pb))) {
        /* First check if the config is being modified. */
        if (linked_attrs_dn_is_config(dn)) {
            linked_attrs_load_config();
        }

        /* get the mod set */
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
        smods = slapi_mods_new();
        slapi_mods_init_byref(smods, mods);

        next_mod = slapi_mod_new();
        smod = slapi_mods_get_first_smod(smods, next_mod);
        while(smod) {
            char *type = (char *)slapi_mod_get_type(smod);

            /* See if there is an applicable link configured. */
            linked_attrs_read_lock();

            /* Bail out if the plug-in close function was just called. */
            if (!g_plugin_started) {
                linked_attrs_unlock();
                return 0;
            }

            linked_attrs_find_config(dn, type, &config);

            /* If we have a matching config entry, we have
             * work to do. If not, we can go to the next smod. */
            if (config) {
                int op = slapi_mod_get_operation(smod);

                /* Prevent other threads from managing 
                 * this specific link at the same time. */
                slapi_lock_mutex(config->lock);

                switch(op & ~LDAP_MOD_BVALUES) {
                case LDAP_MOD_ADD:
                    /* Find the entries pointed to by the new
                     * values and add the backpointers. */
                    linked_attrs_add_backpointers(dn, config, smod);
                    break;
                case LDAP_MOD_DELETE:
                    /* Find the entries pointed to by the deleted
                     * values and remove the backpointers. */
                    linked_attrs_del_backpointers(pb, dn, config, smod);
                    break;
                case LDAP_MOD_REPLACE:
                    /* Find the entries pointed to by the deleted
                     * values and remove the backpointers.  If
                     * any new values are being added, find those
                     * entries and add the backpointers. */
                    linked_attrs_replace_backpointers(pb, dn, config, smod);
                    break;
                default:
                    slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                                    "linked_attrs_mod_post_op: unknown mod type\n" );
                    break;
                }

                slapi_unlock_mutex(config->lock);
            }

            config = NULL;
            linked_attrs_unlock();
            slapi_mod_done(next_mod);
            smod = slapi_mods_get_next_smod(smods, next_mod);
        }

        slapi_mod_free(&next_mod);
        slapi_mods_free(&smods);
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_mod_post_op\n");

    return 0;
}

static int
linked_attrs_add_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    char *dn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_add_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !linked_attrs_oktodo(pb))
        return 0;

    /* Reload config if a config entry was added. */
    if ((dn = linked_attrs_get_dn(pb))) {
        if (linked_attrs_dn_is_config(dn))
            linked_attrs_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_add_post_op: Error "
                        "retrieving dn\n");
    }

    /* Get the newly added entry. */
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);

    if (e) {
        Slapi_Attr *attr = NULL;
        char *type = NULL;
        struct configEntry *config = NULL;

        slapi_entry_first_attr(e, &attr);
        while (attr) {
            slapi_attr_get_type(attr, &type);

            /* See if there is an applicable link configured. */
            linked_attrs_read_lock();

            /* Bail out if the plug-in close function was just called. */
            if (!g_plugin_started) {
                linked_attrs_unlock();
                return 0;
            }

            linked_attrs_find_config(dn, type, &config);

            /* If config was found, add the backpointers to this entry. */
            if (config) {
                Slapi_ValueSet *vals = NULL;

                slapi_attr_get_valueset(attr, &vals);

                slapi_lock_mutex(config->lock);

                linked_attrs_mod_backpointers(dn, config->managedtype,
                                              config->scope, LDAP_MOD_ADD, vals);

                slapi_unlock_mutex(config->lock);

                slapi_valueset_free(vals);
            }

            config = NULL;
            linked_attrs_unlock();

            slapi_entry_next_attr(e, attr, &attr);
        }
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_add_post_op: Error "
                        "retrieving post-op entry %s\n", dn);
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_add_post_op\n");

    return 0;
}

static int
linked_attrs_del_post_op(Slapi_PBlock *pb)
{
    char *dn = NULL;
    Slapi_Entry *e = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_del_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !linked_attrs_oktodo(pb))
        return 0;

    /* Reload config if a config entry was deleted. */
    if ((dn = linked_attrs_get_dn(pb))) {
        if (linked_attrs_dn_is_config(dn))
            linked_attrs_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_del_post_op: Error "
                        "retrieving dn\n");
    }

    /* Get deleted entry, then go through types to find config. */
    slapi_pblock_get( pb, SLAPI_ENTRY_PRE_OP, &e );

    if (e) {
        Slapi_Attr *attr = NULL;
        char *type = NULL;
        struct configEntry *config = NULL;

        slapi_entry_first_attr(e, &attr);
        while (attr) {
            slapi_attr_get_type(attr, &type);

            /* See if there is an applicable link configured. */
            linked_attrs_read_lock();

            /* Bail out if the plug-in close function was just called. */
            if (!g_plugin_started) {
                linked_attrs_unlock();
                return 0;
            }

            linked_attrs_find_config(dn, type, &config);

            /* If config was found, delete the backpointers to this entry. */
            if (config) {
                Slapi_ValueSet *vals = NULL;

                slapi_attr_get_valueset(attr, &vals);

                slapi_lock_mutex(config->lock);

                linked_attrs_mod_backpointers(dn, config->managedtype,
                                              config->scope, LDAP_MOD_DELETE, vals);

                slapi_unlock_mutex(config->lock);

                slapi_valueset_free(vals);
            }

            config = NULL;

            /* See if any of the values for this attribute are managed
             * backpointers.  We need to remove the forward link if so. */
            if (linked_attrs_config_index_has_type(type)) {
                int hint = 0;
                Slapi_Value *val = NULL;

                /* Loop through values and see if we have matching config */
                hint = slapi_attr_first_value(attr, &val);
                while (val) {
                    linked_attrs_find_config_reverse(slapi_value_get_string(val),
                                                      type, &config);

                    if (config) {
                        Slapi_ValueSet *vals = slapi_valueset_new();
                        slapi_valueset_add_value(vals, val);

                        slapi_lock_mutex(config->lock);

                        /* Delete forward link value. */
                        linked_attrs_mod_backpointers(dn, config->linktype,
                                                      config->scope, LDAP_MOD_DELETE, vals);

                        slapi_unlock_mutex(config->lock);

                        slapi_valueset_free(vals);
                        config = NULL;
                    }

                    hint = slapi_attr_next_value(attr, hint, &val);
                }
            }

            linked_attrs_unlock();

            slapi_entry_next_attr(e, attr, &attr);
        }
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_del_post_op: Error "
                        "retrieving pre-op entry %s\n", dn);
    }

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_del_post_op\n");

    return 0;
}

static int
linked_attrs_modrdn_post_op(Slapi_PBlock *pb)
{
    char *old_dn = NULL;
    char *new_dn = NULL;
    Slapi_Entry *post_e = NULL;
    Slapi_Attr *attr = NULL;
    char *type = NULL;
    struct configEntry *config = NULL;
    int rc = 0;

    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "--> linked_attrs_modrdn_post_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started || !linked_attrs_oktodo(pb)) {
        goto done;
    }

    /* Reload config if an existing config entry was renamed,
     * or if the new dn brings an entry into the scope of the
     * config entries. */
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_e);
    if (post_e) {
        new_dn = slapi_entry_get_ndn(post_e);
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_modrdn_post_op: Error "
                        "retrieving post-op entry\n");
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    if ((old_dn = linked_attrs_get_dn(pb))) {
        if (linked_attrs_dn_is_config(old_dn) || linked_attrs_dn_is_config(new_dn))
            linked_attrs_load_config();
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                        "linked_attrs_modrdn_post_op: Error "
                        "retrieving dn\n");
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    /* Check if this operation requires any updates to links. */
    slapi_entry_first_attr(post_e, &attr);
    while (attr) {
        slapi_attr_get_type(attr, &type);

        /* See if there is an applicable link configured. */
        linked_attrs_read_lock();

        /* Bail out if the plug-in close function was just called. */
        if (!g_plugin_started) {
            linked_attrs_unlock();
            return 0;
        }

        linked_attrs_find_config(old_dn, type, &config);

        /* If config was found for the old dn, delete the backpointers
         * to this entry. */
        if (config) {
            Slapi_ValueSet *vals = NULL;

            slapi_attr_get_valueset(attr, &vals);

            slapi_lock_mutex(config->lock);

            /* Delete old dn value. */
            linked_attrs_mod_backpointers(old_dn, config->managedtype,
                                          config->scope, LDAP_MOD_DELETE, vals);

            slapi_unlock_mutex(config->lock);

            slapi_valueset_free(vals);
            config = NULL;
        }

        linked_attrs_find_config(new_dn, type, &config);

        /* If config was found for the new dn, add the backpointers
         * to this entry.  We do this separate check for both dn's
         * to catch an entry that comes into or goes out of scope
         * from the MODRDN operation. */
        if (config) {
            Slapi_ValueSet *vals = NULL;

            slapi_attr_get_valueset(attr, &vals);

            slapi_lock_mutex(config->lock);

            /* Add new dn value. */
            linked_attrs_mod_backpointers(new_dn, config->managedtype,
                                          config->scope, LDAP_MOD_ADD, vals);

            slapi_unlock_mutex(config->lock);

            slapi_valueset_free(vals);
            config = NULL;
        }

        /* See if any of the values for this attribute are managed
         * backpointers.  We need to update the forward link if so. */
        if (linked_attrs_config_index_has_type(type)) {
            int hint = 0;
            Slapi_Value *val = NULL;

            /* Loop through values and see if we have matching config */
            hint = slapi_attr_first_value(attr, &val);
            while (val) {
                linked_attrs_find_config_reverse(slapi_value_get_string(val),
                                                  type, &config);

                /* If the new DN is within scope, we should fixup the forward links. */
                if (config && slapi_dn_issuffix(new_dn, (config->scope))) {
                    Slapi_ValueSet *vals = slapi_valueset_new();
                    slapi_valueset_add_value(vals, val);

                    slapi_lock_mutex(config->lock);

                    /* Delete old dn value. */
                    linked_attrs_mod_backpointers(old_dn, config->linktype,
                                                  config->scope, LDAP_MOD_DELETE, vals);

                    /* Add new dn value. */
                    linked_attrs_mod_backpointers(new_dn, config->linktype,
                                                  config->scope, LDAP_MOD_ADD, vals);

                    slapi_unlock_mutex(config->lock);

                    slapi_valueset_free(vals);
                    config = NULL;
                }

                hint = slapi_attr_next_value(attr, hint, &val);
            }
        }

        linked_attrs_unlock();

        slapi_entry_next_attr(post_e, attr, &attr);
    }
done:
    slapi_log_error(SLAPI_LOG_TRACE, LINK_PLUGIN_SUBSYSTEM,
                    "<-- linked_attrs_modrdn_post_op\n");

    return rc;
}


/*
 * Debug functions to print config
 */
void
linked_attrs_dump_config()
{
    PRCList *list;

    linked_attrs_read_lock();

    /* Bail out if the plug-in close function was just called. */
    if (!g_plugin_started)
        goto bail;

    if (!PR_CLIST_IS_EMPTY(g_link_config)) {
        list = PR_LIST_HEAD(g_link_config);
        while (list != g_link_config) {
            linked_attrs_dump_config_entry((struct configEntry *)list);
            list = PR_NEXT_LINK(list);
        }
    }

bail:
    linked_attrs_unlock();
}

void
linked_attrs_dump_config_index()
{
    PRCList *list;

    linked_attrs_read_lock();

    /* Bail out if the plug-in close function was just called. */
    if (!g_plugin_started)
        goto bail;

    if (!PR_CLIST_IS_EMPTY(g_managed_config_index)) {
        list = PR_LIST_HEAD(g_managed_config_index);
        while (list != g_managed_config_index) {
            linked_attrs_dump_config_entry(((struct configIndex *)list)->config);
            list = PR_NEXT_LINK(list);
        }
    }

bail:
    linked_attrs_unlock();
}


void
linked_attrs_dump_config_entry(struct configEntry * entry)
{
    slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                    "<==== Linked Attribute Pair =====>\n");
    slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                    "<---- config entry dn -----> %s\n", entry->dn);
    slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                    "<---- link type -----------> %s\n", entry->linktype);
    slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                    "<---- managed type --------> %s\n", entry->managedtype);
    slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                    "<---- scope ---------------> %s\n", entry->scope);
}
