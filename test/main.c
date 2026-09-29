/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_slapd.h"
#include "slapi-private.h"

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
    int result = 0;
    slapi_td_init();
    result += run_libslapd_tests();
    result += run_plugin_tests();
    slapi_td_destroy();

    PR_Cleanup();
    return result;
}
