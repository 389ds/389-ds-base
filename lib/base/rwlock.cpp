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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdlib.h>
#include "crit.h"
#include "rwlock.h"

/*
 * rwLock.c
 *    Implements a shared/exclusive lock package atop the
 * critical section/condition variables. It allows multiple
 * shared lock holders and only one exclusive lock holder
 * on a lock variable.
 *
 * NOTE : It currently favors writers over readers and writers
 *  may starve readers. It is usually preferable to allow updates
 *  if they are not as frequent. We may have to change this if
 *  the common usage pattern differs from this.
 */

typedef struct {
    CRITICAL crit;           /* Short term crit to synchronize lock ops */
    CONDVAR  readFree;       /* Indicates lock is free for readers */
    CONDVAR  writeFree;      /* Indicates lock is free for the writer */
    int      numReaders;     /* Number of read locks held */
    int      write;          /* Flag to indicate write lock held */
    int      numWriteWaiters;/* Number of threads waiting for write lock */
} rwLock_t;

/* 
 * rwlock_init()
 *   Allocate and initialize the rwlock structure and return
 *  to the caller.
 */
RWLOCK rwlock_Init()
{
    rwLock_t *rwLockP;

    rwLockP = (rwLock_t *)PERM_MALLOC(sizeof(rwLock_t));
    rwLockP->numReaders = 0;
    rwLockP->write = 0;
    rwLockP->numWriteWaiters = 0;
    rwLockP->crit = crit_init();
    rwLockP->readFree = condvar_init(rwLockP->crit);
    rwLockP->writeFree = condvar_init(rwLockP->crit);
    return((RWLOCK)rwLockP);
}

/* 
 * rwlock_terminate()
 *   Terminate the associated condvars and critical sections
 */
void rwlock_Terminate(RWLOCK lockP)
{
    rwLock_t   *rwLockP = (rwLock_t *)lockP;
    
    crit_terminate(rwLockP->crit);
    condvar_terminate(rwLockP->readFree);
    condvar_terminate(rwLockP->writeFree);
    PERM_FREE(rwLockP);
}

/*
 * rwlock_ReadLock -- Obtain a shared lock. The caller would
 *  block if there are writers or writeWaiters.
 */
void rwlock_ReadLock(RWLOCK lockP)
{
    rwLock_t *rwLockP = (rwLock_t *)lockP;

    crit_enter(rwLockP->crit);                 
    while (rwLockP->write || rwLockP->numWriteWaiters != 0)
        condvar_wait(rwLockP->readFree);
    rwLockP->numReaders++;                              
    crit_exit(rwLockP->crit);                 
}

/*
 * rwlock_writeLock -- Obtain an exclusive lock. The caller would
 * block if there are other readers or a writer.
 */
void rwlock_WriteLock(RWLOCK lockP)
{
    rwLock_t *rwLockP = (rwLock_t *)lockP;

    crit_enter(rwLockP->crit);                 
    rwLockP->numWriteWaiters++;                                
    while (rwLockP->numReaders != 0 || rwLockP->write)             
        condvar_wait(rwLockP->writeFree);      
    rwLockP->numWriteWaiters--;                                
    rwLockP->write = 1;                                   
    crit_exit(rwLockP->crit);                 
}

/*
 * rw_Unlock -- Releases the lock. 
 */
void rwlock_Unlock(RWLOCK lockP)
{
    rwLock_t *rwLockP = (rwLock_t *)lockP;
                                                         
    crit_enter(rwLockP->crit);                 
    if (rwLockP->write)                                      
        rwLockP->write = 0;                              
    else                                                 
        rwLockP->numReaders--;                                 
    if (rwLockP->numReaders == 0)                              
        if (rwLockP->numWriteWaiters != 0)                     
            condvar_notify(rwLockP->writeFree);             
        else                                             
            condvar_notifyAll(rwLockP->readFree);           
    crit_exit(rwLockP->crit);                 
}
                                                         
/*
 * rwlock_DemoteLock -- Change an exclusive lock on the given lock
 * variable into a shared lock.  
 */
void rwlock_DemoteLock(RWLOCK lockP)
{
    rwLock_t *rwLockP = (rwLock_t *)lockP;
                                                         
    crit_enter(rwLockP->crit);                 
    rwLockP->numReaders = 1;
    rwLockP->write = 0;
    if (rwLockP->numWriteWaiters == 0)
        condvar_notifyAll(rwLockP->readFree);
    crit_exit(rwLockP->crit);                 
}
