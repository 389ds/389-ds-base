/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * systhr.c: Abstracted threading mechanisms
 * 
 * Rob McCool
 */

#include "systhr.h"

#define USE_NSPR
#ifdef USE_NSPR
#include "nspr.h"
#include "private/prpriv.h"
#ifdef LINUX
#    include <sys/time.h>
#    include <sys/resource.h>
#else
/* This declaration should be removed when NSPR newer than v4.6 is picked up,
   which should have the fix for bug 326110
 */
extern "C" {
int32 PR_GetSysfdTableMax(void);
int32 PR_SetSysfdTableSize(int table_size);
}
#endif
#endif
#include "systems.h"

#if defined (USE_NSPR)

#if defined(__hpux) && defined(__ia64)
#define DEFAULT_STACKSIZE (256*1024)
#else
#define DEFAULT_STACKSIZE (64*1024)
#endif

static unsigned long _systhr_stacksize = DEFAULT_STACKSIZE;

NSAPI_PUBLIC 
void systhread_set_default_stacksize(unsigned long size)
{
	_systhr_stacksize = size;
}

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC SYS_THREAD
systhread_start(int prio, int stksz, void (*fn)(void *), void *arg)
{
#if defined(LINUX) && !defined(USE_PTHREADS)
    prio /= 8; /* quick and dirty fix for user thread priority scale problem */
    if (prio > 3) prio = 3;
#endif

    PRThread *ret = PR_CreateThread(PR_USER_THREAD, (void (*)(void *))fn,
				    (void *)arg, (PRThreadPriority)prio, 
				    PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                                    stksz ? stksz : _systhr_stacksize);
    return (void *) ret;
}

NSPR_END_EXTERN_C


NSAPI_PUBLIC SYS_THREAD systhread_current(void)
{
    return PR_GetCurrentThread();
}

NSAPI_PUBLIC void systhread_yield(void)
{
  /* PR_Yield(); */
  PR_Sleep(PR_INTERVAL_NO_WAIT);
}


NSAPI_PUBLIC void systhread_timerset(int usec)
{
   /* This is an interesting problem.  If you ever do turn on interrupts
    * on the server, you're in for lots of fun with NSPR Threads
   PR_StartEvents(usec); */
}


NSAPI_PUBLIC 
SYS_THREAD systhread_attach(void)
{
    PRThread *ret;
    ret = PR_AttachThread(PR_USER_THREAD, PR_PRIORITY_NORMAL, NULL);

    return (void *) ret;
}

NSAPI_PUBLIC
void systhread_detach(SYS_THREAD thr)
{
    /* XXXMB - this is not correct! */
    PR_DetachThread();
}

NSAPI_PUBLIC void systhread_terminate(SYS_THREAD thr)
{

    /* Should never be here. PR_DestroyThread is no 
     * longer used. */
    PR_ASSERT(0);
  
    /* PR_DestroyThread((PRThread *) thr); */
}

NSAPI_PUBLIC void systhread_sleep(int milliseconds)
{
    PR_Sleep(milliseconds);
}

NSAPI_PUBLIC void systhread_init(char *name)
{
    PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 256);
#ifdef LINUX
    /*
     * NSPR 4.6 does not export PR_SetSysfdTableSize
     * and PR_GetSysfdTableMax by mistake (NSPR Bugzilla
     * bug 326110) on platforms that use GCC with symbol
     * visibility, so we have to call the system calls
     * directly.
     */
    {
        struct rlimit rlim;
        if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
            return;
        rlim.rlim_cur = rlim.rlim_max;
        (void) setrlimit(RLIMIT_NOFILE, &rlim);
    }
#else 
    PR_SetSysfdTableSize(PR_GetSysfdTableMax());
#endif
}


NSAPI_PUBLIC int systhread_newkey()
{
    uintn newkey;

    PR_NewThreadPrivateIndex(&newkey, NULL);
    return (newkey);
}

NSAPI_PUBLIC void *systhread_getdata(int key)
{
    return PR_GetThreadPrivate(key);
}

NSAPI_PUBLIC void systhread_setdata(int key, void *data)
{
    PR_SetThreadPrivate(key, data);
}

/* 
 * Drag in the Java code, so our dynamic library full of it works 
 * i.e. force these symbols to load.
 */
NSAPI_PUBLIC void systhread_dummy(void)
{

}

#endif

