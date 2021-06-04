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

/* #define DEBUG_TXN 1 */

#ifdef DEBUG_TXN
#include <execinfo.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <syscall.h>

int dbg_txn_begin(const char *funcname, MDB_env *env, MDB_txn *parent_txn, int flags, MDB_txn **txn);
int dbg_txn_end(const char *funcname, MDB_txn *txn, int iscommit);

#define TXN_BEGIN(env, parent_txn, flags, txn)  dbg_txn_begin(__FUNCTION__, env, parent_txn, flags, txn)
#define TXN_COMMIT(txn)                         dbg_txn_end(__FUNCTION__, txn, 1)
#define TXN_ABORT(txn)                          dbg_txn_end(__FUNCTION__, txn, 0)
#define TXN_LOG(msg,txn)                        slapi_log_err(SLAPI_LOG_INFO, (char*)__FUNCTION__, msg, (ulong)(txn))
#define pthread_gettid()                        syscall(__NR_gettid)

#else
#define TXN_BEGIN(env, parent_txn, flags, txn)  mdb_txn_begin(env, parent_txn, flags, txn)
#define TXN_COMMIT(txn)                         mdb_txn_commit(txn)
#define TXN_ABORT(txn)                          mdb_txn_abort(txn)
#define TXN_LOG(msg,txn)
#define pthread_gettid()                        0
#endif
#define TXN(txn)                                dbmdb_txn(txn)
#define TXN_MAGIC0                              0x7A78A89A9AAABBBL
#define TXN_MAGIC1                              0xdeadbeefdeadbeefL



/* transaction context (on which dbi_txn_t is mapped) */
typedef struct dbmdb_txn_t {
    long magic[2];
    MDB_txn *txn;                 /* MDB txn handle */
    int refcnt;                   /* Number of users */
    int flags;
    struct dbmdb_txn_t *parent;
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
    slapi_ch_free((void**)anchor);
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
        anchor = (dbmdb_txn_t **)slapi_ch_calloc(1, sizeof anchor);
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



/* Info file used to check version and get parameters needed to open the db in dbscan case */

#ifdef DEBUG_TXN

static void get_stack(char *buff, size_t buflen)
{
    /* faster version but provides less info */
    void *frames[50];
    int nbframes;
    char *pt = buff;
    char *end = &buff[buflen] - 1;  /* reserve space for final \0 */
    char **symbols;
    int len, i;

    nbframes = backtrace(frames, (sizeof frames)/sizeof frames[0]);
    symbols = backtrace_symbols(frames, nbframes);
    if (symbols) {
        for (i=0; i<nbframes; i++) {
            len = strlen(symbols[i]);
            if (pt + len + 2 >= end) {
                break;  /* Buffer too small */
            }
            *pt++ = '\t';
            strcpy(pt, symbols[i]);
            pt += len;
            *pt++ = '\n';
        }
        free(symbols);
    }
    *pt = 0;
}


int dbg_txn_begin(const char *funcname, MDB_env *env, MDB_txn *parent_txn, int flags, MDB_txn **txn)
{
    char stack[2048];
    char strflags[100];
    get_stack(stack, sizeof stack);
    dbmdb_envflags2str(flags, strflags, sizeof strflags);
    slapi_log_err(SLAPI_LOG_INFO, (char*)funcname, "TXN_BEGIN[%d]. txn_parent=0x%lx, %s, stack is:\n%s\n Waiting ...", pthread_gettid(), (ulong)parent_txn, strflags, stack);
    int rc = mdb_txn_begin(env, parent_txn, flags, txn);
    slapi_log_err(SLAPI_LOG_INFO, (char*)funcname,
        "Done. txn_begin(env=0x%lx, txn_parent=0x%lx, flags=0x%x, txn=0x%lx) returned %d.\n",
        (ulong)env, (ulong)parent_txn, flags, (ulong)*txn, rc);
    return rc;
}

int dbg_txn_end(const char *funcname, MDB_txn *txn, int iscommit)
{
    int rc = 0;
    char stack[2048];
    get_stack(stack, sizeof stack);
    if (iscommit) {
        rc = mdb_txn_commit(txn);
        slapi_log_err(SLAPI_LOG_INFO, (char*)funcname, "TXN_COMMIT[%d] (txn=0x%lx) returned %d. stack is:\n%s\n", pthread_gettid(), (ulong)txn, rc, stack);
    } else {
        mdb_txn_abort(txn);
        slapi_log_err(SLAPI_LOG_INFO, (char*)funcname, "TXN_ABORT[%d] (txn=0x%lx). stack is:\n%s\n", pthread_gettid(), (ulong)txn, stack);
    }
    return rc;
}
#endif

int dbmdb_is_read_only_txn_thread(void)
{
    dbmdb_txn_t *ltxn = *get_mdbtxnanchor();
    return ltxn ? (ltxn->flags & TXNFL_RDONLY) : 0;
}

int dbmdb_start_txn(const char *funcname, dbi_txn_t *parent_txn, int flags, dbi_txn_t **txn)
{
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
    rc = TXN_BEGIN(g_ctx->env, TXN(parent_txn), ((flags & TXNFL_RDONLY)? MDB_RDONLY: 0), &mtxn);
    if (rc == 0) {
        ltxn = calloc(1, sizeof *ltxn);
        ltxn->magic[0] = TXN_MAGIC0;
        ltxn->magic[1] = TXN_MAGIC1;
        ltxn->refcnt = 1;
        ltxn->txn = mtxn;
        ltxn->flags = flags;
        ltxn->parent = parent_txn;
        push_mdbtxn(ltxn);
        *txn = (dbi_txn_t*)ltxn;
#ifdef DEBUG_TXN
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_start_txn", "dbi_txn_t=0x%lx mdb_txn=0x%lx\n", (ulong)ltxn, (ulong)mtxn);
#endif
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

    if (!ltxn)
        return rc;
    ltxn->refcnt--;
    TXN_LOG("release txn 0X%lx\n", ltxn->txn);
    if (ltxn->refcnt == 0) {
        if (rc || (ltxn->flags & (TXNFL_DBI|TXNFL_RDONLY)) == TXNFL_RDONLY) {
            TXN_ABORT(ltxn->txn);
        } else {
            rc = TXN_COMMIT(ltxn->txn);
        }
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


