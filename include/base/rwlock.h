/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * rwlock.h: Shared/Exclusive lock abstraction. 
 * 
 * Sanjay Krishnamurthi
 */
#ifndef _BASE_RWLOCK_H_
#define _BASE_RWLOCK_H_

#include "netsite.h"
#include "crit.h"

NSPR_BEGIN_EXTERN_C

typedef void* RWLOCK;

/*
 * rwlock_Init()
 *  creates and returns a new readwrite lock variable. 
 */
NSAPI_PUBLIC RWLOCK rwlock_Init(void);

/*
 * rwlock_ReadLock()
 */
NSAPI_PUBLIC void rwlock_ReadLock(RWLOCK lock);

/*
 * rwlock_WriteLock()
 */
NSAPI_PUBLIC void rwlock_WriteLock(RWLOCK lock);

/*
 * rwlock_Unlock()
 */
NSAPI_PUBLIC void rwlock_Unlock(RWLOCK lock);

/*
 * rwlock_DemoteLock()
 */
NSAPI_PUBLIC void rwlock_DemoteLock(RWLOCK lock);

/*
 * rwlock_terminate removes a previously allocated RWLOCK variable.
 */
NSAPI_PUBLIC void rwlock_Terminate(RWLOCK lock);

NSPR_END_EXTERN_C

#endif /* _BASE_RWLOCK_H_ */
