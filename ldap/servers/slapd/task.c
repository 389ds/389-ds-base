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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * directory online tasks (import, export, backup, restore)
 */

#include "slap.h"


/***********************************
 * Static Global Variables
 ***********************************/
/* don't panic, this is only used when creating new tasks or removing old
 * ones...
 */
static Slapi_Task *global_task_list = NULL;
static PRLock *global_task_lock = NULL;
static int shutting_down = 0;

/***********************************
 * Private Defines
 ***********************************/
#define TASK_BASE_DN      "cn=tasks,cn=config"
#define TASK_IMPORT_DN    "cn=import,cn=tasks,cn=config"
#define TASK_EXPORT_DN    "cn=export,cn=tasks,cn=config"
#define TASK_BACKUP_DN    "cn=backup,cn=tasks,cn=config"
#define TASK_RESTORE_DN   "cn=restore,cn=tasks,cn=config"
#define TASK_INDEX_DN     "cn=index,cn=tasks,cn=config"
#define TASK_UPGRADEDB_DN "cn=upgradedb,cn=tasks,cn=config"
#define TASK_TOMBSTONE_FIXUP_DN  "cn=fixup tombstones,cn=tasks,cn=config"

#define TASK_LOG_NAME           "nsTaskLog"
#define TASK_STATUS_NAME        "nsTaskStatus"
#define TASK_EXITCODE_NAME      "nsTaskExitCode"
#define TASK_PROGRESS_NAME      "nsTaskCurrentItem"
#define TASK_WORK_NAME          "nsTaskTotalItems"

#define DEFAULT_TTL     "120"   /* seconds */
#define TASK_SYSCONFIG_FILE_ATTR "sysconfigfile" /* sysconfig reload task file attr */
#define TASK_SYSCONFIG_LOGCHANGES_ATTR "logchanges"
#define TASK_TOMBSTONE_FIXUP "fixup tombstones task"
#define TASK_TOMBSTONE_FIXUP_BACKEND "backend"
#define TASK_TOMBSTONE_FIXUP_SUFFIX "suffix"
#define TASK_TOMBSTONE_FIXUP_STRIPCSN "stripcsn"

#define LOG_BUFFER              256
/* if the cumul. log gets larger than this, it's truncated: */
#define MAX_SCROLLBACK_BUFFER   8192

#define NEXTMOD(_type, _val) do { \
    modlist[cur].mod_op = LDAP_MOD_REPLACE; \
    modlist[cur].mod_type = (_type); \
    modlist[cur].mod_values = (char **)slapi_ch_malloc(2*sizeof(char *)); \
    modlist[cur].mod_values[0] = (_val); \
    modlist[cur].mod_values[1] = NULL; \
    mod[cur] = &modlist[cur]; \
    cur++; \
} while (0)


/***********************************
 * Static Function Prototypes
 ***********************************/
static Slapi_Task *new_task(const char *dn, void *plugin);
static void destroy_task(time_t when, void *arg);
static int task_modify(Slapi_PBlock *pb, Slapi_Entry *e,
                       Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static int task_deny(Slapi_PBlock *pb, Slapi_Entry *e,
                     Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static void task_generic_destructor(Slapi_Task *task);
static const char *fetch_attr(Slapi_Entry *e, const char *attrname,
                              const char *default_val);
static Slapi_Entry *get_internal_entry(Slapi_PBlock *pb, char *dn);
static void modify_internal_entry(char *dn, LDAPMod **mods);

/***********************************
 * Public Functions
 ***********************************/ 
/*
 * slapi_new_task: create a new task, fill in DN, and setup modify callback
 * argument:
 *     dn: task dn
 * result: 
 *     Success: Slapi_Task object
 *     Failure: NULL
 */
Slapi_Task *
slapi_new_task(const char *dn)
{
    return new_task(dn, NULL);
}

Slapi_Task *
slapi_plugin_new_task(const char *dn, void *plugin)
{
    return new_task(dn, plugin);
}

/* slapi_destroy_task: destroy a task
 * argument:
 *     task: task to destroy
 * result: 
 *     none
 */
void
slapi_destroy_task(void *arg)
{
    if (arg) {
        destroy_task(1, arg);
    }
}

/*
 * Sets the initial task state and updated status
 */
void slapi_task_begin(Slapi_Task *task, int total_work)
{
    if (task) {
        task->task_work = total_work;
        task->task_progress = 0;
        task->task_state = SLAPI_TASK_RUNNING;
        slapi_task_status_changed(task);
    }
}

/*
 * Increments task progress and updates status
 */
void slapi_task_inc_progress(Slapi_Task *task)
{
    if (task) {
        task->task_progress++;
        slapi_task_status_changed(task);
    }
}

/*
 * Sets completed task state and updates status
 */
void slapi_task_finish(Slapi_Task *task, int rc)
{
    if (task) {
        task->task_exitcode = rc;
        task->task_state = SLAPI_TASK_FINISHED;
        slapi_plugin_op_finished(task->origin_plugin);
        slapi_task_status_changed(task);
    }
}

/*
 * Cancels a task
 */
void slapi_task_cancel(Slapi_Task *task, int rc)
{
    if (task) {
        task->task_exitcode = rc;
        task->task_state = SLAPI_TASK_CANCELLED;
        slapi_task_status_changed(task);
    }
}

/*
 * Get the current state of a task
 */
int slapi_task_get_state(Slapi_Task *task)
{
    if (task) {
        return task->task_state;
    }

    return 0; /* return value not currently used */
}

/* this changes the 'nsTaskStatus' value, which is transient (anything logged
 * here wipes out any previous status)
 */
void slapi_task_log_status(Slapi_Task *task, char *format, ...)
{
    va_list ap;

    if (! task->task_status)
        task->task_status = (char *)slapi_ch_malloc(10 * LOG_BUFFER);
    if (! task->task_status)
        return;        /* out of memory? */

    va_start(ap, format);
    PR_vsnprintf(task->task_status, (10 * LOG_BUFFER), format, ap);
    va_end(ap);
    slapi_task_status_changed(task);
}

void slapi_task_log_notice_ext(Slapi_Task *task, char *format, va_list ap)
{
    char buffer[LOG_BUFFER];
    size_t len;

    PR_vsnprintf(buffer, LOG_BUFFER, format, ap);

    if (task->task_log_lock) {
        PR_Lock(task->task_log_lock);
    }
    len = 2 + strlen(buffer) + (task->task_log ? strlen(task->task_log) : 0);
    if ((len > MAX_SCROLLBACK_BUFFER) && task->task_log) {
        size_t i;
        char *newbuf;

        /* start from middle of buffer, and find next linefeed */
        i = strlen(task->task_log)/2;
        while (task->task_log[i] && (task->task_log[i] != '\n'))
            i++;
        if (task->task_log[i])
            i++;
        len = strlen(task->task_log) - i + 2 + strlen(buffer);
        newbuf = (char *)slapi_ch_malloc(len);
        strcpy(newbuf, task->task_log + i);
        slapi_ch_free((void **)&task->task_log);
        task->task_log = newbuf;
    } else {
        if (! task->task_log) {
            task->task_log = (char *)slapi_ch_malloc(len);
            task->task_log[0] = 0;
        } else {
            task->task_log = (char *)slapi_ch_realloc(task->task_log, len);
        }
    }

    if (task->task_log[0])
        strcat(task->task_log, "\n");
    strcat(task->task_log, buffer);
    if (task->task_log_lock) {
        PR_Unlock(task->task_log_lock);
    }

    slapi_task_status_changed(task);
}

void slapi_task_log_status_ext(Slapi_Task *task, char *format, va_list ap)
{
    if (! task->task_status)
        task->task_status = (char *)slapi_ch_malloc(10 * LOG_BUFFER);
    if (! task->task_status)
        return;        /* out of memory? */

    PR_vsnprintf(task->task_status, (10 * LOG_BUFFER), format, ap);
    slapi_task_status_changed(task);
}

/* this adds a line to the 'nsTaskLog' value, which is cumulative (anything
 * logged here is added to the end)
 */
void slapi_task_log_notice(Slapi_Task *task, char *format, ...)
{
    va_list ap;
    char buffer[LOG_BUFFER];
    size_t len;

    va_start(ap, format);
    PR_vsnprintf(buffer, LOG_BUFFER, format, ap);
    va_end(ap);

    if (task->task_log_lock) {
        PR_Lock(task->task_log_lock);
    }
    len = 2 + strlen(buffer) + (task->task_log ? strlen(task->task_log) : 0);
    if ((len > MAX_SCROLLBACK_BUFFER) && task->task_log) {
        size_t i;
        char *newbuf;

        /* start from middle of buffer, and find next linefeed */
        i = strlen(task->task_log)/2;
        while (task->task_log[i] && (task->task_log[i] != '\n'))
            i++;
        if (task->task_log[i])
            i++;
        len = strlen(task->task_log) - i + 2 + strlen(buffer);
        newbuf = (char *)slapi_ch_malloc(len);
        strcpy(newbuf, task->task_log + i);
        slapi_ch_free((void **)&task->task_log);
        task->task_log = newbuf;
    } else {
        if (! task->task_log) {
            task->task_log = (char *)slapi_ch_malloc(len);
            task->task_log[0] = 0;
        } else {
            task->task_log = (char *)slapi_ch_realloc(task->task_log, len);
        }
    }

    if (task->task_log[0])
        strcat(task->task_log, "\n");
    strcat(task->task_log, buffer);
    if (task->task_log_lock) {
        PR_Unlock(task->task_log_lock);
    }

    slapi_task_status_changed(task);
}

/* update attributes in the entry under "cn=tasks" to match the current
 * status of the task. */
void slapi_task_status_changed(Slapi_Task *task)
{
    LDAPMod modlist[20];
    LDAPMod *mod[20];
    int cur = 0, i;
    char s1[20], s2[20], s3[20];

    if (shutting_down) {
        /* don't care about task status updates anymore */
        return;
    }

    if (task->task_log_lock) {
        PR_Lock(task->task_log_lock);
    }
    NEXTMOD(TASK_LOG_NAME, task->task_log);
    if (task->task_log_lock) {
        PR_Unlock(task->task_log_lock);
    }
    NEXTMOD(TASK_STATUS_NAME, task->task_status);
    sprintf(s1, "%d", task->task_exitcode);
    sprintf(s2, "%d", task->task_progress);
    sprintf(s3, "%d", task->task_work);
    NEXTMOD(TASK_PROGRESS_NAME, s2);
    NEXTMOD(TASK_WORK_NAME, s3);
    /* only add the exit code when the job is done */
    if ((task->task_state == SLAPI_TASK_FINISHED) ||
        (task->task_state == SLAPI_TASK_CANCELLED)) {
        NEXTMOD(TASK_EXITCODE_NAME, s1);
        /* make sure the console can tell the task has ended */
        if (task->task_progress != task->task_work) {
            task->task_progress = task->task_work;
        }
    }

    mod[cur] = NULL;
    modify_internal_entry(task->task_dn, mod);

    for (i = 0; i < cur; i++)
        slapi_ch_free((void **)&modlist[i].mod_values);

    /*
     * Removed (task->task_state == SLAPI_TASK_CANCELLED) from 
     * task_state checking to fix bz 515805.
     */
    if ((task->task_state == SLAPI_TASK_FINISHED) &&
        !(task->task_flags & SLAPI_TASK_DESTROYING)) {
        Slapi_PBlock *pb = slapi_pblock_new();
        Slapi_Entry *e;
        int ttl;
        time_t expire;

        e = get_internal_entry(pb, task->task_dn);
        if (e == NULL)
            return;
        ttl = atoi(fetch_attr(e, "ttl", DEFAULT_TTL));
        if (ttl > 3600)
            ttl = 3600;         /* be reasonable. */
        expire = time(NULL) + ttl;
        task->task_flags |= SLAPI_TASK_DESTROYING;
        /* queue an event to destroy the state info */
        slapi_eq_once(destroy_task, (void *)task, expire);

        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
    }
}

/*
 * Stash some opaque task specific data in the task for later use.
 */
void slapi_task_set_data(Slapi_Task *task, void *data)
{
    if (task) {
        task->task_private = data;
    }
}

/*
 * Retrieve some opaque task specific data from the task.
 */
void * slapi_task_get_data(Slapi_Task *task)
{
    if (task) {
        return task->task_private;
    }

    return NULL; /* return value not currently used */
}

/*
 * Increment the task reference count
 */
void slapi_task_inc_refcount(Slapi_Task *task)
{
    if (task) {
        task->task_refcount++;
    }
}

/*
 * Decrement the task reference count
 */
void slapi_task_dec_refcount(Slapi_Task *task)
{
    if (task) {
        task->task_refcount--;
    }
}

/*
 * Returns the task reference count
 */
int slapi_task_get_refcount(Slapi_Task *task)
{
    if (task) {
        return task->task_refcount;
    }

    return 0; /* return value not currently used */
}

int
slapi_plugin_task_register_handler(const char *name, dseCallbackFn func, Slapi_PBlock *plugin_pb)
{
    Slapi_PBlock *add_pb = NULL;
    Slapi_Operation *op;
    LDAPMod *mods[3];
    LDAPMod mod[3];
    const char *objectclass[3];
    const char *cnvals[2];
    char *dn = NULL;
    int ret = -1;
    int x;

    dn = slapi_create_dn_string("cn=%s,%s", name, TASK_BASE_DN);
    if (NULL == dn) {
        LDAPDebug1Arg( LDAP_DEBUG_ANY,
                       "slapi_task_register_handler: "
                       "failed to create task dn for %s\n", name);
        return ret;
    }

    add_pb = slapi_pblock_new();
    if (add_pb == NULL) {
        goto out;
    }

    /* this is painful :( */
    mods[0] = &mod[0];
    mod[0].mod_op = LDAP_MOD_ADD;
    mod[0].mod_type = "objectClass";
    mod[0].mod_values = (char **)objectclass;
    objectclass[0] = "top";
    objectclass[1] = "extensibleObject";
    objectclass[2] = NULL;
    mods[1] = &mod[1];
    mod[1].mod_op = LDAP_MOD_ADD;
    mod[1].mod_type = "cn";
    mod[1].mod_values = (char **)cnvals;
    cnvals[0] = name;
    cnvals[1] = NULL;
    mods[2] = NULL;
    slapi_add_internal_set_pb(add_pb, dn, mods, NULL,
                              plugin_get_default_component_id(), 0);
    x = 1;
    slapi_pblock_set(add_pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &x);
    /* Make sure these adds don't appear in the audit and change logs */
    slapi_pblock_get(add_pb, SLAPI_OPERATION, &op);
    operation_set_flag(op, OP_FLAG_ACTION_NOLOG);

    slapi_add_internal_pb(add_pb);
    slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &x);
    if ((x != LDAP_SUCCESS) && (x != LDAP_ALREADY_EXISTS)) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Can't create task node '%s' (error %d)\n",
                  name, x, 0);
        ret = x;
        goto out;
    }

    /* register add callback */
    slapi_config_register_callback_plugin(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP,
                                   dn, LDAP_SCOPE_SUBTREE, "(objectclass=*)", func, NULL, plugin_pb);
    /* deny modify/delete of the root task entry */
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP,
                                   dn, LDAP_SCOPE_BASE, "(objectclass=*)", task_deny, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP,
                                   dn, LDAP_SCOPE_BASE, "(objectclass=*)", task_deny, NULL);

    ret = 0;

