/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl5_protocol.c */
/*

 The replication protocol object manages the replication protocol for
 a given replica. It determines which protocol(s) are appropriate to
 use when updating a given replica. It also knows how to arbitrate
 incremental and total update protocols for a given replica.

*/

#include "repl5.h"
#include "windowsrepl.h"
#include "repl5_prot_private.h"

#define PROTOCOL_5_INCREMENTAL 1
#define PROTOCOL_5_TOTAL 2
#define PROTOCOL_4_INCREMENTAL 3
#define PROTOCOL_4_TOTAL 4
#define PROTOCOL_WINDOWS_INCREMENTAL 5
#define PROTOCOL_WINDOWS_TOTAL 6

typedef struct repl_protocol
{
	Private_Repl_Protocol *prp_incremental; /* inc protocol to use */
	Private_Repl_Protocol *prp_total; /* total protocol to use */
	Private_Repl_Protocol *prp_active_protocol; /* Pointer to active protocol */
	Repl_Agmt *agmt; /* The replication agreement we're servicing */
	Repl_Connection *conn; /* Connection to remote server */
	Object *replica_object; /* Local replica. If non-NULL, replica object is acquired */
	int state;
	int next_state;
	PRLock *lock;
} repl_protocol;


/* States */
#define STATE_FINISHED 503
#define STATE_BAD_STATE_SHOULD_NEVER_HAPPEN 599

/* Forward declarations */
static Private_Repl_Protocol *private_protocol_factory(Repl_Protocol *rp, int type);




/*
 * Create a new protocol instance.
 */
Repl_Protocol *
prot_new(Repl_Agmt *agmt, int protocol_state)
{
	Slapi_DN *replarea_sdn = NULL;
	Repl_Protocol *rp = (Repl_Protocol *)slapi_ch_malloc(sizeof(Repl_Protocol));

	rp->prp_incremental = rp->prp_total = rp->prp_active_protocol = NULL;
	if (protocol_state == STATE_PERFORMING_TOTAL_UPDATE)
	{
		rp->state = STATE_PERFORMING_TOTAL_UPDATE;
	}
	else
	{
		rp->state = STATE_PERFORMING_INCREMENTAL_UPDATE;
	}
	rp->next_state = STATE_PERFORMING_INCREMENTAL_UPDATE; 
	if ((rp->lock = PR_NewLock()) == NULL)
	{
		goto loser;
	}
	rp->agmt = agmt;
	/* now done in private_protocol_factory
	if ((rp->conn = conn_new(agmt)) == NULL)
	{
		goto loser;
	} */
	/* Acquire the local replica object */
	replarea_sdn = agmt_get_replarea(agmt);
	rp->replica_object = replica_get_replica_from_dn(replarea_sdn);
	if (NULL == rp->replica_object)
	{
		/* Whoa, no local replica!?!? */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Unable to locate replica object for local replica %s\n",
			agmt_get_long_name(agmt),
			slapi_sdn_get_dn(replarea_sdn));
		goto loser;
	}

	if (get_agmt_agreement_type(agmt) == REPLICA_TYPE_MULTIMASTER)
	{
	rp->prp_incremental = private_protocol_factory(rp, PROTOCOL_5_INCREMENTAL);
	rp->prp_total = private_protocol_factory(rp, PROTOCOL_5_TOTAL);
	} 
	else if  (get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS)
	{
	rp->prp_incremental = private_protocol_factory(rp, PROTOCOL_WINDOWS_INCREMENTAL);
	rp->prp_total = private_protocol_factory(rp, PROTOCOL_WINDOWS_TOTAL);
	}

	/* XXXggood register callback handlers for entries updated, and
		schedule window enter/leave. */
	slapi_sdn_free(&replarea_sdn);
	
	return rp;
loser:
	prot_delete(&rp);
	return NULL;
}





Object *
prot_get_replica_object(Repl_Protocol *rp)
{
	PR_ASSERT(NULL != rp);
	return rp->replica_object;
}





Repl_Agmt *
prot_get_agreement(Repl_Protocol *rp)
{
        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
	if (NULL == rp) return NULL;
	return rp->agmt;
}




void
prot_free(Repl_Protocol **rpp)
{
    Repl_Protocol *rp = NULL;

    if (rpp == NULL || *rpp == NULL) return;

    rp = *rpp;

    PR_Lock(rp->lock);
    if (NULL != rp->prp_incremental)
    {
        rp->prp_incremental->delete(&rp->prp_incremental);
    }
    if (NULL != rp->prp_total)
    {
        rp->prp_total->delete(&rp->prp_total);
    }
    if (NULL != rp->replica_object)
    {
        object_release(rp->replica_object);
    }
    if (NULL != rp->conn)
    {
        conn_delete(rp->conn);
    }
    rp->prp_active_protocol = NULL;
    PR_Unlock(rp->lock);
    slapi_ch_free((void **)rpp);
}

