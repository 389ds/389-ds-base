/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef NETSITE_H
#define NETSITE_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * Standard defs for NetSite servers.
 */

/*
** Macro shorthands for conditional C++ extern block delimiters.
** Don't redefine for compatability with NSPR.
*/
#ifndef NSPR_BEGIN_EXTERN_C
#ifdef __cplusplus
#define NSPR_BEGIN_EXTERN_C	extern "C" {
#define NSPR_END_EXTERN_C	}
#else
#define NSPR_BEGIN_EXTERN_C
#define NSPR_END_EXTERN_C
#endif
#endif /* NSPR_BEGIN_EXTERN_C */
#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

#ifndef VERSION_H
#include "version.h"
#endif /* !VERSION_H */

#ifndef BASE_SYSTEMS_H
#include "base/systems.h"
#endif /* !BASE_SYSTEMS_H */

#ifndef VOID
#define VOID void
#endif

#ifdef XP_UNIX
/*
 * Provide some typedefs that are commonly used on windows
 *
 * DO NOT USE THESE!  They will be deleted later!
 *
 */
#define CONST const
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef float               FLOAT;
typedef FLOAT               *PFLOAT;
typedef BOOL                *PBOOL;
typedef BOOL                *LPBOOL;
typedef BYTE                *PBYTE;
typedef BYTE                *LPBYTE;
typedef int                 *PINT;
typedef int                 *LPINT;
typedef WORD                *PWORD;
typedef WORD                *LPWORD;
typedef long                *LPLONG;
typedef DWORD               *PDWORD;
typedef DWORD               *LPDWORD;
typedef void                *LPVOID;

#ifndef SNI
#if !defined (boolean) && !defined (__GNUC__)
typedef int                  boolean;
#endif
#endif

#endif
#define NS_TRUE              1
#define NS_FALSE             0

NSPR_BEGIN_EXTERN_C

#ifndef APSTUDIO_READONLY_SYMBOLS

/* Include the public netsite.h definitions */
#ifndef PUBLIC_NETSITE_H
#ifdef MALLOC_DEBUG
#define NS_MALLOC_DEBUG
#endif /* MALLOC_DEBUG */
#include "public/netsite.h"
#endif /* PUBLIC_NETSITE_H */

#endif /* !APSTUDIO_READONLY_SYMBOLS */

/*
 * If NS_MALLOC_DEBUG is defined, declare the debug version of the memory
 * allocation API.
 */
#ifdef NS_MALLOC_DEBUG
#define PERM_MALLOC(size) INTsystem_malloc_perm(size, __LINE__, __FILE__)
NSAPI_PUBLIC void *INTsystem_malloc_perm(int size, int line, char *file);

#define PERM_CALLOC(size) INTsystem_calloc_perm(size, __LINE__, __FILE__)
NSAPI_PUBLIC void *INTsystem_calloc_perm(int size, int line, char *file);

#define PERM_REALLOC(ptr, size) INTsystem_realloc_perm(ptr, size, __LINE__, __FILE__)
NSAPI_PUBLIC void *INTsystem_realloc_perm(void *ptr, int size, int line, char *file);

#define PERM_FREE(ptr) INTsystem_free_perm((void *) ptr, __LINE__, __FILE__)
NSAPI_PUBLIC void INTsystem_free_perm(void *ptr, int line, char *file);

#define PERM_STRDUP(ptr) INTsystem_strdup_perm(ptr, __LINE__, __FILE__)
NSAPI_PUBLIC char *INTsystem_strdup_perm(const char *ptr, int line, char *file);
#endif /* NS_MALLOC_DEBUG */

/*
 * Only the mainline needs to set the malloc key.
 */

void setThreadMallocKey(int key);

/* This probably belongs somewhere else, perhaps with a different name */
NSAPI_PUBLIC char *INTdns_guess_domain(char * hname);

/* --- Begin public functions --- */

#ifdef INTNSAPI

NSAPI_PUBLIC char *INTsystem_version();

/*
   Depending on the system, memory allocated via these macros may come from 
   an arena. If these functions are called from within an Init function, they 
   will be allocated from permanent storage. Otherwise, they will be freed 
   when the current request is finished.
 */

#define MALLOC(size) INTsystem_malloc(size)
NSAPI_PUBLIC void *INTsystem_malloc(int size);

#define CALLOC(size) INTsystem_calloc(size)
NSAPI_PUBLIC void *INTsystem_calloc(int size);

#define REALLOC(ptr, size) INTsystem_realloc(ptr, size)
NSAPI_PUBLIC void *INTsystem_realloc(void *ptr, int size);

#define FREE(ptr) INTsystem_free((void *) ptr)
NSAPI_PUBLIC void INTsystem_free(void *ptr);

#define STRDUP(ptr) INTsystem_strdup(ptr)
NSAPI_PUBLIC char *INTsystem_strdup(const char *ptr);

/*
   These macros always provide permanent storage, for use in global variables
   and such. They are checked at runtime to prevent them from returning NULL.
 */

#ifndef NS_MALLOC_DEBUG

#define PERM_MALLOC(size) INTsystem_malloc_perm(size)
NSAPI_PUBLIC void *INTsystem_malloc_perm(int size);

#define PERM_CALLOC(size) INTsystem_calloc_perm(size)
NSAPI_PUBLIC void *INTsystem_calloc_perm(int size);

#define PERM_REALLOC(ptr, size) INTsystem_realloc_perm(ptr, size)
NSAPI_PUBLIC void *INTsystem_realloc_perm(void *ptr, int size);

#define PERM_FREE(ptr) INTsystem_free_perm((void *) ptr)
NSAPI_PUBLIC void INTsystem_free_perm(void *ptr);

#define PERM_STRDUP(ptr) INTsystem_strdup_perm(ptr)
NSAPI_PUBLIC char *INTsystem_strdup_perm(const char *ptr);

#endif /* !NS_MALLOC_DEBUG */

/* Thread-Private data key index for accessing the thread-private memory pool.
 * Each thread creates its own pool for allocating data.  The MALLOC/FREE/etc
 * macros have been defined to check the thread private data area with the
 * thread_malloc_key index to find the address for the pool currently in use.
 *
 * If a thread wants to use a different pool, it must change the thread-local-
 * storage[thread_malloc_key].
 */

NSAPI_PUBLIC int INTgetThreadMallocKey(void);

/* Not sure where to put this. */
NSAPI_PUBLIC void INTmagnus_atrestart(void (*fn)(void *), void *data);

#endif /* INTNSAPI */

/* --- End public functions --- */

NSPR_END_EXTERN_C

#define system_version_set INTsystem_version_set
#define dns_guess_domain INTdns_guess_domain

#ifdef INTNSAPI

#define system_version INTsystem_version
#define system_malloc INTsystem_malloc
#define system_calloc INTsystem_calloc
#define system_realloc INTsystem_realloc
#define system_free INTsystem_free
#define system_strdup INTsystem_strdup
#define system_malloc_perm INTsystem_malloc_perm
#define system_calloc_perm INTsystem_calloc_perm
#define system_realloc_perm INTsystem_realloc_perm
#define system_free_perm INTsystem_free_perm
#define system_strdup_perm INTsystem_strdup_perm
#define getThreadMallocKey INTgetThreadMallocKey
#define magnus_atrestart INTmagnus_atrestart

#endif /* INTNSAPI */

#endif /* NETSITE_H */
