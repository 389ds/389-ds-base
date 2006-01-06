/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _SEARCHTHREAD_H
#define _SEARCHTHREAD_H

typedef struct _searchthread SearchThread;

SearchThread *st_new(void);
void st_setThread(SearchThread *st, PRThread *tid, int id);
int st_getThread(SearchThread *st, PRThread **tid);
void search_start(void *v);
void st_getCountMinMax(SearchThread *st, PRUint32 *count, PRUint32 *min,
		       PRUint32 *max);
int st_alive(SearchThread *st);

#endif
