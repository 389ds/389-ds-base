/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"
#include <string.h>
#include <haproxy.h>


typedef struct test_input {
    const char *input_str;
    int expected_result;
    size_t expected_len;
    int expected_proxy_connection;
    PRNetAddr expected_pr_netaddr_from;
    PRNetAddr expected_pr_netaddr_dest;
} test_input;

test_input test_cases[] = {
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_HEADER_PARSED,
        .expected_len = 39,
        .expected_proxy_connection = 1,

/* We need to support both big-endian (x390x) and little-endian (x86) architectures,
 * it's better to dynamically adjust the byte order in our test cases based on
 * the architecture of the system executing the tests.*/
#ifdef __s390x__
        .expected_pr_netaddr_from = { .inet = { .family = PR_AF_INET, .ip = 0xC0A80001, .port = 0x3039 }},
        .expected_pr_netaddr_dest = { .inet = { .family = PR_AF_INET, .ip = 0xC0A80002, .port = 0x0185 }}
#else
        .expected_pr_netaddr_from = { .inet = { .family = PR_AF_INET, .ip = 0x0100A8C0, .port = 0x3930 }},
        .expected_pr_netaddr_dest = { .inet = { .family = PR_AF_INET, .ip = 0x0200A8C0, .port = 0x8501 }}
#endif
    },
    {
        .input_str = "PROXY TCP6 2001:db8::1 2001:db8::2 12345 389\r\n",
        .expected_result = HAPROXY_HEADER_PARSED,
        .expected_len = 46,
        .expected_proxy_connection = 1,
#ifdef __s390x__
        .expected_pr_netaddr_from = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}}}, .port = 0x3039 }},
        .expected_pr_netaddr_dest = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}}}, .port = 0x0185 }}
#else
        .expected_pr_netaddr_from = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}}}, .port = 0x3930 }},
        .expected_pr_netaddr_dest = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}}}, .port = 0x8501 }}
#endif
    },
    {
        .input_str = "PROXY TCP6 ::ffff:192.168.0.1 ::ffff:192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_HEADER_PARSED,
        .expected_len = 54,
        .expected_proxy_connection = 1,
#ifdef __s390x__
        .expected_pr_netaddr_from = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0xa8, 0x00, 0x01}}}, .port = 0x3039 }},
        .expected_pr_netaddr_dest = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0xa8, 0x00, 0x02}}}, .port = 0x0185 }}
#else
        .expected_pr_netaddr_from = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0xa8, 0x00, 0x01}}}, .port = 0x3930 }},
        .expected_pr_netaddr_dest = { .ipv6 = { .family = PR_AF_INET6, .ip = {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0xa8, 0x00, 0x02}}}, .port = 0x8501 }}
