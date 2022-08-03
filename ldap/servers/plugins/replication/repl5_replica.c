/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2022 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* repl5_replica.c */

#include "slapi-plugin.h"
#include "repl5.h"
#include "repl_shared.h"
#include "csnpl.h"
#include "cl5_api.h"
#include "slap.h"

#define RUV_SAVE_INTERVAL (30 * 1000) /* 30 seconds */
#define REPLICA_RDN "cn=replica"

/*
 * A replica is a locally-held copy of a portion of the DIT.
 */
struct replica
{
    Slapi_DN *repl_root;               /* top of the replicated are */
    char *repl_name;                   /* unique replica name */
    PRBool new_name;                   /* new name was generated - need to be saved */
    ReplicaUpdateDNList updatedn_list; /* list of dns with which a supplier should bind to update this replica */
    Slapi_ValueSet *updatedn_groups;   /* set of groups whose memebers are allowed to update replica */
    ReplicaUpdateDNList groupdn_list;  /* exploded listof dns from update group */
    uint32_t updatedn_group_last_check;    /* the time of the last group check */
    int64_t updatedn_group_check_interval; /* the group check interval */
    ReplicaType repl_type;             /* is this replica read-only ? */
    ReplicaId repl_rid;                /* replicaID */
    Object *repl_ruv;                  /* replica update vector */
    CSNPL *min_csn_pl;                 /* Pending list for minimal CSN */
    void *csn_pl_reg_id;               /* registration assignment for csn callbacks */
    unsigned long repl_state_flags;    /* state flags */
    uint32_t repl_flags;               /* persistent, externally visible flags */
    PRMonitor *repl_lock;              /* protects entire structure */
    Slapi_Eq_Context repl_eqcxt_rs;    /* context to cancel event that saves ruv */
    Slapi_Eq_Context repl_eqcxt_tr;    /* context to cancel event that reaps tombstones */
    Slapi_Eq_Context repl_eqcxt_ka_update; /* keep-alive entry update event */
    Object *repl_csngen;               /* CSN generator for this replica */
    PRBool repl_csn_assigned;          /* Flag set when new csn is assigned. */
    int64_t repl_purge_delay;          /* When purgeable, CSNs are held on to for this many extra seconds */
    PRBool tombstone_reap_stop;        /* TRUE when the tombstone reaper should stop */
    PRBool tombstone_reap_active;      /* TRUE when the tombstone reaper is running */
    int64_t tombstone_reap_interval;   /* Time in seconds between tombstone reaping */
    Slapi_ValueSet *repl_referral;     /* A list of administrator provided referral URLs */
    PRBool state_update_inprogress;    /* replica state is being updated */
    PRLock *agmt_lock;                 /* protects agreement creation, start and stop */
    char *locking_purl;                /* supplier who has exclusive access */
    uint64_t locking_conn;             /* The supplier's connection id */
    Slapi_Counter *protocol_timeout;   /* protocol shutdown timeout */
    Slapi_Counter *backoff_min;        /* backoff retry minimum */
    Slapi_Counter *backoff_max;        /* backoff retry maximum */
    Slapi_Counter *precise_purging;    /* Enable precise tombstone purging */
    uint64_t agmt_count;               /* Number of agmts */
    Slapi_Counter *release_timeout;    /* The amount of time to wait before releasing active replica */
    uint64_t abort_session;            /* Abort the current replica session */
    cldb_Handle *cldb;                 /* database info for the changelog */
    int64_t keepalive_update_interval; /* interval to do dummy update to keep RUV fresh */
};


typedef struct reap_callback_data
{
    int rc;
    uint64_t num_entries;
    uint64_t num_purged_entries;
    CSN *purge_csn;
    PRBool *tombstone_reap_stop;
} reap_callback_data;


/* Forward declarations of helper functions*/
static Slapi_Entry *_replica_get_config_entry(const Slapi_DN *root, const char **attrs);
static int _replica_check_validity(const Replica *r);
static int _replica_init_from_config(Replica *r, Slapi_Entry *e, char *errortext);
static int _replica_update_entry(Replica *r, Slapi_Entry *e, char *errortext);
static int _replica_config_changelog(Replica *r);
static int _replica_configure_ruv(Replica *r, PRBool isLocked);
static char *_replica_get_config_dn(const Slapi_DN *root);
static char *_replica_type_as_string(const Replica *r);
/* DBDB, I think this is probably bogus : */
static int replica_create_ruv_tombstone(Replica *r);
static void assign_csn_callback(const CSN *csn, void *data);
static void abort_csn_callback(const CSN *csn, void *data);
static void eq_cb_reap_tombstones(time_t when, void *arg);
static CSN *_replica_get_purge_csn_nolock(const Replica *r);
static void replica_get_referrals_nolock(const Replica *r, char ***referrals);
static int replica_log_ruv_elements_nolock(const Replica *r);
static void replica_replace_ruv_tombstone(Replica *r);
static void start_agreements_for_replica(Replica *r, PRBool start);
static void _delete_tombstone(const char *tombstone_dn, const char *uniqueid, int ext_op_flags);
static void replica_strip_cleaned_rids(Replica *r);

static void
replica_lock(PRMonitor *lock)
{
    PR_EnterMonitor(lock);
}

static void
replica_unlock(PRMonitor *lock)
{
    PR_ExitMonitor(lock);
}
/*
 * Allocates new replica and reads its state and state of its component from
 * various parts of the DIT.
 */
Replica *
replica_new(const Slapi_DN *root)
{
    Replica *r = NULL;
    Slapi_Entry *e = NULL;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];

    PR_ASSERT(root);

    /* check if there is a replica associated with the tree */
    e = _replica_get_config_entry(root, NULL);
    if (e) {
        errorbuf[0] = '\0';
        replica_new_from_entry(e, errorbuf,
                               PR_FALSE, /* not a newly added entry */
                               &r);

        if (NULL == r) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "replica_new - Unable to configure replica %s: %s\n",
                          slapi_sdn_get_dn(root), errorbuf);
        }
        slapi_entry_free(e);
    }

    return r;
}

/* constructs the replica object from the newly added entry */
int
replica_new_from_entry(Slapi_Entry *e, char *errortext, PRBool is_add_operation, Replica **rp)
{
    Replica *r;
    int rc = LDAP_SUCCESS;

    if (e == NULL) {
        if (NULL != errortext) {
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "NULL entry");
        }
        return LDAP_OTHER;
    }

    r = (Replica *)slapi_ch_calloc(1, sizeof(Replica));

    if (!r) {
        if (NULL != errortext) {
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Out of memory");
        }
        rc = LDAP_OTHER;
        goto done;
    }

    if ((r->repl_lock = PR_NewMonitor()) == NULL) {
        if (NULL != errortext) {
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to create replica lock");
        }
        rc = LDAP_OTHER;
        goto done;
    }

    if ((r->agmt_lock = PR_NewLock()) == NULL) {
        if (NULL != errortext) {
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "failed to create replica lock");
        }
        rc = LDAP_OTHER;
        goto done;
    }

    /* init the slapi_counter/atomic settings */
    r->protocol_timeout = slapi_counter_new();
    r->release_timeout = slapi_counter_new();
    r->backoff_min = slapi_counter_new();
    r->backoff_max = slapi_counter_new();
    r->precise_purging = slapi_counter_new();

    /* read parameters from the replica config entry */
    rc = _replica_init_from_config(r, e, errortext);
    if (rc != LDAP_SUCCESS) {
        goto done;
    }

    /* configure ruv */
    rc = _replica_configure_ruv(r, PR_FALSE);
    if (rc != 0) {
        rc = LDAP_OTHER;
        goto done;
    } else {
        rc = LDAP_SUCCESS;
    }


    /* If smallest csn exists in RUV for our local replica, it's ok to begin iteration */
    PR_ASSERT(object_get_data(r->repl_ruv));

    if (is_add_operation) {
        /*
         * This is called by an ldap add operation.
         * Update the entry to contain information generated
         * during replica initialization
         */
        rc = _replica_update_entry(r, e, errortext);
        /* add changelog config entry to config 
         * this is only needed for replicas logging changes,
         * but for now let it exist for all replicas. Makes handling
         * of changing replica flags easier
         */
        _replica_config_changelog(r);
        if (r->repl_flags & REPLICA_LOG_CHANGES) {
            /* Init changelog db file */
            cldb_SetReplicaDB(r, NULL);
        }
    } else {
        /*
         * Entry is already in dse.ldif - update it on the disk
         * (done by the update state event scheduled below)
         */
    }
    if (rc != 0) {
        rc = LDAP_OTHER;
        goto done;
    } else {
        rc = LDAP_SUCCESS;
    }

    /* ONREPL - the state update can occur before the entry is added to the DIT.
       In that case the updated would fail but nothing bad would happen. The next
       scheduled update would save the state */
    r->repl_eqcxt_rs = slapi_eq_repeat_rel(replica_update_state, r->repl_name,
                                           slapi_current_rel_time_t() + START_UPDATE_DELAY,
                                           RUV_SAVE_INTERVAL);

    /* create supplier update event */
    if (r->repl_eqcxt_ka_update == NULL && replica_get_type(r) == REPLICA_TYPE_UPDATABLE) {
        r->repl_eqcxt_ka_update = slapi_eq_repeat_rel(replica_subentry_update, r,
                                                   slapi_current_rel_time_t() + START_UPDATE_DELAY,
                                                   replica_get_keepalive_update_interval(r));
    }

    if (r->tombstone_reap_interval > 0) {
        /*
         * Reap Tombstone should be started some time after the plugin started.
         * This will allow the server to fully start before consuming resources.
         */
        r->repl_eqcxt_tr = slapi_eq_repeat_rel(eq_cb_reap_tombstones, r->repl_name,
                                               slapi_current_rel_time_t() + r->tombstone_reap_interval,
                                               1000 * r->tombstone_reap_interval);
    }

done:
    if (rc != LDAP_SUCCESS && r) {
        replica_destroy((void **)&r);
    }

    *rp = r;
    return rc;
}


void
replica_flush(Replica *r)
{
    PR_ASSERT(NULL != r);
    if (NULL != r) {
        replica_lock(r->repl_lock);
        /* Make sure we dump the CSNGen state */
        r->repl_csn_assigned = PR_TRUE;
        replica_unlock(r->repl_lock);
        /* This function take the Lock Inside */
        /* And also write the RUV */
        replica_update_state((time_t)0, r->repl_name);
    }
}

void
replica_set_csn_assigned(Replica *r)
{
    replica_lock(r->repl_lock);
    r->repl_csn_assigned = PR_TRUE;
    replica_unlock(r->repl_lock);
}

/*
 * Deallocate a replica. arg should point to the address of a
 * pointer that points to a replica structure.
 */
void
replica_destroy(void **arg)
{
    Replica *r;

    if (arg == NULL)
        return;

    r = *((Replica **)arg);

    PR_ASSERT(r);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_destroy\n");

    /*
     * The function will not be called unless the refcnt of its
     * wrapper object is 0. Hopefully this refcnt could sync up
     * this destruction and the events such as tombstone reap
     * and ruv updates.
     */

    if (r->repl_eqcxt_ka_update) {
        slapi_eq_cancel_rel(r->repl_eqcxt_ka_update);
        r->repl_eqcxt_ka_update = NULL;
    }

    if (r->repl_eqcxt_rs) {
        slapi_eq_cancel_rel(r->repl_eqcxt_rs);
        r->repl_eqcxt_rs = NULL;
    }

    if (r->repl_eqcxt_tr) {
        slapi_eq_cancel_rel(r->repl_eqcxt_tr);
        r->repl_eqcxt_tr = NULL;
    }

    if (r->repl_root) {
        slapi_sdn_free(&r->repl_root);
    }

    slapi_ch_free_string(&r->locking_purl);

    if (r->updatedn_list) {
        replica_updatedn_list_free(r->updatedn_list);
        r->updatedn_list = NULL;
    }

    if (r->groupdn_list) {
        replica_updatedn_list_free(r->groupdn_list);
        r->groupdn_list = NULL;
    }
    if (r->updatedn_groups) {
        slapi_valueset_free(r->updatedn_groups);
    }

    /* slapi_ch_free accepts NULL pointer */
    slapi_ch_free((void **)&r->repl_name);

    if (r->repl_lock) {
        PR_DestroyMonitor(r->repl_lock);
        r->repl_lock = NULL;
    }

    if (r->agmt_lock) {
        PR_DestroyLock(r->agmt_lock);
        r->agmt_lock = NULL;
    }

    if (NULL != r->repl_ruv) {
        object_release(r->repl_ruv);
    }

    if (NULL != r->repl_csngen) {
        if (r->csn_pl_reg_id) {
            csngen_unregister_callbacks((CSNGen *)object_get_data(r->repl_csngen), r->csn_pl_reg_id);
        }
        object_release(r->repl_csngen);
    }

    if (NULL != r->repl_referral) {
        slapi_valueset_free(r->repl_referral);
    }

    if (NULL != r->min_csn_pl) {
        csnplFree(&r->min_csn_pl);
        ;
    }

    slapi_counter_destroy(&r->protocol_timeout);
    slapi_counter_destroy(&r->release_timeout);
    slapi_counter_destroy(&r->backoff_min);
    slapi_counter_destroy(&r->backoff_max);
    slapi_counter_destroy(&r->precise_purging);

    slapi_ch_free((void **)arg);
}

/******************************************************************************
 ******************** REPLICATION KEEP ALIVE ENTRIES **************************
 ******************************************************************************
 * They are subentries of the replicated suffix and there is one per supplier.  *
 * These entries exist only to trigger a change that get replicated over the  *
 * topology.                                                                  *
 * Their main purpose is to generate records in the changelog and they are    *
 * updated from time to time by fractional replication to insure that at      *
 * least a change must be replicated by FR after a great number of not        *
 * replicated changes are found in the changelog. The interest is that the    *
 * fractional RUV get then updated so less changes need to be walked in the   *
 * changelog when searching for the first change to send                      *
 ******************************************************************************/

#define KEEP_ALIVE_ATTR "keepalivetimestamp"
#define KEEP_ALIVE_ENTRY "repl keep alive"
#define KEEP_ALIVE_DN_FORMAT "cn=%s %d,%s"


static int
replica_subentry_create(const char *repl_root, ReplicaId rid)
{
    char *entry_string = NULL;
    Slapi_Entry *e = NULL;
    Slapi_PBlock *pb = NULL;
    int return_value;
    int rc = 0;

    entry_string = slapi_ch_smprintf("dn: cn=%s %d,%s\nobjectclass: top\nobjectclass: ldapsubentry\nobjectclass: extensibleObject\ncn: %s %d",
                                     KEEP_ALIVE_ENTRY, rid, repl_root, KEEP_ALIVE_ENTRY, rid);
    if (entry_string == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_subentry_create - Failed in slapi_ch_smprintf\n");
        rc = -1;
        goto done;
    }

    slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name,
                  "replica_subentry_create - add %s\n", entry_string);
    e = slapi_str2entry(entry_string, 0);

    /* create the entry */
    pb = slapi_pblock_new();


    slapi_add_entry_internal_set_pb(pb, e, NULL, /* controls */
                                    repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0 /* flags */);
    slapi_add_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &return_value);
    if (return_value != LDAP_SUCCESS &&
        return_value != LDAP_ALREADY_EXISTS &&
        return_value != LDAP_REFERRAL /* CONSUMER */) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_subentry_create - Unable to "
                                                       "create replication keep alive entry %s: error %d - %s\n",
                      slapi_entry_get_dn_const(e),
                      return_value, ldap_err2string(return_value));
        rc = -1;
        goto done;
    }

done:

    slapi_pblock_destroy(pb);
    slapi_ch_free_string(&entry_string);
    return rc;
}

