/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* fixup_task.c - linked attributes fixup task */

#include "linked_attrs.h"

/*
 * Function Prototypes
 */
static void linked_attrs_fixup_task_destructor(Slapi_Task *task);
static void linked_attrs_fixup_task_thread(void *arg);
static void linked_attrs_fixup_links(struct configEntry *config);
static int linked_attrs_remove_backlinks_callback(Slapi_Entry *e, void *callback_data);
static int linked_attrs_add_backlinks_callback(Slapi_Entry *e, void *callback_data);

/*
 * Function Implementations
 */
int
linked_attrs_fixup_task_add(Slapi_PBlock *pb,
                            Slapi_Entry *e,
                            Slapi_Entry *eAfter __attribute__((unused)),
                            int *returncode,
                            char *returntext __attribute__((unused)),
                            void *arg)
{
    PRThread *thread = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    task_data *mytaskdata = NULL;
    Slapi_Task *task = NULL;
    const char *linkdn = NULL;
    char *bind_dn;

    *returncode = LDAP_SUCCESS;

    mytaskdata = (task_data *)slapi_ch_calloc(1, sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* get arg(s) and setup our task data */
    linkdn = slapi_entry_attr_get_ref(e, "linkdn");
    if (linkdn) {
        mytaskdata->linkdn = slapi_dn_normalize(slapi_ch_strdup(linkdn));
    }
    slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &bind_dn);
    mytaskdata->bind_dn = slapi_ch_strdup(bind_dn);

    /* allocate new task now */
    task = slapi_plugin_new_task(slapi_entry_get_ndn(e), arg);

    /* register our destructor for cleaning up our private data */
    slapi_task_set_destructor_fn(task, linked_attrs_fixup_task_destructor);

    /* Stash a pointer to our data in the task */
    slapi_task_set_data(task, mytaskdata);

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, linked_attrs_fixup_task_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, LINK_PLUGIN_SUBSYSTEM,
                      "linked_attrs_fixup_task_add - Unable to create task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        slapi_task_finish(task, *returncode);
        rv = SLAPI_DSE_CALLBACK_ERROR;
    } else {
        rv = SLAPI_DSE_CALLBACK_OK;
    }

out:

    return rv;
}

static void
linked_attrs_fixup_task_destructor(Slapi_Task *task)
{
    if (task) {
        task_data *mydata = (task_data *)slapi_task_get_data(task);
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
        if (mydata) {
            slapi_ch_free_string(&mydata->linkdn);
            slapi_ch_free_string(&mydata->bind_dn);
            /* Need to cast to avoid a compiler warning */
            slapi_ch_free((void **)&mydata);
        }
    }
}

static void
linked_attrs_fixup_task_thread(void *arg)
{
    Slapi_Task *task = (Slapi_Task *)arg;
    task_data *td = NULL;
    PRCList *main_config = NULL;
    int found_config = 0;
    int rc = 0;

    if (!task) {
        return; /* no task */
    }
    slapi_task_inc_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                  "linked_attrs_fixup_task_thread --> refcount incremented.\n");
    /* Fetch our task data from the task */
    td = (task_data *)slapi_task_get_data(task);

    /* init and set the bind dn in the thread data */
    slapi_td_set_dn(slapi_ch_strdup(td->bind_dn));

    /* Log started message. */
    slapi_task_begin(task, 1);
    slapi_task_log_notice(task, "Linked attributes fixup task starting (link dn: \"%s\") ...\n",
                          td->linkdn ? td->linkdn : "");
    slapi_log_err(SLAPI_LOG_INFO, LINK_PLUGIN_SUBSYSTEM,
                  "linked_attrs_fixup_task_thread - Syntax validate task starting (link dn: \"%s\") ...\n",
                  td->linkdn ? td->linkdn : "");

    linked_attrs_read_lock();
    main_config = linked_attrs_get_config();
    if (!PR_CLIST_IS_EMPTY(main_config)) {
        struct configEntry *config_entry = NULL;
        PRCList *list = PR_LIST_HEAD(main_config);

        while (list != main_config) {
            config_entry = (struct configEntry *)list;

            /* See if this is the requested config and fix up if so. */
            if (td->linkdn) {
                if (strcasecmp(td->linkdn, config_entry->dn) == 0) {
                    found_config = 1;
                    slapi_task_log_notice(task, "Fixing up linked attribute pair (%s)\n",
                                          config_entry->dn);
                    slapi_log_err(SLAPI_LOG_INFO, LINK_PLUGIN_SUBSYSTEM,
                                  "linked_attrs_fixup_task_thread - Fixing up linked attribute pair (%s)\n", config_entry->dn);

                    linked_attrs_fixup_links(config_entry);
                    break;
                }
            } else {
                /* No config DN was supplied, so fix up all configured links. */
                slapi_task_log_notice(task, "Fixing up linked attribute pair (%s)\n",
                                      config_entry->dn);
                slapi_log_err(SLAPI_LOG_INFO, LINK_PLUGIN_SUBSYSTEM,
                              "linked_attrs_fixup_task_thread - Fixing up linked attribute pair (%s)\n", config_entry->dn);

                linked_attrs_fixup_links(config_entry);
            }

            list = PR_NEXT_LINK(list);
        }
    }

    /* Log a message if we didn't find the requested attribute pair. */
    if (td->linkdn && !found_config) {
        slapi_task_log_notice(task, "Requested link config DN not found (%s)\n",
                              td->linkdn);
        slapi_log_err(SLAPI_LOG_ERR, LINK_PLUGIN_SUBSYSTEM,
                      "linked_attrs_fixup_task_thread - Requested link config DN not found (%s)\n", td->linkdn);
    }

    linked_attrs_unlock();

    /* Log finished message. */
    slapi_task_log_notice(task, "Linked attributes fixup task complete.");
    slapi_task_log_status(task, "Linked attributes fixup task complete.");
    slapi_log_err(SLAPI_LOG_INFO, LINK_PLUGIN_SUBSYSTEM, "linked_attrs_fixup_task_thread - Linked attributes fixup task complete.\n");
    slapi_task_inc_progress(task);

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rc);

    slapi_task_dec_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                  "linked_attrs_fixup_task_thread <-- refcount decremented.\n");
}

