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
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* repl5_replica.c */

#include "slapi-plugin.h"
#include "repl.h"   /* ONREPL - this is bad */
#include "repl5.h" 
#include "repl_shared.h" 
#include "csnpl.h"
#include "cl5_api.h"

#define RUV_SAVE_INTERVAL (30 * 1000) /* 30 seconds */

#define REPLICA_RDN				 "cn=replica"
#define CHANGELOG_RDN            "cn=legacy changelog"

/*
 * A replica is a locally-held copy of a portion of the DIT.
 */
struct replica {
	Slapi_DN *repl_root;			/* top of the replicated area			*/
	char *repl_name;                /* unique replica name                  */
	PRBool new_name;                /* new name was generated - need to be saved */
	ReplicaUpdateDNList updatedn_list;	/* list of dns with which a supplier should bind
										   to update this replica				*/
	ReplicaType	 repl_type;			/* is this replica read-only ?			*/
	PRBool  legacy_consumer;        /* if true, this replica is supplied by 4.0 consumer */
	char*   legacy_purl;            /* partial url of the legacy supplier   */
	ReplicaId repl_rid;				/* replicaID							*/
	Object	*repl_ruv;				/* replica update vector				*/
	PRBool repl_ruv_dirty;          /* Dirty flag for ruv                   */
	CSNPL *min_csn_pl;              /* Pending list for minimal CSN         */
	void *csn_pl_reg_id;            /* registration assignment for csn callbacks */
	unsigned long repl_state_flags;	/* state flags							*/
	PRUint32    repl_flags;         /* persistent, externally visible flags */
	PRLock	*repl_lock;				/* protects entire structure			*/
	Slapi_Eq_Context repl_eqcxt_rs;	/* context to cancel event that saves ruv */	
	Slapi_Eq_Context repl_eqcxt_tr;	/* context to cancel event that reaps tombstones */	
	Object *repl_csngen;			/* CSN generator for this replica */
	PRBool repl_csn_assigned;		/* Flag set when new csn is assigned. */
	PRUint32 repl_purge_delay;		/* When purgeable, CSNs are held on to for this many extra seconds */
	PRBool tombstone_reap_stop;		/* TRUE when the tombstone reaper should stop */
	PRBool tombstone_reap_active;	/* TRUE when the tombstone reaper is running */
	long tombstone_reap_interval; 	/* Time in seconds between tombstone reaping */
	Slapi_ValueSet *repl_referral;  /* A list of administrator provided referral URLs */
	PRBool state_update_inprogress; /* replica state is being updated */
	PRLock *agmt_lock;          	/* protects agreement creation, start and stop */
	char *locking_purl;				/* supplier who has exclusive access */
	PRUint64 protocol_timeout;		/* protocol shutdown timeout */
	PRUint64 backoff_min;			/* backoff retry minimum */
	PRUint64 backoff_max;			/* backoff retry maximum */
};


typedef struct reap_callback_data
{
	int rc;
	unsigned long num_entries;
	unsigned long num_purged_entries;
	CSN *purge_csn;
	PRBool *tombstone_reap_stop;
} reap_callback_data;


/* Forward declarations of helper functions*/
static Slapi_Entry* _replica_get_config_entry (const Slapi_DN *root, char **attrs);
static int _replica_check_validity (const Replica *r);
static int _replica_init_from_config (Replica *r, Slapi_Entry *e, char *errortext);
static int _replica_update_entry (Replica *r, Slapi_Entry *e, char *errortext);
static int _replica_configure_ruv (Replica *r, PRBool isLocked);
static char * _replica_get_config_dn (const Slapi_DN *root);
static char * _replica_type_as_string (const Replica *r);
/* DBDB, I think this is probably bogus : */
static int replica_create_ruv_tombstone(Replica *r);
static void assign_csn_callback(const CSN *csn, void *data);
static void abort_csn_callback(const CSN *csn, void *data);
static void eq_cb_reap_tombstones(time_t when, void *arg);
static CSN *_replica_get_purge_csn_nolock (const Replica *r);
static void replica_get_referrals_nolock (const Replica *r, char ***referrals);
static void replica_clear_legacy_referrals (const Slapi_DN *repl_root_sdn, char **referrals, const char *state);
static void replica_remove_legacy_attr (const Slapi_DN *repl_root_sdn, const char *attr);
static int replica_log_ruv_elements_nolock (const Replica *r);
static void replica_replace_ruv_tombstone(Replica *r);
static void start_agreements_for_replica (Replica *r, PRBool start);
static void _delete_tombstone(const char *tombstone_dn, const char *uniqueid, int ext_op_flags);
static void replica_strip_cleaned_rids(Replica *r);

/* Allocates new replica and reads its state and state of its component from
 * various parts of the DIT. 
 */
Replica *
replica_new(const Slapi_DN *root)
{
	Replica *r = NULL;
	Slapi_Entry *e = NULL;
	char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
	
	PR_ASSERT (root);

	/* check if there is a replica associated with the tree */
	e = _replica_get_config_entry (root, NULL);
	if (e)
	{
		errorbuf[0] = '\0';
		r = replica_new_from_entry(e, errorbuf,
			PR_FALSE /* not a newly added entry */);

	    if (NULL == r)
	    {
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Unable to "
			    "configure replica %s: %s\n", 
				slapi_sdn_get_dn(root),
			    errorbuf);
	    }

	    slapi_entry_free (e);
    }

	return r;
}

/* constructs the replica object from the newly added entry */
Replica *
replica_new_from_entry (Slapi_Entry *e, char *errortext, PRBool is_add_operation)
{
    int rc = 0;
    Replica *r;
	char *repl_name = NULL;

    if (e == NULL)
    {
        if (NULL != errortext)
		{
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "NULL entry");
		}
        return NULL;        
    }

   	r = (Replica *)slapi_ch_calloc(1, sizeof(Replica));

        if (!r)
	{
        	if (NULL != errortext)
		{
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Out of memory");
		}
		rc = -1;
		goto done;
	}

	if ((r->repl_lock = PR_NewLock()) == NULL)
	{
		if (NULL != errortext)
		{
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to create replica lock");
		}
		rc = -1;
		goto done;
	}

	if ((r->agmt_lock = PR_NewLock()) == NULL)
	{
		if (NULL != errortext)
		{
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to create replica lock");
		}
		rc = -1;
		goto done;
	}

    /* read parameters from the replica config entry */
    rc = _replica_init_from_config (r, e, errortext);
    if (rc != 0)
	{
		goto done;
	}

	/* configure ruv */
	rc = _replica_configure_ruv (r, PR_FALSE);
	if (rc != 0)
	{
		goto done;
	}

	/* If smallest csn exists in RUV for our local replica, it's ok to begin iteration */
	PR_ASSERT (object_get_data (r->repl_ruv));

	if (is_add_operation)
	{
		/*
		 * This is called by an ldap add operation.
         * Update the entry to contain information generated
		 * during replica initialization
		 */
		rc = _replica_update_entry (r, e, errortext);
	}
	else
	{
		/*
		 * Entry is already in dse.ldif - update it on the disk
		 * (done by the update state event scheduled below)
		 */
	}
    if (rc != 0)
		goto done;

    /* ONREPL - the state update can occur before the entry is added to the DIT. 
       In that case the updated would fail but nothing bad would happen. The next
       scheduled update would save the state */
	repl_name = slapi_ch_strdup (r->repl_name);
	r->repl_eqcxt_rs = slapi_eq_repeat(replica_update_state, repl_name,
                                       current_time () + START_UPDATE_DELAY, RUV_SAVE_INTERVAL);

	if (r->tombstone_reap_interval > 0)
	{
		/* 
		 * Reap Tombstone should be started some time after the plugin started.
		 * This will allow the server to fully start before consuming resources.
		 */
		repl_name = slapi_ch_strdup (r->repl_name);
		r->repl_eqcxt_tr = slapi_eq_repeat(eq_cb_reap_tombstones, repl_name,
										   current_time() + r->tombstone_reap_interval,
										   1000 * r->tombstone_reap_interval);
	}

    if (r->legacy_consumer)
    {
        legacy_consumer_init_referrals (r);
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_new_from_entry: "
                        "replica for %s was configured as legacy consumer\n",
                        slapi_sdn_get_dn(r->repl_root));
    }

    replica_check_for_tasks(r, e);

done:
    if (rc != 0 && r)
	{
        replica_destroy ((void**)&r);    
	}
   
    return r;
}


void
replica_flush(Replica *r)
{
	PR_ASSERT(NULL != r);
	if (NULL != r)
	{
		PR_Lock(r->repl_lock);
		/* Make sure we dump the CSNGen state */
		r->repl_csn_assigned = PR_TRUE;
		PR_Unlock(r->repl_lock);
		/* This function take the Lock Inside */
		/* And also write the RUV */
		replica_update_state((time_t)0, r->repl_name);
	}
}

void
replica_set_csn_assigned(Replica *r)
{
    PR_Lock(r->repl_lock);
    r->repl_csn_assigned = PR_TRUE;
    PR_Unlock(r->repl_lock);
}

/* 
 * Deallocate a replica. arg should point to the address of a
 * pointer that points to a replica structure.
 */
void
replica_destroy(void **arg)
{
	Replica *r;
	void *repl_name;

	if (arg == NULL)
		return;

	r = *((Replica **)arg);

	PR_ASSERT(r);

	slapi_log_error (SLAPI_LOG_REPL, NULL, "replica_destroy\n");

	/*
	 * The function will not be called unless the refcnt of its
	 * wrapper object is 0. Hopefully this refcnt could sync up
	 * this destruction and the events such as tombstone reap
	 * and ruv updates.
	 */

	if (r->repl_eqcxt_rs)
    {
		repl_name = slapi_eq_get_arg (r->repl_eqcxt_rs);
		slapi_ch_free (&repl_name);
		slapi_eq_cancel(r->repl_eqcxt_rs);
		r->repl_eqcxt_rs = NULL;
    }

	if (r->repl_eqcxt_tr)
    {
		repl_name = slapi_eq_get_arg (r->repl_eqcxt_tr);
		slapi_ch_free (&repl_name);
		slapi_eq_cancel(r->repl_eqcxt_tr);
		r->repl_eqcxt_tr = NULL;
    }

	if (r->repl_root)
    {
		slapi_sdn_free(&r->repl_root);
    }

	slapi_ch_free_string(&r->locking_purl);

	if (r->updatedn_list)
    {
		replica_updatedn_list_free(r->updatedn_list);
		r->updatedn_list = NULL;
    }

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&r->repl_name);
	slapi_ch_free ((void**)&r->legacy_purl);    

	if (r->repl_lock)
	{
		PR_DestroyLock(r->repl_lock);
		r->repl_lock = NULL;
	}

	if (r->agmt_lock)
	{
		PR_DestroyLock(r->agmt_lock);
		r->agmt_lock = NULL;
	}

	if(NULL != r->repl_ruv) 
	{
		object_release(r->repl_ruv);
	}

	if(NULL != r->repl_csngen) 
	{
        if (r->csn_pl_reg_id)
        {
            csngen_unregister_callbacks((CSNGen *)object_get_data (r->repl_csngen), r->csn_pl_reg_id);
        }
		object_release(r->repl_csngen);
	}

	if (NULL != r->repl_referral)
	{
		slapi_valueset_free(r->repl_referral);
	}

	if (NULL != r->min_csn_pl)
	{
		csnplFree(&r->min_csn_pl);;
	}

	slapi_ch_free((void **)arg);
}

/*
 * Attempt to obtain exclusive access to replica (advisory only)
 *
 * Returns PR_TRUE if exclusive access was granted,
 * PR_FALSE otherwise
 * The parameter isInc tells whether or not the replica is being
 * locked for an incremental update session - if the replica is
 * successfully locked, this value is used - if the replica is already
 * in use, this value will be set to TRUE or FALSE, depending on what
 * type of update session has the replica in use currently
 * locking_purl is the supplier who is attempting to acquire access
 * current_purl is the supplier who already has access, if any
 */
PRBool
replica_get_exclusive_access(Replica *r, PRBool *isInc, PRUint64 connid, int opid,
							 const char *locking_purl,
							 char **current_purl)
{
	PRBool rval = PR_TRUE;

	PR_ASSERT(r);

	PR_Lock(r->repl_lock);
	if (r->repl_state_flags & REPLICA_IN_USE)
	{
		if (isInc)
			*isInc = (r->repl_state_flags & REPLICA_INCREMENTAL_IN_PROGRESS);

		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"conn=%" NSPRIu64 " op=%d repl=\"%s\": "
				"Replica in use locking_purl=%s\n",
				connid, opid,
				slapi_sdn_get_dn(r->repl_root),
				r->locking_purl ? r->locking_purl : "unknown");
		rval = PR_FALSE;
		if (current_purl)
		{
			*current_purl = slapi_ch_strdup(r->locking_purl);
		}
	}
	else
	{
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
						"conn=%" NSPRIu64 " op=%d repl=\"%s\": Acquired replica\n",
						connid, opid,
						slapi_sdn_get_dn(r->repl_root));
		r->repl_state_flags |= REPLICA_IN_USE;
		if (isInc && *isInc) 
		{
			r->repl_state_flags |= REPLICA_INCREMENTAL_IN_PROGRESS;
		}
		else
		{ 
			/* if connid or opid != 0, it's a total update */
			/* Both set to 0 means we're disabling replication */
			if (connid || opid)
			{
				r->repl_state_flags |= REPLICA_TOTAL_IN_PROGRESS;
			}
		}
		slapi_ch_free_string(&r->locking_purl);
		r->locking_purl = slapi_ch_strdup(locking_purl);
	}
	PR_Unlock(r->repl_lock);
	return rval;
}

/* 
 * Relinquish exclusive access to the replica 
 */
void
replica_relinquish_exclusive_access(Replica *r, PRUint64 connid, int opid)
{
	PRBool isInc;

	PR_ASSERT(r);
	
	PR_Lock(r->repl_lock);
	isInc = (r->repl_state_flags & REPLICA_INCREMENTAL_IN_PROGRESS);
	/* check to see if the replica is in use and log a warning if not */
	if (!(r->repl_state_flags & REPLICA_IN_USE))
	{
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
					"conn=%" NSPRIu64 " op=%d repl=\"%s\": "
					"Replica not in use\n",
					connid, opid,
					slapi_sdn_get_dn(r->repl_root));
	} else {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
					"conn=%" NSPRIu64 " op=%d repl=\"%s\": "
					"Released replica held by locking_purl=%s\n",
					connid, opid,
					slapi_sdn_get_dn(r->repl_root),
					r->locking_purl);

		slapi_ch_free_string(&r->locking_purl);
		r->repl_state_flags &= ~(REPLICA_IN_USE);
		if (isInc)
			r->repl_state_flags &= ~(REPLICA_INCREMENTAL_IN_PROGRESS);
		else
			r->repl_state_flags &= ~(REPLICA_TOTAL_IN_PROGRESS);
	}
	PR_Unlock(r->repl_lock);
}

