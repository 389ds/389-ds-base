/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"
#include "benchmark.h"
#include <avl.h>
#include <plhash.h>
#include <stdio.h>

/* Implement a set of generic benchmarks that use callbacks to get to our tree */

/* AVL wrappers */

int
avl_intcmp(uint64_t *a, uint64_t *b)
{
    if (*a < *b) {
        return -1;
    } else if (*a > *b) {
        return 1;
    }
    return 0;
}

uint64_t *
avl_intdup(uint64_t *a)
{
    uint64_t *b = malloc(sizeof(uint64_t));
    *b = *a;
    return b;
}

int64_t
avl_init_wrapper(void **inst)
{
    Avlnode **tree = (Avlnode **)inst;
    *tree = NULLAVL;
    return 0;
}

int64_t
avl_add_wrapper(void **inst, uint64_t *key, void *value __attribute__((unused)))
{
    Avlnode **tree = (Avlnode **)inst;
    return avl_insert(tree, avl_intdup(key), avl_intcmp, avl_dup_error);
}

int64_t
avl_search_wrapper(void **inst, uint64_t *key, void **value_out)
{
    Avlnode **tree = (Avlnode **)inst;
    *value_out = avl_find(*tree, key, avl_intcmp);
    if (*value_out == NULL) {
        return 1;
    }
    return 0;
}

int64_t
avl_delete_wrapper(void **inst, uint64_t *key)
{
    void *value_out = NULL;
    Avlnode **tree = (Avlnode **)inst;
    /* delete is broken :(  */
    value_out = avl_find(*tree, key, avl_intcmp);
    if (value_out == NULL) {
        return 1;
    }
    return 0;
}

int64_t
avl_destroy_wrapper(void **inst)
{
    Avlnode **tree = (Avlnode **)inst;
    (void)avl_free(*tree, (IFP)free);
    return 0;
}

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
hash_func_large(const void *key)
{
    uint64_t ik = *(uint64_t *)key;
    return ik % HASH_BUCKETS_LARGE;
}

PLHashNumber
hash_func_med(const void *key)
{
    uint64_t ik = *(uint64_t *)key;
    return ik % HASH_BUCKETS_MED;
}

PLHashNumber
hash_func_small(const void *key)
{
    uint64_t ik = *(uint64_t *)key;
    return ik % HASH_BUCKETS_SMALL;
}

PRIntn
hash_key_compare(const void *a, const void *b)
{
    uint64_t ia = *(uint64_t *)a;
    uint64_t ib = *(uint64_t *)b;
    return ia == ib;
}

PRIntn
hash_value_compare(const void *a __attribute__((unused)), const void *b __attribute__((unused)))
{
    // This cheats and says they are always differnt, but I don't think I use this ....
    return 1;
}

int64_t
hash_small_init_wrapper(void **inst)
{
    PLHashTable **table = (PLHashTable **)inst;

    *table = PL_NewHashTable(HASH_BUCKETS_SMALL, hash_func_small, hash_key_compare, hash_value_compare, NULL, NULL);
    return 0;
}

int64_t
hash_med_init_wrapper(void **inst)
{
    PLHashTable **table = (PLHashTable **)inst;

    *table = PL_NewHashTable(HASH_BUCKETS_MED, hash_func_med, hash_key_compare, hash_value_compare, NULL, NULL);
    return 0;
}

int64_t
hash_large_init_wrapper(void **inst)
{
    PLHashTable **table = (PLHashTable **)inst;

    *table = PL_NewHashTable(HASH_BUCKETS_LARGE, hash_func_large, hash_key_compare, hash_value_compare, NULL, NULL);
    return 0;
}

int64_t
hash_add_wrapper(void **inst, uint64_t *key, void *value __attribute__((unused)))
{
    PLHashTable **table = (PLHashTable **)inst;
    // WARNING: We have to add key as value too else hashmap won't add it!!!
    uint64_t *i = avl_intdup(key);
    PL_HashTableAdd(*table, (void *)i, (void *)i);
    return 0;
}

