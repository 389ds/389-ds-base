/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef BASE_POOL_H
#define BASE_POOL_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * pool.h
 *
 * Module for handling memory allocations.
 *
 * Notes:
 * This module is used instead of the NSPR prarena module because the prarenas
 * did not fit as cleanly into the existing server.
 *
 * Mike Belshe
 * 10-02-95
 *
 */

#ifdef MALLOC_POOLS

#ifndef NETSITE_H
#include "netsite.h"
#endif /* !NETSITE_H */

/* --- Begin function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

int pool_internal_init(void);

#ifdef DEBUG_CACHES
NSAPI_PUBLIC int INTpool_service_debug(pblock *pb, Session *sn, Request *rq);
#endif

NSAPI_PUBLIC pool_handle_t *INTpool_create(void);
NSAPI_PUBLIC void INTpool_terminate(void);

NSAPI_PUBLIC void INTpool_destroy(pool_handle_t *pool_handle);

NSAPI_PUBLIC int INTpool_enabled(void);

NSAPI_PUBLIC void *INTpool_malloc(pool_handle_t *pool_handle, size_t size);

NSAPI_PUBLIC void INTpool_free(pool_handle_t *pool_handle, void *ptr);

NSAPI_PUBLIC void *INTpool_calloc(pool_handle_t *pool_handle, size_t nelem, size_t elsize);

NSAPI_PUBLIC
void *INTpool_realloc(pool_handle_t *pool_handle, void *ptr, size_t size);

NSAPI_PUBLIC
char *INTpool_strdup(pool_handle_t *pool_handle, const char *orig_str);

NSPR_END_EXTERN_C

#ifdef DEBUG_CACHES
#define pool_service_debug INTpool_service_debug
#endif /* DEBUG_CACHES */

#define pool_create INTpool_create
#define pool_terminate INTpool_terminate
#define pool_destroy INTpool_destroy
#define pool_enabled INTpool_enabled
#define pool_malloc INTpool_malloc
#define pool_free INTpool_free
#define pool_calloc INTpool_calloc
#define pool_realloc INTpool_realloc
#define pool_strdup INTpool_strdup

#endif /* INTNSAPI */

#endif /* MALLOC_POOLS */

#endif /* !BASE_POOL_H_ */
