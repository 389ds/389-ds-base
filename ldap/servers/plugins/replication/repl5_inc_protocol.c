/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* repl5_inc_protocol.c */
/*

 The Prot_Incremental object implements the DS 5.0 multi-master incremental
 replication protocol.


Stuff to do:

- Need to figure out how asynchronous events end up in here. They are:
  - entry updated in replicated area.
  - backoff timeout
  - enter/leave.

Perhaps these events should be properties of the main protocol.
*/
#include <plstr.h>


#include "repl5.h"
#include "repl5_ruv.h"
#include "repl5_prot_private.h"
#include "cl5_api.h"
#include "slapi-plugin.h"

extern int slapi_log_urp;

/*** from proto-slap.h ***/
void ava_done(struct ava *ava);

typedef struct repl5_inc_private
{
    char *ruv; /* RUV on remote replica (use diff type for this? - ggood */
    Backoff_Timer *backoff;
    Repl_Protocol *rp;
    PRLock *lock;
    PRUint32 eventbits;
} repl5_inc_private;

/* Structures used to communicate with the result reading thread */

#ifndef UIDSTR_SIZE
#define UIDSTR_SIZE 35 /* size of the string representation of the id */
#endif

typedef struct repl5_inc_operation
{
    int ldap_message_id;
    unsigned long operation_type;
    char csn_str[CSN_STRSIZE];
    char uniqueid[UIDSTR_SIZE + 1];
    ReplicaId replica_id;
    struct repl5_inc_operation *next;
} repl5_inc_operation;

typedef struct result_data
{
    Private_Repl_Protocol *prp;
    int rc;
    PRLock *lock;                             /* Lock to protect access to this structure, the message id list and to force memory barriers */
    PRThread *result_tid;                     /* The async result thread */
    repl5_inc_operation *operation_list_head; /* List of IDs for outstanding operations */
    repl5_inc_operation *operation_list_tail; /* List of IDs for outstanding operations */
    int abort;                                /* Flag used to tell the sending thread asyncronously that it should abort (because an error came up in a result) */
    PRUint32 num_changes_sent;
    int stop_result_thread; /* Flag used to tell the result thread to exit */
    int last_message_id_sent;
    int last_message_id_received;
    int flowcontrol_detection;
    int result; /* The UPDATE_TRANSIENT_ERROR etc */
    int WaitForAsyncResults;
    time_t abort_time;
} result_data;

/* Various states the incremental protocol can pass through */
#define STATE_START 0 /* ONREPL - should we rename this - we don't use it just to start up? */
#define STATE_WAIT_WINDOW_OPEN 1
#define STATE_WAIT_CHANGES 2
#define STATE_READY_TO_ACQUIRE 3
#define STATE_BACKOFF_START 4 /* ONREPL - can we combine BACKOFF_START and BACKOFF states? */
#define STATE_BACKOFF 5
#define STATE_SENDING_UPDATES 6
#define STATE_STOP_FATAL_ERROR 7
#define STATE_STOP_FATAL_ERROR_PART2 8
#define STATE_STOP_NORMAL_TERMINATION 9

/* Events (synchronous and asynchronous; these are bits) */
#define EVENT_WINDOW_OPENED 1
#define EVENT_WINDOW_CLOSED 2
#define EVENT_TRIGGERING_CRITERIA_MET 4 /* ONREPL - should we rename this to EVENT_CHANGE_AVAILABLE */
#define EVENT_BACKOFF_EXPIRED 8
#define EVENT_REPLICATE_NOW 16
#define EVENT_PROTOCOL_SHUTDOWN 32
#define EVENT_AGMT_CHANGED 64

#define UPDATE_NO_MORE_UPDATES 201
#define UPDATE_TRANSIENT_ERROR 202
#define UPDATE_FATAL_ERROR 203
#define UPDATE_SCHEDULE_WINDOW_CLOSED 204
#define UPDATE_CONNECTION_LOST 205
#define UPDATE_TIMEOUT 206
#define UPDATE_YIELD 207

/* Return codes from examine_update_vector */
#define EXAMINE_RUV_PRISTINE_REPLICA 401
#define EXAMINE_RUV_GENERATION_MISMATCH 402
#define EXAMINE_RUV_REPLICA_TOO_OLD 403
#define EXAMINE_RUV_OK 404
#define EXAMINE_RUV_PARAM_ERROR 405

#define MAX_CHANGES_PER_SESSION 10000

/*
 * Maximum time to wait between replication sessions. If we
 * don't see any updates for a period equal to this interval,
 * we go ahead and start a replication session, just to be safe
 */
#define MAX_WAIT_BETWEEN_SESSIONS PR_SecondsToInterval(60 * 5) /* 5 minutes */

/*
 * tests if the protocol has been shutdown and we need to quit
 * event_occurred resets the bits in the bit flag, so whoever tests for shutdown
 * resets the flags, so the next one who tests for shutdown won't get it, so we
 * also look at the terminate flag
 */
#define PROTOCOL_IS_SHUTDOWN(prp) (event_occurred(prp, EVENT_PROTOCOL_SHUTDOWN) || prp->terminate)

/* mods should be LDAPMod **mods */
#define MODS_ARE_EMPTY(mods) ((mods == NULL) || (mods[0] == NULL))

/* Forward declarations */
static PRUint32 event_occurred(Private_Repl_Protocol *prp, PRUint32 event);
static void reset_events(Private_Repl_Protocol *prp);
static void protocol_sleep(Private_Repl_Protocol *prp, PRIntervalTime duration);
static int send_updates(Private_Repl_Protocol *prp, RUV *ruv, PRUint32 *num_changes_sent);
static void repl5_inc_backoff_expired(time_t timer_fire_time, void *arg);
static int examine_update_vector(Private_Repl_Protocol *prp, RUV *ruv);
static const char *state2name(int state);
static const char *event2name(int event);
static const char *op2string(int op);
static int repl5_inc_update_from_op_result(Private_Repl_Protocol *prp, ConnResult replay_crc, int connection_error, char *csn_str, char *uniqueid, ReplicaId replica_id, int *finished, PRUint32 *num_changes_sent);

/* Push a newly sent operation onto the tail of the list */
static void
repl5_int_push_operation(result_data *rd, repl5_inc_operation *it)
{
    repl5_inc_operation *tail = NULL;
    PR_Lock(rd->lock);
    tail = rd->operation_list_tail;
    if (tail) {
        tail->next = it;
    }
    if (NULL == rd->operation_list_head) {
        rd->operation_list_head = it;
    }
    rd->operation_list_tail = it;
    PR_Unlock(rd->lock);
}

/* Pop the next operation in line to respond from the list */
/* The caller is expected to free the operation item */
static repl5_inc_operation *
repl5_inc_pop_operation(result_data *rd)
{
    repl5_inc_operation *head = NULL;
    repl5_inc_operation *ret = NULL;
    PR_Lock(rd->lock);
    head = rd->operation_list_head;
    if (head) {
        ret = head;
        rd->operation_list_head = head->next;
        if (rd->operation_list_tail == head) {
            rd->operation_list_tail = NULL;
        }
    }
    PR_Unlock(rd->lock);
    return ret;
}

static void
repl5_inc_op_free(repl5_inc_operation *op)
{
    slapi_ch_free((void **)&op);
}

static repl5_inc_operation *
repl5_inc_operation_new(void)
{
    repl5_inc_operation *ret = NULL;
    ret = (repl5_inc_operation *)slapi_ch_calloc(1, sizeof(repl5_inc_operation));
    return ret;
}

/* Called when in compatibility mode, to get the next result from the wire
 * The operation thread will not send a second operation until it has read the
 * previous result. */
static int
repl5_inc_get_next_result(result_data *rd)
{
    ConnResult conres = 0;
    int message_id = 0;
    /* Wait on the next result */
    conres = conn_read_result(rd->prp->conn, &message_id);
    /* Return it to the caller */
    return conres;
}

#if NEEDED_FOR_DEBUGGING
static void
repl5_inc_log_operation_failure(int operation_code, int ldap_error, char *ldap_error_string, const char *agreement_name)
{
    char *op_string = slapi_op_type_to_string(operation_code);

    slapi_log_err(SLAPI_LOG_DEBUG, repl_plugin_name,
                  "repl5_inc_log_operation_failure - %s: Received error %d: %s for %s operation\n",
                  agreement_name,
                  ldap_error, ldap_error_string ? ldap_error_string : "NULL",
                  op_string ? op_string : "NULL");
}
#endif

/* Thread that collects results from async operations sent to the consumer */
static void
repl5_inc_result_threadmain(void *param)
{
    result_data *rd = (result_data *)param;
    ConnResult conres = 0;
    Repl_Connection *conn = rd->prp->conn;
    int finished = 0;
    int message_id = 0;
    int yield_session = 0;

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "repl5_inc_result_threadmain - Starting\n");
    while (!finished) {
        LDAPControl **returned_controls = NULL;
        repl5_inc_operation *op = NULL;
        ReplicaId replica_id = 0;
        char *csn_str = NULL;
        char *uniqueid = NULL;
        char *ldap_error_string = NULL;
        time_t time_now = 0;
        time_t start_time = slapi_current_utc_time();
        int connection_error = 0;
        int operation_code = 0;
        int backoff_time = 1;

        /* Read the next result */
        /* We call the get result function with a short timeout (non-blocking)
         * this is so we don't block here forever, and can stop this thread when
         * the time comes. However, we do need to implement blocking with timeout
         * semantics here instead.
         */

        while (!finished) {
            conres = conn_read_result_ex(conn, NULL, NULL, &returned_controls, LDAP_RES_ANY, &message_id, 0);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "repl5_inc_result_threadmain - "
                                                            "Read result for message_id %d\n",
                          message_id);
            /* Timeout here means that we didn't block, not a real timeout */
            if (CONN_TIMEOUT == conres) {
                /* We need to a) check that the 'real' timeout hasn't expired and
                 * b) implement a backoff sleep to avoid spinning */
                /* Did the connection's timeout expire ? */
                time_now = slapi_current_utc_time();
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
                PR_Lock(rd->lock);
                if (rd->stop_result_thread) {
                    finished = 1;
                }
                PR_Unlock(rd->lock);
            } else {
                /*
                 * Something other than a timeout, so we exit the loop.
                 * First check if we were told to abort the session
                 */;
                Replica *r = rd->prp->replica;
                if (replica_get_release_timeout(r) &&
                    slapi_control_present(returned_controls,
                                          REPL_ABORT_SESSION_OID,
                                          NULL, NULL)) {
                    yield_session = 1;
                }
                break;
            }
        }
        if (conres != CONN_TIMEOUT) {
            int return_value;
            int should_finish = 0;
            if (message_id) {
                rd->last_message_id_received = message_id;
            }
            /* Handle any error etc */

            /* Get the stored operation details from the queue, unless we timed out... */
            op = repl5_inc_pop_operation(rd);
            if (op) {
                csn_str = op->csn_str;
                replica_id = op->replica_id;
                uniqueid = op->uniqueid;
            }

            conn_get_error_ex(conn, &operation_code, &connection_error, &ldap_error_string);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "repl5_inc_result_threadmain - Result %d, %d, %d, %d, %s\n",
                          operation_code, connection_error, conres, message_id, ldap_error_string);
            return_value = repl5_inc_update_from_op_result(rd->prp, conres, connection_error,
                                                           csn_str, uniqueid, replica_id, &should_finish,
                                                           &(rd->num_changes_sent));
            if (return_value || should_finish) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "repl5_inc_result_threadmain - Got op result %d should finish %d\n",
                              return_value, should_finish);
                /* If so then we need to take steps to abort the update process */
                PR_Lock(rd->lock);
                rd->result = return_value;
                rd->abort = ABORT_SESSION;
                PR_Unlock(rd->lock);
                /*
                 * We also need to log the error, including details stored from
                 * when the operation was sent.  We cannot finish yet - we still
                 * need to wait for the pending results, then the main repl code
                 * will shut down this thread.  We can finish if we have
                 * disconnected - in that case, there will be nothing to read
                 */
                if (return_value == UPDATE_CONNECTION_LOST) {
                    finished = 1;
                }
            } else {
                /* old semantics had result set outside of lock */
                rd->result = return_value;
            }
        }

        /* Should we stop ? */
        PR_Lock(rd->lock);
        if (!finished && yield_session && rd->abort != SESSION_ABORTED && rd->abort_time == 0) {
            rd->abort_time = slapi_current_utc_time();
            rd->abort = SESSION_ABORTED; /* only set the abort time once */
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "repl5_inc_result_threadmain - "
                                                            "Abort control detected, setting abort time...(%s)\n",
                          agmt_get_long_name(rd->prp->agmt));
        }
        if (rd->stop_result_thread) {
            finished = 1;
        }
        PR_Unlock(rd->lock);
        if (op) {
            repl5_inc_op_free(op);
        }
    }
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "repl5_inc_result_threadmain exiting\n");
}

