/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2016  Red Hat
 * see files 'COPYING' and 'COPYING.openssl' for use and warranty
 * information
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Additional permission under GPLv3 section 7:
 * 
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL, or a modified version of OpenSSL licensed
 * under the OpenSSL license
 * (https://www.openssl.org/source/license.html), the licensors of this
 * Program grant you additional permission to convey the resulting
 * work. Corresponding Source for a non-source form of such a
 * combination shall include the source code for the parts that are
 * licensed under the OpenSSL license as well as that of the covered
 * work.
 * --- END COPYRIGHT BLOCK ---
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* For cmocka */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* For string and time manipulation in tests */
#include <unistd.h>
#include <string.h>

/* For signaling tests */
#include <signal.h>

/* For NS itself */
#include <nunc-stans.h>
/* We need the internal headers for state checks */
#include "../ns/ns_event_fw.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif


static int cb_check = 0;
static PRLock *cb_lock = NULL;
static PRCondVar *cb_cond = NULL;

void
ns_test_logger(int priority __attribute__((unused)), const char *fmt, va_list varg)
{
    // Should we do anything with priority?
    vprintf(fmt, varg);
}

/* All our other tests will use this in some form. */
static int
ns_test_setup(void **state)
{
    /* Ensure that we can create a new nunc-stans instance. */
    struct ns_thrpool_t *tp = NULL;
    struct ns_thrpool_config ns_config;
    /* Reset the callback check */
    cb_check = 0;
    /* Create the cond var the CB check will use. */
    cb_lock = PR_NewLock();
    cb_cond = PR_NewCondVar(cb_lock);

    ns_thrpool_config_init(&ns_config);

    ns_config.log_fct = ns_test_logger;
    ns_config.max_threads = 4;
    tp = ns_thrpool_new(&ns_config);
    assert_non_null(tp);

    *state = tp;

    return 0;
}

static int
ns_test_teardown(void **state)
{
    struct ns_thrpool_t *tp = *state;
    ns_thrpool_shutdown(tp);
    assert_int_equal(ns_thrpool_wait(tp), 0);

    ns_thrpool_destroy(tp);

    PR_DestroyCondVar(cb_cond);
    PR_DestroyLock(cb_lock);

    return 0;
}

static void
ns_init_test_job_cb(struct ns_job_t *job __attribute__((unused)))
{
    cb_check += 1;
    PR_Lock(cb_lock);
    PR_NotifyCondVar(cb_cond);
    PR_Unlock(cb_lock);
}

static void
ns_init_disarm_job_cb(struct ns_job_t *job)
{
    if (ns_job_done(job) == PR_SUCCESS) {
        cb_check = 1;
    } else {
        assert_int_equal(1,0);
    }
    PR_Lock(cb_lock);
    PR_NotifyCondVar(cb_cond);
    /* Disarm ourselves */
    PR_Unlock(cb_lock);
}

static void
ns_init_do_nothing_cb(struct ns_job_t *job __attribute__((unused)))
{
    /* I mean it, do nothing! */
    return;
}

static void
ns_init_test(void **state)
{
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    PR_Lock(cb_lock);
    assert_int_equal(
        ns_add_job(tp, NS_JOB_NONE|NS_JOB_THREAD, ns_init_test_job_cb, NULL, &job),
        0);

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);

    assert_int_equal(cb_check, 1);

    /* Once the job is done, it's not in the event queue, and it's complete */
    /* We have to stall momentarily to let the work_job_execute release the job to us */
    PR_Sleep(PR_SecondsToInterval(1));
    assert_int_equal(ns_job_done(job), 0);
}

