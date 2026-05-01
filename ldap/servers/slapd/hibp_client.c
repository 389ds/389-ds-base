/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/*
 * hibp_client.c - DS HIBP client
 */

#include "hibp.h"
#include "prinit.h"
#include <curl/curl.h>
#include <pk11pub.h>

#define HIBP_API_URL "https://api.pwnedpasswords.com/range/"
#define HIBP_API_TIMEOUT_SECS 10
#define HIBP_CURL_INIT_OK 0
#define HIBP_CURL_INIT_FAILED -1
#define HIBP_SHA1_DIGEST_BYTES 20
#define HIBP_SHA1_HEX_PREFIX_LEN 5
#define HIBP_SHA1_HEX_SUFFIX_LEN (HIBP_SHA1_DIGEST_BYTES * 2 - HIBP_SHA1_HEX_PREFIX_LEN)
#define HIBP_RESPONSE_INITIAL_SIZE (8 * 1024)       /* 8 KB */
#define HIBP_RESPONSE_MAX_SIZE     (1024 * 1024)    /* 1 MB */

/*
 * Hash provider abstraction layer
 *
 * Allows for different SHA-1 implementations for FIPS and non-FIPS mode
 *
 * Future:  Standalone implementation for FIPS mode if NSS SHA-1 is restricted
 */
typedef int (*hibp_hash_fn)(const char *input, size_t len, unsigned char *digest);
static hibp_hash_fn g_hash_provider = NULL;

/*
 * Default non FIPS mode hash provider using NSS
 *
 * Returns: 0 on success, -1 on error
 */
static int
hibp_sha1_nss(const char *input, size_t len, unsigned char *digest)
{
    SECStatus rv = PK11_HashBuf(SEC_OID_SHA1, digest, (unsigned char *)input, len);
    return (rv == SECSuccess) ? 0 : -1;
}

/* Set custom hash provider
 *
 * Must be called during initialisation
 */
void
hibp_set_hash_provider(hibp_hash_fn fn)
{
    g_hash_provider = fn;
}

/*
 * Get the current hash provider
 *
 * Returns: hash provider function pointer
 */
static hibp_hash_fn
hibp_get_hash_provider(void)
{
    return g_hash_provider ? g_hash_provider : hibp_sha1_nss;
}

typedef struct hibp_response
{
    char *data;
    size_t size;
    size_t capacity;
} HIBPResponse;

/* One time curl initialisation */
static PRCallOnceType g_curl_init_control = {0};
static int g_curl_init_result = HIBP_CURL_INIT_FAILED;

/* Mock API response for testing */
static const char *g_mock_response = NULL;

void
hibp_set_mock_response(const char *response)
{
    g_mock_response = response;
}

/*
 * libcurl callback to handle response data
 *
 * Can be called multiple times per request as data chunks arrive,
 * appending to the response buffer.
 *
 * Returns: number of bytes handled, 0 on error
 */
