/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* repl5_replica.c */

#include "slapi-plugin.h"
#include "repl.h"   /* ONREPL - this is bad */
#include "repl5.h" 
#include "windowsrepl.h"
#include "repl_shared.h" 
#include "csnpl.h"
#include "cl5_api.h"

/* from proto-slap.h */
int g_get_shutdown();

#define RUV_SAVE_INTERVAL (30 * 1000) /* 30 seconds */
#define START_UPDATE_DELAY 2 /* 2 second */
#define START_REAP_DELAY 3600 /* 1 hour */

#define REPLICA_RDN				 "cn=windowsreplica"
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
	ReplicaId repl_rid;				/* replicaID	*/
	Object	*repl_ruv;				/* replica update vector */
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
	long tombstone_reap_interval; /* Time in seconds between tombstone reaping */
	Slapi_ValueSet *repl_referral;  /* A list of administrator provided referral URLs */
    PRBool state_update_inprogress; /* replica state is being updated */
    PRLock *agmt_lock;          /* protects agreement creation, start and stop */
	char *locking_purl;			/* supplier who has exclusive access */


    Object *consumer_repl_ruv; /* tracks location in changelog for changes to send to active directoroy */

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
static Slapi_Entry* _windows_replica_get_config_entry (const Slapi_DN *root);
static int _windows_replica_check_validity (const Replica *r);
static int _windows_replica_init_from_config (Replica *r, Slapi_Entry *e, char *errortext);
static int __replica_update_entry (Replica *r, Slapi_Entry *e, char *errortext);
static int __replica_configure_ruv (Replica *r, PRBool isLocked);
static void _windows_replica_update_state (time_t when, void *arg);
static char * _replica_get_config_dn (const Slapi_DN *root);
static char * __replica_type_as_string (const Replica *r);
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
void replica_consumer_set_ruv (Replica *r, RUV *ruv) ;

/* PRBool */
/* replica_is_state_flag_set(Replica *r, PRInt32 flag) */
/* { */
/* 	PR_ASSERT(r); */
/* 	if (r) */
/* 		return (r->repl_state_flags & flag); */
/* 	else */
/* 		return PR_FALSE; */
/* } */

/* Replica * */
/* windows_replica_new(const Slapi_DN *root) */
/* { */
/*   Replica *r = NULL; */
/*   Slapi_Entry *e = NULL; */
/*   char errorbuf[BUFSIZ]; */
/*   char ebuf[BUFSIZ]; */

/*   PR_ASSERT (root); */

/*   /\* check if there is a replica associated with the tree *\/ */
/*   e = _windows_replica_get_config_entry (root); */
/*   if (e) */
/*     { */
/*       errorbuf[0] = '\0'; */
/*       r = windows_replica_new_from_entry(e, errorbuf, */
/* 				 PR_FALSE /\* not a newly added entry *\/); */

/*       if (NULL == r) */
/* 	{ */
/* 	  slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "Unable to " */
/* 			  "configure replica %s: %s\n", */
/* 			  escape_string(slapi_sdn_get_dn(root), ebuf), */
/* 			  errorbuf); */
/* 	} */

/*       slapi_entry_free (e); */
/*     } */

/*   return r; */
/* } */

/*
int windows_replica_start_agreement
(Replica *r, Repl_Agmt *ra)
{
    int ret = 0;

    if (r == NULL) return -1;

    PR_Lock(r->agmt_lock);

    slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "windows_replica_start_agreement: state_flag =%d\n",
                        !replica_is_state_flag_set(r, REPLICA_AGREEMENTS_DISABLED));


    if (!replica_is_state_flag_set(r, REPLICA_AGREEMENTS_DISABLED)) {
        ret = windows_agmt_start(ra); 
	slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "windows_replica_start_agreement: rc=%d\n", ret);
	ret = 0;
    }

    PR_Unlock(r->agmt_lock);
    
    return ret;
} */


/*
 * A callback function registed as op->o_replica_attr_handler and
 * called by backend ops to get replica attributes.
 */
int
__replica_get_attr ( Slapi_PBlock *pb, const char* type, void *value )
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


