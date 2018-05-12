/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"
#include "benchmark_par.h"
#include <plhash.h>
#include <stdio.h>
#include <unistd.h>

/* Wrappers */

struct b_tree_cow_cb bptree_test = {
    "sds b+tree without search csum", /* name */
    NULL,                             /* inst */
    bptree_init_wrapper,              /* init */
    generic_read_begin,               /* read_begin */
    generic_read_complete,            /* read_complete */
    generic_write_begin,              /* write_begin */
    generic_write_commit,             /* write commit */
    bptree_add_wrapper,               /* add */
    bptree_search_wrapper,            /* search */
    bptree_delete_wrapper,            /* delete */
    bptree_destroy_wrapper,           /* destroy inst */
};

struct b_tree_cow_cb htree_test = {
    "sds htree",           /* name */
    NULL,                  /* inst */
    htree_init_wrapper,    /* init */
    generic_read_begin,    /* read_begin */
    generic_read_complete, /* read_complete */
    generic_write_begin,   /* write_begin */
    generic_write_commit,  /* write commit */
    htree_add_wrapper,     /* add */
    htree_search_wrapper,  /* search */
    htree_delete_wrapper,  /* delete */
    htree_destroy_wrapper, /* destroy inst */
};

struct b_tree_cow_cb bptree_cow_test = {
    "sds b+tree with copy on write", /* name */
    NULL,                            /* inst */
    bptree_cow_init_wrapper,         /* init */
    bptree_cow_read_begin,           /* read_begin */
    bptree_cow_read_complete,        /* read_complete */
    bptree_cow_write_begin,          /* write_begin */
    bptree_cow_write_commit,         /* write commit */
    bptree_cow_add_wrapper,          /* add */
    bptree_cow_search_wrapper,       /* search */
    bptree_cow_delete_wrapper,       /* delete */
    bptree_cow_destroy_wrapper,      /* destroy inst */
};

struct b_tree_cow_cb hash_small_cb_test = {
    "pl hashmap small",
    NULL,
    hash_small_init_wrapper,
    generic_read_begin,    /* read_begin */
    generic_read_complete, /* read_complete */
    generic_write_begin,   /* write_begin */
    generic_write_commit,  /* write commit */
    hash_add_wrapper,
    hash_search_wrapper,
    hash_delete_wrapper,
    hash_destroy_wrapper,
};

struct b_tree_cow_cb hash_med_cb_test = {
    "pl hashmap med",
    NULL,
    hash_med_init_wrapper,
    generic_read_begin,    /* read_begin */
    generic_read_complete, /* read_complete */
    generic_write_begin,   /* write_begin */
    generic_write_commit,  /* write commit */
    hash_add_wrapper,
    hash_search_wrapper,
    hash_delete_wrapper,
    hash_destroy_wrapper,
};

struct b_tree_cow_cb hash_large_cb_test = {
    "pl hashmap large",
    NULL,
    hash_large_init_wrapper,
    generic_read_begin,    /* read_begin */
    generic_read_complete, /* read_complete */
    generic_write_begin,   /* write_begin */
    generic_write_commit,  /* write commit */
    hash_add_wrapper,
    hash_search_wrapper,
    hash_delete_wrapper,
    hash_destroy_wrapper,
};

/* The benchmarks */

/* == Random and contended == */

void
batch_random(struct thread_info *info)
{
    // printf("tid %d\n", info->tid);

    void *read_txn = NULL;
    void *write_txn = NULL;

    size_t cf = 0;
    size_t step = 0;
    size_t current_step = 0;
    size_t max_factors = info->iter / 2048;
    size_t baseid = 0;
    void *output;
    /* Give ourselves a unique base id */
    for (size_t i = 0; i < info->tid; i++) {
        baseid += 50000;
    }

    /* Now start stepping, and randomly deleting */
    for (step = 0; step < (info->iter * 50); step++) {
        current_step = step + fill_pattern[step % 2048];
        size_t mod = current_step % 10;
        cf = step % max_factors;
        uint64_t target = fill_pattern[step % 2048] + (2048 << cf) + baseid;

        if (mod >= 8) {
            info->ds->write_begin(&(info->ds->inst), &write_txn);
            info->ds->delete (&(info->ds->inst), write_txn, &target);
            if (info->write_delay != 0) {
                usleep(info->write_delay);
            }
            info->ds->write_commit(&(info->ds->inst), write_txn);
        } else if (mod >= 6) {
            info->ds->write_begin(&(info->ds->inst), &write_txn);
            info->ds->add(&(info->ds->inst), write_txn, &target, NULL);
            if (info->write_delay != 0) {
                usleep(info->write_delay);
            }
            info->ds->write_commit(&(info->ds->inst), write_txn);
        } else {
            info->ds->read_begin(&(info->ds->inst), &read_txn);
            info->ds->search(&(info->ds->inst), read_txn, &target, &output);
            if (info->read_delay != 0) {
                usleep(info->read_delay);
            }
            info->ds->read_complete(&(info->ds->inst), read_txn);
        }
    }

    // printf("tid %d complete\n", info->tid);
}

