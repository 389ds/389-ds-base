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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* windows_tot_protocol.c */
/*

 The tot_protocol object implements the DS 5.0 multi-master total update
 replication protocol, used to (re)populate a replica. 

*/


#include "repl.h"
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
	RUV *starting_ruv = NULL;
	Replica *replica = NULL;
	Object *local_ruv_obj = NULL;
	
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_tot_run\n", 0, 0, 0 );
	
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
	
    rc = windows_acquire_replica (prp, &ruv, 0 /* don't check RUV for total protocol */);
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
    
	windows_private_null_dirsync_cookie(prp->agmt);

	/* get everything */
	windows_dirsync_inc_run(prp);
	cookie_has_more = windows_private_dirsync_has_more(prp->agmt);	
	
	windows_private_save_dirsync_cookie(prp->agmt);

	/* If we got a change from dirsync, we should have a good RUV
	 * that has a min & max value.  If no change was generated,
	 * the RUV will have NULL min and max csns.  We deal with
	 * updating these values when we process the first change in
	 * the incremental sync protocol ( send_updates() ).  We will
	 * use this value for setting the consumer RUV if the total
	 * update succeeds. */
        replica = object_get_data(prp->replica_object);
        local_ruv_obj = replica_get_ruv (replica);
        starting_ruv = ruv_dup((RUV*)  object_get_data ( local_ruv_obj ));
        object_release (local_ruv_obj);
	

	/* send everything */
	dn = slapi_sdn_get_dn( windows_private_get_directory_subtree(prp->agmt));

	pb = slapi_pblock_new ();
    /* Perform a subtree search for any ntuser or ntgroup entries underneath the
     * suffix defined in the sync agreement. */
    slapi_search_internal_set_pb (pb, dn, 
                                  LDAP_SCOPE_SUBTREE, "(|(objectclass=ntuser)(objectclass=ntgroup))", NULL, 0, NULL, NULL, 
                                  repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    cb_data.prp = prp;
    cb_data.rc = 0;
	cb_data.num_entries = 0UL;
	cb_data.sleep_on_busy = 0UL;
	cb_data.last_busy = current_time ();

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
		/* Now update our consumer RUV for this agreement.
		 * This ensures that future incrememental updates work.
		 */
		if (slapi_is_loglevel_set(SLAPI_LOG_REPL))
		{
			slapi_log_error(SLAPI_LOG_REPL, NULL, "total update setting consumer RUV:\n");
			ruv_dump (starting_ruv, "consumer", NULL);
		}
		agmt_set_consumer_ruv(prp->agmt, starting_ruv );
	}

done:
	if (starting_ruv)
	{
		ruv_destroy(&starting_ruv);
	}
	
	prp->stopped = 1;
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_tot_run\n", 0, 0, 0 );
}

static int
windows_tot_stop(Private_Repl_Protocol *prp)
{
	int return_value;
	int seconds = 600;
	PRIntervalTime start, maxwait, now;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_tot_stop\n", 0, 0, 0 );

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

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_tot_stop\n", 0, 0, 0 );

	return return_value;
}



static int
windows_tot_status(Private_Repl_Protocol *prp)
{
	int return_value = 0;
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_tot_status\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_tot_status\n", 0, 0, 0 );
	return return_value;
}



static void
windows_tot_noop(Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_tot_noop\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_tot_noop\n", 0, 0, 0 );
	/* noop */
}


Private_Repl_Protocol *
Windows_Tot_Protocol_new(Repl_Protocol *rp)
{
	windows_tot_private *rip = NULL;
	Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_malloc(sizeof(Private_Repl_Protocol));

	LDAPDebug( LDAP_DEBUG_TRACE, "=> Windows_Tot_Protocol_new\n", 0, 0, 0 );

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
	LDAPDebug( LDAP_DEBUG_TRACE, "<= Windows_Tot_Protocol_new\n", 0, 0, 0 );
	return prp;
loser:
	windows_tot_delete(&prp);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= Windows_Tot_Protocol_new - loser\n", 0, 0, 0 );
	return NULL;
}

static void
windows_tot_delete(Private_Repl_Protocol **prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_tot_delete\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_tot_delete\n", 0, 0, 0 );
}

static 
void get_result (int rc, void *cb_data)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> get_result\n", 0, 0, 0 );
    PR_ASSERT (cb_data);
    ((callback_data*)cb_data)->rc = rc;
	LDAPDebug( LDAP_DEBUG_TRACE, "<= get_result\n", 0, 0, 0 );
}

static 
int send_entry (Slapi_Entry *e, void *cb_data)
{
    int rc;
    Private_Repl_Protocol *prp;
   
	unsigned long *num_entriesp;
	time_t *sleep_on_busyp;
	time_t *last_busyp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_entry\n", 0, 0, 0 );

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
		LDAPDebug( LDAP_DEBUG_TRACE, "<= send_entry\n", 0, 0, 0 );
		return -1;    
    }

    /* skip ruv tombstone - not relvant to Active Directory */
    if (is_ruv_tombstone_entry (e)) {
		LDAPDebug( LDAP_DEBUG_TRACE, "<= send_entry\n", 0, 0, 0 );
        return 0;
	}

	/* push the entry to the consumer */
	rc = windows_process_total_entry(prp,e);
	
	(*num_entriesp)++;

	LDAPDebug( LDAP_DEBUG_TRACE, "<= send_entry\n", 0, 0, 0 );

	if (CONN_OPERATION_SUCCESS == rc) {
		return 0;
	} else {
		((callback_data*)cb_data)->rc = rc;
		return -1;
	}
}