#endif
    },
    /* Invalid IP */
    {
        .input_str = "PROXY TCP4 256.168.0.1 192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    /* Invalid port */
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2 123456 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    /* One port */
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2 12345\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    /* No ports */
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    /* Empty string */
    {
        .input_str = "",
        .expected_result = HAPROXY_NOT_A_HEADER,
        .expected_proxy_connection = 0,
    },
    /* Invalid protocol */
    {
        .input_str = "PROXY TCP3 192.168.0.1 192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    /* Missing protocol */
    {
        .input_str = "PROXY 192.168.0.1 192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },

};

size_t num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

void test_libslapd_haproxy_v1(void **state) {
    (void)state;
    int result = 0;

    for (size_t i = 0; i < num_tests; i++) {
        int proxy_connection = 0;
        PRNetAddr pr_netaddr_from = {{0}};
        PRNetAddr pr_netaddr_dest = {{0}};
        size_t str_len = strlen(test_cases[i].input_str);
        result = haproxy_parse_v1_hdr(test_cases[i].input_str, &str_len, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest);

        assert_int_equal(result, test_cases[i].expected_result);
        assert_int_equal(proxy_connection, test_cases[i].expected_proxy_connection);

        if (test_cases[i].expected_result == 0) {
            // slapi_log_error(SLAPI_LOG_ERR, "haproxy_parse_v1_hdr", "Expected pr_netaddr_from: ");
            // slapi_log_prnetaddr(&test_cases[i].expected_pr_netaddr_from);
            // slapi_log_error(SLAPI_LOG_ERR, "haproxy_parse_v1_hdr", "Actual pr_netaddr_from: ");
            // slapi_log_prnetaddr(&pr_netaddr_from);

            // slapi_log_error(SLAPI_LOG_ERR, "haproxy_parse_v1_hdr", "Expected pr_netaddr_dest: ");
            // slapi_log_prnetaddr(&test_cases[i].expected_pr_netaddr_dest);
            // slapi_log_error(SLAPI_LOG_ERR, "haproxy_parse_v1_hdr", "Actual pr_netaddr_dest: ");
            // slapi_log_prnetaddr(&pr_netaddr_dest);

            assert_memory_equal(&test_cases[i].expected_pr_netaddr_from, &pr_netaddr_from, sizeof(PRNetAddr));
            assert_memory_equal(&test_cases[i].expected_pr_netaddr_dest, &pr_netaddr_dest, sizeof(PRNetAddr));
        }
    }
}


void test_libslapd_haproxy_v2_invalid(void **state) {
    (void) state; // Unused

    struct {
        char *desc;
        char *str;
        struct proxy_hdr_v2 hdr_v2;
        size_t str_len;
        int expected_result;
        int expected_proxy_connection;
    } tests[] = {
        {"short header",
         "short",
         {},
         sizeof("short"),
         HAPROXY_NOT_A_HEADER,
         0},
        {"invalid header",
         "invalid_signature",
         {},
         sizeof("invalid_signature"),
         HAPROXY_NOT_A_HEADER,
         0},
        {"invalid signature",
         NULL,
         {"INVALID", PP2_VERSION | PP2_VER_CMD_PROXY, PP2_FAM_INET | PP2_TRANS_STREAM, htons(PP2_ADDR_LEN_INET)},
         PP2_HEADER_LEN + PP2_ADDR_LEN_INET,
         HAPROXY_NOT_A_HEADER,
         0},
        {"unsupported family",
         NULL,
         {PP2_SIGNATURE, PP2_VERSION | PP2_VER_CMD_PROXY, 0x30 | PP2_TRANS_STREAM, htons(0)},
         PP2_HEADER_LEN,
         HAPROXY_ERROR,
         0},
        {"unsupported protocol",
         NULL,
         {PP2_SIGNATURE, PP2_VERSION | PP2_VER_CMD_PROXY, PP2_FAM_INET | 0x30, htons(0)},
         PP2_HEADER_LEN,
         HAPROXY_ERROR,
         0},
        {"invalid version",
         NULL,
         {PP2_SIGNATURE, (PP2_VERSION ^ 0xF0) | PP2_VER_CMD_PROXY, PP2_FAM_INET | PP2_TRANS_STREAM, htons(PP2_ADDR_LEN_INET)},
         PP2_HEADER_LEN + PP2_ADDR_LEN_INET,
         HAPROXY_ERROR,
         0},
        {"valid header, wrong command",
         NULL,
         {PP2_SIGNATURE, PP2_VERSION | (PP2_VER_CMD_PROXY ^ 0xF0), PP2_FAM_INET | PP2_TRANS_STREAM, htons(PP2_ADDR_LEN_INET)},
         PP2_HEADER_LEN + PP2_ADDR_LEN_INET,
         HAPROXY_ERROR,
         0},
        {"valid header, too long",
         NULL,
         {PP2_SIGNATURE, PP2_VERSION | PP2_VER_CMD_PROXY, PP2_FAM_INET | PP2_TRANS_STREAM, htons(PP2_ADDR_LEN_INET * 2)},
         PP2_HEADER_LEN + PP2_ADDR_LEN_INET,
         HAPROXY_ERROR,
         0}
        };

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int proxy_connection;
        PRNetAddr pr_netaddr_from;
        PRNetAddr pr_netaddr_dest;
        char *str_to_test = tests[i].str ? tests[i].str : (char *) &tests[i].hdr_v2;

        int result = haproxy_parse_v2_hdr(str_to_test, &tests[i].str_len, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest);

        assert_int_equal(result, tests[i].expected_result);
        assert_int_equal(proxy_connection, tests[i].expected_proxy_connection);
    }
}

// Test for a valid proxy header v2 with unsupported transport protocol
void test_libslapd_haproxy_v2_unsupported_protocol(void **state) {
    (void) state; // Unused

    // Create a sample string with valid proxy header v2 and unsupported transport protocol
    struct proxy_hdr_v2 hdr_v2;
    memcpy(hdr_v2.sig, PP2_SIGNATURE, PP2_SIGNATURE_LEN);
    hdr_v2.ver_cmd = PP2_VERSION | PP2_VER_CMD_PROXY;
    hdr_v2.fam = PP2_FAM_INET | 0x30; // 0x30 is unsupported
    hdr_v2.len = htons(0);

    size_t str_len = PP2_HEADER_LEN;
    int proxy_connection;
    PRNetAddr pr_netaddr_from;
    PRNetAddr pr_netaddr_dest;

    int result = haproxy_parse_v2_hdr((const char *) &hdr_v2, &str_len, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest);

    assert_int_equal(result, HAPROXY_ERROR);
    assert_int_equal(proxy_connection, 0);
}


// Test for the case when the protocol version is invalid
void test_libslapd_haproxy_v2_invalid_version(void **state) {
    (void) state; // Unused

    // Create a sample string with an invalid protocol version
    struct proxy_hdr_v2 hdr_v2;
    memcpy(hdr_v2.sig, PP2_SIGNATURE, PP2_SIGNATURE_LEN);
    hdr_v2.ver_cmd = (PP2_VERSION ^ 0xF0) | PP2_VER_CMD_PROXY;
    hdr_v2.fam = PP2_FAM_INET | PP2_TRANS_STREAM;
    hdr_v2.len = htons(PP2_ADDR_LEN_INET);

    size_t str_len = PP2_HEADER_LEN + PP2_ADDR_LEN_INET;
    int proxy_connection;
    PRNetAddr pr_netaddr_from;
    PRNetAddr pr_netaddr_dest;

    int result = haproxy_parse_v2_hdr((const char *) &hdr_v2, &str_len, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest);

    assert_int_equal(result, HAPROXY_ERROR);
    assert_int_equal(proxy_connection, 0);
}


// Test for the case when the protocol command is valid - IPv4 and IPv6
void test_libslapd_haproxy_v2_valid(void **state) {
    (void) state; // Unused

    // We need only first two test cases as they are valid
    size_t num_tests = 2;

    for (size_t i = 0; i < num_tests; i++) {
        struct proxy_hdr_v2 hdr_v2;
        memcpy(hdr_v2.sig, PP2_SIGNATURE, PP2_SIGNATURE_LEN);
        hdr_v2.ver_cmd = PP2_VERSION | PP2_VER_CMD_PROXY;

        size_t str_len;
        int proxy_connection;
        PRNetAddr pr_netaddr_from = {0};
        PRNetAddr pr_netaddr_dest = {0};

        if (i == 0) { // IPv4 test case
            hdr_v2.fam = PP2_FAM_INET | PP2_TRANS_STREAM;
            hdr_v2.len = htons(PP2_ADDR_LEN_INET);
            uint32_t src_addr = test_cases[i].expected_pr_netaddr_from.inet.ip;
            uint32_t dst_addr = test_cases[i].expected_pr_netaddr_dest.inet.ip;
            uint16_t src_port = test_cases[i].expected_pr_netaddr_from.inet.port;
            uint16_t dst_port = test_cases[i].expected_pr_netaddr_dest.inet.port;
            memcpy(&hdr_v2.addr.ip4.src_addr, &src_addr, sizeof(src_addr));
            memcpy(&hdr_v2.addr.ip4.dst_addr, &dst_addr, sizeof(dst_addr));
            memcpy(&hdr_v2.addr.ip4.src_port, &src_port, sizeof(src_port));
            memcpy(&hdr_v2.addr.ip4.dst_port, &dst_port, sizeof(dst_port));
            str_len = PP2_HEADER_LEN + PP2_ADDR_LEN_INET;
        } else { // IPv6 test case
            hdr_v2.fam = PP2_FAM_INET6 | PP2_TRANS_STREAM;
            hdr_v2.len = htons(PP2_ADDR_LEN_INET6);
            uint8_t src_addr[16];
            uint8_t dst_addr[16];
            memcpy(src_addr, &test_cases[i].expected_pr_netaddr_from.ipv6.ip, sizeof(src_addr));
            memcpy(dst_addr, &test_cases[i].expected_pr_netaddr_dest.ipv6.ip, sizeof(dst_addr));
            uint16_t src_port = test_cases[i].expected_pr_netaddr_from.ipv6.port;
            uint16_t dst_port = test_cases[i].expected_pr_netaddr_dest.ipv6.port;
            memcpy(&hdr_v2.addr.ip6.src_addr, src_addr, sizeof(src_addr));
            memcpy(&hdr_v2.addr.ip6.dst_addr, dst_addr, sizeof(dst_addr));
            memcpy(&hdr_v2.addr.ip6.src_port, &src_port, sizeof(src_port));
            memcpy(&hdr_v2.addr.ip6.dst_port, &dst_port, sizeof(dst_port));
            str_len = PP2_HEADER_LEN + PP2_ADDR_LEN_INET6;
        }

        int rc = haproxy_parse_v2_hdr((const char *) &hdr_v2, &str_len, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest);

        assert_int_equal(rc, HAPROXY_HEADER_PARSED);
        assert_int_equal(proxy_connection, 1);
        assert_memory_equal(&pr_netaddr_from, &test_cases[i].expected_pr_netaddr_from, sizeof(PRNetAddr));
        assert_memory_equal(&pr_netaddr_dest, &test_cases[i].expected_pr_netaddr_dest, sizeof(PRNetAddr));
    }
}


// Test for a valid proxy header v2 with LOCAL command
void test_libslapd_haproxy_v2_valid_local(void **state) {
    (void) state; // Unused
    // Create a sample string with valid proxy header v2 and LOCAL command
    struct proxy_hdr_v2 hdr_v2;
    memcpy(hdr_v2.sig, PP2_SIGNATURE, PP2_SIGNATURE_LEN);
    hdr_v2.ver_cmd = PP2_VERSION | PP2_VER_CMD_LOCAL;
    hdr_v2.fam = PP2_FAM_INET | PP2_TRANS_STREAM;
    hdr_v2.len = htons(0);

    size_t str_len = PP2_HEADER_LEN;
    int proxy_connection;
    PRNetAddr pr_netaddr_from;
    PRNetAddr pr_netaddr_dest;

    int result = haproxy_parse_v2_hdr((const char *) &hdr_v2, &str_len, &proxy_connection, &pr_netaddr_from, &pr_netaddr_dest);

    assert_int_equal(result, HAPROXY_HEADER_PARSED);
    assert_int_equal(proxy_connection, 0);
}

/* Test IPv4 subnet matching */
void test_haproxy_ipv4_subnet_matching(void **state) {
    PRNetAddr ip1, ip2, network;

    (void)state; /* Unused */

    /* Test /24 subnet */
    PR_StringToNetAddr("192.168.1.50", &ip1);
    PR_StringToNetAddr("192.168.1.0", &network);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 24), 1);

    /* Test IP outside /24 subnet */
    PR_StringToNetAddr("192.168.2.50", &ip2);
    assert_int_equal(haproxy_ipv4_in_subnet(ip2.inet.ip, network.inet.ip, 24), 0);

    /* Test /32 exact match */
    PR_StringToNetAddr("192.168.1.0", &ip1);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 32), 1);

    /* Test /32 non-match */
    PR_StringToNetAddr("192.168.1.1", &ip1);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 32), 0);

    /* Test /16 subnet */
    PR_StringToNetAddr("192.168.0.0", &network);
    PR_StringToNetAddr("192.168.255.255", &ip1);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 16), 1);

    /* Test /0 matches everything */
    PR_StringToNetAddr("1.2.3.4", &ip1);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 0), 1);
}