void
batch_search(struct thread_info *info)
{
    void *read_txn = NULL;
    size_t cf = 0;
    size_t step = 0;
    void *output;

    for (step = 0; step < (info->iter * 100); step++) {
        info->ds->read_begin(&(info->ds->inst), &read_txn);
        for (size_t j = 0; j < info->batchsize; j++) {
            cf = (step * info->tid) % 2048;
            uint64_t target = fill_pattern[cf] + j;

            info->ds->search(&(info->ds->inst), read_txn, &target, &output);
        }
        if (info->read_delay != 0) {
            usleep(info->read_delay);
        }
        info->ds->read_complete(&(info->ds->inst), read_txn);
    }
}


void
batch_insert(struct thread_info *info)
{
    void *write_txn = NULL;

    size_t cf = 0;
    size_t step = 0;
    size_t max_factors = info->iter / 2048;
    size_t baseid = 0;
    /* Give ourselves a unique base id */
    for (size_t i = 0; i < info->tid; i++) {
        baseid += 50000;
    }
    for (step = 0; step < (info->iter * 50); step++) {
        info->ds->write_begin(&(info->ds->inst), &write_txn);
        for (size_t j = 0; j < 10; j++) {
            cf = step % max_factors;
            uint64_t target = fill_pattern[step % 2048] + (2048 << cf) + baseid;
            info->ds->add(&(info->ds->inst), write_txn, &target, NULL);
        }
        if (info->write_delay != 0) {
            usleep(info->write_delay);
        }
        info->ds->write_commit(&(info->ds->inst), write_txn);
    }
}

void
batch_delete(struct thread_info *info)
{
    void *write_txn = NULL;

    size_t cf = 0;
    size_t step = 0;
    size_t max_factors = info->iter / 2048;
    size_t baseid = 0;
    /* Give ourselves a unique base id */
    for (size_t i = 0; i < info->tid; i++) {
        baseid += 50000;
    }
    /* Now start stepping, and randomly deleting */
    for (step = 0; step < (info->iter * 50); step++) {
        info->ds->write_begin(&(info->ds->inst), &write_txn);
        for (size_t j = 0; j < 10; j++) {
            cf = step % max_factors;
            uint64_t target = fill_pattern[step % 2048] + (2048 << cf) + baseid;
            info->ds->delete (&(info->ds->inst), write_txn, &target);
        }
        if (info->write_delay != 0) {
            usleep(info->write_delay);
        }
        info->ds->write_commit(&(info->ds->inst), write_txn);
    }
}

void
bench_thread_batch(void *arg)
{
    struct thread_info *info = (struct thread_info *)arg;
    // printf("tid %d\n", info->tid);
    struct timespec start_time;
    struct timespec finish_time;

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    info->op(info);

    // stop time
    clock_gettime(CLOCK_MONOTONIC, &finish_time);
    // printf("tid %d complete\n", info->tid);
    info->sec = finish_time.tv_sec - start_time.tv_sec;
    info->nsec = finish_time.tv_nsec - start_time.tv_nsec;

    if (info->nsec < 0) {
        // It's negative so take one second
        info->sec -= 1;
        // And set nsec to to a whole value
        info->nsec = 1000000000 - info->nsec;
    }
}

