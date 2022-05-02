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


/* repl5_connection.c */
/*

 The connection object manages a connection to a single replication
 consumer.

XXXggood what to do on timeout? If we close connection, then we won't leave a
replica locked. Seems like right thing to do.
*/

#include "repl5.h"
#include "repl5_prot_private.h"
#include "slapi-private.h"
#include "ldap.h"
#include "nspr.h"
#include "private/pprio.h"
#include "nss.h"

typedef struct repl_connection
{
    char *hostname;
    int port;
    char *binddn;
    int bindmethod;
    int state;
    int last_operation;
    int last_ldap_error;
    const char *status;
    char *last_ldap_errmsg;
    PRUint32 transport_flags;
    LDAP *ld;
    int supports_ldapv3;    /* 1 if does, 0 if doesn't, -1 if not determined */
    int supports_ds50_repl; /* 1 if does, 0 if doesn't, -1 if not determined */
    int supports_ds40_repl; /* 1 if does, 0 if doesn't, -1 if not determined */
    int supports_ds71_repl; /* 1 if does, 0 if doesn't, -1 if not determined */
    int supports_ds90_repl; /* 1 if does, 0 if doesn't, -1 if not determined */
    int linger_time;        /* time in seconds to leave an idle connection open */
    PRBool linger_active;
    Slapi_Eq_Context *linger_event;
    PRBool delete_after_linger;
    int refcnt;
    const Repl_Agmt *agmt;
    PRLock *lock;
    struct timeval timeout;
    int flag_agmt_changed;
    char *plain;
    void *tot_init_callback; /* Used during total update to do flow control */
} repl_connection;

/* #define DEFAULT_LINGER_TIME (5 * 60) */ /* 5 minutes */
#define DEFAULT_LINGER_TIME (60)

/*** from proto-slap.h ***/
int schema_objectclasses_superset_check(struct berval **remote_schema, char *type);
int schema_attributetypes_superset_check(struct berval **remote_schema, char *type);
void supplier_learn_new_definitions(struct berval **objectclasses, struct berval **attributetypes);
/* Controls we add on every outbound operation */

static LDAPControl manageDSAITControl = {LDAP_CONTROL_MANAGEDSAIT, {0, ""}, '\0'};
static int attribute_string_value_present(LDAP *ld, LDAPMessage *entry, const char *type, const char *value);
static int bind_and_check_pwp(Repl_Connection *conn, char *binddn, char *password);
static int32_t conn_connect_with_bootstrap(Repl_Connection *conn, PRBool bootstrap);

static int s_debug_timeout = 0;
static int s_debug_level = 0;
static Slapi_Eq_Context repl5_start_debug_timeout(int *setlevel);
static void repl5_stop_debug_timeout(Slapi_Eq_Context eqctx, int *setlevel);
static void repl5_debug_timeout_callback(time_t when, void *arg);

#define STATE_CONNECTED 600
#define STATE_DISCONNECTED 601

#define STATUS_DISCONNECTED "disconnected"
#define STATUS_CONNECTED "connected"
#define STATUS_PROCESSING_ADD "processing add operation"
#define STATUS_PROCESSING_DELETE "processing delete operation"
#define STATUS_PROCESSING_MODIFY "processing modify operation"
#define STATUS_PROCESSING_RENAME "processing rename operation"
#define STATUS_PROCESSING_EXTENDED_OPERATION "processing extended operation"
#define STATUS_LINGERING "lingering"
#define STATUS_SHUTTING_DOWN "shutting down"
#define STATUS_BINDING "connecting and binding"
#define STATUS_SEARCHING "processing search operation"

#define CONN_NO_OPERATION 0
#define CONN_ADD 1
#define CONN_DELETE 2
#define CONN_MODIFY 3
#define CONN_RENAME 4
#define CONN_EXTENDED_OPERATION 5
#define CONN_BIND 6
#define CONN_INIT 7

/* These are errors returned from ldap operations which should cause us to disconnect and
   retry the connection later */
#define IS_DISCONNECT_ERROR(rc) (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR || rc == LDAP_INVALID_CREDENTIALS || rc == LDAP_INAPPROPRIATE_AUTH || rc == LDAP_LOCAL_ERROR)

/* Forward declarations */
static void close_connection_internal(Repl_Connection *conn);
static void conn_delete_internal(Repl_Connection *conn);


/* converts consumer's response to a string */
char *
conn_result2string(int result)
{
    switch (result) {
    case CONN_OPERATION_SUCCESS:
        return "operation success";
    case CONN_OPERATION_FAILED:
        return "operation failure";
    case CONN_NOT_CONNECTED:
        return "not connected";
    case CONN_SUPPORTS_DS5_REPL:
        return "consumer supports all DS5 extop";
    case CONN_DOES_NOT_SUPPORT_DS5_REPL:
        return "consumer does not support all DS5 extop";
    case CONN_SCHEMA_UPDATED:
        return "consumer schema updated";
    case CONN_SCHEMA_NO_UPDATE_NEEDED:
        return "consumer schema up to date";
    case CONN_LOCAL_ERROR:
        return "local error";
    case CONN_BUSY:
        return "consumer is busy";
    case CONN_SSL_NOT_ENABLED:
        return "NSS not initialized";
    case CONN_TIMEOUT:
        return "time out";
    case CONN_SUPPORTS_DS71_REPL:
        return "consumer supports all DS71 extop";
    case CONN_DOES_NOT_SUPPORT_DS71_REPL:
        return "consumer does not support all DS7.1 extop";
    case CONN_IS_READONLY:
        return "consumer is read only";
    case CONN_IS_NOT_READONLY:
        return "consumer is not read only";
    case CONN_SUPPORTS_DIRSYNC:
        return "consumer supports DIRSYNC control";
    case CONN_DOES_NOT_SUPPORT_DIRSYNC:
        return "consumer does not support DIRSYNC control";
    case CONN_IS_WIN2K3:
        return "consumer is W2K3 or after";
    case CONN_NOT_WIN2K3:
        return "consumer is before W2K3";
    case CONN_SUPPORTS_DS90_REPL:
        return "consumer supports all DS90 extop";
    case CONN_DOES_NOT_SUPPORT_DS90_REPL:
        return "consumer does not support all DS90 extop";
    default:
        return NULL;
    }
}
/*
 * Create a new connection object. Returns a pointer to the object, or
 * NULL if an error occurs.
 */
Repl_Connection *
conn_new(Repl_Agmt *agmt)
{
    Repl_Connection *rpc;

    rpc = (Repl_Connection *)slapi_ch_malloc(sizeof(repl_connection));
    if ((rpc->lock = PR_NewLock()) == NULL) {
        goto loser;
    }
    rpc->hostname = agmt_get_hostname(agmt);
    rpc->port = agmt_get_port(agmt);
    rpc->binddn = agmt_get_binddn(agmt);
    rpc->bindmethod = agmt_get_bindmethod(agmt);
    rpc->transport_flags = agmt_get_transport_flags(agmt);
    rpc->ld = NULL;
    rpc->state = STATE_DISCONNECTED;
    rpc->last_operation = CONN_NO_OPERATION;
    rpc->last_ldap_error = LDAP_SUCCESS;
    rpc->last_ldap_errmsg = NULL;
    rpc->supports_ldapv3 = -1;
    rpc->supports_ds40_repl = -1;
    rpc->supports_ds50_repl = -1;
    rpc->supports_ds71_repl = -1;
    rpc->supports_ds90_repl = -1;

    rpc->linger_active = PR_FALSE;
    rpc->delete_after_linger = PR_FALSE;
    rpc->linger_event = NULL;
    rpc->linger_time = DEFAULT_LINGER_TIME;
    rpc->status = STATUS_DISCONNECTED;
    rpc->agmt = agmt;
    rpc->refcnt = 1;
    rpc->timeout.tv_sec = agmt_get_timeout(agmt);
    rpc->timeout.tv_usec = 0;
    rpc->flag_agmt_changed = 0;
    rpc->plain = NULL;
    return rpc;
loser:
    conn_delete_internal(rpc);
    slapi_ch_free((void **)&rpc);
    return NULL;
}

static PRBool
conn_connected_locked(Repl_Connection *conn, int locked)
{
    PRBool return_value;

    if (!locked)
        PR_Lock(conn->lock);
    return_value = STATE_CONNECTED == conn->state;
    if (!locked)
        PR_Unlock(conn->lock);

    return return_value;
}

/*
 * Return PR_TRUE if the connection is in the connected state
 */
static PRBool
conn_connected(Repl_Connection *conn)
{
    return conn_connected_locked(conn, 1);
}

/*
 * Destroy a connection object.
 */
static void
conn_delete_internal(Repl_Connection *conn)
{
    PR_ASSERT(NULL != conn);
    close_connection_internal(conn);
    /* slapi_ch_free accepts NULL pointer */
    slapi_ch_free_string(&conn->last_ldap_errmsg);
    slapi_ch_free((void **)&conn->hostname);
    slapi_ch_free((void **)&conn->binddn);
    slapi_ch_free((void **)&conn->plain);
}

/*
 *  Used by CLEANALLRUV - free it all!
 */
void
conn_delete_internal_ext(Repl_Connection *conn)
{
    conn_delete_internal(conn);
    PR_DestroyLock(conn->lock);
    slapi_ch_free((void **)&conn);
}

/*
 * Destroy a connection. It is an error to use the connection object
 * after conn_delete() has been called.
 */
