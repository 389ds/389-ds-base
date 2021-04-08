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


/* windows_tot_protocol.c */
/*

 The tot_protocol object implements the DS 5.0 multi-supplier total update
 replication protocol, used to (re)populate a replica.

*/

#include "repl5.h"
#include "windowsrepl.h"
#include "windows_prot_private.h"
#include "slap.h"

/* Private data structures */
typedef struct windows_tot_private
{
    Repl_Protocol *rp;
    Repl_Agmt *ra;
    PRLock *lock;
    PRUint32 eventbits;
} windows_tot_private;

typedef struct callback_data
{
    Private_Repl_Protocol *prp;
    int rc;
    unsigned long num_entries;
    time_t sleep_on_busy; /* not used ??? */
    time_t last_busy;     /* not used ??? */
} callback_data;

/*
 * Number of window seconds to wait until we programmatically decide
 * that the replica has got out of BUSY state
 */
#define SLEEP_ON_BUSY_WINDOW (10)

/* Helper functions */
static void get_result(int rc, void *cb_data);
static int send_entry(Slapi_Entry *e, void *callback_data);
static void windows_tot_delete(Private_Repl_Protocol **prp);

static void
_windows_tot_send_entry(const Repl_Agmt *ra, callback_data *cbp, const Slapi_DN *local_sdn)
{
    Slapi_PBlock *pb = NULL;
    char *dn = NULL;
    int scope = LDAP_SCOPE_SUBTREE;
    char *filter = NULL;
    const char *userfilter = NULL;
    char **attrs = NULL;
    LDAPControl **server_controls = NULL;

    if ((NULL == ra) || (NULL == cbp) || (NULL == local_sdn)) {
        return;
    }
    dn = slapi_ch_strdup(slapi_sdn_get_dn(local_sdn));
    userfilter = windows_private_get_directory_userfilter(ra);
    if (userfilter) {
        if ('(' == *userfilter) {
            filter = slapi_ch_smprintf("(&(|(objectclass=ntuser)(objectclass=ntgroup))%s)",
                                       userfilter);
        } else {
            filter = slapi_ch_smprintf("(&(|(objectclass=ntuser)(objectclass=ntgroup))(%s))",
                                       userfilter);
        }
    } else {
        filter = slapi_ch_strdup("(|(objectclass=ntuser)(objectclass=ntgroup))");
    }

    winsync_plugin_call_pre_ds_search_all_cb(ra, NULL, &dn, &scope, &filter,
                                             &attrs, &server_controls);

    pb = slapi_pblock_new();
    /* Perform a subtree search for any ntuser or ntgroup entries underneath the
    * suffix defined in the sync agreement. */
    slapi_search_internal_set_pb(pb, dn, scope, filter, attrs, 0, server_controls, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);

    slapi_search_internal_callback_pb(pb, cbp /* callback data */,
                                      get_result /* result callback */,
                                      send_entry /* entry callback */,
                                      NULL /* referral callback */);

    slapi_ch_free_string(&dn);
    slapi_ch_free_string(&filter);
    slapi_ch_array_free(attrs);
    attrs = NULL;
    ldap_controls_free(server_controls);
    server_controls = NULL;
    slapi_pblock_destroy(pb);
}

/*
 * Completely refresh a replica. The basic protocol interaction goes
 * like this:
 * - Acquire Replica by sending a StartReplicationRequest extop, with the
 *   total update protocol OID and supplier's ruv.
 * - Send a series of extended operations containing entries.
 * - send an EndReplicationRequest extended operation
 */
