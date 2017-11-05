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
/*! \file nunc-stans.h
    \brief Nunc Stans public API

    This is the public API for Nunc Stans
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef NS_THRPOOL_H
/** \cond */
#define NS_THRPOOL_H
/** \endcond */

#include "nspr.h"

/**
 * ns_result_t encapsulates the set of results that can occur when interacting
 * with nunc_stans. This is better than a simple status, because we can indicate
 * why a failure occured, and allow the caller to handle it gracefully.
 */
typedef enum _ns_result_t {
    /**
     * Indicate the operation succeded.
     */
    NS_SUCCESS = 0,
    /**
     * indicate that the event loop is shutting down, so we may reject operations.
     */
    NS_SHUTDOWN = 1,
    /**
     * We failed to allocate resources as needed.
     */
    NS_ALLOCATION_FAILURE = 2,
    /**
     * An invalid request was made to the API, you should probably check your
     * call.
     */
    NS_INVALID_REQUEST = 3,
    /**
     * You made a request against a job that would violate the safety of the job
     * state machine.
     */
    NS_INVALID_STATE = 4,
    /**
     * This occurs when a lower level OS issue occurs, generally thread related.
     */
    NS_THREAD_FAILURE = 5,
    /**
     * The job is being deleted
     */
    NS_DELETING = 6,
} ns_result_t;

/**
 * Forward declaration of the thread pool struct
 *
 * The actual struct is opaque to applications.  The forward declaration is here
 * for the typedef.
 */
struct ns_thrpool_t;
/**
 * This is the thread pool typedef
 *
 * The actual thread pool is opaque to applications.
 * \sa ns_thrpool_new, ns_thrpool_wait, ns_thrpool_destroy, ns_job_get_tp
 */
typedef struct ns_thrpool_t ns_thrpool_t;

/** \struct ns_job_t
 * The nunc stans event and worker job object.
 *
 * When a new job is created, a pointer to a struct of this object type
 * is returned.  This is the object that will be the argument to the job
 * callback specified.  Since the struct is opaque, the functions ns_job_get_data(), ns_job_get_tp(),
 * ns_job_get_type(), ns_job_get_fd(), and ns_job_get_output_type() can be used to get
 * information about the job.  The function ns_job_done() must be used
 * when the job should be freed.
 * \sa ns_job_get_data, ns_job_get_fd, ns_job_get_tp, ns_job_get_type, ns_job_get_output_type
 */
struct ns_job_t;

/**
 * The job callback function type
 *
 * Job callback functions must have a function signature of ns_job_func_t.
 * \sa ns_add_io_job, ns_add_io_timeout_job, ns_add_timeout_job, ns_add_signal_job, ns_add_job
 */
typedef void (*ns_job_func_t)(struct ns_job_t *);

/**
 * Flag for jobs that are not associated with an event.
 *
 * Use this flag when creating a job that you want to be run but
 * not associated with an event.  Usually used in conjunction with
 * ns_add_job() and #NS_JOB_THREAD to execute a function using the
 * thread pool.
 * \sa ns_add_job, NS_JOB_THREAD
 */
#define NS_JOB_NONE 0x0
/**
 * Flag for accept() jobs - new connection listeners
 *
 * Use this flag when creating jobs that listen for and accept
 * new connections.  This is typically used in conjunction with
 * the #NS_JOB_PERSIST flag so that the job does not have to be
 * rearmed every time it is called.
 *
 * \code
 *   struct ns_job_t *listenerjob;
 *   PRFileDesc *listenfd = PR_OpenTCPSocket(...);
 *   PR_Bind(listenfd, ...);
 *   PR_Listen(listenfd, ...);
 *   listenerctx = new_listenerctx(...); // the application context object
 *   ns_add_io_job(threadpool, NS_JOB_ACCEPT|NS_JOB_PERSIST,
 *                 accept_new_connection, listenerctx, &listenerjob);
 * \endcode
 * You will probably want to keep track of listenerjob and use it with
 * ns_job_done() at application shutdown time to avoid leaking resources.
 * \sa ns_add_io_job, ns_add_io_timeout_job, NS_JOB_IS_ACCEPT, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_ACCEPT 0x1
/**
 * Flag for jobs that will use connect()
 *
 * When creating an I/O job, set this flag in the job_type to be notified
 * when the file descriptor is available for outgoing connections.  In the
 * job callback, use ns_job_get_output_type() and #NS_JOB_IS_CONNECT to
 * see if the callback was called due to connect available if the callback is
 * used with more than one of the job flags.
 * \sa ns_add_io_job, ns_add_io_timeout_job, ns_job_get_type, ns_job_get_output_type, NS_JOB_IS_CONNECT
 */
#define NS_JOB_CONNECT 0x2
/**
 * Flag for I/O read jobs
 *
 * When creating an I/O job, set this flag in the job_type to be notified
 * when the file descriptor is available for reading.  In the
 * job callback, use ns_job_get_output_type() and #NS_JOB_IS_READ to
 * see if the callback was called due to read available if the callback is
 * used with more than one of the job flags.
 * \sa ns_add_io_job, ns_add_io_timeout_job, ns_job_get_type, ns_job_get_output_type, NS_JOB_IS_READ
 */