static Slapi_Entry*
_windows_replica_get_config_entry (const Slapi_DN *root)
{
  int rc = 0;
  char *dn = NULL;
  Slapi_Entry **entries;
  Slapi_Entry *e = NULL;
  Slapi_PBlock *pb = NULL;

  dn = _replica_get_config_dn (root);
  pb = slapi_pblock_new ();

  slapi_search_internal_set_pb (pb, dn, LDAP_SCOPE_BASE, "objectclass=*", NULL, 0, NULL,
				NULL, repl_get_plugin_identity (PLUGIN_WINDOWS_REPLICATION), 0);
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


static char* 
_replica_get_config_dn (const Slapi_DN *root)
{
    char *dn;
    const char *mp_base = slapi_get_mapping_tree_config_root ();
    int len; 

    PR_ASSERT (root);

    len = strlen (REPLICA_RDN) + strlen (slapi_sdn_get_dn (root)) +
          strlen (mp_base) + 8; /* 8 = , + cn= + \" + \" + , + \0 */

    dn = (char*)slapi_ch_malloc (len);
    sprintf (dn, "%s,cn=\"%s\",%s", REPLICA_RDN, slapi_sdn_get_dn (root), mp_base);

    return dn;
}

Replica *
windows_replica_new_from_entry (Slapi_Entry *e, char *errortext, PRBool is_add_operation)
{
    int rc = 0;
    Replica *r;
    RUV *ruv;
    RUV *consumer_ruv;
	char *repl_name = NULL;

    if (e == NULL)
    {
        if (NULL != errortext)
		{
            sprintf (errortext, "NULL entry");
		}
        return NULL;        
    }

   	r = (Replica *)slapi_ch_calloc(1, sizeof(Replica));

	if ((r->repl_lock = PR_NewLock()) == NULL)
	{
		if (NULL != errortext)
		{
            sprintf (errortext, "failed to create replica lock");
		}
		rc = -1;
		goto done;
	}

	if ((r->agmt_lock = PR_NewLock()) == NULL)
	{
		if (NULL != errortext)
		{
            sprintf (errortext, "failed to create replica lock");
		}
		rc = -1;
		goto done;
	}

    /* read parameters from the replica config entry */
    rc = _windows_replica_init_from_config (r, e, errortext);
    if (rc != 0)
	{
		goto done;
	}

	/* configure ruv */
    rc = __replica_configure_ruv (r, PR_FALSE);
	if (rc != 0)
	{
		goto done;
	}

	/* If smallest csn exists in RUV for our local replica, it's ok to begin iteration */
	ruv = (RUV*) object_get_data (r->repl_ruv);  //XXX 
	PR_ASSERT (ruv); 
	
	consumer_ruv = ruv_dup(ruv);
	replica_consumer_set_ruv(r, consumer_ruv);
	
	if (is_add_operation)
	{
		/*
		 * This is called by an ldap add operation.
         * Update the entry to contain information generated
		 * during replica initialization
		 */
	  rc = __replica_update_entry (r, e, errortext);
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
	r->repl_eqcxt_rs = slapi_eq_repeat(_windows_replica_update_state, repl_name, 
                                       current_time () + START_UPDATE_DELAY, RUV_SAVE_INTERVAL);

	if (r->tombstone_reap_interval > 0)
	{
		/* 
		 * Reap Tombstone should be started some time after the plugin started.
		 * This will allow the server to fully start before consuming resources.
		 */
		repl_name = slapi_ch_strdup (r->repl_name);
		// XXX r->repl_eqcxt_tr = slapi_eq_repeat(eq_cb_reap_tombstones, repl_name, current_time() + START_REAP_DELAY, 1000 * r->tombstone_reap_interval);
	}

    if (r->legacy_consumer)
    {
		char ebuf[BUFSIZ];
		
        legacy_consumer_init_referrals (r);
        slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name, "replica_new_from_entry: "
                        "replica for %s was configured as legacy consumer\n",
                        escape_string(slapi_sdn_get_dn(r->repl_root),ebuf));
    }

done:
    if (rc != 0 && r)
	{
        replica_destroy ((void**)&r);    
	}
   
    return r;
 


}

static int 
_windows_replica_init_from_config (Replica *r, Slapi_Entry *e, char *errortext)
{
	int rc;
	Slapi_Attr *attr;
	char *val;
	CSNGen *gen; 
    char buf [BUFSIZ];
    char *errormsg = errortext? errortext : buf;
	Slapi_Attr *a = NULL;
	char dnescape[BUFSIZ]; /* for escape_string */

	PR_ASSERT (r && e);

    /* get replica root */
	val = slapi_entry_attr_get_charptr (e, attr_replicaRoot);
    if (val == NULL)
    {
        sprintf (errormsg, "failed to retrieve %s attribute from (%s)\n", 
                 attr_replicaRoot,
				 escape_string((char*)slapi_entry_get_dn ((Slapi_Entry*)e), dnescape));
        slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "_replica_init_from_config: %s\n",
                        errormsg);
                        
        return -1;	
    }
    
    r->repl_root = slapi_sdn_new_dn_passin (val);

	/* get replica type */
    val = slapi_entry_attr_get_charptr (e, attr_replicaType);
    if (val)
    {
	    r->repl_type = atoi(val);
        slapi_ch_free ((void**)&val);
    }
    else
    {
        r->repl_type = REPLICA_TYPE_READONLY;
    }

    /* get legacy consumer flag */
    val = slapi_entry_attr_get_charptr (e, type_replicaLegacyConsumer);
    if (val)
    {
        if (strcasecmp (val, "on") == 0 || strcasecmp (val, "yes") == 0 ||
            strcasecmp (val, "true") == 0 || strcasecmp (val, "1") == 0)
        {
	        r->legacy_consumer = PR_TRUE;
        }
        else
        {
            r->legacy_consumer = PR_FALSE;
        }

        slapi_ch_free ((void**)&val);
    }
    else
    {
        r->legacy_consumer = PR_FALSE;
    }

    /* get replica flags */
    r->repl_flags = slapi_entry_attr_get_ulong(e, attr_flags);

	/* get replicaid */
	/* the replica id is ignored for read only replicas and is set to the
	   special value READ_ONLY_REPLICA_ID */
	if (r->repl_type == REPLICA_TYPE_READONLY)
	{
		r->repl_rid = READ_ONLY_REPLICA_ID;
		slapi_entry_attr_set_uint(e, attr_replicaId, (unsigned int)READ_ONLY_REPLICA_ID);
	}
	/* a replica id is required for updatable and primary replicas */
	else if (r->repl_type == REPLICA_TYPE_UPDATABLE ||
			 r->repl_type == REPLICA_TYPE_PRIMARY)
	{
		if ((val = slapi_entry_attr_get_charptr (e, attr_replicaId)))
		{
			int temprid = atoi (val);
			slapi_ch_free ((void**)&val);
			if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID)
			{
/* 				sprintf (errormsg, */
/* 						 "attribute %s must have a value greater than 0 " */
/* 						 "and less than %d: entry %s", */
/* 						 attr_replicaId, READ_ONLY_REPLICA_ID, */
/* 						 escape_string((char*)slapi_entry_get_dn ((Slapi_Entry*)e), */
/* 									   dnescape)); */
/* 				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, */
/* 								"_replica_init_from_config: %s\n", */
/* 								errormsg); */
/* 				return -1; */
			}
			else
			{
				r->repl_rid = (ReplicaId)temprid;
			}
		}
		else
		{
			sprintf (errormsg, "failed to retrieve required %s attribute from %s",
					 attr_replicaId,
					 escape_string((char*)slapi_entry_get_dn ((Slapi_Entry*)e),
								   dnescape));
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"_replica_init_from_config: %s\n",
							errormsg);
			return -1;
		}
	}

	attr = NULL;
	rc = slapi_entry_attr_find(e, attr_state, &attr);
	gen = csngen_new (r->repl_rid, attr);
	if (gen == NULL)
	{
        sprintf (errormsg, "failed to create csn generator for replica (%s)",
				 escape_string((char*)slapi_entry_get_dn ((Slapi_Entry*)e),
							   dnescape));
        slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
						"_replica_init_from_config: %s\n",
						errormsg);
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
	}
    else
    {
        rc = slapi_uniqueIDGenerateString (&r->repl_name);
        if (rc != UID_SUCCESS)
	    {
            sprintf (errormsg, "failed to assign replica name for replica (%s); "
                     "uuid generator error - %d ",
					 escape_string((char*)slapi_entry_get_dn ((Slapi_Entry*)e), dnescape),
					 rc);
            slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "_replica_init_from_config: %s\n",
                            errormsg);
		    return -1;
	    }  
        else
            r->new_name = PR_TRUE;          
    }

	/* get the list of referrals */
	slapi_entry_attr_find( e, attr_replicaReferral, &attr );
	if(attr!=NULL)
	{
		slapi_attr_get_valueset(attr, &r->repl_referral);
	}

	/*
	 * Set the purge offset (default 7 days). This is the extra
	 * time we allow purgeable CSNs to stick around, in case a
	 * replica regresses. Could also be useful when LCUP happens,
	 * since we don't know about LCUP replicas, and they can just
	 * turn up whenever they want to.
	 */
	if (slapi_entry_attr_find(e, type_replicaPurgeDelay, &a) == -1)
	{
		/* No purge delay provided, so use default */
		r->repl_purge_delay = 60 * 60 * 24 * 7; /* One week, in seconds */
	}
	else
	{
		r->repl_purge_delay = slapi_entry_attr_get_uint(e, type_replicaPurgeDelay);
	}

	if (slapi_entry_attr_find(e, type_replicaTombstonePurgeInterval, &a) == -1)
	{
		/* No reap interval provided, so use default */
		r->tombstone_reap_interval = 3600 * 24; /* One day */
	}
	else
	{
		r->tombstone_reap_interval = slapi_entry_attr_get_int(e, type_replicaTombstonePurgeInterval);
	}

	r->tombstone_reap_stop = r->tombstone_reap_active = PR_FALSE;

    return (_windows_replica_check_validity (r));
}


