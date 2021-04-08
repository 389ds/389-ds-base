/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* repl5_tot_protocol.c */
/*

 The tot_protocol object implements the DS 5.0 multi-supplier total update
 replication protocol, used to (re)populate a replica.

*/

#include "repl5.h"
#include "repl5_prot_private.h"

/* Private data structures */
typedef struct repl5_tot_private
{
    Repl_Protocol *rp;
    Repl_Agmt *ra;
    PRLock *lock;
    PRUint32 eventbits;
} repl5_tot_private;

typedef struct operation_id_list_item
{
    int ldap_message_id;
    struct operation_id_list_item *next;
} operation_id_list_item;

typedef struct callback_data
{
    Private_Repl_Protocol *prp;
    int rc;
    unsigned long num_entries;
    time_t sleep_on_busy;
    time_t last_busy;
    pthread_mutex_t lock;                    /* Lock to protect access to this structure, the message id list and to force memory barriers */
    PRThread *result_tid;                    /* The async result thread */
    operation_id_list_item *message_id_list; /* List of IDs for outstanding operations */
    int abort;                               /* Flag used to tell the sending thread asyncronously that it should abort (because an error came up in a result) */
    int stop_result_thread;                  /* Flag used to tell the result thread to exit */
    int last_message_id_sent;
    int last_message_id_received;
    int flowcontrol_detection;
} callback_data;

/*
 * Number of window seconds to wait until we programmatically decide
 * that the replica has got out of BUSY state
 */
#define SLEEP_ON_BUSY_WINDOW (10)

/* Helper functions */
static void get_result(int rc, void *cb_data);
static int send_entry(Slapi_Entry *e, void *callback_data);
static void repl5_tot_delete(Private_Repl_Protocol **prp);

#define LOST_CONN_ERR(xx) ((xx == -2) || (xx == LDAP_SERVER_DOWN) || (xx == LDAP_CONNECT_ERROR))
/*
 * Notes on the async version of this code:
 * First, we need to have the supplier and consumer both be async-capable.
 * This is for two reasons : 1) We won't do any testing with mixed releases,
 * so even if we think it might work, we can't be sure. 2) Actually it won't
 * work either because we can't be sure that the consumer will not re-order
 * operations. Also the pre-7.1 consumer had the evil LDAP_BUSY return code,
 * which is incompatible with pipelineing. The 7.1 consumer has interlocks
 * to only process operations in transport-order, and it blocks when the
 * import queue is full rather than returning the LDAP_BUSY return code.
 * Note that it's ok to have a 7.0 supplier talk to a 7.1 consumer because
 * the consumer-side changes are benign to the old supplier code.
 */

/* Code for async result reading.
 * This allows use of full network throughput on high-delay links,
 * because we don't wait for the result PDU to come back before sending the
 * next entry. In order to do this we need to spin up a thread to read the
 * results and handle any errors.
 */

static void
repl5_tot_log_operation_failure(int ldap_error, char *ldap_error_string, const char *agreement_name)
{
    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                  "repl5_tot_log_operation_failure - "
                  "%s: Received error %d (%s): %s for total update operation\n",
                  agreement_name, ldap_error, ldap_err2string(ldap_error),
                  ldap_error_string ? ldap_error_string : "");
}

