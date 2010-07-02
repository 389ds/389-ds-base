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

/* repl5_agmt.c */
/*

 Support for 5.0-style replication agreements.

 Directory Server 5.0 replication agreements contain information about
 replication consumers that we are supplying.

 This module encapsulates the methods available for adding, deleting,
 modifying, and firing replication agreements.

 Methods:

 agmt_new - Create a new replication agreement, in response to a new
            replication agreement being added over LDAP.
 agmt_delete - Destroy an agreement. It is an error to destroy an
                agreement that has not been stopped.
 agmt_getstatus - get the status of this replication agreement.
 agmt_replicate_now - initiate a replication session asap, even if the
                      schedule says we shouldn't.
 agmt_start - start replicating, according to schedule. Starts a new
              thread to handle replication.
 agmt_stop - stop replicating asap and end replication thread.
 agmt_notify_change - notify the replication agreement about a change that
                      has been logged. The replication agreement will
                      decide if it needs to take some action, e.g. start a
                      replication session.
 agmt_initialize_replica - start a complete replica refresh.
 agmt_set_schedule_from_entry - (re)set the schedule associated with this
			replication agreement based on a RA entry's contents.
 agmt_set_credentials_from_entry - (re)set the credentials used to bind
            to the remote replica.
 agmt_set_binddn_from_entry - (re)set the DN used to bind
            to the remote replica.
 agmt_set_bind_method_from_entry - (re)set the bind method used to bind
            to the remote replica (SIMPLE or SSLCLIENTAUTH).
 agmt_set_transportinfo_from_entry - (re)set the transport used to bind
            to the remote replica (SSL or not)
 
*/

#include "repl5.h"
#include "repl5_prot_private.h"
#include "cl5_api.h"
#include "slapi-plugin.h"

#define DEFAULT_TIMEOUT 600 /* (seconds) default outbound LDAP connection */
#define STATUS_LEN 1024

struct changecounter {
	ReplicaId rid;
	PRUint32 num_replayed;
	PRUint32 num_skipped;
};

typedef struct repl5agmt {
	char *hostname; /* remote hostname */
	int port; /* port of remote server */
	PRUint32 transport_flags; /* SSL, TLS, etc. */
	char *binddn; /* DN to bind as */
	struct berval *creds; /* Password, or certificate */
	int bindmethod; /* Bind method - simple, SSL */
	Slapi_DN *replarea; /* DN of replicated area */
	char **frac_attrs; /* list of fractional attributes to be replicated */
	Schedule *schedule; /* Scheduling information */
	int auto_initialize; /* 1 = automatically re-initialize replica */
	const Slapi_DN *dn; /* DN of replication agreement entry */
	const Slapi_RDN *rdn; /* RDN of replication agreement entry */
	char *long_name; /* Long name (rdn + host, port) of entry, for logging */
	Repl_Protocol *protocol; /* Protocol object - manages protocol */
	struct changecounter *changecounters[MAX_NUM_OF_MASTERS]; /* changes sent/skipped since server start up */
	int num_changecounters;
	time_t last_update_start_time; /* Local start time of last update session */
	time_t last_update_end_time; /* Local end time of last update session */
	char last_update_status[STATUS_LEN]; /* Status of last update. Format = numeric code <space> textual description */
	PRBool update_in_progress;
	time_t last_init_start_time; /* Local start time of last total init */
	time_t last_init_end_time; /* Local end time of last total init */
	char last_init_status[STATUS_LEN]; /* Status of last total init. Format = numeric code <space> textual description */
	PRLock *lock;
    Object *consumerRUV;   /* last RUV received from the consumer - used for changelog purging */
	CSN *consumerSchemaCSN; /* last schema CSN received from the consumer */
	ReplicaId consumerRID; /* indicates if the consumer is the originator of a CSN */
	long timeout; /* timeout (in seconds) for outbound LDAP connections to remote server */
	PRBool stop_in_progress; /* set by agmt_stop when shutting down */
	long busywaittime; /* time in seconds to wait after getting a REPLICA BUSY from the consumer -
						  to allow another supplier to finish sending its updates -
						  if set to 0, this means to use the default value if we get a busy
						  signal from the consumer */
	long pausetime; /* time in seconds to pause after sending updates -
					   to allow another supplier to send its updates -
					   should be greater than busywaittime -
					   if set to 0, this means do not pause */
	void *priv; /* private data, used for windows-specific agreement data 
	               for sync agreements or for replication session plug-in
	               private data for normal replication agreements */
	int agreement_type;
} repl5agmt;

/* Forward declarations */
void agmt_delete(void **rap);
static void update_window_state_change_callback (void *arg, PRBool opened);
static int get_agmt_status(Slapi_PBlock *pb, Slapi_Entry* e,
	Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int agmt_set_bind_method_no_lock(Repl_Agmt *ra, const Slapi_Entry *e);
static int agmt_set_transportinfo_no_lock(Repl_Agmt *ra, const Slapi_Entry *e);


/*
Schema for replication agreement:

cn
nsds5ReplicaHost - hostname
nsds5ReplicaPort - port number
nsds5ReplicaTransportInfo - "SSL", "startTLS", or may be absent;
nsds5ReplicaBindDN
nsds5ReplicaCredentials
nsds5ReplicaBindMethod - "SIMPLE" or "SSLCLIENTAUTH".
nsds5ReplicaRoot - Replicated suffix
nsds5ReplicatedAttributeList - Unused so far (meant for fractional repl).
nsds5ReplicaUpdateSchedule
nsds5ReplicaTimeout - Outbound repl operations timeout
nsds50ruv - consumer's RUV
nsds5ReplicaBusyWaitTime - time to wait after getting a REPLICA BUSY from the consumer
nsds5ReplicaSessionPauseTime - time to pause after sending updates to allow another supplier to send
*/


/*
 * Validate an agreement, making sure that it's valid.
 * Return 1 if the agreement is valid, 0 otherwise.
 */
static int
agmt_is_valid(Repl_Agmt *ra)
{
	int return_value = 1; /* assume valid, initially */
	PR_ASSERT(NULL != ra);
	PR_ASSERT(NULL != ra->dn);

	if (NULL == ra->hostname)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
			"is malformed: missing host name.\n", slapi_sdn_get_dn(ra->dn));
		return_value = 0;
	}
	if (ra->port <= 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
			"is malformed: invalid port number %d.\n", slapi_sdn_get_dn(ra->dn), ra->port);
		return_value = 0;
	}
	if (ra->timeout < 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
			"is malformed: invalid timeout %ld.\n", slapi_sdn_get_dn(ra->dn), ra->timeout);
		return_value = 0;
	}
	if (ra->busywaittime < 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
			"is malformed: invalid busy wait time %ld.\n", slapi_sdn_get_dn(ra->dn), ra->busywaittime);
		return_value = 0;
	}
	if (ra->pausetime < 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
			"is malformed: invalid pausetime %ld.\n", slapi_sdn_get_dn(ra->dn), ra->pausetime);
		return_value = 0;
	}
	if ((0 != ra->transport_flags) && (BINDMETHOD_SASL_GSSAPI == ra->bindmethod)) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
						" is malformed: cannot use SASL/GSSAPI if using SSL or TLS - please "
						"change %s to LDAP before changing %s to use SASL/GSSAPI\n",
						slapi_sdn_get_dn(ra->dn), type_nsds5TransportInfo, type_nsds5ReplicaBindMethod);
		return_value = 0;
	}
	if ((0 == ra->transport_flags) && (BINDMETHOD_SSL_CLIENTAUTH == ra->bindmethod)) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Replication agreement \"%s\" "
						" is malformed: cannot use SSLCLIENTAUTH if using plain LDAP - please "
						"change %s to SSL or TLS before changing %s to use SSLCLIENTAUTH\n",
						slapi_sdn_get_dn(ra->dn), type_nsds5TransportInfo, type_nsds5ReplicaBindMethod);
		return_value = 0;
	}
	return return_value;
}


