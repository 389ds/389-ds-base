/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H

#include <config.h>
#endif
#include "mdb_layer.h"

#define TXN_MAGIC0                              0x7A78A89A9AAABBBL
#define TXN_MAGIC1                              0xdeadbeefdeadbeefL

#define GET_HRTIME(hrtime) clock_gettime(CLOCK_THREAD_CPUTIME_ID, hrtime);
#define PERF_LOCK()      pthread_mutex_lock(&g_ctx->perf_lock);
#define PERF_UNLOCK()    pthread_mutex_unlock(&g_ctx->perf_lock);

/* transaction context (on which dbi_txn_t is mapped) */
typedef struct dbmdb_txn_t {
    long magic[2];
    MDB_txn *txn;                 /* MDB txn handle */
    int refcnt;                   /* Number of users */
    int flags;
    struct dbmdb_txn_t *parent;
    struct timespec hr_time_start;
} dbmdb_txn_t;


static PRUintn thread_private_mdb_txn_stack;
static dbmdb_ctx_t *g_ctx;  /* Global dbmdb context */

static void
cleanup_mdbtxn_stack(void *arg)
{
    dbmdb_txn_t **anchor = (dbmdb_txn_t**)arg;
    dbmdb_txn_t *txn = *anchor;
    dbmdb_txn_t *txn2;

    *anchor = NULL;
    slapi_ch_free((void**)&anchor);
    PR_SetThreadPrivate(thread_private_mdb_txn_stack, NULL);
    while (txn) {
        txn2 = txn->parent;
        TXN_ABORT(TXN(txn));
        slapi_ch_free((void**)&txn);
        txn = txn2;
    }
}

void
init_mdbtxn(dbmdb_ctx_t *ctx)
{
    g_ctx = ctx;
    PR_NewThreadPrivateIndex(&thread_private_mdb_txn_stack, cleanup_mdbtxn_stack);
}

static dbmdb_txn_t **get_mdbtxnanchor(void)
{
    dbmdb_txn_t **anchor = (dbmdb_txn_t **) PR_GetThreadPrivate(thread_private_mdb_txn_stack);
    if (!anchor) {
        anchor = (dbmdb_txn_t **)slapi_ch_calloc(1, sizeof (dbmdb_txn_t *));
        PR_SetThreadPrivate(thread_private_mdb_txn_stack, anchor);
    }
    return anchor;
}

static void push_mdbtxn(dbmdb_txn_t *txn)
{
    dbmdb_txn_t **anchor = get_mdbtxnanchor();
    txn->parent = *anchor;
    *anchor = txn;
}

static dbmdb_txn_t *pop_mdbtxn(void)
{
    dbmdb_txn_t **anchor = get_mdbtxnanchor();
    dbmdb_txn_t *txn = *anchor;

    if (txn)
        *anchor = txn->parent;
    return txn;
}

/* Determine if current thread has already a pending txn */
int dbmdb_has_a_txn(void)
{
    dbmdb_txn_t **anchor = get_mdbtxnanchor();
    return (*anchor != NULL);
}


int dbmdb_is_read_only_txn_thread(void)
{
    dbmdb_txn_t *ltxn = *get_mdbtxnanchor();
    return ltxn ? (ltxn->flags & TXNFL_RDONLY) : 0;
}

void cumul_time(const struct timespec *sample, cumuled_time_t *sum)
{
    sum->nbsamples++;
    sum->ns += sample->tv_nsec + 1000000000 * sample->tv_sec;
}