static size_t
hibp_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    HIBPResponse *response = (HIBPResponse *)userp;
    size_t real_size;
    size_t required;
    size_t new_capacity;
    char *new_data = NULL;

    if (!response || !response->data) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_write_callback",
                      "Invalid HIBP response buffer\n");
        return 0;
    }

    if (nmemb > 0 && size > SIZE_MAX / nmemb) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_write_callback",
                      "HIBP response size overflow\n");
        return 0;
    }

    /* Calculate the size of the incoming response buffer */
    real_size = size * nmemb;

    if (real_size > SIZE_MAX - response->size - 1) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_write_callback",
                      "HIBP response buffer size overflow\n");
        return 0;
    }

    /* Calculate the total required size of the response buffer */
    required = response->size + real_size + 1;

    if (required > HIBP_RESPONSE_MAX_SIZE) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_write_callback",
                      "HIBP response exceeded maximum size\n");
        return 0;
    }

    /* Does the size of the response buffer need to be grown to hold the incoming data */
    if (required > response->capacity) {
        new_capacity = response->capacity;

        if (new_capacity == 0) {
            new_capacity = HIBP_RESPONSE_INITIAL_SIZE;
        }

        /* Double the buffer size until it can hold the incoming data */
        while (new_capacity < required) {
            if (new_capacity > HIBP_RESPONSE_MAX_SIZE / 2) {
                new_capacity = HIBP_RESPONSE_MAX_SIZE;
                break;
            }
            new_capacity *= 2;
        }

        /* Has the buffer been grown enough */
        if (new_capacity < required) {
            slapi_log_err(SLAPI_LOG_ERR, "hibp_write_callback",
                          "HIBP response buffer cannot grow enough\n");
            return 0;
        }

        new_data = slapi_ch_realloc(response->data, new_capacity);
        if (!new_data) {
            slapi_log_err(SLAPI_LOG_ERR, "hibp_write_callback",
                          "Failed to allocate HIBP response buffer\n");
            return 0;
        }

        response->data = new_data;
        response->capacity = new_capacity;
    }

    memcpy(response->data + response->size, contents, real_size);
    response->size += real_size;
    response->data[response->size] = '\0';

    return real_size;
}

/*
 * Test wrapper for hibp_write_callback buffer handling
 *
 * Exposes the static hibp_write_callback function for testing
 *
 * Simulates how libcurl delivers data by calling hibp_write_callback
 * multiple times with chunks of input data. This allows for testing
 * buffer growth and overflow logic without network calls.
 *
 * Returns: buffer size on success, -1 on error
 */
int
hibp_write_callback_wrapper(const char *data, size_t data_len, size_t chunk_size, char **out_data)
{
    HIBPResponse response = {0};
    size_t offset = 0;
    size_t bytes_to_write;
    size_t written;

    /* Allocate initial buffer */
    response.capacity = HIBP_RESPONSE_INITIAL_SIZE;
    response.data = slapi_ch_calloc(1, response.capacity);
    if (!response.data) {
        *out_data = NULL;
        return -1;
    }
    response.size = 0;

    /* Feed data in chunks, simulating curl callbacks */
    while (offset < data_len) {
        bytes_to_write = data_len - offset;
        if (bytes_to_write > chunk_size) {
            bytes_to_write = chunk_size;
        }

        written = hibp_write_callback((void *)(data + offset), 1, bytes_to_write, &response);
        if (written != bytes_to_write) {
            slapi_ch_free((void **)&response.data);
            *out_data = NULL;
            return -1;
        }
        offset += written;
    }

    *out_data = response.data;
    return (int)response.size;
}

/*
 * Initialise the libcurl library only once
 *
 * Returns: PR_SUCCESS on success, PR_FAILURE on error
 */
static PRStatus
hibp_curl_global_init(void)
{
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_curl_global_init",
                      "Curl global init failed: %s\n", curl_easy_strerror(res));
        g_curl_init_result = HIBP_CURL_INIT_FAILED;
        return PR_FAILURE;
    }
    g_curl_init_result = HIBP_CURL_INIT_OK;
    return PR_SUCCESS;
}

/*
 * Ensure libcurl library is initialised before use
 *
 * Returns: 0 on success, -1 on error
 */
static int
hibp_ensure_curl_init(void)
{
    if (PR_CallOnce(&g_curl_init_control, hibp_curl_global_init) != PR_SUCCESS) {
        return HIBP_CURL_INIT_FAILED;
    }
    return g_curl_init_result;
}

/*
 * Convert plaintext password to uppercase SHA-1 hex string
 *
 * Uses the current hash provider (NSS by default)
 *
 * Returns: 0 on success, -1 on error
 */
