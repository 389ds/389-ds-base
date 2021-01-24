/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "usn.h"

struct usn_cleanup_data
{
    char *suffix;
    char *maxusn_to_delete;
    char *bind_dn;
};

static int usn_cleanup_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static void usn_cleanup_task_destructor(Slapi_Task *task);


int
usn_cleanup_start(Slapi_PBlock *pb)
{
    int rc = slapi_plugin_task_register_handler("USN tombstone cleanup task",
                                                usn_cleanup_add, pb);
    return rc;
}

int
usn_cleanup_close(void)
{
    int rc = slapi_plugin_task_unregister_handler("USN tombstone cleanup task",
                                                  usn_cleanup_add);
    return rc;
}

/*
 * Task thread
 */
static void
usn_cleanup_thread(void *arg)
{
    Slapi_Task *task = (Slapi_Task *)arg;
    int rv = 0;
    int total_work = 2;
    /* fetch our argument from the task */
    struct usn_cleanup_data *cleanup_data =
        (struct usn_cleanup_data *)slapi_task_get_data(task);
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL, **ep = NULL;
    Slapi_PBlock *delete_pb = NULL;
    char *filter = "objectclass=nsTombstone";

    if (!task) {
        return; /* no task */
    }
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_cleanup_thread\n");
    slapi_task_inc_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, USN_PLUGIN_SUBSYSTEM,
                  "usn_cleanup_thread - refcount incremented.\n");

    if (NULL == usn_get_identity()) { /* plugin is not initialized */
        slapi_task_log_notice(task, "USN plugin is not initialized\n");
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_thread - USN plugin is not initialized\n");
        rv = -1;
        filter = NULL; /* so we don't try to free it */
        goto bail;
    }

    /*  Initialize and set the thread data */
    slapi_td_set_dn(slapi_ch_strdup(cleanup_data->bind_dn));

    /* update task state to show it's running */
    slapi_task_begin(task, total_work);
    if (cleanup_data->maxusn_to_delete) {
        /* (&(objectclass=nsTombstone)(entryusn<=maxusn_to_delete)) */
        int filter_len =
            strlen(filter) + strlen(cleanup_data->maxusn_to_delete) + 32;
        filter = (char *)slapi_ch_malloc(filter_len);
        PR_snprintf(filter, filter_len,
                    "(&(objectclass=nsTombstone)(entryusn<=%s))",
                    cleanup_data->maxusn_to_delete);
    }

    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, cleanup_data->suffix,
                                 LDAP_SCOPE_SUBTREE, filter,
                                 NULL, 0, NULL, NULL, usn_get_identity(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rv);
    if (LDAP_NO_SUCH_OBJECT == rv) {
        slapi_task_log_notice(task,
                              "USN tombstone cleanup: no such suffix %s.\n",
                              cleanup_data->suffix);
        slapi_task_log_status(task,
                              "USN tombstone cleanup: no such suffix %s.\n",
                              cleanup_data->suffix);
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_thread - No such suffix %s.\n",
                      cleanup_data->suffix);
        goto bail;
    } else if (LDAP_SUCCESS != rv) {
        slapi_task_log_notice(task,
                              "USN tombstone cleanup: searching tombstone entries "
                              "in %s failed; (%d).\n",
                              cleanup_data->suffix, rv);
        slapi_task_log_status(task,
                              "USN tombstone cleanup: searching tombstone entries in "
                              "%s failed; (%d).\n",
                              cleanup_data->suffix, rv);
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_thread - Searching tombstone entries in "
                      "%s failed; (%d).\n",
                      cleanup_data->suffix, rv);
        goto bail;
    }

    slapi_task_log_notice(task,
                          "USN tombstone cleanup task starts (suffix: %s) ...\n",
                          cleanup_data->suffix);
    slapi_log_err(SLAPI_LOG_INFO, USN_PLUGIN_SUBSYSTEM,
                  "usn_cleanup_thread - USN tombstone cleanup task starts (suffix: %s) ...\n",
                  cleanup_data->suffix);

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    delete_pb = slapi_pblock_new();
    for (ep = entries; ep && *ep; ep++) {
        int delrv = 0;
        const Slapi_DN *sdn = slapi_entry_get_sdn_const(*ep);
        int opflags = OP_FLAG_TOMBSTONE_ENTRY;

        /* check for shutdown */
        if (slapi_is_shutting_down()) {
            slapi_task_log_notice(task, "USN tombstone cleanup task aborted due to shutdown.");
            slapi_task_log_status(task, "USN tombstone cleanup task aborted due to shutdown.");
            slapi_log_err(SLAPI_LOG_NOTICE, USN_PLUGIN_SUBSYSTEM,
                          "usn_cleanup_thread - Task aborted due to shutdown.\n");
            goto bail;
        }

        slapi_delete_internal_set_pb(delete_pb, slapi_sdn_get_dn(sdn),
                                     NULL, NULL, usn_get_identity(), opflags);
        slapi_delete_internal_pb(delete_pb);
        slapi_pblock_get(delete_pb, SLAPI_PLUGIN_INTOP_RESULT, &delrv);
        if (LDAP_SUCCESS != delrv) {
            slapi_task_log_notice(task,
                                  "USN tombstone cleanup: deleting %s failed; (%d).\n",
                                  slapi_sdn_get_dn(sdn), delrv);
            slapi_task_log_status(task,
                                  "USN tombstone cleanup: deleting %s failed; (%d).\n",
                                  slapi_sdn_get_dn(sdn), delrv);
            slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                          "usn_cleanup_thread - Deleting %s failed; (%d).\n",
                          slapi_sdn_get_dn(sdn), delrv);
            rv = delrv;
        }

        slapi_pblock_init(delete_pb);
        slapi_task_inc_progress(task);
    }
    slapi_task_log_notice(task, "USN tombstone cleanup task finished.");
    slapi_task_log_status(task, "USN tombstone cleanup task finished.");
    slapi_log_err(SLAPI_LOG_INFO, USN_PLUGIN_SUBSYSTEM,
                  "usn_cleanup_thread - USN tombstone cleanup task finished.\n");