Repl_Agmt *
agmt_new_from_entry(Slapi_Entry *e)
{
	Repl_Agmt *ra;
	char *tmpstr;
	Slapi_Attr *sattr;
	char **denied_attrs = NULL;

	char *auto_initialize = NULL;
	char *val_nsds5BeginReplicaRefresh = "start";

	ra = (Repl_Agmt *)slapi_ch_calloc(1, sizeof(repl5agmt));
	if ((ra->lock = PR_NewLock()) == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Unable to create new lock "
			"for replication agreement \"%s\" - agreement ignored.\n",
			slapi_entry_get_dn_const(e));
		goto loser;
	}

	/* Find all the stuff we need for the agreement */

	/*	To Allow Consumer Initialisation when adding an agreement: */

	/* 
		Using 'auto_initialize' member of 'repl5agmt' structure to 
		store the effect of 'nsds5BeginReplicaRefresh' attribute's value 
		in it. 
	*/
	auto_initialize = slapi_entry_attr_get_charptr(e, type_nsds5BeginReplicaRefresh);
	if ((auto_initialize != NULL) && (strcasecmp(auto_initialize, val_nsds5BeginReplicaRefresh) == 0))
	{
		ra->auto_initialize = STATE_PERFORMING_TOTAL_UPDATE; 
	}
	else
	{
		ra->auto_initialize = STATE_PERFORMING_INCREMENTAL_UPDATE; 
	}

	if (auto_initialize)
	{
		slapi_ch_free_string (&auto_initialize);
	}

	/* Host name of remote replica */
	ra->hostname = slapi_entry_attr_get_charptr(e, type_nsds5ReplicaHost);
	/* Port number for remote replica instance */
	ra->port = slapi_entry_attr_get_int(e, type_nsds5ReplicaPort);
	/* SSL, TLS, or other transport stuff */
	ra->transport_flags = 0;
	agmt_set_transportinfo_no_lock(ra, e);

	/* DN to use when binding. May be empty if cert-based auth is to be used. */
	ra->binddn = slapi_entry_attr_get_charptr(e, type_nsds5ReplicaBindDN);
	if (NULL == ra->binddn)
	{
		ra->binddn = slapi_ch_strdup("");
	}
	/* Credentials to use when binding. */
	ra->creds = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
	ra->creds->bv_val = NULL;
	ra->creds->bv_len = 0;
	if (slapi_entry_attr_find(e, type_nsds5ReplicaCredentials, &sattr) == 0)
	{
		Slapi_Value *sval;
		if (slapi_attr_first_value(sattr, &sval) == 0)
		{
			const struct berval *bv = slapi_value_get_berval(sval);
			if (NULL != bv)
			{
				ra->creds->bv_val = slapi_ch_malloc(bv->bv_len + 1);
				memcpy(ra->creds->bv_val, bv->bv_val, bv->bv_len);
				ra->creds->bv_len = bv->bv_len;
				ra->creds->bv_val[bv->bv_len] = '\0'; /* be safe */
			}
		}
	}
	/* How to bind */
	(void)agmt_set_bind_method_no_lock(ra, e);
	
	/* timeout. */
	ra->timeout = DEFAULT_TIMEOUT;
	if (slapi_entry_attr_find(e, type_nsds5ReplicaTimeout, &sattr) == 0)
	{
		Slapi_Value *sval;
		if (slapi_attr_first_value(sattr, &sval) == 0)
		{
			ra->timeout = slapi_value_get_long(sval);
		}
	}

	/* DN of entry at root of replicated area */
	tmpstr = slapi_entry_attr_get_charptr(e, type_nsds5ReplicaRoot);
	if (NULL != tmpstr)
	{
		ra->replarea = slapi_sdn_new_dn_passin(tmpstr);
	}
	/* XXXggood get fractional attribute include/exclude lists here */
	/* Alrighty Gordon, you get your way... */
	if (slapi_entry_attr_find(e, type_nsds5ReplicaUpdateSchedule, &sattr) == 0)
	{
	}
	/* Replication schedule */
	ra->schedule = schedule_new(update_window_state_change_callback, ra, agmt_get_long_name(ra));
	if (slapi_entry_attr_find(e, type_nsds5ReplicaUpdateSchedule, &sattr) == 0)
	{
		schedule_set(ra->schedule, sattr);
	}

	/* busy wait time - time to wait after getting REPLICA BUSY from consumer */
	ra->busywaittime = slapi_entry_attr_get_long(e, type_nsds5ReplicaBusyWaitTime);

	/* pause time - time to pause after a session has ended */
	ra->pausetime = slapi_entry_attr_get_long(e, type_nsds5ReplicaSessionPauseTime);

    /* consumer's RUV */
    if (slapi_entry_attr_find(e, type_ruvElement, &sattr) == 0)
    {
        RUV *ruv;

        if (ruv_init_from_slapi_attr(sattr, &ruv) == 0)
        {
            ra->consumerRUV = object_new (ruv, (FNFree)ruv_destroy);
        }
    }

	ra->consumerRID = 0;

	/* DN and RDN of the replication agreement entry itself */
	ra->dn = slapi_sdn_dup(slapi_entry_get_sdn((Slapi_Entry *)e));
	ra->rdn = slapi_rdn_new_sdn(ra->dn);

	/* Compute long name */
	{
		const char *agmtname = slapi_rdn_get_rdn(ra->rdn);
		char hostname[128];
		char *dot;

		strncpy(hostname, ra->hostname ? ra->hostname : "(unknown)", sizeof(hostname));
		hostname[sizeof(hostname)-1] = '\0';
		dot = strchr(hostname, '.');
		if (dot) {
			*dot = '\0';
		}
		ra->long_name = slapi_ch_smprintf("agmt=\"%s\" (%s:%d)", agmtname, hostname, ra->port);
	}

	/* DBDB: review this code */
	if (slapi_entry_attr_hasvalue(e, "objectclass", "nsDSWindowsReplicationAgreement"))
	{
		ra->agreement_type = REPLICA_TYPE_WINDOWS;
		windows_init_agreement_from_entry(ra,e);
	}
	else
	{
		ra->agreement_type = REPLICA_TYPE_MULTIMASTER;
		repl_session_plugin_call_agmt_init_cb(ra);
	}

	

	/* Initialize status information */
	ra->last_update_start_time = 0UL;
	ra->last_update_end_time = 0UL;
	ra->num_changecounters = 0;
	ra->last_update_status[0] = '\0';
	ra->update_in_progress = PR_FALSE;
	ra->stop_in_progress = PR_FALSE;
	ra->last_init_end_time = 0UL;
	ra->last_init_start_time = 0UL;
	ra->last_init_status[0] = '\0';
	
	/* Fractional attributes */
	slapi_entry_attr_find(e, type_nsds5ReplicatedAttributeList, &sattr);

	/* New set of excluded attributes */
	/* Note: even if sattrs is empty, we have to call this func since there 
	 * could be a default excluded attr list in cn=plugin default config */
	if (agmt_set_replicated_attributes_from_attr(ra, sattr) != 0)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"agmtlist_add_callback: failed to parse "
						"replicated attributes for agreement %s\n",
						agmt_get_long_name(ra));
	}
	/* Check that there are no verboten attributes in the exclude list */
	denied_attrs = agmt_validate_replicated_attributes(ra);
	if (denied_attrs)
	{
		/* Report the error to the client */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"WARNING: Attempt to exclude illegal attributes "
						"from a fractional agreement\n");
		/* Free the list */
		slapi_ch_array_free(denied_attrs);
		goto loser;
	}

	if (!agmt_is_valid(ra))
	{
		goto loser;
	}

	/* Now that the agreement is done, just check if changelog is configured */
	if (cl5GetState() != CL5_STATE_OPEN) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "WARNING: "
						"Replication agreement added but there is no changelog configured. "
						"No change will be replicated until a changelog is configured.\n");
	}
		
	/*
	 * Establish a callback for this agreement's entry, so we can
	 * adorn it with status information when read.
	 */
	slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, slapi_sdn_get_ndn(ra->dn),
		LDAP_SCOPE_BASE, "(objectclass=*)", get_agmt_status, ra);

	return ra;	
loser:
	agmt_delete((void **)&ra);
	return NULL;
}



Repl_Agmt *
agmt_new_from_pblock(Slapi_PBlock *pb)
{
	Slapi_Entry *e;

	slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
	return agmt_new_from_entry(e);
}


/*
 This should never be called directly - only should be called
 as a destructor.  XXXggood this is not finished 
 */