int64_t
hash_search_wrapper(void **inst, uint64_t *key, void **value_out)
{
    PLHashTable **table = (PLHashTable **)inst;
    *value_out = PL_HashTableLookup(*table, (void *)key);
    if (*value_out == NULL) {
        return 1;
    }
    return 0;
}

int64_t
hash_delete_wrapper(void **inst, uint64_t *key)
{
    PLHashTable **table = (PLHashTable **)inst;
    PL_HashTableRemove(*table, (void *)key);
    return 0;
}

int64_t
hash_destroy_wrapper(void **inst)
{
    PLHashTable **table = (PLHashTable **)inst;
    PL_HashTableDestroy(*table);
    return 0;
}

/* The sds b+ tree wrappers */

int64_t
bptree_csum_init_wrapper(void **inst)
{
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_bptree_init(binst, 1, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);
    return 0;
}

int64_t
bptree_init_wrapper(void **inst)
{
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_bptree_init(binst, 0, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);
    return 0;
}

int64_t
bptree_add_wrapper(void **inst, uint64_t *key, void *value)
{
    // THIS WILL BREAK SOMETIME!
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_bptree_insert(*binst, (void *)key, value);
    return 0;
}

int64_t
bptree_search_wrapper(void **inst, uint64_t *key, void **value_out __attribute__((unused)))
{
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_result result = sds_bptree_search(*binst, (void *)key);
    if (result != SDS_KEY_PRESENT) {
        // printf("search result is %d\n", result);
        return 1;
    }
    return 0;
}

int64_t
bptree_delete_wrapper(void **inst, uint64_t *key)
{
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    sds_result result = sds_bptree_delete(*binst, (void *)key);

    if (result != SDS_KEY_PRESENT) {
        // printf("delete result is %d\n", result);
        return 1;
    }
    return 0;
}

int64_t
bptree_destroy_wrapper(void **inst)
{
    sds_bptree_instance **binst = (sds_bptree_instance **)inst;
    // sds_bptree_display(*binst);
    sds_bptree_destroy(*binst);
    return 0;
}

/* sds bptree cow wrapper */

int64_t
bptree_cow_init_wrapper(void **inst)
{
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_cow_init(binst, 0, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_dup);
    return 0;
}

int64_t
bptree_cow_add_wrapper(void **inst, uint64_t *key, void *value)
{
    // THIS WILL BREAK SOMETIME!
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_bptree_cow_insert_atomic(*binst, (void *)key, value);
    return 0;
}

int64_t
bptree_cow_search_wrapper(void **inst, uint64_t *key, void **value_out __attribute__((unused)))
{
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_result result = sds_bptree_cow_search_atomic(*binst, (void *)key);
    if (result != SDS_KEY_PRESENT) {
        // printf("search result is %d\n", result);
        return 1;
    }
    return 0;
}

int64_t
bptree_cow_delete_wrapper(void **inst, uint64_t *key)
{
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    sds_result result = sds_bptree_cow_delete_atomic(*binst, (void *)key);
    if (result != SDS_KEY_PRESENT) {
        // printf("delete result is %d\n", result);
        return 1;
    }
    return 0;
}

int64_t
bptree_cow_destroy_wrapper(void **inst)
{
    sds_bptree_cow_instance **binst = (sds_bptree_cow_instance **)inst;
    // sds_bptree_display(*binst);
    sds_bptree_cow_destroy(*binst);
    return 0;
}

/* sds HTree wrapper */

int64_t
ht_init_wrapper(void **inst)
{
    sds_ht_instance **ht = (sds_ht_instance **)inst;
    sds_ht_init(ht, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_size);
    return 0;
}

int64_t
ht_add_wrapper(void **inst, uint64_t *key, void *value)
{
    sds_ht_instance **ht = (sds_ht_instance **)inst;
    sds_ht_insert(*ht, key, value);
    return 0;
}

int64_t
ht_search_wrapper(void **inst, uint64_t *key, void **value_out __attribute__((unused)))
{
    sds_ht_instance **ht = (sds_ht_instance **)inst;
    sds_result result = sds_ht_search(*ht, key, value_out);
    if (result != SDS_KEY_PRESENT) {
        return 1;
    }
    return 0;
}