void
conn_delete(Repl_Connection *conn)
{
    PRBool destroy_it = PR_FALSE;

    PR_ASSERT(NULL != conn);
    PR_Lock(conn->lock);
    if (conn->linger_active) {
        if (slapi_eq_cancel_rel(conn->linger_event) == 1) {
            /* Event was found and cancelled. Destroy the connection object. */
            destroy_it = PR_TRUE;
        } else {
            /*
             * The event wasn't found, but we think it's still active.
             * That means an event is in the process of being fired
             * off, so arrange for the event to destroy the object .
             */
            conn->delete_after_linger = PR_TRUE;
        }
    } else {
        destroy_it = PR_TRUE;
    }
    if (destroy_it) {
        PR_Unlock(conn->lock);
        conn_delete_internal_ext(conn);
    } else {
        PR_Unlock(conn->lock);
    }
}

void
conn_set_tot_update_cb_nolock(Repl_Connection *conn, void *cb_data)
{
    conn->tot_init_callback = (void *)cb_data;
}
void
conn_set_tot_update_cb(Repl_Connection *conn, void *cb_data)
{
    PR_Lock(conn->lock);
    conn_set_tot_update_cb_nolock(conn, cb_data);
    PR_Unlock(conn->lock);
}

void
conn_get_tot_update_cb_nolock(Repl_Connection *conn, void **cb_data)
{
    *cb_data = (void *)conn->tot_init_callback;
}
void
conn_get_tot_update_cb(Repl_Connection *conn, void **cb_data)
{
    PR_Lock(conn->lock);
    conn_get_tot_update_cb_nolock(conn, cb_data);
    PR_Unlock(conn->lock);
}

/*
 * Return the last operation type processed by the connection
 * object, and the LDAP error encountered.
 */
void
conn_get_error(Repl_Connection *conn, int *operation, int *error)
{
    PR_Lock(conn->lock);
    *operation = conn->last_operation;
    *error = conn->last_ldap_error;
    PR_Unlock(conn->lock);
}

/*
 * Return the last operation type processed by the connection
 * object, and the LDAP error encountered.
 * Beware that the error string will only be in scope and valid
 * before the next operation result has been read from the connection
 * (so don't alias the pointer).
 */
void
conn_get_error_ex(Repl_Connection *conn, int *operation, int *error, char **error_string)
{
    PR_Lock(conn->lock);
    *operation = conn->last_operation;
    *error = conn->last_ldap_error;
    *error_string = conn->last_ldap_errmsg;
    PR_Unlock(conn->lock);
}

/* Returns the result (asyncronously) from an opertation and also returns that operations message ID */
/* The _ex version handles a bunch of parameters (retoidp et al) that were present in the original
 * sync operation functions, but were never actually used) */
ConnResult
conn_read_result_ex(Repl_Connection *conn, char **retoidp, struct berval **retdatap, LDAPControl ***returned_controls, int send_msgid, int *resp_msgid, int block)
{
    LDAPMessage *res = NULL;
    int setlevel = 0;
    int rc = 0;
    int return_value = 0;
    LDAPControl **loc_returned_controls = NULL;
    struct timeval local_timeout = {0};
    time_t time_now = 0;
    time_t start_time = time(NULL);
    int backoff_time = 1;
    Slapi_Eq_Context eqctx = repl5_start_debug_timeout(&setlevel);

    /* Here, we want to not block inside ldap_result().
             * Reason is that blocking there will deadlock with a
             * concurrent sender. We send concurrently, and hence
             * blocking is not good : deadlock results.
             * So, instead, we call ldap_result() with a zero timeout.
             * This makes it do a non-blocking poll and return to us
             * if there's no data to read.
             * We can then handle our timeout here by sleeping and re-trying.
             * In order that we do pickup results reasonably quickly,
             * we implement a backoff algorithm for the sleep: if we
             * keep getting results quickly then we won't spend much time sleeping.
             */

    while (!slapi_is_shutting_down()) {
        /* we have to make sure the update sending thread does not
                   attempt to close connection while we are reading
                   results - so lock the conn while we get the results */
        PR_Lock(conn->lock);

        if ((STATE_CONNECTED != conn->state) || !conn->ld) {
            rc = -1;
            return_value = CONN_NOT_CONNECTED;
            PR_Unlock(conn->lock);
            break;
        }
        rc = ldap_result(conn->ld, send_msgid, 1, &local_timeout, &res);

        PR_Unlock(conn->lock);

        if (0 != rc) {
            /* Something other than a timeout happened */
            break;
        }
        if (block) {
            /* Did the connection's timeout expire ? */
            time_now = slapi_current_rel_time_t();
            if (conn->timeout.tv_sec <= (time_now - start_time)) {
                /* We timed out */
                rc = 0;
                break;
            }
            /* Otherwise we backoff */
            DS_Sleep(PR_MillisecondsToInterval(backoff_time));
            if (backoff_time < 1000) {
                backoff_time <<= 1;
            }
        } else {
            rc = 0;
            break;
        }
    }

    repl5_stop_debug_timeout(eqctx, &setlevel);

    PR_Lock(conn->lock);
    /* we have to check again since the connection may have
               been closed in the meantime
               acquire the lock for the rest of the function
               to protect against another attempt to close
               the conn while we are using it
            */
    if ((STATE_CONNECTED != conn->state) || !conn->ld) {
        rc = -1;
        return_value = CONN_NOT_CONNECTED;
    }

    if (0 == rc) {
        /* Timeout */
        rc = slapi_ldap_get_lderrno(conn->ld, NULL, NULL);
        conn->last_ldap_error = LDAP_TIMEOUT;
        return_value = CONN_TIMEOUT;
    } else if ((-1 == rc) && (CONN_NOT_CONNECTED == return_value)) {
        /* must not access conn->ld if disconnected in another thread */
        /* the other thread that actually did the conn_disconnect() */
        /* will set the status and error info */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_read_result_ex - %s: Connection disconnected by another thread\n",
                      agmt_get_long_name(conn->agmt));
    } else if (-1 == rc) {
        /* Error */
        char *s = NULL;

        rc = slapi_ldap_get_lderrno(conn->ld, NULL, &s);
        slapi_ch_free_string(&conn->last_ldap_errmsg);
        conn->last_ldap_errmsg = s;
        conn->last_ldap_error = rc;
        /* some errors will require a disconnect and retry the connection
                   later */
        if (IS_DISCONNECT_ERROR(rc)) {
            close_connection_internal(conn); /* we already have the lock */
            return_value = CONN_NOT_CONNECTED;
        } else {
            conn->status = STATUS_CONNECTED;
            return_value = CONN_OPERATION_FAILED;
        }
    } else {
        int err;
        char *errmsg = NULL;
        char **referrals = NULL;
        char *matched = NULL;

        if (resp_msgid) {
            *resp_msgid = ldap_msgid(res);
        }

        rc = ldap_parse_result(conn->ld, res, &err, &matched,
                               &errmsg, &referrals, &loc_returned_controls,
                               0 /* Don't free the result */);
        if (IS_DISCONNECT_ERROR(rc)) {
            conn->last_ldap_error = rc;
            close_connection_internal(conn); /* we already have the lock */
            return_value = CONN_NOT_CONNECTED;
            goto done;
        } else if (IS_DISCONNECT_ERROR(err)) {
            conn->last_ldap_error = err;
            close_connection_internal(conn); /* we already have the lock */
            return_value = CONN_NOT_CONNECTED;
            goto done;
        }
        /* Got a result */
        if (retoidp /* total update */) {
            if (!((rc == LDAP_SUCCESS) && (err == LDAP_BUSY))) {
                if (rc == LDAP_SUCCESS) {
                    rc = ldap_parse_extended_result(conn->ld, res, retoidp,
                                                    retdatap, 0 /* Don't Free it */);
                }
                conn->last_ldap_error = rc;
                return_value = (LDAP_SUCCESS == conn->last_ldap_error ? CONN_OPERATION_SUCCESS : CONN_OPERATION_FAILED);
            }
        } else /* regular operation, result returned */
        {
            if (NULL != returned_controls) {
                *returned_controls = loc_returned_controls;
            }
            if (LDAP_SUCCESS != rc) {
                conn->last_ldap_error = rc;
            } else {
                conn->last_ldap_error = err;
            }
            return_value = LDAP_SUCCESS == conn->last_ldap_error ? CONN_OPERATION_SUCCESS : CONN_OPERATION_FAILED;
        }
        conn->status = STATUS_CONNECTED;
    done:
        slapi_ch_free_string(&errmsg);
        slapi_ch_free_string(&matched);
        charray_free(referrals);
    }
    if (res)
        ldap_msgfree(res);
    PR_Unlock(conn->lock); /* release the conn lock */
    return return_value;
}

ConnResult
conn_read_result(Repl_Connection *conn, int *message_id)
{
    return conn_read_result_ex(conn, NULL, NULL, NULL, LDAP_RES_ANY, message_id, 1);
}

/* Because the SDK isn't really thread-safe (it can deadlock between
 * a thread sending an operation and a thread trying to retrieve a response
 * on the same connection), we need to _first_ verify that the connection
 * is writable. If it isn't, we can deadlock if we proceed any further...
 */
