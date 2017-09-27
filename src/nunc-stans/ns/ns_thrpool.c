/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2015  Red Hat
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
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <nspr.h>
#include <private/pprio.h>
#include "ns_event_fw.h"

/* SDS contains the lock free queue wrapper */
#include <sds.h>

#include <assert.h>


/*
 * Threadpool
 */
struct ns_thrpool_t
{
    sds_lqueue *work_q;
    sds_lqueue *event_q;
    int32_t shutdown;
    int32_t shutdown_event_loop;
    pthread_cond_t work_q_cv;
    pthread_mutex_t work_q_lock;
    sds_queue *thread_stack;
    uint32_t thread_count;
    pthread_t event_thread;
    PRFileDesc *event_q_wakeup_pipe_read;
    PRFileDesc *event_q_wakeup_pipe_write;
    ns_job_t *event_q_wakeup_job;
    ns_event_fw_t *ns_event_fw;
    ns_event_fw_ctx_t *ns_event_fw_ctx;
    size_t stacksize;
};

struct ns_thread_t
{
    pthread_t thr;           /* the thread */
    struct ns_thrpool_t *tp; /* pointer back to thread pool */
};

#define ERRNO_WOULD_BLOCK(iii) (iii == EWOULDBLOCK) || (iii == EAGAIN)

/* Forward declarations. */
static void internal_ns_job_done(ns_job_t *job);
static void internal_ns_job_rearm(ns_job_t *job);
static void work_job_execute(ns_job_t *job);
static void event_q_notify(ns_job_t *job);
static void work_q_notify(ns_job_t *job);

/* logging function pointers */
static void (*logger)(int, const char *, va_list) = NULL;
static void (*log_start)(void) = NULL;
static void (*log_close)(void) = NULL;

/* memory function pointers */
static void *(*malloc_fct)(size_t) = NULL;
static void *(*memalign_fct)(size_t size, size_t alignment) = NULL;
static void *(*calloc_fct)(size_t, size_t) = NULL;
static void *(*realloc_fct)(void *, size_t) = NULL;
static void (*free_fct)(void *) = NULL;

/* syslog functions */
static void
ns_syslog(int priority, const char *fmt, va_list varg)
{
    vsyslog(priority, fmt, varg);
}

static void
ns_syslog_start(void)
{
    openlog("nunc-stans", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
}

static void
ns_syslog_close(void)
{
    closelog();
}

/* default memory functions */
static void *
os_malloc(size_t size)
{
    return malloc(size);
}

static void *
os_memalign_malloc(size_t size, size_t alignment)
{
    void *ptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
}

static void *
os_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

static void *
os_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void
os_free(void *ptr)
{
    free(ptr);
}

int32_t
ns_thrpool_is_shutdown(struct ns_thrpool_t *tp)
{
    int32_t result = 0;
#ifdef ATOMIC_64BIT_OPERATIONS
    __atomic_load(&(tp->shutdown), &result, __ATOMIC_ACQUIRE);
#else
    result = PR_AtomicAdd(&(tp->shutdown), 0);
#endif
    return result;
}

int32_t
ns_thrpool_is_event_shutdown(struct ns_thrpool_t *tp)
{
    int32_t result = 0;
#ifdef ATOMIC_64BIT_OPERATIONS
    __atomic_load(&(tp->shutdown_event_loop), &result, __ATOMIC_ACQUIRE);
#else
    result = PR_AtomicAdd(&(tp->shutdown_event_loop), 0);
#endif
    return result;
}

static int32_t
validate_event_timeout(struct timeval *tv)
{
    if (tv->tv_sec < 0 || tv->tv_usec < 0) {
        /* If we get here, you have done something WRONG */
        return 1;
    }
    return 0;
}

static void
job_queue_cleanup(void *arg)
{
    struct ns_job_t *job = (ns_job_t *)arg;
    if (job != NULL) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_queue_job_cleanup: destroying job %x\n", job);
#endif
        internal_ns_job_done(job);
    }
}

static void
internal_ns_job_done(ns_job_t *job)
{
    pthread_mutex_lock(job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "internal_ns_job_done %x state %d moving to NS_JOB_DELETED\n", job, job->state);
#endif
    /* In theory, due to the monitor placement, this should never be able to be seen by any other thread ... */
    job->state = NS_JOB_DELETED;

    if (job->ns_event_fw_fd) {
        job->tp->ns_event_fw->ns_event_fw_io_event_done(job->tp->ns_event_fw_ctx, job);
    }
    if (job->ns_event_fw_time) {
        job->tp->ns_event_fw->ns_event_fw_timer_event_done(job->tp->ns_event_fw_ctx, job);
    }
    if (job->ns_event_fw_sig) {
        job->tp->ns_event_fw->ns_event_fw_signal_event_done(job->tp->ns_event_fw_ctx, job);
    }

    if (job->fd && !NS_JOB_IS_PRESERVE_FD(job->job_type)) {
        PR_Close(job->fd);
    } /* else application is responsible for fd */

    if (job->done_cb != NULL) {
        job->done_cb(job);
    }

    pthread_mutex_unlock(job->monitor);
    pthread_mutex_destroy(job->monitor);
    ns_free(job->monitor);

    ns_free(job);
}

/* Is there a way to get a status back from here? */
static void
internal_ns_job_rearm(ns_job_t *job)
{
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state == NS_JOB_NEEDS_ARM);
/* Don't think I need to check persistence here, it could be the first arm ... */
#ifdef DEBUG
    ns_log(LOG_DEBUG, "internal_ns_rearm_job %x state %d moving to NS_JOB_ARMED\n", job, job->state);
#endif
    job->state = NS_JOB_ARMED;

    /* I think we need to check about is_shutdown here? */

    if (NS_JOB_IS_IO(job->job_type) || NS_JOB_IS_TIMER(job->job_type) || NS_JOB_IS_SIGNAL(job->job_type)) {
        event_q_notify(job);
    } else {
        /* if this is a non event task, just queue it on the work q */
        /* Prevents an un-necessary queue / dequeue to the event_q */
        work_q_notify(job);
    }
    pthread_mutex_unlock(job->monitor);
}

static void
work_job_execute(ns_job_t *job)
{
    /*
     * when we pull out a job to work on it, we do it here rather than calling
     * job function directly.
     *
     * DANGER: After this is called, you must NOT ACCESS job again. It MAY be
     * DELETED! Crashes abound, you have been warned ...
     */
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "work_job_execute %x state %d moving to NS_JOB_RUNNING\n", job, job->state);
#endif
    job->state = NS_JOB_RUNNING;
    /* Do the work. */
    PR_ASSERT(job->func);
    job->func(job);

    /* Only if !threaded job, and persistent, we automatically tell us to rearm */
    if (!NS_JOB_IS_THREAD(job->job_type) && NS_JOB_IS_PERSIST(job->job_type) && job->state == NS_JOB_RUNNING) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "work_job_execute PERSIST and RUNNING, remarking %x as NS_JOB_NEEDS_ARM\n", job);