#define NS_JOB_READ 0x4
/**
 * Flag for I/O write jobs
 *
 * When creating an I/O job, set this flag in the job_type to be notified
 * when the file descriptor is available for writing.  In the
 * job callback, use ns_job_get_output_type() and #NS_JOB_IS_WRITE to
 * see if the callback was called due to write available if the callback is
 * used with more than one of the job flags.
 * \sa ns_add_io_job, ns_add_io_timeout_job, ns_job_get_type, ns_job_get_output_type, NS_JOB_IS_WRITE
 */
#define NS_JOB_WRITE 0x8
/**
 * Flag for timer jobs
 *
 * When creating a timeout or I/O timeout job, set this flag in the job_type
 * to be notified when the time given by the timeval argument has elapsed.
 * In the job callback, use ns_job_get_output_type() and #NS_JOB_IS_TIMER to
 * see if the callback was called due to elapsed time if the callback is
 * used with more than one of the job flags.
 * \sa ns_add_timeout_job, ns_add_io_timeout_job, ns_job_get_type, ns_job_get_output_type, NS_JOB_IS_TIMER
 */
#define NS_JOB_TIMER 0x10
/**
 * Flag for signal jobs
 *
 * When creating a signal job, set this flag in the job_type
 * to be notified when the process receives the given signal.
 * In the job callback, use ns_job_get_output_type() and #NS_JOB_IS_SIGNAL to
 * see if the callback was called due to receiving the signal if the callback is
 * used with more than one of the job flags.
 * \sa ns_add_signal_job, ns_job_get_type, ns_job_get_output_type, NS_JOB_IS_SIGNAL
 */
#define NS_JOB_SIGNAL 0x20
/**
 * Flag to make jobs persistent
 *
 * By default, when an event (I/O, timer, signal) is triggered and
 * the job callback is called, the event is removed from the event framework,
 * and the application will no longer receive callbacks for events.  The
 * application is then responsible for calling ns_job_rearm to "re-arm"
 * the job to respond to the event again.  Adding the job with the
 * #NS_JOB_PERSIST flag added to the job_type means the job will not have
 * to be rearmed.  This is usually used in conjunction with #NS_JOB_ACCEPT
 * for accept jobs.  Use ns_job_get_type() or ns_job_get_output_type() with
 * #NS_JOB_IS_PERSIST to test if the job is persistent.
 *
 * \note Be very careful when using this flag in conjunction with #NS_JOB_THREAD.
 * For example, for a #NS_JOB_ACCEPT job, once you call accept(), the socket
 * may be immediately available for another accept, and your callback could be
 * called again immediately in another thread.  Same for the other types of I/O
 * jobs.  In that case, your job callback function must be thread safe - global
 * resources must be protected with a mutex, the function must be reentrant, etc.
 *
 * \sa NS_JOB_ACCEPT, NS_JOB_THREAD, NS_JOB_IS_PERSIST, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_PERSIST 0x40
/**
 * Flag to make jobs run in a thread pool thread
 *
 * This flag allows you to specify if you want a job to run threaded or not.
 * If the job is threaded, the job callback is executed by a thread in
 * the thread pool, and the job callback function must be thread safe and
 * reentrant.  If the job is not threaded, the job runs in the same thread
 * as the event loop thread.
 *
 * \note When #NS_JOB_THREAD is \e not used, the job callback will be run in the
 *       event loop thread, and will block all events from being processed.
 *       Care must be taken to ensure that the job callback does not block.
 *
 * Use ns_job_get_type() or ns_job_get_output_type() with #NS_JOB_IS_THREAD to
 * test if the job is threaded.
 *
 * \sa NS_JOB_IS_THREAD, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_THREAD 0x80
/**
 * Flag to tell ns_job_done() not to close the job fd
 *
 * I/O jobs will have a file descriptor (fd). If the job->fd lifecycle
 * is managed by the application, this flag tells ns_job_done() not to close
 * the fd.
 * \sa ns_add_io_job, ns_add_io_timeout_job, ns_job_get_type, ns_job_get_output_type, NS_JOB_IS_PRESERVE_FD
 */
#define NS_JOB_PRESERVE_FD 0x100
/**
 * Internal flag to shutdown a worker thread.
 *
 * If you assign this to a job it will cause the worker thread that dequeues it to be
 * shutdown, ready for pthread_join() to be called on it.
 *
 * You probably DON'T want to use this ever, as it really will shutdown threads
 * and you can't get them back .... you have been warned.
 */