int
replica_subentry_check(const char *repl_root, ReplicaId rid)
{
    Slapi_PBlock *pb;
    char *filter = NULL;
    Slapi_Entry **entries = NULL;
    int res;
    int rc = 0;

    pb = slapi_pblock_new();
    filter = slapi_ch_smprintf("(&(objectclass=ldapsubentry)(cn=%s %d))", KEEP_ALIVE_ENTRY, rid);
    slapi_search_internal_set_pb(pb, repl_root, LDAP_SCOPE_ONELEVEL,
                                 filter, NULL, 0, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
    if (res == LDAP_SUCCESS) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries && (entries[0] == NULL)) {
            slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name,
                          "replica_subentry_check - Need to create replication keep alive entry <cn=%s %d,%s>\n",
                          KEEP_ALIVE_ENTRY, rid, repl_root);
            rc = replica_subentry_create(repl_root, rid);
        } else {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "replica_subentry_check - replication keep alive entry <cn=%s %d,%s> already exists\n",
                          KEEP_ALIVE_ENTRY, rid, repl_root);
            rc = 0;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_subentry_check - Error accessing replication keep alive entry <cn=%s %d,%s> res=%d\n",
                      KEEP_ALIVE_ENTRY, rid, repl_root, res);
        /* The status of the entry is not clear, do not attempt to create it */
        rc = 1;
    }
    slapi_free_search_results_internal(pb);

    slapi_pblock_destroy(pb);
    slapi_ch_free_string(&filter);
    return rc;
}

void
replica_subentry_update(time_t when __attribute__((unused)), void *arg)
{
    Slapi_PBlock *modpb = NULL;
    Replica *replica = (Replica *)arg;
    ReplicaId rid;
    LDAPMod *mods[2];
    LDAPMod mod;
    struct berval *vals[2];
    struct berval val;
    const char *repl_root = NULL;
    char buf[SLAPI_TIMESTAMP_BUFSIZE];
    char *dn = NULL;
    int ldrc = 0;

    rid = replica_get_rid(replica);
    repl_root = slapi_ch_strdup(slapi_sdn_get_dn(replica_get_root(replica)));
    replica_subentry_check(repl_root, rid);

    slapi_timestamp_utc_hr(buf, SLAPI_TIMESTAMP_BUFSIZE);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_subentry_update called at %s\n", buf);
    val.bv_val = buf;
    val.bv_len = strlen(val.bv_val);
    vals[0] = &val;
    vals[1] = NULL;

    mod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
    mod.mod_type = KEEP_ALIVE_ATTR;
    mod.mod_bvalues = vals;
    mods[0] = &mod;
    mods[1] = NULL;

    modpb = slapi_pblock_new();
    dn = slapi_ch_smprintf(KEEP_ALIVE_DN_FORMAT, KEEP_ALIVE_ENTRY, rid, repl_root);
    slapi_modify_internal_set_pb(modpb, dn, mods, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_modify_internal_pb(modpb);
    slapi_pblock_get(modpb, SLAPI_PLUGIN_INTOP_RESULT, &ldrc);
    if (ldrc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_subentry_update - "
                      "Failure (%d) to update replication keep alive entry \"%s: %s\"\n",
                      ldrc, KEEP_ALIVE_ATTR, buf);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                      "replica_subentry_update - "
                      "Successful update of replication keep alive entry \"%s: %s\"\n",
                      KEEP_ALIVE_ATTR, buf);
    }

    slapi_pblock_destroy(modpb);
    slapi_ch_free_string((char **)&repl_root);
    slapi_ch_free_string(&dn);
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
replica_get_exclusive_access(Replica *r, PRBool *isInc, uint64_t connid, int opid, const char *locking_purl, char **current_purl)
{
    PRBool rval = PR_TRUE;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);
    if (r->repl_state_flags & REPLICA_IN_USE) {
        if (isInc)
            *isInc = (r->repl_state_flags & REPLICA_INCREMENTAL_IN_PROGRESS);

        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_get_exclusive_access - "
                      "conn=%" PRIu64 " op=%d repl=\"%s\": "
                      "Replica in use locking_purl=%s\n",
                      connid, opid,
                      slapi_sdn_get_dn(r->repl_root),
                      r->locking_purl ? r->locking_purl : "unknown");
        rval = PR_FALSE;
        if (!(r->repl_state_flags & REPLICA_TOTAL_IN_PROGRESS)) {
            /* inc update */
            if (r->locking_purl && r->locking_conn == connid) {
                /* This is the same supplier connection, reset the replica
                 * purl, and return success */
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "replica_get_exclusive_access - "
                              "This is a second acquire attempt from the same replica connection "
                              " - return success instead of busy\n");
                slapi_ch_free_string(&r->locking_purl);
                r->locking_purl = slapi_ch_strdup(locking_purl);
                rval = PR_TRUE;
                goto done;
            }
            if (replica_get_release_timeout(r)) {
                /*
                 * Abort the current session so other replicas can acquire
                 * this server.
                 */
                r->abort_session = ABORT_SESSION;
            }
        }
        if (current_purl) {
            *current_purl = slapi_ch_strdup(r->locking_purl);
        }
    } else {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_get_exclusive_access - "
                      "conn=%" PRIu64 " op=%d repl=\"%s\": Acquired replica\n",
                      connid, opid,
                      slapi_sdn_get_dn(r->repl_root));
        r->repl_state_flags |= REPLICA_IN_USE;
        r->abort_session = SESSION_ACQUIRED;
        if (isInc && *isInc) {
            r->repl_state_flags |= REPLICA_INCREMENTAL_IN_PROGRESS;
        } else {
            /*
             * If connid or opid != 0, it's a total update.
             * Both set to 0 means we're disabling replication
             */
            if (connid || opid) {
                r->repl_state_flags |= REPLICA_TOTAL_IN_PROGRESS;
            }
        }
        slapi_ch_free_string(&r->locking_purl);
        r->locking_purl = slapi_ch_strdup(locking_purl);
        r->locking_conn = connid;
    }
done:
    replica_unlock(r->repl_lock);
    return rval;
}

/*
 * Relinquish exclusive access to the replica
 */
void
replica_relinquish_exclusive_access(Replica *r, uint64_t connid, int opid)
{
    PRBool isInc;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);
    isInc = (r->repl_state_flags & REPLICA_INCREMENTAL_IN_PROGRESS);
    /* check to see if the replica is in use and log a warning if not */
    if (!(r->repl_state_flags & REPLICA_IN_USE)) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_relinquish_exclusive_access - "
                      "conn=%" PRIu64 " op=%d repl=\"%s\": "
                      "Replica not in use\n",
                      connid, opid, slapi_sdn_get_dn(r->repl_root));
    } else {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_relinquish_exclusive_access - "
                      "conn=%" PRIu64 " op=%d repl=\"%s\": "
                      "Released replica held by locking_purl=%s\n",
                      connid, opid,
                      slapi_sdn_get_dn(r->repl_root), r->locking_purl);

        slapi_ch_free_string(&r->locking_purl);
        r->repl_state_flags &= ~(REPLICA_IN_USE);
        if (isInc)
            r->repl_state_flags &= ~(REPLICA_INCREMENTAL_IN_PROGRESS);
        else
            r->repl_state_flags &= ~(REPLICA_TOTAL_IN_PROGRESS);
    }
    replica_unlock(r->repl_lock);
}

/*
 * Returns root of the replicated area
 */
PRBool
replica_get_tombstone_reap_active(const Replica *r)
{
    PR_ASSERT(r);

    return (r->tombstone_reap_active);
}

/*
 * Returns root of the replicated area
 */
const Slapi_DN *
replica_get_root(const Replica *r) /* ONREPL - should we return copy instead? */
{
    PR_ASSERT(r);

    /* replica root never changes so we don't have to lock */
    return (r->repl_root);
}

/*
 * Returns normalized dn of the root of the replicated area
 */
const char *
replica_get_name(const Replica *r) /* ONREPL - should we return copy instead? */
{
    PR_ASSERT(r);

    /* replica name never changes so we don't have to lock */
    return (r->repl_name);
}

/*
 * Returns locking_conn of this replica
 */
uint64_t
replica_get_locking_conn(const Replica *r)
{
    uint64_t connid;
    replica_lock(r->repl_lock);
    connid = r->locking_conn;
    replica_unlock(r->repl_lock);
    return connid;
}
/*
 * Returns replicaid of this replica
 */
ReplicaId
replica_get_rid(const Replica *r)
{
    ReplicaId rid;
    PR_ASSERT(r);

    replica_lock(r->repl_lock);
    rid = r->repl_rid;
    replica_unlock(r->repl_lock);
    return rid;
}

/*
 * Sets replicaid of this replica - should only be used when also changing the type
 */
void
replica_set_rid(Replica *r, ReplicaId rid)
{
    PR_ASSERT(r);

    replica_lock(r->repl_lock);
    r->repl_rid = rid;
    replica_unlock(r->repl_lock);
}

/* Returns true if replica was initialized through ORC or import;
 * otherwise, false. An uninitialized replica should return
 * LDAP_UNWILLING_TO_PERFORM to all client requests
 */
PRBool
replica_is_initialized(const Replica *r)
{
    PR_ASSERT(r);
    return (r->repl_ruv != NULL);
}

/*
 * Returns refcounted object that contains RUV. The caller should release the
 * object once it is no longer used. To release, call object_release
 */
Object *
replica_get_ruv(const Replica *r)
{
    Object *ruv = NULL;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    PR_ASSERT(r->repl_ruv);

    object_acquire(r->repl_ruv);

    ruv = r->repl_ruv;

    replica_unlock(r->repl_lock);

    return ruv;
}

/*
 * Sets RUV vector. This function should be called during replica
 * (re)initialization. During normal operation, the RUV is read from
 * the root of the replicated in the replica_new call
 */
void
replica_set_ruv(Replica *r, RUV *ruv)
{
    PR_ASSERT(r && ruv);

    replica_lock(r->repl_lock);

    if (NULL != r->repl_ruv) {
        object_release(r->repl_ruv);
    }

    /* if the local replica is not in the RUV and it is writable - add it
       and reinitialize min_csn pending list */
    if (r->repl_type == REPLICA_TYPE_UPDATABLE) {
        CSN *csn = NULL;
        if (r->min_csn_pl)
            csnplFree(&r->min_csn_pl);

        if (ruv_contains_replica(ruv, r->repl_rid)) {
            ruv_get_smallest_csn_for_replica(ruv, r->repl_rid, &csn);
            if (csn)
                csn_free(&csn);
            else
                r->min_csn_pl = csnplNew();
            /* We need to make sure the local ruv element is the 1st. */
            ruv_move_local_supplier_to_first(ruv, r->repl_rid);
        } else {
            r->min_csn_pl = csnplNew();
            /* To be sure that the local is in first */
            ruv_add_index_replica(ruv, r->repl_rid, multisupplier_get_local_purl(), 1);
        }
    }

    r->repl_ruv = object_new((void *)ruv, (FNFree)ruv_destroy);

    if (r->repl_flags & REPLICA_LOG_CHANGES) {
        cl5NotifyRUVChange(r);
    }

    replica_unlock(r->repl_lock);
}

/*
 * Check if replica generation is the same than the remote ruv one
 */
int
replica_check_generation(Replica *r, const RUV *remote_ruv)
{
    int return_value;
    char *local_gen = NULL;
    char *remote_gen = ruv_get_replica_generation(remote_ruv);
    Object *local_ruv_obj;
    RUV *local_ruv;

    PR_ASSERT(NULL != r);
    local_ruv_obj = replica_get_ruv(r);
    if (NULL != local_ruv_obj) {
        local_ruv = (RUV *)object_get_data(local_ruv_obj);
        PR_ASSERT(local_ruv);
        local_gen = ruv_get_replica_generation(local_ruv);
        object_release(local_ruv_obj);
    }
    if (NULL == remote_gen || NULL == local_gen || strcmp(remote_gen, local_gen) != 0) {
        return_value = PR_FALSE;
    } else {
        return_value = PR_TRUE;
    }
    slapi_ch_free_string(&remote_gen);
    slapi_ch_free_string(&local_gen);
    return return_value;
}

/*
 * Update one particular CSN in an RUV. This is meant to be called
 * whenever (a) the server has processed a client operation and
 * needs to update its CSN, or (b) the server is completing an
 * inbound replication session operation, and needs to update its
 * local RUV.
 */
int
replica_update_ruv(Replica *r, const CSN *updated_csn, const char *replica_purl)
{
    char csn_str[CSN_STRSIZE];
    int rc = RUV_SUCCESS;

    PR_ASSERT(NULL != r);
    PR_ASSERT(NULL != updated_csn);
#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "replica_update_ruv: csn %s\n",
                  csn_as_string(updated_csn, PR_FALSE, csn_str)); /* XXXggood remove debugging */
#endif
    if (NULL == r) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_update_ruv - Replica "
                                                       "is NULL\n");
        rc = RUV_BAD_DATA;
    } else if (NULL == updated_csn) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_update_ruv - csn "
                                                       "is NULL when updating replica %s\n",
                      slapi_sdn_get_dn(r->repl_root));
        rc = RUV_BAD_DATA;
    } else {
        RUV *ruv;
        replica_lock(r->repl_lock);

        if (r->repl_ruv != NULL) {
            ruv = object_get_data(r->repl_ruv);
            if (NULL != ruv) {
                ReplicaId rid = csn_get_replicaid(updated_csn);
                if (rid == r->repl_rid) {
                    if (NULL != r->min_csn_pl) {
                        CSN *min_csn;
                        PRBool committed;
                        (void)csnplCommit(r->min_csn_pl, updated_csn);
                        min_csn = csnplGetMinCSN(r->min_csn_pl, &committed);
                        if (NULL != min_csn) {
                            if (committed) {
                                ruv_set_min_csn(ruv, min_csn, replica_purl);
                                csnplFree(&r->min_csn_pl);
                            }
                            csn_free(&min_csn);
                        }
                    }
                }
                /* Update max csn for local and remote replicas */
                rc = ruv_update_ruv(ruv, updated_csn, replica_purl, r, r->repl_rid);
                if (RUV_COVERS_CSN == rc) {
                    slapi_log_err(SLAPI_LOG_REPL,
                                  repl_plugin_name, "replica_update_ruv - RUV "
                                                    "for replica %s already covers max_csn = %s\n",
                                  slapi_sdn_get_dn(r->repl_root),
                                  csn_as_string(updated_csn, PR_FALSE, csn_str));
                    /* RUV is not dirty - no write needed */
                } else if (RUV_SUCCESS != rc) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  repl_plugin_name, "replica_update_ruv - Unable "
                                                    "to update RUV for replica %s, csn = %s\n",
                                  slapi_sdn_get_dn(r->repl_root),
                                  csn_as_string(updated_csn, PR_FALSE, csn_str));
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "replica_update_ruv - Unable to get RUV object for replica "
                              "%s\n",
                              slapi_sdn_get_dn(r->repl_root));
                rc = RUV_NOTFOUND;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_update_ruv - "
                                                           "Unable to initialize RUV for replica %s\n",
                          slapi_sdn_get_dn(r->repl_root));
            rc = RUV_NOTFOUND;
        }
        replica_unlock(r->repl_lock);
    }
    return rc;
}

/*
 * Returns refcounted object that contains csn generator. The caller should release the
 * object once it is no longer used. To release, call object_release
 */
Object *
replica_get_csngen(const Replica *r)
{
    Object *csngen;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    object_acquire(r->repl_csngen);
    csngen = r->repl_csngen;

    replica_unlock(r->repl_lock);

    return csngen;
}

/*
 * Returns the replica type.
 */
ReplicaType
replica_get_type(const Replica *r)
{
    PR_ASSERT(r);
    return r->repl_type;
}

uint64_t
replica_get_protocol_timeout(Replica *r)
{
    if (r) {
        return slapi_counter_get_value(r->protocol_timeout);
    } else {
        return 0;
    }
}

uint64_t
replica_get_release_timeout(Replica *r)
{
    if (r) {
        return slapi_counter_get_value(r->release_timeout);
    } else {
        return 0;
    }
}

void
replica_set_release_timeout(Replica *r, uint64_t limit)
{
    if (r) {
        slapi_counter_set_value(r->release_timeout, limit);
    }
}

void
replica_set_protocol_timeout(Replica *r, uint64_t timeout)
{
    if (r) {
        slapi_counter_set_value(r->protocol_timeout, timeout);
    }
}

void
replica_set_groupdn_checkinterval(Replica *r, int interval)
{
    if (r) {
        r->updatedn_group_check_interval = interval;
    }
}

/*
 * Sets the replica type.
 */
void
replica_set_type(Replica *r, ReplicaType type)
{
    PR_ASSERT(r);

    replica_lock(r->repl_lock);
    r->repl_type = type;
    replica_unlock(r->repl_lock);
}

