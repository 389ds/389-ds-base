/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/


/* test_bptree is the test driver for the B+ Tree implemented by bptree/bptree.c */

/* Contains test data. */
#include "test_sds.h"

static int32_t cb_count = 0;

static void
test_31_map_cb(void *k, void *v) {
#ifdef DEBUG
    printf("mapping %" PRIu64 ":%s\n", (uint64_t)k, (char *)v);
#endif
    cb_count++;
}

static void
test_31_map(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    cb_count = 0;

    for (uint64_t i = 1; i < 200 ; i++) {
        /* Make a new string */
        char *ptr = sds_malloc(sizeof(char) * 4);
        /*  */
        sprintf(ptr, "%03"PRIu64, i);
        result = sds_bptree_insert(binst, (void *)&i, ptr);
        assert_int_equal(result, SDS_SUCCESS);
        result = sds_bptree_verify(binst);
        assert_int_equal(result, SDS_SUCCESS);
    }
    sds_bptree_map(binst, test_31_map_cb);
    assert_int_equal(cb_count, 199);
}

/* This helper function allows us to create an array of pointers
 * from the stack arrays
 */
static void
_build_ptr_array(uint64_t **dest, const uint64_t *src, size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
        dest[i] = sds_uint64_t_dup((void *)&(src[i]));
    }
}

static void
test_32_build_simple_tree(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    size_t capacity = 2;
    uint64_t *load_ptr_array[capacity];
    _build_ptr_array(load_ptr_array, load_array, capacity);

    result = sds_bptree_load(binst, (void **)load_ptr_array, NULL, capacity);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_verify(binst);
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_33_build_small_tree(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    size_t capacity = 12;
    uint64_t *load_ptr_array[capacity];
    _build_ptr_array(load_ptr_array, load_array, capacity);

    result = sds_bptree_load(binst, (void **)load_ptr_array, NULL, capacity);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_verify(binst);
    assert_int_equal(result, SDS_SUCCESS);
}

static void
test_34_build_large_tree(void **state) {
    sds_bptree_instance *binst = *state;
    sds_result result = SDS_SUCCESS;

    size_t capacity = 64;
    uint64_t *load_ptr_array[capacity];
    _build_ptr_array(load_ptr_array, load_array, capacity);

    /* Make a simple array */
    result = sds_bptree_load(binst, (void **)load_ptr_array, NULL, capacity);
    assert_int_equal(result, SDS_SUCCESS);
    result = sds_bptree_verify(binst);
    assert_int_equal(result, SDS_SUCCESS);
    // sds_bptree_display(binst);
}

static void
run_set_test(void **state,
            uint64_t *set_a, size_t set_a_count,
            uint64_t *set_b, size_t set_b_count,
            uint64_t *expect, size_t expect_count,
            uint64_t *exclude, size_t exclude_count,
            sds_result (*fn)(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_difference))
{
    void **binst = *state;
    sds_bptree_instance *binst_a = (sds_bptree_instance *) binst[0];
    sds_bptree_instance *binst_b = (sds_bptree_instance *) binst[1];
    sds_bptree_instance *binst_out = (sds_bptree_instance *) binst[2];
    sds_result result = SDS_SUCCESS;


    /* Bulk load them */
    uint64_t *ptr_set_a[set_a_count];
    _build_ptr_array(ptr_set_a, set_a, set_a_count);
    result = sds_bptree_load(binst_a, (void **)ptr_set_a, NULL, set_a_count);
    assert_int_equal(result, SDS_SUCCESS);

    uint64_t *ptr_set_b[set_b_count];
    _build_ptr_array(ptr_set_b, set_b, set_b_count);
    result = sds_bptree_load(binst_b, (void **)ptr_set_b, NULL, set_b_count);
    assert_int_equal(result, SDS_SUCCESS);

    /* Now difference */
    result = fn(binst_a, binst_b, &binst_out);
    assert_int_equal(result, SDS_SUCCESS);

    /* For the clean up, we have to give the binst_out pointer back to state. */
    /* We have to assign these here, else GCC optimises them out */
    binst[2] = binst_out;

    /* Make sure it's correct */
    for (size_t i = 0; i < expect_count; i++) {
        assert_int_equal(SDS_KEY_PRESENT, sds_bptree_search(binst_out, (void *)&(expect[i])));
    }

    for (size_t i = 0; i < exclude_count; i++) {
        assert_int_equal(SDS_KEY_NOT_PRESENT, sds_bptree_search(binst_out, (void *)&(exclude[i])));
    }


}

static void
test_35_set_difference_1(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 2, 3, 6, 7, 8, 9};
    uint64_t set_b[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};
    uint64_t expect[] = {4, 5, 7, 8, 9, 10, 11, 12 ,13};
    uint64_t exclude[] = {1, 3, 6};

    run_set_test(state, set_a, 7, set_b, 9, expect, 9, exclude, 3, sds_bptree_difference);
}

static void
test_35_set_difference_2(void **state) {
    uint64_t set_a[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};
    uint64_t set_b[] = {1, 2, 3, 6, 7, 8, 9};
    uint64_t expect[] = {4, 5, 7, 8, 9, 10, 11, 12 ,13};
    uint64_t exclude[] = {1, 3, 6};

    run_set_test(state, set_a, 9, set_b, 7, expect, 9, exclude, 3, sds_bptree_difference);
}

static void
test_35_set_difference_3(void **state) {
    uint64_t set_a[] = {1, 9};
    uint64_t set_b[] = {2, 3, 6, 7, 8, 9};
    uint64_t expect[] = {1,2,3,6,7,8};
    uint64_t exclude[] = {9};

    run_set_test(state, set_a, 2, set_b, 6, expect, 6, exclude, 1, sds_bptree_difference);
}


