/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"
#include "benchmark_par.h"
#include <plhash.h>
#include <stdio.h>
#include <unistd.h>

// #define WITH_RWLOCK 1

#ifdef WITH_RWLOCK
static pthread_rwlock_t the_lock;
#else
static pthread_mutex_t the_lock;
#endif

int64_t generic_read_begin(void **inst __attribute__((unused)), void **txn __attribute__((unused))) {
#ifdef WITH_RWLOCK
    pthread_rwlock_rdlock(&the_lock);
#else
    pthread_mutex_lock(&the_lock);
#endif
    return 0;
}

int64_t generic_read_complete(void **inst __attribute__((unused)), void *txn __attribute__((unused))) {
#ifdef WITH_RWLOCK
    pthread_rwlock_unlock(&the_lock);
#else
    pthread_mutex_unlock(&the_lock);
#endif
    return 0;
}

int64_t generic_write_begin(void **inst __attribute__((unused)), void **txn __attribute__((unused))) {
#ifdef WITH_RWLOCK
    pthread_rwlock_wrlock(&the_lock);
#else
    pthread_mutex_lock(&the_lock);
#endif
    return 0;
}

int64_t generic_write_commit(void **inst __attribute__((unused)), void *txn __attribute__((unused))) {
#ifdef WITH_RWLOCK
    pthread_rwlock_unlock(&the_lock);
#else
    pthread_mutex_unlock(&the_lock);
#endif
    return 0;
}

/* The sds b+ tree wrappers */

int64_t bptree_init_wrapper(void **inst) {
#ifdef WITH_RWLOCK
    pthread_rwlock_init(&the_lock, NULL);
#else
    pthread_mutex_init(&the_lock, NULL);
#endif
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_bptree_init(binst, 0, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);
    return 0;
}

int64_t bptree_add_wrapper(void **inst, void *txn __attribute__((unused)), void *key, void *value) {
    // THIS WILL BREAK SOMETIME!
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_bptree_insert(*binst, key, value);
    return 0;
}

int64_t bptree_search_wrapper(void **inst, void *txn __attribute__((unused)), void *key, void **value_out __attribute__((unused))) {
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_result result = sds_bptree_search(*binst, key);
    if (result != SDS_KEY_PRESENT) {
        // printf("search result is %d\n", result);
        return 1;
    }
    return 0;
}

int64_t bptree_delete_wrapper(void **inst, void *txn __attribute__((unused)), void *key) {
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_result result = sds_bptree_delete(*binst, key);

    if (result != SDS_KEY_PRESENT) {
        // printf("delete result is %d\n", result);
        return 1;
    }
    return 0;
}

int64_t bptree_destroy_wrapper(void **inst) {
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    // sds_bptree_display(*binst);
    sds_bptree_destroy(*binst);
#ifdef WITH_RWLOCK
    pthread_rwlock_destroy(&the_lock);
#else
    pthread_mutex_destroy(&the_lock);
#endif
    return 0;
}

/* sds bptree cow wrapper */

int64_t bptree_cow_init_wrapper(void **inst) {
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_cow_init(binst, 0, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_dup);
    return 0;
}

int64_t bptree_cow_read_begin(void **inst, void **read_txn) {
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_transaction **txn = (sds_bptree_transaction **)read_txn;
    sds_bptree_cow_rotxn_begin(*binst, txn);
    return 0;
}

int64_t bptree_cow_read_complete(void **inst __attribute__((unused)), void *read_txn) {
    // sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_transaction *txn = (sds_bptree_transaction *)read_txn;
    sds_bptree_cow_rotxn_close(&txn);
    return 0;
}

int64_t bptree_cow_write_begin(void **inst, void **write_txn) {
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_transaction **txn = (sds_bptree_transaction **)write_txn;
#ifdef DEBUG
    assert_int_equal(sds_bptree_cow_verify(*binst), SDS_SUCCESS);
#endif
    sds_bptree_cow_wrtxn_begin(*binst, txn);
    return 0;
}

int64_t bptree_cow_write_commit(void **inst __attribute__((unused)), void *write_txn) {
    // sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_transaction *txn = (sds_bptree_transaction *)write_txn;
    sds_bptree_cow_wrtxn_commit(&txn);
    return 0;
}

int64_t bptree_cow_add_wrapper(void **inst __attribute__((unused)), void *write_txn, void *key, void *value) {
    // THIS WILL BREAK SOMETIME!
    sds_bptree_transaction *txn = (sds_bptree_transaction *)write_txn;
    sds_bptree_cow_insert(txn, key, value);
    return 0;
}

int64_t bptree_cow_search_wrapper(void **inst __attribute__((unused)), void *read_txn, void *key, void **value_out __attribute__((unused))) {
    sds_bptree_transaction *txn = (sds_bptree_transaction *)read_txn;
    sds_result result = sds_bptree_cow_search(txn, key);
    if (result != SDS_KEY_PRESENT) {
        return 1;
    }
    return 0;
}

