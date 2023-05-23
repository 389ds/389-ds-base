/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
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
        .expected_pr_netaddr_from = { .inet = { .family = PR_AF_INET, .ip = 0x0100A8C0, .port = 0x3930 }},
        .expected_pr_netaddr_dest = { .inet = { .family = PR_AF_INET, .ip = 0x0200A8C0, .port = 0x8501 }}
    },
    {
        .input_str = "PROXY TCP6 2001:db8::1 2001:db8::2 12345 389\r\n",
        .expected_result = HAPROXY_HEADER_PARSED,
        .expected_len = 46,
        .expected_proxy_connection = 1,
        .expected_pr_netaddr_from = { .ipv6 = { .family = PR_AF_INET6, .ip = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, .port = 0x3930 }},
        .expected_pr_netaddr_dest = { .ipv6 = { .family = PR_AF_INET6, .ip = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}, .port = 0x8501 }},
    },
    {
    .input_str = "PROXY TCP6 ::ffff:192.168.0.1 ::ffff:192.168.0.2 12345 389\r\n",
    .expected_result = HAPROXY_HEADER_PARSED,
    .expected_len = 54,
    .expected_proxy_connection = 1,
    .expected_pr_netaddr_from = { .ipv6 = { .family = PR_AF_INET6, .ip = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0xa8, 0x00, 0x01}, .port = 0x3930 }},
    .expected_pr_netaddr_dest = { .ipv6 = { .family = PR_AF_INET6, .ip = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xc0, 0xa8, 0x00, 0x02}, .port = 0x8501 }},
    },
    // Invalid IP
    {
        .input_str = "PROXY TCP4 256.168.0.1 192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    // Invalid port
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2 123456 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    // One port
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2 12345\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    // No ports
    {
        .input_str = "PROXY TCP4 192.168.0.1 192.168.0.2\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    // Empty string
    {
        .input_str = "",
        .expected_result = HAPROXY_NOT_A_HEADER,
        .expected_proxy_connection = 0,
    },
    // Invalid protocol
    {
        .input_str = "PROXY TCP3 192.168.0.1 192.168.0.2 12345 389\r\n",
        .expected_result = HAPROXY_ERROR,
        .expected_proxy_connection = 0,
    },
    // Missing protocol
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