/* Thread that collects results from async operations sent to the consumer */
static void
repl5_tot_result_threadmain(void *param)
{
    callback_data *cb = (callback_data *)param;
    ConnResult conres = 0;
    Repl_Connection *conn = cb->prp->conn;
    int finished = 0;
    int connection_error = 0;
    char *ldap_error_string = NULL;
    int operation_code = 0;

    while (!finished) {
        int message_id = 0;
        time_t time_now = 0;
        time_t start_time = slapi_current_rel_time_t();
        int backoff_time = 1;

        /* Read the next result */
        /* We call the get result function with a short timeout (non-blocking)
         * this is so we don't block here forever, and can stop this thread when
         * the time comes. However, we do need to implement blocking with timeout
         * semantics here instead.
         */

        while (!finished) {
            conres = conn_read_result_ex(conn, NULL, NULL, NULL, LDAP_RES_ANY, &message_id, 0);
            /* Timeout here means that we didn't block, not a real timeout */
            if (CONN_TIMEOUT == conres) {
                /* We need to a) check that the 'real' timeout hasn't expired and
                 * b) implement a backoff sleep to avoid spinning */
                /* Did the connection's timeout expire ? */
                time_now = slapi_current_rel_time_t();
                if (conn_get_timeout(conn) <= (time_now - start_time)) {
                    /* We timed out */
                    conres = CONN_TIMEOUT;
                    break;
                }
                /* Otherwise we backoff */
                DS_Sleep(PR_MillisecondsToInterval(backoff_time));
                if (backoff_time < 1000) {
                    backoff_time <<= 1;
                }
                /* Should we stop ? */
                pthread_mutex_lock(&(cb->lock));
                if (cb->stop_result_thread) {
                    finished = 1;
                }
                pthread_mutex_unlock(&(cb->lock));
            } else {
                /* Something other than a timeout, so we exit the loop */
                break;
            }
        }

        if (message_id) {
            cb->last_message_id_received = message_id;
        }
        conn_get_error_ex(conn, &operation_code, &connection_error, &ldap_error_string);

        if (connection_error && connection_error != LDAP_TIMEOUT) {
            repl5_tot_log_operation_failure(connection_error, ldap_error_string, agmt_get_long_name(cb->prp->agmt));
        }
        /* Was the result itself an error ? */
        if (0 != conres) {
            /* If so then we need to take steps to abort the update process */
            pthread_mutex_lock(&(cb->lock));
            cb->abort = 1;
            if (conres == CONN_NOT_CONNECTED) {
                cb->rc = LDAP_CONNECT_ERROR;
            }
            pthread_mutex_unlock(&(cb->lock));
        }
        /* Should we stop ? */
        pthread_mutex_lock(&(cb->lock));
        /* if the connection is not connected, then we cannot read any more
           results - we are finished */
        if (cb->stop_result_thread || (conres == CONN_NOT_CONNECTED)) {
            finished = 1;
        }
        pthread_mutex_unlock(&(cb->lock));
    }
}

static int
repl5_tot_create_async_result_thread(callback_data *cb_data)
{
    int retval = 0;
    PRThread *tid = NULL;
    /* Create a thread that reads results from the connection and stores status in the callback_data structure */
    tid = PR_CreateThread(PR_USER_THREAD,
                          repl5_tot_result_threadmain, (void *)cb_data,
                          PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD,
                          SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (NULL == tid) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "repl5_tot_create_async_result_thread - Failed. " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      PR_GetError(), slapd_pr_strerror(PR_GetError()));
        retval = -1;
    } else {
        cb_data->result_tid = tid;
    }
    return retval;
}

static int
repl5_tot_destroy_async_result_thread(callback_data *cb_data)
{
    int retval = 0;
    PRThread *tid = cb_data->result_tid;
    if (tid) {
        pthread_mutex_lock(&(cb_data->lock));
        cb_data->stop_result_thread = 1;
        pthread_mutex_unlock(&(cb_data->lock));
        (void)PR_JoinThread(tid);
    }
    return retval;
}

/* Called when in compatibility mode, to get the next result from the wire
 * The operation thread will not send a second operation until it has read the
 * previous result. */
static int
repl5_tot_get_next_result(callback_data *cb_data)
{
    ConnResult conres = 0;
    int message_id = 0;
    int connection_error = 0;
    char *ldap_error_string = NULL;
    int operation_code = 0;
    /* Wait on the next result */
    conres = conn_read_result(cb_data->prp->conn, &message_id);
    conn_get_error_ex(cb_data->prp->conn, &operation_code, &connection_error, &ldap_error_string);
    if (connection_error) {
        repl5_tot_log_operation_failure(connection_error, ldap_error_string, agmt_get_long_name(cb_data->prp->agmt));
    }
    /* Return it to the caller */
    return conres;
}