#define NS_JOB_SHUTDOWN_WORKER 0x200
/**
 * Bitflag type for job types
 *
 * This is the job_type bitfield argument used when adding jobs, and the return
 * value of the functions ns_job_get_type() and ns_job_get_output_type().  The
 * value is one or more of the NS_JOB_* macros OR'd together.
 * \code
 *   ns_job_type_t job_type = NS_JOB_READ|NS_JOB_SIGNAL;
 * \endcode
 * When used with ns_job_get_type() or ns_job_get_output_type() to see what type
 * of job it is, use the return value with one of the NS_JOB_IS_* macros:
 * \code
 *   if (NS_JOB_IS_TIMER(ns_job_get_output_type(job))) {
 *     // handle timeout
 *   }
 * \endcode
 * \sa ns_add_io_job, ns_add_io_timeout_job, ns_add_job, ns_add_signal_job, ns_add_timeout_job, ns_job_get_type, ns_job_get_output_type
 */
typedef uint_fast16_t ns_job_type_t;

/**
 * Used to test an #ns_job_type_t value for #NS_JOB_ACCEPT
 * \sa NS_JOB_ACCEPT, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_ACCEPT(eee) ((eee)&NS_JOB_ACCEPT)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_READ
 * \sa NS_JOB_READ, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_READ(eee) ((eee)&NS_JOB_READ)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_CONNECT
 * \sa NS_JOB_CONNECT, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_CONNECT(eee) ((eee)&NS_JOB_CONNECT)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_WRITE
 * \sa NS_JOB_WRITE, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_WRITE(eee) ((eee)&NS_JOB_WRITE)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_TIMER
 * \sa NS_JOB_TIMER, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_TIMER(eee) ((eee)&NS_JOB_TIMER)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_SIGNAL
 * \sa NS_JOB_SIGNAL, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_SIGNAL(eee) ((eee)&NS_JOB_SIGNAL)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_PERSIST
 * \sa NS_JOB_PERSIST, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_PERSIST(eee) ((eee)&NS_JOB_PERSIST)
/**
 * Used to test if an #ns_job_type_t is to shutdown the worker thread.
 */
#define NS_JOB_IS_SHUTDOWN_WORKER(eee) ((eee)&NS_JOB_SHUTDOWN_WORKER)
/**
 * Used to test an #ns_job_type_t value to see if it is any sort of I/O job
 * \sa NS_JOB_IS_ACCEPT, NS_JOB_IS_READ, NS_JOB_IS_CONNECT, NS_JOB_IS_WRITE, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_IO(eee) (NS_JOB_IS_ACCEPT(eee) || NS_JOB_IS_READ(eee) || NS_JOB_IS_CONNECT(eee) || NS_JOB_IS_WRITE(eee))
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_THREAD
 * \sa NS_JOB_THREAD, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_THREAD(eee) ((eee)&NS_JOB_THREAD)
/**
 * Used to test an #ns_job_type_t value for #NS_JOB_PRESERVE_FD
 * \sa NS_JOB_PRESERVE_FD, ns_job_get_type, ns_job_get_output_type
 */
#define NS_JOB_IS_PRESERVE_FD(eee) ((eee)&NS_JOB_PRESERVE_FD)

/**
 * Used to set an #ns_job_type_t value to have #NS_JOB_READ
 */
#define NS_JOB_SET_READ(eee) ((eee) |= NS_JOB_READ)
/**
 * Used to set an #ns_job_type_t value to have #NS_JOB_WRITE
 */
#define NS_JOB_SET_WRITE(eee) ((eee) |= NS_JOB_WRITE)
/**
 * Used to set an #ns_job_type_t value to have #NS_JOB_THREAD
 */
#define NS_JOB_SET_THREAD(eee) ((eee) |= NS_JOB_THREAD)
/**
 * Remove #NS_JOB_READ from an #ns_job_type_t value
 */
#define NS_JOB_UNSET_READ(eee) ((eee) &= ~NS_JOB_READ)
/**
 * Remove #NS_JOB_WRITE from an #ns_job_type_t value
 */
#define NS_JOB_UNSET_WRITE(eee) ((eee) &= ~NS_JOB_WRITE)
/**
 * Remove #NS_JOB_THREAD from an #ns_job_type_t value
 */
#define NS_JOB_UNSET_THREAD(eee) ((eee) &= ~NS_JOB_THREAD)

/**
 * Used to configure the thread pool
 *
 * This is the argument to ns_thrpool_new().  This is used to set all of the
 * configuration parameters for the thread pool.
 *
 * This must be initialized using ns_thrpool_config_init().  This will
 * initialize the fields to their default values.  Use like this:
 * \code
 *   struct ns_thrpool_config nsconfig;
 *   ns_thrpool_config_init(&nsconfig);
 *   nsconfig.max_threads = 16;
 *   nsconfig.malloc_fct = mymalloc;
 *   ...
 *   rc = ns_thrpool_new(&nsconfig);
 * \endcode
 * \sa ns_thrpool_config_init, ns_thrpool_new
 */
struct ns_thrpool_config
{
    /** \cond */
    int32_t init_flag;
    /** \endcond */
    size_t max_threads; /**< Do not grow the thread pool greater than this size */
    size_t stacksize;   /**< Thread stack size */

    /* pluggable logging functions  */
    void (*log_fct)(int, const char *, va_list); /**< Provide a function that works like vsyslog */
    void (*log_start_fct)(void);                 /**< Function to call to initialize the logging system */
    void (*log_close_fct)(void);                 /**< Function to call to shutdown the logging system */

