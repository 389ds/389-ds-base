/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

static void
test_1_lqueue_invalid_create(void **state __attribute__((unused)))
{
    sds_result result = SDS_SUCCESS;
    result = sds_lqueue_init(NULL, NULL);
    assert_int_equal(result, SDS_NULL_POINTER);
}

static void
test_2_lqueue_enqueue(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    result = sds_lqueue_enqueue(q, (void *)1);
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_3_lqueue_enqueue_multiple(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    result = sds_lqueue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);

    for (uint64_t i = 1; i < 100; i++) {
        result = sds_lqueue_enqueue(q, (void *)i);
        assert_int_equal(result, SDS_SUCCESS);
    }
}

static void
test_4_lqueue_invalid_dequeue(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    void *ptr = NULL;
    /* Attempt a dequeue on a list with no elements. */
    result = sds_lqueue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_LIST_EXHAUSTED);
    /* Attempt a dequeue on a list with an element, but null ptr */
    result = sds_lqueue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_lqueue_dequeue(q, NULL);
    assert_int_equal(result, SDS_NULL_POINTER);
}

static void
test_5_lqueue_dequeue(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    void *ptr = NULL;
    result = sds_lqueue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    /* Attempt a dequeue on a list with 1 element. */
    result = sds_lqueue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_equal(ptr, NULL);
    /* Attempt a dequeue on a list with no elements. */
    result = sds_lqueue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_LIST_EXHAUSTED);
}

static void
test_6_lqueue_dequeue_multiple(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    uint64_t *ptr = NULL;

    for (uint64_t i = 0; i < 100; i++) {
        ptr = sds_malloc(sizeof(uint64_t));
        *ptr = i;
        result = sds_lqueue_enqueue(q, (void *)ptr);
        assert_int_equal(result, SDS_SUCCESS);
    }
    for (uint64_t i = 0; i < 99; i++) {
        result = sds_lqueue_dequeue(q, (void **)&ptr);
        assert_int_equal(result, SDS_SUCCESS);
        assert_int_equal(*ptr, i);
        sds_free(ptr);
    }
    /* Attempt a dequeue on a list with 1 element. */
    result = sds_lqueue_dequeue(q, (void **)&ptr);
    assert_int_equal(result, SDS_SUCCESS);
    assert_int_equal(*ptr, 99);
    sds_free(ptr);
}

static void
test_7_lqueue_random(void **state)
{
    sds_lqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    uint64_t ptr = 0;

    for (size_t i = 0; i < 500; i++) {
        if (fill_pattern[i] % 2 != 0) {
            result = sds_lqueue_dequeue(q, (void **)&ptr);
            if (result != SDS_SUCCESS && result != SDS_LIST_EXHAUSTED) {
                assert_null(1);
            }
        } else {
            result = sds_lqueue_enqueue(q, (void *)fill_pattern[i]);
            assert_int_equal(result, SDS_SUCCESS);
        }
    }
}

static void
test_8_lqueue_implicit_free(void **state __attribute__((unused)))
{
    sds_result result = SDS_SUCCESS;
    sds_lqueue *q = NULL;
    result = sds_lqueue_init(&q, sds_free);
    assert_int_equal(result, SDS_SUCCESS);

    /* Get the queue ready on this thread. */
    assert_int_equal(sds_lqueue_tprep(q), SDS_SUCCESS);

    void *ptr = sds_malloc(8);
    result = sds_lqueue_enqueue(q, ptr);
    assert_int_equal(result, SDS_SUCCESS);
    /* We destroy the queue after enqueue, and it will trigger the free */
    result = sds_lqueue_destroy(q);
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_9_lqueue_thread(void *arg)
{
    void **state = (void **)arg;
    test_7_lqueue_random(state);
}

static void
test_9_lqueue_parallel_stress(void **state)
{
    PRThread *t[8] = {0};

    /* Just launch the threads */
    for (size_t i = 0; i < 8; i++) {
        t[i] = PR_CreateThread(PR_USER_THREAD, test_9_lqueue_thread, (void *)state,
                               PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
        assert_ptr_not_equal(t[i], NULL);
    }

    /* Now wait .... */
    for (size_t i = 0; i < 8; i++) {
        assert_int_equal(PR_JoinThread(t[i]), PR_SUCCESS);
    }
}

int
run_lqueue_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_1_lqueue_invalid_create),
        cmocka_unit_test_setup_teardown(test_2_lqueue_enqueue,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_3_lqueue_enqueue_multiple,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_4_lqueue_invalid_dequeue,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_5_lqueue_dequeue,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_6_lqueue_dequeue_multiple,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_7_lqueue_random,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
        cmocka_unit_test(test_8_lqueue_implicit_free),
        cmocka_unit_test_setup_teardown(test_9_lqueue_parallel_stress,
                                        lqueue_test_setup,
                                        lqueue_test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