static void
repl5_tot_waitfor_async_results(callback_data *cb_data)
{
    int done = 0;
    int loops = 0;
    int last_entry = 0;

    /* Keep pulling results off the LDAP connection until we catch up to the last message id stored in the rd */
    while (!done) {
        /* Lock the structure to force memory barrier */
        pthread_mutex_lock(&(cb_data->lock));
        /* Are we caught up ? */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "repl5_tot_waitfor_async_results - %d %d\n",
                      cb_data->last_message_id_received, cb_data->last_message_id_sent);
        if (cb_data->last_message_id_received >= cb_data->last_message_id_sent) {
            /* If so then we're done */
            done = 1;
        }
        if (cb_data->abort && LOST_CONN_ERR(cb_data->rc)) {
            done = 1; /* no connection == no more results */
        }
        pthread_mutex_unlock(&(cb_data->lock));
        /* If not then sleep a bit */
        DS_Sleep(PR_SecondsToInterval(1));
        loops++;

        if (last_entry < cb_data->last_message_id_received) {
            /* we are making progress - reset the loop counter */
            loops = 0;
        }
        last_entry = cb_data->last_message_id_received;

        /* If we sleep forever then we can conclude that something bad happened, and bail... */
        /* Arbitrary 30 second delay : basically we should only expect to wait as long as it takes to process a few operations, which should be on the order of a second at most */
        if (!done && (loops > 30)) {
            /* Log a warning */
            slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                          "repl5_tot_waitfor_async_results - Timed out waiting for responses: %d %d\n",
                          cb_data->last_message_id_received, cb_data->last_message_id_sent);
            done = 1;
        }
    }
}

/* This routine checks that the entry id of the suffix is
 * stored in the parentid index
 * The entry id of the suffix is stored with the equality key 0 (i.e. '=0')
 * It first checks if the key '=0' exists. If it does not exists or if the first value
 * stored with that key, does not match the suffix entryid (stored in the suffix entry
 * from id2entry.db then it updates the value
 */
static void
check_suffix_entryID(Slapi_Backend *be, Slapi_Entry *suffix)
{
    u_int32_t entryid;
    char *entryid_str;
    struct _back_info_index_key bck_info;

    /* we are using a specific key in parentid to store the suffix entry id: '=0' */
    bck_info.index = SLAPI_ATTR_PARENTID;
    bck_info.key = "0";

    /* First try to retrieve from parentid index the suffix entryID */
    if (slapi_back_get_info(be, BACK_INFO_INDEX_KEY, (void **) &bck_info)) {
        slapi_log_err(SLAPI_LOG_REPL, "check_suffix_entryID", "Total update: fail to retrieve suffix entryID. Let's try to write it\n");
    }

    /* Second retrieve the suffix entryid from the suffix entry itself */
    entryid_str = (char*)slapi_entry_attr_get_ref(suffix, "entryid");
    if (entryid_str == NULL) {
        char *dn;
        dn = slapi_entry_get_ndn(suffix);
        slapi_log_err(SLAPI_LOG_ERR, "check_suffix_entryID", "Unable to retrieve entryid of the suffix entry %s\n", dn ? dn : "<unknown>");
        return;
    }
    entryid = (u_int32_t) atoi(entryid_str);

    if (!bck_info.key_found || bck_info.id != entryid) {
        /* The suffix entryid is not present in parentid index
         *  or differs from what is in id2entry (entry 'suffix')
         * So write it to the parentid so that the range index used
         * during total init will know the entryid of the suffix
         */
        bck_info.id = entryid;
        if (slapi_back_set_info(be, BACK_INFO_INDEX_KEY, (void **) &bck_info)) {
            slapi_log_err(SLAPI_LOG_ERR, "check_suffix_entryID", "Total update: fail to register suffix entryid, continue assuming suffix is the first entry\n");
        }
    }
}

/*
 * Completely refresh a replica. The basic protocol interaction goes
 * like this:
 * - Acquire Replica by sending a StartReplicationRequest extop, with the
 *   total update protocol OID and supplier's ruv.
 * - Send a series of extended operations containing entries.
 * - send an EndReplicationRequest extended operation
 */