    /* pluggable memory functions */
    void *(*malloc_fct)(size_t);                          /**< malloc() replacement */
    void *(*memalign_fct)(size_t size, size_t alignment); /**< posix_memalign() replacement. Note the argument order! */
    void *(*calloc_fct)(size_t, size_t);                  /**< calloc() replacement */
    void *(*realloc_fct)(void *, size_t);                 /**< realloc() replacement */
    void (*free_fct)(void *);                             /**< free() replacement */
};

/**
 * Initialize a thrpool config struct
 *
 * The config struct must be allocated/freed by the caller.  A stack
 * variable is typically used.
 * \code
 *   struct ns_thrpool_config nsconfig;
 *   ns_thrpool_config_init(&nsconfig);
 *   nsconfig.max_threads = 16;
 *   nsconfig.malloc_fct = mymalloc;
 *   ...
 *   rc = ns_thrpool_new(&nsconfig);
 * \endcode
 * \sa ns_thrpool_config, ns_thrpool_new
 * \param tp_config - thread pool config struct
 */
void ns_thrpool_config_init(struct ns_thrpool_config *tp_config);

/**
 * The application is finished with this job
 *
 * The application uses this function to tell nunc-stans that it is finished
 * using this job.  Once the application calls this function, it may no
 * longer refer to job - it should be considered as an allocated pointer
 * that the free() function has been called with.  An application will
 * typically call ns_job_done() at the end of a job callback function for
 * non-persistent jobs (not using #NS_JOB_PERSIST), or at application
 * shutdown time for persistent jobs (using #NS_JOB_PERSIST).  For an I/O job,
 * ns_job_done will close() the file descriptor associated with the job unless
 * the #NS_JOB_PRESERVE_FD is specified when the job is added.
 *
 * Note that a persistant job can only disarm/ns_job_done() itself, unless the
 * threadpool is in shutdown, then external threads may request the job to be marked
 * as done(). This is to protect from a set of known race conditions that may occur.
 *
 * All jobs *must* have ns_job_done() called upon them to clean them correctly. Failure
 * to do so may cause resource leaks.
 *
 * \code
 *   void read_callback(struct ns_job_t *job)
 *   {
 *       appctx_t *appctx = (appctx_t *)ns_job_get_data(job);
 *       ...
 *       ns_job_done(job);
 *       // ok to use appctx here, but not job
 *       // app must free or ensure appctx is not leaked
 *       return;
 *   }
 * \endcode
 * \param job the job to clean up
 * \retval NS_SUCCESS Job was successfully queued for removal.
 * \retval NS_INVALID_STATE Failed to mark job for removal. Likely the job is ARMED!
 *    We cannot remove jobs that are armed due to the race conditions it can cause.
 * \retval NS_INVALID_REQUEST No job was provided to remove (IE NULL request)
 * \sa ns_job_t, ns_job_get_data, NS_JOB_PERSIST, NS_JOB_PRESERVE_FD
 */
ns_result_t ns_job_done(struct ns_job_t *job);

/**
 * Create a new job which is not yet armed.
 *
 * Specify the type of job using the job_type bitfield.  You can specify
 * more than one type of job.
 *
 * The callback will need to rearm the job or add another job if it wants to
 * be notified of more events.
 *
 * This job is not armed at creation unlike other ns_add_*_job. This means that
 * after the job is created, you can use ns_job_set_*, and when ready, arm the job.
 *
 * \code
 *   struct ns_job_t *job;
 *   ns_create_job(tp, NS_JOB_READ, my_callback, &job);
 *   ns_job_set_data(job, data);
 *   // You may not alter job once it is armed.
 *   ns_job_rearm(job);
 * \endcode
 *
 * \param tp The thread pool you want to add an I/O job to.
 * \param job_type A set of flags that indicates the job type.
 * \param func The callback function to call when processing the job.
 * \param[out] job The address of a job pointer that will be filled in once the job is allocated.
 * \retval NS_SUCCESS Job was successfully added.
 * \retval NS_ALLOCATION_FAILURE Failed to allocate job.
 * \retval NS_INVALID_REQUEST As create job does not create armed, if you make a request
 *  with a NULL job parameter, this would create a memory leak. As a result, we fail if
 *  the request is NULL.
 * \warning The thread pool will not allow a job to be added when it has been signaled
 *          to shutdown.  It will return PR_FAILURE in that case.
 * \sa ns_job_t, ns_job_get_data, NS_JOB_READ, NS_JOB_WRITE, NS_JOB_ACCEPT, NS_JOB_CONNECT, NS_JOB_IS_IO, ns_job_done
 */
ns_result_t
ns_create_job(struct ns_thrpool_t *tp, ns_job_type_t job_type, ns_job_func_t func, struct ns_job_t **job);

