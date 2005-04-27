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

#include "repl.h"
#include "repl5.h"
#include "windowsrepl.h"
#include "windows_prot_private.h"
#include "slap.h" /* PSEUDO_ATTR_UNHASHED */
#include "repl5_ruv.h"

#include "cl5_api.h"
#include "slapi-plugin.h"
extern int slapi_log_urp;


/*** from proto-slap.h ***/
void ava_done(struct ava *ava);

typedef struct windows_inc_private
{
	char *ruv;	/* RUV on remote replica (use diff type for this? - ggood */
	Backoff_Timer *backoff;
	Repl_Protocol *rp;
	PRLock *lock;
	PRUint32 eventbits;
} windows_inc_private;


/* Various states the incremental protocol can pass through */
#define	STATE_START 0                   /* ONREPL - should we rename this - we don't use it just to start up? */
#define STATE_WAIT_WINDOW_OPEN 1
#define STATE_WAIT_CHANGES 2
#define STATE_READY_TO_ACQUIRE 3
#define STATE_BACKOFF_START 4           /* ONREPL - can we combine BACKOFF_START and BACKOFF states? */
#define STATE_BACKOFF 5
#define STATE_SENDING_UPDATES 6
#define STATE_STOP_FATAL_ERROR 7
#define STATE_STOP_FATAL_ERROR_PART2 8
#define STATE_STOP_NORMAL_TERMINATION 9

/* Events (synchronous and asynchronous; these are bits) */
#define EVENT_WINDOW_OPENED 1
#define EVENT_WINDOW_CLOSED 2
#define EVENT_TRIGGERING_CRITERIA_MET 4             /* ONREPL - should we rename this to EVENT_CHANGE_AVAILABLE */
#define EVENT_BACKOFF_EXPIRED 8
#define EVENT_REPLICATE_NOW 16
#define EVENT_PROTOCOL_SHUTDOWN 32
#define EVENT_AGMT_CHANGED 64
#define EVENT_RUN_DIRSYNC 128

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

#define MAX_CHANGES_PER_SESSION	10000
/*
 * Maximum time to wait between replication sessions. If we
 * don't see any updates for a period equal to this interval,
 * we go ahead and start a replication session, just to be safe
 */
#define MAX_WAIT_BETWEEN_SESSIONS PR_SecondsToInterval(60 * 5) /* 5 minutes */
/*
 * Periodic synchronization interval.  This is used for scheduling the periodic_dirsync event.
 * The time is in milliseconds.
 */
#define PERIODIC_DIRSYNC_INTERVAL 5 * 60 * 1000 /* DBDB this should probably be configurable. 5 mins fixed for now */
/*
 * tests if the protocol has been shutdown and we need to quit
 * event_occurred resets the bits in the bit flag, so whoever tests for shutdown
 * resets the flags, so the next one who tests for shutdown won't get it, so we
 * also look at the terminate flag
 */
#define PROTOCOL_IS_SHUTDOWN(prp) (event_occurred(prp, EVENT_PROTOCOL_SHUTDOWN) || prp->terminate)

/* Forward declarations */
static PRUint32 event_occurred(Private_Repl_Protocol *prp, PRUint32 event);
static void reset_events (Private_Repl_Protocol *prp);
static void protocol_sleep(Private_Repl_Protocol *prp, PRIntervalTime duration);
static int send_updates(Private_Repl_Protocol *prp, RUV *ruv, PRUint32 *num_changes_sent);
static void windows_inc_backoff_expired(time_t timer_fire_time, void *arg);
static int windows_examine_update_vector(Private_Repl_Protocol *prp, RUV *ruv);
static PRBool ignore_error_and_keep_going(int error);
static const char* state2name (int state);
static const char* event2name (int event);
static const char* acquire2name (int code);
static void periodic_dirsync(time_t when, void *arg);

static Slapi_Eq_Context dirsync;
/*
 * It's specifically ok to delete a protocol instance that
 * is currently running. The instance will be shut down, and
 * then resources will be freed. Since a graceful shutdown is
 * attempted, this function may take some time to complete.
 */
static void
windows_inc_delete(Private_Repl_Protocol **prpp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_delete\n", 0, 0, 0 );
	/* First, stop the protocol if it isn't already stopped */
	/* Then, delete all resources used by the protocol */
	slapi_eq_cancel(dirsync); 
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_delete\n", 0, 0, 0 );
}

/* helper function */
void
w_set_pause_and_busy_time(long *pausetime, long *busywaittime)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> w_set_pause_and_busy_time\n", 0, 0, 0 );
  /* If neither are set, set busy time to its default */
  if (!*pausetime && !*busywaittime)
    {
      *busywaittime = PROTOCOL_BUSY_BACKOFF_MINIMUM;
    }
  /* pause time must be at least 1 more than the busy backoff time */
  if (*pausetime && !*busywaittime)
    {
      /*
       * user specified a pause time but no busy wait time - must
       * set busy wait time to 1 less than pause time - if pause
       * time is 1, we must set it to 2
       */
      if (*pausetime < 2)
        {
	  *pausetime = 2;
	}
      *busywaittime = *pausetime - 1;
    }
  else if (!*pausetime && *busywaittime)
    {
      /*
       * user specified a busy wait time but no pause time - must
       * set pause time to 1 more than busy wait time
       */
      *pausetime = *busywaittime + 1;
    }
  else if (*pausetime && *busywaittime && *pausetime <= *busywaittime)
    {
      /*
       * user specified both pause and busy wait times, but the pause
       * time was <= busy wait time - pause time must be at least
       * 1 more than the busy wait time
       */
      *pausetime = *busywaittime + 1;
    }
	LDAPDebug( LDAP_DEBUG_TRACE, "<= w_set_pause_and_busy_time\n", 0, 0, 0 );
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
 * DBDB: what follows is quite possibly the worst code I have ever seen.
 * Unfortunately we chose not to re-write it when we did the windows sync version.
 */

/*
 * Main state machine for the incremental protocol. This routine will,
 * under normal circumstances, not return until the protocol is shut
 * down.
 */
