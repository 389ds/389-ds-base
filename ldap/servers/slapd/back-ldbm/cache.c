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

/* cache.c - routines to maintain an in-core cache of entries */

#include "back-ldbm.h"

#ifdef DEBUG
#define LDAP_CACHE_DEBUG
/* #define LDAP_CACHE_DEBUG_LRU * causes slowdown */
/* #define CACHE_DEBUG * causes slowdown */
#define LDAP_CACHE_DEBUG_LRU  1
#define CACHE_DEBUG  1
#endif


/* don't let hash be smaller than this # of slots */
#define MINHASHSIZE 1024

/*
 * the cache has three entry points (ways to find things):
 *
 *      by entry       e.g., if you already have an entry from the cache
 *                     and want to delete it. (really by entry ptr)
 *      by dn          e.g., when looking for the base object of a search
 *      by id          e.g., for search candidates
 *      by uniqueid
 *
 * these correspond to three different avl trees that are maintained.
 * those avl trees are being destroyed as we speak.
 */

#ifdef LDAP_CACHE_DEBUG
#define ASSERT(_x)                                                                      \
    do {                                                                                \
        if (!(_x)) {                                                                    \
            slapi_log_err(SLAPI_LOG_ERR, "cache", "BAD CACHE ASSERTION at %s/%d: %s\n", \
                          __FILE__, __LINE__, #_x);                                     \
            slapi_log_backtrace(SLAPI_LOG_ERR);                                         \
            *(char *)23L = 1;                                                           \
        }                                                                               \
    } while (0)

#define LOG(...) slapi_log_err(SLAPI_LOG_CACHE, (char *)__func__, __VA_ARGS__)
//# define LOG(_a, _x1, _x2, _x3)  slapi_log_err(SLAPI_LOG_CACHE, _a, _x1, _x2, _x3)
#else
#define ASSERT(_x) ;
//#define LOG(_a, _x1, _x2, _x3)  ;
#define LOG(...)
#endif
#define LOGPATTERN(cache, dn, msg, ...) { if (debug_pattern_matches(cache, dn)) { slapi_log_err(SLAPI_LOG_INFO, (char *)__func__, "CACHE DEBUG: " msg, __VA_ARGS__); } }
#define LOG2(...) slapi_log_err(SLAPI_LOG_INFO, (char *)__func__, __VA_ARGS__)


struct pinned_ctx {
    struct backentry *head;
    struct backentry *tail;
    uint64_t npinned;
    uint64_t size;
};


typedef enum {
    ENTRY_CACHE,
    DN_CACHE,
} CacheType;

#define LRU_DETACH(cache, e) lru_detach((cache), (void *)(e))
#define CACHE_LRU_HEAD(cache, type) ((type)((cache)->c_lruhead))
#define CACHE_LRU_TAIL(cache, type) ((type)((cache)->c_lrutail))
#define BACK_LRU_NEXT(entry, type) ((type)((entry)->ep_lrunext))
#define BACK_LRU_PREV(entry, type) ((type)((entry)->ep_lruprev))

/* static functions */
static void entrycache_clear_int(struct cache *cache);
static void entrycache_set_max_size(struct cache *cache, uint64_t bytes);
static int entrycache_remove_int(struct cache *cache, struct backentry *e);
static void entrycache_return(struct cache *cache, struct backentry **bep, PRBool locked);
static int entrycache_replace(struct cache *cache, struct backentry *olde, struct backentry *newe);
static int entrycache_add_int(struct cache *cache, struct backentry *e, int state, struct backentry **alt);
static struct backentry *entrycache_flush(struct cache *cache);
static bool debug_pattern_matches(struct cache *cache, const char *dn);
#ifdef LDAP_CACHE_DEBUG_LRU
static void entry_lru_verify(struct cache *cache, struct backentry *e, int in);
#endif

static int dn_same_id(const void *bdn, const void *k);
static void dncache_clear_int(struct cache *cache);
static void dncache_set_max_size(struct cache *cache, uint64_t bytes);
static int dncache_remove_int(struct cache *cache, struct backdn *dn);
static void dncache_return(struct cache *cache, struct backdn **bdn);
static int dncache_replace(struct cache *cache, struct backdn *olddn, struct backdn *newdn);
static int dncache_add_int(struct cache *cache, struct backdn *bdn, int state, struct backdn **alt);
static struct backdn *dncache_flush(struct cache *cache);
static int cache_is_in_cache_nolock(void *ptr);
void pinned_remove(struct cache *cache, void *ptr);
void pinned_flush(struct cache *cache);
#ifdef LDAP_CACHE_DEBUG_LRU
static void dn_lru_verify(struct cache *cache, struct backdn *dn, int in);
#endif

/***** tiny hashtable implementation *****/

#define HASH_VALUE(_key, _keylen) \
    ((ht->hashfn == NULL) ? (*(unsigned int *)(_key)) : ((*ht->hashfn)(_key, _keylen)))
#define HASH_NEXT(ht, entry) (*(void **)((char *)(entry) + (ht)->offset))

static int
entry_same_id(const void *e, const void *k)
{
    return (((struct backentry *)e)->ep_id == *(ID *)k);
}

static unsigned long
dn_hash(const void *key, size_t keylen)
{
    unsigned char *x = (unsigned char *)key;
    ssize_t i;
    unsigned long val = 0;

    for (i = keylen - 1; i >= 0; i--)
        val += ((val << 5) + (*x++)) & 0xffffffff;
    return val;
}

#ifdef UUIDCACHE_ON
static unsigned long
uuid_hash(const void *key, size_t keylen)
{
    unsigned char *x = (unsigned char *)key;
    size_t i;
    unsigned long val = 0;

    for (i = 0; i < keylen; i++, x++) {
        char c = (*x <= '9' ? (*x - '0') : (*x - 'A' + 10));
        val = ((val << 4) ^ (val >> 28) ^ c) & 0xffffffff;
    }
    return val;
}

static int
entry_same_uuid(const void *e, const void *k)
{
    struct backentry *be = (struct backentry *)e;
    const char *uuid = slapi_entry_get_uniqueid(be->ep_entry);

    return (strcmp(uuid, (char *)k) == 0);
}
#endif

static int
entry_same_dn(const void *e, const void *k)
{
    struct backentry *be = (struct backentry *)e;
    const char *ndn = slapi_sdn_get_ndn(backentry_get_sdn(be));

    return (strcmp(ndn, (char *)k) == 0);
}

Hashtable *
new_hash(u_long size, u_long offset, HashFn hfn, HashTestFn tfn)
{
    static u_long prime[] = {3, 5, 7, 11, 13, 17, 19};
    Hashtable *ht;
    int ok = 0;
    size_t i = 0;

    if (size < MINHASHSIZE)
        size = MINHASHSIZE;
    /* move up to nearest relative prime (it's a statistical thing) */
    size |= 1;
    do {
        ok = 1;
        for (i = 0; i < (sizeof(prime) / sizeof(prime[0])); i++)
            if (!(size % prime[i]))
                ok = 0;
        if (!ok)
            size += 2;
    } while (!ok);

    ht = (Hashtable *)slapi_ch_calloc(1, sizeof(Hashtable) + size * sizeof(void *));
    if (!ht)
        return NULL;
    ht->size = size;
    ht->offset = offset;
    ht->hashfn = hfn;
    ht->testfn = tfn;
    /* calloc zeroes out the slots automagically */
    return ht;
}

/* adds an entry to the hash -- returns 1 on success, 0 if the key was
 * already there (filled into 'alt' if 'alt' is not NULL)
 */
int
add_hash(Hashtable *ht, void *key, uint32_t keylen, void *entry, void **alt)
{
    struct backcommon *back_entry = (struct backcommon *)entry;
    u_long val, slot;
    void *e;

    val = HASH_VALUE(key, keylen);
    slot = (val % ht->size);
    /* first, check if this key is already in the table */
    e = ht->slot[slot];
    while (e) {
        if ((*ht->testfn)(e, key)) {
            /* ack! already in! */
            if (alt)
                *alt = e;
            return 0;
        }
        e = HASH_NEXT(ht, e);
    }
    /* ok, it's not already there, so add it */
    back_entry->ep_create_time = slapi_current_rel_time_hr();
    HASH_NEXT(ht, entry) = ht->slot[slot];
    ht->slot[slot] = entry;
    return 1;
}

/* returns 1 if the item was found, and puts a ptr to it in 'entry' */
int
find_hash(Hashtable *ht, const void *key, uint32_t keylen, void **entry)
{
    u_long val, slot;
    void *e;

    val = HASH_VALUE(key, keylen);
    slot = (val % ht->size);
    e = ht->slot[slot];
    while (e) {
        if ((*ht->testfn)(e, key)) {
            *entry = e;
            return 1;
        }
        e = HASH_NEXT(ht, e);
    }
    /* no go */
    *entry = NULL;
    return 0;
}

/* returns 1 if the item was found and removed */
int
remove_hash(Hashtable *ht, const void *key, uint32_t keylen)
{
    u_long val, slot;
    void *e, *laste = NULL;

    val = HASH_VALUE(key, keylen);
    slot = (val % ht->size);
    e = ht->slot[slot];
    while (e) {
        if ((*ht->testfn)(e, key)) {
            /* remove this one */
            if (laste)
                HASH_NEXT(ht, laste) = HASH_NEXT(ht, e);
            else
                ht->slot[slot] = HASH_NEXT(ht, e);
            HASH_NEXT(ht, e) = NULL;
            return 1;
        }
        laste = e;
        e = HASH_NEXT(ht, e);
    }
    /* nope */
    return 0;
}

#ifdef LDAP_CACHE_DEBUG
void
dump_hash(Hashtable *ht)
{
    u_long i;
    void *e;
    char ep_id[16];
    char ep_ids[80];
    char *p;
    int ids_size = 80;

    p = ep_ids;
    for (i = 0; i < ht->size; i++) {
        int len;
        e = ht->slot[i];
        if (NULL == e) {
            continue;
        }
        do {
            PR_snprintf(ep_id, 16, "%u-%u", ((struct backcommon *)e)->ep_id, ((struct backcommon *)e)->ep_refcnt);
            len = strlen(ep_id);
            if (ids_size < len + 1) {
                slapi_log_err(SLAPI_LOG_DEBUG, "dump_hash", "%s\n", ep_ids);
                p = ep_ids;
                ids_size = 80;
            }
            PR_snprintf(p, ids_size, "%s:", ep_id);
            p += len + 1;
            ids_size -= len + 1;
        } while ((e = HASH_NEXT(ht, e)));
    }
    if (p != ep_ids) {
        slapi_log_err(SLAPI_LOG_DEBUG, "dump_hash", "%s\n", ep_ids);
    }
}
#endif

/* hashtable distribution stats --
 * slots: # of slots in the hashtable
 * total_entries: # of entries in the hashtable
 * max_entries_per_slot: highest number of chained entries in a single slot
 * slot_stats: if X is the number of entries in a given slot, then
 *     slot_stats[X] will hold the number of slots that held X entries
 */
static void
hash_stats(Hashtable *ht, u_long *slots, int *total_entries, int *max_entries_per_slot, int **slot_stats)
{
#define MAX_SLOT_STATS 50
    u_long i;
    int x;
    void *e;

    *slot_stats = (int *)slapi_ch_malloc(MAX_SLOT_STATS * sizeof(int));
    for (i = 0; i < MAX_SLOT_STATS; i++)
        (*slot_stats)[i] = 0;

    *slots = ht->size;
    *max_entries_per_slot = 0;
    *total_entries = 0;
    for (i = 0; i < ht->size; i++) {
        e = ht->slot[i];
        x = 0;
        while (e) {
            x++;
            (*total_entries)++;
            e = HASH_NEXT(ht, e);
        }
        if (x < MAX_SLOT_STATS)
            (*slot_stats)[x]++;
        if (x > *max_entries_per_slot)
            *max_entries_per_slot = x;
    }
}


/***** add/remove entries to/from the LRU list *****/

#ifdef LDAP_CACHE_DEBUG_LRU
static void
pinned_verify(struct cache *cache, int lineno)
{
    uint64_t size = 0;
    uint64_t count = 0;
    struct backentry *e = cache->c_pinned_ctx->head;
    for (;e; e = BACK_LRU_NEXT(e, struct backentry*)) {
        if (e->ep_lrunext == NULL) {
            ASSERT (cache->c_pinned_ctx->tail == e);
        }
        ASSERT((e->ep_state & ENTRY_STATE_PINNED) == ENTRY_STATE_PINNED);
        size += e->ep_size;
        count ++;
    }
    ASSERT(size == cache->c_pinned_ctx->size);
    ASSERT(count == cache->c_pinned_ctx->npinned);
}

static void
lru_verify(struct cache *cache, void *ptr, int in)
{
    struct backcommon *e;
    if (NULL == ptr) {
        LOG("=> lru_verify\n<= lru_verify (null entry)\n");
        return;
    }
    e = (struct backcommon *)ptr;
    if (CACHE_TYPE_ENTRY == e->ep_type) {
        entry_lru_verify(cache, (struct backentry *)e, in);
    } else {
        dn_lru_verify(cache, (struct backdn *)e, in);
    }
}

/* for debugging -- painstakingly verify the lru list is ok -- if 'in' is
 * true, then entry 'e' should be in the list right now; otherwise, it
 * should NOT be in the list.
 */
static void
entry_lru_verify(struct cache *cache, struct backentry *e, int in)
{
    int is_in = 0;
    int count = 0;
    struct backentry *ep;

    ep = CACHE_LRU_HEAD(cache, struct backentry *);
    while (ep) {
        ASSERT((e->ep_state & ENTRY_STATE_PINNED) == 0);
        count++;
        if (ep == e) {
            is_in = 1;
        }
        if (ep->ep_lruprev) {
            ASSERT(BACK_LRU_NEXT(BACK_LRU_PREV(ep, struct backentry *), struct backentry *) == ep);
        } else {
            ASSERT(ep == CACHE_LRU_HEAD(cache, struct backentry *));
        }
        if (ep->ep_lrunext) {
            ASSERT(BACK_LRU_PREV(BACK_LRU_NEXT(ep, struct backentry *), struct backentry *) == ep);
        } else {
            ASSERT(ep == CACHE_LRU_TAIL(cache, struct backentry *));
        }

        ep = BACK_LRU_NEXT(ep, struct backentry *);
    }
    ASSERT(is_in == in);
}
#else
#define pinned_verify(cache, lineno)
#endif

/* assume lock is held */
static void
lru_detach(struct cache *cache, void *ptr)
{
    struct backcommon *e;
    if (NULL == ptr) {
        LOG("=> lru_detach\n<= lru_detach (null entry)\n");
        return;
    }
    e = (struct backcommon *)ptr;
#ifdef LDAP_CACHE_DEBUG_LRU
    pinned_verify(cache, __LINE__);
    lru_verify(cache, e, 1);
#endif
    if (e->ep_lruprev) {
        e->ep_lruprev->ep_lrunext = NULL;
        cache->c_lrutail = e->ep_lruprev;
    } else {
        cache->c_lruhead = NULL;
        cache->c_lrutail = NULL;
    }
#ifdef LDAP_CACHE_DEBUG_LRU
    lru_verify(cache, e, 0);
#endif
}

/* assume lock is held */
static void
lru_delete(struct cache *cache, void *ptr)
{
    struct backcommon *e;
    if (NULL == ptr) {
        LOG("=> lru_delete\n<= lru_delete (null entry)\n");
        return;
    }
    e = (struct backcommon *)ptr;
#ifdef LDAP_CACHE_DEBUG_LRU
    pinned_verify(cache, __LINE__);
    lru_verify(cache, e, 1);
#endif
    if (e->ep_lruprev)
        e->ep_lruprev->ep_lrunext = e->ep_lrunext;
    else
        cache->c_lruhead = e->ep_lrunext;
    if (e->ep_lrunext)
        e->ep_lrunext->ep_lruprev = e->ep_lruprev;
    else
        cache->c_lrutail = e->ep_lruprev;
#ifdef LDAP_CACHE_DEBUG_LRU
    e->ep_lrunext = e->ep_lruprev = NULL;
    lru_verify(cache, e, 0);
#endif
}

/* assume lock is held */
static void
lru_add(struct cache *cache, void *ptr)
{
    struct backcommon *e;
    if (NULL == ptr) {
        LOG("=> lru_add\n<= lru_add (null entry)\n");
        return;
    }
    e = (struct backcommon *)ptr;
    ASSERT(e->ep_refcnt == 0);
    ASSERT((e->ep_state & ENTRY_STATE_PINNED) == 0);

#ifdef LDAP_CACHE_DEBUG_LRU
    pinned_verify(cache, __LINE__);
    lru_verify(cache, e, 0);
#endif
    e->ep_lruprev = NULL;
    e->ep_lrunext = cache->c_lruhead;
    cache->c_lruhead = e;
    if (e->ep_lrunext)
        e->ep_lrunext->ep_lruprev = e;
    if (!cache->c_lrutail)
        cache->c_lrutail = e;
#ifdef LDAP_CACHE_DEBUG_LRU
    lru_verify(cache, e, 1);
#endif
}


/***** cache overhead *****/

static void
cache_make_hashes(struct cache *cache, int type)
{
    u_long hashsize = (cache->c_stats.maxentries > 0) ? cache->c_stats.maxentries : (cache->c_stats.maxsize / 512);

    if (CACHE_TYPE_ENTRY == type) {
        cache->c_dntable = new_hash(hashsize,
                                    HASHLOC(struct backentry, ep_dn_link),
                                    dn_hash, entry_same_dn);
        cache->c_idtable = new_hash(hashsize,
                                    HASHLOC(struct backentry, ep_id_link),
                                    NULL, entry_same_id);
#ifdef UUIDCACHE_ON
        cache->c_uuidtable = new_hash(hashsize,
                                      HASHLOC(struct backentry, ep_uuid_link),
                                      uuid_hash, entry_same_uuid);
#endif
    } else if (CACHE_TYPE_DN == type) {
        cache->c_dntable = NULL;
        cache->c_idtable = new_hash(hashsize,
                                    HASHLOC(struct backdn, dn_id_link),
                                    NULL, dn_same_id);
#ifdef UUIDCACHE_ON
        cache->c_uuidtable = NULL;
#endif
    }
}

/*
 * Helper function for flush_hash() to calculate if the entry should be
 * removed from the cache.
 */
static int32_t
flush_remove_entry(struct timespec *entry_time, struct timespec *start_time)
{
    struct timespec diff;

    slapi_timespec_diff(entry_time, start_time, &diff);
    if (diff.tv_sec >= 0) {
        return 1;
    } else {
        return 0;
    }
}

static inline void
dbgec_test_if_entry_pointer_is_valid(void *e, void *prev, int slot, int line)
{
    /* Check if the entry pointer is rightly aligned and crash loudly otherwise */
    if ( ((uint64_t)e) & ((sizeof(long))-1) ) {
        /* If this message occurs, it means that we have reproduced the elusive entry cache corruption
         * seen first while fixing replication conflict_resolution CI test
         * FYI some debug attempt have been stored in https://github.com/progier389/389-ds-base.git
         * in branches:
         *  debug-stuff1 (older try with log of debugging trick in slapd/dbgec*)
         *  debug-stuff2: Log the dn associated with backentries in a mmap file and retrieve the
         *  dn of the corrupted entry.
         *   Note: if we are able to reproduce using this debug stuff and if it is always the same entry
         *   we may catch the issue by adding a watchpoint when adding that backentry
         *   Note: you should disable the setuid to be able to use watchpoint
         */
        slapi_log_err(SLAPI_LOG_FATAL, "dbgec_test_if_entry_pointer_is_valid", "cache.c[%d]: Wrong entry address: %p Previous entry address is: %p hash table slot is %d\n", line, e, prev, slot);
        slapi_log_backtrace(SLAPI_LOG_FATAL);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds="
#pragma GCC diagnostic ignored "-Wstringop-overflow="
        *(char*)23 = 1;   /* abort() somehow corrupt gdb stack backtrace so lets generate a SIGSEGV */
#pragma GCC diagnostic pop
        abort();
    }
}


/*
 * Flush all the cache entries that were added after the "start time"
 * This is called when a backend transaction plugin fails, and we need
 * to remove all the possible invalid entries in the cache.  We need
 * to check both the ID and DN hashtables when checking the entry cache.
 *
 * If the ref count is 0, we can straight up remove it from the cache, but
 * if the ref count is greater than 1, then the entry is currently in use.
 * In the later case we set the entry state to ENTRY_STATE_INVALID, and
 * when the owning thread cache_returns() the cache entry is automatically
 * removed so another thread can not use/lock the invalid cache entry.
 */
static void
flush_hash(struct cache *cache, struct timespec *start_time, int32_t type)
{
    Hashtable *ht = cache->c_idtable; /* start with the ID table as it's in both ENTRY and DN caches */
    void *e, *laste = NULL;
    char flush_etime[ETIME_BUFSIZ] = {0};
    struct timespec duration;
    struct timespec flush_start;
    struct timespec flush_end;

    clock_gettime(CLOCK_MONOTONIC, &flush_start);
    cache_lock(cache);

    for (size_t i = 0; i < ht->size; i++) {
        e = ht->slot[i];
        dbgec_test_if_entry_pointer_is_valid(e, NULL, i, __LINE__);
        while (e) {
            struct backcommon *entry = (struct backcommon *)e;
            uint64_t remove_it = 0;
            if (flush_remove_entry(&entry->ep_create_time, start_time)) {
                /* Mark the entry to be removed */
                slapi_log_err(SLAPI_LOG_CACHE, "flush_hash", "[%s] Removing entry id (%d)\n",
                        type ? "DN CACHE" : "ENTRY CACHE", entry->ep_id);
                remove_it = 1;
            }
            laste = e;
            e = HASH_NEXT(ht, e);
            dbgec_test_if_entry_pointer_is_valid(e, laste, i, __LINE__);

            if (remove_it) {
                /* since we have the cache lock we know we can trust refcnt */
                entry->ep_state |= ENTRY_STATE_INVALID;
                if (entry->ep_refcnt == 0) {
                    entry->ep_refcnt++;
                    if (entry->ep_state & ENTRY_STATE_PINNED) {
                        pinned_remove(cache, laste);
                        lru_delete(cache, laste);
                    } else {
                        lru_delete(cache, laste);
                    }
                    if (type == ENTRY_CACHE) {
                        entrycache_remove_int(cache, laste);
                        entrycache_return(cache, (struct backentry **)&laste, PR_TRUE);
                    } else {
                        dncache_remove_int(cache, laste);
                        dncache_return(cache, (struct backdn **)&laste);
                    }
                } else {
                    /* Entry flagged for removal */
                    slapi_log_err(SLAPI_LOG_CACHE, "flush_hash",
                            "[%s] Flagging entry to be removed later: id (%d) refcnt: %d\n",
                            type ? "DN CACHE" : "ENTRY CACHE", entry->ep_id, entry->ep_refcnt);
                }
            }
        }
    }

    if (type == ENTRY_CACHE) {
        /* Also check the DN hashtable */
        ht = cache->c_dntable;

        for (size_t i = 0; i < ht->size; i++) {
            e = ht->slot[i];
            dbgec_test_if_entry_pointer_is_valid(e, NULL, i, __LINE__);
            while (e) {
                struct backcommon *entry = (struct backcommon *)e;
                uint64_t remove_it = 0;
                if (flush_remove_entry(&entry->ep_create_time, start_time)) {
                    /* Mark the entry to be removed */
                    slapi_log_err(SLAPI_LOG_CACHE, "flush_hash", "[ENTRY CACHE] Removing entry id (%d)\n",
                            entry->ep_id);
                    remove_it = 1;
                }
                laste = e;
                e = HASH_NEXT(ht, e);
                dbgec_test_if_entry_pointer_is_valid(e, laste, i, __LINE__);

                if (remove_it) {
                    /* since we have the cache lock we know we can trust refcnt */
                    entry->ep_state |= ENTRY_STATE_INVALID;
                    if (entry->ep_refcnt == 0) {
                        entry->ep_refcnt++;
                        if (entry->ep_state & ENTRY_STATE_PINNED) {
                            pinned_remove(cache, laste);
                            lru_delete(cache, laste);
                        } else {
                            lru_delete(cache, laste);
                        }
                        entrycache_remove_int(cache, laste);
                        entrycache_return(cache, (struct backentry **)&laste, PR_TRUE);
                    } else {
                        /* Entry flagged for removal */
                        slapi_log_err(SLAPI_LOG_CACHE, "flush_hash",
                                "[ENTRY CACHE] Flagging entry to be removed later: id (%d) refcnt: %d\n",
                                entry->ep_id, entry->ep_refcnt);
                    }
                }
            }
        }
    }

    cache_unlock(cache);

    clock_gettime(CLOCK_MONOTONIC, &flush_end);
    slapi_timespec_diff(&flush_end, &flush_start, &duration);
    snprintf(flush_etime, ETIME_BUFSIZ, "%" PRId64 ".%.09" PRId64 "", (int64_t)duration.tv_sec, (int64_t)duration.tv_nsec);
    slapi_log_err(SLAPI_LOG_WARNING, "flush_hash", "Upon BETXN callback failure, entry cache is flushed during %s\n", flush_etime);
}

void
revert_cache(ldbm_instance *inst, struct timespec *start_time)
{
    if (inst == NULL) {
        return;
    }
    flush_hash(&inst->inst_cache, start_time, ENTRY_CACHE);
    flush_hash(&inst->inst_dncache, start_time, DN_CACHE);
}

/* initialize the cache */
int
cache_init(struct cache *cache, struct ldbm_instance *inst, uint64_t maxsize, int64_t maxentries, int type)
{
    slapi_log_err(SLAPI_LOG_TRACE, "cache_init", "-->\n");
    struct cache_stats stats_zeros = { 0 };
    cache->c_stats = stats_zeros;
    cache->c_stats.maxsize = maxsize;
    /* coverity[missing_lock] */
    cache->c_stats.maxentries = maxentries;
    cache->c_inst = inst;
    cache->c_lruhead = cache->c_lrutail = NULL;
    cache_make_hashes(cache, type);
    cache->c_pinned_ctx = (struct pinned_ctx*)slapi_ch_calloc(1, sizeof (struct pinned_ctx));
    
    if (((cache->c_mutex = PR_NewMonitor()) == NULL) ||
        ((cache->c_emutexalloc_mutex = PR_NewLock()) == NULL)) {
        slapi_log_err(SLAPI_LOG_ERR, "cache_init", "PR_NewMonitor failed\n");
        return 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "cache_init", "<--\n");
    return 1;
}

#define CACHE_FULL(cache)                                                  \
    (((cache)->c_stats.size > (cache)->c_stats.maxsize) || \
     (((cache)->c_stats.maxentries > 0) &&                                       \
      ((cache)->c_stats.nentries > (cache)->c_stats.maxentries)))

#define NOT_0(v) (((v)==0) ? 1L : (v))

#define AV_WEIGHT(cache) ((cache)->c_stats.weight/NOT_0((cache)->c_stats.nehw))


/* clear out the cache to make room for new entries
 * you must be holding cache->c_mutex !!
 * return a pointer on the list of entries that get kicked out
 * of the cache.
 * These entries should be freed outside of the cache->c_mutex
 */
static struct backentry *
entrycache_flush(struct cache *cache)
{
    struct backentry *e = NULL;

    LOG("=> entrycache_flush\n");

    /* all entries on the LRU list are guaranteed to have a refcnt = 0
     * (iow, nobody's using them), so just delete from the tail down
     * until the cache is a managable size again.
     * (cache->c_mutex is locked when we enter this)
     */
    while ((cache->c_lrutail != NULL) && CACHE_FULL(cache)) {
        if (e == NULL) {
            e = CACHE_LRU_TAIL(cache, struct backentry *);
        } else {
            e = BACK_LRU_PREV(e, struct backentry *);
        }
        ASSERT(e->ep_refcnt == 0);
        e->ep_refcnt++;
        if (entrycache_remove_int(cache, e) < 0) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "entrycache_flush", "Unable to delete entry\n");
            break;
        }
        if (e == CACHE_LRU_HEAD(cache, struct backentry *)) {
            break;
        }
    }
    if (e)
        LRU_DETACH(cache, e);
    LOG("<= entrycache_flush (down to %lu entries, %lu bytes)\n",
        cache->c_stats.nentries, cache->c_stats.size);
    return e;
}