static void
ns_set_data_test(void **state)
{
    /* Add a job with data */
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    char *data = malloc(6);

    strcpy(data, "first");

    PR_Lock(cb_lock);
    assert_int_equal(
        ns_add_job(tp, NS_JOB_NONE|NS_JOB_THREAD, ns_init_test_job_cb, data, &job),
        0);

    /* Let the job run */
    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);

    /* Check that the data is correct */
    char *retrieved = (char *)ns_job_get_data(job);
    assert_int_equal(strcmp("first", retrieved), 0);

    free(retrieved);

    /* set new data */
    data = malloc(7);
    strcpy(data, "second");

    while (job->state != NS_JOB_WAITING) {
        PR_Sleep(PR_MillisecondsToInterval(50));
    }
    ns_job_set_data(job, data);

    /* Rearm, and let it run again. */
    PR_Lock(cb_lock);
    ns_job_rearm(job);
    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);

    /* Make sure it's now what we expect */
    retrieved = (char *)ns_job_get_data(job);
    assert_int_equal(strcmp("second", retrieved), 0);

    free(retrieved);

    /* Because the job is not queued, we must free it */
    /*
     * It's possible here, that the worker thread is still processing state
     * as a result, we aren't the owning thread, and ns_job_done fails.
     * So we actually have to loop on freeing this until it's released to
     * waiting. we might need a load barrier here ...
     */

    while (job->state != NS_JOB_WAITING) {
        PR_Sleep(PR_MillisecondsToInterval(50));
    }

    assert_int_equal(ns_job_done(job), 0);
}

static void
ns_job_done_cb_test(void **state)
{
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    PR_Lock(cb_lock);
    assert_int_equal(
        ns_create_job(tp, NS_JOB_NONE|NS_JOB_THREAD, ns_init_do_nothing_cb, &job),
        0);

    ns_job_set_done_cb(job, ns_init_test_job_cb);
    /* Remove it */
    assert_int_equal(ns_job_done(job), 0);

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);

    assert_int_equal(cb_check, 1);

}

static void
ns_init_rearm_job_cb(struct ns_job_t *job)
{
    if (ns_job_rearm(job) == PR_FAILURE) {
        cb_check = 1;
        /* we failed to re-arm as expected, let's go away ... */
        assert_int_equal(ns_job_done(job), 0);
    } else {
        assert_int_equal(1, 0);
    }
    PR_Lock(cb_lock);
    PR_NotifyCondVar(cb_cond);
    /* Disarm ourselves */
    PR_Unlock(cb_lock);
}

static void
ns_job_persist_rearm_ignore_test(void **state)
{
    /* Test that rearm ignores the persistent job. */
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    PR_Lock(cb_lock);
    assert_int_equal(
        ns_create_job(tp, NS_JOB_NONE|NS_JOB_THREAD|NS_JOB_PERSIST, ns_init_rearm_job_cb, &job),
        0);

    /* This *will* arm the job, and will trigger the cb. */
    assert_int_equal(ns_job_rearm(job), 0);
    /* 
     * Now when the CB fires, it will *try* to rearm, but will fail, so we
     * should see only 1 in the cb_check.
     */

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);

    /* If we fail to rearm, this is set to 1 Which is what we want. */
    assert_int_equal(cb_check, 1);

}

static void
ns_job_persist_disarm_test(void **state)
{
    /* Make a persistent job */
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    assert_int_equal(
        ns_create_job(tp, NS_JOB_NONE|NS_JOB_PERSIST, ns_init_disarm_job_cb, &job),
        0);

    assert_int_equal(ns_job_rearm(job), 0);

    /* In the callback it should disarm */
    PR_Lock(cb_lock);
    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);
    /* Make sure it did */
    assert_int_equal(cb_check, 1);
}

/*
 * This tests a very specific issue in the directory server code. It's possible
 * that a job will try to disarm itself from within the worker thread. This can
 * race, as the event thread will get the work, free the job. The worker then
 * returns from the fn, and will heap-use-after-free because the job was
 * yanked from under it. To test this, you *will* need ASAN enabled to detect
 * the failure condition (it will crash and burn)
 *
 * The bug happens in ns_add_job, when it calls the cb.
 * The moment we add this, it will then trigger the job to run in thrpool event_cb
 * this of course will call job->func(job), in this case ns_init_race_done_job_cb.
 * Because the race_done job sends to ns_job_done, this will free job in the event
 * thread, but then the work thread will return after the PR_Sleep to event_cb.
 * At this point, the wt attempts to access job->type and job->state, to determine
 * if the job needs rearm. Because the et freed job, this is now a use after
 * free.
 */