static PRBool
valuesets_equal(Slapi_ValueSet *new_dn_groups, Slapi_ValueSet *old_dn_groups)
{
    Slapi_Attr *attr = NULL;
    Slapi_Value *val = NULL;
    int idx = 0;
    PRBool rc = PR_TRUE;

    if (new_dn_groups == NULL) {
        if (old_dn_groups == NULL)
            return PR_TRUE;
        else
            return PR_FALSE;
    }
    if (old_dn_groups == NULL) {
        return PR_FALSE;
    }

    /* if there is not the same number of value, no need to check the value themselves */
    if (new_dn_groups->num != old_dn_groups->num) {
        return PR_FALSE;
    }

    attr = slapi_attr_new();
    slapi_attr_init(attr, attr_replicaBindDnGroup);

    /* Check that all values in old_dn_groups also exist in new_dn_groups */
    for (idx = slapi_valueset_first_value(old_dn_groups, &val);
         val && (idx != -1);
         idx = slapi_valueset_next_value(old_dn_groups, idx, &val)) {
        if (slapi_valueset_find(attr, new_dn_groups, val) == NULL) {
            rc = PR_FALSE;
            break;
        }
    }
    slapi_attr_free(&attr);
    return rc;
}
/*
 * Returns true if sdn is the same as updatedn and false otherwise
 */
PRBool
replica_is_updatedn(Replica *r, const Slapi_DN *sdn)
{
    PRBool result = PR_FALSE;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    if ((r->updatedn_list == NULL) && (r->groupdn_list == NULL)) {
        if (sdn == NULL) {
            result = PR_TRUE;
        } else {
            result = PR_FALSE;
        }
        replica_unlock(r->repl_lock);
        return result;
    }

    if (r->updatedn_list) {
        result = replica_updatedn_list_ismember(r->updatedn_list, sdn);
        if (result == PR_TRUE) {
            /* sdn is present in the updatedn_list */
            replica_unlock(r->repl_lock);
            return result;
        }
    }

    if (r->groupdn_list) {
        /* check and rebuild groupdns */
        if (r->updatedn_group_check_interval > -1) {
            time_t now = slapi_current_rel_time_t();
            if (now - r->updatedn_group_last_check > r->updatedn_group_check_interval) {
                Slapi_ValueSet *updatedn_groups_copy = NULL;
                ReplicaUpdateDNList groupdn_list = replica_updatedn_list_new(NULL);

                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "Authorized replication managers is resync (%ld)\n", now);
                updatedn_groups_copy = slapi_valueset_new();
                slapi_valueset_set_valueset(updatedn_groups_copy, r->updatedn_groups);
                r->updatedn_group_last_check = now; /* Just to be sure no one will try to reload */

                /* It can do internal searches, to avoid deadlock release the replica lock
                 * as we are working on local variables
                 */
                replica_unlock(r->repl_lock);
                replica_updatedn_list_group_replace(groupdn_list, updatedn_groups_copy);
                replica_lock(r->repl_lock);

                if (valuesets_equal(r->updatedn_groups, updatedn_groups_copy)) {
                    /* the updatedn_groups has not been updated while we release the replica
                     * this is fine to apply the groupdn_list
                     */
                    replica_updatedn_list_delete(r->groupdn_list, NULL);
                    replica_updatedn_list_free(r->groupdn_list);
                    r->groupdn_list = groupdn_list;
                } else {
                    /* the unpdatedn_groups has been updated while we released the replica
                     * groupdn_list in the replica is up to date. Do not replace it
                     */
                    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "Authorized replication managers (%s) was updated during a refresh\n", attr_replicaBindDnGroup);
                    replica_updatedn_list_delete(groupdn_list, NULL);
                    replica_updatedn_list_free(groupdn_list);
                }
                slapi_valueset_free(updatedn_groups_copy);
            }
        }
        result = replica_updatedn_list_ismember(r->groupdn_list, sdn);
    }

    replica_unlock(r->repl_lock);

    return result;
}
/*
 * Sets updatedn list for this replica
 */
void
replica_set_updatedn(Replica *r, const Slapi_ValueSet *vs, int mod_op)
{
    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    if (!r->updatedn_list)
        r->updatedn_list = replica_updatedn_list_new(NULL);

    if (SLAPI_IS_MOD_DELETE(mod_op) || vs == NULL ||
        (0 == slapi_valueset_count(vs))) /* null value also causes list deletion */
        replica_updatedn_list_delete(r->updatedn_list, vs);
    else if (SLAPI_IS_MOD_REPLACE(mod_op))
        replica_updatedn_list_replace(r->updatedn_list, vs);
    else if (SLAPI_IS_MOD_ADD(mod_op))
        replica_updatedn_list_add(r->updatedn_list, vs);

    replica_unlock(r->repl_lock);
}

/*
 * Sets updatedn list for this replica
 */
void
replica_set_groupdn(Replica *r, const Slapi_ValueSet *vs, int mod_op)
{
    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    if (!r->groupdn_list)
        r->groupdn_list = replica_updatedn_list_new(NULL);
    if (!r->updatedn_groups)
        r->updatedn_groups = slapi_valueset_new();

    if (SLAPI_IS_MOD_DELETE(mod_op) || vs == NULL ||
        (0 == slapi_valueset_count(vs))) {
        /* null value also causes list deletion */
        slapi_valueset_free(r->updatedn_groups);
        r->updatedn_groups = NULL;
        replica_updatedn_list_delete(r->groupdn_list, vs);
    } else if (SLAPI_IS_MOD_REPLACE(mod_op)) {
        if (r->updatedn_groups) {
            slapi_valueset_done(r->updatedn_groups);
        } else {
            r->updatedn_groups = slapi_valueset_new();
        }
        slapi_valueset_set_valueset(r->updatedn_groups, vs);
        replica_updatedn_list_group_replace(r->groupdn_list, vs);
    } else if (SLAPI_IS_MOD_ADD(mod_op)) {
        if (!r->updatedn_groups) {
            r->updatedn_groups = slapi_valueset_new();
        }
        slapi_valueset_join_attr_valueset(NULL, r->updatedn_groups, vs);
        replica_updatedn_list_add_ext(r->groupdn_list, vs, 1);
    }
    replica_unlock(r->repl_lock);
}

void
replica_reset_csn_pl(Replica *r)
{
    replica_lock(r->repl_lock);

    if (NULL != r->min_csn_pl) {
        csnplFree(&r->min_csn_pl);
    }
    r->min_csn_pl = csnplNew();

    replica_unlock(r->repl_lock);
}

/* gets current replica generation for this replica */
char *
replica_get_generation(const Replica *r)
{
    int rc = 0;
    char *gen = NULL;

    if (r && r->repl_ruv) {
        replica_lock(r->repl_lock);

        if (rc == 0)
            gen = ruv_get_replica_generation((RUV *)object_get_data(r->repl_ruv));

        replica_unlock(r->repl_lock);
    }

    return gen;
}

PRBool
replica_is_flag_set(const Replica *r, uint32_t flag)
{
    if (r)
        return (r->repl_flags & flag);
    else
        return PR_FALSE;
}

void
replica_set_flag(Replica *r, uint32_t flag, PRBool clear)
{
    if (r == NULL)
        return;

    replica_lock(r->repl_lock);

    if (clear) {
        r->repl_flags &= ~flag;
    } else {
        r->repl_flags |= flag;
    }

    replica_unlock(r->repl_lock);
}

void
replica_replace_flags(Replica *r, uint32_t flags)
{
    if (r) {
        replica_lock(r->repl_lock);
        r->repl_flags = flags;
        replica_unlock(r->repl_lock);
    }
}

void
replica_get_referrals(const Replica *r, char ***referrals)
{
    replica_lock(r->repl_lock);
    replica_get_referrals_nolock(r, referrals);
    replica_unlock(r->repl_lock);
}

void
replica_set_referrals(Replica *r, const Slapi_ValueSet *vs)
{
    int ii = 0;
    Slapi_Value *vv = NULL;
    if (r->repl_referral == NULL) {
        r->repl_referral = slapi_valueset_new();
    } else {
        slapi_valueset_done(r->repl_referral);
    }
    slapi_valueset_set_valueset(r->repl_referral, vs);
    /* make sure the DN is included in the referral LDAP URL */
    if (r->repl_referral) {
        Slapi_ValueSet *newvs = slapi_valueset_new();
        const char *repl_root = slapi_sdn_get_dn(r->repl_root);
        ii = slapi_valueset_first_value(r->repl_referral, &vv);
        while (vv) {
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
                if (ref[len - 1] != '/') {
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
replica_update_csngen_state_ext(Replica *r, const RUV *ruv, const CSN *extracsn)
{
    int rc = 0;
    CSNGen *gen;
    CSN *csn = NULL;

    PR_ASSERT(r && ruv);

    if (!replica_check_generation(r, ruv)) /* ruv has wrong generation - we are done */
    {
        return 0;
    }

    rc = ruv_get_max_csn(ruv, &csn);
    if (rc != RUV_SUCCESS) {
        return -1;
    }

    if ((csn == NULL) && (extracsn == NULL)) /* ruv contains no csn and no extra - we are done */
    {
        return 0;
    }

    if (csn_compare(extracsn, csn) > 0) /* extracsn > csn */
    {
        csn_free(&csn);        /* free */
        csn = (CSN *)extracsn; /* use this csn to do the update */
    }

    replica_lock(r->repl_lock);

    gen = (CSNGen *)object_get_data(r->repl_csngen);
    PR_ASSERT(gen);

    rc = csngen_adjust_time(gen, csn);
    /* rc will be either CSN_SUCCESS (0) or clock skew */

    /* done: */

    replica_unlock(r->repl_lock);
    if (csn != extracsn) /* do not free the given csn */
    {
        csn_free(&csn);
    }

    return rc;
}

int
replica_update_csngen_state(Replica *r, const RUV *ruv)
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
    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "Replica state:\n");
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\treplica root: %s\n",
                  slapi_sdn_get_ndn(r->repl_root));
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\treplica type: %s\n",
                  _replica_type_as_string(r));
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\treplica id: %d\n", r->repl_rid);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\tflags: %d\n", r->repl_flags);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\tstate flags: %lu\n", r->repl_state_flags);
    if (r->updatedn_list)
        updatedn_list = replica_updatedn_list_to_string(r->updatedn_list, "\n\t\t");
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\tupdate dn: %s\n",
                  updatedn_list ? updatedn_list : "not configured");
    slapi_ch_free_string(&updatedn_list);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "\tCSN generator: %s configured\n",
                  r->repl_csngen ? "" : "not");
    /* JCMREPL - Dump Referrals */

    replica_unlock(r->repl_lock);
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

    replica_lock(r->repl_lock);

    csn = _replica_get_purge_csn_nolock(r);

    replica_unlock(r->repl_lock);

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
replica_log_ruv_elements(const Replica *r)
{
    int rc = 0;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    rc = replica_log_ruv_elements_nolock(r);

    replica_unlock(r->repl_lock);

    return rc;
}

void
consumer5_set_mapping_tree_state_for_replica(const Replica *r, RUV *supplierRuv)
{
    const Slapi_DN *repl_root_sdn = replica_get_root(r);
    char **ruv_referrals = NULL;
    char **replica_referrals = NULL;
    RUV *ruv;
    int state_backend = -1;
    const char *mtn_state = NULL;

    replica_lock(r->repl_lock);

    if (supplierRuv == NULL) {
        ruv = (RUV *)object_get_data(r->repl_ruv);
        PR_ASSERT(ruv);

        ruv_referrals = ruv_get_referrals(ruv); /* ruv_referrals has to be free'd */
    } else {
        ruv_referrals = ruv_get_referrals(supplierRuv);
    }

    replica_get_referrals_nolock(r, &replica_referrals); /* replica_referrals has to be free'd */

    /* JCMREPL - What if there's a Total update in progress? */
    if (r->repl_type == REPLICA_TYPE_READONLY) {
        state_backend = 0;
    } else if (r->repl_type == REPLICA_TYPE_UPDATABLE) {
        state_backend = 1;
    }
    /* Unlock to avoid changing MTN state under repl lock */
    replica_unlock(r->repl_lock);

    if (state_backend == 0) {
        /* Read-Only - The mapping tree should be refering all update operations. */
        mtn_state = STATE_UPDATE_REFERRAL;
    } else if (state_backend == 1) {
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
replica_set_enabled(Replica *r, PRBool enable)
{
    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    if (enable) {
        if (r->repl_eqcxt_rs == NULL) /* event is not already registered */
        {
            r->repl_eqcxt_rs = slapi_eq_repeat_rel(replica_update_state, r->repl_name,
                                                   slapi_current_rel_time_t() + START_UPDATE_DELAY,
                                                   RUV_SAVE_INTERVAL);

        }
        /* create supplier update event */
        if (r->repl_eqcxt_ka_update == NULL && replica_get_type(r) == REPLICA_TYPE_UPDATABLE) {
            r->repl_eqcxt_ka_update = slapi_eq_repeat_rel(replica_subentry_update, r,
                                                       slapi_current_rel_time_t() + START_UPDATE_DELAY,
                                                       replica_get_keepalive_update_interval(r));
        }
    } else /* disable */
    {
        if (r->repl_eqcxt_rs) /* event is still registerd */
        {
            slapi_eq_cancel_rel(r->repl_eqcxt_rs);
            r->repl_eqcxt_rs = NULL;
        }
        /* Remove supplier update event */
        if (replica_get_type(r) == REPLICA_TYPE_PRIMARY) {
            slapi_eq_cancel_rel(r->repl_eqcxt_ka_update);
            r->repl_eqcxt_ka_update = NULL;
        }
    }

    replica_unlock(r->repl_lock);
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
replica_reload_ruv(Replica *r)
{
    int rc = 0;
    Object *old_ruv_obj = NULL, *new_ruv_obj = NULL;
    RUV *upper_bound_ruv = NULL;
    RUV *new_ruv = NULL;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    old_ruv_obj = r->repl_ruv;

    r->repl_ruv = NULL;

    rc = _replica_configure_ruv(r, PR_TRUE);

    replica_unlock(r->repl_lock);

    if (rc != 0) {
        return rc;
    }

    /* check if there is a changelog and whether this replica logs changes */
    if (cldb_is_open(r) && (r->repl_flags & REPLICA_LOG_CHANGES)) {

        /* Compare new ruv to the changelog's upper bound ruv. We could only keep
           the existing changelog if its upper bound is the same as replica's RUV.
           This is because if changelog has changes not in RUV, they will be
           eventually sent to the consumer's which will cause a state mismatch
           (because the supplier does not actually contain the changes in its data store.
           If, on the other hand, the changelog is not as up to date as the supplier,
           it is not really useful since out of sync consumer's can't be brought
           up to date using this changelog and hence will need to be reinitialized */

        /* replace ruv to make sure we work with the correct changelog file */
        replica_lock(r->repl_lock);

        new_ruv_obj = r->repl_ruv;
        r->repl_ruv = old_ruv_obj;

        replica_unlock(r->repl_lock);

        rc = cl5GetUpperBoundRUV(r, &upper_bound_ruv);
        if (rc != CL5_SUCCESS && rc != CL5_NOTFOUND) {
            return -1;
        }

        if (upper_bound_ruv) {
            new_ruv = object_get_data(new_ruv_obj);
            PR_ASSERT(new_ruv);

            /* ONREPL - there are more efficient ways to establish RUV equality.
               However, because this is not in the critical path and we at most
               have 2 elements in the RUV, this will not effect performance */

            if (!ruv_covers_ruv(new_ruv, upper_bound_ruv) ||
                !ruv_covers_ruv(upper_bound_ruv, new_ruv)) {

                /* We can't use existing changelog - remove existing file */
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replica_reload_ruv - "
                        "New data for replica %s does not match the data in the changelog.\n "
                        "Recreating the changelog file. This could affect replication with replica's "
                        "consumers in which case the consumers should be reinitialized.\n",
                        slapi_sdn_get_dn(r->repl_root));

                /* need to reset changelog db */
                rc = cldb_RemoveReplicaDB(r);

                /* reinstate new ruv */
                replica_lock(r->repl_lock);

                r->repl_ruv = new_ruv_obj;
                cldb_SetReplicaDB(r, NULL);
                if (rc == CL5_SUCCESS) {
                    /* log changes to mark starting point for replication */
                    rc = replica_log_ruv_elements_nolock(r);
                }

                replica_unlock(r->repl_lock);
            } else {
                /* we just need to reinstate new ruv */
                replica_lock(r->repl_lock);

                r->repl_ruv = new_ruv_obj;

                replica_unlock(r->repl_lock);
            }
        } else /* upper bound vector is not there - we have no changes logged */
        {
            /* reinstate new ruv */
            replica_lock(r->repl_lock);

            r->repl_ruv = new_ruv_obj;

            /* just log elements of the current RUV. This is to have
               a starting point for iteration through the changes */
            rc = replica_log_ruv_elements_nolock(r);

            replica_unlock(r->repl_lock);
        }
    }

    if (rc == 0) {
        consumer5_set_mapping_tree_state_for_replica(r, NULL);
        /* reset mapping tree referrals based on new local RUV */
    }

    if (old_ruv_obj)
        object_release(old_ruv_obj);

    if (upper_bound_ruv)
        ruv_destroy(&upper_bound_ruv);

    return rc;
}

/* this function is called during server startup for each replica
   to check whether the replica's data was reloaded offline and
   whether replica's changelog needs to be reinitialized */

/* the function does not use replica lock but all functions it calls are
   thread safe. Locking replica lock while calling changelog functions
   causes a deadlock because changelog calls replica functions that
   that lock the same lock */
int
replica_check_for_data_reload(Replica *r, void *arg __attribute__((unused)))
{
    int rc = 0;
    RUV *upper_bound_ruv = NULL;
    RUV *r_ruv = NULL;
    Object *ruv_obj;

    PR_ASSERT(r);

    /* check that we have a changelog and if this replica logs changes */
    if (cldb_is_open(r) && (r->repl_flags & REPLICA_LOG_CHANGES)) {
        /* Compare new ruv to the purge ruv. If the new contains csns which
           are smaller than those in purge ruv, we need to remove old and
           create new changelog file for this replica. This is because we
           will not have sufficient changes to incrementally update a consumer
           to the current state of the supplier. */

        rc = cl5GetUpperBoundRUV(r, &upper_bound_ruv);
        if (rc != CL5_SUCCESS && rc != CL5_NOTFOUND) {
            return -1;
        }

        if (upper_bound_ruv && ruv_replica_count(upper_bound_ruv) > 0) {
            ruv_obj = replica_get_ruv(r);
            r_ruv = object_get_data(ruv_obj);
            PR_ASSERT(r_ruv);

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

            if (slapi_disorderly_shutdown(PR_FALSE)) {
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replica_check_for_data_reload - "
                                                                   "Disorderly shutdown for replica %s. Check if DB RUV needs to be updated\n",
                              slapi_sdn_get_dn(r->repl_root));

                if (ruv_covers_ruv(upper_bound_ruv, r_ruv) && !ruv_covers_ruv(r_ruv, upper_bound_ruv)) {
                    /*
                     * The Changelog RUV is ahead of the RUV in the DB.
                     * RUV DB was likely not flushed on disk.
                     */

                    ruv_force_csn_update_from_ruv(upper_bound_ruv, r_ruv,
                                                  "Force update of database RUV (from CL RUV) -> ", SLAPI_LOG_NOTICE);
                }

            } else {

                rc = ruv_compare_ruv(upper_bound_ruv, "changelog max RUV", r_ruv, "database RUV", 0, SLAPI_LOG_ERR);
                if (RUV_COMP_IS_FATAL(rc)) {

                    /* We can't use existing changelog - remove existing file */
                    slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replica_check_for_data_reload - "
                                                                       "Data for replica %s does not match the data in the changelog. "
                                                                       "Recreating the changelog file. "
                                                                       "This could affect replication with replica's consumers in which case the "
                                                                       "consumers should be reinitialized.\n",
                                  slapi_sdn_get_dn(r->repl_root));


                    /* need to reset changelog db */
                    rc = cldb_RemoveReplicaDB(r);
                    cldb_SetReplicaDB(r, NULL);

                    if (rc == CL5_SUCCESS) {
                        /* log changes to mark starting point for replication */
                        rc = replica_log_ruv_elements(r);
                    }
                } else if (rc) {
                    slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replica_check_for_data_reload - "
                                                                       "For replica %s there were some differences between the changelog max RUV and the "
                                                                       "database RUV.  If there are obsolete elements in the database RUV, you "
                                                                       "should remove them using the CLEANALLRUV task.  If they are not obsolete, "
                                                                       "you should check their status to see why there are no changes from those "
                                                                       "servers in the changelog.\n",
                                  slapi_sdn_get_dn(r->repl_root));
                    rc = 0;
                }
            } /* slapi_disorderly_shutdown */

            object_release(ruv_obj);
        } else /* we have no changes currently logged for this replica */
        {
            /* log changes to mark starting point for replication */
            rc = replica_log_ruv_elements(r);
        }
    }

    if (rc == 0) {
        /* reset mapping tree referrals based on new local RUV */
        consumer5_set_mapping_tree_state_for_replica(r, NULL);
    }

    if (upper_bound_ruv)
        ruv_destroy(&upper_bound_ruv);

    return rc;
}