/**
 * Adds an I/O job to the thread pool
 *
 * Specify the type of I/O job using the job_type bitfield.  You can specify
 * more than one type of I/O job.  Use ns_job_get_output_type(job) to
 * determine which event triggered the I/O.
 * \code
 *   ns_add_io_job(tp, fd, NS_JOB_READ|NS_JOB_WRITE, my_io_callback, ...);
 *   ...
 *   void my_io_callback(struct ns_job_t *job)
 *   {
 *       if (NS_JOB_IS_READ(ns_job_get_output_type(job))) {
 *           // handle reading from fd
 *       } else {
 *           // handle writing to fd
 *       }
 *   }
 * \endcode
 * The callback will need to rearm the job or add another job if it wants to
 * be notified of more events, or use #NS_JOB_PERSIST.  If you want an I/O job
 * that will timeout if I/O is not detected within a certain period of time,
 * use ns_add_io_timeout_job().
 * \param tp The thread pool you want to add an I/O job to.
 * \param fd The file descriptor to use for I/O.
 * \param job_type A set of flags that indicates the job type.
 * \param func The callback function to call when processing the job.
 * \param data Arbitrary data that will be available to the job callback function.
 * \param[out] job The address of a job pointer that will be filled in once the job is allocated.
 *            \c NULL can be passed if a pointer to the job is not needed.
 * \retval NS_SUCCESS Job was successfully added.
 * \retval NS_ALLOCATION_FAILURE Failed to allocate job.
 * \retval NS_INVALID_REQUEST An invalid job request was made: likely you asked for an
 * accept job to be threaded, which is currently invalid.
 * \warning The thread pool will not allow a job to be added when it has been signaled
 *          to shutdown.  It will return PR_FAILURE in that case.
 * \sa ns_job_t, ns_job_get_data, NS_JOB_READ, NS_JOB_WRITE, NS_JOB_ACCEPT, NS_JOB_CONNECT, NS_JOB_IS_IO, ns_job_done
 */
ns_result_t ns_add_io_job(struct ns_thrpool_t *tp,
                          PRFileDesc *fd,
                          ns_job_type_t job_type,
                          ns_job_func_t func,
                          void *data,
                          struct ns_job_t **job);

/**
 * Adds a timeout job to the thread pool
 *
 * The func function will be called when the timer expires.
 *
 * \param tp The thread pool you want to add a timeout job to.
 * \param tv The timer that needs to expire before triggering the callback function.
 * \param job_type A set of flags that indicates the job type - #NS_JOB_TIMER + other flags
 * \param func The callback function to call when processing the job.
 * \param data Arbitrary data that will be available to the job callback function.
 * \param[out] job The address of a job pointer that will be filled in once the job is allocated.
 *            \c NULL can be passed if a pointer to the job is not needed.
 * \retval NS_SUCCESS Job was successfully added.
 * \retval NS_ALLOCATION_FAILURE Failed to allocate job.
 * \retval NS_INVALID_REQUEST An invalid job request was made: likely you asked for a
 * timeout that is not valid (negative integer).
 * \warning The thread pool will not allow a job to be added when it has been signaled
 *          to shutdown.  It will return PR_FAILURE in that case.
 * \sa ns_job_t, ns_job_get_data, NS_JOB_TIMER, NS_JOB_IS_TIMER, ns_job_done
 */
ns_result_t ns_add_timeout_job(struct ns_thrpool_t *tp,
                               struct timeval *tv,
                               ns_job_type_t job_type,
                               ns_job_func_t func,
                               void *data,
                               struct ns_job_t **job);

/**
 * Adds an I/O job to the thread pool's work queue with a timeout.
 *
 * The callback func function should test the type of event that triggered
 * the callback using ns_job_get_output_type(job) to get the
 * #ns_job_type_t, then use #NS_JOB_IS_TIMER(output_type) to see if
 * this callback was triggered by a timer event.  This is useful if
 * you want to perform some sort of I/O, but you require that I/O
 * must happen in a certain amount of time.
 * \code
 *   ns_add_io_timeout_job(tp, fd, &tv, NS_JOB_READ|NS_JOB_TIMER, my_iot_callback, ...);
 *   ...
 *   void my_iot_callback(struct ns_job_t *job)
 *   {
 *       if (NS_JOB_IS_TIMER(ns_job_get_output_type(job))) {
 *           // handle timeout condition
 *       } else {
 *           // handle read from fd
 *       }
 *   }
 * \endcode
 * \note This is like adding an I/O job, with an optional timeout.
 * This is not like adding a timeout job with an additional I/O
 * event component.  This depends on the underlying event framework
 * having the ability to have a timed I/O job.  For example, libevent
 * I/O events can have a timeout.
 *
 * \param tp The thread pool whose work queue you want to add an I/O job to.
 * \param fd The file descriptor to use for I/O.
 * \param tv The timer that needs to expire before triggering the callback function.
 * \param job_type A set of flags that indicates the job type.
 * \param func The callback function for a worker thread to call when processing the job.
 * \param data Arbitrary data that will be available to the job callback function.
 * \param[out] job The address of a job pointer that will be filled in once the job is allocated.
 *            \c NULL can be passed if a pointer to the job is not needed.
 * \retval NS_SUCCESS Job was successfully added.
 * \retval NS_ALLOCATION_FAILURE Failed to allocate job.
 * \retval NS_INVALID_REQUEST An invalid job request was made: likely you asked for a
 * timeout that is not valid (negative integer). Another failure is you requested a
 * threaded accept job.
 * \warning The thread pool will not allow a job to be added when it has been signaled
 *          to shutdown.  It will return NS_SHUTDOWN in that case.
 * \sa ns_job_t, ns_job_get_data, NS_JOB_READ, NS_JOB_WRITE, NS_JOB_ACCEPT, NS_JOB_CONNECT, NS_JOB_IS_IO, ns_job_done, NS_JOB_TIMER, NS_JOB_IS_TIMER
 */