int
hibp_sha1_hex(const char *password, char *hex_output)
{
    unsigned char sha1_digest[HIBP_SHA1_DIGEST_BYTES];
    hibp_hash_fn hash_fn;

    if (!password || !hex_output) {
        return -1;
    }

    hash_fn = hibp_get_hash_provider();
    if (hash_fn(password, strlen(password), sha1_digest) != 0) {
        hex_output[0] = '\0';
        return -1;
    }

    for (int i = 0; i < HIBP_SHA1_DIGEST_BYTES; i++) {
        sprintf(&hex_output[i * 2], "%02X", sha1_digest[i]);
    }
    hex_output[HIBP_SHA1_DIGEST_BYTES * 2] = '\0';

    return 0;
}

/*
 * Query HIBP API with hash prefix to get matching suffixes
 *
 * Returns: 0 on success, -1 on error
 */
static int
hibp_query_api(const char *prefix, const char *api_url, HIBPResponse *response, int timeout)
{
    char url[512];
    char error_buffer[CURL_ERROR_SIZE] = {0};
    CURL *curl = NULL;
    CURLcode res;
    long response_code;
    int n;

    /* Are we using a mock response for testing */
    if (g_mock_response != NULL) {
        size_t mock_len = strlen(g_mock_response);
        response->data = slapi_ch_malloc(mock_len + 1);
        if (response->data == NULL) {
            return -1;
        }
        memcpy(response->data, g_mock_response, mock_len + 1);
        response->size = mock_len;
        response->capacity = mock_len + 1;
        return 0;
    }

    /* Ensure libcurl library is initialised */
    if (hibp_ensure_curl_init() != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_query_api",
                      "curl_global_init() failed\n");
        return -1;
    }

    /* Get a libcurl handle for this request */
    curl = curl_easy_init();
    if (!curl) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_query_api",
                      "curl_easy_init() failed\n");
        return -1;
    }

    /* Build the API URL, use HIBP_API_URL by default */
    if (api_url && strlen(api_url) > 0) {
        n = snprintf(url, sizeof(url), "%s%s", api_url, prefix);
    } else {
        n = snprintf(url, sizeof(url), "%s%s", HIBP_API_URL, prefix);
    }
    if (n < 0 || (size_t)n >= sizeof(url)) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_query_api", "URL too long or encoding error\n");
        curl_easy_cleanup(curl);
        return -1;
    }

    /* Initialise the response buffer */
    response->capacity = HIBP_RESPONSE_INITIAL_SIZE;
    response->data = slapi_ch_calloc(1, response->capacity);
    response->size = 0;

    /* Set libcurl options, validate critical ones */
    if (curl_easy_setopt(curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, hibp_write_callback) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_query_api",
                      "Failed to set critical libcurl options\n");
        slapi_ch_free((void **)&response->data);
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "389-ds-hibp-client/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    /* Send HTTP request, blocks until response is received or timeout expires */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_query_api",
                      "curl_easy_perform() failed: %s (%s) for URL %s\n",
                      curl_easy_strerror(res), error_buffer, url);
        slapi_ch_free((void **)&response->data);
        curl_easy_cleanup(curl);
        return -1;
    }

    /* HTTP transaction successful, check the HTTP response code */
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code != 200) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_query_api",
                      "HTTP error %ld for URL %s\n", response_code, url);
        slapi_ch_free((void **)&response->data);
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

/*
 * Parse HIBP API response to find matching hash suffix
 *
 * Iterates through the response buffer line by line, looking for the
 * matching hash suffix.
 *
 * Returns: breach count if found, 0 if not found, -1 on error
 */
static int
hibp_parse_response(HIBPResponse *response, const char *suffix)
{
    char *line = NULL;
    char *next_line = NULL;
    char *cr = NULL;
    char *colon = NULL;
    char *count_str = NULL;
    char *endptr = NULL;
    long count = 0;

    if (!response || !response->data || !suffix) {
        slapi_log_err(SLAPI_LOG_ERR, "hibp_parse_response",
                      "Invalid response or suffix\n");
        return -1;
    }

    line = response->data;

    while (line && *line) {
        // line: 1E4C9B93F3F0682250B6CF8331B7EE68FD8:10429567\r\n */
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }

        cr = strchr(line, '\r');
        if (cr) {
            *cr = '\0';
        }

        // line: "1E4C9B93F3F0682250B6CF8331B7EE68FD8:10429567"
        colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            count_str = colon + 1;
            // Line: "1E4C9B93F3F0682250B6CF8331B7EE68FD8"
            // count_str: "10429567"
            if (strcasecmp(line, suffix) == 0) {
                endptr = NULL;
                count = strtol(count_str, &endptr, 10);
                if (endptr == count_str || count < 0 || count > INT_MAX) {
                    return 0;  /* Treat an invalid count as not found */
                }
                return (int)count;
            }
        }

        line = next_line;
    }

    return 0;
}

