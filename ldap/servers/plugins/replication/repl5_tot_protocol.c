/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl5_tot_protocol.c */
/*

 The tot_protocol object implements the DS 5.0 multi-master total update
 replication protocol, used to (re)populate a replica. 

*/

#include "repl.h"
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
	PRLock *lock; /* Lock to protect access to this structure, the message id list and to force memory barriers */
	PRThread *result_tid; /* The async result thread */
	operation_id_list_item *message_id_list; /* List of IDs for outstanding operations */
	int abort; /* Flag used to tell the sending thread asyncronously that it should abort (because an error came up in a result) */
	int stop_result_thread; /* Flag used to tell the result thread to exit */
	int last_message_id_sent;
	int last_message_id_received;
} callback_data;

/* 
 * Number of window seconds to wait until we programmatically decide
 * that the replica has got out of BUSY state 
 */
#define SLEEP_ON_BUSY_WINDOW  (10)

/* Helper functions */
static void get_result (int rc, void *cb_data);
static int send_entry (Slapi_Entry *e, void *callback_data);
static void repl5_tot_delete(Private_Repl_Protocol **prp);

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
repl5_tot_log_operation_failure(int ldap_error, char* ldap_error_string, const char *agreement_name)
{
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Received error %d: %s for total update operation\n",
						agreement_name,
						ldap_error, ldap_error_string ? ldap_error_string : "NULL",
						0);
}

/* Thread that collects results from async operations sent to the consumer */
static void repl5_tot_result_threadmain(void *param) 
{
	callback_data *cb = (callback_data*) param;
	int res = 0;
	ConnResult conres = 0;
	Repl_Connection *conn = cb->prp->conn;
	int finished = 0;
	int connection_error = 0;
	char *ldap_error_string = NULL;
	int operation_code = 0;

	while (!finished) 
	{
		int message_id = 0;
		time_t time_now = 0;
		time_t start_time = time( NULL );
		int backoff_time = 1;

		/* Read the next result */
		/* We call the get result function with a short timeout (non-blocking) 
		 * this is so we don't block here forever, and can stop this thread when
		 * the time comes. However, we do need to implement blocking with timeout
		 * semantics here instead.
		 */

		while (!finished)
		{
			conres = conn_read_result_ex(conn, NULL, NULL, NULL, &message_id, 0);
			/* Timeout here means that we didn't block, not a real timeout */
			if (CONN_TIMEOUT == conres)
			{
				/* We need to a) check that the 'real' timeout hasn't expired and
				 * b) implement a backoff sleep to avoid spinning */
				/* Did the connection's timeout expire ? */
				time_now = time( NULL );
				if (conn_get_timeout(conn) <= ( time_now - start_time ))
				{
					/* We timed out */
					conres = CONN_TIMEOUT;
					break;
				}
				/* Otherwise we backoff */
				DS_Sleep(PR_MillisecondsToInterval(backoff_time));
				if (backoff_time < 1000) 
				{
					backoff_time <<= 1;
				}
				/* Should we stop ? */
				PR_Lock(cb->lock);
				if (cb->stop_result_thread) 
				{
					finished = 1;
				}
				PR_Unlock(cb->lock);
			} else
			{
				/* Something other than a timeout, so we exit the loop */
				break;
			}
		}

		if (message_id)
		{
			cb->last_message_id_received = message_id;
		}
		conn_get_error_ex(conn, &operation_code, &connection_error, &ldap_error_string);
		
		if (connection_error)
		{
			repl5_tot_log_operation_failure(connection_error,ldap_error_string,agmt_get_long_name(cb->prp->agmt));
		}
		/* Was the result itself an error ? */
		if (0 != conres)
		{
			/* If so then we need to take steps to abort the update process */
			PR_Lock(cb->lock);
			cb->abort = 1;
			PR_Unlock(cb->lock);
		}
		/* Should we stop ? */
		PR_Lock(cb->lock);
		if (cb->stop_result_thread) 
		{
			finished = 1;
		}
		PR_Unlock(cb->lock);
	}
}

static int repl5_tot_create_async_result_thread(callback_data *cb_data)
{
	int retval = 0;
	PRThread *tid = NULL;
	/* Create a thread that reads results from the connection and stores status in the callback_data structure */
	tid = PR_CreateThread(PR_USER_THREAD, 
				repl5_tot_result_threadmain, (void*)cb_data,
				PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 
				SLAPD_DEFAULT_THREAD_STACKSIZE);
	if (NULL == tid) 
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
					"repl5_tot_create_async_result_thread failed. "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					PR_GetError(), slapd_pr_strerror( PR_GetError() ));
		retval = -1;
	} else {
		cb_data->result_tid = tid;
	}
	return retval; 
}

