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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * fsmutex: Mutexes that are filesystem-based so they're available from more
 * than one process and address space
 * 
 * Rob McCool
 */

#include "base/fsmutex.h"
#ifdef THREAD_ANY
#include "base/crit.h"
#include "base/systhr.h"
#endif

#include "base/util.h"
#ifdef XP_WIN32
typedef HANDLE sys_fsmutex_t;
#endif

#ifdef XP_UNIX
#include "base/file.h"
typedef SYS_FILE sys_fsmutex_t;
#endif


typedef struct {
    sys_fsmutex_t mutex;
    char *id;
#ifdef THREAD_ANY
    CRITICAL crit;
#endif
    int flags;
} fsmutex_s;



/* ----------------------------- fsmutex_init ----------------------------- */


#ifdef XP_UNIX
static int 
_fsmutex_create(fsmutex_s *fsm, char *name, int number)
{
    char tn[256];
    SYS_FILE lfd;
    int visible = (fsm->flags & FSMUTEX_VISIBLE ? 1 : 0);

    util_snprintf(tn, 256, "/tmp/%s.%d", name, number);
    if(!visible)
        unlink(tn);
    if( (lfd = PR_Open(tn, PR_RDWR|PR_CREATE_FILE, 0644)) == NULL)
        return -1;

    if(!visible)
        unlink(tn);
    else
        fsm->id = PERM_STRDUP(tn);
    fsm->mutex = lfd;
    return 0;
}
#endif

#ifdef XP_WIN32
static int 
_fsmutex_create(fsmutex_s *fsm, char *name, int number)
{
    char tn[256];
    util_snprintf(tn, sizeof(tn), "%s.%d", name, number);

    fsm->id = NULL;
    fsm->mutex = CreateMutex(NULL, FALSE, 
                             (fsm->flags & FSMUTEX_VISIBLE ? tn : NULL));
    return (fsm->mutex ? 0 : -1);
}
#endif

NSAPI_PUBLIC FSMUTEX
fsmutex_init(char *name, int number, int flags)
{
    fsmutex_s *ret = (fsmutex_s *) PERM_MALLOC(sizeof(fsmutex_s));

    ret->flags = flags;
    if(_fsmutex_create(ret, name, number) == -1) {
        PERM_FREE(ret);
        return NULL;
    }
#ifdef THREAD_ANY
    if(flags & FSMUTEX_NEEDCRIT)
        ret->crit = crit_init();
#endif
    return (FSMUTEX) ret;
}

#ifdef XP_UNIX
NSAPI_PUBLIC void 
fsmutex_setowner(FSMUTEX fsm, uid_t uid, gid_t gid)
{
    if(!geteuid())
        (void) chown( ((fsmutex_s *)fsm)->id, uid, gid);
}
#endif


/* -------------------------- fsmutex_terminate --------------------------- */


#ifdef XP_UNIX
static void 
_fsmutex_delete(fsmutex_s *fsm)
{
    if(fsm->flags & FSMUTEX_VISIBLE)
        unlink(fsm->id);
    PERM_FREE(fsm->id);
    PR_Close(fsm->mutex);
}
#endif

#ifdef XP_WIN32
static void 
_fsmutex_delete(fsmutex_s *fsm)
{
    CloseHandle(fsm->mutex);
}
#endif

NSAPI_PUBLIC void
fsmutex_terminate(FSMUTEX id)
{
    fsmutex_s *fsm = (fsmutex_s *) id;

    _fsmutex_delete(fsm);
#ifdef THREAD_ANY
    if(fsm->flags & FSMUTEX_NEEDCRIT)
        crit_terminate(fsm->crit);
#endif
    PERM_FREE(fsm);
}


/* ----------------------------- fsmutex_lock ----------------------------- */


NSAPI_PUBLIC void
fsmutex_lock(FSMUTEX id)
{
    fsmutex_s *fsm = (fsmutex_s *) id;
#ifdef THREAD_ANY
    if(fsm->flags & FSMUTEX_NEEDCRIT)
        crit_enter(fsm->crit);
#endif
#ifdef XP_UNIX
#ifdef THREAD_NSPR_USER
    /* Poll to avoid blocking. XXXrobm If errno is wrong this may go awry. */
    while(system_tlock(fsm->mutex) == -1)
        systhread_sleep(1000);
#else
    system_flock(fsm->mutex );
#endif
#endif
#ifdef XP_WIN32
    WaitForSingleObject(fsm->mutex, INFINITE);
#endif
}


/* ---------------------------- fsmutex_unlock ---------------------------- */


NSAPI_PUBLIC void
fsmutex_unlock(FSMUTEX id)
{
    fsmutex_s *fsm = (fsmutex_s *) id;
#ifdef XP_UNIX
    system_ulock(fsm->mutex);
#endif
#ifdef XP_WIN32
    ReleaseMutex(fsm->mutex);
#endif
#ifdef THREAD_ANY
    if(fsm->flags & FSMUTEX_NEEDCRIT)
        crit_exit(fsm->crit);
#endif
}
