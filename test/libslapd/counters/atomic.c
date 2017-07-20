/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

void
test_libslapd_counters_atomic_usage(void **state __attribute__((unused)))
{
    Slapi_Counter *tc = slapi_counter_new();

    uint64_t value = 0;
    /* Check that it starts as 0 */
    value = slapi_counter_get_value(tc);
    assert_true(value == 0);
    /* Increment */
    slapi_counter_increment(tc);
    value = slapi_counter_get_value(tc);
    assert_true(value == 1);
    /* add */
    slapi_counter_add(tc, 100);
    value = slapi_counter_get_value(tc);
    assert_true(value == 101);
    /* set */
    slapi_counter_set_value(tc, 200);
    value = slapi_counter_get_value(tc);
    assert_true(value == 200);
    /* dec */
    slapi_counter_decrement(tc);
    value = slapi_counter_get_value(tc);
    assert_true(value == 199);
    /* sub */
    slapi_counter_subtract(tc, 99);
    value = slapi_counter_get_value(tc);
    assert_true(value == 100);
    /* init */
    slapi_counter_init(tc);
    value = slapi_counter_get_value(tc);
    assert_true(value == 0);


    slapi_counter_destroy(&tc);

    /* We could attempt a more complex thread test later? */
}

void
test_libslapd_counters_atomic_overflow(void **state __attribute__((unused)))
{
    Slapi_Counter *tc = slapi_counter_new();
    /* This is intmax ... */
    uint32_t value_32 = 0xFFFFFFFF;
    uint64_t value = 0;

    slapi_counter_set_value(tc, (uint64_t)value_32);
    value = slapi_counter_get_value(tc);
    assert_true(value == (uint64_t)value_32);

    slapi_counter_increment(tc);
    value = slapi_counter_get_value(tc);
    assert_true(value != 0);
    assert_true(value > (uint64_t)value_32);

    slapi_counter_destroy(&tc);
}