ns_result_t ns_add_io_timeout_job(struct ns_thrpool_t *tp,
                                  PRFileDesc *fd,
                                  struct timeval *tv,
                                  ns_job_type_t job_type,
                                  ns_job_func_t func,
                                  void *data,
                                  struct ns_job_t **job);

/**
 * Adds a signal job to the thread pool
 *
 * The \a func function will be called when the signal is received by the process.
 *
 * \param tp The thread pool you want to add a signal job to.
 * \param signum The signal number that you want to trigger the callback function.
 * \param job_type A set of flags that indicates the job type.
 * \param func The callback function to call when processing the job.
 * \param data Arbitrary data that will be available to the job callback function.
 * \param[out] job The address of a job pointer that will be filled in once the job is allocated.
 *            \c NULL can be passed if a pointer to the job is not needed.
 * \retval NS_SUCCESS Job was successfully added.
 * \retval NS_ALLOCATION_FAILURE Failed to allocate job.
 * \warning The thread pool will not allow a job to be added when it has been signaled
 *          to shutdown.  It will return PR_FAILURE in that case.
 * \sa ns_job_t, ns_job_get_data, NS_JOB_SIGNAL, NS_JOB_IS_SIGNAL
 */
ns_result_t ns_add_signal_job(ns_thrpool_t *tp,
                              int32_t signum,
                              ns_job_type_t job_type,
                              ns_job_func_t func,
                              void *data,
                              struct ns_job_t **job);

/**
 * Add a non-event related job to the thread pool
 *
 * A non-event related job is a job that is executed immediately that is not contingent
 * on an event or signal.  This is typically used when the application wants to do some
 * processing in parallel using a thread from the thread pool.
 * \code
 *   ns_add_job(tp, NS_JOB_NONE|NS_JOB_THREAD, my_callback, ...);
 *   ...
 *   void my_callback(struct ns_job_t *job)
 *   {
 *      // now in a separate thread
 *   }
 * \endcode
 *
 * \param tp The thread pool you want to add the job to.
 * \param job_type A set of flags that indicates the job type (usually just NS_JOB_NONE|NS_JOB_THREAD)
 * \param func The callback function to call when processing the job.
 * \param data Arbitrary data that will be available to the job callback function.
 * \param[out] job The address of a job pointer that will be filled in once the job is allocated.
 *            \c NULL can be passed if a pointer to the job is not needed.
 * \retval NS_SUCCESS Job was successfully added.
 * \retval NS_ALLOCATION_FAILURE Failed to allocate job.
 * \warning The thread pool will not allow a job to be added when it has been signaled
 *          to shutdown.  It will return PR_FAILURE in that case.
 * \sa ns_job_t, ns_job_get_data, NS_JOB_NONE, NS_JOB_THREAD
 */
ns_result_t ns_add_job(ns_thrpool_t *tp, ns_job_type_t job_type, ns_job_func_t func, void *data, struct ns_job_t **job);

/**
 * Allows the callback to access the file descriptor for an I/O job
 *
 * \code
 *   void my_io_job_callback(struct ns_job_t *job)
 *   {
 *       PRFileDesc *fd = ns_job_get_fd(job);
 *       rc = PR_Read(fd, ...);
 *       ...
 *   }
 * \endcode
 * If the job is not an I/O job, the function will return NULL.
 *
 * \param job The job to get the fd for.
 * \retval fd The file descriptor associated with the I/O job.
 * \retval NULL The job is not an I/O job
 * \sa ns_job_t, ns_add_io_job, ns_add_io_timeout_job
 */
PRFileDesc *ns_job_get_fd(struct ns_job_t *job);

/**
 * Allows the callback to access the private data field in the job.
 *
 * This is the \c data field passed in when the job is added.  This
 * data is private to the application - nunc-stans does not touch it
 * in any way.  The application is responsible for managing the lifecycle
 * of this data.
 * \code
 *   void my_job_callback(struct ns_job_t *job)
 *   {
 *       myappctx_t *myappctx = (myappctx_t *)ns_job_get_data(job);
 *       ...
 *   }
 * \endcode
 *
 * \param job The job to get the data for.
 * \return The private data associated with the job.
 * \sa ns_job_t
 */
void *ns_job_get_data(struct ns_job_t *job);

