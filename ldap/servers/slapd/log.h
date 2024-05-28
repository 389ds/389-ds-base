/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2022-2024 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/***********************************************************************
 * log.h
 *
 * structures related to logging facility.
 *
 *************************************************************************/
#include <stdio.h>
#ifdef LINUX
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE /* glibc2 needs this */
#endif
#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif
#endif
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef LINUX
#include <sys/statfs.h>
#else
#include <sys/statvfs.h>
#endif
#include <fcntl.h>
#include "prio.h"
#include "prprf.h"
#include "slap.h"
#include "slapi-plugin.h"

/* Use the syslog level names (prioritynames) */
#define SYSLOG_NAMES 1

#define LOG_MB_IN_BYTES (1024 * 1024)

#define LOG_SUCCESS            0 /* fine & dandy */
#define LOG_CONTINUE           LOG_SUCCESS
#define LOG_ERROR              1 /* default error case */
#define LOG_EXCEEDED           2 /* err: > max logs allowed */
#define LOG_ROTATE             3 /* ok; go to the next log */
#define LOG_UNABLE_TO_OPENFILE 4
#define LOG_DONE               5

#define LOG_UNIT_UNKNOWN 0
#define LOG_UNIT_MONTHS  1
#define LOG_UNIT_WEEKS   2
#define LOG_UNIT_DAYS    3
#define LOG_UNIT_HOURS   4
#define LOG_UNIT_MINS    5


#define LOGFILE_NEW      0
#define LOGFILE_REOPENED 1


#define LOG_UNIT_TYPE_UNKNOWN "unknown"
#define LOG_UNIT_TYPE_MONTHS  "month"
#define LOG_UNIT_TYPE_WEEKS   "week"
#define LOG_UNIT_TYPE_DAYS    "day"
#define LOG_UNIT_TYPE_HOURS   "hour"
#define LOG_UNIT_TYPE_MINUTES "minute"

#define LOG_BUFFER_MAXSIZE 512 * 1024

#define PREVLOGFILE "Previous Log File:"

/* see log.c for why this is done */
typedef PRFileDesc *LOGFD;


struct logfileinfo
{
    PRInt64 l_size;             /* size is in bytes */
    time_t l_ctime;             /* log creation time*/
    PRBool l_compressed;        /* log was compressed */
    struct logfileinfo *l_next; /* next log */
};
typedef struct logfileinfo LogFileInfo;

struct logbufinfo
{
    char *top;         /* beginning of the buffer */
    char *current;     /* current pointer into buffer */
    size_t maxsize;    /* size of buffer */
    PRLock *lock;      /* lock for access logging */
    uint64_t refcount; /* Reference count for buffer copies */
};
typedef struct logbufinfo LogBufferInfo;

struct logging_opts
{
    /* These are access log specific */
    int log_access_state;
    int log_access_mode;                 /* access mode */
    int log_access_maxnumlogs;           /* Number of logs */
    PRInt64 log_access_maxlogsize;       /* max log size in bytes*/
    int log_access_rotationtime;         /* time in units. */
    int log_access_rotationunit;         /* time in units. */
    int log_access_rotationtime_secs;    /* time in seconds */
    int log_access_rotationsync_enabled; /* 0 or 1*/
    int log_access_rotationsynchour;     /* 0-23 */
    int log_access_rotationsyncmin;      /* 0-59 */
    time_t log_access_rotationsyncclock; /* clock in seconds */
    PRInt64 log_access_maxdiskspace;     /* space in bytes */
    PRInt64 log_access_minfreespace;     /* free space in bytes */
    int log_access_exptime;              /* time */
    int log_access_exptimeunit;          /* unit time */
    int log_access_exptime_secs;         /* time in secs */
    int log_access_level;                /* access log level */
    char *log_access_file;               /* access log file path */
    LOGFD log_access_fdes;               /* fp for the cur access log */
    unsigned int log_numof_access_logs;  /* number of logs */
    time_t log_access_ctime;             /* log creation time */
    LogFileInfo *log_access_logchain;    /* all the logs info */
    char *log_accessinfo_file;           /* access log rotation info file */
    LogBufferInfo *log_access_buffer;    /* buffer for access log */
    int log_access_compress;             /* Compress rotated logs */
    int log_access_stat_level;           /* statistics level in access log file */