#endif
        job->state = NS_JOB_NEEDS_ARM;
    }

    if (job->state == NS_JOB_NEEDS_DELETE) {
/* Send it to the job mincer! */
#ifdef DEBUG
        ns_log(LOG_DEBUG, "work_job_execute %x state %d job func complete, sending to job_done...\n", job, job->state);
#endif
        pthread_mutex_unlock(job->monitor);
        internal_ns_job_done(job);
        /* MUST NOT ACCESS JOB AGAIN.*/
    } else if (job->state == NS_JOB_NEEDS_ARM) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "work_job_execute %x state %d job func complete, sending to rearm...\n", job, job->state);
#endif
        /* Rearm the job! */
        pthread_mutex_unlock(job->monitor);
        internal_ns_job_rearm(job);
    } else {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "work_job_execute %x state %d job func complete move to NS_JOB_WAITING\n", job, job->state);
#endif
        /* This should never occur? What's going on ... */
        PR_ASSERT(!NS_JOB_IS_PERSIST(job->job_type));
        /* We are now idle, set waiting. */
        job->state = NS_JOB_WAITING;
        pthread_mutex_unlock(job->monitor);
    }
    /* MUST NOT ACCESS JOB AGAIN */
}

static void
work_q_wait(ns_thrpool_t *tp)
{
    pthread_mutex_lock(&(tp->work_q_lock));
    pthread_cond_wait(&(tp->work_q_cv), &(tp->work_q_lock));
    pthread_mutex_unlock(&(tp->work_q_lock));
}

static void
work_q_notify(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "work_q_notify %x state %d\n", job, job->state);
#endif
    PR_ASSERT(job->state == NS_JOB_ARMED);
    if (job->state != NS_JOB_ARMED) {
        /* Maybe we should return some error here? */
        ns_log(LOG_ERR, "work_q_notify %x state %d is not ARMED, cannot queue!\n", job, job->state);
        pthread_mutex_unlock(job->monitor);
        return;
    }
    /* MUST NOT ACCESS job after enqueue. So we stash tp.*/
    ns_thrpool_t *ltp = job->tp;
    pthread_mutex_unlock(job->monitor);
    sds_lqueue_enqueue(ltp->work_q, (void *)job);
    pthread_mutex_lock(&(ltp->work_q_lock));
    pthread_cond_signal(&(ltp->work_q_cv));
    pthread_mutex_unlock(&(ltp->work_q_lock));
    PR_Sleep(PR_INTERVAL_NO_WAIT); /* yield to allow worker thread to pick up job */
}

/*
 * worker thread function
 */
static void *
worker_thread_func(void *arg)
{
    ns_thread_t *thr = (ns_thread_t *)arg;
    ns_thrpool_t *tp = thr->tp;
    sds_result result = SDS_SUCCESS;
    int_fast32_t is_shutdown = 0;

    /* Get ready to use lock free ds */
    sds_lqueue_tprep(tp->work_q);

    /*
     * Execute jobs until shutdown is set and the queues are empty.
     */
    while (!is_shutdown) {
        ns_job_t *job = NULL;
        result = sds_lqueue_dequeue(tp->work_q, (void **)&job);
        /* Don't need monitor here, job_dequeue barriers the memory for us. Job will be valid */
        /* Is it possible for a worker thread to get stuck here during shutdown? */
        if (result == SDS_LIST_EXHAUSTED) {
            work_q_wait(tp);
        } else if (result == SDS_SUCCESS && job != NULL) {
            /* Even if we are shutdown here, we can process a job. */
            /* Should we just keep dequeing until we exhaust the list? */
            if (NS_JOB_IS_SHUTDOWN_WORKER(job->job_type)) {
                ns_log(LOG_INFO, "worker_thread_func notified to shutdown!\n");
                internal_ns_job_done(job);
                is_shutdown = 1;
            } else {
                work_job_execute(job);
            }
            /* MUST NOT ACCESS JOB FROM THIS POINT */
        } else {
            ns_log(LOG_ERR, "worker_thread_func encountered a recoverable issue during processing of the queue\n");
        }
    }

    ns_log(LOG_INFO, "worker_thread_func shutdown complete!\n");
    /* With sds, it cleans the thread on join automatically. */
    return NULL;
}

/*
 * Add a new event, or modify or delete an existing event
 */
static void
update_event(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "update_event %x state %d\n", job, job->state);
#endif
    PR_ASSERT(job->state == NS_JOB_NEEDS_DELETE || job->state == NS_JOB_ARMED);
    if (job->state == NS_JOB_NEEDS_DELETE) {
        pthread_mutex_unlock(job->monitor);
        internal_ns_job_done(job);
        return;
    } else if (NS_JOB_IS_IO(job->job_type) || job->ns_event_fw_fd) {
        if (!job->ns_event_fw_fd) {
            job->tp->ns_event_fw->ns_event_fw_add_io(job->tp->ns_event_fw_ctx, job);
        } else {
            job->tp->ns_event_fw->ns_event_fw_mod_io(job->tp->ns_event_fw_ctx, job);
        }
        pthread_mutex_unlock(job->monitor);
        /* We need these returns to prevent a race on the next else if condition when we release job->monitor */
        return;
    } else if (NS_JOB_IS_TIMER(job->job_type) || job->ns_event_fw_time) {
        if (!job->ns_event_fw_time) {
            job->tp->ns_event_fw->ns_event_fw_add_timer(job->tp->ns_event_fw_ctx, job);
        } else {
            job->tp->ns_event_fw->ns_event_fw_mod_timer(job->tp->ns_event_fw_ctx, job);
        }
        pthread_mutex_unlock(job->monitor);
        return;
    } else if (NS_JOB_IS_SIGNAL(job->job_type) || job->ns_event_fw_sig) {
        if (!job->ns_event_fw_sig) {
            job->tp->ns_event_fw->ns_event_fw_add_signal(job->tp->ns_event_fw_ctx, job);
        } else {
            job->tp->ns_event_fw->ns_event_fw_mod_signal(job->tp->ns_event_fw_ctx, job);
        }
        pthread_mutex_unlock(job->monitor);
        return;
    } else {
        /* It's a "run now" job. */
        if (NS_JOB_IS_THREAD(job->job_type)) {
            pthread_mutex_unlock(job->monitor);
            work_q_notify(job);
        } else {
            pthread_mutex_unlock(job->monitor);
            event_q_notify(job);
        }
    }

    return;
}