/* remove everything from the cache */
static void
entrycache_clear_int(struct cache *cache)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;
    size_t size = cache->c_stats.maxsize;

    cache->c_stats.maxsize = 0;
    pinned_flush(cache);
    eflush = entrycache_flush(cache);
    while (eflush) {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    cache->c_stats.maxsize = size;
    if (cache->c_stats.nentries > 0) {
        slapi_log_err(SLAPI_LOG_CACHE,
                      "entrycache_clear_int", "There are still %" PRIu64 " entries "
                                              "in the entry cache.\n",
                      cache->c_stats.nentries);
#ifdef LDAP_CACHE_DEBUG
        slapi_log_err(SLAPI_LOG_DEBUG, "entrycache_clear_int", "ID(s) in entry cache:\n");
        dump_hash(cache->c_idtable);
#endif
    }
}

void
cache_clear(struct cache *cache, int type)
{
    cache_lock(cache);
    if (CACHE_TYPE_ENTRY == type) {
        entrycache_clear_int(cache);
    } else if (CACHE_TYPE_DN == type) {
        dncache_clear_int(cache);
    }
    cache_unlock(cache);
}

static void
erase_cache(struct cache *cache, int type)
{
    if (CACHE_TYPE_ENTRY == type) {
        entrycache_clear_int(cache);
    } else if (CACHE_TYPE_DN == type) {
        dncache_clear_int(cache);
    }
    slapi_ch_free((void **)&cache->c_dntable);
    slapi_ch_free((void **)&cache->c_idtable);
#ifdef UUIDCACHE_ON
    slapi_ch_free((void **)&cache->c_uuidtable);
#endif
}

