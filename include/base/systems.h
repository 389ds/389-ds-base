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

#ifndef BASE_SYSTEMS_H
#define BASE_SYSTEMS_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/*
 * systems.h: Lists of defines for systems
 *
 * This sets what general flavor the system is (UNIX, etc.),
 * and defines what extra functions your particular system needs.
 */


/* --- Begin common definitions for all supported platforms --- */

#define DAEMON_ANY
#define DAEMON_STATS

/* --- End common definitions for all supported platforms --- */

/* --- Begin platform-specific definitions --- */


#if defined(HPUX)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#undef BSD_RLIMIT
#undef BSD_SIGNALS
#ifdef MCC_PROXY
#define DAEMON_NEEDS_SEMAPHORE
#else
#undef DAEMON_NEEDS_SEMAPHORE
#endif
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_HPSHL
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_PRIVATE
#define HAS_STATFS
#define HAVE_ATEXIT
#define JAVA_STATIC_LINK
#undef NEED_CRYPT_H
#define NET_SOCKETS
#define SA_HANDLER_T(x) (void (*)(int)) x
/* warning: mmap doesn't work under 9.04 */
#define SHMEM_MMAP_FLAGS MAP_FILE | MAP_VARIABLE | MAP_SHARED

#elif defined(LINUX)

#define ACCELERATOR_CACHE
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define DAEMON_UNIX_MOBRULE
#define BSD_RLIMIT
#undef BSD_SIGNALS
#define FILE_UNIX_MMAP
#define FILE_MMAP_FLAGS (MAP_FILE | MAP_SHARED)
#define SHMEM_UNIX_MMAP
#define SHMEM_MMAP_FLAGS MAP_SHARED
#define AUTH_DBM
#define SEM_FLOCK
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define HAVE_ATEXIT
#define HAS_STATFS
#define JAVA_STATIC_LINK
#define SA_HANDLER_T(x) (void (*)(int))(x)
#undef NEED_CRYPT_PROTO
#define NET_SOCKETS
#ifndef NO_DOMAINNAME
#define NO_DOMAINNAME
#endif

#elif defined(SOLARIS) || defined(SOLARISx86)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#define BSD_RLIMIT
#undef BSD_SIGNALS
#define DAEMON_NEEDS_SEMAPHORE
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATVFS
#define HAVE_ATEXIT
#define HAVE_PW_R
#define HAVE_STRTOK_R
#define HAVE_TIME_R 3 /* arg count */
#define NEED_CRYPT_H
#define NEED_FILIO
#if OSVERSION < 506 || OSVERSION == 50501
#define NEED_GHN_PROTO
#endif
#define NET_SOCKETS
#if OSVERSION > 504
#define SA_HANDLER_T(x) x
#endif
#define SHMEM_MMAP_FLAGS MAP_SHARED

#elif defined(SUNOS4)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#define BSD_MAIL
#define BSD_RLIMIT
#define BSD_SIGNALS
#define BSD_TIME
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS 1
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATFS
#undef HAVE_ATEXIT
#undef NEED_CRYPT_H
#define NEED_CRYPT_PROTO
#define NEED_FILIO
#define NET_SOCKETS
#define SHMEM_MMAP_FLAGS MAP_SHARED

#elif defined(__FreeBSD__)

#define ACCELERATOR_CACHE
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define DAEMON_UNIX_MOBRULE
#define BSD_RLIMIT
#define BSD_SIGNALS
#define FILE_UNIX_MMAP
#define FILE_MMAP_FLAGS (MAP_FILE | MAP_SHARED)
#define SHMEM_UNIX_MMAP
#define SHMEM_MMAP_FLAGS MAP_SHARED
#define AUTH_DBM
#define SEM_FLOCK
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define HAVE_ATEXIT
#define HAS_STATFS
#define JAVA_STATIC_LINK
#undef NEED_CRYPT_PROTO
#define NET_SOCKETS
#ifndef NO_DOMAINNAME
#define NO_DOMAINNAME
#endif

#else
#error "Missing defines in ns/netsite/include/base/systems.h"
#endif

/* Pick up the configuration symbols in the public interface */
#ifndef PUBLIC_BASE_SYSTEMS_H
#include "public/base/systems.h"
#endif /* PUBLIC_BASE_SYSTEMS_H */


/* --- Begin defaults for values not defined above --- */

#ifndef DAEMON_LISTEN_SIZE
#define DAEMON_LISTEN_SIZE 128
#define DAEMON_LISTEN_SIZE_STR "128"
#endif /* !DAEMON_LISTEN_SIZE */

#ifndef SA_HANDLER_T
#define SA_HANDLER_T(x) (void (*)()) x
#endif

#ifdef HAS_CONSTVALUED_STRFUNCS
#define CONSTVALSTRCAST (char *)
#else
#define CONSTVALSTRCAST
#endif

#ifndef THROW_HACK
#define THROW_HACK /* as nothing */
#endif


/* --- End defaults for values not defined above --- */

/* --- Begin the great debate --- */

/* NS_MAIL builds sec-key.c which calls systhread_init, which requires */
/* that USE_NSPR is defined when systhr.c is compiled.  --lachman */
/* MCC_PROXY does the same thing now --nbreslow -- LIKE HELL --ari */
#define USE_NSPR
/* XXXrobm This is UNIX-only for the moment */
#define LOG_BUFFERING
#ifdef SW_THREADS
#define THREAD_NSPR_USER
#else
#define THREAD_NSPR_KERNEL
#endif /* SW_THREADS */
#define THREAD_ANY

/* --- End the great debate --- */

#ifndef APSTUDIO_READONLY_SYMBOLS

#ifndef NSPR_PRIO_H
#include <prio.h>
#define NSPR_PRIO_H
#endif /* !NSPR_PRIO_H */

/*
 * These types have to be defined early, because they are defined
 * as (void *) in the public API.
 */

#ifndef SYS_FILE_T
typedef PRFileDesc *SYS_FILE;
#define SYS_FILE_T PRFileDesc *
#endif /* !SYS_FILE_T */

#ifndef SYS_NETFD_T
typedef PRFileDesc *SYS_NETFD;
#define SYS_NETFD_T PRFileDesc *
#endif /* !SYS_NETFD_T */

#endif /* !APSTUDIO_READONLY_SYMBOLS */

#endif /* BASE_SYSTEMS_H */
