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

#include "usn.h"

struct usn_cleanup_data {
    char *suffix;
    char *maxusn_to_delete;
};


static int usn_cleanup_add(Slapi_PBlock *pb, Slapi_Entry *e,
            Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);

int
usn_cleanup_start(Slapi_PBlock *pb)
{
    int rc = slapi_task_register_handler("USN tombstone cleanup task",
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
            (struct usn_cleanup_data*)slapi_task_get_data(task);
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL, **ep = NULL;
    Slapi_PBlock *delete_pb = NULL;
    char *filter = "objectclass=nsTombstone";

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_cleanup_thread\n");

    if (NULL == usn_get_identity()) { /* plugin is not initialized */
        slapi_task_log_notice(task, "USN plugin is not initialized\n");
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                      "USN tombstone cleanup: USN plugin is not initialized\n");
        rv = -1;
        filter = NULL; /* so we don't try to free it */
        goto bail;
    }

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
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                    "USN tombstone cleanup: no such suffix %s.\n",
                    cleanup_data->suffix);
        goto bail;
    } else if (LDAP_SUCCESS != rv) {
        slapi_task_log_notice(task,
                    "USN tombstone cleanup: searching tombstone entries "
                    "in %s failed; (%d).\n", cleanup_data->suffix, rv);
        slapi_task_log_status(task,
                    "USN tombstone cleanup: searching tombstone entries in "
                    "%s failed; (%d).\n", cleanup_data->suffix, rv);
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                    "USN tombstone cleanup: searching tombstone entries in "
                    "%s failed; (%d).\n", cleanup_data->suffix, rv);
        goto bail;
    }

    slapi_task_log_notice(task,
            "USN tombstone cleanup task starts (suffix: %s) ...\n", 
            cleanup_data->suffix);
    slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
            "USN tombstone cleanup task starts (suffix: %s) ...\n", 
            cleanup_data->suffix);

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    delete_pb = slapi_pblock_new();
    for (ep = entries; ep && *ep; ep++) {
        int delrv = 0;
        const Slapi_DN *sdn = slapi_entry_get_sdn_const(*ep);

        slapi_delete_internal_set_pb(delete_pb, slapi_sdn_get_dn(sdn),
                                     NULL, NULL, usn_get_identity(), 0);
        slapi_delete_internal_pb(delete_pb);
        slapi_pblock_get(delete_pb, SLAPI_PLUGIN_INTOP_RESULT, &delrv);
        if (LDAP_SUCCESS != delrv) {
            slapi_task_log_notice(task,
                    "USN tombstone cleanup: deleting %s failed; (%d).\n",
                    slapi_sdn_get_dn(sdn), delrv);
            slapi_task_log_status(task,
                    "USN tombstone cleanup: deleting %s failed; (%d).\n",
                    slapi_sdn_get_dn(sdn), delrv);
            slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                    "USN tombstone cleanup: deleting %s failed; (%d).\n",
                    slapi_sdn_get_dn(sdn), delrv);
            rv = delrv;
        }

        slapi_pblock_init(delete_pb);
        slapi_task_inc_progress(task);
    }
    slapi_task_log_notice(task, "USN tombstone cleanup task finished.");
    slapi_task_log_status(task, "USN tombstone cleanup task finished.");
    slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                    "USN tombstone cleanup task finished.\n");
bail:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    slapi_pblock_destroy(delete_pb);
    if (cleanup_data->maxusn_to_delete) {
        slapi_ch_free_string(&filter);
    }
    slapi_ch_free_string(&cleanup_data->maxusn_to_delete);
    slapi_ch_free_string(&cleanup_data->suffix);
    slapi_ch_free((void **)&cleanup_data);

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rv);

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
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
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                        "_usn_cleanup_is_mmr_enabled: failed to normalize "
                        "mappingtree dn for %s\n", suffix);
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
usn_cleanup_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter,
                int *returncode, char *returntext, void *arg)
{
    PRThread *thread = NULL;
    char *cn = NULL;
    char *suffix = NULL;
    char *backend = NULL;
    char *maxusn = NULL;
    struct usn_cleanup_data *cleanup_data = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    Slapi_Task *task = NULL;
    Slapi_Backend *be = NULL;
    const Slapi_DN *be_suffix = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_cleanup_add\n");

    *returncode = LDAP_SUCCESS;
    cn = slapi_entry_attr_get_charptr(e, "cn");
    if (NULL == cn) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    /* get args */
    suffix = slapi_entry_attr_get_charptr(e, "suffix");
    backend = slapi_entry_attr_get_charptr(e, "backend");
    maxusn = slapi_entry_attr_get_charptr(e, "maxusn_to_delete");

    if (NULL == suffix && NULL == backend) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
            "USN tombstone cleanup: Both suffix and backend are missing.\n");
        *returncode = LDAP_PARAM_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    /* suffix is not given, but backend is; get the suffix */
    if (NULL == suffix && NULL != backend) {
        be = slapi_be_select_by_instance_name(backend);
        be_suffix = slapi_be_getsuffix(be, 0);
        if (be_suffix) {
            suffix = slapi_ch_strdup(slapi_sdn_get_ndn(be_suffix));
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                "USN tombstone cleanup: Backend %s is invalid.\n", backend);
            *returncode = LDAP_PARAM_ERROR;
            rv = SLAPI_DSE_CALLBACK_ERROR;
            goto bail;
        }
    }

    /* The suffix is the target of replication, 
     * we don't want to clean up tombstones used by MMR */
    if (_usn_cleanup_is_mmr_enabled(suffix)) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
            "USN tombstone cleanup: Suffix %s is replicated. Unwilling to "
            "perform cleaning up tombstones.\n", suffix);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto bail;
    }

    cleanup_data =
      (struct usn_cleanup_data *)slapi_ch_malloc(sizeof(struct usn_cleanup_data));
    cleanup_data->suffix = slapi_ch_strdup(suffix);
    cleanup_data->maxusn_to_delete = slapi_ch_strdup(maxusn);

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if (task == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
            "USN tombstone cleanup: unable to allocate new task.\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_ch_free((void**)&cleanup_data);
        goto bail;
    }

    /* Stash our argument in the task for use by the task thread */
    slapi_task_set_data(task, cleanup_data);

    /* start the USN tombstone cleanup task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, usn_cleanup_thread,
                          (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                          PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                "USN tombstone cleanup: unable to create task thread.\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_task_finish(task, *returncode);
    } else {
        /* thread successful */
        rv = SLAPI_DSE_CALLBACK_OK;
    }
bail:
    slapi_ch_free_string(&cn);
    slapi_ch_free_string(&suffix);
    slapi_ch_free_string(&backend);
    slapi_ch_free_string(&maxusn);
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_cleanup_add\n");
    return rv;
}

