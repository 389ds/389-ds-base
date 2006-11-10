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

#if defined(AIX)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#define BSD_RLIMIT
#undef BSD_SIGNALS
/* AIX can handle really big shoes */
#define DAEMON_LISTEN_SIZE 4096
#define DAEMON_NEEDS_SEMAPHORE
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW|RTLD_GLOBAL
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATFS
#define HAVE_ATEXIT
#define HAVE_PW_R /* reent passwd routines */
#define HAVE_STRERROR_R
#define HAVE_STRTOK_R
#define HAVE_TIME_R 2 /* arg count */
#define HAVE_STRFTIME /* no cftime */
#define JAVA_STATIC_LINK
#undef NEED_CRYPT_H
#define NEED_SETEID_PROTO /* setegid, seteuid */
#define NEED_STRINGS_H /* for strcasecmp */
#define NET_SOCKETS
#define SA_HANDLER_T(x) (void (*)(int))x
#if OSVERSION < 4210
#define SA_NOCLDWAIT 0 /* AIX < 4.2 don't got this */
#endif /* OSVERSION < 4210 */
#define SHMEM_MMAP_FLAGS MAP_SHARED
#ifdef HW_THREADS
#define THREAD_ANY
#endif

#elif defined(BSDI)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#define BSD_MAIL
#define BSD_RLIMIT
#define BSD_SIGNALS
#define BSD_TIME
#define DAEMON_UNIX_MOBRULE
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS (MAP_FILE | MAP_SHARED)
#define HAS_STATFS
#define HAVE_ATEXIT
#undef NEED_CRYPT_PROTO
#define NET_SOCKETS
#ifndef NO_DOMAINNAME
#define NO_DOMAINNAME
#endif
#define SHMEM_MMAP_FLAGS MAP_SHARED
#define JAVA_STATIC_LINK

#elif defined(HPUX)

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
#define HAVE_STRFTIME
#define JAVA_STATIC_LINK
#undef NEED_CRYPT_H
#define NET_SOCKETS
#define SA_HANDLER_T(x) (void (*)(int))x
/* warning: mmap doesn't work under 9.04 */
#define SHMEM_MMAP_FLAGS MAP_FILE | MAP_VARIABLE | MAP_SHARED

#elif defined (IRIX)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#define BSD_RLIMIT
#undef BSD_SIGNALS
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATVFS
#define HAVE_ATEXIT
#define HAVE_STRTOK_R
#ifdef IRIX
#define HAVE_TIME_R 2 /* arg count */
#else
#define HAVE_TIME_R 3 /* arg count */
#define NEED_SETEID_PROTO /* setegid, seteuid */
#endif
#define JAVA_STATIC_LINK
#define NEED_CRYPT_H
#define NET_SOCKETS
#define SA_HANDLER_T(x) (void (*)(int))x
#define SHMEM_MMAP_FLAGS MAP_SHARED
#define THROW_HACK throw()

#elif defined(NCR)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#undef BSD_RLIMIT
/* #define DAEMON_NEEDS_SEMAPHORE */
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATVFS
#define HAVE_ATEXIT
#define HAVE_STRTOK_R
#define JAVA_STATIC_LINK
#define NEED_CRYPT_H
#define NEED_FILIO
#define NEED_GHN_PROTO
#define NET_SOCKETS
#define SHMEM_MMAP_FLAGS MAP_SHARED

#elif defined(NEC)

#define ACCELERATOR_CACHE
#define DNS_CACHE
#define AUTH_DBM
#undef BSD_RLIMIT
#define DAEMON_NEEDS_SEMAPHORE
#define DAEMON_UNIX_MOBRULE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define DLL_CAPABLE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATVFS
#define HAVE_ATEXIT
#define HAVE_STRTOK_R
#define HAVE_TIME_R 2 /* arg count */
#define JAVA_STATIC_LINK
#define NEED_CRYPT_H
#define NEED_FILIO
#define NET_SOCKETS
#define SHMEM_MMAP_FLAGS MAP_SHARED

#elif defined(OSF1)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#define BSD_RLIMIT
#undef BSD_SIGNALS
#define BSD_TIME
#define DAEMON_NEEDS_SEMAPHORE
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAVE_ATEXIT
#define HAVE_STRFTIME /* no cftime */
#define HAVE_TIME_R 2 /* ctime_r arg count */
#define NET_SOCKETS
#define SA_HANDLER_T(x) (void (*)(int))x
#define SHMEM_MMAP_FLAGS MAP_SHARED

#elif defined(SCO)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#undef BSD_RLIMIT
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
#undef NEED_CRYPT_H
#undef NEED_FILIO
#undef NEED_GHN_PROTO
#undef NEED_SETEID_PROTO /* setegid, seteuid */
#define NET_SOCKETS
#define SHMEM_MMAP_FLAGS MAP_SHARED
#define SA_HANDLER_T(x) (void (*)(int))x


#elif defined(SNI)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#undef BSD_RLIMIT
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
#define JAVA_STATIC_LINK
#define NEED_CRYPT_H
#define NEED_FILIO
#define NEED_SETEID_PROTO /* setegid, seteuid */
#define NET_SOCKETS
#define SHMEM_MMAP_FLAGS MAP_SHARED
#define USE_PIPE
/*
 * define this if your C++ platform has separate inline functions for
 * e.g. const char *strchr(const char *, char)
 *  and
 *      char *strchr(char *, char)
 * and your compiler complains about this:
 * func(const char *bla)
 * {
 *     char *fasel = strchr(bla, '.');
 * ....
 * because it says that you cannot initialize a char * with a const char *
 */