/* Test IPv6 subnet matching */
void test_haproxy_ipv6_subnet_matching(void **state) {
    PRNetAddr ip1, network;
    struct in6_addr *ip_v6, *net_v6;

    (void)state; /* Unused */

    /* Test /64 subnet */
    PR_StringToNetAddr("2001:db8::1234", &ip1);
    PR_StringToNetAddr("2001:db8::", &network);
    ip_v6 = (struct in6_addr *)&ip1.ipv6.ip;
    net_v6 = (struct in6_addr *)&network.ipv6.ip;
    assert_int_equal(haproxy_ipv6_in_subnet(ip_v6, net_v6, 64), 1);

    /* Test IP outside /64 subnet */
    PR_StringToNetAddr("2001:db9::1234", &ip1);
    ip_v6 = (struct in6_addr *)&ip1.ipv6.ip;
    assert_int_equal(haproxy_ipv6_in_subnet(ip_v6, net_v6, 64), 0);

    /* Test /32 subnet */
    PR_StringToNetAddr("2001:db8:abcd::1234", &ip1);
    PR_StringToNetAddr("2001:db8::", &network);
    ip_v6 = (struct in6_addr *)&ip1.ipv6.ip;
    net_v6 = (struct in6_addr *)&network.ipv6.ip;
    assert_int_equal(haproxy_ipv6_in_subnet(ip_v6, net_v6, 32), 1);

    /* Test /128 exact match */
    PR_StringToNetAddr("2001:db8::1", &ip1);
    PR_StringToNetAddr("2001:db8::1", &network);
    ip_v6 = (struct in6_addr *)&ip1.ipv6.ip;
    net_v6 = (struct in6_addr *)&network.ipv6.ip;
    assert_int_equal(haproxy_ipv6_in_subnet(ip_v6, net_v6, 128), 1);

    /* Test /0 matches everything */
    PR_StringToNetAddr("fe80::1", &ip1);
    ip_v6 = (struct in6_addr *)&ip1.ipv6.ip;
    assert_int_equal(haproxy_ipv6_in_subnet(ip_v6, net_v6, 0), 1);
}