static void
event_q_wait(ns_thrpool_t *tp __attribute__((unused)))
{
    /* unused for now */
    /* the main event loop will do our "waiting" - waiting for events
       to happen, or we can trigger an event to "wakeup" the event
       loop (see event_q_notify) */
}

static void
event_q_wake(ns_thrpool_t *tp)
{
    int32_t len;

/* Rather than trying to make  anew event, tell the event loop to exit with no
     * events.
     */
#ifdef DEBUG
    ns_log(LOG_DEBUG, "event_q_wake attempting to wake event queue.\n");
#endif

    /* NSPR I/O doesn't allow non-blocking signal pipes, so use write instead of PR_Write */
    len = write(PR_FileDesc2NativeHandle(tp->event_q_wakeup_pipe_write),
                "a", 1);
    if (1 != len) {
        if ((errno == 0) || ERRNO_WOULD_BLOCK(errno)) {
            ns_log(LOG_DEBUG, "Write blocked for wakeup pipe - ignore %d\n",
                   errno);
        } else {
            ns_log(LOG_ERR, "Error: could not write wakeup pipe: %d:%s\n",
                   errno, PR_ErrorToString(errno, PR_LANGUAGE_I_DEFAULT));
        }
    }
    PR_Sleep(PR_INTERVAL_NO_WAIT); /* yield to allow event thread to pick up event */
#ifdef DEBUG
    ns_log(LOG_DEBUG, "event_q_wake result. 0\n");
#endif
}

static void
event_q_notify(ns_job_t *job)
{
    ns_thrpool_t *tp = job->tp;
    /* if we are being called from a thread other than the
       event loop thread, we have to notify that thread to
       perform the event work */
    if (pthread_equal(tp->event_thread, pthread_self()) != 0) {
        /* If we are being run from the same thread as the event
           loop thread, we can just update the event here */
        update_event(job);
    } else {
/* The event loop may be waiting for events, and may wait a long
           time by default if there are no events to process - since we
           want to add an event, we have to "wake up" the event loop by
           posting an event - this will cause the wakeup_cb to be called
           which will empty the event_q and add all of the events
        */
/* NOTE: once job is queued, it may be deleted immediately in
         * another thread, if the event loop picks up the deletion
         * job before we can notify it below - so make sure not to
         * refer to job after the enqueue.
         */
#ifdef DEBUG
        ns_log(LOG_DEBUG, "event_q_notify enqueuing %x with state %d\n", job, job->state);
#endif

        sds_lqueue_enqueue(tp->event_q, (void *)job);
        event_q_wake(tp);
    }
}

/* This is run inside the event loop thread, and only in the
   event loop thread
   This function pulls the io/timer/signal event requests off
   the request queue, formats the events in the format required
   by the event framework, and adds them
*/
static void
get_new_event_requests(ns_thrpool_t *tp)
{
    ns_job_t *job = NULL;
    while (sds_lqueue_dequeue(tp->event_q, (void **)&job) == SDS_SUCCESS) {
        if (job != NULL) {
#ifdef DEBUG
            ns_log(LOG_DEBUG, "get_new_event_requests Dequeuing %x with state %d\n", job, job->state);
#endif
            update_event(job);
#ifdef DEBUG
        } else {
            ns_log(LOG_DEBUG, "get_new_event_requests Dequeuing NULL job\n");
#endif
        }
    }
}

static void *
event_loop_thread_func(void *arg)
{
    struct ns_thrpool_t *tp = (struct ns_thrpool_t *)arg;
    int rc;

    sds_lqueue_tprep(tp->event_q);

    while (!ns_thrpool_is_event_shutdown(tp)) {
        /* get new event requests */
        get_new_event_requests(tp);
        /* process events */
        /* return 1 - no events ; 0 - normal exit ; -1 - error */
        rc = tp->ns_event_fw->ns_event_fw_loop(tp->ns_event_fw_ctx);
#ifdef DEBUG
        ns_log(LOG_DEBUG, "event_loop_thread_func woke event queue. rc=%d\n", rc);
#endif
        if (rc == -1) {       /* error */
        } else if (rc == 0) { /* exiting */
        } else {              /* no events to process */
            event_q_wait(tp);
        }
    }
    return NULL;
}

/*
 * The event framework calls this function when it receives an
 * event - the event framework does all of the work to map
 * the framework flags, etc. into the job
 * This function is called only in the event loop thread.
 * If the THREAD flag is set, this means the job is to be
 * handed off to the work q - otherwise, execute it now
 * in this thread (the event loop thread)
 * the function must be careful not to block the event
 * loop thread or starvation will occur
 */
static void
event_cb(ns_job_t *job)
{
    PR_ASSERT(job);
    /*
     * Sometimes if we queue then request to delete a job REALLY fast, this
     * will trigger
     * Fix is that if ARMED we don't allow the job to move to NS_JOB_NEEDS_DELETE
     */

    /* There is no guarantee this won't be called once we start to enter the shutdown, especially with timers .... */
    pthread_mutex_lock(job->monitor);

    PR_ASSERT(job->state == NS_JOB_ARMED || job->state == NS_JOB_NEEDS_DELETE);
    if (job->state == NS_JOB_ARMED && NS_JOB_IS_THREAD(job->job_type)) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "event_cb %x state %d threaded, send to work_q\n", job, job->state);
#endif
        pthread_mutex_unlock(job->monitor);
        work_q_notify(job);
    } else if (job->state == NS_JOB_NEEDS_DELETE) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "event_cb %x state %d ignoring as NS_JOB_NEEDS_DELETE set\n", job, job->state);
#endif
        /*
         * If the job is in need of delete we IGNORE IT
         * It's here because it's been QUEUED for deletion and *may* be coming
         * from the thrpool destroy thread!
         */
        pthread_mutex_unlock(job->monitor);

    } else {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "event_cb %x state %d non-threaded, execute right meow\n", job, job->state);
#endif
        /* Not threaded, execute now! */
        pthread_mutex_unlock(job->monitor);
        work_job_execute(job);
        /* MUST NOT ACCESS JOB FROM THIS POINT */
    }
}

