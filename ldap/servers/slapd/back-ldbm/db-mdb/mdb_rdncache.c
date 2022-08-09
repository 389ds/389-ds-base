/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include <assert.h>
#include "mdb_import.h"

static RDNcacheElem_t *rdncache_new_elem(RDNcacheHead_t *head, ID entryid, ID parentid, int nrdnlen, const char *nrdn, int rdnlen, const char *rdn, WorkerQueueData_t *slot);
static RDNcacheElem_t *rdncache_rdn_lookup_no_lock(RDNcache_t *cache,  WorkerQueueData_t *slot, ID parentid, const char *nrdn, int lognotfound);
int rdncache_has_older_slots(ImportCtx_t *ctx, WorkerQueueData_t *slot);

/* Should maybe use ./ldap/libraries/libavl/avl.c instead of array */


#define RDNCACHE_MUTEX_LOCK(l)    assert(pthread_mutex_lock(l) == 0)
#define RDNCACHE_MUTEX_UNLOCK(l)  assert(pthread_mutex_unlock(l) == 0)


static RDNcacheHead_t *
rdncache_new_head(RDNcache_t *cache)
{
    int len = sizeof (RDNcacheHead_t) + sizeof (RDNcacheMemPool_t);
    char *pt = slapi_ch_calloc(len,1);
    RDNcacheHead_t *head = (RDNcacheHead_t*) pt;
    pt += sizeof (RDNcacheHead_t);
    head->refcnt = 1;
    head->cache = cache;
    head->mem = (RDNcacheMemPool_t *)pt;
    head->mem->next = NULL;
    head->nbitems = 0;
    head->maxitems = DEFAULT_RDNCACHEQUEUE_LEN;
    /* Note: using separate calloc for the queues that may be realloced */
    head->head_per_id = (RDNcacheElem_t **) slapi_ch_calloc(head->maxitems, sizeof(RDNcacheElem_t *));
    head->head_per_rdn = (RDNcacheElem_t **) slapi_ch_calloc(head->maxitems, sizeof(RDNcacheElem_t *));
    return head;
}

static RDNcacheHead_t *
rdncache_head_get(RDNcacheHead_t *head)
{
    if (head) {
        slapi_atomic_incr_32((int32_t*)&(head->refcnt), __ATOMIC_ACQ_REL);
    }
    return head;
}

static RDNcacheElem_t *
rdncache_elem_get(RDNcacheElem_t *elem)
{
    if (elem) {
        rdncache_head_get(elem->head);
    }
    return elem;
}

void
rdncache_head_release(RDNcacheHead_t **head)
{
    RDNcacheMemPool_t *pt, *nextpt;
    RDNcacheHead_t *h = *head;
    int32_t count;

    if (!h) {
        return;
    }
    count = slapi_atomic_decr_32(&h->refcnt, __ATOMIC_ACQ_REL);

    if (count == 0) {
        *head = NULL;
        slapi_ch_free((void**) &h->head_per_id);
        slapi_ch_free((void**) &h->head_per_rdn);
        for (pt=h->mem; pt != (RDNcacheMemPool_t*)(&h[1]); pt=nextpt) {
            nextpt = pt->next;
            slapi_ch_free((void**) &pt);
        }
        /* this free also h->mem */
        slapi_ch_free((void**) &h);
    }
}

void
rdncache_elem_release(RDNcacheElem_t **elem)
{
    if (*elem) {
        rdncache_head_release(&(*elem)->head);
    }
    *elem = NULL;
}

RDNcache_t *
rdncache_init(ImportCtx_t *ctx)
{
	RDNcache_t *cache = CALLOC(RDNcache_t);
    cache->ctx = ctx;
    pthread_mutex_init(&cache->mutex, NULL);
    pthread_cond_init(&cache->condvar, NULL);
	cache->cur = rdncache_new_head(cache);
    cache->prev = rdncache_new_head(cache);
    return cache;
}

void
rdncache_free(RDNcache_t **cache)
{
    RDNcache_t *c = *cache;
    pthread_mutex_destroy(&c->mutex);
    pthread_cond_destroy(&c->condvar);
    rdncache_head_release(&c->cur);
    rdncache_head_release(&c->prev);
    slapi_ch_free((void**)cache);
}


/* Search in cache queue the rdncache elem who has wanted entryid
 * If found: Return the index in head->head_per_id array
 * Else the elem should be inserted at index (-1-returnValue)
 */
