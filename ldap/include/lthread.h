/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* lthread.h - ldap threads header file */

#ifndef _LTHREAD_H
#define _LTHREAD_H

#if defined( THREAD_SUNOS4_LWP )
/***********************************
 *                                 *
 * thread definitions for sunos4   *
 *                                 *
 ***********************************/

#define _THREAD

#include <lwp/lwp.h>
#include <lwp/stackdep.h>

typedef void	*(*VFP)();

/* thread attributes and thread type */
typedef int		pthread_attr_t;
typedef thread_t	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE	0
#define PTHREAD_CREATE_DETACHED	1
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS	0
#define PTHREAD_SCOPE_SYSTEM	1

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef mon_t	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE	0
#define PTHREAD_SHARE_PROCESS	1

/* condition variable attributes and condition variable type */
typedef int	pthread_condattr_t;
typedef struct lwpcv {
	int		lcv_created;
	cv_t		lcv_cv;
} pthread_cond_t;

#else /* end sunos4 */

#if defined( THREAD_SUNOS5_LWP )
/***********************************
 *                                 *
 * thread definitions for sunos5   *
 *                                 *
 ***********************************/

#define _THREAD

#include <thread.h>
#include <synch.h>

typedef void	*(*VFP)();

/* sunos5 threads are preemptive */
#define PTHREAD_PREEMPTIVE	1

/* thread attributes and thread type */
typedef int		pthread_attr_t;
typedef thread_t	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED THR_DETACHED
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS   0
#define PTHREAD_SCOPE_SYSTEM    THR_BOUND

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef mutex_t	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE   USYNC_THREAD
#define PTHREAD_SHARE_PROCESS   USYNC_PROCESS

/* condition variable attributes and condition variable type */
typedef int     pthread_condattr_t;
typedef cond_t	pthread_cond_t;

#else /* end sunos5 */

#if defined( THREAD_MIT_PTHREADS )
/***********************************
 *                                 *
 * definitions for mit pthreads    *
 *                                 *
 ***********************************/

#define _THREAD

#include <pthread.h>

#else /* end mit pthreads */

#if defined( THREAD_AIX_PTHREADS )
/***********************************
 *                                 *
 * definitions for aix pthreads    *
 *                                 *
 ***********************************/

#define _THREAD

#include <pthread.h>

typedef void	*(*VFP)(void *);

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE 0

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

#else /* aix pthreads */

#if defined( THREAD_HP_DCE_PTHREADS )
/**************************************
 *                                    *
 * definitions for HP dce pthreads    *
 *                                    *
 **************************************/

#define _THREAD
typedef void	*(*VFP)();

#include <pthread.h>

/* dce threads are preemptive */
#define PTHREAD_PREEMPTIVE	1

/* pthread_kill() is a noop on HP */
#define PTHREAD_KILL_IS_NOOP	1

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE	0
#define PTHREAD_CREATE_DETACHED	1

#define pthread_attr_init( a )		pthread_attr_create( a )
#define pthread_attr_destroy( a )	pthread_attr_delete( a )
#define pthread_attr_setdetachstate( a, b ) \
					pthread_attr_setdetach_np( a, b )
/*
 * HP's DCE threads implementation passes a (pthread_attr_t *)
 * for the second argument.  So, we need to fake things a bit.
 * hpdce_pthread_create_detached() is in thread.c.  Note that we
 * create threads and detach them.  If you need to create a joinable
 * thread, you need to call hpdce_pthread_create_joinable() directly.
 */
#define	pthread_create( a, b, c, d ) \
				hpdce_pthread_create_detached( a, b, c, d )

int
hpdce_pthread_create_joinable( pthread_t *tid, pthread_attr_t *attr,
	VFP func, void *arg );
int hpdce_pthread_create_detached( pthread_t *tid, pthread_attr_t *attr,
	VFP func, void *arg );
#else /* HP dce pthreads */

#if defined( THREAD_DCE_PTHREADS )
/***********************************
 *                                 *
 * definitions for dce pthreads    *
 *                                 *
 ***********************************/

#define _THREAD
typedef void	*(*VFP)();

#include <pthread.h>

/* dce threads are preemptive */
#define PTHREAD_PREEMPTIVE	1

/* thread state - joinable or not */
#ifndef PTHREAD_CREATE_JOINABLE
#define PTHREAD_CREATE_JOINABLE	0
#endif
#ifndef PTHREAD_CREATE_DETACHED
#define PTHREAD_CREATE_DETACHED	1
#endif

#define pthread_attr_init( a )		pthread_attr_create( a )
#define pthread_attr_destroy( a )	pthread_attr_delete( a )
#define pthread_attr_setdetachstate( a, b ) \
					pthread_attr_setdetach_np( a, b )
#if defined( OSF1 )
/* pthread_create's second parameter is passed by value, not by reference.
 * To work around this, call another function instead:
 */
#define	pthread_create( a, b, c, d )	std_pthread_create( a, b, c, d )
extern int
std_pthread_create (pthread_t               *tid,
		    pthread_attr_t          *attr,
		    pthread_startroutine_t  func,
		    pthread_addr_t          arg); /* defined in thread.c */

/* OSF1 doesn't support pthread_kill() */
#define PTHREAD_KILL_IS_NOOP	1

#endif /* OSF1 */

#else /* dce pthreads */

