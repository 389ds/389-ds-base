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

#if defined(AIX)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)
#define NEED_STRCASECMP
#define NEED_STRNCASECMP

#elif defined(BSDI)

#define BSD_FLOCK
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(HPUX)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
/* warning: mmap doesn't work under 9.04 */
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined (IRIX)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(NCR)
 
#define CASECMPARG_T unsigned
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(NEC)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(OSF1)

#undef BSD_FLOCK
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(SCO)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(SNI)
 
#define CASECMPARG_T const
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#if 0
#define socketpair(a,b,c,d) pipe(d)
#endif
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(SOLARIS) || defined (SOLARISx86)

#undef	FILE_UNIX	/* avoid redefinition message */
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
/* The Solaris routines return ENOSPC when too many semaphores are SEM_UNDO. */
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined (SONY)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(SUNOS4)

#define BSD_FLOCK
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(UNIXWARE) || defined(UnixWare)
 
#define CASECMPARG_T const
#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined(Linux)

#define FILE_UNIX
#define FILE_UNIX_MMAP
#define MALLOC_POOLS
#define SEM_FLOCK
#define SHMEM_UNIX_MMAP
#define ZERO(ptr,len) memset(ptr,0,len)

#elif defined (XP_WIN32)      /* Windows NT */

#include <wtypes.h>
#include <winbase.h>

typedef void* PASSWD;

#define caddr_t PCHAR
#define CASECMPARG_T const
#define FILE_WIN32
#define FILE_WIN32_MMAP
#define MALLOC_POOLS
#define NEED_STRCASECMP
#define NEED_STRNCASECMP
#define NET_WINSOCK
#define NSAPI_PUBLIC __declspec(dllexport)
/* The stat call under NT doesn't define these macros */
#ifndef S_ISDIR
#define S_ISDIR(mode)   ((mode&S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode)   ((mode&S_IFMT) == S_IFREG)                             
#endif
#ifndef S_ISLNK
#define S_ISLNK(x) (0)
#endif
#define SEM_WIN32
#define SHMEM_WIN32_MMAP
#define ZERO(ptr, len) ZeroMemory(ptr, len)

#else

#error "Missing defines in ns/netsite/include/public/base/systems.h"

#endif	/* Windows NT */

#ifndef NSPR_BEGIN_EXTERN_C
#ifdef __cplusplus
#define NSPR_BEGIN_EXTERN_C	extern "C" {
#define NSPR_END_EXTERN_C	}
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
