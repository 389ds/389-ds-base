/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#if !defined( _COS_CACHE_H )
#define _COS_CACHE_H

typedef void cos_cache;

int cos_cache_init();
void cos_cache_stop();
int cos_cache_getref(cos_cache **ppCache);
int cos_cache_addref(cos_cache *pCache);
int cos_cache_release(cos_cache *pCache);
void cos_cache_change_notify(Slapi_PBlock *pb);

#endif /* _COS_CACHE_H */
