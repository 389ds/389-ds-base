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


#ifndef _SEARCHTHREAD_H
#define _SEARCHTHREAD_H

typedef struct _searchthread SearchThread;

SearchThread *st_new(void);
void st_setThread(SearchThread *st, PRThread *tid, int id);
int st_getThread(SearchThread *st, PRThread **tid);
void search_start(void *v);
void st_getCountMinMax(SearchThread *st, PRUint32 *count, PRUint32 *min, PRUint32 *max);
int st_alive(SearchThread *st);

#endif
