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

/* Access internal CSNGen structure to inject mock clock */
/* Must match exact layout from csngen.c lines 56-72 */
typedef struct {
    PRUint16 rid;           /* ReplicaId is PRUint16 */
    time_t sampled_time;
    time_t local_offset;
    time_t remote_offset;
    PRUint16 seq_num;
} test_csngen_state;

typedef struct {
    Slapi_RWLock *lock;     /* Must be Slapi_RWLock*, not void* - order matters! */
    DataList *list;
} test_callback_list;

struct csngen {
    test_csngen_state state;                  /* csngen_state state */
    int32_t (*gettime)(struct timespec *tp);  /* function pointer */
    test_callback_list callbacks;             /* callback_list callbacks */
    Slapi_RWLock *lock;                       /* Slapi_RWLock *lock */
};

void
test_libslapd_csngen_clock_failure(void **state __attribute__((unused)))
{
    CSNGen *gen = csngen_new(1, NULL);
    struct csngen *gen_internal = (struct csngen *)gen;
    CSN *csn = NULL;
    int rc;
    
    assert_non_null(gen);
    gen_internal->gettime = mock_clock_gettime_fail;
    
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
    struct csngen *gen_internal = (struct csngen *)gen;
    CSN *csn1 = NULL;
    CSN *csn2 = NULL;
    int rc;
    
    assert_non_null(gen);
    gen_internal->gettime = mock_clock_gettime_fail;
    
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
    struct csngen *gen_internal = (struct csngen *)gen;
    CSN *csn1 = NULL;
    CSN *csn2 = NULL;
    int rc;
    
    assert_non_null(gen);
    gen_internal->gettime = mock_clock_gettime_fail;
    
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
    struct csngen *gen_internal = (struct csngen *)gen;
    CSN *csn = NULL;
    int rc;
    
    assert_non_null(gen);
    gen_internal->gettime = mock_clock_gettime_fail;
    
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
    struct csngen *gen_internal = (struct csngen *)gen;
    CSN *csn1 = NULL;
    CSN *csn2 = NULL;
    CSN *csn3 = NULL;
    int rc;
    time_t saved_time;
    
    assert_non_null(gen);
    gen_internal->gettime = mock_clock_gettime_fail;
    
    mock_clock_should_fail = 0;
    mock_clock_time = 0;
    mock_clock_jump = 0;
    
    rc = csngen_new_csn(gen, &csn1, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    
    saved_time = mock_clock_time;
    mock_clock_time = saved_time;
    
    rc = csngen_new_csn(gen, &csn2, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    assert_true(csn_compare(csn1, csn2) < 0);
    
    mock_clock_time = saved_time + 1;
    rc = csngen_new_csn(gen, &csn3, PR_FALSE);
    assert_int_equal(rc, CSN_SUCCESS);
    assert_true(csn_compare(csn2, csn3) < 0);
    
    csn_free(&csn1);
    csn_free(&csn2);
    csn_free(&csn3);
    csngen_free(&gen);
}
