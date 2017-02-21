/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

static void
test_1_queue_invalid_create(void **state __attribute__((unused))) {
    sds_result result = SDS_SUCCESS;
    result = sds_queue_init(NULL, NULL);
    assert_int_equal(result, SDS_NULL_POINTER);
}

static void
test_2_queue_enqueue(void **state) {
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;
    result = sds_queue_enqueue(q, (void *)1);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_not_equal(q->head, NULL);
    assert_ptr_not_equal(q->tail, NULL);
}

static void
test_3_queue_enqueue_multiple(void **state) {
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;

    result = sds_queue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    /* Take a ref to the head */
    sds_queue_node *head = q->head;

    for (uint64_t i = 1; i < 100; i++) {
        result = sds_queue_enqueue(q, (void *)i);
        assert_int_equal(result, SDS_SUCCESS);
        assert_ptr_equal(head, q->head);
    }
}

static void
test_4_queue_invalid_dequeue(void **state) {
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;
    void *ptr = NULL;
    /* Attempt a dequeue on a list with no elements. */
    result = sds_queue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_LIST_EXHAUSTED);
    /* Attempt a dequeue on a list with an element, but null ptr */
    result = sds_queue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_queue_dequeue(q, NULL);
    assert_int_equal(result, SDS_NULL_POINTER);
}

static void
test_5_queue_dequeue(void **state) {
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;
    void *ptr = NULL;
    result = sds_queue_enqueue(q, (void *)NULL);
    assert_int_equal(result, SDS_SUCCESS);
    /* Attempt a dequeue on a list with 1 element. */
    result = sds_queue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_SUCCESS);
    assert_ptr_equal(ptr, NULL);
    assert_ptr_equal(q->head, NULL);
    assert_ptr_equal(q->tail, NULL);
    /* Attempt a dequeue on a list with no elements. */
    result = sds_queue_dequeue(q, &ptr);
    assert_int_equal(result, SDS_LIST_EXHAUSTED);
    assert_ptr_equal(q->head, NULL);
    assert_ptr_equal(q->tail, NULL);
}

static void
test_6_queue_dequeue_multiple(void **state) {
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t *ptr = NULL;

    for (uint64_t i = 0; i < 100; i++) {
        ptr = sds_malloc(sizeof(uint64_t));
        *ptr = i;
        result = sds_queue_enqueue(q, (void *)ptr);
        assert_int_equal(result, SDS_SUCCESS);
    }
    /* Take a ref to the tail */
    sds_queue_node *tail = q->tail;
    for (uint64_t i = 0; i < 99; i++) {
        result = sds_queue_dequeue(q, (void **)&ptr);
        assert_int_equal(result, SDS_SUCCESS);
        assert_int_equal(*ptr, i);
        sds_free(ptr);
        assert_ptr_equal(tail, q->tail);
    }
    /* Attempt a dequeue on a list with 1 element. */
    result = sds_queue_dequeue(q, (void **)&ptr);
    assert_int_equal(result, SDS_SUCCESS);
    assert_int_equal(*ptr, 99);
    sds_free(ptr);
    assert_ptr_equal(q->head, NULL);
    assert_ptr_equal(q->tail, NULL);

}

static void
test_7_queue_random(void **state) {
    sds_queue *q = *state;
    sds_result result = SDS_SUCCESS;
    uint64_t ptr = 0;

    for (size_t i = 0; i < 500; i++) {
        if (fill_pattern[i] % 2 != 0) {
            result = sds_queue_dequeue(q, (void **)&ptr);
            if (result != SDS_SUCCESS && result != SDS_LIST_EXHAUSTED) {
                assert_null(1);
            }
        } else {
            result = sds_queue_enqueue(q, (void *)fill_pattern[i]);
            assert_int_equal(result, SDS_SUCCESS);
        }
    }
}

static void
test_8_queue_implicit_free(void **state __attribute__((unused))) {
    sds_result result = SDS_SUCCESS;
    sds_queue *q = NULL;
    result = sds_queue_init(&q, sds_free);
    assert_int_equal(result, SDS_SUCCESS);
    void *ptr = sds_malloc(8);
    result = sds_queue_enqueue(q, ptr);
    assert_int_equal(result, SDS_SUCCESS);
    /* We destroy the queue after enqueue, and it will trigger the free */
    result = sds_queue_destroy(q);
    assert_int_equal(result, SDS_SUCCESS);
}

int
run_queue_tests (void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_1_queue_invalid_create),
        cmocka_unit_test_setup_teardown(test_2_queue_enqueue,
                                        queue_test_setup,
                                        queue_test_teardown),
        cmocka_unit_test_setup_teardown(test_3_queue_enqueue_multiple,
                                        queue_test_setup,
                                        queue_test_teardown),
        cmocka_unit_test_setup_teardown(test_4_queue_invalid_dequeue,
                                        queue_test_setup,
                                        queue_test_teardown),
        cmocka_unit_test_setup_teardown(test_5_queue_dequeue,
                                        queue_test_setup,
                                        queue_test_teardown),
        cmocka_unit_test_setup_teardown(test_6_queue_dequeue_multiple,
                                        queue_test_setup,
                                        queue_test_teardown),
        cmocka_unit_test_setup_teardown(test_7_queue_random,
                                        queue_test_setup,
                                        queue_test_teardown),
        cmocka_unit_test(test_8_queue_implicit_free),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}