/* openldap has LBER_SB_OPT_DATA_READY but that doesn't really
   work for our purposes - so we grab the openldap fd from the
   ber sockbuf layer, import it into a PR Poll FD, then
   do the poll
*/
static ConnResult
see_if_write_available(Repl_Connection *conn, PRIntervalTime timeout)
{
    PRFileDesc *pollfd = NULL;
    PRPollDesc polldesc;
    ber_socket_t fd = 0;
    int rc;

    /* get the sockbuf */
    if ((ldap_get_option(conn->ld, LDAP_OPT_DESC, &fd) != LDAP_OPT_SUCCESS) || (fd <= 0)) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "see_if_write_available - %s - *Invalid connection\n",
                      agmt_get_long_name(conn->agmt));
        conn->last_ldap_error = LDAP_PARAM_ERROR;
        return CONN_OPERATION_FAILED;
    }
    /* wrap the sockbuf fd with a NSPR FD created especially
       for use with polling, and only with polling */
    pollfd = PR_CreateSocketPollFd(fd);
    polldesc.fd = pollfd;
    polldesc.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
    polldesc.out_flags = 0;

    /* do the poll */
    rc = PR_Poll(&polldesc, 1, timeout);

    /* unwrap the socket */
    PR_DestroySocketPollFd(pollfd);

    /* check */
    if (rc == 0) { /* timeout */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "see_if_write_available - %s - poll timed out - poll interval [%d]\n",
                      agmt_get_long_name(conn->agmt),
                      timeout);
        return CONN_TIMEOUT;
    } else if ((rc < 0) || (!(polldesc.out_flags & PR_POLL_WRITE))) { /* error */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "see_if_write_available - %s - error during poll attempt [%d:%s]\n",
                      agmt_get_long_name(conn->agmt),
                      PR_GetError(), slapd_pr_strerror(PR_GetError()));
        conn->last_ldap_error = LDAP_PARAM_ERROR;
        return CONN_OPERATION_FAILED;
    }

    return CONN_OPERATION_SUCCESS;
}

/*
 * During a total update, this function checks how much entries
 * have been sent to the consumer without having received their acknowledgment.
 * Basically it checks how late is the consumer.
 *
 * If the consumer is too late, it pause the RA.sender (releasing the lock) to
 * let the consumer to catch up and RA.reader to receive the acknowledgments.
 *
 * Caller must hold conn->lock
 */
static void
check_flow_control_tot_init(Repl_Connection *conn, int optype, const char *extop_oid, int sent_msgid)
{
    int rcv_msgid;
    int once;

    if ((sent_msgid != 0) && (optype == CONN_EXTENDED_OPERATION) && (strcmp(extop_oid, REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID) == 0)) {
        /* We are sending entries part of the total update of a consumer
         * Wait a bit if the consumer needs to catchup from the current sent entries
         */
        rcv_msgid = repl5_tot_last_rcv_msgid(conn);
        if (rcv_msgid == -1) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "check_flow_control_tot_init - %s - Check_flow_control_tot_init no callback data [ msgid sent: %d]\n",
                          agmt_get_long_name(conn->agmt),
                          sent_msgid);
        } else if (sent_msgid < rcv_msgid) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "check_flow_control_tot_init - %s - Invalid message ids [ msgid sent: %d, rcv: %d]\n",
                          agmt_get_long_name(conn->agmt),
                          sent_msgid,
                          rcv_msgid);
        } else if ((sent_msgid - rcv_msgid) > agmt_get_flowcontrolwindow(conn->agmt)) {
            int totalUpdatePause;

            totalUpdatePause = agmt_get_flowcontrolpause(conn->agmt);
            if (totalUpdatePause) {
                /* The consumer is late. Last sent entry compare to last acknowledged entry
                 * overpass the allowed limit (flowcontrolwindow)
                 * Give some time to the consumer to catch up
                 */
                once = repl5_tot_flowcontrol_detection(conn, 1);
                PR_Unlock(conn->lock);
                if (once == 1) {
                    /* This is the first time we hit total update flow control.
                     * Log it at least once to inform administrator there is
                     * a potential configuration issue here
                     */
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                  "check_flow_control_tot_init - %s -  Total update flow control gives time (%d msec) to the consumer before sending more entries [ msgid sent: %d, rcv: %d])\n"
                                  "If total update fails you can try to increase %s and/or decrease %s in the replica agreement configuration\n",
                                  agmt_get_long_name(conn->agmt),
                                  totalUpdatePause,
                                  sent_msgid,
                                  rcv_msgid,
                                  type_nsds5ReplicaFlowControlPause,
                                  type_nsds5ReplicaFlowControlWindow);
                }
                DS_Sleep(PR_MillisecondsToInterval(totalUpdatePause));
                PR_Lock(conn->lock);
            }
        }
    }
}
/*
 * Test if the connection is available to do a write.
 * This function is doing a periodic polling of the connection.
 * If the polling times out:
 *  - it releases the connection lock (to let other thread ,i.e.
 *    replication result thread, the opportunity to use the connection)
 *  - Sleeps for a short period (100ms)
 *  - acquires the connection lock
 *
 * It loops until
 *  - it is available
 *  - exceeds RA complete timeout
 *  - server is shutdown
 *  - connection is disconnected (Disable, stop, delete the RA
 *    'terminate' the replication protocol and disconnect the connection)
 *
 * Return:
 *   - CONN_OPERATION_SUCCESS if the connection is available
 *   - CONN_TIMEOUT if the overall polling/sleeping delay exceeds RA timeout
 *   - CONN_NOT_CONNECTED if the replication connection state is disconnected
 *   - other ConnResult
 *
 * Caller must hold conn->Lock. At the exit, conn->lock is held
 */
static ConnResult
conn_is_available(Repl_Connection *conn)
{
    time_t poll_timeout_sec = 1;   /* Polling for 1sec */
    time_t yield_delay_msec = 100; /* Delay to wait */
    time_t start_time = slapi_current_rel_time_t();
    time_t time_now;
    ConnResult return_value = CONN_OPERATION_SUCCESS;

    while (!slapi_is_shutting_down() && (conn->state != STATE_DISCONNECTED)) {
        return_value = see_if_write_available(conn, PR_SecondsToInterval(poll_timeout_sec));
        if (return_value == CONN_TIMEOUT) {
            /* in case of timeout we return CONN_TIMEOUT only
             * if the RA.timeout is exceeded
             */
            time_now = slapi_current_rel_time_t();
            if (conn->timeout.tv_sec <= (time_now - start_time)) {
                break;
            } else {
                /* Else give connection to others threads */
                PR_Unlock(conn->lock);
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "conn_is_available - %s: transient timeout. retry)\n",
                              agmt_get_long_name(conn->agmt));
                DS_Sleep(PR_MillisecondsToInterval(yield_delay_msec));
                PR_Lock(conn->lock);
            }
        } else {
            break;
        }
    }
    if (conn->state == STATE_DISCONNECTED) {
        return_value = CONN_NOT_CONNECTED;
    }
    return return_value;
}
/*
 * Common code to send an LDAPv3 operation and collect the result.
 * Return values:
 * CONN_OPERATION_SUCCESS - the operation succeeded
 * CONN_OPERATION_FAILED - the operation was sent to the consumer
 * and failed. Use conn_get_error() to determine the LDAP error
 * code.
 * CONN_NOT_CONNECTED - no connection is active. The caller should
 * use conn_connect() to connect to the replica and bind, then should
 * reacquire the replica (if needed).
 * CONN_BUSY - the server is busy with previous requests, must wait for a while
 * before retrying
 * DBDB: also returns the operation's message ID, if it was successfully sent, now that
 * we're reading results async.
 */
static ConnResult
perform_operation(Repl_Connection *conn, int optype, const char *dn, LDAPMod **attrs, const char *newrdn, const char *newparent, int deleteoldrdn, LDAPControl *update_control, const char *extop_oid, struct berval *extop_payload, int *message_id)
{
    int rc = -1;
    ConnResult return_value = CONN_OPERATION_FAILED;
    LDAPControl *server_controls[3];
    /* LDAPControl **loc_returned_controls; */
    const char *op_string = NULL;
    int msgid = 0;

    server_controls[0] = &manageDSAITControl;
    server_controls[1] = update_control;
    server_controls[2] = NULL;

    /*
     * Lock the conn to prevent the result reader thread
     * from closing the connection out from under us.
     */
    PR_Lock(conn->lock);
    if (STATE_CONNECTED == conn->state) {
        int setlevel = 0;

        Slapi_Eq_Context eqctx = repl5_start_debug_timeout(&setlevel);

        return_value = conn_is_available(conn);
        if (return_value != CONN_OPERATION_SUCCESS) {
            PR_Unlock(conn->lock);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "perform_operation - %s - Connection is not available (%d)\n",
                          agmt_get_long_name(conn->agmt),
                          return_value);
            return return_value;
        }
        conn->last_operation = optype;
        switch (optype) {
        case CONN_ADD:
            conn->status = STATUS_PROCESSING_ADD;
            op_string = "add";
            rc = ldap_add_ext(conn->ld, dn, attrs, server_controls,
                              NULL /* clientctls */, &msgid);
            break;
        case CONN_MODIFY:
            conn->status = STATUS_PROCESSING_MODIFY;
            op_string = "modify";
            rc = ldap_modify_ext(conn->ld, dn, attrs, server_controls,
                                 NULL /* clientctls */, &msgid);
            break;
        case CONN_DELETE:
            conn->status = STATUS_PROCESSING_DELETE;
            op_string = "delete";
            rc = ldap_delete_ext(conn->ld, dn, server_controls,
                                 NULL /* clientctls */, &msgid);
            break;
        case CONN_RENAME:
            conn->status = STATUS_PROCESSING_RENAME;
            op_string = "rename";
            rc = ldap_rename(conn->ld, dn, newrdn, newparent, deleteoldrdn,
                             server_controls, NULL /* clientctls */, &msgid);
            break;
        case CONN_EXTENDED_OPERATION:
            conn->status = STATUS_PROCESSING_EXTENDED_OPERATION;
            op_string = "extended";
            rc = ldap_extended_operation(conn->ld, extop_oid, extop_payload,
                                         server_controls, NULL /* clientctls */, &msgid);
        }
        repl5_stop_debug_timeout(eqctx, &setlevel);
        if (LDAP_SUCCESS == rc) {
            /* DBDB: The code that used to be here has been moved for async operation
             * Results are now picked up in another thread. All we need to do here is
             * queue the operation details in the outstanding operation list.
             */
            return_value = CONN_OPERATION_SUCCESS;
        } else {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "perform_operation - %s: Failed to send %s operation: LDAP error %d (%s)\n",
                          agmt_get_long_name(conn->agmt),
                          op_string ? op_string : "NULL", rc, ldap_err2string(rc));
            conn->last_ldap_error = rc;
            if (IS_DISCONNECT_ERROR(rc)) {
                close_connection_internal(conn); /* already have the lock */
                return_value = CONN_NOT_CONNECTED;
            } else {
                conn->status = STATUS_CONNECTED;
                return_value = CONN_OPERATION_FAILED;
            }
        }
    } else {
        /* conn->last_ldap_error has been set to a more specific value
         * in the thread that did the disconnection
         * conn->last_ldap_error = LDAP_SERVER_DOWN;
         */
        return_value = CONN_NOT_CONNECTED;
    }

    check_flow_control_tot_init(conn, optype, extop_oid, msgid);

    PR_Unlock(conn->lock); /* release the lock */
    if (message_id) {
        *message_id = msgid;
    }
    return return_value;
}