/* 
 * Returns root of the replicated area 
 */
PRBool
replica_get_tombstone_reap_active(const Replica *r)
{
	PR_ASSERT(r);

	return(r->tombstone_reap_active);
}

/* 
 * Returns root of the replicated area 
 */
const Slapi_DN *
replica_get_root(const Replica *r) /* ONREPL - should we return copy instead? */
{
	PR_ASSERT(r);

    /* replica root never changes so we don't have to lock */
	return(r->repl_root);
}

/* 
 * Returns normalized dn of the root of the replicated area 
 */
const char *
replica_get_name(const Replica *r) /* ONREPL - should we return copy instead? */
{
    PR_ASSERT(r);

    /* replica name never changes so we don't have to lock */
    return(r->repl_name);
}

/* 
 * Returns replicaid of this replica 
 */
ReplicaId 
replica_get_rid (const Replica *r)
{
	ReplicaId rid;
	PR_ASSERT(r);

	PR_Lock(r->repl_lock);
	rid = r->repl_rid;
	PR_Unlock(r->repl_lock);
	return rid;
}

/* 
 * Sets replicaid of this replica - should only be used when also changing the type
 */
void
replica_set_rid (Replica *r, ReplicaId rid)
{
	PR_ASSERT(r);

	PR_Lock(r->repl_lock);
	r->repl_rid = rid;
	PR_Unlock(r->repl_lock);
}

/* Returns true if replica was initialized through ORC or import;
 * otherwise, false. An uninitialized replica should return
 * LDAP_UNWILLING_TO_PERFORM to all client requests 
 */
PRBool 
replica_is_initialized (const Replica *r)
{
	PR_ASSERT(r);
	return (r->repl_ruv != NULL);
}

/* 
 * Returns refcounted object that contains RUV. The caller should release the
 * object once it is no longer used. To release, call object_release 
 */
Object *
replica_get_ruv (const Replica *r)
{
	Object *ruv = NULL;

	PR_ASSERT(r);

	PR_Lock(r->repl_lock);

	PR_ASSERT (r->repl_ruv);

    object_acquire (r->repl_ruv);

	ruv = r->repl_ruv;

	PR_Unlock(r->repl_lock);

	return ruv;
}

/* 
 * Sets RUV vector. This function should be called during replica
 * (re)initialization. During normal operation, the RUV is read from
 * the root of the replicated in the replica_new call 
 */
void 
replica_set_ruv (Replica *r, RUV *ruv)
{
	PR_ASSERT(r && ruv);

	PR_Lock(r->repl_lock);

	if(NULL != r->repl_ruv) 
	{
		object_release(r->repl_ruv);
	}

    /* if the local replica is not in the RUV and it is writable - add it
       and reinitialize min_csn pending list */
    if (r->repl_type == REPLICA_TYPE_UPDATABLE)
    {
        CSN *csn = NULL;
        if (r->min_csn_pl)
            csnplFree (&r->min_csn_pl);

        if (ruv_contains_replica (ruv, r->repl_rid))
        {
            ruv_get_smallest_csn_for_replica(ruv, r->repl_rid, &csn);
            if (csn)
                csn_free (&csn);
            else
                r->min_csn_pl = csnplNew ();    
			/* We need to make sure the local ruv element is the 1st. */
			ruv_move_local_supplier_to_first(ruv, r->repl_rid);			
        }
        else
        {
            r->min_csn_pl = csnplNew ();
			/* To be sure that the local is in first */ 
			ruv_add_index_replica(ruv, r->repl_rid, multimaster_get_local_purl(), 1);
        }
    }

	r->repl_ruv = object_new((void*)ruv, (FNFree)ruv_destroy);
	r->repl_ruv_dirty = PR_TRUE;

	PR_Unlock(r->repl_lock);
}

/*
 * Update one particular CSN in an RUV. This is meant to be called
 * whenever (a) the server has processed a client operation and
 * needs to update its CSN, or (b) the server is completing an
 * inbound replication session operation, and needs to update its
 * local RUV.
 */
void
replica_update_ruv(Replica *r, const CSN *updated_csn, const char *replica_purl)
{
	char csn_str[CSN_STRSIZE];
	
	PR_ASSERT(NULL != r);
	PR_ASSERT(NULL != updated_csn);
#ifdef DEBUG
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
		"replica_update_ruv: csn %s\n",
		csn_as_string(updated_csn, PR_FALSE, csn_str)); /* XXXggood remove debugging */
#endif
	if (NULL == r)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_update_ruv: replica "
			"is NULL\n");
	}
	else if (NULL == updated_csn)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_update_ruv: csn "
			"is NULL when updating replica %s\n", slapi_sdn_get_dn(r->repl_root));
	}
	else
	{
		RUV *ruv;
		PR_Lock(r->repl_lock);
		
		if (r->repl_ruv != NULL)
		{
			ruv = object_get_data(r->repl_ruv);
			if (NULL != ruv)
			{
				ReplicaId rid = csn_get_replicaid(updated_csn);
				if (rid == r->repl_rid)
				{
					if (NULL != r->min_csn_pl)
					{
						CSN *min_csn;
						PRBool committed;
						(void)csnplCommit(r->min_csn_pl, updated_csn);
						min_csn = csnplGetMinCSN(r->min_csn_pl, &committed);
						if (NULL != min_csn)
						{
							if (committed)
							{
								ruv_set_min_csn(ruv, min_csn, replica_purl);
								csnplFree(&r->min_csn_pl);
							}
							csn_free(&min_csn);
						}
					}
				}
				/* Update max csn for local and remote replicas */
				if (ruv_update_ruv (ruv, updated_csn, replica_purl, rid == r->repl_rid) 
                    != RUV_SUCCESS)
				{
					slapi_log_error(SLAPI_LOG_FATAL,
						repl_plugin_name, "replica_update_ruv: unable "
						"to update RUV for replica %s, csn = %s\n",
						slapi_sdn_get_dn(r->repl_root),
						csn_as_string(updated_csn, PR_FALSE, csn_str));
				}
			
				r->repl_ruv_dirty = PR_TRUE;
			}
			else
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"replica_update_ruv: unable to get RUV object for replica "
					"%s\n", slapi_sdn_get_dn(r->repl_root));
			}
		}
		else
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_update_ruv: "
				"unable to initialize RUV for replica %s\n",
				slapi_sdn_get_dn(r->repl_root));
		}
		PR_Unlock(r->repl_lock);
	}
}

/* 
 * Returns refcounted object that contains csn generator. The caller should release the
 * object once it is no longer used. To release, call object_release 
 */
Object *
replica_get_csngen (const Replica *r)
{
	Object *csngen;

	PR_ASSERT(r);

	PR_Lock(r->repl_lock);

	object_acquire (r->repl_csngen);
	csngen = r->repl_csngen;

	PR_Unlock(r->repl_lock);

	return csngen;
}

/* 
 * Returns the replica type.
 */
ReplicaType 
replica_get_type (const Replica *r)
{
	PR_ASSERT(r);
	return r->repl_type;
}

int
replica_get_protocol_timeout(Replica *r)
{
	return (int)r->protocol_timeout;
}

/* 
 * Sets the replica type.
 */
void 
replica_set_type (Replica *r, ReplicaType type)
{
	PR_ASSERT(r);

	PR_Lock(r->repl_lock);
	r->repl_type = type;
	PR_Unlock(r->repl_lock);
}

/* 
 * Returns PR_TRUE if this replica is a consumer of 4.0 server
 * and PR_FALSE otherwise
 */
PRBool
replica_is_legacy_consumer (const Replica *r)
{
	PR_ASSERT(r);
	return r->legacy_consumer;
}

/* 
 * Sets the replica type.
 */
void 
replica_set_legacy_consumer (Replica *r, PRBool legacy_consumer)
{
    int legacy2mmr;
	Slapi_DN *repl_root_sdn = NULL;
	char **referrals = NULL;
	char *replstate = NULL;
	PR_ASSERT(r);

    PR_Lock(r->repl_lock);

    legacy2mmr = r->legacy_consumer && !legacy_consumer;

    /* making the server a regular 5.0 replica */
    if (legacy2mmr)
    {
        slapi_ch_free ((void**)&r->legacy_purl);
        /* Remove copiedFrom/copyingFrom attributes from the root entry */           
		/* set the right state in the mapping tree */
		if (r->repl_type == REPLICA_TYPE_READONLY)
		{
			replica_get_referrals_nolock (r, &referrals);
			replstate = STATE_UPDATE_REFERRAL;
		}
		else /* updateable */
		{
			replstate = STATE_BACKEND;
		}
    }

    r->legacy_consumer = legacy_consumer;
	repl_root_sdn = slapi_sdn_dup(r->repl_root);
    PR_Unlock(r->repl_lock);

    if (legacy2mmr)
    {
		replica_clear_legacy_referrals(repl_root_sdn, referrals, replstate);
		/* Also change state of the mapping tree node and/or referrals */
        replica_remove_legacy_attr (repl_root_sdn, type_copiedFrom);
        replica_remove_legacy_attr (repl_root_sdn, type_copyingFrom);
    }
	charray_free(referrals);
	slapi_sdn_free(&repl_root_sdn);
}

/* Gets partial url of the legacy supplier - applicable for legacy consumer only */
char *
replica_get_legacy_purl (const Replica *r)
{
    char *purl;

    PR_Lock (r->repl_lock);

    PR_ASSERT (r->legacy_consumer);

    purl = slapi_ch_strdup (r->legacy_purl);

    PR_Unlock (r->repl_lock);

    return purl;
}

void 
replica_set_legacy_purl (Replica *r, const char *purl)
{
    PR_Lock (r->repl_lock);

    PR_ASSERT (r->legacy_consumer);

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&r->legacy_purl);

    r->legacy_purl = slapi_ch_strdup (purl);

    PR_Unlock (r->repl_lock);
}

/* 
 * Returns true if sdn is the same as updatedn and false otherwise 
 */
PRBool 
replica_is_updatedn (const Replica *r, const Slapi_DN *sdn)
{
	PRBool result;

	PR_ASSERT (r);

	PR_Lock(r->repl_lock);

    if (sdn == NULL)
    {
        result = (r->updatedn_list == NULL);    
    }
    else if (r->updatedn_list == NULL)
    {
        result = PR_FALSE;
    }
    else
    {
		result = replica_updatedn_list_ismember(r->updatedn_list, sdn);
    }

	PR_Unlock(r->repl_lock);

	return result;
}

/* 
 * Sets updatedn list for this replica 
 */
void 
replica_set_updatedn (Replica *r, const Slapi_ValueSet *vs, int mod_op)
{
	PR_ASSERT (r);

	PR_Lock(r->repl_lock);

	if (!r->updatedn_list)
		r->updatedn_list = replica_updatedn_list_new(NULL);

	if (SLAPI_IS_MOD_DELETE(mod_op) || vs == NULL ||
		(0 == slapi_valueset_count(vs))) /* null value also causes list deletion */
		replica_updatedn_list_delete(r->updatedn_list, vs);
	else if (SLAPI_IS_MOD_REPLACE(mod_op))
		replica_updatedn_list_replace(r->updatedn_list, vs);
	else if (SLAPI_IS_MOD_ADD(mod_op))
		replica_updatedn_list_add(r->updatedn_list, vs);

	PR_Unlock(r->repl_lock);
}

void
replica_reset_csn_pl(Replica *r)
{
    PR_Lock(r->repl_lock);

    if (NULL != r->min_csn_pl){
        csnplFree (&r->min_csn_pl);
    }
    r->min_csn_pl = csnplNew();

    PR_Unlock(r->repl_lock);
}

/* gets current replica generation for this replica */
char *replica_get_generation (const Replica *r)
{
    int rc = 0;
    char *gen = NULL;

    if (r)
    {        
        PR_Lock(r->repl_lock);

        PR_ASSERT (r->repl_ruv);
	        
        if (rc == 0)
            gen = ruv_get_replica_generation ((RUV*)object_get_data (r->repl_ruv));

        PR_Unlock(r->repl_lock);    
    }

    return gen;
}

PRBool replica_is_flag_set (const Replica *r, PRUint32 flag)
{
    if (r)
        return (r->repl_flags & flag);
    else
        return PR_FALSE;
}

void replica_set_flag (Replica *r, PRUint32 flag, PRBool clear)
{
    if (r == NULL)
        return;

	PR_Lock(r->repl_lock);

    if (clear)
    {
        r->repl_flags &= ~flag; 
    }
    else
    {
        r->repl_flags |= flag;
    }

	PR_Unlock(r->repl_lock);
}

void replica_replace_flags (Replica *r, PRUint32 flags)
{
    if (r)
    {
		PR_Lock(r->repl_lock);
        r->repl_flags = flags;
		PR_Unlock(r->repl_lock);
    }
}

void
replica_get_referrals(const Replica *r, char ***referrals)
{
    PR_Lock(r->repl_lock);
    replica_get_referrals_nolock (r, referrals);
    PR_Unlock(r->repl_lock);
}

void
replica_set_referrals(Replica *r,const Slapi_ValueSet *vs)
{
	int ii = 0;
	Slapi_Value *vv = NULL;
	if (r->repl_referral == NULL)
	{
		r->repl_referral = slapi_valueset_new();
	}
	else
	{
		slapi_valueset_done(r->repl_referral);
	}
    slapi_valueset_set_valueset(r->repl_referral, vs);
	/* make sure the DN is included in the referral LDAP URL */
	if (r->repl_referral)
	{
		Slapi_ValueSet *newvs = slapi_valueset_new();
		const char *repl_root = slapi_sdn_get_dn(r->repl_root);
		ii = slapi_valueset_first_value(r->repl_referral, &vv);
		while (vv)
		{
			const char *ref = slapi_value_get_string(vv);
			LDAPURLDesc *lud = NULL;

			(void)slapi_ldap_url_parse(ref, &lud, 0, NULL);
			/* see if the dn is already in the referral URL */
			if (!lud || !lud->lud_dn) {
				/* add the dn */
				Slapi_Value *newval = NULL;
				int len = strlen(ref);
				char *tmpref = NULL;
				int need_slash = 0;
				if (ref[len-1] != '/') {
					need_slash = 1;
				}
				tmpref = slapi_ch_smprintf("%s%s%s", ref, (need_slash ? "/" : ""),
						repl_root);
				newval = slapi_value_new_string(tmpref);
				slapi_ch_free_string(&tmpref); /* sv_new_string makes a copy */
				slapi_valueset_add_value(newvs, newval);
				slapi_value_free(&newval); /* s_vs_add_value makes a copy */
			}
			if (lud)
				ldap_free_urldesc(lud);
			ii = slapi_valueset_next_value(r->repl_referral, ii, &vv);
		}
		if (slapi_valueset_count(newvs) > 0) {
			slapi_valueset_done(r->repl_referral);
			slapi_valueset_set_valueset(r->repl_referral, newvs);
		}
		slapi_valueset_free(newvs); /* s_vs_set_vs makes a copy */
	}
}

