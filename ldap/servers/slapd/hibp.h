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
 * Function pointer for pluggable SHA-1 implementation
 * Allows for different hash implementations (FIPS vs non FIPS)
 */
typedef int (*hibp_hash_fn)(const char *input, size_t len, unsigned char *digest);

/* Set the hash provider, called during initialisation */
void hibp_set_hash_provider(hibp_hash_fn fn);

/* Initialise the HIBP subsystem */
int hibp_init(void);

/*
 * Check if password appears in breach database
 * Returns: >0 = breach count, 0 = not found, -1 = error
 */
int hibp_check_password(const char *password, passwdPolicy *pwpolicy);

/*
 * Convert password to uppercase SHA-1 hex string (40 chars + null terminator)
 * Returns: 0 on success, -1 on error
 */
int hibp_sha1_hex(const char *password, char *hex_output);

/* Test wrapper, expose hibp_parse_response for testing
 * Returns: breach count if found, 0 if not found, -1 on error
 */
int hibp_parse_response_wrapper(char *response_data, const char *suffix);

/* Test wrapper, expose hibp_write_callback for testing
 * Returns: buffer size on success, -1 on error
 */
int hibp_write_callback_wrapper(const char *data, size_t data_len, size_t chunk_size, char **out_data);

/* Set mock API response for testing */
void hibp_set_mock_response(const char *response);