static void
windows_inc_run(Private_Repl_Protocol *prp)
{
	int current_state = STATE_START;
	int next_state = STATE_START;
	windows_inc_private *prp_priv = (windows_inc_private *)prp->private;
	int done = 0;
	int e1 = 0;
	RUV *ruv = NULL;
	Replica *replica = NULL;
	int wait_change_timer_set = 0;
	time_t last_start_time = 0;
	PRUint32 num_changes_sent = 0;
	char *hostname = NULL;
	int portnum = 0;
	/* use a different backoff timer strategy for ACQUIRE_REPLICA_BUSY errors */
	PRBool use_busy_backoff_timer = PR_FALSE;
	long pausetime = 0;
	long busywaittime = 0;
	// Some operations should only be done the first time STATE_START is true.
	static PRBool is_first_start = PR_TRUE;
 
	PRBool run_dirsync = PR_FALSE;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_run\n", 0, 0, 0 );

	prp->stopped = 0;
	prp->terminate = 0;
	hostname = agmt_get_hostname(prp->agmt);
	portnum = agmt_get_port(prp->agmt);

	windows_private_load_dirsync_cookie(prp->agmt);
	
	do {
		int rc = 0;

		/* Take action, based on current state, and compute new state. */
		switch (current_state)
		{
			case STATE_START:

				dev_debug("windows_inc_run(STATE_START)");
				if (PROTOCOL_IS_SHUTDOWN(prp))
				{
					done = 1;
					break;
				}

				/*
				 * Our initial state. See if we're in a schedule window. If
				 * so, then we're ready to acquire the replica and see if it
				 * needs any updates from us. If not, then wait for the window
				 * to open.
				 */
				if (agmt_schedule_in_window_now(prp->agmt))
				{
					next_state = STATE_READY_TO_ACQUIRE;
				} else
				{
					next_state = STATE_WAIT_WINDOW_OPEN;
				}

				/* we can get here from other states because some events happened and were
				   not cleared. For instance when we wake up in STATE_WAIT_CHANGES state.
				   Since this is a fresh start state, we should clear all events */
				/* ONREPL - this does not feel right - we should take another look
				   at this state machine */
				reset_events (prp);

				/* Cancel any linger timer that might be in effect... */
				windows_conn_cancel_linger(prp->conn);
				/* ... and disconnect, if currently connected */
				windows_conn_disconnect(prp->conn);
				/* get the new pause time, if any */
				pausetime = agmt_get_pausetime(prp->agmt);
				/* get the new busy wait time, if any */
				busywaittime = agmt_get_busywaittime(prp->agmt);
				if (pausetime || busywaittime)
				{
					/* helper function to make sure they are set correctly */
					w_set_pause_and_busy_time(&pausetime, &busywaittime);
				}


				if (is_first_start) {
					/*
					 * The function, the arguments, the time (hence) when it is first to be called, 
					 * and the repeat interval. 
					 */ 
					/* DBDB: we should probably make this polling interval configurable */
					dirsync = slapi_eq_repeat(periodic_dirsync, (void*) prp, (time_t)0 , PERIODIC_DIRSYNC_INTERVAL);
					is_first_start = PR_FALSE;
				}
				break;

			case STATE_WAIT_WINDOW_OPEN:
				/*
				 * We're waiting for a schedule window to open. If one did,
				 * or we receive a "replicate now" event, then start a protocol
				 * session immediately. If the replication schedule changed, go
				 * back to start.  Otherwise, go back to sleep.
				 */
				dev_debug("windows_inc_run(STATE_WAIT_WINDOW_OPEN)");
				if (PROTOCOL_IS_SHUTDOWN(prp))
				  {
					done = 1;
					break;
				  }
				else if (event_occurred(prp, EVENT_WINDOW_OPENED))
				  {
					next_state = STATE_READY_TO_ACQUIRE;
				  }
				else if (event_occurred(prp, EVENT_REPLICATE_NOW))
				  {
					next_state = STATE_READY_TO_ACQUIRE;
				  }
				else if (event_occurred(prp, EVENT_AGMT_CHANGED))
				  {
					next_state = STATE_START;
					run_dirsync = PR_TRUE;
					windows_conn_set_agmt_changed(prp->conn);
				  }
				else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET)) /* change available */
				  {
					/* just ignore it and go to sleep */
					protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
				  }
				else if (e1 = event_occurred(prp, EVENT_WINDOW_CLOSED) ||
					 event_occurred(prp, EVENT_BACKOFF_EXPIRED))
				  {
					/* this events - should not occur - log a warning and go to sleep */
					slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
						"%s: Incremental protocol: "
						"event %s should not occur in state %s; going to sleep\n",
						agmt_get_long_name(prp->agmt),
						e1 ? event2name(EVENT_WINDOW_CLOSED) : event2name(EVENT_BACKOFF_EXPIRED), 
						state2name(current_state));
					protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
				  }
				else
				  {
					/* wait until window opens or an event occurs */
					slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
						"%s: Incremental protocol: "
						"waiting for update window to open\n", agmt_get_long_name(prp->agmt));
					protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
				  }
				break;

			case STATE_WAIT_CHANGES:
				/*
				 * We're in a replication window, but we're waiting for more
				 * changes to accumulate before we actually hook up and send
				 * them.
				 */
				dev_debug("windows_inc_run(STATE_WAIT_CHANGES)");
				if (PROTOCOL_IS_SHUTDOWN(prp))
				  {
					dev_debug("windows_inc_run(STATE_WAIT_CHANGES): PROTOCOL_IS_SHUTING_DOWN -> end windows_inc_run\n");
					done = 1;
					break;
				  }
				else if (event_occurred(prp, EVENT_REPLICATE_NOW))
				  {
					dev_debug("windows_inc_run(STATE_WAIT_CHANGES): EVENT_REPLICATE_NOW received -> STATE_READY_TO_ACQUIRE\n");
					next_state = STATE_READY_TO_ACQUIRE;
					wait_change_timer_set = 0;
					/* We also want to run dirsync on a 'replicate now' event */
					run_dirsync = PR_TRUE;
				  }
				else if ( event_occurred(prp, EVENT_RUN_DIRSYNC))
				{
					dev_debug("windows_inc_run(STATE_WAIT_CHANGES): EVENT_REPLICATE_NOW received -> STATE_READY_TO_ACQUIRE\n");
					next_state = STATE_READY_TO_ACQUIRE;
					wait_change_timer_set = 0;
					run_dirsync = PR_TRUE;
					
				}
				else if (event_occurred(prp, EVENT_AGMT_CHANGED))
				  {
					dev_debug("windows_inc_run(STATE_WAIT_CHANGES): EVENT_AGMT_CHANGED received -> STATE_START\n");
					next_state = STATE_START;
					windows_conn_set_agmt_changed(prp->conn);
					wait_change_timer_set = 0;
					/* We also want to run dirsync on a 'agreement changed' event, because that's how we receive 'send updates now' */
					run_dirsync = PR_TRUE;
				  }
				else if (event_occurred(prp, EVENT_WINDOW_CLOSED))
				  {
					dev_debug("windows_inc_run(STATE_WAIT_CHANGES): EVENT_WINDOW_CLOSED received -> STATE_WAIT_WINDOW_OPEN\n");
					next_state = STATE_WAIT_WINDOW_OPEN;
					wait_change_timer_set = 0;
				  }
				else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET) )
				  {
					dev_debug("windows_inc_run(STATE_WAIT_CHANGES): EVENT_TRIGGERING_CRITERIA_MET received -> STATE_READY_TO_ACQUIRE\n");
					next_state = STATE_READY_TO_ACQUIRE;
					wait_change_timer_set = 0;
				  }
				else if (e1 = event_occurred(prp, EVENT_WINDOW_OPENED) ||
					 event_occurred(prp, EVENT_BACKOFF_EXPIRED))
				  {
					/* this events - should not occur - log a warning and clear the event */
					slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "%s: Incremental protocol: "
							"event %s should not occur in state %s\n", 
							agmt_get_long_name(prp->agmt),
							e1 ? event2name(EVENT_WINDOW_OPENED) : event2name(EVENT_BACKOFF_EXPIRED), 
							state2name(current_state));
					wait_change_timer_set = 0;
				  }			
				else
				  {
					if (wait_change_timer_set)
					{
						/* We are here because our timer expired */
						dev_debug("windows_inc_run(STATE_WAIT_CHANGES): wait_change_timer_set expired -> STATE_START\n");
						next_state = STATE_START;
						run_dirsync = PR_TRUE;
						wait_change_timer_set = 0;
					}
					else
					{
						/* We are here because the last replication session
						 * finished or aborted.
						 */
						wait_change_timer_set = 1;
						protocol_sleep(prp, MAX_WAIT_BETWEEN_SESSIONS);				
      				}
				  }
				break;

      case STATE_READY_TO_ACQUIRE:
		
				dev_debug("windows_inc_run(STATE_READY_TO_ACQUIRE)");
				if (PROTOCOL_IS_SHUTDOWN(prp))
				  {
					done = 1;
					break;
				  }

				/* ONREPL - at this state we unconditionally acquire the replica
				   ignoring all events. Not sure if this is good */
				object_acquire(prp->replica_object);
				replica = object_get_data(prp->replica_object);
						
				rc = windows_acquire_replica(prp, &ruv , (run_dirsync == 0) /* yes, check the consumer RUV for incremental, but not if we're going to dirsync afterwards */);

				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
						"windows_acquire_replica returned %s (%d)\n",
						acquire2name(rc),
						rc);

				use_busy_backoff_timer = PR_FALSE; /* default */
				if (rc == ACQUIRE_SUCCESS)
				  {
					next_state = STATE_SENDING_UPDATES;
				  }
				else if (rc == ACQUIRE_REPLICA_BUSY)
				  {
					next_state = STATE_BACKOFF_START;
					use_busy_backoff_timer = PR_TRUE;
				  }
				else if (rc == ACQUIRE_CONSUMER_WAS_UPTODATE)
				  {
					next_state = STATE_WAIT_CHANGES;
				
				  }
				else if (rc == ACQUIRE_TRANSIENT_ERROR)
				  {
					next_state = STATE_BACKOFF_START;
				  }
				else if (rc == ACQUIRE_FATAL_ERROR)
				  {
					next_state = STATE_STOP_FATAL_ERROR;
				  }
				if (rc != ACQUIRE_SUCCESS)
				  {
					int optype, ldaprc;
					windows_conn_get_error(prp->conn, &optype, &ldaprc);
					agmt_set_last_update_status(prp->agmt, ldaprc,
								prp->last_acquire_response_code, NULL);
				  }
						
				object_release(prp->replica_object); replica = NULL;
				break;

		case STATE_BACKOFF_START:
				dev_debug("windows_inc_run(STATE_BACKOFF_START)");
				if (PROTOCOL_IS_SHUTDOWN(prp))
				  {
					done = 1;
					break;
				  }
				if (event_occurred(prp, EVENT_REPLICATE_NOW) || event_occurred(prp, EVENT_RUN_DIRSYNC))
				  {
					next_state = STATE_READY_TO_ACQUIRE;
				  }
				else if (event_occurred(prp, EVENT_AGMT_CHANGED))
				  {
					next_state = STATE_START;
					run_dirsync = PR_TRUE; /* Also trigger dirsync for the 'send updates now' feature */
					windows_conn_set_agmt_changed(prp->conn);
				  }
				else if (event_occurred (prp, EVENT_WINDOW_CLOSED))
				  {
					next_state = STATE_WAIT_WINDOW_OPEN;
				  }
				else if (event_occurred (prp, EVENT_TRIGGERING_CRITERIA_MET))
				  {
					/* consume and ignore */
				  }
				else if (e1 = event_occurred (prp, EVENT_WINDOW_OPENED) || 
					 event_occurred (prp, EVENT_BACKOFF_EXPIRED))
				  {
					/* This should never happen */
					/* this events - should not occur - log a warning and go to sleep */
					slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"%s: Incremental protocol: event %s should not occur in state %s\n", 
							agmt_get_long_name(prp->agmt),
							e1 ? event2name(EVENT_WINDOW_OPENED) : event2name(EVENT_BACKOFF_EXPIRED), 
							state2name(current_state));
				  }
				else
				  {
							/* Set up the backoff timer to wake us up at the appropriate time */
					if (use_busy_backoff_timer)
					{
					  /* we received a busy signal from the consumer, wait for a while */
					  if (!busywaittime)
						{
					  busywaittime = PROTOCOL_BUSY_BACKOFF_MINIMUM;
						}
					  prp_priv->backoff = backoff_new(BACKOFF_FIXED, busywaittime,
									  busywaittime);
					}
					else
					{
					  prp_priv->backoff = backoff_new(BACKOFF_EXPONENTIAL, PROTOCOL_BACKOFF_MINIMUM,
									  PROTOCOL_BACKOFF_MAXIMUM);
					}
					next_state = STATE_BACKOFF;
					backoff_reset(prp_priv->backoff, windows_inc_backoff_expired, (void *)prp);
					protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
					use_busy_backoff_timer = PR_FALSE;
				  }
					break;

				case STATE_BACKOFF:
					/*
					 * We're in a backoff state. 
					 */
					dev_debug("windows_inc_run(STATE_BACKOFF)");
					if (PROTOCOL_IS_SHUTDOWN(prp))
					  {
						if (prp_priv->backoff)
						  backoff_delete(&prp_priv->backoff);
						done = 1;
						break;
					  }
					else if (event_occurred(prp, EVENT_REPLICATE_NOW) || event_occurred(prp, EVENT_RUN_DIRSYNC))
					  {
						next_state = STATE_READY_TO_ACQUIRE;
					  }
					else if (event_occurred(prp, EVENT_AGMT_CHANGED))
					  {
						next_state = STATE_START;
						run_dirsync = PR_TRUE;

						windows_conn_set_agmt_changed(prp->conn);
								/* Destroy the backoff timer, since we won't need it anymore */ 
						if (prp_priv->backoff)   
						  backoff_delete(&prp_priv->backoff);
					  }
					else if (event_occurred(prp, EVENT_WINDOW_CLOSED))
					  {
						next_state = STATE_WAIT_WINDOW_OPEN;
								/* Destroy the backoff timer, since we won't need it anymore */
						if (prp_priv->backoff)
						  backoff_delete(&prp_priv->backoff);
					  }
					else if (event_occurred(prp, EVENT_BACKOFF_EXPIRED))
					  {
						rc = windows_acquire_replica(prp, &ruv, 1 /* check RUV for incremental */);
						use_busy_backoff_timer = PR_FALSE;
						if (rc == ACQUIRE_SUCCESS)
						  {
						next_state = STATE_SENDING_UPDATES;
						  }
						else if (rc == ACQUIRE_REPLICA_BUSY)
						  {
						next_state = STATE_BACKOFF;
						use_busy_backoff_timer = PR_TRUE;
						  }
						else if (rc == ACQUIRE_CONSUMER_WAS_UPTODATE)
						  {
							next_state = STATE_WAIT_CHANGES;
						  }
						else if (rc == ACQUIRE_TRANSIENT_ERROR)
						  {
						next_state = STATE_BACKOFF;
						  }
						else if (rc == ACQUIRE_FATAL_ERROR)
						  {
						next_state = STATE_STOP_FATAL_ERROR;
						  }
						if (rc != ACQUIRE_SUCCESS)
						  {
						int optype, ldaprc;
						windows_conn_get_error(prp->conn, &optype, &ldaprc);
						agmt_set_last_update_status(prp->agmt, ldaprc,
										prp->last_acquire_response_code, NULL);
						  }
								/*
								 * We either need to step the backoff timer, or
								 * destroy it if we don't need it anymore.
								 */
						if (STATE_BACKOFF == next_state)
						  {
						time_t next_fire_time;
						time_t now;
						/* Step the backoff timer */
						time(&now);
						next_fire_time = backoff_step(prp_priv->backoff);
						/* And go back to sleep */
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
								"%s: Replication session backing off for %d seconds\n",
								agmt_get_long_name(prp->agmt),
								next_fire_time - now);

						protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
						  }
						else
						  {
						/* Destroy the backoff timer, since we won't need it anymore */
						backoff_delete(&prp_priv->backoff);
						  }            
						use_busy_backoff_timer = PR_FALSE;
					  }
					else if (event_occurred(prp, EVENT_TRIGGERING_CRITERIA_MET))
					  {
						/* changes are available */
						if ( prp_priv->backoff == NULL || backoff_expired (prp_priv->backoff, 60) )
						{
							/*
							 * Have seen cases that the agmt stuck here forever since
							 * somehow the backoff timer was not in event queue anymore.
							 * If the backoff timer has expired more than 60 seconds,
							 * destroy it.
							 */
							if ( prp_priv->backoff )
								backoff_delete(&prp_priv->backoff);
							next_state = STATE_READY_TO_ACQUIRE;
						}
						else
						{
							/* ignore changes and go to sleep */
							protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
						}
					  }
					else if (event_occurred(prp, EVENT_WINDOW_OPENED))
					  {
						/* this should never happen - log an error and go to sleep */
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "%s: Incremental protocol: "
								"event %s should not occur in state %s; going to sleep\n", 
								agmt_get_long_name(prp->agmt),
								event2name(EVENT_WINDOW_OPENED), state2name(current_state));
						protocol_sleep(prp, PR_INTERVAL_NO_TIMEOUT);
					  }
					break;
      case STATE_SENDING_UPDATES:
	dev_debug("windows_inc_run(STATE_SENDING_UPDATES)");
	agmt_set_update_in_progress(prp->agmt, PR_TRUE);
	num_changes_sent = 0;
	last_start_time = current_time();
	agmt_set_last_update_start(prp->agmt, last_start_time);
	/*
	 * We've acquired the replica, and are ready to send any
	 * needed updates.
	 */
	if (PROTOCOL_IS_SHUTDOWN(prp))
	  {
	    windows_release_replica (prp);
	    done = 1;
	    agmt_set_update_in_progress(prp->agmt, PR_FALSE);
	    agmt_set_last_update_end(prp->agmt, current_time());
	    /* MAB: I don't find the following status correct. How do we know it has
	       been stopped by an admin and not by a total update request, for instance?
	       In any case, how is this protocol shutdown situation different from all the 
	       other ones that are present in this state machine? */
	    /* richm: We at least need to let monitors know that the protocol has been
	       shutdown - maybe they can figure out why */
	    agmt_set_last_update_status(prp->agmt, 0, 0, "Protocol stopped");
	    break;
	  } 

	agmt_set_last_update_status(prp->agmt, 0, 0, "Incremental update started");

	dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> windows_examine_update_vector");
	rc = windows_examine_update_vector(prp, ruv);
	/*
	 * Decide what to do next - proceed with incremental,
	 * backoff, or total update
	 */
	switch (rc)
	  {
	  case EXAMINE_RUV_PARAM_ERROR:
	    /* this is really bad - we have NULL prp! */
	    next_state = STATE_STOP_FATAL_ERROR;
	    break;
	  case EXAMINE_RUV_PRISTINE_REPLICA:
	    slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
			"%s: Replica has no update vector. It has never been initialized.\n",
		   	 agmt_get_long_name(prp->agmt));
	    next_state = STATE_BACKOFF_START;
	    break;
	  case EXAMINE_RUV_GENERATION_MISMATCH:
	    slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
			"%s: Replica has a different generation ID than the local data.\n",
		    agmt_get_long_name(prp->agmt));
	    next_state = STATE_BACKOFF_START;
	    break;
	  case EXAMINE_RUV_REPLICA_TOO_OLD:
	    slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
			"%s: Replica update vector is too out of date to bring "
		    "into sync using the incremental protocol. The replica "
		    "must be reinitialized.\n", agmt_get_long_name(prp->agmt));
	    next_state = STATE_BACKOFF_START;
	    break;
	  case EXAMINE_RUV_OK:
	    /* update our csn generator state with the consumer's ruv data */
	    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> windows_examine_update_vector OK");
	    object_acquire(prp->replica_object);
	    replica = object_get_data(prp->replica_object);
	    rc = replica_update_csngen_state (replica, ruv); 
	    object_release (prp->replica_object);
	    replica = NULL;
	    if (rc != 0) /* too much skew */
	      {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
			"%s: Incremental protocol: fatal error - too much time skew between replicas!\n",
			agmt_get_long_name(prp->agmt));
		next_state = STATE_STOP_FATAL_ERROR;
	      }   
	    else
	      {
		rc = send_updates(prp, ruv, &num_changes_sent);
		if (rc == UPDATE_NO_MORE_UPDATES)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_NO_MORE_UPDATES -> STATE_WAIT_CHANGES");
		    agmt_set_last_update_status(prp->agmt, 0, 0, "Incremental update succeeded");
		    next_state = STATE_WAIT_CHANGES;
		  }
		else if (rc == UPDATE_YIELD)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_YIELD -> STATE_BACKOFF_START");
		    agmt_set_last_update_status(prp->agmt, 0, 0, "Incremental update succeeded and yielded");
		    next_state = STATE_BACKOFF_START;
		  }
		else if (rc == UPDATE_TRANSIENT_ERROR)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_TRANSIENT_ERROR -> STATE_BACKOFF_START");
		    next_state = STATE_BACKOFF_START;
		  }
		else if (rc == UPDATE_FATAL_ERROR)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_FATAL_ERROR -> STATE_STOP_FATAL_ERROR");
		    next_state = STATE_STOP_FATAL_ERROR;
		  }
		else if (rc == UPDATE_SCHEDULE_WINDOW_CLOSED)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_SCHEDULE_WINDOW_CLOSED -> STATE_WAIT_WINDOW_OPEN");
		    /* ONREPL - I don't think we should check this. We might be
		       here because of replicate_now event - so we don't care
		       about the schedule */
		    next_state = STATE_WAIT_WINDOW_OPEN;
		    /* ONREPL - do we need to release the replica here ? */
		    windows_conn_disconnect (prp->conn);
		  }
		else if (rc == UPDATE_CONNECTION_LOST)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_CONNECTION_LOST -> STATE_BACKOFF_START");
		    next_state = STATE_BACKOFF_START;
		  }
		else if (rc == UPDATE_TIMEOUT)
		  {
		    dev_debug("windows_inc_run(STATE_SENDING_UPDATES) -> send_updates = UPDATE_TIMEOUT -> STATE_BACKOFF_START");
		    next_state = STATE_BACKOFF_START;
		  }
	      }
	    last_start_time = 0UL;
	    break;
	  }

	if ( run_dirsync )
	{
		windows_dirsync_inc_run(prp);
		windows_private_save_dirsync_cookie(prp->agmt);
		run_dirsync = PR_FALSE;
	}

	agmt_set_last_update_end(prp->agmt, current_time());
	agmt_set_update_in_progress(prp->agmt, PR_FALSE);
	/* If timed out, close the connection after released the replica */
	windows_release_replica(prp);
	if (rc == UPDATE_TIMEOUT) {
	  windows_conn_disconnect(prp->conn);
	}
	if (rc == UPDATE_NO_MORE_UPDATES && num_changes_sent > 0)
	{
	  if (pausetime > 0)
	  {
	    /* richm - 20020219 - If we have acquired the consumer, and another master has gone
	       into backoff waiting for us to release it, we may acquire the replica sooner
	       than the other master has a chance to, and the other master may not be able
	       to acquire the consumer for a long time (hours, days?) if this server is
	       under a heavy load (see reliab06 et. al. system tests)
	       So, this sleep gives the other master(s) a chance to acquire the consumer
	       replica */
	      long loops = pausetime;
	      /* the while loop is so that we don't just sleep and sleep if an
		 event comes in that we should handle immediately (like shutdown) */
	      slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			      "%s: Pausing updates for %ld seconds to allow other suppliers to update consumer\n",
			      agmt_get_long_name(prp->agmt), pausetime);
	      while (loops-- && !(PROTOCOL_IS_SHUTDOWN(prp)))
	        {
		    DS_Sleep(PR_SecondsToInterval(1));
		}
	  }
	  else if (num_changes_sent > 10)
	  {
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
	/* XXXggood update state in replica */
	agmt_set_last_update_status(prp->agmt, -1, 0, "Incremental update has failed and requires administrator action");
	dev_debug("windows_inc_run(STATE_STOP_FATAL_ERROR)");
	slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
		"%s: Incremental update failed and requires administrator action\n",
		agmt_get_long_name(prp->agmt));
	next_state = STATE_STOP_FATAL_ERROR_PART2;
	break;
      case STATE_STOP_FATAL_ERROR_PART2:
	if (PROTOCOL_IS_SHUTDOWN(prp))
	  {
	    done = 1;
	    break;
	  } 

	/* MAB: This state is the FATAL state where we are supposed to get
	   as a result of a FATAL error on send_updates. But, as bug 
	   states, send_updates was always returning TRANSIENT errors and never
	   FATAL... In other words, this code has never been tested before...

	   As of 01/16/01, this piece of code was in a very dangerous state. In particular, 
		1) it does not catch any events
		2) it is a terminal state (once reached it never transitions to a different state)

	   Both things combined make this state to become a consuming infinite loop
	   that is useless after all (we are in a fatal place requiring manual admin jobs */

	/* MAB: The following lines fix problem number 1 above... When the code gets
	   into this state, it should only get a chance to get out of it by an
	   EVENT_AGMT_CHANGED event... All other events should be ignored */
	else if (event_occurred(prp, EVENT_AGMT_CHANGED))
	  {
	    dev_debug("windows_inc_run(STATE_STOP_FATAL_ERROR): EVENT_AGMT_CHANGED received\n");
	    /* Chance to recover for the EVENT_AGMT_CHANGED event. 
	       This is not mandatory, but fixes problem 2 above */
	    next_state = STATE_STOP_NORMAL_TERMINATION;
	  }
	else
	  {
	    dev_debug("windows_inc_run(STATE_STOP_FATAL_ERROR): Event received. Clearing it\n");
	    reset_events (prp);
	  }

	protocol_sleep (prp, PR_INTERVAL_NO_TIMEOUT);
	break;
		
      case STATE_STOP_NORMAL_TERMINATION:
	/*
	 * We encountered some sort of a fatal error. Return.
	 */
	/* XXXggood update state in replica */
	dev_debug("windows_inc_run(STATE_STOP_NORMAL_TERMINATION)");
	done = 1;
	break;
      }

    slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
		"%s: State: %s -> %s\n",
	    agmt_get_long_name(prp->agmt),
	    state2name(current_state), state2name(next_state));


    current_state = next_state;
  } while (!done);
  slapi_ch_free((void**)&hostname);
  /* remove_protocol_callbacks(prp); */
  prp->stopped = 1;
  /* Cancel any linger timer that might be in effect... */
  conn_cancel_linger(prp->conn);
  /* ... and disconnect, if currently connected */
  conn_disconnect(prp->conn);
  LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_run\n", 0, 0, 0 );
}



