/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

static void
test_1_tqueue_invalid_create(void **state __attribute__((unused))) {
    sds_result result = SDS_SUCCESS;
    result = sds_tqueue_init(NULL, NULL);
    assert_int_equal(result, SDS_NULL_POINTER);
}

static void
test_2_tqueue_enqueue(void **state) {
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_tqueue_enqueue(q, (void *)1);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_not_equal(q->uq->head, NULL);
    assert_ptr_not_equal(q->uq->tail, NULL);
}

static void
test_3_tqueue_enqueue_multiple(void **state) {
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;

    result = sds_tqueue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    /* Take a ref to the head */
    sds_queue_node *head = q->uq->head;

    for (uint64_t i = 1; i < 100; i++) {
        result = sds_tqueue_enqueue(q, (void *)i);
        assert_int_equal(result, SDS_SUCCESS);
        assert_ptr_equal(head, q->uq->head);
    }
}

static void
test_4_tqueue_invalid_dequeue(void **state) {
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    void *ptr = NULL;
    /* Attempt a dequeue on a list with no elements. */
    result = sds_tqueue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_LIST_EXHAUSTED);
    /* Attempt a dequeue on a list with an element, but null ptr */
    result = sds_tqueue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_tqueue_dequeue(q, NULL);
    assert_int_equal(result, SDS_NULL_POINTER);
}

static void
test_5_tqueue_dequeue(void **state) {
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    void *ptr = NULL;
    result = sds_tqueue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    /* Attempt a dequeue on a list with 1 element. */
    result = sds_tqueue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_equal(ptr, NULL);
    assert_ptr_equal(q->uq->head, NULL);
    assert_ptr_equal(q->uq->tail, NULL);
    /* Attempt a dequeue on a list with no elements. */
    result = sds_tqueue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_LIST_EXHAUSTED);
    assert_ptr_equal(q->uq->head, NULL);
    assert_ptr_equal(q->uq->tail, NULL);
}

static void
test_6_tqueue_dequeue_multiple(void **state) {
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t *ptr = NULL;

    for (uint64_t i = 0; i < 100; i++) {
        ptr = sds_malloc(sizeof(uint64_t));
        *ptr = i;
        result = sds_tqueue_enqueue(q, (void *)ptr);
        assert_int_equal(result, SDS_SUCCESS);
    }
    /* Take a ref to the tail */
    sds_queue_node *tail = q->uq->tail;
    for (uint64_t i = 0; i < 99; i++) {
        result = sds_tqueue_dequeue(q, (void **)&ptr);
        assert_int_equal(result, SDS_SUCCESS);
        assert_int_equal(*ptr, i);
        sds_free(ptr);
        assert_ptr_equal(tail, q->uq->tail);
    }
    /* Attempt a dequeue on a list with 1 element. */
    result = sds_tqueue_dequeue(q, (void **)&ptr);
    assert_int_equal(result, SDS_SUCCESS);
    assert_int_equal(*ptr, 99);
    sds_free(ptr);
    assert_ptr_equal(q->uq->head, NULL);
    assert_ptr_equal(q->uq->tail, NULL);

}

static void
test_7_tqueue_random(void **state) {
    sds_tqueue *q = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t ptr = 0;

    for (size_t i = 0; i < 500; i++) {
        if (fill_pattern[i] % 2 != 0) {
            result = sds_tqueue_dequeue(q, (void **)&ptr);
            if (result != SDS_SUCCESS && result != SDS_LIST_EXHAUSTED) {
                assert_null(1);
            }
        } else {
            result = sds_tqueue_enqueue(q, (void *)fill_pattern[i]);
            assert_int_equal(result, SDS_SUCCESS);
        }
    }
}

static void
test_8_tqueue_implicit_free(void **state __attribute__((unused))) {
    sds_result result = SDS_SUCCESS;
    sds_tqueue *q = NULL;
    result = sds_tqueue_init(&q, sds_free);
    assert_int_equal(result, SDS_SUCCESS);
    void *ptr = sds_malloc(8);
    result = sds_tqueue_enqueue(q, ptr);
    assert_int_equal(result, SDS_SUCCESS);
    /* We destroy the queue after enqueue, and it will trigger the free */
    result = sds_tqueue_destroy(q);
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_9_tqueue_thread(void *arg) {
    test_7_tqueue_random((void **)arg);
}

static void
test_9_tqueue_parallel_stress(void **state) {
    PRThread *t[8] = {0};

    /* Just launch the threads */
    for (size_t i = 0; i < 8; i++) {
        t[i] = PR_CreateThread(PR_USER_THREAD, test_9_tqueue_thread, (void *)state,
                               PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
        assert_ptr_not_equal(t[i], NULL);
    }

    /* Now wait .... */
    for (size_t i = 0; i < 8; i++) {
        assert_int_equal(PR_JoinThread(t[i]), PR_SUCCESS);
    }

}

int
run_tqueue_tests (void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_1_tqueue_invalid_create),
        cmocka_unit_test_setup_teardown(test_2_tqueue_enqueue,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_3_tqueue_enqueue_multiple,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_4_tqueue_invalid_dequeue,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_5_tqueue_dequeue,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_6_tqueue_dequeue_multiple,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
        cmocka_unit_test_setup_teardown(test_7_tqueue_random,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
        cmocka_unit_test(test_8_tqueue_implicit_free),
        cmocka_unit_test_setup_teardown(test_9_tqueue_parallel_stress,
                                        tqueue_test_setup,
                                        tqueue_test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}


