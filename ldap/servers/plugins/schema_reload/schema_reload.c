/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
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

static Slapi_PluginDesc pdesc = {"schemareload",
                                 VENDOR, DS_PACKAGE_VERSION,
                                 "task plugin to reload schema files"};

static int schemareload_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static int schemareload_start(Slapi_PBlock *pb);
static int schemareload_close(Slapi_PBlock *pb);
static void schemareload_destructor(Slapi_Task *task);

/*
 * Init function
 * Specified in the plugin entry as "nsslapd-pluginInitfunc: schemareload_init"
 */
int
schemareload_init(Slapi_PBlock *pb)
{
    int rc = 0;

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_03);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                           (void *)schemareload_start);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)schemareload_close);

    return rc;
}

/*
 * Task start function
 * Register the function schemareload_add, which invokes the task on demand.
 */
static int
schemareload_start(Slapi_PBlock *pb)
{
    int rc = 0;

    if ((schemareload_lock = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "schemareload", "schemareload_start - Failed to create global schema reload lock.\n");
        return -1;
    }
    rc = slapi_plugin_task_register_handler("schema reload task", schemareload_add, pb);
    if (rc != LDAP_SUCCESS) {
        PR_DestroyLock(schemareload_lock);
    }

    return rc;
}

static int
schemareload_close(Slapi_PBlock *pb __attribute__((unused)))
{

    slapi_plugin_task_unregister_handler("schema reload task", schemareload_add);
    PR_DestroyLock(schemareload_lock);

    return 0;
}

typedef struct _task_data
{
    char *schemadir;
    char *bind_dn;
} task_data;

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
    task_data *td = NULL;

    if (!task) {
        return; /* no task */
    }
    slapi_task_inc_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, "schemareload",
                  "schemareload_thread --> refcount incremented.\n");
    /* Fetch our task data from the task */
    td = (task_data *)slapi_task_get_data(task);

    /* Initialize and set the bind dn in the thread data */
    slapi_td_set_dn(slapi_ch_strdup(td->bind_dn));

    /* update task state to show it's running */
    slapi_task_begin(task, total_work);
    PR_Lock(schemareload_lock); /* make schema reload serialized */
    slapi_task_log_notice(task, "Schema reload task starts (schema dir: %s) ...\n", td->schemadir ? td->schemadir : "default");
    slapi_log_err(SLAPI_LOG_INFO, "schemareload", "schemareload_thread - Schema reload task starts (schema dir: %s) ...\n", td->schemadir ? td->schemadir : "default");

    rv = slapi_validate_schema_files(td->schemadir);
    slapi_task_inc_progress(task);

    if (slapi_is_shutting_down()) {
        slapi_task_log_notice(task, "Server is shuttoing down; Schema validation aborted.");
        slapi_task_log_status(task, "Server is shuttoing down; Schema validation aborted.");
        slapi_log_err(SLAPI_LOG_ERR, "schemareload", "schemareload_thread - Server is shutting down; Schema validation aborted.\n");
    } else if (LDAP_SUCCESS == rv) {
        slapi_task_log_notice(task, "Schema validation passed.");
        slapi_task_log_status(task, "Schema validation passed.");
        slapi_log_err(SLAPI_LOG_INFO, "schemareload", "schemareload_thread - Schema validation passed.\n");

        rv = slapi_reload_schema_files(td->schemadir);
        slapi_task_inc_progress(task);

        /* update task state to say we're finished */
        if (LDAP_SUCCESS == rv) {
            slapi_task_log_notice(task, "Schema reload task finished.");
            slapi_task_log_status(task, "Schema reload task finished.");
            slapi_log_err(SLAPI_LOG_INFO, "schemareload", "schemareload_thread - Schema reload task finished.\n");
        } else {
            slapi_task_log_notice(task, "Schema reload task failed.");
            slapi_task_log_status(task, "Schema reload task failed.");
            slapi_log_err(SLAPI_LOG_ERR, "schemareload", "schemareload_thread - Schema reload task failed.\n");
        }
    } else {
        slapi_task_log_notice(task, "Schema validation failed.");
        slapi_task_log_status(task, "Schema validation failed.");
        slapi_log_err(SLAPI_LOG_ERR, "schemareload", "schemareload_thread - Schema validation failed.\n");
    }
    PR_Unlock(schemareload_lock);

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rv);
    slapi_task_dec_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, "schemareload",
                  "schemareload_thread <-- refcount decremented.\n");
}

static void
schemareload_destructor(Slapi_Task *task)
{
    if (task) {
        task_data *mydata = (task_data *)slapi_task_get_data(task);
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
        if (mydata) {
            slapi_ch_free_string(&mydata->schemadir);
            slapi_ch_free_string(&mydata->bind_dn);
            /* Need to cast to avoid a compiler warning */
            slapi_ch_free((void **)&mydata);
        }
    }
}

/*
 * Invoked when the task instance is added by the client (step 5 of the comment)
 * Get the necessary attributes from the task entry, and spawns a thread to do
 * the task.
 */
static int
schemareload_add(Slapi_PBlock *pb,
                 Slapi_Entry *e,
                 Slapi_Entry *eAfter __attribute__((unused)),
                 int *returncode,
                 char *returntext __attribute__((unused)),
                 void *arg)
{
    PRThread *thread = NULL;
    const char *schemadir = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_Task *task = NULL;
    task_data *mytaskdata = NULL;
    char *bind_dn;

    *returncode = LDAP_SUCCESS;
    if (slapi_entry_attr_get_ref(e, "cn") == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* get the requestor dn for our thread data*/
    slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &bind_dn);

    /* get arg(s) */
    schemadir = slapi_entry_attr_get_ref(e, "schemadir");

    /* allocate new task now */
    task = slapi_plugin_new_task(slapi_entry_get_ndn(e), arg);
    if (task == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "schemareload", "schemareload_add - Unable to allocate new task!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    mytaskdata = (task_data *)slapi_ch_malloc(sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    mytaskdata->schemadir = slapi_ch_strdup(schemadir);
    mytaskdata->bind_dn = slapi_ch_strdup(bind_dn);

    /* set a destructor that will clean up schemadir for us when the task is complete */
    slapi_task_set_destructor_fn(task, schemareload_destructor);

    /* Stash our task_data for use by the task thread */
    slapi_task_set_data(task, mytaskdata);

    /* start the schema reload task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, schemareload_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "schemareload",
                      "schemareload_add - Unable to create schema reload task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
    } else {
        /* thread successful */
        rv = SLAPI_DSE_CALLBACK_OK;
    }

out:

    return rv;
}
