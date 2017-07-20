/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

void
test_ht_null_init(void **state __attribute__((unused)))
{
    assert_true(SDS_NULL_POINTER == sds_ht_init(NULL, NULL, NULL, NULL, NULL, NULL));
}

void
test_ht_null_insert(void **state)
{
    sds_ht_instance *ht = *state;
    assert_true(sds_ht_insert(ht, NULL, NULL) == SDS_INVALID_KEY);
}

void
test_ht_simple_insert(void **state)
{
    sds_ht_instance *ht = *state;
    uint64_t key = 1;

    assert_true(sds_ht_insert(ht, &key, NULL) == SDS_SUCCESS);
}

void
test_ht_simple_search(void **state)
{
    sds_ht_instance *ht = *state;
    uint64_t key = 1;
    uint64_t key_missing = 2;
    void *value = NULL;

    assert_true(sds_ht_insert(ht, &key, NULL) == SDS_SUCCESS);
    assert_true(sds_ht_search(ht, &key, &value) == SDS_KEY_PRESENT);
    assert_true(sds_ht_search(ht, &key_missing, &value) == SDS_KEY_NOT_PRESENT);
}

void
test_ht_medium_insert(void **state)
{
    sds_ht_instance *ht = *state;
    void *value = NULL;
    for (uint64_t i = 0; i < 256; i++) {
        assert_true(sds_ht_insert(ht, &i, NULL) == SDS_SUCCESS);
    }
    for (uint64_t i = 0; i < 256; i++) {
        assert_true(sds_ht_search(ht, &i, &value) == SDS_KEY_PRESENT);
    }
}

void
test_ht_large_insert(void **state)
{
    sds_ht_instance *ht = *state;
    void *value = NULL;
    for (uint64_t i = 0; i < 8192; i++) {
        assert_true(sds_ht_insert(ht, &i, NULL) == SDS_SUCCESS);
    }
    assert_true(sds_ht_verify(ht) == SDS_SUCCESS);
    for (uint64_t i = 0; i < 8192; i++) {
        assert_true(sds_ht_search(ht, &i, &value) == SDS_KEY_PRESENT);
    }
}

void
test_ht_small_delete(void **state)
{
    sds_ht_instance *ht = *state;
    uint64_t key = 1;
    uint64_t key_missing = 2;
    void *value = NULL;

    assert_true(sds_ht_insert(ht, &key, NULL) == SDS_SUCCESS);
    assert_true(sds_ht_search(ht, &key, &value) == SDS_KEY_PRESENT);
    assert_true(sds_ht_delete(ht, &key_missing) == SDS_KEY_NOT_PRESENT);
    assert_true(sds_ht_delete(ht, &key) == SDS_KEY_PRESENT);
    assert_true(sds_ht_search(ht, &key, &value) == SDS_KEY_NOT_PRESENT);
}

void
test_ht_medium_delete(void **state)
{
    sds_ht_instance *ht = *state;
    void *value = NULL;
    for (uint64_t i = 0; i < 256; i++) {
        assert_true(sds_ht_insert(ht, &i, NULL) == SDS_SUCCESS);
        assert_true(sds_ht_verify(ht) == SDS_SUCCESS);
    }
    for (uint64_t i = 0; i < 256; i++) {
        assert_true(sds_ht_search(ht, &i, &value) == SDS_KEY_PRESENT);
    }
    for (uint64_t i = 0; i < 256; i++) {
        assert_true(sds_ht_delete(ht, &i) == SDS_KEY_PRESENT);
        assert_true(sds_ht_verify(ht) == SDS_SUCCESS);
    }
    for (uint64_t i = 0; i < 256; i++) {
        assert_true(sds_ht_search(ht, &i, &value) == SDS_KEY_NOT_PRESENT);
    }
}


int
run_ht_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ht_null_init),
        cmocka_unit_test_setup_teardown(test_ht_null_insert,
                                        ht_test_setup,
                                        ht_test_teardown),
        cmocka_unit_test_setup_teardown(test_ht_simple_insert,
                                        ht_test_setup,
                                        ht_test_teardown),
        cmocka_unit_test_setup_teardown(test_ht_simple_search,
                                        ht_test_setup,
                                        ht_test_teardown),
        cmocka_unit_test_setup_teardown(test_ht_medium_insert,
                                        ht_test_setup,
                                        ht_test_teardown),
        cmocka_unit_test_setup_teardown(test_ht_large_insert,
                                        ht_test_setup,
                                        ht_test_teardown),
        cmocka_unit_test_setup_teardown(test_ht_small_delete,
                                        ht_test_setup,
                                        ht_test_teardown),
        cmocka_unit_test_setup_teardown(test_ht_medium_delete,
                                        ht_test_setup,
                                        ht_test_teardown),
    };
    return cmocka_run_group_tests_name("ht", tests, NULL, NULL);
}