    /* These are security audit log specific */
    int log_security_state;
    int log_security_mode;                 /* access mode */
    int log_security_maxnumlogs;           /* Number of logs */
    PRInt64 log_security_maxlogsize;       /* max log size in bytes*/
    int log_security_rotationtime;         /* time in units. */
    int log_security_rotationunit;         /* time in units. */
    int log_security_rotationtime_secs;    /* time in seconds */
    int log_security_rotationsync_enabled; /* 0 or 1*/
    int log_security_rotationsynchour;     /* 0-23 */
    int log_security_rotationsyncmin;      /* 0-59 */
    time_t log_security_rotationsyncclock; /* clock in seconds */
    PRInt64 log_security_maxdiskspace;     /* space in bytes */
    PRInt64 log_security_minfreespace;     /* free space in bytes */
    int log_security_exptime;              /* time */
    int log_security_exptimeunit;          /* unit time */
    int log_security_exptime_secs;         /* time in secs */
    int log_security_level;                /* security log level */
    char *log_security_file;               /* security log file path */
    LOGFD log_security_fdes;               /* fp for the cur security log */
    unsigned int log_numof_security_logs;  /* number of logs */
    time_t log_security_ctime;             /* log creation time */
    LogFileInfo *log_security_logchain;    /* all the logs info */
    char *log_securityinfo_file;           /* security log rotation info file */
    LogBufferInfo *log_security_buffer;    /* buffer for security log */
    int log_security_compress;             /* Compress rotated logs */

    /* These are error log specific */
    int log_error_state;
    int log_error_mode;                 /* access mode */
    int log_error_maxnumlogs;           /* Number of logs */
    PRInt64 log_error_maxlogsize;       /* max log size in bytes*/
    int log_error_rotationtime;         /* time in units. */
    int log_error_rotationunit;         /* time in units. */
    int log_error_rotationtime_secs;    /* time in seconds */
    int log_error_rotationsync_enabled; /* 0 or 1*/
    int log_error_rotationsynchour;     /* 0-23 */
    int log_error_rotationsyncmin;      /* 0-59 */
    time_t log_error_rotationsyncclock; /* clock in seconds */
    PRInt64 log_error_maxdiskspace;     /* space in bytes */
    PRInt64 log_error_minfreespace;     /* free space in bytes */
    int log_error_exptime;              /* time */
    int log_error_exptimeunit;          /* unit time */
    int log_error_exptime_secs;         /* time in secs */
    int log_error_compress;             /* Compress rotated logs */
    char *log_error_file;               /* error log file path */
    LOGFD log_error_fdes;               /* fp for the cur error log */
    unsigned int log_numof_error_logs;  /* number of logs */
    time_t log_error_ctime;             /* log creation time */
    LogFileInfo *log_error_logchain;    /* all the logs info */
    char *log_errorinfo_file;           /* error log rotation info file */
    Slapi_RWLock *log_error_rwlock;     /* lock on error*/

    /* These are audit log specific */
    int log_audit_state;
    int log_audit_mode;                 /* access mode */
    int log_audit_maxnumlogs;           /* Number of logs */
    PRInt64 log_audit_maxlogsize;       /* max log size in bytes*/
    int log_audit_rotationtime;         /* time in units. */
    int log_audit_rotationunit;         /* time in units. */
    int log_audit_rotationtime_secs;    /* time in seconds */
    int log_audit_rotationsync_enabled; /* 0 or 1*/
    int log_audit_rotationsynchour;     /* 0-23 */
    int log_audit_rotationsyncmin;      /* 0-59 */
    time_t log_audit_rotationsyncclock; /* clock in seconds */
    PRInt64 log_audit_maxdiskspace;     /* space in bytes */
    PRInt64 log_audit_minfreespace;     /* free space in bytes */
    int log_audit_exptime;              /* time */
    int log_audit_exptimeunit;          /* unit time */
    int log_audit_exptime_secs;         /* time in secs */
    char *log_audit_file;               /* audit log name */
    LOGFD log_audit_fdes;               /* audit log fdes */
    unsigned int log_numof_audit_logs;  /* number of logs */
    time_t log_audit_ctime;             /* log creation time */
    LogFileInfo *log_audit_logchain;    /* all the logs info */
    char *log_auditinfo_file;           /* audit log rotation info file */
    int log_audit_compress;             /* Compress rotated logs */
    LogBufferInfo *log_audit_buffer;    /* buffer for access log */