int 
replica_update_csngen_state_ext (Replica *r, const RUV *ruv, const CSN *extracsn)
{
    int rc = 0;
    CSNGen *gen;
    CSN *csn = NULL;
   
    PR_ASSERT (r && ruv);

    rc = ruv_get_max_csn(ruv, &csn);
    if (rc != RUV_SUCCESS)
    {
        return -1;
    }

    if ((csn == NULL) && (extracsn == NULL)) /* ruv contains no csn and no extra - we are done */
    {
        return 0;
    }

    if (csn_compare(extracsn, csn) > 0) /* extracsn > csn */
    {
        csn_free (&csn); /* free */
        csn = (CSN*)extracsn; /* use this csn to do the update */
    }

    PR_Lock(r->repl_lock);

    gen = (CSNGen *)object_get_data (r->repl_csngen);
    PR_ASSERT (gen);

    rc = csngen_adjust_time (gen, csn);
    /* rc will be either CSN_SUCCESS (0) or clock skew */

/* done: */

    PR_Unlock(r->repl_lock);
    if (csn != extracsn) /* do not free the given csn */
    {
        csn_free (&csn);
    }

    return rc;  
}

int 
replica_update_csngen_state (Replica *r, const RUV *ruv)
{
    return replica_update_csngen_state_ext(r, ruv, NULL);
}

/* 
 * dumps replica state for debugging purpose 
 */
void
replica_dump(Replica *r)
{
	char *updatedn_list = NULL;
	PR_ASSERT (r);

	PR_Lock(r->repl_lock);

    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "Replica state:\n");	
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\treplica root: %s\n",
                    slapi_sdn_get_ndn (r->repl_root));
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\treplica type: %s\n",
                    _replica_type_as_string (r));    	
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\treplica id: %d\n", r->repl_rid);
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\tflags: %d\n", r->repl_flags);
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\tstate flags: %lu\n", r->repl_state_flags);
	if (r->updatedn_list)
		updatedn_list = replica_updatedn_list_to_string(r->updatedn_list, "\n\t\t");
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\tupdate dn: %s\n",
            updatedn_list? updatedn_list : "not configured");
	slapi_ch_free_string(&updatedn_list);
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\truv: %s configured and is %sdirty\n",
                    r->repl_ruv ? "" : "not", r->repl_ruv_dirty ? "" : "not ");
    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "\tCSN generator: %s configured\n",
                    r->repl_csngen ? "" : "not");
	/* JCMREPL - Dump Referrals */

	PR_Unlock(r->repl_lock);
}


/*
 * Return the CSN of the purge point. Any CSNs smaller than the
 * purge point can be safely removed from entries within this
 * this replica. Returns an allocated CSN that must be freed by
 * the caller, or NULL if purging is disabled.
 */

CSN *
replica_get_purge_csn(const Replica *r)
{
    CSN *csn;

    PR_Lock(r->repl_lock);

    csn= _replica_get_purge_csn_nolock(r);

    PR_Unlock(r->repl_lock);

    return csn;
}


/*  
 * This function logs a dummy entry for the smallest csn in the RUV. 
 * This is necessary because, to get the next change, we need to position 
 * changelog on the previous change. So this function insures that we always have one.
 */

/* ONREPL we will need to change this function to log all the
 * ruv elements not just the smallest when changelog iteration 
 * algoritm changes to iterate replica by replica 
*/
int 
replica_log_ruv_elements (const Replica *r)
{
    int rc = 0;
    
    PR_ASSERT (r);

    PR_Lock(r->repl_lock);

    rc = replica_log_ruv_elements_nolock (r);

    PR_Unlock(r->repl_lock);

    return rc;
}

void
consumer5_set_mapping_tree_state_for_replica(const Replica *r, RUV *supplierRuv)
{
	const Slapi_DN *repl_root_sdn= replica_get_root(r);
	char **ruv_referrals= NULL;
	char **replica_referrals= NULL;
    RUV *ruv;
	int state_backend = -1;
	const char *mtn_state = NULL;
	
    PR_Lock (r->repl_lock);

	if ( supplierRuv == NULL )
	{
		ruv = (RUV*)object_get_data (r->repl_ruv);
		PR_ASSERT (ruv);

		ruv_referrals= ruv_get_referrals(ruv); /* ruv_referrals has to be free'd */
	}
	else
	{
		ruv_referrals = ruv_get_referrals(supplierRuv);
	}

	replica_get_referrals_nolock (r, &replica_referrals); /* replica_referrals has to be free'd */
	
    /* JCMREPL - What if there's a Total update in progress? */
	if( (r->repl_type==REPLICA_TYPE_READONLY) || (r->legacy_consumer) )
	{
		state_backend = 0;
	}
	else if (r->repl_type==REPLICA_TYPE_UPDATABLE)
	{
		state_backend = 1;
	}
	/* Unlock to avoid changing MTN state under repl lock */
    PR_Unlock (r->repl_lock);

	if(state_backend == 0 )
	{
		/* Read-Only - The mapping tree should be refering all update operations. */
		mtn_state = STATE_UPDATE_REFERRAL;
	}
	else if (state_backend == 1)
	{
		/* Updatable - The mapping tree should be accepting all update operations. */
		mtn_state = STATE_BACKEND;
	}

	/* JCMREPL - Check the return code. */
	repl_set_mtn_state_and_referrals(repl_root_sdn, mtn_state, NULL,
									 ruv_referrals, replica_referrals);
	charray_free(ruv_referrals);
	charray_free(replica_referrals);
}

void 
replica_set_enabled (Replica *r, PRBool enable)
{
	char *repl_name = NULL;

    PR_ASSERT (r);

    PR_Lock (r->repl_lock);

    if (enable)
    {
        if (r->repl_eqcxt_rs == NULL)   /* event is not already registered */
        {
            repl_name = slapi_ch_strdup (r->repl_name);
            r->repl_eqcxt_rs = slapi_eq_repeat(replica_update_state, repl_name,
                                               current_time() + START_UPDATE_DELAY, RUV_SAVE_INTERVAL);  
        }
    }
    else    /* disable */
    {
        if (r->repl_eqcxt_rs)   /* event is still registerd */
        {
			repl_name = slapi_eq_get_arg (r->repl_eqcxt_rs);
			slapi_ch_free ((void**)&repl_name);
            slapi_eq_cancel(r->repl_eqcxt_rs);
            r->repl_eqcxt_rs = NULL;
        }   
    }

    PR_Unlock (r->repl_lock);
}

/* This function is generally called when replica's data store
   is reloaded. It retrieves new RUV from the datastore. If new
   RUV does not exist or if it is not as up to date as the purge RUV 
   of the corresponding changelog file, we need to remove */

/* the function minimizes the use of replica lock where ever possible. 
   Locking replica lock while calling changelog functions
   causes a deadlock because changelog calls replica functions that
   that lock the same lock */

int 
replica_reload_ruv (Replica *r)
{
    int rc = 0;
    Object *old_ruv_obj = NULL, *new_ruv_obj = NULL;
    RUV *upper_bound_ruv = NULL;
    RUV *new_ruv = NULL;
    Object *r_obj;

    PR_ASSERT (r);

    PR_Lock (r->repl_lock);

    old_ruv_obj = r->repl_ruv;

    r->repl_ruv = NULL;

    rc = _replica_configure_ruv (r, PR_TRUE);

    PR_Unlock (r->repl_lock);

    if (rc != 0)
    {
        return rc;
    }

    /* check if there is a changelog and whether this replica logs changes */
    if (cl5GetState () == CL5_STATE_OPEN && (r->repl_flags & REPLICA_LOG_CHANGES))
    {

        /* Compare new ruv to the changelog's upper bound ruv. We could only keep
           the existing changelog if its upper bound is the same as replica's RUV.
           This is because if changelog has changes not in RUV, they will be
           eventually sent to the consumer's which will cause a state mismatch 
           (because the supplier does not actually contain the changes in its data store.
           If, on the other hand, the changelog is not as up to date as the supplier,
           it is not really useful since out of sync consumer's can't be brought
           up to date using this changelog and hence will need to be reinitialized */

        /* replace ruv to make sure we work with the correct changelog file */
        PR_Lock (r->repl_lock);

        new_ruv_obj = r->repl_ruv;
        r->repl_ruv = old_ruv_obj;

        PR_Unlock (r->repl_lock);

        rc = cl5GetUpperBoundRUV (r, &upper_bound_ruv);
        if (rc != CL5_SUCCESS && rc != CL5_NOTFOUND)
        {
            return -1;
        }

        if (upper_bound_ruv)
        {            
            new_ruv = object_get_data (new_ruv_obj);
            PR_ASSERT (new_ruv);

            /* ONREPL - there are more efficient ways to establish RUV equality.
               However, because this is not in the critical path and we at most
               have 2 elements in the RUV, this will not effect performance */

            if (!ruv_covers_ruv (new_ruv, upper_bound_ruv) ||
                !ruv_covers_ruv (upper_bound_ruv, new_ruv))
            {                
                /* create a temporary replica object to conform to the interface */
                r_obj = object_new (r, NULL);

                /* We can't use existing changelog - remove existing file */
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_reload_ruv: "
			        "Warning: new data for replica %s does not match the data in the changelog.\n"
                    " Recreating the changelog file. This could affect replication with replica's "
                    " consumers in which case the consumers should be reinitialized.\n",
                    slapi_sdn_get_dn(r->repl_root));
                rc = cl5DeleteDBSync (r_obj);

                /* reinstate new ruv */
                PR_Lock (r->repl_lock);

                r->repl_ruv = new_ruv_obj;                

                object_release (r_obj);

                if (rc == CL5_SUCCESS)
                {
                    /* log changes to mark starting point for replication */
                    rc = replica_log_ruv_elements_nolock (r);
                }

                PR_Unlock (r->repl_lock);
            } 
            else
            {
                /* we just need to reinstate new ruv */
                PR_Lock (r->repl_lock);

                r->repl_ruv = new_ruv_obj;

                PR_Unlock (r->repl_lock);
            }                           
        }
        else    /* upper bound vector is not there - we have no changes logged */
        {
            /* reinstate new ruv */
            PR_Lock (r->repl_lock);

            r->repl_ruv = new_ruv_obj;

            /* just log elements of the current RUV. This is to have 
               a starting point for iteration through the changes */
            rc = replica_log_ruv_elements_nolock (r);

            PR_Unlock (r->repl_lock);                    
        }        
    }

    if (rc == 0)
    {
        consumer5_set_mapping_tree_state_for_replica(r, NULL);
        /* reset mapping tree referrals based on new local RUV */
    }

    if (old_ruv_obj)
        object_release (old_ruv_obj);

    if (upper_bound_ruv)
        ruv_destroy (&upper_bound_ruv);
        
    return rc;
}

/* this function is called during server startup for each replica
   to check whether the replica's data was reloaded offline and
   whether replica's changelog needs to be reinitialized */

/* the function does not use replica lock but all functions it calls are
   thread safe. Locking replica lock while calling changelog functions
   causes a deadlock because changelog calls replica functions that
   that lock the same lock */
int replica_check_for_data_reload (Replica *r, void *arg)
{
    int rc = 0;
    RUV *upper_bound_ruv = NULL;
    RUV *r_ruv = NULL;
    Object *r_obj, *ruv_obj;

    PR_ASSERT (r);

    /* check that we have a changelog and if this replica logs changes */
    if (cl5GetState () == CL5_STATE_OPEN && (r->repl_flags & REPLICA_LOG_CHANGES))
    {
        /* Compare new ruv to the purge ruv. If the new contains csns which
           are smaller than those in purge ruv, we need to remove old and
           create new changelog file for this replica. This is because we
           will not have sufficient changes to incrementally update a consumer
           to the current state of the supplier. */    

        rc = cl5GetUpperBoundRUV (r, &upper_bound_ruv);
        if (rc != CL5_SUCCESS && rc != CL5_NOTFOUND)
        {
            return -1;
        }

        if (upper_bound_ruv)
        {
            ruv_obj = replica_get_ruv (r);
            r_ruv = object_get_data (ruv_obj);
            PR_ASSERT (r_ruv);

            /* Compare new ruv to the changelog's upper bound ruv. We could only keep
               the existing changelog if its upper bound is the same as replica's RUV.
               This is because if changelog has changes not in RUV, they will be
               eventually sent to the consumer's which will cause a state mismatch 
               (because the supplier does not actually contain the changes in its data store.
               If, on the other hand, the changelog is not as up to date as the supplier,
               it is not really useful since out of sync consumer's can't be brought
               up to date using this changelog and hence will need to be reinitialized */

			/*
			 * Actually we can ignore the scenario that the changelog's upper
			 * bound ruv covers data store's ruv for two reasons: (1) a change
			 * is always written to the changelog after it is committed to the
			 * data store;  (2) a change will be ignored if the server has seen
			 * it before - this happens frequently at the beginning of replication
			 * sessions.
			 */

            rc = ruv_compare_ruv(upper_bound_ruv, "changelog max RUV", r_ruv, "database RUV", 0, SLAPI_LOG_FATAL);
            if (RUV_COMP_IS_FATAL(rc))
            {
                /* create a temporary replica object to conform to the interface */
                r_obj = object_new (r, NULL);

                /* We can't use existing changelog - remove existing file */
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_check_for_data_reload: "
                    "Warning: data for replica %s does not match the data in the changelog. "
                    "Recreating the changelog file. "
                    "This could affect replication with replica's consumers in which case the "
                    "consumers should be reinitialized.\n",
                    slapi_sdn_get_dn(r->repl_root));

                rc = cl5DeleteDBSync (r_obj);

                object_release (r_obj);

                if (rc == CL5_SUCCESS)
                {
                    /* log changes to mark starting point for replication */
                    rc = replica_log_ruv_elements (r);
                }
            }
            else if (rc)
            {
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_check_for_data_reload: "
                    "Warning: for replica %s there were some differences between the changelog max RUV and the "
                    "database RUV.  If there are obsolete elements in the database RUV, you "
                    "should remove them using the CLEANALLRUV task.  If they are not obsolete, "
                    "you should check their status to see why there are no changes from those "
                    "servers in the changelog.\n",
                    slapi_sdn_get_dn(r->repl_root));
                rc = 0;
            }

            object_release (ruv_obj);
        }
        else    /* we have no changes currently logged for this replica */
        {
            /* log changes to mark starting point for replication */
            rc = replica_log_ruv_elements (r);
        }
    }

    if (rc == 0)
    {
         /* reset mapping tree referrals based on new local RUV */
        consumer5_set_mapping_tree_state_for_replica(r, NULL);
    }

    if (upper_bound_ruv)
        ruv_destroy (&upper_bound_ruv);
        
    return rc;
}

