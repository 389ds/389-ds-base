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

#ifdef THREAD_WIN32
#include <process.h>

typedef struct {
    HANDLE hand;
    DWORD id;
} sys_thread_s;

#endif



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
#ifdef UnixWare /* for ANSI C++ standard, see base/systrh.h */
systhread_start(int prio, int stksz, ArgFn_systhread_start fn, void *arg)
#else
systhread_start(int prio, int stksz, void (*fn)(void *), void *arg)
#endif
{
#if (defined(Linux) || defined(SNI) || defined(UnixWare)) && !defined(USE_PTHREADS)
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
#ifdef XP_UNIX
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

#elif defined(THREAD_WIN32)

#include <nspr/prthread.h>
#define DEFAULT_STACKSIZE 262144

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC 
SYS_THREAD systhread_start(int prio, int stksz, void (*fn)(void *), void *arg)
{
    sys_thread_s *ret = (sys_thread_s *) MALLOC(sizeof(sys_thread_s));

    if ((ret->hand = (HANDLE)_beginthreadex(NULL, stksz, (unsigned (__stdcall *)(void *))fn, 
	                                        arg, 0, &ret->id)) == 0) {
        FREE(ret);
        return NULL;
    }
    return (void *)ret;
}

NSPR_END_EXTERN_C

NSAPI_PUBLIC SYS_THREAD systhread_current(void)
{
    /* XXXrobm this is busted.... */
    return GetCurrentThread();
}

NSAPI_PUBLIC void systhread_timerset(int usec)
{
}

NSAPI_PUBLIC SYS_THREAD systhread_attach(void)
{
    return NULL;
}

NSAPI_PUBLIC void systhread_yield(void)
{
    systhread_sleep(0);
}

NSAPI_PUBLIC void systhread_terminate(SYS_THREAD thr)
{
    TerminateThread(((sys_thread_s *)thr)->hand, 0);
}


NSAPI_PUBLIC void systhread_sleep(int milliseconds)
{
    /* XXXrobm there must be a better way to do this */
    HANDLE sem = CreateSemaphore(NULL, 1, 4, "sleeper");
    WaitForSingleObject(sem, INFINITE);
    WaitForSingleObject(sem, milliseconds);
    CloseHandle(sem);
}

NSAPI_PUBLIC void systhread_init(char *name)
{
    PR_Init(PR_USER_THREAD, 1, 0);
}


NSAPI_PUBLIC int systhread_newkey()
{
    return TlsAlloc();
}

NSAPI_PUBLIC void *systhread_getdata(int key)
{
    return (void *)TlsGetValue(key);
}

NSAPI_PUBLIC void systhread_setdata(int key, void *data)
{
    TlsSetValue(key, data);
}

#endif

