/** BEGIN COPYRIGHT BLOCK
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

#if defined(MALLOC_POOLS) && defined(MCC_HTTPD) && defined(THREAD_ANY)
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
#if defined(MALLOC_POOLS) && defined(MCC_HTTPD) && defined(THREAD_ANY)
    return pool_malloc(MALLOC_KEY, size);
#else
    return malloc(size);
#endif
}


NSAPI_PUBLIC void *system_calloc(int size)
{
    void *ret;
#if defined(MALLOC_POOLS) && defined(MCC_HTTPD) && defined(THREAD_ANY)
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
#if defined(MALLOC_POOLS) && defined(MCC_HTTPD) && defined(THREAD_ANY)
    return pool_realloc(MALLOC_KEY, ptr, size);
#else
    return realloc(ptr, size);
#endif
}


NSAPI_PUBLIC void system_free(void *ptr)
{
#if defined(MALLOC_POOLS) && defined(MCC_HTTPD) && defined(THREAD_ANY)
    pool_free(MALLOC_KEY, ptr);
#else
    PR_ASSERT(ptr);
    free(ptr);
#endif
}

NSAPI_PUBLIC char *system_strdup(const char *ptr)
{
    PR_ASSERT(ptr);
#if defined(MALLOC_POOLS) && defined(MCC_HTTPD) && defined(THREAD_ANY)
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