/* Helper functions */
/* reads replica configuration entry. The entry is the child of the 
   mapping tree node for the replica's backend */

static Slapi_Entry* 
_replica_get_config_entry (const Slapi_DN *root, char **attrs)
{
	int rc = 0;
	char *dn = NULL;
	Slapi_Entry **entries;
	Slapi_Entry *e = NULL;
	Slapi_PBlock *pb = NULL;

	dn = _replica_get_config_dn (root);
	if (NULL == dn) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"_replica_get_config_entry: failed to get the config dn for %s\n",
			slapi_sdn_get_dn (root));
		return NULL;
	}
	pb = slapi_pblock_new ();

	slapi_search_internal_set_pb (pb, dn, LDAP_SCOPE_BASE, "objectclass=*", attrs, 0, NULL,
								  NULL, repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
	slapi_search_internal_pb (pb);	
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if (rc == 0)
	{
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
		e = slapi_entry_dup (entries [0]);		
	}

	slapi_free_search_results_internal(pb);
	slapi_pblock_destroy (pb);
	slapi_ch_free_string(&dn);
		
	return e; 
}

/* It does an internal search to read the in memory RUV
 * of the provided suffix
  */
Slapi_Entry *
get_in_memory_ruv(Slapi_DN *suffix_sdn)
{
        char *attrs[3];
        
        /* these two attributes needs to be asked when reading the RUV */
        attrs[0] = type_ruvElement;
        attrs[1] = type_ruvElementUpdatetime;
        attrs[2] = NULL;
        return(_replica_get_config_entry(suffix_sdn, attrs));
}

char *
replica_get_dn(Replica *r)
{
    return _replica_get_config_dn (r->repl_root);
}

static int 
_replica_check_validity (const Replica *r)
{
    PR_ASSERT (r);

    if (r->repl_root == NULL || r->repl_type == 0 || r->repl_rid == 0 ||
        r->repl_csngen == NULL || r->repl_name == NULL)
	{
        return -1;    
	}
    else
	{
        return 0;
	}
}

/* replica configuration entry has the following format:
	dn: cn=replica,<mapping tree node dn>
    objectclass:    top
    objectclass:    nsds5Replica
    objectclass:    extensibleObject
	nsds5ReplicaRoot:	<root of the replica>
	nsds5ReplicaId:	<replica id>
	nsds5ReplicaType:	<type of the replica: primary, read-write or read-only>
	nsState:		<state of the csn generator> missing the first time replica is started
	nsds5ReplicaBindDN:		<supplier update dn> consumers only
	nsds5ReplicaReferral: <referral URL to updatable replica> consumers only
	nsds5ReplicaPurgeDelay: <time, in seconds, to keep purgeable CSNs, 0 == keep forever>
	nsds5ReplicaTombstonePurgeInterval: <time, in seconds, between tombstone purge runs, 0 == don't reap>
	nsds5ReplicaLegacyConsumer: <TRUE | FALSE>

	richm: changed slapi entry from const to editable - if the replica id is supplied for a read
	only replica, we ignore it and replace the value with the READ_ONLY_REPLICA_ID
 */
static int 
_replica_init_from_config (Replica *r, Slapi_Entry *e, char *errortext)
{
    Slapi_Attr *a = NULL;
    Slapi_Attr *attr;
    CSNGen *gen;
    char buf [SLAPI_DSE_RETURNTEXT_SIZE];
    char *errormsg = errortext? errortext : buf;
    char *val;
    int backoff_min;
    int backoff_max;
    int rc;

    PR_ASSERT (r && e);

    /* get replica root */
    val = slapi_entry_attr_get_charptr (e, attr_replicaRoot);
    if (val == NULL){
        PR_snprintf (errormsg, SLAPI_DSE_RETURNTEXT_SIZE, "failed to retrieve %s attribute from (%s)\n", 
                 attr_replicaRoot,
				 (char*)slapi_entry_get_dn ((Slapi_Entry*)e));
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "_replica_init_from_config: %s\n",
                        errormsg);
        return -1;	
    }
    
    r->repl_root = slapi_sdn_new_dn_passin (val);

    /* get replica type */
    val = slapi_entry_attr_get_charptr (e, attr_replicaType);
    if (val){
	    r->repl_type = atoi(val);
        slapi_ch_free ((void**)&val);
    } else {
        r->repl_type = REPLICA_TYPE_READONLY;
    }

    /* get legacy consumer flag */
    val = slapi_entry_attr_get_charptr (e, type_replicaLegacyConsumer);
    if (val){
        if (strcasecmp (val, "on") == 0 || strcasecmp (val, "yes") == 0 ||
            strcasecmp (val, "true") == 0 || strcasecmp (val, "1") == 0)
        {
	        r->legacy_consumer = PR_TRUE;
        } else {
            r->legacy_consumer = PR_FALSE;
        }
        slapi_ch_free ((void**)&val);
    } else {
        r->legacy_consumer = PR_FALSE;
    }

    /* grab and validate the backoff retry settings */
    backoff_min = slapi_entry_attr_get_int(e, type_replicaBackoffMin);
    if(backoff_min <= 0){
        if (backoff_min != 0){
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Invalid value for %s: %d  Using default value (%d)\n",
                type_replicaBackoffMin, backoff_min, PROTOCOL_BACKOFF_MINIMUM );
        }
        backoff_min = PROTOCOL_BACKOFF_MINIMUM;
    }

    backoff_max = slapi_entry_attr_get_int(e, type_replicaBackoffMax);
    if(backoff_max <= 0){
        if(backoff_max != 0) {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Invalid value for %s: %d  Using default value (%d)\n",
                type_replicaBackoffMax, backoff_max, PROTOCOL_BACKOFF_MAXIMUM );
        }
        backoff_max = PROTOCOL_BACKOFF_MAXIMUM;
    }
    if(backoff_min > backoff_max){
        /* Ok these values are invalid, reset back the defaults */
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Backoff minimum (%d) can not be greater than "
            "the backoff maximum (%d).  Using default values: min (%d) max (%d)\n", backoff_min, backoff_max,
            PROTOCOL_BACKOFF_MINIMUM, PROTOCOL_BACKOFF_MAXIMUM);
        r->backoff_min = PROTOCOL_BACKOFF_MINIMUM;
        r->backoff_max = PROTOCOL_BACKOFF_MAXIMUM;
    } else {
        r->backoff_min = backoff_min;
        r->backoff_max = backoff_max;
    }

    /* get the protocol timeout */
    r->protocol_timeout = slapi_entry_attr_get_int(e, type_replicaProtocolTimeout);
    if(r->protocol_timeout == 0){
        r->protocol_timeout = DEFAULT_PROTOCOL_TIMEOUT;
    }

    /* get replica flags */
    r->repl_flags = slapi_entry_attr_get_ulong(e, attr_flags);

    /*
     * Get replicaid
     * The replica id is ignored for read only replicas and is set to the
     * special value READ_ONLY_REPLICA_ID
     */
    if (r->repl_type == REPLICA_TYPE_READONLY){
        r->repl_rid = READ_ONLY_REPLICA_ID;
        slapi_entry_attr_set_uint(e, attr_replicaId, (unsigned int)READ_ONLY_REPLICA_ID);
    }
    /* a replica id is required for updatable and primary replicas */
    else if (r->repl_type == REPLICA_TYPE_UPDATABLE ||
             r->repl_type == REPLICA_TYPE_PRIMARY)
    {
	    if ((val = slapi_entry_attr_get_charptr (e, attr_replicaId))){
            int temprid = atoi (val);
            slapi_ch_free ((void**)&val);
            if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID){
                PR_snprintf (errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "attribute %s must have a value greater than 0 "
                    "and less than %d: entry %s",
                    attr_replicaId, READ_ONLY_REPLICA_ID,
                    (char*)slapi_entry_get_dn ((Slapi_Entry*)e));
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "_replica_init_from_config: %s\n", errormsg);
                return -1;
            } else {
                r->repl_rid = (ReplicaId)temprid;
            }
        } else {
            PR_snprintf (errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                "failed to retrieve required %s attribute from %s",
                attr_replicaId,(char*)slapi_entry_get_dn ((Slapi_Entry*)e));
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                "_replica_init_from_config: %s\n", errormsg);
            return -1;
        }
    }

    attr = NULL;
    rc = slapi_entry_attr_find(e, attr_state, &attr);
    gen = csngen_new (r->repl_rid, attr);
    if (gen == NULL){
        PR_snprintf (errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
            "failed to create csn generator for replica (%s)",
            (char*)slapi_entry_get_dn ((Slapi_Entry*)e));
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
            "_replica_init_from_config: %s\n", errormsg);
        return -1;
    }
    r->repl_csngen = object_new((void*)gen, (FNFree)csngen_free);

    /* Hook generator so we can maintain min/max CSN info */
    r->csn_pl_reg_id = csngen_register_callbacks(gen, assign_csn_callback, r, abort_csn_callback, r);

    /* get replication bind dn */
    r->updatedn_list = replica_updatedn_list_new(e);

    /* get replica name */
    val = slapi_entry_attr_get_charptr (e, attr_replicaName);
    if (val) {
        r->repl_name = val;
    } else {
        rc = slapi_uniqueIDGenerateString (&r->repl_name);
        if (rc != UID_SUCCESS){
            PR_snprintf (errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                "failed to assign replica name for replica (%s); uuid generator error - %d ",
                (char*)slapi_entry_get_dn ((Slapi_Entry*)e), rc);
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "_replica_init_from_config: %s\n",
                errormsg);
		    return -1;
        } else
            r->new_name = PR_TRUE;          
    }

    /* get the list of referrals */
    slapi_entry_attr_find( e, attr_replicaReferral, &attr );
    if(attr!=NULL){
        slapi_attr_get_valueset(attr, &r->repl_referral);
    }

    /*
     * Set the purge offset (default 7 days). This is the extra
     * time we allow purgeable CSNs to stick around, in case a
     * replica regresses. Could also be useful when LCUP happens,
     * since we don't know about LCUP replicas, and they can just
     * turn up whenever they want to.
     */
    if (slapi_entry_attr_find(e, type_replicaPurgeDelay, &a) == -1){
        /* No purge delay provided, so use default */
        r->repl_purge_delay = 60 * 60 * 24 * 7; /* One week, in seconds */
    } else {
        r->repl_purge_delay = slapi_entry_attr_get_uint(e, type_replicaPurgeDelay);
    }

    if (slapi_entry_attr_find(e, type_replicaTombstonePurgeInterval, &a) == -1){
        /* No reap interval provided, so use default */
        r->tombstone_reap_interval = 3600 * 24; /* One day */
    } else {
        r->tombstone_reap_interval = slapi_entry_attr_get_int(e, type_replicaTombstonePurgeInterval);
    }

    r->tombstone_reap_stop = r->tombstone_reap_active = PR_FALSE;

    return (_replica_check_validity (r));
}

void
replica_check_for_tasks(Replica *r, Slapi_Entry *e)
{
    char **clean_vals;

    if(e == NULL || ldif_dump_is_running() == PR_TRUE){
        /* If db2ldif is being run, do not check if there are incomplete tasks */
        return;
    }
    /*
     *  check if we are in the middle of a CLEANALLRUV task,
     *  if so set the cleaned rid, and fire off the thread
     */
    if ((clean_vals = slapi_entry_attr_get_charray(e, type_replicaCleanRUV)) != NULL)
    {
        PRThread *thread = NULL;
        struct berval *payload = NULL;
        CSN *maxcsn = NULL;
        ReplicaId rid;
        char csnstr[CSN_STRSIZE];
        char *token = NULL;
        char *forcing;
        char *csnpart;
        char *ridstr;
        char *iter;
        int i;

        for(i = 0; i < CLEANRIDSIZ && clean_vals[i]; i++){
            cleanruv_data *data = NULL;

            /*
             *  Set the cleanruv data, and add the cleaned rid
             */
            token = ldap_utf8strtok_r(clean_vals[i], ":", &iter);
            if(token){
                rid = atoi(token);
                if(rid <= 0 || rid >= READ_ONLY_REPLICA_ID){
                    slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "CleanAllRUV Task: invalid replica id(%d) "
                       "aborting task.\n", rid);
                    goto done;
                }
            } else {
                slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "CleanAllRUV Task: unable to parse cleanallruv "
                    "data (%s), aborting task.\n",clean_vals[i]);
                goto done;
            }
            csnpart = ldap_utf8strtok_r(iter, ":", &iter);
            maxcsn = csn_new();
            csn_init_by_string(maxcsn, csnpart);
            csn_as_string(maxcsn, PR_FALSE, csnstr);
            forcing = ldap_utf8strtok_r(iter, ":", &iter);
            if(forcing == NULL){
                forcing = "no";
            }

            slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "CleanAllRUV Task: cleanAllRUV task found, "
                "resuming the cleaning of rid(%d)...\n", rid);
            /*
             *  Create payload
             */
            ridstr = slapi_ch_smprintf("%d:%s:%s:%s", rid, slapi_sdn_get_dn(replica_get_root(r)), csnstr, forcing);
            payload = create_cleanruv_payload(ridstr);
            slapi_ch_free_string(&ridstr);

            if(payload == NULL){
                slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "CleanAllRUV Task: Startup: Failed to "
                    "create extended op payload, aborting task");
                csn_free(&maxcsn);
                goto done;
            }
            /*
             *  Setup the data struct, and fire off the thread.
             */
            data = (cleanruv_data*)slapi_ch_calloc(1, sizeof(cleanruv_data));
            if (data == NULL) {
                slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV: failed to allocate cleanruv_data.\n");
                csn_free(&maxcsn);
            } else {
                /* setup our data */
                data->repl_obj = NULL;
                data->replica = NULL;
                data->rid = rid;
                data->task = NULL;
                data->maxcsn = maxcsn;
                data->payload = payload;
                data->sdn = slapi_sdn_dup(r->repl_root);
                data->force = slapi_ch_strdup(forcing);
                data->repl_root = NULL;

                thread = PR_CreateThread(PR_USER_THREAD, replica_cleanallruv_thread_ext,
                        (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                        PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
                if (thread == NULL) {
                    /* log an error and free everything */
                    slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV: unable to create cleanAllRUV "
                        "thread for rid(%d)\n", (int)data->rid);
                    csn_free(&maxcsn);
                    slapi_sdn_free(&data->sdn);
                    ber_bvfree(data->payload);
                    slapi_ch_free_string(&data->force);
                    slapi_ch_free((void **)&data);
                }
            }
        }

