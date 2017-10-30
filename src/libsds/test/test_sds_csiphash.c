/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * test_sds_csiphash validates that siphash operates correctly
 * on this platform.
 */

#include "test_sds.h"

#if defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(HAVE_ENDIAN_H)
#include <endian.h>
#else
#error platform header for endian detection not found.
#endif

static void
test_siphash(void **state __attribute__((unused)))
{
    uint64_t value = 0;
    uint64_t hashout = 0;
    char key[16] = {0};

    uint64_t test_simple = 15794382300316794652U;

    /* Initial simple test */
    value = htole64(5);
    hashout = sds_siphash13(&value, sizeof(uint64_t), key);
    assert_int_equal(hashout, test_simple);

    /* Test a range of input sizes to check endianness behaviour */

    hashout = sds_siphash13("a", 1, key);
    assert_int_equal(hashout, 0x407448d2b89b1813U);

    hashout = sds_siphash13("aa", 2, key);
    assert_int_equal(hashout, 0x7910e0436ed8d1deU);

    hashout = sds_siphash13("aaa", 3, key);
    assert_int_equal(hashout, 0xf752893a6c769652U);

    hashout = sds_siphash13("aaaa", 4, key);
    assert_int_equal(hashout, 0x8b02350718d87164U);

    hashout = sds_siphash13("aaaaa", 5, key);
    assert_int_equal(hashout, 0x92a991474c7eef2U);

    hashout = sds_siphash13("aaaaaa", 6, key);
    assert_int_equal(hashout, 0xf0ab815a640277ccU);

    hashout = sds_siphash13("aaaaaaa", 7, key);
    assert_int_equal(hashout, 0x33f3c6d7dbc82c0dU);

    hashout = sds_siphash13("aaaaaaaa", 8, key);
    assert_int_equal(hashout, 0xc501b12e18428c92U);

    hashout = sds_siphash13("aaaaaaaabbbb", 12, key);
    assert_int_equal(hashout, 0xcddca673069ade64U);

    hashout = sds_siphash13("aaaaaaaabbbbbbbb", 16, key);
    assert_int_equal(hashout, 0xdc54f0bfc0e1deb0U);
}

int
run_siphash_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_siphash),
    };
    return cmocka_run_group_tests_name("siphash", tests, NULL, NULL);
}