static result_data *
repl5_inc_rd_new(Private_Repl_Protocol *prp)
{
    result_data *res = NULL;
    res = (result_data *)slapi_ch_calloc(1, sizeof(result_data));
    if (res) {
        res->prp = prp;
        res->lock = PR_NewLock();
        if (NULL == res->lock) {
            slapi_ch_free((void **)&res);
            res = NULL;
        }
    }
    return res;
}

static void
repl5_inc_rd_list_destroy(repl5_inc_operation *op)
{
    while (op) {
        repl5_inc_operation *next = op->next;
        repl5_inc_op_free(op);
        op = next;
    }
}

static void
repl5_inc_rd_destroy(result_data **pres)
{
    result_data *res = *pres;
    if (res->lock) {
        PR_DestroyLock(res->lock);
    }
    /* Delete the linked list if we have one */
    /* Begin at the head */
    repl5_inc_rd_list_destroy(res->operation_list_head);
    slapi_ch_free((void **)pres);
}

static int
repl5_inc_create_async_result_thread(result_data *rd)
{
    int retval = 0;
    PRThread *tid = NULL;
    /* Create a thread that reads results from the connection and stores status in the callback_data structure */
    tid = PR_CreateThread(PR_USER_THREAD,
                          repl5_inc_result_threadmain, (void *)rd,
                          PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD,
                          SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (NULL == tid) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "repl5_inc_create_async_result_thread - Failed. " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      PR_GetError(), slapd_pr_strerror(PR_GetError()));
        retval = -1;
    } else {
        rd->result_tid = tid;
    }
    return retval;
}

static int
repl5_inc_destroy_async_result_thread(result_data *rd)
{
    int retval = 0;
    PRThread *tid = rd->result_tid;
    if (tid) {
        PR_Lock(rd->lock);
        rd->stop_result_thread = 1;
        PR_Unlock(rd->lock);
        (void)PR_JoinThread(tid);
    }
    return retval;
}

/* The interest of this routine is to give time to the consumer
 * to apply the sent updates and return the acks.
 * So the caller should not hold the replication connection lock
 * to let the RA.reader receives the acks.
 */
static void
repl5_inc_flow_control_results(Repl_Agmt *agmt, result_data *rd)
{
    PR_Lock(rd->lock);
    if ((rd->last_message_id_received <= rd->last_message_id_sent) &&
        ((rd->last_message_id_sent - rd->last_message_id_received) >= agmt_get_flowcontrolwindow(agmt))) {
        rd->flowcontrol_detection++;
        PR_Unlock(rd->lock);
        DS_Sleep(PR_MillisecondsToInterval(agmt_get_flowcontrolpause(agmt)));
    } else {
        PR_Unlock(rd->lock);
    }
}

static int
repl5_inc_waitfor_async_results(result_data *rd)
{
    int done = 0;
    int loops = 0;
    int rc = UPDATE_NO_MORE_UPDATES;

    /* Keep pulling results off the LDAP connection until we catch up to the last message id stored in the rd */
    while (!done && !slapi_is_shutting_down()) {
        /* Lock the structure to force memory barrier */
        PR_Lock(rd->lock);
        /* Are we caught up ? */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "repl5_inc_waitfor_async_results - %d %d\n",
                      rd->last_message_id_received, rd->last_message_id_sent);
        if (rd->last_message_id_received >= rd->last_message_id_sent) {
            /* If so then we're done */
            done = 1;
        } else if (rd->abort && (rd->result == UPDATE_CONNECTION_LOST)) {
            done = 1; /* no connection == no more results */
        }
        /*
         * Return the last operation result
         */
        rc = rd->result;
        PR_Unlock(rd->lock);
        if (!done) {
            /* If not then sleep a bit */
            DS_Sleep(PR_MillisecondsToInterval(rd->WaitForAsyncResults));
        }
        loops++;
        /* If we sleep forever then we can conclude that something bad happened, and bail... */
        /* Arbitrary 30 second delay : basically we should only expect to wait as long as it takes to process a few operations, which should be on the order of a second at most */
        if (!done && (loops > 300)) {
            /* Log a warning */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "repl5_inc_waitfor_async_results  - Timed out waiting for responses: %d %d\n",
                          rd->last_message_id_received, rd->last_message_id_sent);
            done = 1;
        }
    }
    return rc;
}

/*
 * It's specifically ok to delete a protocol instance that
 * is currently running. The instance will be shut down, and
 * then resources will be freed. Since a graceful shutdown is
 * attempted, this function may take some time to complete.
 */
static void
repl5_inc_delete(Private_Repl_Protocol **prpp)
{
    repl5_inc_private *prp_priv = (repl5_inc_private *)(*prpp)->private;
    /* if backoff is set, delete it (from EQ, as well) */
    if (prp_priv->backoff) {
        backoff_delete(&prp_priv->backoff);
    }
    /* First, stop the protocol if it isn't already stopped */
    if (!(*prpp)->stopped) {
        (*prpp)->stopped = 1;
        (*prpp)->stop(*prpp);
    }
    /* Then, delete all resources used by the protocol */
    if ((*prpp)->lock) {
        PR_DestroyLock((*prpp)->lock);
        (*prpp)->lock = NULL;
    }
    if ((*prpp)->cvar) {
        PR_DestroyCondVar((*prpp)->cvar);
        (*prpp)->cvar = NULL;
    }
    slapi_ch_free((void **)&(*prpp)->private);
    slapi_ch_free((void **)prpp);
}

/* helper function */
void
set_pause_and_busy_time(Private_Repl_Protocol *prp, long *pausetime, long *busywaittime)
{
    /* If neither are set, set busy time to its default */
    if (!*pausetime && !*busywaittime) {
        *busywaittime = repl5_get_backoff_min(prp);
    }
    /* pause time must be at least 1 more than the busy backoff time */
    if (*pausetime && !*busywaittime) {
        /*
       * user specified a pause time but no busy wait time - must
       * set busy wait time to 1 less than pause time - if pause
       * time is 1, we must set it to 2
       */
        if (*pausetime < 2) {
            *pausetime = 2;
        }
        *busywaittime = *pausetime - 1;
    } else if (!*pausetime && *busywaittime) {
        /*
       * user specified a busy wait time but no pause time - must
       * set pause time to 1 more than busy wait time
       */
        *pausetime = *busywaittime + 1;
    } else if (*pausetime && *busywaittime && *pausetime <= *busywaittime) {
        /*
       * user specified both pause and busy wait times, but the pause
       * time was <= busy wait time - pause time must be at least
       * 1 more than the busy wait time
       */
        *pausetime = *busywaittime + 1;
    }
}

/*
 * Do the incremental protocol.
 *
 * What's going on here? This thing is a state machine. It has the
 * following states:
 *
 * State transition table:
 *
 * Curr State       Condition/Event                        Next State
 * ----------       ------------                           -----------
 * START            schedule window is open                ACQUIRE_REPLICA
 *                  schedule window is closed              WAIT_WINDOW_OPEN
 * WAIT_WINDOW_OPEN schedule change                        START
 *                  replicate now                          ACQUIRE_REPLICA
 *                  schedule window opens                  ACQUIRE_REPLICA
 * ACQUIRE_REPLICA  acquired replica                        SEND_CHANGES
 *                  failed to acquire - transient error    START_BACKOFF
 *                  failed to acquire - fatal error        STOP_FATAL_ERROR
 * SEND_CHANGES     can't update                           CONSUMER_NEEDS_REINIT
 *                  no changes to send                     WAIT_CHANGES
 *                  can't send - thransient error          START_BACKOF
 *                  can't send - window closed             WAIT_WINDOW_OPEN
 *                  can'r send - fatal error               STOP_FATAL_ERROR
 * START_BACKOF     replicate now                          ACQUIRE_REPLICA
 *                  schedule changes                       START
 *                  schedule window closes                 WAIT_WINDOW_OPEN
 *                  backoff expires & can acquire          SEND_CHANGES
 *                  backoff expires & can't acquire-trans  BACKOFF
 *                  backoff expires & can't acquire-fatal  STOP_FATAL_ERROR
 * BACKOF           replicate now                          ACQUIRE_REPLICA
 *                  schedule changes                       START
 *                  schedule window closes                 WAIT_WINDOW_OPEN
 *                  backoff expires & can acquire          SEND_CHANGES
 *                  backoff expires & can't acquire-trans  BACKOFF
 *                  backoff expires & can't acquire-fatal  STOP_FATAL_ERROR
 * WAIT_CHANGES     schedule window closes                 WAIT_WINDOW_OPEN
 *                  replicate_now                          ACQUIRE_REPLICA
 *                  change available                       ACQUIRE_REPLICA
 *                  schedule_change                        START
 */

/*
 * Main state machine for the incremental protocol. This routine will,
 * under normal circumstances, not return until the protocol is shut
 * down.
 */
