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
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* 
 * sample task plugin
 * 
 * [How to set up the plugin for testing]
 * 1. compile and package with the other plugins
 * 2. put the plugin libsampletask-plugin.so at <prefix>/usr/lib/<PACKAGE_NAME>/plugins
 * 3. register it as a plugin in dse.ldif
 * Plugin entry:
 * dn: cn=sampletask,cn=plugins,cn=config
 * objectClass: top
 * objectClass: nsSlapdPlugin
 * objectClass: extensibleObject
 * cn: sampletask
 * nsslapd-pluginPath: libsampletask-plugin
 * nsslapd-pluginInitfunc: sampletask_init
 * nsslapd-pluginType: object
 * nsslapd-pluginEnabled: on
 * nsslapd-pluginId: sampletask
 * nsslapd-pluginVersion: <plugin_version>
 * nsslapd-pluginVendor: <vendor name>
 * nsslapd-pluginDescription: Sample task plugin
 *
 * 4. create a config task entry in dse.ldif
 * Task entry:
 * dn: cn=sampletask, cn=tasks, cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: sampletask
 *
 * 5. to invoke the sample task, run the command line:
 * $ ./ldapmodify -h <host> -p <port> -D "cn=Directory Manager" -w <pw> -a
 * dn: cn=sampletask 0, cn=sample task, cn=tasks, cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: sample task 0
 * myarg: sample task myarg
 *
 * Result is in the errors log 
 * [...] - Sample task starts (arg: sample task myarg) ...
 * [...] - Sample task finished.
 */

#include "slapi-plugin.h"
#include "nspr.h"

static int task_sampletask_add(Slapi_PBlock *pb, Slapi_Entry *e,
                    Slapi_Entry *eAfter, int *returncode, char *returntext,
                    void *arg);
static int task_sampletask_start(Slapi_PBlock *pb);

/* 
 * Init function
 * Specified in the plugin entry as "nsslapd-pluginInitfunc: sampletask_init"
 */
int
sampletask_init( Slapi_PBlock *pb )
{
    int rc = 0;
	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
                                    (void *) SLAPI_PLUGIN_VERSION_03 );
    rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
                                    (void *) task_sampletask_start );

    return rc;
}

/* 
 * Task start function
 * Register the function task_sampletask_add, which invokes the task on demand.
 */
static int
task_sampletask_start(Slapi_PBlock *pb)
{
    int rc = slapi_task_register_handler("sample task", task_sampletask_add);
	return rc;
}

/*
 * Task thread
 * Not necessary be a thread, but it'd not disturb the server's other jobs.
 */
static void
task_sampletask_thread(void *arg)
{
    Slapi_Task *task = (Slapi_Task *)arg;
    char *myarg = NULL;
    int i, rv = 0;
    int total_work = 3;

    /* fetch our argument from the task */
    myarg = (char *)slapi_task_get_data(task);

    /* update task state to show it's running */
    slapi_task_begin(task, total_work);
    slapi_task_log_notice(task, "Sample task starts (arg: %s) ...\n", myarg);
    slapi_log_error(SLAPI_LOG_FATAL, "sampletask", "Sample task starts (arg: %s) ...\n", myarg);

    /* real work would be done here */
    for (i = 0; i < total_work; i++) {
        PR_Sleep(10000);
        slapi_task_inc_progress(task);
    }

    /* update task state to say we're finished */
    slapi_task_log_notice(task, "Sample task finished.");
    slapi_task_log_status(task, "Sample task finished.");
    slapi_log_error(SLAPI_LOG_FATAL, "sampletask", "Sample task finished.\n");

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
task_sampletask_destructor(Slapi_Task *task)
{
    if (task) {
        char *myarg = (char *)slapi_task_get_data(task);
        if (myarg) {
            slapi_ch_free_string(&myarg);
        }
    }
}

/*
 * Invoked when the task instance is added by the client (step 5 of the comment)
 * Get the necessary attributes from the task entry, and spawns a thread to do
 * the task.
 */
static int
task_sampletask_add(Slapi_PBlock *pb, Slapi_Entry *e,
                    Slapi_Entry *eAfter, int *returncode, char *returntext,
                    void *arg)
{
    PRThread *thread = NULL;
    const char *cn;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_PBlock *mypb = NULL;
    Slapi_Task *task = NULL;
    const char *myarg;

    *returncode = LDAP_SUCCESS;
    if ((cn = fetch_attr(e, "cn", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* get arg(s) */
    if ((myarg = fetch_attr(e, "myarg", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, "sampletask", "unable to allocate new task!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* set a destructor that will clean up myarg for us when the task is complete */
    slapi_task_set_destructor_fn(task, task_sampletask_destructor);

    /* Stash our argument in the task for use by the task thread */
    slapi_task_set_data(task, slapi_ch_strdup(myarg));

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, task_sampletask_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, "sampletask",
                  "unable to create sample task thread!\n");
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
