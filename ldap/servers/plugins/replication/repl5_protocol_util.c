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

/* repl5_protocol_util.c */
/*

Code common to both incremental and total protocols.

*/

#include "repl5.h"
#include "repl5_prot_private.h"


/*
 * Obtain a current CSN (e.g. one that would have been
 * generated for an operation occurring at this time)
 * for a given replica.
 */
CSN *
get_current_csn(Slapi_DN *replarea_sdn)
{
	Object *replica_obj;
	Replica *replica;
	Object *gen_obj;
	CSNGen *gen;
	CSN *current_csn = NULL;

	if (NULL != replarea_sdn)
	{
		replica_obj = replica_get_replica_from_dn(replarea_sdn);
		if (NULL != replica_obj)
		{
			replica = object_get_data(replica_obj);
			if (NULL != replica)
			{
				gen_obj = replica_get_csngen(replica);
				if (NULL != gen_obj)
				{
					gen = (CSNGen *)object_get_data(gen_obj);
					if (NULL != gen)
					{
						if (csngen_new_csn(gen, &current_csn,
							PR_FALSE /* notify */) != CSN_SUCCESS)
						{
							current_csn = NULL;
							
						}
						object_release(gen_obj);
					}
				}
			}
		}
	}
	return current_csn;
}
	
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
acquire_replica(Private_Repl_Protocol *prp, char *prot_oid, RUV **ruv)
{
	int return_value;
	ConnResult crc;
	Repl_Connection *conn;

	PR_ASSERT(prp && prot_oid);

    if (prp->replica_acquired)  /* we already acquire replica */
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Remote replica already acquired\n",
						agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
        return ACQUIRE_SUCCESS;
    }

	if (NULL != ruv)
	{
		ruv_destroy ( ruv );
	}

	if (strcmp(prot_oid, REPL_NSDS50_INCREMENTAL_PROTOCOL_OID) == 0)
	{
		Replica *replica;
		Object *supl_ruv_obj, *cons_ruv_obj;
		PRBool is_newer = PR_FALSE;

		object_acquire(prp->replica_object);
		replica = object_get_data(prp->replica_object);
		supl_ruv_obj = replica_get_ruv ( replica );
		cons_ruv_obj = agmt_get_consumer_ruv ( prp->agmt );
		is_newer = ruv_is_newer ( supl_ruv_obj, cons_ruv_obj );
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

	crc = conn_connect(conn);
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
		conn_cancel_linger(conn);
		/* Does the remote replica support the 5.0 protocol? */
		crc = conn_replica_supports_ds5_repl(conn);
		if (CONN_DOES_NOT_SUPPORT_DS5_REPL == crc)
		{
			return_value = ACQUIRE_FATAL_ERROR;
		}
		else if (CONN_NOT_CONNECTED == crc || CONN_OPERATION_FAILED == crc)
		{
			/* We don't know anything about the remote replica. Try again later. */
			return_value = ACQUIRE_TRANSIENT_ERROR;
		}
		else
		{
			/* Does the remote replica support the 7.1 protocol? */
			crc = conn_replica_supports_ds71_repl(conn);
			if (CONN_DOES_NOT_SUPPORT_DS71_REPL == crc)
			{
				prp->repl50consumer = 1;
			}
			if (CONN_NOT_CONNECTED == crc || CONN_OPERATION_FAILED == crc)
			{
				/* We don't know anything about the remote replica. Try again later. */
				return_value = ACQUIRE_TRANSIENT_ERROR;
			} else 
			{
				CSN *current_csn = NULL;
				struct berval *retdata = NULL;
				char *retoid = NULL;
				Slapi_DN *replarea_sdn;

				/* Check if this is a fractional agreement, we need to
				 * verify that the consumer is read-only */
				if (agmt_is_fractional(prp->agmt)) {
					crc = conn_replica_is_readonly(conn);
					if (CONN_IS_NOT_READONLY == crc) {
						/* This is a fatal error */
						slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
										"%s: Unable to acquire replica: "
										"the agreement is fractional but the replica is not read-only. Fractional agreements must specify a read-only replica "
										"Replication is aborting.\n",
										agmt_get_long_name(prp->agmt));
						return_value = ACQUIRE_FATAL_ERROR;
						goto error;
					}
				}

				/* Good to go. Start the protocol. */

				/* Obtain a current CSN */
				replarea_sdn = agmt_get_replarea(prp->agmt);
				current_csn = get_current_csn(replarea_sdn);
				if (NULL != current_csn)
				{
					struct berval *payload = NSDS50StartReplicationRequest_new(
						prot_oid, slapi_sdn_get_ndn(replarea_sdn), 
						NULL /* XXXggood need to provide referral(s) */, current_csn);
					/* JCMREPL - Need to extract the referrals from the RUV */
					csn_free(&current_csn);
					current_csn = NULL;
					crc = conn_send_extended_operation(conn,
						REPL_START_NSDS50_REPLICATION_REQUEST_OID, payload,  NULL /* update control */, NULL /* Message ID */);
					if (CONN_OPERATION_SUCCESS != crc)
					{
						int operation, error;
						conn_get_error(conn, &operation, &error);

						/* Couldn't send the extended operation */
						return_value = ACQUIRE_TRANSIENT_ERROR; /* XXX right return value? */
						slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"%s: Unable to send a startReplication "
							"extended operation to consumer (%s). Will retry later.\n",
							agmt_get_long_name(prp->agmt),
							error ? ldap_err2string(error) : "unknown error");
					}
					/* Since the operation request is async, we need to wait for the response here */
					crc = conn_read_result_ex(conn,&retoid,&retdata,NULL,NULL,1);
					ber_bvfree(payload);
					payload = NULL;
					/* Look at the response we got. */
					if (CONN_OPERATION_SUCCESS == crc)
					{
						/*
						 * Extop was processed. Look at extop response to see if we're
						 * permitted to go ahead.
						 */
						struct berval **ruv_bervals = NULL;
						int extop_result;
						int extop_rc = decode_repl_ext_response(retdata, &extop_result,
																&ruv_bervals);
						if (0 == extop_rc)
						{
							prp->last_acquire_response_code = extop_result;
							switch (extop_result)
							{
							/* XXXggood handle other error codes here */
							case NSDS50_REPL_INTERNAL_ERROR:
									slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
										"%s: Unable to acquire replica: "
										"an internal error occurred on the remote replica. "
										"Replication is aborting.\n",
										agmt_get_long_name(prp->agmt));
									return_value = ACQUIRE_FATAL_ERROR;
									break;
							case NSDS50_REPL_PERMISSION_DENIED:
								/* Not allowed to send updates */
								{
									char *repl_binddn = agmt_get_binddn(prp->agmt);
									slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
										"%s: Unable to acquire replica: permission denied. "
										"The bind dn \"%s\" does not have permission to "
										"supply replication updates to the replica. "
										"Will retry later.\n",
										agmt_get_long_name(prp->agmt), repl_binddn);
										slapi_ch_free((void **)&repl_binddn);
									return_value = ACQUIRE_TRANSIENT_ERROR;
									break;
								}
							case NSDS50_REPL_NO_SUCH_REPLICA:
								/* There is no such replica on the consumer */
								{
									Slapi_DN *repl_root = agmt_get_replarea(prp->agmt);
									slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
										"%s: Unable to acquire replica: there is no "
										"replicated area \"%s\" on the consumer server. "
										"Replication is aborting.\n",
										agmt_get_long_name(prp->agmt),
										slapi_sdn_get_dn(repl_root));
										slapi_sdn_free(&repl_root);
									return_value = ACQUIRE_FATAL_ERROR;
									break;
								}
							case NSDS50_REPL_EXCESSIVE_CLOCK_SKEW:
								/* Large clock skew between the consumer and the supplier */
								slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
									"%s: Unable to acquire replica: "
									"Excessive clock skew between the supplier and "
									"the consumer. Replication is aborting.\n",
									agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
								break;
							case NSDS50_REPL_DECODING_ERROR:
								/* We sent something the replica couldn't understand. */
								slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
									"%s: Unable to acquire replica: "
									"the consumer was unable to decode the "
									"startReplicationRequest extended operation sent by the "
									"supplier. Replication is aborting.\n",
									agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
								break;
							case NSDS50_REPL_REPLICA_BUSY:
								/* Someone else is updating the replica. Try later. */
								slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: Unable to acquire replica: "
									"the replica is currently being updated"
									"by another supplier. Will try later\n",
									agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_REPLICA_BUSY;
								break;
							case NSDS50_REPL_LEGACY_CONSUMER:
								/* remote replica is a legacy consumer */
								slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
									"%s: Unable to acquire replica: the replica "
									"is supplied by a legacy supplier. "
									"Replication is aborting.\n", agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
								break;
							case NSDS50_REPL_REPLICAID_ERROR:
								/* remote replica detected a duplicate ReplicaID */
								slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
									"%s: Unable to aquire replica: the replica "
									"has the same Replica ID as this one. "
									"Replication is aborting.\n",
									agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
								break;
							case NSDS50_REPL_REPLICA_READY:
								/* We've acquired the replica. */
								slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: Replica was successfully acquired.\n",
									agmt_get_long_name(prp->agmt));
								/* Parse the update vector */
								if (NULL != ruv_bervals && NULL != ruv)
								{
									if (ruv_init_from_bervals(ruv_bervals, ruv) != RUV_SUCCESS)
									{
										/* Couldn't parse the update vector */
										*ruv = NULL;
										slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
											"%s: Warning: acquired replica, "
											"but could not parse update vector. "
											"The replica must be reinitialized.\n",
											agmt_get_long_name(prp->agmt));
									}
								}

								/* Save consumer's RUV in the replication agreement.
								   It is used by the changelog trimming code */
								if (ruv && *ruv)
									agmt_set_consumer_ruv (prp->agmt, *ruv);

								return_value = ACQUIRE_SUCCESS;
								break;
							default:
								return_value = ACQUIRE_FATAL_ERROR;
							}
						}
						else
						{
							/* Couldn't parse the response */
							slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
								"%s: Unable to parse the response to the "
								"startReplication extended operation. "
								"Replication is aborting.\n", 
								agmt_get_long_name(prp->agmt));
							prp->last_acquire_response_code = NSDS50_REPL_INTERNAL_ERROR;
							return_value = ACQUIRE_FATAL_ERROR;
						}
						if (NULL != ruv_bervals)
							ber_bvecfree(ruv_bervals);
					}
					else
					{
						int operation, error;
						conn_get_error(conn, &operation, &error);

						/* Couldn't send the extended operation */
						return_value = ACQUIRE_TRANSIENT_ERROR; /* XXX right return value? */
						slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"%s: Unable to receive the response for a startReplication "
							"extended operation to consumer (%s). Will retry later.\n",
							agmt_get_long_name(prp->agmt),
							error ? ldap_err2string(error) : "unknown error");
					}
				}
				else
				{
					/* Couldn't get a current CSN */
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Unable to obtain current CSN. "
						"Replication is aborting.\n",
						agmt_get_long_name(prp->agmt));
					return_value = ACQUIRE_FATAL_ERROR;
				}
				slapi_sdn_free(&replarea_sdn);
				if (NULL != retoid)
					ldap_memfree(retoid);
				if (NULL != retdata)
					ber_bvfree(retdata);
			}
		}
	}