/* to be used on shutdown or when destroying a backend instance */
void
cache_destroy_please(struct cache *cache, int type)
{
    erase_cache(cache, type);
    PR_DestroyMonitor(cache->c_mutex);
    PR_DestroyLock(cache->c_emutexalloc_mutex);
}

void
cache_set_max_size(struct cache *cache, uint64_t bytes, int type)
{
    if (CACHE_TYPE_ENTRY == type) {
        entrycache_set_max_size(cache, bytes);
    } else if (CACHE_TYPE_DN == type) {
        dncache_set_max_size(cache, bytes);
    }
}

static void
entrycache_set_max_size(struct cache *cache, uint64_t bytes)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;

    if (bytes < MINCACHESIZE) {
        /* During startup, this value can be 0 to indicate an autotune is about
         * to happen. In that case, suppress this warning.
         */
        if (bytes > 0) {
            slapi_log_err(SLAPI_LOG_WARNING, "entrycache_set_max_size", "Minimum cache size is %" PRIu64 " -- rounding up\n", MINCACHESIZE);
        }
        bytes = MINCACHESIZE;
    }
    cache_lock(cache);
    cache->c_stats.maxsize = bytes;
    LOG("entry cache size set to %" PRIu64 "\n", bytes);
    /* check for full cache, and clear out if necessary */
    if (CACHE_FULL(cache)) {
        pinned_flush(cache);
        eflush = entrycache_flush(cache);
    }
    while (eflush) {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    if (cache->c_stats.nentries < 50) {
        /* there's hardly anything left in the cache -- clear it out and
        * resize the hashtables for efficiency.
        */
        erase_cache(cache, CACHE_TYPE_ENTRY);
        cache_make_hashes(cache, CACHE_TYPE_ENTRY);
    }
    cache_unlock(cache);
    /* This may already have been called by one of the functions in
     * ldbm_instance_config
     */
    slapi_pal_meminfo *mi = spal_meminfo_get();
    if (util_is_cachesize_sane(mi, &bytes) != UTIL_CACHESIZE_VALID) {
        slapi_log_err(SLAPI_LOG_WARNING, "entrycache_set_max_size", "Cachesize (%" PRIu64 ") may use more than the available physical memory.\n", bytes);
    }
    spal_meminfo_destroy(mi);
}

void
cache_set_max_entries(struct cache *cache, int64_t entries)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;

    /* this is a dumb remnant of pre-5.0 servers, where the cache size
     * was given in # entries instead of memory footprint.  hopefully,
     * we can eventually drop this.
     */
    cache_lock(cache);
    cache->c_stats.maxentries = entries;
    if (entries >= 0) {
        LOG("entry cache entry-limit set to %lu\n", entries);
    } else {
        LOG("entry cache entry-limit turned off\n");
    }

    /* check for full cache, and clear out if necessary */
    if (CACHE_FULL(cache)) {
        pinned_flush(cache);
        eflush = entrycache_flush(cache);
    }
    cache_unlock(cache);
    while (eflush) {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
}

uint64_t
cache_get_max_size(struct cache *cache)
{
    uint64_t n = 0;

    cache_lock(cache);
    n = cache->c_stats.maxsize;
    cache_unlock(cache);
    return n;
}

int64_t
cache_get_max_entries(struct cache *cache)
{
    int64_t n;

    cache_lock(cache);
    n = cache->c_stats.maxentries;
    cache_unlock(cache);
    return n;
}