static void
ns_init_race_done_job_cb(struct ns_job_t *job)
{
    cb_check += 1;
    ns_job_done(job);
    /* We need to sleep to let the job race happen */
    PR_Sleep(PR_SecondsToInterval(2));
    PR_Lock(cb_lock);
    PR_NotifyCondVar(cb_cond);
    /* Disarm ourselves */
    PR_Unlock(cb_lock);
}

static void
ns_job_race_done_test(void **state)
{
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    PR_Lock(cb_lock);
    assert_int_equal(
        ns_add_job(tp, NS_JOB_NONE|NS_JOB_THREAD, ns_init_race_done_job_cb, NULL, &job),
        0);

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(5));
    PR_Unlock(cb_lock);

    assert_int_equal(cb_check, 1);

}

/*
 * This tests that when we raise a signal, we catch it and handle it correctly.
 */

static void
ns_job_signal_cb_test(void **state)
{
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;

    PR_Lock(cb_lock);
    assert_int_equal(
        ns_add_signal_job(tp, SIGUSR1, NS_JOB_SIGNAL, ns_init_test_job_cb, NULL, &job),
        0);

    /* The addition of the signal job to the event fw is async */
    PR_Sleep(PR_SecondsToInterval(2));
    /* Send the signal ... */
    raise(SIGUSR1);

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    PR_Unlock(cb_lock);

    assert_int_equal(cb_check, 1);

    /* Remove the signal job now */
    assert_int_equal(ns_job_done(job), 0);
}

/*
 * Test that given a timeout of -1, we fail to create a job.
 */

static void
ns_job_neg_timeout_test(void **state)
{
    struct ns_thrpool_t *tp = *state;

    struct timeval tv = { -1, 0 };

    PR_ASSERT(PR_FAILURE == ns_add_io_timeout_job(tp, 0, &tv, NS_JOB_THREAD, ns_init_do_nothing_cb, NULL, NULL));

    PR_ASSERT(PR_FAILURE == ns_add_timeout_job(tp, &tv, NS_JOB_THREAD, ns_init_do_nothing_cb, NULL, NULL));

}

/*
 * Test that a timeout job fires a within a time window
 */

static void
ns_timer_job_cb(struct ns_job_t *job)
{
    cb_check += 1;
    ns_job_done(job);
    PR_Lock(cb_lock);
    PR_NotifyCondVar(cb_cond);
    /* Disarm ourselves */
    PR_Unlock(cb_lock);
}

static void
ns_job_timer_test(void **state)
{
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;
    struct timeval tv = { 2, 0 };

    PR_Lock(cb_lock);
    assert_true(ns_add_timeout_job(tp, &tv, NS_JOB_THREAD, ns_timer_job_cb, NULL, &job) == PR_SUCCESS);

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(1));
    assert_int_equal(cb_check, 0);

    PR_WaitCondVar(cb_cond, PR_SecondsToInterval(2));
    PR_Unlock(cb_lock);
    assert_int_equal(cb_check, 1);

}

/*
 * Test that within a window, a looping timeout job has fired greater than X times.
 */

static void
ns_timer_persist_job_cb(struct ns_job_t *job)
{
    cb_check += 1;
    if (cb_check < 10) {
        ns_job_rearm(job);
    } else {
        ns_job_done(job);
    }
}

static void
ns_job_timer_persist_test(void **state)
{
    struct ns_thrpool_t *tp = *state;
    struct ns_job_t *job = NULL;
    struct timeval tv = { 1, 0 };

    PR_Lock(cb_lock);
    assert_true(ns_add_timeout_job(tp, &tv, NS_JOB_THREAD, ns_timer_persist_job_cb, NULL, &job) == PR_SUCCESS);

    PR_Sleep(PR_SecondsToInterval(5));

    assert_true(cb_check <= 6);

    PR_Sleep(PR_SecondsToInterval(6));

    assert_int_equal(cb_check, 10);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(ns_init_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_set_data_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_done_cb_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_persist_rearm_ignore_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_persist_disarm_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_race_done_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_signal_cb_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_neg_timeout_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_timer_test,
                                        ns_test_setup,
                                        ns_test_teardown),
        cmocka_unit_test_setup_teardown(ns_job_timer_persist_test,
                                        ns_test_setup,
                                        ns_test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}