static void
repl5_inc_run(Private_Repl_Protocol *prp)
{
    repl5_inc_private *prp_priv = (repl5_inc_private *)prp->private;
    CSN *cons_schema_csn;
    RUV *ruv = NULL;
    PRUint32 num_changes_sent;
    /* use a different backoff timer strategy for ACQUIRE_REPLICA_BUSY errors */
    PRBool use_busy_backoff_timer = PR_FALSE;
    time_t next_fire_time;
    time_t now;
    long busywaittime = 0;
    long pausetime = 0;
    long loops = 0;
    int wait_change_timer_set = 0;
    int current_state = STATE_START;
    int next_state = STATE_START;
    int done;
    int e1;

    prp->stopped = 0;
    prp->terminate = 0;

    /* establish_protocol_callbacks(prp); */
    done = 0;
    do {
        int rc;

        /* Take action, based on current state, and compute new state. */
        switch (current_state) {
        case STATE_START:
            dev_debug("repl5_inc_run(STATE_START)");
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                done = 1;
                break;
            }
            /*
               * Our initial state. See if we're in a schedule window. If
               * so, then we're ready to acquire the replica and see if it
               * needs any updates from us. If not, then wait for the window
               * to open.
               */
            if (agmt_schedule_in_window_now(prp->agmt)) {
                next_state = STATE_READY_TO_ACQUIRE;
            } else {
                next_state = STATE_WAIT_WINDOW_OPEN;
            }
            /*  we can get here from other states because some events happened and were
               *  not cleared. For instance when we wake up in STATE_WAIT_CHANGES state.
               *  Since this is a fresh start state, we should clear all events */
            /*  ONREPL - this does not feel right - we should take another look
               *  at this state machine */
            reset_events(prp);
            /* Cancel any linger timer that might be in effect... */
            conn_cancel_linger(prp->conn);
            /* ... and disconnect, if currently connected */
            conn_disconnect(prp->conn);
            /* get the new pause time, if any */
            pausetime = agmt_get_pausetime(prp->agmt);
            /* get the new busy wait time, if any */
            busywaittime = agmt_get_busywaittime(prp->agmt);
            if (pausetime || busywaittime) {
                /* helper function to make sure they are set correctly */
                set_pause_and_busy_time(prp, &pausetime, &busywaittime);
            }
            break;

        case STATE_WAIT_WINDOW_OPEN:
            /*
               * We're waiting for a schedule window to open. If one did,
               * or we receive a "replicate now" event, then start a protocol
               * session immediately. If the replication schedule changed, go
               * back to start.  Otherwise, go back to sleep.
               */
            dev_debug("repl5_inc_run(STATE_WAIT_WINDOW_OPEN)");
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                done = 1;
                break;
            } else if (event_occurred(prp, EVENT_WINDOW_OPENED)) {
                next_state = STATE_READY_TO_ACQUIRE;
            } else if (event_occurred(prp, EVENT_REPLICATE_NOW)) {
                next_state = STATE_READY_TO_ACQUIRE;
            } else if (event_occurred(prp, EVENT_AGMT_CHANGED)) {
                next_state = STATE_START;
                conn_set_agmt_changed(prp->conn);
            } else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET)) { /* change available */
                /* just ignore it and go to sleep */
                protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
            } else if ((e1 = event_occurred(prp, EVENT_WINDOW_CLOSED)) ||
                       event_occurred(prp, EVENT_BACKOFF_EXPIRED)) {
                /* this events - should not occur - log a warning and go to sleep */
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                              "repl5_inc_run - %s: "
                              "Event %s should not occur in state %s; going to sleep\n",
                              agmt_get_long_name(prp->agmt), e1 ? event2name(EVENT_WINDOW_CLOSED) : event2name(EVENT_BACKOFF_EXPIRED), state2name(current_state));
                protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
            } else {
                /* wait until window opens or an event occurs */
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "repl5_inc_run - %s: Waiting for update window to open\n",
                              agmt_get_long_name(prp->agmt));
                protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
            }
            break;

        case STATE_WAIT_CHANGES:
            /*
           * We're in a replication window, but we're waiting for more
           * changes to accumulate before we actually hook up and send
           * them.
           */
            dev_debug("repl5_inc_run(STATE_WAIT_CHANGES)");
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                dev_debug("repl5_inc_run(STATE_WAIT_CHANGES): PROTOCOL_IS_SHUTING_DOWN -> end repl5_inc_run\n");
                done = 1;
                break;
            } else if (event_occurred(prp, EVENT_REPLICATE_NOW)) {
                dev_debug("repl5_inc_run(STATE_WAIT_CHANGES): EVENT_REPLICATE_NOW received -> STATE_READY_TO_ACQUIRE\n");
                next_state = STATE_READY_TO_ACQUIRE;
                wait_change_timer_set = 0;
            } else if (event_occurred(prp, EVENT_AGMT_CHANGED)) {
                dev_debug("repl5_inc_run(STATE_WAIT_CHANGES): EVENT_AGMT_CHANGED received -> STATE_START\n");
                next_state = STATE_START;
                conn_set_agmt_changed(prp->conn);
                wait_change_timer_set = 0;
            } else if (event_occurred(prp, EVENT_WINDOW_CLOSED)) {
                dev_debug("repl5_inc_run(STATE_WAIT_CHANGES): EVENT_WINDOW_CLOSED received -> STATE_WAIT_WINDOW_OPEN\n");
                next_state = STATE_WAIT_WINDOW_OPEN;
                wait_change_timer_set = 0;
            } else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET)) {
                dev_debug("repl5_inc_run(STATE_WAIT_CHANGES): EVENT_TRIGGERING_CRITERIA_MET received -> STATE_READY_TO_ACQUIRE\n");
                next_state = STATE_READY_TO_ACQUIRE;
                wait_change_timer_set = 0;
            } else if ((e1 = event_occurred(prp, EVENT_WINDOW_OPENED)) || event_occurred(prp, EVENT_BACKOFF_EXPIRED)) {
                /* this events - should not occur - log a warning and clear the event */
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_inc_run - %s: "
                                                               "Event %s should not occur in state %s\n",
                              agmt_get_long_name(prp->agmt),
                              e1 ? event2name(EVENT_WINDOW_OPENED) : event2name(EVENT_BACKOFF_EXPIRED),
                              state2name(current_state));
                wait_change_timer_set = 0;
            } else {
                if (wait_change_timer_set) {
                    /* We are here because our timer expired */
                    dev_debug("repl5_inc_run(STATE_WAIT_CHANGES): wait_change_timer_set expired -> STATE_START\n");
                    next_state = STATE_START;
                    wait_change_timer_set = 0;
                } else {
                    /*
                   * We are here because the last replication session
                   * finished or aborted.
                   */
                    wait_change_timer_set = 1;
                    protocol_sleep(prp, MAX_WAIT_BETWEEN_SESSIONS);
                }
            }
            break;

        case STATE_READY_TO_ACQUIRE:
            dev_debug("repl5_inc_run(STATE_READY_TO_ACQUIRE)");
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                done = 1;
                break;
            }

            /* ONREPL - at this state we unconditionally acquire the replica
             ignoring all events. Not sure if this is good */
            rc = acquire_replica(prp, REPL_NSDS50_INCREMENTAL_PROTOCOL_OID, &ruv);
            use_busy_backoff_timer = PR_FALSE; /* default */
            if (rc == ACQUIRE_SUCCESS) {
                next_state = STATE_SENDING_UPDATES;
            } else if (rc == ACQUIRE_REPLICA_BUSY) {
                next_state = STATE_BACKOFF_START;
                use_busy_backoff_timer = PR_TRUE;
            } else if (rc == ACQUIRE_CONSUMER_WAS_UPTODATE) {
                next_state = STATE_WAIT_CHANGES;
            } else if (rc == ACQUIRE_TRANSIENT_ERROR) {
                next_state = STATE_BACKOFF_START;
            } else if (rc == ACQUIRE_FATAL_ERROR) {
                next_state = STATE_STOP_FATAL_ERROR;
            }
            break;

        case STATE_BACKOFF_START:
            dev_debug("repl5_inc_run(STATE_BACKOFF_START)");
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                done = 1;
                break;
            }
            if (event_occurred(prp, EVENT_REPLICATE_NOW)) {
                next_state = STATE_READY_TO_ACQUIRE;
            } else if (event_occurred(prp, EVENT_AGMT_CHANGED)) {
                next_state = STATE_START;
                conn_set_agmt_changed(prp->conn);
            } else if (event_occurred(prp, EVENT_WINDOW_CLOSED)) {
                next_state = STATE_WAIT_WINDOW_OPEN;
            } else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET)) {
                /* consume and ignore */
            } else if ((e1 = event_occurred(prp, EVENT_WINDOW_OPENED)) ||
                       event_occurred(prp, EVENT_BACKOFF_EXPIRED)) {
                /* This should never happen */
                /* this events - should not occur - log a warning and go to sleep */
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "repl5_inc_run - %s: Event %s should not occur in state %s\n",
                              agmt_get_long_name(prp->agmt),
                              e1 ? event2name(EVENT_WINDOW_OPENED) : event2name(EVENT_BACKOFF_EXPIRED),
                              state2name(current_state));
            } else {
                /* Set up the backoff timer to wake us up at the appropriate time */
                /* if previous backoff set up, delete it. */
                if (prp_priv->backoff) {
                    backoff_delete(&prp_priv->backoff);
                }
                if (use_busy_backoff_timer) {
                    /* we received a busy signal from the consumer, wait for a while */
                    if (!busywaittime) {
                        busywaittime = repl5_get_backoff_min(prp);
                    }
                    prp_priv->backoff = backoff_new(BACKOFF_FIXED, busywaittime, busywaittime);
                } else {
                    prp_priv->backoff = backoff_new(BACKOFF_EXPONENTIAL, repl5_get_backoff_min(prp),
                                                    repl5_get_backoff_max(prp));
                }
                next_state = STATE_BACKOFF;
                backoff_reset(prp_priv->backoff, repl5_inc_backoff_expired, (void *)prp);
                protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
                use_busy_backoff_timer = PR_FALSE;
            }
            break;

        case STATE_BACKOFF:
            /*
           * We're in a backoff state.
           */
            dev_debug("repl5_inc_run(STATE_BACKOFF)");
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                if (prp_priv->backoff)
                    backoff_delete(&prp_priv->backoff);
                done = 1;
                break;
            } else if (event_occurred(prp, EVENT_REPLICATE_NOW)) {
                next_state = STATE_READY_TO_ACQUIRE;
            } else if (event_occurred(prp, EVENT_AGMT_CHANGED)) {
                next_state = STATE_START;
                conn_set_agmt_changed(prp->conn);
                /* Destroy the backoff timer, since we won't need it anymore */
                if (prp_priv->backoff)
                    backoff_delete(&prp_priv->backoff);
            } else if (event_occurred(prp, EVENT_WINDOW_CLOSED)) {
                next_state = STATE_WAIT_WINDOW_OPEN;
                /* Destroy the backoff timer, since we won't need it anymore */
                if (prp_priv->backoff)
                    backoff_delete(&prp_priv->backoff);
            } else if (event_occurred(prp, EVENT_BACKOFF_EXPIRED)) {
                rc = acquire_replica(prp, REPL_NSDS50_INCREMENTAL_PROTOCOL_OID, &ruv);
                use_busy_backoff_timer = PR_FALSE;
                if (rc == ACQUIRE_SUCCESS) {
                    next_state = STATE_SENDING_UPDATES;
                } else if (rc == ACQUIRE_REPLICA_BUSY) {
                    next_state = STATE_BACKOFF;
                    use_busy_backoff_timer = PR_TRUE;
                } else if (rc == ACQUIRE_CONSUMER_WAS_UPTODATE) {
                    next_state = STATE_WAIT_CHANGES;
                } else if (rc == ACQUIRE_TRANSIENT_ERROR) {
                    next_state = STATE_BACKOFF;
                } else if (rc == ACQUIRE_FATAL_ERROR) {
                    next_state = STATE_STOP_FATAL_ERROR;
                }
                /*
               * We either need to step the backoff timer, or
               * destroy it if we don't need it anymore
               */
                if (STATE_BACKOFF == next_state) {
                    /* Step the backoff timer */
                    now = slapi_current_utc_time();
                    next_fire_time = backoff_step(prp_priv->backoff);
                    /* And go back to sleep */
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                  "repl5_inc_run - %s: Replication session backing off for %ld seconds\n",
                                  agmt_get_long_name(prp->agmt), next_fire_time - now);
                    protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
                } else {
                    /* Destroy the backoff timer, since we won't need it anymore */
                    backoff_delete(&prp_priv->backoff);
                }
            } else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET)) {
                /* changes are available */
                if (prp_priv->backoff == NULL || backoff_expired(prp_priv->backoff, 60)) {
                    /*
                  * Have seen cases that the agmt stuck here forever since
                  * somehow the backoff timer was not in event queue anymore.
                  * If the backoff timer has expired more than 60 seconds, destroy it.
                  */
                    if (prp_priv->backoff)
                        backoff_delete(&prp_priv->backoff);
                    next_state = STATE_READY_TO_ACQUIRE;
                } else {
                    /* ignore changes and go to sleep */
                    protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
                }
            } else if (event_occurred(prp, EVENT_WINDOW_OPENED)) {
                /* this should never happen - log an error and go to sleep */
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "repl5_inc_run - %s: "
                                                               "Event %s should not occur in state %s; going to sleep\n",
                              agmt_get_long_name(prp->agmt), event2name(EVENT_WINDOW_OPENED),
                              state2name(current_state));
                protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
            }
            break;

        case STATE_SENDING_UPDATES:
            dev_debug("repl5_inc_run(STATE_SENDING_UPDATES)");
            num_changes_sent = 0;
            /*
           * We've acquired the replica, and are ready to send any needed updates.
           */
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                release_replica(prp);
                done = 1;
                agmt_set_update_in_progress(prp->agmt, PR_FALSE);
                agmt_set_last_update_end(prp->agmt, slapi_current_utc_time());
                /* MAB: I don't find the following status correct. How do we know it has
               * been stopped by an admin and not by a total update request, for instance?
               * In any case, how is this protocol shutdown situation different from all the
               * other ones that are present in this state machine? */
                /* richm: We at least need to let monitors know that the protocol has been
               * shutdown - maybe they can figure out why */
                agmt_set_last_update_status(prp->agmt, 0, 0, "Protocol stopped");
                if (NULL != ruv) {
                    ruv_destroy(&ruv);
                    ruv = NULL;
                }
                agmt_update_done(prp->agmt, 0);
                break;
            }

            agmt_set_last_update_status(prp->agmt, 0, 0, "Incremental update started");
            /* ONREPL - in this state we send changes no matter what other events occur.
           * This is because we can get because of the REPLICATE_NOW event which
           * has high priority. Is this ok? */
            /* First, push new schema to the consumer if needed */
            /* ONREPL - should we push schema after we examine the RUV? */
            /*
           * GGOOREPL - I don't see why we should wait until we've
           * examined the RUV.  The schema entry has its own CSN that is
           * used to decide if the remote schema needs to be updated.
           */
            cons_schema_csn = agmt_get_consumer_schema_csn(prp->agmt);
            rc = conn_push_schema(prp->conn, &cons_schema_csn);
            if (cons_schema_csn != agmt_get_consumer_schema_csn(prp->agmt)) {
                agmt_set_consumer_schema_csn(prp->agmt, cons_schema_csn);
            }
            if (CONN_SCHEMA_UPDATED != rc && CONN_SCHEMA_NO_UPDATE_NEEDED != rc) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "%s: Warning: unable to replicate schema: rc=%d\n", agmt_get_long_name(prp->agmt), rc);
                /* But keep going */
            }
            dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> examine_update_vector");
            rc = examine_update_vector(prp, ruv);
            /*
           *  Decide what to do next - proceed with incremental, backoff, or total update
           */
            switch (rc) {
            case EXAMINE_RUV_PARAM_ERROR:
                /* this is really bad - we have NULL prp! */
                next_state = STATE_STOP_FATAL_ERROR;
                break;
            case EXAMINE_RUV_PRISTINE_REPLICA:
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                              "repl5_inc_run - %s: Replica has no update vector. It has never been initialized.\n",
                              agmt_get_long_name(prp->agmt));
                agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_RUV_ERROR,
                                            "Replica is not initialized");
                next_state = STATE_BACKOFF_START;
                break;
            case EXAMINE_RUV_GENERATION_MISMATCH:
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                              "repl5_inc_run - %s: The remote replica has a different database generation ID than "
                              "the local database.  You may have to reinitialize the remote replica, "
                              "or the local replica.\n",
                              agmt_get_long_name(prp->agmt));
                agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_RUV_ERROR,
                                            "Replica has different database generation ID, remote "
                                            "replica may need to be initialized");
                next_state = STATE_BACKOFF_START;
                break;
            case EXAMINE_RUV_REPLICA_TOO_OLD:
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                              "repl5_inc_run - %s: Replica update vector is too out of date to bring "
                              "into sync using the incremental protocol. The replica "
                              "must be reinitialized.\n",
                              agmt_get_long_name(prp->agmt));
                agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_RUV_ERROR,
                                            "Replica needs to be reinitialized");
                next_state = STATE_BACKOFF_START;
                break;
            case EXAMINE_RUV_OK:
                /* update our csn generator state with the consumer's ruv data */
                dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> examine_update_vector OK");
                rc = replica_update_csngen_state(prp->replica, ruv);
                if (rc == CSN_LIMIT_EXCEEDED) /* too much skew */ {
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                  "repl5_inc_run - %s: Fatal error - too much time skew between replicas!\n",
                                  agmt_get_long_name(prp->agmt));
                    agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_EXCESSIVE_CLOCK_SKEW,
                                                "fatal error - too much time skew between replicas");
                    next_state = STATE_STOP_FATAL_ERROR;
                } else if (rc != 0) /* internal error */ {
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                  "repl5_inc_run - %s: Fatal internal error updating the CSN generator!\n",
                                  agmt_get_long_name(prp->agmt));
                    agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_INTERNAL_ERROR,
                                                "fatal internal error updating the CSN generator");
                    next_state = STATE_STOP_FATAL_ERROR;
                } else {
                    /*
                       *  Reset our update times and status
                       */
                    agmt_set_last_update_start(prp->agmt, slapi_current_utc_time());
                    agmt_set_last_update_end(prp->agmt, 0);
                    agmt_set_update_in_progress(prp->agmt, PR_TRUE);
                    /*
                       *  Send the updates
                       */
                    rc = send_updates(prp, ruv, &num_changes_sent);
                    if (rc == UPDATE_NO_MORE_UPDATES) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_NO_MORE_UPDATES -> STATE_WAIT_CHANGES");
                        agmt_set_last_update_status(prp->agmt, 0, 0, "Incremental update succeeded");
                        next_state = STATE_WAIT_CHANGES;
                    } else if (rc == UPDATE_YIELD) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_YIELD -> STATE_BACKOFF_START");
                        agmt_set_last_update_status(prp->agmt, 0, 0, "Incremental update succeeded and yielded");
                        use_busy_backoff_timer = PR_TRUE;
                        next_state = STATE_BACKOFF_START;
                    } else if (rc == UPDATE_TRANSIENT_ERROR) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_TRANSIENT_ERROR -> STATE_BACKOFF_START");
                        agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_TRANSIENT_ERROR,
                                                    "Incremental update transient warning.  Backing off, will retry update later.");
                        next_state = STATE_BACKOFF_START;
                    } else if (rc == UPDATE_FATAL_ERROR) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_FATAL_ERROR -> STATE_STOP_FATAL_ERROR");
                        next_state = STATE_STOP_FATAL_ERROR;
                    } else if (rc == UPDATE_SCHEDULE_WINDOW_CLOSED) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_SCHEDULE_WINDOW_CLOSED -> STATE_WAIT_WINDOW_OPEN");
                        /*
                           * ONREPL - I don't think we should check this. We might be
                           * here because of replicate_now event - so we don't care
                           * about the schedule
                           */
                        next_state = STATE_WAIT_WINDOW_OPEN;
                        /* ONREPL - do we need to release the replica here ? */
                        conn_disconnect(prp->conn);
                    } else if (rc == UPDATE_CONNECTION_LOST) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_CONNECTION_LOST -> STATE_BACKOFF_START");
                        agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CONN_ERROR,
                                                    "Incremental update connection error.  Backing off, will retry update later.");
                        next_state = STATE_BACKOFF_START;
                    } else if (rc == UPDATE_TIMEOUT) {
                        dev_debug("repl5_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_TIMEOUT -> STATE_BACKOFF_START");
                        agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CONN_TIMEOUT,
                                                    "Incremental update timeout error.  Backing off, will retry update later.");
                        next_state = STATE_BACKOFF_START;
                    }
                    /* Set the updates times based off the result of send_updates() */
                    if (rc == UPDATE_NO_MORE_UPDATES) {
                        /* update successful, set the end time */
                        agmt_set_last_update_end(prp->agmt, slapi_current_utc_time());
                    } else {
                        /* Failed to send updates, reset the start time to zero */
                        agmt_set_last_update_start(prp->agmt, 0);
                    }
                    agmt_set_update_in_progress(prp->agmt, PR_FALSE);
                }
                break;
            }

            if (NULL != ruv) {
                ruv_destroy(&ruv);
                ruv = NULL;
            }
            agmt_update_done(prp->agmt, 0);

            /* If timed out, close the connection after released the replica */
            release_replica(prp);
            if (rc == UPDATE_TIMEOUT) {
                conn_disconnect(prp->conn);
            }
            if (rc == UPDATE_NO_MORE_UPDATES && num_changes_sent > 0) {
                if (pausetime > 0) {
                    /* richm - 20020219 - If we have acquired the consumer, and another master has gone
                   * into backoff waiting for us to release it, we may acquire the replica sooner
                   * than the other master has a chance to, and the other master may not be able
                   * to acquire the consumer for a long time (hours, days?) if this server is
                   * under a heavy load (see reliab06 et. al. system tests)
                   * So, this sleep gives the other master(s) a chance to acquire the consumer replica */
                    loops = pausetime;
                    /* the while loop is so that we don't just sleep and sleep if an
                   * event comes in that we should handle immediately (like shutdown) */
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                  "repl5_inc_run - %s: Pausing updates for %ld seconds to allow other suppliers to update consumer\n",
                                  agmt_get_long_name(prp->agmt), pausetime);
                    while (loops-- && !(PROTOCOL_IS_SHUTDOWN(prp))) {
                        DS_Sleep(PR_SecondsToInterval(1));
                    }
                } else if (num_changes_sent > 10) {
                    /* wait for consumer to write its ruv if the replication was busy */
                    /* When asked, consumer sends its ruv in cache to the supplier. */
                    /* DS_Sleep ( PR_SecondsToInterval(1) ); */
                }
            }
            break;

        case STATE_STOP_FATAL_ERROR:
            /*
           * We encountered some sort of a fatal error. Suspend.
           */
            dev_debug("repl5_inc_run(STATE_STOP_FATAL_ERROR)");
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "repl5_inc_run - %s: Incremental update failed and requires administrator action\n",
                          agmt_get_long_name(prp->agmt));
            next_state = STATE_STOP_FATAL_ERROR_PART2;
            break;

        case STATE_STOP_FATAL_ERROR_PART2:
            if (PROTOCOL_IS_SHUTDOWN(prp)) {
                done = 1;
                break;
            }
            /* MAB: This state is the FATAL state where we are supposed to get
           * as a result of a FATAL error on send_updates. But, as bug
           * states, send_updates was always returning TRANSIENT errors and never
           * FATAL... In other words, this code has never been tested before...
           *
           * As of 01/16/01, this piece of code was in a very dangerous state. In particular,
           * 1) it does not catch any events
           * 2) it is a terminal state (once reached it never transitions to a different state)
           *
           * Both things combined make this state to become a consuming infinite loop
           * that is useless after all (we are in a fatal place requiring manual admin jobs */

            /* MAB: The following lines fix problem number 1 above... When the code gets
           * into this state, it should only get a chance to get out of it by an
           * EVENT_AGMT_CHANGED event... All other events should be ignored */
            else if (event_occurred(prp, EVENT_AGMT_CHANGED)) {
                dev_debug("repl5_inc_run(STATE_STOP_FATAL_ERROR): EVENT_AGMT_CHANGED received\n");
                /* Chance to recover for the EVENT_AGMT_CHANGED event.
              * This is not mandatory, but fixes problem 2 above */
                next_state = STATE_STOP_NORMAL_TERMINATION;
            } else {
                dev_debug("repl5_inc_run(STATE_STOP_FATAL_ERROR): Event received. Clearing it\n");
                reset_events(prp);
            }

            protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
            break;

        case STATE_STOP_NORMAL_TERMINATION:
            /*
           * We encountered some sort of a fatal error. Return.
           */
            /* XXXggood update state in replica */
            dev_debug("repl5_inc_run(STATE_STOP_NORMAL_TERMINATION)");
            done = 1;
            break;
        }

        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "repl5_inc_run - %s: State: %s -> %s\n",
                      agmt_get_long_name(prp->agmt), state2name(current_state), state2name(next_state));

        current_state = next_state;
    } while (!done);

    /* remove_protocol_callbacks(prp); */
    prp->stopped = 1;
    /* Cancel any linger timer that might be in effect... */
    conn_cancel_linger(prp->conn);
    /* ... and disconnect, if currently connected */
    conn_disconnect(prp->conn);
}