static void
wakeup_cb(ns_job_t *job)
{
    int32_t len;
    char buf[1];

#ifdef DEBUG
    ns_log(LOG_DEBUG, "wakeup_cb %x state %d wakeup_cb\n", job, job->state);
#endif

    /* NSPR I/O doesn't allow non-blocking signal pipes, so use read instead of PR_Read */
    len = read(PR_FileDesc2NativeHandle(job->tp->event_q_wakeup_pipe_read),
               buf, 1);
    if (1 != len) {
        if ((errno == 0) || ERRNO_WOULD_BLOCK(errno)) {
            ns_log(LOG_DEBUG, "Read blocked for wakeup pipe - ignore %d\n",
                   errno);
        } else {
            ns_log(LOG_ERR, "Error: could not read wakeup pipe: %d:%s\n",
                   errno, PR_ErrorToString(errno, PR_LANGUAGE_I_DEFAULT));
        }
    }
    /* wakeup_cb is usually called because a worker thread has posted a new
       event we need to add - get all new event requests */
    get_new_event_requests(job->tp);
}
/* convenience function for the event fw to use to allocate
   space for its event fw event object
   it is assumed that eventually job will have a memory region/arena
   to use
   these functions will be called in the event loop thread from
   the event framework function that adds a new event
*/
static void *
alloc_event_context(size_t size, ns_job_t *job __attribute__((unused)))
{
    return ns_malloc(size);
}

static void
free_event_context(void *ev_ctx, ns_job_t *job __attribute__((unused)))
{
    ns_free(ev_ctx);
}

static ns_job_t *
new_ns_job(ns_thrpool_t *tp, PRFileDesc *fd, ns_job_type_t job_type, ns_job_func_t func, struct ns_job_data_t *data)
{
    ns_job_t *job = ns_calloc(1, sizeof(ns_job_t));
    job->monitor = ns_calloc(1, sizeof(pthread_mutex_t));

    pthread_mutexattr_t *monitor_attr = ns_calloc(1, sizeof(pthread_mutexattr_t));
    pthread_mutexattr_init(monitor_attr);
    pthread_mutexattr_settype(monitor_attr, PTHREAD_MUTEX_RECURSIVE);
    assert(pthread_mutex_init(job->monitor, monitor_attr) == 0);
    ns_free(monitor_attr);

    job->tp = tp;
    /* We have to have this due to our obsession of hiding struct contents ... */
    /* It's only used in tevent anyway .... */
    job->ns_event_fw_ctx = tp->ns_event_fw_ctx;
    job->fd = fd;
    job->func = func;
    job->data = data;
    job->alloc_event_context = alloc_event_context;
    job->free_event_context = free_event_context;
    job->event_cb = event_cb;
    job->job_type = job_type;
    job->done_cb = NULL;
#ifdef DEBUG
    ns_log(LOG_DEBUG, "new_ns_job %x initial NS_JOB_WAITING\n", job);
#endif
    job->state = NS_JOB_WAITING;
    return job;
}

static ns_job_t *
alloc_io_context(ns_thrpool_t *tp, PRFileDesc *fd, ns_job_type_t job_type, ns_job_func_t func, struct ns_job_data_t *data)
{
    ns_job_t *job = new_ns_job(tp, fd, job_type, func, data);

    return job;
}

static ns_job_t *
alloc_timeout_context(ns_thrpool_t *tp, struct timeval *tv, ns_job_type_t job_type, ns_job_func_t func, struct ns_job_data_t *data)
{
    ns_job_t *job = new_ns_job(tp, NULL, NS_JOB_TIMER | job_type, func, data);
    job->tv = *tv;

    return job;
}

static ns_job_t *
alloc_signal_context(ns_thrpool_t *tp, PRInt32 signum, ns_job_type_t job_type, ns_job_func_t func, struct ns_job_data_t *data)
{
    ns_job_t *job = new_ns_job(tp, NULL, NS_JOB_SIGNAL | job_type, func, data);
    job->signal = signum;

    return job;
}

ns_result_t
ns_job_done(ns_job_t *job)
{
    PR_ASSERT(job);
    if (job == NULL) {
        return NS_INVALID_REQUEST;
    }

    /* Get the shutdown state ONCE at the start, atomically */
    int32_t shutdown_state = ns_thrpool_is_shutdown(job->tp);

    pthread_mutex_lock(job->monitor);

    if (job->state == NS_JOB_NEEDS_DELETE || job->state == NS_JOB_DELETED) {
/* Just return if the job has been marked for deletion */
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_job_done %x tp shutdown -> %x state %d return early\n", job, shutdown_state, job->state);
#endif
        pthread_mutex_unlock(job->monitor);
        return NS_SUCCESS;
    }

    /* Do not allow an armed job to be removed UNLESS the server is shutting down */
    if (job->state == NS_JOB_ARMED && !shutdown_state) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_job_done %x tp shutdown -> false state %d failed to mark as done\n", job, job->state);
#endif
        pthread_mutex_unlock(job->monitor);
        return NS_INVALID_STATE;
    }

    if (job->state == NS_JOB_RUNNING || job->state == NS_JOB_NEEDS_ARM) {
/* For this to be called, and NS_JOB_RUNNING, we *must* be the callback thread! */
/* Just mark it (ie do nothing), the work_job_execute function will trigger internal_ns_job_done */
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_job_done %x tp shutdown -> false state %d setting to async NS_JOB_NEEDS_DELETE\n", job, job->state);
#endif
        job->state = NS_JOB_NEEDS_DELETE;
        pthread_mutex_unlock(job->monitor);
    } else if (!shutdown_state) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_job_done %x tp shutdown -> false state %d setting NS_JOB_NEEDS_DELETE and queuing\n", job, job->state);
#endif
        job->state = NS_JOB_NEEDS_DELETE;
        pthread_mutex_unlock(job->monitor);
        event_q_notify(job);
    } else {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_job_done %x tp shutdown -> true state %d setting NS_JOB_NEEDS_DELETE and delete immediate\n", job, job->state);
#endif
        job->state = NS_JOB_NEEDS_DELETE;
        /* We are shutting down, just remove it! */
        pthread_mutex_unlock(job->monitor);
        internal_ns_job_done(job);
    }
    return NS_SUCCESS;
}

ns_result_t
ns_create_job(struct ns_thrpool_t *tp, ns_job_type_t job_type, ns_job_func_t func, struct ns_job_t **job)
{
    if (job == NULL) {
        /* This won't queue the job, so to pass NULL makes no sense */
        return NS_INVALID_REQUEST;
    }

    if (ns_thrpool_is_shutdown(tp)) {
        return NS_SHUTDOWN;
    }

    *job = new_ns_job(tp, NULL, job_type, func, NULL);
    if (*job == NULL) {
        return NS_ALLOCATION_FAILURE;
    }

    return NS_SUCCESS;
}

