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
 * file.c: system specific functions for reading/writing files
 * 
 * See file.h for formal definitions of what these functions do
 *
 * Rob McCool
 */


#include "base/file.h"
#ifdef BSD_RLIMIT
#include <sys/time.h>
#include <sys/resource.h>
#else
#include <stdlib.h>
#include <signal.h>
#endif
#include <prerror.h>
#include "private/pprio.h"
#include "prlock.h"

extern "C" char *nscperror_lookup(int err);

/* --- globals -------------------------------------------------------------*/
/* PRFileDesc * SYS_ERROR_FD = NULL; */

const int errbuf_size = 256;
const unsigned int LOCKFILERANGE=0x7FFFFFFF;
PRLock *_atomic_write_lock = NULL;

/* --------------------------------- stat --------------------------------- */


   /* XXXMB - Can't convert to PR_GetFileInfo because we directly exported
    * the stat interface... Damn.
    */
NSAPI_PUBLIC int system_stat(char *path, struct stat *finfo)
{
    if(stat(path, finfo) == -1)
        return -1;

    if(S_ISREG(finfo->st_mode) && (path[strlen(path) - 1] == '/')) {
        /* File with trailing slash */
        errno = ENOENT;
        return -1;
    }
    return 0;
}


NSAPI_PUBLIC int system_fread(SYS_FILE fd, char *buf, int sz) 
{
    /* XXXMB - this is the *one* function which does return a length
     * instead of the IO_ERROR/IO_OKAY.
     */
    return PR_Read(fd, buf, sz);
}

NSAPI_PUBLIC int system_fwrite(SYS_FILE fd, char *buf, int sz) {
    int n,o,w;

    for(n=sz,o=0; n; n-=w,o+=w) {
        if((w = PR_Write(fd, &buf[o], n)) < 0)
            return IO_ERROR;
    }
    return IO_OKAY;
}

/* ---------------------------- Standard UNIX ----------------------------- */


#include <sys/file.h>   /* flock */

NSAPI_PUBLIC int system_fwrite_atomic(SYS_FILE fd, char *buf, int sz) 
{
    int ret;
#if 0
    if(flock(fd,LOCK_EX) == -1)
        return IO_ERROR;
#endif
    ret = system_fwrite(fd,buf,sz);
#if 0
    if(flock(fd,LOCK_UN) == -1)
        return IO_ERROR;  /* ??? */
#endif
    return ret;
}

/* -------------------------- system_nocoredumps -------------------------- */


NSAPI_PUBLIC int system_nocoredumps(void)
{
#ifdef BSD_RLIMIT
    struct rlimit rl;

    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    return setrlimit(RLIMIT_CORE, &rl);
#else
#define EXITFUNC exit
    signal(SIGQUIT, EXITFUNC);
    signal(SIGILL, EXITFUNC);
    signal(SIGTRAP, EXITFUNC);
    signal(SIGABRT, EXITFUNC);
    signal(SIGIOT, EXITFUNC);
    signal(SIGEMT, EXITFUNC);
    signal(SIGFPE, EXITFUNC);
    signal(SIGBUS, EXITFUNC);
    signal(SIGSEGV, EXITFUNC);
    signal(SIGSYS, EXITFUNC);


    return 0;
#endif
}

/* --------------------------- file_setinherit ---------------------------- */

NSAPI_PUBLIC int file_setinherit(SYS_FILE fd, int value)
{
    int flags = 0;
    PRInt32 nativeFD;
    PRFileDesc *bottom = fd;

    while (bottom->lower != NULL) {
      bottom = bottom->lower;
    }

    nativeFD = PR_FileDesc2NativeHandle(bottom);
#if 0
fprintf(stderr, "\nInfo(file_setinherit): Native file descriptor is %d\n", nativeFD);
#endif
    flags = fcntl(nativeFD, F_GETFD, 0);
    if(flags == -1)
        return -1;
    if(value)
        flags &= (~FD_CLOEXEC);
    else
        flags |= FD_CLOEXEC;
    fcntl(nativeFD, F_SETFD, flags);
    return 0;
    
    /* Comment out for ns security/ nspr integration (HACK for NOW)
    int flags = fcntl(PR_FileDesc2NativeHandle(fd), F_GETFD, 0);
    if(flags == -1)
        return -1;
    if(value)
        flags &= (~FD_CLOEXEC);
    else
        flags |= FD_CLOEXEC;
    fcntl(PR_FileDesc2NativeHandle(fd), F_SETFD, flags);
    return 0;
    */
}

NSAPI_PUBLIC SYS_FILE system_fopenRO(char *p)
{
    SYS_FILE f = PR_Open(p, PR_RDONLY, 0);

    if (!f)
        return SYS_ERROR_FD;
    return f;
}