void
agmt_delete(void **rap)
{
	Repl_Agmt *ra;
	PR_ASSERT(NULL != rap);
	PR_ASSERT(NULL != *rap);

	ra = (Repl_Agmt *)*rap;

	/* do prot_delete first - we may be doing some processing using this
	   replication agreement, and prot_delete will make sure the
	   processing is complete - then it should be safe to clean up the
	   other fields below
	*/
	prot_delete(&ra->protocol);

	/*
	 * Remove the callback for this agreement's entry
	 */
	slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
								 slapi_sdn_get_ndn(ra->dn),
								 LDAP_SCOPE_BASE, "(objectclass=*)",
								 get_agmt_status);

        /*
	 * Call the replication session cleanup callback.  We
	 * need to do this before we free replarea.
	 */
	if (ra->agreement_type != REPLICA_TYPE_WINDOWS) {
		repl_session_plugin_call_destroy_agmt_cb(ra);
	}

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free((void **)&(ra->hostname));
	slapi_ch_free((void **)&(ra->binddn));

	slapi_ch_array_free(ra->frac_attrs);

	if (NULL != ra->creds)
	{
		/* XXX free berval */
	}
	if (NULL != ra->replarea)
	{
		slapi_sdn_free(&ra->replarea);
	}

    if (NULL != ra->consumerRUV)
	{
		object_release (ra->consumerRUV);
	}

	csn_free (&ra->consumerSchemaCSN);
	while ( --(ra->num_changecounters) >= 0 )
	{
	    slapi_ch_free((void **)&ra->changecounters[ra->num_changecounters]);
	}

	if (ra->agreement_type == REPLICA_TYPE_WINDOWS)
	{
		windows_agreement_delete(ra);
	}

	schedule_destroy(ra->schedule);
	slapi_ch_free((void **)&ra->long_name);
	slapi_ch_free((void **)rap);
}


/*
 * Allow replication for this replica to begin. Replication will
 * occur at the next scheduled time. Returns 0 on success, -1 on
 * failure.
 */
int
agmt_start(Repl_Agmt *ra)
{
    Repl_Protocol *prot = NULL;

	int protocol_state;

	/*	To Allow Consumer Initialisation when adding an agreement: */	
	if (ra->auto_initialize == STATE_PERFORMING_TOTAL_UPDATE)
	{
		protocol_state = STATE_PERFORMING_TOTAL_UPDATE;
	}
	else
	{
		protocol_state = STATE_PERFORMING_INCREMENTAL_UPDATE;
	}

    /* First, create a new protocol object */
    if ((prot = prot_new(ra, protocol_state)) == NULL) {
        return -1;
    }

    /* Now it is safe to own the agreement lock */
    PR_Lock(ra->lock);

    /* Check that replication is not already started */
    if (ra->protocol != NULL) {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replication already started for agreement \"%s\"\n", agmt_get_long_name(ra));
        PR_Unlock(ra->lock);
        prot_free(&prot);
        return 0;
    }

    ra->protocol = prot;

    /* Start the protocol thread */
    prot_start(ra->protocol);

    PR_Unlock(ra->lock);
    return 0;
}


/*
 * Allow replication for this replica to begin. Replication will
 * occur at the next scheduled time. Returns 0 on success, -1 on
 * failure.
 */
int
windows_agmt_start(Repl_Agmt *ra)
{
  Repl_Protocol *prot = NULL;

	int protocol_state;

	/*	To Allow Consumer Initialisation when adding an agreement: */	
	if (ra->auto_initialize == STATE_PERFORMING_TOTAL_UPDATE)
	{
		protocol_state = STATE_PERFORMING_TOTAL_UPDATE;
	}
	else
	{
		protocol_state = STATE_PERFORMING_INCREMENTAL_UPDATE;
	}

    /* First, create a new protocol object */
    if ((prot = prot_new(ra, protocol_state)) == NULL) {
        return -1;
    }

    /* Now it is safe to own the agreement lock */
    PR_Lock(ra->lock);

    /* Check that replication is not already started */
    if (ra->protocol != NULL) {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replication already started for agreement \"%s\"\n", agmt_get_long_name(ra));
        PR_Unlock(ra->lock);
        prot_free(&prot);
        return 0;
    }

    ra->protocol = prot;

    /* Start the protocol thread */
    prot_start(ra->protocol);

    PR_Unlock(ra->lock);
    return 0;
}


/*
Cease replicating to this replica as soon as possible.
*/
int
agmt_stop(Repl_Agmt *ra)
{
	int return_value = 0;
	Repl_Protocol *rp = NULL;

	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}
	ra->stop_in_progress = PR_TRUE;
	rp = ra->protocol;
	PR_Unlock(ra->lock);
	if (NULL != rp) /* we use this pointer outside the lock - dangerous? */
	{
		prot_stop(rp);
	}
	PR_Lock(ra->lock);
	ra->stop_in_progress = PR_FALSE;
	/* we do not reuse the protocol object so free it */
	prot_free(&ra->protocol);
	PR_Unlock(ra->lock);
	return return_value;
}

/*
Send any pending updates as soon as possible, ignoring any replication
schedules.
*/
int
agmt_replicate_now(Repl_Agmt *ra)
{
	int return_value = 0;

	return return_value;
}

/*
 * Return a copy of the remote replica's hostname.
 */
char *
agmt_get_hostname(const Repl_Agmt *ra)
{
	char *return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = slapi_ch_strdup(ra->hostname);
	PR_Unlock(ra->lock);
	return return_value;
}

/*
 * Return the port number of the remote replica's instance.
 */
int
agmt_get_port(const Repl_Agmt *ra)
{
	int return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->port;
	PR_Unlock(ra->lock);
	return return_value;
}

/*
 * Return the transport flags for this agreement.
 */
PRUint32
agmt_get_transport_flags(const Repl_Agmt *ra)
{
	unsigned int return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->transport_flags;
	PR_Unlock(ra->lock);
	return return_value;
}

/*
 * Return a copy of the bind dn to be used with this
 * agreement (may return NULL if no binddn is required, 
 * e.g. SSL client auth.
 */
char *
agmt_get_binddn(const Repl_Agmt *ra)
{
	char *return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->binddn == NULL ? NULL : slapi_ch_strdup(ra->binddn);
	PR_Unlock(ra->lock);
	return return_value;
}

/*
 * Return a copy of the credentials.
 */
struct berval *
agmt_get_credentials(const Repl_Agmt *ra)
{
	struct berval *return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
	return_value->bv_val = (char *)slapi_ch_malloc(ra->creds->bv_len + 1);
	return_value->bv_len = ra->creds->bv_len;
	memcpy(return_value->bv_val, ra->creds->bv_val, ra->creds->bv_len);
	return_value->bv_val[return_value->bv_len] = '\0'; /* just in case */
	PR_Unlock(ra->lock);
	return return_value;
}

int
agmt_get_bindmethod(const Repl_Agmt *ra)
{
	int return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->bindmethod;
	PR_Unlock(ra->lock);
	return return_value;
}

/*
 * Return a copy of the dn at the top of the replicated area.
 */
Slapi_DN *
agmt_get_replarea(const Repl_Agmt *ra)
{
	Slapi_DN *return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = slapi_sdn_new();
	slapi_sdn_copy(ra->replarea, return_value);
	PR_Unlock(ra->lock);
	return return_value;
}

int
agmt_is_fractional(const Repl_Agmt *ra)
{
	int return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->frac_attrs != NULL;
	PR_Unlock(ra->lock);
	return return_value;
}

/* Returns a COPY of the attr list, remember to free it */
char **
agmt_get_fractional_attrs(const Repl_Agmt *ra)
{
	char ** return_value = NULL;
	PR_ASSERT(NULL != ra);
	if (NULL == ra->frac_attrs)
	{
		return NULL;
	}
	PR_Lock(ra->lock);
	return_value = charray_dup(ra->frac_attrs);
	PR_Unlock(ra->lock);
	return return_value;
}
int
agmt_is_fractional_attr(const Repl_Agmt *ra, const char *attrname)
{
	int return_value;
	PR_ASSERT(NULL != ra);
	if (NULL == ra->frac_attrs)
	{
		return 0;
	}
	PR_Lock(ra->lock);
	/* Scan the list looking for a match */
	return_value = charray_inlist(ra->frac_attrs,(char*)attrname);
	PR_Unlock(ra->lock);
	return return_value;
}

