/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* cache.c - routines to maintain an in-core cache of entries */

#include "back-ldbm.h"

#ifdef DEBUG
#define LDAP_CACHE_DEBUG
/* #define LDAP_CACHE_DEBUG_LRU * causes slowdown */
#endif

/* cache can't get any smaller than this (in bytes) */
#define MINCACHESIZE       (size_t)512000

/* don't let hash be smaller than this # of slots */
#define MINHASHSIZE       1024

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
#define ASSERT(_x) do { \
    if (!(_x)) { \
       LDAPDebug(LDAP_DEBUG_ANY, "BAD CACHE ASSERTION at %s/%d: %s\n", \
                __FILE__, __LINE__, #_x); \
       *(char *)0L = 23; \
    } \
} while (0)
#define LOG(_a, _x1, _x2, _x3)  LDAPDebug(LDAP_DEBUG_CACHE, _a, _x1, _x2, _x3)
#else
#define ASSERT(_x) ;
#define LOG(_a, _x1, _x2, _x3)  ;
#endif

#define LRU_DETACH(cache, e) lru_detach((cache), (void *)(e))

#define CACHE_LRU_HEAD(cache, type) ((type)((cache)->c_lruhead))
#define CACHE_LRU_TAIL(cache, type) ((type)((cache)->c_lrutail))

#define BACK_LRU_NEXT(entry, type) ((type)((entry)->ep_lrunext))
#define BACK_LRU_PREV(entry, type) ((type)((entry)->ep_lruprev))

/* static functions */
static void entrycache_clear_int(struct cache *cache);
static void entrycache_set_max_size(struct cache *cache, size_t bytes);
static int entrycache_remove_int(struct cache *cache, struct backentry *e);
static void entrycache_return(struct cache *cache, struct backentry **bep);
static int entrycache_replace(struct cache *cache, struct backentry *olde, struct backentry *newe);
static int entrycache_add_int(struct cache *cache, struct backentry *e, int state, struct backentry **alt);
static struct backentry *entrycache_flush(struct cache *cache);
#ifdef LDAP_CACHE_DEBUG_LRU
static void entry_lru_verify(struct cache *cache, struct backentry *e, int in);
#endif

static int dn_same_id(const void *bdn, const void *k);
static void dncache_clear_int(struct cache *cache);
static void dncache_set_max_size(struct cache *cache, size_t bytes);
static int dncache_remove_int(struct cache *cache, struct backdn *dn);
static void dncache_return(struct cache *cache, struct backdn **bdn);
static int dncache_replace(struct cache *cache, struct backdn *olddn, struct backdn *newdn);
static int dncache_add_int(struct cache *cache, struct backdn *bdn, int state, struct backdn **alt);
static struct backdn *dncache_flush(struct cache *cache);
#ifdef LDAP_CACHE_DEBUG_LRU
static void dn_lru_verify(struct cache *cache, struct backdn *dn, int in);
#endif


/***** tiny hashtable implementation *****/

#define HASH_VALUE(_key, _keylen) \
    ((ht->hashfn == NULL) ? (*(unsigned int *)(_key)) : \
     ((*ht->hashfn)(_key, _keylen)))
#define HASH_NEXT(ht, entry) (*(void **)((char *)(entry) + (ht)->offset))

static int entry_same_id(const void *e, const void *k)
{
    return (((struct backentry *)e)->ep_id == *(ID *)k);
}

static unsigned long dn_hash(const void *key, size_t keylen)
{
    unsigned char *x = (unsigned char *)key;
    ssize_t i;
    unsigned long val = 0;

    for (i = keylen-1; i >= 0; i--)
       val += ((val << 5) + (*x++)) & 0xffffffff;
    return val;
}

#ifdef UUIDCACHE_ON 
static unsigned long uuid_hash(const void *key, size_t keylen)
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

static int entry_same_uuid(const void *e, const void *k)
{
    struct backentry *be = (struct backentry *)e;
    const char *uuid = slapi_entry_get_uniqueid(be->ep_entry);

    return (strcmp(uuid, (char *)k) == 0);
}
#endif

static int entry_same_dn(const void *e, const void *k)
{
    struct backentry *be = (struct backentry *)e;
    const char *ndn = slapi_sdn_get_ndn(backentry_get_sdn(be));

    return (strcmp(ndn, (char *)k) == 0);
}