static int repl5_tot_destroy_async_result_thread(callback_data *cb_data)
{
	int retval = 0;
	PRThread *tid = cb_data->result_tid;
	if (tid) {
		PR_Lock(cb_data->lock);
		cb_data->stop_result_thread = 1;
		PR_Unlock(cb_data->lock);
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
	if (connection_error)
	{
		repl5_tot_log_operation_failure(connection_error,ldap_error_string,agmt_get_long_name(cb_data->prp->agmt));
	}
	/* Return it to the caller */
	return conres;
}

static void
repl5_tot_waitfor_async_results(callback_data *cb_data)
{
	int done = 0;
	int loops = 0;
	/* Keep pulling results off the LDAP connection until we catch up to the last message id stored in the rd */
	while (!done) 
	{
		/* Lock the structure to force memory barrier */
		PR_Lock(cb_data->lock);
		/* Are we caught up ? */
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
					"repl5_inc_waitfor_async_results: %d %d\n",
					cb_data->last_message_id_received, cb_data->last_message_id_sent, 0);
		if (cb_data->last_message_id_received >= cb_data->last_message_id_sent) 
		{
			/* If so then we're done */
			done = 1;
		}
		PR_Unlock(cb_data->lock);
		/* If not then sleep a bit */
		DS_Sleep(PR_SecondsToInterval(1));
		loops++;
		/* If we sleep forever then we can conclude that something bad happened, and bail... */
		/* Arbitrary 30 second delay : basically we should only expect to wait as long as it takes to process a few operations, which should be on the order of a second at most */
		if (loops > 300) 
		{
			/* Log a warning */
			slapi_log_error(SLAPI_LOG_FATAL, NULL,
					"repl5_tot_waitfor_async_results timed out waiting for responses: %d %d\n",
					cb_data->last_message_id_received, cb_data->last_message_id_sent, 0);
			done = 1;
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
static void
repl5_tot_run(Private_Repl_Protocol *prp)
{
    int rc;
    callback_data cb_data = {0};
    Slapi_PBlock *pb;
    LDAPControl **ctrls;
	char *hostname = NULL;
	int portnum = 0;
	Slapi_DN *area_sdn = NULL;
	CSN *remote_schema_csn = NULL;
	
	PR_ASSERT(NULL != prp);

	prp->stopped = 0;
	if (prp->terminate)
	{
        prp->stopped = 1;
		goto done;
	}

	conn_set_timeout(prp->conn, agmt_get_timeout(prp->agmt));

    /* acquire remote replica */
	agmt_set_last_init_start(prp->agmt, current_time());
    rc = acquire_replica (prp, REPL_NSDS50_TOTAL_PROTOCOL_OID, NULL /* ruv */);
    /* We never retry total protocol, even in case a transient error.
       This is because if somebody already updated the replica we don't
       want to do it again */
    if (rc != ACQUIRE_SUCCESS)
    {
		int optype, ldaprc;
		conn_get_error(prp->conn, &optype, &ldaprc);
		agmt_set_last_init_status(prp->agmt, ldaprc,
				  prp->last_acquire_response_code, NULL);
        goto done;
    }
	else if (prp->terminate)
    {
        conn_disconnect(prp->conn);
        prp->stopped = 1;
		goto done;    
    }
	
	hostname = agmt_get_hostname(prp->agmt);
	portnum = agmt_get_port(prp->agmt);

    agmt_set_last_init_status(prp->agmt, 0, 0, "Total schema update in progress");
	remote_schema_csn = agmt_get_consumer_schema_csn ( prp->agmt );
	rc = conn_push_schema(prp->conn, &remote_schema_csn);
	if (CONN_SCHEMA_UPDATED != rc && CONN_SCHEMA_NO_UPDATE_NEEDED != rc)
	{
	    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Warning: unable to "
						"replicate schema to host %s, port %d. Continuing with "
						"total update session.\n",
						hostname, portnum);
		/* But keep going */
		agmt_set_last_init_status(prp->agmt, 0, rc, "Total schema update failed");
	}
	else
	{
		agmt_set_last_init_status(prp->agmt, 0, 0, "Total schema update succeeded");
	}

    /* ONREPL - big assumption here is that entries a returned in the id order
       and that the order implies that perent entry is always ahead of the
       child entry in the list. Otherwise, the consumer would not be
       properly updated because bulk import at the moment skips orphand entries. */
	/* XXXggood above assumption may not be valid if orphaned entry moved???? */

    agmt_set_last_init_status(prp->agmt, 0, 0, "Total update in progress");

    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Beginning total update of replica "
		"\"%s\".\n", agmt_get_long_name(prp->agmt));
    pb = slapi_pblock_new ();

	/* RMREPL - need to send schema here */

	area_sdn = agmt_get_replarea(prp->agmt);
    /* we need to provide managedsait control so that referral entries can
       be replicated */
    ctrls = (LDAPControl **)slapi_ch_calloc (3, sizeof (LDAPControl *));
    ctrls[0] = create_managedsait_control ();
    ctrls[1] = create_backend_control(area_sdn);
	
    slapi_search_internal_set_pb (pb, slapi_sdn_get_dn (area_sdn), 
                                  LDAP_SCOPE_SUBTREE, "(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))", NULL, 0, ctrls, NULL, 
                                  repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);

    cb_data.prp = prp;
    cb_data.rc = 0;
	cb_data.num_entries = 0UL;
	cb_data.sleep_on_busy = 0UL;
	cb_data.last_busy = current_time ();
	cb_data.lock = PR_NewLock();

	/* Before we get started on sending entries to the replica, we need to 
	 * setup things for async propagation: 
	 * 1. Create a thread that will read the LDAP results from the connection.
	 * 2. Anything else ?
	 */
	if (!prp->repl50consumer) 
	{
		rc = repl5_tot_create_async_result_thread(&cb_data);
		if (rc) {
			slapi_log_error (SLAPI_LOG_FATAL, repl_plugin_name, "%s: repl5_tot_run: "
							 "repl5_tot_create_async_result_thread failed; error - %d\n", 
							 agmt_get_long_name(prp->agmt), rc);
			goto done;
		}
	}

    /* this search get all the entries from the replicated area including tombstones
       and referrals */
    slapi_search_internal_callback_pb (pb, &cb_data /* callback data */,
                                       get_result /* result callback */,
                                       send_entry /* entry callback */,
	    					           NULL /* referral callback*/);

	/* 
	 * After completing the sending operation (or optionally failing), we need to clean up
	 * the async propagation stuff:
	 * 1. Stop the thread that collects LDAP results from the connection.
	 * 2. Anything else ?
	 */

	if (!prp->repl50consumer) 
	{
		repl5_tot_waitfor_async_results(&cb_data);
		rc = repl5_tot_destroy_async_result_thread(&cb_data);
		if (rc) {
			slapi_log_error (SLAPI_LOG_FATAL, repl_plugin_name, "%s: repl5_tot_run: "
							 "repl5_tot_destroy_async_result_thread failed; error - %d\n", 
							 agmt_get_long_name(prp->agmt), rc);
		}
	}

	/* From here on, things are the same as in the old sync code : 
	 * the entire total update either succeeded, or it failed. 
	 * If it failed, then cb_data.rc contains the error code, and
	 * suitable messages will have been logged to the error log about the failure.
	 */

	slapi_pblock_destroy (pb);
	agmt_set_last_init_end(prp->agmt, current_time());
	rc = cb_data.rc;
	release_replica(prp);
	slapi_sdn_free(&area_sdn);
	
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error (SLAPI_LOG_REPL, repl_plugin_name, "%s: repl5_tot_run: "
                         "failed to obtain data to send to the consumer; LDAP error - %d\n", 
                         agmt_get_long_name(prp->agmt), rc);
		agmt_set_last_init_status(prp->agmt, rc, 0, "Total update aborted");
    } else {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Finished total update of replica "
						"\"%s\". Sent %d entries.\n", agmt_get_long_name(prp->agmt), cb_data.num_entries);
		agmt_set_last_init_status(prp->agmt, 0, 0, "Total update succeeded");
	}