int
agmt_get_auto_initialize(const Repl_Agmt *ra)
{
	int return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->auto_initialize;
	PR_Unlock(ra->lock);
	return return_value;
}

long
agmt_get_timeout(const Repl_Agmt *ra)
{
	long return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->timeout;
	PR_Unlock(ra->lock);
	return return_value;
}

long
agmt_get_busywaittime(const Repl_Agmt *ra)
{
	long return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->busywaittime;
	PR_Unlock(ra->lock);
	return return_value;
}
long
agmt_get_pausetime(const Repl_Agmt *ra)
{
	long return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	return_value = ra->pausetime;
	PR_Unlock(ra->lock);
	return return_value;
}

/*
 * Warning - reference to the long name of the agreement is returned.
 * The long name of an agreement is the DN of the agreement entry,
 * followed by the host/port for the replica.
 */
const char *
agmt_get_long_name(const Repl_Agmt *ra)
{
	char *return_value = NULL;

	return_value = ra ? ra->long_name : "";
	return return_value;
}

/*
 * Warning - reference to dn is returned. However, since the dn of
 * the replication agreement is its name, it won't change during the
 * lifetime of the replication agreement object.
 */
const Slapi_DN *
agmt_get_dn_byref(const Repl_Agmt *ra)
{
	const Slapi_DN *return_value = NULL;

	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		return_value = ra->dn;
	}
	return return_value;
}

/* Return 1 if name matches the replication Dn, 0 otherwise */
int
agmt_matches_name(const Repl_Agmt *ra, const Slapi_DN *name)
{
	int return_value = 0;
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		PR_Lock(ra->lock);
		if (slapi_sdn_compare(name, ra->dn) == 0)
		{
			return_value = 1;
		}
		PR_Unlock(ra->lock);
	}
	return return_value;
}

/* Return 1 if name matches the replication area, 0 otherwise */
int
agmt_replarea_matches(const Repl_Agmt *ra, const Slapi_DN *name)
{
	int return_value = 0;
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		PR_Lock(ra->lock);
		if (slapi_sdn_compare(name, ra->replarea) == 0)
		{
			return_value = 1;
		}
		PR_Unlock(ra->lock);
	}
	return return_value;
}


int
agmt_schedule_in_window_now(const Repl_Agmt *ra)
{
	int return_value;
	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (NULL != ra->schedule && schedule_in_window_now(ra->schedule))
	{
		return_value = 1;
	}
	else
	{
		return_value = 0;
	}
	PR_Unlock(ra->lock);
	return return_value;
}


/*
 * Set or reset the credentials used to bind to the remote replica.
 *
 * Returns 0 if credentials set, or -1 if an error occurred.
 */
int
agmt_set_credentials_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	Slapi_Attr *sattr = NULL;
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	slapi_entry_attr_find(e, type_nsds5ReplicaCredentials, &sattr);
	PR_Lock(ra->lock);
	slapi_ch_free((void **)&ra->creds->bv_val);
	ra->creds->bv_len = 0;
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			const struct berval *bv = slapi_value_get_berval(sval);
			ra->creds->bv_val = slapi_ch_calloc(1, bv->bv_len + 1);
			memcpy(ra->creds->bv_val, bv->bv_val, bv->bv_len);
			ra->creds->bv_len = bv->bv_len;
		}
	}
	/* If no credentials set, set to zero-length string */
	ra->creds->bv_val = NULL == ra->creds->bv_val ? slapi_ch_strdup("") : ra->creds->bv_val;
	PR_Unlock(ra->lock);
	prot_notify_agmt_changed(ra->protocol, ra->long_name);
	return return_value;
}
			
/*
 * Set or reset the DN used to bind to the remote replica.
 *
 * Returns 0 if DN set, or -1 if an error occurred.
 */
int
agmt_set_binddn_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	Slapi_Attr *sattr = NULL;
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	slapi_entry_attr_find(e, type_nsds5ReplicaBindDN, &sattr);
	PR_Lock(ra->lock);
	slapi_ch_free((void **)&ra->binddn);
	ra->binddn = NULL;
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			const char *val = slapi_value_get_string(sval);
			ra->binddn = slapi_ch_strdup(val);
		}
	}
	/* If no BindDN set, set to zero-length string */
	if (ra->binddn == NULL) {
		ra->binddn = slapi_ch_strdup("");
	}
	PR_Unlock(ra->lock);
	prot_notify_agmt_changed(ra->protocol, ra->long_name);
	return return_value;
}

static int
agmt_parse_excluded_attrs_filter(const char *attr_string, size_t *offset)
{
	char *filterstring = "(objectclass=*) ";
	size_t filterstringlen = strlen(filterstring);
	int retval = 0;

	if (strncmp(attr_string + *offset,filterstring,filterstringlen) == 0) 
	{
		(*offset) += filterstringlen; 
	} else 
	{
		retval = -1;
	}
	return retval;
}

static int
agmt_parse_excluded_attrs_exclude(const char *attr_string, size_t *offset)
{
	char *excludestring = "$ EXCLUDE ";
	size_t excludestringlen = strlen(excludestring);
	int retval = 0;

	if (strncmp(attr_string + *offset,excludestring,excludestringlen) == 0) 
	{
		(*offset) += excludestringlen; 
	} else 
	{
		retval = -1;
	}
	return retval;
}

static int
agmt_parse_excluded_attrs_next(const char *attr_string, size_t *offset, char*** attrs)
{
	int retval = 0;
	char *beginstr = ((char*) attr_string) + *offset;
	char *tmpstr = NULL;
	size_t stringlen = 0;
	char c = 0;

	/* Find the end of the current attribute name, if one is present */
	while (1)
	{
		c = *(beginstr + stringlen);
		if ('\0' == c || ' ' == c) 
		{
			break;
		}
		stringlen++;
	}
	if (0 != stringlen) 
	{
		tmpstr = slapi_ch_malloc(stringlen + 1);
		strncpy(tmpstr,beginstr,stringlen);
		tmpstr[stringlen] = '\0';
		if (charray_inlist(*attrs, tmpstr)) /* tmpstr is already in attrs */
		{
			slapi_ch_free_string(&tmpstr);
		}
		else
		{
			charray_add(attrs,tmpstr);
		}
		(*offset) += stringlen;
		/* Skip a delimiting space */
		if (c == ' ')
		{
			(*offset)++;
		}
	} else
	{
		retval = -1;
	}
	return retval;
}

/* It looks like this:
 * nsDS5ReplicatedAttributeList: (objectclass=*) $ EXCLUDE jpegPhoto telephoneNumber
 * This function could be called multiple times: to set excluded attrs in the
 * plugin default config and to set the ones in the replica agreement. 
 * The excluded attrs from replica agreement are added to the ones from 
 * default config.  (Therefore, *attrs should not be initialized in this 
 * function.)
 */
static int 
agmt_parse_excluded_attrs_config_attr(const char *attr_string, char ***attrs)
{
	int retval = 0;
	size_t offset = 0;
	char **new_attrs = NULL;

	/* First parse and skip the filter */
	retval = agmt_parse_excluded_attrs_filter(attr_string, &offset);
	if (retval) 
	{
		goto error;
	}
	/* Now look for the 'EXCLUDE' keyword */
	retval = agmt_parse_excluded_attrs_exclude(attr_string, &offset);
	if (retval) 
	{
		goto error;
	}
	/* Finally walk the list of attrs, storing in our chararray */
	while (!retval)
	{
		retval = agmt_parse_excluded_attrs_next(attr_string, &offset, &new_attrs);
	}
	/* If we got to here, we can't have an error */
	retval = 0;
	if (new_attrs) 
	{
		charray_merge_nodup(attrs, new_attrs, 1);
		slapi_ch_array_free(new_attrs);
	}
error:
	return retval;
}

/*
 * _agmt_set_default_fractional_attrs
 *   helper function to set nsds5ReplicatedAttributeList value (from cn=plugin
 *   default config,cn=config) to frac_attrs in Repl_Agmt.  
 *   nsds5ReplicatedAttributeList set in each agreement is added to the
 *   default list set in this function.
 */
