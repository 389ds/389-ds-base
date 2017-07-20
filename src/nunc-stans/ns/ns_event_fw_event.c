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

/*
 * This implements the thrpool event framework interface
 * to the libevent event framework
 */
#include <private/pprio.h>
#include <event.h>
#include <event2/event.h>
typedef struct event_base ns_event_fw_ctx_t;
typedef struct event ns_event_fw_fd_t;
typedef struct event ns_event_fw_time_t;
typedef struct event ns_event_fw_sig_t;
#define NO_EVENT_TYPEDEFS
#include "ns_event_fw.h"
#include <syslog.h>

static void
event_logger_cb(int severity, const char *msg)
{
    int priority = 0;
    char *text = NULL;

    switch (severity) {
    case _EVENT_LOG_DEBUG:
        priority = LOG_DEBUG;
        break;
    case _EVENT_LOG_MSG:
        priority = LOG_DEBUG;
        break;
    case _EVENT_LOG_WARN:
        priority = LOG_DEBUG;
        break;
    case _EVENT_LOG_ERR:
        priority = LOG_ERR;
        break;
    default:
        priority = LOG_ERR;
        break; /* never reached */
    }
    /* need to append newline */
    text = PR_smprintf("%s\n", msg);
    ns_log(priority, text);
    ns_free(text);
}

static ns_job_type_t
event_flags_to_type(short events)
{
    /* The volatile here prevents gcc rearranging this code within the thread. */
    volatile ns_job_type_t job_type = 0;

    /* Either we timeout *or* we are a real event */
    if (!(events & EV_TIMEOUT)) {
        if (events & EV_READ) {
            job_type |= NS_JOB_READ;
        }
        if (events & EV_WRITE) {
            job_type |= NS_JOB_WRITE;
        }
        if (events & EV_SIGNAL) {
            job_type |= NS_JOB_SIGNAL;
        }
    } else {
        job_type = NS_JOB_TIMER;
    }
    return job_type;
}

/* this is called by the event main loop when an event
   is triggered - this "maps" the event library interface
   to our nspr/thrpool interface */
static void
event_cb(int fd, short event, void *arg)
{
    ns_job_t *job = (ns_job_t *)arg;

    PR_ASSERT(arg);
    if (job->fd && fd > 0) {
        PR_ASSERT(fd == PR_FileDesc2NativeHandle(job->fd));
    }

    job->output_job_type = event_flags_to_type(event);

    job->event_cb(job);
}

/* convert from job job_type to event fw type */
static short
job_type_to_flags(ns_job_type_t job_type)
{
    short flags = 0;
    if (NS_JOB_IS_ACCEPT(job_type) || NS_JOB_IS_READ(job_type)) {
        flags |= EV_READ;
    }
    if (NS_JOB_IS_WRITE(job_type) || NS_JOB_IS_CONNECT(job_type)) {
        flags |= EV_WRITE;
    }
    if (NS_JOB_IS_PERSIST(job_type)) {
        flags |= EV_PERSIST;
    }
    if (NS_JOB_IS_TIMER(job_type)) {
        flags |= EV_TIMEOUT;
    }
    if (NS_JOB_IS_SIGNAL(job_type)) {
        flags |= EV_SIGNAL;
    }
    return flags;
}

static ns_event_fw_ctx_t *
ns_event_fw_init(void)
{
    event_set_log_callback(event_logger_cb);
#ifdef EVENT_SET_MEM_FUNCTIONS_IMPLEMENTED
    event_set_mem_functions(ns_malloc, ns_realloc, ns_free);
#endif
    return event_base_new();
}

static void
ns_event_fw_destroy(ns_event_fw_ctx_t *event_ctx_t)
{
    event_base_free(event_ctx_t);
}

/*
 * remove just removes the event from active consideration
 * by the event framework - the event object may be
 * reactivated by calling mod - event done will actually
 * remove and free the object
 */
static void
ns_event_fw_io_event_remove(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    event_del(job->ns_event_fw_fd);
}

