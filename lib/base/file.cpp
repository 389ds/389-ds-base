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
#ifdef XP_WIN32
#include <time.h>  /* time */
#include <sys/stat.h> /* stat */
#include <errno.h>
#include <direct.h>
#include <base/nterr.h>
/* Removed for ns security integration
#include <xp_error.h>
*/
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
#ifdef XP_WIN32

    int chop, l;

/* The NT stat is very peculiar about directory names. */
/* XXX aruna - there is a bug here, maybe in the C runtime.
 * Stating the same path in a separate program succeeds. From
 * jblack's profiling, this needs to be replaced by the Win32
 * calls anyway.*/

    l = strlen(path);
    if((path[l - 1] == '/') && 
       (!(isalpha(path[0]) && (!strcmp(&path[1], ":/")))))
    {
        chop = 1;
        path[--l] = '\0';
    }
    else chop = 0;
#endif /* XP_WIN32 */

#ifdef XP_UNIX
    if(stat(path, finfo) == -1)
        return -1;
#else /* XP_WIN32 */

    if(_stat(path, (struct _stat *)finfo) == -1) {
        /* XXXMB - this sucks; 
         * try to convert to an error code we'll expect...
         */
        switch(errno) {
            case ENOENT: PR_SetError(PR_FILE_NOT_FOUND_ERROR, errno); break;
            default: PR_SetError(PR_UNKNOWN_ERROR, errno); break;
        }
        return -1;
    }

    /* NT sets the time fields to -1 if it thinks that the file
     * is a device ( like com1.html, lpt1.html etc)	In this case
     * simply set last modified time to the current time....
     */

    if (finfo->st_mtime == -1) {
        finfo->st_mtime = time(NULL);
    }
    if (finfo->st_atime == -1) {
        finfo->st_atime = 0;
    }
    if (finfo->st_ctime == -1) {
        finfo->st_ctime = 0;
    }
    if(chop)
        path[l++] = '/';

#endif /* XP_WIN32 */

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

#ifdef XP_UNIX

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
#if defined(SNI)
/* C++ compiler seems to find more that one overloaded instance of exit() ?! */
#define EXITFUNC ::exit
#else
#define EXITFUNC exit
#endif
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
#endif /* XP_UNIX */

/* --------------------------- file_setinherit ---------------------------- */

NSAPI_PUBLIC int file_setinherit(SYS_FILE fd, int value)
{
#if defined(XP_WIN32)
    int ret;

//    ret = SetHandleInformation((HANDLE)PR_FileDesc2NativeHandle(fd), 0, value?HANDLE_FLAG_INHERIT:0);
	// This function did nothing before since the mask was set to 0.
    ret = SetHandleInformation((HANDLE)PR_FileDesc2NativeHandle(fd), HANDLE_FLAG_INHERIT, value?HANDLE_FLAG_INHERIT:0);
    return ret==0?-1:0;
#elif defined(XP_UNIX)
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
#endif
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


#ifdef FILE_WIN32

int CgiBuffering;

NSAPI_PUBLIC SYS_FILE system_fopen(char *path, int access, int flags)
{
    char p2[MAX_PATH];
    SYS_FILE ret;
    HANDLE fd;

	if (strlen(path) >= MAX_PATH) {
		return SYS_ERROR_FD;
	}

    file_unix2local(path, p2);

    fd = CreateFile(p2, access, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                    NULL, flags, 0, NULL);
    ret = PR_ImportFile((int32)fd);

    if(ret == INVALID_HANDLE_VALUE)
        return SYS_ERROR_FD;

    return ret;
}



NSAPI_PUBLIC int system_pread(SYS_FILE fd, char *buf, int BytesToRead) {
    unsigned long BytesRead = 0;
    int result = 0;
    BOOLEAN TimeoutSet = FALSE;

    /* XXXMB - nspr20 should be able to do this; but right now it doesn't
     * return proper error info.
     * fix it later...
     */
    if(ReadFile((HANDLE)PR_FileDesc2NativeHandle(fd), (LPVOID)buf, BytesToRead, &BytesRead, NULL) == FALSE) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            return IO_EOF;
        } else {
            return IO_ERROR;
        }
    }
    return (BytesRead ? BytesRead : IO_EOF);
}

NSAPI_PUBLIC int system_pwrite(SYS_FILE fd, char *buf, int BytesToWrite) 
{
    unsigned long BytesWritten;
    
    if (WriteFile((HANDLE)PR_FileDesc2NativeHandle(fd), (LPVOID)buf, 
                  BytesToWrite, &BytesWritten, NULL) == FALSE) {
        return IO_ERROR;
    }
    return BytesWritten;
}


NSAPI_PUBLIC int system_fwrite_atomic(SYS_FILE fd, char *buf, int sz) 
{
    int ret;

#if 0
    if(system_flock(fd) == IO_ERROR)
        return IO_ERROR;
#endif
    /* XXXMB - this is technically thread unsafe, but it catches any 
     * callers of fwrite_atomic when we're single threaded and just coming
     * to life.
     */
    if (!_atomic_write_lock) {
        _atomic_write_lock = PR_NewLock();
    }
    PR_Lock(_atomic_write_lock);
    ret = system_fwrite(fd,buf,sz);
    PR_Unlock(_atomic_write_lock);
#if 0
     if(system_ulock(fd) == IO_ERROR)
        return IO_ERROR;
#endif
    return ret;
}


