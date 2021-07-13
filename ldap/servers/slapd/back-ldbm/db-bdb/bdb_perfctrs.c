/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Database performance counters stuff  */
#include "bdb_layer.h"

#include "bdb_perfctrs.h"

#define TXN_STAT(env, statp, flags, malloc) \
    (env)->txn_stat((env), (statp), (flags))
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
    (env)->memp_stat((env), (gsp), (fsp), (flags))
#define LOG_STAT(env, spp, flags, malloc) (env)->log_stat((env), (spp), (flags))
#define LOCK_STAT(env, statp, flags, malloc) \
    (env)->lock_stat((env), (statp), (flags))
#define GET_N_LOCK_WAITS(lockstat) lockstat->st_lock_wait

static void bdb_perfctrs_update(perfctrs_private *priv, DB_ENV *db_env);
static void bdb_perfctr_add_to_entry(Slapi_Entry *e, char *type, uint64_t countervalue);

/* Init perf ctrs */
void
bdb_perfctrs_init(struct ldbminfo *li __attribute__((unused)), perfctrs_private **ret_priv)
{
    perfctrs_private *priv = NULL;

    *ret_priv = NULL;

    /*
     * We need the perfctrs_private area on all platforms.
     */
    priv = (perfctrs_private *)slapi_ch_calloc(1, sizeof(perfctrs_private));
    priv->memory = slapi_ch_calloc(1, sizeof(performance_counters));

    *ret_priv = priv;
    return;
}

/* Terminate perf ctrs */
void
bdb_perfctrs_terminate(perfctrs_private **priv, DB_ENV *db_env)
{
    DB_MPOOL_STAT *mpstat = NULL;
    DB_TXN_STAT *txnstat = NULL;
    DB_LOG_STAT *logstat = NULL;
    DB_LOCK_STAT *lockstat = NULL;

    MEMP_STAT(db_env, &mpstat, NULL, DB_STAT_CLEAR, (void *)slapi_ch_malloc);
    slapi_ch_free((void **)&mpstat);
    TXN_STAT(db_env, &txnstat, DB_STAT_CLEAR, (void *)slapi_ch_malloc);
    slapi_ch_free((void **)&txnstat);
    LOG_STAT(db_env, &logstat, DB_STAT_CLEAR, (void *)slapi_ch_malloc);
    slapi_ch_free((void **)&logstat);
    LOCK_STAT(db_env, &lockstat, DB_STAT_CLEAR, (void *)slapi_ch_malloc);
    slapi_ch_free((void **)&lockstat);
    if (NULL != (*priv)->memory) {
        slapi_ch_free(&(*priv)->memory);
    }

    slapi_ch_free((void **)priv);
}

/* Wait while checking for perfctr update requests */
void
bdb_perfctrs_wait(size_t milliseconds, perfctrs_private *priv __attribute__((unused)), DB_ENV *db_env __attribute__((unused)))
{
    /* Just sleep */
    PRIntervalTime interval; /*NSPR timeout stuffy*/
    interval = PR_MillisecondsToInterval(milliseconds);
    DS_Sleep(interval);
}