Hashtable *new_hash(u_long size, u_long offset, HashFn hfn,
                        HashTestFn tfn)
{
    static u_long prime[] = { 3, 5, 7, 11, 13, 17, 19 };
    Hashtable *ht;
    int ok = 0, i;

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

    ht = (Hashtable*)slapi_ch_calloc(1, sizeof(Hashtable) + size*sizeof(void *));
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
int add_hash(Hashtable *ht, void *key, size_t keylen, void *entry,
                  void **alt)
{
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
    HASH_NEXT(ht, entry) = ht->slot[slot];
    ht->slot[slot] = entry;
    return 1;
}

/* returns 1 if the item was found, and puts a ptr to it in 'entry' */
int find_hash(Hashtable *ht, const void *key, size_t keylen, void **entry)
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
int remove_hash(Hashtable *ht, const void *key, size_t keylen)
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
            PR_snprintf(ep_id, 16, "%u", ((struct backcommon *)e)->ep_id);
            len = strlen(ep_id);
            if (ids_size < len + 1) {
                LDAPDebug1Arg(LDAP_DEBUG_ANY, "%s\n", ep_ids);
                p = ep_ids; ids_size = 80;
            }
            PR_snprintf(p, ids_size, "%s:", ep_id);
            p += len + 1; ids_size -= len + 1;
        } while (e = HASH_NEXT(ht, e));
    }
    if (p != ep_ids) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, "%s\n", ep_ids);
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
static void hash_stats(Hashtable *ht, u_long *slots, int *total_entries,
                     int *max_entries_per_slot, int **slot_stats)
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
lru_verify(struct cache *cache, void *ptr, int in)
{
    struct backcommon *e;
    if (NULL == ptr)
    {
        LOG("=> lru_verify\n<= lru_verify (null entry)\n", 0, 0, 0);
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
        count++;
        if (ep == e) {
           is_in = 1;
        }
        if (ep->ep_lruprev) {
           ASSERT(BACK_LRU_NEXT(BACK_LRU_PREV(ep, struct backentry *), struct backentry *)== ep);
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
#endif

/* assume lock is held */
static void lru_detach(struct cache *cache, void *ptr)
{
    struct backcommon *e;
    if (NULL == ptr)
    {
        LOG("=> lru_detach\n<= lru_detach (null entry)\n", 0, 0, 0);
        return;
    }
    e = (struct backcommon *)ptr;
#ifdef LDAP_CACHE_DEBUG_LRU
    lru_verify(cache, e, 1);
#endif
    if (e->ep_lruprev)
    {
       e->ep_lruprev->ep_lrunext = NULL;
       cache->c_lrutail = e->ep_lruprev;
    }
    else
    {
       cache->c_lruhead = NULL;
       cache->c_lrutail = NULL;
    }
#ifdef LDAP_CACHE_DEBUG_LRU
    lru_verify(cache, e, 0);
#endif
}

/* assume lock is held */
static void lru_delete(struct cache *cache, void *ptr)
{
    struct backcommon *e;
    if (NULL == ptr)
    {
        LOG("=> lru_delete\n<= lru_delete (null entry)\n", 0, 0, 0);
        return;
    }
    e = (struct backcommon *)ptr;
#ifdef LDAP_CACHE_DEBUG_LRU
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
static void lru_add(struct cache *cache, void *ptr)
{
    struct backcommon *e;
    if (NULL == ptr)
    {
        LOG("=> lru_add\n<= lru_add (null entry)\n", 0, 0, 0);
        return;
    }
    e = (struct backcommon *)ptr;
#ifdef LDAP_CACHE_DEBUG_LRU
    lru_verify(cache, e, 0);
#endif
    e->ep_lruprev = NULL;
    e->ep_lrunext = cache->c_lruhead;
    cache->c_lruhead = e;
    if (e->ep_lrunext)
       e->ep_lrunext->ep_lruprev = e;
    if (! cache->c_lrutail)
       cache->c_lrutail = e;
#ifdef LDAP_CACHE_DEBUG_LRU
    lru_verify(cache, e, 1);
#endif
}


/***** cache overhead *****/

static void cache_make_hashes(struct cache *cache, int type)
{
    u_long hashsize = (cache->c_maxentries > 0) ? cache->c_maxentries :
                     (cache->c_maxsize/512); 

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

/* initialize the cache */
int cache_init(struct cache *cache, size_t maxsize, long maxentries, int type)
{
    LDAPDebug(LDAP_DEBUG_TRACE, "=> cache_init\n", 0, 0, 0);
    cache->c_maxsize = maxsize;
    cache->c_maxentries = maxentries;
    cache->c_cursize = slapi_counter_new();
    cache->c_curentries = 0;
    if (config_get_slapi_counters()) {
        cache->c_hits = slapi_counter_new();
        cache->c_tries = slapi_counter_new();
    } else {
        cache->c_hits = NULL;
        cache->c_tries = NULL;
    }
    cache->c_lruhead = cache->c_lrutail = NULL;
    cache_make_hashes(cache, type);

    if (((cache->c_mutex = PR_NewLock()) == NULL) ||
       ((cache->c_emutexalloc_mutex = PR_NewLock()) == NULL)) {
       LDAPDebug(LDAP_DEBUG_ANY, "ldbm: cache_init: PR_NewLock failed\n",
                0, 0, 0);
       return 0;
    }
    LDAPDebug(LDAP_DEBUG_TRACE, "<= cache_init\n", 0, 0, 0);
    return 1;
}

#define  CACHE_FULL(cache) \
       ((slapi_counter_get_value((cache)->c_cursize) > (cache)->c_maxsize) || \
        (((cache)->c_maxentries > 0) && \
         ((cache)->c_curentries > (cache)->c_maxentries)))


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

    LOG("=> entrycache_flush\n", 0, 0, 0);

    /* all entries on the LRU list are guaranteed to have a refcnt = 0
     * (iow, nobody's using them), so just delete from the tail down
     * until the cache is a managable size again.
     * (cache->c_mutex is locked when we enter this)
     */
    while ((cache->c_lrutail != NULL) && CACHE_FULL(cache)) {
        if (e == NULL)
        {
            e = CACHE_LRU_TAIL(cache, struct backentry *);
        }
        else
        {
            e = BACK_LRU_PREV(e, struct backentry *);
        }
        ASSERT(e->ep_refcnt == 0);
        e->ep_refcnt++;
        if (entrycache_remove_int(cache, e) < 0) {
           LDAPDebug(LDAP_DEBUG_ANY,
                     "entry cache flush: unable to delete entry\n", 0, 0, 0);
           break;
        }
        if(e == CACHE_LRU_HEAD(cache, struct backentry *)) {
            break;
        }
    }
    if (e)
        LRU_DETACH(cache, e);
    LOG("<= entrycache_flush (down to %lu entries, %lu bytes)\n",
            cache->c_curentries, slapi_counter_get_value(cache->c_cursize), 0);
    return e;
}

/* remove everything from the cache */
static void entrycache_clear_int(struct cache *cache)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;
    size_t size = cache->c_maxsize;

    cache->c_maxsize = 0;
    eflush = entrycache_flush(cache);
    while (eflush)
    {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    cache->c_maxsize = size;
    if (cache->c_curentries > 0) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                     "entrycache_clear_int: there are still %ld entries "
                     "in the entry cache.\n", cache->c_curentries);
#ifdef LDAP_CACHE_DEBUG
        LDAPDebug0Args(LDAP_DEBUG_ANY, "ID(s) in entry cache:\n");
        dump_hash(cache->c_idtable);
#endif
    }
}

void cache_clear(struct cache *cache, int type)
{
    PR_Lock(cache->c_mutex);
    if (CACHE_TYPE_ENTRY == type) {
        entrycache_clear_int(cache);
    } else if (CACHE_TYPE_DN == type) {
        dncache_clear_int(cache);
    }
    PR_Unlock(cache->c_mutex);
}

static void erase_cache(struct cache *cache, int type)
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
void cache_destroy_please(struct cache *cache, int type)
{
    erase_cache(cache, type);
    PR_DestroyLock(cache->c_mutex);
    PR_DestroyLock(cache->c_emutexalloc_mutex);
}

void cache_set_max_size(struct cache *cache, size_t bytes, int type)
{
    if (CACHE_TYPE_ENTRY == type) {
        entrycache_set_max_size(cache, bytes);
    } else if (CACHE_TYPE_DN == type) {
        dncache_set_max_size(cache, bytes);
    }
}

static void entrycache_set_max_size(struct cache *cache, size_t bytes)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;

    if (bytes < MINCACHESIZE) {
       LDAPDebug2Args(LDAP_DEBUG_ANY,
                      "WARNING -- Max entry cache size %lu is set; "
                      "Minimum entry cache size is %lu -- rounding up\n",
                      bytes, MINCACHESIZE);
       bytes = MINCACHESIZE;
    } else {
       LDAPDebug1Arg(LDAP_DEBUG_BACKLDBM,
                     "Max entry cache size is set to %lu\n", bytes);
    }
    PR_Lock(cache->c_mutex);
    cache->c_maxsize = bytes;
    LOG("entry cache size set to %lu\n", bytes, 0, 0);
    /* check for full cache, and clear out if necessary */
    if (CACHE_FULL(cache))
       eflush = entrycache_flush(cache);
    while (eflush)
    {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    if (cache->c_curentries < 50) {
       /* there's hardly anything left in the cache -- clear it out and
        * resize the hashtables for efficiency.
        */
       erase_cache(cache, CACHE_TYPE_ENTRY);
       cache_make_hashes(cache, CACHE_TYPE_ENTRY);
    }
    PR_Unlock(cache->c_mutex);
    if (! dblayer_is_cachesize_sane(&bytes)) {
       LDAPDebug(LDAP_DEBUG_ANY,
                "WARNING -- Possible CONFIGURATION ERROR -- cachesize "
                "(%lu) may be configured to use more than the available "
                "physical memory.\n", bytes, 0, 0);
    }
}

void cache_set_max_entries(struct cache *cache, long entries)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;

    /* this is a dumb remnant of pre-5.0 servers, where the cache size
     * was given in # entries instead of memory footprint.  hopefully,
     * we can eventually drop this.
     */
    PR_Lock(cache->c_mutex);
    cache->c_maxentries = entries;
    if (entries >= 0) {
        LOG("entry cache entry-limit set to %lu\n", entries, 0, 0);
    } else {
        LOG("entry cache entry-limit turned off\n", 0, 0, 0);
    }

    /* check for full cache, and clear out if necessary */
    if (CACHE_FULL(cache))
        eflush = entrycache_flush(cache);
    PR_Unlock(cache->c_mutex);
    while (eflush)
    {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
}

size_t cache_get_max_size(struct cache *cache)
{
    size_t n = 0;

    PR_Lock(cache->c_mutex);
    n = cache->c_maxsize;
    PR_Unlock(cache->c_mutex);
    return n;
}

long cache_get_max_entries(struct cache *cache)
{
    long n;

    PR_Lock(cache->c_mutex);
    n = cache->c_maxentries;
    PR_Unlock(cache->c_mutex);
    return n;
}

/* determine the general size of a cache entry */
static size_t cache_entry_size(struct backentry *e)
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

/* the monitor code wants to be able to safely fetch the cache stats --
 * if it ever wants to pull out more info, we might want to change all
 * these u_long *'s to a struct
 */
void cache_get_stats(struct cache *cache, PRUint64 *hits, PRUint64 *tries,
                     long *nentries, long *maxentries,
                     size_t *size, size_t *maxsize)
{
    PR_Lock(cache->c_mutex);
    if (hits) *hits = slapi_counter_get_value(cache->c_hits);
    if (tries) *tries = slapi_counter_get_value(cache->c_tries);
    if (nentries) *nentries = cache->c_curentries;
    if (maxentries) *maxentries = cache->c_maxentries;
    if (size) *size = slapi_counter_get_value(cache->c_cursize);
    if (maxsize) *maxsize = cache->c_maxsize;
    PR_Unlock(cache->c_mutex);
}

void cache_debug_hash(struct cache *cache, char **out)
{
    u_long slots;
    int total_entries, max_entries_per_slot, *slot_stats;
    int i, j;
    Hashtable *ht = NULL;
    char *name = "unknown";

    PR_Lock(cache->c_mutex);
    *out = (char *)slapi_ch_malloc(1024);
    **out = 0;

    for (i = 0; i < 3; i++) {
        if (i > 0)
            sprintf(*out + strlen(*out), "; ");
        switch(i) {
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
                "items per slot) -- ", name, slots, total_entries,
                max_entries_per_slot);
        for (j = 0; j <= max_entries_per_slot; j++)
            sprintf(*out + strlen(*out), "%d[%d] ", j, slot_stats[j]);
        slapi_ch_free((void **)&slot_stats);
    }
    PR_Unlock(cache->c_mutex);
}


/***** general-purpose cache stuff *****/

/* remove an entry from the cache */
/* you must be holding c_mutex !! */
static int
entrycache_remove_int(struct cache *cache, struct backentry *e)
{
    int ret = 1;       /* assume not in cache */
    const char *ndn;
#ifdef UUIDCACHE_ON 
    const char *uuid;
#endif

    LOG("=> entrycache_remove_int (%s)\n", backentry_get_ndn(e), 0, 0);
    if (e->ep_state & ENTRY_STATE_NOTINCACHE)
    {
        return ret;
    }

    /* remove from all hashtables -- this function may be called from places
     * where the entry isn't in all the tables yet, so we don't care if any
     * of these return errors.
     */
    ndn = slapi_sdn_get_ndn(backentry_get_sdn(e));
    if (remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn)))
    {
       ret = 0;
    }
    else
    {
        LOG("remove %s from dn hash failed\n", ndn, 0, 0);
    }
    if (remove_hash(cache->c_idtable, &(e->ep_id), sizeof(ID)))
    {
       ret = 0;
    }
    else
    {
        LOG("remove %d from id hash failed\n", e->ep_id, 0, 0);
    }
#ifdef UUIDCACHE_ON 
    uuid = slapi_entry_get_uniqueid(e->ep_entry);
    if (remove_hash(cache->c_uuidtable, (void *)uuid, strlen(uuid)))
    {
       ret = 0;
    }
    else
    {
        LOG("remove %d from uuid hash failed\n", uuid, 0, 0);
    }
#endif
    if (ret == 0) {
        /* won't be on the LRU list since it has a refcount on it */
        /* adjust cache size */
        slapi_counter_subtract(cache->c_cursize, e->ep_size);
        cache->c_curentries--;
        LOG("<= entrycache_remove_int (size %lu): cache now %lu entries, "
            "%lu bytes\n", e->ep_size, cache->c_curentries,
            slapi_counter_get_value(cache->c_cursize));
    }

    /* mark for deletion (will be erased when refcount drops to zero) */
    e->ep_state |= ENTRY_STATE_DELETED;
    LOG("<= entrycache_remove_int: %d\n", ret, 0, 0);
    return ret;
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
int cache_remove(struct cache *cache,  void *ptr)
{
    int ret = 0;
    struct backcommon *e;
    if (NULL == ptr)
    {
        LOG("=> lru_remove\n<= lru_remove (null entry)\n", 0, 0, 0);
        return ret;
    }
    e = (struct backcommon *)ptr;

    PR_Lock(cache->c_mutex);
    if (CACHE_TYPE_ENTRY == e->ep_type) {
        ASSERT(e->ep_refcnt > 0);
        ret = entrycache_remove_int(cache, (struct backentry *)e);
    } else if (CACHE_TYPE_DN == e->ep_type) {
        ret = dncache_remove_int(cache, (struct backdn *)e);
    }
    PR_Unlock(cache->c_mutex);
    return ret;
}

/* replace an entry in the cache.
 * returns:       0 on success
 *                1 if the entry wasn't in the cache
 */
int cache_replace(struct cache *cache, void *oldptr, void *newptr)
{
    struct backcommon *olde;
    if (NULL == oldptr || NULL == newptr)
    {
        LOG("=> lru_replace\n<= lru_replace (null entry)\n", 0, 0, 0);
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

static int entrycache_replace(struct cache *cache, struct backentry *olde,
                              struct backentry *newe)
{
    int found;
    const char *oldndn;
    const char *newndn;
#ifdef UUIDCACHE_ON 
    const char *olduuid;
    const char *newuuid;
#endif

    LOG("=> entrycache_replace (%s) -> (%s)\n", backentry_get_ndn(olde),
        backentry_get_ndn(newe), 0);

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
    PR_Lock(cache->c_mutex);

    /*
     * First, remove the old entry from all the hashtables.
     * If the old entry is in cache but not in at least one of the
     * cache tables, operation error 
     */
    if ( (olde->ep_state & ENTRY_STATE_NOTINCACHE) == 0 ) {

        found = remove_hash(cache->c_dntable, (void *)oldndn, strlen(oldndn));
        found &= remove_hash(cache->c_idtable, &(olde->ep_id), sizeof(ID));
#ifdef UUIDCACHE_ON
        found &= remove_hash(cache->c_uuidtable, (void *)olduuid, strlen(olduuid));
#endif
        if (!found) {
            LOG("entry cache replace: cache index tables out of sync\n", 0, 0, 0);
            PR_Unlock(cache->c_mutex);
            return 1;
        }
    }
    if (! entry_same_dn(newe, (void *)oldndn) &&
         (newe->ep_state & ENTRY_STATE_NOTINCACHE) == 0) {
        /* if we're doing a modrdn, the new entry can be in the dn table
         * already, so we need to remove that too.
         */
        if (remove_hash(cache->c_dntable, (void *)newndn, strlen(newndn)))
        {
            slapi_counter_subtract(cache->c_cursize, newe->ep_size);
            cache->c_curentries--;
            LOG("entry cache replace remove entry size %lu\n", newe->ep_size, 0, 0);
        }
    }

    /* now, add the new entry to the hashtables */
    /* (probably don't need such extensive error handling, once this has been
     * tested enough that we believe it works.)
     */
    if (!add_hash(cache->c_dntable, (void *)newndn, strlen(newndn), newe, NULL)) {
       LOG("entry cache replace: can't add dn\n", 0, 0, 0);
       PR_Unlock(cache->c_mutex);
       return 1;
    }
    if (!add_hash(cache->c_idtable, &(newe->ep_id), sizeof(ID), newe, NULL)) {
       LOG("entry cache replace: can't add id\n", 0, 0, 0);
       remove_hash(cache->c_dntable, (void *)newndn, strlen(newndn));
       PR_Unlock(cache->c_mutex);
       return 1;
    }
#ifdef UUIDCACHE_ON 
    if (newuuid && !add_hash(cache->c_uuidtable, (void *)newuuid, strlen(newuuid),
                       newe, NULL)) {
       LOG("entry cache replace: can't add uuid\n", 0, 0, 0);
       remove_hash(cache->c_dntable, (void *)newndn, strlen(newndn));
       remove_hash(cache->c_idtable, &(newe->ep_id), sizeof(ID));
       PR_Unlock(cache->c_mutex);
       return 1;
    }
#endif
    /* adjust cache meta info */
    newe->ep_refcnt = 1;
    newe->ep_size = cache_entry_size(newe);
    if (newe->ep_size > olde->ep_size) {
        slapi_counter_add(cache->c_cursize, newe->ep_size - olde->ep_size);
    } else if (newe->ep_size < olde->ep_size) {
        slapi_counter_subtract(cache->c_cursize, olde->ep_size - newe->ep_size);
    }
    olde->ep_state = ENTRY_STATE_DELETED;
    newe->ep_state = 0;
    PR_Unlock(cache->c_mutex);
    LOG("<= entrycache_replace OK,  cache size now %lu cache count now %ld\n",
             slapi_counter_get_value(cache->c_cursize), cache->c_curentries, 0);
    return 0;
}

/* call this when you're done with an entry that was fetched via one of
 * the cache_find_* calls.
 */
void cache_return(struct cache *cache, void **ptr)
{
    struct backcommon *bep;

    if (NULL == ptr || NULL == *ptr)
    {
        LOG("=> cache_return\n<= cache_return (null entry)\n", 0, 0, 0);
        return;
    }
    bep = *(struct backcommon **)ptr;
    if (CACHE_TYPE_ENTRY == bep->ep_type) {
        entrycache_return(cache, (struct backentry **)ptr);
    } else if (CACHE_TYPE_DN == bep->ep_type) {
        dncache_return(cache, (struct backdn **)ptr);
    }
}

static void
entrycache_return(struct cache *cache, struct backentry **bep)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;
    struct backentry *e;

    e = *bep;
    LOG("=> entrycache_return (%s) entry count: %d, entry in cache:%ld\n",
                    backentry_get_ndn(e), e->ep_refcnt, cache->c_curentries);

    PR_Lock(cache->c_mutex);
    if (e->ep_state & ENTRY_STATE_NOTINCACHE)
    {
        backentry_free(bep);
    }
    else
    {
        ASSERT(e->ep_refcnt > 0);
        if (! --e->ep_refcnt) {
            if (e->ep_state & ENTRY_STATE_DELETED) {
                backentry_free(bep);
            } else {
                lru_add(cache, e);
                /* the cache might be overfull... */
                if (CACHE_FULL(cache))
                    eflush = entrycache_flush(cache);
            }
        }
    }
    PR_Unlock(cache->c_mutex);
    while (eflush)
    {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    LOG("<= entrycache_return\n", 0, 0, 0);
}


/* lookup entry by DN (assume cache lock is held) */
struct backentry *cache_find_dn(struct cache *cache, const char *dn, unsigned long ndnlen)
{
    struct backentry *e;

    LOG("=> cache_find_dn (%s)\n", dn, 0, 0);

    /*entry normalized by caller (dn2entry.c)  */
    PR_Lock(cache->c_mutex);
    if (find_hash(cache->c_dntable, (void *)dn, ndnlen, (void **)&e)) {
       /* need to check entry state */
       if (e->ep_state != 0) {
           /* entry is deleted or not fully created yet */
           PR_Unlock(cache->c_mutex);
           LOG("<= cache_find_dn (NOT FOUND)\n", 0, 0, 0);
           return NULL;
       }
       if (e->ep_refcnt == 0)
           lru_delete(cache, (void *)e);
       e->ep_refcnt++;
       PR_Unlock(cache->c_mutex);
       slapi_counter_increment(cache->c_hits);
    } else {
       PR_Unlock(cache->c_mutex);
    }
    slapi_counter_increment(cache->c_tries);

    LOG("<= cache_find_dn (%sFOUND)\n", e ? "" : "NOT ", 0, 0);
    return e;
}


/* lookup an entry in the cache by its id# (you must return it later) */
struct backentry *cache_find_id(struct cache *cache, ID id)
{
    struct backentry *e;

    LOG("=> cache_find_id (%lu)\n", (u_long)id, 0, 0);

    PR_Lock(cache->c_mutex);
    if (find_hash(cache->c_idtable, &id, sizeof(ID), (void **)&e)) {
       /* need to check entry state */
       if (e->ep_state != 0) {
           /* entry is deleted or not fully created yet */
           PR_Unlock(cache->c_mutex);
           LOG("<= cache_find_id (NOT FOUND)\n", 0, 0, 0);
           return NULL;
       }
       if (e->ep_refcnt == 0)
           lru_delete(cache, (void *)e);
       e->ep_refcnt++;
       PR_Unlock(cache->c_mutex);
       slapi_counter_increment(cache->c_hits);
    } else {
       PR_Unlock(cache->c_mutex);
    }
    slapi_counter_increment(cache->c_tries);

    LOG("<= cache_find_id (%sFOUND)\n", e ? "" : "NOT ", 0, 0);
    return e;
}

#ifdef UUIDCACHE_ON 
/* lookup an entry in the cache by it's uuid (you must return it later) */
struct backentry *cache_find_uuid(struct cache *cache, const char *uuid)
{
    struct backentry *e;

    LOG("=> cache_find_uuid (%s)\n", uuid, 0, 0);

    PR_Lock(cache->c_mutex);
    if (find_hash(cache->c_uuidtable, uuid, strlen(uuid), (void **)&e)) {
       /* need to check entry state */
       if (e->ep_state != 0) {
           /* entry is deleted or not fully created yet */
           PR_Unlock(cache->c_mutex);
           LOG("<= cache_find_uuid (NOT FOUND)\n", 0, 0, 0);
           return NULL;
       }
       if (e->ep_refcnt == 0)
           lru_delete(cache, (void *)e);
       e->ep_refcnt++;
       PR_Unlock(cache->c_mutex);
       slapi_counter_increment(cache->c_hits);
    } else {
       PR_Unlock(cache->c_mutex);
    }
    slapi_counter_increment(cache->c_tries);

    LOG("<= cache_find_uuid (%sFOUND)\n", e ? "" : "NOT ", 0, 0);
    return e;
}
#endif

/* add an entry to the cache */
static int
entrycache_add_int(struct cache *cache, struct backentry *e, int state,
                   struct backentry **alt)
{
    struct backentry *eflush = NULL;
    struct backentry *eflushtemp = NULL;
    const char *ndn = slapi_sdn_get_ndn(backentry_get_sdn(e));
#ifdef UUIDCACHE_ON 
    const char *uuid = slapi_entry_get_uniqueid(e->ep_entry);
#endif
    struct backentry *my_alt;
    int already_in = 0;

    LOG("=> entrycache_add_int( \"%s\", %ld )\n", backentry_get_ndn(e),
        e->ep_id, 0);

    PR_Lock(cache->c_mutex);
    if (! add_hash(cache->c_dntable, (void *)ndn, strlen(ndn), e,
           (void **)&my_alt)) {
        LOG("entry \"%s\" already in dn cache\n", backentry_get_ndn(e), 0, 0);
        /* add_hash filled in 'my_alt' if necessary */
        if (my_alt == e)
        {
            if ((e->ep_state & ENTRY_STATE_CREATING) && (state == 0))
            {
                /* attempting to "add" an entry that's already in the cache,
                 * and the old entry was a placeholder and the new one isn't?
                 * sounds like a confirmation of a previous add!
                 */
                LOG("confirming a previous add\n", 0, 0, 0);
                already_in = 1;
            }
            else
            {
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
                e->ep_state = state; /* might be CREATING */
                /* returning 1 (entry already existed), but don't set to alt
                 * to prevent that the caller accidentally thinks the existing
                 * entry is not the same one the caller has and releases it.
                 */
                PR_Unlock(cache->c_mutex);
                return 1;
            }
        }
        else
        {
            if (my_alt->ep_state & ENTRY_STATE_CREATING)
            {
                LOG("the entry is reserved\n", 0, 0, 0);
                e->ep_state |= ENTRY_STATE_NOTINCACHE;
                PR_Unlock(cache->c_mutex);
                return -1;
            }
            else if (state != 0)
            {
                LOG("the entry already exists. cannot reserve it.\n", 0, 0, 0);
                e->ep_state |= ENTRY_STATE_NOTINCACHE;
                PR_Unlock(cache->c_mutex);
                return -1;
            }
            else
            {
                if (alt) {
                    *alt = my_alt;
                    if ((*alt)->ep_refcnt == 0)
                        lru_delete(cache, (void *)*alt);
                    (*alt)->ep_refcnt++;
                }
                PR_Unlock(cache->c_mutex);
                return 1;
            }
        }
    }

    /* creating an entry with ENTRY_STATE_CREATING just creates a stub
     * which is only stored in the dn table (basically, reserving the dn) --
     * doing an add later with state==0 will "confirm" the add
     */
    if (state == 0) {
        /* neither of these should fail, or something is very wrong. */
        if (! add_hash(cache->c_idtable, &(e->ep_id), sizeof(ID), e, NULL)) {
            LOG("entry %s already in id cache!\n", backentry_get_ndn(e), 0, 0);
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
                LOG("<= entrycache_add_int (ignoring)\n", 0, 0, 0);
                PR_Unlock(cache->c_mutex);
                return 0;
            }
            remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn));
            e->ep_state |= ENTRY_STATE_NOTINCACHE;
            PR_Unlock(cache->c_mutex);
            return -1;
        }
#ifdef UUIDCACHE_ON 
        if (uuid) {
            /* (only insert entries with a uuid) */
            if (! add_hash(cache->c_uuidtable, (void *)uuid, strlen(uuid), e,
                   NULL)) {
                LOG("entry %s already in uuid cache!\n", backentry_get_ndn(e),
                            0, 0);
                remove_hash(cache->c_dntable, (void *)ndn, strlen(ndn));
                remove_hash(cache->c_idtable, &(e->ep_id), sizeof(ID));
                e->ep_state |= ENTRY_STATE_NOTINCACHE;
                PR_Unlock(cache->c_mutex);
                return -1;
            }
        }
#endif
    }

    e->ep_state = state;

    if (! already_in) {
        e->ep_refcnt = 1;
        e->ep_size = cache_entry_size(e);
    
        slapi_counter_add(cache->c_cursize, e->ep_size);
        cache->c_curentries++;
        /* don't add to lru since refcnt = 1 */
        LOG("added entry of size %lu -> total now %lu out of max %lu\n",
          e->ep_size, slapi_counter_get_value(cache->c_cursize), cache->c_maxsize);
        if (cache->c_maxentries >= 0) {
            LOG("    total entries %ld out of %ld\n",
                    cache->c_curentries, cache->c_maxentries, 0);
        }
        /* check for full cache, and clear out if necessary */
        if (CACHE_FULL(cache))
            eflush = entrycache_flush(cache);
    }
    PR_Unlock(cache->c_mutex);

    while (eflush)
    {
        eflushtemp = BACK_LRU_NEXT(eflush, struct backentry *);
        backentry_free(&eflush);
        eflush = eflushtemp;
    }
    LOG("<= entrycache_add_int OK\n", 0, 0, 0);
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
int cache_add(struct cache *cache, void *ptr, void **alt)
{
    struct backcommon *e;
    if (NULL == ptr)
    {
        LOG("=> cache_add\n<= cache_add (null entry)\n", 0, 0, 0);
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
int cache_add_tentative(struct cache *cache, struct backentry *e,
                     struct backentry **alt)
{
    return entrycache_add_int(cache, e, ENTRY_STATE_CREATING, alt);
}

/* locks an entry so that it can be modified (you should have gotten the
 * entry via cache_find_*).
 * returns 0 on success, 1 if the entry is scheduled for deletion.
 */
int cache_lock_entry(struct cache *cache, struct backentry *e)
{
    LOG("=> cache_lock_entry (%s)\n", backentry_get_ndn(e), 0, 0);

    if (! e->ep_mutexp) {
       /* make sure only one thread does this */
       PR_Lock(cache->c_emutexalloc_mutex);
       if (! e->ep_mutexp)
           e->ep_mutexp = PR_NewLock();
       PR_Unlock(cache->c_emutexalloc_mutex);
    }

    /* wait on entry lock (done w/o holding the cache lock) */
    PR_Lock(e->ep_mutexp);

    /* make sure entry hasn't been deleted now */
    PR_Lock(cache->c_mutex);
    if (e->ep_state & (ENTRY_STATE_DELETED|ENTRY_STATE_NOTINCACHE)) {
       PR_Unlock(cache->c_mutex);
       PR_Unlock(e->ep_mutexp);
       LOG("<= cache_lock_entry (DELETED)\n", 0, 0, 0);
       return 1;
    }
    PR_Unlock(cache->c_mutex);

    LOG("<= cache_lock_entry (FOUND)\n", 0, 0, 0);
    return 0;
}

/* the opposite of above */
void cache_unlock_entry(struct cache *cache, struct backentry *e)
{
    LOG("=> cache_unlock_entry\n", 0, 0, 0);
    PR_Unlock(e->ep_mutexp);
}

/* DN cache */
/* remove everything from the cache */
static void
dncache_clear_int(struct cache *cache)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;
    size_t size = cache->c_maxsize;

    if (!entryrdn_get_switch()) {
        return;
    }

    cache->c_maxsize = 0;
    dnflush = dncache_flush(cache);
    while (dnflush)
    {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
    cache->c_maxsize = size;
    if (cache->c_curentries > 0) {
       LDAPDebug1Arg(LDAP_DEBUG_ANY,
                     "dncache_clear_int: there are still %ld dn's "
                     "in the dn cache. :/\n", cache->c_curentries);
    }
}

static int
dn_same_id(const void *bdn, const void *k)
{
    return (((struct backdn *)bdn)->ep_id == *(ID *)k);
}

static void
dncache_set_max_size(struct cache *cache, size_t bytes)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;

    if (!entryrdn_get_switch()) {
        return;
    }

    if (bytes < MINCACHESIZE) {
       LDAPDebug2Args(LDAP_DEBUG_ANY,
                      "WARNING -- Max dn cache size %lu is set; "
                      "Minimum dn cache size is %lu -- rounding up\n",
                      bytes, MINCACHESIZE);
       bytes = MINCACHESIZE;
    } else {
       LDAPDebug1Arg(LDAP_DEBUG_BACKLDBM,
                     "Max dn cache size is set to %lu\n", bytes);
    }
    PR_Lock(cache->c_mutex);
    cache->c_maxsize = bytes;
    LOG("dn cache size set to %lu\n", bytes, 0, 0);
    /* check for full cache, and clear out if necessary */
    if (CACHE_FULL(cache)) {
       dnflush = dncache_flush(cache);
    }
    while (dnflush)
    {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
    if (cache->c_curentries < 50) {
       /* there's hardly anything left in the cache -- clear it out and
        * resize the hashtables for efficiency.
        */
       erase_cache(cache, CACHE_TYPE_DN);
       cache_make_hashes(cache, CACHE_TYPE_DN);
    }
    PR_Unlock(cache->c_mutex);
    if (! dblayer_is_cachesize_sane(&bytes)) {
       LDAPDebug1Arg(LDAP_DEBUG_ANY,
                "WARNING -- Possible CONFIGURATION ERROR -- cachesize "
                "(%lu) may be configured to use more than the available "
                "physical memory.\n", bytes);
    }
}

/* remove a dn from the cache */
/* you must be holding c_mutex !! */
static int
dncache_remove_int(struct cache *cache, struct backdn *bdn)
{
    int ret = 1;       /* assume not in cache */

    if (!entryrdn_get_switch()) {
        return 0;
    }

    LOG("=> dncache_remove_int (%s)\n", slapi_sdn_get_dn(bdn->dn_sdn), 0, 0);
    if (bdn->ep_state & ENTRY_STATE_NOTINCACHE)
    {
        return ret;
    }

    /* remove from id hashtable */
    if (remove_hash(cache->c_idtable, &(bdn->ep_id), sizeof(ID)))
    {
       ret = 0;
    }
    else
    {
        LOG("remove %d from id hash failed\n", bdn->ep_id, 0, 0);
    }
    if (ret == 0) {
        /* won't be on the LRU list since it has a refcount on it */
        /* adjust cache size */
        slapi_counter_subtract(cache->c_cursize, bdn->ep_size);
        cache->c_curentries--;
        LOG("<= dncache_remove_int (size %lu): cache now %lu dn's, %lu bytes\n",
            bdn->ep_size, cache->c_curentries,
            slapi_counter_get_value(cache->c_cursize));
    }

    /* mark for deletion (will be erased when refcount drops to zero) */
    bdn->ep_state |= ENTRY_STATE_DELETED;
    LOG("<= dncache_remove_int: %d\n", ret, 0, 0);
    return ret;
}

static void
dncache_return(struct cache *cache, struct backdn **bdn)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;

    if (!entryrdn_get_switch()) {
        return;
    }

    LOG("=> dncache_return (%s) reference count: %d, dn in cache:%ld\n",
      slapi_sdn_get_dn((*bdn)->dn_sdn), (*bdn)->ep_refcnt, cache->c_curentries);

    PR_Lock(cache->c_mutex);
    if ((*bdn)->ep_state & ENTRY_STATE_NOTINCACHE)
    {
        backdn_free(bdn);
    }
    else
    {
        ASSERT((*bdn)->ep_refcnt > 0);
        if (! --(*bdn)->ep_refcnt) {
            if ((*bdn)->ep_state & ENTRY_STATE_DELETED) {
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
    PR_Unlock(cache->c_mutex);
    while (dnflush)
    {
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

    if (!entryrdn_get_switch()) {
        return bdn;
    }

    LOG("=> dncache_find_id (%lu)\n", (u_long)id, 0, 0);

    PR_Lock(cache->c_mutex);
    if (find_hash(cache->c_idtable, &id, sizeof(ID), (void **)&bdn)) {
       /* need to check entry state */
       if (bdn->ep_state != 0) {
           /* entry is deleted or not fully created yet */
           PR_Unlock(cache->c_mutex);
           LOG("<= dncache_find_id (NOT FOUND)\n", 0, 0, 0);
           return NULL;
       }
       if (bdn->ep_refcnt == 0)
           lru_delete(cache, (void *)bdn);
       bdn->ep_refcnt++;
       PR_Unlock(cache->c_mutex);
       slapi_counter_increment(cache->c_hits);
    } else {
       PR_Unlock(cache->c_mutex);
    }
    slapi_counter_increment(cache->c_tries);

    LOG("<= cache_find_id (%sFOUND)\n", bdn ? "" : "NOT ", 0, 0);
    return bdn;
}

/* add a dn to the cache */
static int
dncache_add_int(struct cache *cache, struct backdn *bdn, int state,
                struct backdn **alt)
{
    struct backdn *dnflush = NULL;
    struct backdn *dnflushtemp = NULL;
    struct backdn *my_alt;
    int already_in = 0;

    if (!entryrdn_get_switch()) {
        return 0;
    }

    LOG("=> dncache_add_int( \"%s\", %ld )\n", slapi_sdn_get_dn(bdn->dn_sdn), 
        bdn->ep_id, 0);

    PR_Lock(cache->c_mutex);

    if (! add_hash(cache->c_idtable, &(bdn->ep_id), sizeof(ID), bdn,
                                                           (void **)&my_alt)) {
        LOG("entry %s already in id cache!\n", slapi_sdn_get_dn(bdn->dn_sdn), 0, 0);
        if (my_alt == bdn)
        {
            if ((bdn->ep_state & ENTRY_STATE_CREATING) && (state == 0))
            {
                /* attempting to "add" a dn that's already in the cache,
                 * and the old entry was a placeholder and the new one isn't?
                 * sounds like a confirmation of a previous add!
                 */
                LOG("confirming a previous add\n", 0, 0, 0);
                already_in = 1;
            }
            else
            {
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
                PR_Unlock(cache->c_mutex);
                return 1;
            }
        }
        else
        {
            if (my_alt->ep_state & ENTRY_STATE_CREATING)
            {
                LOG("the entry is reserved\n", 0, 0, 0);
                bdn->ep_state |= ENTRY_STATE_NOTINCACHE;
                PR_Unlock(cache->c_mutex);
                return -1;
            }
            else if (state != 0)
            {
                LOG("the entry already exists. cannot reserve it.\n", 0, 0, 0);
                bdn->ep_state |= ENTRY_STATE_NOTINCACHE;
                PR_Unlock(cache->c_mutex);
                return -1;
            }
            else
            {
                if (alt) {
                    *alt = my_alt;
                    if ((*alt)->ep_refcnt == 0)
                        lru_delete(cache, (void *)*alt);
                    (*alt)->ep_refcnt++;
                }
                PR_Unlock(cache->c_mutex);
                return 1;
            }
        }
    }

    bdn->ep_state = state;

    if (! already_in) {
        bdn->ep_refcnt = 1;
        if (0 == bdn->ep_size) {
            bdn->ep_size = slapi_sdn_get_size(bdn->dn_sdn);
        }
    
        slapi_counter_add(cache->c_cursize, bdn->ep_size);
        cache->c_curentries++;
        /* don't add to lru since refcnt = 1 */
        LOG("added entry of size %lu -> total now %lu out of max %lu\n",
            bdn->ep_size, slapi_counter_get_value(cache->c_cursize),
            cache->c_maxsize);
        if (cache->c_maxentries >= 0) {
            LOG("    total entries %ld out of %ld\n",
                    cache->c_curentries, cache->c_maxentries, 0);
        }
        /* check for full cache, and clear out if necessary */
        if (CACHE_FULL(cache)) {
            dnflush = dncache_flush(cache);
        }
    }
    PR_Unlock(cache->c_mutex);

    while (dnflush)
    {
        dnflushtemp = BACK_LRU_NEXT(dnflush, struct backdn *);
        backdn_free(&dnflush);
        dnflush = dnflushtemp;
    }
    LOG("<= dncache_add_int OK\n", 0, 0, 0);
    return 0;
}

static int
dncache_replace(struct cache *cache, struct backdn *olddn, struct backdn *newdn)
{
    int found;

    if (!entryrdn_get_switch()) {
        return 0;
    }

    LOG("=> dncache_replace (%s) -> (%s)\n",
        slapi_sdn_get_dn(olddn->dn_sdn), slapi_sdn_get_dn(newdn->dn_sdn), 0);

    /* remove from all hashtable -- this function may be called from places
     * where the entry isn't in all the table yet, so we don't care if any
     * of these return errors.
     */
    PR_Lock(cache->c_mutex);

    /*
     * First, remove the old entry from the hashtable.
     * If the old entry is in cache but not in at least one of the
     * cache tables, operation error 
     */
    if ( (olddn->ep_state & ENTRY_STATE_NOTINCACHE) == 0 ) {

        found = remove_hash(cache->c_idtable, &(olddn->ep_id), sizeof(ID));
        if (!found) {
            LOG("dn cache replace: cache index tables out of sync\n", 0, 0, 0);
            PR_Unlock(cache->c_mutex);
            return 1;
        }
    }

    /* now, add the new entry to the hashtables */
    /* (probably don't need such extensive error handling, once this has been
     * tested enough that we believe it works.)
     */
    if (!add_hash(cache->c_idtable, &(newdn->ep_id), sizeof(ID), newdn, NULL)) {
       LOG("dn cache replace: can't add id\n", 0, 0, 0);
       PR_Unlock(cache->c_mutex);
       return 1;
    }
    /* adjust cache meta info */
    newdn->ep_refcnt = 1;
    if (0 == newdn->ep_size) {
        newdn->ep_size = slapi_sdn_get_size(newdn->dn_sdn);
    }
    if (newdn->ep_size > olddn->ep_size) {
        slapi_counter_add(cache->c_cursize, newdn->ep_size - olddn->ep_size);
    } else if (newdn->ep_size < olddn->ep_size) {
        slapi_counter_subtract(cache->c_cursize, olddn->ep_size - newdn->ep_size);
    }
    olddn->ep_state = ENTRY_STATE_DELETED;
    newdn->ep_state = 0;
    PR_Unlock(cache->c_mutex);
    LOG("<= dncache_replace OK,  cache size now %lu cache count now %ld\n",
             slapi_counter_get_value(cache->c_cursize), cache->c_curentries, 0);
    return 0;
}

static struct backdn *
dncache_flush(struct cache *cache)
{
    struct backdn *dn = NULL;

    if (!entryrdn_get_switch()) {
        return dn;
    }

    LOG("=> dncache_flush\n", 0, 0, 0);

    /* all entries on the LRU list are guaranteed to have a refcnt = 0
     * (iow, nobody's using them), so just delete from the tail down
     * until the cache is a managable size again.
     * (cache->c_mutex is locked when we enter this)
     */
    while ((cache->c_lrutail != NULL) && CACHE_FULL(cache)) {
        if (dn == NULL)
        {
            dn = CACHE_LRU_TAIL(cache, struct backdn *);
        }
        else
        {
            dn = BACK_LRU_PREV(dn, struct backdn *);
        }
        ASSERT(dn->ep_refcnt == 0);
        dn->ep_refcnt++;
        if (dncache_remove_int(cache, dn) < 0) {
           LDAPDebug(LDAP_DEBUG_ANY, "dn cache flush: unable to delete entry\n",
                    0, 0, 0);
           break;
        }
        if(dn == CACHE_LRU_HEAD(cache, struct backdn *)) {
            break;
        }
    }
    if (dn)
        LRU_DETACH(cache, dn);
    LOG("<= dncache_flush (down to %lu dns, %lu bytes)\n", cache->c_curentries,
        slapi_counter_get_value(cache->c_cursize), 0);
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
           ASSERT(BACK_LRU_NEXT(BACK_LRU_PREV(dnp, struct backdn *), struct backdn *)== dnp);
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