static int
_agmt_set_default_fractional_attrs(Repl_Agmt *ra)
{
	Slapi_PBlock *newpb = NULL;
	Slapi_Entry **entries = NULL;
	int rc = LDAP_SUCCESS;
	char *attrs[2];

	attrs[0] = (char *)type_nsds5ReplicatedAttributeList;
	attrs[1] = NULL;

	newpb = slapi_pblock_new();
	slapi_search_internal_set_pb(newpb,
					SLAPI_PLUGIN_DEFAULT_CONFIG, /* Base DN */
					LDAP_SCOPE_BASE,
					"(objectclass=*)",
					attrs, /* Attrs */
					0, /* AttrOnly */
					NULL, /* Controls */
					NULL, /* UniqueID */
					repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
					0);
	slapi_search_internal_pb(newpb);
	slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	ra->frac_attrs = NULL;
	if (LDAP_SUCCESS == rc && entries && *entries) /* default config entry exists */
	{
		Slapi_Attr *attr;
		Slapi_Value *sval = NULL;
		if (0 == slapi_entry_attr_find(*entries,
								      type_nsds5ReplicatedAttributeList, &attr))
		{
			int i;
			const char *val = NULL;
			for (i = slapi_attr_first_value(attr, &sval);
				 i >= 0; i = slapi_attr_next_value(attr, i, &sval)) {
				val = slapi_value_get_string(sval);
				rc = agmt_parse_excluded_attrs_config_attr(val,
														   &(ra->frac_attrs));
				if (0 != rc) {
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"_agmt_set_default_fractional_attrs: failed to "
							"parse default config (%s) attribute %s value: %s\n",
							SLAPI_PLUGIN_DEFAULT_CONFIG,
							type_nsds5ReplicatedAttributeList, val);
				}
			}
		}
	}

	slapi_free_search_results_internal(newpb);
	slapi_pblock_destroy(newpb);

	return rc;
}

/*
 * Set or reset the set of replicated attributes.
 *
 * Returns 0 if DN set, or -1 if an error occurred.
 */
int
agmt_set_replicated_attributes_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	Slapi_Attr *sattr = NULL;
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	slapi_entry_attr_find(e, type_nsds5ReplicatedAttributeList, &sattr);
	PR_Lock(ra->lock);
	if (ra->frac_attrs) 
	{
		slapi_ch_array_free(ra->frac_attrs);
		ra->frac_attrs = NULL;
	}
	_agmt_set_default_fractional_attrs(ra);
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			const char *val = slapi_value_get_string(sval);
			return_value = agmt_parse_excluded_attrs_config_attr(val,&(ra->frac_attrs));
		}
	}
	PR_Unlock(ra->lock);
	prot_notify_agmt_changed(ra->protocol, ra->long_name);
	return return_value;
}

/*
 * Set or reset the set of replicated attributes.
 *
 * Returns 0 if DN set, or -1 if an error occurred.
 */
int
agmt_set_replicated_attributes_from_attr(Repl_Agmt *ra, Slapi_Attr *sattr)
{
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->frac_attrs) 
	{
		slapi_ch_array_free(ra->frac_attrs);
		ra->frac_attrs = NULL;
	}
	_agmt_set_default_fractional_attrs(ra);
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			const char *val = slapi_value_get_string(sval);
			return_value = agmt_parse_excluded_attrs_config_attr(val,&(ra->frac_attrs));
		}
	}
	PR_Unlock(ra->lock);
	return return_value;
}

char **
agmt_validate_replicated_attributes(Repl_Agmt *ra)
{
	
	static char* verbotten_attrs[] = {
		"nsuniqueid",
		"modifiersname",
		"lastmodifiedtime",
		"dc", "o", "ou", "cn", "objectclass",
		NULL
	};

	char **retval = NULL;
	char **frac_attrs = ra->frac_attrs;

	/* Iterate over the frac attrs */
	if (frac_attrs) 
	{
		char *this_attr = NULL;
		int i = 0;
		for (i = 0; (this_attr = frac_attrs[i]); i++)
		{
			if (charray_inlist(verbotten_attrs,this_attr)) {
				int k = 0;
				charray_add(&retval,this_attr);
				/* Remove this attr from the list */
				for (k = i; frac_attrs[k] ; k++)
				{
					frac_attrs[k] = frac_attrs[k+1];
				}
				i--;
			}
		}
	}

	return retval;
}

/*
 * Set or reset the bind method used to bind to the remote replica.
 *
 * Returns 0 if bind method set, or -1 if an error occurred.
 */
static int 
agmt_set_bind_method_no_lock(Repl_Agmt *ra, const Slapi_Entry *e)
{
	char *tmpstr = NULL;
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	tmpstr = slapi_entry_attr_get_charptr(e, type_nsds5ReplicaBindMethod);

	if (NULL == tmpstr || strcasecmp(tmpstr, "SIMPLE") == 0)
	{
		ra->bindmethod = BINDMETHOD_SIMPLE_AUTH;
	}
	else if (strcasecmp(tmpstr, "SSLCLIENTAUTH") == 0)
	{
		ra->bindmethod = BINDMETHOD_SSL_CLIENTAUTH;
	}
	else if (strcasecmp(tmpstr, "SASL/GSSAPI") == 0)
	{
		ra->bindmethod = BINDMETHOD_SASL_GSSAPI;
	}
	else if (strcasecmp(tmpstr, "SASL/DIGEST-MD5") == 0)
	{
		ra->bindmethod = BINDMETHOD_SASL_DIGEST_MD5;
	}
	else
	{
		ra->bindmethod = BINDMETHOD_SIMPLE_AUTH;
	}
	slapi_ch_free((void **)&tmpstr);
	return return_value;
}

int
agmt_set_bind_method_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}
	return_value = agmt_set_bind_method_no_lock(ra, e);
	PR_Unlock(ra->lock);
	prot_notify_agmt_changed(ra->protocol, ra->long_name);
	return return_value;
}

/*
 * Set or reset the transport used to bind to the remote replica.
 *
 * Returns 0 if transport set, or -1 if an error occurred.
 */
static int
agmt_set_transportinfo_no_lock(Repl_Agmt *ra, const Slapi_Entry *e)
{
	char *tmpstr;
	int rc = 0;
	
	tmpstr = slapi_entry_attr_get_charptr(e, type_nsds5TransportInfo);
	if (!tmpstr || !strcasecmp(tmpstr, "LDAP")) {
		ra->transport_flags = 0;
	} else if (strcasecmp(tmpstr, "SSL") == 0) {
		ra->transport_flags = TRANSPORT_FLAG_SSL;
	} else if (strcasecmp(tmpstr, "TLS") == 0) {
		ra->transport_flags = TRANSPORT_FLAG_TLS;
	}
	/* else do nothing - invalid value is a no-op */

	slapi_ch_free_string(&tmpstr);
	return (rc);
}

int 
agmt_set_transportinfo_from_entry(Repl_Agmt *ra, const Slapi_Entry *e) 
{
	int return_value = 0;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}
	return_value = agmt_set_transportinfo_no_lock(ra, e);
	PR_Unlock(ra->lock);
	prot_notify_agmt_changed(ra->protocol, ra->long_name);

	return return_value;
}
	
	
/*
 * Set or reset the replication schedule.  Notify the protocol handler
 * that a change has been made.
 *
 * Returns 0 if schedule was set or -1 if an error occurred.
 */
int
agmt_set_schedule_from_entry( Repl_Agmt *ra, const Slapi_Entry *e )
{
	Slapi_Attr	*sattr;
	int			return_value = 0;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}
	PR_Unlock(ra->lock);

	if (slapi_entry_attr_find(e, type_nsds5ReplicaUpdateSchedule, &sattr) != 0)
	{
		sattr = NULL;	/* no schedule ==> delete any existing one  */
	}

	/* make it so */
	return_value = schedule_set(ra->schedule, sattr);

	if ( 0 == return_value ) {
		/* schedule set OK -- spread the news */
		prot_notify_agmt_changed(ra->protocol, ra->long_name);
	}

	return return_value;
}

/*
 * Set or reset the timeout used to bind to the remote replica.
 *
 * Returns 0 if timeout set, or -1 if an error occurred.
 */