/* determine the general size of a cache entry */
static size_t
cache_entry_size(struct backentry *e)
{
    size_t size = 0;

    if (e->ep_entry)
        size += slapi_entry_size(e->ep_entry);
    if (e->ep_vlventry)
        size += slapi_entry_size(e->ep_vlventry);
    /* cannot size ep_mutexp (PRLock) */
    size += sizeof(struct backentry);
    return size;
}

/* the monitor code wants to be able to safely fetch the cache stats */
void
cache_get_stats(struct cache *cache, struct cache_stats *stats)
{
    cache_lock(cache);
    *stats = cache->c_stats;
    cache_unlock(cache);
}

void
cache_debug_hash(struct cache *cache, char **out)
{
    u_long slots;
    int total_entries, max_entries_per_slot, *slot_stats;
    int i, j;
    Hashtable *ht = NULL;
    const char *name = "unknown";

    cache_lock(cache);
    *out = (char *)slapi_ch_malloc(1024);
    **out = 0;

    for (i = 0; i < 3; i++) {
        if (i > 0)
            sprintf(*out + strlen(*out), "; ");
        switch (i) {
        case 0:
            ht = cache->c_dntable;
            name = "dn";
            break;
        case 1:
            ht = cache->c_idtable;
            name = "id";
            break;
#ifdef UUIDCACHE_ON
        case 2:
        default:
            ht = cache->c_uuidtable;
            name = "uuid";
            break;
#endif
        }
        if (NULL == ht) {
            continue;
        }
        hash_stats(ht, &slots, &total_entries, &max_entries_per_slot,
                   &slot_stats);
        sprintf(*out + strlen(*out), "%s hash: %lu slots, %d items (%d max "
                                     "items per slot) -- ",
                name, slots, total_entries,
                max_entries_per_slot);
        for (j = 0; j <= max_entries_per_slot; j++)
            sprintf(*out + strlen(*out), "%d[%d] ", j, slot_stats[j]);
        slapi_ch_free((void **)&slot_stats);
    }
    cache_unlock(cache);
}


/***** general-purpose cache stuff *****/

/* Determine if a entry should be logged. */
static bool
debug_pattern_matches(struct cache *cache, const char *dn)
{
    if (cache->c_inst->cache_debug_re && dn) {
        if (slapi_re_exec_nt(cache->c_inst->cache_debug_re, dn)) {
            return true;
        }
    }
    return false;
}

/* remove an entry from the cache */
/* you must be holding c_mutex !! */
static int
entrycache_remove_int(struct cache *cache, struct backentry *e)
{
    int ret = 1; /* assume not in cache */
    const char *ndn;
#ifdef UUIDCACHE_ON
    const char *uuid;
#endif

    LOGPATTERN(cache, backentry_get_ndn(e),
               "Cache average weight is %lu . Removing entry from "
               " cache with size: %lu, weight: %lu, dn:%s\n",
               AV_WEIGHT(cache), e->ep_size, e->ep_weight,
               backentry_get_ndn(e));
    LOG("=> entrycache_remove_int (%s) (%u) (%u)\n", backentry_get_ndn(e), e->ep_id, e->ep_refcnt);
    if (e->ep_state & ENTRY_STATE_NOTINCACHE) {
        return ret;
    }

    /* remove from all hashtables -- this function may be called from places
     * where the entry isn't in all the tables yet, so we don't care if any
     * of these return errors.
     */
    ndn = slapi_sdn_get_ndn(backentry_get_sdn(e));
    if (remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn))) {
        ret = 0;
    } else {
        LOG("remove %s from dn hash failed\n", ndn);
    }
    /* if entry was added tentatively, it will be in the dntable
       but not in the idtable - we cannot just remove it from
       the idtable - in the case of modrdn, this will remove
       the _real_ entry from the idtable, leading to a cache
       imbalance
    */
    if (!(e->ep_state & ENTRY_STATE_CREATING)) {
        if (remove_hash(cache->c_idtable, &(e->ep_id), sizeof(ID))) {
            ret = 0;
        } else {
            LOG("remove %s (%d) from id hash failed\n", ndn, e->ep_id);
        }
    }
#ifdef UUIDCACHE_ON
    uuid = slapi_entry_get_uniqueid(e->ep_entry);
    if (remove_hash(cache->c_uuidtable, (void *)uuid, strlen(uuid))) {
        ret = 0;
    } else {
        LOG("remove %d from uuid hash failed\n", uuid);
    }
#endif
    if (ret == 0) {
        /* won't be on the LRU list since it has a refcount on it */
        /* adjust cache size */
        cache->c_stats.size -= e->ep_size;
        cache->c_stats.nentries--;
        cache->c_stats.weight -= e->ep_weight;
        if (e->ep_weight != 0) {
            cache->c_stats.nehw--;
        }
        LOG("<= entrycache_remove_int (id %d, size %lu, weight %lu): "
            "cache now %lu entries, %lu bytes, average weight %lu\n",
            e->ep_id, e->ep_size, e->ep_weight,
            cache->c_stats.nentries, cache->c_stats.size,
            AV_WEIGHT(cache));
    }

    /* mark for deletion (will be erased when refcount drops to zero) */
    e->ep_state |= ENTRY_STATE_DELETED;
#if 0
    if (slapi_is_loglevel_set(SLAPI_LOG_CACHE)) {
        dump_hash(cache->c_idtable);
    }
#endif
    LOG("<= entrycache_remove_int: %d\n", ret);
    return ret;
}

/* Remove the entry from pinned table.
 * Assume lock is held
 */
void
pinned_remove(struct cache *cache, void *ptr)
{
    struct backentry *e = (struct backentry *)ptr;
    ASSERT(e->ep_state & ENTRY_STATE_PINNED);
    cache->c_pinned_ctx->npinned--;
    cache->c_pinned_ctx->size -= e->ep_size;
    e->ep_state &= ~ENTRY_STATE_PINNED;
    LOGPATTERN(cache, backentry_get_ndn(e),
               "Removing entry %s weight: %lu from pinned entries\n",
               backentry_get_ndn(e), e->ep_weight);

    if (cache->c_pinned_ctx->head == e) {
        if (cache->c_pinned_ctx->tail == e) {
            cache->c_pinned_ctx->head = cache->c_pinned_ctx->tail = NULL;
        } else {
            cache->c_pinned_ctx->head = BACK_LRU_NEXT(e, struct backentry *);
        }
    } else if (cache->c_pinned_ctx->tail == e) {
        cache->c_pinned_ctx->tail = BACK_LRU_PREV(e, struct backentry *);
    } else {
        BACK_LRU_PREV(e, struct backentry *)->ep_lrunext = BACK_LRU_NEXT(e, struct backcommon *);
        BACK_LRU_NEXT(e, struct backentry *)->ep_lruprev = BACK_LRU_PREV(e, struct backcommon *);
    }
    e->ep_lrunext = e->ep_lruprev = NULL;
    if (e->ep_refcnt == 0) {
        lru_add(cache, ptr);
    }
}

/* Ensure pinned entries respects the cache memory and maxentrie limits
 * May put entries back in the lru so this function should be called 
 * just before calling entrycache_flush
 */
void
pinned_flush(struct cache *cache)
{
    uint64_t maxpinned = cache->c_inst->cache_pinned_entries;

    pinned_verify(cache, __LINE__);
    if (cache->c_stats.maxentries >= 0 && cache->c_stats.maxentries < maxpinned) {
        maxpinned = cache->c_stats.maxentries;
    }
    while (cache->c_pinned_ctx->npinned > maxpinned) {
        pinned_remove(cache, cache->c_pinned_ctx->head);
    }
    pinned_verify(cache, __LINE__);
    while (cache->c_pinned_ctx->size > (cache)->c_stats.maxsize) {
        pinned_remove(cache, cache->c_pinned_ctx->head);
    }
    pinned_verify(cache, __LINE__);
}

/* Check if entry should be pinned and eventuially add it in the
 *  pinned entry list
 * Returns true if the entry is pinned
 * Assume lock is held
 */
bool
pinned_add(struct cache *cache, void *ptr)
{
    struct backentry *e = (struct backentry *)ptr;
    struct backentry *e2 = NULL;
    uint64_t maxpinned = cache->c_inst->cache_pinned_entries;

    if (cache->c_stats.maxentries >= 0 && cache->c_stats.maxentries < maxpinned) {
        maxpinned = cache->c_stats.maxentries;
    }

    if (cache->c_pinned_ctx->npinned > maxpinned) {
        pinned_flush(cache);
    }
    pinned_verify(cache, __LINE__);
    if (cache->c_inst->cache_pinned_entries == 0) {
        return false;
    }
    if (e->ep_weight == 0) {
        /* Do not bother to store entries without weight in the pinned array */
        return false;
    }
    if (e->ep_state & ENTRY_STATE_PINNED) {
        /* Entry is already pinned ==> nothingh to do */
        return true;
    }
    if ((cache->c_pinned_ctx->head) &&
        (cache->c_pinned_ctx->npinned >= maxpinned) &&
        (cache->c_pinned_ctx->head->ep_weight >= e->ep_weight)) {
            return false;
    }
    /* Now it is time to insert the entry in the pinned list */
    cache->c_pinned_ctx->npinned++;
    cache->c_pinned_ctx->size += e->ep_size;
    e->ep_state |= ENTRY_STATE_PINNED;
    e2 = cache->c_pinned_ctx->head;
    if (e2 == NULL) {
        cache->c_pinned_ctx->head = cache->c_pinned_ctx->tail = e;
        e->ep_lrunext = e->ep_lruprev = NULL;
        LOGPATTERN(cache, backentry_get_ndn(e),
                   "Adding entry %s weight: %lu from pinned entries\n",
                   backentry_get_ndn(e), e->ep_weight);
        pinned_flush(cache);
        pinned_verify(cache, __LINE__);
        return true;
    }
    for (;;) {
        if (e2->ep_weight > e->ep_weight) {
            /* Should insert e before e2 */
            e->ep_lrunext = (struct backcommon*)e2;
            e->ep_lruprev = e2->ep_lruprev;
            e2->ep_lruprev = (struct backcommon*)e;
            if (e->ep_lruprev == NULL) {
                cache->c_pinned_ctx->head = e;
            } else {
                e->ep_lruprev->ep_lrunext = (struct backcommon*)e;
            }
            break;
        } else if (e2->ep_lrunext == NULL) {
            /* Should add e after e2 */
            e2->ep_lrunext = (struct backcommon*)e;
            e->ep_lruprev = (struct backcommon*)e2;
            e->ep_lrunext = NULL;
            cache->c_pinned_ctx->tail = e;
            break;
        } else {
            e2 = BACK_LRU_NEXT(e2, struct backentry*);
        }
    }
    pinned_verify(cache, __LINE__);
    pinned_flush(cache);
    pinned_verify(cache, __LINE__);
    LOGPATTERN(cache, backentry_get_ndn(e),
               "Adding entry %s weight: %lu from pinned entries\n",
               backentry_get_ndn(e), e->ep_weight);
    return true;
}

/* remove an entry/a dn from the cache.
 * you must have a refcount on e (iow, fetched via cache_find_*).  the
 * entry is removed from the cache, but NOT freed!  you are responsible
 * for freeing the entry yourself when done with it, preferrably via
 * cache_return (called AFTER cache_remove).  some code still does this
 * via backentry_free, which is okay, as long as you know you're the only
 * thread holding a reference to the deleted entry.
 * returns:       0 on success
 *              1 if the entry wasn't in the cache at all (not even partially)
 */