/*
 * Go to sleep until awakened.
 */
static void
protocol_sleep(Private_Repl_Protocol *prp, PRIntervalTime duration)
{
    PR_ASSERT(NULL != prp);
    PR_Lock(prp->lock);
    /* we should not go to sleep if there are events available to be processed.
       Otherwise, we can miss the event that suppose to wake us up */
    if (prp->eventbits == 0)
        PR_WaitCondVar(prp->cvar, duration);
    else {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "protocol_sleep - %s: Can't go to sleep: event bits - %x\n",
                      agmt_get_long_name(prp->agmt), prp->eventbits);
    }
    PR_Unlock(prp->lock);
}

/*
 * Notify the protocol about some event. Signal the condition
 * variable in case the protocol is sleeping. Multiple occurences
 * of a single event type are not remembered (e.g. no stack
 * of events is maintained).
 */
static void
event_notify(Private_Repl_Protocol *prp, PRUint32 event)
{
    PR_ASSERT(NULL != prp);
    PR_Lock(prp->lock);
    prp->eventbits |= event;
    PR_NotifyCondVar(prp->cvar);
    PR_Unlock(prp->lock);
}

/*
 * Test to see if an event occurred. The event is cleared when
 * read.
 */
static PRUint32
event_occurred(Private_Repl_Protocol *prp, PRUint32 event)
{
    PRUint32 return_value;
    PR_ASSERT(NULL != prp);
    PR_Lock(prp->lock);
    return_value = (prp->eventbits & event);
    prp->eventbits &= ~event; /* Clear event */
    PR_Unlock(prp->lock);
    return return_value;
}