static int 
_windows_replica_check_validity (const Replica *r)
{
    PR_ASSERT (r);

    if (r->repl_root == NULL || r->repl_type == 0 ||
        r->repl_rid > MAX_REPLICA_ID  || r->repl_name == NULL)
	{
        return -1;    
	}
    else
	{
        return 0;
	}
}


/* NOTE - this is the only non-api function that performs locking because
   it is called by the event queue */
static void 
_windows_replica_update_state (time_t when, void *arg)
{
	int rc;
	const char *replica_name = (const char *)arg;
	Object *replica_object = NULL;
	Replica *r;
	Slapi_Mod smod;
	LDAPMod *mods[3];
	Slapi_PBlock *pb = NULL;
	char *dn = NULL;

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
		struct berval *vals[2];
		struct berval val;
		LDAPMod mod;

		mods[1] = &mod;

		mod.mod_op   = LDAP_MOD_REPLACE;
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
		repl_get_plugin_identity (PLUGIN_WINDOWS_REPLICATION), 0);
	slapi_modify_internal_pb (pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if (rc != LDAP_SUCCESS) 
	{
		char ebuf[BUFSIZ];
		
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "_replica_update_state: "
			"failed to update state of csn generator for replica %s: LDAP "
			"error - %d\n", escape_string(slapi_sdn_get_dn(r->repl_root),ebuf), rc);
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


/* this function is called during server startup for each replica
   to check whether the replica's data was reloaded offline and
   whether replica's changelog needs to be reinitialized */

/* the function does not use replica lock but all functions it calls are
   thread safe. Locking replica lock while calling changelog functions
   causes a deadlock because changelog calls replica functions that
   that lock the same lock */
int windows_replica_check_for_data_reload (Replica *r, void *arg)
{
    int rc = 0;
    RUV *upper_bound_ruv = NULL;
    RUV *r_ruv = NULL;
    Object *r_obj, *ruv_obj;
    int cl_cover_be, be_cover_cl;

    PR_ASSERT (r);

    /* check that we have a changelog and if this replica logs changes */
    if (cl5GetState () == CL5_STATE_OPEN && r->repl_flags & REPLICA_LOG_CHANGES)
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

            be_cover_cl = ruv_covers_ruv (r_ruv, upper_bound_ruv);
            cl_cover_be = ruv_covers_ruv (upper_bound_ruv, r_ruv);
            if (!cl_cover_be)
            {
                /* the data was reloaded and we can no longer use existing changelog */
				char ebuf[BUFSIZ];

                /* create a temporary replica object to conform to the interface */
                r_obj = object_new (r, NULL);

                /* We can't use existing changelog - remove existing file */
                slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "replica_check_for_data_reload: "
			        "Warning: data for replica %s was reloaded and it no longer matches the data "
                    "in the changelog (replica data %s changelog). Recreating the changelog file. This could affect replication "
                    "with replica's consumers in which case the consumers should be reinitialized.\n",
                    escape_string(slapi_sdn_get_dn(r->repl_root),ebuf),
		((!be_cover_cl && !cl_cover_be) ? "<>" : (!be_cover_cl ? "<" : ">")) );

                rc = cl5DeleteDBSync (r_obj);

                object_release (r_obj);

                if (rc == CL5_SUCCESS)
                {
                    /* log changes to mark starting point for replication */
                    rc = replica_log_ruv_elements (r);
                }
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
      //        consumer5_set_mapping_tree_state_for_replica(r, NULL);
    }

    if (upper_bound_ruv)
        ruv_destroy (&upper_bound_ruv);
        
    return rc;
}