int
cache_remove(struct cache *cache, void *ptr)
{
    int ret = 0;
    struct backcommon *e;
    if (NULL == ptr) {
        LOG("=> lru_remove\n<= lru_remove (null entry)\n");
        return ret;
    }
    e = (struct backcommon *)ptr;

    cache_lock(cache);
    if (CACHE_TYPE_ENTRY == e->ep_type) {
        ASSERT(e->ep_refcnt > 0);
        ret = entrycache_remove_int(cache, (struct backentry *)e);
    } else if (CACHE_TYPE_DN == e->ep_type) {
        ret = dncache_remove_int(cache, (struct backdn *)e);
    }
    cache_unlock(cache);
    return ret;
}

/* replace an entry in the cache.
 * returns:       0 on success
 *                1 if the entry wasn't in the cache
 */
int
cache_replace(struct cache *cache, void *oldptr, void *newptr)
{
    struct backcommon *olde;
    if (NULL == oldptr || NULL == newptr) {
        LOG("=> lru_replace\n<= lru_replace (null entry)\n");
        return 0;
    }
    olde = (struct backcommon *)oldptr;

    if (CACHE_TYPE_ENTRY == olde->ep_type) {
        return entrycache_replace(cache, (struct backentry *)oldptr,
                                  (struct backentry *)newptr);
    } else if (CACHE_TYPE_DN == olde->ep_type) {
        return dncache_replace(cache, (struct backdn *)oldptr,
                               (struct backdn *)newptr);
    }
    return 0;
}

static int
entrycache_replace(struct cache *cache, struct backentry *olde, struct backentry *newe)
{
    int found = 0;
    int found_in_dn = 0;
    int found_in_id = 0;
#ifdef UUIDCACHE_ON
    int found_in_uuid = 0;
#endif
    const char *oldndn;
    const char *newndn;
#ifdef UUIDCACHE_ON
    const char *olduuid;
    const char *newuuid;
#endif
    size_t entry_size = 0;
    struct backentry *alte = NULL;
    Slapi_Attr *attr = NULL;

    LOG("=> entrycache_replace (%s) -> (%s)\n", backentry_get_ndn(olde),
        backentry_get_ndn(newe));

    /* remove from all hashtables -- this function may be called from places
     * where the entry isn't in all the tables yet, so we don't care if any
     * of these return errors.
     */
    oldndn = slapi_sdn_get_ndn(backentry_get_sdn(olde));
#ifdef UUIDCACHE_ON
    olduuid = slapi_entry_get_uniqueid(olde->ep_entry);
    newuuid = slapi_entry_get_uniqueid(newe->ep_entry);
#endif
    newndn = slapi_sdn_get_ndn(backentry_get_sdn(newe));
    entry_size = cache_entry_size(newe);

    /* Might have added/removed a referral */
    if (slapi_entry_attr_find(newe->ep_entry, "ref", &attr) && attr) {
        slapi_entry_set_flag(newe->ep_entry, SLAPI_ENTRY_FLAG_REFERRAL);
    } else {
        slapi_entry_clear_flag(newe->ep_entry, SLAPI_ENTRY_FLAG_REFERRAL);
    }

    cache_lock(cache);

    /*
     * First, remove the old entry from all the hashtables.
     * If the old entry is in cache but not in at least one of the
     * cache tables, operation error
     */
    if ((olde->ep_state & ENTRY_STATE_NOTINCACHE) == 0) {
        found_in_dn = remove_hash(cache->c_dntable, (void *)oldndn, strlen(oldndn));
        found_in_id = remove_hash(cache->c_idtable, &(olde->ep_id), sizeof(ID));
#ifdef UUIDCACHE_ON
        found_in_uuid = remove_hash(cache->c_uuidtable, (void *)olduuid, strlen(olduuid));
#endif
        found = found_in_dn && found_in_id;
#ifdef UUIDCACHE_ON
        found = found && found_in_uuid;
#endif
    }
    /* If fails, we have to make sure the both entires are removed from the cache,
     * otherwise, we have no idea what's left in the cache or not... */
    if (cache_is_in_cache_nolock(newe)) {
        /* if we're doing a modrdn or turning an entry to a tombstone,
         * the new entry can be in the dn table already, so we need to remove that too.
         */
        if (remove_hash(cache->c_dntable, (void *)newndn, strlen(newndn))) {
            cache->c_stats.size -= newe->ep_size;
            cache->c_stats.nentries--;
            newe->ep_refcnt--;
            LOG("entry cache replace remove entry size %lu\n", newe->ep_size);
        }
    }
    /*
     * The old entry could have been "removed" between the add and this replace,
     * The entry is NOT freed, but NOT in the dn hash.
     * which could happen since the entry is not necessarily locked.
     * This is ok.
     */
    olde->ep_state &= ~ENTRY_STATE_UNAVAILABLE;  /* Reset the state */
    olde->ep_state |= ENTRY_STATE_DELETED; /* olde is removed from the cache, so set DELETED here. */
    if (!found) {
        if (olde->ep_state & ENTRY_STATE_DELETED) {
            LOG("entry cache replace (%s): cache index tables out of sync - found dn [%d] id [%d]; but the entry is alreay deleted.\n",
                oldndn, found_in_dn, found_in_id);
        } else {
#ifdef UUIDCACHE_ON
            LOG("entry cache replace: cache index tables out of sync - found dn [%d] id [%d] uuid [%d]\n",
                found_in_dn, found_in_id, found_in_uuid);
#else
            LOG("entry cache replace (%s): cache index tables out of sync - found dn [%d] id [%d]\n",
                oldndn, found_in_dn, found_in_id);
#endif
            cache_unlock(cache);
            return 1;
        }
    }
    /* Lets propagate the weight */
    if (newe->ep_weight == 0) {
        newe->ep_weight = olde->ep_weight;
    }
    if (olde->ep_weight) {
        cache->c_stats.nehw--;
    }
    if (newe->ep_weight) {
        cache->c_stats.nehw++;
    }
    /* now, add the new entry to the hashtables */
    /* (probably don't need such extensive error handling, once this has been
     * tested enough that we believe it works.)
     */
    if (!add_hash(cache->c_dntable, (void *)newndn, strlen(newndn), newe, (void **)&alte)) {
        LOG("entry cache replace (%s): can't add to dn table (returned %s)\n",
            newndn, alte ? slapi_entry_get_dn(alte->ep_entry) : "none");
        cache_unlock(cache);
        return 1;
    }
    if (!add_hash(cache->c_idtable, &(newe->ep_id), sizeof(ID), newe, (void **)&alte)) {
        LOG("entry cache replace (%s): can't add to id table (returned %s)\n",
            newndn, alte ? slapi_entry_get_dn(alte->ep_entry) : "none");
        if (remove_hash(cache->c_dntable, (void *)newndn, strlen(newndn)) == 0) {
            LOG("entry cache replace: failed to remove dn table\n");
        }
        cache_unlock(cache);
        return 1;
    }
#ifdef UUIDCACHE_ON
    if (newuuid && !add_hash(cache->c_uuidtable, (void *)newuuid, strlen(newuuid), newe, NULL)) {
        LOG("entry cache replace: can't add uuid\n", 0, 0, 0);
        if (remove_hash(cache->c_dntable, (void *)newndn, strlen(newndn)) == 0) {
            LOG("entry cache replace: failed to remove dn table(uuid cache)\n");
        }
        if (remove_hash(cache->c_idtable, &(newe->ep_id), sizeof(ID)) == 0) {
            LOG("entry cache replace: failed to remove id table(uuid cache)\n");
        }
        cache_unlock(cache);
        return 1;
    }
#endif
    /* adjust cache meta info */
    newe->ep_refcnt++;
    newe->ep_size = entry_size;
    if (newe->ep_size > olde->ep_size) {
        cache->c_stats.size += newe->ep_size - olde->ep_size;
    } else if (newe->ep_size < olde->ep_size) {
        cache->c_stats.size -= olde->ep_size - newe->ep_size;
    }
    newe->ep_state &= ~ENTRY_STATE_UNAVAILABLE;
    cache_unlock(cache);
    LOG("<= entrycache_replace OK,  cache size now %lu cache count now %ld\n",
        cache->c_stats.size, cache->c_stats.nentries);
    return 0;
}

/* call this when you're done with an entry that was fetched via one of
 * the cache_find_* calls.
 */
void
cache_return(struct cache *cache, void **ptr)
{
    struct backcommon *bep;

    if (NULL == ptr || NULL == *ptr) {
        LOG("=> cache_return\n<= cache_return (null entry)\n");
        return;
    }
    bep = *(struct backcommon **)ptr;
    if (CACHE_TYPE_ENTRY == bep->ep_type) {
        entrycache_return(cache, (struct backentry **)ptr, PR_FALSE);
    } else if (CACHE_TYPE_DN == bep->ep_type) {
        dncache_return(cache, (struct backdn **)ptr);
    }
}

static void
entrycache_return(struct cache *cache, struct backentry **bep, PRBool locked)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;
    struct backentry *e;

    e = *bep;
    if (!e) {
        slapi_log_err(SLAPI_LOG_ERR, "entrycache_return", "Backentry is NULL\n");
        return;
    }
    LOG("entrycache_return - (%s) entry count: %d, entry in cache:%ld\n",
        backentry_get_ndn(e), e->ep_refcnt, cache->c_stats.nentries);

    if (locked == PR_FALSE) {
        cache_lock(cache);
    }
    if (e->ep_state & ENTRY_STATE_NOTINCACHE) {
        backentry_free(bep);
    } else {
        ASSERT(e->ep_refcnt > 0);
        if (!--e->ep_refcnt) {
            if (e->ep_state & (ENTRY_STATE_DELETED | ENTRY_STATE_INVALID)) {
                const char *ndn = slapi_sdn_get_ndn(backentry_get_sdn(e));
                if (ndn) {
                    /*
                     * State is "deleted" and there are no more references,
                     * so we need to remove the entry from the DN cache because
                     * we don't/can't always call cache_remove().
                     */
                    if (remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn)) == 0) {
                        LOG("entrycache_return -Failed to remove %s from dn table\n", ndn);
                    }
                }
                if (e->ep_state & ENTRY_STATE_INVALID) {
                    /* Remove it from the hash table before we free the back entry */
                    slapi_log_err(SLAPI_LOG_CACHE, "entrycache_return",
                            "Finally flushing invalid entry: %d (%s)\n",
                            e->ep_id, backentry_get_ndn(e));
                    entrycache_remove_int(cache, e);
                }
                backentry_free(bep);
            } else {
                pinned_verify(cache, __LINE__);
                if (!pinned_add(cache, e)) {
                    lru_add(cache, e);
                }
                pinned_verify(cache, __LINE__);
                /* the cache might be overfull... */
                if (CACHE_FULL(cache)) {
                    pinned_flush(cache);
                    eflush = entrycache_flush(cache);
                }
                pinned_verify(cache, __LINE__);
            }
        }
    }
    pinned_verify(cache, __LINE__);
    if (locked == PR_FALSE) {
        cache_unlock(cache);
    }
    while (eflush) {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    pinned_verify(cache, __LINE__);
    LOG("entrycache_return - returning.\n");
}