#define INIT_RETRY_MAX 5
#define INIT_RETRY_INT 1
static void
repl5_tot_run(Private_Repl_Protocol *prp)
{
    int rc;
    callback_data cb_data = {0};
    Slapi_PBlock *pb = NULL;
    LDAPControl **ctrls;
    char *hostname = NULL;
    int portnum = 0;
    Slapi_DN *area_sdn = NULL;
    CSN *remote_schema_csn = NULL;
    int init_retry = 0;
    ReplicaId rid = 0; /* Used to create the replica keep alive subentry */
    char **instances = NULL;
    Slapi_Backend *be = NULL;
    int is_entryrdn = 0;

    PR_ASSERT(NULL != prp);

    prp->stopped = 0;
    if (prp->terminate) {
        goto done;
    }

    area_sdn = agmt_get_replarea(prp->agmt);
    if (!area_sdn) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - Unable to "
                                                       "get repl area.  Please check agreement.\n");
        goto done;
    }

    conn_set_timeout(prp->conn, agmt_get_timeout(prp->agmt));

    /* acquire remote replica */
    agmt_set_last_init_start(prp->agmt, slapi_current_utc_time());
retry:
    rc = acquire_replica(prp, REPL_NSDS50_TOTAL_PROTOCOL_OID, NULL /* ruv */);
    /* We never retry total protocol, even in case a transient error.
       This is because if somebody already updated the replica we don't
       want to do it again */
    /* But there are scenarios where a total update request could completely
     * be lostif the initial acquire fails: do a few retries for transient
     * errors.
     */
    if (rc != ACQUIRE_SUCCESS) {
        int optype, ldaprc, wait_retry;
        conn_get_error(prp->conn, &optype, &ldaprc);
        if (rc == ACQUIRE_TRANSIENT_ERROR && INIT_RETRY_MAX > init_retry++) {
            wait_retry = init_retry * INIT_RETRY_INT;
            slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "repl5_tot_run - "
                                                               "Unable to acquire replica for total update, error: %d, retrying in %d seconds.\n",
                          ldaprc, wait_retry);
            DS_Sleep(PR_SecondsToInterval(wait_retry));
            goto retry;
        } else {
            agmt_set_last_init_status(prp->agmt, ldaprc,
                                      prp->last_acquire_response_code, 0, NULL);
            goto done;
        }
    } else if (prp->terminate) {
        conn_disconnect(prp->conn);
        goto done;
    }

    hostname = agmt_get_hostname(prp->agmt);
    portnum = agmt_get_port(prp->agmt);

    agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total schema update in progress");
    remote_schema_csn = agmt_get_consumer_schema_csn(prp->agmt);
    rc = conn_push_schema(prp->conn, &remote_schema_csn);

    if (remote_schema_csn != agmt_get_consumer_schema_csn(prp->agmt)) {
        csn_free(&remote_schema_csn);
    }

    if (CONN_SCHEMA_UPDATED != rc && CONN_SCHEMA_NO_UPDATE_NEEDED != rc) {
        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "repl5_tot_run - "
                                                           "Unable to replicate schema to host %s, port %d. "
                                                           "Continuing with total update session.\n",
                      hostname, portnum);
        /* But keep going */
        agmt_set_last_init_status(prp->agmt, 0, rc, 0, "Total schema update failed");
    } else {
        agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total schema update succeeded");
    }

    /* ONREPL - big assumption here is that entries a returned in the id order
       and that the order implies that perent entry is always ahead of the
       child entry in the list. Otherwise, the consumer would not be
       properly updated because bulk import at the moment skips orphand entries. */
    /* XXXggood above assumption may not be valid if orphaned entry moved???? */

    agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total update in progress");

    slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name, "repl5_tot_run - Beginning total update of replica "
                                                    "\"%s\".\n",
                  agmt_get_long_name(prp->agmt));

    /* RMREPL - need to send schema here */

    pb = slapi_pblock_new();

    /*
     * Get the info about the entryrdn vs. entrydn from the backend.
     * If NOT is_entryrdn, its ancestor entries are always found prior to an entry.
     */
    rc = slapi_lookup_instance_name_by_suffix((char *)slapi_sdn_get_dn(area_sdn), NULL, &instances, 1);
    if (rc || !instances) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - Unable to "
                                                       "get the instance name for the suffix \"%s\".\n",
                      slapi_sdn_get_dn(area_sdn));
        goto done;
    }
    be = slapi_be_select_by_instance_name(instances[0]);
    slapi_ch_array_free(instances);

    if (!be) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - Unable to "
                                                       "get the instance for the suffix \"%s\".\n",
                      slapi_sdn_get_dn(area_sdn));
        goto done;
    }
    rc = slapi_back_get_info(be, BACK_INFO_IS_ENTRYRDN, (void **)&is_entryrdn);
    if (is_entryrdn) {
        /*
         * Supporting entries out of order -- parent could have a larger id than its children.
         * Entires are retireved sorted by parentid without the allid threshold.
         */
        /* Get suffix */
        Slapi_Entry *suffix = NULL;
        Slapi_PBlock *suffix_pb = NULL;
        rc = slapi_search_get_entry(&suffix_pb, area_sdn, NULL, &suffix, repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION));
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run -  Unable to "
                                                           "get the suffix entry \"%s\".\n",
                          slapi_sdn_get_dn(area_sdn));
            goto done;
        }

        cb_data.prp = prp;
        cb_data.rc = 0;
        cb_data.num_entries = 1UL;
        cb_data.sleep_on_busy = 0UL;
        cb_data.last_busy = slapi_current_rel_time_t();
        cb_data.flowcontrol_detection = 0;
        pthread_mutex_init(&(cb_data.lock), NULL);

        /* This allows during perform_operation to check the callback data
         * especially to do flow contol on delta send msgid / recv msgid
         */
        conn_set_tot_update_cb(prp->conn, (void *)&cb_data);

        /* Send suffix first. */
        rc = send_entry(suffix, (void *)&cb_data);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - Unable to "
                                                           "send the suffix entry \"%s\" to the consumer.\n",
                          slapi_sdn_get_dn(area_sdn));
            goto done;
        }

        /* we need to provide managedsait control so that referral entries can
           be replicated */
        ctrls = (LDAPControl **)slapi_ch_calloc(3, sizeof(LDAPControl *));
        ctrls[0] = create_managedsait_control();
        ctrls[1] = create_backend_control(area_sdn);

        /* Time to make sure it exists a keep alive subentry for that replica */
        if (prp->replica) {
            rid = replica_get_rid(prp->replica);
        }
        replica_subentry_check(area_sdn, rid);

        /* Send the subtree of the suffix in the order of parentid index plus ldapsubentry and nstombstone. */
        check_suffix_entryID(be, suffix);
        slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(area_sdn),
                                     LDAP_SCOPE_SUBTREE, "(parentid>=1)", NULL, 0, ctrls, NULL,
                                     repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), OP_FLAG_BULK_IMPORT);
        cb_data.num_entries = 0UL;
        slapi_search_get_entry_done(&suffix_pb);
    } else {
        /* Original total update */
        /* we need to provide managedsait control so that referral entries can
           be replicated */
        ctrls = (LDAPControl **)slapi_ch_calloc(3, sizeof(LDAPControl *));
        ctrls[0] = create_managedsait_control();
        ctrls[1] = create_backend_control(area_sdn);

        /* Time to make sure it exists a keep alive subentry for that replica */
        if (prp->replica) {
            rid = replica_get_rid(prp->replica);
        }
        replica_subentry_check(area_sdn, rid);

        slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(area_sdn),
                                     LDAP_SCOPE_SUBTREE, "(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))", NULL, 0, ctrls, NULL,
                                     repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);

        cb_data.prp = prp;
        cb_data.rc = 0;
        cb_data.num_entries = 0UL;
        cb_data.sleep_on_busy = 0UL;
        cb_data.last_busy = slapi_current_rel_time_t();
        cb_data.flowcontrol_detection = 0;
        pthread_mutex_init(&(cb_data.lock), NULL);

        /* This allows during perform_operation to check the callback data
         * especially to do flow contol on delta send msgid / recv msgid
         */
        conn_set_tot_update_cb(prp->conn, (void *)&cb_data);
    }

    /* Before we get started on sending entries to the replica, we need to
     * setup things for async propagation:
     * 1. Create a thread that will read the LDAP results from the connection.
     * 2. Anything else ?
     */
    if (!prp->repl50consumer) {
        rc = repl5_tot_create_async_result_thread(&cb_data);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - %s"
                                                           "repl5_tot_create_async_result_thread failed; error - %d\n",
                          agmt_get_long_name(prp->agmt), rc);
            goto done;
        }
    }

    /* this search get all the entries from the replicated area including tombstones
       and referrals
       Note that cb_data.rc contains values from ConnResult
     */
    slapi_search_internal_callback_pb(pb, &cb_data /* callback data */,
                                      get_result /* result callback */,
                                      send_entry /* entry callback */,
                                      NULL /* referral callback*/);

    /*
     * After completing the sending operation (or optionally failing), we need to clean up
     * the async propagation stuff:
     * 1. Stop the thread that collects LDAP results from the connection.
     * 2. Anything else ?
     */

    if (!prp->repl50consumer) {
        if (cb_data.rc == CONN_OPERATION_SUCCESS) { /* no need to wait if we already failed */
            repl5_tot_waitfor_async_results(&cb_data);
        }
        rc = repl5_tot_destroy_async_result_thread(&cb_data);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - %s - "
                                                           "repl5_tot_destroy_async_result_thread failed; error - %d\n",
                          agmt_get_long_name(prp->agmt), rc);
        }
    }

    /* From here on, things are the same as in the old sync code :
     * the entire total update either succeeded, or it failed.
     * If it failed, then cb_data.rc contains the error code, and
     * suitable messages will have been logged to the error log about the failure.
     */

    agmt_set_last_init_end(prp->agmt, slapi_current_utc_time());
    rc = cb_data.rc;
    agmt_set_update_in_progress(prp->agmt, PR_FALSE);
    agmt_update_done(prp->agmt, 1);
    release_replica(prp);

    if (rc != CONN_OPERATION_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_tot_run - Total update failed for replica \"%s\", "
                                                       "error (%d)\n",
                      agmt_get_long_name(prp->agmt), rc);
        agmt_set_last_init_status(prp->agmt, 0, 0, rc, "Total update aborted");
    } else {
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name, "repl5_tot_run - Finished total update of replica "
                                                        "\"%s\". Sent %lu entries.\n",
                      agmt_get_long_name(prp->agmt), cb_data.num_entries);
        agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total update succeeded");
        agmt_set_last_update_status(prp->agmt, 0, 0, NULL);
    }