int
agmt_set_timeout_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	Slapi_Attr *sattr = NULL;
	int return_value = -1;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}

	slapi_entry_attr_find(e, type_nsds5ReplicaTimeout, &sattr);
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			long tmpval = slapi_value_get_long(sval);
			if (tmpval >= 0) {
				ra->timeout = tmpval;
				return_value = 0; /* success! */
			}
		}
	}
	PR_Unlock(ra->lock);
	if (return_value == 0)
	{
		prot_notify_agmt_changed(ra->protocol, ra->long_name);
	}
	return return_value;
}

/*
 * Set or reset the busywaittime
 *
 * Returns 0 if busywaittime set, or -1 if an error occurred.
 */
int
agmt_set_busywaittime_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	Slapi_Attr *sattr = NULL;
	int return_value = -1;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}

	slapi_entry_attr_find(e, type_nsds5ReplicaBusyWaitTime, &sattr);
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			long tmpval = slapi_value_get_long(sval);
			if (tmpval >= 0) {
				ra->busywaittime = tmpval;
				return_value = 0; /* success! */
			}
		}
	}
	PR_Unlock(ra->lock);
	if (return_value == 0)
	{
		prot_notify_agmt_changed(ra->protocol, ra->long_name);
	}
	return return_value;
}

/*
 * Set or reset the pausetime
 *
 * Returns 0 if pausetime set, or -1 if an error occurred.
 */
int
agmt_set_pausetime_from_entry(Repl_Agmt *ra, const Slapi_Entry *e)
{
	Slapi_Attr *sattr = NULL;
	int return_value = -1;

	PR_ASSERT(NULL != ra);
	PR_Lock(ra->lock);
	if (ra->stop_in_progress)
	{
		PR_Unlock(ra->lock);
		return return_value;
	}

	slapi_entry_attr_find(e, type_nsds5ReplicaSessionPauseTime, &sattr);
	if (NULL != sattr)
	{
		Slapi_Value *sval = NULL;
		slapi_attr_first_value(sattr, &sval);
		if (NULL != sval)
		{
			long tmpval = slapi_value_get_long(sval);
			if (tmpval >= 0) {
				ra->pausetime = tmpval;
				return_value = 0; /* success! */
			}
		}
	}
	PR_Unlock(ra->lock);
	if (return_value == 0)
	{
		prot_notify_agmt_changed(ra->protocol, ra->long_name);
	}
	return return_value;
}

/* XXXggood - also make this pass an arg that tells if there was
 * an update to a priority attribute */
void
agmt_notify_change(Repl_Agmt *agmt, Slapi_PBlock *pb)
{
	if (NULL != pb)
	{
		/* Is the entry within our replicated area? */
		char *target_dn;
		Slapi_DN *target_sdn;
		int change_is_relevant = 0;

		PR_ASSERT(NULL != agmt);
		PR_Lock(agmt->lock);
		if (agmt->stop_in_progress)
		{
			PR_Unlock(agmt->lock);
			return;
		}

		slapi_pblock_get(pb, SLAPI_TARGET_DN, &target_dn);
		target_sdn = slapi_sdn_new_dn_byref(target_dn); /* XXX see if you can avoid allocating this */

		if (slapi_sdn_issuffix(target_sdn, agmt->replarea))
		{
			/*
			 * Yep, it's in our replicated area. Is this a fractional
			 * replication agreement?
			 */
			if (NULL != agmt->frac_attrs)
			{
				/*
				 * Yep, it's fractional. See if the change should be
				 * tossed because it doesn't affect any of the replicated
				 * attributes.
				 */
				int optype;
				int affects_non_fractional_attribute = 0;

				slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
				if (SLAPI_OPERATION_MODIFY == optype)
				{
					LDAPMod **mods;
					int i, j;

					slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
					for (i = 0; !affects_non_fractional_attribute && NULL != agmt->frac_attrs[i]; i++)
					{
						for (j = 0; !affects_non_fractional_attribute && NULL != mods[j]; j++)
						{
							if (!slapi_attr_types_equivalent(agmt->frac_attrs[i],
								mods[j]->mod_type))
							{
								affects_non_fractional_attribute = 1;
							}
						}
					}
				}
				else
				{
					/*
					 * Add, delete, and modrdn always cause some sort of
					 * operation replay, even if agreement is fractional.
					 */
					affects_non_fractional_attribute = 1;
				}
				if (affects_non_fractional_attribute)
				{
					change_is_relevant = 1;
				}
			}
			else
			{
				/* Not a fractional agreement */
				change_is_relevant = 1;
			}
		}
		PR_Unlock(agmt->lock);
		slapi_sdn_free(&target_sdn);
		if (change_is_relevant)
		{
			/* Notify the protocol that a change has occurred */
			prot_notify_update(agmt->protocol);
		}
	}
}



int
agmt_is_50_mm_protocol(const Repl_Agmt *agmt)
{
	return 1; /* XXXggood could support > 1 protocol */
}



int
agmt_initialize_replica(const Repl_Agmt *agmt)
{
	PR_ASSERT(NULL != agmt);
	PR_Lock(agmt->lock);
	if (agmt->stop_in_progress)
	{
		PR_Unlock(agmt->lock);
		return 0;
	}
	PR_Unlock(agmt->lock);
	/* Call prot_initialize_replica only if the suffix is enabled (agmt->protocol != NULL) */
	if (NULL != agmt->protocol) {
		prot_initialize_replica(agmt->protocol);
	}
	else {
		/* agmt->protocol == NULL --> Suffix is disabled */
		return -1;
	}
	return 0;
}

/* delete nsds5BeginReplicaRefresh attribute to indicate to the clients
   that replica initialization have completed */
void 
agmt_replica_init_done (const Repl_Agmt *agmt)
{
    int rc;
    Slapi_PBlock *pb = slapi_pblock_new ();
    LDAPMod *mods [2];
    LDAPMod mod;

    mods[0] = &mod;
	mods[1] = NULL;
	mod.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
	mod.mod_type = (char*)type_nsds5ReplicaInitialize;
    mod.mod_bvalues = NULL;
	
    slapi_modify_internal_set_pb(pb, slapi_sdn_get_dn (agmt->dn), mods, NULL/* controls */, 
          NULL/* uniqueid */, repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0/* flags */);
    slapi_modify_internal_pb (pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_ATTRIBUTE)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "agmt_replica_init_done: "
                        "failed to remove (%s) attribute from (%s) entry; LDAP error - %d\n",
                        type_nsds5ReplicaInitialize, slapi_sdn_get_ndn (agmt->dn), rc);   
    }

    slapi_pblock_destroy (pb);
}

/* Agreement object is acquired on behalf of the caller.
   The caller is responsible for releasing the object
   when it is no longer used */

Object* 
agmt_get_consumer_ruv (Repl_Agmt *ra)
{
    Object *rt = NULL;

	PR_ASSERT(NULL != ra);

	PR_Lock(ra->lock);
    if (ra->consumerRUV)
    {
        object_acquire (ra->consumerRUV);
        rt = ra->consumerRUV;
    }

    PR_Unlock(ra->lock);

    return rt;
}

int 
agmt_set_consumer_ruv (Repl_Agmt *ra, RUV *ruv)
{
    if (ra == NULL || ruv == NULL)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "agmt_set_consumer_ruv: invalid argument" 
                        " agmt - %p, ruv - %p\n", ra, ruv); 
        return -1;
    }

    PR_Lock(ra->lock);
    
    if (ra->consumerRUV)
    {
        object_release (ra->consumerRUV);
    }

    ra->consumerRUV = object_new (ruv_dup (ruv), (FNFree)ruv_destroy);

    PR_Unlock(ra->lock);

    return 0;
}