/* lookup entry by DN (assume cache lock is held) */
struct backentry *
cache_find_dn(struct cache *cache, const char *dn, unsigned long ndnlen)
{
    struct backentry *e;

    LOG("=> cache_find_dn - (%s)\n", dn);

    /*entry normalized by caller (dn2entry.c)  */
    cache_lock(cache);
    if (find_hash(cache->c_dntable, (void *)dn, ndnlen, (void **)&e)) {
        /* need to check entry state */
        if ((e->ep_state & ENTRY_STATE_UNAVAILABLE) != 0) {
            /* entry is deleted or not fully created yet */
            cache_unlock(cache);
            LOG("<= cache_find_dn (NOT FOUND)\n");
            return NULL;
        }
        if (e->ep_refcnt == 0 && (e->ep_state & ENTRY_STATE_PINNED) == 0)
            lru_delete(cache, (void *)e);
        e->ep_refcnt++;
        cache->c_stats.hits++;
    }
    cache->c_stats.tries++;
    cache_unlock(cache);

    LOG("<= cache_find_dn - (%sFOUND)\n", e ? "" : "NOT ");
    return e;
}


/* lookup an entry in the cache by its id# (you must return it later) */
struct backentry *
cache_find_id(struct cache *cache, ID id)
{
    struct backentry *e;

    LOG("=> cache_find_id (%lu)\n", (u_long)id);

    cache_lock(cache);
    if (find_hash(cache->c_idtable, &id, sizeof(ID), (void **)&e)) {
        /* need to check entry state */
        if ((e->ep_state & ENTRY_STATE_UNAVAILABLE) != 0) {
            /* entry is deleted or not fully created yet */
            cache_unlock(cache);
            LOG("<= cache_find_id (NOT FOUND)\n");
            return NULL;
        }
        if (e->ep_refcnt == 0 && (e->ep_state & ENTRY_STATE_PINNED) == 0)
            lru_delete(cache, (void *)e);
        e->ep_refcnt++;
        cache->c_stats.hits++;
    }
    cache->c_stats.tries++;
    cache_unlock(cache);

    LOG("<= cache_find_id (%sFOUND)\n", e ? "" : "NOT ");
    return e;
}

#ifdef UUIDCACHE_ON
/* lookup an entry in the cache by it's uuid (you must return it later) */
struct backentry *
cache_find_uuid(struct cache *cache, const char *uuid)
{
    struct backentry *e;

    LOG("=> cache_find_uuid (%s)\n", uuid);

    cache_lock(cache);
    if (find_hash(cache->c_uuidtable, uuid, strlen(uuid), (void **)&e)) {
        /* need to check entry state */
        if ((e->ep_state & ENTRY_STATE_UNAVAILABLE) != 0) {
            /* entry is deleted or not fully created yet */
            cache_unlock(cache);
            LOG("<= cache_find_uuid (NOT FOUND)\n");
            return NULL;
        }
        if (e->ep_refcnt == 0 && (entry->ep_state & ENTRY_STATE_PINNED) == 0)
            lru_delete(cache, (void *)e);
        e->ep_refcnt++;
        cache->c_stats.hits++;
    }
    cache->c_stats.tries++;
    cache_unlock(cache);

    LOG("<= cache_find_uuid (%sFOUND)\n", e ? "" : "NOT ");
    return e;
}
#endif

/* add an entry to the cache */
static int
entrycache_add_int(struct cache *cache, struct backentry *e, int state, struct backentry **alt)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;
    const char *ndn = slapi_sdn_get_ndn(backentry_get_sdn(e));
#ifdef UUIDCACHE_ON
    const char *uuid = slapi_entry_get_uniqueid(e->ep_entry);
#endif
    struct backentry *my_alt;
    size_t entry_size = 0;
    int already_in = 0;
    Slapi_Attr *attr = NULL;

    LOG("=> entrycache_add_int( \"%s\", %ld )\n", backentry_get_ndn(e),
        (long int)e->ep_id);

    if (e->ep_size == 0) {
        /*
         *  This entry has not yet been assigned its size, as it's not in
         *  the cache yet.  Calculate it outside of the cache lock
         */
        entry_size = cache_entry_size(e);
    } else {
        entry_size = e->ep_size;
    }
    LOGPATTERN(cache, backentry_get_ndn(e),
               "Cache average weight is %lu . Adding entry in "
               "cache with size: %lu, weight: %lu, dn:%s\n",
               AV_WEIGHT(cache), entry_size, e->ep_weight,
               backentry_get_ndn(e));
    LOG("=> entrycache_add_int( \"%s\", %ld ) size is %lu weight is %lu\n",
        backentry_get_ndn(e), (long int)e->ep_id,
        entry_size, e->ep_weight);

    /* Check for referrals now so we don't have to do it for every base
     * search in the future */
    if (slapi_entry_attr_find(e->ep_entry, "ref", &attr) && attr) {
        slapi_entry_set_flag(e->ep_entry, SLAPI_ENTRY_FLAG_REFERRAL);
    } else {
        slapi_entry_clear_flag(e->ep_entry, SLAPI_ENTRY_FLAG_REFERRAL);
    }

    cache_lock(cache);
    if (!add_hash(cache->c_dntable, (void *)ndn, strlen(ndn), e,
                  (void **)&my_alt)) {
        LOG("entry \"%s\" already in dn cache\n", ndn);
        /* add_hash filled in 'my_alt' if necessary */
        if (my_alt == e) {
            if ((e->ep_state & ENTRY_STATE_CREATING) && (state == 0)) {
                /* attempting to "add" an entry that's already in the cache,
                 * and the old entry was a placeholder and the new one isn't?
                 * sounds like a confirmation of a previous add!
                 */
                LOG("confirming a previous add\n");
                already_in = 1;
            } else {
                /* the entry already in the cache and either one of these:
                 * 1) ep_state: CREATING && state: CREATING
                 *    ==> keep protecting the entry; increase the refcnt
                 * 2) ep_state: 0 && state: CREATING
                 *    ==> change the state to CREATING (protect it);
                 *        increase the refcnt
                 * 3) ep_state: 0 && state: 0
                 *    ==> increase the refcnt
                 */
                if (e->ep_refcnt == 0)
                    lru_delete(cache, (void *)e);
                e->ep_refcnt++;
                e->ep_state &= ~ENTRY_STATE_UNAVAILABLE;
                e->ep_state |= state; /* might be CREATING */
                /* returning 1 (entry already existed), but don't set to alt
                 * to prevent that the caller accidentally thinks the existing
                 * entry is not the same one the caller has and releases it.
                 */
                cache_unlock(cache);
                return 1;
            }
        } else {
            if (my_alt->ep_state & ENTRY_STATE_CREATING) {
                LOG("the entry %s is reserved (ep_state: 0x%x, state: 0x%x)\n", ndn, e->ep_state, state);
                e->ep_state |= ENTRY_STATE_NOTINCACHE;
                cache_unlock(cache);
                return -1;
            } else if (state != 0) {
                LOG("the entry %s already exists. cannot reserve it. (ep_state: 0x%x, state: 0x%x)\n",
                    ndn, e->ep_state, state);
                e->ep_state |= ENTRY_STATE_NOTINCACHE;
                cache_unlock(cache);
                return -1;
            } else {
                if (alt) {
                    *alt = my_alt;
                    if (e->ep_refcnt == 0 && (e->ep_state & ENTRY_STATE_PINNED) == 0)
                        lru_delete(cache, (void *)*alt);
                    (*alt)->ep_refcnt++;
                    LOG("the entry %s already exists.  returning existing entry %s (state: 0x%x)\n",
                        ndn, backentry_get_ndn(my_alt), state);
                    cache_unlock(cache);
                    return 1;
                } else {
                    LOG("the entry %s already exists.  Not returning existing entry %s (state: 0x%x)\n",
                        ndn, backentry_get_ndn(my_alt), state);
                    cache_unlock(cache);
                    return -1;
                }
            }
        }
    }

    /* creating an entry with ENTRY_STATE_CREATING just creates a stub
     * which is only stored in the dn table (basically, reserving the dn) --
     * doing an add later with state==0 will "confirm" the add
     */
    if (state == 0) {
        /* neither of these should fail, or something is very wrong. */
        if (!add_hash(cache->c_idtable, &(e->ep_id), sizeof(ID), e, NULL)) {
            LOG("entry %s already in id cache!\n", ndn);
            if (already_in) {
                /* there's a bug in the implementatin of 'modify' and 'modrdn'
                 * that i'm working around here.  basically they do a
                 * tentative add of the new (modified) entry, which places
                 * the new entry in the cache, indexed only by dn.
                 *
                 * later they call id2entry_add() on the new entry, which
                 * "adds" the new entry to the cache.  unfortunately, that
                 * add will fail, since the old entry is still in the cache,
                 * and both the old and new entries have the same ID and UUID.
                 *
                 * i catch that here, and just return 0 for success, without
                 * messing with either entry.  a later cache_replace() will
                 * remove the old entry and add the new one, and all will be
                 * fine (i think).
                 */
                LOG("<= entrycache_add_int (ignoring)\n");
                cache_unlock(cache);
                return 0;
            }
            if (remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn)) == 0) {
                LOG("entrycache_add_int: failed to remove %s from dn table\n", ndn);
            }
            e->ep_state |= ENTRY_STATE_NOTINCACHE;
            cache_unlock(cache);
            LOG("entrycache_add_int: failed to add %s to cache (ep_state: %x, already_in: %d)\n",
                ndn, e->ep_state, already_in);
            return -1;
        }
#ifdef UUIDCACHE_ON
        if (uuid) {
            /* (only insert entries with a uuid) */
            if (!add_hash(cache->c_uuidtable, (void *)uuid, strlen(uuid), e,
                          NULL)) {
                LOG("entry %s already in uuid cache!\n", backentry_get_ndn(e),
                    0, 0);
                if (remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn)) == 0) {
                    LOG("entrycache_add_int: failed to remove dn table(uuid cache)\n");
                }
                if (remove_hash(cache->c_idtable, &(e->ep_id), sizeof(ID)) == 0) {
                    LOG("entrycache_add_int: failed to remove id table(uuid cache)\n";
                }
                e->ep_state |= ENTRY_STATE_NOTINCACHE;
                cache_unlock(cache);
                return -1;
            }
        }
#endif
    }

    e->ep_state &= ~ENTRY_STATE_UNAVAILABLE;
    e->ep_state |= state;

    if (!already_in) {
        e->ep_refcnt = 1;
        e->ep_size = entry_size;
        cache->c_stats.size += e->ep_size;
        cache->c_stats.nentries++;
        cache->c_stats.weight += e->ep_weight;
        if (e->ep_weight) {
            cache->c_stats.nehw++;
        }
        /* don't add to lru since refcnt = 1 */
        LOG("added entry of size %lu -> total now %lu out of max %lu "
            ". Entry weight is %lu -> Average weight is %lu\n",
            e->ep_size, cache->c_stats.size, cache->c_stats.maxsize,
            e->ep_weight, AV_WEIGHT(cache));
        if (cache->c_stats.maxentries > 0) {
            LOG("    total entries %ld out of %ld\n",
                cache->c_stats.nentries, cache->c_stats.maxentries);
        }
        /* check for full cache, and clear out if necessary */
        if (CACHE_FULL(cache)) {
            pinned_flush(cache);
            eflush = entrycache_flush(cache);
        }
    }
    cache_unlock(cache);

    while (eflush) {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    LOG("<= entrycache_add_int OK\n");
    return 0;
}