done:
    slapi_pblock_destroy(pb);
    slapi_sdn_free(&area_sdn);
    slapi_ch_free_string(&hostname);
    if (cb_data.flowcontrol_detection > 1) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "%s: Total update flow control triggered %d times\n"
                      "You may increase %s and/or decrease %s in the replica agreement configuration\n",
                      agmt_get_long_name(prp->agmt),
                      cb_data.flowcontrol_detection,
                      type_nsds5ReplicaFlowControlPause,
                      type_nsds5ReplicaFlowControlWindow);
    }
    conn_set_tot_update_cb(prp->conn, NULL);
    pthread_mutex_destroy(&(cb_data.lock));
    prp->stopped = 1;
}

static int
repl5_tot_stop(Private_Repl_Protocol *prp)
{
    int return_value;
    PRIntervalTime start, maxwait, now;
    PRUint64 timeout = DEFAULT_PROTOCOL_TIMEOUT;

    if ((timeout = agmt_get_protocol_timeout(prp->agmt)) == 0) {
        timeout = DEFAULT_PROTOCOL_TIMEOUT;
        if (prp->replica) {
            if ((timeout = replica_get_protocol_timeout(prp->replica)) == 0) {
                timeout = DEFAULT_PROTOCOL_TIMEOUT;
            }
        }
    }

    prp->terminate = 1;
    maxwait = PR_SecondsToInterval(timeout);
    start = PR_IntervalNow();
    now = start;
    while (!prp->stopped && ((now - start) < maxwait)) {
        DS_Sleep(PR_SecondsToInterval(1));
        now = PR_IntervalNow();
    }
    if (!prp->stopped) {
        /* Isn't listening. Disconnect from the replica. */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "repl5_tot_stop - "
                                                        "protocol not stopped after waiting for %d seconds "
                                                        "for agreement %s\n",
                      PR_IntervalToSeconds(now - start),
                      agmt_get_long_name(prp->agmt));
        conn_disconnect(prp->conn);
        return_value = -1;
    } else {
        return_value = 0;
    }

    return return_value;
}


