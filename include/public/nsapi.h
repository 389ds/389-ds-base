/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef PUBLIC_NSAPI_H
#define PUBLIC_NSAPI_H

/*
 * File:        nsapi.h
 *
 * Description:
 *
 *      This file defines an interface for extending the server with
 *      in-process plug-ins.
 */

#include "base/systems.h"

#if defined(FILE_UNIX_MMAP) || defined(FILE_WIN32_MMAP)
#define FILE_MMAP
#endif

/* --- Begin miscellaneous definitions --- */

/* Used in some places as a length limit on error messages */
#define MAGNUS_ERROR_LEN 1024

#ifdef XP_WIN32
#define ENDLINE "\r\n"
#else
#define ENDLINE "\n"
#endif

/*
 * The maximum length of an error message. NOT RUN-TIME CHECKED
 */

#define MAX_ERROR_LEN 1024

/* A warning is a minor mishap, such as a 404 being issued. */
#define LOG_WARN 0

/* 
 * A misconfig is when there is a syntax error or permission violation in
 * a config. file.
 */
#define LOG_MISCONFIG 1

/* 
 * Security warnings are issued when authentication fails, or a host is
 * given a 403 return code.
 */
#define LOG_SECURITY 2

/*
 * A failure is when a request could not be fulfilled due to an internal
 * problem, such as a CGI script exiting prematurely, or a filesystem 
 * permissions problem.
 */
#define LOG_FAILURE 3

/*
 * A catastrophe is a fatal server error such as running out of
 * memory or processes, or a system call failing, or even a server crash. 
 * The server child cannot recover from a catastrophe.
 */
#define LOG_CATASTROPHE 4

/*
 * Informational message, of no concern.
 */
#define LOG_INFORM 5

/*
 * Internal log messages to be logged.  Internal use only.
 * Enable with "LogVerbose on" in magnus.conf
 */
#define LOG_VERBOSE 6

/*
 * The time format to use in the error log
 */

#define ERR_TIMEFMT "[%d/%b/%Y:%H:%M:%S]"


/* The fd you will get if you are reporting errors to SYSLOG */

#define ERRORS_TO_SYSLOG -1

/* Return codes from file I/O routines */
#define IO_OKAY 1
#define IO_ERROR -1
#define IO_EOF 0

/* The disk page size on this machine. */
#define FILE_BUFFERSIZE 4096

#ifdef XP_UNIX

#define FILE_PATHSEP '/'
#define FILE_PARENT "../"

#elif defined(XP_WIN32)

#define FILE_PATHSEP '/'
#define FILE_PARENT "..\\"

#endif /* XP_WIN32 */

/* WILDPAT uses shell expressions */
#define WILDPAT_VALID(exp)              shexp_valid(exp)
#define WILDPAT_MATCH(str, exp)         shexp_match(str, exp)
#define WILDPAT_CMP(str, exp)           shexp_cmp(str, exp)
#define WILDPAT_CASECMP(str, exp)       shexp_casecmp(str, exp)
#define WILDPAT_USES_SHEXP              1

/* Define return codes from WILDPAT_VALID */
#define NON_WILDPAT     -1              /* exp is ordinary string */
#define INVALID_WILDPAT -2              /* exp is an invalid pattern */
#define VALID_WILDPAT   1               /* exp is a valid pattern */

//#endif /* USE_REGEX */

/* Define return codes from regexp_valid and shexp_valid */
#define NON_SXP         NON_WILDPAT     /* exp is an ordinary string */
#define INVALID_SXP     INVALID_WILDPAT /* exp is an invalid shell exp */
#define VALID_SXP       VALID_WILDPAT   /* exp is a valid shell exp */

#define SYSTHREAD_DEFAULT_PRIORITY 16

/* --- Begin native platform includes --- */

#if defined(FILE_UNIX) || defined(FILE_UNIX_MMAP)
#include <sys/types.h>                  /* caddr_t */
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef FILE_WIN32
#include <direct.h>
#endif /* FILE_WIN32 */