static void
reset_events(Private_Repl_Protocol *prp)
{
    PR_ASSERT(NULL != prp);
    PR_Lock(prp->lock);
    prp->eventbits = 0;
    PR_Unlock(prp->lock);
}

/*
 * Replay the actual update to the consumer. Construct an appropriate LDAP
 * operation, attach the baggage LDAPv3 control that contains the CSN, etc.,
 * and send the operation to the consumer.
 */
ConnResult
replay_update(Private_Repl_Protocol *prp, slapi_operation_parameters *op, int *message_id)
{
    ConnResult return_value = CONN_OPERATION_FAILED;
    LDAPControl *update_control;
    char *parentuniqueid;
    LDAPMod **modrdn_mods = NULL;
    char csn_str[CSN_STRSIZE]; /* For logging only */

    if (message_id) {
        /* if we get out of this function without setting message_id, it means
           we didn't send an op, so no result needs to be processed */
        *message_id = 0;
    }

    /* Construct the replication info control that accompanies the operation */
    if (SLAPI_OPERATION_ADD == op->operation_type) {
        parentuniqueid = op->p.p_add.parentuniqueid;
    } else if (SLAPI_OPERATION_MODRDN == op->operation_type) {
        /*
         * For modrdn operations, we need to send along modified attributes, e.g.
         * modifytimestamp.
         * And the superior_uniqueid !
         */
        modrdn_mods = op->p.p_modrdn.modrdn_mods;
        parentuniqueid = op->p.p_modrdn.modrdn_newsuperior_address.uniqueid;
    } else {
        parentuniqueid = NULL;
    }
    if (create_NSDS50ReplUpdateInfoControl(op->target_address.uniqueid,
                                           parentuniqueid, op->csn, modrdn_mods, &update_control) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                      "replay_update - %s: Unable to create NSDS50ReplUpdateInfoControl "
                      "for operation with csn %s. Skipping update.\n",
                      agmt_get_long_name(prp->agmt), csn_as_string(op->csn, PR_FALSE, csn_str));
    } else {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "replay_update - %s: Sending %s operation (dn=\"%s\" csn=%s)\n",
                          agmt_get_long_name(prp->agmt),
                          op2string(op->operation_type), REPL_GET_DN(&op->target_address),
                          csn_as_string(op->csn, PR_FALSE, csn_str));
        }
        /* What type of operation is it? */
        switch (op->operation_type) {
        case SLAPI_OPERATION_ADD: {
            LDAPMod **entryattrs;
            /* Convert entry to mods */
            (void)slapi_entry2mods(op->p.p_add.target_entry,
                                   NULL /* &entrydn : We don't need it */,
                                   &entryattrs);
            if (NULL == entryattrs) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "replay_update - %s: Cannot convert entry to LDAPMods.\n",
                              agmt_get_long_name(prp->agmt));
                return_value = CONN_LOCAL_ERROR;
            } else {
                /* If fractional agreement, trim down the entry */
                if (agmt_is_fractional(prp->agmt)) {
                    repl5_strip_fractional_mods(prp->agmt, entryattrs);
                }
                if (MODS_ARE_EMPTY(entryattrs)) {
                    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                      "replay_update - %s: %s operation (dn=\"%s\" csn=%s) "
                                      "not sent - empty\n",
                                      agmt_get_long_name(prp->agmt),
                                      op2string(op->operation_type),
                                      REPL_GET_DN(&op->target_address),
                                      csn_as_string(op->csn, PR_FALSE, csn_str));
                    }
                    return_value = CONN_OPERATION_SUCCESS;
                } else {
                    return_value = conn_send_add(prp->conn, REPL_GET_DN(&op->target_address),
                                                 entryattrs, update_control, message_id);
                }
                ldap_mods_free(entryattrs, 1);
            }
            break;
        }
        case SLAPI_OPERATION_MODIFY:
            /* If fractional agreement, trim down the mods */
            if (agmt_is_fractional(prp->agmt)) {
                repl5_strip_fractional_mods(prp->agmt, op->p.p_modify.modify_mods);
            }
            if (MODS_ARE_EMPTY(op->p.p_modify.modify_mods)) {
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                  "replay_update - %s: %s operation (dn=\"%s\" csn=%s) "
                                  "not sent - empty\n",
                                  agmt_get_long_name(prp->agmt),
                                  op2string(op->operation_type),
                                  REPL_GET_DN(&op->target_address),
                                  csn_as_string(op->csn, PR_FALSE, csn_str));
                }
                return_value = CONN_OPERATION_SUCCESS;
            } else {
                return_value = conn_send_modify(prp->conn, REPL_GET_DN(&op->target_address),
                                                op->p.p_modify.modify_mods, update_control, message_id);
            }
            break;
        case SLAPI_OPERATION_DELETE:
            return_value = conn_send_delete(prp->conn, REPL_GET_DN(&op->target_address),
                                            update_control, message_id);
            break;
        case SLAPI_OPERATION_MODRDN:
            /* XXXggood need to pass modrdn mods in update control! */
            return_value = conn_send_rename(prp->conn, REPL_GET_DN(&op->target_address),
                                            op->p.p_modrdn.modrdn_newrdn,
                                            REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address),
                                            op->p.p_modrdn.modrdn_deloldrdn,
                                            update_control, message_id);
            break;
        default:
            slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replay_update - %s: Unknown "
                                                               "operation type %lu found in changelog - skipping change.\n",
                          agmt_get_long_name(prp->agmt), op->operation_type);
        }

        destroy_NSDS50ReplUpdateInfoControl(&update_control);
    }

    if (CONN_OPERATION_SUCCESS == return_value) {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "replay_update - %s: Consumer successfully sent operation with csn %s\n",
                          agmt_get_long_name(prp->agmt), csn_as_string(op->csn, PR_FALSE, csn_str));
        }
    } else {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "replay_update - %s: Consumer could not replay operation with csn %s\n",
                          agmt_get_long_name(prp->agmt), csn_as_string(op->csn, PR_FALSE, csn_str));
        }
    }
    return return_value;
}

static PRBool
is_dummy_operation(const slapi_operation_parameters *op)
{
    return (strcmp(op->target_address.uniqueid, START_ITERATION_ENTRY_UNIQUEID) == 0);
}

void
cl5_operation_parameters_done(struct slapi_operation_parameters *sop)
{
    if (sop != NULL) {
        switch (sop->operation_type) {
        case SLAPI_OPERATION_BIND:
            slapi_ch_free((void **)&(sop->p.p_bind.bind_saslmechanism));
            if (sop->p.p_bind.bind_creds)
                ber_bvecfree((struct berval **)&(sop->p.p_bind.bind_creds));
            if (sop->p.p_bind.bind_ret_saslcreds)
                ber_bvecfree((struct berval **)&(sop->p.p_bind.bind_ret_saslcreds));
            sop->p.p_bind.bind_creds = NULL;
            sop->p.p_bind.bind_ret_saslcreds = NULL;
            break;
        case SLAPI_OPERATION_COMPARE:
            ava_done((struct ava *)&(sop->p.p_compare.compare_ava));
            break;
        case SLAPI_OPERATION_SEARCH:
            slapi_ch_free((void **)&(sop->p.p_search.search_strfilter));
            charray_free(sop->p.p_search.search_attrs);
            slapi_filter_free(sop->p.p_search.search_filter, 1);
            break;
        case SLAPI_OPERATION_MODRDN:
            sop->p.p_modrdn.modrdn_deloldrdn = 0;
            break;
        case SLAPI_OPERATION_EXTENDED:
            slapi_ch_free((void **)&(sop->p.p_extended.exop_oid));
            if (sop->p.p_extended.exop_value)
                ber_bvecfree((struct berval **)&(sop->p.p_extended.exop_value));
            sop->p.p_extended.exop_value = NULL;
            break;
        default:
            break;
        }
    }
    operation_parameters_done(sop);
}

