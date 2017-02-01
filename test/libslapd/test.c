/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../test_slapd.h"

void
test_libslapd_hello(void **state __attribute__((unused))) {
    /* It works! */
    assert_int_equal(1, 1);
}

int
run_libslapd_tests (void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_libslapd_hello),
        cmocka_unit_test(test_libslapd_pblock_analytics),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}


