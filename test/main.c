/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_slapd.h"

int
main(int argc __attribute__((unused)), char **argv __attribute__((unused)))
{
    int result = 0;
    result += run_libslapd_tests();
    result += run_plugin_tests();

    PR_Cleanup();
    return result;
}
