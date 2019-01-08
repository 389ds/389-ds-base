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

/*
 * A self hosting echo server, that stress tests job addition,
 * removal, timers, and more.
 */

/* Our local stress test header */
#include "test_nuncstans_stress.h"

struct conn_ctx
{
    size_t offset;              /* current offset into buffer for reading or writing */
    size_t len;                 /* size of buffer */
    size_t needed;              /* content-length + start of body */
    size_t body;                /* when reading, offset from buffer of beginning of http body */
    size_t cl;                  /* http content-length when reading */
#define CONN_BUFFER_SIZE BUFSIZ /* default buffer size */
    char *buffer;
};

static FILE *logfp;

void do_logging(int, const char *, ...);

int64_t client_success_count = 0;
int64_t server_success_count = 0;
int64_t client_fail_count = 0;
int64_t client_timeout_count = 0;
int64_t server_fail_count = 0;

int
ns_stress_teardown(void **state)
{
    struct test_params *tparams = (struct test_params *)*state;
    free(tparams);
    return 0;
}

#define PR_WOULD_BLOCK(iii) (iii == PR_PENDING_INTERRUPT_ERROR) || (iii == PR_WOULD_BLOCK_ERROR)

static void
setup_logging(void)
{
    logfp = stdout;
}

static void
do_vlogging(int level, const char *format, va_list varg)
{
#ifdef DEBUG
    if (level <= LOG_DEBUG) {
#else
    if (level <= LOG_ERR) {
#endif
        fprintf(logfp, "%d ", level);
        vfprintf(logfp, format, varg);
    }
}

void
do_logging(int level, const char *format, ...)
{
    va_list varg;
    va_start(varg, format);
    do_vlogging(level, format, varg);
    va_end(varg);
}

/* Server specifics */

static struct conn_ctx *
conn_ctx_new(void)
{
    struct conn_ctx *connctx = calloc(1, sizeof(struct conn_ctx));
    return connctx;
}

static void
conn_ctx_free(struct conn_ctx *connctx)
{
    /* Why don't we use PR_DELETE here? */
    if (connctx->buffer != NULL) {
        free(connctx->buffer);
    }
    free(connctx);
}

static void
server_conn_write(struct ns_job_t *job)
{
    struct conn_ctx *connctx;
    int32_t len;

    do_logging(LOG_DEBUG, "job about to write ...\n");
    assert(job != NULL);
    connctx = (struct conn_ctx *)ns_job_get_data(job);
    assert(connctx != NULL);
    if (NS_JOB_IS_TIMER(ns_job_get_output_type(job))) {
        do_logging(LOG_ERR, "conn_write: job [%p] timeout\n", job);
        __atomic_add_fetch_8(&server_fail_count, 1, __ATOMIC_ACQ_REL);
        conn_ctx_free(connctx);
        assert_int_equal(ns_job_done(job), 0);
        return;
    }

    /* Get the data out of our connctx */
    char *data = calloc(1, sizeof(char) * (connctx->offset + 1));
    memcpy(data, connctx->buffer, connctx->offset);
    data[connctx->offset] = '\0';

    /* Should I write a new line also */
    len = PR_Write(ns_job_get_fd(job), data, connctx->offset);

    /* Set the buffer window back to the start */
    connctx->offset = 0;

    if (len < 0) {
        /* Error */
        printf("ERROR: occured in conn_write\n");
    }
    /* After we finish writing, do we stop being a thread? */
    do_logging(LOG_DEBUG, "Wrote \"%s\"\n", data);
    free(data);
    /* The job is still a *read* IO event job, so this should be okay */
    assert_int_equal(ns_job_rearm(job), 0);
    return;
}

static void
server_conn_read(struct ns_job_t *job)
{
    do_logging(LOG_DEBUG, "Reading from connection\n");

    struct conn_ctx *connctx;
    int32_t len;
    int32_t nbytes;

    assert(job != NULL);
    connctx = (struct conn_ctx *)ns_job_get_data(job);
    assert(connctx != NULL);

    if (NS_JOB_IS_TIMER(ns_job_get_output_type(job))) {
        /* The event that triggered this call back is because we timed out waiting for IO */
        do_logging(LOG_ERR, "conn_read: job [%p] timed out\n", job);
        __atomic_add_fetch_8(&server_fail_count, 1, __ATOMIC_ACQ_REL);
        conn_ctx_free(connctx);
        assert_int_equal(ns_job_done(job), 0);
        return;
    }

    if (connctx->needed != 0) {
        nbytes = connctx->needed - connctx->offset;
    } else {
        nbytes = CONN_BUFFER_SIZE;
    }

    /* If our buffer is incorrectly sized, realloc it to match what we are about to read */
    if ((nbytes + connctx->offset) > connctx->len) {
        connctx->len = nbytes + connctx->offset;
        connctx->buffer = (char *)PR_Realloc(connctx->buffer, connctx->len * sizeof(char));
    }

    /* Read and append to our buffer */
    len = PR_Read(ns_job_get_fd(job), connctx->buffer + connctx->offset, nbytes);
    if (len < 0) {
        PRErrorCode prerr = PR_GetError();
        if (PR_WOULD_BLOCK(prerr)) {
            /* We don't have poll, so we rearm the job for more data */
            if (NS_JOB_IS_PERSIST(ns_job_get_type(job)) != 0) {
                assert_int_equal(ns_job_rearm(job), 0);
            }
            do_logging(LOG_ERR, "conn_read: block error for job [%p] %d: %s\n", job, PR_GetError(), PR_ErrorToString(PR_GetError(), PR_LANGUAGE_I_DEFAULT));
            return;
        } else {
            do_logging(LOG_ERR, "conn_read: read error for job [%p] %d: %s\n", job, PR_GetError(), PR_ErrorToString(PR_GetError(), PR_LANGUAGE_I_DEFAULT));
#ifdef ATOMIC_64BIT_OPERATIONS
            __atomic_add_fetch_8(&server_fail_count, 1, __ATOMIC_ACQ_REL);
#else
            PR_AtomicIncrement(&server_fail_count);
#endif
            conn_ctx_free(connctx);
            assert_int_equal(ns_job_done(job), 0);
            return;
        }
        /* Error */
    } else if (len == 0) {
        /* Didn't read anything */
        do_logging(LOG_DEBUG, "conn_read: job [%p] closed\n", job);
        /* Increment the success */
        __atomic_add_fetch_8(&server_success_count, 1, __ATOMIC_ACQ_REL);
        conn_ctx_free(connctx);
        assert_int_equal(ns_job_done(job), 0);
        return;
    } else {
        /* Wild data appears! */
        connctx->offset += len;
        char *data = PR_Malloc(sizeof(char) * (connctx->offset + 1));
        memcpy(data, connctx->buffer, connctx->offset);
        data[connctx->offset] = '\0';
        do_logging(LOG_DEBUG, "[%p] data received \n", job);
        do_logging(LOG_DEBUG, "data: \"%s\" \n", data);
        free(data);
        server_conn_write(job);
        do_logging(LOG_DEBUG, "job rearmed for write ...\n");
        return;
    }
}

static void
server_conn_handler(struct ns_job_t *job)
{
    do_logging(LOG_DEBUG, "Handling a connection\n");

    assert(job != NULL);

    if (NS_JOB_IS_READ(ns_job_get_type(job)) != 0) {
        server_conn_read(job);
    } else {
        /* We should not be able to get here! */
        assert(0);
    }

    return;
}

static void
server_listen_accept(struct ns_job_t *job)
{
    PRFileDesc *connfd = NULL;
    struct conn_ctx *connctx;
    PRSocketOptionData prsod = {PR_SockOpt_Nonblocking, {PR_TRUE}};

    PR_ASSERT(job);

    PRFileDesc *listenfd = ns_job_get_fd(job);
    PR_ASSERT(listenfd);
    connfd = PR_Accept(listenfd, NULL, PR_INTERVAL_NO_WAIT);

    if (connfd != NULL) {
        PR_SetSocketOption(connfd, &prsod);
        connctx = conn_ctx_new();

        assert_int_equal(ns_add_io_job(ns_job_get_tp(job), connfd, NS_JOB_READ | NS_JOB_THREAD, server_conn_handler, connctx, NULL), 0);

        do_logging(LOG_DEBUG, "server_listen_accept: accepting connection to job [%p]\n", job);

    } else {
        PRErrorCode prerr = PR_GetError();
        if (PR_WOULD_BLOCK(prerr)) {
            /* Let it go .... let it gooooo! */
            /* Can't hold up connection dispatch anymore! */
        } else {
            do_logging(LOG_ERR, "server_listen_accept: accept error for job [%p] %d %s\n", job, prerr, PR_ErrorToString(prerr, PR_LANGUAGE_I_DEFAULT));
        }
    }
}

static struct ns_job_t *
server_listener_init(ns_thrpool_t *tp, PRFileDesc *listenfd)
{
    struct ns_job_t *listen_job = NULL;
    ns_add_io_job(tp, listenfd, NS_JOB_ACCEPT | NS_JOB_PERSIST, server_listen_accept, NULL, &listen_job);
    return listen_job;
}

/* Client specifics */

static void
test_client_shutdown(struct ns_job_t *job)
{
    do_logging(LOG_DEBUG, "Received shutdown signal\n");
    do_logging(LOG_DEBUG, "status .... fail_count: %d success_count: %d\n", client_fail_count, client_success_count);
    ns_thrpool_shutdown(ns_job_get_tp(job));
    /* This also needs to start the thrpool shutdown for the server. */
    ns_thrpool_shutdown(ns_job_get_data(job));
}

static void
client_response_cb(struct ns_job_t *job)
{

    char *buffer = calloc(1, 20);
    int32_t buflen = 20;
    int32_t len = 0;

    len = PR_Read(ns_job_get_fd(job), buffer, buflen);
    if (len < 0) {
        /* PRErrorCode prerr = PR_GetError(); */
        do_logging(LOG_ERR, "FAIL: connection error, no data \n");
        __atomic_add_fetch_8(&client_fail_count, 1, __ATOMIC_ACQ_REL);
        goto done;
    } else if (len == 0) {
        do_logging(LOG_ERR, "FAIL: connection closed, no data \n");
        __atomic_add_fetch_8(&client_fail_count, 1, __ATOMIC_ACQ_REL);
        goto done;
    } else {
        /* Be paranoid, force last byte null */
        buffer[buflen - 1] = '\0';
        if (strncmp("this is a test!\n", buffer, strlen("this is a test!\n")) != 0) {
            do_logging(LOG_ERR, "FAIL: connection incorrect response, no data \n");
            __atomic_add_fetch_8(&client_fail_count, 1, __ATOMIC_ACQ_REL);
            goto done;
        }
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    __atomic_add_fetch_8(&client_success_count, 1, __ATOMIC_ACQ_REL);
    do_logging(LOG_ERR, "PASS: %ld.%ld %d\n", ts.tv_sec, ts.tv_nsec, client_success_count);

done:
    free(buffer);
    assert_int_equal(ns_job_done(job), 0);
}

static void
client_initiate_connection_cb(struct ns_job_t *job)
{
    /* Create a socket */
    PRFileDesc *sock = NULL;
    PRNetAddr netaddr = {{0}};
    char *data = "this is a test!\n";

    sock = PR_OpenTCPSocket(PR_AF_INET);
    if (sock == NULL) {
        char *err = NULL;
        PR_GetErrorText(err);
        do_logging(LOG_ERR, "FAIL: Socket failed, %d -> %s\n", PR_GetError(), err);
        __atomic_add_fetch_8(&client_fail_count, 1, __ATOMIC_ACQ_REL);
        goto done;
    }

    PR_SetNetAddr(PR_IpAddrLoopback, PR_AF_INET, 12345, &netaddr);

    /* Connect */
    /*  */
    if (PR_Connect(sock, &netaddr, PR_SecondsToInterval(5)) != 0) {
        /* char *err = malloc(PR_GetErrorTextLength()); */
        char *err = NULL;
        PR_GetErrorText(err);
        do_logging(LOG_ERR, "FAIL: cannot connect, timeout %d -> %s check nspr4/prerr.h \n", PR_GetError(), err);
        /* Atomic increment fail */
        __atomic_add_fetch_8(&client_timeout_count, 1, __ATOMIC_ACQ_REL);
        if (sock != NULL) {
            PR_Close(sock);
        }
        goto done;
    }
    /* Now write data. */
    assert_true(PR_Write(sock, data, strlen(data) + 1) > 0);
    /* create the read job to respond to events on the socket. */
    assert_int_equal(ns_add_io_job(ns_job_get_tp(job), sock, NS_JOB_READ | NS_JOB_THREAD, client_response_cb, NULL, NULL), 0);

done:
    assert_int_equal(ns_job_done(job), 0);
}

static void
client_create_work(struct ns_job_t *job)
{
    struct test_params *tparams = ns_job_get_data(job);

    struct timespec ts;
    PR_Sleep(PR_SecondsToInterval(1));
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("BEGIN: %ld.%ld\n", ts.tv_sec, ts.tv_nsec);
    for (int32_t i = 0; i < tparams->jobs; i++) {
        assert_int_equal(ns_add_job(ns_job_get_tp(job), NS_JOB_NONE | NS_JOB_THREAD, client_initiate_connection_cb, NULL, NULL), 0);
    }
    assert_int_equal(ns_job_done(job), 0);

    printf("Create work thread complete!\n");
}

void
ns_stress_test(void **state)
{

    struct test_params *tparams = *state;

    /* Setup both thread pools. */

    /* Client first */

    int64_t job_count = tparams->jobs * tparams->client_thread_count;
    struct ns_thrpool_t *ctp;
    struct ns_thrpool_config client_ns_config;
    struct ns_job_t *sigterm_job = NULL;
    struct ns_job_t *sighup_job = NULL;
    struct ns_job_t *sigint_job = NULL;
    struct ns_job_t *sigtstp_job = NULL;
    struct ns_job_t *sigusr1_job = NULL;
    struct ns_job_t *sigusr2_job = NULL;
    struct ns_job_t *final_job = NULL;

    struct timeval timeout = {tparams->test_timeout, 0};

    setup_logging();

    ns_thrpool_config_init(&client_ns_config);
    client_ns_config.max_threads = tparams->client_thread_count;
    client_ns_config.log_fct = do_vlogging;
    ctp = ns_thrpool_new(&client_ns_config);

    /* Now the server */

    struct ns_thrpool_t *stp;
    struct ns_thrpool_config server_ns_config;
    struct ns_job_t *listen_job = NULL;

    ns_thrpool_config_init(&server_ns_config);
    server_ns_config.max_threads = tparams->server_thread_count;
    server_ns_config.log_fct = do_vlogging;
    stp = ns_thrpool_new(&server_ns_config);

    /* Now, add the signal handlers. */

    assert_int_equal(ns_add_signal_job(ctp, SIGTERM, NS_JOB_PERSIST, test_client_shutdown, stp, &sigterm_job), 0);
    assert_int_equal(ns_add_signal_job(ctp, SIGHUP, NS_JOB_PERSIST, test_client_shutdown, stp, &sighup_job), 0);
    assert_int_equal(ns_add_signal_job(ctp, SIGINT, NS_JOB_PERSIST, test_client_shutdown, stp, &sigint_job), 0);
    assert_int_equal(ns_add_signal_job(ctp, SIGTSTP, NS_JOB_PERSIST, test_client_shutdown, stp, &sigtstp_job), 0);
    assert_int_equal(ns_add_signal_job(ctp, SIGUSR1, NS_JOB_PERSIST, test_client_shutdown, stp, &sigusr1_job), 0);
    assert_int_equal(ns_add_signal_job(ctp, SIGUSR2, NS_JOB_PERSIST, test_client_shutdown, stp, &sigusr2_job), 0);

    /* Create the socket for the server, and set it to listen. */

    PRFileDesc *listenfd = NULL;
    PRNetAddr netaddr;
    PRSocketOptionData prsod = {PR_SockOpt_Nonblocking, {PR_TRUE}};

    PR_SetNetAddr(PR_IpAddrAny, PR_AF_INET, 12345, &netaddr);

    listenfd = PR_OpenTCPSocket(PR_NetAddrFamily(&netaddr));
    PR_SetSocketOption(listenfd, &prsod);
    prsod.option = PR_SockOpt_Reuseaddr;
    PR_SetSocketOption(listenfd, &prsod);
    assert_true(PR_Bind(listenfd, &netaddr) == PR_SUCCESS);
    assert_true(PR_Listen(listenfd, 32) == PR_SUCCESS);

    listen_job = server_listener_init(stp, listenfd);

    /* Add the timeout. */

    assert_int_equal(ns_add_timeout_job(ctp, &timeout, NS_JOB_NONE | NS_JOB_THREAD, test_client_shutdown, stp, &final_job), 0);

    /* While true, add connect / write jobs */
    for (PRInt32 i = 0; i < tparams->client_thread_count; i++) {
        assert_int_equal(ns_add_job(ctp, NS_JOB_NONE | NS_JOB_THREAD, client_create_work, tparams, NULL), 0);
    }

    /* Wait for all the clients to be done dispatching jobs to the server */

    if (ns_thrpool_wait(ctp) != 0) {
        printf("Error in ctp?\n");
    }

    if (ns_thrpool_wait(stp) != 0) {
        printf("Error in stp?\n");
    }

    /* Can mark as done becaus shutdown has begun. */
    assert_int_equal(ns_job_done(listen_job), 0);


    assert_int_equal(ns_job_done(sigterm_job), 0);
    assert_int_equal(ns_job_done(sighup_job), 0);
    assert_int_equal(ns_job_done(sigint_job), 0);
    assert_int_equal(ns_job_done(sigtstp_job), 0);
    assert_int_equal(ns_job_done(sigusr1_job), 0);
    assert_int_equal(ns_job_done(sigusr2_job), 0);
    assert_int_equal(ns_job_done(final_job), 0);

    ns_thrpool_destroy(stp);
    ns_thrpool_destroy(ctp);

    /* Destroy the server thread pool. */

    if (client_success_count != job_count) {
        do_logging(LOG_ERR, "FAIL, not all client jobs succeeded!\n");
    }
    if (server_success_count != job_count) {
        do_logging(LOG_ERR, "FAIL, not all server jobs succeeded!\n");
    }
    do_logging(LOG_ERR, "job_count: %d client_fail_count: %d client_timeout_count: %d server_fail_count: %d client_success_count: %d server_success_count: %d\n", job_count, client_fail_count, client_timeout_count, server_fail_count, client_success_count, server_success_count);

    assert_int_equal(client_fail_count, 0);
    /* We don't check the client timeout count, because it's often non 0, and it's generally because we *really* overwhelm the framework, we can handle the loss */
    /* assert_int_equal(client_timeout_count, 0); */
    assert_int_equal(server_fail_count, 0);
    /* Can't assert this due to timeout: instead guarantee if we connect to the server, we WORKED 100% of the time. */
    /*
    assert_int_equal(server_success_count, job_count);
    assert_int_equal(client_success_count, job_count);
    */
    assert_int_equal(server_success_count, client_success_count);
    int32_t job_threshold = (tparams->jobs * tparams->client_thread_count) * 0.95;
    assert_true(client_success_count >= job_threshold);

    PR_Cleanup();
}