/* queue a file descriptor to listen for and accept new connections */
ns_result_t
ns_add_io_job(ns_thrpool_t *tp, PRFileDesc *fd, ns_job_type_t job_type, ns_job_func_t func, void *data, ns_job_t **job)
{
    ns_job_t *_job = NULL;

    if (job) {
        *job = NULL;
    }

    /* Don't allow a job to be added if the threadpool is being shut down. */
    if (ns_thrpool_is_shutdown(tp)) {
        return NS_SHUTDOWN;
    }

    /* Don't allow an accept job to be run outside of the event thread.
     * We do this so a listener job won't shut down while still processing
     * current connections in other threads.
     * TODO: Need to be able to have multiple threads accept() at the same time
     * This is fine - just have to remove the listen job in the polling thread
     * immediately after receiving notification - then call the job to do the
     * accept(), which will add back the persistent listener job immediately after
     * doing the accept()
     * This will be a combination of a non-threaded job and a threaded job
     *
     */
    if (NS_JOB_IS_ACCEPT(job_type) && NS_JOB_IS_THREAD(job_type)) {
        return NS_INVALID_REQUEST;
    }

    /* get an event context for an accept */
    _job = alloc_io_context(tp, fd, job_type, func, data);
    if (!_job) {
        return NS_ALLOCATION_FAILURE;
    }

    pthread_mutex_lock(_job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_add_io_job state %d moving to NS_JOB_ARMED\n", (_job)->state);
#endif
    _job->state = NS_JOB_NEEDS_ARM;
    pthread_mutex_unlock(_job->monitor);
    internal_ns_job_rearm(_job);

    /* fill in a pointer to the job for the caller if requested */
    if (job) {
        *job = _job;
    }

    return NS_SUCCESS;
}

ns_result_t
ns_add_timeout_job(ns_thrpool_t *tp, struct timeval *tv, ns_job_type_t job_type, ns_job_func_t func, void *data, ns_job_t **job)
{
    ns_job_t *_job = NULL;

    if (job) {
        *job = NULL;
    }

    /* Don't allow a job to be added if the threadpool is being shut down. */
    if (ns_thrpool_is_shutdown(tp)) {
        return NS_SHUTDOWN;
    }

    if (validate_event_timeout(tv)) {
        return NS_INVALID_REQUEST;
    }

    /* get an event context for a timer job */
    _job = alloc_timeout_context(tp, tv, job_type, func, data);
    if (!_job) {
        return NS_ALLOCATION_FAILURE;
    }

    pthread_mutex_lock(_job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_add_timeout_job state %d moving to NS_JOB_ARMED\n", (_job)->state);
#endif
    _job->state = NS_JOB_NEEDS_ARM;
    pthread_mutex_unlock(_job->monitor);
    internal_ns_job_rearm(_job);

    /* fill in a pointer to the job for the caller if requested */
    if (job) {
        *job = _job;
    }

    return NS_SUCCESS;
}

/* queue a file descriptor to listen for and accept new connections */
ns_result_t
ns_add_io_timeout_job(ns_thrpool_t *tp, PRFileDesc *fd, struct timeval *tv, ns_job_type_t job_type, ns_job_func_t func, void *data, ns_job_t **job)
{
    ns_job_t *_job = NULL;

    if (job) {
        *job = NULL;
    }

    /* Don't allow a job to be added if the threadpool is being shut down. */
    if (ns_thrpool_is_shutdown(tp)) {
        return NS_SHUTDOWN;
    }

    if (validate_event_timeout(tv)) {
        return NS_INVALID_REQUEST;
    }

    /* Don't allow an accept job to be run outside of the event thread.
     * We do this so a listener job won't shut down while still processing
     * current connections in other threads.
     * TODO: Need to be able to have multiple threads accept() at the same time
     * This is fine - just have to remove the listen job in the polling thread
     * immediately after receiving notification - then call the job to do the
     * accept(), which will add back the persistent listener job immediately after
     * doing the accept()
     * This will be a combination of a non-threaded job and a threaded job
     *
     */
    if (NS_JOB_IS_ACCEPT(job_type) && NS_JOB_IS_THREAD(job_type)) {
        return NS_INVALID_REQUEST;
    }

    /* get an event context for an accept */
    _job = alloc_io_context(tp, fd, job_type | NS_JOB_TIMER, func, data);
    if (!_job) {
        return NS_ALLOCATION_FAILURE;
    }
    pthread_mutex_lock(_job->monitor);
    _job->tv = *tv;

#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_add_io_timeout_job state %d moving to NS_JOB_ARMED\n", (_job)->state);
#endif
    _job->state = NS_JOB_NEEDS_ARM;
    pthread_mutex_unlock(_job->monitor);
    internal_ns_job_rearm(_job);

    /* fill in a pointer to the job for the caller if requested */
    if (job) {
        *job = _job;
    }

    return NS_SUCCESS;
}

ns_result_t
ns_add_signal_job(ns_thrpool_t *tp, int32_t signum, ns_job_type_t job_type, ns_job_func_t func, void *data, ns_job_t **job)
{
    ns_job_t *_job = NULL;

    if (job) {
        *job = NULL;
    }

    /* Don't allow a job to be added if the threadpool is being shut down. */
    if (ns_thrpool_is_shutdown(tp)) {
        return NS_SHUTDOWN;
    }

    /* get an event context for a signal job */
    _job = alloc_signal_context(tp, signum, job_type, func, data);
    if (!_job) {
        return NS_ALLOCATION_FAILURE;
    }

    pthread_mutex_lock(_job->monitor);
#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_add_signal_job state %d moving to NS_JOB_ARMED\n", (_job)->state);
#endif
    _job->state = NS_JOB_NEEDS_ARM;
    pthread_mutex_unlock(_job->monitor);
    internal_ns_job_rearm(_job);

    /* fill in a pointer to the job for the caller if requested */
    if (job) {
        *job = _job;
    }

    return NS_SUCCESS;
}

ns_result_t
ns_add_job(ns_thrpool_t *tp, ns_job_type_t job_type, ns_job_func_t func, void *data, ns_job_t **job)
{
    ns_job_t *_job = NULL;

    if (job) {
        *job = NULL;
    }

    /* Don't allow a job to be added if the threadpool is being shut down. */
    if (ns_thrpool_is_shutdown(tp)) {
        return NS_SHUTDOWN;
    }

    _job = new_ns_job(tp, NULL, job_type, func, data);
    if (!_job) {
        return NS_ALLOCATION_FAILURE;
    }
    /* fill in a pointer to the job for the caller if requested */
    if (job) {
        *job = _job;
    }

#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_add_job %x state %d moving to NS_JOB_ARMED\n", _job, (_job)->state);
#endif
    _job->state = NS_JOB_NEEDS_ARM;
    internal_ns_job_rearm(_job);

    return NS_SUCCESS;
}

ns_result_t
ns_add_shutdown_job(ns_thrpool_t *tp)
{
    ns_job_t *_job = NULL;
    _job = new_ns_job(tp, NULL, NS_JOB_SHUTDOWN_WORKER, NULL, NULL);
    if (!_job) {
        return NS_ALLOCATION_FAILURE;
    }
    pthread_mutex_lock(_job->monitor);
    _job->state = NS_JOB_NEEDS_ARM;
    pthread_mutex_unlock(_job->monitor);
    internal_ns_job_rearm(_job);
    return NS_SUCCESS;
}

/*
 * Because of the design of work_job_execute, when we are in RUNNING
 * we hold the monitor. As a result, we don't need to assert the current thread
 * because we *already must* be the current thread, as no one else could take
 * the monitor away.
 *
 * The same is true of DELETED, which represents that we are deleting things.
 * To set NEEDS_DELETE -> DELETED, we must hold the monitor, and at that point
 * the monitor is released and the job destroyed. As a result, we only need to
 * assert that we are not "NEEDS_DELETE" in many cases.
 */

void *
ns_job_get_data(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state != NS_JOB_DELETED);
    if (job->state != NS_JOB_DELETED) {
        pthread_mutex_unlock(job->monitor);
        return job->data;
    } else {
        pthread_mutex_unlock(job->monitor);
        return NULL;
    }
}

ns_result_t
ns_job_set_data(ns_job_t *job, void *data)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state == NS_JOB_WAITING || job->state == NS_JOB_RUNNING);
    if (job->state == NS_JOB_WAITING || job->state == NS_JOB_RUNNING) {
        job->data = data;
        pthread_mutex_unlock(job->monitor);
        return NS_SUCCESS;
    } else {
        pthread_mutex_unlock(job->monitor);
        return NS_INVALID_STATE;
    }
}

