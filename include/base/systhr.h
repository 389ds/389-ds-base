/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef BASE_SYSTHR_H
#define BASE_SYSTHR_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * systhr.h: Abstracted threading mechanisms
 * 
 * Rob McCool
 */

#ifndef NETSITE_H
#include "netsite.h"
#endif /* !NETSITE_H */

#ifdef THREAD_ANY

/* --- Begin function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

#ifdef UnixWare
typedef void(*ArgFn_systhread_start)(void *);
NSAPI_PUBLIC
SYS_THREAD INTsysthread_start( int prio, int stksz, \
                              ArgFn_systhread_start, void *arg);
#else
NSAPI_PUBLIC
SYS_THREAD INTsysthread_start(int prio, int stksz, void (*fn)(void *), void *arg);
#endif

NSAPI_PUBLIC SYS_THREAD INTsysthread_current(void);

NSAPI_PUBLIC void INTsysthread_yield(void);

NSAPI_PUBLIC SYS_THREAD INTsysthread_attach(void);

NSAPI_PUBLIC void INTsysthread_detach(SYS_THREAD thr);

NSAPI_PUBLIC void INTsysthread_terminate(SYS_THREAD thr);

NSAPI_PUBLIC void INTsysthread_sleep(int milliseconds);

NSAPI_PUBLIC void INTsysthread_init(char *name);

NSAPI_PUBLIC void INTsysthread_timerset(int usec);

NSAPI_PUBLIC int INTsysthread_newkey(void);

NSAPI_PUBLIC void *INTsysthread_getdata(int key);

NSAPI_PUBLIC void INTsysthread_setdata(int key, void *data);

NSAPI_PUBLIC 
void INTsysthread_set_default_stacksize(unsigned long size);

NSPR_END_EXTERN_C

/* --- End function prototypes --- */
#define systhread_start INTsysthread_start
#define systhread_current INTsysthread_current
#define systhread_yield INTsysthread_yield
#define systhread_attach INTsysthread_attach
#define systhread_detach INTsysthread_detach
#define systhread_terminate INTsysthread_terminate
#define systhread_sleep INTsysthread_sleep
#define systhread_init INTsysthread_init
#define systhread_timerset INTsysthread_timerset
#define systhread_newkey INTsysthread_newkey
#define systhread_getdata INTsysthread_getdata
#define systhread_setdata INTsysthread_setdata
#define systhread_set_default_stacksize INTsysthread_set_default_stacksize

#endif /* INTNSAPI */

#endif /* THREAD_ANY */

#endif /* !BASE_SYSTHR_H */