done:
        slapi_ch_array_free(clean_vals);
    }

    if ((clean_vals = slapi_entry_attr_get_charray(e, type_replicaAbortCleanRUV)) != NULL)
    {
        PRThread *thread = NULL;
        struct berval *payload;
        ReplicaId rid;
        char *certify = NULL;
        char *ridstr = NULL;
        char *token = NULL;
        char *repl_root;
        char *iter;
        int i;

        for(i = 0; clean_vals[i]; i++){
            cleanruv_data *data = NULL;

            token = ldap_utf8strtok_r(clean_vals[i], ":", &iter);
            if(token){
                rid = atoi(token);
                if(rid <= 0 || rid >= READ_ONLY_REPLICA_ID){
                    slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: invalid replica id(%d) "
                        "aborting abort task.\n", rid);
                    goto done2;
                }
            } else {
                slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: unable to parse cleanallruv "
                    "data (%s), aborting abort task.\n",clean_vals[i]);
                goto done2;
            }

            repl_root = ldap_utf8strtok_r(iter, ":", &iter);
            certify = ldap_utf8strtok_r(iter, ":", &iter);

            if(!is_cleaned_rid(rid)){
                slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: replica id(%d) is not "
                    "being cleaned, nothing to abort.  Aborting abort task.\n", rid);
                delete_aborted_rid(r, rid, repl_root, 0);
                goto done2;
            }

            add_aborted_rid(rid, r, repl_root);
            stop_ruv_cleaning();

            slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: abort task found, "
                "resuming abort of rid(%d).\n", rid);
            /*
             *  Setup the data struct, and fire off the abort thread.
             */
            data = (cleanruv_data*)slapi_ch_calloc(1, sizeof(cleanruv_data));
            if (data == NULL) {
                slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: failed to allocate cleanruv_data.\n");
            } else {
                ridstr = slapi_ch_smprintf("%d:%s:%s", rid, repl_root, certify);
                payload = create_cleanruv_payload(ridstr);
                slapi_ch_free_string(&ridstr);

                if(payload == NULL){
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: failed to create extended "
                        "op payload\n");
                    slapi_ch_free((void **)&data);
                } else {
                    /* setup the data */
                    data->repl_obj = NULL;
                    data->replica = NULL;
                    data->rid = rid;
                    data->task = NULL;
                    data->payload = payload;
                    data->repl_root = slapi_ch_strdup(repl_root);
                    data->sdn = slapi_sdn_dup(r->repl_root);
                    data->certify = slapi_ch_strdup(certify);

                    thread = PR_CreateThread(PR_USER_THREAD, replica_abort_task_thread,
                            (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                            PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
                    if (thread == NULL) {
                        slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: unable to create abort cleanAllRUV "
                            "thread for rid(%d)\n", (int)data->rid);
                        slapi_sdn_free(&data->sdn);
                        ber_bvfree(data->payload);
                        slapi_ch_free_string(&data->repl_root);
                        slapi_ch_free_string(&data->certify);
                        slapi_ch_free((void **)&data);
                    }
                }
            }
        }

done2:
        slapi_ch_array_free(clean_vals);
    }
}

/* This function updates the entry to contain information generated 
   during replica initialization.
   Returns 0 if successful and -1 otherwise */
static int 
_replica_update_entry (Replica *r, Slapi_Entry *e, char *errortext)
{
    int rc;
    Slapi_Mod smod;
    Slapi_Value *val;

    PR_ASSERT (r);

    /* add attribute that stores state of csn generator */
    rc = csngen_get_state ((CSNGen*)object_get_data (r->repl_csngen), &smod);
	if (rc != CSN_SUCCESS)
    {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to get csn generator's state; csn error - %d", rc);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"_replica_update_entry: %s\n", errortext);
		return -1;    
    }

    val = slapi_value_new_berval(slapi_mod_get_first_value(&smod));

    rc = slapi_entry_add_value (e, slapi_mod_get_type (&smod), val);

    slapi_value_free(&val);
    slapi_mod_done (&smod);

    if (rc != 0)
    {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to update replica entry");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"_replica_update_entry: %s\n", errortext);
		return -1;    
    }

    /* add attribute that stores replica name */
    rc = slapi_entry_add_string (e, attr_replicaName, r->repl_name);
    if (rc != 0)
    {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to update replica entry");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"_replica_update_entry: %s\n", errortext);
		return -1;    
    }
    else
        r->new_name = PR_FALSE;
    
    return 0;
}

/* DN format: cn=replica,cn=\"<root>\",cn=mapping tree,cn=config */
static char* 
_replica_get_config_dn (const Slapi_DN *root)
{
    char *dn;
    /* "cn=mapping tree,cn=config" */
    const char *mp_base = slapi_get_mapping_tree_config_root ();

    PR_ASSERT (root);

    /* This function converts the old style DN to the new style. */
    dn = slapi_ch_smprintf("%s,cn=\"%s\",%s", 
                           REPLICA_RDN, slapi_sdn_get_dn (root), mp_base);
    return dn;
}

/* This function retrieves RUV from the root of the replicated tree.
 * The attribute can be missing if
 * (1) this replica is the first supplier and replica generation has not been assigned
 * or
 * (2) this is a consumer that has not been yet initialized
 * In either case, replica_set_ruv should be used to further initialize the replica.
 * Returns 0 on success, -1 on failure. If 0 is returned, the RUV is present in the replica.
 */
static int
_replica_configure_ruv  (Replica *r, PRBool isLocked)
{
	Slapi_PBlock *pb = NULL;
	char *attrs[2];
	int rc;
	int return_value = -1;
	Slapi_Entry **entries = NULL;
	Slapi_Attr *attr;
	RUV *ruv = NULL;
    CSN *csn = NULL;
	ReplicaId rid = 0;
	
	/* read ruv state from the ruv tombstone entry */
	pb = slapi_pblock_new();
	if (!pb) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			"_replica_configure_ruv: Out of memory\n");
		goto done;
	}

	attrs[0] = (char*)type_ruvElement;
	attrs[1] = NULL;
	slapi_search_internal_set_pb(
		pb,
		slapi_sdn_get_dn(r->repl_root),
		LDAP_SCOPE_BASE, 
		"objectclass=*",
		attrs,
		0, /* attrsonly */
		NULL, /* controls */
		RUV_STORAGE_ENTRY_UNIQUEID, 
		repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION),
		OP_FLAG_REPLICATED);  /* flags */
	slapi_search_internal_pb (pb);

	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc == LDAP_SUCCESS)
    {
        /* get RUV attributes and construct the RUV */
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	    if (NULL == entries || NULL == entries[0])
	    {
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			    "_replica_configure_ruv: replica ruv tombstone entry for "
			    "replica %s not found\n", 
				slapi_sdn_get_dn(r->repl_root));
		    goto done;
	    }
	
	    rc = slapi_entry_attr_find(entries[0], type_ruvElement, &attr);
	    if (rc != 0) /* ruv attribute is missing - this not allowed */
	    {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			    "_replica_configure_ruv: replica ruv tombstone entry for "
			    "replica %s does not contain %s\n", 
				slapi_sdn_get_dn(r->repl_root), type_ruvElement);
		    goto done;
        }

		/* Check in the tombstone we have retrieved if the local purl is
			already present:
				rid == 0: the local purl is not present
				rid != 0: the local purl is present ==> nothing to do
		 */
        ruv_init_from_slapi_attr_and_check_purl (attr, &ruv, &rid);
        if (ruv)
	    {
		    char *generation = NULL;
		    generation = ruv_get_replica_generation(ruv);
		    if (NULL != generation)
		    {
                r->repl_ruv = object_new((void*)ruv, (FNFree)ruv_destroy);

				/* Is the local purl in the ruv? (the port or the host could have
				   changed)
				 */
				/* A consumer only doesn't have its purl in its ruv  */
				if (r->repl_type == REPLICA_TYPE_UPDATABLE)
				{
					int need_update = 0;
#define RUV_UPDATE_PARTIAL 1
#define RUV_UPDATE_FULL    2
					if (rid == 0)
					{
						/* We can not have more than 1 ruv with the same rid
						   so we replace it */
						const char *purl = NULL;

						purl = multimaster_get_local_purl();
						ruv_delete_replica(ruv, r->repl_rid);
						ruv_add_index_replica(ruv, r->repl_rid, purl, 1);
						need_update = RUV_UPDATE_PARTIAL; /* ruv changed, so write tombstone */
					}
					else /* bug 540844: make sure the local supplier rid is first in the ruv */
					{
						/* make sure local supplier is first in list */
						ReplicaId first_rid = 0;
						char *first_purl = NULL;
						ruv_get_first_id_and_purl(ruv, &first_rid, &first_purl);
						/* if the local supplier is not first in the list . . . */
						/* rid is from changelog; first_rid is from backend */
						if (rid != first_rid)
						{
							/* . . . move the local supplier to the beginning of the list */
							ruv_move_local_supplier_to_first(ruv, rid);
							need_update = RUV_UPDATE_PARTIAL; /* must update tombstone also */
						}
						/* r->repl_rid is from config; rid is from changelog */
						if (r->repl_rid != rid)
						{
						    /* Most likely, the replica was once deleted
						     * and recreated with a different rid from the
						     * previous. */
						    /* must recreate ruv tombstone */
						    need_update = RUV_UPDATE_FULL;
						    if(NULL != r->repl_ruv) 
						    {
						        object_release(r->repl_ruv);
						        r->repl_ruv = NULL;
						    }
						}
					}

					/* Update also the directory entry */
					if (RUV_UPDATE_PARTIAL == need_update) {
						/* richm 20010821 bug 556498
						   replica_replace_ruv_tombstone acquires the repl_lock, so release
						   the lock then reacquire it if locked */
						if (isLocked) PR_Unlock(r->repl_lock);
						replica_replace_ruv_tombstone(r);
						if (isLocked) PR_Lock(r->repl_lock);
					} else if (RUV_UPDATE_FULL == need_update) {
						_delete_tombstone(slapi_sdn_get_dn(r->repl_root), 
						                  RUV_STORAGE_ENTRY_UNIQUEID, 
						                  OP_FLAG_REPL_RUV);
						rc = replica_create_ruv_tombstone(r);
						if (rc) {
							slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
								"_replica_configure_ruv: "
								"failed to recreate replica ruv tombstone entry"
								" (%s); LDAP error - %d\n",
								slapi_sdn_get_dn(r->repl_root), rc);
							goto done;
						}
					}
#undef RUV_UPDATE_PARTIAL
#undef RUV_UPDATE_FULL
				}

			    slapi_ch_free((void **)&generation);
			    return_value = 0;
		    }
            else
            {
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
								"RUV for replica %s is missing replica generation\n",
								slapi_sdn_get_dn(r->repl_root));
                goto done;
            }		
	    }
	    else
	    {
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"Unable to convert %s attribute in entry %s to a replica update vector.\n",
							type_ruvElement, slapi_sdn_get_dn(r->repl_root));
            goto done;
	    }
	
    }
    else /* search failed */
	{
		if (LDAP_NO_SUCH_OBJECT == rc)
		{
			/* The entry doesn't exist: create it */
			rc = replica_create_ruv_tombstone(r);
			if (LDAP_SUCCESS != rc)
			{
				/*
				 * XXXggood - the following error appears on startup if we try
				 * to initialize replica RUVs before the backend instance is up.
				 * It's alarming to see this error, and we should suppress it
				 * (or avoid trying to configure it) if the backend instance is
				 * not yet online.
				 */
				/*
				 * XXXrichm - you can also get this error when the backend is in
				 * read only mode c.f. bug 539782
				 */
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
					"_replica_configure_ruv: failed to create replica ruv tombstone "
					"entry (%s); LDAP error - %d\n",
					slapi_sdn_get_dn(r->repl_root), rc);
				goto done;
			}
            else
            {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"_replica_configure_ruv: No ruv tombstone found for replica %s. "
					"Created a new one\n",
					slapi_sdn_get_dn(r->repl_root));
                return_value = 0;
            }
		}
		else
		{
			/* see if the suffix is disabled */
			char *state = slapi_mtn_get_state(r->repl_root);
			if (state && !strcasecmp(state, "disabled"))
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
								"_replica_configure_ruv: replication disabled for "
								"entry (%s); LDAP error - %d\n",
								slapi_sdn_get_dn(r->repl_root), rc);
				slapi_ch_free_string(&state);
				goto done;
			}
			else if (!r->repl_ruv) /* other error */
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
								"_replica_configure_ruv: replication broken for "
								"entry (%s); LDAP error - %d\n",
								slapi_sdn_get_dn(r->repl_root), rc);
				slapi_ch_free_string(&state);
				goto done;
			}
			else /* some error but continue anyway? */
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"_replica_configure_ruv: Error %d reading tombstone for replica %s.\n",
								rc, slapi_sdn_get_dn(r->repl_root));
                return_value = 0;
			}
			slapi_ch_free_string(&state);
		}
    }

    if (NULL != r->min_csn_pl)
	{
        csnplFree (&r->min_csn_pl);
	}

    /* create pending list for min csn if necessary */
    if (ruv_get_smallest_csn_for_replica ((RUV*)object_get_data (r->repl_ruv), 
                                           r->repl_rid, &csn) == RUV_SUCCESS)
	{
        csn_free (&csn);
		r->min_csn_pl = NULL;
	}
	else
	{
		/*
		 * The local replica has not generated any of its own CSNs yet.
		 * We need to watch CSNs being generated and note the first
		 * locally-generated CSN that's committed. Once that event occurs,
		 * the RUV is suitable for iteration over locally generated
		 * changes.
		 */
		r->min_csn_pl = csnplNew();
	}
		
done:
	if (NULL != pb)
	{
		slapi_free_search_results_internal(pb);
		slapi_pblock_destroy (pb);
	}
    if (return_value != 0)
    {
        if (ruv)
            ruv_destroy (&ruv);
    }

	return return_value;
}

/* NOTE - this is the only non-api function that performs locking because
   it is called by the event queue */
