/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

typedef struct callback_data
{
    Private_Repl_Protocol *prp;
    int rc;    
	unsigned long num_entries;
    time_t sleep_on_busy;
	time_t last_busy;
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
    callback_data cb_data;
    Slapi_PBlock *pb;
    LDAPControl **ctrls;
	PRBool replica_acquired = PR_FALSE;
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

    /* this search get all the entries from the replicated area including tombstones
       and referrals */
    slapi_search_internal_callback_pb (pb, &cb_data /* callback data */,
                                       get_result /* result callback */,
                                       send_entry /* entry callback */,
	    					           NULL /* referral callback*/);
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

    /* convert the entry to the on the wire format */
    bere = entry2bere(e);
    if (bere == NULL)
    {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s: send_entry: Encoding Error\n",
				agmt_get_long_name(prp->agmt));
		((callback_data*)cb_data)->rc = -1;
        return -1;
    }

    rc = ber_flatten(bere, &bv);
    ber_free (bere, 1);
    if (rc != 0)
    {
		((callback_data*)cb_data)->rc = -1;
        return -1;
    }

    do {
	/* push the entry to the consumer */
	rc = conn_send_extended_operation(prp->conn, REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID,
	                              bv /* payload */, NULL /* retoidp */, 
                                      NULL /* retdatap */, NULL /* update_control */, 
                                      NULL /* returned_controls */);   

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

	if (CONN_OPERATION_SUCCESS == rc) {
		return 0;
	} else {
		((callback_data*)cb_data)->rc = rc;
		return -1;
	}
}

