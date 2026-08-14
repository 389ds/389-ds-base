/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * Copyright (C) 2019 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../test_slapd.h"

void
test_libslapd_hello(void **state __attribute__((unused)))
{
    /* It works! */
    assert_int_equal(1, 1);
}

int
run_libslapd_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_libslapd_hello),
        cmocka_unit_test(test_libslapd_pblock_analytics),
        cmocka_unit_test(test_libslapd_pblock_v3c_target_dn),
        cmocka_unit_test(test_libslapd_pblock_v3c_target_sdn),
        cmocka_unit_test(test_libslapd_pblock_v3c_original_target_dn),
        cmocka_unit_test(test_libslapd_pblock_v3c_target_uniqueid),
        cmocka_unit_test(test_libslapd_schema_filter_validate_simple),
        cmocka_unit_test(test_libslapd_operation_v3c_target_spec),
        cmocka_unit_test(test_libslapd_counters_atomic_usage),
        cmocka_unit_test(test_libslapd_counters_atomic_overflow),
        cmocka_unit_test(test_libslapd_pal_meminfo),
        cmocka_unit_test(test_libslapd_util_cachesane),
        /* HAProxy header parsing tests */
        cmocka_unit_test(test_libslapd_haproxy_v1),
        cmocka_unit_test(test_libslapd_haproxy_v2_valid),
        cmocka_unit_test(test_libslapd_haproxy_v2_valid_local),
        cmocka_unit_test(test_libslapd_haproxy_v2_invalid),
        /* HAProxy IP validation and subnet matching tests */
        cmocka_unit_test(test_haproxy_ipv4_subnet_matching),
        cmocka_unit_test(test_haproxy_ipv6_subnet_matching),
        cmocka_unit_test(test_haproxy_trusted_ip_parsing),
        cmocka_unit_test(test_haproxy_parsing_edge_cases),
        cmocka_unit_test(test_haproxy_netmask_precomputation),
        cmocka_unit_test(test_haproxy_ip_matches_cidr),
        cmocka_unit_test(test_haproxy_ipv4_mask_edge_cases),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
