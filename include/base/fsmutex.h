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

/*
 * fsmutex: Mutexes that are filesystem-based so they're available from more
 * than one process and address space
 * 
 * Rob McCool
 */


#ifndef FSMUTEX_H
#define FSMUTEX_H

#include "netsite.h"

typedef void * FSMUTEX;


/* ------------------------------ Prototypes ------------------------------ */

NSPR_BEGIN_EXTERN_C

/* 
   Flags to fsmutex_init. 

   FSMUTEX_VISIBLE makes a filesystem mutex which can be opened by other
   programs or processes.

   FSMUTEX_NEEDCRIT specifies that the fsmutex_lock and fsmutex_unlock 
   functions should also use a critical section to ensure that more than
   one thread does not acquire the mutex at a time. If this flag is not 
   specified, it is up to the caller to ensure that only thread within a 
   process tries to acquire the lock at any given time.
 */
#define FSMUTEX_VISIBLE 0x01
#define FSMUTEX_NEEDCRIT 0x02


/*
   fsmutex_init creates a new filesystem-based mutex. The resulting mutex
   is part of the filesystem. The name and number parameters are used to
   create a name for the mutex. If the FSMUTEX_VISIBLE flag is specified, 
   the mutex will be left in the filesystem for other programs and processes
   to access. If a mutex with the given name/number combination already
   exists, the calling process is allowed access to it. If the mutex does
   not already exist, the mutex is created.

   Returns NULL on failure, a void pointer to a fsmutex structure otherwise.
   This fsmutex structure is local to the current process.
 */
NSAPI_PUBLIC FSMUTEX fsmutex_init(char *name, int number, int flags);

/* 
   Sets the ownership of the underlying filesystem object to the given
   uid and gid. Only effective if the server is running as root.
 */
#ifdef XP_UNIX
#include <unistd.h>
#ifdef __sony
#include <sys/types.h>
#endif
NSAPI_PUBLIC void fsmutex_setowner(FSMUTEX fsm, uid_t uid, gid_t gid);
#endif



/*
   fsmutex_terminate deletes a filesystem-based mutex. A mutex will only
   be deleted when every process which has an open pointer to the mutex 
   calls this function.
 */
NSAPI_PUBLIC void fsmutex_terminate(FSMUTEX id);

/*
   fsmutex_lock attempts to acquire the given filesystem-based mutex. If 
   another process is holding the mutex, or if the FSMUTEX_NEEDCRIT flag
   was passed to fsmutex_init and another thread in the current process is 
   holding the mutex, then the calling thread will block until the mutex
   is available.
 */
NSAPI_PUBLIC void fsmutex_lock(FSMUTEX id);

/*
   fsmutex_unlock releases a filesystem-based mutex previously acquired
   by fsmutex_lock.
 */
NSAPI_PUBLIC void fsmutex_unlock(FSMUTEX id);

NSPR_END_EXTERN_C

#endif
