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


#include "csnpl.h"
#include "llist.h"

struct csnpl
{
    LList *csnList;        /* pending list */
    Slapi_RWLock *csnLock; /* lock to serialize access to PL */
};


typedef struct _csnpldata
{
    PRBool committed;      /* True if CSN committed */
    CSN *csn;              /* The actual CSN */
    Replica *prim_replica; /* The replica where the prom csn was generated */
    const CSN *prim_csn;   /* The primary CSN of an operation consising of multiple sub ops*/
} csnpldata;

static PRBool csn_primary_or_nested(csnpldata *csn_data, const CSNPL_CTX *csn_ctx);

/* forward declarations */
#ifdef DEBUG
static void _csnplDumpContentNoLock(CSNPL *csnpl, const char *caller);
#endif

CSNPL *
csnplNew()
{
    CSNPL *csnpl;

    csnpl = (CSNPL *)slapi_ch_malloc(sizeof(CSNPL));
    if (csnpl == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "csnplNew - Failed to allocate pending list\n");
        return NULL;
    }

    csnpl->csnList = llistNew();
    if (csnpl->csnList == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "csnplNew - Failed to allocate pending list\n");
        slapi_ch_free((void **)&csnpl);
        return NULL;
    }

    /* ONREPL: do locks need different names */
    csnpl->csnLock = slapi_new_rwlock();

    if (csnpl->csnLock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "csnplNew - Failed to create lock; NSPR error - %d\n",
                      PR_GetError());
        slapi_ch_free((void **)&(csnpl->csnList));
        slapi_ch_free((void **)&csnpl);
        return NULL;
    }

    return csnpl;
}


void
csnpldata_free(csnpldata **data_to_free)
{
    if (NULL != data_to_free) {
        if (NULL != (*data_to_free)->csn) {
            csn_free(&(*data_to_free)->csn);
        }
        slapi_ch_free((void **)data_to_free);
    }
}

void
csnplFree(CSNPL **csnpl)
{
    if ((csnpl == NULL) || (*csnpl == NULL))
        return;

    /* free all remaining nodes */
    llistDestroy(&((*csnpl)->csnList), (FNFree)csnpldata_free);

    if ((*csnpl)->csnLock)
        slapi_destroy_rwlock((*csnpl)->csnLock);

    slapi_ch_free((void **)csnpl);
}

/* This function isnerts a CSN into the pending list
 * Returns: 0 if the csn was successfully inserted
 *          1 if the csn has already been seen
 *         -1 for any other kind of errors
 */
int
csnplInsert(CSNPL *csnpl, const CSN *csn, const CSNPL_CTX *prim_csn)
{
    int rc;
    csnpldata *csnplnode;
    char csn_str[CSN_STRSIZE];

    if (csnpl == NULL || csn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "csnplInsert - Invalid argument\n");
        return -1;
    }

    slapi_rwlock_wrlock(csnpl->csnLock);

    /* check to see if this csn is larger than the last csn in the
       pending list. It has to be if we have not seen it since
       the csns are always added in the accending order. */
    csnplnode = llistGetTail(csnpl->csnList);
    if (csnplnode && csn_compare(csnplnode->csn, csn) >= 0) {
        slapi_rwlock_unlock(csnpl->csnLock);
        return 1;
    }

    csnplnode = (csnpldata *)slapi_ch_calloc(1, sizeof(csnpldata));
    csnplnode->committed = PR_FALSE;
    csnplnode->csn = csn_dup(csn);
    if (prim_csn) {
        csnplnode->prim_csn = prim_csn->prim_csn;
        csnplnode->prim_replica = prim_csn->prim_repl;
    }
    csn_as_string(csn, PR_FALSE, csn_str);
    rc = llistInsertTail(csnpl->csnList, csn_str, csnplnode);

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplInsert");
#endif

    slapi_rwlock_unlock(csnpl->csnLock);
    if (rc != 0) {
        char s[CSN_STRSIZE];
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "csnplInsert - Failed to insert csn (%s) into pending list\n", csn_as_string(csn, PR_FALSE, s));
        }
        return -1;
    }

    return 0;
}

