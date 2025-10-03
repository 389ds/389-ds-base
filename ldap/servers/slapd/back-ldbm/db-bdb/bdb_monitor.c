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

#include "bdb_layer.h"
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


/* DSE callback to monitor stats for a particular instance */
int
bdb_monitor_instance_search(Slapi_PBlock *pb __attribute__((unused)),
                            Slapi_Entry *e,
                            Slapi_Entry *entryAfter __attribute__((unused)),
                            int *returncode,
                            char *returntext __attribute__((unused)),
                            void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    struct ldbminfo *li = NULL;
    struct berval val;
    struct berval *vals[2];
    char buf[BUFSIZ];
    /* NPCTE fix for bugid 544365, esc 0. <P.R> <04-Jul-2001> */
    struct stat astat;
    /* end of NPCTE fix for bugid 544365 */
    DB_MPOOL_FSTAT **mpfstat = NULL;
    int i, j;
    char *absolute_pathname = NULL;
    struct cache_stats cstats = {0};

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

    if (bdb_memp_stat(li, NULL, &mpfstat) != 0) {
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    for (i = 0; (mpfstat[i] && (mpfstat[i]->file_name != NULL)); i++) {
        /* only print out stats on files used by this instance */
        if (strlen(mpfstat[i]->file_name) < strlen(inst->inst_dir_name))
            continue;
        if (strncmp(mpfstat[i]->file_name, inst->inst_dir_name,
                    strlen(inst->inst_dir_name)) != 0)
            continue;
        if (mpfstat[i]->file_name[strlen(inst->inst_dir_name)] != get_sep(mpfstat[i]->file_name))
            continue;

        /* Since the filenames are now relative, we need to construct an absolute version
         * for the purpose of stat() etc below...
         */
        slapi_ch_free_string(&absolute_pathname);
        absolute_pathname = slapi_ch_smprintf("%s%c%s", inst->inst_parent_dir_name, get_sep(inst->inst_parent_dir_name), mpfstat[i]->file_name);

        /* Hide statistic of deleted files (mainly indexes) */
        if (stat(absolute_pathname, &astat))
            continue;
        /* If the file has been re-created after been deleted
         * We should show only statistics for the last instance
         * Since SleepyCat returns the statistic of the last open file first,
         * we should only display the first statistic record for a given file
         */
        for (j = 0; j < i; j++)
            if (!strcmp(mpfstat[i]->file_name, mpfstat[j]->file_name))
                break;
        if (j < i)
            continue;

        /* Get each file's stats */
        PR_snprintf(buf, sizeof(buf), "%s", mpfstat[i]->file_name);
        MSETF("dbFilename-%d", i);
        sprintf(buf, "%lu", (unsigned long)mpfstat[i]->st_cache_hit);
        MSETF("dbFileCacheHit-%d", i);
        sprintf(buf, "%lu", (unsigned long)mpfstat[i]->st_cache_miss);
        MSETF("dbFileCacheMiss-%d", i);
        sprintf(buf, "%lu", (unsigned long)mpfstat[i]->st_page_in);
        MSETF("dbFilePageIn-%d", i);
        sprintf(buf, "%lu", (unsigned long)mpfstat[i]->st_page_out);
        MSETF("dbFilePageOut-%d", i);

        slapi_ch_free_string(&absolute_pathname);
    }

    slapi_ch_free_string(&absolute_pathname);
    slapi_ch_free((void **)&mpfstat);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


/* monitor global ldbm stats */
int
bdb_monitor_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct berval val;
    struct berval *vals[2];
    char buf[BUFSIZ];
    DB_MPOOL_STAT *mpstat = NULL;
    DB_MPOOL_FSTAT **mpfstat = NULL;
    uint64_t cache_tries;
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

    /* we have to ask for file stats in order to get correct global stats */
    if (bdb_memp_stat(li, &mpstat, &mpfstat) != 0) {
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* cache hits*/
    sprintf(buf, "%lu", (unsigned long)mpstat->st_cache_hit);
    MSET("dbCacheHits");

    /* cache tries*/
    cache_tries = (mpstat->st_cache_miss + mpstat->st_cache_hit);
    sprintf(buf, "%" PRIu64, cache_tries);
    MSET("dbCacheTries");

    /* cache hit ratio*/
    sprintf(buf, "%lu", (unsigned long)(100.0 * (double)mpstat->st_cache_hit / (double)(cache_tries > 0 ? cache_tries : 1)));
    MSET("dbCacheHitRatio");

    sprintf(buf, "%lu", (unsigned long)mpstat->st_page_in);
    MSET("dbCachePageIn");
    sprintf(buf, "%lu", (unsigned long)mpstat->st_page_out);
    MSET("dbCachePageOut");
    sprintf(buf, "%lu", (unsigned long)mpstat->st_ro_evict);
    MSET("dbCacheROEvict");
    sprintf(buf, "%lu", (unsigned long)mpstat->st_rw_evict);
    MSET("dbCacheRWEvict");

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

    slapi_ch_free((void **)&mpstat);

    if (mpfstat)
        slapi_ch_free((void **)&mpfstat);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


/* monitor global ldbm database stats */
int
bdb_dbmonitor_search(Slapi_PBlock *pb __attribute__((unused)),
                           Slapi_Entry *e,
                           Slapi_Entry *entryAfter __attribute__((unused)),
                           int *returncode,
                           char *returntext __attribute__((unused)),
                           void *arg)
{
    dblayer_private *dbpriv = NULL;
    struct ldbminfo *li = NULL;

    PR_ASSERT(NULL != arg);
    li = (struct ldbminfo *)arg;
    dbpriv = (dblayer_private *)li->li_dblayer_private;
    PR_ASSERT(NULL != dbpriv);

    bdb_perfctrs_as_entry(e, BDB_CONFIG(li)->perf_private, ((bdb_db_env *)dbpriv->dblayer_env)->bdb_DB_ENV);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}