ns_thrpool_t *
ns_job_get_tp(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state != NS_JOB_DELETED);
    if (job->state != NS_JOB_DELETED) {
        pthread_mutex_unlock(job->monitor);
        return job->tp;
    } else {
        pthread_mutex_unlock(job->monitor);
        return NULL;
    }
}

ns_job_type_t
ns_job_get_output_type(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state == NS_JOB_RUNNING);
    if (job->state == NS_JOB_RUNNING) {
        pthread_mutex_unlock(job->monitor);
        return job->output_job_type;
    } else {
        pthread_mutex_unlock(job->monitor);
        return 0;
    }
}

ns_job_type_t
ns_job_get_type(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state != NS_JOB_DELETED);
    if (job->state != NS_JOB_DELETED) {
        pthread_mutex_unlock(job->monitor);
        return job->job_type;
    } else {
        pthread_mutex_unlock(job->monitor);
        return 0;
    }
}

PRFileDesc *
ns_job_get_fd(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state != NS_JOB_DELETED);
    if (job->state != NS_JOB_DELETED) {
        pthread_mutex_unlock(job->monitor);
        return job->fd;
    } else {
        pthread_mutex_unlock(job->monitor);
        return NULL;
    }
}

ns_result_t
ns_job_set_done_cb(struct ns_job_t *job, ns_job_func_t func)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state == NS_JOB_WAITING || job->state == NS_JOB_RUNNING);
    if (job->state == NS_JOB_WAITING || job->state == NS_JOB_RUNNING) {
        job->done_cb = func;
        pthread_mutex_unlock(job->monitor);
        return NS_SUCCESS;
    } else {
        pthread_mutex_unlock(job->monitor);
        return NS_INVALID_STATE;
    }
}


/*
 * This is a convenience function - use if you need to re-arm the same event
 * usually not needed for persistent jobs
 */
ns_result_t
ns_job_rearm(ns_job_t *job)
{
    PR_ASSERT(job);
    pthread_mutex_lock(job->monitor);
    PR_ASSERT(job->state == NS_JOB_WAITING || job->state == NS_JOB_RUNNING);

    if (ns_thrpool_is_shutdown(job->tp)) {
        return NS_SHUTDOWN;
    }

    if (job->state == NS_JOB_WAITING) {
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_rearm_job %x state %d moving to NS_JOB_NEEDS_ARM\n", job, job->state);
#endif
        job->state = NS_JOB_NEEDS_ARM;
        internal_ns_job_rearm(job);
        pthread_mutex_unlock(job->monitor);
        return NS_SUCCESS;
    } else if (!NS_JOB_IS_PERSIST(job->job_type) && job->state == NS_JOB_RUNNING) {
/* For this to be called, and NS_JOB_RUNNING, we *must* be the callback thread! */
/* Just mark it (ie do nothing), the work_job_execute function will trigger internal_ns_job_rearm */
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_rearm_job %x state %d setting NS_JOB_NEEDS_ARM\n", job, job->state);
#endif
        job->state = NS_JOB_NEEDS_ARM;
        pthread_mutex_unlock(job->monitor);
        return NS_SUCCESS;
    } else {
        pthread_mutex_unlock(job->monitor);
        return NS_INVALID_STATE;
    }
    /* Unreachable code .... */
    return NS_INVALID_REQUEST;
}

static void
ns_thrpool_delete(ns_thrpool_t *tp)
{
    ns_free(tp);
}

static ns_thrpool_t *
ns_thrpool_alloc(void)
{
    ns_thrpool_t *tp;

    tp = ns_calloc(1, sizeof(struct ns_thrpool_t));
    if (NULL == tp) {
        goto failed;
    }

    return tp;
failed:
    ns_thrpool_delete(tp);
    PR_SetError(PR_OUT_OF_MEMORY_ERROR, 0);
    return NULL;
}

/* libevent does not make public the file descriptors used to
   wakeup epoll_wait - you have to send a signal - instead of
   that just create our own wakeup pipe */
