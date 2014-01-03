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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
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

NSAPI_PUBLIC void *INTpool_malloc(pool_handle_t *pool_handle, size_t size );

NSAPI_PUBLIC void INTpool_free(pool_handle_t *pool_handle, void *ptr );

NSAPI_PUBLIC void *INTpool_calloc(pool_handle_t *pool_handle, size_t nelem, size_t elsize);

NSAPI_PUBLIC 
void *INTpool_realloc(pool_handle_t *pool_handle, void *ptr, size_t size );

NSAPI_PUBLIC
char *INTpool_strdup(pool_handle_t *pool_handle, const char *orig_str );

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
