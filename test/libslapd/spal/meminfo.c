/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"

#include <slapi_pal.h>
#include <slapi-private.h>

/*
 * Assert that our meminfo interface in slapi_pal works.
 */

void
test_libslapd_pal_meminfo(void **state __attribute__((unused))) {
    slapi_pal_meminfo *mi = spal_meminfo_get();
    assert_true(mi->pagesize_bytes > 0);
    assert_true(mi->system_total_pages > 0);
    assert_true(mi->system_total_bytes > 0);
    assert_true(mi->process_consumed_pages > 0);
    assert_true(mi->process_consumed_bytes > 0);
    assert_true(mi->system_available_pages > 0);
    assert_true(mi->system_available_bytes > 0);
    spal_meminfo_destroy(mi);
}

void
test_libslapd_util_cachesane(void **state __attribute__((unused))) {
    slapi_pal_meminfo *mi = spal_meminfo_get();
    uint64_t request = 0;
    mi->system_available_bytes = 0;
    assert_true(util_is_cachesize_sane(mi, &request) == UTIL_CACHESIZE_ERROR);

    // Set the values to known quantities
    request = 50000;
    mi->system_available_bytes = 99999;
    assert_true(util_is_cachesize_sane(mi, &request) == UTIL_CACHESIZE_VALID);

    request = 99999;
    assert_true(util_is_cachesize_sane(mi, &request) == UTIL_CACHESIZE_VALID);

    request = 100000;
    assert_true(util_is_cachesize_sane(mi, &request) == UTIL_CACHESIZE_REDUCED);
    assert_true(request <= 75000);

    spal_meminfo_destroy(mi);
}



