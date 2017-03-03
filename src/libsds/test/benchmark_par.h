/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#include <time.h>
#include <pthread.h>

struct b_tree_cow_cb {
    char *name;
    void *inst;
    int64_t (*init)(void **inst);
    int64_t (*read_begin)(void **inst, void **read_txn);
    int64_t (*read_complete)(void **inst, void *read_txn);
    int64_t (*write_begin)(void **inst, void **write_txn);
    int64_t (*write_commit)(void **inst, void *write_txn);
    int64_t (*add)(void **inst, void *write_txn, uint64_t *key, void *value);
    int64_t (*search)(void **inst, void *read_txn, uint64_t *key, void **value_out);
    int64_t (*delete)(void **inst, void *write_txn, uint64_t *key);
    int64_t (*destroy)(void **inst);
};

struct thread_info {
    uint64_t iter;
    size_t tid;
    struct b_tree_cow_cb *ds;
    size_t batchsize;
    void (*op)(struct thread_info *info);
    uint32_t write_delay;
    uint32_t read_delay;
    time_t sec;
    long nsec;
};

/* Wrappers and definitions */


int64_t generic_read_begin(void **inst, void **txn);
int64_t generic_read_complete(void **inst, void *txn);
int64_t generic_write_begin(void **inst, void **txn);
int64_t generic_write_commit(void **inst, void *txn);

int64_t bptree_init_wrapper(void **inst);
int64_t bptree_read_begin(void **inst, void **txn);
int64_t bptree_read_complete(void **inst, void *txn);
int64_t bptree_write_begin(void **inst, void **txn);
int64_t bptree_write_commit(void **inst, void *txn);
int64_t bptree_add_wrapper(void **inst, void *txn, uint64_t *key, void *value);
int64_t bptree_search_wrapper(void **inst, void *txn, uint64_t *key, void **value_out);
int64_t bptree_delete_wrapper(void **inst, void *txn, uint64_t *key);
int64_t bptree_destroy_wrapper(void **inst);

int64_t bptree_cow_init_wrapper(void **inst);
int64_t bptree_cow_read_begin(void **inst, void **read_txn);
int64_t bptree_cow_read_complete(void **inst, void *read_txn);
int64_t bptree_cow_write_begin(void **inst, void **write_txn);
int64_t bptree_cow_write_commit(void **inst, void *write_txn);
int64_t bptree_cow_add_wrapper(void **inst, void *write_txn, uint64_t *key, void *value);
int64_t bptree_cow_search_wrapper(void **inst, void *read_txn, uint64_t *key, void **value_out);
int64_t bptree_cow_delete_wrapper(void **inst, void *write_txn, uint64_t *key);
int64_t bptree_cow_destroy_wrapper(void **inst);

/* Hash map structures */
int64_t hash_small_init_wrapper(void **inst);
int64_t hash_med_init_wrapper(void **inst);
int64_t hash_large_init_wrapper(void **inst);
int64_t hash_add_wrapper(void **inst, void *write_txn, uint64_t *key, void *value);
int64_t hash_search_wrapper(void **inst, void *read_txn, uint64_t *key, void **value_out);
int64_t hash_delete_wrapper(void **inst, void *write_txn, uint64_t *key);
int64_t hash_destroy_wrapper(void **inst);
