/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/***********************************************************************
 * log.h 
 *
 * structures related to logging facility.
 *
 *************************************************************************/
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef  _WIN32
#include <errno.h>
#ifdef LINUX
#include <sys/statfs.h>
#else
#include <sys/statvfs.h>
#endif
#endif
#include <fcntl.h>
#include "prio.h"
#include "prprf.h"
#include "slap.h"
#include "slapi-plugin.h"

#define LOG_MB_IN_BYTES	(1024 * 1024)

#define LOG_SUCCESS 		0		/* fine & dandy */
#define LOG_CONTINUE		LOG_SUCCESS
#define LOG_ERROR   		1		/* default error case */
#define LOG_EXCEEDED 		2		/*err: > max logs allowed */
#define LOG_ROTATE		3		/*ok; go to the next log */
#define LOG_UNABLE_TO_OPENFILE  4

#define LOG_UNIT_UNKNOWN	0
#define LOG_UNIT_MONTHS 	1
#define LOG_UNIT_WEEKS		2
#define	LOG_UNIT_DAYS		3
#define LOG_UNIT_HOURS		4
#define LOG_UNIT_MINS		5


#define LOGFILE_NEW		0
#define LOGFILE_REOPENED	1


#define LOG_UNIT_TYPE_UNKNOWN	"unknown"
#define LOG_UNIT_TYPE_MONTHS	"month"
#define LOG_UNIT_TYPE_WEEKS	"week"
#define LOG_UNIT_TYPE_DAYS	"day"
#define LOG_UNIT_TYPE_HOURS	"hour"
#define LOG_UNIT_TYPE_MINUTES	"minute"

#define LOG_BUFFER_MAXSIZE          512 * 1024

/* see log.c for why this is done */
#ifdef XP_WIN32
typedef FILE *LOGFD;
#else
typedef PRFileDesc *LOGFD;
#endif


struct logfileinfo {
	int		l_size;  		/* size is in KB */
	time_t		l_ctime;		/* log creation time*/
	struct logfileinfo	*l_next;		/* next log */
};
typedef struct logfileinfo	LogFileInfo;

struct logbufinfo  {
    char    *top;                       /* beginning of the buffer */
    char    *current;                   /* current pointer into buffer */
    size_t  maxsize;                    /* size of buffer */
    PRLock *lock;                       /* lock for access logging */
	PRInt32 refcount;					/* Reference count for buffer copies */
};
typedef struct logbufinfo LogBufferInfo;

struct logging_opts {
	/* These are access log specific */
	int		log_access_state;
	int		log_access_mode;			/* access mode */
	int		log_access_maxnumlogs;		/* Number of logs */
	int		log_access_maxlogsize;		/* max log size in bytes*/
	int		log_access_rotationtime;	/* time in units. */
	int		log_access_rotationunit;	/* time in units. */
	int		log_access_rotationtime_secs; 	/* time in seconds */
	int		log_access_rotationsync_enabled;/* 0 or 1*/
	int		log_access_rotationsynchour;	/* 0-23 */
	int		log_access_rotationsyncmin;	/* 0-59 */
	time_t	log_access_rotationsyncclock;	/* clock in seconds */
	int		log_access_maxdiskspace;	/* space in bytes */
	int		log_access_minfreespace;	/* free space in bytes */
	int		log_access_exptime;		/* time */
	int		log_access_exptimeunit;		/* unit time */
	int		log_access_exptime_secs;	/* time in secs */

	int		log_access_level;	/* access log level */
	char		*log_access_file;	/* access log file path */
	LOGFD		log_access_fdes;	/* fp for the cur access log */
	unsigned int	log_numof_access_logs;	/* number of logs */
	time_t		log_access_ctime;	/* log creation time */
	LogFileInfo	*log_access_logchain;	/* all the logs info */
	char		*log_accessinfo_file;	/* access log rotation info file */
    LogBufferInfo *log_access_buffer;   /* buffer for access log */
    
