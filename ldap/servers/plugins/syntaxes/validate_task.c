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

/* validate_task.c - syntax validation task */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

/*
 * Globals
 */
static void *_PluginID = NULL;


/*
 * Data Structures
 */
typedef struct _task_data
{
    char *dn;
    char *filter_str;
    Slapi_Counter *invalid_entries;
} task_data;


/*
 * Function Prototypes
 */
int syntax_validate_task_init(Slapi_PBlock *pb);
static int syntax_validate_task_start(Slapi_PBlock *pb);
static int syntax_validate_task_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static void syntax_validate_task_destructor(Slapi_Task *task);
static void syntax_validate_task_thread(void *arg);
static int syntax_validate_task_callback(Slapi_Entry *e, void *callback_data);
static void syntax_validate_set_plugin_id(void *plugin_id);
static void *syntax_validate_get_plugin_id(void);


/*
 * Function Implementations
 */
int
syntax_validate_task_init(Slapi_PBlock *pb)
{
    int rc = 0;
    char *syntax_validate_plugin_identity = NULL;

    /* Save plugin ID. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &syntax_validate_plugin_identity);
    PR_ASSERT(syntax_validate_plugin_identity);
    syntax_validate_set_plugin_id(syntax_validate_plugin_identity);

    /* Register task callback. */
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_03);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                           (void *)syntax_validate_task_start);

    return rc;
}

static int
syntax_validate_task_start(Slapi_PBlock *pb __attribute__((unused)))
{
    int rc = slapi_task_register_handler("syntax validate", syntax_validate_task_add);
    return rc;
}

static int
syntax_validate_task_add(Slapi_PBlock *pb __attribute__((unused)),
                         Slapi_Entry *e,
                         Slapi_Entry *eAfter __attribute__((unused)),
                         int *returncode,
                         char *returntext __attribute__((unused)),
                         void *arg __attribute__((unused)))
{
    PRThread *thread = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    task_data *mytaskdata = NULL;
    Slapi_Task *task = NULL;
    const char *filter;
    const char *dn = 0;

    *returncode = LDAP_SUCCESS;
    /* get arg(s) */
    if ((dn = slapi_fetch_attr(e, "basedn", 0)) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    if ((filter = slapi_fetch_attr(e, "filter", "(objectclass=*)")) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* setup our task data */
    mytaskdata = (task_data *)slapi_ch_malloc(sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mytaskdata->dn = slapi_ch_strdup(dn);
    mytaskdata->filter_str = slapi_ch_strdup(filter);
    mytaskdata->invalid_entries = slapi_counter_new();

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));

    /* register our destructor for cleaning up our private data */
    slapi_task_set_destructor_fn(task, syntax_validate_task_destructor);

    /* Stash a pointer to our data in the task */
    slapi_task_set_data(task, mytaskdata);

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, syntax_validate_task_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, SYNTAX_PLUGIN_SUBSYSTEM,
                      "syntax_validate_task_add - Unable to create task thread!\n");
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
syntax_validate_task_destructor(Slapi_Task *task)
{
    if (task) {
        task_data *mydata = (task_data *)slapi_task_get_data(task);
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
        if (mydata) {
            slapi_ch_free_string(&mydata->dn);
            slapi_ch_free_string(&mydata->filter_str);
            slapi_counter_destroy(&mydata->invalid_entries);
            /* Need to cast to avoid a compiler warning */
            slapi_ch_free((void **)&mydata);
        }
    }
}

static void
syntax_validate_task_thread(void *arg)
{
    int rc = 0;
    Slapi_Task *task = (Slapi_Task *)arg;
    task_data *td = NULL;
    Slapi_PBlock *search_pb = NULL;

    if (!task) {
        return; /* no task */
    }
    slapi_task_inc_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM,
                  "syntax_validate_task_thread - refcount incremented.\n");
    /* Fetch our task data from the task */
    td = (task_data *)slapi_task_get_data(task);

    /* Log started message. */
    slapi_task_begin(task, 1);
    slapi_task_log_notice(task, "Syntax validation task starting (arg: %s) ...\n",
                          td->filter_str);
    slapi_log_err(SLAPI_LOG_INFO, SYNTAX_PLUGIN_SUBSYSTEM,
                  "syntax_validate_task_thread - Starting (base: \"%s\", filter: \"%s\") ...\n",
                  td->dn, td->filter_str);

    /* Perform the search and use a callback
     * to validate each matching entry. */
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, td->dn,
                                 LDAP_SCOPE_SUBTREE, td->filter_str, 0, 0,
                                 0, 0, syntax_validate_get_plugin_id(), 0);

    rc = slapi_search_internal_callback_pb(search_pb, td, 0, syntax_validate_task_callback, 0);

    slapi_pblock_destroy(search_pb);

    /* Log finished message. */
    slapi_task_log_notice(task, "Syntax validate task complete.  Found %" PRIu64
                                " invalid entries.\n",
                          slapi_counter_get_value(td->invalid_entries));
    slapi_task_log_status(task, "Syntax validate task complete.  Found %" PRIu64
                                " invalid entries.\n",
                          slapi_counter_get_value(td->invalid_entries));
    slapi_log_err(SLAPI_LOG_INFO, SYNTAX_PLUGIN_SUBSYSTEM, "syntax_validate_task_thread - Complete."
                                                          "  Found %" PRIu64 " invalid entries.\n",
                  slapi_counter_get_value(td->invalid_entries));
    slapi_task_inc_progress(task);

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rc);
    slapi_task_dec_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM,
                  "syntax_validate_task_thread - refcount decremented.\n");
}

static int
syntax_validate_task_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    char *dn = slapi_entry_get_dn(e);
    task_data *td = (task_data *)callback_data;
    Slapi_PBlock *pb = NULL;

    /*
     * If the server is ordered to shutdown, stop the fixup and return an error.
     */
    if (slapi_is_shutting_down()) {
        rc = -1;
        goto bail;
    }
    /* Override the syntax checking config to force syntax checking. */
    if (slapi_entry_syntax_check(NULL, e, 1) != 0) {
        char *error_text = NULL;

        /* We need a pblock to get more details on the syntax violation,
         * but we don't want to allocate a pblock unless we need it for
         * performance reasons.  This means that we will actually call
         * slapi_entry_syntax_check() twice for entries that have a
         * syntax violation. */
        pb = slapi_pblock_new();
        slapi_entry_syntax_check(pb, e, 1);
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &error_text);
        slapi_log_err(SLAPI_LOG_ERR, SYNTAX_PLUGIN_SUBSYSTEM,
                      "syntax_validate_task_callback - Entry \"%s\" violates syntax.\n%s",
                      dn, error_text);
        slapi_pblock_destroy(pb);

        /* Keep a tally of the number of invalid entries found. */
        slapi_counter_increment(td->invalid_entries);
    }
bail:
    return rc;
}

/*
 * Plug-in identity management helper functions
 */
static void
syntax_validate_set_plugin_id(void *plugin_id)
{
    _PluginID = plugin_id;
}

static void *
syntax_validate_get_plugin_id(void)
{
    return _PluginID;
}