/*
 * Go to sleep until awakened.
 */
static void
protocol_sleep(Private_Repl_Protocol *prp, PRIntervalTime duration)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> protocol_sleep\n", 0, 0, 0 );
	PR_ASSERT(NULL != prp);
	PR_Lock(prp->lock);
    /* we should not go to sleep if there are events available to be processed.
       Otherwise, we can miss the event that suppose to wake us up */
    if (prp->eventbits == 0)
	    PR_WaitCondVar(prp->cvar, duration);
    else
    {
        slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"%s: Incremental protocol: can't go to sleep: event bits - %x\n",
			agmt_get_long_name(prp->agmt), prp->eventbits);
    }
	PR_Unlock(prp->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= protocol_sleep\n", 0, 0, 0 );
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
	LDAPDebug( LDAP_DEBUG_TRACE, "=> event_notify\n", 0, 0, 0 );
	PR_ASSERT(NULL != prp);
	PR_Lock(prp->lock);
	prp->eventbits |= event;
	PR_NotifyCondVar(prp->cvar);
	PR_Unlock(prp->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= event_notify\n", 0, 0, 0 );
}


/*
 * Test to see if an event occurred. The event is cleared when
 * read.
 */
static PRUint32
event_occurred(Private_Repl_Protocol *prp, PRUint32 event)
{
	PRUint32 return_value;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> event_occurred\n", 0, 0, 0 );

	PR_ASSERT(NULL != prp);
	PR_Lock(prp->lock);
	return_value = (prp->eventbits & event);
	prp->eventbits &= ~event; /* Clear event */
	PR_Unlock(prp->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= event_occurred\n", 0, 0, 0 );
	return return_value;
}

static void
reset_events (Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> reset_events\n", 0, 0, 0 );
	PR_ASSERT(NULL != prp);
	PR_Lock(prp->lock);
	prp->eventbits = 0;
	PR_Unlock(prp->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= reset_events\n", 0, 0, 0 );
}



static PRBool
is_dummy_operation (const slapi_operation_parameters *op)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> is_dummy_operation\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= is_dummy_operation\n", 0, 0, 0 );
    return (strcmp (op->target_address.uniqueid, START_ITERATION_ENTRY_UNIQUEID) == 0);
}



void
w_cl5_operation_parameters_done (struct slapi_operation_parameters *sop)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> w_cl5_operation_parameters_done\n", 0, 0, 0 );
	if(sop!=NULL) {
		switch(sop->operation_type) 
		{
		case SLAPI_OPERATION_BIND:
			slapi_ch_free((void **)&(sop->p.p_bind.bind_saslmechanism));
			if (sop->p.p_bind.bind_creds)
				ber_bvecfree((struct berval**)&(sop->p.p_bind.bind_creds));
			if (sop->p.p_bind.bind_ret_saslcreds)
				ber_bvecfree((struct berval**)&(sop->p.p_bind.bind_ret_saslcreds));
			sop->p.p_bind.bind_creds = NULL;
			sop->p.p_bind.bind_ret_saslcreds = NULL;
			break;
		case SLAPI_OPERATION_COMPARE:
			ava_done((struct ava *)&(sop->p.p_compare.compare_ava));
			break;
		case SLAPI_OPERATION_SEARCH:
			slapi_ch_free((void **)&(sop->p.p_search.search_strfilter));
			charray_free(sop->p.p_search.search_attrs);
			slapi_filter_free(sop->p.p_search.search_filter,1);
			break;
		case SLAPI_OPERATION_MODRDN:
			sop->p.p_modrdn.modrdn_deloldrdn = 0;
			break;
		case SLAPI_OPERATION_EXTENDED:
			slapi_ch_free((void **)&(sop->p.p_extended.exop_oid));
			if (sop->p.p_extended.exop_value)
				ber_bvecfree((struct berval**)&(sop->p.p_extended.exop_value));
			sop->p.p_extended.exop_value = NULL;
			break;
		default:
			break;
		}
	}
	operation_parameters_done(sop);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= w_cl5_operation_parameters_done\n", 0, 0, 0 );
}



/*
 * Send a set of updates to the replica.  Assumes that (1) the replica
 * has already been acquired, (2) that the consumer's update vector has
 * been checked and (3) that it's ok to send incremental updates.
 * Returns:
 * UPDATE_NO_MORE_UPDATES - all updates were sent succussfully
 * UPDATE_TRANSIENT_ERROR - some non-permanent error occurred. Try again later.
 * UPDATE_FATAL_ERROR - some bad, permanent error occurred.
 * UPDATE_SCHEDULE_WINDOW_CLOSED - the schedule window closed on us.
 */
static int
send_updates(Private_Repl_Protocol *prp, RUV *remote_update_vector, PRUint32 *num_changes_sent)
{
	CL5Entry entry;
	slapi_operation_parameters op;
	int return_value;
	int rc;
	CL5ReplayIterator *changelog_iterator = NULL;
	RUV *current_ruv = ruv_dup(remote_update_vector);

	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_updates\n", 0, 0, 0 );

	*num_changes_sent = 0;
	/*
	 * Iterate over the changelog. Retrieve each update,
	 * construct an appropriate LDAP operation,
	 * attaching the CSN, and send the change.
	 */
    
	
	rc = cl5CreateReplayIteratorEx( prp, remote_update_vector, &changelog_iterator, agmt_get_consumerRID(prp->agmt)); 
	if (CL5_SUCCESS != rc)
	{
		switch (rc)
		{
		case CL5_BAD_DATA: /* invalid parameter passed to the function */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Invalid parameter passed to cl5CreateReplayIterator\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_BAD_FORMAT:     /* db data has unexpected format */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Unexpected format encountered in changelog database\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_BAD_STATE: /* changelog is in an incorrect state for attempted operation */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Changelog database was in an incorrect state\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_BAD_DBVERSION:  /* changelog has invalid dbversion */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Incorrect dbversion found in changelog database\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_DB_ERROR:       /* database error */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: A changelog database error was encountered\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_NOTFOUND:       /* we have no changes to send */
			slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"%s: No changes to send\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_NO_MORE_UPDATES;
			break;
		case CL5_MEMORY_ERROR:   /* memory allocation failed */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Memory allocation error occurred\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_SYSTEM_ERROR:   /* NSPR error occurred: use PR_GetError for furhter info */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: An NSPR error (%d) occurred\n",
				agmt_get_long_name(prp->agmt), PR_GetError());
			return_value = UPDATE_TRANSIENT_ERROR;
			break;
		case CL5_CSN_ERROR:      /* CSN API failed */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: A CSN API failure was encountered\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_TRANSIENT_ERROR;
			break;
		case CL5_RUV_ERROR:      /* RUV API failed */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: An RUV API failure occurred\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_TRANSIENT_ERROR;
			break;
		case CL5_OBJSET_ERROR:   /* namedobjset api failed */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: A namedobject API failure occurred\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_TRANSIENT_ERROR;
			break;
		case CL5_PURGED_DATA:    /* requested data has been purged */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Data required to update replica has been purged. "
				"The replica must be reinitialized.\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_MISSING_DATA:   /* data should be in the changelog, but is missing */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: Missing data encountered\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_FATAL_ERROR;
			break;
		case CL5_UNKNOWN_ERROR:   /* unclassified error */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: An unknown error was ecountered\n",
				agmt_get_long_name(prp->agmt));
			return_value = UPDATE_TRANSIENT_ERROR;
			break;
		default:
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"%s: An unknown error (%d) occurred "
				"(cl5CreateReplayIterator)\n",
				agmt_get_long_name(prp->agmt), rc);
			return_value = UPDATE_TRANSIENT_ERROR;
		}
	}
	else
	{
		int finished = 0;
		ConnResult replay_crc;
        char csn_str[CSN_STRSIZE];

		memset ( (void*)&op, 0, sizeof (op) );
		entry.op = &op;
		do {
			int mark_record_done = 0;
			w_cl5_operation_parameters_done ( entry.op );
			memset ( (void*)entry.op, 0, sizeof (op) );
			rc = cl5GetNextOperationToReplay(changelog_iterator, &entry);
			switch (rc)
			{
			case CL5_SUCCESS:
                /* check that we don't return dummy entries */
                if (is_dummy_operation (entry.op))
                {
                    slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, 
                        "%s: changelog iteration code returned a dummy entry with csn %s, "
                        "skipping ...\n",
						agmt_get_long_name(prp->agmt), csn_as_string(entry.op->csn, PR_FALSE, csn_str));
				    continue;
                }
				/* This is where the work actually happens: */
				replay_crc = windows_replay_update(prp, entry.op);
				if (CONN_OPERATION_SUCCESS != replay_crc)
				{
					int operation, error;
					conn_get_error(prp->conn, &operation, &error);
					csn_as_string(entry.op->csn, PR_FALSE, csn_str);
					/* Figure out what to do next */
					if (CONN_OPERATION_FAILED == replay_crc)
					{
						/* Map ldap error code to return value */
						if (!ignore_error_and_keep_going(error))
						{
							return_value = UPDATE_TRANSIENT_ERROR;
							finished = 1;
						}
						else
						{
							agmt_inc_last_update_changecount (prp->agmt, csn_get_replicaid(entry.op->csn), 1 /*skipped*/);
							mark_record_done = 1;
						}
						slapi_log_error(finished ? SLAPI_LOG_FATAL : slapi_log_urp, windows_repl_plugin_name,
							"%s: Consumer failed to replay change (uniqueid %s, CSN %s): %s. %s.\n",
							agmt_get_long_name(prp->agmt),
							entry.op->target_address.uniqueid, csn_str,
							ldap_err2string(error),
							finished ? "Will retry later" : "Skipping");
					}
					else if (CONN_NOT_CONNECTED == replay_crc)
					{
						/* We lost the connection - enter backoff state */

						return_value = UPDATE_TRANSIENT_ERROR;
						finished = 1;
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"%s: Consumer failed to replay change (uniqueid %s, CSN %s): "
							"%s. Will retry later.\n",
							agmt_get_long_name(prp->agmt),
							entry.op->target_address.uniqueid, csn_str,
							error ? ldap_err2string(error) : "Connection lost");
					}
					else if (CONN_TIMEOUT == replay_crc)
					{
						return_value = UPDATE_TIMEOUT;
						finished = 1;
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"%s: Consumer timed out to replay change (uniqueid %s, CSN %s): "
							"%s.\n",
							agmt_get_long_name(prp->agmt),
							entry.op->target_address.uniqueid, csn_str,
							error ? ldap_err2string(error) : "Timeout");
					}
					else if (CONN_LOCAL_ERROR == replay_crc)
					{
						/*
						 * Something bad happened on the local server - enter 
						 * backoff state.
						 */
						return_value = UPDATE_TRANSIENT_ERROR;
						finished = 1;
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"%s: Failed to replay change (uniqueid %s, CSN %s): "
							"Local error. Will retry later.\n",
							agmt_get_long_name(prp->agmt),
							entry.op->target_address.uniqueid, csn_str);
					}
						
				}
				else
				{
					/* Positive response received */
					(*num_changes_sent)++;
					agmt_inc_last_update_changecount (prp->agmt, csn_get_replicaid(entry.op->csn), 0 /*replayed*/);
					mark_record_done = 1;
				}
				if (mark_record_done)
				{
					/* bring the consumers (AD) RUV up to date */
					ruv_force_csn_update(current_ruv, entry.op->csn);
				}
				break;
			case CL5_BAD_DATA:
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: Invalid parameter passed to cl5GetNextOperationToReplay\n",
					agmt_get_long_name(prp->agmt));
				return_value = UPDATE_FATAL_ERROR;
				finished = 1;
				break;
			case CL5_NOTFOUND:
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"%s: No more updates to send (cl5GetNextOperationToReplay)\n",
					agmt_get_long_name(prp->agmt));
				return_value = UPDATE_NO_MORE_UPDATES;
				finished = 1;
				break;
			case CL5_DB_ERROR:
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: A database error occurred (cl5GetNextOperationToReplay)\n",
					agmt_get_long_name(prp->agmt));
				return_value = UPDATE_FATAL_ERROR;
				finished = 1;
				break;
			case CL5_BAD_FORMAT:
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: A malformed changelog entry was encountered (cl5GetNextOperationToReplay)\n",
					agmt_get_long_name(prp->agmt));
				break;
			case CL5_MEMORY_ERROR:
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: A memory allocation error occurred (cl5GetNextOperationToRepla)\n",
					agmt_get_long_name(prp->agmt));
				return_value = UPDATE_FATAL_ERROR;
				break;
			default:
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: Unknown error code (%d) returned from cl5GetNextOperationToReplay\n",
					agmt_get_long_name(prp->agmt), rc);
				return_value = UPDATE_TRANSIENT_ERROR;
				break;
			}
			/* Check for protocol shutdown */
			if (prp->terminate)
			{
				return_value = UPDATE_NO_MORE_UPDATES;
				finished = 1;
			}
			if (*num_changes_sent >= MAX_CHANGES_PER_SESSION)
			{
				return_value = UPDATE_YIELD;
				finished = 1;
			}
		} while (!finished);
		w_cl5_operation_parameters_done ( entry.op );
		cl5DestroyReplayIterator(&changelog_iterator);
	}
	/* Save the RUV that we successfully replayed, this ensures that next time we start off at the next changelog record */
	if (current_ruv)
	{
		agmt_set_consumer_ruv(prp->agmt,current_ruv);
		ruv_destroy(&current_ruv);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= send_updates\n", 0, 0, 0 );
	return return_value;
}