error:

	if (ACQUIRE_SUCCESS != return_value)
	{
		/* could not acquire the replica, so reinstate the linger timer, since this
		   means we won't call release_replica, which also reinstates the timer */
		conn_start_linger(conn);
	}
    else
    {
        /* replica successfully acquired */
        prp->replica_acquired = PR_TRUE;
    }

	return return_value;
}


/*
 * Release a replica by sending an "end replication" extended request.
 */
void
release_replica(Private_Repl_Protocol *prp)
{
	int rc;
	struct berval *retdata = NULL;
	char *retoid = NULL;
	struct berval *payload = NULL;
	Slapi_DN *replarea_sdn = NULL;
	int sent_message_id = 0;
	int ret_message_id = 0;
	ConnResult conres = 0;

	PR_ASSERT(NULL != prp);
	PR_ASSERT(NULL != prp->conn);

    if (!prp->replica_acquired)
        return;
    
	replarea_sdn = agmt_get_replarea(prp->agmt);
	payload = NSDS50EndReplicationRequest_new((char *)slapi_sdn_get_dn(replarea_sdn)); /* XXXggood had to cast away const */
	slapi_sdn_free(&replarea_sdn);
	rc = conn_send_extended_operation(prp->conn,
		REPL_END_NSDS50_REPLICATION_REQUEST_OID, payload, NULL /* update control */, &sent_message_id /* Message ID */);
	if (0 != rc)
	{
		int operation, error;
		conn_get_error(prp->conn, &operation, &error);
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Warning: unable to send endReplication extended operation (%s)\n",
			agmt_get_long_name(prp->agmt),
			error ? ldap_err2string(error) : "unknown error");
		goto error;
	}
	/* Since the operation request is async, we need to wait for the response here */
	conres = conn_read_result_ex(prp->conn,&retoid,&retdata,NULL,&ret_message_id,1);
	if (CONN_OPERATION_SUCCESS != conres)
	{
		int operation, error;
		conn_get_error(prp->conn, &operation, &error);
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Warning: unable to receive endReplication extended operation response (%s)\n",
			agmt_get_long_name(prp->agmt),
			error ? ldap_err2string(error) : "unknown error");
	}
	else
	{
		struct berval **ruv_bervals = NULL; /* Shouldn't actually be returned */
		int extop_result;
		int extop_rc = 0;

		/* Check the message id's match */
		if (sent_message_id != sent_message_id)
		{
			int operation, error;
			conn_get_error(prp->conn, &operation, &error);
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: Warning: response message id does not match the request (%s)\n",
				agmt_get_long_name(prp->agmt),
				error ? ldap_err2string(error) : "unknown error");
		}

		extop_rc = decode_repl_ext_response(retdata, &extop_result,
			(struct berval ***)&ruv_bervals);
		if (0 == extop_rc)
		{
			if (NSDS50_REPL_REPLICA_RELEASE_SUCCEEDED == extop_result)
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: Successfully released consumer\n", agmt_get_long_name(prp->agmt));
			}
			else
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: Unable to release consumer: response code %d\n",
					agmt_get_long_name(prp->agmt), extop_result);
                /* disconnect from the consumer so that it does not stay locked */
                conn_disconnect (prp->conn);
			}
		}
		else
		{
			/* Couldn't parse the response */
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: Warning: Unable to parse the response "
				" to the endReplication extended operation.\n", 
				agmt_get_long_name(prp->agmt));
		}
		if (NULL != ruv_bervals)
			ber_bvecfree(ruv_bervals);
		/* XXXggood free ruv_bervals if we got them for some reason */
	}
	if (NULL != payload)
		ber_bvfree(payload);
	if (NULL != retoid)
		ldap_memfree(retoid);
	if (NULL != retdata)
		ber_bvfree(retdata);

	/* replica is released, start the linger timer on the connection, which
	   was stopped in acquire_replica */
	conn_start_linger(prp->conn);