/* Helper functions */
/* reads replica configuration entry. The entry is the child of the
   mapping tree node for the replica's backend */

static Slapi_Entry *
_replica_get_config_entry(const Slapi_DN *root, const char **attrs)
{
    int rc = 0;
    char *dn = NULL;
    Slapi_Entry **entries;
    Slapi_Entry *e = NULL;
    Slapi_PBlock *pb = NULL;

    dn = _replica_get_config_dn(root);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "_replica_get_config_entry - Failed to get the config dn for %s\n",
                      slapi_sdn_get_dn(root));
        return NULL;
    }
    pb = slapi_pblock_new();

    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_BASE, "objectclass=*", (char **)attrs, 0, NULL,
                                 NULL, repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc == 0) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        e = slapi_entry_dup(entries[0]);
    }

    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    slapi_ch_free_string(&dn);

    return e;
}

/* It does an internal search to read the in memory RUV
 * of the provided suffix
  */
Slapi_Entry *
get_in_memory_ruv(Slapi_DN *suffix_sdn)
{
    const char *attrs[4];

    /* these two attributes needs to be asked when reading the RUV */
    attrs[0] = type_ruvElement;
    attrs[1] = type_ruvElementUpdatetime;
    attrs[2] = type_agmtMaxCSN;
    attrs[3] = NULL;
    return (_replica_get_config_entry(suffix_sdn, attrs));
}

char *
replica_get_dn(Replica *r)
{
    return _replica_get_config_dn(r->repl_root);
}

static int
_replica_check_validity(const Replica *r)
{
    PR_ASSERT(r);

    if (r->repl_root == NULL || r->repl_type == 0 || r->repl_rid == 0 ||
        r->repl_csngen == NULL || r->repl_name == NULL) {
        return LDAP_OTHER;
    } else {
        return LDAP_SUCCESS;
    }
}

/* replica configuration entry has the following format:
    dn: cn=replica,<mapping tree node dn>
    objectclass:    top
    objectclass:    nsds5Replica
    objectclass:    extensibleObject
    nsds5ReplicaRoot:    <root of the replica>
    nsds5ReplicaId:    <replica id>
    nsds5ReplicaType:    <type of the replica: primary, read-write or read-only>
    nsState:        <state of the csn generator> missing the first time replica is started
    nsds5ReplicaBindDN:        <supplier update dn> consumers only
    nsds5ReplicaBindDNGroup: group, containing replicaBindDNs
    nsds5ReplicaBindDNGroupCheckInterval: defines how frequently to check for update of bindGroup
    nsds5ReplicaReferral: <referral URL to updatable replica> consumers only
    nsds5ReplicaPurgeDelay: <time, in seconds, to keep purgeable CSNs, 0 == keep forever>
    nsds5ReplicaTombstonePurgeInterval: <time, in seconds, between tombstone purge runs, 0 == don't reap>

    richm: changed slapi entry from const to editable - if the replica id is supplied for a read
    only replica, we ignore it and replace the value with the READ_ONLY_REPLICA_ID
 */
static int
_replica_init_from_config(Replica *r, Slapi_Entry *e, char *errortext)
{
    Slapi_Attr *attr;
    CSNGen *gen;
    char *precise_purging = NULL;
    char buf[SLAPI_DSE_RETURNTEXT_SIZE];
    char *errormsg = errortext ? errortext : buf;
    char *val;
    int64_t backoff_min;
    int64_t backoff_max;
    int64_t ptimeout = 0;
    int64_t release_timeout = 0;
    int64_t interval = 0;
    int64_t rtype = 0;
    int rc;

    PR_ASSERT(r && e);

    /* get replica root */
    val = slapi_entry_attr_get_charptr(e, attr_replicaRoot);
    if (val == NULL) {
        PR_snprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to retrieve %s attribute from (%s)",
                    attr_replicaRoot,
                    (char *)slapi_entry_get_dn((Slapi_Entry *)e));
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_init_from_config - %s\n",
                      errormsg);
        return LDAP_OTHER;
    }

    r->repl_root = slapi_sdn_new_dn_passin(val);

    /* get replica type */
    if (slapi_entry_attr_exists(e, attr_replicaType)) {
        if ((val = (char*)slapi_entry_attr_get_ref(e, attr_replicaType))) {
            if (repl_config_valid_num(attr_replicaType, (char *)val, 0, REPLICA_TYPE_UPDATABLE, &rc, errormsg, &rtype) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
            r->repl_type = rtype;
        } else {
            r->repl_type = REPLICA_TYPE_READONLY;
        }
    } else {
        r->repl_type = REPLICA_TYPE_READONLY;
    }

    /* grab and validate the backoff min retry settings */
    if (slapi_entry_attr_exists(e, type_replicaBackoffMin)) {
        if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaBackoffMin))) {
            if (repl_config_valid_num(type_replicaBackoffMin, val, 1, INT_MAX, &rc, errormsg, &backoff_min) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
        } else {
            backoff_min = PROTOCOL_BACKOFF_MINIMUM;
        }
    } else {
        backoff_min = PROTOCOL_BACKOFF_MINIMUM;
    }

    /* grab and validate the backoff max retry settings */
    if (slapi_entry_attr_exists(e, type_replicaBackoffMax)) {
        if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaBackoffMax))) {
            if (repl_config_valid_num(type_replicaBackoffMax, val, 1, INT_MAX, &rc, errormsg, &backoff_max) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
        } else {
            backoff_max = PROTOCOL_BACKOFF_MAXIMUM;
        }
    } else {
        backoff_max = PROTOCOL_BACKOFF_MAXIMUM;
    }

    /* Verify backoff min and max work together */
    if (backoff_min > backoff_max) {
        PR_snprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Backoff minimum (%" PRId64 ") can not be greater than the backoff maximum (%" PRId64 ").",
                    backoff_min, backoff_max);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_init_from_config - "
                      "%s\n", errormsg);
        return LDAP_UNWILLING_TO_PERFORM;
    } else {
        slapi_counter_set_value(r->backoff_min, backoff_min);
        slapi_counter_set_value(r->backoff_max, backoff_max);
    }

    /* get the protocol timeout */
    if (slapi_entry_attr_exists(e, type_replicaProtocolTimeout)) {
        if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaProtocolTimeout))) {
            if (repl_config_valid_num(type_replicaProtocolTimeout, val, 0, INT_MAX, &rc, errormsg, &ptimeout) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
            slapi_counter_set_value(r->protocol_timeout, ptimeout);
        } else {
            slapi_counter_set_value(r->protocol_timeout, DEFAULT_PROTOCOL_TIMEOUT);
        }
    } else {
        slapi_counter_set_value(r->protocol_timeout, DEFAULT_PROTOCOL_TIMEOUT);
    }

    /* Get the release timeout */
    if (slapi_entry_attr_exists(e, type_replicaReleaseTimeout)) {
        if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaReleaseTimeout))) {
            if (repl_config_valid_num(type_replicaReleaseTimeout, val, 0, INT_MAX, &rc, errortext, &release_timeout) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
            slapi_counter_set_value(r->release_timeout, release_timeout);
        } else {
            slapi_counter_set_value(r->release_timeout, 0);
        }
    } else {
        slapi_counter_set_value(r->release_timeout, 0);
    }

    /* check for precise tombstone purging */
    precise_purging = (char*)slapi_entry_attr_get_ref(e, type_replicaPrecisePurge);
    if (precise_purging) {
        if (strcasecmp(precise_purging, "on") == 0) {
            slapi_counter_set_value(r->precise_purging, 1);
        } else if (strcasecmp(precise_purging, "off") == 0) {
            slapi_counter_set_value(r->precise_purging, 0);
        } else {
            /* Invalid value */
            PR_snprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid value for %s: %s",
                        type_replicaPrecisePurge, precise_purging);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_init_from_config - "
                          "%s\n", errormsg);
            return LDAP_UNWILLING_TO_PERFORM;
        }
    } else {
        slapi_counter_set_value(r->precise_purging, 0);
    }

    /* get replica flags */
    if (slapi_entry_attr_exists(e, attr_flags)) {
        int64_t rflags;
        if((val = (char*)slapi_entry_attr_get_ref(e, attr_flags))) {
            if (repl_config_valid_num(attr_flags, val, 0, 1, &rc, errortext, &rflags) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
            r->repl_flags = (uint32_t)rflags;
        } else {
            r->repl_flags = 0;
        }
    } else {
        r->repl_flags = 0;
    }

    /*
     * Get replicaid
     * The replica id is ignored for read only replicas and is set to the
     * special value READ_ONLY_REPLICA_ID
     */
    if (r->repl_type == REPLICA_TYPE_READONLY) {
        r->repl_rid = READ_ONLY_REPLICA_ID;
        slapi_entry_attr_set_uint(e, attr_replicaId, (unsigned int)READ_ONLY_REPLICA_ID);
    }
    /* a replica id is required for updatable and primary replicas */
    else if (r->repl_type == REPLICA_TYPE_UPDATABLE ||
             r->repl_type == REPLICA_TYPE_PRIMARY) {
        if ((val = (char*)slapi_entry_attr_get_ref(e, attr_replicaId))) {
            int64_t rid;
            if (repl_config_valid_num(attr_replicaId, val, 1, 65534, &rc, errormsg, &rid) != 0) {
                return LDAP_UNWILLING_TO_PERFORM;
            }
            r->repl_rid = (ReplicaId)rid;
        } else {
            PR_snprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Failed to retrieve required %s attribute from %s",
                        attr_replicaId, (char *)slapi_entry_get_dn((Slapi_Entry *)e));
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "_replica_init_from_config - %s\n", errormsg);
            return LDAP_OTHER;
        }
    }

    attr = NULL;
    rc = slapi_entry_attr_find(e, attr_state, &attr);
    gen = csngen_new(r->repl_rid, attr);
    if (gen == NULL) {
        PR_snprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Failed to create csn generator for replica (%s)",
                    (char *)slapi_entry_get_dn((Slapi_Entry *)e));
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "_replica_init_from_config - %s\n", errormsg);
        return LDAP_OTHER;
    }
    r->repl_csngen = object_new((void *)gen, (FNFree)csngen_free);

    /* Hook generator so we can maintain min/max CSN info */
    r->csn_pl_reg_id = csngen_register_callbacks(gen, assign_csn_callback, r, abort_csn_callback, r);

    /* get replication bind dn */
    r->updatedn_list = replica_updatedn_list_new(e);

    /* get replication bind dn groups */
    r->updatedn_groups = replica_updatedn_group_new(e);
    r->groupdn_list = replica_groupdn_list_new(r->updatedn_groups);
    r->updatedn_group_last_check = 0;
    /* get groupdn check interval */
    if ((val = (char*)slapi_entry_attr_get_ref(e, attr_replicaBindDnGroupCheckInterval))) {
        if (repl_config_valid_num(attr_replicaBindDnGroupCheckInterval, val, -1, INT_MAX, &rc, errormsg, &interval) != 0) {
            return LDAP_UNWILLING_TO_PERFORM;
        }
        r->updatedn_group_check_interval = interval;
    } else {
        r->updatedn_group_check_interval = -1;
    }

    /* get replica name */
    val = slapi_entry_attr_get_charptr(e, attr_replicaName);
    if (val) {
        r->repl_name = val;
    } else {
        rc = slapi_uniqueIDGenerateString(&r->repl_name);
        if (rc != UID_SUCCESS) {
            PR_snprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Failed to assign replica name for replica (%s); uuid generator error - %d ",
                        (char *)slapi_entry_get_dn((Slapi_Entry *)e), rc);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_init_from_config - %s\n",
                          errormsg);
            return LDAP_OTHER;
        } else
            r->new_name = PR_TRUE;
    }

    /* get the list of referrals */
    slapi_entry_attr_find(e, attr_replicaReferral, &attr);
    if (attr != NULL) {
        slapi_attr_get_valueset(attr, &r->repl_referral);
    }

    /*
     * Set the purge offset (default 7 days). This is the extra
     * time we allow purgeable CSNs to stick around, in case a
     * replica regresses. Could also be useful when LCUP happens,
     * since we don't know about LCUP replicas, and they can just
     * turn up whenever they want to.
     */
    if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaPurgeDelay))) {
        if (repl_config_valid_num(type_replicaPurgeDelay, val, -1, INT_MAX, &rc, errormsg, &interval) != 0) {
            return LDAP_UNWILLING_TO_PERFORM;
        }
        r->repl_purge_delay = interval;
    } else {
        r->repl_purge_delay = 60 * 60 * 24 * 7; /* One week, in seconds */
    }

    if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaTombstonePurgeInterval))) {
        if (repl_config_valid_num(type_replicaTombstonePurgeInterval, val, -1, INT_MAX, &rc, errormsg, &interval) != 0) {
            return LDAP_UNWILLING_TO_PERFORM;
        }
        r->tombstone_reap_interval = interval;
    } else {
        r->tombstone_reap_interval = 3600 * 24; /* One week, in seconds */
    }

    if ((val = (char*)slapi_entry_attr_get_ref(e, type_replicaKeepAliveUpdateInterval))) {
        if (repl_config_valid_num(type_replicaKeepAliveUpdateInterval, val, REPLICA_KEEPALIVE_UPDATE_INTERVAL_MIN,
                                  INT_MAX, &rc, errormsg, &interval) != 0)
        {
            return LDAP_UNWILLING_TO_PERFORM;
        }
        r->keepalive_update_interval = interval;
    } else {
        r->keepalive_update_interval = DEFAULT_REPLICA_KEEPALIVE_UPDATE_INTERVAL;
    }

    r->tombstone_reap_stop = r->tombstone_reap_active = PR_FALSE;

    /* No supplier holding the replica */
    r->locking_conn = ULONG_MAX;

    return (_replica_check_validity(r));
}