bail:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    slapi_pblock_destroy(delete_pb);
    if (cleanup_data->maxusn_to_delete) {
        slapi_ch_free_string(&filter);
    }

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rv);
    slapi_task_dec_refcount(task);
    slapi_log_err(SLAPI_LOG_PLUGIN, USN_PLUGIN_SUBSYSTEM,
                  "usn_cleanup_thread - refcount decremented.\n");
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_cleanup_thread\n");
}

#define MAPPING_TREE_BASE_DN "cn=mapping tree,cn=config"

static int
_usn_cleanup_is_mmr_enabled(const char *suffix)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL;
    char *base_dn = NULL;
    int rc = 0; /* disabled, by default */

    /* This function converts the old style DN to the new one */
    base_dn = slapi_create_dn_string("cn=replica,cn=\"%s\",%s",
                                     suffix, MAPPING_TREE_BASE_DN);
    if (NULL == base_dn) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "_usn_cleanup_is_mmr_enabled - Failed to normalize "
                      "mappingtree dn for %s\n",
                      suffix);
        return 1;
    }
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, base_dn, LDAP_SCOPE_ONELEVEL,
                                 "objectclass=nsDS5ReplicationAgreement",
                                 NULL, 0, NULL, NULL, usn_get_identity(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (LDAP_SUCCESS != rc) { /* agreement is not available */
        goto bail;
    }
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (entries && *entries) {
        rc = 1; /* At least one agreement on the suffix is found */
    }
bail:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    slapi_ch_free_string(&base_dn);

    return rc;
}

