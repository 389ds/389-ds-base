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

#ifndef PUBLIC_BASE_SYSTEMS_H
#define PUBLIC_BASE_SYSTEMS_H

/*
 * File:        systems.h
 *
 * Description:
 *
 *      This file defines various platform-dependent symbols, which are
 *      used to configure platform-dependent aspects of the API.
 */

/* --- Begin native platform configuration definitions --- */

#if defined(HPUX)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
/* warning: mmap doesn't work under 9.04 */
#define SHMEM_UNIX_MMAP
#define ZERO(ptr, len) memset(ptr, 0, len)

#elif defined(SOLARIS) || defined(SOLARISx86)

#undef FILE_UNIX /* avoid redefinition message */
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
/* The Solaris routines return ENOSPC when too many semaphores are SEM_UNDO. */
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr, len) memset(ptr, 0, len)

#elif defined(SUNOS4)

#define BSD_FLOCK
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr, len) memset(ptr, 0, len)

#elif defined(LINUX)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr, len) memset(ptr, 0, len)

#elif defined(__FreeBSD__)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr, len) memset(ptr, 0, len)

#else
#error "Missing defines in ns/netsite/include/public/base/systems.h"
#endif

#ifndef NSPR_BEGIN_EXTERN_C
#ifdef __cplusplus
#define NSPR_BEGIN_EXTERN_C extern "C" {
#define NSPR_END_EXTERN_C }
#else
#define NSPR_BEGIN_EXTERN_C
#define NSPR_END_EXTERN_C
#endif /* __cplusplus */
#endif /* !NSPR_BEGIN_EXTERN_C */

#ifndef NSAPI_PUBLIC
#define NSAPI_PUBLIC
#endif /* !NSAPI_PUBLIC */

#if defined(NEED_STRCASECMP) || defined(NEED_STRNCASECMP)
#ifndef CASECMPARG_T
#define CASECMPARG_T const
#endif /* !CASECMPARG_T */
#endif /* NEED_STRCASECMP || NEED_STRNCASECMP */

#endif /* PUBLIC_BASE_SYSTEMS_H */