static void
replica_delete_task_config(Slapi_Entry *e, char *attr, char *value)
{
    Slapi_PBlock *modpb;
    struct berval *vals[2];
    struct berval val[1];
    LDAPMod *mods[2];
    LDAPMod mod;
    int32_t rc = 0;

    val[0].bv_len = strlen(value);
    val[0].bv_val = value;
    vals[0] = &val[0];
    vals[1] = NULL;

    mod.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
    mod.mod_type = attr;
    mod.mod_bvalues = vals;
    mods[0] = &mod;
    mods[1] = NULL;

    modpb = slapi_pblock_new();
    slapi_modify_internal_set_pb(modpb, slapi_entry_get_dn(e), mods, NULL, NULL,
            repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_modify_internal_pb(modpb);
    slapi_pblock_get(modpb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_destroy(modpb);

    if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                "delete_cleaned_rid_config - Failed to remove task data from (%s) error (%d)\n",
                slapi_entry_get_dn(e), rc);
    }
}

void
replica_check_for_tasks(time_t when __attribute__((unused)), void *arg)
{
    const Slapi_DN *replica_root = (Slapi_DN *)arg;
    Slapi_Entry *e = NULL;
    Replica *r = NULL;
    char **clean_vals;

    e = _replica_get_config_entry(replica_root, NULL);
    r = replica_get_replica_from_dn(replica_root);

    if (e == NULL || r == NULL || ldif_dump_is_running() == PR_TRUE) {
        /* If db2ldif is being run, do not check if there are incomplete tasks */
        return;
    }
    /*
     *  check if we are in the middle of a CLEANALLRUV task,
     *  if so set the cleaned rid, and fire off the thread
     */
    if ((clean_vals = slapi_entry_attr_get_charray(e, type_replicaCleanRUV)) != NULL) {
        for (size_t i = 0; i < CLEANRIDSIZ && clean_vals[i]; i++) {
            struct timespec ts = slapi_current_rel_time_hr();
            PRBool original_task = PR_TRUE;
            Slapi_Entry *task_entry = NULL;
            Slapi_PBlock *add_pb = NULL;
            int32_t result = 0;
            ReplicaId rid;
            char *token = NULL;
            char *forcing;
            char *iter = NULL;
            char *repl_root = NULL;
            char *ridstr = NULL;
            char *rdn = NULL;
            char *dn = NULL;
            char *orig_val = slapi_ch_strdup(clean_vals[i]);

            /*
             *  Get all the needed from
             */
            token = ldap_utf8strtok_r(clean_vals[i], ":", &iter);
            if (token) {
                rid = atoi(token);
                if (rid <= 0 || rid >= READ_ONLY_REPLICA_ID) {
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "CleanAllRUV Task - "
                            "Invalid replica id(%d) aborting task.  Aborting cleaning task!\n", rid);
                    replica_delete_task_config(e, (char *)type_replicaCleanRUV, orig_val);
                    goto done;
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "CleanAllRUV Task - "
                        "Unable to parse cleanallruv data (%s), missing rid, aborting task.  Aborting cleaning task!\n",
                        clean_vals[i]);
                replica_delete_task_config(e, (char *)type_replicaCleanRUV, orig_val);
                goto done;
            }

            /* Get forcing */
            forcing = ldap_utf8strtok_r(iter, ":", &iter);
            if (forcing == NULL || strlen(forcing) > 3) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "CleanAllRUV Task - "
                        "Unable to parse cleanallruv data (%s), missing/invalid force option (%s).  Aborting cleaning task!\n",
                        clean_vals[i], forcing ? forcing : "missing force option");
                replica_delete_task_config(e, (char *)type_replicaCleanRUV, orig_val);
                goto done;
            }

            /* Get original task flag */
            token = ldap_utf8strtok_r(iter, ":", &iter);
            if (token) {
                if (!atoi(token)) {
                     original_task = PR_FALSE;
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "CleanAllRUV Task - "
                        "Unable to parse cleanallruv data (%s), missing original task flag.  Aborting cleaning task!\n",
                        clean_vals[i]);
                replica_delete_task_config(e, (char *)type_replicaCleanRUV, orig_val);
                goto done;
            }

            /* Get repl root */
            token = ldap_utf8strtok_r(iter, ":", &iter);
            if (token) {
                repl_root = token;
            } else {
                /* no repl root, have to void task */
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "CleanAllRUV Task - "
                        "Unable to parse cleanallruv data (%s), missing replication root aborting task.  Aborting cleaning task!\n",
                        clean_vals[i]);
                replica_delete_task_config(e, (char *)type_replicaCleanRUV, orig_val);
                goto done;
            }

            /*
             * We have all our data, now add the task....
             */
            slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name, "CleanAllRUV Task - "
                    "CleanAllRUV task found, resuming the cleaning of rid(%d)...\n", rid);

            add_pb = slapi_pblock_new();
            task_entry = slapi_entry_alloc();
            rdn = slapi_ch_smprintf("restarted-%ld", ts.tv_sec);
            dn = slapi_create_dn_string("cn=%s,cn=cleanallruv, cn=tasks, cn=config", rdn, ts.tv_sec);
            slapi_entry_init(task_entry, dn, NULL);

            ridstr = slapi_ch_smprintf("%d", rid);
            slapi_entry_add_string(task_entry, "objectclass", "top");
            slapi_entry_add_string(task_entry, "objectclass", "extensibleObject");
            slapi_entry_add_string(task_entry, "cn", rdn);
            slapi_entry_add_string(task_entry, "replica-base-dn", repl_root);
            slapi_entry_add_string(task_entry, "replica-id", ridstr);
            slapi_entry_add_string(task_entry, "replica-force-cleaning", forcing);
            slapi_entry_add_string(task_entry, "replica-original-task", original_task ? "1" : "0");

            slapi_add_entry_internal_set_pb(add_pb, task_entry, NULL,
                    repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
            slapi_add_internal_pb(add_pb);
            slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
            slapi_pblock_destroy(add_pb);
            if (result != LDAP_SUCCESS) {
               slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                       "replica_check_for_tasks - failed to add cleanallruv task entry: %s\n",
                       ldap_err2string(result));
            }

        done:
            slapi_ch_free_string(&orig_val);
            slapi_ch_free_string(&ridstr);
            slapi_ch_free_string(&rdn);
        }
        slapi_ch_array_free(clean_vals);
    }

    if ((clean_vals = slapi_entry_attr_get_charray(e, type_replicaAbortCleanRUV)) != NULL) {
        for (size_t i = 0; clean_vals[i]; i++) {
            struct timespec ts = slapi_current_rel_time_hr();
            PRBool original_task = PR_TRUE;
            Slapi_Entry *task_entry = NULL;
            Slapi_PBlock *add_pb = NULL;
            ReplicaId rid;
            char *certify = NULL;
            char *ridstr = NULL;
            char *token = NULL;
            char *repl_root;
            char *iter = NULL;
            char *rdn = NULL;
            char *dn = NULL;
            char *orig_val = slapi_ch_strdup(clean_vals[i]);
            int32_t result = 0;

            token = ldap_utf8strtok_r(clean_vals[i], ":", &iter);
            if (token) {
                rid = atoi(token);
                if (rid <= 0 || rid >= READ_ONLY_REPLICA_ID) {
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "Abort CleanAllRUV Task - "
                            "Invalid replica id(%d) aborting abort task.\n", rid);
                    replica_delete_task_config(e, (char *)type_replicaAbortCleanRUV, orig_val);
                    goto done2;
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "Abort CleanAllRUV Task - "
                        "Unable to parse cleanallruv data (%s), aborting abort task.\n", clean_vals[i]);
                replica_delete_task_config(e, (char *)type_replicaAbortCleanRUV, orig_val);
                goto done2;
            }

            repl_root = ldap_utf8strtok_r(iter, ":", &iter);
            certify = ldap_utf8strtok_r(iter, ":", &iter);

            /* Get original task flag */
            token = ldap_utf8strtok_r(iter, ":", &iter);
            if (token) {
                if (!atoi(token)) {
                     original_task = PR_FALSE;
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                        "Abort CleanAllRUV Task - Unable to parse cleanallruv data (%s), "
                        "missing original task flag.  Aborting abort task!\n",
                        clean_vals[i]);
                replica_delete_task_config(e, (char *)type_replicaAbortCleanRUV, orig_val);
                goto done2;
            }

            if (!is_cleaned_rid(rid)) {
                slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name, "Abort CleanAllRUV Task - "
                        "Replica id(%d) is not being cleaned, nothing to abort.  Aborting abort task.\n", rid);
                replica_delete_task_config(e, (char *)type_replicaAbortCleanRUV, orig_val);
                goto done2;
            }

            add_aborted_rid(rid, r, repl_root, certify, original_task);
            stop_ruv_cleaning();

            slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name, "Abort CleanAllRUV Task - "
                    "Abort task found, resuming abort of rid(%d).\n", rid);

            add_pb = slapi_pblock_new();
            task_entry = slapi_entry_alloc();
            rdn = slapi_ch_smprintf("restarted-abort-%ld", ts.tv_sec);
            dn = slapi_create_dn_string("cn=%s,cn=abort cleanallruv, cn=tasks, cn=config", rdn, ts.tv_sec);
            slapi_entry_init(task_entry, dn, NULL);

            ridstr = slapi_ch_smprintf("%d", rid);
            slapi_entry_add_string(task_entry, "objectclass", "top");
            slapi_entry_add_string(task_entry, "objectclass", "extensibleObject");
            slapi_entry_add_string(task_entry, "cn", rdn);
            slapi_entry_add_string(task_entry, "replica-base-dn", repl_root);
            slapi_entry_add_string(task_entry, "replica-id", ridstr);
            slapi_entry_add_string(task_entry, "replica-certify-all", certify);
            slapi_entry_add_string(task_entry, "replica-original-task", original_task ? "1" : "0");

            slapi_add_entry_internal_set_pb(add_pb, task_entry, NULL,
                    repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
            slapi_add_internal_pb(add_pb);
            slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
            slapi_pblock_destroy(add_pb);
            if (result != LDAP_SUCCESS) {
               slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                       "replica_check_for_tasks - failed to add cleanallruv abort task entry: %s\n",
                       ldap_err2string(result));
            }
        done2:
            slapi_ch_free_string(&orig_val);
            slapi_ch_free_string(&ridstr);
            slapi_ch_free_string(&rdn);

        }
        slapi_ch_array_free(clean_vals);
    }
    slapi_entry_free(e);
}

/* This function updates the entry to contain information generated
   during replica initialization.
   Returns 0 if successful and -1 otherwise */
static int
_replica_update_entry(Replica *r, Slapi_Entry *e, char *errortext)
{
    int rc;
    Slapi_Mod smod;
    Slapi_Value *val;

    PR_ASSERT(r);

    /* add attribute that stores state of csn generator */
    rc = csngen_get_state((CSNGen *)object_get_data(r->repl_csngen), &smod);
    if (rc != CSN_SUCCESS) {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to get csn generator's state; csn error - %d", rc);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "_replica_update_entry - %s\n", errortext);
        return -1;
    }

    val = slapi_value_new_berval(slapi_mod_get_first_value(&smod));

    rc = slapi_entry_add_value(e, slapi_mod_get_type(&smod), val);

    slapi_value_free(&val);
    slapi_mod_done(&smod);

    if (rc != 0) {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to update replica entry");
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "_replica_update_entry - %s\n", errortext);
        return -1;
    }

    /* add attribute that stores replica name */
    rc = slapi_entry_add_string(e, attr_replicaName, r->repl_name);
    if (rc != 0) {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to update replica entry");
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "_replica_update_entry - %s\n", errortext);
        return -1;
    } else
        r->new_name = PR_FALSE;

    return 0;
}

/* DN format: cn=replica,cn=\"<root>\",cn=mapping tree,cn=config */
static char *
_replica_get_config_dn(const Slapi_DN *root)
{
    char *dn;
    /* "cn=mapping tree,cn=config" */
    const char *mp_base = slapi_get_mapping_tree_config_root();

    PR_ASSERT(root);

    /* This function converts the old style DN to the new style. */
    dn = slapi_ch_smprintf("%s,cn=\"%s\",%s",
                           REPLICA_RDN, slapi_sdn_get_dn(root), mp_base);
    return dn;
}
/* when a replica is added the changelog config entry is created
 * it will only the container entry, specifications for trimming 
 * or encyrption need to be added separately
 */
