/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* windows_tot_protocol.c */
/*

 The tot_protocol object implements the DS 5.0 multi-master total update
 replication protocol, used to (re)populate a replica. 

*/

#include "windowsrepl.h"
#include "windows_prot_private.h"
#include "repl.h"
#include "repl5.h"
#include "repl5_prot_private.h"

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
static void windows_tot_delete(Private_Repl_Protocol **prp);

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
    Slapi_PBlock *pb;
	const char* dn;
	CSN *remote_schema_csn = NULL;
	PRBool cookie_has_more = PR_TRUE;
	RUV *ruv = NULL;
	
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
	
    rc = windows_acquire_replica (prp, &ruv);
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
	
	
	agmt_set_last_init_status(prp->agmt, 0, 0, "Total schema update in progress");
	remote_schema_csn = agmt_get_consumer_schema_csn ( prp->agmt );

    agmt_set_last_init_status(prp->agmt, 0, 0, "Total update in progress");

    slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "Beginning total update of replica "
		"\"%s\".\n", agmt_get_long_name(prp->agmt));
    
	windows_private_null_dirsync_control(prp->agmt);

	/* get everything */
	windows_dirsync_inc_run(prp);
	cookie_has_more = windows_private_dirsync_has_more(prp->agmt);	
	
	

	/* send everything */
	dn = slapi_sdn_get_dn( windows_private_get_directory_replarea(prp->agmt));

	pb = slapi_pblock_new ();
    slapi_search_internal_set_pb (pb, dn, /* XXX modify the searchfilter and scope? */
                                  LDAP_SCOPE_ONELEVEL, "(|(objectclass=ntuser)(objectclass=ntgroup)(nsuniqueid=*))", NULL, 0, NULL, NULL, 
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
	windows_release_replica(prp);
		
    if (rc != LDAP_SUCCESS)
    {
        slapi_log_error (SLAPI_LOG_REPL, windows_repl_plugin_name, "%s: windows_tot_run: "
                         "failed to obtain data to send to the consumer; LDAP error - %d\n", 
                         agmt_get_long_name(prp->agmt), rc);
		agmt_set_last_init_status(prp->agmt, rc, 0, "Total update aborted");
    } else {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "Finished total update of replica "
						"\"%s\". Sent %d entries.\n", agmt_get_long_name(prp->agmt), cb_data.num_entries);
		agmt_set_last_init_status(prp->agmt, 0, 0, "Total update succeeded");
	}

done:
	
	prp->stopped = 1;
}

static int
windows_tot_stop(Private_Repl_Protocol *prp)
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
        slapi_log_error (SLAPI_LOG_REPL, windows_repl_plugin_name, "windows_tot_run: "
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
windows_tot_status(Private_Repl_Protocol *prp)
{
	int return_value = 0;
	return return_value;
}



static void
windows_tot_noop(Private_Repl_Protocol *prp)
{
	/* noop */
}


Private_Repl_Protocol *
Windows_Tot_Protocol_new(Repl_Protocol *rp)
{
	windows_tot_private *rip = NULL;
	Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_malloc(sizeof(Private_Repl_Protocol));
	prp->delete = windows_tot_delete;
	prp->run = windows_tot_run;
	prp->stop = windows_tot_stop;
	prp->status = windows_tot_status;
	prp->notify_update = windows_tot_noop;
	prp->notify_agmt_changed = windows_tot_noop;
    prp->notify_window_opened = windows_tot_noop;
    prp->notify_window_closed = windows_tot_noop;
	prp->replica_object = prot_get_replica_object(rp);
	prp->update_now = windows_tot_noop;
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
	rip = (void *)slapi_ch_malloc(sizeof(windows_tot_private));
	rip->rp = rp;
	prp->private = (void *)rip;
    prp->replica_acquired = PR_FALSE;
	return prp;
loser:
	windows_tot_delete(&prp);
	return NULL;
}

static void
windows_tot_delete(Private_Repl_Protocol **prp)
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
   
   // struct berval *bv;
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

    do {
	/* push the entry to the consumer */
	rc = add_or_modify_user(e);
	
	if (rc == CONN_BUSY) {
		time_t now = current_time ();
		if ((now - *last_busyp) < (*sleep_on_busyp + 10)) {
			*sleep_on_busyp +=5;
	}
	else {
		*sleep_on_busyp = 5;
	}
	*last_busyp = now;

	slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
		"Replica \"%s\" is busy. Waiting %ds while"
		" it finishes processing its current import queue\n", 
		agmt_get_long_name(prp->agmt), *sleep_on_busyp);
		DS_Sleep(PR_SecondsToInterval(*sleep_on_busyp));
    	}
    } 
    while (rc == CONN_BUSY);

	(*num_entriesp)++;

	if (CONN_OPERATION_SUCCESS == rc) {
		return 0;
	} else {
		((callback_data*)cb_data)->rc = rc;
		return -1;
	}
}