/* This function updates the entry to contain information generated 
   during replica initialization.
   Returns 0 if successful and -1 otherwise */
static int 
__replica_update_entry (Replica *r, Slapi_Entry *e, char *errortext)
{
    int rc;
    Slapi_Mod smod;
    Slapi_Value *val;

    PR_ASSERT (r);

    /* add attribute that stores state of csn generator */
    rc = csngen_get_state ((CSNGen*)object_get_data (r->repl_csngen), &smod);
	if (rc != CSN_SUCCESS)
    {
        sprintf (errortext, "failed to get csn generator's state; csn error - %d", rc);
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
        sprintf (errortext, "failed to update replica entry");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"_replica_update_entry: %s\n", errortext);
		return -1;    
    }

    /* add attribute that stores replica name */
    rc = slapi_entry_add_string (e, attr_replicaName, r->repl_name);
    if (rc != 0)
    {
        sprintf (errortext, "failed to update replica entry");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"_replica_update_entry: %s\n", errortext);
		return -1;    
    }
    else
        r->new_name = PR_FALSE;
    
    return 0;
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
__replica_configure_ruv  (Replica *r, PRBool isLocked)
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
	char ebuf[BUFSIZ];
	
	/* read ruv state from the ruv tombstone entry */
	pb = slapi_pblock_new();
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
				escape_string(slapi_sdn_get_dn(r->repl_root),ebuf));
		    goto done;
	    }
	
	    rc = slapi_entry_attr_find(entries[0], type_ruvElement, &attr);
	    if (rc != 0) /* ruv attribute is missing - this not allowed */
	    {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			    "_replica_configure_ruv: replica ruv tombstone entry for "
			    "replica %s does not contain %s\n", 
				escape_string(slapi_sdn_get_dn(r->repl_root),ebuf), type_ruvElement);
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
					if (rid == 0)
					{
						/* We can not have more than 1 ruv with the same rid
						   so we replace it */
						const char *purl = NULL;

						purl = multimaster_get_local_purl();
						ruv_delete_replica(ruv, r->repl_rid);
						ruv_add_index_replica(ruv, r->repl_rid, purl, 1);
						need_update = 1; /* ruv changed, so write tombstone */
					}
					else /* bug 540844: make sure the local supplier rid is first in the ruv */
					{
						/* make sure local supplier is first in list */
						ReplicaId first_rid = 0;
						char *first_purl = NULL;
						ruv_get_first_id_and_purl(ruv, &first_rid, &first_purl);
						/* if the local supplier is not first in the list . . . */
						if (rid != first_rid)
						{
							/* . . . move the local supplier to the beginning of the list */
							ruv_move_local_supplier_to_first(ruv, rid);
							need_update = 1; /* must update tombstone also */
						}
					}

					/* Update also the directory entry */
					if (need_update) {
						/* richm 20010821 bug 556498
						   replica_replace_ruv_tombstone acquires the repl_lock, so release
						   the lock then reacquire it if locked */
						if (isLocked) PR_Unlock(r->repl_lock);
						replica_replace_ruv_tombstone(r);
						if (isLocked) PR_Lock(r->repl_lock);
					}
				}

			    slapi_ch_free((void **)&generation);
			    return_value = 0;
		    }
            else
            {
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
								"RUV for replica %s is missing replica generation\n",
								escape_string(slapi_sdn_get_dn(r->repl_root),ebuf));
                goto done;
            }		
	    }
	    else
	    {
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"Unable to convert %s attribute in entry %s to a replica update vector.\n",
							type_ruvElement, escape_string(slapi_sdn_get_dn(r->repl_root),ebuf));
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
					escape_string(slapi_sdn_get_dn(r->repl_root),ebuf), rc);
				goto done;
			}
            else
            {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"_replica_configure_ruv: No ruv tombstone found for replica %s. "
					"Created a new one\n",
					escape_string(slapi_sdn_get_dn(r->repl_root),ebuf));
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
								escape_string(slapi_sdn_get_dn(r->repl_root),ebuf), rc);
				slapi_ch_free_string(&state);
				goto done;
			}
			else if (!r->repl_ruv) /* other error */
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
								"_replica_configure_ruv: replication broken for "
								"entry (%s); LDAP error - %d\n",
								escape_string(slapi_sdn_get_dn(r->repl_root),ebuf), rc);
				slapi_ch_free_string(&state);
				goto done;
			}
			else /* some error but continue anyway? */
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"_replica_configure_ruv: Error %d reading tombstone for replica %s.\n",
								rc, escape_string(slapi_sdn_get_dn(r->repl_root),ebuf));
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