/* Update perfctrs */
static void
bdb_perfctrs_update(perfctrs_private *priv, DB_ENV *db_env)
{
    int ret = 0;
    performance_counters *perf;
    if (NULL == priv) {
        return;
    }
    if (NULL == db_env) {
        return;
    }
    perf = (performance_counters *)priv->memory;
    if (NULL == perf) {
        return;
    }
    /* Call libdb to get the various stats */
    if (bdb_uses_logging(db_env)) {
        DB_LOG_STAT *logstat = NULL;
        ret = LOG_STAT(db_env, &logstat, 0, (void *)slapi_ch_malloc);
        if (0 == ret) {
            perf->log_region_wait_rate = logstat->st_region_wait;
            perf->log_write_rate = 1024 * 1024 * logstat->st_w_mbytes + logstat->st_w_bytes;
            perf->log_bytes_since_checkpoint = 1024 * 1024 * logstat->st_wc_mbytes + logstat->st_wc_bytes;
        }
        slapi_ch_free((void **)&logstat);
    }
    if (bdb_uses_transactions(db_env)) {
        DB_TXN_STAT *txnstat = NULL;
        ret = TXN_STAT(db_env, &txnstat, 0, (void *)slapi_ch_malloc);
        if (0 == ret) {
            perf->active_txns = txnstat->st_nactive;
            perf->commit_rate = txnstat->st_ncommits;
            perf->abort_rate = txnstat->st_naborts;
            perf->txn_region_wait_rate = txnstat->st_region_wait;
        }
        slapi_ch_free((void **)&txnstat);
    }
    if (bdb_uses_locking(db_env)) {
        DB_LOCK_STAT *lockstat = NULL;
        ret = LOCK_STAT(db_env, &lockstat, 0, (void *)slapi_ch_malloc);
        if (0 == ret) {
            perf->lock_region_wait_rate = lockstat->st_region_wait;
            perf->deadlock_rate = lockstat->st_ndeadlocks;
            perf->configured_locks = lockstat->st_maxlocks;
            perf->current_locks = lockstat->st_nlocks;
            perf->max_locks = lockstat->st_maxnlocks;
            perf->lockers = lockstat->st_nlockers;
            perf->lock_conflicts = GET_N_LOCK_WAITS(lockstat);
            perf->lock_request_rate = lockstat->st_nrequests;
            perf->current_lock_objects = lockstat->st_nobjects;
            perf->max_lock_objects = lockstat->st_maxnobjects;
        }
        slapi_ch_free((void **)&lockstat);
    }
    if (bdb_uses_mpool(db_env)) {
        DB_MPOOL_STAT *mpstat = NULL;
        ret = MEMP_STAT(db_env, &mpstat, NULL, 0, (void *)slapi_ch_malloc);
        if (0 == ret) {
#define ONEG 1073741824
            perf->cache_size_bytes = mpstat->st_gbytes * ONEG + mpstat->st_bytes;
            perf->cache_hit = mpstat->st_cache_hit;
            perf->cache_try = mpstat->st_cache_hit + mpstat->st_cache_miss;
            perf->page_create_rate = mpstat->st_page_create;
            perf->page_read_rate = mpstat->st_page_in;
            perf->page_write_rate = mpstat->st_page_out;
            perf->page_ro_evict_rate = mpstat->st_ro_evict;
            perf->page_rw_evict_rate = mpstat->st_rw_evict;
            perf->hash_buckets = mpstat->st_hash_buckets;
            perf->hash_search_rate = mpstat->st_hash_searches;
            perf->longest_chain_length = mpstat->st_hash_longest;
            perf->hash_elements_examine_rate = mpstat->st_hash_examined;
            perf->pages_in_use = mpstat->st_page_dirty + mpstat->st_page_clean;
            perf->dirty_pages = mpstat->st_page_dirty;
            perf->clean_pages = mpstat->st_page_clean;
            perf->page_trickle_rate = mpstat->st_page_trickle;
            perf->cache_region_wait_rate = mpstat->st_region_wait;
            slapi_ch_free((void **)&mpstat);
        }
    }
    /* Place the stats in the shared memory region */
    /* Bump the sequence number */
    perf->sequence_number++;
}


/*
 * Define a map (array of structures) which is used to retrieve performance
 * counters from the performance_counters structure and map them to an
 * LDAP attribute type.
 */

#define SLAPI_LDBM_PERFCTR_AT_PREFIX "nsslapd-db-"
typedef struct slapi_ldbm_perfctr_at_map
{
    char *pam_type;    /* name of LDAP attribute type */
    size_t pam_offset; /* offset into performance_counters struct */
} SlapiLDBMPerfctrATMap;

