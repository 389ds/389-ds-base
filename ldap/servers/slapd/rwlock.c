/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * rwlock.c - generic multiple reader, single-writer locking routines.
 *
 * The general idea is:
 *
 *  If you have a data structure which you'd like to allow multiple threads
 *  to read, but only one thread at a time to write, you include in your
 *  data structure a pointer to an rwl structure, and call rwl_new() to
 *  obtain an allocated and initialized rwl structure.
 *
 *  Then, call the appropriate functions via the provided function pointers
 *  to acquire/relinquish read or write locks on your data structure.  You
 *  may want to provide some convenience macros to make the code prettier.
 *
 *  The semantics are:
 *  - a thread attempting to obtain a read lock will succeed immediately as
 *    long as there are no threads with write locks.
 *  - a thread attempting to obtain a write lock will wait until all readers
 *    have relinquished their read locks.
 *  - a thread attempting to obtain a write lock blocks other threads from
 *    obtaining read locks.  As long as all readers release their locks,
 *    the write will eventually get the lock.
 */

#include "slap.h"

#include <prlock.h>
#include <prcvar.h>
#include "rwlock.h"

/*
 * Function: __rwl_acquire_read_lock
 *
 * Description: acquire a read lock.
 *
 * Arguments: rp: pointer to an rwl stucture
 *
 * Returns: 0 on success, -1 on failure.
 */
static int
__rwl_acquire_read_lock( rwl *rp )
{
    if ( rp == NULL ) {
	return -1;
    }
    PR_Lock( rp->rwl_writers_mutex );
    PR_Lock( rp->rwl_readers_mutex );
    rp->rwl_num_readers++;
    (void)PR_Unlock( rp->rwl_readers_mutex );
    (void)PR_Unlock( rp->rwl_writers_mutex );
    return 0;
}




/*
 * Function: __rwl_acquire_write_lock
 *
 * Description: acquire a write lock.
 *
 * Arguments: rp: pointer to an rwl stucture
 *
 * Returns: 0 on success, -1 on failure.
 */
static int
__rwl_acquire_write_lock( rwl *rp )
{
    if ( rp == NULL ) {
	return -1;
    }
    PR_Lock( rp->rwl_writers_mutex );
    PR_Lock( rp->rwl_readers_mutex );
    rp->rwl_writer_waiting = 1;
    while ( rp->rwl_num_readers > 0 ) {
	if ( PR_WaitCondVar( rp->rwl_writer_waiting_cv, PR_INTERVAL_NO_TIMEOUT ) != 0 ) {
	    (void)PR_Unlock( rp->rwl_writers_mutex );
	    (void)PR_Unlock( rp->rwl_readers_mutex );
	    return -1;
	}
    }
    /* XXXggood should rwl_writer_waiting be set zero here? */
    return 0;
}




/*
 * Function: __rwl_relinquish_read_lock
 *
 * Description: relinquish a read lock.
 *
 * Arguments: rp: pointer to an rwl stucture
 *
 * Returns: 0 on success, -1 on failure.
 */
static int
__rwl_relinquish_read_lock( rwl *rp )
{
    if ( rp == NULL ) {
	return -1;
    }
    PR_Lock( rp->rwl_readers_mutex );
    if ( --rp->rwl_num_readers == 0 && rp->rwl_writer_waiting ) {
	PR_NotifyCondVar( rp->rwl_writer_waiting_cv );
    }
    (void)PR_Unlock( rp->rwl_readers_mutex );
    return 0;
}




/*
 * Function: __rwl_relinquish_write_lock
 *
 * Description: relinquish a write lock.
 *
 * Arguments: rp: pointer to an rwl stucture
 *
 * Returns: 0 on success, -1 on failure.
 */
static int
__rwl_relinquish_write_lock( rwl *rp )
{
    if ( rp == NULL ) {
	return -1;
    }
    rp->rwl_writer_waiting = 0;
    (void)PR_Unlock( rp->rwl_readers_mutex );
    (void)PR_Unlock( rp->rwl_writers_mutex );
    return 0;
}



/*
 * Function: rwl_new
 *
 * Description: allocate and initialize a wrl structure.
 * 
 * Arguments: none
 *
 * Returns: on success, returns a pointer to an allocated, initialized rwl structure.
 *          on failure, returns NULL.
 *
 */
rwl *
rwl_new()
{
    rwl	*rp;

    if (( rp = (rwl *)malloc( sizeof( rwl ))) == NULL ) {
	return NULL;
    }
    
    if (( rp->rwl_readers_mutex = PR_NewLock()) == NULL ) {
	free( rp );
	return NULL;
    }
    if (( rp->rwl_writers_mutex = PR_NewLock()) == NULL ) {
	PR_DestroyLock( rp->rwl_readers_mutex );
	free( rp );
	return NULL;
    }
    if (( rp->rwl_writer_waiting_cv = PR_NewCondVar( rp->rwl_readers_mutex )) == NULL ) {
	PR_DestroyLock( rp->rwl_readers_mutex );
	PR_DestroyLock( rp->rwl_writers_mutex );
	free( rp );
    }
    rp->rwl_num_readers = rp->rwl_writer_waiting = 0;
    
    rp->rwl_acquire_read_lock = __rwl_acquire_read_lock;
    rp->rwl_relinquish_read_lock = __rwl_relinquish_read_lock;

    rp->rwl_acquire_write_lock = __rwl_acquire_write_lock;
    rp->rwl_relinquish_write_lock = __rwl_relinquish_write_lock;

    return rp;
}




/*
 * Function: rwl_free
 * 
 * Description: deallocates and frees an rwl structure.
 *
 * Arguments: rh: handle to an rwl structure.
 *
 * Returns: nothing
 */
void
rwl_free( rwl **rh )
{
    rwl	*rp;

    if ( rh == NULL || *rh == NULL ) {
	return;
    }
    rp = *rh;

    if ( rp->rwl_readers_mutex != NULL ) {
	PR_DestroyLock( rp->rwl_readers_mutex );
    }
    if ( rp->rwl_writers_mutex != NULL ) {
	PR_DestroyLock( rp->rwl_writers_mutex );
    }
    if ( rp->rwl_writer_waiting_cv != NULL ) {
	PR_DestroyCondVar( rp->rwl_writer_waiting_cv );
    }
    memset( rp, '\0', sizeof( rwl ));
    free( rp );
    *rh = NULL;
}