static void
linked_attrs_fixup_links(struct configEntry *config)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_PBlock *fixup_pb = NULL;
    char *del_filter = NULL;
    char *add_filter = NULL;
    int rc = 0;

    del_filter = slapi_ch_smprintf("%s=*", config->managedtype);
    add_filter = slapi_ch_smprintf("%s=*", config->linktype);

    /* Lock the attribute pair. */
    slapi_lock_mutex(config->lock);

    if (config->scope) {
        /*
         * If this is a backend txn plugin, start the transaction
         */
        if (plugin_is_betxn) {
            Slapi_DN *fixup_dn = slapi_sdn_new_dn_byref(config->scope);
            Slapi_Backend *be = slapi_be_select(fixup_dn);
            slapi_sdn_free(&fixup_dn);

            if (be) {
                fixup_pb = slapi_pblock_new();
                slapi_pblock_set(fixup_pb, SLAPI_BACKEND, be);
                if (slapi_back_transaction_begin(fixup_pb) != LDAP_SUCCESS) {
                    slapi_log_err(SLAPI_LOG_ERR, LINK_PLUGIN_SUBSYSTEM,
                                  "linked_attrs_fixup_links - Failed to start transaction\n");
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, LINK_PLUGIN_SUBSYSTEM,
                              "linked_attrs_fixup_link - Failed to get be backend from %s\n",
                              config->scope);
            }
        }

        /* Find all entries with the managed type present
         * within the scope and remove the managed type. */
        slapi_search_internal_set_pb(pb, config->scope, LDAP_SCOPE_SUBTREE,
                                     del_filter, 0, 0, 0, 0, linked_attrs_get_plugin_id(), 0);

        rc = slapi_search_internal_callback_pb(pb, config->managedtype, 0,
                                               linked_attrs_remove_backlinks_callback, 0);

        /* Clean out pblock for reuse. */
        slapi_pblock_init(pb);

        /* Find all entries with the link type present within the
         * scope and add backlinks to the entries they point to. */
        slapi_search_internal_set_pb(pb, config->scope, LDAP_SCOPE_SUBTREE,
                                     add_filter, 0, 0, 0, 0, linked_attrs_get_plugin_id(), 0);

        slapi_search_internal_callback_pb(pb, config, 0,
                                          linked_attrs_add_backlinks_callback, 0);
        /*
         *  Finish the transaction.
         */
        if (plugin_is_betxn && fixup_pb) {
            if (rc == 0) {
                slapi_back_transaction_commit(fixup_pb);
            } else {
                slapi_back_transaction_abort(fixup_pb);
            }
            slapi_pblock_destroy(fixup_pb);
        }
    } else {
        /* Loop through all non-private backend suffixes and
         * remove the managed type from any entry that has it.
         * We then find any entry with the linktype present and
         * generate the proper backlinks. */
        void *node = NULL;
        config->suffix = slapi_get_first_suffix(&node, 0);

        while (config->suffix) {
            /*
             * If this is a backend txn plugin, start the transaction
             */
            if (plugin_is_betxn) {
                Slapi_Backend *be = slapi_be_select(config->suffix);
                if (be) {
                    fixup_pb = slapi_pblock_new();
                    slapi_pblock_set(fixup_pb, SLAPI_BACKEND, be);
                    if (slapi_back_transaction_begin(fixup_pb) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, LINK_PLUGIN_SUBSYSTEM,
                                      "linked_attrs_fixup_links: failed to start transaction\n");
                    }
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, LINK_PLUGIN_SUBSYSTEM,
                                  "linked_attrs_fixup_links: failed to get be backend from %s\n",
                                  slapi_sdn_get_dn(config->suffix));
                }
            }

            slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(config->suffix),
                                         LDAP_SCOPE_SUBTREE, del_filter,
                                         0, 0, 0, 0,
                                         linked_attrs_get_plugin_id(), 0);

            slapi_search_internal_callback_pb(pb, config->managedtype, 0,
                                              linked_attrs_remove_backlinks_callback, 0);

            /* Clean out pblock for reuse. */
            slapi_pblock_init(pb);

            slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(config->suffix),
                                         LDAP_SCOPE_SUBTREE, add_filter,
                                         0, 0, 0, 0,
                                         linked_attrs_get_plugin_id(), 0);

            rc = slapi_search_internal_callback_pb(pb, config, 0,
                                                   linked_attrs_add_backlinks_callback, 0);

            /* Clean out pblock for reuse. */
            slapi_pblock_init(pb);

            config->suffix = slapi_get_next_suffix(&node, 0);
            /*
             *  Finish the transaction.
             */
            if (plugin_is_betxn && fixup_pb) {
                if (rc == 0) {
                    slapi_back_transaction_commit(fixup_pb);
                } else {
                    slapi_back_transaction_abort(fixup_pb);
                }
                slapi_pblock_destroy(fixup_pb);
            }
        }
    }

    /* Unlock the attribute pair. */
    slapi_unlock_mutex(config->lock);

    slapi_ch_free_string(&del_filter);
    slapi_ch_free_string(&add_filter);
    slapi_pblock_destroy(pb);
}

