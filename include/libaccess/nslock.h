/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nslock_h
#define __nslock_h

/*
 * Description (nslock.h)
 *
 *	This file defines to interface for a locking facility that
 *	provides exclusive access to a resource across multiple
 *	server processes.
 */

#include "nserror.h"
#include "base/crit.h"

#ifdef __PRIVATE_NSLOCK

/*
 * Description (NSLock_t)
 *
 *	This type represents a lock.  It includes a name which
 *	uniquely identifies the lock, and a handle for referencing
 *	the lock once it has been initialized.
 */

typedef struct NSLock_s NSLock_t;
struct NSLock_s {
    NSLock_t * nl_next;			/* next lock on NSLock_List */
    char * nl_name;			/* name associate with lock */
#if defined(FILE_UNIX)
    CRITICAL nl_crit;			/* critical section for threads */
    SYS_FILE nl_fd;			/* file descriptor */
    int nl_cnt;				/* nsLockAcquire() count */
#elif defined(XP_WIN32)
#else
#error "nslock.h needs work for this platform"
#endif
};

#endif /* __PRIVATE_NSLOCK */

/* Define error identifiers */

/* nsLockOpen() */
#define NSLERR1000	1000		/* insufficient dynamic memory */
#define NSLERR1020	1020		/* error creating lock */
#define NSLERR1040	1040		/* error accessing lock */

/* nsLockAcquire() */
#define NSLERR1100	1100		/* error acquiring lock */

/* Define error return codes */

#define NSLERRNOMEM	-1		/* insufficient dynamic memory */
#define NSLERRCREATE	-2		/* error creating lock */
#define NSLERROPEN	-3		/* error accessing lock */
#define NSLERRLOCK	-4		/* error acquiring lock */

NSPR_BEGIN_EXTERN_C

/* Functions in nslock.c */
extern NSAPI_PUBLIC int nsLockOpen(NSErr_t * errp,
				   char * lockname, void **plock);
extern NSAPI_PUBLIC int nsLockAcquire(NSErr_t * errp, void * lock);
extern NSAPI_PUBLIC void nsLockRelease(void * lock);
extern NSAPI_PUBLIC void nsLockClose(void * lock);

NSPR_END_EXTERN_C

#endif __nslock_h
