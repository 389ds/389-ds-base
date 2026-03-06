/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* ********************************************************
eventq.c - Event queue/scheduling system.

There are 3 publicly-accessible entry points:

slapi_eq_once_rel(): cause an event to happen exactly once
slapi_eq_repeat_rel(): cause an event to happen repeatedly
slapi_eq_cancel_rel(): cancel a pending event

There is also an initialization point which must be
called by the server to initialize the event queue system:
eq_start_rel(), and an entry point used to shut down the system:
eq_stop_rel().
*********************************************************** */

#include "slap.h"
#include "prlock.h"
#include "prcvar.h"
#include "prinit.h"

/*
 * Private definition of slapi_eq_context. Only this
 * module (eventq.c) should know about the layout of
 * this structure.
 */
typedef struct _slapi_eq_context
{
    time_t ec_when;
    time_t ec_interval;
    slapi_eq_fn_t ec_fn;
    void *ec_arg;
    Slapi_Eq_Context ec_id;
    struct _slapi_eq_context *ec_next;
} slapi_eq_context;

/*
 * Definition of the event queue.
 */
typedef struct _event_queue
{
    pthread_mutex_t eq_lock;
    pthread_cond_t eq_cv;
    slapi_eq_context *eq_queue;
} event_queue;

/*
 * The event queue itself.
 */
static event_queue eqs_rel = {0};
static event_queue *eq_rel = &eqs_rel;

/*
 * Thread ID of the main thread loop
 */
static PRThread *eq_loop_rel_tid = NULL;

/*
 * Flags used to control startup/shutdown of the event queue
 */
static int eq_rel_running = 0;
static int eq_rel_stopped = 0;
static int eq_rel_initialized = 0;
static pthread_mutex_t ss_rel_lock;
static pthread_cond_t ss_rel_cv;
PRCallOnceType init_once_rel = {0};

/* Forward declarations */
static slapi_eq_context *eq_new_rel(slapi_eq_fn_t fn, void *arg, time_t when, unsigned long interval);
static void eq_enqueue_rel(slapi_eq_context *newec);
static slapi_eq_context *eq_dequeue_rel(time_t now);
static PRStatus eq_create_rel(void);


/* ******************************************************** */


/*
 * slapi_eq_once_rel: cause an event to happen exactly once.
 *
 * Arguments:
 *  fn: the function to call
 *  arg: an argument to pass to the called function
 *  when: the time that the function should be called
 * Returns:
 *  slapi_eq_context - a handle to an opaque object which
 *  the caller can use to refer to this particular scheduled
 *  event.
 */
Slapi_Eq_Context
slapi_eq_once_rel(slapi_eq_fn_t fn, void *arg, time_t when)
{
    slapi_eq_context *tmp;
    PR_ASSERT(eq_rel_initialized);
    if (!eq_rel_stopped) {

        Slapi_Eq_Context id;

        tmp = eq_new_rel(fn, arg, when, 0UL);
        id = tmp->ec_id;

        eq_enqueue_rel(tmp);

        /* After this point, <tmp> may have      */
        /* been freed, depending on the thread   */
        /* scheduling. Too bad             */

        slapi_log_err(SLAPI_LOG_HOUSE, NULL,
                      "added one-time event id %p at time %ld\n",
                      id, when);
        return (id);
    }
    return NULL; /* JCM - Not sure if this should be 0 or something else. */
}


/*
 * slapi_eq_repeat_rel: cause an event to happen repeatedly.
 *
 * Arguments:
 *  fn: the function to call
 *  arg: an argument to pass to the called function
 *  when: the time that the function should first be called
 *  interval: the amount of time (in milliseconds) between
 *            successive calls to the function
 * Returns:
 *  slapi_eq_context - a handle to an opaque object which
 *  the caller can use to refer to this particular scheduled
 */
