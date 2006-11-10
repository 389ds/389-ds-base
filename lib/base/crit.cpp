/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * crit.c: Critical section abstraction. Used in threaded servers to protect
 *         areas where two threads can interfere with each other.
 *
 *         Condvars are condition variables that are used for thread-thread 
 *         synchronization.
 * 
 * Rob McCool
 */

#include "systems.h"

#include "netsite.h"
#include "crit.h"
#include "pool.h"

#include "base/dbtbase.h"

#ifdef USE_NSPR
/*
#include "prmon.h"
#include "private/primpl.h"
*/
#include "nspr.h"

#include "prthread.h"
#include "prlock.h"
#include "prcvar.h"
#include "prinrval.h"

/*
 * Defined to replace PR_Monitor() with PR_Lock().
 */
typedef struct critical {
	PRLock		*lock;
	PRUint32	count;
	PRThread	*owner;
} critical_t;

typedef struct condvar {
	critical_t	*lock;
	PRCondVar	*cvar;
} condvar_t;

#endif
/* -------------------------- critical sections --------------------------- */

/* Useful for ASSERTs only. Returns non-zero if the current thread is the owner.
 */
NSAPI_PUBLIC int crit_owner_is_me(CRITICAL id)
{
#ifdef USE_NSPR
    critical_t *crit = (critical_t*)id;

    return (crit->owner == PR_GetCurrentThread());
#else
    return 1;
#endif
}

NSAPI_PUBLIC CRITICAL crit_init(void)
{
#ifdef USE_NSPR
    critical_t *crit = (critical_t*)PERM_MALLOC(sizeof(critical_t)) ;

    if (crit) {
        if (!(crit->lock = PR_NewLock())) {
            PERM_FREE(crit);
            return NULL;
        }
        crit->count = 0;
        crit->owner = 0;
    }
    return (void *) crit;
#else
    return NULL;
#endif
}

NSAPI_PUBLIC void crit_enter(CRITICAL id)
{
#ifdef USE_NSPR
    critical_t *crit = (critical_t*)id;
    PRThread *me = PR_GetCurrentThread();

    if ( crit->owner == me) {
        PR_ASSERT(crit->count > 0);
        crit->count++;
    } 
    else {
        PR_Lock(crit->lock);
        PR_ASSERT(crit->count == 0);
        crit->count = 1;
        crit->owner = me;
    }
#endif
}

NSAPI_PUBLIC void crit_exit(CRITICAL id)
{
#ifdef USE_NSPR
    critical_t	*crit = (critical_t*)id;

    if (crit->owner != PR_GetCurrentThread()) 
        return;

    if ( --crit->count == 0) {
        crit->owner = 0;
        PR_Unlock(crit->lock);
    }
    PR_ASSERT(crit->count >= 0);
#endif
}

NSAPI_PUBLIC void crit_terminate(CRITICAL id)
{
#ifdef USE_NSPR
    critical_t	*crit = (critical_t*)id;

    PR_DestroyLock((PRLock*)crit->lock);
    PERM_FREE(crit);
#endif
}


/* ------------------------- condition variables -------------------------- */


NSAPI_PUBLIC CONDVAR condvar_init(CRITICAL id)
{
#ifdef USE_NSPR
    critical_t	*crit = (critical_t*)id;

    condvar_t *cvar = (condvar_t*)PERM_MALLOC(sizeof(condvar_t)) ;

    if (crit) {
        cvar->lock = crit;
        if ((cvar->cvar = PR_NewCondVar((PRLock *)cvar->lock->lock)) == 0) {
            PERM_FREE(cvar);
            return NULL;
        }
    }
    return (void *) cvar;
#endif
}

NSAPI_PUBLIC void condvar_wait(CONDVAR _cv)
{
#ifdef USE_NSPR
    condvar_t *cv = (condvar_t *)_cv;
    /* Save away recursion count so we can restore it after the wait */
    int saveCount = cv->lock->count;
    PRThread *saveOwner = cv->lock->owner;

    PR_ASSERT(cv->lock->owner == PR_GetCurrentThread());
    cv->lock->count = 0;
    cv->lock->owner = 0;

    PR_WaitCondVar(cv->cvar, PR_INTERVAL_NO_TIMEOUT);

    cv->lock->count = saveCount;
    cv->lock->owner = saveOwner;
#endif
}


NSAPI_PUBLIC void condvar_timed_wait(CONDVAR _cv, long secs)
{
#ifdef USE_NSPR
    condvar_t *cv = (condvar_t *)_cv;
    /* Save away recursion count so we can restore it after the wait */
    int saveCount = cv->lock->count;
    PRThread *saveOwner = cv->lock->owner;
 
    PR_ASSERT(cv->lock->owner == PR_GetCurrentThread());
    cv->lock->count = 0;
    cv->lock->owner = 0;
    
    PRIntervalTime timeout = PR_INTERVAL_NO_TIMEOUT;
    if (secs > 0) 
	timeout = PR_SecondsToInterval(secs); 
    PR_WaitCondVar(cv->cvar, timeout);
 
    cv->lock->count = saveCount;
    cv->lock->owner = saveOwner;
#endif
}



NSAPI_PUBLIC void condvar_notify(CONDVAR _cv)
{
#ifdef USE_NSPR
    condvar_t *cv = (condvar_t *)_cv;
    PR_ASSERT(cv->lock->owner == PR_GetCurrentThread());
    PR_NotifyCondVar(cv->cvar);
#endif
}

