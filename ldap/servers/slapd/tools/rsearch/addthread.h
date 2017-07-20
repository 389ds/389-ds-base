/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#ifndef _ADDTHREAD_H
#define _ADDTHREAD_H

typedef struct _addthread AddThread;

AddThread *at_new(void);
void at_setThread(AddThread *at, PRThread *tid, int id);
int at_getThread(AddThread *at, PRThread **tid);
void infadd_start(void *v);
void at_getCountMinMax(AddThread *at, PRUint32 *count, PRUint32 *min, PRUint32 *max, PRUint32 *total);
int at_alive(AddThread *at);
void at_initID(unsigned long i);

#endif