Slapi_Eq_Context
slapi_eq_repeat_rel(slapi_eq_fn_t fn, void *arg, time_t when, unsigned long interval)
{
    slapi_eq_context *tmp;
    PR_ASSERT(eq_rel_initialized);
    if (!eq_rel_stopped) {
        tmp = eq_new_rel(fn, arg, when, interval);
        eq_enqueue_rel(tmp);
        slapi_log_err(SLAPI_LOG_HOUSE, NULL,
                      "added repeating event id %p at time %ld, interval %lu\n",
                      tmp->ec_id, when, interval);
        return (tmp->ec_id);
    }
    return NULL; /* JCM - Not sure if this should be 0 or something else. */
}


/*
 * slapi_eq_cancel_rel: cancel a pending event.
 * Arguments:
 *  ctx: the context of the event which should be de-scheduled
 */
int
slapi_eq_cancel_rel(Slapi_Eq_Context ctx)
{
    slapi_eq_context **p, *tmp = NULL;
    int found = 0;

    PR_ASSERT(eq_rel_initialized);
    if (!eq_rel_stopped) {
        pthread_mutex_lock(&(eq_rel->eq_lock));
        p = &(eq_rel->eq_queue);
        while (!found && *p != NULL) {
            if ((*p)->ec_id == ctx) {
                tmp = *p;
                *p = (*p)->ec_next;
                slapi_ch_free((void **)&tmp);
                found = 1;
            } else {
                p = &((*p)->ec_next);
            }
        }
        pthread_mutex_unlock(&(eq_rel->eq_lock));
    }
    slapi_log_err(SLAPI_LOG_HOUSE, NULL,
                  "cancellation of event id %p requested: %s\n",
                  ctx, found ? "cancellation succeeded" : "event not found");
    return found;
}


/*
 * Construct a new ec structure
 */
static slapi_eq_context *
eq_new_rel(slapi_eq_fn_t fn, void *arg, time_t when, unsigned long interval)
{
    slapi_eq_context *retptr = (slapi_eq_context *)slapi_ch_calloc(1, sizeof(slapi_eq_context));

    retptr->ec_fn = fn;
    retptr->ec_arg = arg;
    /*
     * retptr->ec_when = when < now ? now : when;
     * we used to amke this check, but it make no sense: when queued, if when
     * has expired, we'll be executed anyway. save the cycles, and just set
     * ec_when.
     */
    retptr->ec_when = when;
    retptr->ec_interval = interval == 0UL ? 0UL : (interval + 999) / 1000;
    retptr->ec_id = (Slapi_Eq_Context)retptr;
    return retptr;
}


/*
 * Add a new event to the event queue.
 */
static void
eq_enqueue_rel(slapi_eq_context *newec)
{
    slapi_eq_context **p;

    PR_ASSERT(NULL != newec);
    pthread_mutex_lock(&(eq_rel->eq_lock));
    /* Insert <newec> in order (sorted by start time) in the list */
    for (p = &(eq_rel->eq_queue); *p != NULL; p = &((*p)->ec_next)) {
        if ((*p)->ec_when > newec->ec_when) {
            break;
        }
    }
    if (NULL != *p) {
        newec->ec_next = *p;
    } else {
        newec->ec_next = NULL;
    }
    *p = newec;
    pthread_cond_signal(&(eq_rel->eq_cv)); /* wake up scheduler thread */
    pthread_mutex_unlock(&(eq_rel->eq_lock));
}


/*
 * If there is an event in the queue scheduled at time
 * <now> or before, dequeue it and return a pointer
 * to it. Otherwise, return NULL.
 */
static slapi_eq_context *
eq_dequeue_rel(time_t now)
{
    slapi_eq_context *retptr = NULL;

    pthread_mutex_lock(&(eq_rel->eq_lock));
    if (NULL != eq_rel->eq_queue && eq_rel->eq_queue->ec_when <= now) {
        retptr = eq_rel->eq_queue;
        eq_rel->eq_queue = retptr->ec_next;
    }
    pthread_mutex_unlock(&(eq_rel->eq_lock));
    return retptr;
}


/*
 * Call all events which are due to run.
 * Note that if we've missed a schedule
 * opportunity, we don't try to catch up
 * by calling the function repeatedly.
 */