static int
_replica_config_changelog(Replica *replica)
{
    int rc = 0;

    Slapi_Backend *be = slapi_be_select(replica_get_root(replica));

    Slapi_Entry *config_entry = slapi_entry_alloc();
    slapi_entry_init(config_entry, slapi_ch_strdup("cn=changelog"), NULL);
    slapi_entry_add_string(config_entry, "objectclass", "top");
    slapi_entry_add_string(config_entry, "objectclass", "extensibleObject");

    rc = slapi_back_ctrl_info(be, BACK_INFO_CLDB_SET_CONFIG, (void *)config_entry);

    return rc;
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
_replica_configure_ruv(Replica *r, PRBool isLocked __attribute__((unused)))
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
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "_replica_configure_ruv - Out of memory\n");
        goto done;
    }

    attrs[0] = (char *)type_ruvElement;
    attrs[1] = NULL;
    slapi_search_internal_set_pb(
        pb,
        slapi_sdn_get_dn(r->repl_root),
        LDAP_SCOPE_BASE,
        "objectclass=*",
        attrs,
        0,    /* attrsonly */
        NULL, /* controls */
        RUV_STORAGE_ENTRY_UNIQUEID,
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        OP_FLAG_REPLICATED); /* flags */
    slapi_search_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc == LDAP_SUCCESS) {
        /* get RUV attributes and construct the RUV */
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (NULL == entries || NULL == entries[0]) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "_replica_configure_ruv - Replica ruv tombstone entry for "
                          "replica %s not found\n",
                          slapi_sdn_get_dn(r->repl_root));
            goto done;
        }

        rc = slapi_entry_attr_find(entries[0], type_ruvElement, &attr);
        if (rc != 0) /* ruv attribute is missing - this not allowed */
        {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "_replica_configure_ruv - Replica ruv tombstone entry for "
                          "replica %s does not contain %s\n",
                          slapi_sdn_get_dn(r->repl_root), type_ruvElement);
            goto done;
        }

        /* Check in the tombstone we have retrieved if the local purl is
            already present:
                rid == 0: the local purl is not present
                rid != 0: the local purl is present ==> nothing to do
         */
        ruv_init_from_slapi_attr_and_check_purl(attr, &ruv, &rid);
        if (ruv) {
            char *generation = NULL;
            generation = ruv_get_replica_generation(ruv);
            if (NULL != generation) {
                r->repl_ruv = object_new((void *)ruv, (FNFree)ruv_destroy);
                /* Is the local purl in the ruv? (the port or the host could have
                   changed)
                 */
                /* A consumer only doesn't have its purl in its ruv  */
                if (r->repl_type == REPLICA_TYPE_UPDATABLE) {
                    int need_update = 0;
#define RUV_UPDATE_PARTIAL 1
#define RUV_UPDATE_FULL 2
                    if (rid == 0) {
                        /* We can not have more than 1 ruv with the same rid
                           so we replace it */
                        const char *purl = NULL;

                        purl = multisupplier_get_local_purl();
                        ruv_delete_replica(ruv, r->repl_rid);
                        ruv_add_index_replica(ruv, r->repl_rid, purl, 1);
                        need_update = RUV_UPDATE_PARTIAL; /* ruv changed, so write tombstone */
                    } else                                /* bug 540844: make sure the local supplier rid is first in the ruv */
                    {
                        /* make sure local supplier is first in list */
                        ReplicaId first_rid = 0;
                        char *first_purl = NULL;
                        ruv_get_first_id_and_purl(ruv, &first_rid, &first_purl);
                        /* if the local supplier is not first in the list . . . */
                        /* rid is from changelog; first_rid is from backend */
                        if (rid != first_rid) {
                            /* . . . move the local supplier to the beginning of the list */
                            ruv_move_local_supplier_to_first(ruv, rid);
                            need_update = RUV_UPDATE_PARTIAL; /* must update tombstone also */
                        }
                        /* r->repl_rid is from config; rid is from changelog */
                        if (r->repl_rid != rid) {
                            /* Most likely, the replica was once deleted
                             * and recreated with a different rid from the
                             * previous. */
                            /* must recreate ruv tombstone */
                            need_update = RUV_UPDATE_FULL;
                            if (NULL != r->repl_ruv) {
                                object_release(r->repl_ruv);
                                r->repl_ruv = NULL;
                            }
                        }
                    }

                    /* Update also the directory entry */
                    if (RUV_UPDATE_PARTIAL == need_update) {
                        replica_replace_ruv_tombstone(r);
                    } else if (RUV_UPDATE_FULL == need_update) {
                        _delete_tombstone(slapi_sdn_get_dn(r->repl_root),
                                          RUV_STORAGE_ENTRY_UNIQUEID,
                                          OP_FLAG_REPL_RUV);
                        rc = replica_create_ruv_tombstone(r);
                        if (rc) {
                            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                          "_replica_configure_ruv - "
                                          "Failed to recreate replica ruv tombstone entry"
                                          " (%s); LDAP error - %d\n",
                                          slapi_sdn_get_dn(r->repl_root), rc);
                            slapi_ch_free_string(&generation);
                            goto done;
                        }
                    }
#undef RUV_UPDATE_PARTIAL
#undef RUV_UPDATE_FULL
                }

                slapi_ch_free_string(&generation);
                return_value = 0;
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_configure_ruv - "
                                                               "RUV for replica %s is missing replica generation\n",
                              slapi_sdn_get_dn(r->repl_root));
                goto done;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_configure_ruv - "
                                                           "Unable to convert %s attribute in entry %s to a replica update vector.\n",
                          type_ruvElement, slapi_sdn_get_dn(r->repl_root));
            goto done;
        }

    } else /* search failed */
    {
        if (LDAP_NO_SUCH_OBJECT == rc) {
            /* The entry doesn't exist: create it */
            rc = replica_create_ruv_tombstone(r);
            if (LDAP_SUCCESS != rc) {
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
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "_replica_configure_ruv - Failed to create replica ruv tombstone "
                              "entry (%s); LDAP error - %d\n",
                              slapi_sdn_get_dn(r->repl_root), rc);
                goto done;
            } else {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "_replica_configure_ruv - No ruv tombstone found for replica %s. "
                              "Created a new one\n",
                              slapi_sdn_get_dn(r->repl_root));
                return_value = 0;
            }
        } else {
            /* see if the suffix is disabled */
            char *state = slapi_mtn_get_state(r->repl_root);
            if (state && !strcasecmp(state, "disabled")) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "_replica_configure_ruv - Replication disabled for "
                              "entry (%s); LDAP error - %d\n",
                              slapi_sdn_get_dn(r->repl_root), rc);
                slapi_ch_free_string(&state);
                goto done;
            } else if (!r->repl_ruv) /* other error */
            {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "_replica_configure_ruv - Replication broken for "
                              "entry (%s); LDAP error - %d\n",
                              slapi_sdn_get_dn(r->repl_root), rc);
                slapi_ch_free_string(&state);
                goto done;
            } else /* some error but continue anyway? */
            {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "_replica_configure_ruv - Error %d reading tombstone for replica %s.\n",
                              rc, slapi_sdn_get_dn(r->repl_root));
                return_value = 0;
            }
            slapi_ch_free_string(&state);
        }
    }

    if (NULL != r->min_csn_pl) {
        csnplFree(&r->min_csn_pl);
    }

    /* create pending list for min csn if necessary */
    if (ruv_get_smallest_csn_for_replica((RUV *)object_get_data(r->repl_ruv),
                                         r->repl_rid, &csn) == RUV_SUCCESS) {
        csn_free(&csn);
        r->min_csn_pl = NULL;
    } else {
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
    if (NULL != pb) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
    }
    if (return_value != 0) {
        if (ruv)
            ruv_destroy(&ruv);
    }

    return return_value;
}

/* NOTE - this is the only non-api function that performs locking because
   it is called by the event queue */
void
replica_update_state(time_t when __attribute__((unused)), void *arg)
{
    int rc;
    const char *replica_name = (const char *)arg;
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

    r = replica_get_by_name(replica_name);
    if (NULL == r) {
        return;
    }

    replica_lock(r->repl_lock);

    /* replica state is currently being updated
       or no CSN was assigned - bail out */
    if (r->state_update_inprogress) {
        replica_unlock(r->repl_lock);
        return;
    }

    /* This might be a consumer */
    if (!r->repl_csn_assigned) {
        /* EY: the consumer needs to flush ruv to disk. */
        replica_unlock(r->repl_lock);
        if (replica_write_ruv(r)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "replica_update_state - Failed write RUV for %s\n",
                          slapi_sdn_get_dn(r->repl_root));
        }
        return;
    }

    /* ONREPL update csn generator state of an updatable replica only */
    /* ONREPL state always changes because we update time every second and
       we write state to the disk less frequently */
    rc = csngen_get_state((CSNGen *)object_get_data(r->repl_csngen), &smod);
    if (rc != 0) {
        replica_unlock(r->repl_lock);
        return;
    }

    r->state_update_inprogress = PR_TRUE;
    r->repl_csn_assigned = PR_FALSE;

    dn = _replica_get_config_dn(r->repl_root);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_update_state - Failed to get the config dn for %s\n",
                      slapi_sdn_get_dn(r->repl_root));
        replica_unlock(r->repl_lock);
        return;
    }
    pb = slapi_pblock_new();
    mods[0] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod);

    /* we don't want to held lock during operations since it causes lock contention
       and sometimes deadlock. So releasing lock here */

    replica_unlock(r->repl_lock);

    /* replica repl_name and new_name attributes do not get changed once
       the replica is configured - so it is ok that they are outside replica lock */

    /* write replica name if it has not been written before */
    if (r->new_name) {
        mods[1] = &mod;

        mod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        mod.mod_type = (char *)attr_replicaName;
        mod.mod_bvalues = vals;
        vals[0] = &val;
        vals[1] = NULL;
        val.bv_val = r->repl_name;
        val.bv_len = strlen(val.bv_val);
        mods[2] = NULL;
    } else {
        mods[1] = NULL;
    }

    slapi_modify_internal_set_pb(pb, dn, mods, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_modify_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_update_state - "
                                                       "Failed to update state of csn generator for replica %s: LDAP "
                                                       "error - %d\n",
                      slapi_sdn_get_dn(r->repl_root), rc);
    } else {
        r->new_name = PR_FALSE;
    }

    /* update RUV - performs its own locking */
    if (replica_write_ruv(r)) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_update_state - Failed write RUV for %s\n",
                      slapi_sdn_get_dn(r->repl_root));
    }

    /* since this is the only place this value is changed and we are
       guaranteed that only one thread enters the function, its ok
       to change it outside replica lock */
    r->state_update_inprogress = PR_FALSE;

    slapi_ch_free((void **)&dn);
    slapi_pblock_destroy(pb);
    slapi_mod_done(&smod);

}

int
replica_write_ruv(Replica *r)
{
    int rc = LDAP_SUCCESS;
    Slapi_Mod smod, rmod;
    Slapi_Mod smod_last_modified;
    LDAPMod *mods[4];
    Slapi_PBlock *pb;

    PR_ASSERT(r);

    replica_lock(r->repl_lock);

    PR_ASSERT(r->repl_ruv);

    ruv_to_smod((RUV *)object_get_data(r->repl_ruv), &smod);
    ruv_last_modified_to_smod((RUV *)object_get_data(r->repl_ruv), &smod_last_modified);

    replica_unlock(r->repl_lock);

    mods[0] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod);
    mods[1] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod_last_modified);
    if (agmt_maxcsn_to_smod(r, &rmod) == LDAP_SUCCESS) {
        mods[2] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&rmod);
    } else {
        mods[2] = NULL;
    }
    mods[3] = NULL;
    pb = slapi_pblock_new();

    /* replica name never changes so it is ok to reference it outside the lock */
    slapi_modify_internal_set_pb_ext(
        pb,
        r->repl_root, /* only used to select be */
        mods,
        NULL, /* controls */
        RUV_STORAGE_ENTRY_UNIQUEID,
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        /* Add OP_FLAG_TOMBSTONE_ENTRY so that this doesn't get logged in the Retro ChangeLog */
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | OP_FLAG_TOMBSTONE_ENTRY |
            OP_FLAG_REPL_RUV);
    slapi_modify_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    /* ruv does not exist - create one */
    replica_lock(r->repl_lock);

    if (rc == LDAP_NO_SUCH_OBJECT) {
        /* this includes an internal operation - but since this only happens
           during server startup - its ok that we have lock around it */
        rc = _replica_configure_ruv(r, PR_TRUE);
    } else if (rc != LDAP_SUCCESS) { /* error */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_write_ruv - Failed to update RUV tombstone for %s; "
                      "LDAP error - %d\n",
                      slapi_sdn_get_dn(r->repl_root), rc);
    }

    replica_unlock(r->repl_lock);

    slapi_mod_done(&smod);
    slapi_mod_done(&rmod);
    slapi_mod_done(&smod_last_modified);
    slapi_pblock_destroy(pb);

    return rc;
}


/* This routine figures out if an operation is for a replicated area and if so,
 * pulls out the operation CSN and returns it through the smods parameter.
 * It also informs the caller of the RUV entry's unique ID, since the caller
 * may not have access to the macro in repl5.h. */
int
replica_ruv_smods_for_op(Slapi_PBlock *pb, char **uniqueid, Slapi_Mods **smods)
{
    Object *ruv_obj;
    Replica *replica;
    RUV *ruv;
    RUV *ruv_copy;
    CSN *opcsn = NULL;
    Slapi_Mod smod;
    Slapi_Mod smod_last_modified;
    Slapi_Operation *op;
    Slapi_Entry *target_entry = NULL;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &target_entry);
    if (target_entry && is_ruv_tombstone_entry(target_entry)) {
        /* disallow direct modification of the RUV tombstone entry
           must use the CLEANRUV task instead */
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_ruv_smods_for_op - Attempted to directly modify the tombstone RUV "
                      "entry [%s] - use the CLEANALLRUV task instead\n",
                      slapi_entry_get_dn_const(target_entry));
        return (-1);
    }

    replica = replica_get_replica_for_op(pb);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (NULL != replica && NULL != op) {
        opcsn = operation_get_csn(op);
    }

    /* If the op has no CSN then it's not in a replicated area, so we're done */
    if (NULL == opcsn) {
        return (0);
    }

    ruv_obj = replica_get_ruv(replica);
    PR_ASSERT(ruv_obj);

    ruv = (RUV *)object_get_data(ruv_obj);
    PR_ASSERT(ruv);

    ruv_copy = ruv_dup(ruv);

    object_release(ruv_obj);

    rc = ruv_set_max_csn_ext(ruv_copy, opcsn, NULL, PR_TRUE);
    if (rc == RUV_COVERS_CSN) { /* change would "revert" RUV - ignored */
        rc = 0;                 /* tell caller to ignore */
    } else if (rc == RUV_SUCCESS) {
        rc = 1;  /* tell caller success */
    } else {     /* error */
        rc = -1; /* tell caller error */
    }

    if (rc == 1) {
        ruv_to_smod(ruv_copy, &smod);
        ruv_last_modified_to_smod(ruv_copy, &smod_last_modified);
    }
    ruv_destroy(&ruv_copy);

    if (rc == 1) {
        *smods = slapi_mods_new();
        slapi_mods_add_smod(*smods, &smod);
        slapi_mods_add_smod(*smods, &smod_last_modified);
        *uniqueid = slapi_ch_strdup(RUV_STORAGE_ENTRY_UNIQUEID);
    } else {
        *smods = NULL;
        *uniqueid = NULL;
    }

    return rc;
}


const CSN *
entry_get_deletion_csn(Slapi_Entry *e)
{
    const CSN *deletion_csn = NULL;

    PR_ASSERT(NULL != e);
    if (NULL != e) {
        Slapi_Attr *oc_attr = NULL;
        if (entry_attr_find_wsi(e, SLAPI_ATTR_OBJECTCLASS, &oc_attr) == ATTRIBUTE_PRESENT) {
            Slapi_Value *tombstone_value = NULL;
            struct berval v;
            v.bv_val = SLAPI_ATTR_VALUE_TOMBSTONE;
            v.bv_len = strlen(SLAPI_ATTR_VALUE_TOMBSTONE);
            if (attr_value_find_wsi(oc_attr, &v, &tombstone_value) == VALUE_PRESENT) {
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
    if (NULL == tombstone_dn || NULL == uniqueid) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_delete_tombstone - "
                                                       "NULL tombstone_dn or uniqueid provided.\n");
    } else {
        int ldaprc;
        Slapi_PBlock *pb = slapi_pblock_new();
        slapi_delete_internal_set_pb(pb, tombstone_dn, NULL, /* controls */
                                     uniqueid, repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                     OP_FLAG_TOMBSTONE_ENTRY | ext_op_flags);
        slapi_delete_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ldaprc);
        if (LDAP_SUCCESS != ldaprc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "_delete_tombstone - Unable to delete tombstone %s, "
                          "uniqueid %s: %s.\n",
                          tombstone_dn, uniqueid,
                          ldap_err2string(ldaprc));
        }
        slapi_pblock_destroy(pb);
    }
}

static void
get_reap_result(int rc, void *cb_data)
{
    PR_ASSERT(cb_data);

    ((reap_callback_data *)cb_data)->rc = rc;
}