void 
agmt_update_consumer_ruv (Repl_Agmt *ra)
{
    int rc;
    RUV *ruv;
    Slapi_Mod smod;
    Slapi_Mod smod_last_modified;
    Slapi_PBlock *pb;
    LDAPMod *mods[3];

    PR_ASSERT (ra);
    PR_Lock(ra->lock);

    if (ra->consumerRUV)
    {
        ruv = (RUV*) object_get_data (ra->consumerRUV);
        PR_ASSERT (ruv);

        ruv_to_smod(ruv, &smod);
        ruv_last_modified_to_smod(ruv, &smod_last_modified);

		/* it is ok to release the lock here because we are done with the agreement data.
		   we have to do it before issuing the modify operation because it causes
		   agmtlist_notify_all to be called which uses the same lock - hence the deadlock */
		PR_Unlock(ra->lock);

        pb = slapi_pblock_new ();
        mods[0] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod);
        mods[1] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod_last_modified);
        mods[2] = NULL;

        slapi_modify_internal_set_pb (pb, (char*)slapi_sdn_get_dn(ra->dn), mods, NULL, NULL, 
                                      repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
        slapi_modify_internal_pb (pb);

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_ATTRIBUTE)
        {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "%s: agmt_update_consumer_ruv: "
                            "failed to update consumer's RUV; LDAP error - %d\n",
							ra->long_name, rc);
        }

        slapi_mod_done (&smod);
        slapi_mod_done (&smod_last_modified);
        slapi_pblock_destroy (pb);
    }
	else
		PR_Unlock(ra->lock);
}

CSN* 
agmt_get_consumer_schema_csn (Repl_Agmt *ra)
{
	CSN *rt;

	PR_ASSERT(NULL != ra);

	PR_Lock(ra->lock);
    rt = ra->consumerSchemaCSN;
    PR_Unlock(ra->lock);

    return rt;
}

void 
agmt_set_consumer_schema_csn (Repl_Agmt *ra, CSN *csn)
{
	PR_ASSERT(NULL != ra);

	PR_Lock(ra->lock);
	csn_free(&ra->consumerSchemaCSN);
    ra->consumerSchemaCSN = csn;
    PR_Unlock(ra->lock);
}

void
agmt_set_last_update_start (Repl_Agmt *ra, time_t start_time)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		ra->last_update_start_time = start_time;
		ra->last_update_end_time = 0UL;
	}
}


void
agmt_set_last_update_end (Repl_Agmt *ra, time_t end_time)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		ra->last_update_end_time = end_time;
	}
}

void
agmt_set_last_init_start (Repl_Agmt *ra, time_t start_time)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		ra->last_init_start_time = start_time;
		ra->last_init_end_time = 0UL;
	}
}


void
agmt_set_last_init_end (Repl_Agmt *ra, time_t end_time)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		ra->last_init_end_time = end_time;
	}
}

void
agmt_set_last_update_status (Repl_Agmt *ra, int ldaprc, int replrc, const char *message)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		if (replrc == NSDS50_REPL_UPTODATE)
		{
			/* no session started, no status update */
		}
		else if (ldaprc != LDAP_SUCCESS)
		{
			char *replmsg = NULL;
			if ( replrc ) {
				replmsg = protocol_response2string(replrc);
				/* Do not mix the unknown replication error with the known ldap one */
				if ( strcasecmp(replmsg, "unknown error") == 0 ) {
					replmsg = NULL;
				}
			}
			if (ldaprc > 0) {
				PR_snprintf(ra->last_update_status, STATUS_LEN,
							"%d %s%sLDAP error: %s%s%s",
							ldaprc, 
							message?message:"",message?"":" - ",
							ldap_err2string(ldaprc),
							replmsg ? " - " : "", replmsg ? replmsg : "");
			} else { /* ldaprc is < 0 */
				PR_snprintf(ra->last_update_status, STATUS_LEN,
							"%d %s%sSystem error%s%s",
							ldaprc,message?message:"",message?"":" - ",
							replmsg ? " - " : "", replmsg ? replmsg : "");
			}
		}
		/* ldaprc == LDAP_SUCCESS */
		else if (replrc != 0)
		{
			if (replrc == NSDS50_REPL_REPLICA_BUSY)
			{
				PR_snprintf(ra->last_update_status, STATUS_LEN,
					"%d Can't acquire busy replica", replrc ); 
			}
			else if (replrc == NSDS50_REPL_REPLICA_RELEASE_SUCCEEDED)
			{
				PR_snprintf(ra->last_update_status, STATUS_LEN, "%d %s",
					ldaprc, "Replication session successful");
			}
			else if (replrc == NSDS50_REPL_DISABLED)
			{
				PR_snprintf(ra->last_update_status, STATUS_LEN, "%d Incremental update aborted: "
					"Replication agreement for %s\n can not be updated while the replica is disabled.\n"
					"(If the suffix is disabled you must enable it then restart the server for replication to take place).",
					replrc, ra->long_name ? ra->long_name : "a replica");
				/* Log into the errors log, as "ra->long_name" is not accessible from the caller */
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Incremental update aborted: Replication agreement for \"%s\" "
					"can not be updated while the replica is disabled\n", ra->long_name ? ra->long_name : "a replica");
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"(If the suffix is disabled you must enable it then restart the server for replication to take place).\n");
			}
			else
			{
				PR_snprintf(ra->last_update_status, STATUS_LEN,
					"%d Replication error acquiring replica: %s%s%s",
					replrc, protocol_response2string(replrc),
					message?" - ":"",message?message:"");
			}
		}
		else if (message != NULL) /* replrc == NSDS50_REPL_REPLICA_READY == 0 */
		{
			PR_snprintf(ra->last_update_status, STATUS_LEN, 
						"%d Replica acquired successfully: %s",
						ldaprc, message);
		}
		else
		{ /* agmt_set_last_update_status(0,0,NULL) to reset agmt */
			PR_snprintf(ra->last_update_status, STATUS_LEN, "%d", ldaprc);
		}
	}
}

void
agmt_set_last_init_status (Repl_Agmt *ra, int ldaprc, int replrc, const char *message)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		if (ldaprc != LDAP_SUCCESS)
		{
			char *replmsg = NULL;
			if ( replrc ) {
				replmsg = protocol_response2string(replrc);
				/* Do not mix the unknown replication error with the known ldap one */
				if ( strcasecmp(replmsg, "unknown error") == 0 ) {
					replmsg = NULL;
				}
			}
			if (ldaprc > 0) {
				PR_snprintf(ra->last_init_status, STATUS_LEN,
							"%d %s%sLDAP error: %s%s%s",
							ldaprc, 
							message?message:"",message?"":" - ",
							ldap_err2string(ldaprc),
							replmsg ? " - " : "", replmsg ? replmsg : "");
			} else { /* ldaprc is < 0 */
				PR_snprintf(ra->last_init_status, STATUS_LEN,
							"%d %s%sSystem error%s%s",
							ldaprc,message?message:"",message?"":" - ",
							replmsg ? " - " : "", replmsg ? replmsg : "");
			}
		}
		/* ldaprc == LDAP_SUCCESS */
		else if (replrc != 0)
		{
			if (replrc == NSDS50_REPL_REPLICA_RELEASE_SUCCEEDED)
			{
				PR_snprintf(ra->last_init_status, STATUS_LEN, "%d %s",
					ldaprc, "Replication session successful");
			}
			else if (replrc == NSDS50_REPL_DISABLED)
			{
				PR_snprintf(ra->last_init_status, STATUS_LEN, "%d Total update aborted: "
					"Replication agreement for %s\n can not be updated while the replica is disabled.\n"
					"(If the suffix is disabled you must enable it then restart the server for replication to take place).",
					replrc, ra->long_name ? ra->long_name : "a replica");
				/* Log into the errors log, as "ra->long_name" is not accessible from the caller */
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Total update aborted: Replication agreement for \"%s\" "
					"can not be updated while the replica is disabled\n", ra->long_name ? ra->long_name : "a replica");
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"(If the suffix is disabled you must enable it then restart the server for replication to take place).\n");
			}
			else
			{
				PR_snprintf(ra->last_init_status, STATUS_LEN,
					"%d Replication error acquiring replica: %s%s%s",
					replrc, protocol_response2string(replrc),
					message?" - ":"",message?message:"");
			}
		}
		else if (message != NULL) /* replrc == NSDS50_REPL_REPLICA_READY == 0 */
		{
			PR_snprintf(ra->last_init_status, STATUS_LEN,
						"%d Replica acquired successfully: %s", 
						ldaprc, message);
		}
		else
		{ /* agmt_set_last_init_status(0,0,NULL) to reset agmt */
			PR_snprintf(ra->last_init_status, STATUS_LEN, "%d", ldaprc);
		}
	}
}


void
agmt_set_update_in_progress (Repl_Agmt *ra, PRBool in_progress)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		ra->update_in_progress = in_progress;
	}
}