int
csnplRemove(CSNPL *csnpl, const CSN *csn)
{
    csnpldata *data;
    char csn_str[CSN_STRSIZE];

    if (csnpl == NULL || csn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "csnplRemove: invalid argument\n");
        return -1;
    }

    csn_as_string(csn, PR_FALSE, csn_str);
    slapi_rwlock_wrlock(csnpl->csnLock);

    data = (csnpldata *)llistRemove(csnpl->csnList, csn_str);
    if (data == NULL) {
        slapi_rwlock_unlock(csnpl->csnLock);
        return -1;
    }

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplRemove");
#endif

    csn_free(&data->csn);
    slapi_ch_free((void **)&data);

    slapi_rwlock_unlock(csnpl->csnLock);

    return 0;
}
PRBool
csn_primary(Replica *replica, const CSN *csn, const CSNPL_CTX *csn_ctx)
{
    if (csn_ctx == NULL)
        return PR_FALSE;

    if (replica != csn_ctx->prim_repl) {
        /* The CSNs are not from the same replication topology
         * so even if the csn values are equal they are not related
         * to the same operation
         */
        return PR_FALSE;
    }

    /* Here the two CSNs belong to the same replication topology */

    /* check if the CSN identifies the primary update */
    if (csn_is_equal(csn, csn_ctx->prim_csn)) {
        return PR_TRUE;
    }

    return PR_FALSE;
}

static PRBool
csn_primary_or_nested(csnpldata *csn_data, const CSNPL_CTX *csn_ctx)
{
    if ((csn_data == NULL) || (csn_ctx == NULL))
        return PR_FALSE;

    if (csn_data->prim_replica != csn_ctx->prim_repl) {
        /* The CSNs are not from the same replication topology
         * so even if the csn values are equal they are not related
         * to the same operation
         */
        return PR_FALSE;
    }

    /* Here the two CSNs belong to the same replication topology */

    /* First check if the CSN identifies the primary update */
    if (csn_is_equal(csn_data->csn, csn_ctx->prim_csn)) {
        return PR_TRUE;
    }

    /* Second check if the CSN identifies a nested update */
    if (csn_is_equal(csn_data->prim_csn, csn_ctx->prim_csn)) {
        return PR_TRUE;
    }

    return PR_FALSE;
}

int
csnplRemoveAll(CSNPL *csnpl, const CSNPL_CTX *csn_ctx)
{
    csnpldata *data;
    void *iterator;

    slapi_rwlock_wrlock(csnpl->csnLock);
    data = (csnpldata *)llistGetFirst(csnpl->csnList, &iterator);
    while (NULL != data) {
        if (csn_primary_or_nested(data, csn_ctx)) {
            csnpldata_free(&data);
            data = (csnpldata *)llistRemoveCurrentAndGetNext(csnpl->csnList, &iterator);
        } else {
            data = (csnpldata *)llistGetNext(csnpl->csnList, &iterator);
        }
    }
#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplRemoveAll");
#endif
    slapi_rwlock_unlock(csnpl->csnLock);
    return 0;
}


int
csnplCommitAll(CSNPL *csnpl, const CSNPL_CTX *csn_ctx)
{
    csnpldata *data;
    void *iterator;
    char csn_str[CSN_STRSIZE];

    csn_as_string(csn_ctx->prim_csn, PR_FALSE, csn_str);
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "csnplCommitALL: committing all csns for csn %s\n", csn_str);
    slapi_rwlock_wrlock(csnpl->csnLock);
    data = (csnpldata *)llistGetFirst(csnpl->csnList, &iterator);
    while (NULL != data) {
        csn_as_string(data->csn, PR_FALSE, csn_str);
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "csnplCommitALL: processing data csn %s\n", csn_str);
        if (csn_primary_or_nested(data, csn_ctx)) {
            data->committed = PR_TRUE;
        }
        data = (csnpldata *)llistGetNext(csnpl->csnList, &iterator);
    }
    slapi_rwlock_unlock(csnpl->csnLock);
    return 0;
}

int
csnplCommit(CSNPL *csnpl, const CSN *csn)
{
    csnpldata *data;
    char csn_str[CSN_STRSIZE];

    if (csnpl == NULL || csn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "csnplCommit: invalid argument\n");
        return -1;
    }
    csn_as_string(csn, PR_FALSE, csn_str);

    slapi_rwlock_wrlock(csnpl->csnLock);

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplCommit");
#endif

    data = (csnpldata *)llistGet(csnpl->csnList, csn_str);
    if (data == NULL) {
        /*
         * In the scenario "4.x supplier -> 6.x legacy-consumer -> 6.x consumer"
         * csn will have rid=65535. Hence 6.x consumer will get here trying
         * to commit r->min_csn_pl because its rid matches that in the csn.
         * However, r->min_csn_pl is always empty for a dedicated consumer.
         * Exclude READ-ONLY replica ID here from error logging.
         */
        ReplicaId rid = csn_get_replicaid(csn);
        if (rid < MAX_REPLICA_ID) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "csnplCommit: can't find csn %s\n", csn_str);
        }
        slapi_rwlock_unlock(csnpl->csnLock);
        return -1;
    } else {
        data->committed = PR_TRUE;
    }

    slapi_rwlock_unlock(csnpl->csnLock);

    return 0;
}


