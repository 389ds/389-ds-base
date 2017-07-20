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
 * to the libtevent event framework
 */
#include <private/pprio.h>
#include <talloc.h>
#include <tevent.h>
typedef struct tevent_context ns_event_fw_ctx_t;
typedef struct tevent_fd ns_event_fw_fd_t;
typedef struct tevent_timer ns_event_fw_time_t;
typedef struct tevent_signal ns_event_fw_sig_t;
#define NO_EVENT_TYPEDEFS
#include "ns_event_fw.h"
#include <syslog.h>

static void ns_event_fw_add_io(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_mod_io(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_add_timer(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_mod_timer(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_add_signal(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_mod_signal(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_io_event_remove(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_io_event_done(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_timer_event_done(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);
static void ns_event_fw_signal_event_done(ns_event_fw_ctx_t *ns_event_fw_ctx, ns_job_t *job);

static void
tevent_logger_cb(void *context __attribute__((unused)), enum tevent_debug_level level, const char *fmt, va_list ap)
{
    int priority = 0;
    char *msg = NULL;

    switch (level) {
    case TEVENT_DEBUG_FATAL:
        priority = LOG_ERR;
        break;
    case TEVENT_DEBUG_ERROR:
        priority = LOG_ERR;
        break;
    case TEVENT_DEBUG_WARNING:
        priority = LOG_DEBUG;
        break;
    case TEVENT_DEBUG_TRACE:
        priority = LOG_DEBUG;
        break;
    default:
        priority = LOG_DEBUG;
        break; /* never reached */
    }

    msg = PR_smprintf("%s\n", fmt);
    ns_log_valist(priority, msg, ap);
    ns_free(msg);
}

static ns_job_type_t
event_flags_to_type(uint16_t flags)
{
    ns_job_type_t job_type = 0;
    if (flags & TEVENT_FD_READ) {
        job_type |= NS_JOB_READ;
    }
    if (flags & TEVENT_FD_WRITE) {
        job_type |= NS_JOB_WRITE;
    }
    return job_type;
}

/* this is called by the event main loop when an fd event
   is triggered - this "maps" the event library interface
   to our nspr/thrpool interface */
static void
fd_event_cb(struct tevent_context *ev __attribute__((unused)), struct tevent_fd *fde __attribute__((unused)), uint16_t flags, void *arg)
{
    ns_job_t *job = (ns_job_t *)arg;

    PR_ASSERT(arg);

    /* I/O jobs can be timed - if we get an fd event, cancel the timer */
    if (job->ns_event_fw_time) {
        ns_event_fw_timer_event_done(job->ns_event_fw_ctx, job);
    }
    job->output_job_type = event_flags_to_type(flags);
    job->event_cb(job);
}

/* this is called by the event main loop when a timer event
   is triggered - this "maps" the event library interface
   to our nspr/thrpool interface */
static void
timer_event_cb(struct tevent_context *ev __attribute__((unused)), struct tevent_timer *te __attribute__((unused)), struct timeval current_time __attribute__((unused)), void *arg)
{
    ns_job_t *job = (ns_job_t *)arg;

    PR_ASSERT(arg);

    /* I/O jobs can be timed - if we get an timer event, cancel the fd */
    if (job->ns_event_fw_fd) {
        ns_event_fw_io_event_done(job->ns_event_fw_ctx, job);
    }
    job->output_job_type = NS_JOB_TIMER;
    job->event_cb(job);
}

/* this is called by the event main loop when a signal event
   is triggered - this "maps" the event library interface
   to our nspr/thrpool interface */
static void
signal_event_cb(struct tevent_context *ev __attribute__((unused)), struct tevent_signal *se __attribute__((unused)), int signum, int count __attribute__((unused)), void *siginfo __attribute__((unused)), void *arg)
{
    ns_job_t *job = (ns_job_t *)arg;

    PR_ASSERT(arg);

    job->output_job_type = NS_JOB_SIGNAL;
    job->signal = signum;
    job->event_cb(job);
}

/* convert from job job_type to event fw type */
static uint16_t
job_type_to_flags(ns_job_type_t job_type)
{
    uint16_t flags = 0;
    if (NS_JOB_IS_ACCEPT(job_type) || NS_JOB_IS_READ(job_type)) {
        flags |= TEVENT_FD_READ;
    }
    if (NS_JOB_IS_WRITE(job_type) || NS_JOB_IS_CONNECT(job_type)) {
        flags |= TEVENT_FD_WRITE;
    }
    return flags;
}

static ns_event_fw_ctx_t *
ns_event_fw_init(void)
{
    ns_event_fw_ctx_t *ev = tevent_context_init(NULL);
    tevent_set_debug(ev, tevent_logger_cb, NULL);
    return ev;
}

static void
ns_event_fw_destroy(ns_event_fw_ctx_t *ns_event_fw_ctx_t)
{
    talloc_free(ns_event_fw_ctx_t);
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
    tevent_fd_set_flags(job->ns_event_fw_fd, 0);
}

static void
ns_event_fw_io_event_done(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    ns_event_fw_io_event_remove(ns_event_fw_ctx, job);
    talloc_free(job->ns_event_fw_fd);
    job->ns_event_fw_fd = NULL;
}

static void
ns_event_fw_timer_event_done(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    talloc_free(job->ns_event_fw_time);
    job->ns_event_fw_time = NULL;
}

static void
ns_event_fw_signal_event_done(
    ns_event_fw_ctx_t *ns_event_fw_ctx __attribute__((unused)),
    ns_job_t *job)
{
    talloc_free(job->ns_event_fw_sig);
    job->ns_event_fw_sig = NULL;
}

static void
ns_event_fw_add_io(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    /* set the fields in the event */
    uint16_t flags = job_type_to_flags(job->job_type);

    job->ns_event_fw_fd = tevent_add_fd(ns_event_fw_ctx, ns_event_fw_ctx,
                                        PR_FileDesc2NativeHandle(job->fd),
                                        flags, fd_event_cb, job);
    /* add/schedule the timer event */
    if (job->tv.tv_sec || job->tv.tv_usec) {
        job->ns_event_fw_time = tevent_add_timer(ns_event_fw_ctx, ns_event_fw_ctx,
                                                 job->tv, timer_event_cb, job);
    }
}

static void
ns_event_fw_mod_io(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    uint16_t events = job_type_to_flags(job->job_type);

    if (job->tv.tv_sec || job->tv.tv_usec) {
        ns_event_fw_mod_timer(ns_event_fw_ctx, job);
    }
    tevent_fd_set_flags(job->ns_event_fw_fd, events);
}

static void
ns_event_fw_add_timer(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    job->ns_event_fw_time = tevent_add_timer(ns_event_fw_ctx, ns_event_fw_ctx,
                                             job->tv, timer_event_cb, job);
}

static void
ns_event_fw_mod_timer(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    ns_event_fw_timer_event_done(ns_event_fw_ctx, job);
    ns_event_fw_add_timer(ns_event_fw_ctx, job);
}

static void
ns_event_fw_add_signal(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    job->ns_event_fw_sig = tevent_add_signal(ns_event_fw_ctx, ns_event_fw_ctx,
                                             job->signal, 0, signal_event_cb, job);
}

static void
ns_event_fw_mod_signal(
    ns_event_fw_ctx_t *ns_event_fw_ctx,
    ns_job_t *job)
{
    ns_event_fw_signal_event_done(ns_event_fw_ctx, job);
    ns_event_fw_add_signal(ns_event_fw_ctx, job);
}

/* returns
   1 - no events to process
   0 - normal termination
   -1 - error
*/
static int
ns_event_fw_loop(ns_event_fw_ctx_t *ns_event_fw_ctx)
{
    int rc = tevent_loop_once(ns_event_fw_ctx);
    if (rc == 0) {
        rc = 1;
    } else {
        rc = -1;
    }
    /* tevent apparently has no normal termination return value */
    return rc;
}

static ns_event_fw_t ns_event_fw_tevent = {
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
get_event_framework_tevent()
{
    return &ns_event_fw_tevent;
}