/*
 * Send an LDAP add operation.
 */
ConnResult
conn_send_add(Repl_Connection *conn, const char *dn, LDAPMod **attrs, LDAPControl *update_control, int *message_id)
{
    return perform_operation(conn, CONN_ADD, dn, attrs, NULL /* newrdn */,
                             NULL /* newparent */, 0 /* deleteoldrdn */, update_control,
                             NULL /* extop OID */, NULL /* extop payload */, message_id);
}


/*
 * Send an LDAP delete operation.
 */
ConnResult
conn_send_delete(Repl_Connection *conn, const char *dn, LDAPControl *update_control, int *message_id)
{
    return perform_operation(conn, CONN_DELETE, dn, NULL /* attrs */,
                             NULL /* newrdn */, NULL /* newparent */, 0 /* deleteoldrdn */,
                             update_control, NULL /* extop OID */, NULL /* extop payload */, message_id);
}


/*
 * Send an LDAP modify operation.
 */
ConnResult
conn_send_modify(Repl_Connection *conn, const char *dn, LDAPMod **mods, LDAPControl *update_control, int *message_id)
{
    return perform_operation(conn, CONN_MODIFY, dn, mods, NULL /* newrdn */,
                             NULL /* newparent */, 0 /* deleteoldrdn */, update_control,
                             NULL /* extop OID */, NULL /* extop payload */, message_id);
}

/*
 * Send an LDAP moddn operation.
 */
ConnResult
conn_send_rename(Repl_Connection *conn, const char *dn, const char *newrdn, const char *newparent, int deleteoldrdn, LDAPControl *update_control, int *message_id)
{
    return perform_operation(conn, CONN_RENAME, dn, NULL /* attrs */,
                             newrdn, newparent, deleteoldrdn, update_control,
                             NULL /* extop OID */, NULL /* extop payload */, message_id);
}

/*
 * Send an LDAP extended operation.
 */
ConnResult
conn_send_extended_operation(Repl_Connection *conn, const char *extop_oid, struct berval *payload, LDAPControl *update_control, int *message_id)
{
    return perform_operation(conn, CONN_EXTENDED_OPERATION, NULL /* dn */, NULL /* attrs */,
                             NULL /* newrdn */, NULL /* newparent */, 0 /* deleteoldrdn */,
                             update_control, extop_oid, payload, message_id);
}

/*
 * Synchronously read an entry and return a specific attribute's values.
 * Returns CONN_OPERATION_SUCCESS if successful. Returns
 * CONN_OPERATION_FAILED if the operation was sent but an LDAP error
 * occurred (conn->last_ldap_error is set in this case), and
 * CONN_NOT_CONNECTED if no connection was active.
 *
 * The caller must free the returned_bvals.
 */
ConnResult
conn_read_entry_attribute(Repl_Connection *conn, const char *dn, char *type, struct berval ***returned_bvals)
{
    ConnResult return_value;
    int ldap_rc;
    LDAPControl *server_controls[2];
    LDAPMessage *res = NULL;
    char *attrs[2];

    PR_Lock(conn->lock);

    PR_ASSERT(NULL != type);
    if (conn_connected(conn)) {
        server_controls[0] = &manageDSAITControl;
        server_controls[1] = NULL;
        attrs[0] = type;
        attrs[1] = NULL;
        ldap_rc = ldap_search_ext_s(conn->ld, dn, LDAP_SCOPE_BASE,
                                    "(objectclass=*)", attrs, 0 /* attrsonly */,
                                    server_controls, NULL /* client controls */,
                                    &conn->timeout, 0 /* sizelimit */, &res);
        if (LDAP_SUCCESS == ldap_rc) {
            LDAPMessage *entry = ldap_first_entry(conn->ld, res);
            if (NULL != entry) {
                *returned_bvals = ldap_get_values_len(conn->ld, entry, type);
            }
            return_value = CONN_OPERATION_SUCCESS;
        } else if (IS_DISCONNECT_ERROR(ldap_rc)) {
            close_connection_internal(conn);
            return_value = CONN_NOT_CONNECTED;
        } else {
            return_value = CONN_OPERATION_FAILED;
        }
        conn->last_ldap_error = ldap_rc;
        if (NULL != res) {
            ldap_msgfree(res);
            res = NULL;
        }
    } else {
        return_value = CONN_NOT_CONNECTED;
    }
    PR_Unlock(conn->lock);

    return return_value;
}

/*
 * Return an pointer to a string describing the connection's status.
*/

const char *
conn_get_status(Repl_Connection *conn)
{
    return conn->status;
}

/*
 * Cancel any outstanding linger timer. Should be called when
 * a replication session is beginning.
 */
void
conn_cancel_linger(Repl_Connection *conn)
{
    PR_ASSERT(NULL != conn);
    PR_Lock(conn->lock);
    if (conn->linger_active) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_cancel_linger - %s - Canceling linger on the connection\n",
                      agmt_get_long_name(conn->agmt));
        conn->linger_active = PR_FALSE;
        if (slapi_eq_cancel_rel(conn->linger_event) == 1) {
            conn->refcnt--;
        }
        conn->linger_event = NULL;
        conn->status = STATUS_CONNECTED;
    } else {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_cancel_linger - %s - No linger to cancel on the connection\n",
                      agmt_get_long_name(conn->agmt));
    }
    PR_Unlock(conn->lock);
}

/*
 * Called when our linger timeout timer expires. This means
 * we should check to see if perhaps the connection's become
 * active again, in which case we do nothing. Otherwise,
 * we close the connection.
 */
static void
linger_timeout(time_t event_time __attribute__((unused)), void *arg)
{
    PRBool delete_now;
    Repl_Connection *conn = (Repl_Connection *)arg;

    PR_ASSERT(NULL != conn);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "linger_timeout - %s - Linger timeout has expired on the connection\n",
                  agmt_get_long_name(conn->agmt));
    PR_Lock(conn->lock);
    if (conn->linger_active) {
        conn->linger_active = PR_FALSE;
        conn->linger_event = NULL;
        close_connection_internal(conn);
    }
    delete_now = conn->delete_after_linger;
    PR_Unlock(conn->lock);
    if (delete_now) {
        conn_delete_internal(conn);
    }
}

/*
 * Indicate that a session is ending. The linger timer starts when
 * this function is called.
 */
void
conn_start_linger(Repl_Connection *conn)
{
    time_t now = slapi_current_rel_time_t();

    PR_ASSERT(NULL != conn);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "conn_start_linger -%s - Beginning linger on the connection\n",
                  agmt_get_long_name(conn->agmt));
    if (!conn_connected(conn)) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_start_linger - %s - No linger on the closed conn\n",
                      agmt_get_long_name(conn->agmt));
        return;
    }

    PR_Lock(conn->lock);
    if (conn->linger_active) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_start_linger - %s - Linger already active on the connection\n",
                      agmt_get_long_name(conn->agmt));
    } else {
        conn->linger_active = PR_TRUE;
        conn->linger_event = slapi_eq_once_rel(linger_timeout, conn, now + conn->linger_time);
        conn->status = STATUS_LINGERING;
    }
    PR_Unlock(conn->lock);
}

/*
 * If no connection is currently active, opens a connection and binds to
 * the remote server. If a connection is open (e.g. lingering) then
 * this is a no-op.
 *
 * Returns CONN_OPERATION_SUCCESS on success, or CONN_OPERATION_FAILED
 * on failure. Sets conn->last_ldap_error and conn->last_operation;
 */
ConnResult
conn_connect(Repl_Connection *conn)
{
    ConnResult return_value = CONN_OPERATION_SUCCESS;

    PR_Lock(conn->lock);

    /* Connection already open, just return SUCCESS */
    if (conn->state == STATE_CONNECTED) {
        PR_Unlock(conn->lock);
        return return_value;
    }
    return_value = conn_connect_with_bootstrap(conn, PR_FALSE);
    if (return_value != CONN_OPERATION_SUCCESS &&
        (conn->last_ldap_error == LDAP_INVALID_CREDENTIALS ||
         conn->last_ldap_error == LDAP_INAPPROPRIATE_AUTH ||
         conn->last_ldap_error == LDAP_NO_SUCH_OBJECT))
    {
        /* try the bootstrap credentials */
        return_value = conn_connect_with_bootstrap(conn, PR_TRUE);
    }

    PR_Unlock(conn->lock);

    return return_value;
}