error:
    prp->replica_acquired = PR_FALSE;
}

/* converts consumer's response to a string */
char *
protocol_response2string (int response)
{
    switch (response)
    {
        case NSDS50_REPL_REPLICA_READY:             return "replica acquired";
        case NSDS50_REPL_REPLICA_BUSY:              return "replica busy";
        case NSDS50_REPL_EXCESSIVE_CLOCK_SKEW:      return "excessive clock skew";
        case NSDS50_REPL_PERMISSION_DENIED:         return "permission denied";
        case NSDS50_REPL_DECODING_ERROR:            return "decoding error";
        case NSDS50_REPL_UNKNOWN_UPDATE_PROTOCOL:   return "unknown update protocol";
        case NSDS50_REPL_NO_SUCH_REPLICA:           return "no such replica";
        case NSDS50_REPL_BELOW_PURGEPOINT:          return "csn below purge point";
        case NSDS50_REPL_INTERNAL_ERROR:            return "internal error";
        case NSDS50_REPL_REPLICA_RELEASE_SUCCEEDED: return "replica released";
        case NSDS50_REPL_LEGACY_CONSUMER:           return "replica is a legacy consumer";						
		case NSDS50_REPL_REPLICAID_ERROR:			return "duplicate replica ID detected";
		case NSDS50_REPL_UPTODATE:					return "no change to send";
        default:                                    return "unknown error";
    }
}