int64_t bptree_cow_delete_wrapper(void **inst __attribute__((unused)), void *write_txn, void *key) {
    // sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_transaction *txn = (sds_bptree_transaction *)write_txn;
    sds_result result = sds_bptree_cow_delete(txn, key);
    if (result != SDS_KEY_PRESENT) {
        return 1;
    }
    return 0;
}

int64_t bptree_cow_destroy_wrapper(void **inst) {
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    // sds_bptree_display(*binst);
    sds_bptree_cow_destroy(*binst);
    return 0;
}

/* Hashmap structures */

/* NSPR PLHash wrappers */

/*
 * This number is chosen because it's the amount used by the NDN cache in
 * Directory Server. Most of the other caches are 2047 or lower. The exception
 * is the entry cache, which is often upwards of 20483 buckets. Even with
 * 20483 buckets, at 1,000,000, we still with perfect hashing devolve to
 * linked lists of length 48.
 * If you increase the DS cache to 120MB, you get 245761 slots, which is only
 * 4 long.
 * There is a quirk! DS cache only allows 1 item, so the moment the cache
 * hash matches it's overriden! Uh oh!
 * With this value of 2053, we are looking at lists of 487 at a maximum!
 */

#define HASH_BUCKETS_SMALL 2053
#define HASH_BUCKETS_MED 20483
#define HASH_BUCKETS_LARGE 245761

PLHashNumber
hash_func_large(const void *key) {
    uint64_t ik = (uint64_t)key;
    return ik % HASH_BUCKETS_LARGE;
}

PLHashNumber
hash_func_med(const void *key) {
    uint64_t ik = (uint64_t)key;
    return ik % HASH_BUCKETS_MED;
}

PLHashNumber
hash_func_small(const void *key) {
    uint64_t ik = (uint64_t)key;
    return ik % HASH_BUCKETS_SMALL;
}

PRIntn
hash_key_compare (const void *a, const void *b) {
    uint64_t ia = (uint64_t)a;
    uint64_t ib = (uint64_t)b;
    return ia == ib;
}

PRIntn
hash_value_compare(const void *a __attribute__((unused)), const void *b __attribute__((unused))) {
    // This cheats and says they are always differnt, but I don't think I use this ....
    return 1;
}

int64_t
hash_small_init_wrapper(void **inst) {
    PLHashTable **table = (PLHashTable **)inst;
#ifdef WITH_RWLOCK
    pthread_rwlock_init(&the_lock, NULL);
#else
    pthread_mutex_init(&the_lock, NULL);
#endif

    *table = PL_NewHashTable(HASH_BUCKETS_SMALL, hash_func_small, hash_key_compare, hash_value_compare, NULL, NULL);
    return 0;
}

int64_t
hash_med_init_wrapper(void **inst) {
    PLHashTable **table = (PLHashTable **)inst;
#ifdef WITH_RWLOCK
    pthread_rwlock_init(&the_lock, NULL);
#else
    pthread_mutex_init(&the_lock, NULL);
#endif

    *table = PL_NewHashTable(HASH_BUCKETS_MED, hash_func_med, hash_key_compare, hash_value_compare, NULL, NULL);
    return 0;
}

int64_t
hash_large_init_wrapper(void **inst) {
    PLHashTable **table = (PLHashTable **)inst;
#ifdef WITH_RWLOCK
    pthread_rwlock_init(&the_lock, NULL);
#else
    pthread_mutex_init(&the_lock, NULL);
#endif

    *table = PL_NewHashTable(HASH_BUCKETS_LARGE, hash_func_large, hash_key_compare, hash_value_compare, NULL, NULL);
    return 0;
}

int64_t
hash_add_wrapper(void **inst, void *write_txn __attribute__((unused)), void *key, void *value __attribute__((unused))) {
    PLHashTable **table = (PLHashTable **)inst;
    // WARNING: We have to add key as value too else hashmap won't add it!!!
    PL_HashTableAdd(*table, key, key);
    return 0;
}

int64_t
hash_search_wrapper(void **inst, void *read_txn __attribute__((unused)), void *key, void **value_out) {
    PLHashTable **table = (PLHashTable **)inst;
    *value_out = PL_HashTableLookup(*table, key);
    if (*value_out == NULL) {
        return 1;
    }
    return 0;
}

int64_t
hash_delete_wrapper(void **inst, void *write_txn __attribute__((unused)), void *key) {
    PLHashTable **table = (PLHashTable **)inst;
    PL_HashTableRemove(*table, key);
    return 0;
}

int64_t
hash_destroy_wrapper(void **inst) {
    PLHashTable **table = (PLHashTable **)inst;
#ifdef WITH_RWLOCK
    pthread_rwlock_destroy(&the_lock);
#else
    pthread_mutex_destroy(&the_lock);
#endif
    PL_HashTableDestroy(*table);
    return 0;
}