int64_t
ht_delete_wrapper(void **inst, uint64_t *key)
{
    sds_ht_instance **ht = (sds_ht_instance **)inst;
    sds_result result = sds_ht_delete(*ht, key);
    if (result != SDS_KEY_PRESENT) {
        return 1;
    }
    return 0;
}

int64_t
ht_destroy_wrapper(void **inst)
{
    sds_ht_instance **ht = (sds_ht_instance **)inst;
    sds_ht_destroy(*ht);
    return 0;
}

/* The benchmarks */

void
bench_1_insert_seq(struct b_tree_cb *ds, uint64_t iter)
{

    struct timespec start_time;
    struct timespec finish_time;

    ds->init(&(ds->inst));

    printf("BENCH: Start ...\n");

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (uint64_t i = 1; i < iter; i++) {
        if (ds->add(&(ds->inst), &i, NULL) != 0) {
            printf("FAIL: Error inserting %" PRIu64 " to %s\n", i, ds->name);
            break;
        }
    }

    // stop time
    clock_gettime(CLOCK_MONOTONIC, &finish_time);

    printf("BENCH: Complete ...\n");

    // diff time
    time_t sec = finish_time.tv_sec - start_time.tv_sec;
    long nsec = finish_time.tv_nsec - start_time.tv_nsec;

    if (nsec < 0) {
        // It's negative so take one second
        sec -= 1;
        // And set nsec to to a whole value
        nsec = 1000000000 - nsec;
    }

    printf("BENCH: %s bench_1_insert_seq %" PRIu64 " time %" PRId64 ".%010" PRId64 "\n", ds->name, iter, sec, nsec);

    ds->destroy(&(ds->inst));
}