int
repl5_strip_fractional_mods(Repl_Agmt *agmt, LDAPMod ** mods)
{
	int retval = 0;
	int i = 0;
	char **a = agmt_get_fractional_attrs(agmt);
	if (a) {
		/* Iterate through the fractional attr list */
		for ( i = 0; a[i] != NULL; i++ ) 
		{
			char *this_excluded_attr = a[i];
			int j = 0;

			for ( j = 0; NULL != mods[ j ]; )
			{
				/* For each one iterate through the attrs in this mod list */
				/* For any that match, remove the mod */
				LDAPMod *this_mod = mods[j];
				if (0 == slapi_attr_type_cmp(this_mod->mod_type,this_excluded_attr,SLAPI_TYPE_CMP_SUBTYPE))
				{
					/* Move down all subsequent mods */
					int k = 0;
					for (k = j; mods[k+1] ; k++)
					{
						mods[k] = mods[k+1];
					}
					/* Zero the end of the array */
					mods[k] = NULL;
					/* Adjust value of j, implicit in not incrementing it */
					/* Free this mod */
					ber_bvecfree(this_mod->mod_bvalues);
					slapi_ch_free((void **)&(this_mod->mod_type));
					slapi_ch_free((void **)&this_mod);

				} else {
					j++;
				}
			}
		}
		slapi_ch_array_free(a);
	}
	return retval;
}
