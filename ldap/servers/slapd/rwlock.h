/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _RWLOCK_H_
#define _RWLOCK_H_

#include <prlock.h>
#include <prcvar.h>

typedef struct _rwl {
    PRLock	*rwl_readers_mutex;
    PRLock	*rwl_writers_mutex;
    PRCondVar	*rwl_writer_waiting_cv;
    int		rwl_num_readers;
    int		rwl_writer_waiting;

    int		(*rwl_acquire_read_lock)( struct _rwl * );
    int		(*rwl_relinquish_read_lock)( struct _rwl * );

    int		(*rwl_acquire_write_lock)( struct _rwl * );
    int		(*rwl_relinquish_write_lock)( struct _rwl * );
} rwl;

extern rwl *rwl_new();
extern void rwl_free( rwl **rh );
#endif /* _RWLOCK_H_ */