/*
 * There are cases when using bind DN group credentials that the consumer does
 * not have the Bind DN group, or it is outdated.  In those cases we can try
 * use bootstrap credentials (if set) to attempt to authenticate again.  Each
 * new connection will always try the default credentials first, but if it
 * fails it will try the bootstrap settings as a backup to get things synched
 * up again, or "bootstrapped".
 */
static int32_t
conn_connect_with_bootstrap(Repl_Connection *conn, PRBool bootstrap)
{
    int32_t optdata;
    int32_t secure = 0;
    struct berval *creds = NULL;
    ConnResult return_value = CONN_OPERATION_SUCCESS;
    int32_t pw_ret = 1;

    if (bootstrap) {
        /* default credentials failed, try the bootstrap creds */
        char *binddn = NULL;

        if((binddn = agmt_get_bootstrap_binddn(conn->agmt)) == NULL) {
            /* There are no bootstrap settings, just return error */
            return CONN_OPERATION_FAILED;
        }
        slapi_ch_free_string(&conn->plain);
        slapi_ch_free_string(&conn->binddn);
        conn->binddn = binddn;
        creds = agmt_get_bootstrap_credentials(conn->agmt);
        conn->bindmethod = agmt_get_bootstrap_bindmethod(conn->agmt);
        conn->transport_flags = agmt_get_bootstrap_transport_flags(conn->agmt);
    } else {
        slapi_ch_free_string(&conn->binddn);
        conn->binddn = agmt_get_binddn(conn->agmt);
        creds = agmt_get_credentials(conn->agmt);
        conn->bindmethod = agmt_get_bindmethod(conn->agmt);
        conn->transport_flags = agmt_get_transport_flags(conn->agmt);
    }

    if (conn->flag_agmt_changed) {
        /* So far we cannot change Hostname */
        conn->timeout.tv_sec = agmt_get_timeout(conn->agmt);
        conn->flag_agmt_changed = 0;
        conn->port = agmt_get_port(conn->agmt); /* port could be updated */

    }

    if (conn->plain == NULL) {
        char *plain = NULL;

        /* kexcoff: for reversible encryption */
        /* We need to test the return code of pw_rever_decode in order to decide
         * if a free for plain will be needed (pw_ret == 0) or not (pw_ret != 0) */
        if (bootstrap) {
            pw_ret = pw_rever_decode(creds->bv_val, &plain, type_nsds5ReplicaBootstrapCredentials);
        } else {
            pw_ret = pw_rever_decode(creds->bv_val, &plain, type_nsds5ReplicaCredentials);;
        }
        /* Pb occured in decryption: stop now, binding will fail */
        if (pw_ret == -1) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "conn_connect - %s - Decoding of the credentials failed.\n",
                          agmt_get_long_name(conn->agmt));
            return_value = CONN_OPERATION_FAILED;
            conn->last_ldap_error = LDAP_INVALID_CREDENTIALS;
            conn->state = STATE_DISCONNECTED;
            goto done;
        } /* Else, does not mean that the plain is correct, only means the we had no internal
           decoding pb */
        conn->plain = slapi_ch_strdup(plain);
        if (!pw_ret) {
            slapi_ch_free_string(&plain);
        }
    }

    /* ugaston: if SSL has been selected in the replication agreement, SSL client
     * initialisation should be done before ever trying to open any connection at all.
     */
    if (conn->transport_flags == TRANSPORT_FLAG_STARTTLS) {
        secure = SLAPI_LDAP_INIT_FLAG_startTLS;
    } else if (conn->transport_flags == TRANSPORT_FLAG_LDAPS) {
        secure = SLAPI_LDAP_INIT_FLAG_SSL;
    }

    if (secure > 0) {
        if (!NSS_IsInitialized()) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "conn_connect - %s - SSL Not Initialized, Replication over SSL FAILED\n",
                          agmt_get_long_name(conn->agmt));
            conn->last_ldap_error = LDAP_INAPPROPRIATE_AUTH;
            conn->last_operation = CONN_INIT;
            return_value = CONN_SSL_NOT_ENABLED;
            goto done;
        }
    }

    if (return_value == CONN_OPERATION_SUCCESS) {
        /* Now we initialize the LDAP Structure and set options */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_connect - %s - Trying %s%s slapi_ldap_init_ext\n",
                      agmt_get_long_name(conn->agmt),
                      secure ? "secure" : "non-secure",
                      (secure == SLAPI_LDAP_INIT_FLAG_startTLS) ? " startTLS" : "");
        /* shared = 1 because we will read results from a second thread */
        if (conn->ld) {
            /* Since we call slapi_ldap_init, we must call slapi_ldap_unbind */
            /* ldap_unbind internally calls ldap_ld_free */
            slapi_ldap_unbind(conn->ld);
        }
        conn->ld = slapi_ldap_init_ext(NULL, conn->hostname, conn->port, secure, 1, NULL);
        if (NULL == conn->ld) {
            return_value = CONN_OPERATION_FAILED;
            conn->state = STATE_DISCONNECTED;
            conn->last_operation = CONN_INIT;
            conn->last_ldap_error = LDAP_LOCAL_ERROR;
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "conn_connect - %s - Failed to establish %s%sconnection to the consumer\n",
                          agmt_get_long_name(conn->agmt),
                          secure ? "secure " : "",
                          (secure == SLAPI_LDAP_INIT_FLAG_startTLS) ? "startTLS " : "");
            goto done;
        }

        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_connect - %s - binddn = %s,  passwd = %s\n",
                      agmt_get_long_name(conn->agmt),
                      conn->binddn ? conn->binddn : "NULL", creds->bv_val ? creds->bv_val : "NULL");

        /* Set some options for the connection. */
        optdata = LDAP_DEREF_NEVER; /* Don't dereference aliases */
        ldap_set_option(conn->ld, LDAP_OPT_DEREF, &optdata);

        optdata = LDAP_VERSION3; /* We need LDAP version 3 */
        ldap_set_option(conn->ld, LDAP_OPT_PROTOCOL_VERSION, &optdata);

        /* Don't chase any referrals (although we shouldn't get any) */
        ldap_set_option(conn->ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

        /* override the default timeout with the specified timeout */
        ldap_set_option(conn->ld, LDAP_OPT_NETWORK_TIMEOUT, &conn->timeout);
        /* We've got an ld. Now bind to the server. */
        conn->last_operation = CONN_BIND;
    }

    if (bind_and_check_pwp(conn, conn->binddn, conn->plain) == CONN_OPERATION_FAILED) {
        conn->last_ldap_error = slapi_ldap_get_lderrno(conn->ld, NULL, NULL);
        conn->state = STATE_DISCONNECTED;
        return_value = CONN_OPERATION_FAILED;
    } else {
        conn->last_ldap_error = LDAP_SUCCESS;
        conn->state = STATE_CONNECTED;
        return_value = CONN_OPERATION_SUCCESS;
    }

done:
    ber_bvfree(creds);

    if (bootstrap) {
        /* free "plain" so we use the default credentials on the next session */
        slapi_ch_free_string(&conn->plain);
    }
    if (return_value == CONN_OPERATION_SUCCESS) {
        conn->last_ldap_error = LDAP_SUCCESS;
        conn->state = STATE_CONNECTED;
    } else {
        close_connection_internal(conn);
    }

    return return_value;
}


static void
close_connection_internal(Repl_Connection *conn)
{
    conn->state = STATE_DISCONNECTED;
    conn->status = STATUS_DISCONNECTED;
    conn->supports_ds50_repl = -1;
    conn->supports_ds71_repl = -1;
    conn->supports_ds90_repl = -1;
    /* do this last, to minimize the chance that another thread
       might read conn->state as not disconnected and attempt
       to use conn->ld */
    if (NULL != conn->ld) {
        /* Since we call slapi_ldap_init, we must call slapi_ldap_unbind */
        slapi_ldap_unbind(conn->ld);
    }
    conn->ld = NULL;
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "close_connection_internal - %s - Disconnected from the consumer\n", agmt_get_long_name(conn->agmt));
}

void
conn_disconnect(Repl_Connection *conn)
{
    PR_ASSERT(NULL != conn);
    PR_Lock(conn->lock);
    close_connection_internal(conn);
    PR_Unlock(conn->lock);
}

/*
 * Determine if the remote replica supports DS 5.0 replication.
 * Return codes:
 * CONN_SUPPORTS_DS5_REPL - the remote replica suport DS5 replication
 * CONN_DOES_NOT_SUPPORT_DS5_REPL - the remote replica does not
 * support DS5 replication.
 * CONN_OPERATION_FAILED - it could not be determined if the remote
 * replica supports DS5 replication.
 * CONN_NOT_CONNECTED - no connection was active.
 */
