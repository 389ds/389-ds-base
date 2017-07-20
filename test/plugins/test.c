/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../test_slapd.h"

void
test_plugin_hello(void **state __attribute__((unused)))
{
    /* It works! */
    assert_int_equal(1, 1);
}

int
run_plugin_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_plugin_hello),
        cmocka_unit_test_setup_teardown(test_plugin_pwdstorage_pbkdf2_auth,
                                        test_plugin_pwdstorage_nss_setup,
                                        test_plugin_pwdstorage_nss_stop),
        cmocka_unit_test_setup_teardown(test_plugin_pwdstorage_pbkdf2_rounds,
                                        test_plugin_pwdstorage_nss_setup,
                                        test_plugin_pwdstorage_nss_stop),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