/* Test trusted IP parsing and binary matching */
void test_haproxy_trusted_ip_parsing(void **state) {
    struct berval bv1 = {.bv_val = "192.168.1.0/24", .bv_len = 14};
    struct berval bv2 = {.bv_val = "10.0.0.5", .bv_len = 8};
    struct berval bv3 = {.bv_val = "2001:db8::/32", .bv_len = 13};
    struct berval bv4 = {.bv_val = "fe80::1", .bv_len = 7};
    struct berval *bvals[] = {&bv1, &bv2, &bv3, &bv4, NULL};
    size_t count = 0;
    char errorbuf[1024];
    haproxy_trusted_entry_t *parsed;
    PRNetAddr test_ip1, test_ip2, test_ip3, test_ip4, test_ip5, test_ip6, test_ip7;

    (void)state; /* Unused */

    /* Create test configuration with mixed IPs and subnets */

    /* Parse into binary format */
    parsed = haproxy_parse_trusted_ips(bvals, &count, errorbuf);

    assert_non_null(parsed);
    assert_int_equal(count, 4);

    /* Verify parsed structure for IPv4 subnet */
    assert_int_equal(parsed[0].is_subnet, 1);
    assert_int_equal(parsed[0].prefix_len, 24);
    assert_int_equal(parsed[0].network.raw.family, PR_AF_INET);

    /* Verify parsed structure for IPv4 single IP */
    assert_int_equal(parsed[1].is_subnet, 0);
    assert_int_equal(parsed[1].prefix_len, -1);
    assert_int_equal(parsed[1].network.raw.family, PR_AF_INET);

    /* Verify parsed structure for IPv6 subnet */
    assert_int_equal(parsed[2].is_subnet, 1);
    assert_int_equal(parsed[2].prefix_len, 32);
    assert_int_equal(parsed[2].network.raw.family, PR_AF_INET6);

    /* Verify parsed structure for IPv6 single IP */
    assert_int_equal(parsed[3].is_subnet, 0);
    assert_int_equal(parsed[3].prefix_len, -1);
    assert_int_equal(parsed[3].network.raw.family, PR_AF_INET6);

    /* Test binary matching with parsed entries */

    /* Test 1: IP in IPv4 subnet should match */
    PR_StringToNetAddr("192.168.1.100", &test_ip1);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip1, parsed, count), 1);

    /* Test 2: IP outside IPv4 subnet should not match */
    PR_StringToNetAddr("192.168.2.100", &test_ip2);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip2, parsed, count), 0);

    /* Test 3: Exact IPv4 match */
    PR_StringToNetAddr("10.0.0.5", &test_ip3);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip3, parsed, count), 1);

    /* Test 4: IP in IPv6 subnet should match */
    PR_StringToNetAddr("2001:db8::1234", &test_ip4);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip4, parsed, count), 1);

    /* Test 5: IP outside IPv6 subnet should not match */
    PR_StringToNetAddr("2001:db9::1234", &test_ip5);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip5, parsed, count), 0);

    /* Test 6: Exact IPv6 match */
    PR_StringToNetAddr("fe80::1", &test_ip6);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip6, parsed, count), 1);

    /* Test 7: IPv4-mapped IPv6 address normalization */
    PR_StringToNetAddr("::ffff:192.168.1.50", &test_ip7);
    /* Should match the 192.168.1.0/24 subnet after normalization */
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip7, parsed, count), 1);

    /* Cleanup */
    slapi_ch_free((void **)&parsed);
}

