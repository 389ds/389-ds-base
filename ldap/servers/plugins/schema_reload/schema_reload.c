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
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* 
 * Dynamic schema file reload plugin
 * 
 * plugin entry in dse.ldif
 * dn: cn=Schema Reload,cn=plugins,cn=config
 * objectClass: top
 * objectClass: nsSlapdPlugin
 * objectClass: extensibleObject
 * cn: Schema Reload
 * nsslapd-pluginPath: libschemareload-plugin
 * nsslapd-pluginInitfunc: schemareload_init
 * nsslapd-pluginType: object
 * nsslapd-pluginEnabled: on
 * nsslapd-pluginId: schemareload
 * nsslapd-pluginVersion: <plugin_version>
 * nsslapd-pluginVendor: <vendor name>
 * nsslapd-pluginDescription: task plugin to reload schema files
 *
 * config task entry in dse.ldif (registered in schemareload_start)
 * dn: cn=schema reload task, cn=tasks, cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: schema reload task
 *
 * To invoke the sample task, run the command line:
 * $ ldapmodify -h <host> -p <port> -D "cn=Directory Manager" -w <pw> -a
 * dn: cn=schema reload task 0, cn=schema reload task, cn=tasks, cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: schema reload task 0
 * [ schemadir: path to reload files from ]
 */

#include "slap.h"
#include "slapi-plugin.h"
#include "nspr.h"

static PRLock *schemareload_lock = NULL;

static Slapi_PluginDesc pdesc = { "schemareload",
    VENDOR, DS_PACKAGE_VERSION,
    "task plugin to reload schema files" };

static int schemareload_add(Slapi_PBlock *pb, Slapi_Entry *e,
                    Slapi_Entry *eAfter, int *returncode, char *returntext,
                    void *arg);
static int schemareload_start(Slapi_PBlock *pb);

/* 
 * Init function
 * Specified in the plugin entry as "nsslapd-pluginInitfunc: schemareload_init"
 */
int
schemareload_init( Slapi_PBlock *pb )
{
    int rc = 0;
    schemareload_lock = PR_NewLock();
    if (NULL == schemareload_lock) {
        slapi_log_error(SLAPI_LOG_FATAL, "schemareload", 
                        "Failed to create global schema reload lock.");
        return rc;
    }
    rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
                                    (void *) SLAPI_PLUGIN_VERSION_03 );
    rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
        (void *)&pdesc );
    rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
                                    (void *) schemareload_start );

    return rc;
}

/* 
 * Task start function
 * Register the function schemareload_add, which invokes the task on demand.
 */
static int
schemareload_start(Slapi_PBlock *pb)
{
    int rc = slapi_task_register_handler("schema reload task", schemareload_add);
    return rc;
}

/*
 * Task thread
 * This is the heart of the reload-schema-file task:
 * - calling the schema APIs to validate the schema files,
 * - reloading them if the files are valid.
 * Not necessary be a thread, but it'd not disturb the server's other jobs.
 */
static void
schemareload_thread(void *arg)
{
    Slapi_Task *task = (Slapi_Task *)arg;
    int rv = 0;
    int total_work = 2;
    /* fetch our argument from the task */
    char *schemadir = (char *)slapi_task_get_data(task);

    /* update task state to show it's running */
    slapi_task_begin(task, total_work);
    PR_Lock(schemareload_lock);    /* make schema reload serialized */
    slapi_task_log_notice(task, "Schema reload task starts (schema dir: %s) ...\n", schemadir?schemadir:"default");
    slapi_log_error(SLAPI_LOG_FATAL, "schemareload", "Schema reload task starts (schema dir: %s) ...\n", schemadir?schemadir:"default");

    rv = slapi_validate_schema_files(schemadir);
    slapi_task_inc_progress(task);

    if (LDAP_SUCCESS == rv) {
        slapi_task_log_notice(task, "Schema validation passed.");
        slapi_task_log_status(task, "Schema validation passed.");
        slapi_log_error(SLAPI_LOG_FATAL, "schemareload", "Schema validation passed.\n");

        rv = slapi_reload_schema_files(schemadir);
        slapi_task_inc_progress(task);

        /* update task state to say we're finished */
        if (LDAP_SUCCESS == rv) {
            slapi_task_log_notice(task, "Schema reload task finished.");
            slapi_task_log_status(task, "Schema reload task finished.");
            slapi_log_error(SLAPI_LOG_FATAL, "schemareload", "Schema reload task finished.\n");
        } else {
            slapi_task_log_notice(task, "Schema reload task failed.");
            slapi_task_log_status(task, "Schema reload task failed.");
            slapi_log_error(SLAPI_LOG_FATAL, "schemareload", "Schema reload task failed.\n");
        }
        PR_Unlock(schemareload_lock);
    } else {
        slapi_task_log_notice(task, "Schema validation failed.");
        slapi_task_log_status(task, "Schema validation failed.");
        slapi_log_error(SLAPI_LOG_FATAL, "schemareload", "Schema validation failed.\n");
        PR_Unlock(schemareload_lock);
    }

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rv);
}

/* extract a single value from the entry (as a string) -- if it's not in the
 * entry, the default will be returned (which can be NULL).
 * you do not need to free anything returned by this.
 */
static const char *fetch_attr(Slapi_Entry *e, const char *attrname,
                                              const char *default_val)
{
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;

    if (slapi_entry_attr_find(e, attrname, &attr) != 0)
        return default_val;
    slapi_attr_first_value(attr, &val);
    return slapi_value_get_string(val);
}

static void
schemareload_destructor(Slapi_Task *task)
{
    if (task) {
        char *schemadir = (char *)slapi_task_get_data(task);
        slapi_ch_free_string(&schemadir);
    }
}

/*
 * Invoked when the task instance is added by the client (step 5 of the comment)
 * Get the necessary attributes from the task entry, and spawns a thread to do
 * the task.
 */
static int
schemareload_add(Slapi_PBlock *pb, Slapi_Entry *e,
                    Slapi_Entry *eAfter, int *returncode, char *returntext,
                    void *arg)
{
    PRThread *thread = NULL;
    const char *schemadir = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_Task *task = NULL;

    *returncode = LDAP_SUCCESS;
    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* get arg(s) */
    schemadir = fetch_attr(e, "schemadir", NULL);

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, "schemareload", "unable to allocate new task!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* set a destructor that will clean up schemadir for us when the task is complete */
    slapi_task_set_destructor_fn(task, schemareload_destructor);

    /* Stash our argument in the task for use by the task thread */
    slapi_task_set_data(task, slapi_ch_strdup(schemadir));

    /* start the schema reload task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, schemareload_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, "schemareload",
                  "unable to create schema reload task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_task_finish(task, *returncode);
    } else {
        /* thread successful */
        rv = SLAPI_DSE_CALLBACK_OK;
    }

out:
    return rv;
}
