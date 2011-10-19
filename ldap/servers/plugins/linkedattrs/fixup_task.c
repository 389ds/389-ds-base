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
static const char *fetch_attr(Slapi_Entry *e, const char *attrname,
                              const char *default_val);

/*
 * Function Implementations
 */
int
linked_attrs_fixup_task_add(Slapi_PBlock *pb, Slapi_Entry *e,
                Slapi_Entry *eAfter, int *returncode,
                char *returntext, void *arg)
{
	PRThread *thread = NULL;
	int rv = SLAPI_DSE_CALLBACK_OK;
	task_data *mytaskdata = NULL;
	Slapi_Task *task = NULL;
	const char *linkdn = NULL;

	*returncode = LDAP_SUCCESS;
	/* get arg(s) */
	linkdn = fetch_attr(e, "linkdn", 0);

	/* setup our task data */
	mytaskdata = (task_data*)slapi_ch_calloc(1, sizeof(task_data));
	if (mytaskdata == NULL) {
		*returncode = LDAP_OPERATIONS_ERROR;
		rv = SLAPI_DSE_CALLBACK_ERROR;
		goto out;
	}

	if (linkdn) {
	    mytaskdata->linkdn = slapi_dn_normalize(slapi_ch_strdup(linkdn));
	}

	/* allocate new task now */
	task = slapi_new_task(slapi_entry_get_ndn(e));

	/* register our destructor for cleaning up our private data */
	slapi_task_set_destructor_fn(task, linked_attrs_fixup_task_destructor);

	/* Stash a pointer to our data in the task */
	slapi_task_set_data(task, mytaskdata);

	/* start the sample task as a separate thread */
	thread = PR_CreateThread(PR_USER_THREAD, linked_attrs_fixup_task_thread,
		(void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
		PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
	if (thread == NULL) {
		slapi_log_error( SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
			"unable to create task thread!\n");
		*returncode = LDAP_OPERATIONS_ERROR;
		rv = SLAPI_DSE_CALLBACK_ERROR;
		slapi_task_finish(task, *returncode);
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
		if (mydata) {
			slapi_ch_free_string(&mydata->linkdn);
			/* Need to cast to avoid a compiler warning */
			slapi_ch_free((void **)&mydata);
		}
	}
}

static void
linked_attrs_fixup_task_thread(void *arg)
{
	int rc = 0;
	Slapi_Task *task = (Slapi_Task *)arg;
	task_data *td = NULL;
        PRCList *main_config = NULL;
        int found_config = 0;

	/* Fetch our task data from the task */
	td = (task_data *)slapi_task_get_data(task);

	/* Log started message. */
	slapi_task_begin(task, 1);
	slapi_task_log_notice(task, "Linked attributes fixup task starting (link dn: \"%s\") ...\n",
	                      td->linkdn ? td->linkdn : "");
	slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
	                "Syntax validate task starting (link dn: \"%s\") ...\n",
                        td->linkdn ? td->linkdn : "");

        linked_attrs_read_lock();
        main_config = linked_attrs_get_config();
        if (!PR_CLIST_IS_EMPTY(main_config)) {
            struct configEntry *config_entry = NULL;
            PRCList *list = PR_LIST_HEAD(main_config);

            while (list != main_config) {
                config_entry = (struct configEntry *) list;

                /* See if this is the requested config and fix up if so. */
                if (td->linkdn) {
                    if  (strcasecmp(td->linkdn, config_entry->dn) == 0) {
                        found_config = 1;
                        slapi_task_log_notice(task, "Fixing up linked attribute pair (%s)\n",
                               config_entry->dn);
                        slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                               "Fixing up linked attribute pair (%s)\n",
                               config_entry->dn);

                        linked_attrs_fixup_links(config_entry);
                        break;
                    }
                } else {
                    /* No config DN was supplied, so fix up all configured links. */
                    slapi_task_log_notice(task, "Fixing up linked attribute pair (%s)\n",
                            config_entry->dn);
                    slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                           "Fixing up linked attribute pair (%s)\n", config_entry->dn);

                    linked_attrs_fixup_links(config_entry);
                }

                list = PR_NEXT_LINK(list);
            }
        }

        /* Log a message if we didn't find the requested attribute pair. */
        if (td->linkdn && !found_config) {
            slapi_task_log_notice(task, "Requested link config DN not found (%s)\n",
                    td->linkdn);
            slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM,
                    "Requested link config DN not found (%s)\n", td->linkdn);
        }

        linked_attrs_unlock();

	/* Log finished message. */
	slapi_task_log_notice(task, "Linked attributes fixup task complete.\n");
	slapi_task_log_status(task, "Linked attributes fixup task complete.\n");
	slapi_log_error(SLAPI_LOG_FATAL, LINK_PLUGIN_SUBSYSTEM, "Linked attributes fixup task complete.\n");
	slapi_task_inc_progress(task);

	/* this will queue the destruction of the task */
	slapi_task_finish(task, rc);
}