/* create an entry in the cache, and increase its refcount (you must
 * return it when you're done).
 * returns:  0       entry has been created & locked
 *           1       entry already existed
 *          -1       something bad happened
 *
 * if 'alt' is not NULL, and the entry is found to already exist in the
 * cache, a refcounted pointer to that entry will be placed in 'alt'.
 * (this means code which suffered from race conditions between multiple
 * entry modifiers can now work.)
 */
int
cache_add(struct cache *cache, void *ptr, void **alt)
{
    struct backcommon *e;
    if (NULL == ptr) {
        LOG("=> cache_add\n<= cache_add (null entry)\n");
        return 0;
    }
    e = (struct backcommon *)ptr;
    if (CACHE_TYPE_ENTRY == e->ep_type) {
        return entrycache_add_int(cache, (struct backentry *)e,
                                  0, (struct backentry **)alt);
    } else if (CACHE_TYPE_DN == e->ep_type) {
        return dncache_add_int(cache, (struct backdn *)e,
                               0, (struct backdn **)alt);
    }
    return 0;
}

/* same as above, but add it tentatively: nobody else can use this entry
 * from the cache until you later call cache_add.
 */
int
cache_add_tentative(struct cache *cache, struct backentry *e, struct backentry **alt)
{
    return entrycache_add_int(cache, e, ENTRY_STATE_CREATING, alt);
}

void
cache_lock(struct cache *cache)
{
    PR_EnterMonitor(cache->c_mutex);
}

void
cache_unlock(struct cache *cache)
{
    PR_ExitMonitor(cache->c_mutex);
}

/* locks an entry so that it can be modified (you should have gotten the
 * entry via cache_find_*).
 * returns 0 on success,
 * returns 1 if the entry lock could not be created
 * returns 2 (RETRY_CACHE_LOCK) if the entry is scheduled for deletion.
 */
int
cache_lock_entry(struct cache *cache, struct backentry *e)
{
    LOG("=> cache_lock_entry (%s)\n", backentry_get_ndn(e));

    if (!e->ep_mutexp) {
        /* make sure only one thread does this */
        PR_Lock(cache->c_emutexalloc_mutex);
        if (!e->ep_mutexp) {
            e->ep_mutexp = PR_NewMonitor();
            if (!e->ep_mutexp) {
                PR_Unlock(cache->c_emutexalloc_mutex);
                LOG("<= cache_lock_entry (DELETED)\n");
                slapi_log_err(SLAPI_LOG_ERR,
                              "cache_lock_entry", "Failed to create a lock for %s\n",
                              backentry_get_ndn(e));
                LOG("<= cache_lock_entry (FAILED)\n");
                return 1;
            }
        }
        PR_Unlock(cache->c_emutexalloc_mutex);
    }

    /* wait on entry lock (done w/o holding the cache lock) */
    PR_EnterMonitor(e->ep_mutexp);

    /* make sure entry hasn't been deleted now */
    cache_lock(cache);
    if (e->ep_state & (ENTRY_STATE_DELETED | ENTRY_STATE_NOTINCACHE | ENTRY_STATE_INVALID)) {
        cache_unlock(cache);
        PR_ExitMonitor(e->ep_mutexp);
        LOG("<= cache_lock_entry (DELETED)\n");
        return RETRY_CACHE_LOCK;
    }
    cache_unlock(cache);

    LOG("<= cache_lock_entry (FOUND)\n");
    return 0;
}

int
cache_is_reverted_entry(struct cache *cache, struct backentry *e)
{
    struct backentry *dummy_e;

    cache_lock(cache);
    if (find_hash(cache->c_idtable, &e->ep_id, sizeof(ID), (void **)&dummy_e)) {
        if (dummy_e->ep_state & ENTRY_STATE_INVALID) {
            slapi_log_err(SLAPI_LOG_WARNING, "cache_is_reverted_entry", "Entry reverted = %d (0x%lX)  [entry: %p] refcnt=%d\n",
                          dummy_e->ep_state,
                          pthread_self(),
                          dummy_e, dummy_e->ep_refcnt);
            cache_unlock(cache);
            return 1;
        }
    }
    cache_unlock(cache);
    return 0;
}
/* the opposite of above */
void
cache_unlock_entry(struct cache *cache __attribute__((unused)), struct backentry *e)
{
    LOG("=> cache_unlock_entry\n");
    if (PR_ExitMonitor(e->ep_mutexp)) {
        LOG("=> cache_unlock_entry - monitor was not entered!!!\n");
    }
}

/* DN cache */
/* remove everything from the cache */
static void
dncache_clear_int(struct cache *cache)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;
    size_t size = cache->c_stats.maxsize;

    cache->c_stats.maxsize = 0;
    dnflush = dncache_flush(cache);
    while (dnflush) {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
    cache->c_stats.maxsize = size;
    if (cache->c_stats.nentries > 0) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "dncache_clear_int", "There are still %" PRIu64 " dn's "
                                           "in the dn cache. :/\n",
                      cache->c_stats.nentries);
    }
}

static int
dn_same_id(const void *bdn, const void *k)
{
    return (((struct backdn *)bdn)->ep_id == *(ID *)k);
}

static void
dncache_set_max_size(struct cache *cache, uint64_t bytes)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;

    if (bytes < MINCACHESIZE) {
        bytes = MINCACHESIZE;
        slapi_log_err(SLAPI_LOG_WARNING,
                      "dncache_set_max_size", "Minimum cache size is %" PRIu64 " -- rounding up\n",
                      MINCACHESIZE);
    }
    cache_lock(cache);
    cache->c_stats.maxsize = bytes;
    LOG("entry cache size set to %" PRIu64 "\n", bytes);
    /* check for full cache, and clear out if necessary */
    if (CACHE_FULL(cache)) {
        dnflush = dncache_flush(cache);
    }
    while (dnflush) {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
    if (cache->c_stats.nentries < 50) {
        /* there's hardly anything left in the cache -- clear it out and
        * resize the hashtables for efficiency.
        */
        erase_cache(cache, CACHE_TYPE_DN);
        cache_make_hashes(cache, CACHE_TYPE_DN);
    }
    cache_unlock(cache);
    /* This may already have been called by one of the functions in
     * ldbm_instance_config
     */

    slapi_pal_meminfo *mi = spal_meminfo_get();
    if (util_is_cachesize_sane(mi, &bytes) != UTIL_CACHESIZE_VALID) {
        slapi_log_err(SLAPI_LOG_WARNING, "dncache_set_max_size", "Cachesize (%" PRIu64 ") may use more than the available physical memory.\n", bytes);
    }
    spal_meminfo_destroy(mi);
}

/* remove a dn from the cache */
/* you must be holding c_mutex !! */
static int
dncache_remove_int(struct cache *cache, struct backdn *bdn)
{
    int ret = 1; /* assume not in cache */

    LOG("=> dncache_remove_int (%s)\n", slapi_sdn_get_dn(bdn->dn_sdn));
    if (bdn->ep_state & ENTRY_STATE_NOTINCACHE) {
        return ret;
    }

    /* remove from id hashtable */
    if (remove_hash(cache->c_idtable, &(bdn->ep_id), sizeof(ID))) {
        ret = 0;
    } else {
        LOG("remove %d from id hash failed\n", bdn->ep_id);
    }
    if (ret == 0) {
        /* won't be on the LRU list since it has a refcount on it */
        /* adjust cache size */
        cache->c_stats.size -= bdn->ep_size;
        cache->c_stats.nentries--;
        LOG("<= dncache_remove_int (size %lu): cache now %lu dn's, %lu bytes\n",
            bdn->ep_size, cache->c_stats.nentries,
            cache->c_stats.size);
    }

    /* mark for deletion (will be erased when refcount drops to zero) */
    bdn->ep_state |= ENTRY_STATE_DELETED;
    LOG("<= dncache_remove_int: %d\n", ret);
    return ret;
}

static void
dncache_return(struct cache *cache, struct backdn **bdn)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;

    LOG("=> dncache_return (%s) reference count: %d, dn in cache:%ld\n",
        slapi_sdn_get_dn((*bdn)->dn_sdn), (*bdn)->ep_refcnt, cache->c_stats.nentries);

    cache_lock(cache);
    if ((*bdn)->ep_state & ENTRY_STATE_NOTINCACHE) {
        backdn_free(bdn);
    } else {
        ASSERT((*bdn)->ep_refcnt > 0);
        if (!--(*bdn)->ep_refcnt) {
            if ((*bdn)->ep_state & (ENTRY_STATE_DELETED | ENTRY_STATE_INVALID)) {
                if ((*bdn)->ep_state & ENTRY_STATE_INVALID) {
                    /* Remove it from the hash table before we free the back dn */
                    slapi_log_err(SLAPI_LOG_CACHE, "dncache_return",
                            "Finally flushing invalid entry: %d (%s)\n",
                            (*bdn)->ep_id, slapi_sdn_get_dn((*bdn)->dn_sdn));
                    dncache_remove_int(cache, (*bdn));
                }
                backdn_free(bdn);
            } else {
                lru_add(cache, (void *)*bdn);
                /* the cache might be overfull... */
                if (CACHE_FULL(cache)) {
                    dnflush = dncache_flush(cache);
                }
            }
        }
    }
    cache_unlock(cache);
    while (dnflush) {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
}

/* lookup a dn in the cache by its id# (you must return it later) */
struct backdn *
dncache_find_id(struct cache *cache, ID id)
{
    struct backdn *bdn = NULL;

    LOG("=> dncache_find_id (%lu)\n", (u_long)id);

    cache_lock(cache);
    if (find_hash(cache->c_idtable, &id, sizeof(ID), (void **)&bdn)) {
        /* need to check entry state */
        if (bdn->ep_state != 0) {
            /* entry is deleted or not fully created yet */
            cache_unlock(cache);
            LOG("<= dncache_find_id (NOT FOUND)\n");
            return NULL;
        }
        if (bdn->ep_refcnt == 0)
            lru_delete(cache, (void *)bdn);
        bdn->ep_refcnt++;
        cache->c_stats.hits++;
    }
    cache->c_stats.tries++;
    cache_unlock(cache);

    LOG("<= cache_find_id (%sFOUND)\n", bdn ? "" : "NOT ");
    return bdn;
}