static SlapiLDBMPerfctrATMap bdb_perfctr_at_map[] = {
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "abort-rate",
     offsetof(performance_counters, abort_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "active-txns",
     offsetof(performance_counters, active_txns)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-hit",
     offsetof(performance_counters, cache_hit)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-try",
     offsetof(performance_counters, cache_try)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-region-wait-rate",
     offsetof(performance_counters, cache_region_wait_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-size-bytes",
     offsetof(performance_counters, cache_size_bytes)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "clean-pages",
     offsetof(performance_counters, clean_pages)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "commit-rate",
     offsetof(performance_counters, commit_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "deadlock-rate",
     offsetof(performance_counters, deadlock_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "dirty-pages",
     offsetof(performance_counters, dirty_pages)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "hash-buckets",
     offsetof(performance_counters, hash_buckets)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "hash-elements-examine-rate",
     offsetof(performance_counters, hash_elements_examine_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "hash-search-rate",
     offsetof(performance_counters, hash_search_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "lock-conflicts",
     offsetof(performance_counters, lock_conflicts)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "lock-region-wait-rate",
     offsetof(performance_counters, lock_region_wait_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "lock-request-rate",
     offsetof(performance_counters, lock_request_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "lockers",
     offsetof(performance_counters, lockers)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "configured-locks",
     offsetof(performance_counters, configured_locks)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "current-locks",
     offsetof(performance_counters, current_locks)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "max-locks",
     offsetof(performance_counters, max_locks)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "current-lock-objects",
     offsetof(performance_counters, current_lock_objects)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "max-lock-objects",
     offsetof(performance_counters, max_lock_objects)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "log-bytes-since-checkpoint",
     offsetof(performance_counters, log_bytes_since_checkpoint)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "log-region-wait-rate",
     offsetof(performance_counters, log_region_wait_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "log-write-rate",
     offsetof(performance_counters, log_write_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "longest-chain-length",
     offsetof(performance_counters, longest_chain_length)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "page-create-rate",
     offsetof(performance_counters, page_create_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "page-read-rate",
     offsetof(performance_counters, page_read_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "page-ro-evict-rate",
     offsetof(performance_counters, page_ro_evict_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "page-rw-evict-rate",
     offsetof(performance_counters, page_rw_evict_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "page-trickle-rate",
     offsetof(performance_counters, page_trickle_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "page-write-rate",
     offsetof(performance_counters, page_write_rate)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "pages-in-use",
     offsetof(performance_counters, pages_in_use)},
    {SLAPI_LDBM_PERFCTR_AT_PREFIX "txn-region-wait-rate",
     offsetof(performance_counters, txn_region_wait_rate)},
};
#define SLAPI_LDBM_PERFCTR_AT_MAP_COUNT \
    (sizeof(bdb_perfctr_at_map) / sizeof(SlapiLDBMPerfctrATMap))


/*
 * Set attributes and values in entry `e' based on performance counter
 * information (from `priv').
 */
void
bdb_perfctrs_as_entry(Slapi_Entry *e, perfctrs_private *priv, DB_ENV *db_env)
{
    performance_counters *perf;
    size_t i;

    if (priv == NULL)
        return;

    perf = (performance_counters *)priv->memory;

    /*
     * First, update the values so they are current.
     */
    bdb_perfctrs_update(priv, db_env);

    /*
     * Then convert all the counters to attribute values.
     */
    for (i = 0; i < SLAPI_LDBM_PERFCTR_AT_MAP_COUNT; ++i) {
        bdb_perfctr_add_to_entry(e, bdb_perfctr_at_map[i].pam_type,
                             *((uint64_t *)((char *)perf + bdb_perfctr_at_map[i].pam_offset)));
    }
}


static void
bdb_perfctr_add_to_entry(Slapi_Entry *e, char *type, uint64_t countervalue)
{
    slapi_entry_attr_set_ulong(e, type, countervalue);
}