static void
ns_event_fw_io_event_done(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    ns_event_fw_io_event_remove(ns_event_fw_ctx, job);
    job->free_event_context(job->ns_event_fw_fd, job);
    job->ns_event_fw_fd = NULL;
}

static void
ns_event_fw_timer_event_remove(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    evtimer_del(job->ns_event_fw_time);
}

static void
ns_event_fw_timer_event_done(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    ns_event_fw_timer_event_remove(ns_event_fw_ctx, job);
    job->free_event_context(job->ns_event_fw_time, job);
    job->ns_event_fw_time = NULL;
}

static void
ns_event_fw_signal_event_remove(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    evsignal_del(job->ns_event_fw_sig);
}

static void
ns_event_fw_signal_event_done(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    ns_event_fw_signal_event_remove(ns_event_fw_ctx, job);
    job->free_event_context(job->ns_event_fw_sig, job);
    job->ns_event_fw_sig = NULL;
}

static void
ns_event_fw_add_io(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    struct timeval *tv = NULL;
    /* allocate a new event structure - use the job for the memory context */
    struct event *ev = job->alloc_event_context(sizeof(struct event), job);
    /* set the fields in the event */
    short flags = job_type_to_flags(job->job_type);

    event_assign(ev, ns_event_fw_ctx, PR_FileDesc2NativeHandle(job->fd), flags, event_cb, job);
    if (job->tv.tv_sec || job->tv.tv_usec) {
        tv = &job->tv;
    }
    event_add(ev, tv);
    job->ns_event_fw_fd = ev;
}

static void
ns_event_fw_mod_io(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    struct timeval *tv = NULL;
    short events = job_type_to_flags(job->job_type);

    if (job->tv.tv_sec || job->tv.tv_usec) {
        tv = &job->tv;
    }
    if (events) {
        job->ns_event_fw_fd->ev_events = events;
        event_add(job->ns_event_fw_fd, tv);
    } else {
        /* setting the job_type to remove IO events will remove it from the event system */
        ns_event_fw_io_event_remove(ns_event_fw_ctx, job);
    }
}

static void
ns_event_fw_add_timer(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    /* allocate a new event structure - use the job for the memory context */
    struct event *ev = job->alloc_event_context(sizeof(struct event), job);
    evtimer_assign(ev, ns_event_fw_ctx, event_cb, job);
    evtimer_add(ev, &job->tv);
    job->ns_event_fw_time = ev;
}

static void
ns_event_fw_mod_timer(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    evtimer_add(job->ns_event_fw_time, &job->tv);
}

static void
ns_event_fw_add_signal(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    /* allocate a new event structure - use the job for the memory context */
    struct event *ev = job->alloc_event_context(sizeof(struct event), job);
    evsignal_assign(ev, ns_event_fw_ctx, job->signal, event_cb, job);
    evsignal_add(ev, NULL);
    job->ns_event_fw_sig = ev;
}

static void
ns_event_fw_mod_signal(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    /* If the event in the ev argument already has a scheduled timeout, calling event_add() replaces the old timeout with the new one, or clears the old timeout if the timeout argument is NULL. */
    evsignal_add(job->ns_event_fw_sig, NULL);
}

/* returns
   1 - no events to process
   0 - normal termination
   -1 - error
*/
static int
ns_event_fw_loop(ns_event_fw_ctx_t *ns_event_fw_ctx)
{
    int rc = event_base_loop(ns_event_fw_ctx, EVLOOP_ONCE);
    if (rc == 0) {
        rc = 1;
    } else {
        rc = -1;
    }

    return rc;
}

static ns_event_fw_t ns_event_fw_event = {
    ns_event_fw_init,
    ns_event_fw_destroy,
    ns_event_fw_loop,
    ns_event_fw_add_io,
    ns_event_fw_mod_io,
    ns_event_fw_add_timer,
    ns_event_fw_mod_timer,
    ns_event_fw_add_signal,
    ns_event_fw_mod_signal,
    ns_event_fw_io_event_done,
    ns_event_fw_timer_event_done,
    ns_event_fw_signal_event_done};

ns_event_fw_t *
get_event_framework_event()
{
    return &ns_event_fw_event;
}