#ifdef NET_WINSOCK
#include <winsock.h>
struct iovec {
	char		*iov_base;
    unsigned	iov_len;
};
#else
#if !defined(SUNOS4) && !defined(HPUX) && !defined(LINUX)
#include <sys/select.h>
#endif
#include <sys/time.h>    /* struct timeval */
#include <sys/socket.h>
#include <netinet/in.h> /* sockaddr and in_addr */
#include <sys/uio.h>
#endif /* NET_WINSOCK */

#include <sys/stat.h>

#include <ctype.h>  /* isspace */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef XP_UNIX
#include <dirent.h>
#include <pwd.h>                /* struct passwd */
#endif /* XP_UNIX */
#include "libadminutil/psetc.h"

/* --- End native platform includes --- */

/* --- Begin type definitions --- */

#ifndef SYS_FILE_T
typedef void *SYS_FILE;
#define SYS_FILE_T void *
#endif /* !SYS_FILE_T */

#define SYS_ERROR_FD ((SYS_FILE)-1)

typedef void* CONDVAR;
typedef void *COUNTING_SEMAPHORE;
typedef void* CRITICAL;

#ifdef XP_UNIX
typedef DIR* SYS_DIR;
typedef struct dirent SYS_DIRENT;
#endif /* XP_UNIX */

#ifdef XP_WIN32

typedef struct {
    char *d_name;
} dirent_s;

typedef struct {
    HANDLE dp;
    WIN32_FIND_DATA fdata;
    dirent_s de;
} dir_s;

typedef dir_s* SYS_DIR;
typedef dirent_s SYS_DIRENT;

#endif /* XP_WIN32 */

typedef struct {
    char *name,*value;
} pb_param;

struct pb_entry {
    pb_param *param;
    struct pb_entry *next;
};

typedef struct {
    int hsize;
    struct pb_entry **ht;
} pblock;

#ifndef POOL_HANDLE_T
#define POOL_HANDLE_T
typedef void *pool_handle_t;
#endif

typedef struct PListStruct_s PListStruct_s;
typedef struct ACLListHandle ACLListHandle;

/* Define a handle for a thread */
typedef void* SYS_THREAD;

/* Define an error value for the thread handle */
#define SYS_THREAD_ERROR NULL

/*
 * Hierarchy of httpd_object
 *
 * An object contains dtables. 
 * 
 * Each dtable is a table of directives that were entered of a certain type.
 * There is one dtable for each unique type of directive.
 *
 * Each dtable contains an array of directives, each of which is equivalent
 * to one directive that occurred in a config. file.
 *
 * It is up to the caller to determine how many dtables will be allocated
 * and to keep track of which of their directive types maps to which dtable
 * number.
 */


/*
 * directive is a structure containing the protection and parameters to an
 * instance of a directive within an httpd_object.
 *
 * param is the parameters, client is the protection.
 */

typedef struct {
    pblock *param;
    pblock *client;
} directive;

/* --- End type definitions --- */

#ifdef NEED_STRCASECMP
#define strcasecmp(s1, s2) util_strcasecmp(s1, s2)
#endif /* NEED_STRCASECMP */

#ifdef NEED_STRNCASECMP
#define strncasecmp(s1, s2, n) util_strncasecmp(s1, s2, n)
#endif /* NEED_STRNCASECMP */

#ifdef XP_UNIX
#define dir_open opendir
#define dir_read readdir
#define dir_close closedir
#define dir_create(path) mkdir(path, 0755)
#define dir_remove rmdir
#define system_chdir chdir
#define file_unix2local(path,p2) strcpy(p2,path)
#endif /* XP_UNIX */

#ifdef XP_WIN32
#define dir_create _mkdir
#define dir_remove _rmdir
#define system_chdir SetCurrentDirectory
#endif /* XP_WIN32 */

/*
 * Thread-safe variant of localtime
 */
#define system_localtime(curtime, ret) util_localtime(curtime, ret)

#endif /* !PUBLIC_NSAPI_H */
