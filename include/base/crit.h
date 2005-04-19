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
#ifndef BASE_CRIT_H
#define BASE_CRIT_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * crit.h: Critical section abstraction. Used in threaded servers to protect
 *         areas where two threads can interfere with each other.
 *
 *         Condvars are condition variables that are used for thread-thread 
 *         synchronization.
 * 
 * Rob McCool
 */

#ifndef NETSITE_H
#include "netsite.h"
#endif /* !NETSITE_H */

/* Define C++ interface */
#ifdef __cplusplus

#include "prlog.h" /* NSPR */

#ifndef prmon_h___
#include "prmon.h"
#endif /* !prmon_h___ */

class NSAPI_PUBLIC CriticalSection
{
public:
    CriticalSection();
    ~CriticalSection();
    void Acquire(){PR_EnterMonitor(_crtsec);}
    void Release(){PR_ExitMonitor(_crtsec);}

private:
    PRMonitor *_crtsec;
};

inline CriticalSection::CriticalSection():_crtsec(0)
{
    _crtsec = PR_NewMonitor();
    PR_ASSERT(_crtsec);
}

inline CriticalSection::~CriticalSection()
{
    if (_crtsec)
        PR_DestroyMonitor(_crtsec);
}

class SafeLock {
 public:
    SafeLock (CriticalSection&);		// acquire lock
    ~SafeLock (); 						// release lock
 private:
    CriticalSection& lock; 
};

inline SafeLock::SafeLock (CriticalSection& _lock) : lock(_lock)
{
    lock.Acquire();
}

inline SafeLock::~SafeLock ()
{
    lock.Release();
}
#endif /* __cplusplus */

/* --- Begin function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

/* ASSERT function only */
NSAPI_PUBLIC int crit_owner_is_me(CRITICAL id);

/*
 * INTcrit_init creates and returns a new critical section variable. At the 
 * time of creation no one has entered it.
 */
NSAPI_PUBLIC CRITICAL INTcrit_init(void);

/*
 * INTcrit_enter enters a critical section. If someone is already in the
 * section, the calling thread is blocked until that thread exits.
 */
NSAPI_PUBLIC void INTcrit_enter(CRITICAL id);


/*
 * INTcrit_exit exits a critical section. If another thread is blocked waiting
 * to enter, it will be unblocked and given ownership of the section.
 */
NSAPI_PUBLIC void INTcrit_exit(CRITICAL id);


/*
 * INTcrit_terminate removes a previously allocated critical section variable.
 */
NSAPI_PUBLIC void INTcrit_terminate(CRITICAL id);


/*
 * INTcondvar_init initializes and returns a new condition variable. You 
 * must provide a critical section to be associated with this condition 
 * variable.
 */
NSAPI_PUBLIC CONDVAR INTcondvar_init(CRITICAL id);


/*
 * INTcondvar_wait blocks on the given condition variable. The calling thread
 * will be blocked until another thread calls INTcondvar_notify on this variable.
 * The caller must have entered the critical section associated with this
 * condition variable prior to waiting for it.
 */
NSAPI_PUBLIC void INTcondvar_wait(CONDVAR cv);
NSAPI_PUBLIC void condvar_timed_wait(CONDVAR _cv, long secs);


/*
 * INTcondvar_notify awakens any threads blocked on the given condition
 * variable. The caller must have entered the critical section associated
 * with this variable first.
 */
NSAPI_PUBLIC void INTcondvar_notify(CONDVAR cv);

/*
 * INTcondvar_notifyAll awakens all threads blocked on the given condition
 * variable. The caller must have entered the critical section associated
 * with this variable first.
 */
NSAPI_PUBLIC void INTcondvar_notifyAll(CONDVAR cv);

/*
 * INTcondvar_terminate frees the given previously allocated condition variable
 */
NSAPI_PUBLIC void INTcondvar_terminate(CONDVAR cv);


/*
 * Create a counting semaphore.  
 * Return non-zero on success, 0 on failure.
 */
NSAPI_PUBLIC COUNTING_SEMAPHORE INTcs_init(int initial_count);

/*
 * Destroy a counting semaphore 
 */
NSAPI_PUBLIC void INTcs_terminate(COUNTING_SEMAPHORE csp);

/*
 * Wait to "enter" the semaphore.
 * Return 0 on success, -1 on failure.
 */
NSAPI_PUBLIC int INTcs_wait(COUNTING_SEMAPHORE csp);

/*
 * Enter the semaphore if the count is > 0.  Otherwise return -1.
 *
 */
NSAPI_PUBLIC int INTcs_trywait(COUNTING_SEMAPHORE csp);

/*
 * Release the semaphore- allowing a thread to enter.
 * Return 0 on success, -1 on failure.
 */
NSAPI_PUBLIC int INTcs_release(COUNTING_SEMAPHORE csp);

NSPR_END_EXTERN_C

/* --- End function prototypes --- */

#define crit_init INTcrit_init
#define crit_enter INTcrit_enter
#define crit_exit INTcrit_exit
#define crit_terminate INTcrit_terminate
#define condvar_init INTcondvar_init
#define condvar_wait INTcondvar_wait
#define condvar_notify INTcondvar_notify
#define condvar_notifyAll INTcondvar_notifyAll
#define condvar_terminate INTcondvar_terminate
#define cs_init INTcs_init
#define cs_terminate INTcs_terminate
#define cs_wait INTcs_wait
#define cs_trywait INTcs_trywait
#define cs_release INTcs_release

#endif /* INTNSAPI */

#endif /* !BASE_CRIT_H */