static int
rdncache_lookup_by_id(RDNcacheHead_t *head,  ID entryid)
{
    int min = 0;
    int max = head->nbitems-1;
    int idx, delta;

    while (min <= max) {
        idx = (min+max) / 2;
        delta =  head->head_per_id[idx]->eid - entryid;
        if (delta == 0) {
            return idx;
        }
        if (delta>0) {
            max = idx - 1;
        } else {
            min = idx + 1;
        }
    }
    return -1 - min;
}

/* Search in cache queue the rdncache elem who has wanted parentid and nrdn
 * If found: Return the index in head->head_per_rdn array
 * Else the elem should be inserted at index (-1-returnValue)
 */
static int
rdncache_lookup_by_rdn(RDNcacheHead_t *head,  ID parentid, const char *nrdn)
{
    int min = 0;
    int max = head->nbitems-1;
    int idx, delta;

    while (min <= max) {
        idx = (min+max) / 2;
        delta =  head->head_per_rdn[idx]->pid - parentid;
        if (delta == 0) {
            delta = strcmp(head->head_per_rdn[idx]->nrdn, nrdn);
        }
        if (delta == 0) {
            return idx;
        }
        if (delta>0) {
            max = idx - 1;
        } else {
            min = idx + 1;
        }
    }
    return -1 - min;
}

/* Search in entryrdn dbi the rdncache elem who has wanted entryid
 * If found: a new cache element is added in the cache and returned
 * otherwise NULL is returned.
 */
static RDNcacheElem_t *
rdncache_index_lookup_by_id(RDNcache_t *cache,  ID entryid)
{
    RDNcacheElem_t *elem = NULL;
    ImportCtx_t *ctx = cache->ctx;
    backend *be = ctx->job->inst->inst_be;
	MDB_val key = {0};
	MDB_val data = {0};
    dbmdb_cursor_t cur = {0};
    char key_str[10];
    int nrdnlen = 0;
    int rdnlen = 0;
    char *nrdn = NULL;
    char *rdn = NULL;
    ID id = 0;
    ID parentid = 0;
    int rc;

    /* TXNFL_DBI insure that there is no already open txn within the thread */
    rc = dbmdb_open_cursor(&cur, ctx->ctx, ctx->entryrdn->dbi, TXNFL_RDONLY|TXNFL_DBI);
    if (rc) {
        return NULL;
    }
    /* Lets looks first for the parent entry to get the parentid */
    sprintf(key_str, "P%d", entryid);
    key.mv_data = key_str;
    key.mv_size = strlen(key_str) + 1;
    rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_SET);
    if (rc == 0) {
        entryrdn_decode_data(be, data.mv_data, &parentid, NULL, NULL, NULL, NULL);
        /* Then the entry itself */
        sprintf(key_str, "%d", entryid);
        key.mv_size--;
        rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_SET);
        if (rc == 0) {
            entryrdn_decode_data(be, data.mv_data, &id, &nrdnlen, &nrdn, &rdnlen, &rdn);
            elem = rdncache_new_elem(cache->cur, entryid, parentid, nrdnlen, nrdn, rdnlen, rdn, 0);
        }
    }
    dbmdb_close_cursor(&cur, -1);
    return elem;
}