static void
eq_call_all_rel(void)
{
    slapi_eq_context *p;
    time_t curtime = slapi_current_rel_time_t();

    while ((p = eq_dequeue_rel(curtime)) != NULL) {
        /* Call the scheduled function */
        p->ec_fn(p->ec_when, p->ec_arg);
        slapi_log_err(SLAPI_LOG_HOUSE, NULL,
                      "Event id %p called at %ld (scheduled for %ld)\n",
                      p->ec_id, curtime, p->ec_when);
        if (0UL != p->ec_interval) {
            /* This is a repeating event. Requeue it. */
            do {
                p->ec_when += p->ec_interval;
            } while (p->ec_when < curtime);
            eq_enqueue_rel(p);
        } else {
            slapi_ch_free((void **)&p);
        }
    }
}


/*
 * The main event queue loop.
 */
static void
eq_loop_rel(void *arg __attribute__((unused)))
{
    slapi_set_thread_name("event-q");
    while (eq_rel_running) {
        time_t curtime = slapi_current_rel_time_t();
        int until;

        pthread_mutex_lock(&(eq_rel->eq_lock));
        while (!((NULL != eq_rel->eq_queue) && (eq_rel->eq_queue->ec_when <= curtime))) {
            if (!eq_rel_running) {
                pthread_mutex_unlock(&(eq_rel->eq_lock));
                goto bye;
            }
            /* Compute new timeout */
            if (NULL != eq_rel->eq_queue) {
                struct timespec current_time = slapi_current_rel_time_hr();
                until = eq_rel->eq_queue->ec_when - curtime;
                current_time.tv_sec += until;
                pthread_cond_timedwait(&eq_rel->eq_cv, &eq_rel->eq_lock, &current_time);
            } else {
                pthread_cond_wait(&eq_rel->eq_cv, &eq_rel->eq_lock);
            }
            curtime = slapi_current_rel_time_t();
        }
        /* There is some work to do */
        pthread_mutex_unlock(&(eq_rel->eq_lock));
        eq_call_all_rel();
    }
bye:
    eq_rel_stopped = 1;
    pthread_mutex_lock(&ss_rel_lock);
    pthread_cond_broadcast(&ss_rel_cv);
    pthread_mutex_unlock(&ss_rel_lock);
}


/*
 * Allocate and initialize the event queue structures.
 */
