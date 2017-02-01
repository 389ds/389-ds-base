/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
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
int run_libslapd_tests (void);

/* == The tests == */

/* libslapd */
void test_libslapd_hello(void **state);

/* libslapd-pblock-analytics */
void test_libslapd_pblock_analytics(void **state);