static void 
linked_attrs_fixup_links(struct configEntry *config)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    char *del_filter = NULL;
    char *add_filter = NULL;

    del_filter = slapi_ch_smprintf("%s=*", config->managedtype);
    add_filter = slapi_ch_smprintf("%s=*", config->linktype);

    /* Lock the attribute pair. */
    slapi_lock_mutex(config->lock);

    if (config->scope) {
        /* Find all entries with the managed type present
         * within the scope and remove the managed type. */
        slapi_search_internal_set_pb(pb, config->scope, LDAP_SCOPE_SUBTREE,
                del_filter, 0, 0, 0, 0, linked_attrs_get_plugin_id(), 0);

        slapi_search_internal_callback_pb(pb, config->managedtype, 0,
                linked_attrs_remove_backlinks_callback, 0);

        /* Clean out pblock for reuse. */
        slapi_pblock_init(pb);

        /* Find all entries with the link type present within the
         * scope and add backlinks to the entries they point to. */
        slapi_search_internal_set_pb(pb, config->scope, LDAP_SCOPE_SUBTREE,
                add_filter, 0, 0, 0, 0, linked_attrs_get_plugin_id(), 0);

        slapi_search_internal_callback_pb(pb, config, 0,
                linked_attrs_add_backlinks_callback, 0);
    } else {
        /* Loop through all non-private backend suffixes and
         * remove the managed type from any entry that has it.
         * We then find any entry with the linktype present and
         * generate the proper backlinks. */
        void *node = NULL;
        Slapi_DN * suffix = slapi_get_first_suffix (&node, 0);

        while (suffix) {
            slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(suffix),
                                         LDAP_SCOPE_SUBTREE, del_filter,
                                         0, 0, 0, 0,
                                         linked_attrs_get_plugin_id(), 0);

            slapi_search_internal_callback_pb(pb, config->managedtype, 0,
                    linked_attrs_remove_backlinks_callback, 0);

            /* Clean out pblock for reuse. */
            slapi_pblock_init(pb);

            slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(suffix),
                                         LDAP_SCOPE_SUBTREE, add_filter,
                                         0, 0, 0, 0,
                                         linked_attrs_get_plugin_id(), 0);

            slapi_search_internal_callback_pb(pb, config, 0,
                    linked_attrs_add_backlinks_callback, 0);

            /* Clean out pblock for reuse. */
            slapi_pblock_init(pb);

            suffix = slapi_get_next_suffix (&node, 0);
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
    Slapi_PBlock *pb = slapi_pblock_new();
    char *val[1];
    LDAPMod mod;
    LDAPMod *mods[2];

    /* Remove all values of the passed in type. */
    val[0] = 0;

    mod.mod_op = LDAP_MOD_DELETE;
    mod.mod_type = type;
    mod.mod_values = val;

    mods[0] = &mod;
    mods[1] = 0;

    slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                    "Removing backpointer attribute (%s) from entry (%s)\n",
                    type, slapi_sdn_get_dn(sdn));

    /* Perform the operation. */
    slapi_modify_internal_set_pb_ext(pb, sdn, mods, 0, 0,
                                     linked_attrs_get_plugin_id(), 0);
    slapi_modify_internal_pb(pb);

    slapi_pblock_destroy(pb);

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
        Slapi_DN *targetsdn = slapi_sdn_new_dn_byref(targetdn);

        if (config->scope) {
            /* Check if the target is within the scope. */
            perform_update = slapi_dn_issuffix(targetdn, config->scope);
        } else {
            /* Find out the root suffix that the linkdn is in
             * and see if the target is in the same backend. */
            Slapi_Backend *be = NULL;
            Slapi_DN *linksdn = slapi_sdn_new_dn_byref(linkdn);

            if ((be = slapi_be_select(linksdn))) {
                perform_update = slapi_sdn_issuffix(targetsdn, slapi_be_getsuffix(be, 0));
            }

            slapi_sdn_free(&linksdn);
        }

        if (perform_update) {
            slapi_log_error(SLAPI_LOG_PLUGIN, LINK_PLUGIN_SUBSYSTEM,
                            "Adding backpointer (%s) in entry (%s)\n",
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

    slapi_ch_array_free(targets);
    slapi_pblock_destroy(pb);

    return rc;
}

/* extract a single value from the entry (as a string) -- if it's not in the
 * entry, the default will be returned (which can be NULL).
 * you do not need to free anything returned by this.
 */
static const char *
fetch_attr(Slapi_Entry *e, const char *attrname,
           const char *default_val)
{
Slapi_Attr *attr;
Slapi_Value *val = NULL;

	if (slapi_entry_attr_find(e, attrname, &attr) != 0) {
		return default_val;
	}

	slapi_attr_first_value(attr, &val);

	return slapi_value_get_string(val);
}