ConnResult
conn_replica_supports_ds5_repl(Repl_Connection *conn)
{
    ConnResult return_value;
    int ldap_rc;

    PR_Lock(conn->lock);
    if (conn_connected(conn)) {
        if (conn->supports_ds50_repl == -1) {
            LDAPMessage *res = NULL;
            LDAPMessage *entry = NULL;
            char *attrs[] = {"supportedcontrol", "supportedextension", NULL};

            conn->status = STATUS_SEARCHING;
            ldap_rc = ldap_search_ext_s(conn->ld, "", LDAP_SCOPE_BASE,
                                        "(objectclass=*)", attrs, 0 /* attrsonly */,
                                        NULL /* server controls */, NULL /* client controls */,
                                        &conn->timeout, LDAP_NO_LIMIT, &res);
            if (LDAP_SUCCESS == ldap_rc) {
                conn->supports_ds50_repl = 0;
                entry = ldap_first_entry(conn->ld, res);
                if (!attribute_string_value_present(conn->ld, entry, "supportedcontrol", REPL_NSDS50_UPDATE_INFO_CONTROL_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
                } else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_START_NSDS50_REPLICATION_REQUEST_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
                } else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_END_NSDS50_REPLICATION_REQUEST_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
                } else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
                } else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_NSDS50_REPLICATION_RESPONSE_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
                } else {
                    conn->supports_ds50_repl = 1;
                    return_value = CONN_SUPPORTS_DS5_REPL;
                }
            } else {
                if (IS_DISCONNECT_ERROR(ldap_rc)) {
                    conn->last_ldap_error = ldap_rc; /* specific reason */
                    close_connection_internal(conn);
                    return_value = CONN_NOT_CONNECTED;
                } else {
                    return_value = CONN_OPERATION_FAILED;
                }
            }
            if (NULL != res)
                ldap_msgfree(res);
        } else {
            return_value = conn->supports_ds50_repl ? CONN_SUPPORTS_DS5_REPL : CONN_DOES_NOT_SUPPORT_DS5_REPL;
        }
    } else {
        /* Not connected */
        return_value = CONN_NOT_CONNECTED;
    }
    PR_Unlock(conn->lock);

    return return_value;
}

/*
 * Determine if the remote replica supports DS 7.1 replication.
 * Return codes:
 * CONN_SUPPORTS_DS71_REPL - the remote replica suport DS7.1 replication
 * CONN_DOES_NOT_SUPPORT_DS71_REPL - the remote replica does not
 * support DS7.1 replication.
 * CONN_OPERATION_FAILED - it could not be determined if the remote
 * replica supports DS5 replication.
 * CONN_NOT_CONNECTED - no connection was active.
 */
ConnResult
conn_replica_supports_ds71_repl(Repl_Connection *conn)
{
    ConnResult return_value;
    int ldap_rc;

    PR_Lock(conn->lock);
    if (conn_connected(conn)) {
        if (conn->supports_ds71_repl == -1) {
            LDAPMessage *res = NULL;
            LDAPMessage *entry = NULL;
            char *attrs[] = {"supportedcontrol", "supportedextension", NULL};

            conn->status = STATUS_SEARCHING;
            ldap_rc = ldap_search_ext_s(conn->ld, "", LDAP_SCOPE_BASE,
                                        "(objectclass=*)", attrs, 0 /* attrsonly */,
                                        NULL /* server controls */, NULL /* client controls */,
                                        &conn->timeout, LDAP_NO_LIMIT, &res);
            if (LDAP_SUCCESS == ldap_rc) {
                conn->supports_ds71_repl = 0;
                entry = ldap_first_entry(conn->ld, res);
                if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_NSDS71_REPLICATION_ENTRY_REQUEST_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS71_REPL;
                } else {
                    conn->supports_ds71_repl = 1;
                    return_value = CONN_SUPPORTS_DS71_REPL;
                }
            } else {
                if (IS_DISCONNECT_ERROR(ldap_rc)) {
                    conn->last_ldap_error = ldap_rc; /* specific reason */
                    close_connection_internal(conn);
                    return_value = CONN_NOT_CONNECTED;
                } else {
                    return_value = CONN_OPERATION_FAILED;
                }
            }
            if (NULL != res)
                ldap_msgfree(res);
        } else {
            return_value = conn->supports_ds71_repl ? CONN_SUPPORTS_DS71_REPL : CONN_DOES_NOT_SUPPORT_DS71_REPL;
        }
    } else {
        /* Not connected */
        return_value = CONN_NOT_CONNECTED;
    }
    PR_Unlock(conn->lock);

    return return_value;
}

/*
 * Determine if the remote replica supports DS 9.0 replication.
 * Return codes:
 * CONN_SUPPORTS_DS90_REPL - the remote replica suport DS5 replication
 * CONN_DOES_NOT_SUPPORT_DS90_REPL - the remote replica does not
 * support DS9.0 replication.
 * CONN_OPERATION_FAILED - it could not be determined if the remote
 * replica supports DS9.0 replication.
 * CONN_NOT_CONNECTED - no connection was active.
 */
ConnResult
conn_replica_supports_ds90_repl(Repl_Connection *conn)
{
    ConnResult return_value;
    int ldap_rc;

    PR_Lock(conn->lock);
    if (conn_connected(conn)) {
        if (conn->supports_ds90_repl == -1) {
            LDAPMessage *res = NULL;
            LDAPMessage *entry = NULL;
            char *attrs[] = {"supportedcontrol", "supportedextension", NULL};

            conn->status = STATUS_SEARCHING;
            ldap_rc = ldap_search_ext_s(conn->ld, "", LDAP_SCOPE_BASE,
                                        "(objectclass=*)", attrs, 0 /* attrsonly */,
                                        NULL /* server controls */, NULL /* client controls */,
                                        &conn->timeout, LDAP_NO_LIMIT, &res);
            if (LDAP_SUCCESS == ldap_rc) {
                conn->supports_ds90_repl = 0;
                entry = ldap_first_entry(conn->ld, res);
                if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_START_NSDS90_REPLICATION_REQUEST_OID)) {
                    return_value = CONN_DOES_NOT_SUPPORT_DS90_REPL;
                } else {
                    conn->supports_ds90_repl = 1;
                    return_value = CONN_SUPPORTS_DS90_REPL;
                }
            } else {
                if (IS_DISCONNECT_ERROR(ldap_rc)) {
                    conn->last_ldap_error = ldap_rc; /* specific reason */
                    close_connection_internal(conn);
                    return_value = CONN_NOT_CONNECTED;
                } else {
                    return_value = CONN_OPERATION_FAILED;
                }
            }
            if (NULL != res)
                ldap_msgfree(res);
        } else {
            return_value = conn->supports_ds90_repl ? CONN_SUPPORTS_DS90_REPL : CONN_DOES_NOT_SUPPORT_DS90_REPL;
        }
    } else {
        /* Not connected */
        return_value = CONN_NOT_CONNECTED;
    }
    PR_Unlock(conn->lock);

    return return_value;
}

/* Determine if the replica is read-only */
ConnResult
conn_replica_is_readonly(Repl_Connection *conn)
{
    ReplicaId rid = agmt_get_consumer_rid((Repl_Agmt *)conn->agmt, conn);
    if (rid == READ_ONLY_REPLICA_ID) {
        return CONN_IS_READONLY;
    } else {
        return CONN_IS_NOT_READONLY;
    }
}

/*
 * Return 1 if "value" is a value of attribute type "type" in entry "entry".
 * Otherwise, return 0.
 */
static int
attribute_string_value_present(LDAP *ld, LDAPMessage *entry, const char *type, const char *value)
{
    int return_value = 0;
    ber_len_t vallen;

    if (NULL != entry) {
        char *atype = NULL;
        BerElement *ber = NULL;

        vallen = strlen(value);
        atype = ldap_first_attribute(ld, entry, &ber);
        while (NULL != atype && 0 == return_value) {
            if (strcasecmp(atype, type) == 0) {
                struct berval **vals = ldap_get_values_len(ld, entry, atype);
                int i;
                for (i = 0; return_value == 0 && NULL != vals && NULL != vals[i]; i++) {
                    if ((vallen == vals[i]->bv_len) && !strncmp(vals[i]->bv_val, value, vallen)) {
                        return_value = 1;
                    }
                }
                if (NULL != vals) {
                    ldap_value_free_len(vals);
                }
            }
            ldap_memfree(atype);
            atype = ldap_next_attribute(ld, entry, ber);
        }
        if (NULL != ber)
            ber_free(ber, 0);
        /* The last atype has not been freed yet */
        if (NULL != atype)
            ldap_memfree(atype);
    }
    return return_value;
}

/* It returns the objectclasses and attributetypes of the remote schema
 * in the form of berval arrays.
 * In case of success, the caller must free those berval arrays with ber_bvecfree
 *  */
static ConnResult
supplier_read_consumer_definitions(Repl_Connection *conn, struct berval ***remote_objectclasses, struct berval ***remote_attributetypes)
{
    ConnResult return_value = CONN_OPERATION_SUCCESS;
    struct berval **remote_schema_objectclasses_bervals = NULL;
    struct berval **remote_schema_attributetypes_bervals = NULL;

    *remote_objectclasses = NULL;
    *remote_attributetypes = NULL;

    /* read the objectclass then the attribytetype from the remote schema */
    return_value = conn_read_entry_attribute(conn, "cn=schema", "objectclasses", &remote_schema_objectclasses_bervals);
    if (return_value == CONN_OPERATION_SUCCESS) {
        *remote_objectclasses = remote_schema_objectclasses_bervals;

        return_value = conn_read_entry_attribute(conn, "cn=schema", "attributetypes", &remote_schema_attributetypes_bervals);
        if (return_value == CONN_OPERATION_SUCCESS) {
            *remote_attributetypes = remote_schema_attributetypes_bervals;
        } else {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "%s: Fail to retrieve the remote schema attributetypes\n",
                          agmt_get_long_name(conn->agmt));
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "%s: Fail to retrieve the remote schema objectclasses\n",
                      agmt_get_long_name(conn->agmt));
    }

    if (return_value != CONN_OPERATION_SUCCESS) {
        /* in case of failure free everything */
        *remote_objectclasses = NULL;
        if (remote_schema_objectclasses_bervals) {
            ber_bvecfree(remote_schema_objectclasses_bervals);
        }

        *remote_attributetypes = NULL;
        if (remote_schema_attributetypes_bervals) {
            ber_bvecfree(remote_schema_attributetypes_bervals);
        }
    }
    return return_value;
}
//