static RDNcacheElem_t *
rdncache_index_lookup_by_rdn(RDNcache_t *cache,  ID parentid, int _nrdnlen, const char *_nrdn, int lognotfound)
{
    RDNcacheElem_t *elem = NULL;
    ImportCtx_t *ctx = cache->ctx;
    backend *be = ctx->job->inst->inst_be;
    char *elem2search = NULL;
    dbmdb_cursor_t cur = {0};
	MDB_val data = {0};
	MDB_val key = {0};
    char *nrdn = NULL;
    char *rdn = NULL;
    char key_str[10];
    int len = strlen(_nrdn)+1;
    int nrdnlen = 0;
    int rdnlen = 0;
    ID id = 0;
    int rc;

    /* TXNFL_DBI insures that there is no already open txn within the thread */
    rc = dbmdb_open_cursor(&cur, ctx->ctx, ctx->entryrdn->dbi, TXNFL_RDONLY|TXNFL_DBI);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "rdncache_index_lookup_by_rdn",  "Failed to open cursor, rc=%d (%s)\n", rc, mdb_strerror(rc));
        return NULL;
    }
    sprintf(key_str, "C%d", parentid);
    key.mv_data = parentid == 0 ? (void*)_nrdn : key_str;
    key.mv_size = strlen(key.mv_data) + 1;
    data.mv_data = entryrdn_encode_data(be, &data.mv_size, 0, _nrdn, _nrdn);
    elem2search = data.mv_data;
    rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_GET_BOTH);
    if (rc == 0) {
        entryrdn_decode_data(be, data.mv_data, &id, &nrdnlen, &nrdn, &rdnlen, &rdn);
        elem = rdncache_new_elem(cache->cur, id, parentid, nrdnlen, nrdn, rdnlen, rdn, NULL);
    } else if (rc == MDB_NOTFOUND) {
        if (lognotfound) {
            slapi_log_err(SLAPI_LOG_ERR, "rdncache_index_lookup_by_rdn",  "[%d]: Failed to find key %s data ndn %s\n", __LINE__, (char*)key.mv_data, _nrdn);
        }
        data.mv_size = 0;
        data.mv_data = NULL;
        rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_SET_KEY);
        while (rc == 0) {
            entryrdn_decode_data(be, data.mv_data, &id, &nrdnlen, &nrdn, &rdnlen, &rdn);
            if (len != nrdnlen || strncmp(_nrdn, nrdn, nrdnlen)) {
                rc = MDB_CURSOR_GET(cur.cur, &key, &data, MDB_NEXT_DUP);
                continue;
            }
            elem = rdncache_new_elem(cache->cur, id, parentid, nrdnlen, nrdn, rdnlen, rdn, NULL);
            break;
        }
    }
    if (rc == MDB_NOTFOUND) {
        if (lognotfound) {
            slapi_log_err(SLAPI_LOG_ERR, "rdncache_index_lookup_by_rdn",  "Failed to find key %s data ndn %s rc=%d (%s)\n", (char*)key.mv_data, _nrdn, rc, mdb_strerror(rc));
        }
    } else if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "rdncache_index_lookup_by_rdn",  "Failed to find key %s data ndn %s rc=%d (%s)\n", (char*)key.mv_data, _nrdn, rc, mdb_strerror(rc));
    }

    dbmdb_close_cursor(&cur, -1);
    slapi_ch_free_string(&elem2search);
    return elem;
}

/*
 * Wait until all rdn of entries with entryid < current entryid are in the cache.
 *  Note: Caller must hold cache->mutex
 *  Beware: cache->cur may change while being in this function (because the mutex is released)
 */
static void
rdncache_wait4older_slots(RDNcache_t *cache, WorkerQueueData_t *slot)
{
    int has_working_worker;
    if (slot) {
        /* Must process dn in order to check for duplicate dn */
        while ((has_working_worker = rdncache_has_older_slots(cache->ctx, slot))) {
            /* So let wait until previous working slot have stored the entry rdn */
            safe_cond_wait(&cache->condvar, &cache->mutex);
        }
    }
}


/*
 * Add a new item in the entryrdn cur cache
 *  parentid == 0 means that we are adding the suffix DN
 *  if slot is not null then it check that the rdn is not a duplicate
 *  (i.e: with same parentid and nrdn but diffrent entryid)
 *  Note: Caller must hold head->cache->mutex
 */