NSAPI_PUBLIC void condvar_notifyAll(CONDVAR _cv)
{
#ifdef USE_NSPR
    condvar_t *cv = (condvar_t *)_cv;
    PR_ASSERT(cv->lock->owner == PR_GetCurrentThread());
    PR_NotifyAllCondVar(cv->cvar);
#endif
}

NSAPI_PUBLIC void condvar_terminate(CONDVAR _cv)
{
#ifdef USE_NSPR
    condvar_t *cv = (condvar_t *)_cv;
    PR_DestroyCondVar(cv->cvar);
    PERM_FREE(cv);
#endif
}

/* ----------------------- Counting semaphores ---------------------------- */
/* These are currently not listed in crit.h because they aren't yet used; 
 * although they do work.
 */

/*
 * XXXMB - these should be in NSPR.  
 *
 */

#if defined(SOLARIS) && defined(HW_THREADS)
#include <synch.h>
typedef sema_t counting_sem_t;
#elif defined(IRIX) && defined(HW_THREADS)
#include <ulocks.h>
typedef usema_t *counting_sem_t;
#else
typedef struct counting_sem_t {
	CRITICAL lock;
	CRITICAL cv_lock;
	CONDVAR cv;
	int count;
	int max;
} counting_sem_t;
#endif

NSAPI_PUBLIC COUNTING_SEMAPHORE
cs_init(int initial_count)
{
	counting_sem_t *cs = (counting_sem_t *)PERM_MALLOC(sizeof(counting_sem_t));
#if defined(SOLARIS) && defined(HW_THREADS)
    if ( sema_init(cs, initial_count, USYNC_THREAD, NULL) < 0) {
		ereport(LOG_FAILURE, XP_GetAdminStr(DBT_csInitFailureS_), system_errmsg());
		PERM_FREE(cs);
		return NULL;
	}

	return (COUNTING_SEMAPHORE)cs;
#elif defined(IRIX) && defined(HW_THREADS)
	usptr_t *arena;

	usconfig(CONF_INITSIZE, 64*1024);
	if ( (arena = usinit("/tmp/cs.locks")) == NULL) 
		return NULL;

	if ( (cs = (counting_sem_t *)usnewsema(arena, 0)) == NULL)
		return NULL;

	return cs;
	
#else

	cs->count = initial_count;
	cs->lock = crit_init();
	cs->cv_lock = crit_init();
	cs->cv = condvar_init(cs->cv_lock);

	return (COUNTING_SEMAPHORE)cs;
#endif
}

NSAPI_PUBLIC void
cs_terminate(COUNTING_SEMAPHORE csp)
{
	counting_sem_t *cs = (counting_sem_t *)csp;

#if defined(SOLARIS) && defined(HW_THREADS)
    if ( sema_destroy(cs) < 0 ) {
		ereport(LOG_FAILURE, XP_GetAdminStr(DBT_csTerminateFailureS_), system_errmsg());
	}
    PERM_FREE(cs);
	return;
#elif defined(IRIX) && defined(HW_THREADS)
	/* usfreesema() */
	return;
#else
	condvar_terminate(cs->cv);
	crit_terminate(cs->cv_lock);
	crit_terminate(cs->lock);
	PERM_FREE(cs);

	return;
#endif
}

NSAPI_PUBLIC int
cs_wait(COUNTING_SEMAPHORE csp)
{
	counting_sem_t *cs = (counting_sem_t *)csp;
	int ret;

#if defined(SOLARIS) && defined(HW_THREADS)
    if ( (ret = sema_wait(cs)) < 0 ) {
		ereport(LOG_FAILURE, XP_GetAdminStr(DBT_csWaitFailureS_), system_errmsg());
		return -1;
	}
	return ret;
#elif defined(IRIX) && defined(HW_THREADS)
	uspsema(cs);
	return 0;
#else
	crit_enter(cs->lock);
	while ( cs->count == 0 ) {
		crit_enter(cs->cv_lock);
		crit_exit(cs->lock);
		condvar_wait(cs->cv);
		crit_exit(cs->cv_lock);
		crit_enter(cs->lock);
	}
	ret = --(cs->count);
	crit_exit(cs->lock);

	return 0;
#endif
}

NSAPI_PUBLIC int
cs_trywait(COUNTING_SEMAPHORE csp)
{
	counting_sem_t *cs = (counting_sem_t *)csp;
	int ret;

#if defined(SOLARIS) && defined(HW_THREADS)
	ret = sema_trywait(cs)?-1:0;
    return ret;
#elif defined(IRIX) && defined(HW_THREADS)
	ret = uscpsema(cs);
	return (ret == 1)?0:-1;
#else
	crit_enter(cs->lock);
	if (cs->count > 0) {
		ret = --(cs->count);
		crit_exit(cs->lock);
        return 0;
    }
    crit_exit(cs->lock);
	return -1;

#endif
}

NSAPI_PUBLIC int 
cs_release(COUNTING_SEMAPHORE csp)
{
	counting_sem_t *cs = (counting_sem_t *)csp;
	int ret;

#if defined(SOLARIS) && defined(HW_THREADS)
    if ( (ret = sema_post(cs)) < 0 ) {
		ereport(LOG_FAILURE, XP_GetAdminStr(DBT_csPostFailureS_), system_errmsg());
		return -1;
	}
	return ret;
#elif defined(IRIX) && defined(HW_THREADS)
	usvsema(cs);
	return 0;
#else
	crit_enter(cs->lock);
	ret = ++(cs->count);
	if (cs->count == 1) {
		crit_enter(cs->cv_lock);
		condvar_notify(cs->cv);
		crit_exit(cs->cv_lock);
	}
	crit_exit(cs->lock);

	return 0;
#endif
}