done:
	slapi_ch_free_string(&hostname);
	if (cb_data.lock) 
	{
		PR_DestroyLock(cb_data.lock);
	}
	prp->stopped = 1;
}

static int
repl5_tot_stop(Private_Repl_Protocol *prp)
{
	int return_value;
	int seconds = 600;
	PRIntervalTime start, maxwait, now;

	prp->terminate = 1;
	maxwait = PR_SecondsToInterval(seconds);
	start = PR_IntervalNow();
	now = start;
	while (!prp->stopped && ((now - start) < maxwait))
	{
		DS_Sleep(PR_SecondsToInterval(1));
		now = PR_IntervalNow();
	}
	if (!prp->stopped)
	{
		/* Isn't listening. Disconnect from the replica. */
        slapi_log_error (SLAPI_LOG_REPL, repl_plugin_name, "repl5_tot_run: "
                         "protocol not stopped after waiting for %d seconds "
						 "for agreement %s\n", PR_IntervalToSeconds(now-start),
						 agmt_get_long_name(prp->agmt));
        conn_disconnect(prp->conn);
		return_value = -1;
	}
	else
	{
		return_value = 0;
	}

	return return_value;
}



static int
repl5_tot_status(Private_Repl_Protocol *prp)
{
	int return_value = 0;
	return return_value;
}



static void
repl5_tot_noop(Private_Repl_Protocol *prp)
{
	/* noop */
}