static void
windows_tot_run(Private_Repl_Protocol *prp)
{
    int rc;
    callback_data cb_data;
    RUV *ruv = NULL;
    RUV *starting_ruv = NULL;
    Object *local_ruv_obj = NULL;
    int one_way;

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> windows_tot_run\n");

    PR_ASSERT(NULL != prp);

    prp->stopped = 0;
    if (prp->terminate) {
        prp->stopped = 1;
        goto done;
    }

    one_way = windows_private_get_one_way(prp->agmt);

    windows_conn_set_timeout(prp->conn, agmt_get_timeout(prp->agmt));

    /* acquire remote replica */
    agmt_set_last_init_start(prp->agmt, slapi_current_utc_time());

    rc = windows_acquire_replica(prp, &ruv, 0 /* don't check RUV for total protocol */);
    /* We never retry total protocol, even in case a transient error.
     * This is because if somebody already updated the replica we don't
     * want to do it again */
    if (rc != ACQUIRE_SUCCESS) {
        int optype, ldaprc;
        windows_conn_get_error(prp->conn, &optype, &ldaprc);
        agmt_set_last_init_status(prp->agmt, ldaprc,
                                  prp->last_acquire_response_code, 0, NULL);
        goto done;
    } else if (prp->terminate) {
        windows_conn_disconnect(prp->conn);
        prp->stopped = 1;
        goto done;
    }

    agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total schema update in progress");

    agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total update in progress");

    agmt_set_update_in_progress(prp->agmt, PR_TRUE);

    slapi_log_err(SLAPI_LOG_ERR, windows_repl_plugin_name, "windows_tot_run - Beginning total update of replica "
                                                           "\"%s\".\n",
                  agmt_get_long_name(prp->agmt));

    windows_private_null_dirsync_cookie(prp->agmt);

    /* call begin total update callback */
    winsync_plugin_call_begin_update_cb(prp->agmt,
                                        windows_private_get_directory_treetop(prp->agmt),
                                        windows_private_get_windows_treetop(prp->agmt),
                                        1 /* is_total == TRUE */);

    if ((one_way == ONE_WAY_SYNC_DISABLED) || (one_way == ONE_WAY_SYNC_FROM_AD)) {
        /* get everything */
        windows_dirsync_inc_run(prp);
    }

    windows_private_save_dirsync_cookie(prp->agmt);

    /* If we got a change from dirsync, we should have a good RUV
     * that has a min & max value.  If no change was generated,
     * the RUV will have NULL min and max csns.  We deal with
     * updating these values when we process the first change in
     * the incremental sync protocol ( send_updates() ).  We will
     * use this value for setting the consumer RUV if the total
     * update succeeds. */
    local_ruv_obj = replica_get_ruv(prp->replica);
    starting_ruv = ruv_dup((RUV *)object_get_data(local_ruv_obj));
    object_release(local_ruv_obj);

    /* Set up the callback data. */
    cb_data.prp = prp;
    cb_data.rc = 0;
    cb_data.num_entries = 0UL;
    cb_data.sleep_on_busy = 0UL;
    cb_data.last_busy = slapi_current_utc_time();

    /* Don't send anything if one-way (ONE_WAY_SYNC_FROM_AD) is set. */
    if ((one_way == ONE_WAY_SYNC_DISABLED) || (one_way == ONE_WAY_SYNC_TO_AD)) {
        /* send everything */
        const subtreePair *subtree_pairs = NULL;
        const subtreePair *sp = NULL;

        subtree_pairs = windows_private_get_subtreepairs(prp->agmt);
        if (subtree_pairs) {
            for (sp = subtree_pairs; sp && sp->DSsubtree; sp++) {
                _windows_tot_send_entry(prp->agmt, &cb_data, sp->DSsubtree);
            }
        } else {
            _windows_tot_send_entry(prp->agmt, &cb_data, windows_private_get_directory_subtree(prp->agmt));
        }
    }
    rc = cb_data.rc;
    windows_release_replica(prp);

    if (rc != CONN_OPERATION_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, windows_repl_plugin_name, "windows_tot_run - %s - "
                                                                "failed to obtain data to send to the consumer; LDAP error - %d\n",
                      agmt_get_long_name(prp->agmt), rc);
        agmt_set_last_init_status(prp->agmt, 0, 0, rc, "Total update aborted");
    } else {
        slapi_log_err(SLAPI_LOG_ERR, windows_repl_plugin_name, "windows_tot_run - Finished total update of replica "
                                                               "\"%s\". Sent %lu entries.\n",
                      agmt_get_long_name(prp->agmt), cb_data.num_entries);
        agmt_set_last_init_status(prp->agmt, 0, 0, 0, "Total update succeeded");
        /* Now update our consumer RUV for this agreement.
         * This ensures that future incrememental updates work.
         */
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, windows_repl_plugin_name, "windows_tot_run - "
                                                                    "Total update setting consumer RUV:\n");
            ruv_dump(starting_ruv, "consumer", NULL);
        }
        agmt_set_consumer_ruv(prp->agmt, starting_ruv);
    }

    /* Do another dirsync to ensure we get GUIDs for newly added entries. */
    if ((one_way == ONE_WAY_SYNC_DISABLED) || (one_way == ONE_WAY_SYNC_FROM_AD)) {
        windows_dirsync_inc_run(prp);
    }

    /* Save the dirsync cookie. */
    windows_private_save_dirsync_cookie(prp->agmt);

    agmt_set_last_init_end(prp->agmt, slapi_current_utc_time());
    agmt_set_update_in_progress(prp->agmt, PR_FALSE);
    agmt_update_done(prp->agmt, 1);

    /* call end total update callback */
    winsync_plugin_call_end_update_cb(prp->agmt,
                                      windows_private_get_directory_treetop(prp->agmt),
                                      windows_private_get_windows_treetop(prp->agmt),
                                      1 /* is_total == TRUE */);