static int
linked_attrs_remove_backlinks_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    Slapi_DN *sdn = slapi_entry_get_sdn(e);
    char *type = (char *)callback_data;
    Slapi_PBlock *pb = NULL;
    char *val[1];
    LDAPMod mod;
    LDAPMod *mods[2];

    /*
     * If the server is ordered to shutdown, stop the fixup and return an error.
     */
    if (slapi_is_shutting_down()) {
        rc = -1;
        goto bail;
    }

    pb = slapi_pblock_new();
    /* Remove all values of the passed in type. */
    val[0] = 0;

    mod.mod_op = LDAP_MOD_DELETE;
    mod.mod_type = type;
    mod.mod_values = val;

    mods[0] = &mod;
    mods[1] = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                  "linked_attrs_remove_backlinks_callback - Removing backpointer attribute (%s) from entry (%s)\n",
                  type, slapi_sdn_get_dn(sdn));

    /* Perform the operation. */
    slapi_modify_internal_set_pb_ext(pb, sdn, mods, 0, 0,
                                     linked_attrs_get_plugin_id(), 0);
    slapi_modify_internal_pb(pb);

    slapi_pblock_destroy(pb);
bail:
    return rc;
}

static int
linked_attrs_add_backlinks_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    char *linkdn = slapi_entry_get_dn(e);
    struct configEntry *config = (struct configEntry *)callback_data;
    Slapi_PBlock *pb = slapi_pblock_new();
    int i = 0;
    char **targets = NULL;
    char *val[2];
    LDAPMod mod;
    LDAPMod *mods[2];

    /*
     * If the server is ordered to shutdown, stop the fixup and return an error.
     */
    if (slapi_is_shutting_down()) {
        rc = -1;
        goto done;
    }
    /* Setup the modify operation.  Only the target will
     * change, so we only need to do this once. */
    val[0] = linkdn;
    val[1] = 0;

    mod.mod_op = LDAP_MOD_ADD;
    mod.mod_type = config->managedtype;
    mod.mod_values = val;

    mods[0] = &mod;
    mods[1] = 0;

    targets = slapi_entry_attr_get_charray(e, config->linktype);
    for (i = 0; targets && targets[i]; ++i) {
        char *targetdn = (char *)targets[i];
        int perform_update = 0;
        Slapi_DN *targetsdn = NULL;

        if (slapi_is_shutting_down()) {
            rc = -1;
            goto done;
        }

        targetsdn = slapi_sdn_new_normdn_byref(targetdn);
        if (config->scope) {
            /* Check if the target is within the scope. */
            perform_update = slapi_dn_issuffix(targetdn, config->scope);
        } else {
            /* Find out the root suffix that the linkdn is in
             * and see if the target is in the same backend. */
            Slapi_Backend *be = NULL;
            Slapi_DN *linksdn = slapi_sdn_new_normdn_byref(linkdn);

            if ((be = slapi_be_select(linksdn))) {
                perform_update = slapi_sdn_issuffix(targetsdn, slapi_be_getsuffix(be, 0));
            }

            slapi_sdn_free(&linksdn);
        }

        if (perform_update) {
            slapi_log_err(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                          "linked_attrs_add_backlinks_callback - Adding backpointer (%s) in entry (%s)\n",
                          linkdn, targetdn);

            /* Perform the modify operation. */
            slapi_modify_internal_set_pb_ext(pb, targetsdn, mods, 0, 0,
                                             linked_attrs_get_plugin_id(), 0);
            slapi_modify_internal_pb(pb);

            /* Initialize the pblock so we can reuse it. */
            slapi_pblock_init(pb);
        }
        slapi_sdn_free(&targetsdn);
    }

done:
    slapi_ch_array_free(targets);
    slapi_pblock_destroy(pb);

    return rc;
}
