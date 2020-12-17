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

/*
 * slapi2nspr.c - expose a subset of the NSPR20/21 API to SLAPI plugin writers
 *
 * Also include slapi2pthread functions
 *
 */

#include "slap.h"
#include <nspr.h>

/*
 * Note that Slapi_Mutex and Slapi_CondVar are defined like this in
 * slapi-plugin.h:
 *
 *    typedef struct slapi_mutex    Slapi_Mutex;
 *    typedef struct slapi_condvar    Slapi_CondVar;
 *
 * but there is no definition for struct slapi_mutex or struct slapi_condvar.
 * This seems to work okay since we always use them in pointer form and cast
 * directly to their NSPR equivalents.  Clever, huh?
 */


/*
 * ---------------- SLAPI API Functions --------------------------------------
 */

/*
 * Function: slapi_new_mutex
 * Description: behaves just like PR_NewLock().
 * Returns: a pointer to the new mutex (NULL if a mutex can't be created).
 */
Slapi_Mutex *
slapi_new_mutex(void)
{
    pthread_mutex_t *new_mutex = (pthread_mutex_t *)slapi_ch_calloc(1, sizeof(pthread_mutex_t));
    pthread_mutex_init(new_mutex, NULL);
    return ((Slapi_Mutex *)new_mutex);
}

/*
 * Function: slapi_destroy_mutex
 * Description: behaves just like pthread_mutex_destroy().
 */
void
slapi_destroy_mutex(Slapi_Mutex *mutex)
{
    if (mutex != NULL) {
        pthread_mutex_destroy((pthread_mutex_t *)mutex);
        slapi_ch_free((void **)&mutex);
    }
}


/*
 * Function: slapi_lock_mutex
 * Description: behaves just like pthread_mutex_lock().
 */
inline void __attribute__((always_inline))
slapi_lock_mutex(Slapi_Mutex *mutex)
{
    if (mutex != NULL) {
        pthread_mutex_lock((pthread_mutex_t *)mutex);
    }
}


/*
 * Function: slapi_unlock_mutex
 * Description: behaves just like pthread_mutex_unlock().
 * Returns:
 *    non-zero if mutex was successfully unlocked.
 *    0 if mutex is NULL or is not locked by the calling thread.
 */
inline int __attribute__((always_inline))
slapi_unlock_mutex(Slapi_Mutex *mutex)
{
    PR_ASSERT(mutex != NULL);
    if (mutex == NULL || pthread_mutex_unlock((pthread_mutex_t *)mutex) != 0) {
        return (0);
    } else {
        return (1);
    }
}


/*
 * Function: slapi_new_condvar
 * Description: behaves just like PR_NewCondVar().
 * Returns: pointer to a new condition variable (NULL if one can't be created).
 */
Slapi_CondVar *
slapi_new_condvar(Slapi_Mutex *mutex __attribute__((unused)))
{
    pthread_cond_t *new_cv = (pthread_cond_t *)slapi_ch_calloc(1, sizeof(pthread_cond_t));
    pthread_condattr_t condAttr;

    pthread_condattr_init(&condAttr);
    pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC);
    pthread_cond_init(new_cv, &condAttr);
    /* Done with the cond attr, it's safe to destroy it */
    pthread_condattr_destroy(&condAttr);

    return (Slapi_CondVar *)new_cv;
}


/*
 * Function: slapi_destroy_condvar
 * Description: behaves just like PR_DestroyCondVar().
 */
void
slapi_destroy_condvar(Slapi_CondVar *cvar)
{
    if (cvar != NULL) {
        pthread_cond_destroy((pthread_cond_t *)cvar);
        slapi_ch_free((void **)&cvar);
    }
}


/*
 * Function: slapi_wait_condvar (DEPRECATED)
 * Description: behaves just like PR_WaitCondVar() except timeout is
 *    in seconds and microseconds instead of PRIntervalTime units.
 *    If timeout is NULL, this call blocks indefinitely.
 * Returns:
 *    non-zero is all goes well.
 *    0 if cvar is NULL, the caller has not locked the mutex associated
 *        with cvar, or the waiting thread was interrupted.
 */
int
slapi_wait_condvar(Slapi_CondVar *cvar, struct timeval *timeout)
{
    /* Deprecated in favor of slapi_wait_condvar_pt() which requires that the
     * mutex be passed in */
    PRIntervalTime prit;

    if (cvar == NULL) {
        return (0);
    }

    if (timeout == NULL) {
        prit = PR_INTERVAL_NO_TIMEOUT;
    } else {
        prit = PR_SecondsToInterval(timeout->tv_sec) + PR_MicrosecondsToInterval(timeout->tv_usec);
    }

    if (PR_WaitCondVar((PRCondVar *)cvar, prit) != PR_SUCCESS) {
        return (0);
    }

    return (1);

}

