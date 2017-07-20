/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_nuncstans_stress.h"

int
ns_stress_large_setup(void **state)
{
    struct test_params *tparams = malloc(sizeof(struct test_params));
    tparams->client_thread_count = 80;
    tparams->server_thread_count = 20;
    tparams->jobs = 200;
    tparams->test_timeout = 70;
    *state = tparams;
    return 0;
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(ns_stress_test,
                                        ns_stress_large_setup,
                                        ns_stress_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
