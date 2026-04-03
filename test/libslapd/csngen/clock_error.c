/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "../../test_slapd.h"
#include <slapi-private.h>
#include <time.h>

/* Mock clock globals */
static int mock_clock_should_fail = 0;
static time_t mock_clock_time = 0;
static time_t mock_clock_jump = 0;

static int32_t
mock_clock_gettime_fail(struct timespec *tp)
{
    if (mock_clock_should_fail) {
        return -1;
    }
    
    if (mock_clock_time == 0) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        mock_clock_time = now.tv_sec;
    } else if (mock_clock_jump != 0) {
        mock_clock_time += mock_clock_jump;
        mock_clock_jump = 0;
    } else {
        mock_clock_time += 1;
    }
    
    tp->tv_sec = mock_clock_time;
    tp->tv_nsec = 0;
    return 0;
}

void
test_libslapd_csngen_clock_failure(void **state __attribute__((unused)))
{
    CSNGen *gen = csngen_new(1, NULL);
    CSN *csn = NULL;
    int rc;

    assert_non_null(gen);
    csngen_set_gettime(gen, mock_clock_gettime_fail);
    
    mock_clock_should_fail = 0;
    mock_clock_time = 0;
    mock_clock_jump = 0;
    
    rc = csngen_new_csn(gen, &csn, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    csn_free(&csn);
    
    mock_clock_should_fail = 1;
    rc = csngen_new_csn(gen, &csn, PR_FALSE);
    assert_int_equal(rc, CSN_TIME_ERROR);
    /* Note: csn may not be set to NULL on error, just check return code */
    
    csngen_free(&gen);
}

void
test_libslapd_csngen_large_time_jump(void **state __attribute__((unused)))
{
    CSNGen *gen = csngen_new(1, NULL);
    CSN *csn1 = NULL;
    CSN *csn2 = NULL;
    int rc;

    assert_non_null(gen);
    csngen_set_gettime(gen, mock_clock_gettime_fail);
    
    mock_clock_should_fail = 0;
    mock_clock_time = 0;
    mock_clock_jump = 0;
    
    rc = csngen_new_csn(gen, &csn1, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    
    mock_clock_jump = 31 * 24 * 60 * 60;
    rc = csngen_new_csn(gen, &csn2, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    assert_true(csn_compare(csn1, csn2) < 0);
    
    csn_free(&csn1);
    csn_free(&csn2);
    csngen_free(&gen);
}

void
test_libslapd_csngen_time_backwards(void **state __attribute__((unused)))
{
    CSNGen *gen = csngen_new(1, NULL);
    CSN *csn1 = NULL;
    CSN *csn2 = NULL;
    int rc;

    assert_non_null(gen);
    csngen_set_gettime(gen, mock_clock_gettime_fail);
    
    mock_clock_should_fail = 0;
    mock_clock_time = 0;
    mock_clock_jump = 0;
    
    rc = csngen_new_csn(gen, &csn1, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    
    mock_clock_jump = -3600;
    rc = csngen_new_csn(gen, &csn2, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    assert_true(csn_compare(csn1, csn2) < 0);
    
    csn_free(&csn1);
    csn_free(&csn2);
    csngen_free(&gen);
}

void
test_libslapd_csngen_multiple_clock_failures(void **state __attribute__((unused)))
{
    CSNGen *gen = csngen_new(1, NULL);
    CSN *csn = NULL;
    int rc;

    assert_non_null(gen);
    csngen_set_gettime(gen, mock_clock_gettime_fail);
    
    mock_clock_should_fail = 0;
    mock_clock_time = 0;
    mock_clock_jump = 0;
    
    rc = csngen_new_csn(gen, &csn, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    csn_free(&csn);
    
    mock_clock_should_fail = 1;
    for (int i = 0; i < 5; i++) {
        rc = csngen_new_csn(gen, &csn, PR_FALSE);
        assert_int_equal(rc, CSN_TIME_ERROR);
        /* Note: csn may not be set to NULL on error, just check return code */
    }
    
    mock_clock_should_fail = 0;
    rc = csngen_new_csn(gen, &csn, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    csn_free(&csn);
    
    csngen_free(&gen);
}

void
test_libslapd_csngen_seqnum_handling(void **state __attribute__((unused)))
{
    CSNGen *gen = csngen_new(1, NULL);
    CSN *csn1 = NULL;
    CSN *csn2 = NULL;
    CSN *csn3 = NULL;
    int rc;

    assert_non_null(gen);
    csngen_set_gettime(gen, mock_clock_gettime_fail);

    mock_clock_should_fail = 0;
    mock_clock_time = 0;
    mock_clock_jump = 0;

    rc = csngen_new_csn(gen, &csn1, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);

    rc = csngen_new_csn(gen, &csn2, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    assert_true(csn_compare(csn1, csn2) < 0);

    rc = csngen_new_csn(gen, &csn3, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    assert_true(csn_compare(csn2, csn3) < 0);

    csn_free(&csn1);
    csn_free(&csn2);
    csn_free(&csn3);
    csngen_free(&gen);
}