static RDNcacheElem_t *
rdncache_new_elem(RDNcacheHead_t *head, ID entryid, ID parentid, int nrdnlen,
                  const char *nrdn, int rdnlen, const char *rdn, WorkerQueueData_t *slot)
{
    int idx_by_rdn = 0, idx_by_id = 0;
    RDNcacheMemPool_t *memp = head->mem;
    RDNcache_t *cache = head->cache;
    RDNcacheElem_t *elem = NULL;
    RDNcacheElem_t tmp;
    int len;

    if (slot) {
        elem = rdncache_rdn_lookup_no_lock(cache, slot, parentid, nrdn, 0);
        if (elem) {
            return elem;
        }
    }
    /* Beware not to compute idx_by_rdn before waiting because lock get released */
    idx_by_rdn = rdncache_lookup_by_rdn(head, parentid, nrdn);
    idx_by_id = rdncache_lookup_by_id(head, entryid);
    if (idx_by_rdn >= 0) {
        /* Elem already created ( may happen in some race condition case
         *  when adding entry from prev queue or from the dbi
         */
        return head->head_per_rdn[idx_by_rdn];
    }
    PR_ASSERT(idx_by_rdn < 0);  /* rdncache_rdn_lookup would have caught this case */

    tmp.nrdnlen = nrdnlen;
    tmp.rdnlen = rdnlen;
    len = RDNCACHEELEMSIZE(&tmp);
    if (memp->free_offset + len > RDNPAGE_SIZE) {
        /* Not enough space in pool ==> add a new one */
        RDNcacheMemPool_t *newmem = CALLOC(RDNcacheMemPool_t);
        newmem->next = memp;
        newmem->free_offset = 0;
        head->mem = memp = newmem;
    }
    elem = (RDNcacheElem_t *)&memp->mem[memp->free_offset];
    memp->free_offset += len;

    head->nbitems++;
    if (head->nbitems == head->maxitems) {
        /* Need to realloc the queues */
        head->maxitems *= 2;
        head->head_per_rdn = (RDNcacheElem_t**)slapi_ch_realloc((char*)head->head_per_rdn, head->maxitems * sizeof (RDNcacheElem_t*));
        head->head_per_id = (RDNcacheElem_t**)slapi_ch_realloc((char*)head->head_per_id, head->maxitems * sizeof (RDNcacheElem_t*));
    }

    /* get insertion position */
    idx_by_id = -1 - idx_by_id;
    idx_by_rdn = -1 - idx_by_rdn;
    if (idx_by_id < head->nbitems) {
        memmove(&head->head_per_id[idx_by_id+1], &head->head_per_id[idx_by_id], (head->nbitems-idx_by_id)*sizeof (RDNcacheElem_t*));
    }
    if (idx_by_rdn < head->nbitems) {
        memmove(&head->head_per_rdn[idx_by_rdn+1], &head->head_per_rdn[idx_by_rdn], (head->nbitems-idx_by_rdn)*sizeof (RDNcacheElem_t*));
    }
    head->head_per_id[idx_by_id] = elem;
    head->head_per_rdn[idx_by_rdn] = elem;
    elem->head = head;
    elem->eid = entryid;
    elem->pid = parentid;
    elem->nrdnlen = nrdnlen;
    memcpy(elem->nrdn, nrdn, nrdnlen);
    elem->rdnlen = rdnlen;
    memcpy(ELEMRDN(elem), rdn, rdnlen);
    return elem;
}

/*
 * Insert new entry in cache. return !=0 if DN already exists
 */
RDNcacheElem_t *
rdncache_add_elem(RDNcache_t *cache, WorkerQueueData_t *slot, ID entryid, ID parentid, int nrdnlen, const char *nrdn, int rdnlen, const char *rdn)
{
    RDNcacheElem_t *elem;
    RDNCACHE_MUTEX_LOCK(&cache->mutex);
    rdncache_wait4older_slots(cache, slot);
    elem = rdncache_new_elem(cache->cur, entryid, parentid, nrdnlen, nrdn, rdnlen, rdn, slot);
    elem = rdncache_elem_get(elem);
	RDNCACHE_MUTEX_UNLOCK(&cache->mutex);
    return elem;
}

/* release prev cache queue then switch cur cache queue to prev */
void rdncache_rotate(RDNcache_t *cache)
{
    RDNcacheHead_t *oldhead;
    RDNcacheHead_t *newhead = rdncache_new_head(cache);
    RDNCACHE_MUTEX_LOCK(&cache->mutex);
    oldhead = cache->prev;
    cache->prev = cache->cur;
    cache->cur = newhead;
    RDNCACHE_MUTEX_UNLOCK(&cache->mutex);
    rdncache_head_release(&oldhead);
}

/* determine if a worker thread is still working on an entry whose id is smaller than current entry */
int
rdncache_has_older_slots(ImportCtx_t *ctx, WorkerQueueData_t *slot)
{
    ImportQueue_t *q = &ctx->workerq;
    WorkerQueueData_t *slots = (WorkerQueueData_t*)(q->slots);
    WorkerQueueData_t *s;
    int i, wid;

    for (i=0; i<q->max_slots; i++) {
        s = &slots[i];
        wid = s->wait_id;
        if (wid >0 && wid < slot->wait_id) {
            return 1;
        }
    }
    return 0;
}