void
agmt_inc_last_update_changecount (Repl_Agmt *ra, ReplicaId rid, int skipped)
{
	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		int i;

		for ( i = 0; i < ra->num_changecounters; i++ )
		{
			if ( ra->changecounters[i]->rid == rid )
				break;
		}

		if ( i < ra->num_changecounters )
		{
			if ( skipped )
				ra->changecounters[i]->num_skipped ++;
			else
				ra->changecounters[i]->num_replayed ++;
		}
		else
		{
			ra->num_changecounters ++;
			ra->changecounters[i] = (struct changecounter*) slapi_ch_calloc(1, sizeof(struct changecounter));
			ra->changecounters[i]->rid = rid;
			if ( skipped )
				ra->changecounters[i]->num_skipped = 1;
			else
				ra->changecounters[i]->num_replayed = 1;
		}
	}
}

void
agmt_get_changecount_string (Repl_Agmt *ra, char *buf, int bufsize)
{
	char tmp_buf[32]; /* 5 digit RID, 10 digit each replayed and skipped */
	int i;
	int buflen = 0;

	*buf = '\0';
	if (NULL != ra)
	{
		for ( i = 0; i < ra->num_changecounters; i++ )
		{
			PR_snprintf (tmp_buf, sizeof(tmp_buf), "%u:%u/%u ",
				ra->changecounters[i]->rid,
				ra->changecounters[i]->num_replayed,
				ra->changecounters[i]->num_skipped);
			PR_snprintf (buf+buflen, bufsize-buflen, "%s", tmp_buf);
			buflen += strlen (tmp_buf);
		}
	}
}

static int
get_agmt_status(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter,
	int *returncode, char *returntext, void *arg)
{
	char *time_tmp = NULL;
	char changecount_string[BUFSIZ];
	Repl_Agmt *ra = (Repl_Agmt *)arg;

	PR_ASSERT(NULL != ra);
	if (NULL != ra)
	{
		PRBool reapActive = PR_FALSE;
		Slapi_DN *replarea_sdn = NULL;
		Object *repl_obj = NULL;

		replarea_sdn = agmt_get_replarea(ra);
		repl_obj = replica_get_replica_from_dn(replarea_sdn);
		slapi_sdn_free(&replarea_sdn);
		if (repl_obj) {
			Replica *replica = (Replica*)object_get_data (repl_obj);
			reapActive = replica_get_tombstone_reap_active(replica);
			object_release(repl_obj);
		}
		slapi_entry_attr_set_int(e, "nsds5replicaReapActive", (int)reapActive);

		/* these values persist in the dse.ldif file, so we delete them
		   here to avoid multi valued attributes */
		slapi_entry_attr_delete(e, "nsds5replicaLastUpdateStart");
		slapi_entry_attr_delete(e, "nsds5replicaLastUpdateEnd");
		slapi_entry_attr_delete(e, "nsds5replicaChangesSentSinceStartup");
		slapi_entry_attr_delete(e, "nsds5replicaLastUpdateStatus");
		slapi_entry_attr_delete(e, "nsds5replicaUpdateInProgress");
		slapi_entry_attr_delete(e, "nsds5replicaLastInitStart");
		slapi_entry_attr_delete(e, "nsds5replicaLastInitStatus");
		slapi_entry_attr_delete(e, "nsds5replicaLastInitEnd");

		/* now, add the real values (singly) */
		if (ra->last_update_start_time == 0)
		{
			slapi_entry_add_string(e, "nsds5replicaLastUpdateStart", "0");
		}
		else
		{
			time_tmp = format_genTime(ra->last_update_start_time);
			slapi_entry_add_string(e, "nsds5replicaLastUpdateStart", time_tmp);
			slapi_ch_free((void **)&time_tmp);
		}
		if (ra->last_update_end_time == 0)
		{
			slapi_entry_add_string(e, "nsds5replicaLastUpdateEnd", "0");
		}
		else
		{
			time_tmp = format_genTime(ra->last_update_end_time);
			slapi_entry_add_string(e, "nsds5replicaLastUpdateEnd", time_tmp);
			slapi_ch_free((void **)&time_tmp);
		}
		agmt_get_changecount_string (ra, changecount_string, sizeof (changecount_string) );
		slapi_entry_add_string(e, "nsds5replicaChangesSentSinceStartup", changecount_string);
		if (ra->last_update_status[0] == '\0')
		{
			slapi_entry_add_string(e, "nsds5replicaLastUpdateStatus", "0 No replication sessions started since server startup");
		}
		else
		{
			slapi_entry_add_string(e, "nsds5replicaLastUpdateStatus", ra->last_update_status);
		}
		slapi_entry_add_string(e, "nsds5replicaUpdateInProgress", ra->update_in_progress ? "TRUE" : "FALSE");
		if (ra->last_init_start_time == 0)
		{
			slapi_entry_add_string(e, "nsds5replicaLastInitStart", "0");
		}
		else
		{
			time_tmp = format_genTime(ra->last_init_start_time);
			slapi_entry_add_string(e, "nsds5replicaLastInitStart", time_tmp);
			slapi_ch_free((void **)&time_tmp);
		}		
		if (ra->last_init_end_time == 0)
		{
			slapi_entry_add_string(e, "nsds5replicaLastInitEnd", "0");
		}
		else
		{
			time_tmp = format_genTime(ra->last_init_end_time);
			slapi_entry_add_string(e, "nsds5replicaLastInitEnd", time_tmp);
			slapi_ch_free((void **)&time_tmp);
		}		
		if (ra->last_init_status[0] != '\0')
		{
			slapi_entry_add_string(e, "nsds5replicaLastInitStatus", ra->last_init_status);
		}
	}
	return SLAPI_DSE_CALLBACK_OK;
}

static void 
update_window_state_change_callback (void *arg, PRBool opened)
{
    Repl_Agmt *agmt = (Repl_Agmt*)arg;

    PR_ASSERT (agmt); 
 
    if (opened)
    {
        prot_notify_window_opened (agmt->protocol);
    }
    else
    {
        prot_notify_window_closed (agmt->protocol);
    }          
}

ReplicaId
agmt_get_consumer_rid ( Repl_Agmt *agmt, void *conn )
{
	if ( agmt->consumerRID <= 0 ) {

		char *mapping_tree_node = NULL;
		struct berval **bvals = NULL;


		/* This function converts the old style DN to the new one. */
		mapping_tree_node = 
		slapi_create_dn_string("cn=replica,cn=\"%s\",cn=mapping tree,cn=config",
							   slapi_sdn_get_dn (agmt->replarea) );
		if (NULL == mapping_tree_node) {
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
							"agmt_get_consumer_rid: failed to normalize "
							"replica dn for %s\n", 
							slapi_sdn_get_dn (agmt->replarea));
			agmt->consumerRID = 0;
		}
		conn_read_entry_attribute ( conn, mapping_tree_node, "nsDS5ReplicaID", &bvals );
		if ( NULL != bvals && NULL != bvals[0] ) {
			char *ridstr = slapi_ch_malloc( bvals[0]->bv_len + 1 );
			memcpy ( ridstr, bvals[0]->bv_val, bvals[0]->bv_len );
			ridstr[bvals[0]->bv_len] = '\0';
			agmt->consumerRID = atoi (ridstr);
			slapi_ch_free ( (void**) &ridstr );
			ber_bvecfree ( bvals );
		}
		slapi_ch_free_string(&mapping_tree_node);
	}

	return agmt->consumerRID;
}

int 
get_agmt_agreement_type( Repl_Agmt *agmt)
{
    PR_ASSERT (agmt); 
    return agmt->agreement_type;
}

void* agmt_get_priv (const Repl_Agmt *agmt)
{
	PR_ASSERT (agmt);
	return agmt->priv;
}

void agmt_set_priv (Repl_Agmt *agmt, void* priv)
{
	PR_ASSERT (agmt);
	agmt->priv = priv;
}

ReplicaId agmt_get_consumerRID(Repl_Agmt *ra)
{
	return ra->consumerRID;
}

int
agmt_has_protocol(Repl_Agmt *agmt)
{
	if (agmt) {
		return NULL != agmt->protocol;
	}
	return 0;
}