static PRBool
update_consumer_schema(Repl_Connection *conn)
{
    struct berval **remote_schema_objectclasses_bervals = NULL;
    struct berval **remote_schema_attributetypes_bervals = NULL;
    PRBool ok_to_send_schema = PR_TRUE;

    if (supplier_read_consumer_definitions(conn, &remote_schema_objectclasses_bervals, &remote_schema_attributetypes_bervals) == CONN_OPERATION_SUCCESS) {
        if (schema_objectclasses_superset_check(remote_schema_objectclasses_bervals, OC_SUPPLIER) ||
            schema_attributetypes_superset_check(remote_schema_attributetypes_bervals, OC_SUPPLIER)) {

            /* The consumer contains definitions that needs to be learned */
            supplier_learn_new_definitions(remote_schema_objectclasses_bervals, remote_schema_attributetypes_bervals);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "update_consumer_schema - [S] Schema %s must not be overwritten (set replication log for additional info)\n",
                          agmt_get_long_name(conn->agmt));
            ok_to_send_schema = PR_FALSE;
        }
        ber_bvecfree(remote_schema_objectclasses_bervals);
        ber_bvecfree(remote_schema_attributetypes_bervals);
    } else {
        /* We can not be sure, be conservative and not send the schema */
        ok_to_send_schema = PR_FALSE;
    }
    return ok_to_send_schema;
}

/*
 * Read the remote server's schema entry, then read the local schema entry,
 * and compare the nsschemacsn attribute. If the local csn is newer, or
 * the remote csn is absent, push the schema down to the consumer.
 * Return codes:
 * CONN_SCHEMA_UPDATED if the schema was pushed successfully
 * CONN_SCHEMA_NO_UPDATE_NEEDED if the schema was as new or newer than
 * the local server's schema
 * CONN_OPERATION_FAILED if an error occurred
 * CONN_NOT_CONNECTED if no connection was active
 * NOTE: Should only be called when a replication session has been
 * established by sending a startReplication extended operation.
 */
ConnResult
conn_push_schema(Repl_Connection *conn, CSN **remotecsn)
{
    ConnResult return_value = CONN_OPERATION_SUCCESS;
    char *nsschemacsn = "nsschemacsn";
    Slapi_Entry **entries = NULL;
    Slapi_Entry *schema_entry = NULL;
    CSN *localcsn = NULL;
    Slapi_PBlock *spb = NULL;
    char localcsnstr[CSN_STRSIZE + 1] = {0};
    char remotecnsstr[CSN_STRSIZE + 1] = {0};

    if (!remotecsn) {
        return_value = CONN_OPERATION_FAILED;
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "conn_push_schema - NULL remote CSN\n");
    } else if (!conn_connected_locked(conn, 0 /* not locked */)) {
        return_value = CONN_NOT_CONNECTED;
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "conn_push_schema - %s: Schema replication update failed: not connected to consumer\n",
                      agmt_get_long_name(conn->agmt));
    } else {
        localcsn = dup_global_schema_csn();
        if (NULL == localcsn) {
            /* Local server has epoch CSN, so don't push schema */
            return_value = CONN_SCHEMA_NO_UPDATE_NEEDED;
        } else if (*remotecsn && csn_compare(localcsn, *remotecsn) <= 0) {
            /* Local server schema is not newer than the remote one */
            return_value = CONN_SCHEMA_NO_UPDATE_NEEDED;
        } else {
            if (*remotecsn) {
                csn_as_string(*remotecsn, PR_FALSE, remotecnsstr);
                csn_as_string(localcsn, PR_FALSE, localcsnstr);
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "conn_push_schema - [S] Checking consumer schema localcsn:%s / remotecsn:%s\n", localcsnstr, remotecnsstr);
            } else {
                csn_as_string(localcsn, PR_FALSE, localcsnstr);
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "conn_push_schema - [S] Checking consumer schema localcsn:%s / remotecsn:NULL\n", localcsnstr);
            }
            if (!update_consumer_schema(conn)) {
                /* At least one schema definition (attributetypes/objectclasses) of the consumer
                 * is a superset of the supplier.
                 * It is not possible push the schema immediately.
                 * Note: in update_consumer_schema, it may update the local supplier schema.
                 * So it could be possible that a second attempt (right now) of update_consumer_schema
                 * would be successful
                 */
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "conn_push_schema - [S] schema definitions may have been learned\n");
                if (!update_consumer_schema(conn)) {
                    slapi_log_err(SLAPI_LOG_REPL, "schema",
                                  "conn_push_schema - [S] learned definitions are not suffisant to try to push the schema \n");
                    return_value = CONN_OPERATION_FAILED;
                }
            }
            if (return_value == CONN_OPERATION_SUCCESS) {
                struct berval **remote_schema_csn_bervals = NULL;

                /* Get remote server's schema */
                return_value = conn_read_entry_attribute(conn, "cn=schema", nsschemacsn,
                                                         &remote_schema_csn_bervals);
                if (CONN_OPERATION_SUCCESS == return_value) {
                    if (NULL != remote_schema_csn_bervals && NULL != remote_schema_csn_bervals[0]) {
                        char remotecsnstr[CSN_STRSIZE + 1] = {0};
                        memcpy(remotecsnstr, remote_schema_csn_bervals[0]->bv_val,
                               remote_schema_csn_bervals[0]->bv_len);
                        remotecsnstr[remote_schema_csn_bervals[0]->bv_len] = '\0';
                        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                      "conn_push_schema - [S] Reread remotecsn:%s\n", remotecsnstr);
                        *remotecsn = csn_new_by_string(remotecsnstr);
                        if (*remotecsn && (csn_compare(localcsn, *remotecsn) <= 0)) {
                            return_value = CONN_SCHEMA_NO_UPDATE_NEEDED;
                        }
                        /* Need to free the remote_schema_csn_bervals */
                        ber_bvecfree(remote_schema_csn_bervals);
                    }
                    if (return_value == CONN_OPERATION_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                                      "conn_push_schema - Schema checking successful: ok to push the schema (%s)\n", agmt_get_long_name(conn->agmt));
                    }
                }
            }
        }
    }
    if (CONN_OPERATION_SUCCESS == return_value) {
        /* We know we need to push the schema out. */
        LDAPMod ocmod = {0};
        LDAPMod atmod = {0};
        LDAPMod csnmod = {0};
        LDAPMod *attrs[4] = {0};
        int numvalues = 0;
        Slapi_Attr *attr = NULL;
        char *csnvalues[2];

        ocmod.mod_type = "objectclasses";
        ocmod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        ocmod.mod_bvalues = NULL;
        atmod.mod_type = "attributetypes";
        atmod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        atmod.mod_bvalues = NULL;
        csnmod.mod_type = nsschemacsn;
        csnmod.mod_op = LDAP_MOD_REPLACE;
        csn_as_string(localcsn, PR_FALSE, localcsnstr);
        csnvalues[0] = localcsnstr;
        csnvalues[1] = NULL;
        csnmod.mod_values = csnvalues;
        attrs[0] = &ocmod;
        attrs[1] = &atmod;
        attrs[2] = &csnmod;
        attrs[3] = NULL;

        return_value = CONN_OPERATION_FAILED; /* assume failure */

        /* Get local schema */
        spb = slapi_search_internal("cn=schema", LDAP_SCOPE_BASE, "(objectclass=*)",
                                    NULL /* controls */, NULL /* schema_csn_attrs */, 0 /* attrsonly */);
        slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (NULL == entries || NULL == entries[0]) {
            /* Whoops - couldn't read our own schema! */
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "conn_push_schema - %s - Error: unable to read local schema definitions.\n",
                          agmt_get_long_name(conn->agmt));
            return_value = CONN_OPERATION_FAILED;
        } else {
            schema_entry = entries[0];
            if (slapi_entry_attr_find(schema_entry, "objectclasses", &attr) != -1) {
                int i, ind;
                Slapi_Value *value;
                slapi_attr_get_numvalues(attr, &numvalues);
                ocmod.mod_bvalues = (struct berval **)slapi_ch_malloc((numvalues + 1) *
                                                                      sizeof(struct berval *));
                for (i = 0, ind = slapi_attr_first_value(attr, &value);
                     ind != -1; ind = slapi_attr_next_value(attr, ind, &value), i++) {
                    /* XXXggood had to cast away const below */
                    ocmod.mod_bvalues[i] = (struct berval *)slapi_value_get_berval(value);
                }
                ocmod.mod_bvalues[numvalues] = NULL;
                if (slapi_entry_attr_find(schema_entry, "attributetypes", &attr) != -1) {
                    ConnResult result;
                    slapi_attr_get_numvalues(attr, &numvalues);
                    atmod.mod_bvalues = (struct berval **)slapi_ch_malloc((numvalues + 1) *
                                                                          sizeof(struct berval *));
                    for (i = 0, ind = slapi_attr_first_value(attr, &value);
                         ind != -1; ind = slapi_attr_next_value(attr, ind, &value), i++) {
                        /* XXXggood had to cast away const below */
                        atmod.mod_bvalues[i] = (struct berval *)slapi_value_get_berval(value);
                    }
                    atmod.mod_bvalues[numvalues] = NULL;

                    result = conn_send_modify(conn, "cn=schema", attrs, NULL, NULL); /* DBDB: this needs to be fixed to use async */
                    result = conn_read_result(conn, NULL);
                    switch (result) {
                    case CONN_OPERATION_FAILED: {
                        int ldaperr = -1, optype = -1;
                        conn_get_error(conn, &optype, &ldaperr);
                        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                      "%s: Schema replication update failed: %s\n",
                                      agmt_get_long_name(conn->agmt),
                                      ldaperr == -1 ? "Unknown Error" : ldap_err2string(ldaperr));
                        return_value = CONN_OPERATION_FAILED;
                        break;
                    }
                    case CONN_NOT_CONNECTED:
                        return_value = CONN_NOT_CONNECTED;
                        break;
                    case CONN_OPERATION_SUCCESS:
                        return_value = CONN_SCHEMA_UPDATED;
                        break;
                    default:
                        break;
                    }
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "conn_push_schema - %s - Schema replication update failed: "
                              "unable to prepare schema entry for transmission.\n",
                              agmt_get_long_name(conn->agmt));
            }
        }
        /* slapi_ch_free accepts NULL pointer */
        slapi_ch_free((void **)&ocmod.mod_bvalues);
        slapi_ch_free((void **)&atmod.mod_bvalues);
    }
    if (NULL != spb) {
        slapi_free_search_results_internal(spb);
        slapi_pblock_destroy(spb);
        spb = NULL;
    }
    if (NULL != localcsn) {
        csn_free(&localcsn);
    }

    return return_value;
}