void
bench_2_search_seq(struct b_tree_cb *ds, uint64_t iter)
{


    struct timespec start_time;
    struct timespec finish_time;

    ds->init(&(ds->inst));

    printf("BENCH: Start ...\n");

    for (uint64_t i = 1; i < iter; i++) {
        if (ds->add(&(ds->inst), &i, NULL) != 0) {
            printf("FAIL: Error inserting %" PRIu64 " to %s\n", i, ds->name);
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    void *output;
    for (uint64_t j = 0; j < 100; j++) {
        for (uint64_t i = 1; i < iter; i++) {
            if (ds->search(&(ds->inst), &i, &output) != 0) {
                printf("FAIL: Error finding %" PRIu64 " in %s\n", i, ds->name);
                break;
            }
        }
    }

    // stop time
    clock_gettime(CLOCK_MONOTONIC, &finish_time);

    printf("BENCH: Complete ...\n");

    // diff time
    time_t sec = finish_time.tv_sec - start_time.tv_sec;
    long nsec = finish_time.tv_nsec - start_time.tv_nsec;

    if (nsec < 0) {
        // It's negative so take one second
        sec -= 1;
        // And set nsec to to a whole value
        nsec = 1000000000 - nsec;
    }

    printf("BENCH: %s bench_2_search_seq %" PRIu64 " time %" PRId64 ".%010" PRId64 "\n", ds->name, iter, sec, nsec);

    ds->destroy(&(ds->inst));
}


void
bench_3_delete_seq(struct b_tree_cb *ds, uint64_t iter)
{

    struct timespec start_time;
    struct timespec finish_time;

    ds->init(&(ds->inst));


    for (uint64_t i = 1; i < iter; i++) {
        if (ds->add(&(ds->inst), &i, NULL) != 0) {
            printf("FAIL: Error inserting %" PRIu64 " to %s\n", i, ds->name);
            break;
        }
    }

    printf("BENCH: Start ...\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (uint64_t i = 1; i < iter; i++) {
        if (ds->delete (&(ds->inst), &i) != 0) {
            printf("FAIL: Error deleting %" PRIu64 " from %s\n", i, ds->name);
            break;
        }
    }

    // stop time
    clock_gettime(CLOCK_MONOTONIC, &finish_time);

    printf("BENCH: Complete ...\n");

    // diff time
    time_t sec = finish_time.tv_sec - start_time.tv_sec;
    long nsec = finish_time.tv_nsec - start_time.tv_nsec;

    if (nsec < 0) {
        // It's negative so take one second
        sec -= 1;
        // And set nsec to to a whole value
        nsec = 1000000000 - nsec;
    }

    printf("BENCH: %s bench_3_delete_seq %" PRIu64 " time %" PRId64 ".%010" PRId64 "\n", ds->name, iter, sec, nsec);

    ds->destroy(&(ds->inst));
}

void
bench_4_insert_search_delete_random(struct b_tree_cb *ds, uint64_t iter)
{
    struct timespec start_time;
    struct timespec finish_time;

    size_t max_factors = iter / 2048;

    size_t step = 0;
    size_t cf = 0;
    uint64_t current_step;
    void *output;

    printf("BENCH: Start ...\n");
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    ds->init(&(ds->inst));

    /* First, load part of our tree in. */

    for (size_t j = 0; j < max_factors; j++) {
        for (size_t i = 0; i < 2048; i++) {
            uint64_t x = fill_pattern[i] + (2048 << j);
            ds->add(&(ds->inst), &x, NULL);
        }
    }

    /* Now start stepping, and randomly deleting */
    for (step = 0; step < (iter * 10); step++) {
        current_step = step + fill_pattern[step % 2048];
        int mod = current_step % 10;
        cf = step % max_factors;

        uint64_t x = fill_pattern[step % 2048] + (2048 << cf);
        if (mod >= 8) {
            ds->delete (&(ds->inst), &x);
        } else if (mod >= 6) {
            ds->add(&(ds->inst), &x, NULL);
        } else {
            ds->search(&(ds->inst), &x, &output);
        }
    }

    // stop time
    clock_gettime(CLOCK_MONOTONIC, &finish_time);

    printf("BENCH: Complete ... \n");

    // diff time
    time_t sec = finish_time.tv_sec - start_time.tv_sec;
    long nsec = finish_time.tv_nsec - start_time.tv_nsec;

    if (nsec < 0) {
        // It's negative so take one second
        sec -= 1;
        // And set nsec to to a whole value
        nsec = 1000000000 - nsec;
    }

    printf("BENCH: %s bench_4_insert_search_delete_random %" PRIu64 " time %" PRId64 ".%010" PRId64 "\n", ds->name, iter, sec, nsec);

    ds->destroy(&(ds->inst));
}

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
    /* Setup the avl tree. */
    struct b_tree_cb avl_test = {0};
    avl_test.name = "avl tree";
    avl_test.inst = NULL;
    avl_test.init = avl_init_wrapper;
    avl_test.add = avl_add_wrapper;
    avl_test.search = avl_search_wrapper;
    avl_test.delete = avl_delete_wrapper;
    avl_test.destroy = avl_destroy_wrapper;

    struct b_tree_cb hash_large_test = {0};
    hash_large_test.name = "pl hashmap large";
    hash_large_test.inst = NULL;
    hash_large_test.init = hash_large_init_wrapper;
    hash_large_test.add = hash_add_wrapper;
    hash_large_test.search = hash_search_wrapper;
    hash_large_test.delete = hash_delete_wrapper;
    hash_large_test.destroy = hash_destroy_wrapper;

    struct b_tree_cb hash_med_test = {0};
    hash_med_test.name = "pl hashmap medium";
    hash_med_test.inst = NULL;
    hash_med_test.init = hash_med_init_wrapper;
    hash_med_test.add = hash_add_wrapper;
    hash_med_test.search = hash_search_wrapper;
    hash_med_test.delete = hash_delete_wrapper;
    hash_med_test.destroy = hash_destroy_wrapper;

    struct b_tree_cb hash_small_test = {0};
    hash_small_test.name = "pl hashmap small";
    hash_small_test.inst = NULL;
    hash_small_test.init = hash_small_init_wrapper;
    hash_small_test.add = hash_add_wrapper;
    hash_small_test.search = hash_search_wrapper;
    hash_small_test.delete = hash_delete_wrapper;
    hash_small_test.destroy = hash_destroy_wrapper;

    struct b_tree_cb bptree_test = {0};
    bptree_test.name = "sds b+tree without search csum";
    bptree_test.inst = NULL;
    bptree_test.init = bptree_init_wrapper;
    bptree_test.add = bptree_add_wrapper;
    bptree_test.search = bptree_search_wrapper;
    bptree_test.delete = bptree_delete_wrapper;
    bptree_test.destroy = bptree_destroy_wrapper;

    struct b_tree_cb bptree_cow_test = {0};
    bptree_cow_test.name = "sds b+tree with copy on write";
    bptree_cow_test.inst = NULL;
    bptree_cow_test.init = bptree_cow_init_wrapper;
    bptree_cow_test.add = bptree_cow_add_wrapper;
    bptree_cow_test.search = bptree_cow_search_wrapper;
    bptree_cow_test.delete = bptree_cow_delete_wrapper;
    bptree_cow_test.destroy = bptree_cow_destroy_wrapper;

    struct b_tree_cb htree_test = {0};
    htree_test.name = "sds htree";
    htree_test.inst = NULL;
    htree_test.init = ht_init_wrapper;
    htree_test.add = ht_add_wrapper;
    htree_test.search = ht_search_wrapper;
    htree_test.delete = ht_delete_wrapper;
    htree_test.destroy = ht_destroy_wrapper;

    uint64_t test_arrays[] = {5000, 10000, 100000, 500000, 1000000, 2500000, 5000000, 10000000};

    for (size_t i = 0; i < 5; i++) {
        bench_1_insert_seq(&avl_test, test_arrays[i]);
        bench_1_insert_seq(&hash_small_test, test_arrays[i]);
        bench_1_insert_seq(&hash_med_test, test_arrays[i]);
        bench_1_insert_seq(&hash_large_test, test_arrays[i]);
        bench_1_insert_seq(&bptree_test, test_arrays[i]);
        bench_1_insert_seq(&bptree_cow_test, test_arrays[i]);
        bench_1_insert_seq(&htree_test, test_arrays[i]);
        printf("---\n");

        bench_2_search_seq(&avl_test, test_arrays[i]);
        if (test_arrays[i] < 500000) {
            bench_2_search_seq(&hash_small_test, test_arrays[i]);
        }
        if (test_arrays[i] < 1000000) {
            bench_2_search_seq(&hash_med_test, test_arrays[i]);
        }
        bench_2_search_seq(&hash_large_test, test_arrays[i]);
        bench_2_search_seq(&bptree_test, test_arrays[i]);
        bench_2_search_seq(&bptree_cow_test, test_arrays[i]);
        bench_2_search_seq(&htree_test, test_arrays[i]);
        printf("---\n");

        bench_3_delete_seq(&avl_test, test_arrays[i]);
        bench_3_delete_seq(&hash_small_test, test_arrays[i]);
        bench_3_delete_seq(&hash_med_test, test_arrays[i]);
        bench_3_delete_seq(&hash_large_test, test_arrays[i]);
        bench_3_delete_seq(&bptree_test, test_arrays[i]);
        bench_3_delete_seq(&bptree_cow_test, test_arrays[i]);
        bench_3_delete_seq(&htree_test, test_arrays[i]);
        printf("---\n");

        bench_4_insert_search_delete_random(&avl_test, test_arrays[i]);
        if (test_arrays[i] < 500000) {
            bench_4_insert_search_delete_random(&hash_small_test, test_arrays[i]);
        }
        if (test_arrays[i] < 1000000) {
            bench_4_insert_search_delete_random(&hash_med_test, test_arrays[i]);
        }
        bench_4_insert_search_delete_random(&hash_large_test, test_arrays[i]);
        bench_4_insert_search_delete_random(&bptree_test, test_arrays[i]);
        bench_4_insert_search_delete_random(&bptree_cow_test, test_arrays[i]);
        bench_4_insert_search_delete_random(&htree_test, test_arrays[i]);
        printf("---\n");

        printf("======\n");
    }
}