void
batch_test_run(struct b_tree_cow_cb *ds, uint64_t iter, char *name, struct thread_info *info, size_t info_count)
{
    PRThread *t[80] = {0};


    printf("BENCH: Start ...\n");

    // This launches a number of threads, and waits for them to join.
    // Need some kind of thread struct to pass to it?

    for (size_t i = 0; i < info_count; i++) {
        t[i] = PR_CreateThread(PR_USER_THREAD, bench_thread_batch, (void *)&(info[i]),
                               PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
        assert_ptr_not_equal(t[i], NULL);
    }

    for (size_t i = 0; i < info_count; i++) {
        assert_int_equal(PR_JoinThread(t[i]), PR_SUCCESS);
    }

    printf("BENCH: Complete ... \n");

    printf("BENCH: %s %s %" PRIu64 " times\n", ds->name, name, iter);
    for (size_t i = 0; i < info_count; i++) {
        printf("      tid %" PRIu64 ": %" PRId64 ".%010" PRId64 "\n", info[i].tid, info[i].sec, info[i].nsec);
    }
}

/* Batch running */

static void
populate_ds(struct b_tree_cow_cb *ds, uint64_t iter)
{
    void *write_txn;

    size_t max_factors = iter / 2048;

    size_t count = 0;

    // Inject a number of elements first, just so that search and delete have
    // something to work with. we won't bench this.
    ds->write_begin(&(ds->inst), &write_txn);
    for (size_t j = 0; j < max_factors; j++) {
        for (size_t i = 0; i < 2048; i++) {
            uint64_t target = fill_pattern[i] + (2048 * j);
            ds->add(&(ds->inst), write_txn, &target, NULL);
            count++;
        }
    }
    printf("Added %" PRIu64 "\n", count);
    ds->write_commit(&(ds->inst), write_txn);
}

/* Now we can just construct some generic tests. */

void
bench_insert_search_delete_batch(struct b_tree_cow_cb *ds, uint64_t iter)
{
    struct thread_info info[10] = {{0}};

    ds->init(&(ds->inst));

    populate_ds(ds, iter);

    size_t i = 0;
    for (; i < 3; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 10;
    }
    for (; i < 5; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 10;
    }
    for (; i < 10; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 10;
    }

    batch_test_run(ds, iter, "bench_insert_search_delete_batch", info, 10);

    ds->destroy(&(ds->inst));
}

void
bench_insert_search_delete_random(struct b_tree_cow_cb *ds, uint64_t iter)
{
    struct thread_info info[10] = {{0}};
    ds->init(&(ds->inst));

    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 10; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_random;
        info[i].batchsize = 10;
    }
    batch_test_run(ds, iter, "bench_insert_search_delete_random", info, 10);

    ds->destroy(&(ds->inst));
}

void
bench_isd_write_delay(struct b_tree_cow_cb *ds, uint64_t iter)
{
    struct thread_info info[10] = {{0}};
    ds->init(&(ds->inst));
    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 2; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 10;
        info[i].write_delay = 1;
    }
    for (; i < 3; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 10;
        info[i].write_delay = 1;
    }
    for (; i < 10; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 10;
        info[i].write_delay = 1;
    }

    batch_test_run(ds, iter, "bench_isd_write_delay", info, 10);

    ds->destroy(&(ds->inst));
}

void
bench_isd_read_delay(struct b_tree_cow_cb *ds, uint64_t iter)
{
    struct thread_info info[10] = {{0}};
    ds->init(&(ds->inst));
    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 2; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 10;
        info[i].read_delay = 1;
    }
    for (; i < 3; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 10;
        info[i].read_delay = 1;
    }
    for (; i < 10; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 10;
        info[i].read_delay = 1;
    }

    batch_test_run(ds, iter, "bench_isd_read_delay", info, 10);

    ds->destroy(&(ds->inst));
}

void
bench_high_thread_small_batch_read(struct b_tree_cow_cb *ds, uint64_t iter)
{
    /* DS can scale to many threads, so simulate high contention. */
    struct thread_info info[80] = {{0}};
    ds->init(&(ds->inst));
    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 10; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 5;
    }
    for (; i < 20; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 5;
    }
    for (; i < 80; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 5;
    }

    batch_test_run(ds, iter, "bench_high_thread_small_batch_read", info, 80);

    ds->destroy(&(ds->inst));
}

void
bench_high_thread_large_batch_read(struct b_tree_cow_cb *ds, uint64_t iter)
{
    /* DS can scale to many threads, so simulate high contention. */
    struct thread_info info[80] = {{0}};
    ds->init(&(ds->inst));
    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 10; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 64;
    }
    for (; i < 20; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 64;
    }
    for (; i < 80; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 64;
    }

    batch_test_run(ds, iter, "bench_high_thread_large_batch_read", info, 80);

    ds->destroy(&(ds->inst));
}

void
bench_high_thread_small_batch_write(struct b_tree_cow_cb *ds, uint64_t iter)
{
    /* DS can scale to many threads, so simulate high contention. */
    struct thread_info info[80] = {{0}};
    ds->init(&(ds->inst));
    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 30; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 5;
    }
    for (; i < 50; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 5;
    }
    for (; i < 80; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 5;
    }

    batch_test_run(ds, iter, "bench_high_thread_small_batch_write", info, 80);

    ds->destroy(&(ds->inst));
}