Private_Repl_Protocol *
Repl_5_Tot_Protocol_new(Repl_Protocol *rp)
{
	repl5_tot_private *rip = NULL;
	Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_malloc(sizeof(Private_Repl_Protocol));
	prp->delete = repl5_tot_delete;
	prp->run = repl5_tot_run;
	prp->stop = repl5_tot_stop;
	prp->status = repl5_tot_status;
	prp->notify_update = repl5_tot_noop;
	prp->notify_agmt_changed = repl5_tot_noop;
    prp->notify_window_opened = repl5_tot_noop;
    prp->notify_window_closed = repl5_tot_noop;
	prp->update_now = repl5_tot_noop;
	if ((prp->lock = PR_NewLock()) == NULL)
	{
		goto loser;
	}
	if ((prp->cvar = PR_NewCondVar(prp->lock)) == NULL)
	{
		goto loser;
	}
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
	return prp;
loser:
	repl5_tot_delete(&prp);
	return NULL;
}

static void
repl5_tot_delete(Private_Repl_Protocol **prp)
{
}

static 
void get_result (int rc, void *cb_data)
{
    PR_ASSERT (cb_data);
    ((callback_data*)cb_data)->rc = rc;
}

static 
int send_entry (Slapi_Entry *e, void *cb_data)
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

    PR_ASSERT (cb_data);

    prp = ((callback_data*)cb_data)->prp;
	num_entriesp = &((callback_data *)cb_data)->num_entries;
	sleep_on_busyp = &((callback_data *)cb_data)->sleep_on_busy;
	last_busyp = &((callback_data *)cb_data)->last_busy;
    PR_ASSERT (prp);

    if (prp->terminate)
    {
        conn_disconnect(prp->conn);
        prp->stopped = 1;
		((callback_data*)cb_data)->rc = -1;
		return -1;    
    }

    /* skip ruv tombstone - need to  do this because it might be
       more up to date then the data we are sending to the client.
       RUV is sent separately via the protocol */
    if (is_ruv_tombstone_entry (e))
        return 0;

    /* ONREPL we would purge copiedFrom and copyingFrom here but I decided against it.
       Instead, it will get removed when this replica stops being 4.0 consumer and
       then propagated to all its consumer */

	if (agmt_is_fractional(prp->agmt))
	{
		frac_excluded_attrs = agmt_get_fractional_attrs(prp->agmt);
	}

    /* convert the entry to the on the wire format */
	bere = entry2bere(e,frac_excluded_attrs);

	if (frac_excluded_attrs)
	{
		slapi_ch_array_free(frac_excluded_attrs);
	}

    if (bere == NULL)
    {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s: send_entry: Encoding Error\n",
				agmt_get_long_name(prp->agmt));
		((callback_data*)cb_data)->rc = -1;
        retval = -1;
		goto error;
    }

    rc = ber_flatten(bere, &bv);
    ber_free (bere, 1);
    if (rc != 0)
    {
		((callback_data*)cb_data)->rc = -1;
        retval = -1;
		goto error;
    }

    do {
	/* push the entry to the consumer */
	rc = conn_send_extended_operation(prp->conn, REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID,
	                              bv /* payload */, NULL /* update_control */, &message_id);   

	if (message_id) 
	{
		((callback_data*)cb_data)->last_message_id_sent = message_id;
	}

	/* If we are talking to a 5.0 type consumer, we need to wait here and retrieve the 
	 * response. Reason is that it can return LDAP_BUSY, indicating that its queue has
	 * filled up. This completely breaks pipelineing, and so we need to fall back to 
	 * sync transmission for those consumers, in case they pull the LDAP_BUSY stunt on us :( */

	if (prp->repl50consumer) 
	{
		/* Get the response here */
		rc = repl5_tot_get_next_result((callback_data*)cb_data);
	}

	if (rc == CONN_BUSY) {
		time_t now = current_time ();
		if ((now - *last_busyp) < (*sleep_on_busyp + 10)) {
			*sleep_on_busyp +=5;
	}
	else {
		*sleep_on_busyp = 5;
	}
	*last_busyp = now;

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
		"Replica \"%s\" is busy. Waiting %ds while"
		" it finishes processing its current import queue\n", 
		agmt_get_long_name(prp->agmt), *sleep_on_busyp);
		DS_Sleep(PR_SecondsToInterval(*sleep_on_busyp));
    	}
    } 
    while (rc == CONN_BUSY);

    ber_bvfree(bv);
	(*num_entriesp)++;

	/* For async operation we need to inspect the abort status from the result thread here */

	if (CONN_OPERATION_SUCCESS == rc) {
		retval = 0;
	} else {
		((callback_data*)cb_data)->rc = rc;
		retval = -1;
	}
error:
	return retval;
}