int dbmdb_start_txn(const char *funcname, dbi_txn_t *parent_txn, int flags, dbi_txn_t **txn)
{
    struct timespec hr_time_start;
    struct timespec hr_time_now;
    struct timespec hr_elapsed;
    dbmdb_perfctrs_txn_t *perf;
    dbmdb_txn_t *ltxn = NULL;
    MDB_txn *mtxn = NULL;
    int rc = 0;

    /* If parent is explicitly provided, we need to generate a sub txn */
    /* otherwise we use the txn that is pushed in the thread local storage stack */

    /* MDB txn model is quite limited:
     *  ReadOnly TXN: only one per thread (no sub txn for readOnly parent / no readOnly sub txn)
     * If both TXN are write - we can use sub txn
     */
    if (g_ctx->readonly)
        flags |= TXNFL_RDONLY;      /* Always use read only txn if env is open in read-only mode */
    if (parent_txn) {
        ltxn = parent_txn;
    } else {
        /* Let see if we can reuse the last txn in the stack.
         * No need to check for a backend lvl txn (dblayer_get_pvt_txn)
         * because it is also in the stack
         */
        ltxn = *get_mdbtxnanchor();
    }
    if (ltxn) {
        if (flags & TXNFL_DBI) {
            /* Too bad: we cannot handle this situation as it leads to inconsistency
             * if parent txn get aborted.
             */
             slapi_log_error(SLAPI_LOG_CRIT, "dbmdb_start_txn",
                    "Code issue: Trying to handle a db instance in a thread that is already holding a txn.\n");
            slapi_log_backtrace(SLAPI_LOG_CRIT);
            abort();
        }


        if (ltxn->flags & TXNFL_RDONLY) {
            PR_ASSERT(flags & TXNFL_RDONLY);
            /* Cannot use sub txn */
            /* So lets rather reuse the parent txn */
            /* lets reuse the txn in stack */
            ltxn->refcnt++; /* No need to lock as only current thread handles it */
            *txn = (dbi_txn_t*)ltxn;
            TXN_LOG("Reusing txn 0X%lx\n", ltxn->txn);
            return 0;
        }
        parent_txn = ltxn;
        flags &= ~TXNFL_RDONLY;
    }

    /* Here we need to open a new txn */
    perf = (flags & TXNFL_RDONLY) ? &g_ctx->perf_rotxn : &g_ctx->perf_rwtxn;
    PERF_LOCK();
    perf->nbwaiting++;
    PERF_UNLOCK();

    GET_HRTIME(&hr_time_start);
    rc = TXN_BEGIN(g_ctx->env, TXN(parent_txn), ((flags & TXNFL_RDONLY)? MDB_RDONLY: 0), &mtxn);
    GET_HRTIME(&hr_time_now);
    slapi_timespec_diff(&hr_time_now, &hr_time_start, &hr_elapsed);
    PERF_LOCK();
    perf->nbwaiting--;
    perf->nbactive++;
    cumul_time(&hr_elapsed, &perf->granttime);
    PERF_UNLOCK();

    if (rc == 0) {
        ltxn = (dbmdb_txn_t *) slapi_ch_calloc(1, sizeof *ltxn);
        ltxn->magic[0] = TXN_MAGIC0;
        ltxn->magic[1] = TXN_MAGIC1;
        ltxn->refcnt = 1;
        ltxn->txn = mtxn;
        ltxn->flags = flags;
        ltxn->parent = parent_txn;
        ltxn->hr_time_start = hr_time_now;
        push_mdbtxn(ltxn);
        *txn = (dbi_txn_t*)ltxn;
        dbg_log(__FILE__,__LINE__,__FUNCTION__, DBGMDB_LEVEL_TXN, "%s: dbi_txn_t=%p mdb_txn=%p\n", funcname, ltxn, mtxn);
    } else {
        slapi_log_error(SLAPI_LOG_ERR, "dbmdb_start_txn",
            "Failed to begin a txn for function %s. err=%d %s\n",
            funcname, rc, mdb_strerror(rc));
    }
    return rc;
}

int dbmdb_end_txn(const char *funcname, int rc, dbi_txn_t **txn)
{
    dbmdb_txn_t *ltxn = (dbmdb_txn_t*)*txn;
    struct timespec hr_time_now;
    struct timespec hr_elapsed;
    dbmdb_perfctrs_txn_t *perf;

    if (!ltxn)
        return rc;
    ltxn->refcnt--;
    perf = (ltxn->flags & TXNFL_RDONLY) ? &g_ctx->perf_rotxn : &g_ctx->perf_rwtxn;
    TXN_LOG("release txn 0X%lx\n", ltxn->txn);
    if (ltxn->refcnt == 0) {
        if (rc || (ltxn->flags & (TXNFL_DBI|TXNFL_RDONLY)) == TXNFL_RDONLY) {
            TXN_ABORT(ltxn->txn);
        } else {
            rc = TXN_COMMIT(ltxn->txn);
        }
        GET_HRTIME(&hr_time_now);
        slapi_timespec_diff(&hr_time_now, &ltxn->hr_time_start, &hr_elapsed);
        PERF_LOCK();
        perf->nbactive--;
        if (rc || (ltxn->flags & (TXNFL_DBI|TXNFL_RDONLY)) == TXNFL_RDONLY) {
            perf->nbabort++;
        } else {
            perf->nbcommit++;
        }
        cumul_time(&hr_elapsed, &perf->lifetime);
        PERF_UNLOCK();

        ltxn->txn = NULL;
        pop_mdbtxn();
        slapi_ch_free((void**)txn);
    }
    return rc;
}

/* Convert dbi_txn_t to MDB_txn */
MDB_txn *dbmdb_txn(dbi_txn_t *txn)
{
    dbmdb_txn_t *dbtxn = (dbmdb_txn_t*) txn;
    if (dbtxn) {
        PR_ASSERT(dbtxn->magic[0] == TXN_MAGIC0);
        PR_ASSERT(dbtxn->magic[1] == TXN_MAGIC1);
        return dbtxn->txn;
    }
    return NULL;
}


