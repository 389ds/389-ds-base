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
/*
 * system.c: A grab bag of system-level abstractions
 * 
 * Many authors
 */

#include "netsite.h"
#include "prlog.h"
#include "base/ereport.h"

#ifdef XP_WIN32
#include <windows.h>
#endif

#include "base/systems.h"	/* find out if we have malloc pools */

static int thread_malloc_key = -1;

#if defined(MALLOC_POOLS) && defined(THREAD_ANY)
#include "base/pool.h"
#include "base/systhr.h"

#define MALLOC_KEY \
    ((pool_handle_t *)(thread_malloc_key != -1 ? systhread_getdata(thread_malloc_key) : NULL))

#endif


#ifdef MCC_DEBUG
#define DEBUG_MALLOC
#endif

#ifdef DEBUG_MALLOC

/* The debug malloc routines provide several functions:
 *
 *  - detect allocated memory overflow/underflow
 *  - detect multiple frees
 *  - intentionally clobbers malloc'd buffers
 *  - intentionally clobbers freed buffers
 */
#define DEBUG_MAGIC 0x12345678
#define DEBUG_MARGIN 32
#define DEBUG_MARGIN_CHAR '*'
#define DEBUG_MALLOC_CHAR '.'
#define DEBUG_FREE_CHAR   'X'
#endif /* DEBUG_MALLOC */

NSAPI_PUBLIC void *system_malloc(int size)
{
#if defined(MALLOC_POOLS) && defined(THREAD_ANY)
    return pool_malloc(MALLOC_KEY, size);
#else
    return malloc(size);
#endif
}


NSAPI_PUBLIC void *system_calloc(int size)
{
    void *ret;
#if defined(MALLOC_POOLS) && defined(THREAD_ANY)
    ret = pool_malloc(MALLOC_KEY, size);
#else
    ret = malloc(size);
#endif
    if(ret)
        ZERO(ret, size);
    return ret;
}


NSAPI_PUBLIC void *system_realloc(void *ptr, int size)
{
#if defined(MALLOC_POOLS) && defined(THREAD_ANY)
    return pool_realloc(MALLOC_KEY, ptr, size);
#else
    return realloc(ptr, size);
#endif
}


NSAPI_PUBLIC void system_free(void *ptr)
{
#if defined(MALLOC_POOLS) && defined(THREAD_ANY)
    pool_free(MALLOC_KEY, ptr);
#else
    PR_ASSERT(ptr);
    free(ptr);
#endif
}

NSAPI_PUBLIC char *system_strdup(const char *ptr)
{
    PR_ASSERT(ptr);
#if defined(MALLOC_POOLS) && defined(THREAD_ANY)
    return pool_strdup(MALLOC_KEY, ptr);
#else
    return strdup(ptr);
#endif
}


NSAPI_PUBLIC void *system_malloc_perm(int size)
{
#ifndef DEBUG_MALLOC
    return malloc(size);
#else
    char *ptr = (char *)malloc(size + 2*DEBUG_MARGIN+2*sizeof(int));
    char *real_ptr;
    int *magic;
    int *length;
  
    magic = (int *)ptr;
    *magic = DEBUG_MAGIC;
    ptr += sizeof(int);
    length = (int *)ptr;
    *length = size;
    ptr += sizeof(int);
    memset(ptr, DEBUG_MARGIN_CHAR, DEBUG_MARGIN);
    ptr += DEBUG_MARGIN;
    memset(ptr, DEBUG_MALLOC_CHAR, size);
    real_ptr = ptr;
    ptr += size;
    memset(ptr, DEBUG_MARGIN_CHAR, DEBUG_MARGIN);

    return real_ptr;
#endif
}

NSAPI_PUBLIC void *system_calloc_perm(int size)
{
    void *ret = system_malloc_perm(size);
    if(ret)
        ZERO(ret, size);
    return ret;
}

NSAPI_PUBLIC void *system_realloc_perm(void *ptr, int size)
{
#ifndef DEBUG_MALLOC
    return realloc(ptr, size);
#else
    int *magic, *length;
    char *cptr;

    cptr = (char *)ptr - DEBUG_MARGIN - 2 * sizeof(int);
    magic = (int *)cptr;
    if (*magic == DEBUG_MAGIC) {
        cptr += sizeof(int);
        length = (int *)cptr;
        if (*length < size) {
            char *newptr = (char *)system_malloc_perm(size);
            memcpy(newptr, ptr, *length);
            system_free_perm(ptr);

            return newptr;
        }else {
            return ptr;
        }
    } else {
        ereport(LOG_WARN, "realloc: attempt to realloc to smaller size");
        return realloc(ptr, size);
    }

#endif
}

NSAPI_PUBLIC void system_free_perm(void *ptr)
{
#ifdef DEBUG_MALLOC
    int *length, *magic;
    char *baseptr, *cptr;
    int index;

    PR_ASSERT(ptr);

    cptr = baseptr = ((char *)ptr) - DEBUG_MARGIN - 2*sizeof(int);

    magic = (int *)cptr;
    if (*magic == DEBUG_MAGIC) {
        cptr += sizeof(int);

        length = (int *)cptr;

        cptr += sizeof(int); 
        for (index=0; index<DEBUG_MARGIN; index++)
            if (cptr[index] != DEBUG_MARGIN_CHAR) {
                ereport(LOG_CATASTROPHE, "free: corrupt memory (prebounds overwrite)");
                break;
            }

        cptr += DEBUG_MARGIN + *length;
        for (index=0; index<DEBUG_MARGIN; index++)
            if (cptr[index] != DEBUG_MARGIN_CHAR) {
                ereport(LOG_CATASTROPHE, "free: corrupt memory (prebounds overwrite)");
                break;
            }

        memset(baseptr, DEBUG_FREE_CHAR, *length + 2*DEBUG_MARGIN+sizeof(int));
    } else {
        ereport(LOG_CATASTROPHE, "free: freeing unallocated memory");
    }
    free(baseptr);
#else
    free(ptr);
#endif
}

NSAPI_PUBLIC char *system_strdup_perm(const char *ptr)
{
#ifndef DEBUG_MALLOC
    PR_ASSERT(ptr);
    return strdup(ptr);
#else
    int len = strlen(ptr);
    char *nptr = (char *)system_malloc_perm(len+1);
    memcpy(nptr, ptr, len);
    nptr[len] = '\0';
    return nptr;
#endif
}

NSAPI_PUBLIC int 
getThreadMallocKey(void)
{
    return thread_malloc_key;
}

void
setThreadMallocKey(int key)
{
    thread_malloc_key = key;
}