/* Helper to update the agreement state based on a the result of a replay operation */
static int
repl5_inc_update_from_op_result(Private_Repl_Protocol *prp, ConnResult replay_crc, int connection_error, char *csn_str, char *uniqueid, ReplicaId replica_id, int *finished, PRUint32 *num_changes_sent)
{
    int return_value = 0;

    if (CONN_OPERATION_SUCCESS != replay_crc) {
        /* Figure out what to do next */
        if (CONN_OPERATION_FAILED == replay_crc) {
            /* Map ldap error code to return value */
            if (!ignore_error_and_keep_going(connection_error)) {
                return_value = UPDATE_TRANSIENT_ERROR;
                *finished = 1;
            } else {
                agmt_inc_last_update_changecount(prp->agmt, replica_id, 1 /*skipped*/);
            }
            slapi_log_err(*finished ? SLAPI_LOG_WARNING : slapi_log_urp,
                          repl_plugin_name,
                          "repl5_inc_update_from_op_result - %s: Consumer failed to replay change (uniqueid %s, CSN %s): %s (%d). %s.\n",
                          agmt_get_long_name(prp->agmt),
                          uniqueid, csn_str,
                          ldap_err2string(connection_error), connection_error,
                          *finished ? "Will retry later" : "Skipping");
        } else if (CONN_NOT_CONNECTED == replay_crc) {
            /* We lost the connection - enter backoff state */

            return_value = UPDATE_CONNECTION_LOST;
            *finished = 1;
            slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                          "repl5_inc_update_from_op_result - %s: Consumer failed to replay change (uniqueid %s, CSN %s): "
                          "%s(%d). Will retry later.\n",
                          agmt_get_long_name(prp->agmt),
                          uniqueid, csn_str,
                          connection_error ? ldap_err2string(connection_error) : "Connection lost",
                          connection_error);
        } else if (CONN_TIMEOUT == replay_crc) {
            return_value = UPDATE_TIMEOUT;
            *finished = 1;
            slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                          "repl5_inc_update_from_op_result - %s: Consumer timed out to replay change (uniqueid %s, CSN %s): "
                          "%s.\n",
                          agmt_get_long_name(prp->agmt),
                          uniqueid, csn_str,
                          connection_error ? ldap_err2string(connection_error) : "Timeout");
        } else if (CONN_LOCAL_ERROR == replay_crc) {
            /*
             * Something bad happened on the local server - enter
             * backoff state.
             */
            return_value = UPDATE_TRANSIENT_ERROR;
            *finished = 1;
            slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                          "repl5_inc_update_from_op_result - %s: Failed to replay change (uniqueid %s, CSN %s): "
                          "Local error. Will retry later.\n",
                          agmt_get_long_name(prp->agmt),
                          uniqueid, csn_str);
        }
        if (*finished) {
            /*
             * A serious error has occurred, the consumer might have closed
             * the connection already, but we need to close the conn on the
             * supplier side to properly set the conn structure as closed.
             */
            conn_disconnect(prp->conn);
        }
    } else {
        /* Positive response received */
        (*num_changes_sent)++;
        agmt_inc_last_update_changecount(prp->agmt, replica_id, 0 /*replayed*/);
    }
    return return_value;
}

/*
 * Send a set of updates to the replica.  Assumes that (1) the replica
 * has already been acquired, (2) that the consumer's update vector has
 * been checked and (3) that it's ok to send incremental updates.
 * Returns:
 * UPDATE_NO_MORE_UPDATES - all updates were sent successfully
 * UPDATE_TRANSIENT_ERROR - some non-permanent error occurred. Try again later.
 * UPDATE_FATAL_ERROR - some bad, permanent error occurred.
 * UPDATE_SCHEDULE_WINDOW_CLOSED - the schedule window closed on us.
 */