out:
    slapi_ch_free_string(&dn);
    if (add_pb) {
        slapi_pblock_destroy(add_pb);
    }
    return ret;
}

/* name is, for example, "import" */
int slapi_task_register_handler(const char *name, dseCallbackFn func)
{
    return slapi_plugin_task_register_handler(name, func, NULL);
}

void slapi_task_set_destructor_fn(Slapi_Task *task, TaskCallbackFn func)
{
    if (task) {
        task->destructor = func;
    }
}

void slapi_task_set_cancel_fn(Slapi_Task *task, TaskCallbackFn func)
{
    if (task) {
        task->cancel = func;
    }
}


/***********************************
 * Static Helper Functions
 ***********************************/
/* create a new task, fill in DN, and setup modify callback */
static Slapi_Task *
new_task(const char *rawdn, void *plugin)
{
    Slapi_Task *task = NULL;
    char *dn = NULL;

    if (rawdn == NULL) {
        return NULL;
    }

    dn = slapi_create_dn_string("%s", rawdn);
    if (NULL == dn) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "new_task failed: invalid task dn: %s\n", rawdn);
        return NULL;
    }
    task = (Slapi_Task *)slapi_ch_calloc(1, sizeof(Slapi_Task));
    PR_Lock(global_task_lock);
    task->next = global_task_list;
    global_task_list = task;
    PR_Unlock(global_task_lock);
    task->task_dn = dn;
    task->task_state = SLAPI_TASK_SETUP;
    task->task_flags = SLAPI_TASK_RUNNING_AS_TASK;
    task->destructor = NULL;
    task->cancel = NULL;
    task->task_private = NULL;
    task->origin_plugin = plugin;
    slapi_plugin_op_started(task->origin_plugin);

    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", task_modify, (void *)task);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)", task_deny, NULL);
    /* don't add entries under this one */
#if 0
    /* don't know why, but this doesn't work.  it makes the current add
     * operation fail. :(
     */
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=*)", task_deny, NULL);
#endif
    /* To protect task_log to be realloced if it's in use */
    task->task_log_lock = PR_NewLock();

    return task;
}

/* called by the event queue to destroy a task */
static void
destroy_task(time_t when, void *arg)
{
    Slapi_Task *task = (Slapi_Task *)arg;
    Slapi_Task *t1;
    Slapi_PBlock *pb;

    if (task == NULL) return;

    pb = slapi_pblock_new();

    /* Call the custom destructor callback if one was provided,
     * then perform the internal task destruction. */
    if (task->destructor != NULL) {
        (*task->destructor)(task);
    }

    task_generic_destructor(task);

    /* if when == 0, we're already locked (called during shutdown) */
    if (when != 0) {
        PR_Lock(global_task_lock);
    }
    if (global_task_list == task) {
        global_task_list = task->next;
    } else {
        for (t1 = global_task_list; t1; t1 = t1->next) {
            if (t1->next == task) {
                t1->next = task->next;
                break;
            }
        }
    }
    if (when != 0) {
        PR_Unlock(global_task_lock);
    }

    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP,
                                 task->task_dn, LDAP_SCOPE_BASE, "(objectclass=*)", task_modify);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP,
                                 task->task_dn, LDAP_SCOPE_BASE, "(objectclass=*)", task_deny);
    slapi_delete_internal_set_pb(pb, task->task_dn, NULL, NULL,
                                 (void *)plugin_get_default_component_id(), 0);

    slapi_delete_internal_pb(pb);
    slapi_pblock_destroy(pb);

    slapi_ch_free((void **)&task->task_dn);
    slapi_ch_free((void **)&task);
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

