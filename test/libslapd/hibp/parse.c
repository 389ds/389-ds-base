/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"
#include <string.h>
#include <pthread.h>
#include <nss.h>
#include <hibp.h>

#define HIBP_SHA1_HEX_LENGTH 41  /* 40 hex chars + null terminator */

/* Global NSS state for standalone testing */
static int g_nss_initialised = 0;

/* Ensure NSS is initialised */
static void
ensure_nss_init(void)
{
    if (!g_nss_initialised) {
        if (NSS_NoDB_Init(NULL) != SECSuccess) {
            fprintf(stderr, "Fatal error: unable to initialize the NSS subcomponent.");
            return;
        }
        g_nss_initialised = 1;
    }
}

/* SHA-1 conversion tests */
void
test_hibp_sha1_hex(void **state)
{
    char hex_output[HIBP_SHA1_HEX_LENGTH];
    char long_password[1024];

    (void)state; /* Unused */
    ensure_nss_init();

    /* Basic password */
    assert_int_equal(hibp_sha1_hex("password", hex_output), 0);
    assert_string_equal(hex_output, "5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8");

    /* Empty string */
    assert_int_equal(hibp_sha1_hex("", hex_output), 0);
    assert_string_equal(hex_output, "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");

    /* Special characters */
    assert_int_equal(hibp_sha1_hex("P@ssw0rd!", hex_output), 0);
    assert_string_equal(hex_output, "076D3E6C4B9F654B5B220B9045B7458AB6B4CBC6");

    /* Unicode characters */
    assert_int_equal(hibp_sha1_hex("rúnfhocal", hex_output), 0);
    assert_int_equal(strlen(hex_output), 40);

    /* Long password */
    memset(long_password, 'A', 1000);
    long_password[1000] = '\0';
    assert_int_equal(hibp_sha1_hex(long_password, hex_output), 0);
    assert_int_equal(strlen(hex_output), 40);
}