static int
send_updates(Private_Repl_Protocol *prp, RUV *remote_update_vector, PRUint32 *num_changes_sent)
{
    CL5Entry entry;
    slapi_operation_parameters op;
    int return_value = 0;
    int rc;
    CL5ReplayIterator *changelog_iterator;
    int message_id = 0;
    result_data *rd = NULL;

    *num_changes_sent = 0;
    /*
     * Iterate over the changelog. Retrieve each update,
     * construct an appropriate LDAP operation,
     * attaching the CSN, and send the change.
     */

    rc = cl5CreateReplayIterator(prp, remote_update_vector, &changelog_iterator);
    if (CL5_SUCCESS != rc) {
        switch (rc) {
        case CL5_BAD_DATA: /* invalid parameter passed to the function */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Invalid parameter passed to cl5CreateReplayIterator\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Invalid parameter passed to cl5CreateReplayIterator");
            return_value = UPDATE_FATAL_ERROR;
            break;
        case CL5_BAD_FORMAT: /* db data has unexpected format */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Unexpected format encountered in changelog database\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Unexpected format encountered in changelog database");
            return_value = UPDATE_FATAL_ERROR;
            break;
        case CL5_BAD_STATE: /* changelog is in an incorrect state for attempted operation */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Changelog database was in an incorrect state\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Changelog database was in an incorrect state");
            return_value = UPDATE_FATAL_ERROR;
            break;
        case CL5_BAD_DBVERSION: /* changelog has invalid dbversion */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Incorrect dbversion found in changelog database\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Incorrect dbversion found in changelog database");
            return_value = UPDATE_FATAL_ERROR;
            break;
        case CL5_DB_ERROR: /* database error */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: A changelog database error was encountered\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Changelog database error was encountered");
            return_value = UPDATE_FATAL_ERROR;
            break;
        case CL5_NOTFOUND: /* we have no changes to send */
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "send_updates - %s: No changes to send\n",
                          agmt_get_long_name(prp->agmt));
            return_value = UPDATE_NO_MORE_UPDATES;
            break;
        case CL5_MEMORY_ERROR: /* memory allocation failed */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Memory allocation error occurred\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "changelog memory allocation error occurred");
            return_value = UPDATE_FATAL_ERROR;
            break;
        case CL5_SYSTEM_ERROR: /* NSPR error occurred: use PR_GetError for further info */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: An NSPR error (%d) occurred\n",
                          agmt_get_long_name(prp->agmt), PR_GetError());
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        case CL5_CSN_ERROR: /* CSN API failed */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: A CSN API failure was encountered\n",
                          agmt_get_long_name(prp->agmt));
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        case CL5_RUV_ERROR: /* RUV API failed */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: An RUV API failure occurred\n",
                          agmt_get_long_name(prp->agmt));
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        case CL5_OBJSET_ERROR: /* namedobjset api failed */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: A namedobject API failure occurred\n",
                          agmt_get_long_name(prp->agmt));
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        case CL5_PURGED_DATA: /* requested data has been purged */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Data required to update replica has been purged from the changelog. "
                          "If the error persists the replica must be reinitialized.\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Data required to update replica has been purged from the changelog. "
                                        "If the error persists the replica must be reinitialized.");
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        case CL5_MISSING_DATA: /* data should be in the changelog, but is missing */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: Missing data encountered. "
                          "If the error persists the replica must be reinitialized.\n",
                          agmt_get_long_name(prp->agmt));
            agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                        "Changelog data is missing. "
                                        "If the error persists the replica must be reinitialized.");
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        case CL5_UNKNOWN_ERROR: /* unclassified error */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: An unknown error was encountered\n",
                          agmt_get_long_name(prp->agmt));
            return_value = UPDATE_TRANSIENT_ERROR;
            break;
        default:
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "send_updates - %s: An unknown error (%d) occurred "
                          "(cl5CreateReplayIterator)\n",
                          agmt_get_long_name(prp->agmt), rc);
            return_value = UPDATE_TRANSIENT_ERROR;
        }
    } else {
        ConnResult replay_crc;
        Replica *replica = prp->replica;
        PRBool subentry_update_needed = PR_FALSE;
        PRUint64 release_timeout = replica_get_release_timeout(replica);
        char csn_str[CSN_STRSIZE];
        int skipped_updates = 0;
        int fractional_repl;
        int finished = 0;
#define FRACTIONAL_SKIPPED_THRESHOLD 100

        /* Start the results reading thread */
        rd = repl5_inc_rd_new(prp);
        if (!prp->repl50consumer) {
            rc = repl5_inc_create_async_result_thread(rd);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "send_updates - %s: "
                                                               "repl5_inc_create_async_result_thread failed; error - %d\n",
                              agmt_get_long_name(prp->agmt), rc);
                agmt_set_last_update_status(prp->agmt, 0, rc, "Failed to create result thread");
                return_value = UPDATE_FATAL_ERROR;
            }
        }

        memset((void *)&op, 0, sizeof(op));
        entry.op = &op;
        fractional_repl = agmt_is_fractional(prp->agmt);
        do {
            cl5_operation_parameters_done(entry.op);
            memset((void *)entry.op, 0, sizeof(op));
            rc = cl5GetNextOperationToReplay(changelog_iterator, &entry);
            switch (rc) {
            case CL5_SUCCESS:
                /* check that we don't return dummy entries */
                if (is_dummy_operation(entry.op)) {
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                  "send_updates - %s: changelog iteration code returned a dummy entry with csn %s, "
                                  "skipping ...\n",
                                  agmt_get_long_name(prp->agmt), csn_as_string(entry.op->csn, PR_FALSE, csn_str));
                    continue;
                }
                replay_crc = replay_update(prp, entry.op, &message_id);
                if (message_id) {
                    rd->last_message_id_sent = message_id;
                }
                /* If we're talking to an old non-async replica, we need to pick up the response here */
                if (CONN_OPERATION_SUCCESS != replay_crc) {
                    int operation, error;
                    conn_get_error(prp->conn, &operation, &error);
                    csn_as_string(entry.op->csn, PR_FALSE, csn_str);
                    /* Figure out what to do next */
                    if (CONN_OPERATION_FAILED == replay_crc) {
                        /* Map ldap error code to return value */
                        if (!ignore_error_and_keep_going(error)) {
                            return_value = UPDATE_TRANSIENT_ERROR;
                            finished = 1;
                        } else {
                            agmt_inc_last_update_changecount(prp->agmt, csn_get_replicaid(entry.op->csn), 1 /*skipped*/);
                        }
                        slapi_log_err(finished ? SLAPI_LOG_WARNING : slapi_log_urp,
                                      "send_updates - %s: Failed to send update operation to consumer (uniqueid %s, CSN %s): %s. %s.\n",
                                      (char *)agmt_get_long_name(prp->agmt),
                                      entry.op->target_address.uniqueid, csn_str,
                                      ldap_err2string(error),
                                      finished ? "Will retry later" : "Skipping");
                    } else if (CONN_NOT_CONNECTED == replay_crc) {
                        /* We lost the connection - enter backoff state */

                        return_value = UPDATE_CONNECTION_LOST;
                        finished = 1;
                        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                                      "send_updates - %s: Failed to send update operation to consumer (uniqueid %s, CSN %s): "
                                      "%s. Will retry later.\n",
                                      agmt_get_long_name(prp->agmt),
                                      entry.op->target_address.uniqueid, csn_str,
                                      error ? ldap_err2string(error) : "Connection lost");
                    } else if (CONN_TIMEOUT == replay_crc) {
                        return_value = UPDATE_TIMEOUT;
                        finished = 1;
                        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                                      "send_updates - %s: Timed out sending update operation to consumer (uniqueid %s, CSN %s): "
                                      "%s.\n",
                                      agmt_get_long_name(prp->agmt),
                                      entry.op->target_address.uniqueid, csn_str,
                                      error ? ldap_err2string(error) : "Timeout");
                    } else if (CONN_LOCAL_ERROR == replay_crc) {
                        /*
                         * Something bad happened on the local server - enter
                         * backoff state.
                         */
                        return_value = UPDATE_TRANSIENT_ERROR;
                        finished = 1;
                        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                                      "send_updates - %s: Failed to send update operation to consumer (uniqueid %s, CSN %s): "
                                      "Local error. Will retry later.\n",
                                      agmt_get_long_name(prp->agmt),
                                      entry.op->target_address.uniqueid, csn_str);
                    }

                } else {
                    char *uniqueid = NULL;
                    ReplicaId replica_id = 0;

                    csn_as_string(entry.op->csn, PR_FALSE, csn_str);
                    replica_id = csn_get_replicaid(entry.op->csn);
                    uniqueid = entry.op->target_address.uniqueid;

                    if (fractional_repl && message_id) {
                        /* This update was sent no need to update the subentry
                         * and restart counting the skipped updates
                         */
                        subentry_update_needed = PR_FALSE;
                        skipped_updates = 0;
                    }

                    if (prp->repl50consumer && message_id) {
                        int operation, error = 0;

                        conn_get_error(prp->conn, &operation, &error);

                        /* Get the response here */
                        replay_crc = repl5_inc_get_next_result(rd);
                        conn_get_error(prp->conn, &operation, &error);
                        csn_as_string(entry.op->csn, PR_FALSE, csn_str);
                        return_value = repl5_inc_update_from_op_result(prp, replay_crc, error, csn_str, uniqueid, replica_id, &finished, num_changes_sent);
                    } else if (message_id) {
                        /* Queue the details for pickup later in the response thread */
                        repl5_inc_operation *sop = NULL;
                        sop = repl5_inc_operation_new();
                        PL_strncpyz(sop->csn_str, csn_str, sizeof(sop->csn_str));
                        sop->ldap_message_id = message_id;
                        sop->operation_type = entry.op->operation_type;
                        sop->replica_id = replica_id;
                        PL_strncpyz(sop->uniqueid, uniqueid, sizeof(sop->uniqueid));
                        repl5_int_push_operation(rd, sop);
                        repl5_inc_flow_control_results(prp->agmt, rd);
                    } else {
                        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                      "send_updates - %s: Skipping update operation with no message_id (uniqueid %s, CSN %s):\n",
                                      agmt_get_long_name(prp->agmt),
                                      entry.op->target_address.uniqueid, csn_str);
                        agmt_inc_last_update_changecount(prp->agmt, csn_get_replicaid(entry.op->csn), 1 /*skipped*/);
                        if (fractional_repl) {
                            skipped_updates++;
                            if (skipped_updates > FRACTIONAL_SKIPPED_THRESHOLD) {
                                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                              "send_updates - %s: skipped updates is too high (%d) if no other update is sent we will update the subentry\n",
                                              agmt_get_long_name(prp->agmt), skipped_updates);
                                subentry_update_needed = PR_TRUE;
                            }
                        }
                    }
                }
                break;
            case CL5_BAD_DATA:
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "send_updates - %s: Invalid parameter passed to cl5GetNextOperationToReplay\n",
                              agmt_get_long_name(prp->agmt));
                agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                            "Invalid parameter passed to cl5GetNextOperationToReplay");
                return_value = UPDATE_FATAL_ERROR;
                finished = 1;
                break;
            case CL5_NOTFOUND:
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "send_updates - %s: No more updates to send (cl5GetNextOperationToReplay)\n",
                              agmt_get_long_name(prp->agmt));
                return_value = UPDATE_NO_MORE_UPDATES;
                finished = 1;
                break;
            case CL5_DB_ERROR:
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "send_updates - %s: A database error occurred (cl5GetNextOperationToReplay)\n",
                              agmt_get_long_name(prp->agmt));
                agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                            "Database error occurred while getting the next operation to replay");
                return_value = UPDATE_FATAL_ERROR;
                finished = 1;
                break;
            case CL5_BAD_FORMAT:
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                              "send_updates - %s: A malformed changelog entry was encountered (cl5GetNextOperationToReplay)\n",
                              agmt_get_long_name(prp->agmt));
                break;
            case CL5_MEMORY_ERROR:
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "send_updates - %s: A memory allocation error occurred (cl5GetNextOperationToReplay)\n",
                              agmt_get_long_name(prp->agmt));
                agmt_set_last_update_status(prp->agmt, 0, NSDS50_REPL_CL_ERROR,
                                            "Memory allocation error occurred (cl5GetNextOperationToReplay)");
                return_value = UPDATE_FATAL_ERROR;
                break;
            case CL5_IGNORE_OP:
                break;
            default:
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "send_updates - %s: Unknown error code (%d) returned from cl5GetNextOperationToReplay\n",
                              agmt_get_long_name(prp->agmt), rc);
                return_value = UPDATE_TRANSIENT_ERROR;
                break;
            }
            /* Check for protocol shutdown */
            if (prp->terminate) {
                return_value = UPDATE_NO_MORE_UPDATES;
                finished = 1;
            }
            if (*num_changes_sent >= MAX_CHANGES_PER_SESSION) {
                return_value = UPDATE_YIELD;
                finished = 1;
            }
            PR_Lock(rd->lock);
            /* See if the result thread has hit a problem */

            if (!finished && rd->abort_time) {
                time_t current_time = slapi_current_utc_time();
                if ((current_time - rd->abort_time) >= release_timeout) {
                    rd->result = UPDATE_YIELD;
                    return_value = UPDATE_YIELD;
                    finished = 1;
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                  "send_updates - Aborting send_updates...(%s)\n",
                                  agmt_get_long_name(rd->prp->agmt));
                }
            }

            if (!finished && rd->abort == ABORT_SESSION) {
                return_value = rd->result;
                finished = 1;
            }
            PR_Unlock(rd->lock);
        } while (!finished);

        if (fractional_repl && subentry_update_needed) {
            ReplicaId rid = -1; /* Used to create the replica keep alive subentry */
            Slapi_DN *replarea_sdn = NULL;

            if (replica) {
                rid = replica_get_rid(replica);
            }
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "send_updates - %s: skipped updates was definitely too high (%d) update the subentry now\n",
                          agmt_get_long_name(prp->agmt), skipped_updates);
            replarea_sdn = agmt_get_replarea(prp->agmt);
            if (!replarea_sdn) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "send_updates - Unknown replication area due to agreement not found.");
                agmt_set_last_update_status(prp->agmt, 0, -1, "Agreement is corrupted: missing suffix");
                return_value = UPDATE_FATAL_ERROR;
            } else {
                replica_subentry_update(replarea_sdn, rid);
            }
        }
        /* Terminate the results reading thread */
        if (!prp->repl50consumer) {
            /* We need to ensure that we wait until all the responses have been received from our operations */
            if (return_value != UPDATE_CONNECTION_LOST) {
                /*
                 * If we already have an error, there is no need to check the
                 * async result thread anymore.
                 */
                if (return_value == UPDATE_NO_MORE_UPDATES || return_value == UPDATE_YIELD) {
                    /*
                     * We need to double check that an error hasn't popped up from
                     * the async result thread since our last check.
                     */
                    int final_result;

                    rd->WaitForAsyncResults = agmt_get_WaitForAsyncResults(prp->agmt);
                    if ((final_result = repl5_inc_waitfor_async_results(rd))) {
                        return_value = final_result;
                    }
                }
            }

            rc = repl5_inc_destroy_async_result_thread(rd);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "%s: repl5_inc_run: "
                                                               "send_updates - repl5_inc_destroy_async_result_thread failed; error - %d\n",
                              agmt_get_long_name(prp->agmt), rc);
            }
            *num_changes_sent = rd->num_changes_sent;
        }
        PR_Lock(rd->lock);
        if (rd->flowcontrol_detection) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "send_updates - %s: Incremental update flow control triggered %d times\n"
                          "You may increase %s and/or decrease %s in the replica agreement configuration\n",
                          agmt_get_long_name(prp->agmt),
                          rd->flowcontrol_detection,
                          type_nsds5ReplicaFlowControlPause,
                          type_nsds5ReplicaFlowControlWindow);
        }
        PR_Unlock(rd->lock);
        repl5_inc_rd_destroy(&rd);

        cl5_operation_parameters_done(entry.op);
        cl5DestroyReplayIterator(&changelog_iterator, replica);
    }
    return return_value;
}

/*
 * XXXggood this should probably be in the superclass, since the full update
 * protocol is going to need it too.
 */
static int
repl5_inc_stop(Private_Repl_Protocol *prp)
{
    PRIntervalTime start, maxwait, now;
    PRUint64 timeout;
    int return_value;

    if ((timeout = agmt_get_protocol_timeout(prp->agmt)) == 0) {
        timeout = DEFAULT_PROTOCOL_TIMEOUT;
        if (prp->replica) {
            if ((timeout = replica_get_protocol_timeout(prp->replica)) == 0) {
                timeout = DEFAULT_PROTOCOL_TIMEOUT;
            }
        }
    }

    maxwait = PR_SecondsToInterval(timeout);
    prp->terminate = 1;
    event_notify(prp, EVENT_PROTOCOL_SHUTDOWN);
    start = PR_IntervalNow();
    now = start;
    while (!prp->stopped && ((now - start) < maxwait)) {
        DS_Sleep(PR_MillisecondsToInterval(100));
        now = PR_IntervalNow();
    }
    if (!prp->stopped) {
        /* Isn't listening. Do something drastic. */
        return_value = -1;
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "repl5_inc_stop - %s: Protocol does not stop after %" PRIu64 " seconds\n",
                      agmt_get_long_name(prp->agmt), timeout);
    } else {
        return_value = 0;
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "repl5_inc_stop - %s: Protocol stopped after %d seconds\n",
                      agmt_get_long_name(prp->agmt),
                      PR_IntervalToSeconds(now - start));
    }
    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        if (NULL == prp->replica) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "repl5_inc_stop - %s: Protocol replica is NULL\n",
                          agmt_get_long_name(prp->agmt));
        } else {
            if (NULL == prp->replica) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "repl5_inc_stop - %s:replica is NULL\n",
                              agmt_get_long_name(prp->agmt));
            } else {
                Object *ruv_obj = replica_get_ruv(prp->replica);
                if (NULL == ruv_obj) {
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                  "repl5_inc_stop - %s: rruv_obj is NULL\n",
                                  agmt_get_long_name(prp->agmt));
                } else {
                    /* object was acquired in replica_get_ruv */
                    RUV *ruv = (RUV *)object_get_data(ruv_obj);
                    if (NULL == ruv) {
                        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                      "repl5_inc_stop - %s: ruv is NULL\n",
                                      agmt_get_long_name(prp->agmt));

                    } else {
                        ruv_dump(ruv, "Database RUV", NULL);
                    }
                    object_release(ruv_obj);
                }
            }
        }
    }
    return return_value;
}