/* Test edge cases and error handling in parsing */
void test_haproxy_parsing_edge_cases(void **state) {
    size_t count = 0;
    char errorbuf[1024];
    haproxy_trusted_entry_t *parsed;
    struct berval *empty[] = {NULL};
    struct berval bv_8 = {.bv_val = "10.0.0.0/8", .bv_len = 10};
    struct berval bv_16 = {.bv_val = "172.16.0.0/16", .bv_len = 13};
    struct berval bv_32 = {.bv_val = "203.0.113.1/32", .bv_len = 14};
    struct berval *prefixes[] = {&bv_8, &bv_16, &bv_32, NULL};
    PRNetAddr test_ip;

    (void)state; /* Unused */

    /* Test NULL input */
    parsed = haproxy_parse_trusted_ips(NULL, &count, errorbuf);
    assert_null(parsed);
    assert_int_equal(count, 0);

    /* Test empty array */
    parsed = haproxy_parse_trusted_ips(empty, &count, errorbuf);
    assert_null(parsed);
    assert_int_equal(count, 0);


    /* Test various CIDR prefix lengths */
    parsed = haproxy_parse_trusted_ips(prefixes, &count, errorbuf);
    assert_non_null(parsed);
    assert_int_equal(count, 3);

    /* /8 should match wide range */
    PR_StringToNetAddr("10.255.255.255", &test_ip);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip, parsed, count), 1);

    /* /16 should match within range */
    PR_StringToNetAddr("172.16.99.99", &test_ip);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip, parsed, count), 1);

    /* Outside /16 */
    PR_StringToNetAddr("172.17.0.1", &test_ip);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip, parsed, count), 0);

    /* /32 exact match only */
    PR_StringToNetAddr("203.0.113.1", &test_ip);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip, parsed, count), 1);

    PR_StringToNetAddr("203.0.113.2", &test_ip);
    assert_int_equal(haproxy_ip_matches_parsed(&test_ip, parsed, count), 0);

    slapi_ch_free((void **)&parsed);
}