CSN *
csnplGetMinCSN(CSNPL *csnpl, PRBool *committed)
{
    csnpldata *data;
    CSN *csn = NULL;
    slapi_rwlock_rdlock(csnpl->csnLock);
    if ((data = (csnpldata *)llistGetHead(csnpl->csnList)) != NULL) {
        csn = csn_dup(data->csn);
        if (NULL != committed) {
            *committed = data->committed;
        }
    }
    slapi_rwlock_unlock(csnpl->csnLock);

    return csn;
}


/*
 * Roll up the list of pending CSNs, removing all of the CSNs at the
 * head of the the list that are committed and contiguous. Returns
 * the largest committed CSN, or NULL if no contiguous block of
 * committed CSNs appears at the beginning of the list. The caller
 * is responsible for freeing the CSN returned.
 */
CSN *
csnplRollUp(CSNPL *csnpl, CSN **first_commited)
{
    CSN *largest_committed_csn = NULL;
    csnpldata *data;
    PRBool freeit = PR_TRUE;
    void *iterator;

    slapi_rwlock_wrlock(csnpl->csnLock);
    if (first_commited) {
        /* Avoid non-initialization issues due to careless callers */
        *first_commited = NULL;
    }
    data = (csnpldata *)llistGetFirst(csnpl->csnList, &iterator);
    while (NULL != data && data->committed) {
        if (NULL != largest_committed_csn && freeit) {
            csn_free(&largest_committed_csn);
        }
        freeit = PR_TRUE;
        largest_committed_csn = data->csn; /* Save it */
        if (first_commited && (*first_commited == NULL)) {
            *first_commited = data->csn;
            freeit = PR_FALSE;
        }
        /* llistRemoveCurrentAndGetNext will detach the current node
               so we have to free the data associated with it, but not the csn */
        data->csn = NULL;
        csnpldata_free(&data);
        data = (csnpldata *)llistRemoveCurrentAndGetNext(csnpl->csnList, &iterator);
    }

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplRollUp");
#endif

    slapi_rwlock_unlock(csnpl->csnLock);
    return largest_committed_csn;
}

#ifdef DEBUG
/* Dump current content of the list - for debugging */
void
csnplDumpContent(CSNPL *csnpl, const char *caller)
{
    if (csnpl) {
        slapi_rwlock_rdlock(csnpl->csnLock);
        _csnplDumpContentNoLock(csnpl, caller);
        slapi_rwlock_unlock(csnpl->csnLock);
    }
}

/* helper function */
static void
_csnplDumpContentNoLock(CSNPL *csnpl, const char *caller)
{
    csnpldata *data;
    void *iterator;
    char csn_str[CSN_STRSIZE];
    char primcsn_str[CSN_STRSIZE];

    data = (csnpldata *)llistGetFirst(csnpl->csnList, &iterator);
    if (data) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "%s: CSN Pending list content:\n",
                      caller ? caller : "");
    }
    while (data) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "%s,(prim %s), %s\n",
                      csn_as_string(data->csn, PR_FALSE, csn_str),
                      data->prim_csn ? csn_as_string(data->prim_csn, PR_FALSE, primcsn_str) : " ",
                      data->committed ? "committed" : "not committed");
        data = (csnpldata *)llistGetNext(csnpl->csnList, &iterator);
    }
}
#endif

/* wrapper around csn_free, to satisfy NSPR thread context API */
void
csnplFreeCSNPL_CTX(void *arg)
{
    CSNPL_CTX *csnpl_ctx = (CSNPL_CTX *)arg;
    csn_free(&csnpl_ctx->prim_csn);
    if (csnpl_ctx->sec_repl) {
        slapi_ch_free((void **)&csnpl_ctx->sec_repl);
    }
    slapi_ch_free((void **)&csnpl_ctx);
}