/* supply the pblock, destroy it when you're done */
static Slapi_Entry *get_internal_entry(Slapi_PBlock *pb, char *dn)
{
    Slapi_Entry **entries = NULL;
    int ret = 0;

    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_BASE, "(objectclass=*)",
                                 NULL, 0, NULL, NULL, (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't find task entry '%s'\n",
                  dn, 0, 0);
        return NULL;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((NULL == entries) || (NULL == entries[0])) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't find task entry '%s'\n",
                  dn, 0, 0);
        return NULL;
    }
    return entries[0];
}

static void modify_internal_entry(char *dn, LDAPMod **mods)
{
    Slapi_PBlock pb;
    Slapi_Operation *op;
    int ret = 0;
    int tries = 0;
    int dont_write_file = 1;

    do {

        pblock_init(&pb);

        slapi_modify_internal_set_pb(&pb, dn, mods, NULL, NULL,
        (void *)plugin_get_default_component_id(), 0);

        /* all modifications to the cn=tasks subtree are transient --
         * we erase them all when the server starts up next time, so there's
         * no need to save them in the dse file.
         */

        slapi_pblock_set(&pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &dont_write_file);
        /* Make sure these mods are not logged in audit or changelog */
        slapi_pblock_get(&pb, SLAPI_OPERATION, &op);
        operation_set_flag(op, OP_FLAG_ACTION_NOLOG);

        slapi_modify_internal_pb(&pb);
        slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
        if (ret != LDAP_SUCCESS) {
            /* could be waiting for another thread to finish adding this
             * entry -- try at least 3 times before giving up.
             */
            tries++;
            if (tries == 3) {
                LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't modify task "
                        "entry '%s'; %s (%d)\n", dn, ldap_err2string(ret), ret);
                pblock_done(&pb);
                return;
            }
            DS_Sleep(PR_SecondsToInterval(1));
        }

        pblock_done(&pb);

    } while (ret != LDAP_SUCCESS);
}

static void task_generic_destructor(Slapi_Task *task)
{
    if (task->task_log) {
        slapi_ch_free((void **)&task->task_log);
    }
    if (task->task_status) {
        slapi_ch_free((void **)&task->task_status);
    }
    if (task->task_log_lock) {
        PR_DestroyLock(task->task_log_lock);
        task->task_log_lock = NULL;
    }
    task->task_log = task->task_status = NULL;
}


/**********  actual task callbacks  **********/