/*
 * Test wrapper for hibp_parse_response parsing logic
 *
 * Exposes the static hibp_parse_response function for testing
 *
 * Takes response data as a string and converts it to HIBPResponse struct
 *
 * Returns: breach count if found, 0 if not found, -1 on error
 */
int
hibp_parse_response_wrapper(char *response_data, const char *suffix)
{
    HIBPResponse response = {0};
    response.data = response_data;
    response.size = strlen(response_data);
    return hibp_parse_response(&response, suffix);
}

/*
 * Check if password appears in HIBP breach database
 *
 * Converts password to SHA-1, queries the HIBP API using hash prefix
 * and checks for a matching suffix
 *
 * Returns: breach count if found, 0 if not found, -1 on error
 */
int
hibp_check_password(const char *password, passwdPolicy *pwpolicy)
{
    char sha1_hex[HIBP_SHA1_DIGEST_BYTES * 2 + 1];
    char prefix[HIBP_SHA1_HEX_PREFIX_LEN + 1];
    char suffix[HIBP_SHA1_HEX_SUFFIX_LEN + 1];
    HIBPResponse response = {0};
    int result = -1;
    int timeout;

    if (!password || !pwpolicy) {
        slapi_log_err(SLAPI_LOG_PWDPOLICY, "hibp_check_password",
                        "Invalid input: password or password policy is NULL\n");
        return -1;
    }

    /* Generate plaintext password hash */
    if (hibp_sha1_hex(password, sha1_hex) != 0) {
        slapi_log_err(SLAPI_LOG_PWDPOLICY, "hibp_check_password",
                        "Failed to convert password to SHA-1 hash\n");
        return -1;
    }

    /* Split hash into prefix and suffix */
    strncpy(prefix, sha1_hex, HIBP_SHA1_HEX_PREFIX_LEN);
    prefix[HIBP_SHA1_HEX_PREFIX_LEN] = '\0';
    strcpy(suffix, sha1_hex + HIBP_SHA1_HEX_PREFIX_LEN);

    slapi_log_err(SLAPI_LOG_DEBUG, "hibp_check_password", "Checking prefix: %s\n", prefix);

    /* Configure timeout */
    if (pwpolicy->pw_breach_db_timeout > 0) {
        timeout = pwpolicy->pw_breach_db_timeout;
    } else {
        timeout = HIBP_API_TIMEOUT_SECS;
    }

    /* Query the HIBP API with hash prefix */
    if (hibp_query_api(prefix, pwpolicy->pw_breach_db_url, &response, timeout) == 0) {
        result = hibp_parse_response(&response, suffix);
        slapi_ch_free((void **)&response.data);
    }

    return result;
}

/*
 * Initialise HIBP subsystem
 *
 * Must be called before hibp_check_password
 *
 * Returns: 0 on success, -1 on error
 */
int
hibp_init(void)
{
    if (slapd_pk11_isFIPS()) {
        /*
         * TODO: If NSS SHA-1 fails in FIPS mode, switch to standalone implementation.
         */
        slapi_log_err(SLAPI_LOG_WARNING, "hibp_init",
                      "FIPS mode detected - HIBP password checking may not work "
                      "if SHA-1 is restricted\n");
    } else {
        hibp_set_hash_provider(hibp_sha1_nss);
    }

    return hibp_ensure_curl_init();
}
