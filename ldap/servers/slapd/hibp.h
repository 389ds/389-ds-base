/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#include "slap.h"

/*
 * Core HIBP API
 */

/* Initialise the HIBP subsystem */
int hibp_init(void);

/* Shutdown the HIBP subsystem */
void hibp_shutdown(void);

/*
 * Check if password appears in breach database
 *
 * Returns: >0 = breach count, 0 = not found, -1 = error
 */
int hibp_check_password(const char *password, passwdPolicy *pwpolicy);

/*
 * Hash provider abstraction
 */

/* Function pointer for pluggable SHA-1 implementation */
typedef int (*hibp_hash_fn)(const char *input, size_t len, unsigned char *digest);

/* Set the hash provider, called during initialisation */
void hibp_set_hash_provider(hibp_hash_fn fn);

/*
 * Convert password to uppercase SHA-1 hex string (40 chars + null terminator)
 *
 * Returns: 0 on success, -1 on error
 */
int hibp_sha1_hex(const char *password, char *hex_output);

/*
 * Cache API
 */

/*
 * Initialise the HIBP cache
 *
 * Returns: 0 on success, -1 on error
 */
int hibp_cache_init(size_t max_size, int32_t ttl_seconds);

/* Destroy cache and free all resources */
void hibp_cache_destroy(void);

/*
 * Get cached response for prefix
 *
 * Returns: response data (caller must free), NULL if not cached
 */
char *hibp_cache_get(const char *prefix, size_t *response_size);

/* Store response in cache */
void hibp_cache_put(const char *prefix, const char *response_data, size_t response_size);

/*
 * Test helpers - expose internal functions for unit testing
 */

/* Set mock API response for testing */
void hibp_set_mock_response(const char *response);

/*
 * Test wrapper for hibp_parse_response
 *
 * Returns: breach count if found, 0 if not found, -1 on error
 */
int hibp_parse_response_wrapper(char *response_data, const char *suffix);

/*
 * Test wrapper for hibp_write_callback buffer handling
 *
 * Returns: buffer size on success, -1 on error
 */
int hibp_write_callback_wrapper(const char *data, size_t data_len, size_t chunk_size, char **out_data);