/* Test netmask precomputation during parsing */
void test_haproxy_netmask_precomputation(void **state) {
    struct berval bv1 = {.bv_val = "192.168.1.0/24", .bv_len = 14};
    struct berval *bvals[] = {&bv1, NULL};
    struct berval bv2 = {.bv_val = "2001:db8:abcd::/48", .bv_len = 18};
    struct berval *bvals2[] = {&bv2, NULL};
    struct berval bv3 = {.bv_val = "2001:db8::/33", .bv_len = 13};
    struct berval *bvals3[] = {&bv3, NULL};
    size_t count = 0;
    char errorbuf[1024];
    haproxy_trusted_entry_t *parsed;
    uint32_t expected_mask;
    int i;

    (void)state; /* Unused */

    /* IPv4 /24 netmask should be 0xFFFFFF00 (255.255.255.0) */
    parsed = haproxy_parse_trusted_ips(bvals, &count, errorbuf);

    assert_non_null(parsed);
    assert_int_equal(count, 1);

    /* Verify netmask was pre-computed */
    expected_mask = htonl(0xFFFFFF00);
    assert_int_equal(parsed[0].netmask.inet.ip, expected_mask);

    slapi_ch_free((void **)&parsed);

    /* IPv6 /48 netmask - first 6 bytes should be 0xFF, rest 0x00 */
    parsed = haproxy_parse_trusted_ips(bvals2, &count, errorbuf);
    assert_non_null(parsed);
    assert_int_equal(count, 1);

    /* Verify IPv6 netmask bytes */
    for (i = 0; i < 6; i++) {
        assert_int_equal(parsed[0].netmask.ipv6.ip.pr_s6_addr[i], 0xFF);
    }
    for (i = 6; i < 16; i++) {
        assert_int_equal(parsed[0].netmask.ipv6.ip.pr_s6_addr[i], 0x00);
    }

    slapi_ch_free((void **)&parsed);

    /* IPv6 /33 - partial byte mask (first 4 bytes + 1 bit) */
    parsed = haproxy_parse_trusted_ips(bvals3, &count, errorbuf);
    assert_non_null(parsed);

    /* First 4 bytes should be 0xFF */
    for (i = 0; i < 4; i++) {
        assert_int_equal(parsed[0].netmask.ipv6.ip.pr_s6_addr[i], 0xFF);
    }
    /* 5th byte should be 0x80 (10000000 binary - 1 bit set) */
    assert_int_equal(parsed[0].netmask.ipv6.ip.pr_s6_addr[4], 0x80);
    /* Remaining bytes should be 0x00 */
    for (i = 5; i < 16; i++) {
        assert_int_equal(parsed[0].netmask.ipv6.ip.pr_s6_addr[i], 0x00);
    }

    slapi_ch_free((void **)&parsed);
}