static int
repl5_tot_status(Private_Repl_Protocol *prp __attribute__((unused)))
{
    int return_value = 0;
    return return_value;
}


static void
repl5_tot_noop(Private_Repl_Protocol *prp __attribute__((unused)))
{
    /* noop */
    return;
}


Private_Repl_Protocol *
Repl_5_Tot_Protocol_new(Repl_Protocol *rp)
{
    repl5_tot_private *rip = NULL;
    pthread_condattr_t cattr;
    Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_calloc(1, sizeof(Private_Repl_Protocol));

    prp->delete = repl5_tot_delete;
    prp->run = repl5_tot_run;
    prp->stop = repl5_tot_stop;
    prp->status = repl5_tot_status;
    prp->notify_update = repl5_tot_noop;
    prp->notify_agmt_changed = repl5_tot_noop;
    prp->notify_window_opened = repl5_tot_noop;
    prp->notify_window_closed = repl5_tot_noop;
    prp->update_now = repl5_tot_noop;
    if (pthread_mutex_init(&(prp->lock), NULL) != 0) {
        goto loser;
    }
    if (pthread_condattr_init(&cattr) != 0) {
        goto loser;
    }
    if (pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC) != 0) {
        goto loser;
    }
    if (pthread_cond_init(&(prp->cvar), &cattr) != 0) {
        goto loser;
    }
    pthread_condattr_destroy(&cattr);
    prp->stopped = 1;
    prp->terminate = 0;
    prp->eventbits = 0;
    prp->conn = prot_get_connection(rp);
    prp->agmt = prot_get_agreement(rp);
    rip = (void *)slapi_ch_malloc(sizeof(repl5_tot_private));
    rip->rp = rp;
    prp->private = (void *)rip;
    prp->replica_acquired = PR_FALSE;
    prp->repl50consumer = 0;
    prp->repl71consumer = 0;
    prp->repl90consumer = 0;
    prp->replica = prot_get_replica(rp);
    return prp;
