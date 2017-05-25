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
#  include <sys/endian.h>
#elif defined(HAVE_ENDIAN_H)
#  include <endian.h>
#else
#  error platform header for endian detection not found.
#endif

static void
test_siphash(void **state __attribute__((unused))) {

    //
    uint64_t value = 0;
    uint64_t hashout = 0;
    char key[16] = {0};

    uint64_t test_a = 15794382300316794652U;
    uint64_t test_b = 13042610424265326907U;

    // Initial simple test
    value = htole64(5);
    hashout = sds_siphash13(&value, sizeof(uint64_t), key);
    assert_true(hashout == test_a);

    char *test = "abc";
    hashout = sds_siphash13(test, 4, key);
    assert_true(hashout == test_b);
}

int
run_siphash_tests (void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_siphash),
    };
    return cmocka_run_group_tests_name("siphash", tests, NULL, NULL);
}



