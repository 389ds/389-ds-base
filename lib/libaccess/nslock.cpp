/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nslock.c)
 *
 *	This modules provides an interprocess locking mechanism, based
 *	on a named lock.
 */

#include "netsite.h"
#include "base/file.h"
#define __PRIVATE_NSLOCK
#include "nslock.h"
#include <assert.h>

char * NSLock_Program = "NSLOCK";

#ifdef FILE_UNIX
/*
 * The process-wide list of locks, NSLock_List, is protected by the
 * critical section, NSLock_Crit.
 */
CRITICAL NSLock_Crit = 0;
NSLock_t * NSLock_List = 0;
#endif /* FILE_UNIX */

/*
 * Description (nsLockOpen)
 *
 *	This function is used to initialize a handle for a lock.  The
 *	caller specifies a unique name for the lock, and a handle is
 *	returned.  The returned handle should be used by only one
 *	thread at a time, i.e. if multiple threads in a process are
 *	using the same lock, they should either have their own handles
 *	or protect a single handle with a critical section.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	lockname		- pointer to name of lock
 *	plock			- pointer to returned handle for lock
 *
 * Returns:
 *
 *	If successful, a handle for the specified lock is returned via
 *	'plock', and the return value is zero.  Otherwise the return
 *	value is a negative error code (see nslock.h), and an error
 *	frame is generated if an error frame list was provided.
 */

NSAPI_PUBLIC int nsLockOpen(NSErr_t * errp, char * lockname, void **plock)
{
    NSLock_t * nl = 0;			/* pointer to lock structure */
    int len;				/* length of lockname */
    int eid;
    int rv;

#ifdef FILE_UNIX
    /* Have we created the critical section for NSLock_List yet? */
    if (NSLock_Crit == 0) {

	/* Narrow the window for simultaneous initialization */
	NSLock_Crit = (CRITICAL)(-1);

	/* Create it */
	NSLock_Crit = crit_init();
    }

    /* Lock the list of locks */
    crit_enter(NSLock_Crit);

    /* See if a lock with the specified name exists already */
    for (nl = NSLock_List; nl != 0; nl = nl->nl_next) {
	if (!strcmp(nl->nl_name, lockname)) break;
    }

    /* Create a new lock if we didn't find it */
    if (nl == 0) {

	len = strlen(lockname);

	nl = (NSLock_t *)PERM_MALLOC(sizeof(NSLock_t) + len + 5);
	if (nl == 0) goto err_nomem;

	nl->nl_name = (char *)(nl + 1);
	strcpy(nl->nl_name, lockname);
	strcpy(&nl->nl_name[len], ".lck");
	nl->nl_cnt = 0;

	nl->nl_fd = open(nl->nl_name, O_RDWR|O_CREAT|O_EXCL, 0644);
	if (nl->nl_fd < 0) {

	    if (errno != EEXIST) {
		crit_exit(NSLock_Crit);
		goto err_create;
	    }

	    /* O_RDWR or O_WRONLY is required to use lockf on Solaris */
	    nl->nl_fd = open(nl->nl_name, O_RDWR, 0);
	    if (nl->nl_fd < 0) {
		crit_exit(NSLock_Crit);
		goto err_open;
	    }
	}

	/* Remove ".lck" from the lock name */
	nl->nl_name[len] = 0;

	/* Create a critical section for this lock (gag!) */
	nl->nl_crit = crit_init();

	/* Add this lock to NSLock_List */
	nl->nl_next = NSLock_List;
	NSLock_List = nl;
    }

    crit_exit(NSLock_Crit);

#else
/* write me */
    nl = (void *)4;
#endif /* FILE_UNIX */

    *plock = (void *)nl;
    return 0;

  err_nomem:
    eid = NSLERR1000;
    rv = NSLERRNOMEM;
    nserrGenerate(errp, rv, eid, NSLock_Program, 0);
    goto punt;

  err_create:
    eid = NSLERR1020;
    rv = NSLERRCREATE;
    goto err_file;

  err_open:
    eid = NSLERR1040;
    rv = NSLERROPEN;
  err_file:
    nserrGenerate(errp, rv, eid, NSLock_Program, 1, nl->nl_name);
  punt:
    if (nl) {
	FREE(nl);
    }
    *plock = 0;
    return rv;
}

/*
 * Description (nsLockAcquire)
 *
 *	This function is used to acquire exclusive ownership of a lock
 *	previously accessed via nsLockOpen().  The calling thread will
 *	be blocked until the lock is acquired.  Other threads in the
 *	process should not be blocked.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	lock			- handle for lock from nsLockOpen()
 *
 * Returns:
 *
 *	If successful, the return value is zero.  Otherwise the return
 *	value is a negative error code (see nslock.h), and an error
 *	frame is generated if an error frame list was provided.
 */

NSAPI_PUBLIC int nsLockAcquire(NSErr_t * errp, void * lock)
{
    NSLock_t * nl = (NSLock_t *)lock;
    int eid;
    int rv;

#ifdef FILE_UNIX
    /* Enter the critical section for the lock */
    crit_enter(nl->nl_crit);

    /* Acquire the file lock if we haven't already */
    if (nl->nl_cnt == 0) {
	rv = system_flock(nl->nl_fd);
	if (rv) {
	    crit_exit(nl->nl_crit);
	    goto err_lock;
	}
    }

    /* Bump the lock count */
    nl->nl_cnt++;

    crit_exit(nl->nl_crit);
#else
 /* write me */
#endif /* FILE_UNIX */

    /* Indicate success */
    return 0;

  err_lock:
    eid = NSLERR1100;
    rv = NSLERRLOCK;
    nserrGenerate(errp, rv, eid, NSLock_Program, 1, nl->nl_name);

    return rv;
}

/*
 * Description (nsLockRelease)
 *
 *	This function is used to release exclusive ownership to a lock
 *	that was previously obtained via nsLockAcquire().
 *
 * Arguments:
 *
 *	lock			- handle for lock from nsLockOpen()
 */

NSAPI_PUBLIC void nsLockRelease(void * lock)
{
    NSLock_t * nl = (NSLock_t *)lock;

#ifdef FILE_UNIX
    assert(nl->nl_cnt > 0);

    crit_enter(nl->nl_crit);

    if (--nl->nl_cnt <= 0) {
	system_ulock(nl->nl_fd);
	nl->nl_cnt = 0;
    }

    crit_exit(nl->nl_crit);
#endif /* FILE_UNIX */
}

/*
 * Description (nsLockClose)
 *
 *	This function is used to close a lock handle that was previously
 *	acquired via nsLockOpen().  The lock should not be owned.
 *
 * Arguments:
 *
 *	lock			- handle for lock from nsLockOpen()
 */

NSAPI_PUBLIC void nsLockClose(void * lock)
{
    NSLock_t * nl = (NSLock_t *)lock;

#ifdef FILE_UNIX
    /* Don't do anything with the lock, since it will get used again */
#if 0
    crit_enter(nl->nl_crit);
    close(nl->nl_fd);
    crit_exit(nl->nl_crit);
    FREE(nl);
#endif
#else
 /* write me */
#endif FILE_UNIX
}