NSAPI_PUBLIC void file_unix2local(char *path, char *p2)
{
    /* Try to handle UNIX-style paths */
    if((!strchr(path, FILE_PATHSEP))) {
        int x;

        for(x = 0; path[x]; x++)
            p2[x] = (path[x] == '/' ? '\\' : path[x]);
        p2[x] = '\0';
    }
    else
        strcpy(p2, path);
}


NSAPI_PUBLIC int system_nocoredumps(void)
{
    return 0;
}

/* --------------------------- system_winerr ------------------------------ */


#include <winsock.h>
#include <errno.h>
#include "util.h"

NSAPI_PUBLIC char *system_winsockerr(void)
{
	int errn = WSAGetLastError();

	return FindError(errn);
}

NSAPI_PUBLIC char *system_winerr(void)
{
	int errn = GetLastError();

	if (errn == 0)
		errn = WSAGetLastError();
	return FindError(errn);
}

/* ------------------------- Dir related stuff ---------------------------- */


NSAPI_PUBLIC SYS_DIR dir_open(char *pathp)
{
    dir_s *ret = (dir_s *) MALLOC(sizeof(dir_s));
    char path[MAX_PATH];
    int l;

	if (strlen(pathp) >= MAX_PATH) {
		return NULL;
	}

    l = util_sprintf(path, "%s", pathp) - 1;
	path[strlen(pathp)] = '\0';
    if(path[strlen(path) - 1] != FILE_PATHSEP)
        	strcpy (path + strlen(path), "\\*.*");
	else
		util_sprintf(path, "%s*.*", path);

    ret->de.d_name = NULL;
    if( (ret->dp = FindFirstFile(path, &ret->fdata)) != INVALID_HANDLE_VALUE)
        return ret;
    FREE(ret);
    return NULL;
}

NSAPI_PUBLIC SYS_DIRENT *dir_read(SYS_DIR ds)
{
    if(FindNextFile(ds->dp, &ds->fdata) == FALSE)
        return NULL;
    if(ds->de.d_name)
        FREE(ds->de.d_name);
    ds->de.d_name = STRDUP(ds->fdata.cFileName);

    return &ds->de;
}

NSAPI_PUBLIC void dir_close(SYS_DIR ds)
{
    FindClose(ds->dp);
    if(ds->de.d_name)
        FREE(ds->de.d_name);
    FREE(ds);
}

#endif /* FILE_WIN32 */

NSAPI_PUBLIC int file_notfound(void)
{
#ifdef FILE_WIN32
    int errn = PR_GetError();
    return (errn == PR_FILE_NOT_FOUND_ERROR);
#else
    return (errno == ENOENT);
#endif
}

NSAPI_PUBLIC int dir_create_all(char *dir)
{
    struct stat fi;
    char *t;

#ifdef XP_WIN32
    t = dir + 3;
#else /* XP_UNIX */
    t = dir + 1;
#endif
    while(1) {
        t = strchr(t, FILE_PATHSEP);
        if(t) *t = '\0';
        if(stat(dir, &fi) == -1) {
            if(dir_create(dir) == -1)
                return -1;
        }
        if(t) *t++ = FILE_PATHSEP;
        else break;
    }
    return 0;
}


#ifdef XP_UNIX
#if !defined(SNI) && !defined(LINUX)
extern char *sys_errlist[];
#endif /* SNI */
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
#ifdef XP_WIN32
        HashNtErrors();
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
    int sys_error = 0;
    PRErrorCode nscp_error;
#ifdef XP_WIN32
    LPTSTR sysmsg = 0;
#endif


    /* Grab the OS error message */
#ifdef XP_WIN32
    sys_error = GetLastError();
#else
    sys_error = errno;
#endif
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
#if defined(XP_WIN32)
        msglen = FormatMessage(
                    FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER,
                    NULL, 
                    GetLastError(), 
                    LOCALE_SYSTEM_DEFAULT, 
                    (LPTSTR)&sysmsg, 
                    0, 
                    0);
        if (msglen > 0)
            lmsg = sysmsg;
        else
            lmsg = system_winerr();
        SetLastError(0);
#else
/* replaced
#if defined(SNI) || defined(LINUX)
	/ C++ platform has no definition for sys_errlist /
	lmsg = strerror(errno);
#else
	lmsg = sys_errlist[errno];
#endif
with lmsg =strerror(errno);*/
	lmsg=strerror(errno);
        errno = 0;
#endif
    }

    /* At this point lmsg points to something. */
    msglen = strlen(lmsg);

    if (*buff == NULL)
        *buff = STRDUP(lmsg);
    else if (maxlen > msglen)
        memcpy(*buff, lmsg, msglen+1);
    else
        msglen = 0;

#ifdef XP_WIN32
    /* NT's FormatMessage() dynamically allocated the msg; free it */
    if (sysmsg)
        LocalFree(sysmsg);
#endif

    return msglen;
}

NSAPI_PUBLIC char *
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
        buff = "Could not retrieve system error message";
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