    /* These are auditfail log specific */
    int log_auditfail_state;
    int log_auditfail_mode;                 /* access mode */
    int log_auditfail_maxnumlogs;           /* Number of logs */
    PRInt64 log_auditfail_maxlogsize;       /* max log size in bytes*/
    int log_auditfail_rotationtime;         /* time in units. */
    int log_auditfail_rotationunit;         /* time in units. */
    int log_auditfail_rotationtime_secs;    /* time in seconds */
    int log_auditfail_rotationsync_enabled; /* 0 or 1*/
    int log_auditfail_rotationsynchour;     /* 0-23 */
    int log_auditfail_rotationsyncmin;      /* 0-59 */
    time_t log_auditfail_rotationsyncclock; /* clock in seconds */
    PRInt64 log_auditfail_maxdiskspace;     /* space in bytes */
    PRInt64 log_auditfail_minfreespace;     /* free space in bytes */
    int log_auditfail_exptime;              /* time */
    int log_auditfail_exptimeunit;          /* unit time */
    int log_auditfail_exptime_secs;         /* time in secs */
    char *log_auditfail_file;               /* auditfail log name */
    LOGFD log_auditfail_fdes;               /* auditfail log fdes */
    unsigned int log_numof_auditfail_logs;  /* number of logs */
    time_t log_auditfail_ctime;             /* log creation time */
    LogFileInfo *log_auditfail_logchain;    /* all the logs info */
    char *log_auditfailinfo_file;           /* auditfail log rotation info file */
    int log_auditfail_compress;             /* Compress rotated logs */
    LogBufferInfo *log_auditfail_buffer;    /* buffer for access log */

    int log_backend;
};

/* For log_state */
#define LOGGING_ENABLED    (int)0x1 /* logging is enabled */
#define LOGGING_NEED_TITLE 0x2 /* need to write title */
#define LOGGING_COMPRESS_ENABLED (int)0x1 /* log compression is enabled */

#define LOG_ACCESS_LOCK_READ()    PR_Lock(loginfo.log_access_buffer->lock)
#define LOG_ACCESS_UNLOCK_READ()  PR_Unlock(loginfo.log_access_buffer->lock)
#define LOG_ACCESS_LOCK_WRITE()   PR_Lock(loginfo.log_access_buffer->lock)
#define LOG_ACCESS_UNLOCK_WRITE() PR_Unlock(loginfo.log_access_buffer->lock)

#define LOG_SECURITY_LOCK_READ()    PR_Lock(loginfo.log_security_buffer->lock)
#define LOG_SECURITY_UNLOCK_READ()  PR_Unlock(loginfo.log_security_buffer->lock)
#define LOG_SECURITY_LOCK_WRITE()   PR_Lock(loginfo.log_security_buffer->lock)
#define LOG_SECURITY_UNLOCK_WRITE() PR_Unlock(loginfo.log_security_buffer->lock)

#define LOG_ERROR_LOCK_READ()    slapi_rwlock_rdlock(loginfo.log_error_rwlock)
#define LOG_ERROR_UNLOCK_READ()  slapi_rwlock_unlock(loginfo.log_error_rwlock)
#define LOG_ERROR_LOCK_WRITE()   slapi_rwlock_wrlock(loginfo.log_error_rwlock)
#define LOG_ERROR_UNLOCK_WRITE() slapi_rwlock_unlock(loginfo.log_error_rwlock)

#define LOG_AUDIT_LOCK_READ()    PR_Lock(loginfo.log_audit_buffer->lock)
#define LOG_AUDIT_UNLOCK_READ()  PR_Unlock(loginfo.log_audit_buffer->lock)
#define LOG_AUDIT_LOCK_WRITE()   PR_Lock(loginfo.log_audit_buffer->lock)
#define LOG_AUDIT_UNLOCK_WRITE() PR_Unlock(loginfo.log_audit_buffer->lock)

#define LOG_AUDITFAIL_LOCK_READ()    PR_Lock(loginfo.log_auditfail_buffer->lock)
#define LOG_AUDITFAIL_UNLOCK_READ()  PR_Unlock(loginfo.log_auditfail_buffer->lock)
#define LOG_AUDITFAIL_LOCK_WRITE()   PR_Lock(loginfo.log_auditfail_buffer->lock)
#define LOG_AUDITFAIL_UNLOCK_WRITE() PR_Unlock(loginfo.log_auditfail_buffer->lock)

/* For using with slapi_log_access */
#define TBUFSIZE 75                         /* size for time buffers */
#define SLAPI_LOG_BUFSIZ 2048               /* size for data buffers */
#define SLAPI_ACCESS_LOG_FMTBUF 128         /* size for access log formating line buffer */
#define SLAPI_SECURITY_LOG_FMTBUF 256       /* size for security log formating line buffer */