/**
 * Allows the caller to set private data into the job
 * Care should be taken to make sure that the previous contents are
 * freed, or that the data is freed after use. Leaks will be annoying to track
 * down with this!
 *
 * This sets the \c data field. This data is private to the application - nunc-stans
 * will not touch it in any way.
 * \code
 *   void my_job_callback(struct ns_job_t *job)
 *   {
 *      myappctx_t *myappctx = malloc(sizeof(struct appctx));
 *      ...
 *      // You must check and *free* the data if required.
 *      // Else you may introduce a memory leak!
 *      void *data = ns_job_get_data(job);
 *      if (data != NULL) {
 *          myapp_use_data(data);
 *          ...
 *      }
 *      free(data);
 *      if (ns_job_set_data(job, (void *)myappctx) == PR_SUCCESS) {
 *          //handle the error, you probably have a bug ....
 *      }
 *   }
 * \endcode
 *
 * \param job The job to set the data for
 * \param data The void * pointer to the data to set
 * \retval NS_SUCCESS Job was modified correctly.
 * \retval NS_INVALID_STATE Failed to modify the job as this may be unsafe.
 * \sa ns_job_t
 */
ns_result_t ns_job_set_data(struct ns_job_t *job, void *data);

/**
 * Allows the callback to access the job type flags.
 *
 * Usually used in conjunction with one of the NS_JOB_IS_* macros.
 *
 * \code
 *   void my_job_callback(struct ns_job_t *job)
 *   {
 *       if (NS_JOB_IS_READ(ns_job_get_type(job)) {
 *       ...
 *       }
 *   }
 * \endcode
 * \param job The job to get the type for.
 * \return The #ns_job_type_t flags for the job
 * \sa ns_job_t, NS_JOB_IS_READ, NS_JOB_IS_WRITE, NS_JOB_IS_IO, NS_JOB_IS_TIMER
 */
ns_job_type_t ns_job_get_type(struct ns_job_t *job);

/**
 * Allows the callback to access the thread pool that the job is associated with.
 *
 * Useful for adding jobs from within job callbacks.
 *
 * \code
 *   void my_job_callback(struct ns_job_t *job)
 *   {
 *      // do some work
 *      // need to listen for some events
 *      ns_add_io_job(ns_job_get_tp(job), fd, ...);
 *      // finished with job
 *      ns_job_done(job);
 *      return;
 *   }
 * \endcode
 * \param job The job to get the thread pool for.
 * \return The thread pool associated with the job.
 * \sa ns_job_t, ns_add_io_job
 */
ns_thrpool_t *ns_job_get_tp(struct ns_job_t *job);

/**
 * Allows the callback to know which event triggered the callback. Can only be called from within the callback itself.
 *
 * The callback func may need to know which event triggered the
 * callback.  This function will allow access to the type of event
 * that triggered the callback.  For example, when using
 * ns_add_io_timeout_job(), the callback can be called either because of
 * an I/O event or a timer event.  Use #NS_JOB_IS_TIMER to tell if
 * the event is a timer event, like this:
 * \code
 *   if (NS_JOB_IS_TIMER(ns_job_get_output_type(job))) {
 *      ... handle timeout ...
 *   } else {
 *      ... handle I/O ...
 *   }
 * \endcode
 * \param job The job to get the output type for.
 * \return The #ns_job_type_t corresponding to the event that triggered the callback
 * \sa ns_job_t, NS_JOB_IS_TIMER, NS_JOB_IS_READ, NS_JOB_IS_WRITE, NS_JOB_IS_IO, ns_add_io_timeout_job
 */
ns_job_type_t ns_job_get_output_type(struct ns_job_t *job);

/**
 * Allows setting the job done callback.
 *
 * The job done callback will be triggered when ns_job_done is called on the
 * job. This allows jobs to have private data fields cleaned and freed correctly
 * \code
 *  ns_create_job(tp, NS_JOB_READ, my_callback, &job);
 *  if (ns_job_set_done_cb(job, my_done_callback) != PR_SUCCESS) {
 *      // you must handle this error!!!! the cb did not set!
 *  }
 *  ns_job_rearm(job);
 *  ...
 *  void my_done_callback(struct ns_job_t job) {
 *      free(ns_job_get_data(job));
 *  }
 * \endcode
 * \param job The job to set the callback for.
 * \param func The callback function, to be called when ns_job_done is triggered.
 * \retval NS_SUCCESS Job was modified correctly.
 * \retval NS_INVALID_STATE Failed to modify the job as this may be unsafe.
 * \sa ns_job_t, ns_job_done
 */
ns_result_t ns_job_set_done_cb(struct ns_job_t *job, ns_job_func_t func);

/**
 * Block until a job is completed. This returns the next state of the job as as a return.
 *
 * \param job The job to set the callback for.
 * \retval ns_job_state_t The next state the job will move to. IE, WAITING, DELETED, ARMED.
 */
ns_result_t ns_job_wait(struct ns_job_t *job);