void
replica_update_state (time_t when, void *arg)
{
	int rc;
	const char *replica_name = (const char *)arg;
	Object *replica_object = NULL;
	Replica *r;
	Slapi_Mod smod;
	LDAPMod *mods[3];
	Slapi_PBlock *pb = NULL;
	char *dn = NULL;
	struct berval *vals[2];
	struct berval val;
	LDAPMod mod;

	if (NULL == replica_name) 
		return;

	/*
	 * replica_get_by_name() will acquire the replica object
	 * and that could prevent the replica from being destroyed
	 * until the object_release is called.
	 */
	replica_object = replica_get_by_name(replica_name);
	if (NULL == replica_object) 
	{
		return;
	}

	/* We have a reference, so replica won't vanish on us. */
	r = (Replica *)object_get_data(replica_object);
	if (NULL == r)
	{
		goto done;
	}

	PR_Lock(r->repl_lock);

	/* replica state is currently being updated
	   or no CSN was assigned - bail out */
	if (r->state_update_inprogress)
	{
		PR_Unlock(r->repl_lock); 
		goto done;
	}

	/* This might be a consumer */
	if (!r->repl_csn_assigned)
	{
		/* EY: the consumer needs to flush ruv to disk. */
		PR_Unlock(r->repl_lock);
		replica_write_ruv(r);
		goto done;
	}
	
	/* ONREPL update csn generator state of an updatable replica only */
	/* ONREPL state always changes because we update time every second and
	   we write state to the disk less frequently */
	rc = csngen_get_state ((CSNGen*)object_get_data (r->repl_csngen), &smod);
	if (rc != 0)
	{
		PR_Unlock(r->repl_lock);
		goto done;
	}

	r->state_update_inprogress = PR_TRUE;
	r->repl_csn_assigned = PR_FALSE;

	dn = _replica_get_config_dn (r->repl_root);
	if (NULL == dn) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"replica_update_state: failed to get the config dn for %s\n",
			slapi_sdn_get_dn (r->repl_root));
		PR_Unlock(r->repl_lock);
		goto done;
	}
	pb = slapi_pblock_new();
	mods[0] = (LDAPMod*)slapi_mod_get_ldapmod_byref(&smod);

	/* we don't want to held lock during operations since it causes lock contention
       and sometimes deadlock. So releasing lock here */

    PR_Unlock(r->repl_lock);

	/* replica repl_name and new_name attributes do not get changed once
       the replica is configured - so it is ok that they are outside replica lock */
	
	/* write replica name if it has not been written before */
	if (r->new_name)
	{
		mods[1] = &mod;

		mod.mod_op   = LDAP_MOD_REPLACE|LDAP_MOD_BVALUES;
		mod.mod_type = (char*)attr_replicaName;
		mod.mod_bvalues = vals;
		vals [0] = &val;
		vals [1] = NULL;
		val.bv_val = r->repl_name;
		val.bv_len = strlen (val.bv_val);
		mods[2] = NULL;
	}
	else
	{
		mods[1] = NULL;
	}

	slapi_modify_internal_set_pb (pb, dn, mods, NULL, NULL,
		repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
	slapi_modify_internal_pb (pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if (rc != LDAP_SUCCESS) 
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_update_state: "
			"failed to update state of csn generator for replica %s: LDAP "
			"error - %d\n", slapi_sdn_get_dn(r->repl_root), rc);
	}
	else
	{
		r->new_name = PR_FALSE;
	}

	/* update RUV - performs its own locking */
	replica_write_ruv (r);

	/* since this is the only place this value is changed and we are 
	   guaranteed that only one thread enters the function, its ok
	   to change it outside replica lock */
    r->state_update_inprogress = PR_FALSE;

	slapi_ch_free ((void**)&dn);
	slapi_pblock_destroy (pb);
	slapi_mod_done (&smod);

done:
	if (replica_object)
		object_release (replica_object);
}

void
replica_write_ruv (Replica *r)
{	
	int rc;
	Slapi_Mod smod;
	Slapi_Mod smod_last_modified;
	LDAPMod *mods [3];	 
	Slapi_PBlock *pb;

	PR_ASSERT(r);

    PR_Lock(r->repl_lock);

    if (!r->repl_ruv_dirty)
    {
        PR_Unlock(r->repl_lock);
        return;
    }

	PR_ASSERT (r->repl_ruv);
	
	ruv_to_smod ((RUV*)object_get_data(r->repl_ruv), &smod);
	ruv_last_modified_to_smod ((RUV*)object_get_data(r->repl_ruv), &smod_last_modified);

    PR_Unlock (r->repl_lock);

	mods [0] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod);
	mods [1] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod_last_modified);
	mods [2] = NULL;
	pb = slapi_pblock_new();

    /* replica name never changes so it is ok to reference it outside the lock */
	slapi_modify_internal_set_pb_ext(
		pb,
		r->repl_root, /* only used to select be */
		mods,
		NULL, /* controls */
		RUV_STORAGE_ENTRY_UNIQUEID,
		repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION),
		/* Add OP_FLAG_TOMBSTONE_ENTRY so that this doesn't get logged in the Retro ChangeLog */
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | OP_FLAG_TOMBSTONE_ENTRY |
		OP_FLAG_REPL_RUV );
	slapi_modify_internal_pb (pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    /* ruv does not exist - create one */
    PR_Lock(r->repl_lock);

    if (rc == LDAP_SUCCESS)
    {
        r->repl_ruv_dirty = PR_FALSE;   
    }
    else if (rc == LDAP_NO_SUCH_OBJECT)
    {
        /* this includes an internal operation - but since this only happens
           during server startup - its ok that we have lock around it */
        rc = _replica_configure_ruv  (r, PR_TRUE);
        if (rc == 0)
            r->repl_ruv_dirty = PR_FALSE;
    }
	else /* error */
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
			"replica_write_ruv: failed to update RUV tombstone for %s; "
			"LDAP error - %d\n", 
			slapi_sdn_get_dn(r->repl_root), rc);
	}

    PR_Unlock(r->repl_lock);	
	
	slapi_mod_done (&smod);
	slapi_mod_done (&smod_last_modified);
	slapi_pblock_destroy (pb);
}


/* This routine figures out if an operation is for a replicated area and if so,
 * pulls out the operation CSN and returns it through the smods parameter.
 * It also informs the caller of the RUV entry's unique ID, since the caller
 * may not have access to the macro in repl5.h. */
int
replica_ruv_smods_for_op( Slapi_PBlock *pb, char **uniqueid, Slapi_Mods **smods )
{
    Object *replica_obj;
    Object *ruv_obj;
    Replica *replica;
    RUV *ruv;
    RUV *ruv_copy;
    CSN *opcsn = NULL;
    Slapi_Mod smod;
    Slapi_Mod smod_last_modified;
    Slapi_Operation *op;
    Slapi_Entry *target_entry = NULL;

    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &target_entry);
    if (target_entry && is_ruv_tombstone_entry(target_entry)) {
        /* disallow direct modification of the RUV tombstone entry
           must use the CLEANRUV task instead */
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
                        "replica_ruv_smods_for_op: attempted to directly modify the tombstone RUV "
                        "entry [%s] - use the CLEANALLRUV task instead\n",
                        slapi_entry_get_dn_const(target_entry));
        return (-1);
    }

    replica_obj = replica_get_replica_for_op (pb);
    slapi_pblock_get( pb, SLAPI_OPERATION, &op );

    if (NULL != replica_obj && NULL != op) {
        opcsn = operation_get_csn( op );
    }

    /* If the op has no CSN then it's not in a replicated area, so we're done */
    if (NULL == opcsn) {
        return (0);
    }

    replica = (Replica*)object_get_data(replica_obj);
    PR_ASSERT (replica);

    ruv_obj = replica_get_ruv(replica);
    PR_ASSERT (ruv_obj);

    ruv = (RUV*)object_get_data(ruv_obj);
    PR_ASSERT (ruv);

    ruv_copy = ruv_dup( ruv );

    object_release (ruv_obj);
    object_release (replica_obj);

    ruv_set_max_csn( ruv_copy, opcsn, NULL );

    ruv_to_smod( ruv_copy, &smod );
    ruv_last_modified_to_smod( ruv_copy, &smod_last_modified );

    ruv_destroy( &ruv_copy );

    *smods = slapi_mods_new();
    slapi_mods_add_smod(*smods, &smod);
    slapi_mods_add_smod(*smods, &smod_last_modified);
    *uniqueid = slapi_ch_strdup( RUV_STORAGE_ENTRY_UNIQUEID );

    return (1);
}



const CSN *
_get_deletion_csn(Slapi_Entry *e)
{
	const CSN *deletion_csn = NULL;

	PR_ASSERT(NULL != e);
	if (NULL != e)
	{
		Slapi_Attr *oc_attr = NULL;
		if (entry_attr_find_wsi(e, SLAPI_ATTR_OBJECTCLASS, &oc_attr) == ATTRIBUTE_PRESENT)
		{
			Slapi_Value *tombstone_value = NULL;
			struct berval v;
			v.bv_val = SLAPI_ATTR_VALUE_TOMBSTONE;
			v.bv_len = strlen(SLAPI_ATTR_VALUE_TOMBSTONE);
			if (attr_value_find_wsi(oc_attr, &v, &tombstone_value) == VALUE_PRESENT)
			{
				deletion_csn = value_get_csn(tombstone_value, CSN_TYPE_VALUE_UPDATED);
			}
		}
	}
	return deletion_csn;
}
	

static void
_delete_tombstone(const char *tombstone_dn, const char *uniqueid, int ext_op_flags)
{

	PR_ASSERT(NULL != tombstone_dn && NULL != uniqueid);
	if (NULL == tombstone_dn || NULL == uniqueid)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "_delete_tombstone: "
			"NULL tombstone_dn or uniqueid provided.\n");
	}
	else
	{
		int ldaprc;
		Slapi_PBlock *pb = slapi_pblock_new();
		slapi_delete_internal_set_pb(pb, tombstone_dn, NULL, /* controls */
			uniqueid, repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
			OP_FLAG_TOMBSTONE_ENTRY | ext_op_flags);
		slapi_delete_internal_pb(pb);
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ldaprc);
		if (LDAP_SUCCESS != ldaprc)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"_delete_tombstone: unable to delete tombstone %s, "
				"uniqueid %s: %s.\n", tombstone_dn, uniqueid,
				ldap_err2string(ldaprc));
		}
		slapi_pblock_destroy(pb);
	}
}

static
void get_reap_result (int rc, void *cb_data)
{
	PR_ASSERT (cb_data);

	((reap_callback_data*)cb_data)->rc = rc;
}

static int
process_reap_entry (Slapi_Entry *entry, void *cb_data)
{
	char deletion_csn_str[CSN_STRSIZE];
	char purge_csn_str[CSN_STRSIZE];
	unsigned long *num_entriesp = &((reap_callback_data *)cb_data)->num_entries;
	unsigned long *num_purged_entriesp = &((reap_callback_data *)cb_data)->num_purged_entries;
	CSN *purge_csn = ((reap_callback_data *)cb_data)->purge_csn;
	/* this is a pointer into the actual value in the Replica object - so that
	   if the value is set in the replica, we will know about it immediately */
	PRBool *tombstone_reap_stop = ((reap_callback_data *)cb_data)->tombstone_reap_stop;
	const CSN *deletion_csn = NULL;
	int rc = -1;

	/* abort reaping if we've been told to stop or we're shutting down */
	if (*tombstone_reap_stop || slapi_is_shutting_down()) {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"process_reap_entry: the tombstone reap process "
						" has been stopped\n");
		return rc;
	}

	/* we only ask for the objectclass in the search - the deletion csn is in the 
	   objectclass attribute values - if we need more attributes returned by the
	   search in the future, see _replica_reap_tombstones below and add more to the
	   attrs array */
	deletion_csn = _get_deletion_csn(entry);

	if ((NULL == deletion_csn || csn_compare(deletion_csn, purge_csn) < 0) &&
		(!is_ruv_tombstone_entry(entry))) {
		if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"process_reap_entry: removing tombstone %s "
							"because its deletion csn (%s) is less than the "
							"purge csn (%s).\n", 
							slapi_entry_get_dn(entry),
							csn_as_string(deletion_csn, PR_FALSE, deletion_csn_str),
							csn_as_string(purge_csn, PR_FALSE, purge_csn_str));
		}
		if (slapi_entry_attr_get_ulong(entry, "tombstonenumsubordinates") < 1) {
			_delete_tombstone(slapi_entry_get_dn(entry),
			                  slapi_entry_get_uniqueid(entry), 0);
			(*num_purged_entriesp)++;
		}
	}
	else {
		if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"process_reap_entry: NOT removing tombstone "
							"%s\n", slapi_entry_get_dn(entry));
		}
	}
	(*num_entriesp)++;

	return 0;
}




/* This does the actual work of searching for tombstones and deleting them.
   This must be called in a separate thread because it may take a long time.
*/
static void
_replica_reap_tombstones(void *arg)
{
	const char *replica_name = (const char *)arg;
	Slapi_PBlock *pb = NULL;
	Object *replica_object = NULL;
	Replica *replica = NULL;
	CSN *purge_csn = NULL;

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"Info: Beginning tombstone reap for replica %s.\n",
					replica_name ? replica_name : "(null)");

	if (NULL == replica_name) 
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"Warning: Replica name is null in tombstone reap\n");
		goto done;
	}

	/*
	 * replica_get_by_name() will acquire the replica object
	 * and that could prevent the replica from being destroyed
	 * until the object_release is called.
	 */
	replica_object = replica_get_by_name(replica_name);
	if (NULL == replica_object) 
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"Warning: Replica object %s is null in tombstone reap\n", replica_name);
		goto done;
	}

	/* We have a reference, so replica won't vanish on us. */
	replica = (Replica *)object_get_data(replica_object);
	if (NULL == replica)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"Warning: Replica %s is null in tombstone reap\n", replica_name);
		goto done;
	}

	if (replica->tombstone_reap_stop)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"Info: Replica %s reap stop flag is set for tombstone reap\n", replica_name);
		goto done;
	}

	purge_csn = replica_get_purge_csn(replica);
	if (NULL != purge_csn) 
	{
		LDAPControl **ctrls;
		int oprc;
		reap_callback_data cb_data;
		char **attrs = NULL;

		/* we just need the objectclass - for the deletion csn
		   and the dn and nsuniqueid - for possible deletion
		   and tombstonenumsubordinates to check if it has numsubordinates
		   saves time to return only 3 attrs
		*/
		charray_add(&attrs, slapi_ch_strdup("objectclass"));
		charray_add(&attrs, slapi_ch_strdup("nsuniqueid"));
		charray_add(&attrs, slapi_ch_strdup("tombstonenumsubordinates"));

		ctrls = (LDAPControl **)slapi_ch_calloc (3, sizeof (LDAPControl *));
		ctrls[0] = create_managedsait_control();
		ctrls[1] = create_backend_control(replica->repl_root);
		ctrls[2] = NULL;
		pb = slapi_pblock_new();
		slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(replica->repl_root),
									 LDAP_SCOPE_SUBTREE, "(objectclass=nstombstone)",
									 attrs, 0, ctrls, NULL,
									 repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);

		cb_data.rc = 0;
		cb_data.num_entries = 0UL;
		cb_data.num_purged_entries = 0UL;
		cb_data.purge_csn = purge_csn;
		/* set the cb data pointer to point to the actual memory address in
		   the actual Replica object - so that when the value in the Replica
		   is set, the reap process will know about it immediately */
		cb_data.tombstone_reap_stop = &(replica->tombstone_reap_stop);

		slapi_search_internal_callback_pb(pb, &cb_data /* callback data */,
										  get_reap_result /* result callback */,
										  process_reap_entry /* entry callback */,
										  NULL /* referral callback*/);

		charray_free(attrs);

		oprc = cb_data.rc;

		if (LDAP_SUCCESS != oprc)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"_replica_reap_tombstones: failed when searching for "
							"tombstones in replica %s: %s. Will try again in %ld "
							"seconds.\n", slapi_sdn_get_dn(replica->repl_root),
							ldap_err2string(oprc), replica->tombstone_reap_interval);
		}
		else
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"_replica_reap_tombstones: purged %ld of %ld tombstones "
							"in replica %s. Will try again in %ld "
							"seconds.\n", cb_data.num_purged_entries, cb_data.num_entries,
							slapi_sdn_get_dn(replica->repl_root),
							replica->tombstone_reap_interval);
		}
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"Info: No purge CSN for tombstone reap for replica %s.\n",
						replica_name);
	}