RDNcacheElem_t *rdncache_id_lookup(RDNcache_t *cache,  WorkerQueueData_t *slot, ID entryid)
{
    RDNcacheElem_t *elem = NULL;
    int idx;

	RDNCACHE_MUTEX_LOCK(&cache->mutex);
    idx = rdncache_lookup_by_id(cache->cur, entryid);
    if (idx >= 0) {
        elem = cache->cur->head_per_id[idx];
    } else {
        /* Not found. Lets look in previous cache */
        idx = rdncache_lookup_by_id(cache->prev, entryid);
        if (idx >= 0) {
            /* found it, lets copy it in current cache */
            elem = cache->prev->head_per_id[idx];
            elem = rdncache_new_elem(cache->cur, elem->eid, elem->pid, elem->nrdnlen, elem->nrdn, elem->rdnlen, ELEMRDN(elem), 0);
        }
    }
    if (!elem) {
        /* Still Not found. Lets look in index (and copy it in cache) */
        elem = rdncache_index_lookup_by_id(cache, entryid);
    }

    /* If still not found, last chance is that another worker is being processing it. */
    if (!elem) {
        /* So let wait until previous working slot have stored the entry rdn */
        rdncache_wait4older_slots(cache, slot);
        /* Now either it is in the current cache or it is missing */
        idx = rdncache_lookup_by_id(cache->cur, entryid);
        elem = (idx >= 0) ? cache->cur->head_per_id[idx] : NULL;
    }
    /* increase refcount while still holding the lock */
    elem = rdncache_elem_get(elem);
	RDNCACHE_MUTEX_UNLOCK(&cache->mutex);
    return elem;
}

/* Search for rdn element in cache
 *   usually expected flags is 1 , 0 means that we are looking for duplicate from rdncache_new_elem()
 */
static RDNcacheElem_t *
rdncache_rdn_lookup_no_lock(RDNcache_t *cache,  WorkerQueueData_t *slot, ID parentid, const char *nrdn, int lognotfound)
{
    RDNcacheElem_t *elem = NULL;
    int idx;

    idx = rdncache_lookup_by_rdn(cache->cur, parentid, nrdn);
    if (idx >= 0) {
        elem = cache->cur->head_per_rdn[idx];
    } else {
        /* Not found. Lets look in previous cache */
        idx = rdncache_lookup_by_rdn(cache->prev, parentid, nrdn);
        if (idx >= 0) {
            /* found it, lets copy it in current cache */
            elem = cache->prev->head_per_rdn[idx];
            elem = rdncache_new_elem(cache->cur, elem->eid, elem->pid, elem->nrdnlen, elem->nrdn, elem->rdnlen, ELEMRDN(elem), NULL);
        }
    }
    if (!elem) {
        /* Still Not found. Lets look in index dbi (and copy it in cache) */
        elem = rdncache_index_lookup_by_rdn(cache, parentid, strlen(nrdn)+1, nrdn, lognotfound);
    }

    return elem;
}
/* Search for rdn element in cache
 *   usually expected flags is 1 , 0 means that we are looking for duplicate from rdncache_new_elem()
 */
RDNcacheElem_t *rdncache_rdn_lookup(RDNcache_t *cache,  WorkerQueueData_t *slot, ID parentid, const char *nrdn)
{
    RDNcacheElem_t *elem = NULL;

    RDNCACHE_MUTEX_LOCK(&cache->mutex);
    elem = rdncache_rdn_lookup_no_lock(cache,  slot, parentid, nrdn, 0);
    /* If not found, last chance is that another worker is being processing it. */
    if (!elem) {
        /* So let wait until previous working slot have stored the entry rdn */
        rdncache_wait4older_slots(cache, slot);
        /* Now either it is in the current cache or it is missing */
        elem = rdncache_rdn_lookup_no_lock(cache,  slot, parentid, nrdn, 1);
    }
    /* increase refcount while still holding the lock */
    elem = rdncache_elem_get(elem);
	RDNCACHE_MUTEX_UNLOCK(&cache->mutex);
    return elem;
}

/* For debug with gdb */
void
rdncache_dump_head(RDNcacheHead_t *head)
{
    RDNcacheElem_t *elem;
    int i;
    for (i=0; i<head->nbitems; i++) {
        elem = head->head_per_id[i];
        if (elem) {
            printf("id: %d RDN: %s ParentID: %d\n", elem->eid, elem->nrdn, elem->pid);
        }
    }
}

/* For debug with gdb */
void
rdncache_dump_head_byrdn(RDNcacheHead_t *head)
{
    RDNcacheElem_t *elem;
    int i;
    for (i=0; i<head->nbitems; i++) {
        elem = head->head_per_rdn[i];
        if (elem) {
            printf("id: %d RDN: %s ParentID: %d\n", elem->eid, elem->nrdn, elem->pid);
        }
    }
}