static int task_deny(Slapi_PBlock *pb, Slapi_Entry *e,
                     Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    /* internal operations (conn=NULL) are allowed to do whatever they want */
    if (pb->pb_conn == NULL) {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

static int task_modify(Slapi_PBlock *pb, Slapi_Entry *e,
                       Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    Slapi_Task *task = (Slapi_Task *)arg;
    LDAPMod **mods;
    int i;

    /* the connection block will be NULL for internal operations */
    if (pb->pb_conn == NULL) {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    /* ignore eAfter, just scan the mods for anything unacceptable */
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (i = 0; (mods != NULL) && (mods[i] != NULL); i++) {
        /* for some reason, "modifiersName" and "modifyTimestamp" are
         * stuck in by the server */
        if ((strcasecmp(mods[i]->mod_type, "ttl") != 0) &&
            (strcasecmp(mods[i]->mod_type, "nsTaskCancel") != 0) &&
            (strcasecmp(mods[i]->mod_type, "modifiersName") != 0) &&
            (strcasecmp(mods[i]->mod_type, "modifyTimestamp") != 0)) {
            /* you aren't allowed to change this! */
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
    }

    /* okay, we've decided to accept these changes.  now look at the new
     * entry and absorb any new values.
     */
    if (strcasecmp(fetch_attr(eAfter, "nsTaskCancel", "false"), "true") == 0) {
        /* cancel this task, if not already */
        if (task->task_state != SLAPI_TASK_CANCELLED) {
            task->task_state = SLAPI_TASK_CANCELLED;
            if (task->cancel) {
                (*task->cancel)(task);
                LDAPDebug(LDAP_DEBUG_ANY, "Cancelling task '%s'\n",
                          fetch_attr(eAfter, "cn", "?"), 0, 0);
            }
        }
    }
    /* we fetch ttl from the entry when it's needed */

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

static int task_import_add(Slapi_PBlock *pb, Slapi_Entry *e,
                           Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;
    Slapi_Backend *be = NULL;
    const char *instance_name;
    char **ldif_file = NULL, **include = NULL, **exclude = NULL;
    int idx, rv = 0;
    const char *do_attr_indexes, *uniqueid_kind_str;
    int uniqueid_kind = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
    Slapi_PBlock mypb;
    Slapi_Task *task;
    char *nameFrombe_name = NULL;
    const char *encrypt_on_import = NULL;

    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        return SLAPI_DSE_CALLBACK_ERROR;
    }
    instance_name = fetch_attr(e, "nsInstance", NULL);

    encrypt_on_import = fetch_attr(e, "nsImportEncrypt", NULL);

    /* include/exclude suffixes */
    if (slapi_entry_attr_find(e, "nsIncludeSuffix", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            rv = charray_normdn_add(&include,
                                    (char *)slapi_value_get_string(val),
                                    "nsIncludeSuffix");
            if (0 != rv) {
                *returncode = LDAP_PARAM_ERROR;
                return SLAPI_DSE_CALLBACK_ERROR;
            }
        }
    }
    if (slapi_entry_attr_find(e, "nsExcludeSuffix", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            rv = charray_normdn_add(&exclude, 
                                    (char *)slapi_value_get_string(val),
                                    "nsExcludeSuffix");
            if (0 != rv) {
                *returncode = LDAP_PARAM_ERROR;
                return SLAPI_DSE_CALLBACK_ERROR;
            }
        }
    }

    /*
     * if instance is given, just use it to get the backend.
     * otherwise, we use included/excluded suffix list to specify a backend.
     */
    if (NULL == instance_name) {
        char **instances, **ip;
        int counter;

        if (slapi_lookup_instance_name_by_suffixes(include, exclude,
                                                   &instances) < 0) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "ERROR: No backend instance is specified.\n", 0, 0, 0);
            *returncode = LDAP_OBJECT_CLASS_VIOLATION;
            return SLAPI_DSE_CALLBACK_ERROR;
        }

        if (instances) {
            for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
                ;

            if (counter == 1){
                instance_name = *instances;
                nameFrombe_name = *instances;
                
            }
            else if (counter == 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ERROR: No backend instance is specified.\n", 0, 0, 0);
                *returncode = LDAP_OBJECT_CLASS_VIOLATION;
                return SLAPI_DSE_CALLBACK_ERROR;
            } else {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ERROR: Multiple backend instances are specified: "
                          "%s, %s, ...\n", instances[0], instances[1], 0);
                *returncode = LDAP_OBJECT_CLASS_VIOLATION;
                return SLAPI_DSE_CALLBACK_ERROR;
            }
        } else {
            *returncode = LDAP_OBJECT_CLASS_VIOLATION;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
    }

    /* lookup the backend */
    be = slapi_be_select_by_instance_name(instance_name);
    if (be == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "can't import to nonexistent backend %s\n",
                  instance_name, 0, 0);
        slapi_ch_free_string(&nameFrombe_name);
        *returncode = LDAP_NO_SUCH_OBJECT;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* refuse to do an import on pre-V3 plugins.  plugin api V3 is the one
     * for DS 5.0 where the import/export stuff changed a lot.
     */
    if (! SLAPI_PLUGIN_IS_V3(be->be_database)) {
        LDAPDebug(LDAP_DEBUG_ANY, "can't perform an import with pre-V3 "
                  "backend plugin %s\n", be->be_database->plg_name, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        slapi_ch_free_string(&nameFrombe_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }
    if (be->be_database->plg_ldif2db == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ERROR: no ldif2db function defined for "
                  "backend %s\n", be->be_database->plg_name, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        slapi_ch_free_string(&nameFrombe_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* get ldif filenames -- from here on, memory has been allocated */
    if (slapi_entry_attr_find(e, "nsFilename", &attr) != 0) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        slapi_ch_free_string(&nameFrombe_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }
    for (idx = slapi_attr_first_value(attr, &val);
         idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
        charray_add(&ldif_file, slapi_ch_strdup(slapi_value_get_string(val)));
    }

    do_attr_indexes = fetch_attr(e, "nsImportIndexAttrs", "true");
    uniqueid_kind_str = fetch_attr(e, "nsUniqueIdGenerator", NULL);
    if (uniqueid_kind_str != NULL) {
        if (strcasecmp(uniqueid_kind_str, "none") == 0) {
            uniqueid_kind = SLAPI_UNIQUEID_GENERATE_NONE;
        } else if (strcasecmp(uniqueid_kind_str, "deterministic") == 0) {
            uniqueid_kind = SLAPI_UNIQUEID_GENERATE_NAME_BASED;
        } else {
            /* default - time based */
            uniqueid_kind = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
        }
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to allocate new task!\n", 0, 0, 0);
        rv = LDAP_OPERATIONS_ERROR;
        goto out;
    }

    memset(&mypb, 0, sizeof(mypb));
    mypb.pb_backend = be;
    mypb.pb_plugin = be->be_database;
    mypb.pb_removedupvals = atoi(fetch_attr(e, "nsImportChunkSize", "0"));
    mypb.pb_ldif2db_noattrindexes =
        !(strcasecmp(do_attr_indexes, "true") == 0);
    mypb.pb_ldif_generate_uniqueid = uniqueid_kind;
    mypb.pb_ldif_namespaceid =
        (char *)fetch_attr(e, "nsUniqueIdGeneratorNamespace", NULL);
    mypb.pb_instance_name = (char *)instance_name;
    mypb.pb_ldif_files = ldif_file;
    mypb.pb_ldif_include = include;
    mypb.pb_ldif_exclude = exclude;
    mypb.pb_task = task;
    mypb.pb_task_flags = SLAPI_TASK_RUNNING_AS_TASK;
    if (NULL != encrypt_on_import && 0 == strcasecmp(encrypt_on_import, "true") ) {
        mypb.pb_ldif_encrypt = 1;
    }

    rv = (*mypb.pb_plugin->plg_ldif2db)(&mypb);
    if (rv == 0) {
        slapi_entry_attr_set_charptr(e, TASK_LOG_NAME, "");
        slapi_entry_attr_set_charptr(e, TASK_STATUS_NAME, "");
        slapi_entry_attr_set_int(e, TASK_PROGRESS_NAME, task->task_progress);
        slapi_entry_attr_set_int(e, TASK_WORK_NAME, task->task_work);
    }

out:
    slapi_ch_free_string(&nameFrombe_name);
    charray_free(ldif_file);
    charray_free(include);
    charray_free(exclude);
    if (rv != 0) {
        *returncode = LDAP_OPERATIONS_ERROR;
        destroy_task(1, task); 
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

static void task_export_thread(void *arg)
{
    Slapi_PBlock *pb = (Slapi_PBlock *)arg;
    char **instance_names = (char **)pb->pb_instance_name;
    char **inp;
    char *ldif_file = pb->pb_ldif_file;
    char *this_ldif_file = NULL;
    Slapi_Backend *be = NULL;
    int rv = -1;
    int count;
    Slapi_Task *task = pb->pb_task;

    g_incr_active_threadcnt();
    for (count = 0, inp = instance_names; *inp; inp++, count++)
        ;
    slapi_task_begin(task, count);

    for (inp = instance_names; *inp; inp++) {
        int release_me = 0;
        /* lookup the backend */
        be = slapi_be_select_by_instance_name((const char *)*inp);
        if (be == NULL) {
            /* shouldn't happen */
            LDAPDebug(LDAP_DEBUG_ANY, "ldbm2ldif: backend '%s' is AWOL!\n",
                      (const char *)*inp, 0, 0);
            continue;
        }

        pb->pb_backend = be;
        pb->pb_plugin = be->be_database;
        pb->pb_instance_name = (char *)*inp;

        /* ldif_file name for each? */
        if (pb->pb_ldif_printkey & EXPORT_APPENDMODE) {
            if (inp == instance_names) { /* first export */
                pb->pb_ldif_printkey |= EXPORT_APPENDMODE_1;
            } else {
                pb->pb_ldif_printkey &= ~EXPORT_APPENDMODE_1;
            }
        } else {
            if (strcmp(ldif_file, "-")) {    /* not '-' */
                char *p;
#if defined( _WIN32 )
                char sep = '\\';
                if (NULL != strchr(ldif_file, '/'))
                    sep = '/';
#else
                char sep = '/';
#endif
                this_ldif_file = (char *)slapi_ch_malloc(strlen(ldif_file) +
                                                     strlen(*inp) + 2);
                p = strrchr(ldif_file, sep);
                if (NULL == p) {
                    sprintf(this_ldif_file, "%s_%s", *inp, ldif_file);
                } else {
                    char *q;

                    q = p + 1;
                    *p = '\0';
                    sprintf(this_ldif_file, "%s%c%s_%s",
                                            ldif_file, sep, *inp, q);
                    *p = sep;
                }
                pb->pb_ldif_file = this_ldif_file;
                release_me = 1;
            }
        }

        slapi_task_log_notice(task, "Beginning export of '%s'", *inp);
        LDAPDebug(LDAP_DEBUG_ANY, "Beginning export of '%s'\n", *inp, 0, 0);

        rv = (*pb->pb_plugin->plg_db2ldif)(pb);
        if (rv != 0) {
            slapi_task_log_notice(task, "backend '%s' export failed (%d)",
                                  *inp, rv);
            LDAPDebug(LDAP_DEBUG_ANY,
                      "ldbm2ldif: backend '%s' export failed (%d)\n",
                      (const char *)*inp, rv, 0);
        }

        if (release_me) {
            slapi_ch_free((void **)&this_ldif_file);
        }

        if (rv != 0)
            break;

        slapi_task_inc_progress(task);
    }

    /* free the memory now */
    charray_free(instance_names);
    slapi_ch_free((void **)&ldif_file);
    charray_free(pb->pb_ldif_include);
    charray_free(pb->pb_ldif_exclude);
    slapi_pblock_destroy(pb);

    if (rv == 0) {
        slapi_task_log_notice(task, "Export finished.");
        LDAPDebug(LDAP_DEBUG_ANY, "Export finished.\n", 0, 0, 0);
    } else {
        slapi_task_log_notice(task, "Export failed.");
        LDAPDebug(LDAP_DEBUG_ANY, "Export failed.\n", 0, 0, 0);
    }

    slapi_task_finish(task, rv);
    g_decr_active_threadcnt();
}

static int task_export_add(Slapi_PBlock *pb, Slapi_Entry *e,
           Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;
    Slapi_Backend *be = NULL;
    char *ldif_file = NULL;
    char **instance_names = NULL, **inp;
    char **include = NULL, **exclude = NULL;
    int idx, rv = SLAPI_DSE_CALLBACK_OK;
    int export_replica_flag = 0;
    int ldif_printkey_flag = 0;
    int dump_uniqueid_flag = 0;
    int instance_cnt = 0;
    const char *my_ldif_file;
    const char *use_one_file;
    const char *export_replica;
    const char *ldif_printkey;
    const char *dump_uniqueid;
    Slapi_PBlock *mypb = NULL;
    Slapi_Task *task = NULL;
    PRThread *thread;
    const char *decrypt_on_export = NULL;

    *returncode = LDAP_SUCCESS;
    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    decrypt_on_export = fetch_attr(e, "nsExportDecrypt", NULL);

    /* nsInstances -- from here on, memory has been allocated */
    if (slapi_entry_attr_find(e, "nsInstance", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            charray_add(&instance_names,
                        slapi_ch_strdup(slapi_value_get_string(val)));
            instance_cnt++;
        }
    }

    /* include/exclude suffixes */
    if (slapi_entry_attr_find(e, "nsIncludeSuffix", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            rv = charray_normdn_add(&include, 
                                    (char *)slapi_value_get_string(val),
                                    "nsIncludeSuffix");
            if (0 != rv) {
                *returncode = LDAP_PARAM_ERROR;
                goto out;
            }
        }
    }
    if (slapi_entry_attr_find(e, "nsExcludeSuffix", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            rv = charray_normdn_add(&exclude, 
                                    (char *)slapi_value_get_string(val),
                                    "nsExcludeSuffix");
            if (0 != rv) {
                *returncode = LDAP_PARAM_ERROR;
                goto out;
            }
        }
    }

    if (NULL == instance_names) {
        char **ip;

        if (slapi_lookup_instance_name_by_suffixes(include, exclude,
                                                   &instance_names) < 0) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "ERROR: No backend instance is specified.\n", 0, 0, 0);
            *returncode = LDAP_OBJECT_CLASS_VIOLATION;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }

        if (instance_names) {
            for (ip = instance_names, instance_cnt = 0; ip && *ip;
                 ip++, instance_cnt++)
                ;

            if (instance_cnt == 0) {
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ERROR: No backend instance is specified.\n", 0, 0, 0);
                *returncode = LDAP_OBJECT_CLASS_VIOLATION;
                rv = SLAPI_DSE_CALLBACK_ERROR;
                goto out;
            }
        } else {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "ERROR: No backend instance is specified.\n", 0, 0, 0);
            *returncode = LDAP_OBJECT_CLASS_VIOLATION;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }
    }

    /* ldif file name */
    if ((my_ldif_file = fetch_attr(e, "nsFilename", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    ldif_file = slapi_ch_strdup(my_ldif_file);
    /* if true, multiple backends are dumped into one ldif file */
    use_one_file = fetch_attr(e, "nsUseOneFile", "true");
    if (strcasecmp(use_one_file, "true") == 0) {
        ldif_printkey_flag |= EXPORT_APPENDMODE;
    } 

    /* -r: export replica */
    export_replica = fetch_attr(e, "nsExportReplica", "false");
    if (!strcasecmp(export_replica, "true"))        /* true */
        export_replica_flag = 1;

    /* -N: eq "false" ==> does not print out key value */
    ldif_printkey = fetch_attr(e, "nsPrintKey", "true");
    if (!strcasecmp(ldif_printkey, "true"))                /* true */
        ldif_printkey_flag |= EXPORT_PRINTKEY;

    /* -C: eq "true" ==> use only id2entry file */
    ldif_printkey = fetch_attr(e, "nsUseId2Entry", "false");
    if (!strcasecmp(ldif_printkey, "true"))                /* true */
        ldif_printkey_flag |= EXPORT_ID2ENTRY_ONLY;

    /* if "true" ==> 8-bit strings are not base64 encoded */
    ldif_printkey = fetch_attr(e, "nsMinimalEncoding", "false");
    if (!strcasecmp(ldif_printkey, "true"))                /* true */
        ldif_printkey_flag |= EXPORT_MINIMAL_ENCODING;

    /* -U: eq "true" ==> does not fold the output */
    ldif_printkey = fetch_attr(e, "nsNoWrap", "false");
    if (!strcasecmp(ldif_printkey, "true"))                /* true */
        ldif_printkey_flag |= EXPORT_NOWRAP;

    /* -1: eq "true" ==> does not print version line */
    ldif_printkey = fetch_attr(e, "nsNoVersionLine", "false");
    if (!strcasecmp(ldif_printkey, "true"))                /* true */
        ldif_printkey_flag |= EXPORT_NOVERSION;

    /* -u: eq "false" ==> does not dump unique id */
    dump_uniqueid = fetch_attr(e, "nsDumpUniqId", "true");
    if (!strcasecmp(dump_uniqueid, "true"))        /* true */
        dump_uniqueid_flag = 1;

    /* check that all the backends are ok */
    for (inp = instance_names; *inp; inp++) {
        /* lookup the backend */
        be = slapi_be_select_by_instance_name((const char *)*inp);
        if (be == NULL) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "can't export to nonexistent backend %s\n", *inp, 0, 0);
            *returncode = LDAP_NO_SUCH_OBJECT;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }

        /* refuse to do an export on pre-V3 plugins.  plugin api V3 is the one
         * for DS 5.0 where the import/export stuff changed a lot.
         */
        if (! SLAPI_PLUGIN_IS_V3(be->be_database)) {
            LDAPDebug(LDAP_DEBUG_ANY, "can't perform an export with pre-V3 "
                      "backend plugin %s\n", be->be_database->plg_name, 0, 0);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }
        if (be->be_database->plg_db2ldif == NULL) {
            LDAPDebug(LDAP_DEBUG_ANY, "ERROR: no db2ldif function defined for "
                      "backend %s\n", be->be_database->plg_name, 0, 0);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to allocate new task!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    mypb = slapi_pblock_new();
    if (mypb == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mypb->pb_ldif_include = include;
    mypb->pb_ldif_exclude = exclude;
    mypb->pb_ldif_printkey = ldif_printkey_flag;
    mypb->pb_ldif_dump_replica = export_replica_flag;
    mypb->pb_ldif_dump_uniqueid = dump_uniqueid_flag;
    mypb->pb_ldif_file = ldif_file;
    /* horrible hack */
    mypb->pb_instance_name = (char *)instance_names;
    mypb->pb_task = task;
    mypb->pb_task_flags = SLAPI_TASK_RUNNING_AS_TASK;
    if (NULL != decrypt_on_export && 0 == strcasecmp(decrypt_on_export, "true") ) {
        mypb->pb_ldif_encrypt = 1;
    }

    /* start the export as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, task_export_thread,
                        (void *)mypb, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                        PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "unable to create ldbm2ldif thread!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_pblock_destroy(mypb);
        goto out;
    }

    /* thread successful -- don't free the pb, let the thread do that. */
    return SLAPI_DSE_CALLBACK_OK;

out:
    charray_free(instance_names);
    charray_free(include);
    charray_free(exclude);
    if (ldif_file != NULL) {
        slapi_ch_free((void **)&ldif_file);
    }
    if (task) {
        destroy_task(1, task);
    }

    return rv;
}


static void task_backup_thread(void *arg)
{
    Slapi_PBlock *pb = (Slapi_PBlock *)arg;
    Slapi_Task *task = pb->pb_task;
    int rv;

    g_incr_active_threadcnt();
    slapi_task_begin(task, 1);

    slapi_task_log_notice(task, "Beginning backup of '%s'",
                          pb->pb_plugin->plg_name);
    LDAPDebug(LDAP_DEBUG_ANY, "Beginning backup of '%s'\n",
              pb->pb_plugin->plg_name, 0, 0);

    rv = (*pb->pb_plugin->plg_db2archive)(pb);
    if (rv != 0) {
        slapi_task_log_notice(task, "Backup failed (error %d)", rv);
        slapi_task_log_status(task, "Backup failed (error %d)", rv);
        LDAPDebug(LDAP_DEBUG_ANY, "Backup failed (error %d)\n", rv, 0, 0);
    } else {
        slapi_task_log_notice(task, "Backup finished.");
        slapi_task_log_status(task, "Backup finished.");
        LDAPDebug(LDAP_DEBUG_ANY, "Backup finished.\n", 0, 0, 0);
    }

    slapi_task_finish(task, rv);
    slapi_ch_free((void **)&pb->pb_seq_val);
    slapi_pblock_destroy(pb);
    g_decr_active_threadcnt();
}

static int task_backup_add(Slapi_PBlock *pb, Slapi_Entry *e,
                           Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    Slapi_Backend *be = NULL;
    PRThread *thread = NULL;
    const char *archive_dir = NULL;
    const char *my_database_type = NULL;
    const char *database_type = "ldbm database";
    char *cookie = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_PBlock *mypb = NULL;
    Slapi_Task *task = NULL;

    *returncode = LDAP_SUCCESS;
    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* archive dir name */
    if ((archive_dir = fetch_attr(e, "nsArchiveDir", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* database type */
    my_database_type = fetch_attr(e, "nsDatabaseType", NULL);
    if (NULL != my_database_type)
        database_type = my_database_type;

    /* get backend that has db2archive and the database type matches.  */
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (NULL != be->be_database->plg_db2archive &&
            !strcasecmp(database_type, be->be_database->plg_name))
            break;

        be = (backend *)slapi_get_next_backend (cookie);
    }
    slapi_ch_free_string(&cookie);
    if (NULL == be || NULL == be->be_database->plg_db2archive) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "ERROR: no db2archive function defined.\n", 0, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    if (! SLAPI_PLUGIN_IS_V3(be->be_database)) {
        LDAPDebug(LDAP_DEBUG_ANY, "can't perform an backup with pre-V3 "
                  "backend plugin %s\n", be->be_database->plg_name, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to allocate new task!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    mypb = slapi_pblock_new();
    if (mypb == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mypb->pb_seq_val = slapi_ch_strdup(archive_dir);
    mypb->pb_plugin = be->be_database;
    mypb->pb_task = task;
    mypb->pb_task_flags = SLAPI_TASK_RUNNING_AS_TASK;

    /* start the backup as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, task_backup_thread,
                             (void *)mypb, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "unable to create backup thread!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_ch_free((void **)&mypb->pb_seq_val);
        slapi_pblock_destroy(mypb);
        goto out;
    }

    /* thread successful -- don't free the pb, let the thread do that. */
    return SLAPI_DSE_CALLBACK_OK;

out:
    if (task) {
        destroy_task(1, task);
    }
    return rv;
}


static void task_restore_thread(void *arg)
{
    Slapi_PBlock *pb = (Slapi_PBlock *)arg;
    Slapi_Task *task = pb->pb_task;
    int rv;

    g_incr_active_threadcnt();
    slapi_task_begin(task, 1);

    slapi_task_log_notice(task, "Beginning restore to '%s'",
                          pb->pb_plugin->plg_name);
    LDAPDebug(LDAP_DEBUG_ANY, "Beginning restore to '%s'\n",
              pb->pb_plugin->plg_name, 0, 0);

    rv = (*pb->pb_plugin->plg_archive2db)(pb);
    if (rv != 0) {
        slapi_task_log_notice(task, "Restore failed (error %d)", rv);
        slapi_task_log_status(task, "Restore failed (error %d)", rv);
        LDAPDebug(LDAP_DEBUG_ANY, "Restore failed (error %d)\n", rv, 0, 0);
    } else {
        slapi_task_log_notice(task, "Restore finished.");
        slapi_task_log_status(task, "Restore finished.");
        LDAPDebug(LDAP_DEBUG_ANY, "Restore finished.\n", 0, 0, 0);
    }

    slapi_task_finish(task, rv);
    slapi_ch_free((void **)&pb->pb_seq_val);
    slapi_ch_free_string(&pb->pb_instance_name);
    slapi_pblock_destroy(pb);
    g_decr_active_threadcnt();
}

static int task_restore_add(Slapi_PBlock *pb, Slapi_Entry *e,
                            Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    Slapi_Backend *be = NULL;
    const char *instance_name = NULL;
    const char *archive_dir = NULL;
    const char *my_database_type = NULL;
    const char *database_type = "ldbm database";
    char *cookie = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_PBlock *mypb = NULL;
    Slapi_Task *task = NULL;
    PRThread *thread = NULL;

    *returncode = LDAP_SUCCESS;
    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* archive dir name */
    if ((archive_dir = fetch_attr(e, "nsArchiveDir", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* database type */
    my_database_type = fetch_attr(e, "nsDatabaseType", NULL);
    if (NULL != my_database_type)
        database_type = my_database_type;

    instance_name = fetch_attr(e, "nsInstance", NULL);

    /* get backend that has archive2db and the database type matches.  */
    be = slapi_get_first_backend (&cookie);
    while (be) {
        if (NULL != be->be_database->plg_archive2db &&
            !strcasecmp(database_type, be->be_database->plg_name))
            break;

        be = (backend *)slapi_get_next_backend (cookie);
    }
    slapi_ch_free_string(&cookie);
    if (NULL == be || NULL == be->be_database->plg_archive2db) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "ERROR: no archive2db function defined.\n", 0, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* refuse to do an export on pre-V3 plugins.  plugin api V3 is the one
     * for DS 5.0 where the import/export stuff changed a lot.
     */
    if (! SLAPI_PLUGIN_IS_V3(be->be_database)) {
        LDAPDebug(LDAP_DEBUG_ANY, "can't perform an restore with pre-V3 "
                  "backend plugin %s\n", be->be_database->plg_name, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to allocate new task!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    mypb = slapi_pblock_new();
    if (mypb == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mypb->pb_seq_val = slapi_ch_strdup(archive_dir);
    mypb->pb_plugin = be->be_database;
    if (NULL != instance_name)
        mypb->pb_instance_name = slapi_ch_strdup(instance_name);
    mypb->pb_task = task;
    mypb->pb_task_flags = SLAPI_TASK_RUNNING_AS_TASK;

    /* start the restore as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, task_restore_thread,
                             (void *)mypb, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "unable to create restore thread!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_ch_free((void **)&mypb->pb_seq_val);
        slapi_ch_free_string(&pb->pb_instance_name);
        slapi_pblock_destroy(mypb);
        goto out;
    }

    /* thread successful -- don't free the pb, let the thread do that. */
    return SLAPI_DSE_CALLBACK_OK;

out:
    if (task) {
        destroy_task(1, task);
    }
    return rv;
}


static void task_index_thread(void *arg)
{
    Slapi_PBlock *pb = (Slapi_PBlock *)arg;
    Slapi_Task *task = pb->pb_task;
    int rv;

    g_incr_active_threadcnt();
    slapi_task_begin(task, 1);

    rv = (*pb->pb_plugin->plg_db2index)(pb);
    if (rv != 0) {
        slapi_task_log_notice(task, "Index failed (error %d)", rv);
        slapi_task_log_status(task, "Index failed (error %d)", rv);
        LDAPDebug(LDAP_DEBUG_ANY, "Index failed (error %d)\n", rv, 0, 0);
    }

    slapi_task_finish(task, rv);
    charray_free(pb->pb_db2index_attrs);
    slapi_ch_free((void **)&pb->pb_instance_name);
    slapi_pblock_destroy(pb);
    g_decr_active_threadcnt();
}

static int task_index_add(Slapi_PBlock *pb, Slapi_Entry *e,
                          Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg)
{
    const char *instance_name;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_Backend *be = NULL;
    Slapi_Task *task = NULL;
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;
    char **indexlist = NULL;
    int idx;
    Slapi_PBlock *mypb = NULL;
    PRThread *thread = NULL;

    *returncode = LDAP_SUCCESS;
    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if ((instance_name = fetch_attr(e, "nsInstance", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* lookup the backend */
    be = slapi_be_select_by_instance_name(instance_name);
    if (be == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "can't import to nonexistent backend %s\n",
                  instance_name, 0, 0);
        *returncode = LDAP_NO_SUCH_OBJECT;
        return SLAPI_DSE_CALLBACK_ERROR;
    }
    if (be->be_database->plg_db2index == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ERROR: no db2index function defined for "
                  "backend %s\n", be->be_database->plg_name, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* normal indexes */
    if (slapi_entry_attr_find(e, "nsIndexAttribute", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            const char *indexname = slapi_value_get_string(val);
            char *index = slapi_ch_smprintf("t%s", indexname);

            if (index != NULL) {
                charray_add(&indexlist, index);
            }
        }
    }

    /* vlv indexes */
    if (slapi_entry_attr_find(e, "nsIndexVlvAttribute", &attr) == 0) {
        for (idx = slapi_attr_first_value(attr, &val);
             idx >= 0; idx = slapi_attr_next_value(attr, idx, &val)) {
            const char *indexname = slapi_value_get_string(val);
            char *index = slapi_ch_smprintf("T%s", indexname);

            if (index != NULL) {
                charray_add(&indexlist, index);
            }
        }
    }

    if (NULL == indexlist) {
        LDAPDebug(LDAP_DEBUG_ANY, "no index is specified!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_OK;
        goto out;
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to allocate new task!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    mypb = slapi_pblock_new();
    if (mypb == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mypb->pb_backend = be;
    mypb->pb_plugin = be->be_database;
    mypb->pb_instance_name = slapi_ch_strdup(instance_name);
    mypb->pb_db2index_attrs = indexlist;
    mypb->pb_task = task;
    mypb->pb_task_flags = SLAPI_TASK_RUNNING_AS_TASK;

    /* start the db2index as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, task_index_thread,
                             (void *)mypb, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "unable to create index thread!\n", 0, 0, 0);
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_ch_free((void **)&mypb->pb_instance_name);
        slapi_pblock_destroy(mypb);
        goto out;
    }

    /* thread successful -- don't free the pb, let the thread do that. */
    return SLAPI_DSE_CALLBACK_OK;

out:
    if (task) {
        destroy_task(1, task);
    }
    if (indexlist) {
        charray_free(indexlist);
    }
    return rv;
}

static int
task_upgradedb_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter,
                   int *returncode, char *returntext, void *arg)
{
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_Backend *be = NULL;
    Slapi_Task *task = NULL;
    Slapi_PBlock mypb;
    const char *archive_dir = NULL;
    const char *force = NULL;
    const char *database_type = "ldbm database";
    const char *my_database_type = NULL;
    char *cookie = NULL;

    *returncode = LDAP_SUCCESS;
    if (fetch_attr(e, "cn", NULL) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* archive dir name */
    if ((archive_dir = fetch_attr(e, "nsArchiveDir", NULL)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* database type */
    my_database_type = fetch_attr(e, "nsDatabaseType", NULL);
    if (NULL != my_database_type)
        database_type = my_database_type;

    /* force to reindex? */
    force = fetch_attr(e, "nsForceToReindex", NULL);

    /* get backend that has db2archive and the database type matches.  */
    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (NULL != be->be_database->plg_upgradedb)
            break;

        be = (backend *)slapi_get_next_backend (cookie);
    }
    slapi_ch_free_string(&cookie);
    if (NULL == be) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "ERROR: no upgradedb is defined.\n", 0, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if (NULL == be->be_database->plg_upgradedb ||
        strcasecmp(database_type, be->be_database->plg_name)) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "ERROR: no upgradedb is defined in %s.\n",
                  be->be_database->plg_name, 0, 0);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to allocate new task!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /* NGK - This could use some cleanup to use the SLAPI task API, such as slapi_task_begin() */
    task->task_work = 1;
    task->task_progress = 0;

    memset(&mypb, 0, sizeof(mypb));
    mypb.pb_backend = be;
    mypb.pb_plugin = be->be_database;
    if (force && 0 == strcasecmp(force, "true"))
        mypb.pb_seq_type = SLAPI_UPGRADEDB_FORCE; /* force; reindex all regardless the dbversion */
    mypb.pb_seq_val = slapi_ch_strdup(archive_dir);
    mypb.pb_task = task;
    mypb.pb_task_flags = SLAPI_TASK_RUNNING_AS_TASK;

    rv = (mypb.pb_plugin->plg_upgradedb)(&mypb);
    if (rv == 0) {
        slapi_entry_attr_set_charptr(e, TASK_LOG_NAME, "");
        slapi_entry_attr_set_charptr(e, TASK_STATUS_NAME, "");
        slapi_entry_attr_set_int(e, TASK_PROGRESS_NAME, task->task_progress);
        slapi_entry_attr_set_int(e, TASK_WORK_NAME, task->task_work);
    }

out:
    slapi_ch_free((void **)&mypb.pb_seq_val);
    if (rv != 0) {
        if (task)
            destroy_task(1, task);

        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

/*
 * sysconfig reload task
 *
 *   dn: cn=keytab_update,cn=sysconfig reload,cn=tasks,cn=config
 *   objectclass: top
 *   objectclass: extensibleObject
 *   cn: keytab_update
 *   sysconfigfile: /etc/sysconfig/dirsrv-localhost
 *   logchanges: <boolean>
 *
 * Reload environment variables from the instance sysconfig file, or
 * any file using the following formats:
 *
 *    VARIABLE=value
 *    export VARIABLE=value
 *    set VARIABLE value
 *    unset VARIABLE
 *    setenv VARIABLE value
 *    unsetenv VARIABLE
 */
static int
task_sysconfig_reload_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode,
                   char *returntext, void *arg)
{
    FILE *file = NULL;
    char *filename = NULL;
    PRBool logchanges = 0;
    int rc = SLAPI_DSE_CALLBACK_OK;

    *returncode = LDAP_SUCCESS;

    if(( filename = slapi_entry_attr_get_charptr(e, TASK_SYSCONFIG_FILE_ATTR))){
        file = fopen ( filename, "r" );
    } else {
        *returncode = LDAP_OPERATIONS_ERROR;
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "missing required attribute \"%s\".",
                     TASK_SYSCONFIG_FILE_ATTR);
        LDAPDebug(LDAP_DEBUG_ANY, "sysconfig reload task: %s\n", returntext, 0, 0);
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto done;
    }

    /* see if we should log the changes made */
    logchanges = slapi_entry_attr_get_bool(e, TASK_SYSCONFIG_LOGCHANGES_ATTR);

    if ( file != NULL ){
        char line[4096];
        char *s = NULL;
        /* fgets() reads in at most one less than size characters */
        char *end_of_line = line + sizeof(line) - 1;

        if(logchanges){
            LDAPDebug(LDAP_DEBUG_ANY, "sysconfig reload task: processing file (%s)\n",
                      filename, 0 , 0);
        }

        while ( fgets ( line, sizeof line, file ) != NULL ){
            if(line[0] == '#'){
                /* skip comments */
                continue;
            } else {
                char env_value[sizeof(line)];
                char env_var[sizeof(line)];
                int using_setenv = 0;
                int value_index = 0;
                int start_value = 0;
                int var_index = 0;
                int inquotes = 0;

                memset(env_var, 0, sizeof(env_var));
                memset(env_value, 0, sizeof(env_value));

                /*
                 * Remove leading spaces and tabs
                 */
                for (s = line; s && *s; s++){
                    if(*s != ' ' && *s != '\t'){
                        break;
                    }
                }

                /*
                 * Check for "export", "setenv", and "unsetenv" assignments
                 */
                if(strncmp(s, "export ", 7) == 0 ||
                   strncmp(s, "set ", 4) == 0 ||
                   strncmp(s, "unset ", 6) == 0 ||
                   strncmp(s, "setenv ", 7) == 0 ||
                   strncmp(s, "unsetenv ", 9) == 0 )
                {
                    if(*s == 's' || *s == 'u'){ /*  */
                        /*
                         * Using setenv/unsetenv/set/unset, so we need to handle spaces
                         * differently for these assignments.
                         */
                        using_setenv = 1;
                    }
                    if(strncmp(s, "export ", 7) == 0){
                        /* strip off "export " */
                        s = s + 7;
                    } else if(strncmp(s, "set ", 4) == 0){
                        /* strip off "set " */
                        s = s + 4;
                    } else if(strncmp(s, "unset ", 6) == 0){
                        /* strip off "unset " */
                        s = s + 6;
                    } else if(strncmp(s, "setenv ", 7) == 0){
                        /* strip off "setenv " */
                        s = s + 7;
                    } else if(strncmp(s, "unsetenv ", 9) == 0){
                        /* strip off "unsetenv " */
                        s = s + 9;
                    }
                    while(*s == ' ' || *s == '\t'){
                        /* remove any extra spaces/tabs between the assignment cmd and the name */
                        s++;
                    }
                }

                /*
                 * Start parsing the names and values
                 */
                for (; s && (s < end_of_line) && *s; s++){
                    /*
                     * If using "setenv", allow the first space/tab only, and start on the env value
                     */
                    if(using_setenv && (*s == ' ' || *s == '\t')){
                        using_setenv = 0; /* finished doing special space parsing for setenv */
                        start_value = 1; /* start working on the value */
                        while(*s == ' ' || *s == '\t'){
                            /* remove any extra spaces/tabs between variable name and value */
                            s++;
                        }
                    } else if( ((*s == ';' || *s == ' ') && !inquotes) || *s == '\0' || *s == '\n' || *s == '\r'){
                        /* we're done processing the value */
                        break;
                    }

                    /* need to handle quoted values */
                    if(*s == '"'){
                        if(inquotes){
                            inquotes = 0;
                        } else {
                            inquotes = 1;
                        }
                    }

                    if(start_value){
                        /* Build the environment variable value */
                        env_value[value_index] = *s;
                        value_index++;
                    } else if(*s == '='){
                        /* Start on the environment variable value next */
                        start_value = 1;
                    } else {
                        /* Build the environment variable name. skip "export" */
                        env_var[var_index] = *s;
                        var_index++;
                    }
                }
                if(var_index > 0){
                    /* Update the environment variable */
                    if(setenv(env_var, env_value, 1) != 0){
                        *returncode = LDAP_OPERATIONS_ERROR;
                        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,"failed to set (%s)", env_var);
                        LDAPDebug(LDAP_DEBUG_ANY, "sysconfig reload task: %s\n",returntext,0,0);
                        rc = SLAPI_DSE_CALLBACK_ERROR;
                        break;
                    }
                    if(logchanges){
                        LDAPDebug(LDAP_DEBUG_ANY, "sysconfig reload task: set (%s) to (%s)\n",
                                  env_var, env_value , 0);
                    }
                }
            }
        }
        fclose ( file );
    } else {
        *returncode = LDAP_OPERATIONS_ERROR;
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,"failed to open file \"%s\" (%s)",
                filename, strerror(errno));
        LDAPDebug(LDAP_DEBUG_ANY, "sysconfig reload task: %s\n", returntext, 0, 0);
        rc = SLAPI_DSE_CALLBACK_ERROR;
    }

done:
    slapi_ch_free_string(&filename);

    return rc;
}

/*
 * Add the nsTombstoneCSN attribute/value to the entry.
 */
static int
fixup_tombstone(Slapi_PBlock *pb, char *suffix, Slapi_Entry *e, int *fixup_count)
{
    LDAPMod mod;
    LDAPMod *mods[2];
    const CSN *tombstone_csn = NULL;
    char deletion_csn_str[CSN_STRSIZE];
    char *val[2];
    int rc = LDAP_SUCCESS;

    if((tombstone_csn = entry_get_deletion_csn(e))){
        slapi_log_error(SLAPI_LOG_REPL, TASK_TOMBSTONE_FIXUP,
            "Fixing tombstone (%s)\n", slapi_entry_get_dn(e));

        /* We have an entry tombstone that needs fixing */
        slapi_pblock_init(pb);
        csn_as_string(tombstone_csn, PR_FALSE, deletion_csn_str);
        mods[0] = &mod;
        mods[1] = 0;

        val[0] = deletion_csn_str;
        val[1] = 0;

        mod.mod_op = LDAP_MOD_ADD;
        mod.mod_type = SLAPI_ATTR_TOMBSTONE_CSN;
        mod.mod_values = val;

        slapi_modify_internal_set_pb_ext( pb, slapi_entry_get_sdn(e),
            mods, 0, 0, (void *)plugin_get_default_component_id(),
            OP_FLAG_TOMBSTONE_ENTRY | OP_FLAG_TOMBSTONE_FIXUP);
        slapi_modify_internal_pb(pb);

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        pblock_done(pb);
        if(rc == LDAP_SUCCESS){
            (*fixup_count)++;
        }
    }

    return rc;
}

/*
 * Strip out nsTombstoneCSN so the task can be run again to remove them.  Used
 * solely for testing the fixup task.
 */
static void
strip_tombstone(Slapi_PBlock *pb, char *suffix, Slapi_Entry *e, int *strip_count)
{
    LDAPMod mod;
    LDAPMod *mods[2];
    int rc = 0;

    slapi_log_error(SLAPI_LOG_REPL, TASK_TOMBSTONE_FIXUP,
            "Stripping tombstone (%s)\n", slapi_entry_get_dn(e));

    /* We have an entry tombstone that needs stripping */
    slapi_pblock_init(pb);
    mods[0] = &mod;
    mods[1] = 0;

    mod.mod_op = LDAP_MOD_DELETE;
    mod.mod_type = SLAPI_ATTR_TOMBSTONE_CSN;
    mod.mod_values = NULL;

    slapi_modify_internal_set_pb_ext( pb, slapi_entry_get_sdn(e),
             mods, 0, 0, (void *)plugin_get_default_component_id(),
             OP_FLAG_TOMBSTONE_ENTRY | OP_FLAG_TOMBSTONE_FIXUP);
    slapi_modify_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    pblock_done(pb);

    if(rc == LDAP_SUCCESS){
        (*strip_count)++;
    } else {
        slapi_log_error(SLAPI_LOG_REPL, TASK_TOMBSTONE_FIXUP,
            "Stripping tombstone (%s) failed, error %d\n", slapi_entry_get_dn(e), rc);
    }
}

struct task_tombstone_data
{
    char **base;
    int stripcsn;
    Slapi_Task *task;
};

/*
 * Fix tombstone thread - add missing nsTombstoneCSN
 */
static void
task_fixup_tombstone_thread(void *arg)
{
    struct task_tombstone_data *task_data = arg;
    Slapi_Entry **entries = NULL;
    Slapi_Task *task = task_data->task;
    char **base = task_data->base;
    char *filter = NULL;
    int fixup_count = 0;
    int rc, i, j;

    slapi_task_begin(task, 1);
    slapi_task_log_notice(task, "Beginning tombstone fixup task...\n");
    slapi_log_error(SLAPI_LOG_REPL, TASK_TOMBSTONE_FIXUP,
                    "Beginning tombstone fixup task...\n");

    if(task_data->stripcsn){
        /* find tombstones with nsTombstoneCSN */
        filter = "(&(nstombstonecsn=*)(objectclass=nsTombstone))";
    } else {
        /* find tombstones missing nsTombstoneCSN */
        filter = "(&(!(nstombstonecsn=*))(objectclass=nsTombstone))";
    }

    /* Okay check the specified backends only */
    for(i = 0; base && base[i]; i++){
        Slapi_PBlock *search_pb = slapi_pblock_new();

        /* find entries that need fixing... */
        slapi_search_internal_set_pb(search_pb, base[i], LDAP_SCOPE_SUBTREE,
                filter, NULL, 0, NULL, NULL, plugin_get_default_component_id(), 0);
        slapi_search_internal_pb(search_pb);

        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc != LDAP_SUCCESS) {
            slapi_task_log_notice(task,
                    "Failed to search backend for tombstones, error %d\n", rc);
            slapi_log_error(SLAPI_LOG_REPL, TASK_TOMBSTONE_FIXUP,
                    "Failed to search backend for tombstones, error %d\n", rc);
            slapi_pblock_destroy(search_pb);
            slapi_task_finish(task, rc);
            return;
        }

        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries) {
            Slapi_PBlock *fixup_pb = slapi_pblock_new();

            /* process all the tombstone entries */
            for (j = 0; entries[j]; j++){
                if(task_data->stripcsn){
                    /* strip nsTombstoneCSN - used to testing */
                    strip_tombstone(fixup_pb, base[i], entries[j], &fixup_count);
                } else if((rc = fixup_tombstone(fixup_pb, base[i], entries[j], &fixup_count))){
                    /* Failed to update tombstone, log it and move on... */
                    slapi_task_log_notice(task,
                           "Failed to update tombstone entry (%s) error %d\n",
                            slapi_entry_get_dn(entries[j]), rc);
                    slapi_log_error(SLAPI_LOG_FATAL, TASK_TOMBSTONE_FIXUP,
                            "Failed to update tombstone entry (%s) error %d\n",
                            slapi_entry_get_dn(entries[j]), rc);
                }
            }
            slapi_free_search_results_internal(search_pb);
            slapi_pblock_destroy(fixup_pb);
        }
        slapi_pblock_destroy(search_pb);
        slapi_task_inc_progress(task);
    }
    slapi_task_log_notice(task, "%s %d tombstones.\n",
                          task_data->stripcsn ? "Stripped" : "Fixed", fixup_count);
    slapi_log_error(SLAPI_LOG_REPL, TASK_TOMBSTONE_FIXUP, "%s %d tombstones.\n",
                    task_data->stripcsn ? "Stripped" : "Fixed", fixup_count);
    slapi_task_inc_progress(task);
    slapi_task_finish(task, rc);
    slapi_ch_array_free(base);
    slapi_ch_free((void **)&task_data);
}


/*
 *  task_fixup_tombstones_add
 *
 *  Check all the existing tombstones and add nsTombstoneCSN if missing.
 *
 *  dn: cn=fixem,cn=fixup tombstones,cn=tasks,cn=config
 *  objectclass: top
 *  objectclass: extensibleObject
 *  cn: fixem
 *  backend: userRoot
 *  suffix: dc=example,dc=com
 *  stripcsn: yes
 *
 *  backend & suffix are optional.  If skipped, all backends/suffixes are
 *  checked.  Multiple suffixes can also be specified.
 *
 *  Hidden option: "stripcsn" is strictly used to verify the fixup task: run
 *  the task using the strip option to strip tombstones of "nsTombstoneCSN",
 *  then run task, without the strip option, to add "nsTombstoneCSN" back.
 */
static int
task_fixup_tombstones_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter,
                          int *returncode, char *returntext, void *arg)
{
    Slapi_Backend *be = NULL;
    Slapi_Task *task = NULL;
    struct task_tombstone_data *task_data = NULL;
    const Slapi_DN *base_sdn = NULL;
    PRThread *thread = NULL;
    char **backend = NULL;
    char **suffix = NULL;
    char **base = NULL;
    char *stripcsn = NULL;
    int i;

    /*
     * Get the task options.  We will store all the "backends" in the suffix array.
     */
    if((suffix = slapi_entry_attr_get_charray(e, TASK_TOMBSTONE_FIXUP_SUFFIX))){
        for (i = 0; suffix && suffix[i]; i++){
            char *dn = slapi_create_dn_string("%s", suffix[i]);

            if(dn){
                if(slapi_dn_syntax_check(pb, dn, 1)){
                    /* invalid suffix name */
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Invalid DN (%s) used for \"suffix\"\n", suffix[i]);
                    *returncode = LDAP_INVALID_DN_SYNTAX;
                    goto done;
                } else {
                    slapi_ch_array_add(&base, dn);
                }
            }
        }
    }
    if((backend = slapi_entry_attr_get_charray(e, TASK_TOMBSTONE_FIXUP_BACKEND))){
        for (i = 0; backend && backend[i]; i++){
            if((be = slapi_be_select_by_instance_name(backend[i]))){
                if((base_sdn = slapi_be_getsuffix(be, 0))){
                    slapi_ch_array_add(&base, slapi_ch_strdup(slapi_sdn_get_ndn(base_sdn)));
                } else {
                    /* failed to get a suffix */
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "Failed to find a suffix for the backend(%s)\n", backend[i]);
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    goto done;
                }
            } else {
                /* Failed to find a backend */
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Failed to find a backend using (%s)\n", backend[i]);
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                goto done;
            }
        }
    }

    /*
     * If suffix is NULL, we check all the backends
     */
    if(base == NULL){
        char *cookie = NULL;

        /* Gather all the backends */
        be = slapi_get_first_backend(&cookie);
        while(be){
            if((base_sdn = slapi_be_getsuffix(be, 0)) && !be->be_private){
                const char *suf = slapi_sdn_get_ndn(base_sdn);
                /* Need to skip the retro changelog */
                if(strcmp(suf, "cn=changelog"))
                {
                    slapi_ch_array_add(&base, slapi_ch_strdup(suf));
                }
            }
            be = slapi_get_next_backend(cookie);
        }
        slapi_ch_free_string(&cookie);
    }

    task = slapi_new_task(slapi_entry_get_ndn(e));
    task_data = (struct task_tombstone_data *)slapi_ch_calloc(1, sizeof(struct task_tombstone_data));
    task_data->base = base;
    task_data->task = task;
    if((stripcsn = slapi_entry_attr_get_charptr(e, TASK_TOMBSTONE_FIXUP_STRIPCSN))){
        if(strcasecmp(stripcsn, "yes") == 0 || strcasecmp(stripcsn, "on") == 0){
            task_data->stripcsn = 1;
        }
        slapi_ch_free_string(&stripcsn);
    }

    /* start the db2index as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, task_fixup_tombstone_thread,
                             (void *)task_data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "task_fixup_tombstones_add: unable to create index thread!\n", 0, 0, 0);
        *returncode = LDAP_OPERATIONS_ERROR;
        slapi_task_finish(task, *returncode);
        slapi_ch_array_free(base);
        slapi_ch_free((void **)&task_data);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

done:
    slapi_ch_array_free(suffix);
    slapi_ch_array_free(backend);

    if (*returncode != LDAP_SUCCESS){
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    return SLAPI_DSE_CALLBACK_OK;
}

/* cleanup old tasks that may still be in the DSE from a previous session
 * (this can happen if the server crashes [no matter how unlikely we like
 * to think that is].)
 */
void task_cleanup(void)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Entry **entries = NULL;
    int ret = 0, i, x;
    Slapi_DN *rootDN;

    slapi_search_internal_set_pb(pb, TASK_BASE_DN, LDAP_SCOPE_SUBTREE,
                                 "(objectclass=*)", NULL, 0, NULL, NULL,
                                 (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: entire cn=tasks tree seems to "
                  "be AWOL!\n", 0, 0, 0);
        slapi_pblock_destroy(pb);
        return;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (NULL == entries) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: entire cn=tasks tree seems to "
                  "be AWOL!\n", 0, 0, 0);
        slapi_pblock_destroy(pb);
        return;
    }

    rootDN = slapi_sdn_new_dn_byval(TASK_BASE_DN);

    /* rotate through entries, skipping the base dn */
    for (i = 0; entries[i] != NULL; i++) {
        const Slapi_DN *sdn = slapi_entry_get_sdn_const(entries[i]);
        Slapi_PBlock *mypb;
        Slapi_Operation *op;
        
        if (slapi_sdn_compare(sdn, rootDN) == 0)
            continue;

        mypb = slapi_pblock_new();
        if (mypb == NULL) {
            continue;
        }
        slapi_delete_internal_set_pb(mypb, slapi_sdn_get_dn(sdn), NULL, NULL,
                                     plugin_get_default_component_id(), 0);

        /* Make sure these deletes don't appear in the audit and change logs */
        slapi_pblock_get(mypb, SLAPI_OPERATION, &op);
        operation_set_flag(op, OP_FLAG_ACTION_NOLOG);

        x = 1;
        slapi_pblock_set(mypb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, &x);
        slapi_delete_internal_pb(mypb);
        slapi_pblock_destroy(mypb);
    }

    slapi_sdn_free(&rootDN);
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
}

void task_init(void)
{
    global_task_lock = PR_NewLock();
    if (global_task_lock == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "unable to create global tasks lock! "
                  "(that's bad)\n", 0, 0, 0);
        return;
    }

    slapi_task_register_handler("import", task_import_add);
    slapi_task_register_handler("export", task_export_add);
    slapi_task_register_handler("backup", task_backup_add);
    slapi_task_register_handler("restore", task_restore_add);
    slapi_task_register_handler("index", task_index_add);
    slapi_task_register_handler("upgradedb", task_upgradedb_add);
    slapi_task_register_handler("sysconfig reload", task_sysconfig_reload_add);
    slapi_task_register_handler("fixup tombstones", task_fixup_tombstones_add);
}

/* called when the server is shutting down -- abort all existing tasks */
void task_shutdown(void)
{
    Slapi_Task *task;
    int found_any = 0;

    /* first, cancel all tasks */
    PR_Lock(global_task_lock);
    shutting_down = 1;
    for (task = global_task_list; task; task = task->next) {
        if ((task->task_state != SLAPI_TASK_CANCELLED) &&
            (task->task_state != SLAPI_TASK_FINISHED)) {
            task->task_state = SLAPI_TASK_CANCELLED;
            if (task->cancel) {
                LDAPDebug(LDAP_DEBUG_ANY, "Cancelling task '%s'\n",
                          task->task_dn, 0, 0);
                (*task->cancel)(task);
                found_any = 1;
            }
        }
    }

    if (found_any) {
        /* give any tasks 1 second to say their last rites */
        DS_Sleep(PR_SecondsToInterval( 1 ));
    }

    while (global_task_list) {
        destroy_task(0, global_task_list);
    }
    PR_Unlock(global_task_lock);
}