static void
setup_event_q_wakeup(ns_thrpool_t *tp)
{
    ns_job_t *job;
    PR_CreatePipe(&tp->event_q_wakeup_pipe_read, &tp->event_q_wakeup_pipe_write);
    /* setting options is not supported on NSPR pipes - use fcntl
    PRSocketOptionData prsod = {PR_SockOpt_Nonblocking, {PR_TRUE}};
    PR_SetSocketOption(tp->event_q_wakeup_pipe_read, &prsod);
    PR_SetSocketOption(tp->event_q_wakeup_pipe_write, &prsod);
    */
    if (fcntl(PR_FileDesc2NativeHandle(tp->event_q_wakeup_pipe_read), F_SETFD, O_NONBLOCK) == -1) {
        ns_log(LOG_ERR, "setup_event_q_wakeup(): could not make read pipe non-blocking: %d\n",
               PR_GetOSError());
    }
    if (fcntl(PR_FileDesc2NativeHandle(tp->event_q_wakeup_pipe_write), F_SETFD, O_NONBLOCK) == -1) {
        ns_log(LOG_ERR, "setup_event_q_wakeup(): could not make write pipe non-blocking: %d\n",
               PR_GetOSError());
    }
    /* wakeup events are processed inside the event loop thread */
    job = alloc_io_context(tp, tp->event_q_wakeup_pipe_read,
                           NS_JOB_READ | NS_JOB_PERSIST | NS_JOB_PRESERVE_FD,
                           wakeup_cb, NULL);

    pthread_mutex_lock(job->monitor);

/* The event_queue wakeup is ready, arm it. */
#ifdef DEBUG
    ns_log(LOG_DEBUG, "setup_event_q_wakeup %x state %d moving NS_JOB_ARMED\n", job, job->state);
#endif
    job->state = NS_JOB_ARMED;

    /* Now allow it to process events */
    tp->ns_event_fw->ns_event_fw_add_io(tp->ns_event_fw_ctx, job);

    /* Stash the wakeup job in tp so we can release it later. */
    tp->event_q_wakeup_job = job;
    pthread_mutex_unlock(job->monitor);
}

/* Initialize the thrpool config */
#define NS_INIT_MAGIC 0xdefa014

void
ns_thrpool_config_init(struct ns_thrpool_config *tp_config)
{
    tp_config->init_flag = NS_INIT_MAGIC;
    tp_config->max_threads = 1;
    tp_config->stacksize = 0;
    tp_config->log_fct = NULL;
    tp_config->log_start_fct = NULL;
    tp_config->log_close_fct = NULL;
    tp_config->malloc_fct = NULL;
    tp_config->memalign_fct = NULL;
    tp_config->calloc_fct = NULL;
    tp_config->realloc_fct = NULL;
    tp_config->free_fct = NULL;
}

/*
 * Process the config and set the pluggable function pointers
 */
static ns_result_t
ns_thrpool_process_config(struct ns_thrpool_config *tp_config)
{
    /* Check that the config has been properly initialized */
    if (!tp_config || tp_config->init_flag != NS_INIT_MAGIC) {
        return NS_INVALID_REQUEST;
    }
    /*
     * Assign our logging function pointers
     */
    if (tp_config->log_fct) {
        /* Set a logger function */
        logger = tp_config->log_fct;
        if (tp_config->log_start_fct) {
            log_start = tp_config->log_start_fct;
        }
        if (tp_config->log_close_fct) {
            log_close = tp_config->log_close_fct;
        }
    } else {
        /* Default to syslog */
        logger = ns_syslog;
        log_start = ns_syslog_start;
        log_close = ns_syslog_close;
    }
    if (log_start) {
        /* Start logging */
        (log_start)();
    }

    /*
     * Set the memory function pointers
     */
    /* malloc */
    if (tp_config->malloc_fct) {
        malloc_fct = tp_config->malloc_fct;
    } else {
        malloc_fct = os_malloc;
    }

    if (tp_config->memalign_fct) {
        memalign_fct = tp_config->memalign_fct;
    } else {
        memalign_fct = os_memalign_malloc;
    }

    /* calloc */
    if (tp_config->calloc_fct) {
        calloc_fct = tp_config->calloc_fct;
    } else {
        calloc_fct = os_calloc;
    }
    /* realloc */
    if (tp_config->realloc_fct) {
        realloc_fct = tp_config->realloc_fct;
    } else {
        realloc_fct = os_realloc;
    }
    /* free */
    if (tp_config->free_fct) {
        free_fct = tp_config->free_fct;
    } else {
        free_fct = os_free;
    }

    return NS_SUCCESS;
}

ns_thrpool_t *
ns_thrpool_new(struct ns_thrpool_config *tp_config)
{
    pthread_attr_t attr;
    ns_thrpool_t *tp = NULL;
    ns_thread_t *thr;
    size_t ii;

    if (ns_thrpool_process_config(tp_config) != NS_SUCCESS) {
        ns_log(LOG_ERR, "ns_thrpool_new(): config has not been properly initialized\n");
        goto failed;
    }

    tp = ns_thrpool_alloc();
    if (NULL == tp) {
        ns_log(LOG_ERR, "ns_thrpool_new(): failed to allocate thread pool\n");
        goto failed;
    }

    ns_log(LOG_DEBUG, "ns_thrpool_new():  max threads, (%d)\n"
                      "stacksize (%d), event q size (unbounded), work q size (unbounded)\n",
           tp_config->max_threads, tp_config->stacksize);

    tp->stacksize = tp_config->stacksize;

    if (sds_queue_init(&(tp->thread_stack), NULL) != SDS_SUCCESS) {
        goto failed;
    }

    if (pthread_mutex_init(&(tp->work_q_lock), NULL) != 0) {
        goto failed;
    }
    if (pthread_cond_init(&(tp->work_q_cv), NULL) != 0) {
        goto failed;
    }

    if (sds_lqueue_init(&(tp->work_q), job_queue_cleanup) != SDS_SUCCESS) {
        goto failed;
    }
    if (sds_lqueue_init(&(tp->event_q), job_queue_cleanup) != SDS_SUCCESS) {
        goto failed;
    }

    /* NGK TODO - add tevent vs. libevent switch */
    /* tp->ns_event_fw = get_event_framework_tevent(); */
    tp->ns_event_fw = get_event_framework_event();
    tp->ns_event_fw_ctx = tp->ns_event_fw->ns_event_fw_init();

    setup_event_q_wakeup(tp);

    /* Create the thread attributes. */
    if (pthread_attr_init(&attr) != 0) {
        goto failed;
    }
    /* Setup the stack size. */
    if (tp_config->stacksize > 0) {
        if (pthread_attr_setstacksize(&attr, tp_config->stacksize) != 0) {
            goto failed;
        }
    }

    for (ii = 0; ii < tp_config->max_threads; ++ii) {
        tp->thread_count += 1;
        thr = ns_calloc(1, sizeof(ns_thread_t));
        PR_ASSERT(thr);
        thr->tp = tp;
        assert(pthread_create(&(thr->thr), &attr, &worker_thread_func, thr) == 0);
        sds_queue_enqueue(tp->thread_stack, thr);
    }

    assert(pthread_create(&(tp->event_thread), &attr, &event_loop_thread_func, tp) == 0);

    /* We keep the event thread separate from the stack of worker threads. */
    // tp->event_thread = event_thr;

    return tp;
failed:
    ns_thrpool_destroy(tp);
    return NULL;
}