/*
 * Destroy a protocol instance XXXggood not complete
 */
void
prot_delete(Repl_Protocol **rpp)
{
	Repl_Protocol *rp;

	PR_ASSERT(NULL != rpp);
	rp = *rpp;
	/* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
	if (NULL != rp)
	{
		prot_stop(rp);
                prot_free(rpp);
	}
}





/*
 * Get the connection object.
 */
Repl_Connection *
prot_get_connection(Repl_Protocol *rp)
{
	Repl_Connection *return_value;

	PR_ASSERT(NULL != rp);
	PR_Lock(rp->lock);
	return_value = rp->conn;
	PR_Unlock(rp->lock);
	return return_value;
}




/*
 * This function causes the total protocol to start.
 * This is accomplished by registering a state transition
 * to a new state, and then signaling the incremental
 * protocol to stop.
 */
void 
prot_initialize_replica(Repl_Protocol *rp)
{
	PR_ASSERT(NULL != rp);

	PR_Lock(rp->lock);
    /* check that total protocol is not running */
	rp->next_state = STATE_PERFORMING_TOTAL_UPDATE;
	/* Stop the incremental protocol, if running */
	rp->prp_incremental->stop(rp->prp_incremental);
	if (rp->prp_total) agmt_set_last_init_status(rp->prp_total->agmt, 0, 0, NULL);
    PR_Unlock(rp->lock);
}





/*
 * Main thread for protocol manager.

This is a simple state machine. State transition table:

Initial state: incremental update

STATE                   EVENT                   NEXT STATE
-----                   -----                   ----------
incremental update      shutdown                finished
incremental update      total update requested  total update
total update            shutdown                finished
total update            update complete         incremental update
finished                (any)                   finished

*/

static void
prot_thread_main(void *arg)
{
	Repl_Protocol *rp = (Repl_Protocol *)arg;
	int done;

	PR_ASSERT(NULL != rp);

	if (rp->agmt) {
		set_thread_private_agmtname (agmt_get_long_name(rp->agmt));
	}

	done = 0;

	while (!done)
	  {
	    switch (rp->state)
	      {
	      case STATE_PERFORMING_INCREMENTAL_UPDATE:
		/* Run the incremental update protocol */
		PR_Lock(rp->lock);
		dev_debug("prot_thread_main(STATE_PERFORMING_INCREMENTAL_UPDATE): begin");
		rp->prp_active_protocol = rp->prp_incremental;
		PR_Unlock(rp->lock);
		rp->prp_incremental->run(rp->prp_incremental);
		dev_debug("prot_thread_main(STATE_PERFORMING_INCREMENTAL_UPDATE): end");
		break;
	      case STATE_PERFORMING_TOTAL_UPDATE:
		PR_Lock(rp->lock);
    
		/* stop incremental protocol if running */
		rp->prp_active_protocol = rp->prp_total;
		/* After total protocol finished, return to incremental */
		rp->next_state = STATE_PERFORMING_INCREMENTAL_UPDATE;
		PR_Unlock(rp->lock);
		/* Run the total update protocol */
		dev_debug("prot_thread_main(STATE_PERFORMING_TOTAL_UPDATE): begin");
		rp->prp_total->run(rp->prp_total);
		dev_debug("prot_thread_main(STATE_PERFORMING_TOTAL_UPDATE): end");
		/* update the agreement entry to notify clients that 
		   replica initialization is completed. */
		agmt_replica_init_done (rp->agmt);
    
		break;
	      case STATE_FINISHED:
		dev_debug("prot_thread_main(STATE_FINISHED): exiting prot_thread_main");
		done = 1;
		break;
	      }
	    rp->state = rp->next_state;
	  }
}


/*
 * Start a thread to handle the replication protocol.
 */
void
prot_start(Repl_Protocol *rp)
{
	PR_ASSERT(NULL != rp);
	if (NULL != rp)
	{
		if (PR_CreateThread(PR_USER_THREAD, prot_thread_main, (void *)rp,
			PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE) == NULL)
        {
            PRErrorCode prerr = PR_GetError();

            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Unable to create protocol thread; NSPR error - %d, %s\n",
			agmt_get_long_name(rp->agmt),
			prerr, slapd_pr_strerror(prerr));   
        }
	}
	else
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Unable to start "
			"protocol object - NULL protocol object passed to prot_start.\n");
	}
}





/*
 * Stop a protocol instance. 
 */
