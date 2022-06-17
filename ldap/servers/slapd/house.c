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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "slap.h"

#define SLAPD_HOUSEKEEPING_INTERVAL 30 /* seconds */

static PRThread *housekeeping_tid = NULL;
static pthread_mutex_t housekeeping_mutex;
static pthread_cond_t housekeeping_cvar;


static void
housecleaning(void *cur_time __attribute__((unused)))
{
    while (!g_get_shutdown()) {
        struct timespec current_time = {0};
        /*
         * Looks simple, but could potentially take a long time.
         */
        logs_flush();

        if (g_get_shutdown()) {
            break;
        }

        /* get the current monotonic time and add our interval */
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        current_time.tv_sec += SLAPD_HOUSEKEEPING_INTERVAL;

        /* Now we wait... */
        pthread_mutex_lock(&housekeeping_mutex);
        pthread_cond_timedwait(&housekeeping_cvar, &housekeeping_mutex, &current_time);
        pthread_mutex_unlock(&housekeeping_mutex);
    }
}

PRThread *
housekeeping_start(time_t cur_time, void *arg __attribute__((unused)))
{
    static time_t thread_start_time;
    pthread_condattr_t condAttr;
    int rc = 0;

    if (housekeeping_tid) {
        return housekeeping_tid;
    }

    if ((rc = pthread_mutex_init(&housekeeping_mutex, NULL)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "housekeeping_start",
                      "housekeeping cannot create new lock.  error %d (%s)\n",
                      rc, strerror(rc));
    } else if ((rc = pthread_condattr_init(&condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "housekeeping_start",
                      "housekeeping cannot create new condition attribute variable.  error %d (%s)\n",
                      rc, strerror(rc));
    } else if ((rc = pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "housekeeping_start",
                      "housekeeping cannot set condition attr clock.  error %d (%s)\n",
                      rc, strerror(rc));
    } else if ((rc = pthread_cond_init(&housekeeping_cvar, &condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "housekeeping_start",
                      "housekeeping cannot create new condition variable.  error %d (%s)\n",
                      rc, strerror(rc));
    } else {
        pthread_condattr_destroy(&condAttr); /* no longer needed */
        thread_start_time = cur_time;
        if ((housekeeping_tid = PR_CreateThread(PR_USER_THREAD,
                                                (VFP)housecleaning, (void *)&thread_start_time,
                                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD,
                                                SLAPD_DEFAULT_THREAD_STACKSIZE)) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "housekeeping_start",
                          "housekeeping PR_CreateThread failed. " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          PR_GetError(), slapd_pr_strerror(PR_GetError()));
        }
    }

    return housekeeping_tid;
}

void
housekeeping_stop()
{
    if (housekeeping_tid) {
        /* Notify the thread */
        pthread_mutex_lock(&housekeeping_mutex);
        pthread_cond_signal(&housekeeping_cvar);
        pthread_mutex_unlock(&housekeeping_mutex);

        /* Wait for the thread to finish */
        (void)PR_JoinThread(housekeeping_tid);

        /* Clean it all up */
        pthread_mutex_destroy(&housekeeping_mutex);
        pthread_cond_destroy(&housekeeping_cvar);
    }
}
