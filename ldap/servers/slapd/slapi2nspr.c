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
 * slapi2nspr.c - expose a subset of the NSPR20/21 API to SLAPI plugin writers
 *
 */

#include "slap.h"
#include <nspr.h>

/*
 * Note that Slapi_Mutex and Slapi_CondVar are defined like this in
 * slapi-plugin.h:
 *
 *	typedef struct slapi_mutex	Slapi_Mutex;
 *	typedef struct slapi_condvar	Slapi_CondVar;
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
slapi_new_mutex( void )
{
    return( (Slapi_Mutex *)PR_NewLock());
}


/*
 * Function: slapi_destroy_mutex
 * Description: behaves just like PR_DestroyLock().
 */
void
slapi_destroy_mutex( Slapi_Mutex *mutex )
{
    if ( mutex != NULL ) {
	PR_DestroyLock( (PRLock *)mutex );
    }
}


/*
 * Function: slapi_lock_mutex
 * Description: behaves just like PR_Lock().
 */
void
slapi_lock_mutex( Slapi_Mutex *mutex )
{
    if ( mutex != NULL ) {
	PR_Lock( (PRLock *)mutex );
    }
}


/*
 * Function: slapi_unlock_mutex
 * Description: behaves just like PR_Unlock().
 * Returns:
 *	non-zero if mutex was successfully unlocked.
 *	0 if mutex is NULL or is not locked by the calling thread.
 */
int
slapi_unlock_mutex( Slapi_Mutex *mutex )
{
    if ( mutex == NULL || PR_Unlock( (PRLock *)mutex ) == PR_FAILURE ) {
	return( 0 );
    } else {
	return( 1 );
    }
}


/*
 * Function: slapi_new_condvar
 * Description: behaves just like PR_NewCondVar().
 * Returns: pointer to a new condition variable (NULL if one can't be created).
 */
Slapi_CondVar *
slapi_new_condvar( Slapi_Mutex *mutex )
{
    if ( mutex == NULL ) {
	return( NULL );
    }

    return( (Slapi_CondVar *)PR_NewCondVar( (PRLock *)mutex ));
}


/*
 * Function: slapi_destroy_condvar
 * Description: behaves just like PR_DestroyCondVar().
 */
void
slapi_destroy_condvar( Slapi_CondVar *cvar )
{
    if ( cvar != NULL ) {
	PR_DestroyCondVar( (PRCondVar *)cvar );
    }
}


/*
 * Function: slapi_wait_condvar
 * Description: behaves just like PR_WaitCondVar() except timeout is
 *	in seconds and microseconds instead of PRIntervalTime units.
 *	If timeout is NULL, this call blocks indefinitely.
 * Returns:
 *	non-zero is all goes well.
 *	0 if cvar is NULL, the caller has not locked the mutex associated
 *		with cvar, or the waiting thread was interrupted.
 */
int
slapi_wait_condvar( Slapi_CondVar *cvar, struct timeval *timeout )
{
    PRIntervalTime	prit;

    if ( cvar == NULL ) {
	return( 0 );
    }

    if ( timeout == NULL ) {
	prit = PR_INTERVAL_NO_TIMEOUT;
    } else {
	prit = PR_SecondsToInterval( timeout->tv_sec )
		+ PR_MicrosecondsToInterval( timeout->tv_usec ); 
    }

    if ( PR_WaitCondVar( (PRCondVar *)cvar, prit ) != PR_SUCCESS ) {
	return( 0 );
    }

    return( 1 );
}


/*
 * Function: slapi_notify_condvar
 * Description: if notify_all is zero, behaves just like PR_NotifyCondVar().
 *	if notify_all is non-zero, behaves just like PR_NotifyAllCondVar().
 * Returns:
 *	non-zero if all goes well.
 *	0 if cvar is NULL or the caller has not locked the mutex associated
 *		with cvar.
 */
int
slapi_notify_condvar( Slapi_CondVar *cvar, int notify_all )
{
    PRStatus	prrc;

    if ( cvar == NULL ) {
	return( 0 );
    }

    if ( notify_all ) {
	prrc = PR_NotifyAllCondVar( (PRCondVar *)cvar );
    } else {
	prrc = PR_NotifyCondVar( (PRCondVar *)cvar );
    }

    return( prrc == PR_SUCCESS ? 1 : 0 );
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

    return((Slapi_RWLock *)rwlock);
#else
    return((Slapi_RWLock *)PR_NewRWLock(PR_RWLOCK_RANK_NONE, "slapi_rwlock"));
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
slapi_rwlock_rdlock( Slapi_RWLock *rwlock )
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
slapi_rwlock_wrlock( Slapi_RWLock *rwlock )
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
slapi_rwlock_unlock( Slapi_RWLock *rwlock )
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