static int
repl5_inc_status(Private_Repl_Protocol *prp __attribute__((unused)))
{
    int return_value = 0;

    return return_value;
}

static void
repl5_inc_notify_update(Private_Repl_Protocol *prp)
{
    event_notify(prp, EVENT_TRIGGERING_CRITERIA_MET);
}

static void
repl5_inc_update_now(Private_Repl_Protocol *prp)
{
    event_notify(prp, EVENT_REPLICATE_NOW);
}

static void
repl5_inc_notify_agmt_changed(Private_Repl_Protocol *prp)
{
    event_notify(prp, EVENT_AGMT_CHANGED);
}

static void
repl5_inc_notify_window_opened(Private_Repl_Protocol *prp)
{
    event_notify(prp, EVENT_WINDOW_OPENED);
}

static void
repl5_inc_notify_window_closed(Private_Repl_Protocol *prp)
{
    event_notify(prp, EVENT_WINDOW_CLOSED);
}

Private_Repl_Protocol *
Repl_5_Inc_Protocol_new(Repl_Protocol *rp)
{
    repl5_inc_private *rip = NULL;
    Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_malloc(sizeof(Private_Repl_Protocol));
    prp->delete = repl5_inc_delete;
    prp->run = repl5_inc_run;
    prp->stop = repl5_inc_stop;
    prp->status = repl5_inc_status;
    prp->notify_update = repl5_inc_notify_update;
    prp->notify_agmt_changed = repl5_inc_notify_agmt_changed;
    prp->notify_window_opened = repl5_inc_notify_window_opened;
    prp->notify_window_closed = repl5_inc_notify_window_closed;
    prp->update_now = repl5_inc_update_now;
    prp->replica = prot_get_replica(rp);
    if ((prp->lock = PR_NewLock()) == NULL) {
        goto loser;
    }
    if ((prp->cvar = PR_NewCondVar(prp->lock)) == NULL) {
        goto loser;
    }
    prp->stopped = 0;
    prp->terminate = 0;
    prp->eventbits = 0;
    prp->conn = prot_get_connection(rp);
    prp->agmt = prot_get_agreement(rp);
    prp->last_acquire_response_code = NSDS50_REPL_REPLICA_READY;
    rip = (void *)slapi_ch_malloc(sizeof(repl5_inc_private));
    rip->ruv = NULL;
    rip->backoff = NULL;
    rip->rp = rp;
    prp->private = (void *)rip;
    prp->replica_acquired = PR_FALSE;
    prp->repl50consumer = 0;
    prp->repl71consumer = 0;
    prp->repl90consumer = 0;
    return prp;
loser:
    repl5_inc_delete(&prp);
    return NULL;
}

static void
repl5_inc_backoff_expired(time_t timer_fire_time __attribute__((unused)), void *arg)
{
    Private_Repl_Protocol *prp = (Private_Repl_Protocol *)arg;
    PR_ASSERT(NULL != prp);
    event_notify(prp, EVENT_BACKOFF_EXPIRED);
}

/*
 * Examine the update vector and determine our course of action.
 * There are 3 different possibilities, plus a catch-all error:
 * 1 - no update vector (ruv is NULL). The consumer's replica is
 *     pristine, so it needs to be initialized. Return
 *     EXAMINE_RUV_PRISTINE_REPLICA.
 * 2 - ruv is present, but its database generation ID doesn't
 *     match the local generation ID. This means that either
 *     the local replica must be reinitialized from the remote
 *     replica or vice-versa. Return
 *     EXAMINE_RUV_GENERATION_MISMATCH.
 * 3 - ruv is present, and we have all updates needed to bring
 *     the replica up to date using the incremental protocol.
 *     return EXAMINE_RUV_OK.
 * 4 - parameter error. Return EXAMINE_RUV_PARAM_ERROR
 */
static int
examine_update_vector(Private_Repl_Protocol *prp, RUV *remote_ruv)
{
    int return_value;

    PR_ASSERT(NULL != prp);
    if (NULL == prp) {
        return_value = EXAMINE_RUV_PARAM_ERROR;
    } else if (NULL == remote_ruv) {
        return_value = EXAMINE_RUV_PRISTINE_REPLICA;
    } else {
        PR_ASSERT(NULL != prp->replica);
        if (replica_check_generation(prp->replica, remote_ruv)) {
            return_value = EXAMINE_RUV_OK;
        } else {
            return_value = EXAMINE_RUV_GENERATION_MISMATCH;
        }
    }
    return return_value;
}

/*
 * When we get an error from an LDAP operation, we call this
 * function to decide if we should just keep replaying
 * updates, or if we should stop, back off, and try again
 * later.
 * Returns PR_TRUE if we shoould keep going, PR_FALSE if
 * we should back off and try again later.
 *
 * In general, we keep going if the return code is consistent
 * with some sort of bug in URP that causes the consumer to
 * emit an error code that it shouldn't have, e.g. LDAP_ALREADY_EXISTS.
 *
 * We stop if there's some indication that the server just completely
 * failed to process the operation, e.g. LDAP_OPERATIONS_ERROR.
 */
PRBool
ignore_error_and_keep_going(int error)
{
    int return_value = PR_FALSE;

    switch (error) {
    /* Cases where we keep going */
    case LDAP_SUCCESS:
    case LDAP_NO_SUCH_ATTRIBUTE:
    case LDAP_UNDEFINED_TYPE:
    case LDAP_CONSTRAINT_VIOLATION:
    case LDAP_TYPE_OR_VALUE_EXISTS:
    case LDAP_INVALID_SYNTAX:
    case LDAP_NO_SUCH_OBJECT:
    case LDAP_INVALID_DN_SYNTAX:
    case LDAP_IS_LEAF:
    case LDAP_INSUFFICIENT_ACCESS:
    case LDAP_NAMING_VIOLATION:
    case LDAP_OBJECT_CLASS_VIOLATION:
    case LDAP_NOT_ALLOWED_ON_NONLEAF:
    case LDAP_NOT_ALLOWED_ON_RDN:
    case LDAP_ALREADY_EXISTS:
    case LDAP_NO_OBJECT_CLASS_MODS:
        return_value = PR_TRUE;
        break;

    /* Cases where we stop and retry */
    case LDAP_OPERATIONS_ERROR:
    case LDAP_PROTOCOL_ERROR:
    case LDAP_TIMELIMIT_EXCEEDED:
    case LDAP_SIZELIMIT_EXCEEDED:
    case LDAP_STRONG_AUTH_NOT_SUPPORTED:
    case LDAP_STRONG_AUTH_REQUIRED:
    case LDAP_PARTIAL_RESULTS:
    case LDAP_REFERRAL:
    case LDAP_ADMINLIMIT_EXCEEDED:
    case LDAP_UNAVAILABLE_CRITICAL_EXTENSION:
    case LDAP_CONFIDENTIALITY_REQUIRED:
    case LDAP_SASL_BIND_IN_PROGRESS:
    case LDAP_INAPPROPRIATE_MATCHING:
    case LDAP_ALIAS_PROBLEM:
    case LDAP_ALIAS_DEREF_PROBLEM:
    case LDAP_INAPPROPRIATE_AUTH:
    case LDAP_INVALID_CREDENTIALS:
    case LDAP_BUSY:
    case LDAP_UNAVAILABLE:
    case LDAP_UNWILLING_TO_PERFORM:
    case LDAP_LOOP_DETECT:
    case LDAP_SORT_CONTROL_MISSING:
    case LDAP_INDEX_RANGE_ERROR:
    case LDAP_RESULTS_TOO_LARGE:
    case LDAP_AFFECTS_MULTIPLE_DSAS:
    case LDAP_OTHER:
    case LDAP_SERVER_DOWN:
    case LDAP_LOCAL_ERROR:
    case LDAP_ENCODING_ERROR:
    case LDAP_DECODING_ERROR:
    case LDAP_TIMEOUT:
    case LDAP_AUTH_UNKNOWN:
    case LDAP_FILTER_ERROR:
    case LDAP_USER_CANCELLED:
    case LDAP_PARAM_ERROR:
    case LDAP_NO_MEMORY:
    case LDAP_CONNECT_ERROR:
    case LDAP_NOT_SUPPORTED:
    case LDAP_CONTROL_NOT_FOUND:
    case LDAP_NO_RESULTS_RETURNED:
    case LDAP_MORE_RESULTS_TO_RETURN:
    case LDAP_CLIENT_LOOP:
    case LDAP_REFERRAL_LIMIT_EXCEEDED:
        return_value = PR_FALSE;
        break;
    }
    return return_value;
}

/* this function converts a state to its name - for debug output */
static const char *
state2name(int state)
{
    switch (state) {
    case STATE_START:
        return "start";
    case STATE_WAIT_WINDOW_OPEN:
        return "wait_for_window_to_open";
    case STATE_WAIT_CHANGES:
        return "wait_for_changes";
    case STATE_READY_TO_ACQUIRE:
        return "ready_to_acquire_replica";
    case STATE_BACKOFF_START:
        return "start_backoff";
    case STATE_BACKOFF:
        return "backoff";
    case STATE_SENDING_UPDATES:
        return "sending_updates";
    case STATE_STOP_FATAL_ERROR:
        return "stop_fatal_error";
    case STATE_STOP_FATAL_ERROR_PART2:
        return "stop_fatal_error";
    case STATE_STOP_NORMAL_TERMINATION:
        return "stop_normal_termination";
    default:
        return "invalid_state";
    }
}

/* this function convert s an event to its name - for debug output */
static const char *
event2name(int event)
{
    switch (event) {
    case EVENT_WINDOW_OPENED:
        return "update_window_opened";
    case EVENT_WINDOW_CLOSED:
        return "update_window_closed";
    case EVENT_TRIGGERING_CRITERIA_MET:
        return "data_modified";
    case EVENT_BACKOFF_EXPIRED:
        return "backoff_timer_expired";
    case EVENT_REPLICATE_NOW:
        return "replicate_now";
    case EVENT_PROTOCOL_SHUTDOWN:
        return "protocol_shutdown";
    case EVENT_AGMT_CHANGED:
        return "agreement_changed";
    default:
        return "invalid_event";
    }
}

static const char *
op2string(int op)
{
    switch (op) {
    case SLAPI_OPERATION_ADD:
        return "add";
    case SLAPI_OPERATION_MODIFY:
        return "modify";
    case SLAPI_OPERATION_DELETE:
        return "delete";
    case SLAPI_OPERATION_MODRDN:
        return "rename";
    case SLAPI_OPERATION_EXTENDED:
        return "extended";
    }

    return "unknown";
}

void
repl5_set_backoff_min(Private_Repl_Protocol *prp, int min)
{
    if (prp->replica) {
        replica_set_backoff_min(prp->replica, min);
    }
}

void
repl5_set_backoff_max(Private_Repl_Protocol *prp, int max)
{
    if (prp->replica) {
        replica_set_backoff_max(prp->replica, max);
    }
}

int
repl5_get_backoff_min(Private_Repl_Protocol *prp)
{
    if (prp->replica) {
        return (int)replica_get_backoff_min(prp->replica);
    }
    return PROTOCOL_BACKOFF_MINIMUM;
}

int
repl5_get_backoff_max(Private_Repl_Protocol *prp)
{
    if (prp->replica) {
        return (int)replica_get_backoff_max(prp->replica);
    }
    return PROTOCOL_BACKOFF_MAXIMUM;
}