#if defined( THREAD_SGI_SPROC )
/***********************************
 *                                 *
 * thread definitions for sgi irix *
 *                                 *
 ***********************************/

#define _THREAD

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/procset.h>
#include <sys/prctl.h>
#include <ulocks.h>

typedef void	*(*VFP)(void *);

/* sgi threads are preemptive */
#define PTHREAD_PREEMPTIVE	1

/* thread attributes and thread type */
typedef int	pthread_attr_t;
typedef pid_t	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	0
#define pthread_condattr_default	0

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE	0
#define PTHREAD_CREATE_DETACHED	1
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS	0
#define PTHREAD_SCOPE_SYSTEM	1

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef int	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE	0
#define PTHREAD_SHARE_PROCESS	1

/* condition variable attributes and condition variable type */
typedef int	pthread_condattr_t;
struct irix_cv_waiter {
	pid_t			icvw_pid;
	struct irix_cv_waiter	*icvw_next;
};
typedef struct irix_cv {
	pthread_mutex_t		icv_mutex;
	pthread_mutex_t		*icv_waitermutex;
	struct irix_cv_waiter	*icv_waiterq;
} pthread_cond_t;

#else

#if defined( WIN32_KERNEL_THREADS )

/***********************************
 *                                 *
 * thread definitions for Win32    *
 *                                 *
 ***********************************/

#define _THREAD

#include <windows.h>
#include <process.h>
#include "ldap.h"
#include "ldaplog.h"

typedef void	(*VFP)(void *);

/* Win32 threads are preemptive */
#define PTHREAD_PREEMPTIVE	1

/* thread attributes and thread type */
typedef int	pthread_attr_t;
typedef HANDLE	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	0
#define pthread_condattr_default	0

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE	0
#define PTHREAD_CREATE_DETACHED	1
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS	0
#define PTHREAD_SCOPE_SYSTEM	1

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef HANDLE	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE	0
#define PTHREAD_SHARE_PROCESS	1

/* condition variable attributes and condition variable type */
typedef int	pthread_condattr_t;

/* simulated condition variable */
struct win32_cv_waiter {
	pthread_t			icvw_pthread;
	struct win32_cv_waiter	*icvw_next;
};
typedef struct win32_cv {
	pthread_mutex_t		icv_mutex;
	pthread_mutex_t		*icv_waitermutex;
	struct win32_cv_waiter	*icv_waiterq;
} pthread_cond_t;

#endif /* NATIVE_WIN32_THREADS */
#endif /* sgi sproc */
#endif /* dce pthreads */
#endif /* hp dce pthreads */
#endif /* aix pthreads */
#endif /* mit pthreads */
#endif /* sunos5 */
#endif /* sunos4 */

#ifndef _THREAD

/***********************************
 *                                 *
 * thread definitions for no       *
 * underlying library support      *
 *                                 *
 ***********************************/

typedef void	*(*VFP)();

/* thread attributes and thread type */
typedef int	pthread_attr_t;
typedef int	pthread_t;

/* default attr states */
#define pthread_mutexattr_default	NULL
#define pthread_condattr_default	NULL

/* thread state - joinable or not */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 0
/* thread scope - who is in scheduling pool */
#define PTHREAD_SCOPE_PROCESS   0
#define PTHREAD_SCOPE_SYSTEM    0

/* mutex attributes and mutex type */
typedef int	pthread_mutexattr_t;
typedef int	pthread_mutex_t;

/* mutex and condition variable scope - process or system */
#define PTHREAD_SHARE_PRIVATE   0
#define PTHREAD_SHARE_PROCESS   0

/* condition variable attributes and condition variable type */
typedef int     pthread_condattr_t;
typedef int	pthread_cond_t;

#endif /* no threads support */

/* POSIX standard pthread function declarations: */

int pthread_attr_init( pthread_attr_t *attr );
int pthread_attr_destroy( pthread_attr_t *attr );
int pthread_attr_getdetachstate( pthread_attr_t *attr, int *detachstate );
int pthread_attr_setdetachstate( pthread_attr_t *attr, int detachstate );

int pthread_create( pthread_t *tid, pthread_attr_t *attr, VFP func, void *arg );
void pthread_yield();
void pthread_exit();
int pthread_kill( pthread_t tid, int sig );
#if defined( hpux ) || defined( OSF1 ) || defined( AIXV4 ) /* <thread.h> declares pthread_join */
#else
int pthread_join( pthread_t tid, int *status );
#endif

#if defined( hpux ) || defined( OSF1 ) /* <thread.h> declares pthread_mutex_init */
#else
int pthread_mutex_init( pthread_mutex_t *mp, pthread_mutexattr_t *attr );
#endif
int pthread_mutex_destroy( pthread_mutex_t *mp );
int pthread_mutex_lock( pthread_mutex_t *mp );
int pthread_mutex_unlock( pthread_mutex_t *mp );
int pthread_mutex_trylock( pthread_mutex_t *mp );

#if defined( hpux ) || defined( OSF1 ) /* <thread.h> declares pthread_cond_init */
#else
int pthread_cond_init( pthread_cond_t *cv, pthread_condattr_t *attr );
#endif
int pthread_cond_destroy( pthread_cond_t *cv );
int pthread_cond_wait( pthread_cond_t *cv, pthread_mutex_t *mp );
int pthread_cond_signal( pthread_cond_t *cv );
int pthread_cond_broadcast( pthread_cond_t *cv );

#endif /* _LTHREAD_H */