static int
usn_cleanup_add(Slapi_PBlock *pb,
                Slapi_Entry *e,
                Slapi_Entry *eAfter __attribute__((unused)),
                int *returncode,
                char *returntext,
                void *arg)
{
    PRThread *thread = NULL;
    char *suffix = NULL;
    char *backend_str = NULL;
    char *maxusn = NULL;
    char *bind_dn;
    struct usn_cleanup_data *cleanup_data = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_Task *task = NULL;
    Slapi_Backend *be = NULL;
    const Slapi_DN *be_suffix = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_cleanup_add\n");

    *returncode = LDAP_SUCCESS;

    /* get the requestor dn */
    slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &bind_dn);

    if (slapi_entry_attr_get_ref(e, "cn") == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    /* get args */
    suffix = slapi_entry_attr_get_charptr(e, "suffix");
    backend_str = (char *)slapi_entry_attr_get_ref(e, "backend");
    maxusn = slapi_entry_attr_get_charptr(e, "maxusn_to_delete");

    if (!suffix && !backend_str) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_add - Both suffix and backend are missing.\n");
        snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                 "USN cleanup task entry requires either a 'suffix' or 'backend' attribute to be provided");
        *returncode = LDAP_PARAM_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    /* suffix is not given, but backend is; get the suffix */
    if (!suffix && backend_str) {
        be = slapi_be_select_by_instance_name(backend_str);
        be_suffix = slapi_be_getsuffix(be, 0);
        if (be_suffix) {
            suffix = slapi_ch_strdup(slapi_sdn_get_ndn(be_suffix));
        } else {
            slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                          "usn_cleanup_add - Backend %s is invalid.\n", backend_str);
            *returncode = LDAP_PARAM_ERROR;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto bail;
        }
    }

    /* The suffix is the target of replication,
     * we don't want to clean up tombstones used by MMR */
    if (_usn_cleanup_is_mmr_enabled(suffix)) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_add - Suffix %s is replicated. Unwilling to "
                      "perform cleaning up tombstones.\n",
                      suffix);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    /* allocate new task now */
    task = slapi_plugin_new_task(slapi_entry_get_ndn(e), arg);
    if (task == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_add - Unable to allocate new task.\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    /* register our destructor for cleaning up our private data */
    slapi_task_set_destructor_fn(task, usn_cleanup_task_destructor);

    /* Stash our argument in the task for use by the task thread */
    cleanup_data =
        (struct usn_cleanup_data *)slapi_ch_malloc(sizeof(struct usn_cleanup_data));
    cleanup_data->suffix = suffix;
    suffix = NULL; /* don't free in this function */
    cleanup_data->maxusn_to_delete = maxusn;
    maxusn = NULL; /* don't free in this function */
    cleanup_data->bind_dn = bind_dn;
    bind_dn = NULL; /* don't free in this function */
    slapi_task_set_data(task, cleanup_data);

    /* start the USN tombstone cleanup task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, usn_cleanup_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_cleanup_add - Unable to create task thread.\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_task_finish(task, *returncode);
    } else {
        /* thread successful */
        rv = SLAPI_DSE_CALLBACK_OK;
    }
bail:
    slapi_ch_free_string(&suffix);
    slapi_ch_free_string(&maxusn);
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_cleanup_add\n");
    return rv;
}

static void
usn_cleanup_task_destructor(Slapi_Task *task)
{
    slapi_log_err(SLAPI_LOG_PLUGIN, USN_PLUGIN_SUBSYSTEM, "usn_cleanup_task_destructor -->\n");
    if (task) {
        struct usn_cleanup_data *mydata = (struct usn_cleanup_data *)slapi_task_get_data(task);
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
        if (mydata) {
            slapi_ch_free_string(&mydata->suffix);
            slapi_ch_free_string(&mydata->maxusn_to_delete);
            slapi_ch_free_string(&mydata->bind_dn);
            /* Need to cast to avoid a compiler warning */
            slapi_ch_free((void **)&mydata);
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, USN_PLUGIN_SUBSYSTEM, "usn_cleanup_task_destructor <--\n");
}
