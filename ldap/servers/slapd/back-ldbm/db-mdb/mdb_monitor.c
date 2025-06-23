/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2019 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* monitor.c - ldbm backend monitor function */

#include "mdb_layer.h"
#include <sys/stat.h>


#define MSET(_attr)                                   \
    do {                                              \
        val.bv_val = buf;                             \
        val.bv_len = strlen(buf);                     \
        attrlist_replace(&e->e_attrs, (_attr), vals); \
    } while (0)

#define MSETF(_attr, _x)                                   \
    do {                                                   \
        char tmp_atype[37];                                \
        snprintf(tmp_atype, sizeof(tmp_atype), _attr, _x); \
        MSET(tmp_atype);                                   \
    } while (0)

#define MVSET(_val, _idx)                                  \
    do {                                                   \
        mval[_idx].bv_val = _val;                          \
        mval[_idx].bv_len = strlen(_val);                  \
        vals[_idx] = &mval[_idx];                          \
        vals[++_idx] = NULL;                               \
    } while (0)



/* DSE callback to monitor stats for a particular instance */
int
dbmdb_monitor_instance_search(Slapi_PBlock *pb __attribute__((unused)),
                            Slapi_Entry *e,
                            Slapi_Entry *entryAfter __attribute__((unused)),
                            int *returncode,
                            char *returntext __attribute__((unused)),
                            void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    struct ldbminfo *li = NULL;
    struct berval val;
    struct berval mval[3];
    struct berval *vals[4];
    char buf[BUFSIZ];
    struct cache_stats cstats = {0};

    dbmdb_stats_t *stats = NULL;
    int i, j, flags;

    /* Get the LDBM Info structure for the ldbm backend */
    if (inst->inst_be->be_database == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    li = (struct ldbminfo *)inst->inst_be->be_database->plg_private;
    if (li == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    if (inst->inst_be->be_state != BE_STATE_STARTED) {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    vals[0] = &val;
    vals[1] = NULL;

    /* database name */
    PR_snprintf(buf, sizeof(buf), "%s", li->li_plugin->plg_name);
    MSET("database");

    /* read-only status */
    PR_snprintf(buf, sizeof(buf), "%d", inst->inst_be->be_readonly);
    MSET("readOnly");

    /* fetch cache statistics */
    cache_get_stats(&(inst->inst_cache), &cstats);
    sprintf(buf, "%" PRIu64, cstats.hits);
    MSET("entryCacheHits");
    sprintf(buf, "%" PRIu64, cstats.tries);
    MSET("entryCacheTries");
    sprintf(buf, "%" PRIu64, (uint64_t)(100.0 * (double)cstats.hits / (double)(cstats.tries > 0 ? cstats.tries : 1)));
    MSET("entryCacheHitRatio");
    sprintf(buf, "%" PRIu64, cstats.size);
    MSET("currentEntryCacheSize");
    sprintf(buf, "%" PRIu64, cstats.maxsize);
    MSET("maxEntryCacheSize");
    sprintf(buf, "%" PRIu64, cstats.nentries);
    MSET("currentEntryCacheCount");
    sprintf(buf, "%" PRId64, cstats.maxentries);
    MSET("maxEntryCacheCount");
    sprintf(buf, "%" PRId64, cstats.weight / ((cstats.nehw == 0) ? 1 : cstats.nehw));
    MSET("entryCacheAverageLoadTime");

    /* fetch cache statistics */
    cache_get_stats(&(inst->inst_dncache), &cstats);
    sprintf(buf, "%" PRIu64, cstats.hits);
    MSET("dnCacheHits");
    sprintf(buf, "%" PRIu64, cstats.tries);
    MSET("dnCacheTries");
    sprintf(buf, "%" PRIu64, (uint64_t)(100.0 * (double)cstats.hits / (double)(cstats.tries > 0 ? cstats.tries : 1)));
    MSET("dnCacheHitRatio");
    sprintf(buf, "%" PRIu64, cstats.size);
    MSET("currentDnCacheSize");
    sprintf(buf, "%" PRIu64, cstats.maxsize);
    MSET("maxDnCacheSize");
    sprintf(buf, "%" PRIu64, cstats.nentries);
    MSET("currentDnCacheCount");
    sprintf(buf, "%" PRId64, cstats.maxentries);
    MSET("maxDnCacheCount");

#ifdef DEBUG
    {
        /* debugging for hash statistics */
        char *x = NULL;
        cache_debug_hash(&(inst->inst_cache), &x);
        val.bv_val = x;
        val.bv_len = strlen(x);
        attrlist_replace(&e->e_attrs, "entrycache-hashtables", vals);
        slapi_ch_free((void **)&x);
    }
#endif

    stats = dbdmd_gather_stats(MDB_CONFIG(li), inst->inst_be);

    for (i = 0; stats && i<stats->nbdbis; i++) {
        /* only print out stats on files used by this instance */
        if (!stats->dbis[i].dbname)
            continue;

        /* Get each file's stats */
        PR_snprintf(buf, sizeof(buf), "%s", stats->dbis[i].dbname);
        MSETF("dbiName-%d", i);

        flags = stats->dbis[i].flags;
        j = 0;
        if (flags & DBI_STAT_FLAGS_OPEN) {
            PR_snprintf(buf, sizeof(buf), "%s", stats->dbis[i].dbname);
            MVSET("OPEN", j);
        }
        if (flags & DBI_STAT_FLAGS_DIRTY) {
            PR_snprintf(buf, sizeof(buf), "%s", stats->dbis[i].dbname);
            MVSET("DIRTY", j);
        }
        if (flags & DBI_STAT_FLAGS_SUPPORTDUP) {
            PR_snprintf(buf, sizeof(buf), "%s", stats->dbis[i].dbname);
            MVSET("SUPPORT-DUPLICATE-KEYS", j);
        }
        MSETF("dbiFlags-%d", i);
        /* Back to single valued */
        vals[0] = &val;
        vals[1] = NULL;

        if (flags & DBI_STAT_FLAGS_OPEN) {
            PR_snprintf(buf, sizeof(buf), "%u", stats->dbis[i].stat.ms_psize);
            MSETF("dbiPageSize-%d", i);
            PR_snprintf(buf, sizeof(buf), "%u", stats->dbis[i].stat.ms_depth);
            MSETF("dbiTreeDepth-%d", i);
            PR_snprintf(buf, sizeof(buf), "%ld", stats->dbis[i].stat.ms_branch_pages);
            MSETF("dbiBranchPages-%d", i);
            PR_snprintf(buf, sizeof(buf), "%ld", stats->dbis[i].stat.ms_leaf_pages);
            MSETF("dbiLeafPages-%d", i);
            PR_snprintf(buf, sizeof(buf), "%ld", stats->dbis[i].stat.ms_overflow_pages);
            MSETF("dbiOverflowPages-%d", i);
            PR_snprintf(buf, sizeof(buf), "%ld", stats->dbis[i].stat.ms_entries);
            MSETF("dbiEntries-%d", i);
        }
    }
    dbmdb_free_stats(&stats);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


/* monitor global ldbm stats */
int
dbmdb_monitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct berval val;
    struct berval *vals[2];
    char buf[BUFSIZ];
    uint64_t count;
    uint64_t hits;
    uint64_t tries;
    uint64_t size;
    uint64_t maxsize;
    uint64_t thread_size;
    uint64_t evicts;
    uint64_t slots;

    vals[0] = &val;
    vals[1] = NULL;

    /* database name */
    PR_snprintf(buf, sizeof(buf), "%s", li->li_plugin->plg_name);
    MSET("database");

    /* normalized dn cache stats */
    if (ndn_cache_started()) {
        ndn_cache_get_stats(&hits, &tries, &size, &maxsize, &thread_size, &evicts, &slots, &count);
        sprintf(buf, "%" PRIu64, tries);
        MSET("normalizedDnCacheTries");
        sprintf(buf, "%" PRIu64, hits);
        MSET("normalizedDnCacheHits");
        sprintf(buf, "%" PRIu64, (tries - hits));
        MSET("normalizedDnCacheMisses");
        sprintf(buf, "%" PRIu64, (uint64_t)(100.0 * (double)hits / (double)(tries > 0 ? tries : 1)));
        MSET("normalizedDnCacheHitRatio");
        sprintf(buf, "%" PRIu64, evicts);
        MSET("NormalizedDnCacheEvictions");
        sprintf(buf, "%" PRIu64, size);
        MSET("currentNormalizedDnCacheSize");
        if (maxsize == 0) {
            sprintf(buf, "%d", -1);
        } else {
            sprintf(buf, "%" PRIu64, maxsize);
        }
        MSET("maxNormalizedDnCacheSize");
        sprintf(buf, "%" PRIu64, thread_size);
        MSET("NormalizedDnCacheThreadSize");
        sprintf(buf, "%" PRIu64, slots);
        MSET("NormalizedDnCacheThreadSlots");
        sprintf(buf, "%" PRIu64, count);
        MSET("currentNormalizedDnCacheCount");
    }

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


/* monitor global ldbm database stats */
int
dbmdb_dbmonitor_search(Slapi_PBlock *pb __attribute__((unused)),
                           Slapi_Entry *e,
                           Slapi_Entry *entryAfter __attribute__((unused)),
                           int *returncode,
                           char *returntext __attribute__((unused)),
                           void *arg)
{
    struct ldbminfo *li = NULL;
    dbmdb_stats_t *stats = NULL;
    struct berval *vals[2];
    struct berval val;
    char buf[BUFSIZ];
    struct stat mapstat = {0};
    dbmdb_ctx_t *ctx;

    PR_ASSERT(NULL != arg);
    li = (struct ldbminfo *)arg;
    vals[0] = &val;
    vals[1] = NULL;
    ctx = MDB_CONFIG(li);

    PR_snprintf(buf, sizeof(buf), "%s/%s", li->li_directory, DBMAPFILE);
    (void) stat(buf, &mapstat);

    stats = dbdmd_gather_stats(MDB_CONFIG(li), NULL);
    /*  Note: envstat has no interest (looks like the empty default database)
    PR_snprintf(buf, sizeof(buf), "%u", stats->envstat.ms_psize);
    MSET("dbiPageSize");
    PR_snprintf(buf, sizeof(buf), "%u", stats->envstat.ms_depth);
    MSET("dbiTreeDepth");
    PR_snprintf(buf, sizeof(buf), "%ld", stats->envstat.ms_branch_pages);
    MSET("dbiBranchPages");
    PR_snprintf(buf, sizeof(buf), "%ld", stats->envstat.ms_leaf_pages);
    MSET("dbiLeafPages");
    PR_snprintf(buf, sizeof(buf), "%ld", stats->envstat.ms_overflow_pages);
    MSET("dbiOverflowPages");
    PR_snprintf(buf, sizeof(buf), "%ld", stats->envstat.ms_entries);
    */

    PR_snprintf(buf, sizeof(buf), "%lu", stats->envinfo.me_mapsize);
    MSET("dbenvMapMaxSize");
    PR_snprintf(buf, sizeof(buf), "%lu", mapstat.st_size);
    MSET("dbenvMapSize");
    PR_snprintf(buf, sizeof(buf), "%ld", stats->envinfo.me_last_pgno);
    MSET("dbenvLastPageNo");
    PR_snprintf(buf, sizeof(buf), "%ld", stats->envinfo.me_last_txnid);
    MSET("dbenvLastTxnId");
    PR_snprintf(buf, sizeof(buf), "%u", stats->envinfo.me_maxreaders);
    MSET("dbenvMaxReaders");
    PR_snprintf(buf, sizeof(buf), "%u", stats->envinfo.me_numreaders);
    MSET("dbenvNumReaders");

    PR_snprintf(buf, sizeof(buf), "%d", stats->nbdbis);
    MSET("dbenvNumDBIs");

    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rwtxn.nbwaiting);
    MSET("waitingRWtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rwtxn.nbactive);
    MSET("activeRWtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rwtxn.nbabort);
    MSET("abortRWtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rwtxn.nbcommit);
    MSET("commitRWtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rwtxn.granttime.ns/ctx->perf_rwtxn.granttime.nbsamples);
    MSET("grantTimeRWtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rwtxn.lifetime.ns/ctx->perf_rwtxn.lifetime.nbsamples);
    MSET("lifeTimeRWtxn");

    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rotxn.nbwaiting);
    MSET("waitingROtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rotxn.nbactive);
    MSET("activeROtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rotxn.nbabort);
    MSET("abortROtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rotxn.nbcommit);
    MSET("commitROtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rotxn.granttime.ns/ctx->perf_rotxn.granttime.nbsamples);
    MSET("grantTimeROtxn");
    PR_snprintf(buf, sizeof(buf), "%lu", ctx->perf_rotxn.lifetime.ns/ctx->perf_rotxn.lifetime.nbsamples);
    MSET("lifeTimeROtxn");

    dbmdb_free_stats(&stats);

    dbmdb_perfctrs_as_entry(e, MDB_CONFIG(li));

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}
