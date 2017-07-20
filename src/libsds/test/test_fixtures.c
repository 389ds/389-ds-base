/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

int
bptree_test_setup(void **state)
{
    // Create a new tree, and then destroy it.
    sds_bptree_instance *binst = NULL;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(&binst, 1, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_free, sds_uint64_t_dup);

    assert_int_equal(result, SDS_SUCCESS);

    *state = binst;

    return 0;
}

int
bptree_str_test_setup(void **state)
{
    // Create a new tree, and then destroy it.
    sds_bptree_instance *binst = NULL;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(&binst, 1, sds_uint64_t_compare, sds_free, sds_uint64_t_free, sds_uint64_t_dup);

    assert_int_equal(result, SDS_SUCCESS);

    *state = binst;

    return 0;
}

int
bptree_test_teardown(void **state)
{
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_verify(binst);

#ifdef SDS_DEBUG
    if (result != SDS_SUCCESS) {
        sds_log("bptree_test_teardown", "FAIL: B+Tree verification failed %d binst", result);
    }
#endif

    assert_int_equal(result, SDS_SUCCESS);


    result = sds_bptree_destroy(binst);
    assert_int_equal(result, SDS_SUCCESS);

    return 0;
}

int
bptree_test_set_setup(void **state)
{
    sds_bptree_instance *binst_a = NULL;
    sds_bptree_instance *binst_b = NULL;
    sds_bptree_instance *binst_out = NULL;
    void **binst = sds_malloc(sizeof(void *) * 3);
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_init(&binst_a, 1, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_free, sds_uint64_t_dup);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_init(&binst_b, 1, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_free, sds_uint64_t_dup);
    assert_int_equal(result, SDS_SUCCESS);

    binst[0] = (void *)binst_a;
    binst[1] = (void *)binst_b;
    binst[2] = (void *)binst_out;

    *state = binst;

    return 0;
}

int
bptree_test_set_teardown(void **state)
{
    void **binst = *state;
    sds_bptree_instance *binst_a = (sds_bptree_instance *)binst[0];
    sds_bptree_instance *binst_b = (sds_bptree_instance *)binst[1];
    sds_bptree_instance *binst_out = (sds_bptree_instance *)binst[2];

    sds_result result = SDS_SUCCESS;

    result = sds_bptree_destroy(binst_a);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_destroy(binst_b);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_destroy(binst_out);
    assert_int_equal(result, SDS_SUCCESS);

    sds_free(binst);

    return 0;
}

int
bptree_test_cow_setup(void **state)
{
    // Create a new tree, and then destroy it.
    sds_bptree_cow_instance *binst = NULL;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_cow_init(&binst, 1, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_dup);

    assert_int_equal(result, SDS_SUCCESS);

    *state = binst;

    return 0;
}

int
bptree_test_cow_teardown(void **state)
{
    sds_bptree_cow_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    result = sds_bptree_cow_verify(binst);

#ifdef SDS_DEBUG
    if (result != SDS_SUCCESS) {
        sds_log("bptree_test_teardown", "FAIL: B+Tree COW verification failed %d binst", result);
    }
#endif

    assert_int_equal(result, SDS_SUCCESS);

    result = sds_bptree_cow_destroy(binst);
    assert_int_equal(result, SDS_SUCCESS);

    return 0;
}

int
queue_test_setup(void **state)
{
    sds_queue *q = NULL;
    sds_result result = SDS_SUCCESS;
    result = sds_queue_init(&q, NULL);
    assert_int_equal(result, SDS_SUCCESS);
    *state = q;
    return 0;
}

int
queue_test_teardown(void **state)
{
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_queue_destroy(q);
    assert_int_equal(result, SDS_SUCCESS);
    return 0;
}

int
tqueue_test_setup(void **state)
{
    sds_tqueue *q = NULL;
    sds_result result = SDS_SUCCESS;
    result = sds_tqueue_init(&q, NULL);
    assert_int_equal(result, SDS_SUCCESS);
    *state = q;
    return 0;
}

int
tqueue_test_teardown(void **state)
{
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_tqueue_destroy(q);
    assert_int_equal(result, SDS_SUCCESS);
    return 0;
}

int
lqueue_test_setup(void **state)
{
    sds_lqueue *q = NULL;
    sds_result result = SDS_SUCCESS;
    result = sds_lqueue_init(&q, NULL);
    assert_int_equal(result, SDS_SUCCESS);
    *state = q;
    return 0;
}

int
lqueue_test_teardown(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_lqueue_destroy(q);
    assert_int_equal(result, SDS_SUCCESS);
    return 0;
}

int
ht_test_setup(void **state)
{
    sds_ht_instance *ht = NULL;
    sds_result result = SDS_SUCCESS;
    result = sds_ht_init(&ht, sds_uint64_t_compare, sds_uint64_t_free, sds_uint64_t_dup, sds_uint64_t_free, sds_uint64_t_size);
    assert_true(result == SDS_SUCCESS);
    *state = ht;
    return 0;
}

int
ht_test_teardown(void **state)
{
    sds_ht_instance *ht = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_ht_verify(ht);
    assert_true(result == SDS_SUCCESS);
    result = sds_ht_destroy(ht);
    assert_true(result == SDS_SUCCESS);
    return 0;
}