done:
    if (starting_ruv) {
        ruv_destroy(&starting_ruv);
    }

    prp->stopped = 1;
    ruv_destroy(&ruv);
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= windows_tot_run\n");
}

static int
windows_tot_stop(Private_Repl_Protocol *prp)
{
    int return_value;
    int seconds = 600;
    PRIntervalTime start, maxwait, now;

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> windows_tot_stop\n");

    prp->terminate = 1;
    maxwait = PR_SecondsToInterval(seconds);
    start = PR_IntervalNow();
    now = start;
    while (!prp->stopped && ((now - start) < maxwait)) {
        DS_Sleep(PR_SecondsToInterval(1));
        now = PR_IntervalNow();
    }
    if (!prp->stopped) {
        /* Isn't listening. Disconnect from the replica. */
        slapi_log_err(SLAPI_LOG_REPL, windows_repl_plugin_name, "windows_tot_stop - "
                                                                "Protocol not stopped after waiting for %d seconds "
                                                                "for agreement %s\n",
                      PR_IntervalToSeconds(now - start),
                      agmt_get_long_name(prp->agmt));
        windows_conn_disconnect(prp->conn);
        return_value = -1;
    } else {
        return_value = 0;
    }

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= windows_tot_stop\n");

    return return_value;
}


static int
windows_tot_status(Private_Repl_Protocol *prp __attribute__((unused)))
{
    int return_value = 0;
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> windows_tot_status\n");
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= windows_tot_status\n");
    return return_value;
}


static void
windows_tot_noop(Private_Repl_Protocol *prp __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> windows_tot_noop\n");
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= windows_tot_noop\n");
    /* noop */
}


Private_Repl_Protocol *
Windows_Tot_Protocol_new(Repl_Protocol *rp)
{
    windows_tot_private *rip = NULL;
    Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_calloc(1, sizeof(Private_Repl_Protocol));
    pthread_condattr_t cattr;

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> Windows_Tot_Protocol_new\n");

    prp->delete = windows_tot_delete;
    prp->run = windows_tot_run;
    prp->stop = windows_tot_stop;
    prp->status = windows_tot_status;
    prp->notify_update = windows_tot_noop;
    prp->notify_agmt_changed = windows_tot_noop;
    prp->notify_window_opened = windows_tot_noop;
    prp->notify_window_closed = windows_tot_noop;
    prp->replica = prot_get_replica(rp);
    prp->update_now = windows_tot_noop;
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
    rip = (void *)slapi_ch_malloc(sizeof(windows_tot_private));
    rip->rp = rp;
    prp->private = (void *)rip;
    prp->replica_acquired = PR_FALSE;
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= Windows_Tot_Protocol_new\n");
    return prp;
loser:
    windows_tot_delete(&prp);
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= Windows_Tot_Protocol_new - Failed\n");
    return NULL;
}

static void
windows_tot_delete(Private_Repl_Protocol **prpp)
{
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> windows_tot_delete\n");

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

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= windows_tot_delete\n");
}

static void
get_result(int rc, void *cb_data)
{
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> get_result\n");
    PR_ASSERT(cb_data);
    ((callback_data *)cb_data)->rc = rc;
    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= get_result\n");
}

static int
send_entry(Slapi_Entry *e, void *cb_data)
{
    int rc;
    Private_Repl_Protocol *prp;
    unsigned long *num_entriesp;

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "=> send_entry\n");

    PR_ASSERT(cb_data);

    prp = ((callback_data *)cb_data)->prp;
    num_entriesp = &((callback_data *)cb_data)->num_entries;
    PR_ASSERT(prp);

    if (prp->terminate) {
        windows_conn_disconnect(prp->conn);
        prp->stopped = 1;
        ((callback_data *)cb_data)->rc = -1;
        slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= send_entry\n");
        return -1;
    }

    /* skip ruv tombstone - not relvant to Active Directory */
    if (is_ruv_tombstone_entry(e)) {
        slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= send_entry\n");
        return 0;
    }

    /* push the entry to the consumer */
    rc = windows_process_total_entry(prp, e);

    (*num_entriesp)++;

    slapi_log_err(SLAPI_LOG_TRACE, windows_repl_plugin_name, "<= send_entry\n");

    if (CONN_OPERATION_SUCCESS == rc) {
        return 0;
    } else {
        ((callback_data *)cb_data)->rc = rc;
        return -1;
    }
}