/* add a dn to the cache */
static int
dncache_add_int(struct cache *cache, struct backdn *bdn, int state, struct backdn **alt)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;
    struct backdn *my_alt;
    int already_in = 0;

    LOG("=> dncache_add_int( \"%s\", %ld )\n", slapi_sdn_get_dn(bdn->dn_sdn),
        (long int)bdn->ep_id);

    cache_lock(cache);

    if (!add_hash(cache->c_idtable, &(bdn->ep_id), sizeof(ID), bdn,
                  (void **)&my_alt)) {
        LOG("entry %s already in id cache!\n", slapi_sdn_get_dn(bdn->dn_sdn));
        if (my_alt == bdn) {
            if ((bdn->ep_state & ENTRY_STATE_CREATING) && (state == 0)) {
                /* attempting to "add" a dn that's already in the cache,
                 * and the old entry was a placeholder and the new one isn't?
                 * sounds like a confirmation of a previous add!
                 */
                LOG("confirming a previous add\n");
                already_in = 1;
            } else {
                /* the entry already in the cache and either one of these:
                 * 1) ep_state: CREATING && state: CREATING
                 *    ==> keep protecting the entry; increase the refcnt
                 * 2) ep_state: 0 && state: CREATING
                 *    ==> change the state to CREATING (protect it);
                 *        increase the refcnt
                 * 3) ep_state: 0 && state: 0
                 *    ==> increase the refcnt
                 */
                if (bdn->ep_refcnt == 0)
                    lru_delete(cache, (void *)bdn);
                bdn->ep_refcnt++;
                bdn->ep_state = state; /* might be CREATING */
                /* returning 1 (entry already existed), but don't set to alt
                 * to prevent that the caller accidentally thinks the existing
                 * entry is not the same one the caller has and releases it.
                 */
                cache_unlock(cache);
                return 1;
            }
        } else {
            if (my_alt->ep_state & ENTRY_STATE_CREATING) {
                LOG("the entry is reserved\n");
                bdn->ep_state |= ENTRY_STATE_NOTINCACHE;
                cache_unlock(cache);
                return -1;
            } else if (state != 0) {
                LOG("the entry already exists. cannot reserve it.\n");
                bdn->ep_state |= ENTRY_STATE_NOTINCACHE;
                cache_unlock(cache);
                return -1;
            } else {
                if (alt) {
                    *alt = my_alt;
                    if ((*alt)->ep_refcnt == 0)
                        lru_delete(cache, (void *)*alt);
                    (*alt)->ep_refcnt++;
                }
                cache_unlock(cache);
                return 1;
            }
        }
    }

    bdn->ep_state = state;

    if (!already_in) {
        bdn->ep_refcnt = 1;
        if (0 == bdn->ep_size) {
            bdn->ep_size = slapi_sdn_get_size(bdn->dn_sdn);
        }

        cache->c_stats.size += bdn->ep_size;
        cache->c_stats.nentries++;
        /* don't add to lru since refcnt = 1 */
        LOG("added entry of size %lu -> total now %lu out of max %lu\n",
            bdn->ep_size, cache->c_stats.size,
            cache->c_stats.maxsize);
        if (cache->c_stats.maxentries > 0) {
            LOG("    total entries %ld out of %ld\n",
                cache->c_stats.nentries, cache->c_stats.maxentries);
        }
        /* check for full cache, and clear out if necessary */
        if (CACHE_FULL(cache)) {
            dnflush = dncache_flush(cache);
        }
    }
    cache_unlock(cache);

    while (dnflush) {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
    LOG("<= dncache_add_int OK\n");
    return 0;
}

static int
dncache_replace(struct cache *cache, struct backdn *olddn, struct backdn *newdn)
{
    int found;

    LOG("(%s) -> (%s)\n",
        slapi_sdn_get_dn(olddn->dn_sdn), slapi_sdn_get_dn(newdn->dn_sdn));

    /* remove from all hashtable -- this function may be called from places
     * where the entry isn't in all the table yet, so we don't care if any
     * of these return errors.
     */
    cache_lock(cache);

    /*
     * First, remove the old entry from the hashtable.
     * If the old entry is in cache but not in at least one of the
     * cache tables, operation error
     */
    if ((olddn->ep_state & ENTRY_STATE_NOTINCACHE) == 0) {

        found = remove_hash(cache->c_idtable, &(olddn->ep_id), sizeof(ID));
        if (!found) {
            LOG("cache index tables out of sync\n");
            cache_unlock(cache);
            return 1;
        }
    }

    /* now, add the new entry to the hashtables */
    /* (probably don't need such extensive error handling, once this has been
     * tested enough that we believe it works.)
     */
    if (!add_hash(cache->c_idtable, &(newdn->ep_id), sizeof(ID), newdn, NULL)) {
        LOG("dn cache replace: can't add id\n");
        cache_unlock(cache);
        return 1;
    }
    /* adjust cache meta info */
    newdn->ep_refcnt = 1;
    if (0 == newdn->ep_size) {
        newdn->ep_size = slapi_sdn_get_size(newdn->dn_sdn);
    }
    if (newdn->ep_size > olddn->ep_size) {
        cache->c_stats.size += newdn->ep_size - olddn->ep_size;
    } else if (newdn->ep_size < olddn->ep_size) {
        cache->c_stats.size -= olddn->ep_size - newdn->ep_size;
    }
    olddn->ep_state = ENTRY_STATE_DELETED;
    newdn->ep_state = 0;
    cache_unlock(cache);
    LOG("<-- OK,  cache size now %lu cache count now %ld\n",
        cache->c_stats.size, cache->c_stats.nentries);
    return 0;
}

static struct backdn *
dncache_flush(struct cache *cache)
{
    struct backdn *dn = NULL;

    LOG("->\n");

    /* all entries on the LRU list are guaranteed to have a refcnt = 0
     * (iow, nobody's using them), so just delete from the tail down
     * until the cache is a managable size again.
     * (cache->c_mutex is locked when we enter this)
     */
    while ((cache->c_lrutail != NULL) && CACHE_FULL(cache)) {
        if (dn == NULL) {
            dn = CACHE_LRU_TAIL(cache, struct backdn *);
        } else {
            dn = BACK_LRU_PREV(dn, struct backdn *);
        }
        ASSERT(dn->ep_refcnt == 0);
        dn->ep_refcnt++;
        if (dncache_remove_int(cache, dn) < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "dncache_flush", "Unable to delete entry\n");
            break;
        }
        if (dn == CACHE_LRU_HEAD(cache, struct backdn *)) {
            break;
        }
    }
    if (dn)
        LRU_DETACH(cache, dn);
    LOG("(down to %lu dns, %lu bytes)\n", cache->c_stats.nentries,
        cache->c_stats.size);
    return dn;
}

#ifdef LDAP_CACHE_DEBUG_LRU
/* for debugging -- painstakingly verify the lru list is ok -- if 'in' is
 * true, then dn 'dn' should be in the list right now; otherwise, it
 * should NOT be in the list.
 */
static void
dn_lru_verify(struct cache *cache, struct backdn *dn, int in)
{
    int is_in = 0;
    int count = 0;
    struct backdn *dnp;

    dnp = CACHE_LRU_HEAD(cache, struct backdn *);
    while (dnp) {
        count++;
        if (dnp == dn) {
            is_in = 1;
        }
        if (dnp->ep_lruprev) {
            ASSERT(BACK_LRU_NEXT(BACK_LRU_PREV(dnp, struct backdn *), struct backdn *) == dnp);
        } else {
            ASSERT(dnp == CACHE_LRU_HEAD(cache, struct backdn *));
        }
        if (dnp->ep_lrunext) {
            ASSERT(BACK_LRU_PREV(BACK_LRU_NEXT(dnp, struct backdn *), struct backdn *) == dnp);
        } else {
            ASSERT(dnp == CACHE_LRU_TAIL(cache, struct backdn *));
        }

        dnp = BACK_LRU_NEXT(dnp, struct backdn *);
    }
    ASSERT(is_in == in);
}
#endif

#ifdef CACHE_DEBUG
void
check_entry_cache(struct cache *cache, struct backentry *e)
{
    Slapi_DN *sdn = slapi_entry_get_sdn(e->ep_entry);
    struct backentry *debug_e = cache_find_dn(cache,
                                              slapi_sdn_get_dn(sdn),
                                              slapi_sdn_get_ndn_len(sdn));
    int in_cache = cache_is_in_cache(cache, (void *)e);
    if (in_cache) {
        if (debug_e) { /* e is in cache */
            CACHE_RETURN(cache, &debug_e);
            if ((e != debug_e) && !(e->ep_state & ENTRY_STATE_DELETED)) {
                slapi_log_err(SLAPI_LOG_DEBUG, "check_entry_cache",
                              "entry 0x%p is not in dn cache but 0x%p having the same dn %s is "
                              "although in_cache flag is set!!!\n",
                              e, debug_e, slapi_sdn_get_dn(sdn));
            }
        } else if (!(e->ep_state & ENTRY_STATE_DELETED)) {
            slapi_log_err(SLAPI_LOG_DEBUG, "check_entry_cache",
                          "%s (id %d) is not in dn cache although in_cache flag is set!!!\n",
                          slapi_sdn_get_dn(sdn), e->ep_id);
        }
        debug_e = cache_find_id(cache, e->ep_id);
        if (debug_e) { /* e is in cache */
            CACHE_RETURN(cache, &debug_e);
            if ((e != debug_e) && !(e->ep_state & ENTRY_STATE_DELETED)) {
                slapi_log_err(SLAPI_LOG_DEBUG, "check_entry_cache",
                              "entry 0x%p is not in id cache but 0x%p having the same id %d is "
                              "although in_cache flag is set!!!\n",
                              e, debug_e, e->ep_id);
            }
        } else {
            slapi_log_err(SLAPI_LOG_CACHE, "check_entry_cache",
                          "%s (id %d) is not in id cache although in_cache flag is set!!!\n",
                          slapi_sdn_get_dn(sdn), e->ep_id);
        }
    } else {
        if (debug_e) { /* e is in cache */
            CACHE_RETURN(cache, &debug_e);
            if (e == debug_e) {
                slapi_log_err(SLAPI_LOG_DEBUG, "check_entry_cache",
                              "%s (id %d) is in dn cache although in_cache flag is not set!!!\n",
                              slapi_sdn_get_dn(sdn), e->ep_id);
            }
        }
        debug_e = cache_find_id(cache, e->ep_id);
        if (debug_e) { /* e is in cache: bad */
            CACHE_RETURN(cache, &debug_e);
            if (e == debug_e) {
                slapi_log_err(SLAPI_LOG_CACHE, "check_entry_cache",
                              "%s (id %d) is in id cache although in_cache flag is not set!!!\n",
                              slapi_sdn_get_dn(sdn), e->ep_id);
            }
        }
    }
}
#endif

int
cache_has_otherref(struct cache *cache, void *ptr)
{
    struct backcommon *bep;
    int hasref = 0;

    if (NULL == ptr) {
        return hasref;
    }
    bep = (struct backcommon *)ptr;
    cache_lock(cache);
    hasref = bep->ep_refcnt;
    cache_unlock(cache);
    return (hasref > 1) ? 1 : 0;
}

static int
cache_is_in_cache_nolock(void *ptr)
{
    struct backcommon *bep;
    int in_cache = 0;

    if (NULL == ptr) {
        return in_cache;
    }
    bep = (struct backcommon *)ptr;
    in_cache = (bep->ep_state & (ENTRY_STATE_DELETED | ENTRY_STATE_NOTINCACHE)) ? 0 : 1;
    return in_cache;
}

int
cache_is_in_cache(struct cache *cache, void *ptr)
{
    int ret;
    cache_lock(cache);
    ret = cache_is_in_cache_nolock(ptr);
    cache_unlock(cache);
    return ret;
}