loser:
    repl5_tot_delete(&prp);
    return NULL;
}

static void
repl5_tot_delete(Private_Repl_Protocol **prpp)
{
    /* First, stop the protocol if it isn't already stopped */
    if (!(*prpp)->stopped) {
        (*prpp)->stopped = 1;
        (*prpp)->stop(*prpp);
    }
    /* Then, delete all resources used by the protocol */
    if (&((*prpp)->lock)) {
        pthread_mutex_destroy(&((*prpp)->lock));
    }
    if (&((*prpp)->cvar)) {
        pthread_cond_destroy(&(*prpp)->cvar);
    }
    slapi_ch_free((void **)&(*prpp)->private);
    slapi_ch_free((void **)prpp);
}

static void
get_result(int rc, void *cb_data)
{
    PR_ASSERT(cb_data);
    ((callback_data *)cb_data)->rc = rc;
}

/* Call must hold the connection lock */
int
repl5_tot_last_rcv_msgid(Repl_Connection *conn)
{
    struct callback_data *cb_data;

    conn_get_tot_update_cb_nolock(conn, (void **)&cb_data);
    if (cb_data == NULL) {
        return -1;
    } else {
        return cb_data->last_message_id_received;
    }
}

/* Increase the flowcontrol counter
 * Call must hold the connection lock
 */
int
repl5_tot_flowcontrol_detection(Repl_Connection *conn, int increment)
{
    struct callback_data *cb_data;

    conn_get_tot_update_cb_nolock(conn, (void **)&cb_data);
    if (cb_data == NULL) {
        return -1;
    } else {
        cb_data->flowcontrol_detection += increment;
        return cb_data->flowcontrol_detection;
    }
}

