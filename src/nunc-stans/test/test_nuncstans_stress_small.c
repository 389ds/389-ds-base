/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_nuncstans_stress.h"

int
ns_stress_small_setup(void **state) {
    struct test_params *tparams = malloc(sizeof(struct test_params));
    tparams->client_thread_count = 4;
    tparams->server_thread_count = 1;
    tparams->jobs = 64;
    tparams->test_timeout = 30;
    *state = tparams;
    return 0;
}

int
main (void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(ns_stress_test,
                                        ns_stress_small_setup,
                                        ns_stress_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}