void
prot_stop(Repl_Protocol *rp)
{
	PR_ASSERT(NULL != rp);
	if (NULL != rp)
	{
		PR_Lock(rp->lock);
		rp->next_state = STATE_FINISHED;
        if (NULL != rp->prp_incremental)
        {
		    if (rp->prp_incremental->stop(rp->prp_incremental) != 0)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Warning: incremental protocol for replica \"%s\" "
					"did not shut down properly.\n",
					agmt_get_long_name(rp->agmt));
			}
        }
        if (NULL != rp->prp_total)
        {
		    if (rp->prp_total->stop(rp->prp_total) != 0)
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Warning: total protocol for replica \"%s\" "
					"did not shut down properly.\n",
					agmt_get_long_name(rp->agmt));
			}
        }
		PR_Unlock(rp->lock);
	}
	else
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Error: prot_stop() "
			" called on NULL protocol instance.\n");
	}
}





/*
 * Call the notify_update method of the incremental or total update
 * protocol, is either is active.
 */
void
prot_notify_update(Repl_Protocol *rp)
{
        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
        if (NULL == rp) return;

	PR_Lock(rp->lock);
	if (NULL != rp->prp_active_protocol)
	{
		rp->prp_active_protocol->notify_update(rp->prp_active_protocol);
	}
	PR_Unlock(rp->lock);
}


/*
 * Call the notify_agmt_changed method of the incremental or total update
 * protocol, is either is active.
 */
void
prot_notify_agmt_changed(Repl_Protocol *rp, char * agmt_name)
{
        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
        if (NULL == rp) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"Replication agreement for %s could not be updated. "
			"For replication to take place, please enable the suffix "
			"and restart the server\n", agmt_name);
		return;
	}

	PR_Lock(rp->lock);
	if (NULL != rp->prp_active_protocol)
	{
		rp->prp_active_protocol->notify_agmt_changed(rp->prp_active_protocol);
	}
	PR_Unlock(rp->lock);
}


void 
prot_notify_window_opened (Repl_Protocol *rp)
{
        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
        if (NULL == rp) return;

	PR_Lock(rp->lock);
	if (NULL != rp->prp_active_protocol)
	{
		rp->prp_active_protocol->notify_window_opened(rp->prp_active_protocol);
	}
	PR_Unlock(rp->lock);
}


void 
prot_notify_window_closed (Repl_Protocol *rp)
{
        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
        if (NULL == rp) return;

	PR_Lock(rp->lock);
	if (NULL != rp->prp_active_protocol)
	{
		rp->prp_active_protocol->notify_window_closed(rp->prp_active_protocol);
	}
	PR_Unlock(rp->lock);
}


int
prot_status(Repl_Protocol *rp)
{
	int return_status = PROTOCOL_STATUS_UNKNOWN;

        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */
	if (NULL != rp)
	{
		PR_Lock(rp->lock);
		if (NULL != rp->prp_active_protocol)
		{
			return_status = rp->prp_active_protocol->status(rp->prp_active_protocol);
		}
		PR_Unlock(rp->lock);
	}
	return return_status;
}


/*
 * Start an incremental protocol session, even if we're not
 * currently in a schedule window.
 * If the total protocol is active, do nothing.
 * Otherwise, notify the incremental protocol that it should
 * run once.
 */
void
prot_replicate_now(Repl_Protocol *rp)
{
        /* MAB: rp might be NULL for disabled suffixes. Don't ASSERT on it */

	if (NULL != rp)
	{
		PR_Lock(rp->lock);
		if (rp->prp_incremental == rp->prp_active_protocol)
		{
			rp->prp_active_protocol->update_now(rp->prp_active_protocol);
		}
		PR_Unlock(rp->lock);
	}
}

/*
 * A little factory function to create a protocol
 * instance of the correct type.
 */
static Private_Repl_Protocol *
private_protocol_factory(Repl_Protocol *rp, int type)
{
	Private_Repl_Protocol *prp;

	switch (type)
	{
		case PROTOCOL_5_INCREMENTAL:
			if ((rp->conn = conn_new(rp->agmt)) != NULL)
			prp = Repl_5_Inc_Protocol_new(rp);
			break;
		case PROTOCOL_5_TOTAL:
			if ((rp->conn = conn_new(rp->agmt)) != NULL)
			prp = Repl_5_Tot_Protocol_new(rp);
			break;
		case PROTOCOL_WINDOWS_INCREMENTAL: 
			if ((rp->conn = windows_conn_new(rp->agmt)) != NULL)
			prp = Windows_Inc_Protocol_new(rp);
			break;
		case PROTOCOL_WINDOWS_TOTAL: 
			if ((rp->conn = windows_conn_new(rp->agmt)) != NULL)
			prp = Windows_Tot_Protocol_new(rp);
			break;
	}
	return prp;
}