/* Update the tombstone entry to reflect the content of the ruv */
static void
replica_replace_ruv_tombstone(Replica *r)
{
    Slapi_PBlock *pb = NULL;
	char *dn;
	int rc;

    Slapi_Mod smod;
    Slapi_Mod smod_last_modified;
    LDAPMod *mods [3];

	PR_ASSERT(NULL != r && NULL != r->repl_root);

    PR_Lock(r->repl_lock);

    PR_ASSERT (r->repl_ruv);
    ruv_to_smod ((RUV*)object_get_data(r->repl_ruv), &smod);
    ruv_last_modified_to_smod ((RUV*)object_get_data(r->repl_ruv), &smod_last_modified);

    dn = _replica_get_config_dn (r->repl_root);
    mods[0] = (LDAPMod*)slapi_mod_get_ldapmod_byref(&smod);
    mods[1] = (LDAPMod*)slapi_mod_get_ldapmod_byref(&smod_last_modified);

    PR_Unlock (r->repl_lock);

    mods [2] = NULL;
    pb = slapi_pblock_new();

    slapi_modify_internal_set_pb(
        pb,
        (char*)slapi_sdn_get_dn (r->repl_root), /* only used to select be */
        mods,
        NULL, /* controls */
        RUV_STORAGE_ENTRY_UNIQUEID,
        repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP);

    slapi_modify_internal_pb (pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    if (rc != LDAP_SUCCESS)
    {
		if ((rc != LDAP_NO_SUCH_OBJECT) || !replica_is_state_flag_set(r, REPLICA_IN_USE))
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_replace_ruv_tombstone: "
				"failed to update replication update vector for replica %s: LDAP "
							"error - %d\n", (char*)slapi_sdn_get_dn (r->repl_root), rc);
		}
    }

    slapi_ch_free ((void**)&dn);
    slapi_pblock_destroy (pb);
    slapi_mod_done (&smod);
    slapi_mod_done (&smod_last_modified);
}


/* 
 * Returns refcounted object that contains RUV. The caller should release the
 * object once it is no longer used. To release, call object_release 
 */
Object *
replica_consumer_get_ruv (const Replica *r)
{
  Object *ruv = NULL;
  
  PR_ASSERT(r);
  //  PR_ASSERT (r->repl_ruv);
  
  object_acquire (r->consumer_repl_ruv);
  ruv = r->consumer_repl_ruv;
  
  return ruv;
}

void
replica_consumer_set_ruv (Replica *r, RUV *ruv) 
{
  PR_ASSERT(r);
  PR_ASSERT(ruv);
  
  if(NULL != r->consumer_repl_ruv)
    {
      object_release(r->consumer_repl_ruv);
    }

  r->consumer_repl_ruv = object_new((void*)ruv, (FNFree)ruv_destroy);
}