done:
	if (replica)
	{
		PR_Lock(replica->repl_lock);
		replica->tombstone_reap_active = PR_FALSE;
		PR_Unlock(replica->repl_lock);
	}

	if (NULL != purge_csn)
	{
		csn_free(&purge_csn);
	}
	if (NULL != pb)
	{
		slapi_free_search_results_internal(pb);
		slapi_pblock_destroy(pb);
	}
	if (NULL != replica_object)
	{
		object_release(replica_object);
		replica_object = NULL;
		replica = NULL;
	}

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"Info: Finished tombstone reap for replica %s.\n",
					replica_name ? replica_name : "(null)");

}
	
/*
  We don't want to run the reaper function directly from the event
  queue since it may hog the event queue, starving other events.
  See bug 604441
  The function eq_cb_reap_tombstones will fire off the actual thread
  that does the real work.
*/
static void
eq_cb_reap_tombstones(time_t when, void *arg)
{
	const char *replica_name = (const char *)arg;
	Object *replica_object = NULL;
	Replica *replica = NULL;

	if (NULL != replica_name) 
	{
		/*
		 * replica_get_by_name() will acquire the replica object
		 * and that could prevent the replica from being destroyed
		 * until the object_release is called.
		 */
		replica_object = replica_get_by_name(replica_name);
		if (NULL != replica_object) 
		{
			/* We have a reference, so replica won't vanish on us. */
			replica = (Replica *)object_get_data(replica_object);
			if (replica)
			{

				PR_Lock(replica->repl_lock);

				/* No action if purge is disabled or the previous purge is not done yet */
				if (replica->tombstone_reap_interval != 0 &&
					replica->tombstone_reap_active == PR_FALSE)
				{
					/* set the flag here to minimize race conditions */
					replica->tombstone_reap_active = PR_TRUE;
					if (PR_CreateThread(PR_USER_THREAD,
								_replica_reap_tombstones, (void *)replica_name,
								PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
								SLAPD_DEFAULT_THREAD_STACKSIZE) == NULL)
					{
						replica->tombstone_reap_active = PR_FALSE;
						slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
								"Error: unable to create the tombstone reap thread for replica %s.  "
								"Possible system resources problem\n",
								replica_name);
					}
				}
				/* reap thread will wait until this lock is released */
				PR_Unlock(replica->repl_lock);
			}
			object_release(replica_object);
			replica_object = NULL;
			replica = NULL;
		}
	}
}

static char * 
_replica_type_as_string (const Replica *r)
{
    switch (r->repl_type)
    {
        case REPLICA_TYPE_PRIMARY:      return "primary";
	    case REPLICA_TYPE_READONLY:     return "read-only";
	    case REPLICA_TYPE_UPDATABLE:    return "updatable";
        default:                        return "unknown";
    }
}


static const char *root_glue = 
	"dn: %s\n"
	"objectclass: top\n"
	"objectclass: nsTombstone\n"
	"objectclass: extensibleobject\n"
	"nsuniqueid: %s\n";

static int
replica_create_ruv_tombstone(Replica *r)
{
    int return_value = LDAP_LOCAL_ERROR;
    char *root_entry_str;
    Slapi_Entry *e = NULL;
    const char *purl = NULL;
    RUV *ruv;
    struct berval **bvals = NULL;
    Slapi_PBlock *pb = NULL;
    int rc;
	
    PR_ASSERT(NULL != r && NULL != r->repl_root);

    root_entry_str = slapi_ch_smprintf(root_glue, slapi_sdn_get_ndn(r->repl_root), RUV_STORAGE_ENTRY_UNIQUEID);

    e = slapi_str2entry(root_entry_str, SLAPI_STR2ENTRY_TOMBSTONE_CHECK);
    if (e == NULL)
        goto done;

    /* Add ruv */
    if (r->repl_ruv == NULL){
        CSNGen *gen;
        CSN *csn;
        char csnstr [CSN_STRSIZE];

        /* first attempt to write RUV tombstone - need to create RUV */
        gen = (CSNGen *)object_get_data(r->repl_csngen);
        PR_ASSERT (gen);

        if (csngen_new_csn(gen, &csn, PR_FALSE /* notify */) == CSN_SUCCESS){
            (void)csn_as_string(csn, PR_FALSE, csnstr);
            csn_free(&csn);

            /*
             * if this is an updateable replica - add its own
             * element to the RUV so that referrals work correctly
             */
            if (r->repl_type == REPLICA_TYPE_UPDATABLE)
                purl = multimaster_get_local_purl();

            if (ruv_init_new(csnstr, r->repl_rid, purl, &ruv) == RUV_SUCCESS){
                r->repl_ruv = object_new((void*)ruv, (FNFree)ruv_destroy);
                r->repl_ruv_dirty = PR_TRUE;
                return_value = LDAP_SUCCESS;
            } else {
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Cannot create new replica update vector for %s\n",
                    slapi_sdn_get_dn(r->repl_root));
                ruv_destroy(&ruv);
                goto done;
            }
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Cannot obtain CSN for new replica update vector for %s\n",
                slapi_sdn_get_dn(r->repl_root));
            csn_free(&csn);
            goto done;
        }
    } else { /* failed to write the entry because DB was not initialized - retry */
        ruv = (RUV*) object_get_data (r->repl_ruv);
        PR_ASSERT (ruv);
    }
	
    PR_ASSERT (r->repl_ruv);

    rc = ruv_to_bervals(ruv, &bvals);
    if (rc != RUV_SUCCESS){
        goto done;
    }
        
    /* ONREPL this is depricated function but there is currently no better API to use */
    rc = slapi_entry_add_values(e, type_ruvElement, bvals);
    if (rc != 0){
        goto done;
    }        

    pb = slapi_pblock_new();
    slapi_add_entry_internal_set_pb(pb, e, NULL /* controls */,	repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
        OP_FLAG_TOMBSTONE_ENTRY | OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | OP_FLAG_REPL_RUV);
    slapi_add_internal_pb(pb);
    e = NULL; /* add consumes e, upon success or failure */
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &return_value);
    if (return_value == LDAP_SUCCESS)
        r->repl_ruv_dirty = PR_FALSE;
		
done:
    slapi_entry_free (e);

    if (bvals)
        ber_bvecfree(bvals);

    if (pb)
        slapi_pblock_destroy(pb);

    slapi_ch_free_string(&root_entry_str);

    return return_value;
}


static void
assign_csn_callback(const CSN *csn, void *data)
{
    Replica *r = (Replica *)data;
    Object *ruv_obj;
    RUV *ruv;

    PR_ASSERT(NULL != csn);
    PR_ASSERT(NULL != r);

    ruv_obj = replica_get_ruv (r);
    PR_ASSERT (ruv_obj);
    ruv = (RUV*)object_get_data (ruv_obj);
    PR_ASSERT (ruv);

    PR_Lock(r->repl_lock);

    r->repl_csn_assigned = PR_TRUE;
	
    if (NULL != r->min_csn_pl)
    {
        if (csnplInsert(r->min_csn_pl, csn) != 0)
        {
            char csn_str[CSN_STRSIZE]; /* For logging only */
            /* Ack, we can't keep track of min csn. Punt. */
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "assign_csn_callback: "
                    "failed to insert csn %s for replica %s\n",
                    csn_as_string(csn, PR_FALSE, csn_str),
                    slapi_sdn_get_dn(r->repl_root));
            }
            csnplFree(&r->min_csn_pl);
        }
    }

    ruv_add_csn_inprogress (ruv, csn);

    PR_Unlock(r->repl_lock);

    object_release (ruv_obj);
}


static void
abort_csn_callback(const CSN *csn, void *data)
{
	Replica *r = (Replica *)data;
	Object *ruv_obj;
    RUV *ruv;
    int rc;

	PR_ASSERT(NULL != csn);
	PR_ASSERT(NULL != data);

    ruv_obj = replica_get_ruv (r);
    PR_ASSERT (ruv_obj);
    ruv = (RUV*)object_get_data (ruv_obj);
    PR_ASSERT (ruv);

	PR_Lock(r->repl_lock);

	if (NULL != r->min_csn_pl)
	{
		rc = csnplRemove(r->min_csn_pl, csn);
		PR_ASSERT(rc == 0);
	}

    ruv_cancel_csn_inprogress (ruv, csn);
	PR_Unlock(r->repl_lock);

    object_release (ruv_obj);
}

static CSN *
_replica_get_purge_csn_nolock(const Replica *r)
{
	CSN *purge_csn = NULL;
	CSN **csns = NULL;
	RUV *ruv;
	int i;

	if (r->repl_purge_delay > 0)
	{
		/* get a sorted list of all maxcsns in ruv in ascend order */
		object_acquire(r->repl_ruv);
		ruv = object_get_data(r->repl_ruv);
		csns = cl5BuildCSNList (ruv, NULL);
		object_release(r->repl_ruv);

		if (csns == NULL)
			return NULL;

		/* locate the most recent maxcsn in the csn list */
		for (i = 0; csns[i]; i++);
		purge_csn = csn_dup (csns[i-1]);

		/* set purge_csn to the most recent maxcsn - purge_delay */
		if ((csn_get_time(purge_csn) - r->repl_purge_delay) > 0)
			csn_set_time(purge_csn, csn_get_time(purge_csn) - r->repl_purge_delay);
	}

	if (csns)
		cl5DestroyCSNList (&csns);

	return purge_csn;
}

static void 
replica_get_referrals_nolock (const Replica *r, char ***referrals)
{
    if(referrals!=NULL)
	{
        
		int hint;
		int i= 0;
		Slapi_Value *v= NULL;
		
		if (NULL == r->repl_referral)
		{
			*referrals = NULL;
		}
		else
		{
			/* richm: +1 for trailing NULL */
			*referrals= (char**)slapi_ch_calloc(sizeof(char*),1+slapi_valueset_count(r->repl_referral));
			hint= slapi_valueset_first_value( r->repl_referral, &v );
			while(v!=NULL)
			{
				const char *s= slapi_value_get_string(v);
				if(s!=NULL && s[0]!='\0')
				{
					(*referrals)[i]= slapi_ch_strdup(s);
					i++;
				}
				hint= slapi_valueset_next_value( r->repl_referral, hint, &v);
			}
			(*referrals)[i] = NULL;
		}
		
	}
}

static void 
replica_clear_legacy_referrals(const Slapi_DN *repl_root_sdn,
							   char **referrals, const char *state)
{
	repl_set_mtn_state_and_referrals(repl_root_sdn, state, NULL, NULL, referrals);
}

static void 
replica_remove_legacy_attr (const Slapi_DN *repl_root_sdn, const char *attr)
{
    Slapi_PBlock *pb;
    Slapi_Mods smods;
    LDAPControl **ctrls;
    int rc;

    pb = slapi_pblock_new ();
    
    slapi_mods_init(&smods, 1);
    slapi_mods_add(&smods, LDAP_MOD_DELETE, attr, 0, NULL);
   
    
    ctrls = (LDAPControl**)slapi_ch_malloc (2 * sizeof (LDAPControl*));
    ctrls[0] = create_managedsait_control ();
    ctrls[1] = NULL;
    
    /* remove copiedFrom/copyingFrom first */
    slapi_modify_internal_set_pb_ext (pb, repl_root_sdn, 
                                      slapi_mods_get_ldapmods_passout (&smods),
                                      ctrls, NULL /*uniqueid */, 
                                      repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION) , 
                                      0 /* operation_flags */);
 
    slapi_modify_internal_pb (pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS) 
	{
        /* this is not a fatal error because the attribute may not be there */
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_remove_legacy_attr: "
			"failed to remove legacy attribute %s for replica %s; LDAP error - %d\n", 
            attr, slapi_sdn_get_dn(repl_root_sdn), rc);
	}
    
    slapi_mods_done (&smods);
    slapi_pblock_destroy (pb);
}

static int 
replica_log_ruv_elements_nolock (const Replica *r)
{
    int rc = 0;
    slapi_operation_parameters op_params;
    RUV *ruv;
    char *repl_gen; 
    CSN *csn = NULL;

    ruv = (RUV*) object_get_data (r->repl_ruv);
    PR_ASSERT (ruv);

    if ((ruv_get_min_csn(ruv, &csn) == RUV_SUCCESS) && csn)
    {
        /* we log it as a delete operation to have the least number of fields
           to set. the entry can be identified by a special target uniqueid and
           special target dn */
        memset (&op_params, 0, sizeof (op_params));
        op_params.operation_type = SLAPI_OPERATION_DELETE;
        op_params.target_address.sdn = slapi_sdn_new_ndn_byval(START_ITERATION_ENTRY_DN);
        op_params.target_address.uniqueid = START_ITERATION_ENTRY_UNIQUEID;
        op_params.csn = csn;
        repl_gen = ruv_get_replica_generation (ruv);

        rc = cl5WriteOperation(r->repl_name, repl_gen, &op_params, PR_FALSE); 
        if (rc == CL5_SUCCESS)
            rc = 0;
        else
            rc = -1;

        slapi_ch_free ((void**)&repl_gen);
        slapi_sdn_free(&op_params.target_address.sdn);
        csn_free (&csn);
    }

    return rc;
}

void
replica_set_purge_delay(Replica *r, PRUint32 purge_delay)
{
	PR_ASSERT(r);
	PR_Lock(r->repl_lock);
	r->repl_purge_delay = purge_delay;
	PR_Unlock(r->repl_lock);
}