	/* These are error log specific */
	int		log_error_state;
	int		log_error_mode;				/* access mode */
	int		log_error_maxnumlogs;		/* Number of logs */
	int		log_error_maxlogsize;		/* max log size in bytes*/
	int		log_error_rotationtime;	/* time in units. */
	int		log_error_rotationunit;	/* time in units. */
	int		log_error_rotationtime_secs; 	/* time in seconds */
	int		log_error_rotationsync_enabled;/* 0 or 1*/
	int		log_error_rotationsynchour;	/* 0-23 */
	int		log_error_rotationsyncmin;	/* 0-59 */
	time_t	log_error_rotationsyncclock;	/* clock in seconds */
	int		log_error_maxdiskspace;	/* space in bytes */
	int		log_error_minfreespace;	/* free space in bytes */
	int		log_error_exptime;	/* time */
	int		log_error_exptimeunit;	/* unit time */
	int		log_error_exptime_secs;	/* time in secs */

	char		*log_error_file;	/* error log file path */
	LOGFD		log_error_fdes;		/* fp for the cur error log */
	unsigned int	log_numof_error_logs;	/* number of logs */
	time_t		log_error_ctime;	/* log creation time */
	LogFileInfo	*log_error_logchain;	/* all the logs info */
	char		*log_errorinfo_file;	/* error log rotation info file */
	rwl		*log_error_rwlock;	/* lock on error*/

	/* These are audit log specific */
	int		log_audit_state;
	int		log_audit_mode;			/* access mode */
	int		log_audit_maxnumlogs;	/* Number of logs */
	int		log_audit_maxlogsize;	/* max log size in bytes*/
	int		log_audit_rotationtime;	/* time in units. */
	int		log_audit_rotationunit;	/* time in units. */
	int		log_audit_rotationtime_secs; 	/* time in seconds */
	int		log_audit_rotationsync_enabled;/* 0 or 1*/
	int		log_audit_rotationsynchour;	/* 0-23 */
	int		log_audit_rotationsyncmin;	/* 0-59 */
	time_t	log_audit_rotationsyncclock;	/* clock in seconds */
	int		log_audit_maxdiskspace;	/* space in bytes */
	int		log_audit_minfreespace;	/* free space in bytes */
	int		log_audit_exptime;	/* time */
	int		log_audit_exptimeunit;	/* unit time */
	int		log_audit_exptime_secs;	/* time in secs */

	char		*log_audit_file;	/* aufit log name */
	LOGFD		log_audit_fdes;		/* audit log  fdes */
	unsigned int	log_numof_audit_logs;	/* number of logs */
	time_t		log_audit_ctime;	/* log creation time */
	LogFileInfo	*log_audit_logchain;	/* all the logs info */
	char		*log_auditinfo_file;	/* audit log rotation info file */
	rwl		*log_audit_rwlock;	/* lock on audit*/

};

/* For log_state */
#define LOGGING_ENABLED	(int) 0x1		/* logging is enabled */
#define LOGGING_NEED_TITLE	0x2			/* need to write title */

#define LOG_ACCESS_LOCK_READ()    PR_Lock(loginfo.log_access_buffer->lock)
#define LOG_ACCESS_UNLOCK_READ()  PR_Unlock(loginfo.log_access_buffer->lock)
#define LOG_ACCESS_LOCK_WRITE()   PR_Lock(loginfo.log_access_buffer->lock)
#define LOG_ACCESS_UNLOCK_WRITE() PR_Unlock(loginfo.log_access_buffer->lock)

#define LOG_ERROR_LOCK_READ()    loginfo.log_error_rwlock->rwl_acquire_read_lock(loginfo.log_error_rwlock)
#define LOG_ERROR_UNLOCK_READ()  loginfo.log_error_rwlock->rwl_relinquish_read_lock(loginfo.log_error_rwlock)
#define LOG_ERROR_LOCK_WRITE()    loginfo.log_error_rwlock->rwl_acquire_write_lock(loginfo.log_error_rwlock)
#define LOG_ERROR_UNLOCK_WRITE()  loginfo.log_error_rwlock->rwl_relinquish_write_lock(loginfo.log_error_rwlock)

#define LOG_AUDIT_LOCK_READ()    loginfo.log_audit_rwlock->rwl_acquire_read_lock(loginfo.log_audit_rwlock)
#define LOG_AUDIT_UNLOCK_READ()  loginfo.log_audit_rwlock->rwl_relinquish_read_lock(loginfo.log_audit_rwlock)
#define LOG_AUDIT_LOCK_WRITE()    loginfo.log_audit_rwlock->rwl_acquire_write_lock(loginfo.log_audit_rwlock)
#define LOG_AUDIT_UNLOCK_WRITE()  loginfo.log_audit_rwlock->rwl_relinquish_write_lock(loginfo.log_audit_rwlock)

