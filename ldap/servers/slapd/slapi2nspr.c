/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
    return ((Slapi_Mutex *)PR_NewLock());
}


/*
 * Function: slapi_destroy_mutex
 * Description: behaves just like PR_DestroyLock().
 */
void
slapi_destroy_mutex(Slapi_Mutex *mutex)
{
    if (mutex != NULL) {
        PR_DestroyLock((PRLock *)mutex);
    }
}


/*
 * Function: slapi_lock_mutex
 * Description: behaves just like PR_Lock().
 */
void
slapi_lock_mutex(Slapi_Mutex *mutex)
{
    if (mutex != NULL) {
        PR_Lock((PRLock *)mutex);
    }
}


/*
 * Function: slapi_unlock_mutex
 * Description: behaves just like PR_Unlock().
 * Returns:
 *    non-zero if mutex was successfully unlocked.
 *    0 if mutex is NULL or is not locked by the calling thread.
 */
int
slapi_unlock_mutex(Slapi_Mutex *mutex)
{
    if (mutex == NULL || PR_Unlock((PRLock *)mutex) == PR_FAILURE) {
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
slapi_new_condvar(Slapi_Mutex *mutex)
{
    if (mutex == NULL) {
        return (NULL);
    }

    return ((Slapi_CondVar *)PR_NewCondVar((PRLock *)mutex));
}


/*
 * Function: slapi_destroy_condvar
 * Description: behaves just like PR_DestroyCondVar().
 */
void
slapi_destroy_condvar(Slapi_CondVar *cvar)
{
    if (cvar != NULL) {
        PR_DestroyCondVar((PRCondVar *)cvar);
    }
}


/*
 * Function: slapi_wait_condvar
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
    PRStatus prrc;

    if (cvar == NULL) {
        return (0);
    }

    if (notify_all) {
        prrc = PR_NotifyAllCondVar((PRCondVar *)cvar);
    } else {
        prrc = PR_NotifyCondVar((PRCondVar *)cvar);
    }

    return (prrc == PR_SUCCESS ? 1 : 0);
}

Slapi_RWLock *
slapi_new_rwlock_prio(int32_t prio_writer)
{
#ifdef USE_POSIX_RWLOCKS
    pthread_rwlock_t *rwlock = NULL;
    pthread_rwlockattr_t attr;

    pthread_rwlockattr_init(&attr);

#if defined(__GLIBC__)
    if (prio_writer) {
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    } else {
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_READER_NP);
    }
#endif

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

int
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

int
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

int
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
