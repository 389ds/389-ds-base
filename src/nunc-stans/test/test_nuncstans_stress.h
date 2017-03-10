/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* For cmocka */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <nunc-stans.h>

#include <stdio.h>
#include <signal.h>

#include <syslog.h>
#include <string.h>
#include <inttypes.h>

#include <time.h>
#include <sys/time.h>

#include <assert.h>

struct test_params {
    int32_t client_thread_count;
    int32_t server_thread_count;
    int32_t jobs;
    int32_t test_timeout;
};

int ns_stress_teardown(void **state);
void ns_stress_test(void **state);