static int
send_entry(Slapi_Entry *e, void *cb_data)
{
    int rc;
    Private_Repl_Protocol *prp;
    BerElement *bere;
    struct berval *bv;
    unsigned long *num_entriesp;
    time_t *sleep_on_busyp;
    time_t *last_busyp;
    int message_id = 0;
    int retval = 0;
    char **frac_excluded_attrs = NULL;

    PR_ASSERT(cb_data);

    prp = ((callback_data *)cb_data)->prp;
    num_entriesp = &((callback_data *)cb_data)->num_entries;
    sleep_on_busyp = &((callback_data *)cb_data)->sleep_on_busy;
    last_busyp = &((callback_data *)cb_data)->last_busy;
    PR_ASSERT(prp);

    if (prp->terminate) {
        conn_disconnect(prp->conn);
        ((callback_data *)cb_data)->rc = -1;
        return -1;
    }

    /* see if the result reader thread encountered
       a fatal error */
    pthread_mutex_lock((&((callback_data *)cb_data)->lock));
    rc = ((callback_data *)cb_data)->abort;
    pthread_mutex_unlock((&((callback_data *)cb_data)->lock));
    if (rc) {
        conn_disconnect(prp->conn);
        ((callback_data *)cb_data)->rc = -1;
        return -1;
    }
    /* skip ruv tombstone - need to  do this because it might be
       more up to date then the data we are sending to the client.
       RUV is sent separately via the protocol */
    if (is_ruv_tombstone_entry(e))
        return 0;

    /* ONREPL we would purge copiedFrom and copyingFrom here but I decided against it.
       Instead, it will get removed when this replica stops being 4.0 consumer and
       then propagated to all its consumer */

    if (agmt_is_fractional(prp->agmt)) {
        frac_excluded_attrs = agmt_get_fractional_attrs_total(prp->agmt);
    }

    /* convert the entry to the on the wire format */
    bere = entry2bere(e, frac_excluded_attrs);

    if (frac_excluded_attrs) {
        slapi_ch_array_free(frac_excluded_attrs);
    }

    if (bere == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "%s: send_entry: Encoding Error\n",
                      agmt_get_long_name(prp->agmt));
        ((callback_data *)cb_data)->rc = -1;
        retval = -1;
        goto error;
    }

    rc = ber_flatten(bere, &bv);
    ber_free(bere, 1);
    if (rc != 0) {
        ((callback_data *)cb_data)->rc = -1;
        retval = -1;
        goto error;
    }

    do {
        /* push the entry to the consumer */
        rc = conn_send_extended_operation(prp->conn, REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID,
                                          bv /* payload */, NULL /* update_control */, &message_id);

        if (message_id) {
            ((callback_data *)cb_data)->last_message_id_sent = message_id;
        }

        /* If we are talking to a 5.0 type consumer, we need to wait here and retrieve the
         * response. Reason is that it can return LDAP_BUSY, indicating that its queue has
         * filled up. This completely breaks pipelineing, and so we need to fall back to
         * sync transmission for those consumers, in case they pull the LDAP_BUSY stunt on us :( */

        if (prp->repl50consumer) {
            /* Get the response here */
            rc = repl5_tot_get_next_result((callback_data *)cb_data);
        }

        if (rc == CONN_BUSY) {
            time_t now = slapi_current_rel_time_t();
            if ((now - *last_busyp) < (*sleep_on_busyp + 10)) {
                *sleep_on_busyp += 5;
            } else {
                *sleep_on_busyp = 5;
            }
            *last_busyp = now;

            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_entry - Replica \"%s\" is busy. Waiting %lds while"
                          " it finishes processing its current import queue\n",
                          agmt_get_long_name(prp->agmt), *sleep_on_busyp);
            DS_Sleep(PR_SecondsToInterval(*sleep_on_busyp));
        }
    } while (rc == CONN_BUSY);

    ber_bvfree(bv);
    (*num_entriesp)++;

    /* if the connection has been closed, we need to stop
       sending entries and set a special rc value to let
       the result reading thread know the connection has been
       closed - do not attempt to read any more results */
    if (CONN_NOT_CONNECTED == rc) {
        ((callback_data *)cb_data)->rc = -2;
        retval = -1;
    } else {
        ((callback_data *)cb_data)->rc = rc;
        if (CONN_OPERATION_SUCCESS == rc) {
            retval = 0;
        } else {
            retval = -1;
        }
    }
error:
    return retval;
}
