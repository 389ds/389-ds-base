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
#ifndef NS_EVENT_FW_H
#define NS_EVENT_FW_H

#include "ns_private.h"

/* For locks and cond var */
#include <pthread.h>

struct ns_event_fw_ctx_t;
struct ns_event_fw_fd_t;
struct ns_event_fw_time_t;
struct ns_event_fw_sig_t;

#ifndef NO_EVENT_TYPEDEFS
typedef struct ns_event_fw_ctx_t ns_event_fw_ctx_t;
typedef struct ns_event_fw_fd_t ns_event_fw_fd_t;
typedef struct ns_event_fw_time_t ns_event_fw_time_t;
typedef struct ns_event_fw_sig_t ns_event_fw_sig_t;
#endif

/*
 * The states we can have for a job.
 * We start in WAITING. Valid transitions are:
 * // With waiting, *any* thread can change this. Prevents races.
 * WAITING -> NEEDS_DELETE
 * WAITING -> NEEDS_ARM
 * NEEDS_ARM -> ARMED
 * NEEDS_ARM -> NEEDS_DELETE
 * ARMED -> RUNNING
 * When a job is ARMED, and tp->shutdown 1, it CAN move to NEEDS_DELETE
 * ARMED -> NEEDS_DELETE // Only by shutdown of event loop
 * // The trick with RUNNING is that only the current thread working thread can change this!
 * RUNNING -> NEEDS_DELETE
 * RUNNING -> NEEDS_ARM
 * RUNNING -> WAITING
 * NEEDS_DELETE -> DELETED
 */

typedef enum _ns_job_state {
    NS_JOB_NEEDS_DELETE = 2,
    NS_JOB_DELETED = 3,
    NS_JOB_NEEDS_ARM = 4,
    NS_JOB_ARMED = 5,
    NS_JOB_RUNNING = 6,
    NS_JOB_WAITING = 7,
} ns_job_state_t;

/* this is our "kitchen sink" pblock/glue object that is the main
   interface between the app/thread pool/event framework */
typedef struct ns_job_t {
    pthread_mutex_t *monitor;
    struct ns_thrpool_t *tp;
    ns_job_func_t func;
    struct ns_job_data_t *data;
    ns_job_type_t job_type; /* NS_JOB_ACCEPT etc. */
    PRFileDesc *fd; /* for I/O events */
    struct timeval tv; /* used for timed events */
    int signal; /* if the event was triggered by a signal, this is the signal number */
    ns_event_fw_fd_t *ns_event_fw_fd; /* event framework fd event object */
    ns_event_fw_time_t *ns_event_fw_time; /* event framework timer event object */
    ns_event_fw_sig_t *ns_event_fw_sig; /* event framework signal event object */
    ns_job_type_t output_job_type; /* info about event that triggered the callback */
    ns_job_state_t state; /* What state the job is currently in. */
    ns_event_fw_ctx_t *ns_event_fw_ctx;
    void *(*alloc_event_context)(size_t size, struct ns_job_t *job);
    void (*free_event_context)(void *ev_ctx, struct ns_job_t *job);
    /* this is called by the event framework when an event is triggered */
    void (*event_cb)(struct ns_job_t *tpec);
    /* Called during dispose of the job, to allow user defined cleanup */
    ns_job_func_t done_cb;
} ns_job_t;

typedef void (*ns_event_fw_accept_cb_t)(PRFileDesc *fd, ns_job_t *job);
typedef void (*ns_event_fw_read_cb_t)(PRFileDesc *fd, ns_job_t *job);
typedef void (*ns_event_fw_write_cb_t)(PRFileDesc *fd, ns_job_t *job);
typedef void (*ns_event_fw_connect_cb_t)(PRFileDesc *fd, ns_job_t *job);

typedef struct ns_event_fw_t {
    ns_event_fw_ctx_t *(*ns_event_fw_init)(void);
    /*
struct event_base *event_base_new(void);
struct tevent_context *tevent_context_init(TALLOC_CTX *mem_ctx);
    */
    void (*ns_event_fw_destroy)(ns_event_fw_ctx_t *ns_event_fw_ctx);
    /*
void event_base_free(struct event_base *);
int talloc_free(void *ptr);
    */
    int (*ns_event_fw_loop)(ns_event_fw_ctx_t *ns_event_fw_ctx); /* run the event fw main loop */
    /*
      the return value - 1 = no events to process; 0 = normal termination; -1 = error
int event_base_loop(struct event_base *, int);
int tevent_loop_wait(struct tevent_context *ev)
    */
    void (*ns_event_fw_add_io)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    /*
void event_set(struct event *, int, short, void (*)(int, short, void *), void *);
int event_add(struct event *ev, const struct timeval *timeout);

#define tevent_fd * tevent_add_fd(ev, mem_ctx, fd, flags, handler, private_data) \
    */
    void (*ns_event_fw_mod_io)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    /* write, accept, etc. */
    void (*ns_event_fw_add_timer)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
        /*
tevent_timer *tevent_add_timer(ev, mem_ctx, next_event, handler, private_data)

evtimer_set(ev, cb, arg)
evtimer_add(ev, tv)
        */
    void (*ns_event_fw_mod_timer)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    void (*ns_event_fw_add_signal)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    /*
struct tevent_signal *tevent_add_signal(ev, mem_ctx, signum, sa_flags, handler, private_data)

signal_set(ev, x, cb, arg)
signal_add(ev, tv)
    */
    void (*ns_event_fw_mod_signal)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    void (*ns_event_fw_io_event_done)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    void (*ns_event_fw_timer_event_done)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
    void (*ns_event_fw_signal_event_done)(
        ns_event_fw_ctx_t *ns_event_fw_ctx,
        ns_job_t *job);
} ns_event_fw_t;

ns_event_fw_t *get_event_framework_event( void );
ns_event_fw_t *get_event_framework_tevent( void );

#endif /* THRPOOL_NS_EVENT_FW_H */