static int
process_reap_entry(Slapi_Entry *entry, void *cb_data)
{
    char deletion_csn_str[CSN_STRSIZE];
    char purge_csn_str[CSN_STRSIZE];
    uint64_t *num_entriesp = &((reap_callback_data *)cb_data)->num_entries;
    uint64_t *num_purged_entriesp = &((reap_callback_data *)cb_data)->num_purged_entries;
    CSN *purge_csn = ((reap_callback_data *)cb_data)->purge_csn;
    /* this is a pointer into the actual value in the Replica object - so that
       if the value is set in the replica, we will know about it immediately */
    PRBool *tombstone_reap_stop = ((reap_callback_data *)cb_data)->tombstone_reap_stop;
    const CSN *deletion_csn = NULL;
    CSN *tombstone_csn = NULL;
    int rc = -1;

    /* abort reaping if we've been told to stop or we're shutting down */
    if (*tombstone_reap_stop || slapi_is_shutting_down()) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "process_reap_entry - The tombstone reap process "
                      " has been stopped\n");
        return rc;
    }

    /* we only ask for the objectclass in the search - the deletion csn is in the
       objectclass attribute values - if we need more attributes returned by the
       search in the future, see _replica_reap_tombstones below and add more to the
       attrs array */
    deletion_csn = entry_get_deletion_csn(entry);
    if (deletion_csn == NULL) {
        /* this might be a tombstone which was directly added, eg a cenotaph
         * check if a tombstonecsn exist and use it
         */
        char *tombstonecsn_str = (char*)slapi_entry_attr_get_ref(entry, SLAPI_ATTR_TOMBSTONE_CSN);
        if (tombstonecsn_str) {
            tombstone_csn = csn_new_by_string(tombstonecsn_str);
            deletion_csn = tombstone_csn;
        }
    }

    if ((NULL == deletion_csn || csn_compare(deletion_csn, purge_csn) < 0) &&
        (!is_ruv_tombstone_entry(entry))) {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "process_reap_entry - Removing tombstone %s "
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
    } else {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "process_reap_entry - NOT removing tombstone "
                          "%s\n",
                          slapi_entry_get_dn(entry));
        }
    }
    if (!is_ruv_tombstone_entry(entry)) {
        /* Don't update the count for the database tombstone entry */
        (*num_entriesp)++;
    }
    if (tombstone_csn) {
        csn_free(&tombstone_csn);
    }

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
    Replica *replica = NULL;
    CSN *purge_csn = NULL;

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "_replica_reap_tombstones - Beginning tombstone reap for replica %s.\n",
                  replica_name ? replica_name : "(null)");

    if (NULL == replica_name) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "_replica_reap_tombstones - Replica name is null in tombstone reap\n");
        goto done;
    }

    replica = replica_get_by_name(replica_name);
    if (NULL == replica) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "_replica_reap_tombstones - Replica object %s is null in tombstone reap\n", replica_name);
        goto done;
    }

    if (replica->tombstone_reap_stop) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "_replica_reap_tombstones - Replica %s reap stop flag is set for tombstone reap\n", replica_name);
        goto done;
    }

    purge_csn = replica_get_purge_csn(replica);
    if (NULL != purge_csn) {
        LDAPControl **ctrls;
        reap_callback_data cb_data;
        char deletion_csn_str[CSN_STRSIZE];
        char tombstone_filter[128];
        char **attrs = NULL;
        int oprc;

        if (replica_get_precise_purging(replica)) {
            /*
             * Using precise tombstone purging.  Create filter to lookup the exact
             * entries that need to be purged by using a range search on the new
             * tombstone csn index.
             */
            csn_as_string(purge_csn, PR_FALSE, deletion_csn_str);
            PR_snprintf(tombstone_filter, 128,
                        "(&(%s<=%s)(objectclass=nsTombstone)(|(objectclass=*)(objectclass=ldapsubentry)))", SLAPI_ATTR_TOMBSTONE_CSN,
                        csn_as_string(purge_csn, PR_FALSE, deletion_csn_str));
        } else {
            /* Use the old inefficient filter */
            PR_snprintf(tombstone_filter, 128, "(&(objectclass=nsTombstone)(|(objectclass=*)(objectclass=ldapsubentry)))");
        }

        /* we just need the objectclass - for the deletion csn
           and the dn and nsuniqueid - for possible deletion
           and tombstonenumsubordinates to check if it has numsubordinates
           saves time to return only 3 attrs
        */
        charray_add(&attrs, slapi_ch_strdup("objectclass"));
        charray_add(&attrs, slapi_ch_strdup("nsuniqueid"));
        charray_add(&attrs, slapi_ch_strdup("tombstonenumsubordinates"));
        charray_add(&attrs, slapi_ch_strdup(SLAPI_ATTR_TOMBSTONE_CSN));

        ctrls = (LDAPControl **)slapi_ch_calloc(3, sizeof(LDAPControl *));
        ctrls[0] = create_managedsait_control();
        ctrls[1] = create_backend_control(replica->repl_root);
        ctrls[2] = NULL;
        pb = slapi_pblock_new();
        slapi_search_internal_set_pb(pb, slapi_sdn_get_dn(replica->repl_root),
                                     LDAP_SCOPE_SUBTREE, tombstone_filter,
                                     attrs, 0, ctrls, NULL,
                                     repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                     OP_FLAG_REVERSE_CANDIDATE_ORDER);

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

        if (LDAP_SUCCESS != oprc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "_replica_reap_tombstones - Failed when searching for "
                          "tombstones in replica %s: %s. Will try again in %" PRId64 " "
                          "seconds.\n",
                          slapi_sdn_get_dn(replica->repl_root),
                          ldap_err2string(oprc), replica->tombstone_reap_interval);
        } else {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "_replica_reap_tombstones - Purged %" PRIu64 " of %" PRIu64 " tombstones "
                          "in replica %s. Will try again in %" PRId64 " "
                          "seconds.\n",
                          cb_data.num_purged_entries, cb_data.num_entries,
                          slapi_sdn_get_dn(replica->repl_root),
                          replica->tombstone_reap_interval);
        }
    } else {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "_replica_reap_tombstones - No purge CSN for tombstone reap for replica %s.\n",
                      replica_name);
    }

done:
    if (replica) {
        replica_lock(replica->repl_lock);
        replica->tombstone_reap_active = PR_FALSE;
        replica_unlock(replica->repl_lock);
    }

    if (NULL != purge_csn) {
        csn_free(&purge_csn);
    }
    if (NULL != pb) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
    }

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "_replica_reap_tombstones - Finished tombstone reap for replica %s.\n",
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
eq_cb_reap_tombstones(time_t when __attribute__((unused)), void *arg)
{
    const char *replica_name = (const char *)arg;
    Replica *replica = NULL;

    if (NULL != replica_name) {
        replica = replica_get_by_name(replica_name);
        if (replica) {
            replica_lock(replica->repl_lock);

            /* No action if purge is disabled or the previous purge is not done yet */
            if (replica->tombstone_reap_interval != 0 &&
                replica->tombstone_reap_active == PR_FALSE) {
                /* set the flag here to minimize race conditions */
                replica->tombstone_reap_active = PR_TRUE;
                if (PR_CreateThread(PR_USER_THREAD,
                                    _replica_reap_tombstones, (void *)replica_name,
                                    PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                                    SLAPD_DEFAULT_THREAD_STACKSIZE) == NULL) {
                    replica->tombstone_reap_active = PR_FALSE;
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                  "eq_cb_reap_tombstones - Unable to create the tombstone reap thread for replica %s.  "
                                  "Possible system resources problem\n",
                                  replica_name);
                }
            }
            /* reap thread will wait until this lock is released */
            replica_unlock(replica->repl_lock);
        }
        replica = NULL;
    }
}

static char *
_replica_type_as_string(const Replica *r)
{
    switch (r->repl_type) {
    case REPLICA_TYPE_PRIMARY:
        return "primary";
    case REPLICA_TYPE_READONLY:
        return "read-only";
    case REPLICA_TYPE_UPDATABLE:
        return "updatable";
    default:
        return "unknown";
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
    if (r->repl_ruv == NULL) {
        CSNGen *gen;
        CSN *csn;
        char csnstr[CSN_STRSIZE];

        /* first attempt to write RUV tombstone - need to create RUV */
        gen = (CSNGen *)object_get_data(r->repl_csngen);
        PR_ASSERT(gen);

        if (csngen_new_csn(gen, &csn, PR_FALSE /* notify */) == CSN_SUCCESS) {
            (void)csn_as_string(csn, PR_FALSE, csnstr);
            csn_free(&csn);

            /*
             * if this is an updateable replica - add its own
             * element to the RUV so that referrals work correctly
             */
            if (r->repl_type == REPLICA_TYPE_UPDATABLE)
                purl = multisupplier_get_local_purl();

            if (ruv_init_new(csnstr, r->repl_rid, purl, &ruv) == RUV_SUCCESS) {
                r->repl_ruv = object_new((void *)ruv, (FNFree)ruv_destroy);
                return_value = LDAP_SUCCESS;
            } else {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_create_ruv_tombstone - "
                                                               "Cannot create new replica update vector for %s\n",
                              slapi_sdn_get_dn(r->repl_root));
                ruv_destroy(&ruv);
                goto done;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_create_ruv_tombstone - "
                                                           "Cannot obtain CSN for new replica update vector for %s\n",
                          slapi_sdn_get_dn(r->repl_root));
            csn_free(&csn);
            goto done;
        }
    } else { /* failed to write the entry because DB was not initialized - retry */
        ruv = (RUV *)object_get_data(r->repl_ruv);
        PR_ASSERT(ruv);
    }

    PR_ASSERT(r->repl_ruv);

    rc = ruv_to_bervals(ruv, &bvals);
    if (rc != RUV_SUCCESS) {
        goto done;
    }

    /* ONREPL this is depricated function but there is currently no better API to use */
    rc = slapi_entry_add_values(e, type_ruvElement, bvals);
    if (rc != 0) {
        goto done;
    }

    pb = slapi_pblock_new();
    slapi_add_entry_internal_set_pb(pb, e, NULL /* controls */, repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                    OP_FLAG_TOMBSTONE_ENTRY | OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | OP_FLAG_REPL_RUV);
    slapi_add_internal_pb(pb);
    e = NULL; /* add consumes e, upon success or failure */
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &return_value);

done:
    slapi_entry_free(e);

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

    ruv_obj = replica_get_ruv(r);
    PR_ASSERT(ruv_obj);
    ruv = (RUV *)object_get_data(ruv_obj);
    PR_ASSERT(ruv);

    replica_lock(r->repl_lock);

    r->repl_csn_assigned = PR_TRUE;

    if (NULL != r->min_csn_pl) {
        if (csnplInsert(r->min_csn_pl, csn, NULL) != 0) {
            char csn_str[CSN_STRSIZE]; /* For logging only */
            /* Ack, we can't keep track of min csn. Punt. */
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "assign_csn_callback - "
                                                                "Failed to insert csn %s for replica %s\n",
                              csn_as_string(csn, PR_FALSE, csn_str),
                              slapi_sdn_get_dn(r->repl_root));
            }
            csnplFree(&r->min_csn_pl);
        }
    }

    ruv_add_csn_inprogress(r, ruv, csn);

    replica_unlock(r->repl_lock);

    object_release(ruv_obj);
}


static void
abort_csn_callback(const CSN *csn, void *data)
{
    Replica *r = (Replica *)data;
    Object *ruv_obj;
    RUV *ruv;

    PR_ASSERT(NULL != csn);
    PR_ASSERT(NULL != data);

    ruv_obj = replica_get_ruv(r);
    PR_ASSERT(ruv_obj);
    ruv = (RUV *)object_get_data(ruv_obj);
    PR_ASSERT(ruv);

    replica_lock(r->repl_lock);

    if (NULL != r->min_csn_pl) {
        int rc = csnplRemove(r->min_csn_pl, csn);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "abort_csn_callback - csnplRemove failed\n");
            replica_unlock(r->repl_lock);
            return;
        }
    }

    ruv_cancel_csn_inprogress(r, ruv, csn, replica_get_rid(r));
    replica_unlock(r->repl_lock);

    object_release(ruv_obj);
}

static CSN *
_replica_get_purge_csn_nolock(const Replica *r)
{
    CSN *purge_csn = NULL;
    CSN **csns = NULL;
    RUV *ruv;
    int i;

    if (r->repl_purge_delay > 0) {
        /* get a sorted list of all maxcsns in ruv in ascend order */
        object_acquire(r->repl_ruv);
        ruv = object_get_data(r->repl_ruv);
        csns = cl5BuildCSNList(ruv, NULL);
        object_release(r->repl_ruv);

        if (csns == NULL)
            return NULL;

        /* locate the most recent maxcsn in the csn list */
        for (i = 0; csns[i]; i++)
            ;
        purge_csn = csn_dup(csns[i - 1]);

        /* set purge_csn to the most recent maxcsn - purge_delay */
        if ((csn_get_time(purge_csn) - r->repl_purge_delay) > 0)
            csn_set_time(purge_csn, csn_get_time(purge_csn) - r->repl_purge_delay);
    }

    if (csns)
        cl5DestroyCSNList(&csns);

    return purge_csn;
}

static void
replica_get_referrals_nolock(const Replica *r, char ***referrals)
{
    if (referrals != NULL) {

        int hint;
        int i = 0;
        Slapi_Value *v = NULL;

        if (NULL == r->repl_referral) {
            *referrals = NULL;
        } else {
            /* richm: +1 for trailing NULL */
            *referrals = (char **)slapi_ch_calloc(sizeof(char *), 1 + slapi_valueset_count(r->repl_referral));
            hint = slapi_valueset_first_value(r->repl_referral, &v);
            while (v != NULL) {
                const char *s = slapi_value_get_string(v);
                if (s != NULL && s[0] != '\0') {
                    (*referrals)[i] = slapi_ch_strdup(s);
                    i++;
                }
                hint = slapi_valueset_next_value(r->repl_referral, hint, &v);
            }
            (*referrals)[i] = NULL;
        }
    }
}

static int
replica_log_start_iteration(const ruv_enum_data *rid_data, void *data)
{
    int rc = 0;
    slapi_operation_parameters op_params;
    Replica *replica = (Replica *)data;
    cldb_Handle *cldb = NULL;

    if (rid_data->csn == NULL)
        return 0;

    memset(&op_params, 0, sizeof(op_params));
    op_params.operation_type = SLAPI_OPERATION_DELETE;
    op_params.target_address.sdn = slapi_sdn_new_ndn_byval(START_ITERATION_ENTRY_DN);
    op_params.target_address.uniqueid = START_ITERATION_ENTRY_UNIQUEID;
    op_params.csn = csn_dup(rid_data->csn);
    cldb = replica_get_cl_info(replica);
    rc = cl5WriteOperation(cldb, &op_params);
    if (rc == CL5_SUCCESS)
        rc = 0;
    else
        rc = -1;

    slapi_sdn_free(&op_params.target_address.sdn);
    csn_free(&op_params.csn);

    return rc;
}

static int
replica_log_ruv_elements_nolock(const Replica *r)
{
    int rc = 0;
    RUV *ruv;

    ruv = (RUV *)object_get_data(r->repl_ruv);
    PR_ASSERT(ruv);

    /* we log it as a delete operation to have the least number of fields
           to set. the entry can be identified by a special target uniqueid and
           special target dn */
    rc = ruv_enumerate_elements(ruv, replica_log_start_iteration, (void *)r);
    return rc;
}

void
replica_set_purge_delay(Replica *r, uint32_t purge_delay)
{
    PR_ASSERT(r);
    replica_lock(r->repl_lock);
    r->repl_purge_delay = purge_delay;
    replica_unlock(r->repl_lock);
}

void
replica_set_tombstone_reap_interval(Replica *r, long interval)
{
    replica_lock(r->repl_lock);

    /*
     * Leave the event there to purge the existing tombstones
     * if we are about to turn off tombstone creation
     */
    if (interval > 0 && r->repl_eqcxt_tr && r->tombstone_reap_interval != interval) {
        int found;

        found = slapi_eq_cancel_rel(r->repl_eqcxt_tr);
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_set_tombstone_reap_interval - tombstone_reap event (interval=%" PRId64 ") was %s\n",
                      r->tombstone_reap_interval, (found ? "cancelled" : "not found"));
        r->repl_eqcxt_tr = NULL;
    }
    r->tombstone_reap_interval = interval;
    if (interval > 0 && r->repl_eqcxt_tr == NULL) {
        r->repl_eqcxt_tr = slapi_eq_repeat_rel(eq_cb_reap_tombstones, r->repl_name,
                                           slapi_current_rel_time_t() + r->tombstone_reap_interval,
                                           1000 * r->tombstone_reap_interval);
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_set_tombstone_reap_interval - tombstone_reap event (interval=%" PRId64 ") was %s\n",
                      r->tombstone_reap_interval, (r->repl_eqcxt_tr ? "scheduled" : "not scheduled successfully"));
    }
    replica_unlock(r->repl_lock);
}