static PRStatus
eq_create_rel(void)
{
    pthread_condattr_t condAttr;
    int rc = 0;

    /* Init the eventq mutex and cond var */
    if (pthread_mutex_init(&eq_rel->eq_lock, NULL) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_create_rel",
                      "Failed to create lock: error %d (%s)\n",
                      rc, strerror(rc));
        exit(1);
    }
    if ((rc = pthread_condattr_init(&condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_create_rel",
                      "Failed to create new condition attribute variable. error %d (%s)\n",
                      rc, strerror(rc));
        exit(1);
    }
    if ((rc = pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_create_rel",
                      "Cannot set condition attr clock. error %d (%s)\n",
                      rc, strerror(rc));
        exit(1);
    }
    if ((rc = pthread_cond_init(&eq_rel->eq_cv, &condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_create_rel",
                      "Failed to create new condition variable. error %d (%s)\n",
                      rc, strerror(rc));
        exit(1);
    }

    /* Init the "ss" mutex and condition var */
    if (pthread_mutex_init(&ss_rel_lock, NULL) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_create_rel",
                      "Failed to create ss lock: error %d (%s)\n",
                      rc, strerror(rc));
        exit(1);
    }
    if ((rc = pthread_cond_init(&ss_rel_cv, &condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_create_rel",
                      "Failed to create new ss condition variable. error %d (%s)\n",
                      rc, strerror(rc));
        exit(1);
    }
    pthread_condattr_destroy(&condAttr); /* no longer needed */

    eq_rel->eq_queue = NULL;
    eq_rel_initialized = 1;
    return PR_SUCCESS;
}


/*
 * eq_start_rel: start the event queue system.
 *
 * This should be called exactly once. It will start a
 * thread which wakes up periodically and schedules events.
 */
void
eq_start_rel()
{
    PR_ASSERT(eq_rel_initialized);
    eq_rel_running = 1;
    if ((eq_loop_rel_tid = PR_CreateThread(PR_USER_THREAD, (VFP)eq_loop_rel,
                                       NULL, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD,
                                       SLAPD_DEFAULT_THREAD_STACKSIZE)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "eq_start_rel", "eq_loop_rel PR_CreateThread failed\n");
        exit(1);
    }
    slapi_log_err(SLAPI_LOG_HOUSE, NULL, "event queue services have started\n");
}


/*
 * eq_init_rel: initialize the event queue system.
 *
 * This function should be called early in server startup.
 * Once it has been called, the event queue will queue
 * events, but will not fire any events. Once all of the
 * server plugins have been started, the eq_start_rel()
 * function should be called, and events will then start
 * to fire.
 */
void
eq_init_rel()
{
    if (!eq_rel_initialized) {
        if (PR_SUCCESS != PR_CallOnce(&init_once_rel, eq_create_rel)) {
            slapi_log_err(SLAPI_LOG_ERR, "eq_init_rel", "eq_create_rel failed\n");
        }
    }
}


/*
 * eq_stop_rel: shut down the event queue system.
 * Does not return until event queue is fully
 * shut down.
 */
void
eq_stop_rel()
{
    slapi_eq_context *p, *q;

    if (NULL == eq_rel) { /* never started */
        eq_rel_stopped = 1;
        return;
    }

    eq_rel_stopped = 0;
    eq_rel_running = 0;
    /*
     * Signal the eq thread function to stop, and wait until
     * it acknowledges by setting eq_rel_stopped.
     */
    while (!eq_rel_stopped) {
        struct timespec current_time = {0};

        pthread_mutex_lock(&(eq_rel->eq_lock));
        pthread_cond_broadcast(&(eq_rel->eq_cv));
        pthread_mutex_unlock(&(eq_rel->eq_lock));

        pthread_mutex_lock(&ss_rel_lock);
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (current_time.tv_nsec + 100000000 > 1000000000) {
            /* nanoseconds will overflow, adjust the seconds and nanoseconds */
            current_time.tv_sec++;
            /* Add the remainder to nanoseconds */
            current_time.tv_nsec = (current_time.tv_nsec + 100000000) - 1000000000;
        } else {
            current_time.tv_nsec += 100000000; /* 100 ms */
        }
        pthread_cond_timedwait(&ss_rel_cv, &ss_rel_lock, &current_time);
        pthread_mutex_unlock(&ss_rel_lock);
    }
    (void)PR_JoinThread(eq_loop_rel_tid);
    /*
     * XXXggood we don't free the actual event queue data structures.
     * This is intentional, to allow enqueueing/cancellation of events
     * even after event queue services have shut down (these are no-ops).
     * The downside is that the event queue can't be stopped and restarted
     * easily.
     */
    pthread_mutex_lock(&(eq_rel->eq_lock));
    p = eq_rel->eq_queue;
    while (p != NULL) {
        q = p->ec_next;
        slapi_ch_free((void **)&p);
        /* Some ec_arg could get leaked here in shutdown (e.g., replica_name)
         * This can be fixed by specifying a flag when the context is queued.
         * [After 6.2]
         */
        p = q;
    }
    pthread_mutex_unlock(&(eq_rel->eq_lock));
    slapi_log_err(SLAPI_LOG_HOUSE, NULL, "event queue services have shut down\n");
}

/*
 * return arg (ec_arg) only if the context is in the event queue
 */
void *
slapi_eq_get_arg_rel(Slapi_Eq_Context ctx)
{
    slapi_eq_context **p;

    PR_ASSERT(eq_rel_initialized);
    if (eq_rel && !eq_rel_stopped) {
        pthread_mutex_lock(&(eq_rel->eq_lock));
        p = &(eq_rel->eq_queue);
        while (p && *p != NULL) {
            if ((*p)->ec_id == ctx) {
                pthread_mutex_unlock(&(eq_rel->eq_lock));
                return (*p)->ec_arg;
            } else {
                p = &((*p)->ec_next);
            }
        }
        pthread_mutex_unlock(&(eq_rel->eq_lock));
    }
    return NULL;
}