void
replica_set_tombstone_reap_interval (Replica *r, long interval)
{
	char *repl_name;

	PR_Lock(r->repl_lock);

	/*
	 * Leave the event there to purge the existing tombstones
	 * if we are about to turn off tombstone creation
	 */
	if (interval > 0 && r->repl_eqcxt_tr && r->tombstone_reap_interval != interval)
	{
		int found;

		repl_name = slapi_eq_get_arg (r->repl_eqcxt_tr);
		slapi_ch_free ((void**)&repl_name);
		found = slapi_eq_cancel (r->repl_eqcxt_tr);
		slapi_log_error (SLAPI_LOG_REPL, NULL,
			"tombstone_reap event (interval=%ld) was %s\n",
			r->tombstone_reap_interval, (found ? "cancelled" : "not found"));
		r->repl_eqcxt_tr = NULL;
	}
	r->tombstone_reap_interval = interval;
	if ( interval > 0 && r->repl_eqcxt_tr == NULL )
	{
		repl_name = slapi_ch_strdup (r->repl_name);
		r->repl_eqcxt_tr = slapi_eq_repeat (eq_cb_reap_tombstones, repl_name,
											current_time() + r->tombstone_reap_interval,
											1000 * r->tombstone_reap_interval);
		slapi_log_error (SLAPI_LOG_REPL, NULL,
			"tombstone_reap event (interval=%ld) was %s\n",
			r->tombstone_reap_interval, (r->repl_eqcxt_tr ? "scheduled" : "not scheduled successfully"));
	}
	PR_Unlock(r->repl_lock);
}

static void
replica_strip_cleaned_rids(Replica *r)
{
    Object *RUVObj;
    RUV *ruv = NULL;
    ReplicaId rid[32] = {0};
    int i = 0;

    RUVObj = replica_get_ruv(r);
    ruv =  (RUV*)object_get_data (RUVObj);

    ruv_get_cleaned_rids(ruv, rid);
    while(rid[i] != 0){
        ruv_delete_replica(ruv, rid[i]);
        replica_set_ruv_dirty(r);
        replica_write_ruv(r);
        i++;
    }
    object_release(RUVObj);
}

/* Update the tombstone entry to reflect the content of the ruv */
static void
replica_replace_ruv_tombstone(Replica *r)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Mod smod;
    Slapi_Mod smod_last_modified;
    LDAPMod *mods [3];
    char *dn;
    int rc;

    PR_ASSERT(NULL != r && NULL != r->repl_root);

    replica_strip_cleaned_rids(r);

    PR_Lock(r->repl_lock);

    PR_ASSERT (r->repl_ruv);
    ruv_to_smod ((RUV*)object_get_data(r->repl_ruv), &smod);
    ruv_last_modified_to_smod ((RUV*)object_get_data(r->repl_ruv), &smod_last_modified);

    dn = _replica_get_config_dn (r->repl_root);
    if (NULL == dn) {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
            "replica_replace_ruv_tombstone: "
            "failed to get the config dn for %s\n",
            slapi_sdn_get_dn (r->repl_root));
        PR_Unlock(r->repl_lock);
        goto bail;
    }
    mods[0] = (LDAPMod*)slapi_mod_get_ldapmod_byref(&smod);
    mods[1] = (LDAPMod*)slapi_mod_get_ldapmod_byref(&smod_last_modified);

    PR_Unlock (r->repl_lock);

    mods [2] = NULL;
    pb = slapi_pblock_new();

    slapi_modify_internal_set_pb_ext(
        pb,
        r->repl_root, /* only used to select be */
        mods,
        NULL, /* controls */
        RUV_STORAGE_ENTRY_UNIQUEID,
        repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | OP_FLAG_REPL_RUV);

    slapi_modify_internal_pb (pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    if (rc != LDAP_SUCCESS)
    {
        if ((rc != LDAP_NO_SUCH_OBJECT && rc != LDAP_TYPE_OR_VALUE_EXISTS) || !replica_is_state_flag_set(r, REPLICA_IN_USE))
        {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_replace_ruv_tombstone: "
                "failed to update replication update vector for replica %s: LDAP "
                "error - %d\n", (char*)slapi_sdn_get_dn (r->repl_root), rc);
        }
    }

    slapi_ch_free ((void**)&dn);
    slapi_pblock_destroy (pb);
bail:
    slapi_mod_done (&smod);
    slapi_mod_done (&smod_last_modified);
}

void
replica_update_ruv_consumer(Replica *r, RUV *supplier_ruv)
{
	ReplicaId supplier_id = 0;
	char *supplier_purl = NULL;

	if ( ruv_get_first_id_and_purl(supplier_ruv, &supplier_id, &supplier_purl) == RUV_SUCCESS)
	{
		RUV *local_ruv = NULL;

		PR_Lock(r->repl_lock);

		local_ruv =  (RUV*)object_get_data (r->repl_ruv);

		if(is_cleaned_rid(supplier_id) || local_ruv == NULL){
			PR_Unlock(r->repl_lock);
			return;
		}

		if ( ruv_local_contains_supplier(local_ruv, supplier_id) == 0 )
		{
			if ( r->repl_type == REPLICA_TYPE_UPDATABLE )
			{
				/* Add the new ruv right after the consumer own purl */
				ruv_add_index_replica(local_ruv, supplier_id, supplier_purl, 2);
			}
			else
			{
				/* This is a consumer only, add it first */
				ruv_add_index_replica(local_ruv, supplier_id, supplier_purl, 1);
			}
		}
		else
		{   
			/* Replace it */
			ruv_replace_replica_purl(local_ruv, supplier_id, supplier_purl);
		}
		PR_Unlock(r->repl_lock);

		/* Update also the directory entry */
		replica_replace_ruv_tombstone(r);
	}
}

void 
replica_set_ruv_dirty(Replica *r)
{
	PR_ASSERT(r);
	PR_Lock(r->repl_lock);
	r->repl_ruv_dirty = PR_TRUE;
	PR_Unlock(r->repl_lock);
}

PRBool
replica_is_state_flag_set(Replica *r, PRInt32 flag)
{
	PR_ASSERT(r);
	if (r)
		return (r->repl_state_flags & flag);
	else
		return PR_FALSE;
}

void 
replica_set_state_flag (Replica *r, PRUint32 flag, PRBool clear)
{
    if (r == NULL)
        return;

	PR_Lock(r->repl_lock);

    if (clear)
    {
        r->repl_state_flags &= ~flag; 
    }
    else
    {
        r->repl_state_flags |= flag;
    }

	PR_Unlock(r->repl_lock);
}

/**
 * Use this to tell the tombstone reap process to stop.  This will
 * typically be used when we (consumer) get a request to do a
 * total update.
 */
void
replica_set_tombstone_reap_stop(Replica *r, PRBool val)
{
    if (r == NULL)
        return;

	PR_Lock(r->repl_lock);
	r->tombstone_reap_stop = val;
	PR_Unlock(r->repl_lock);
}

/* replica just came back online, probably after data was reloaded */
void 
replica_enable_replication (Replica *r)
{
    int rc;
    
    PR_ASSERT(r);

    /* prevent creation of new agreements until the replica is enabled */
    PR_Lock(r->agmt_lock);

    /* retrieve new ruv */
    rc = replica_reload_ruv (r);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_enable_replication: "
                        "reloading ruv failed\n");
        /* What to do ? */
    }

    /* Replica came back online, Check if the total update was terminated.
       If flag is still set, it was not terminated, therefore the data is
       very likely to be incorrect, and we should not restart Replication threads...
    */
    if (!replica_is_state_flag_set(r, REPLICA_TOTAL_IN_PROGRESS)){
        /* restart outbound replication */
        start_agreements_for_replica (r, PR_TRUE);
		
        /* enable ruv state update */
        replica_set_enabled (r, PR_TRUE);
    }
	
    /* mark the replica as being available for updates */
    replica_relinquish_exclusive_access(r, 0, 0);

    replica_set_state_flag(r, REPLICA_AGREEMENTS_DISABLED, PR_TRUE /* clear */);
    PR_Unlock(r->agmt_lock);

    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_enable_replication: "
                    "replica %s is relinquished\n",
                    slapi_sdn_get_ndn (replica_get_root (r)));
}

/* replica is about to be taken offline */
void 
replica_disable_replication (Replica *r, Object *r_obj)
{
	char *current_purl = NULL;
	char *p_locking_purl = NULL;
	char *locking_purl = NULL;
	ReplicaId junkrid;
    PRBool isInc = PR_FALSE; /* get exclusive access, but not for inc update */
	RUV *repl_ruv = NULL;

    /* prevent creation of new agreements until the replica is disabled */
    PR_Lock(r->agmt_lock);

    /* stop ruv update */
    replica_set_enabled (r, PR_FALSE);

    /* disable outbound replication */
    start_agreements_for_replica (r, PR_FALSE);

    /* close the corresponding changelog file */
    /* close_changelog_for_replica (r_obj); */

    /* mark the replica as being unavailable for updates */
    /* If an incremental update is in progress, we want to wait until it is
       finished until we get exclusive access to the replica, because we have
       to make sure no operations are in progress - it messes up replication
       when a restore is in progress but we are still adding replicated entries
       from a supplier
    */
    repl_ruv = (RUV*) object_get_data (r->repl_ruv);
	ruv_get_first_id_and_purl(repl_ruv, &junkrid, &p_locking_purl);
	locking_purl = slapi_ch_strdup(p_locking_purl);
	p_locking_purl = NULL;
	repl_ruv = NULL;	
    while (!replica_get_exclusive_access(r, &isInc, 0, 0, "replica_disable_replication",
										 &current_purl)) {
        if (!isInc) /* already locked, but not by inc update - break */
            break;
        isInc = PR_FALSE;
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"replica_disable_replication: "
						"replica %s is already locked by (%s) for incoming "
						"incremental update; sleeping 100ms\n",
                        slapi_sdn_get_ndn (replica_get_root (r)),
						current_purl ? current_purl : "unknown");
		slapi_ch_free_string(&current_purl);
        DS_Sleep(PR_MillisecondsToInterval(100));
    }

	slapi_ch_free_string(&current_purl);
	slapi_ch_free_string(&locking_purl);
    replica_set_state_flag(r, REPLICA_AGREEMENTS_DISABLED, PR_FALSE);
    PR_Unlock(r->agmt_lock);

    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "replica_disable_replication: "
                    "replica %s is acquired\n",
                    slapi_sdn_get_ndn (replica_get_root (r)));
}

static void 
start_agreements_for_replica (Replica *r, PRBool start)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;

    agmt_obj = agmtlist_get_first_agreement_for_replica (r);
    while (agmt_obj)
    {
        agmt = (Repl_Agmt*)object_get_data (agmt_obj);
        PR_ASSERT (agmt);
        if(agmt_is_enabled(agmt)){
            if (start)
                agmt_start (agmt);
            else    /* stop */
                agmt_stop (agmt);
        }
        agmt_obj = agmtlist_get_next_agreement_for_replica (r, agmt_obj);
    }
}

int replica_start_agreement(Replica *r, Repl_Agmt *ra)
{
    int ret = 0;

    if (r == NULL) return -1;

    PR_Lock(r->agmt_lock);

    if (!replica_is_state_flag_set(r, REPLICA_AGREEMENTS_DISABLED) && agmt_is_enabled(ra)) {
        ret = agmt_start(ra); /* Start the replication agreement */
    }

    PR_Unlock(r->agmt_lock);
    return ret;
}

int windows_replica_start_agreement(Replica *r, Repl_Agmt *ra)
{
	int ret = 0;
	
	if (r == NULL) return -1;
	
	PR_Lock(r->agmt_lock);
	
	if (!replica_is_state_flag_set(r, REPLICA_AGREEMENTS_DISABLED)) {
		ret = windows_agmt_start(ra); /* Start the replication agreement */
		/* ret = windows_agmt_start(ra); Start the replication agreement */
	}
	
	PR_Unlock(r->agmt_lock);
	return ret;
}


/*
 * A callback function registed as op->o_csngen_handler and
 * called by backend ops to generate opcsn.
 */
CSN *
replica_generate_next_csn ( Slapi_PBlock *pb, const CSN *basecsn )
{
	CSN *opcsn = NULL;
	Object *replica_obj;

	replica_obj = replica_get_replica_for_op (pb);
	if (NULL != replica_obj)
	{
		Replica *replica = (Replica*) object_get_data (replica_obj);
		if ( NULL != replica )
		{
			Slapi_Operation *op;
			slapi_pblock_get (pb, SLAPI_OPERATION, &op);
			if ( replica->repl_type != REPLICA_TYPE_READONLY ||
				 operation_is_flag_set (op, OP_FLAG_LEGACY_REPLICATION_DN ))
			{
				Object *gen_obj = replica_get_csngen (replica);
				if (NULL != gen_obj)
				{
					CSNGen *gen = (CSNGen*) object_get_data (gen_obj);
					if (NULL != gen)
					{
						/* The new CSN should be greater than the base CSN */
						csngen_new_csn (gen, &opcsn, PR_FALSE /* don't notify */);
						if (csn_compare (opcsn, basecsn) <= 0)
						{
							char opcsnstr[CSN_STRSIZE], basecsnstr[CSN_STRSIZE];
							char opcsn2str[CSN_STRSIZE];

							csn_as_string (opcsn, PR_FALSE, opcsnstr);
							csn_as_string (basecsn, PR_FALSE, basecsnstr);
							csn_free ( &opcsn );
							csngen_adjust_time (gen, basecsn);
							csngen_new_csn (gen, &opcsn, PR_FALSE /* don't notify */);
							csn_as_string (opcsn, PR_FALSE, opcsn2str);
							slapi_log_error (SLAPI_LOG_FATAL, NULL,
								"replica_generate_next_csn: "
								"opcsn=%s <= basecsn=%s, adjusted opcsn=%s\n",
								opcsnstr, basecsnstr, opcsn2str);
						}
						/*
						 * Insert opcsn into the csn pending list.
						 * This is the notify effect in csngen_new_csn().
						 */
						assign_csn_callback (opcsn, (void *)replica);
					}
					object_release (gen_obj);
				}
			}
		}
		object_release (replica_obj);
	}

	return opcsn;
}

/*
 * A callback function registed as op->o_replica_attr_handler and
 * called by backend ops to get replica attributes.
 */
int
replica_get_attr ( Slapi_PBlock *pb, const char* type, void *value )
{
	int rc = -1;

	Object *replica_obj;
	replica_obj = replica_get_replica_for_op (pb);
	if (NULL != replica_obj)
	{
		Replica *replica = (Replica*) object_get_data (replica_obj);
		if ( NULL != replica )
		{
			if (strcasecmp (type, type_replicaTombstonePurgeInterval) == 0)
			{
				*((int*)value) = replica->tombstone_reap_interval;
				rc = 0;
			}
			else if (strcasecmp (type, type_replicaPurgeDelay) == 0)
			{
				*((int*)value) = replica->repl_purge_delay;
				rc = 0;
			}
		}
		object_release (replica_obj);
	}

	return rc;
}

int
replica_get_backoff_min(Replica *r)
{
	return (int)r->backoff_min;
}

int
replica_get_backoff_max(Replica *r)
{
	return (int)r->backoff_max;
}

void
replica_set_backoff_min(Replica *r, int min)
{
	r->backoff_min = min;
}

void
replica_set_backoff_max(Replica *r, int max)
{
	r->backoff_max = max;
}