/**
 * Creates a new thread pool
 *
 * Must be called with a struct ns_thrpool_config that has been
 * initialized by ns_thrpool_config_init().  Typically, once the
 * thread pool has been created, one or more listener jobs or
 * other long lived jobs will be added, and the application will
 * just call ns_thrpool_wait().  The application should add at least
 * one job that will listen for shutdown events, signals, etc. which
 * will call ns_thrpool_shutdown().  After ns_thrpool_wait() returns,
 * the application should use ns_job_done() to finish any long-lived
 * jobs, then call ns_thrpool_destroy().
 * \param config A pointer to a struct ns_thrpool_config
 * \return A pointer to the newly created thread pool.
 * \sa ns_thrpool_config, ns_thrpool_config_init, ns_thrpool_wait, ns_thrpool_shutdown, ns_thrpool_destroy
 */
struct ns_thrpool_t *ns_thrpool_new(struct ns_thrpool_config *config);

/**
 * Frees a thread pool from memory
 *
 * This will free a thread pool and it's internal resources from memory.  You should
 * be sure that the thread pool has been shutdown before destroying it by calling
 * ns_thrpool_wait(), then call ns_job_done() to finish any long-lived jobs.  After
 * calling ns_thrpool_destroy(), do not use tp.
 *
 * \param tp The thread pool to destroy.
 * \sa ns_thrpool_config, ns_thrpool_config_init, ns_thrpool_new, ns_thrpool_wait, ns_thrpool_shutdown
 */
void ns_thrpool_destroy(struct ns_thrpool_t *tp);

/**
 * Tells a thread pool to shutdown its threads
 *
 * The application will usually call ns_thrpool_shutdown() from an event
 * callback that is listening for shutdown events e.g. a signal job that
 * is listening for SIGINT or SIGTERM events.
 * \code
 *   tp = ns_thrpool_new(...);
 *   ns_add_signal_job(tp, SIGTERM, handle_shutdown_signal, NS_JOB_SIGNAL|NS_JOB_THREAD, ...);
 *
 *   void handle_shutdown_signal(job)
 *   {
 *     ns_thrpool_shutdown(ns_job_get_tp(job));
 *     // set application shutdown flag
 *     return;
 *   }
 * \endcode
 * Use NS_JOB_SIGNAL|NS_JOB_THREAD so that the job will run in a worker thread, not
 * in the event loop thread.
 * \note The application must call ns_thrpool_shutdown() or ns_thrpool_wait() will
 *       never return.
 *
 * \param tp The thread pool to shutdown.
 * \warning This must only be called from a job that runs in a worker thread.  Calling
 *          this function from the event thread can cause a deadlock.
 * \sa ns_thrpool_config, ns_thrpool_config_init, ns_thrpool_new, ns_thrpool_wait, ns_thrpool_destroy, ns_add_signal_job, NS_JOB_THREAD
 */
void ns_thrpool_shutdown(struct ns_thrpool_t *tp);

/**
 * Checks if a thread pool is shutting down
 *
 * This can be called by worker threads so they know when the thread pool has
 * been requested to shut down.
 *
 * \retval 0 if the thread pool is not shutting down.
 * \retval 1 if the thread pool is shutting down.
 * \sa ns_thrpool_shutdown
 */
int32_t ns_thrpool_is_shutdown(struct ns_thrpool_t *tp);

/**
 * Waits for all threads in the pool to exit
 *
 * This call will block the caller until all threads in the thread pool have exited.  A
 * program will typically create the thread pool by calling ns_thrpool_new(), then it
 * will call ns_thrpool_wait() to wait until the thread pool is shutdown (which is likely
 * initiated by a signal handler).  Once this function successfully returns, the thread
 * pool can be safely destroyed by calling ns_thrpool_destroy().
 * \note The application must call ns_thrpool_shutdown() or ns_thrpool_wait() will
 *       never return.
 *
 * \param tp The thread pool to wait for.
 * \retval NS_SUCCESS The thread pool threads completed successfully
 * \retval NS_THREAD_FAILURE Failure waiting for a thread to rejoin.
 * \sa ns_thrpool_config, ns_thrpool_config_init, ns_thrpool_new, ns_thrpool_destroy, ns_thrpool_shutdown
 */
ns_result_t ns_thrpool_wait(struct ns_thrpool_t *tp);


/**
 * Convenience function to re-arm the same job
 *
 * This is used for non-persistent (not using #NS_JOB_PERSIST) jobs.
 * For example, if you have an I/O reading job, and the job needs to read more data,
 * the job callback can just call ns_job_rearm(), and the job callback will be
 * called again when read is ready on the job fd.  Once this function is called,
 * the job callback may be called immediately if the job uses #NS_JOB_THREAD.  Do not
 * refer to job after calling ns_job_rearm().
 * \note Do not call ns_job_done() with a job if using ns_job_rearm() with the job
 * \param job The job to re-arm
 * \retval NS_SUCCESS The job was queued correctly.
 * \retval NS_SHUTDOWN The job was not able to be queued as the server is in the procees
 * of shutting down.
 * \retval NS_INVALID_STATE The job was not able to be queued as it is in an invalid state
 * \retval NS_INVALID_REQUEST The job to be queued is invalid.
 * \sa ns_job_t, ns_job_done, NS_JOB_PERSIST, NS_JOB_THREAD
 */
ns_result_t ns_job_rearm(struct ns_job_t *job);

#endif /* NS_THRPOOL_H */
