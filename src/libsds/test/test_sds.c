/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * All rights reserved.
 *
 * License: License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "test_sds.h"

int
main ( int argc __attribute__((unused)), char **argv __attribute__((unused))) {
    int result = 0;
    result += run_bpt_tests();
    result += run_set_tests();
    result += run_cow_tests();
    result += run_queue_tests();
    result += run_tqueue_tests();
    result += run_lqueue_tests();
    return result;
}