void
conn_set_timeout(Repl_Connection *conn, long timeout)
{
    PR_ASSERT(NULL != conn);
    PR_ASSERT(timeout >= 0);
    PR_Lock(conn->lock);
    conn->timeout.tv_sec = timeout;
    PR_Unlock(conn->lock);
}

long
conn_get_timeout(Repl_Connection *conn)
{
    long retval = 0;
    PR_ASSERT(NULL != conn);
    retval = conn->timeout.tv_sec;
    return retval;
}

LDAP *
conn_get_ldap(Repl_Connection *conn)
{
    if (conn) {
        return conn->ld;
    } else {
        return NULL;
    }
}

void
conn_set_agmt_changed(Repl_Connection *conn)
{
    PR_ASSERT(NULL != conn);
    PR_Lock(conn->lock);
    if (NULL != conn->agmt)
        conn->flag_agmt_changed = 1;
    PR_Unlock(conn->lock);
}

static const char *
bind_method_to_mech(int bindmethod)
{
    switch (bindmethod) {
    case BINDMETHOD_SSL_CLIENTAUTH:
        return LDAP_SASL_EXTERNAL;
        break;
    case BINDMETHOD_SASL_GSSAPI:
        return "GSSAPI";
        break;
    case BINDMETHOD_SASL_DIGEST_MD5:
        return "DIGEST-MD5";
        break;
    default: /* anything else */
        return LDAP_SASL_SIMPLE;
    }

    return LDAP_SASL_SIMPLE;
}

const char *
conn_get_bindmethod(Repl_Connection *conn)
{
    return (bind_method_to_mech(conn->bindmethod));
}

/*
 * Check the result of an ldap BIND operation to see we it
 * contains the expiration controls
 * return: -1 error, not bound
 *          0, OK bind has succeeded
 */
static int
bind_and_check_pwp(Repl_Connection *conn, char *binddn, char *password)
{

    LDAPControl **ctrls = NULL;
    LDAP *ld = conn->ld;
    int rc;
    const char *mech = bind_method_to_mech(conn->bindmethod);

    rc = slapi_ldap_bind(conn->ld, binddn, password, mech, NULL,
                         &ctrls, &conn->timeout, NULL);

    if (rc == LDAP_SUCCESS) {
        if (conn->last_ldap_error != rc) {
            int log_level = SLAPI_LOG_INFO;
            if (conn->last_ldap_error == LDAP_LOCAL_ERROR) {
                /*
                 * Local errors are not logged by default, so when we recover
                 * from local errors we must also not log it by default.
                 */
                log_level = SLAPI_LOG_REPL;
            }
            conn->last_ldap_error = rc;
            slapi_log_err(log_level,
                          repl_plugin_name,
                          "bind_and_check_pwp - %s: Replication bind with %s auth resumed\n",
                          agmt_get_long_name(conn->agmt),
                          mech ? mech : "SIMPLE");
        }

        if (ctrls) {
            for (size_t i = 0; ctrls[i] != NULL; ++i) {
                if (!(strcmp(ctrls[i]->ldctl_oid, LDAP_CONTROL_PWEXPIRED))) {
                    /* Bind is successfull but password has expired */
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                  "bind_and_check_pwp - %s - Successfully bound %s to consumer, "
                                  "but password has expired on consumer.\n",
                                  agmt_get_long_name(conn->agmt), binddn);
                } else if (!(strcmp(ctrls[i]->ldctl_oid, LDAP_CONTROL_PWEXPIRING))) {
                    /* The password is expiring in n seconds */
                    if ((ctrls[i]->ldctl_value.bv_val != NULL) &&
                        (ctrls[i]->ldctl_value.bv_len > 0)) {
                        int password_expiring = atoi(ctrls[i]->ldctl_value.bv_val);
                        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                      "bind_and_check_pwp - %s - Successfully bound %s to consumer, "
                                      "but password is expiring on consumer in %d seconds.\n",
                                      agmt_get_long_name(conn->agmt), binddn, password_expiring);
                    }
                }
            }
            ldap_controls_free(ctrls);
        }

        return (CONN_OPERATION_SUCCESS);
    } else {
        ldap_controls_free(ctrls);
        /* Do not report the same error over and over again
         * unless replication level logging is enabled. */
        if (conn->last_ldap_error != rc) {
            char *errmsg = NULL;
            conn->last_ldap_error = rc;
            /* errmsg is a pointer directly into the ld structure - do not free */
            rc = slapi_ldap_get_lderrno(ld, NULL, &errmsg);
            slapi_log_err(rc == LDAP_LOCAL_ERROR ? SLAPI_LOG_REPL : SLAPI_LOG_ERR,
                          repl_plugin_name,
                          "bind_and_check_pwp - %s - Replication bind with %s auth failed: LDAP error %d (%s) (%s)\n",
                          agmt_get_long_name(conn->agmt),
                          mech ? mech : "SIMPLE", rc,
                          ldap_err2string(rc), errmsg ? errmsg : "");
            slapi_ch_free_string(&errmsg);
        } else {
            char *errmsg = NULL;
            /* errmsg is a pointer directly into the ld structure - do not free */
            rc = slapi_ldap_get_lderrno(ld, NULL, &errmsg);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "bind_and_check_pwp - %s - Replication bind with %s auth failed: LDAP error %d (%s) (%s)\n",
                          agmt_get_long_name(conn->agmt),
                          mech ? mech : "SIMPLE", rc,
                          ldap_err2string(rc), errmsg ? errmsg : "");
            slapi_ch_free_string(&errmsg);
        }

        return (CONN_OPERATION_FAILED);
    }
}

void
repl5_set_debug_timeout(const char *val)
{
    /* val looks like this: seconds[:debuglevel] */
    /* seconds is the number of seconds to wait until turning on the debug level */
    /* this should be less than the ldap connection timeout (default 10 minutes) */
    /* the optional debug level is the error log debugging level to use (default repl) */
    if (val) {
        const char *p = strchr(val, ':');
        s_debug_timeout = atoi(val);
        if (p) {
            s_debug_level = atoi(p + 1);
        } else {
            s_debug_level = 8192;
        }
    }
}

#ifdef FOR_DEBUGGING
static time_t
PRTime2time_t(PRTime tm)
{
    PRInt64 rt;

    PR_ASSERT(tm);

    LL_DIV(rt, tm, PR_USEC_PER_SEC);

    return (time_t)rt;
}
#endif

static Slapi_Eq_Context
repl5_start_debug_timeout(int *setlevel)
{
    Slapi_Eq_Context eqctx = 0;
    if (s_debug_timeout && s_debug_level) {
        time_t now = slapi_current_rel_time_t();
        eqctx = slapi_eq_once_rel(repl5_debug_timeout_callback, setlevel,
                              s_debug_timeout + now);
    }
    return eqctx;
}

static void
repl5_stop_debug_timeout(Slapi_Eq_Context eqctx, int *setlevel)
{
    char buf[20];

    if (eqctx && !*setlevel) {
        (void)slapi_eq_cancel_rel(eqctx);
    }

    if (s_debug_timeout && s_debug_level && *setlevel) {
        void config_set_errorlog_level(const char *type, char *buf, char *msg, int apply);
        sprintf(buf, "%d", 0);
        config_set_errorlog_level("nsslapd-errorlog-level", buf, NULL, 1);
    }
}

static void
repl5_debug_timeout_callback(time_t when, void *arg)
{
    int *setlevel = (int *)arg;
    void config_set_errorlog_level(const char *type, char *buf, char *msg, int apply);
    char buf[20];

    *setlevel = 1;
    sprintf(buf, "%d", s_debug_level);
    config_set_errorlog_level("nsslapd-errorlog-level", buf, NULL, 1);

    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                  "repl5_debug_timeout_callback: set debug level to %d at %ld\n",
                  s_debug_level, when);
}

void
conn_lock(Repl_Connection *conn)
{
    if (conn) {
        PR_Lock(conn->lock);
    }
}

void
conn_unlock(Repl_Connection *conn)
{
    if (conn) {
        PR_Unlock(conn->lock);
    }
}