/* Test CIDR string matching */
void test_haproxy_ip_matches_cidr(void **state) {
    (void)state; /* Unused */

    /* IPv4 subnet matching */
    assert_int_equal(haproxy_ip_matches_cidr("192.168.1.50", "192.168.1.0/24"), 1);
    assert_int_equal(haproxy_ip_matches_cidr("192.168.2.50", "192.168.1.0/24"), 0);

    /* IPv4 exact match (no CIDR) */
    assert_int_equal(haproxy_ip_matches_cidr("10.0.0.1", "10.0.0.1"), 1);
    assert_int_equal(haproxy_ip_matches_cidr("10.0.0.1", "10.0.0.2"), 0);

    /* IPv6 subnet matching */
    assert_int_equal(haproxy_ip_matches_cidr("2001:db8::1234", "2001:db8::/32"), 1);
    assert_int_equal(haproxy_ip_matches_cidr("2001:db9::1234", "2001:db8::/32"), 0);

    /* IPv6 exact match */
    assert_int_equal(haproxy_ip_matches_cidr("fe80::1", "fe80::1"), 1);
    assert_int_equal(haproxy_ip_matches_cidr("fe80::1", "fe80::2"), 0);

    /* Case insensitivity for IPv6 */
    assert_int_equal(haproxy_ip_matches_cidr("2001:DB8::1", "2001:db8::1"), 1);
}

