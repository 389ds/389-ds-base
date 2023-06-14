/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * Copyright (C) 2019 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#include <config.h>
#include <slapi-plugin.h>

/* For cmocka */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* Test runners */
int run_libslapd_tests(void);
int run_plugin_tests(void);

/* == The tests == */

/* libslapd */
void test_libslapd_hello(void **state);

/* libslapd-filter-optimise */
void test_libslapd_filter_optimise(void **state);

/* libslapd-pblock-analytics */
void test_libslapd_pblock_analytics(void **state);

/* libslapd-pblock-v3_compat */
void test_libslapd_pblock_v3c_target_dn(void **state);
void test_libslapd_pblock_v3c_target_sdn(void **state);
void test_libslapd_pblock_v3c_original_target_dn(void **state);
void test_libslapd_pblock_v3c_target_uniqueid(void **state);

/* libslapd-schema-filter-validate */
void test_libslapd_schema_filter_validate_simple(void **state);

/* libslapd-operation-v3_compat */
void test_libslapd_operation_v3c_target_spec(void **state);

/* libslapd-counters-atomic */

void test_libslapd_counters_atomic_usage(void **state);
void test_libslapd_counters_atomic_overflow(void **state);

/* libslapd-pal-meminfo */

void test_libslapd_pal_meminfo(void **state);
void test_libslapd_util_cachesane(void **state);

/* plugins */

void test_plugin_hello(void **state);

/* plugin-pwdstorage-pbkdf2 */

int test_plugin_pwdstorage_nss_setup(void **state);
int test_plugin_pwdstorage_nss_stop(void **state);

void test_plugin_pwdstorage_pbkdf2_auth(void **state);
void test_plugin_pwdstorage_pbkdf2_rounds(void **state);