void
ns_thrpool_destroy(struct ns_thrpool_t *tp)
{
    void *retval = NULL;
#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_thrpool_destroy\n");
#endif
    if (tp) {
        /* Set the flag to shutdown the event loop. */
#ifdef ATOMIC_64BIT_OPERATIONS
        __atomic_add_fetch(&(tp->shutdown_event_loop), 1, __ATOMIC_RELEASE);
#else
        PR_AtomicIncrement(&(tp->shutdown_event_loop));
#endif
        /* Finish the event queue wakeup job.  This has the
         * side effect of waking up the event loop thread, which
         * will cause it to exit since we set the event loop
         * shutdown flag.  Fake the job to be a threaded job
         * so that we can run it from outside the event loop,
         * and use it to wake up the event loop.
         */

        pthread_mutex_lock(tp->event_q_wakeup_job->monitor);

// tp->event_q_wakeup_job->job_type |= NS_JOB_THREAD;
/* This triggers the job to "run", which will cause a shutdown cascade */
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_thrpool_destroy %x state %d moving to NS_JOB_NEEDS_DELETE\n", tp->event_q_wakeup_job, tp->event_q_wakeup_job->state);
#endif
        tp->event_q_wakeup_job->state = NS_JOB_NEEDS_DELETE;
        pthread_mutex_unlock(tp->event_q_wakeup_job->monitor);
        /* Has to be event_q_notify, not internal_job_done */
        event_q_notify(tp->event_q_wakeup_job);

        /* Wait for the event thread to finish before we free the
         * internals of tp. */
        int32_t rc = pthread_join(tp->event_thread, &retval);
        if (rc != 0) {
            ns_log(LOG_DEBUG, "Failed to join event thread %d\n", rc);
        }

        if (tp->work_q) {
            sds_lqueue_destroy(tp->work_q);
        }

        if (tp->thread_stack) {
            sds_queue_destroy(tp->thread_stack);
        }

        /* Free the work queue condition variable. */
        pthread_cond_destroy(&(tp->work_q_cv));
        /* Free the work queue lock. */
        pthread_mutex_destroy(&(tp->work_q_lock));

        if (tp->event_q) {
            sds_lqueue_destroy(tp->event_q);
        }

        /* Free the event queue wakeup pipe/job. */
        if (tp->event_q_wakeup_pipe_read) {
            PR_Close(tp->event_q_wakeup_pipe_read);
            tp->event_q_wakeup_pipe_read = NULL;
        }

        if (tp->event_q_wakeup_pipe_write) {
            PR_Close(tp->event_q_wakeup_pipe_write);
            tp->event_q_wakeup_pipe_write = NULL;
        }
        /* Already destroyed in the event queue shutdown */
        tp->event_q_wakeup_job = NULL;

        /* Free the event framework context. */
        if (tp->ns_event_fw_ctx) {
            tp->ns_event_fw->ns_event_fw_destroy(tp->ns_event_fw_ctx);
            tp->ns_event_fw_ctx = NULL;
        }

        /* Free the thread pool struct itself. */
        ns_thrpool_delete(tp);
    }
    /* Stop logging */
    if (log_close) {
        (log_close)();
    }
}

/* Triggers the pool of worker threads to shutdown after finishing the remaining work.
 * This must be run in a worker thread, not the event thread.  Running it in the event
 * thread could cause a deadlock. */
void
ns_thrpool_shutdown(struct ns_thrpool_t *tp)
{
#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_thrpool_shutdown initiated ...\n");
#endif
    if (ns_thrpool_is_shutdown(tp) != 0) {
        /* Already done! */
        return;
    }

    /* Set the shutdown flag.  This will cause the worker
     * threads to exit after they finish all remaining work. */
#ifdef ATOMIC_64BIT_OPERATIONS
    __atomic_add_fetch(&(tp->shutdown), 1, __ATOMIC_RELEASE);
#else
    PR_AtomicIncrement(&(tp->shutdown));
#endif

    /* Send worker shutdown jobs into the queues. This allows
     * currently queued jobs to complete.
     */
    for (size_t i = 0; i < tp->thread_count; i++) {
        ns_result_t result = ns_add_shutdown_job(tp);
        PR_ASSERT(result == NS_SUCCESS);
    }
    /* Make sure all threads are woken up to their shutdown jobs. */
    pthread_mutex_lock(&(tp->work_q_lock));
    pthread_cond_broadcast(&(tp->work_q_cv));
    pthread_mutex_unlock(&(tp->work_q_lock));
}

ns_result_t
ns_thrpool_wait(ns_thrpool_t *tp)
{
#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_thrpool_wait has begun\n");
#endif
    ns_result_t retval = NS_SUCCESS;
    ns_thread_t *thr;

    while (sds_queue_dequeue(tp->thread_stack, (void **)&thr) == SDS_SUCCESS) {
        /* void *thread_retval = NULL; */
        int32_t rc = pthread_join(thr->thr, NULL);
#ifdef DEBUG
        ns_log(LOG_DEBUG, "ns_thrpool_wait joined thread, result %d\n", rc);
#endif
        if (rc != 0) {
            /* NGK TODO - this is unused right now. */
            ns_log(LOG_ERR, "ns_thrpool_wait, failed to join thread %d", rc);
            retval = NS_THREAD_FAILURE;
        }
        ns_free(thr);
    }


#ifdef DEBUG
    ns_log(LOG_DEBUG, "ns_thrpool_wait complete, retval %d\n", retval);
#endif
    return retval;
}

/*
 * nunc stans logger
 */
void
ns_log(int priority, const char *fmt, ...)
{
    va_list varg;

    va_start(varg, fmt);
    ns_log_valist(priority, fmt, varg);
    va_end(varg);
}

void
ns_log_valist(int priority, const char *fmt, va_list varg)
{
    (logger)(priority, fmt, varg);
}

/*
 * Pluggable memory functions
 */
void *
ns_malloc(size_t size)
{
    return (malloc_fct)(size);
}

void *
ns_memalign(size_t size, size_t alignment)
{
    return (memalign_fct)(size, alignment);
}

void *
ns_calloc(size_t count, size_t size)
{
    return (calloc_fct)(count, size);
}

void *
ns_realloc(void *ptr, size_t size)
{
    return (realloc_fct)(ptr, size);
}

void
ns_free(void *ptr)
{
    (free_fct)(ptr);
}
