/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl5_protocol_util.c */
/*

Code common to both incremental and total protocols.

*/

#include "repl5.h"
// #include "windows_prot_private.h"
#include "repl5_prot_private.h"
#include "windowsrepl.h"


int ruv_private_new( RUV **ruv, RUV *clone );

/*
 * Acquire exclusive access to a replica. Send a start replication extended
 * operation to the replica. The response will contain a success code, and
 * optionally the replica's update vector if acquisition is successful.
 * This function returns one of the following:
 * ACQUIRE_SUCCESS - the replica was acquired, and we have exclusive update access
 * ACQUIRE_REPLICA_BUSY - another master was updating the replica
 * ACQUIRE_FATAL_ERROR - something bad happened, and it's not likely to improve
 *                       if we wait.
 * ACQUIRE_TRANSIENT_ERROR - something bad happened, but it's probably worth
 *                           another try after waiting a while.
 * If ACQUIRE_SUCCESS is returned, then ruv will point to the replica's update
 * vector. It's possible that the replica does something goofy and doesn't
 * return us an update vector, so be prepared for ruv to be NULL (but this is
 * an error).
 */
int
windows_acquire_replica(Private_Repl_Protocol *prp, RUV **ruv)
{
  char * prot_oid = REPL_NSDS50_INCREMENTAL_PROTOCOL_OID; //xXX get rid of this
	int return_value = ACQUIRE_SUCCESS;
	ConnResult crc;
	Repl_Connection *conn;

	PR_ASSERT(prp);

    if (prp->replica_acquired)  /* we already acquire replica */
    {
        slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
						"%s: Remote replica already acquired\n",
						agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
        return ACQUIRE_SUCCESS;
    }

	//if (NULL != ruv)
	//{		ruv_destroy ( ruv ); 	}

	{
		Replica *replica;
		Object *supl_ruv_obj, *cons_ruv_obj;
		PRBool is_newer = PR_FALSE;
		RUV *r;

	
		if (prp->agmt)
		{		
			cons_ruv_obj = agmt_get_consumer_ruv(prp->agmt);
		}





		object_acquire(prp->replica_object);
		replica = object_get_data(prp->replica_object);
		supl_ruv_obj = replica_get_ruv ( replica );

		/* make a copy of the existing RUV as a starting point 
		   XXX this is probably a not-so-elegant hack */

		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV:\n");
		if (supl_ruv_obj) {
		object_acquire(supl_ruv_obj);
			ruv_dump ((RUV*)  object_get_data ( supl_ruv_obj ), "supplier", NULL);
			object_release(supl_ruv_obj);
		}else
				slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV = null\n");
		
		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, consumer RUV:\n");

		if (cons_ruv_obj) {
			RUV* con;
			object_acquire(cons_ruv_obj);
			con =  (RUV*) object_get_data ( cons_ruv_obj );
			ruv_dump (con,"consumer", NULL);
			object_release( cons_ruv_obj );
		}else
			slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, consumer RUV = null\n");


		is_newer = ruv_is_newer ( supl_ruv_obj, cons_ruv_obj );
		
		/* This follows ruv_is_newer, since it's always newer if it's null */
		if (cons_ruv_obj == NULL) 
		{
			RUV *s; // int rc;
			s = (RUV*)  object_get_data ( replica_get_ruv ( replica ) );
			
			agmt_set_consumer_ruv(prp->agmt, s );
			object_release ( replica_get_ruv ( replica ) );
			cons_ruv_obj =  agmt_get_consumer_ruv(prp->agmt);		
		}

		r = (RUV*)  object_get_data ( cons_ruv_obj); 
		*ruv = r;
		


		if ( supl_ruv_obj ) object_release ( supl_ruv_obj );
		if ( cons_ruv_obj ) object_release ( cons_ruv_obj );
		object_release (prp->replica_object);
		replica = NULL;

 		if (is_newer == PR_FALSE) { 
 			prp->last_acquire_response_code = NSDS50_REPL_UPTODATE; 
 			return ACQUIRE_CONSUMER_WAS_UPTODATE; 
 		} 
	}

	prp->last_acquire_response_code = NSDS50_REPL_REPLICA_NO_RESPONSE;

	/* Get the connection */
	conn = prp->conn;

	crc = windows_conn_connect(conn);
	if (CONN_OPERATION_FAILED == crc)
	{
		return_value = ACQUIRE_TRANSIENT_ERROR;
	}
	else if (CONN_SSL_NOT_ENABLED == crc)
	{
		return_value = ACQUIRE_FATAL_ERROR;
	}
	else
	{
		/* we don't want the timer to go off in the middle of an operation */
		windows_conn_cancel_linger(conn);
		/* Does the remote replica support the dirsync protocol? 
	       if it update the conn object */
		windows_conn_replica_supports_dirsync(conn); 
		if (CONN_NOT_CONNECTED == crc || CONN_OPERATION_FAILED == crc)
		{
			/* We don't know anything about the remote replica. Try again later. */
			return_value = ACQUIRE_TRANSIENT_ERROR;
		}
		else
		{
			/* Good to go. Start the protocol. */
			CSN *current_csn = NULL;
			Slapi_DN *replarea_sdn;

			/* Obtain a current CSN */
			replarea_sdn = agmt_get_replarea(prp->agmt);
			current_csn = get_current_csn(replarea_sdn);
			if (NULL != current_csn)
			{
			  
			  /* Save consumer's RUV in the replication agreement.
                               It is used by the changelog trimming code */
 // if (ruv && *ruv)  agmt_set_consumer_ruv (prp->agmt, *ruv);  XXX deadlock?
// XXX
			    return_value = ACQUIRE_SUCCESS;
			    


			}
			else
			{
				/* Couldn't get a current CSN */
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: Unable to obtain current CSN. "
					"Replication is aborting.\n",
					agmt_get_long_name(prp->agmt));
				return_value = ACQUIRE_FATAL_ERROR;
			}
			slapi_sdn_free(&replarea_sdn);
		}
	}

	if (ACQUIRE_SUCCESS != return_value)
	{
		/* could not acquire the replica, so reinstate the linger timer, since this
		   means we won't call release_replica, which also reinstates the timer */
	     windows_conn_start_linger(conn);
	}
    else
    {
        /* replica successfully acquired */
        prp->replica_acquired = PR_TRUE;
    }

	return return_value;
}

void
windows_release_replica(Private_Repl_Protocol *prp)
{

  struct berval *retdata = NULL;
  char *retoid = NULL;
  struct berval *payload = NULL;
  Slapi_DN *replarea_sdn = NULL;

  PR_ASSERT(NULL != prp);
  PR_ASSERT(NULL != prp->conn);

  if (!prp->replica_acquired)
    return;

  windows_conn_start_linger(prp->conn);

  prp->replica_acquired = PR_FALSE;

}