/* Response parsing tests */
void
test_hibp_parse_response(void **state)
{
    int result;

    (void)state; /* Unused */

    /* Found */
    char response1[] = "0018A45C4D1DEF81644B54AB7F969B88D65:1\r\n"
                       "00D4F6E8FA6EECAD2A3AA415EEC418D38EC:2\r\n";
    result = hibp_parse_response_wrapper(response1, "00D4F6E8FA6EECAD2A3AA415EEC418D38EC");
    assert_int_equal(result, 2);

    /* Not found */
    char response2[] = "0018A45C4D1DEF81644B54AB7F969B88D65:1\r\n";
    result = hibp_parse_response_wrapper(response2, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    assert_int_equal(result, 0);

    /* Empty */
    char response3[] = "";
    result = hibp_parse_response_wrapper(response3, "0018A45C4D1DEF81644B54AB7F969B88D65");
    assert_int_equal(result, 0);

    /* Case insensitive */
    char response4[] = "ABCDEF1234567890ABCDEF1234567890ABCDE:42\r\n";
    result = hibp_parse_response_wrapper(response4, "abcdef1234567890abcdef1234567890abcde");
    assert_int_equal(result, 42);

    /* Malformed (no colon) */
    char response5[] = "NOSEPARATOR\r\n";
    result = hibp_parse_response_wrapper(response5, "NOSEPARATOR");
    assert_int_equal(result, 0);

    /* Invalid count */
    char response6[] = "INVALID234567890ABCDEF1234567890ABCDE:notanumber\r\n";
    result = hibp_parse_response_wrapper(response6, "INVALID234567890ABCDEF1234567890ABCDE");
    assert_int_equal(result, 0);

    /* Large count */
    char response7[] = "0018A45C4D1DEF81644B54AB7F969B88D65:3861493\r\n";
    result = hibp_parse_response_wrapper(response7, "0018A45C4D1DEF81644B54AB7F969B88D65");
    assert_int_equal(result, 3861493);
}

/* Buffer handling tests */
void
test_hibp_buffer_handling(void **state)
{
    char *out_data = NULL;
    char *large_response = NULL;
    char *huge_data = NULL;
    int result;
    const char *small_response = "0018A45C4D1DEF81644B54AB7F969B88D65:123\r\n";
    const char *line = "0018A45C4D1DEF81644B54AB7F969B88D65:1\r\n";
    size_t line_len = strlen(line);
    size_t offset = 0;
    size_t response_size = 16000;

    (void)state; /* Unused */

    /* Small response fits in initial buffer */
    result = hibp_write_callback_wrapper(small_response, strlen(small_response), 1024, &out_data);
    assert_true(result > 0);
    assert_non_null(out_data);
    assert_string_equal(out_data, small_response);
    slapi_ch_free_string(&out_data);

    /* Large response requires buffer growth */
    large_response = slapi_ch_calloc(1, response_size + 1);
    while (offset + line_len < response_size) {
        memcpy(large_response + offset, line, line_len);
        offset += line_len;
    }
    large_response[offset] = '\0';
    result = hibp_write_callback_wrapper(large_response, strlen(large_response), 512, &out_data);
    assert_true(result > 8192);
    assert_non_null(out_data);
    slapi_ch_free_string(&large_response);
    slapi_ch_free_string(&out_data);

    /* Exceeds max size (1MB) */
    huge_data = slapi_ch_calloc(1, 1024 * 1024 + 100);
    memset(huge_data, 'A', 1024 * 1024 + 99);
    result = hibp_write_callback_wrapper(huge_data, 1024 * 1024 + 99, 8192, &out_data);
    assert_int_equal(result, -1);
    assert_null(out_data);
    slapi_ch_free_string(&huge_data);
}

/* Integration tests using mock API response */
void
test_hibp_api_integration(void **state)
{
    int result;
    passwdPolicy policy = {0};

    /*
     * Mock HIBP response for "password" (SHA1: 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8)
     * This simulates a response that does contain the password suffix
     */
    const char *mock_breached_response =
        "1D2DA4053E34E76F6576ED1DA63134B5E2A:2\r\n"
        "1D72CD07550416C216D8AD296BF5C0AE8E0:10\r\n"
        "1E4C9B93F3F0682250B6CF8331B7EE68FD8:10429567\r\n"  /* "password" suffix */
        "1E4D895C4A3EFBC6E7A22E5CE8F6A4AF52C:4\r\n";

    /*
     * Mock HIBP response for "xK9#mQ2$vL7@nP4!wR8^tY1&" {SHA1: 42C0A9886C5AF8846C8E6AED554B3165AAC} 
     * This simulates a response that does not contain the password suffix
     */
    const char *mock_safe_response =
        "0018A45C4D1DEF81644B54AB7F969B88D65:1\r\n"
        "00D4F6E8FA6EECAD2A3AA415EEC418D38EC:2\r\n"
        "1234567890ABCDEF1234567890ABCDEF123:5\r\n";

    (void)state; /* Unused */
    ensure_nss_init();

    /* Initialise client */
    result = hibp_init();
    assert_int_equal(result, 0);

    /* Test NULL inputs */
    result = hibp_check_password(NULL, &policy);
    assert_int_equal(result, -1);
    result = hibp_check_password("password", NULL);
    assert_int_equal(result, -1);

    policy.pw_breach_db_timeout = 10;
    policy.pw_breach_db_url = NULL;

    /* Test breached password with mock */
    hibp_set_mock_response(mock_breached_response);
    result = hibp_check_password("password", &policy);
    assert_int_equal(result, 10429567);

    /* Test safe password with mock */
    hibp_set_mock_response(mock_safe_response);
    result = hibp_check_password("xK9#mQ2$vL7@nP4!wR8^tY1&", &policy);
    assert_int_equal(result, 0);

    /* Reset mock for other tests */
    hibp_set_mock_response(NULL);
}

/* Thread worker for concurrent test */
static void *
hibp_thread_worker(void *arg)
{
    int *result = (int *)arg;
    passwdPolicy policy = {0};
    policy.pw_breach_db_timeout = 10;
    policy.pw_breach_db_url = NULL;

    *result = hibp_check_password("password", &policy);
    return NULL;
}

#define HIBP_TEST_THREADS 4

/* Concurrent test using mock response */
void
test_hibp_concurrent_requests(void **state)
{
    int result;
    int results[HIBP_TEST_THREADS] = {0};
    pthread_t threads[HIBP_TEST_THREADS];
    int i;

    /* Set mock response to "password" (SHA1: 5BAA61E4C9B93F3F0682250B6CF8331B7EE68FD8) */
    const char *mock_response =
        "1E4C9B93F3F0682250B6CF8331B7EE68FD8:10429567\r\n";

    (void)state; /* Unused */
    ensure_nss_init();

    result = hibp_init();
    assert_int_equal(result, 0);

    /* Set mock response before spawning threads */
    hibp_set_mock_response(mock_response);

    for (i = 0; i < HIBP_TEST_THREADS; i++) {
        pthread_create(&threads[i], NULL, hibp_thread_worker, &results[i]);
    }

    for (i = 0; i < HIBP_TEST_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* All threads should find the breached password */
    for (i = 0; i < HIBP_TEST_THREADS; i++) {
        assert_int_equal(results[i], 10429567);
    }

    /* Reset mock */
    hibp_set_mock_response(NULL);
}