/*
 * XXXggood this should probably be in the superclass, since the full update
 * protocol is going to need it too.
 */
static int
windows_inc_stop(Private_Repl_Protocol *prp)
{
	int return_value;
	PRIntervalTime start, maxwait, now;
	int seconds = 1200;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_stop\n", 0, 0, 0 );

	maxwait = PR_SecondsToInterval(seconds);
	prp->terminate = 1;
	event_notify(prp, EVENT_PROTOCOL_SHUTDOWN);
	start = PR_IntervalNow();
	now = start;
	while (!prp->stopped && ((now - start) < maxwait))
	{
		DS_Sleep(PR_SecondsToInterval(1));
		now = PR_IntervalNow();
	}
	if (!prp->stopped)
	{
		/* Isn't listening. Do something drastic. */
		return_value = -1;
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"%s: windows_inc_stop: protocol does not stop after %d seconds\n",
				agmt_get_long_name(prp->agmt), seconds);
	}
	else
	{
		return_value = 0;
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"%s: windows_inc_stop: protocol stopped after %d seconds\n",
				agmt_get_long_name(prp->agmt),
				PR_IntervalToSeconds(now-start));
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_stop\n", 0, 0, 0 );
	return return_value;
}



static int
windows_inc_status(Private_Repl_Protocol *prp)
{
	int return_value = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_status\n", 0, 0, 0 );
	
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_status\n", 0, 0, 0 );

	return return_value;
}