int
slapi_wait_condvar_pt(Slapi_CondVar *cvar, Slapi_Mutex *mutex, struct timeval *timeout)
{
    int32_t rc = 1;

    if (cvar == NULL) {
        return 0;
    }

    if (timeout == NULL) {
        rc = pthread_cond_wait((pthread_cond_t *)cvar, (pthread_mutex_t *)mutex);
    } else {
        struct timespec current_time = {0};
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        current_time.tv_sec += (timeout->tv_sec + PR_MicrosecondsToInterval(timeout->tv_usec));
        rc = pthread_cond_timedwait((pthread_cond_t *)cvar, (pthread_mutex_t *)mutex, &current_time);
    }

    if (rc != 0) {
        /* something went wrong */
        return 0;
    }

    return 1;  /* success */
}


/*
 * Function: slapi_notify_condvar
 * Description: if notify_all is zero, behaves just like PR_NotifyCondVar().
 *    if notify_all is non-zero, behaves just like PR_NotifyAllCondVar().
 * Returns:
 *    non-zero if all goes well.
 *    0 if cvar is NULL or the caller has not locked the mutex associated
 *        with cvar.
 */
int
slapi_notify_condvar(Slapi_CondVar *cvar, int notify_all)
{
    int32_t rc;

    if (cvar == NULL) {
        return 0;
    }

    if (notify_all) {
        rc = pthread_cond_broadcast((pthread_cond_t *)cvar);
    } else {
        rc = pthread_cond_signal((pthread_cond_t *)cvar);
    }

    return (rc == 0 ? 1 : 0);
}

Slapi_RWLock *
slapi_new_rwlock_prio(int32_t prio_writer)
{
#ifdef USE_POSIX_RWLOCKS
    pthread_rwlock_t *rwlock = NULL;
    pthread_rwlockattr_t attr;

    pthread_rwlockattr_init(&attr);
    if (prio_writer) {
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    } else {
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_READER_NP);
    }

    rwlock = (pthread_rwlock_t *)slapi_ch_malloc(sizeof(pthread_rwlock_t));
    if (rwlock) {
        pthread_rwlock_init(rwlock, &attr);
    }

    return ((Slapi_RWLock *)rwlock);
#else
    return ((Slapi_RWLock *)PR_NewRWLock(PR_RWLOCK_RANK_NONE, "slapi_rwlock"));
#endif
}

Slapi_RWLock *
slapi_new_rwlock(void)
{
#ifdef USE_POSIX_RWLOCKS
    pthread_rwlock_t *rwlock = NULL;

    rwlock = (pthread_rwlock_t *)slapi_ch_malloc(sizeof(pthread_rwlock_t));
    if (rwlock) {
        pthread_rwlock_init(rwlock, NULL);
    }

    return ((Slapi_RWLock *)rwlock);
#else
    return ((Slapi_RWLock *)PR_NewRWLock(PR_RWLOCK_RANK_NONE, "slapi_rwlock"));
#endif
}

void
slapi_destroy_rwlock(Slapi_RWLock *rwlock)
{
    if (rwlock != NULL) {
#ifdef USE_POSIX_RWLOCKS
        pthread_rwlock_destroy((pthread_rwlock_t *)rwlock);
        slapi_ch_free((void **)&rwlock);
#else
        PR_DestroyLock((PRRWLock *)rwlock);
#endif
    }
}

inline int __attribute__((always_inline))
slapi_rwlock_rdlock(Slapi_RWLock *rwlock)
{
    int ret = 0;

    if (rwlock != NULL) {
#ifdef USE_POSIX_RWLOCKS
        ret = pthread_rwlock_rdlock((pthread_rwlock_t *)rwlock);
#else
        PR_RWLock_Rlock((PRRWLock *)rwlock);
#endif
    }

    return ret;
}

inline int __attribute__((always_inline))
slapi_rwlock_wrlock(Slapi_RWLock *rwlock)
{
    int ret = 0;

    if (rwlock != NULL) {
#ifdef USE_POSIX_RWLOCKS
        ret = pthread_rwlock_wrlock((pthread_rwlock_t *)rwlock);
#else
        PR_RWLock_Wlock((PRRWLock *)rwlock);
#endif
    }

    return ret;
}

inline int __attribute__((always_inline))
slapi_rwlock_unlock(Slapi_RWLock *rwlock)
{
    int ret = 0;

    if (rwlock != NULL) {
#ifdef USE_POSIX_RWLOCKS
        ret = pthread_rwlock_unlock((pthread_rwlock_t *)rwlock);
#else
        PR_RWLock_Unlock((PRRWLock *)rwlock);
#endif
    }

    return ret;
}

int
slapi_rwlock_get_size(void)
{
#ifdef USE_POSIX_RWLOCKS
    return sizeof(pthread_rwlock_t);
#else
    /*
     * NSPR does not provide the size of PRRWLock.
     * This is a rough estimate to maintain the entry size sane.
     */
    return sizeof("slapi_rwlock") + sizeof(void *) * 6;
#endif
}