#define HAS_CONSTVALUED_STRFUNCS

/* hack for C++ platforms where bool is a keyword */
#ifndef boolean
#define boolean boolean
#endif

#elif defined(Linux)

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

#elif defined (SONY)

#define AUTH_DBM
#undef BSD_RLIMIT
#define DAEMON_NEEDS_SEMAPHORE
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAVE_ATEXIT
#define NEED_CRYPT_H
#define NEED_FILIO
#define NET_SOCKETS
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

#elif defined(UNIXWARE) || defined(UnixWare)

#define ACCELERATOR_CACHE
#define AUTH_DBM
#undef BSD_RLIMIT
#define DAEMON_UNIX_MOBRULE
#define DLL_CAPABLE
#define DLL_DLOPEN
#define DLL_DLOPEN_FLAGS RTLD_NOW
#define DNS_CACHE
#define FILE_INHERIT_FCNTL
#define FILE_MMAP_FLAGS MAP_SHARED
#define HAS_STATVFS
#define HAVE_ATEXIT
#define NEED_CRYPT_H
#define NEED_FILIO
#define NEED_GHN_PROTO
#define NEED_SETEID_PROTO /* setegid, seteuid */
#define NET_SOCKETS
#define SHMEM_MMAP_FLAGS MAP_SHARED

#ifndef boolean
#define boolean boolean
#endif

#if defined (UnixWare)
/* UnixWare but not UNIXWARE... */
#define NEED_STRINGS_H /* for strcasecmp */
#define SA_HANDLER_T(x) (void (*)(int))x
#endif

#elif defined (XP_WIN32)      /* Windows NT */

#include <wtypes.h>
#include <winbase.h>

typedef void* PASSWD;

#define ACCELERATOR_CACHE
#define AUTH_DBM
/* size has been raised to 200 with NT 4.0 server; NT 4.0 workstation is still
 * limited
 */
#define DAEMON_LISTEN_SIZE 200
#define DAEMON_WIN32
#define DLL_CAPABLE
#define DLL_WIN32
#define DNS_CACHE
#define LOG_BUFFERING
#define HAVE_STRFTIME /* no cftime */
#define NEED_CRYPT_PROTO
#define NEEDS_WRITEV
#define NET_SOCKETS
#ifndef NO_DOMAINNAME
#define NO_DOMAINNAME
#endif
#ifdef BUILD_DLL
#define NSAPI_PUBLIC __declspec(dllexport)
#else
#define NSAPI_PUBLIC
#endif /* BUILD_DLL */
#define THREAD_ANY
#define THREAD_NSPR_KERNEL
#define USE_NSPR
#define USE_STRFTIME /* no cftime */

#else

#error "Missing defines in ns/netsite/include/base/systems.h"

#endif	/* Windows NT */

/* Pick up the configuration symbols in the public interface */
#ifndef PUBLIC_BASE_SYSTEMS_H
#include "public/base/systems.h"
#endif /* PUBLIC_BASE_SYSTEMS_H */


/* --- Begin defaults for values not defined above --- */

#ifndef DAEMON_LISTEN_SIZE
#define DAEMON_LISTEN_SIZE 128
#endif /* !DAEMON_LISTEN_SIZE */

#ifndef SA_HANDLER_T
#define SA_HANDLER_T(x) (void (*)())x 
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
#if defined(XP_UNIX)
#define USE_NSPR
/* XXXrobm This is UNIX-only for the moment */
#define LOG_BUFFERING
#ifdef SW_THREADS
#define THREAD_NSPR_USER
#else
#define THREAD_NSPR_KERNEL
#ifdef IRIX 
#undef SEM_FLOCK
#define SEM_IRIX
#endif /* IRIX */
#endif /* SW_THREADS */
#define THREAD_ANY
#endif /* XP_UNIX */

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

#ifdef SEM_WIN32

typedef HANDLE SEMAPHORE;
#define SEMAPHORE_T HANDLE
#define SEM_ERROR NULL
/* That oughta hold them (I hope) */
#define SEM_MAXVALUE 32767

#elif defined(SEM_IRIX)

#ifndef OS_ULOCKS_H
#include <ulocks.h>
#define OS_ULOCKS_H
#endif /* !OS_ULOCKS_H */

typedef struct {
    usptr_t *arena;
    usema_t *sem;
} semirix_s;
typedef semirix_s* SEMAPHORE;
#define SEMAPHORE_T semirix_s *
#define SEM_ERROR NULL

#elif defined(SEM_FLOCK)

#define SEMAPHORE_T SYS_FILE
typedef SYS_FILE SEMAPHORE;
#define SEM_ERROR NULL

#else /* ! SEM_WIN32, !SEM_IRIX */

typedef int SEMAPHORE;
#define SEMAPHORE_T int
#define SEM_ERROR -1

#endif /* SEM_WIN32 */

#endif /* !APSTUDIO_READONLY_SYMBOLS */

#endif /* BASE_SYSTEMS_H */