static void
windows_inc_notify_update(Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_notify_update\n", 0, 0, 0 );
	event_notify(prp, EVENT_TRIGGERING_CRITERIA_MET);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_notify_update\n", 0, 0, 0 );
}


static void
windows_inc_update_now(Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_update_now\n", 0, 0, 0 );
	event_notify(prp, EVENT_REPLICATE_NOW);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_update_now\n", 0, 0, 0 );
}


static void
windows_inc_notify_agmt_changed(Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_notify_agmt_changed\n", 0, 0, 0 );
	event_notify(prp, EVENT_AGMT_CHANGED);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_notify_agmt_changed\n", 0, 0, 0 );
}

static void 
windows_inc_notify_window_opened (Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_notify_window_opened\n", 0, 0, 0 );
    event_notify(prp, EVENT_WINDOW_OPENED);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_notify_window_opened\n", 0, 0, 0 );
}

static void 
windows_inc_notify_window_closed (Private_Repl_Protocol *prp)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_notify_window_closed\n", 0, 0, 0 );
    event_notify(prp, EVENT_WINDOW_CLOSED);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_notify_window_closed\n", 0, 0, 0 );
}


Private_Repl_Protocol *
Windows_Inc_Protocol_new(Repl_Protocol *rp)
{
	windows_inc_private *rip = NULL;
	Private_Repl_Protocol *prp = (Private_Repl_Protocol *)slapi_ch_malloc(sizeof(Private_Repl_Protocol));

	LDAPDebug( LDAP_DEBUG_TRACE, "=> Windows_Inc_Protocol_new\n", 0, 0, 0 );

	prp->delete = windows_inc_delete;
	prp->run = windows_inc_run;
	prp->stop = windows_inc_stop;
	prp->status = windows_inc_status;
	prp->notify_update = windows_inc_notify_update;
	prp->notify_agmt_changed = windows_inc_notify_agmt_changed;
    prp->notify_window_opened = windows_inc_notify_window_opened;
    prp->notify_window_closed = windows_inc_notify_window_closed;
	prp->update_now = windows_inc_update_now;
	prp->replica_object = prot_get_replica_object(rp);
	if ((prp->lock = PR_NewLock()) == NULL)
	{
		goto loser;
	}
	if ((prp->cvar = PR_NewCondVar(prp->lock)) == NULL)
	{
		goto loser;
	}
	prp->stopped = 0;
	prp->terminate = 0;
	prp->eventbits = 0;
	prp->conn = prot_get_connection(rp);
	prp->agmt = prot_get_agreement(rp);
	prp->last_acquire_response_code = NSDS50_REPL_REPLICA_READY;
	rip = (void *)slapi_ch_malloc(sizeof(windows_inc_private));
	rip->ruv = NULL;
	rip->backoff = NULL;
	rip->rp = rp;
	prp->private = (void *)rip;
    prp->replica_acquired = PR_FALSE;

	LDAPDebug( LDAP_DEBUG_TRACE, "<= Windows_Inc_Protocol_new\n", 0, 0, 0 );

	return prp;

loser:
	windows_inc_delete(&prp);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= Windows_Inc_Protocol_new (loser)\n", 0, 0, 0 );
	return NULL;
}