static void
test_36_set_union_1(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 2, 3, 6, 7, 8, 9, 13};
    uint64_t set_b[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};

    uint64_t _union[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    uint64_t exclude[] = {};
    run_set_test(state, set_a, 8, set_b, 9, _union, 13, exclude, 0, sds_bptree_union);
}

static void
test_36_set_union_2(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};
    uint64_t set_b[] = {1, 2, 3, 6, 7, 8, 9, 13};

    uint64_t _union[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    uint64_t exclude[] = {};
    run_set_test(state, set_a, 9, set_b, 8, _union, 13, exclude, 0, sds_bptree_union);
}

static void
test_36_set_union_3(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {};
    uint64_t set_b[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};

    uint64_t _union[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};
    uint64_t exclude[] = {};
    run_set_test(state, set_a, 0, set_b, 9, _union, 9, exclude, 0, sds_bptree_union);
}

static void
test_37_set_intersect_1(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 2, 3, 6, 7, 8, 9, 13};
    uint64_t set_b[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};

    uint64_t _intersect[] = {1, 3, 6, 13};
    uint64_t exclude[] = {2, 4, 5, 7, 8, 9, 10, 11, 12};

    run_set_test(state, set_a, 8, set_b, 9, _intersect, 4, exclude, 9, sds_bptree_intersect);
}

static void
test_37_set_intersect_2(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 3, 4, 5, 6, 10, 11, 12, 13};
    uint64_t set_b[] = {1, 2, 3, 6, 7, 8, 9, 13};

    uint64_t _intersect[] = {1, 3, 6, 13};
    uint64_t exclude[] = {2, 4, 5, 7, 8, 9, 10, 11, 12};

    run_set_test(state, set_a, 9, set_b, 8, _intersect, 4, exclude, 9, sds_bptree_intersect);
}

static void
test_37_set_intersect_3(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint64_t set_b[] = {10, 11, 12, 13, 14, 15};

    uint64_t _intersect[] = {};
    uint64_t exclude[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    run_set_test(state, set_a, 9, set_b, 6, _intersect, 0, exclude, 15, sds_bptree_intersect);
}

static void
test_38_set_compliment_1(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 2, 3, 4, 5, 6, 7};
    uint64_t set_b[] = {1, 2, 3, 4, 5, 8, 9};

    uint64_t _intersect[] = {6, 7};
    uint64_t exclude[] = {1, 2, 3, 4, 5, 8, 9};

    run_set_test(state, set_a, 7, set_b, 7, _intersect, 2, exclude, 7, sds_bptree_compliment);
}

static void
test_38_set_compliment_2(void **state) {
    /* Populate the two sets with different values */
    uint64_t set_a[] = {1, 2, 3, 4, 5, 8, 9};
    uint64_t set_b[] = {1, 2, 3, 4, 5, 6, 7};

    uint64_t _intersect[] = {8, 9};
    uint64_t exclude[] = {1, 2, 3, 4, 5, 6, 7};

    run_set_test(state, set_a, 7, set_b, 7, _intersect, 2, exclude, 7, sds_bptree_compliment);
}

static int64_t
test_39_filter_cb(void *k, void *v) {
#ifdef DEBUG
    printf("filtering %" PRIu64 ":%s\n", *(uint64_t *)k, (char *)v);
#endif
    if (*(uint64_t *)k % 2 == 0) {
        return 1;
    }
    return 0;
}

static void
test_39_set_filter(void **state) {
    sds_bptree_instance *binst = *state;
    sds_bptree_instance *binst_filtered = NULL;
    sds_result result = SDS_SUCCESS;

    for (uint64_t i = 1; i < 200 ; i++) {
        /* Make a new string */
        char *ptr = sds_malloc(sizeof(char) * 4);
        /*  */
        sprintf(ptr, "%03"PRIu64, i);
        result = sds_bptree_insert(binst, (void *)&i, ptr);
        assert_int_equal(result, SDS_SUCCESS);
        result = sds_bptree_verify(binst);
        assert_int_equal(result, SDS_SUCCESS);
    }
    sds_bptree_filter(binst, test_39_filter_cb, &binst_filtered);

    for (uint64_t i = 1; i < 200 ; i++) {
        result = sds_bptree_search(binst_filtered, (void *)&i);
        if (i % 2 == 0) {
            assert_int_equal(result, SDS_KEY_PRESENT);
        } else {
            assert_int_equal(result, SDS_KEY_NOT_PRESENT);
        }
    }
    result = sds_bptree_destroy(binst_filtered);
    assert_int_equal(result, SDS_SUCCESS);
}

int
run_set_tests(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_31_map,
                                        bptree_str_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_32_build_simple_tree,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_33_build_small_tree,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_34_build_large_tree,
                                        bptree_test_setup,
                                        bptree_test_teardown),
        cmocka_unit_test_setup_teardown(test_35_set_difference_1,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_35_set_difference_2,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_35_set_difference_3,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_36_set_union_1,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_36_set_union_2,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_36_set_union_3,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_37_set_intersect_1,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_37_set_intersect_2,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_37_set_intersect_3,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_38_set_compliment_1,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_38_set_compliment_2,
                                        bptree_test_set_setup,
                                        bptree_test_set_teardown),
        cmocka_unit_test_setup_teardown(test_39_set_filter,
                                        bptree_str_test_setup,
                                        bptree_test_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}



