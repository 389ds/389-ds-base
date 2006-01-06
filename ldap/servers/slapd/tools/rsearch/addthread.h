/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _ADDTHREAD_H
#define _ADDTHREAD_H

typedef struct _addthread AddThread;

AddThread *at_new(void);
void at_setThread(AddThread *at, PRThread *tid, int id);
int at_getThread(AddThread *at, PRThread **tid);
void infadd_start(void *v);
void at_getCountMinMax(AddThread *at, PRUint32 *count, PRUint32 *min,
		       PRUint32 *max, PRUint32 *total);
int at_alive(AddThread *at);
void at_initID(unsigned long i);

#endif