/* Test IPv4 mask calculation edge cases and undefined behavior prevention */
void test_haproxy_ipv4_mask_edge_cases(void **state) {
    PRNetAddr ip1, ip2, network;

    (void)state; /* Unused */

    /* Test prefix_len = 0 (should match everything without undefined behavior) */
    PR_StringToNetAddr("192.168.1.1", &ip1);
    PR_StringToNetAddr("10.0.0.0", &network);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 0), 1);

    /* Test with completely different IPs - /0 should still match */
    PR_StringToNetAddr("255.255.255.255", &ip1);
    PR_StringToNetAddr("0.0.0.0", &network);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 0), 1);

    /* Test prefix_len = 32 (exact match only) */
    PR_StringToNetAddr("192.168.1.1", &ip1);
    PR_StringToNetAddr("192.168.1.1", &network);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 32), 1);

    /* Test prefix_len = 32 with different IPs */
    PR_StringToNetAddr("192.168.1.2", &ip2);
    assert_int_equal(haproxy_ipv4_in_subnet(ip2.inet.ip, network.inet.ip, 32), 0);

    /* Test prefix_len = 1 (half of all IPs) */
    PR_StringToNetAddr("127.255.255.255", &ip1);
    PR_StringToNetAddr("0.0.0.0", &network);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 1), 1);

    PR_StringToNetAddr("128.0.0.0", &ip2);
    assert_int_equal(haproxy_ipv4_in_subnet(ip2.inet.ip, network.inet.ip, 1), 0);

    /* Test prefix_len = 31 (smallest non-trivial subnet) */
    PR_StringToNetAddr("192.168.1.0", &network);
    PR_StringToNetAddr("192.168.1.0", &ip1);
    PR_StringToNetAddr("192.168.1.1", &ip2);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 31), 1);
    assert_int_equal(haproxy_ipv4_in_subnet(ip2.inet.ip, network.inet.ip, 31), 1);

    PR_StringToNetAddr("192.168.1.2", &ip1);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 31), 0);

    /* Test invalid prefix lengths */
    PR_StringToNetAddr("192.168.1.1", &ip1);
    PR_StringToNetAddr("192.168.1.0", &network);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, -1), 0);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 33), 0);
    assert_int_equal(haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, 255), 0);

    /* Test various prefix lengths to ensure no undefined behavior */
    PR_StringToNetAddr("10.0.0.0", &network);
    for (int prefix = 0; prefix <= 32; prefix++) {
        PR_StringToNetAddr("10.255.255.255", &ip1);
        /* Should not crash or exhibit undefined behavior */
        int result = haproxy_ipv4_in_subnet(ip1.inet.ip, network.inet.ip, prefix);
        /* For /8, 10.255.255.255 should match 10.0.0.0 */
        if (prefix <= 8) {
            assert_int_equal(result, 1);
        } else if (prefix == 32) {
            assert_int_equal(result, 0); /* Not exact match */
        }
    }
}