static void
windows_inc_backoff_expired(time_t timer_fire_time, void *arg)
{
	Private_Repl_Protocol *prp = (Private_Repl_Protocol *)arg;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_inc_backoff_expired\n", 0, 0, 0 );

	PR_ASSERT(NULL != prp);
	event_notify(prp, EVENT_BACKOFF_EXPIRED);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_inc_backoff_expired\n", 0, 0, 0 );
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
windows_examine_update_vector(Private_Repl_Protocol *prp, RUV *remote_ruv)
{
	int return_value;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_examine_update_vector\n", 0, 0, 0 );

	PR_ASSERT(NULL != prp);
	if (NULL == prp)
	{
		return_value = EXAMINE_RUV_PARAM_ERROR;
	}
	else if (NULL == remote_ruv)
	{
		return_value = EXAMINE_RUV_PRISTINE_REPLICA;
	}
	else
	{
		char *local_gen = NULL;
		char *remote_gen = ruv_get_replica_generation(remote_ruv);
		Object *local_ruv_obj;
		RUV *local_ruv;
		Replica *replica;

		PR_ASSERT(NULL != prp->replica_object);
		replica = object_get_data(prp->replica_object);
		PR_ASSERT(NULL != replica);
		local_ruv_obj = replica_get_ruv (replica);
		if (NULL != local_ruv_obj)
		{
			local_ruv = (RUV*) object_get_data (local_ruv_obj);
			PR_ASSERT (local_ruv);
			local_gen = ruv_get_replica_generation(local_ruv);
			object_release (local_ruv_obj);
		}
		if (NULL == remote_gen || NULL == local_gen || strcmp(remote_gen, local_gen) != 0)
		{
			return_value = EXAMINE_RUV_GENERATION_MISMATCH;
		}
		else
		{
			return_value = EXAMINE_RUV_OK;
		}
		slapi_ch_free((void**)&remote_gen);
		slapi_ch_free((void**)&local_gen);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_examine_update_vector\n", 0, 0, 0 );
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
static PRBool
ignore_error_and_keep_going(int error)
{
	int return_value;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> ignore_error_and_keep_going\n", 0, 0, 0 );

	switch (error)
	{
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
	LDAPDebug( LDAP_DEBUG_TRACE, "<= ignore_error_and_keep_going\n", 0, 0, 0 );
	return return_value;
}

/* this function converts an aquisition code to a string - for debug output */
static const char* 
acquire2name (int code)
{
    switch (code)
    {
        case ACQUIRE_SUCCESS:                       return "success";
        case ACQUIRE_REPLICA_BUSY:            return "replica_busy";
        case ACQUIRE_FATAL_ERROR:                return "fatal_error";
        case ACQUIRE_CONSUMER_WAS_UPTODATE:            return "consumer_was_uptodate";
        case ACQUIRE_TRANSIENT_ERROR:               return "transient_error";
        default:                                return "invalid_code";
    }
}


/* this function converts a state to its name - for debug output */
static const char* 
state2name (int state)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> state2name\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= state2name\n", 0, 0, 0 );
    switch (state)
    {
        case STATE_START:                       return "start";
        case STATE_WAIT_WINDOW_OPEN:            return "wait_for_window_to_open";
        case STATE_WAIT_CHANGES:                return "wait_for_changes";
        case STATE_READY_TO_ACQUIRE:            return "ready_to_acquire_replica";
        case STATE_BACKOFF_START:               return "start_backoff";
        case STATE_BACKOFF:                     return "backoff";
        case STATE_SENDING_UPDATES:             return "sending_updates";
        case STATE_STOP_FATAL_ERROR:            return "stop_fatal_error";
        case STATE_STOP_FATAL_ERROR_PART2:      return "stop_fatal_error";
        case STATE_STOP_NORMAL_TERMINATION:     return "stop_normal_termination";
        default:                                return "invalid_state";
    }
}

/* this function convert s an event to its name - for debug output */
static const char* 
event2name (int event)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> event2name\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= event2name\n", 0, 0, 0 );
    switch (event)
    {
        case EVENT_WINDOW_OPENED:           return "update_window_opened";
        case EVENT_WINDOW_CLOSED:           return "update_window_closed"; 
        case EVENT_TRIGGERING_CRITERIA_MET: return "data_modified";
        case EVENT_BACKOFF_EXPIRED:         return "backoff_timer_expired";  
        case EVENT_REPLICATE_NOW:           return "replicate_now";
        case EVENT_PROTOCOL_SHUTDOWN:       return "protocol_shutdown";
        case EVENT_AGMT_CHANGED:            return "agreement_changed";
        case EVENT_RUN_DIRSYNC:            return "run_dirsync";
        default:                            return "invalid_event";
    }
}


static void 
periodic_dirsync(time_t when, void *arg)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> periodic_dirsync\n", 0, 0, 0 );

	slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"Running Dirsync \n");

	event_notify( (Private_Repl_Protocol*) arg, EVENT_RUN_DIRSYNC);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= periodic_dirsync\n", 0, 0, 0 );
}