NSAPI_PUBLIC SYS_FILE system_fopenWA(char *p)
{
    SYS_FILE f = PR_Open(p, PR_RDWR|PR_CREATE_FILE|PR_APPEND, 0644);

    if (!f)
        return SYS_ERROR_FD;
    return f;
}

NSAPI_PUBLIC SYS_FILE system_fopenRW(char *p)
{
    SYS_FILE f = PR_Open(p, PR_RDWR|PR_CREATE_FILE, 0644);

    if (!f)
        return SYS_ERROR_FD;
    return f;
}

NSAPI_PUBLIC SYS_FILE system_fopenWT(char *p)
{
    SYS_FILE f = PR_Open(p, PR_RDWR|PR_CREATE_FILE|PR_TRUNCATE, 0644);

    if (!f)
        return SYS_ERROR_FD;
    return f;
}

NSAPI_PUBLIC int system_fclose(SYS_FILE fd)
{
    return (PR_Close(fd));
}


NSAPI_PUBLIC int file_notfound(void)
{
    return (errno == ENOENT);
}

#if !defined(LINUX)
extern char *sys_errlist[];
#endif

#define ERRMSG_SIZE 35
#ifdef THREAD_ANY
static int errmsg_key = -1;
#include "systhr.h"
/* Removed for ns security integration
#include "xp_error.h"
*/
#else /* THREAD_ANY */
static char errmsg[ERRMSG_SIZE];
#endif /* THREAD_ANY */

#include "util.h"

void system_errmsg_init(void)
{
    if (errmsg_key == -1) {
#if defined(THREAD_ANY)
        errmsg_key = systhread_newkey();
#endif
        if (!_atomic_write_lock)
            _atomic_write_lock = PR_NewLock();
    }
}

NSAPI_PUBLIC int system_errmsg_fn(char **buff, size_t maxlen)
{
    char static_error[128];
    char *lmsg = 0; /* Local message pointer */
    size_t msglen = 0;
    PRErrorCode nscp_error;

    nscp_error = PR_GetError();

    /* If there is a NSPR error, but it is "unknown", try to get the OSError
     * and use that instead.
     */
    if (nscp_error == PR_UNKNOWN_ERROR)
        errno = PR_GetOSError();

    if (nscp_error != 0 && nscp_error != PR_UNKNOWN_ERROR){
        char *nscp_error_msg;

        nscp_error_msg = nscperror_lookup(nscp_error);
        if(nscp_error_msg){
            PR_SetError(0, 0);
            lmsg = nscp_error_msg;
        } else {
            util_snprintf(static_error, sizeof(static_error), "unknown error %d", nscp_error);
            lmsg = static_error;
        }
    } else {
        lmsg=strerror(errno);
        errno = 0;
    }

    /* At this point lmsg points to something. */
    msglen = strlen(lmsg);

    if (*buff == NULL)
        *buff = STRDUP(lmsg);
    else if (maxlen > msglen)
        memcpy(*buff, lmsg, msglen+1);
    else
        msglen = 0;

    return msglen;
}

NSAPI_PUBLIC const char *
system_errmsg(void)
{
    char *buff = 0;

    if (errmsg_key == -1)
        return "unknown early startup error";

    // rmaxwell - This is extremely lame.
    // Allocate a buffer in thread local storage to 
    // hold the last error message.
    // The whole error message facility is broken and should be
    // updated to get error strings out of the code.
    if(!(buff = (char *) systhread_getdata(errmsg_key))) {
        buff = (char *) PERM_MALLOC(errbuf_size);
        systhread_setdata(errmsg_key, (void *)buff);
    }
    system_errmsg_fn(&buff, errbuf_size);
    if (buff == 0)
        return "Could not retrieve system error message";
    return buff;
}

NSAPI_PUBLIC int
system_rename(char *oldpath, char *newpath)
{
    return rename(oldpath, newpath);
}

NSAPI_PUBLIC int
system_unlink(char *path)
{
    return PR_Delete(path)==PR_FAILURE?-1:0;
}

NSAPI_PUBLIC int system_lseek(SYS_FILE fd, int off, int wh)
{
  switch (wh) {
  case 0:
    return PR_Seek(fd, off, PR_SEEK_SET);
    break;
  case 1:
    return PR_Seek(fd, off, PR_SEEK_CUR);
    break;
  case 2:
    return PR_Seek(fd, off, PR_SEEK_END);
    break;
  default:
    return  -1;
  }
}

NSAPI_PUBLIC int
system_tlock(SYS_FILE fd)
{
    return PR_TLockFile(fd);
}

NSAPI_PUBLIC int
system_flock(SYS_FILE fd)
{
    return PR_LockFile(fd);
}

NSAPI_PUBLIC int 
system_ulock(SYS_FILE fd)
{
    return PR_UnlockFile(fd);
}