void
bench_high_thread_large_batch_write(struct b_tree_cow_cb *ds, uint64_t iter)
{
    /* DS can scale to many threads, so simulate high contention. */
    struct thread_info info[80] = {{0}};
    ds->init(&(ds->inst));
    populate_ds(ds, iter);
    size_t i = 0;
    for (; i < 30; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_insert;
        info[i].batchsize = 64;
    }
    for (; i < 50; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_delete;
        info[i].batchsize = 64;
    }
    for (; i < 80; i++) {
        info[i].iter = iter;
        info[i].tid = i;
        info[i].ds = ds;
        info[i].op = batch_search;
        info[i].batchsize = 64;
    }

    batch_test_run(ds, iter, "bench_high_thread_large_batch_write", info, 80);

    ds->destroy(&(ds->inst));
}

/* End tests */

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{

    uint64_t test_arrays[] = {5000, 10000, 100000, 500000, 1000000, 2500000, 5000000, 10000000};

    for (size_t i = 0; i < 1; i++) {
        /*
        bench_insert_search_delete_batch(&hash_small_cb_test, test_arrays[i]);
        bench_insert_search_delete_batch(&hash_med_cb_test, test_arrays[i]);
        */
        bench_insert_search_delete_batch(&hash_large_cb_test, test_arrays[i]);
        bench_insert_search_delete_batch(&bptree_test, test_arrays[i]);
        bench_insert_search_delete_batch(&bptree_cow_test, test_arrays[i]);
        bench_insert_search_delete_batch(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_isd_write_delay(&hash_small_cb_test, test_arrays[i]);
        bench_isd_write_delay(&hash_med_cb_test, test_arrays[i]);
        */
        bench_isd_write_delay(&hash_large_cb_test, test_arrays[i]);
        bench_isd_write_delay(&bptree_test, test_arrays[i]);
        bench_isd_write_delay(&bptree_cow_test, test_arrays[i]);
        bench_isd_write_delay(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_isd_read_delay(&hash_small_cb_test, test_arrays[i]);
        bench_isd_read_delay(&hash_med_cb_test, test_arrays[i]);
        */
        bench_isd_read_delay(&hash_large_cb_test, test_arrays[i]);
        bench_isd_read_delay(&bptree_test, test_arrays[i]);
        bench_isd_read_delay(&bptree_cow_test, test_arrays[i]);
        bench_isd_read_delay(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_high_thread_small_batch_read(&hash_small_cb_test, test_arrays[i]);
        bench_high_thread_small_batch_read(&hash_med_cb_test, test_arrays[i]);
        */
        bench_high_thread_small_batch_read(&hash_large_cb_test, test_arrays[i]);
        bench_high_thread_small_batch_read(&bptree_test, test_arrays[i]);
        bench_high_thread_small_batch_read(&bptree_cow_test, test_arrays[i]);
        bench_high_thread_small_batch_read(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_high_thread_large_batch_read(&hash_small_cb_test, test_arrays[i]);
        bench_high_thread_large_batch_read(&hash_med_cb_test, test_arrays[i]);
        */
        bench_high_thread_large_batch_read(&hash_large_cb_test, test_arrays[i]);
        bench_high_thread_large_batch_read(&bptree_test, test_arrays[i]);
        bench_high_thread_large_batch_read(&bptree_cow_test, test_arrays[i]);
        bench_high_thread_large_batch_read(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_high_thread_small_batch_write(&hash_small_cb_test, test_arrays[i]);
        bench_high_thread_small_batch_write(&hash_med_cb_test, test_arrays[i]);
        */
        bench_high_thread_small_batch_write(&hash_large_cb_test, test_arrays[i]);
        bench_high_thread_small_batch_write(&bptree_test, test_arrays[i]);
        bench_high_thread_small_batch_write(&bptree_cow_test, test_arrays[i]);
        bench_high_thread_small_batch_write(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_high_thread_large_batch_write(&hash_small_cb_test, test_arrays[i]);
        bench_high_thread_large_batch_write(&hash_med_cb_test, test_arrays[i]);
        */
        bench_high_thread_large_batch_write(&hash_large_cb_test, test_arrays[i]);
        bench_high_thread_large_batch_write(&bptree_test, test_arrays[i]);
        bench_high_thread_large_batch_write(&bptree_cow_test, test_arrays[i]);
        bench_high_thread_large_batch_write(&htree_test, test_arrays[i]);
        printf("---\n");

        /*
        bench_insert_search_delete_random(&hash_small_cb_test, test_arrays[i]);
        bench_insert_search_delete_random(&hash_med_cb_test, test_arrays[i]);
        */
        bench_insert_search_delete_random(&hash_large_cb_test, test_arrays[i]);
        bench_insert_search_delete_random(&bptree_test, test_arrays[i]);
        bench_insert_search_delete_random(&bptree_cow_test, test_arrays[i]);
        bench_insert_search_delete_random(&htree_test, test_arrays[i]);
        printf("---\n");
    }
}