void
replica_set_keepalive_update_interval(Replica *r, int64_t interval)
{
    replica_lock(r->repl_lock);
    r->keepalive_update_interval = interval;
    replica_unlock(r->repl_lock);
}

int64_t
replica_get_keepalive_update_interval(Replica *r)
{
    int64_t interval = DEFAULT_REPLICA_KEEPALIVE_UPDATE_INTERVAL;

    replica_lock(r->repl_lock);
    interval = r->keepalive_update_interval;
    replica_unlock(r->repl_lock);

    return interval;
}

static void
replica_strip_cleaned_rids(Replica *r)
{
    Object *RUVObj;
    RUV *ruv = NULL;
    ReplicaId rid[32] = {0};
    int i = 0;

    RUVObj = replica_get_ruv(r);
    ruv = (RUV *)object_get_data(RUVObj);

    ruv_get_cleaned_rids(ruv, rid);
    while (rid[i] != 0) {
        ruv_delete_replica(ruv, rid[i]);
        if (replica_write_ruv(r)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "replica_strip_cleaned_rids - Failed to write RUV\n");
        }
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
    LDAPMod *mods[3];
    char *dn;
    int rc;

    PR_ASSERT(NULL != r && NULL != r->repl_root);

    replica_strip_cleaned_rids(r);

    replica_lock(r->repl_lock);

    PR_ASSERT(r->repl_ruv);
    ruv_to_smod((RUV *)object_get_data(r->repl_ruv), &smod);
    ruv_last_modified_to_smod((RUV *)object_get_data(r->repl_ruv), &smod_last_modified);

    dn = _replica_get_config_dn(r->repl_root);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_replace_ruv_tombstone - "
                      "Failed to get the config dn for %s\n",
                      slapi_sdn_get_dn(r->repl_root));
        replica_unlock(r->repl_lock);
        goto bail;
    }
    mods[0] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod);
    mods[1] = (LDAPMod *)slapi_mod_get_ldapmod_byref(&smod_last_modified);

    replica_unlock(r->repl_lock);

    mods[2] = NULL;
    pb = slapi_pblock_new();

    slapi_modify_internal_set_pb_ext(
        pb,
        r->repl_root, /* only used to select be */
        mods,
        NULL, /* controls */
        RUV_STORAGE_ENTRY_UNIQUEID,
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | OP_FLAG_REPL_RUV | OP_FLAG_TOMBSTONE_ENTRY);

    slapi_modify_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    if (rc != LDAP_SUCCESS) {
        if ((rc != LDAP_NO_SUCH_OBJECT && rc != LDAP_TYPE_OR_VALUE_EXISTS) || !replica_is_state_flag_set(r, REPLICA_IN_USE)) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_replace_ruv_tombstone - "
                                                           "Failed to update replication update vector for replica %s: LDAP "
                                                           "error - %d\n",
                          (char *)slapi_sdn_get_dn(r->repl_root), rc);
        }
    }

    slapi_ch_free((void **)&dn);
    slapi_pblock_destroy(pb);
bail:
    slapi_mod_done(&smod);
    slapi_mod_done(&smod_last_modified);
}

void
replica_update_ruv_consumer(Replica *r, RUV *supplier_ruv)
{
    ReplicaId supplier_id = 0;
    char *supplier_purl = NULL;

    if (ruv_get_first_id_and_purl(supplier_ruv, &supplier_id, &supplier_purl) == RUV_SUCCESS) {
        RUV *local_ruv = NULL;

        replica_lock(r->repl_lock);

        local_ruv = (RUV *)object_get_data(r->repl_ruv);
        if (is_cleaned_rid(supplier_id) || local_ruv == NULL ||
                !replica_check_generation(r, supplier_ruv)) {
            replica_unlock(r->repl_lock);
            return;
        }

        if (ruv_local_contains_supplier(local_ruv, supplier_id) == 0) {
            if (r->repl_type == REPLICA_TYPE_UPDATABLE) {
                /* Add the new ruv right after the consumer own purl */
                ruv_add_index_replica(local_ruv, supplier_id, supplier_purl, 2);
            } else {
                /* This is a consumer only, add it first */
                ruv_add_index_replica(local_ruv, supplier_id, supplier_purl, 1);
            }
        } else {
            /* Replace it */
            ruv_replace_replica_purl(local_ruv, supplier_id, supplier_purl);
        }
        replica_unlock(r->repl_lock);

        /* Update also the directory entry */
        replica_replace_ruv_tombstone(r);
    }
}

PRBool
replica_is_state_flag_set(Replica *r, int32_t flag)
{
    PR_ASSERT(r);
    if (r)
        return (r->repl_state_flags & flag);
    else
        return PR_FALSE;
}

void
replica_set_state_flag(Replica *r, uint32_t flag, PRBool clear)
{
    if (r == NULL)
        return;

    replica_lock(r->repl_lock);

    if (clear) {
        r->repl_state_flags &= ~flag;
    } else {
        r->repl_state_flags |= flag;
    }

    replica_unlock(r->repl_lock);
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

    replica_lock(r->repl_lock);
    r->tombstone_reap_stop = val;
    replica_unlock(r->repl_lock);
}

/* replica just came back online, probably after data was reloaded */
void
replica_enable_replication(Replica *r)
{
    int rc;

    PR_ASSERT(r);

    /* prevent creation of new agreements until the replica is enabled */
    PR_Lock(r->agmt_lock);
    if (r->repl_flags & REPLICA_LOG_CHANGES) {
        cldb_SetReplicaDB(r, NULL);
    }

    /* retrieve new ruv */
    rc = replica_reload_ruv(r);
    if (rc) {
        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replica_enable_replication - "
                                                           "Reloading ruv failed\n");
        /* What to do ? */
    }

    /* Replica came back online, Check if the total update was terminated.
       If flag is still set, it was not terminated, therefore the data is
       very likely to be incorrect, and we should not restart Replication threads...
    */
    if (!replica_is_state_flag_set(r, REPLICA_TOTAL_IN_PROGRESS)) {
        /* restart outbound replication */
        start_agreements_for_replica(r, PR_TRUE);

        /* enable ruv state update */
        replica_set_enabled(r, PR_TRUE);
    }

    /* mark the replica as being available for updates */
    replica_relinquish_exclusive_access(r, 0, 0);

    replica_set_state_flag(r, REPLICA_AGREEMENTS_DISABLED, PR_TRUE /* clear */);
    PR_Unlock(r->agmt_lock);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_enable_replication - "
                                                    "Replica %s is relinquished\n",
                  slapi_sdn_get_ndn(replica_get_root(r)));
}

/* replica is about to be taken offline */
void
replica_disable_replication(Replica *r)
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
    replica_set_enabled(r, PR_FALSE);

    /* disable outbound replication */
    start_agreements_for_replica(r, PR_FALSE);

    /* close the corresponding changelog file */
    /* close_changelog_for_replica (r_obj); */

    /* mark the replica as being unavailable for updates */
    /* If an incremental update is in progress, we want to wait until it is
       finished until we get exclusive access to the replica, because we have
       to make sure no operations are in progress - it messes up replication
       when a restore is in progress but we are still adding replicated entries
       from a supplier
    */
    repl_ruv = (RUV *)object_get_data(r->repl_ruv);
    ruv_get_first_id_and_purl(repl_ruv, &junkrid, &p_locking_purl);
    locking_purl = slapi_ch_strdup(p_locking_purl);
    p_locking_purl = NULL;
    repl_ruv = NULL;
    while (!replica_get_exclusive_access(r, &isInc, 0, 0, "replica_disable_replication",
                                         &current_purl)) {
        if (!isInc) /* already locked, but not by inc update - break */
            break;
        isInc = PR_FALSE;
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "replica_disable_replication - "
                      "replica %s is already locked by (%s) for incoming "
                      "incremental update; sleeping 100ms\n",
                      slapi_sdn_get_ndn(replica_get_root(r)),
                      current_purl ? current_purl : "unknown");
        slapi_ch_free_string(&current_purl);
        DS_Sleep(PR_MillisecondsToInterval(100));
    }

    slapi_ch_free_string(&current_purl);
    slapi_ch_free_string(&locking_purl);
    replica_set_state_flag(r, REPLICA_AGREEMENTS_DISABLED, PR_FALSE);
    PR_Unlock(r->agmt_lock);
    /* no thread will access the changelog for this replica
     * remove reference from replica object
     */
    if (r->repl_flags & REPLICA_LOG_CHANGES) {
        int32_t write_ruv = 1;
        cldb_UnSetReplicaDB(r, (void *)&write_ruv);
    }

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "replica_disable_replication - "
                                                    "replica %s is acquired\n",
                  slapi_sdn_get_ndn(replica_get_root(r)));
}

static void
start_agreements_for_replica(Replica *r, PRBool start)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;

    agmt_obj = agmtlist_get_first_agreement_for_replica(r);
    while (agmt_obj) {
        agmt = (Repl_Agmt *)object_get_data(agmt_obj);
        PR_ASSERT(agmt);
        if (agmt_is_enabled(agmt)) {
            if (start)
                agmt_start(agmt);
            else /* stop */
                agmt_stop(agmt);
        }
        agmt_obj = agmtlist_get_next_agreement_for_replica(r, agmt_obj);
    }
}

int
replica_start_agreement(Replica *r, Repl_Agmt *ra)
{
    int ret = 0;

    if (r == NULL)
        return -1;

    PR_Lock(r->agmt_lock);

    if (!replica_is_state_flag_set(r, REPLICA_AGREEMENTS_DISABLED) && agmt_is_enabled(ra)) {
        ret = agmt_start(ra); /* Start the replication agreement */
    }

    PR_Unlock(r->agmt_lock);
    return ret;
}

int
windows_replica_start_agreement(Replica *r, Repl_Agmt *ra)
{
    int ret = 0;

    if (r == NULL)
        return -1;

    PR_Lock(r->agmt_lock);

    if (!replica_is_state_flag_set(r, REPLICA_AGREEMENTS_DISABLED)) {
        ret = windows_agmt_start(ra); /* Start the replication agreement */
                                      /* ret = windows_agmt_start(ra); Start the replication agreement */
    }

    PR_Unlock(r->agmt_lock);
    return ret;
}


/*
 * A callback function registered as op->o_csngen_handler and
 * called by backend ops to generate opcsn.
 */
int32_t
replica_generate_next_csn(Slapi_PBlock *pb, const CSN *basecsn, CSN **opcsn)
{
    Replica *replica = replica_get_replica_for_op(pb);
    if (NULL != replica) {
        Slapi_Operation *op;
        slapi_pblock_get(pb, SLAPI_OPERATION, &op);
        if (replica->repl_type != REPLICA_TYPE_READONLY) {
            Object *gen_obj = replica_get_csngen(replica);
            if (NULL != gen_obj) {
                CSNGen *gen = (CSNGen *)object_get_data(gen_obj);
                if (NULL != gen) {
                    /* The new CSN should be greater than the base CSN */
                    if (csngen_new_csn(gen, opcsn, PR_FALSE /* don't notify */) != CSN_SUCCESS) {
                        /* Failed to generate CSN we must abort */
                        object_release(gen_obj);
                        return -1;
                    }
                    if (csn_compare(*opcsn, basecsn) <= 0) {
                        char opcsnstr[CSN_STRSIZE];
                        char basecsnstr[CSN_STRSIZE];
                        char opcsn2str[CSN_STRSIZE];

                        csn_as_string(*opcsn, PR_FALSE, opcsnstr);
                        csn_as_string(basecsn, PR_FALSE, basecsnstr);
                        csn_free(opcsn);
                        csngen_adjust_time(gen, basecsn);
                        if (csngen_new_csn(gen, opcsn, PR_FALSE) != CSN_SUCCESS) {
                            /* Failed to generate CSN we must abort */
                            object_release(gen_obj);
                            return -1;
                        }
                        csn_as_string(*opcsn, PR_FALSE, opcsn2str);
                        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                                      "replica_generate_next_csn - "
                                      "opcsn=%s <= basecsn=%s, adjusted opcsn=%s\n",
                                      opcsnstr, basecsnstr, opcsn2str);
                    }
                    /*
                     * Insert opcsn into the csn pending list.
                     * This is the notify effect in csngen_new_csn().
                     */
                    assign_csn_callback(*opcsn, (void *)replica);
                }
                object_release(gen_obj);
            }
        }
    }

    return 0;
}

/*
 * A callback function registed as op->o_replica_attr_handler and
 * called by backend ops to get replica attributes.
 */
int
replica_get_attr(Slapi_PBlock *pb, const char *type, void *value)
{
    int rc = -1;

    Replica *replica = replica_get_replica_for_op(pb);
    if (NULL != replica) {
        if (strcasecmp(type, type_replicaTombstonePurgeInterval) == 0) {
            *((int *)value) = replica->tombstone_reap_interval;
            rc = 0;
        } else if (strcasecmp(type, type_replicaPurgeDelay) == 0) {
            *((int *)value) = replica->repl_purge_delay;
            rc = 0;
        }
    }

    return rc;
}

uint64_t
replica_get_backoff_min(Replica *r)
{
    if (r) {
        return slapi_counter_get_value(r->backoff_min);
    } else {
        return PROTOCOL_BACKOFF_MINIMUM;
    }
}

uint64_t
replica_get_backoff_max(Replica *r)
{
    if (r) {
        return slapi_counter_get_value(r->backoff_max);
    } else {
        return PROTOCOL_BACKOFF_MAXIMUM;
    }
}

void
replica_set_backoff_min(Replica *r, uint64_t min)
{
    if (r) {
        slapi_counter_set_value(r->backoff_min, min);
    }
}

void
replica_set_backoff_max(Replica *r, uint64_t max)
{
    if (r) {
        slapi_counter_set_value(r->backoff_max, max);
    }
}

void
replica_set_precise_purging(Replica *r, uint64_t on_off)
{
    if (r) {
        slapi_counter_set_value(r->precise_purging, on_off);
    }
}

uint64_t
replica_get_precise_purging(Replica *r)
{
    if (r) {
        return slapi_counter_get_value(r->precise_purging);
    } else {
        return 0;
    }
}

int
replica_get_agmt_count(Replica *r)
{
    return r->agmt_count;
}

void
replica_incr_agmt_count(Replica *r)
{
    if (r) {
        r->agmt_count++;
    }
}

void
replica_decr_agmt_count(Replica *r)
{
    if (r) {
        if (r->agmt_count > 0) {
            r->agmt_count--;
        }
    }
}

/*
 * Add the "Abort Replication Session" control to the pblock
 */
static void
replica_add_session_abort_control(Slapi_PBlock *pb)
{
    LDAPControl ctrl = {0};
    BerElement *ber;
    struct berval *bvp;
    int rc;

    /* Build the BER payload */
    if ((ber = der_alloc()) == NULL) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "add_session_abort_control - Failed to create ber\n");
        return;
    }
    rc = ber_printf(ber, "{}");
    if (rc != -1) {
        rc = ber_flatten(ber, &bvp);
    }
    ber_free(ber, 1);
    if (rc == -1) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "add_session_abort_control - Failed to flatten ber\n");
        return;
    }

    ctrl.ldctl_oid = slapi_ch_strdup(REPL_ABORT_SESSION_OID);
    ctrl.ldctl_value = *bvp;
    bvp->bv_val = NULL;
    ber_bvfree(bvp);
    slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, &ctrl);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "add_session_abort_control - abort control successfully added to result\n");
}

/*
 * Check if we have exceeded the failed replica acquire limit,
 * if so, end the replication session.
 */
void
replica_check_release_timeout(Replica *r, Slapi_PBlock *pb)
{
    replica_lock(r->repl_lock);
    if (r->abort_session == ABORT_SESSION) {
        /* Need to abort this session (just send the control once) */
        replica_add_session_abort_control(pb);
        r->abort_session = SESSION_ABORTED;
    }
    replica_unlock(r->repl_lock);
}

void
replica_lock_replica(Replica *r)
{
    replica_lock(r->repl_lock);
}

void
replica_unlock_replica(Replica *r)
{
    replica_unlock(r->repl_lock);
}

void *
replica_get_cl_info(Replica *r)
{
    return r->cldb;
}

int
replica_set_cl_info(Replica *r, void *cl)
{
    r->cldb = (cldb_Handle *)cl;
    return 0;
}
