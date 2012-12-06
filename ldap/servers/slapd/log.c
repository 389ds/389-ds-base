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
 * Copyright (C) 2010 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
**
** log.c
** 
**	Routines for writing access and error/debug logs
**
**
**	History:
** 	   As of DS 4.0, we support log rotation for the ACCESS/ERROR/AUDIT log. 
*/

#include "log.h"
#include "fe.h"
#ifndef _WIN32
#include <pwd.h> /* getpwnam */
#endif

#if defined( XP_WIN32 )
#include <fcntl.h>
#include "ntslapdmessages.h"
#include "proto-ntutil.h"
extern HANDLE hSlapdEventSource;
extern LPTSTR pszServerName;
#define _PSEP '\\'
#else
#define _PSEP '/'
#endif
/**************************************************************************
 * GLOBALS, defines, and ...
 *************************************************************************/
/* main struct which contains all the information about logging */
PRUintn logbuf_tsdindex;
struct logbufinfo *logbuf_accum;
static struct logging_opts  loginfo;
static int detached=0;

/* used to lock the timestamp info used by vslapd_log_access */
static PRLock *ts_time_lock = NULL;

/*
 * Note: the order of the values in the slapi_log_map array must exactly
 * match that of the SLAPI_LOG_XXX #defines found in slapi-plugin.h (this is
 * so we can use the SLAPI_LOG_XXX values to index directly into the array).
 */
static int slapi_log_map[] = {
    LDAP_DEBUG_ANY,		/* SLAPI_LOG_FATAL */
    LDAP_DEBUG_TRACE,		/* SLAPI_LOG_TRACE */
    LDAP_DEBUG_PACKETS,		/* SLAPI_LOG_PACKETS */
    LDAP_DEBUG_ARGS,		/* SLAPI_LOG_ARGS */
    LDAP_DEBUG_CONNS,		/* SLAPI_LOG_CONNS */
    LDAP_DEBUG_BER,		/* SLAPI_LOG_BER */
    LDAP_DEBUG_FILTER,		/* SLAPI_LOG_FILTER */
    LDAP_DEBUG_CONFIG,		/* SLAPI_LOG_CONFIG */
    LDAP_DEBUG_ACL,		/* SLAPI_LOG_ACL */
    LDAP_DEBUG_SHELL,		/* SLAPI_LOG_SHELL */
    LDAP_DEBUG_PARSE,		/* SLAPI_LOG_PARSE */
    LDAP_DEBUG_HOUSE,		/* SLAPI_LOG_HOUSE */
    LDAP_DEBUG_REPL,		/* SLAPI_LOG_REPL */
    LDAP_DEBUG_CACHE,		/* SLAPI_LOG_CACHE */
    LDAP_DEBUG_PLUGIN,		/* SLAPI_LOG_PLUGIN */
    LDAP_DEBUG_TIMING,		/* SLAPI_LOG_TIMING */
    LDAP_DEBUG_BACKLDBM,	/* SLAPI_LOG_BACKLDBM */
    LDAP_DEBUG_ACLSUMMARY	/* SLAPI_LOG_ACLSUMMARY */
};

#define SLAPI_LOG_MIN	SLAPI_LOG_FATAL		/* from slapi-plugin.h */
#define SLAPI_LOG_MAX	SLAPI_LOG_ACLSUMMARY	/* from slapi-plugin.h */
#define	TBUFSIZE 50				/* size for time buffers */
#define SLAPI_LOG_BUFSIZ 2048			/* size for data buffers */
/**************************************************************************
 * PROTOTYPES
 *************************************************************************/
static int	log__open_accesslogfile(int logfile_type, int locked);
static int	log__open_errorlogfile(int logfile_type, int locked);
static int	log__open_auditlogfile(int logfile_type, int locked);
static int	log__needrotation(LOGFD fp, int logtype);
static int	log__delete_access_logfile();
static int	log__delete_error_logfile(int locked);
static int	log__delete_audit_logfile();
static int 	log__access_rotationinfof(char *pathname);
static int 	log__error_rotationinfof(char *pathname);
static int 	log__audit_rotationinfof(char *pathname);
static int 	log__extract_logheader (FILE *fp, long  *f_ctime, PRInt64 *f_size);
static int	log__check_prevlogs (FILE *fp, char *filename);
static PRInt64 	log__getfilesize(LOGFD fp);
static PRInt64  log__getfilesize_with_filename(char *filename);
static int 	log__enough_freespace(char  *path);

static int 	vslapd_log_error(LOGFD fp, char *subsystem, char *fmt, va_list ap, int locked );
static int 	vslapd_log_access(char *fmt, va_list ap );
static void	log_convert_time (time_t ctime, char *tbuf, int type);
static time_t	log_reverse_convert_time (char *tbuf);
static LogBufferInfo *log_create_buffer(size_t sz);
static void	log_append_buffer2(time_t tnl, LogBufferInfo *lbi, char *msg1, size_t size1, char *msg2, size_t size2);
static void	log_flush_buffer(LogBufferInfo *lbi, int type, int sync_now);
static void	log_write_title(LOGFD fp);
static void log__error_emergency(const char *errstr, int reopen, int locked);
static void vslapd_log_emergency_error(LOGFD fp, const char *msg, int locked);

static int
slapd_log_error_proc_internal(
    char	*subsystem,	/* omitted if NULL */
    char	*fmt,
    va_list ap_err, 
    va_list ap_file); 

/*
 * these macros are used for opening a log file, closing a log file, and
 * writing out to a log file.  we have to do this because currently NSPR
 * is extremely under-performant on NT, while fopen/fwrite fail on several
 * unix platforms if there are more than 128 files open.
 *
 * LOG_OPEN_APPEND(fd, filename, mode) returns true if successful.  'fd' should
 *	be of type LOGFD (check log.h).  the file is open for appending to.
 * LOG_OPEN_WRITE(fd, filename, mode) is the same but truncates the file and
 *	starts writing at the beginning of the file.
 * LOG_WRITE(fd, buffer, size, headersize) writes into a LOGFD
 * LOG_WRITE_NOW(fd, buffer, size, headersize, err) writes into a LOGFD and 
 *  flushes the buffer if necessary
 * LOG_CLOSE(fd) closes the logfile
 */
#ifdef XP_WIN32
#define LOG_OPEN_APPEND(fd, filename, mode) \
	(((fd) = fopen((filename), "a")) != NULL)
#define LOG_OPEN_WRITE(fd, filename, mode) \
	(((fd) = fopen((filename), "w")) != NULL)
#define LOG_WRITE(fd, buffer, size, headersize) \
	if ( fwrite((buffer), (size), 1, (fd)) != 1 ) \
	{\
		ReportSlapdEvent(EVENTLOG_INFORMATION_TYPE, MSG_SERVER_FAILED_TO_WRITE_LOG, 1, (buffer)); \
	}
#define LOG_WRITE_NOW(fd, buffer, size, headersize, err) do {\
		(err) = 0; \
		if ( fwrite((buffer), (size), 1, (fd)) != 1 ) \
		{ \
			ReportSlapdEvent(EVENTLOG_INFORMATION_TYPE, MSG_SERVER_FAILED_TO_WRITE_LOG, 1, (buffer)); \
			(err) = 1; \
		}; \
		fflush((fd)); \
	} while (0)
#define LOG_CLOSE(fd) \
	fclose((fd))
#else  /* xp_win32 */
#define LOG_OPEN_APPEND(fd, filename, mode) \
	(((fd) = PR_Open((filename), PR_WRONLY | PR_APPEND | PR_CREATE_FILE , \
		mode)) != NULL)
#define LOG_OPEN_WRITE(fd, filename, mode) \
	(((fd) = PR_Open((filename), PR_WRONLY | PR_TRUNCATE | \
		PR_CREATE_FILE, mode)) != NULL)
#define LOG_WRITE(fd, buffer, size, headersize) \
	if ( slapi_write_buffer((fd), (buffer), (size)) != (size) ) \
	{ \
		PRErrorCode prerr = PR_GetError(); \
		syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer)+(headersize) ); \
	}
#define LOG_WRITE_NOW(fd, buffer, size, headersize, err) do {\
	(err) = 0; \
	if ( slapi_write_buffer((fd), (buffer), (size)) != (size) ) \
	{ \
		PRErrorCode prerr = PR_GetError(); \
		syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer)+(headersize) ); \
		(err) = prerr; \
	} \
	/* Should be a flush in here ?? Yes because PR_SYNC doesn't work ! */ \
	PR_Sync(fd); \
	} while (0)
#define LOG_CLOSE(fd) \
	PR_Close((fd))
#endif


/******************************************************************************
* Set the access level
******************************************************************************/ 
void g_set_accesslog_level(int val)
{
	LOG_ACCESS_LOCK_WRITE( );
	loginfo.log_access_level = val;
	LOG_ACCESS_UNLOCK_WRITE();
}

/******************************************************************************
* Set whether the process is alive or dead 
* If it is detached, then we write the error in 'stderr'
******************************************************************************/ 
void g_set_detached(int val)
{
	detached = val;
}

/******************************************************************************
* Tell me whether logging begins or not 
******************************************************************************/ 
void g_log_init(int log_enabled)
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
#if defined( XP_WIN32 )
	/* char * instancedir = NULL; obsolete. */
	/* To port to Windows, need to support FHS. */
#endif

	ts_time_lock = PR_NewLock();
	if (! ts_time_lock)
	    exit(-1);

#if defined( XP_WIN32 )
	pszServerName = slapi_ch_malloc( MAX_SERVICE_NAME );
	/* instancedir = config_get_instancedir(); eliminated */
	unixtodospath(instancedir);
	if( !SlapdGetServerNameFromCmdline(pszServerName, instancedir, 1) )
	{
		MessageBox(GetDesktopWindow(), "Failed to get the Directory"
			" Server name from the command-line argument.",
			" ", MB_ICONEXCLAMATION | MB_OK);
		exit( 1 );
	}
	slapi_ch_free((void **)&instancedir);

    /* Register with the NT EventLog */
    hSlapdEventSource = RegisterEventSource(NULL, pszServerName );
    if( !hSlapdEventSource  )
    {
        char szMessage[256];
        PR_snprintf( szMessage, sizeof(szMessage), "Directory Server %s is terminating. Failed "
            "to set the EventLog source.", pszServerName);
        MessageBox(GetDesktopWindow(), szMessage, " ", 
            MB_ICONEXCLAMATION | MB_OK);
        exit( 1 );
    }
#endif

	/* ACCESS LOG */
	loginfo.log_access_state = 0;
	loginfo.log_access_mode = SLAPD_DEFAULT_FILE_MODE;
	loginfo.log_access_maxnumlogs = 1;
	loginfo.log_access_maxlogsize = -1;
	loginfo.log_access_rotationsync_enabled = 0;
	loginfo.log_access_rotationsynchour = -1;
	loginfo.log_access_rotationsyncmin = -1;
	loginfo.log_access_rotationsyncclock = -1;
	loginfo.log_access_rotationtime = 1;                  /* default: 1 */
	loginfo.log_access_rotationunit = LOG_UNIT_DAYS;      /* default: day */
	loginfo.log_access_rotationtime_secs = 86400;         /* default: 1 day */
	loginfo.log_access_maxdiskspace =  -1;
	loginfo.log_access_minfreespace =  -1;
	loginfo.log_access_exptime =  -1;                     /* default: -1 */
	loginfo.log_access_exptimeunit =  LOG_UNIT_MONTHS;    /* default: month */
	loginfo.log_access_exptime_secs = -1;                 /* default: -1 */
	loginfo.log_access_level = LDAP_DEBUG_STATS;
	loginfo.log_access_ctime = 0L;
	loginfo.log_access_fdes = NULL;
	loginfo.log_access_file = NULL;
	loginfo.log_accessinfo_file = NULL;
	loginfo.log_numof_access_logs = 1;
	loginfo.log_access_logchain = NULL;
    loginfo.log_access_buffer = log_create_buffer(LOG_BUFFER_MAXSIZE);
    if (loginfo.log_access_buffer == NULL)
        exit(-1);
	if ((loginfo.log_access_buffer->lock = PR_NewLock())== NULL ) 
		exit (-1);
	slapdFrontendConfig->accessloglevel = LDAP_DEBUG_STATS;

	/* ERROR LOG */
	loginfo.log_error_state = 0;
	loginfo.log_error_mode = SLAPD_DEFAULT_FILE_MODE;
	loginfo.log_error_maxnumlogs = 1;
	loginfo.log_error_maxlogsize = -1;
	loginfo.log_error_rotationsync_enabled = 0;
	loginfo.log_error_rotationsynchour = -1;
	loginfo.log_error_rotationsyncmin = -1;
	loginfo.log_error_rotationsyncclock = -1;
	loginfo.log_error_rotationtime = 1;                   /* default: 1 */
	loginfo.log_error_rotationunit =  LOG_UNIT_WEEKS;     /* default: week */
	loginfo.log_error_rotationtime_secs = 604800;         /* default: 1 week */
	loginfo.log_error_maxdiskspace =  -1;
	loginfo.log_error_minfreespace =  -1;
	loginfo.log_error_exptime =  -1;                      /* default: -1 */
	loginfo.log_error_exptimeunit =  LOG_UNIT_MONTHS;     /* default: month */
	loginfo.log_error_exptime_secs = -1;                  /* default: -1 */
	loginfo.log_error_ctime = 0L;
	loginfo.log_error_file = NULL;
	loginfo.log_errorinfo_file = NULL;
	loginfo.log_error_fdes = NULL;
	loginfo.log_numof_error_logs = 1;
	loginfo.log_error_logchain = NULL;
	if ((loginfo.log_error_rwlock =slapi_new_rwlock())== NULL ) {
		exit (-1);
	}

	/* AUDIT LOG */
	loginfo.log_audit_state = 0;
	loginfo.log_audit_mode = SLAPD_DEFAULT_FILE_MODE;
	loginfo.log_audit_maxnumlogs = 1;
	loginfo.log_audit_maxlogsize = -1;
	loginfo.log_audit_rotationsync_enabled = 0;
	loginfo.log_audit_rotationsynchour = -1;
	loginfo.log_audit_rotationsyncmin = -1;
	loginfo.log_audit_rotationsyncclock = -1;
	loginfo.log_audit_rotationtime = 1;                   /* default: 1 */
	loginfo.log_audit_rotationunit =  LOG_UNIT_WEEKS;     /* default: week */
	loginfo.log_audit_rotationtime_secs = 604800;         /* default: 1 week */
	loginfo.log_audit_maxdiskspace =  -1;
	loginfo.log_audit_minfreespace =  -1;
	loginfo.log_audit_exptime =  -1;                      /* default: -1 */
	loginfo.log_audit_exptimeunit =  LOG_UNIT_WEEKS;      /* default: week */
	loginfo.log_audit_exptime_secs = -1;                  /* default: -1 */
	loginfo.log_audit_ctime = 0L;
	loginfo.log_audit_file = NULL;
	loginfo.log_auditinfo_file = NULL;
	loginfo.log_numof_audit_logs = 1;
	loginfo.log_audit_fdes = NULL;
	loginfo.log_audit_logchain = NULL;
	if ((loginfo.log_audit_rwlock =slapi_new_rwlock())== NULL ) {
		exit (-1);
	}
}

/******************************************************************************
* Tell me if log is enabled or disabled
******************************************************************************/ 
int
log_set_logging(const char *attrname, char *value, int logtype, char *errorbuf, int apply) 
{
	int	v;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

	if ( NULL == value ) {
	  PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			  "%s: NULL value; valid values "
			  "are \"on\" or \"off\"", attrname );
	  return LDAP_OPERATIONS_ERROR;
	}

	if (strcasecmp(value, "on") == 0) {
		v = LOGGING_ENABLED;
	}
	else if (strcasecmp(value, "off") == 0 ) {
	  v = 0;
	}
	else {
	  PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			  "%s: invalid value \"%s\", valid values "
			  "are \"on\" or \"off\"", attrname, value );
	  return LDAP_OPERATIONS_ERROR;
	}
			  
	if ( !apply ){
	  return LDAP_SUCCESS;
	}
	
	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		fe_cfg->accesslog_logging_enabled = v;
		if(v) {
		  loginfo.log_access_state |= LOGGING_ENABLED;
		}
		else {
		  loginfo.log_access_state &= ~LOGGING_ENABLED;
		}
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		fe_cfg->errorlog_logging_enabled = v;
		if (v) {
		  loginfo.log_error_state |= LOGGING_ENABLED;
		}
		else {
		  loginfo.log_error_state &= ~LOGGING_ENABLED;
		}
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		fe_cfg->auditlog_logging_enabled = v;
		if (v) {
		  loginfo.log_audit_state |= LOGGING_ENABLED;
		}
		else {
		  loginfo.log_audit_state &= ~LOGGING_ENABLED;
		}
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	}
	
	return LDAP_SUCCESS;

}
/******************************************************************************
* Tell me  the access log file name inc path
******************************************************************************/ 
char *
g_get_access_log () {
	char	*logfile = NULL;

	LOG_ACCESS_LOCK_READ();
	if ( loginfo.log_access_file) 
		logfile = slapi_ch_strdup (loginfo.log_access_file);
	LOG_ACCESS_UNLOCK_READ();
	
	return logfile;
 
}
/******************************************************************************
* Point to a new access logdir
*
* Returns:
*	LDAP_SUCCESS -- success
*	LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*	LDAP_LOCAL_ERRO  -- some error
******************************************************************************/ 
int
log_update_accesslogdir(char *pathname, int apply)
{
	int		rv = LDAP_SUCCESS;
	LOGFD		fp;

	/* try to open the file, we may have a incorrect path */
	if (! LOG_OPEN_APPEND(fp, pathname, loginfo.log_access_mode)) {
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				pathname, errno, slapd_system_strerror(errno));
		/* stay with the current log file */
		return LDAP_UNWILLING_TO_PERFORM;
	}
	LOG_CLOSE(fp);

	/* skip the rest if we aren't doing this for real */
	if ( !apply ) {
	  return LDAP_SUCCESS;
	}

	/* 
	** The user has changed the access log directory. That means we
	** need to start fresh.
	*/
	LOG_ACCESS_LOCK_WRITE ();
	if (loginfo.log_access_fdes) {
		LogFileInfo	*logp, *d_logp;

		LDAPDebug(LDAP_DEBUG_TRACE,
		   	"LOGINFO:Closing the access log file. "
			"Moving to a new access log file (%s)\n", pathname,0,0);

		LOG_CLOSE(loginfo.log_access_fdes);
		loginfo.log_access_fdes = 0;
		loginfo.log_access_ctime = 0;
		logp = loginfo.log_access_logchain;
		while (logp) {
			d_logp = logp;
			logp = logp->l_next;
			slapi_ch_free((void**)&d_logp);
		}
		loginfo.log_access_logchain = NULL;
		slapi_ch_free((void**)&loginfo.log_access_file);
		loginfo.log_access_file = NULL;
		loginfo.log_numof_access_logs = 1;
	}

	/* Now open the new access log file */
	if ( access_log_openf (pathname, 1 /* locked */)) {
		rv = LDAP_LOCAL_ERROR; /* error Unable to use the new dir */
	}
	LOG_ACCESS_UNLOCK_WRITE();
	return rv;
}
/******************************************************************************
* Tell me  the error log file name inc path
******************************************************************************/ 
char *
g_get_error_log() {
	char	*logfile = NULL;

	LOG_ERROR_LOCK_READ();
	if ( loginfo.log_error_file) 
		logfile = slapi_ch_strdup (loginfo.log_error_file);
	LOG_ERROR_UNLOCK_READ();
	
	return logfile;
}
/******************************************************************************
* Point to a new error logdir
*
* Returns:
*	LDAP_SUCCESS -- success
*	LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*	LDAP_LOCAL_ERRO  -- some error
******************************************************************************/ 
int
log_update_errorlogdir(char *pathname, int apply)
{

	int		rv = LDAP_SUCCESS;
	LOGFD		fp;

	/* try to open the file, we may have a incorrect path */
	if (! LOG_OPEN_APPEND(fp, pathname, loginfo.log_error_mode)) {
		char buffer[SLAPI_LOG_BUFSIZ];
		PRErrorCode prerr = PR_GetError();
		/* stay with the current log file */
		PR_snprintf(buffer, sizeof(buffer),
				"Failed to open file %s. error %d (%s). Exiting...",
				pathname, prerr, slapd_pr_strerror(prerr));
		log__error_emergency(buffer, 0, 0);
		return LDAP_UNWILLING_TO_PERFORM;
	}
	LOG_CLOSE(fp);
	
	/* skip the rest if we aren't doing this for real */
	if ( !apply ) {
	  return LDAP_SUCCESS;
	}

	/* 
	** The user has changed the error log directory. That means we
	** need to start fresh.
	*/
	LOG_ERROR_LOCK_WRITE ();
	if (loginfo.log_error_fdes) {
		LogFileInfo	*logp, *d_logp;

		LOG_CLOSE(loginfo.log_error_fdes);
		loginfo.log_error_fdes = 0;
		loginfo.log_error_ctime = 0;
		logp = loginfo.log_error_logchain;
		while (logp) {
			d_logp = logp;
			logp = logp->l_next;
			slapi_ch_free((void**)&d_logp);
		}
		loginfo.log_error_logchain = NULL;
		slapi_ch_free((void**)&loginfo.log_error_file);
		loginfo.log_error_file = NULL;
		loginfo.log_numof_error_logs = 1;
	}

	/* Now open the new errorlog */
	if ( error_log_openf (pathname, 1 /* obtained lock */)) {
		rv =  LDAP_LOCAL_ERROR; /* error: Unable to use the new dir */
	}
		
	LOG_ERROR_UNLOCK_WRITE();
	return rv;
}
/******************************************************************************
* Tell me  the audit log file name inc path
******************************************************************************/ 
char *
g_get_audit_log() {
	char	*logfile = NULL;

	LOG_AUDIT_LOCK_READ();
	if ( loginfo.log_audit_file) 
		logfile = slapi_ch_strdup (loginfo.log_audit_file);
	LOG_AUDIT_UNLOCK_READ();
	
	return logfile;
}
/******************************************************************************
* Point to a new audit logdir
*
* Returns:
*	LDAP_SUCCESS -- success
*	LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*	LDAP_LOCAL_ERRO  -- some error
******************************************************************************/ 
int
log_update_auditlogdir(char *pathname, int apply)
{
	int		rv = LDAP_SUCCESS;
	LOGFD		fp;

	/* try to open the file, we may have a incorrect path */
	if (! LOG_OPEN_APPEND(fp, pathname, loginfo.log_audit_mode)) {
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				pathname, errno, slapd_system_strerror(errno));
		/* stay with the current log file */
		return LDAP_UNWILLING_TO_PERFORM;
	}
	LOG_CLOSE(fp);

	/* skip the rest if we aren't doing this for real */
	if ( !apply ) {
	  return LDAP_SUCCESS;
	}

	/* 
	** The user has changed the audit log directory. That means we
	** need to start fresh.
	*/
	LOG_AUDIT_LOCK_WRITE ();
	if (loginfo.log_audit_fdes) {
		LogFileInfo	*logp, *d_logp;
		LDAPDebug(LDAP_DEBUG_TRACE,
		   	"LOGINFO:Closing the audit log file. "
			"Moving to a new audit file (%s)\n", pathname,0,0);

		LOG_CLOSE(loginfo.log_audit_fdes);
		loginfo.log_audit_fdes = 0;
		loginfo.log_audit_ctime = 0;
		logp = loginfo.log_audit_logchain;
		while (logp) {
			d_logp = logp;
			logp = logp->l_next;
			slapi_ch_free((void**)&d_logp);
		}
		loginfo.log_audit_logchain = NULL;
		slapi_ch_free((void**)&loginfo.log_audit_file);
		loginfo.log_audit_file = NULL;
		loginfo.log_numof_audit_logs = 1;
	}

	/* Now open the new auditlog */
	if ( audit_log_openf (pathname, 1 /* locked */)) {
		rv = LDAP_LOCAL_ERROR; /* error: Unable to use the new dir */
	}
	LOG_AUDIT_UNLOCK_WRITE();
	return rv;
}

int
log_set_mode (const char *attrname, char *value, int logtype, char *errorbuf, int apply)
{
	int	v = 0;
	int	retval = LDAP_SUCCESS;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

	if ( NULL == value ) {
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			  "%s: null value; valid values "
			  "are are of the format \"yz-yz-yz-\" where y could be 'r' or '-',"
			  " and z could be 'w' or '-'", attrname );
		return LDAP_OPERATIONS_ERROR;
	}

	if ( !apply ){
		return LDAP_SUCCESS;
	}

	v = strtol (value, NULL, 8);
	
	switch (logtype) {
		case SLAPD_ACCESS_LOG:
			LOG_ACCESS_LOCK_WRITE( );
			if (loginfo.log_access_file &&
				( chmod( loginfo.log_access_file, v ) != 0) ) {
				int oserr = errno;
				PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: Failed to chmod access log file to %s: errno %d (%s)",
					attrname, value, oserr, slapd_system_strerror(oserr) );
				retval = LDAP_UNWILLING_TO_PERFORM;
			} else { /* only apply the changes if no file or if successful */
				slapi_ch_free ( (void **) &fe_cfg->accesslog_mode );
				fe_cfg->accesslog_mode = slapi_ch_strdup (value);
				loginfo.log_access_mode = v;
			}
			LOG_ACCESS_UNLOCK_WRITE();
			break;
		case SLAPD_ERROR_LOG:
			LOG_ERROR_LOCK_WRITE( );
			if (loginfo.log_error_file &&
				( chmod( loginfo.log_error_file, v ) != 0) ) {
				int oserr = errno;
				PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: Failed to chmod error log file to %s: errno %d (%s)",
					attrname, value, oserr, slapd_system_strerror(oserr) );
				retval = LDAP_UNWILLING_TO_PERFORM;
			} else { /* only apply the changes if no file or if successful */
				slapi_ch_free ( (void **) &fe_cfg->errorlog_mode );
				fe_cfg->errorlog_mode = slapi_ch_strdup (value);
				loginfo.log_error_mode = v;
			}
			LOG_ERROR_UNLOCK_WRITE();
			break;
		case SLAPD_AUDIT_LOG:
			LOG_AUDIT_LOCK_WRITE( );
			if (loginfo.log_audit_file &&
				( chmod( loginfo.log_audit_file, v ) != 0) ) {
				int oserr = errno;
				PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: Failed to chmod audit log file to %s: errno %d (%s)",
					attrname, value, oserr, slapd_system_strerror(oserr) );
				retval = LDAP_UNWILLING_TO_PERFORM;
			} else { /* only apply the changes if no file or if successful */
				slapi_ch_free ( (void **) &fe_cfg->auditlog_mode );
				fe_cfg->auditlog_mode = slapi_ch_strdup (value);
				loginfo.log_audit_mode = v;
			}
			LOG_AUDIT_UNLOCK_WRITE();
			break;
	}
	return retval;
}

/******************************************************************************
* MAX NUMBER OF LOGS
******************************************************************************/ 
int 
log_set_numlogsperdir(const char *attrname, char *numlogs_str, int logtype, char *returntext, int apply)
{
  slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
  
  int rv = LDAP_SUCCESS;
  int numlogs;
  
  if ( logtype != SLAPD_ACCESS_LOG &&
	   logtype != SLAPD_ERROR_LOG &&
	   logtype != SLAPD_AUDIT_LOG ) {
	rv = LDAP_OPERATIONS_ERROR;
	PR_snprintf( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid log type %d", attrname, logtype );
  }
  if ( !apply || !numlogs_str || !*numlogs_str) {
	return rv;
  }

  numlogs = atoi(numlogs_str);

  if (numlogs >= 1) {
	switch (logtype) {
	case SLAPD_ACCESS_LOG:
	  LOG_ACCESS_LOCK_WRITE( );
	  loginfo.log_access_maxnumlogs = numlogs;
	  fe_cfg->accesslog_maxnumlogs = numlogs;
	  LOG_ACCESS_UNLOCK_WRITE();
	  break;
	case SLAPD_ERROR_LOG:
	  LOG_ERROR_LOCK_WRITE( );
	  loginfo.log_error_maxnumlogs = numlogs;
	  fe_cfg->errorlog_maxnumlogs = numlogs;
	  LOG_ERROR_UNLOCK_WRITE();
	  break;
	case SLAPD_AUDIT_LOG:
	  LOG_AUDIT_LOCK_WRITE( );
	  loginfo.log_audit_maxnumlogs = numlogs;
	  fe_cfg->auditlog_maxnumlogs = numlogs;
	  LOG_AUDIT_UNLOCK_WRITE();
	  break;
	default:
	  rv = LDAP_OPERATIONS_ERROR;
	  LDAPDebug( LDAP_DEBUG_ANY, 
				 "log_set_numlogsperdir: invalid log type %d", logtype,0,0 );
	}
  }
  return rv;
}

/******************************************************************************
* LOG SIZE
* Return Values:
*   LDAP_OPERATIONS_ERROR         -- fail
*   LDAP_SUCCESS                  -- success
*
* NOTE: The config struct should contain the maxlogsize in MB and not in bytes.
******************************************************************************/ 
int 
log_set_logsize(const char *attrname, char *logsize_str, int logtype, char *returntext, int apply)
{
	int	rv = LDAP_SUCCESS;
	PRInt64 mdiskspace= 0;  /* in bytes */
	PRInt64	max_logsize;    /* in bytes */
	int logsize;            /* in megabytes */
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

	if (!apply || !logsize_str || !*logsize_str)
		return rv;

	logsize = atoi(logsize_str);
	
	/* convert it to bytes */
	max_logsize = (PRInt64)logsize * LOG_MB_IN_BYTES;

	if (max_logsize <= 0) {
	  max_logsize = -1;
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		mdiskspace = loginfo.log_access_maxdiskspace;
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		mdiskspace = loginfo.log_error_maxdiskspace;
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		mdiskspace = loginfo.log_audit_maxdiskspace;
		break;
	   default:
		 PR_snprintf( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid logtype %d", attrname, logtype );
		 rv = LDAP_OPERATIONS_ERROR;
	}

	if ((max_logsize > mdiskspace) && (mdiskspace != -1)) 
        rv = 2;

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		if (!rv && apply) {
		  loginfo.log_access_maxlogsize = max_logsize;
		  fe_cfg->accesslog_maxlogsize = logsize;
		}
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		if (!rv && apply) {
		  loginfo.log_error_maxlogsize = max_logsize;
		  fe_cfg->errorlog_maxlogsize = logsize;
		}
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		if (!rv && apply) {
		  loginfo.log_audit_maxlogsize = max_logsize;
		  fe_cfg->auditlog_maxlogsize = logsize;
		}
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		rv = 1;
	}
	/* logsize is in MB */
	if (rv == 2) {
		LDAPDebug (LDAP_DEBUG_ANY, 
			   "Invalid value for Maximum log size:"
			   "Maxlogsize:%d (MB) exceeds Maxdisksize:%d (MB)\n", 
			    logsize, mdiskspace/LOG_MB_IN_BYTES,0);

			rv = LDAP_OPERATIONS_ERROR;
	}
	return rv;
}

time_t
log_get_rotationsyncclock(int synchour, int syncmin)
{
	struct tm	*currtm;
	time_t		currclock;
	time_t		syncclock;
	int			hours, minutes;

	time( &currclock);
	currtm = localtime( &currclock );

	if ( syncmin < currtm->tm_min ) {
		minutes = syncmin + 60 - currtm->tm_min;
		hours = synchour - 1 - currtm->tm_hour;
	} else {
		minutes = syncmin - currtm->tm_min;
		hours = synchour - currtm->tm_hour;
	}
	if ( hours < 0 ) hours += 24;

	syncclock = currclock + hours * 3600 + minutes * 60;
	return syncclock;
}

int
log_set_rotationsync_enabled(const char *attrname, char *value, int logtype, char *errorbuf, int apply)
{
	int	v;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

	if ( NULL == value ) {
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			  "%s: NULL value; valid values "
			  "are \"on\" or \"off\"", attrname );
		return LDAP_OPERATIONS_ERROR;
	}

	if (strcasecmp(value, "on") == 0) {
		v = LDAP_ON;
	}
	else if (strcasecmp(value, "off") == 0 ) {
		v = LDAP_OFF;
	}
	else {
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			  "%s: invalid value \"%s\", valid values "
			  "are \"on\" or \"off\"", attrname, value );
		return LDAP_OPERATIONS_ERROR;
	}
			  
	if ( !apply ){
		return LDAP_SUCCESS;
	}
	
	switch (logtype) {
		case SLAPD_ACCESS_LOG:
			LOG_ACCESS_LOCK_WRITE( );
			fe_cfg->accesslog_rotationsync_enabled = v;
			loginfo.log_access_rotationsync_enabled = v;
			LOG_ACCESS_UNLOCK_WRITE();
			break;
		case SLAPD_ERROR_LOG:
			LOG_ERROR_LOCK_WRITE( );
			fe_cfg->errorlog_rotationsync_enabled = v;
			loginfo.log_error_rotationsync_enabled = v;
			LOG_ERROR_UNLOCK_WRITE();
			break;
		case SLAPD_AUDIT_LOG:
			LOG_AUDIT_LOCK_WRITE( );
			fe_cfg->auditlog_rotationsync_enabled = v;
			loginfo.log_audit_rotationsync_enabled = v;
			LOG_AUDIT_UNLOCK_WRITE();
			break;
	}
	return LDAP_SUCCESS;
}

int 
log_set_rotationsynchour(const char *attrname, char *rhour_str, int logtype, char *returntext, int apply)
{
	int	rhour = -1;
	int	rv = LDAP_SUCCESS;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	
	if ( logtype != SLAPD_ACCESS_LOG &&
		 logtype != SLAPD_ERROR_LOG &&
		 logtype != SLAPD_AUDIT_LOG ) {
	  PR_snprintf( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid log type: %d", attrname, logtype );
	  return LDAP_OPERATIONS_ERROR;
	}
	
	/* return if we aren't doing this for real */
	if ( !apply ) {
	  return rv;
	}

	if ( rhour_str && *rhour_str != '\0' )
		rhour = atol( rhour_str );
	if ( rhour > 23 )
		rhour = rhour % 24;

	switch (logtype) {
		case SLAPD_ACCESS_LOG:
			LOG_ACCESS_LOCK_WRITE( );
			loginfo.log_access_rotationsynchour = rhour;
			loginfo.log_access_rotationsyncclock = log_get_rotationsyncclock( rhour, loginfo.log_access_rotationsyncmin );
			fe_cfg->accesslog_rotationsynchour = rhour;
			LOG_ACCESS_UNLOCK_WRITE();
			break;
		case SLAPD_ERROR_LOG:
			LOG_ERROR_LOCK_WRITE( );
			loginfo.log_error_rotationsynchour = rhour;
			loginfo.log_error_rotationsyncclock = log_get_rotationsyncclock( rhour, loginfo.log_error_rotationsyncmin );
			fe_cfg->errorlog_rotationsynchour = rhour;
			LOG_ERROR_UNLOCK_WRITE();
			break;
		case SLAPD_AUDIT_LOG:
			LOG_AUDIT_LOCK_WRITE( );
			loginfo.log_audit_rotationsynchour = rhour;
			loginfo.log_audit_rotationsyncclock = log_get_rotationsyncclock( rhour, loginfo.log_audit_rotationsyncmin );
			fe_cfg->auditlog_rotationsynchour = rhour;
			LOG_AUDIT_UNLOCK_WRITE();
			break;
		default:
			rv = 1;
	}

	return rv;
}

int 
log_set_rotationsyncmin(const char *attrname, char *rmin_str, int logtype, char *returntext, int apply)
{
	int	rmin = -1;
	int	rv = LDAP_SUCCESS;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	
	if ( logtype != SLAPD_ACCESS_LOG &&
		 logtype != SLAPD_ERROR_LOG &&
		 logtype != SLAPD_AUDIT_LOG ) {
	  PR_snprintf( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid log type: %d", attrname, logtype );
	  return LDAP_OPERATIONS_ERROR;
	}
	
	/* return if we aren't doing this for real */
	if ( !apply ) {
	  return rv;
	}

	if ( rmin_str && *rmin_str != '\0' )
		rmin = atol( rmin_str );
	if ( rmin > 59 )
		rmin = rmin % 60;

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		loginfo.log_access_rotationsyncmin = rmin;
		fe_cfg->accesslog_rotationsyncmin = rmin;
		loginfo.log_access_rotationsyncclock = log_get_rotationsyncclock( loginfo.log_access_rotationsynchour, rmin );
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		loginfo.log_error_rotationsyncmin = rmin;
		loginfo.log_error_rotationsyncclock = log_get_rotationsyncclock( loginfo.log_error_rotationsynchour, rmin );
		fe_cfg->errorlog_rotationsyncmin = rmin;
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		loginfo.log_audit_rotationsyncmin = rmin;
		fe_cfg->auditlog_rotationsyncmin = rmin;
		loginfo.log_audit_rotationsyncclock = log_get_rotationsyncclock( loginfo.log_audit_rotationsynchour, rmin );
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		rv = 1;
	}

	return rv;
}

/******************************************************************************
* ROTATION TIME
* Return Values:
*   1    -- fail
*   0    -- success
******************************************************************************/ 
int 
log_set_rotationtime(const char *attrname, char *rtime_str, int logtype, char *returntext, int apply)
{

	int	runit= 0;
	int value, rtime;
	int	rv = LDAP_SUCCESS;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	
	if ( logtype != SLAPD_ACCESS_LOG &&
		 logtype != SLAPD_ERROR_LOG &&
		 logtype != SLAPD_AUDIT_LOG ) {
	  PR_snprintf( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid log type: %d", attrname, logtype );
	  return LDAP_OPERATIONS_ERROR;
	}
	
	/* return if we aren't doing this for real */
	if ( !apply || !rtime_str || !*rtime_str) {
	  return rv;
	}

	rtime = atoi(rtime_str);

	if (0 == rtime) {
	    rtime = -1;	/* Value Range: -1 | 1 to PR_INT32_MAX */
	}
	
	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		loginfo.log_access_rotationtime = rtime;
		runit = loginfo.log_access_rotationunit;
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		loginfo.log_error_rotationtime = rtime;
		runit = loginfo.log_error_rotationunit;
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		loginfo.log_audit_rotationtime = rtime;
		runit = loginfo.log_audit_rotationunit;
		break;
	   default:
		rv = 1;
	}

	/* find out the rotation unit we have se right now */
	if (runit ==  LOG_UNIT_MONTHS) {
		value = 31 * 24 * 60 * 60 * rtime;
	} else if (runit == LOG_UNIT_WEEKS) {
		value = 7 * 24 * 60 * 60 * rtime;
	} else if (runit == LOG_UNIT_DAYS ) {
		value = 24 * 60 * 60 * rtime;
	} else if (runit == LOG_UNIT_HOURS) {
		value = 3600 * rtime;
	} else if (runit == LOG_UNIT_MINS) {
		value =  60 * rtime;
	} else  {
		/* In this case we don't rotate */
		value =  -1;
	}

	if (rtime > 0 && value < 0) {
	    value = PR_INT32_MAX; /* overflown */
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		 fe_cfg->accesslog_rotationtime = rtime;
		 loginfo.log_access_rotationtime_secs = value;
		 LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		 fe_cfg->errorlog_rotationtime = rtime;
		loginfo.log_error_rotationtime_secs = value;
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		 fe_cfg->auditlog_rotationtime = rtime;
		loginfo.log_audit_rotationtime_secs = value;
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		rv = 1;
	}
	return rv;
}
/******************************************************************************
* ROTATION TIME UNIT
* Return Values:
*   1    -- fail
*   0    -- success
******************************************************************************/ 
int log_set_rotationtimeunit(const char *attrname, char *runit, int logtype, char *errorbuf, int apply)
{
  int origvalue = 0, value = 0;
  int runitType;
  int rv = 0;
  
  slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
  
  if ( logtype != SLAPD_ACCESS_LOG &&
	   logtype != SLAPD_ERROR_LOG &&
	   logtype != SLAPD_AUDIT_LOG ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid log type: %d", attrname, logtype );
	return LDAP_OPERATIONS_ERROR;
  }
  
  if ( (strcasecmp(runit, "month") == 0) ||
  	(strcasecmp(runit, "week") == 0) ||
  	(strcasecmp(runit, "day") == 0) ||
  	(strcasecmp(runit, "hour") == 0) || 
  	(strcasecmp(runit, "minute") == 0)) {
	/* all good values */
  } else  {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: unknown unit \"%s\"", attrname, runit );
	rv = LDAP_OPERATIONS_ERROR;
  }
  
  /* return if we aren't doing this for real */
  if ( !apply ) {
	return rv;
  }
  
  switch (logtype) {
  case SLAPD_ACCESS_LOG:
	LOG_ACCESS_LOCK_WRITE( );
	origvalue = loginfo.log_access_rotationtime;
	break;
  case SLAPD_ERROR_LOG:
	LOG_ERROR_LOCK_WRITE( );
	origvalue = loginfo.log_error_rotationtime;
	break;
  case SLAPD_AUDIT_LOG:
	LOG_AUDIT_LOCK_WRITE( );
	origvalue = loginfo.log_audit_rotationtime;
	break;
  default:
	rv = 1;
  }
  
  if (strcasecmp(runit, "month") == 0) {
	runitType = LOG_UNIT_MONTHS;
	value = origvalue * 31 * 24 * 60 * 60;
  } else if (strcasecmp(runit, "week") == 0) {
	runitType = LOG_UNIT_WEEKS;
	value = origvalue * 7 * 24 * 60 * 60;
  } else if (strcasecmp(runit, "day") == 0) {
	runitType = LOG_UNIT_DAYS;
	value = origvalue * 24 * 60 * 60;
  } else if (strcasecmp(runit, "hour") == 0) { 
	runitType = LOG_UNIT_HOURS;
	value = origvalue * 3600;
  } else if (strcasecmp(runit, "minute") == 0) {
	runitType = LOG_UNIT_MINS;
	value = origvalue * 60;
  } else  {
	/* In this case we don't rotate */
	runitType = LOG_UNIT_UNKNOWN;
	value = -1;
  }

  if (origvalue > 0 && value < 0) {
    value = PR_INT32_MAX;	/* overflown */
  }
  
  switch (logtype) {
  case SLAPD_ACCESS_LOG:
	loginfo.log_access_rotationtime_secs = value;
	loginfo.log_access_rotationunit = runitType;
	slapi_ch_free ( (void **) &fe_cfg->accesslog_rotationunit);
	fe_cfg->accesslog_rotationunit = slapi_ch_strdup ( runit );
	LOG_ACCESS_UNLOCK_WRITE();
	break;
  case SLAPD_ERROR_LOG:
	loginfo.log_error_rotationtime_secs = value;
	loginfo.log_error_rotationunit = runitType;
	slapi_ch_free ( (void **) &fe_cfg->errorlog_rotationunit) ;
	fe_cfg->errorlog_rotationunit = slapi_ch_strdup ( runit );
	LOG_ERROR_UNLOCK_WRITE();
	break;
  case SLAPD_AUDIT_LOG:
	loginfo.log_audit_rotationtime_secs = value;
	loginfo.log_audit_rotationunit = runitType;
	slapi_ch_free ( (void **) &fe_cfg->auditlog_rotationunit);
	fe_cfg->auditlog_rotationunit = slapi_ch_strdup ( runit );
	LOG_AUDIT_UNLOCK_WRITE();
	break;
  default:
	rv = 1;
  }
  return rv;
}
/******************************************************************************
* MAXIMUM DISK SPACE
* Return Values:
*   1    -- fail
*   0    -- success
*
* NOTE:
*   The config struct should contain the value in MB and not in bytes.
******************************************************************************/ 
int
log_set_maxdiskspace(const char *attrname, char *maxdiskspace_str, int logtype, char *errorbuf, int apply)
{
	int	rv = 0;
  	PRInt64	mlogsize;	  /* in bytes */
	PRInt64 maxdiskspace; /* in bytes */
	int	s_maxdiskspace;   /* in megabytes */
  
  	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	
	if ( logtype != SLAPD_ACCESS_LOG &&
	   	logtype != SLAPD_ERROR_LOG &&
	   	logtype != SLAPD_AUDIT_LOG ) {
	   	PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid log type: %d", attrname, logtype );
		return LDAP_OPERATIONS_ERROR;
  	}

	if (!apply || !maxdiskspace_str || !*maxdiskspace_str)
		return rv;

	s_maxdiskspace = atoi(maxdiskspace_str);

	/* Disk space are in MB  but store in bytes */
	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		mlogsize = loginfo.log_access_maxlogsize;
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		mlogsize = loginfo.log_error_maxlogsize;
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		mlogsize = loginfo.log_audit_maxlogsize;
		break;
	   default:
		rv = 1;
		mlogsize = -1;
	}
	maxdiskspace = (PRInt64)s_maxdiskspace * LOG_MB_IN_BYTES;
	if (maxdiskspace < 0) {
		maxdiskspace = -1;
	} else if (maxdiskspace < mlogsize) {
		rv = LDAP_OPERATIONS_ERROR;
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: \"%d (MB)\" is less than max log size \"%d (MB)\"",
			attrname, s_maxdiskspace, (int)(mlogsize/LOG_MB_IN_BYTES) );
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		if (rv== 0 && apply) {
		  loginfo.log_access_maxdiskspace = maxdiskspace;  /* in bytes */
		  fe_cfg->accesslog_maxdiskspace = s_maxdiskspace; /* in megabytes */
		}
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		if (rv== 0 && apply) {
		  loginfo.log_error_maxdiskspace = maxdiskspace;  /* in bytes */
		  fe_cfg->errorlog_maxdiskspace = s_maxdiskspace; /* in megabytes */
		}
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		if (rv== 0 && apply) {
		  loginfo.log_audit_maxdiskspace = maxdiskspace;  /* in bytes */
		  fe_cfg->auditlog_maxdiskspace = s_maxdiskspace; /* in megabytes */
		}
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		 PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			"%s: invalid log type (%d) for setting maximum disk space: %d MB\n",
			attrname, logtype, s_maxdiskspace);
		 rv = LDAP_OPERATIONS_ERROR;
	}
	return rv;

}
/******************************************************************************
* MINIMUM FREE SPACE
* Return Values:
*   1    -- fail
*   0    -- success
******************************************************************************/ 
int 
log_set_mindiskspace(const char *attrname, char *minfreespace_str, int logtype, char *errorbuf, int apply)
{
	int	rv=LDAP_SUCCESS;
	int minfreespace;      /* in megabytes */
	PRInt64 minfreespaceB; /* in bytes */
	
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	
	if ( logtype != SLAPD_ACCESS_LOG &&
		 logtype != SLAPD_ERROR_LOG &&
		 logtype != SLAPD_AUDIT_LOG ) {
	  PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid log type: %d", attrname, logtype );
	  rv = LDAP_OPERATIONS_ERROR;
	}

	/* return if we aren't doing this for real */
	if ( !apply || !minfreespace_str || !*minfreespace_str) {
	  return rv;
	}

	minfreespace = atoi(minfreespace_str);

	/* Disk space are in MB  but store in bytes */
	if (minfreespace >= 1 ) {
		minfreespaceB = (PRInt64)minfreespace * LOG_MB_IN_BYTES;
		switch (logtype) {
		   case SLAPD_ACCESS_LOG:
			LOG_ACCESS_LOCK_WRITE( );
			loginfo.log_access_minfreespace = minfreespaceB;
			fe_cfg->accesslog_minfreespace = minfreespace;
			LOG_ACCESS_UNLOCK_WRITE();
			break;
		   case SLAPD_ERROR_LOG:
			LOG_ERROR_LOCK_WRITE( );
			loginfo.log_error_minfreespace = minfreespaceB;
			fe_cfg->errorlog_minfreespace = minfreespace;
			LOG_ERROR_UNLOCK_WRITE();
			break;
		   case SLAPD_AUDIT_LOG:
			LOG_AUDIT_LOCK_WRITE( );
			loginfo.log_audit_minfreespace = minfreespaceB;
			fe_cfg->auditlog_minfreespace = minfreespace;
			LOG_AUDIT_UNLOCK_WRITE();
			break;
		   default:
			rv = 1;
		}
	}
	return rv;
}
/******************************************************************************
* LOG EXPIRATION TIME
* Return Values:
*   1    -- fail
*   0    -- success
******************************************************************************/ 
int 
log_set_expirationtime(const char *attrname, char *exptime_str, int logtype, char *errorbuf, int apply)
{

	int	eunit, value, exptime;
	int	rsec=0;
	int	rv = 0;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	
	if ( logtype != SLAPD_ACCESS_LOG &&
		 logtype != SLAPD_ERROR_LOG &&
		 logtype != SLAPD_AUDIT_LOG ) {
	  PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid log type: %d", attrname, logtype );
	  rv = LDAP_OPERATIONS_ERROR;
	}
	
	/* return if we aren't doing this for real */
	if ( !apply || !exptime_str || !*exptime_str) {
	  return rv;
	}

	exptime = atoi(exptime_str);	/* <= 0: no exptime */

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		loginfo.log_access_exptime = exptime;
		eunit = loginfo.log_access_exptimeunit;
		rsec = loginfo.log_access_rotationtime_secs;
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		loginfo.log_error_exptime = exptime;
		eunit = loginfo.log_error_exptimeunit;
		rsec = loginfo.log_error_rotationtime_secs;
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		loginfo.log_audit_exptime = exptime;
		eunit = loginfo.log_audit_exptimeunit;
		rsec = loginfo.log_audit_rotationtime_secs;
		break;
	   default:
		rv = 1;
		eunit = -1;
	}
	
	value = -1;	/* never expires, by default */
	if (exptime > 0) {
		if (eunit == LOG_UNIT_MONTHS) {
			value = 31 * 24 * 60 * 60 * exptime;
		} else if (eunit == LOG_UNIT_WEEKS) {
			value = 7 * 24 * 60 * 60 * exptime;
		} else if (eunit == LOG_UNIT_DAYS) {
			value = 24 * 60 * 60 * exptime;
		}
	}
	
	if (value > 0 && value < rsec) {
		value = rsec;
	} else if (exptime > 0 && value < -1) {
		/* value is overflown */
		value = PR_INT32_MAX; 
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		loginfo.log_access_exptime_secs = value;
		fe_cfg->accesslog_exptime = exptime;
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		loginfo.log_error_exptime_secs = value;
		fe_cfg->errorlog_exptime = exptime;
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		loginfo.log_audit_exptime_secs = value;
		fe_cfg->auditlog_exptime = exptime;
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		rv = 1;
	}

	return rv;
}
/******************************************************************************
* LOG EXPIRATION TIME UNIT
* Return Values:
*   1    -- fail
*   0    -- success
******************************************************************************/ 
int
log_set_expirationtimeunit(const char *attrname, char *expunit, int logtype, char *errorbuf, int apply)
{
	int	value = 0;
	int	rv = 0;
	int	exptime, rsecs;
	int	*exptimeunitp = NULL;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

	if ( logtype != SLAPD_ACCESS_LOG &&
	   logtype != SLAPD_ERROR_LOG &&
	   logtype != SLAPD_AUDIT_LOG ) {
	  PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
				"%s: invalid log type: %d", attrname, logtype );
	  return LDAP_OPERATIONS_ERROR;
	}

	if ( NULL == expunit ) {
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: NULL value", attrname );
		return LDAP_OPERATIONS_ERROR;
	}

	if  ( (strcasecmp(expunit, "month") == 0)  || 
		(strcasecmp(expunit, "week") == 0) ||
		(strcasecmp(expunit, "day") == 0)) {
		/* we have good values */	
	} else  {
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: invalid time unit \"%s\"", attrname, expunit );
		rv = LDAP_OPERATIONS_ERROR;;
	}
	
	/* return if we aren't doing this for real */
	if ( !apply ) {
	  return rv;
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_WRITE( );
		exptime = loginfo.log_access_exptime;
		rsecs = loginfo.log_access_rotationtime_secs;
		exptimeunitp = &(loginfo.log_access_exptimeunit);
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		exptime = loginfo.log_error_exptime;
		rsecs = loginfo.log_error_rotationtime_secs;
		exptimeunitp = &(loginfo.log_error_exptimeunit);
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		exptime = loginfo.log_audit_exptime;
		rsecs = loginfo.log_audit_rotationtime_secs;
		exptimeunitp = &(loginfo.log_audit_exptimeunit);
		break;
	   default:
		rv = 1;
		exptime = -1;
		rsecs = -1;
	}

	value = -1;
	if (strcasecmp(expunit, "month") == 0) {
		if (exptime > 0) {
			value = 31 * 24 * 60 * 60 * exptime;
		}
		if (exptimeunitp) {
			*exptimeunitp = LOG_UNIT_MONTHS;
		}
	} else if (strcasecmp(expunit, "week") == 0) {
		if (exptime > 0) {
		 	value = 7 * 24 * 60 * 60 * exptime;
		}
		if (exptimeunitp) {
			*exptimeunitp = LOG_UNIT_WEEKS;
		}
	} else if (strcasecmp(expunit, "day") == 0) {
		if (exptime > 0) {
		 	value = 24 * 60 * 60 * exptime;
		}
		if (exptimeunitp) {
			*exptimeunitp = LOG_UNIT_DAYS;
		}
	}

	if ((value > 0) && value < rsecs) {
		value = rsecs;
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		loginfo.log_access_exptime_secs = value;
		slapi_ch_free ( (void **) &(fe_cfg->accesslog_exptimeunit) );
		fe_cfg->accesslog_exptimeunit = slapi_ch_strdup ( expunit );
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		loginfo.log_error_exptime_secs = value;
		slapi_ch_free ( (void **) &(fe_cfg->errorlog_exptimeunit) );
		fe_cfg->errorlog_exptimeunit = slapi_ch_strdup ( expunit );
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		loginfo.log_audit_exptime_secs = value;
		slapi_ch_free ( (void **) &(fe_cfg->auditlog_exptimeunit) );
		fe_cfg->auditlog_exptimeunit = slapi_ch_strdup ( expunit );
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		rv = 1;
	}

	return rv;
}

/******************************************************************************
 * Write title line in log file
 *****************************************************************************/
static void
log_write_title (LOGFD fp)
{
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
	char *buildnum = config_get_buildnum();
	char buff[512];
	int bufflen = sizeof(buff);
	int err = 0;

	PR_snprintf(buff, bufflen, "\t%s B%s\n",
				fe_cfg->versionstring ? fe_cfg->versionstring : CAPBRAND "-Directory/" DS_PACKAGE_VERSION,
				buildnum ? buildnum : "");
	LOG_WRITE_NOW(fp, buff, strlen(buff), 0, err);

	if (fe_cfg->localhost) {
		PR_snprintf(buff, bufflen, "\t%s:%d (%s)\n\n",
				fe_cfg->localhost,
				fe_cfg->security ? fe_cfg->secureport : fe_cfg->port,
				fe_cfg->configdir ? fe_cfg->configdir : "");
	}
	else {
		/* If fe_cfg->localhost is not set, ignore fe_cfg->port since
		 * it is the default and might be misleading.
		 */
		PR_snprintf(buff, bufflen, "\t<host>:<port> (%s)\n\n",
				fe_cfg->configdir ? fe_cfg->configdir : "");
	}
	LOG_WRITE_NOW(fp, buff, strlen(buff), 0, err);
	slapi_ch_free((void **)&buildnum);
}

/******************************************************************************
*  init function for the error log
*  Returns:
*	0	- success
*	1	- fail
******************************************************************************/ 
int error_log_openf( char *pathname, int locked)
{

	int	rv = 0;
	int	logfile_type =0;

	if (!locked) LOG_ERROR_LOCK_WRITE ();
	/* save the file name */
	slapi_ch_free_string(&loginfo.log_error_file);
	loginfo.log_error_file = slapi_ch_strdup(pathname); 		

	/* store the rotation info fiel path name */
	slapi_ch_free_string(&loginfo.log_errorinfo_file);
	loginfo.log_errorinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

	/*
	** Check if we have a log file already. If we have it then
	** we need to parse the header info and update the loginfo
	** struct.
	*/
	logfile_type = log__error_rotationinfof(loginfo.log_errorinfo_file);

	if (log__open_errorlogfile(logfile_type, 1/* got lock*/) != LOG_SUCCESS) {
		rv = 1;
	}

	if (!locked) LOG_ERROR_UNLOCK_WRITE();
	return rv;
	
}
/******************************************************************************
*  init function for the audit log
*  Returns:
*	0	- success
*	1	- fail
******************************************************************************/ 
int 
audit_log_openf( char *pathname, int locked)
{
	
	int	rv=0;
	int	logfile_type = 0;

	if (!locked) LOG_AUDIT_LOCK_WRITE( );

	/* store the path name */
	slapi_ch_free_string(&loginfo.log_audit_file);
	loginfo.log_audit_file = slapi_ch_strdup ( pathname );

	/* store the rotation info file path name */
	slapi_ch_free_string(&loginfo.log_auditinfo_file);
	loginfo.log_auditinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

	/*
	** Check if we have a log file already. If we have it then
	** we need to parse the header info and update the loginfo
	** struct.
	*/
	logfile_type = log__audit_rotationinfof(loginfo.log_auditinfo_file);

	if (log__open_auditlogfile(logfile_type, 1/* got lock*/) != LOG_SUCCESS) {
		rv = 1;
	}

	if (!locked) LOG_AUDIT_UNLOCK_WRITE();

	return rv;
}
/******************************************************************************
* write in the audit log
******************************************************************************/ 
int
slapd_log_audit_proc (
	char	*buffer,
	int	buf_len)
{
	int err;
	if ( (loginfo.log_audit_state & LOGGING_ENABLED) && (loginfo.log_audit_file != NULL) ){
		LOG_AUDIT_LOCK_WRITE( );
		if (log__needrotation(loginfo.log_audit_fdes,
					SLAPD_AUDIT_LOG) == LOG_ROTATE) {
    		if (log__open_auditlogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
	    		LDAPDebug(LDAP_DEBUG_ANY,
    				"LOGINFO: Unable to open audit file:%s\n",
	    			loginfo.log_audit_file,0,0);
    			LOG_AUDIT_UNLOCK_WRITE();
	    		return 0;
			}
			while (loginfo.log_audit_rotationsyncclock <= loginfo.log_audit_ctime) {
				loginfo.log_audit_rotationsyncclock += PR_ABS(loginfo.log_audit_rotationtime_secs);
			}
		}
		if (loginfo.log_audit_state & LOGGING_NEED_TITLE) {
			log_write_title( loginfo.log_audit_fdes);
			loginfo.log_audit_state &= ~LOGGING_NEED_TITLE;
		}
	    LOG_WRITE_NOW(loginfo.log_audit_fdes, buffer, buf_len, 0, err);
   		LOG_AUDIT_UNLOCK_WRITE();
	    return 0;
	}
	return 0;
}
/******************************************************************************
* write in the error log
******************************************************************************/ 
int
slapd_log_error_proc(
    char	*subsystem,	/* omitted if NULL */
    char	*fmt,
    ... )
{
	va_list ap_err;
	va_list ap_file;
	va_start( ap_err, fmt );
	va_start( ap_file, fmt );
	slapd_log_error_proc_internal(subsystem, fmt, ap_err, ap_file);
	va_end(ap_err);
	va_end(ap_file);
	return 0;
}

static int
slapd_log_error_proc_internal(
    char	*subsystem,	/* omitted if NULL */
    char	*fmt,
    va_list ap_err,
    va_list ap_file)
{
	int	rc = 0;

	if ( (loginfo.log_error_state & LOGGING_ENABLED) && (loginfo.log_error_file != NULL) ) {
		LOG_ERROR_LOCK_WRITE( );
		if (log__needrotation(loginfo.log_error_fdes, 
					SLAPD_ERROR_LOG) == LOG_ROTATE) {
			if (log__open_errorlogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
				LOG_ERROR_UNLOCK_WRITE();
				/* shouldn't continue. error is syslog'ed in open_errorlogfile */
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
				return 0;
			}
			while (loginfo.log_error_rotationsyncclock <= loginfo.log_error_ctime) {
				loginfo.log_error_rotationsyncclock += PR_ABS(loginfo.log_error_rotationtime_secs);
			}
		}

		if (!(detached)) { 
			rc = vslapd_log_error( NULL, subsystem, fmt, ap_err, 1 ); 
		} 
		if ( loginfo.log_error_fdes != NULL ) { 
			if (loginfo.log_error_state & LOGGING_NEED_TITLE) {
				log_write_title(loginfo.log_error_fdes);
				loginfo.log_error_state &= ~LOGGING_NEED_TITLE;
			}
			rc = vslapd_log_error( loginfo.log_error_fdes, subsystem, fmt, ap_file, 1 ); 
		} 
		LOG_ERROR_UNLOCK_WRITE();
	} else {
		/* log the problem in the stderr */
    	rc = vslapd_log_error( NULL, subsystem, fmt, ap_err, 0 ); 
	}
	return( rc );
}

/*
 *  Directly write the already formatted message to the error log
 */
static void
vslapd_log_emergency_error(LOGFD fp, const char *msg, int locked)
{
    time_t    tnl;
    long      tz;
    struct tm *tmsp, tms;
    char      tbuf[ TBUFSIZE ];
    char      buffer[SLAPI_LOG_BUFSIZ];
    char      sign;
    int       size;

    tnl = current_time();
#ifdef _WIN32
    {
        struct tm *pt = localtime( &tnl );
        tmsp = &tms;
        memcpy(&tms, pt, sizeof(struct tm) );
    }
#else
    (void)localtime_r( &tnl, &tms );
    tmsp = &tms;
#endif
#ifdef BSD_TIME
    tz = tmsp->tm_gmtoff;
#else /* BSD_TIME */
    tz = - timezone;
    if ( tmsp->tm_isdst ) {
        tz += 3600;
    }
#endif /* BSD_TIME */
    sign = ( tz >= 0 ? '+' : '-' );
    if ( tz < 0 ) {
        tz = -tz;
    }
    (void)strftime( tbuf, (size_t)TBUFSIZE, "%d/%b/%Y:%H:%M:%S", tmsp);
    sprintf( buffer, "[%s %c%02d%02d] - %s", tbuf, sign, (int)( tz / 3600 ), (int)( tz % 3600 ), msg);
    size = strlen(buffer);

    if(!locked)
        LOG_ERROR_LOCK_WRITE();

    slapi_write_buffer((fp), (buffer), (size));
    PR_Sync(fp);

    if(!locked)
        LOG_ERROR_UNLOCK_WRITE();
}

static int
vslapd_log_error(
    LOGFD	fp,
    char	*subsystem,	/* omitted if NULL */
    char	*fmt,
    va_list	ap,
	int     locked )
{
    time_t    tnl;
    long      tz;
    struct tm *tmsp, tms;
    char      tbuf[ TBUFSIZE ];
    char      sign;
    char      buffer[SLAPI_LOG_BUFSIZ];
    int       blen;
    char      *vbuf;
    int       header_len = 0;
    int       err = 0;

    tnl = current_time();
#ifdef _WIN32
    {
        struct tm *pt = localtime( &tnl );
        tmsp = &tms;
        memcpy(&tms, pt, sizeof(struct tm) );
    }
#else
    (void)localtime_r( &tnl, &tms );
    tmsp = &tms;
#endif
#ifdef BSD_TIME
    tz = tmsp->tm_gmtoff;
#else /* BSD_TIME */
    tz = - timezone;
    if ( tmsp->tm_isdst ) {
    tz += 3600;
    }
#endif /* BSD_TIME */
    sign = ( tz >= 0 ? '+' : '-' );
    if ( tz < 0 ) {
    tz = -tz;
    }
    (void)strftime( tbuf, (size_t)TBUFSIZE, "%d/%b/%Y:%H:%M:%S", tmsp);
    sprintf( buffer, "[%s %c%02d%02d]%s%s - ", tbuf, sign, 
                    (int)( tz / 3600 ), (int)( tz % 3600 ),
                    subsystem ? " " : "",
                    subsystem ? subsystem : "");

    /* Bug 561525: to be able to remove timestamp to not over pollute syslog, we may need
        to skip the timestamp part of the message. 
      The size of the header is: 
        the size of the time string
        + size of space 
        + size of one char (sign)
        + size of 2 char 
        + size of 2 char 
        + size of [ 
        + size of ]
    */

    header_len = strlen(tbuf) + 8;

    if ((vbuf = PR_vsmprintf(fmt, ap)) == NULL) {
        return -1;
    }
    blen = strlen(buffer);
    PR_snprintf (buffer+blen, sizeof(buffer)-blen, "%s", vbuf);
    buffer[sizeof(buffer)-1] = '\0';

    if (fp)
#if 0
        LOG_WRITE_NOW(fp, buffer, strlen(buffer), header_len, err);
#else
 do {
	int size = strlen(buffer);
	(err) = 0;
	if ( slapi_write_buffer((fp), (buffer), (size)) != (size) ) 
	{ 
		PRErrorCode prerr = PR_GetError(); 
		syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer)+(header_len) ); 
		(err) = prerr; 
	} 
	/* Should be a flush in here ?? Yes because PR_SYNC doesn't work ! */ 
	PR_Sync(fp); 
	} while (0);
#endif
    else  /* stderr is always unbuffered */
        fprintf(stderr, "%s", buffer);

    if (err) {
        PR_snprintf(buffer, sizeof(buffer),
                    "Writing to the errors log failed.  Exiting...");
        log__error_emergency(buffer, 1, locked);
        /* failed to write to the errors log.  should not continue. */
        g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
    }

    PR_smprintf_free (vbuf);
    return( 0 );
}

int
slapi_log_error( int severity, char *subsystem, char *fmt, ... )
{
    va_list ap1;
    va_list ap2;
    int     rc;

    if ( severity < SLAPI_LOG_MIN || severity > SLAPI_LOG_MAX ) {
        (void)slapd_log_error_proc( subsystem,
                "slapi_log_error: invalid severity %d (message %s)\n",
                severity, fmt );
        return( -1 );
    }

    if (
#ifdef _WIN32
         *module_ldap_debug
#else
         slapd_ldap_debug
#endif
            & slapi_log_map[ severity ] ) {
        va_start( ap1, fmt );
        va_start( ap2, fmt );
        rc = slapd_log_error_proc_internal( subsystem, fmt, ap1, ap2 );
            va_end( ap1 );
            va_end( ap2 );
    } else {
        rc = 0;        /* nothing to be logged --> always return success */
    }

    return( rc );
}

int
slapi_log_error_ext(int severity, char *subsystem, char *fmt, va_list varg1, va_list varg2)
{
    int rc = 0;

    if ( severity < SLAPI_LOG_MIN || severity > SLAPI_LOG_MAX ) {
        (void)slapd_log_error_proc( subsystem, "slapi_log_error: invalid severity %d (message %s)\n",
            severity, fmt );
            return( -1 );
    }

    if (
    #ifdef _WIN32
        *module_ldap_debug
    #else
        slapd_ldap_debug
    #endif
        & slapi_log_map[ severity ] )
    {
	    rc = slapd_log_error_proc_internal( subsystem, fmt, varg1, varg2 );
    } else {
        rc = 0;        /* nothing to be logged --> always return success */
    }

    return( rc );
}

int
slapi_is_loglevel_set ( const int loglevel )
{

    return ( 
#ifdef _WIN32
    *module_ldap_debug
#else
    slapd_ldap_debug
#endif
        & slapi_log_map[ loglevel ] ? 1 : 0);
}


/******************************************************************************
* write in the access log
******************************************************************************/ 
static int vslapd_log_access(char *fmt, va_list ap)
{
    time_t	tnl;
    long	tz;
    struct tm	*tmsp, tms;
    char	tbuf[ TBUFSIZE ];
    char	sign;
    char	buffer[SLAPI_LOG_BUFSIZ];
    char	vbuf[SLAPI_LOG_BUFSIZ];
    int		blen, vlen;
    /* info needed to keep us from calling localtime/strftime so often: */
    static time_t	old_time = 0;
    static char		old_tbuf[TBUFSIZE];
	static int old_blen = 0;

    tnl = current_time();

    /* check if we can use the old strftime buffer */
    PR_Lock(ts_time_lock);
    if (tnl == old_time) {
	strcpy(buffer, old_tbuf);
	blen = old_blen;
	PR_Unlock(ts_time_lock);
    } else {
	/* nope... painstakingly create the new strftime buffer */
#ifdef _WIN32
        {
            struct tm *pt = localtime( &tnl );
            tmsp = &tms;
            memcpy(&tms, pt, sizeof(struct tm) );
        }
#else
	(void)localtime_r( &tnl, &tms );
	tmsp = &tms;
#endif

#ifdef BSD_TIME
	tz = tmsp->tm_gmtoff;
#else /* BSD_TIME */
	tz = - timezone;
	if ( tmsp->tm_isdst ) {
	    tz += 3600;
	}
#endif /* BSD_TIME */
	sign = ( tz >= 0 ? '+' : '-' );
	if ( tz < 0 ) {
	    tz = -tz;
	}
	(void)strftime( tbuf, (size_t)TBUFSIZE, "%d/%b/%Y:%H:%M:%S", tmsp);
	sprintf( buffer, "[%s %c%02d%02d] ", tbuf, sign, 
		 (int)( tz / 3600 ), (int)( tz % 3600));
	old_time = tnl;
	strcpy(old_tbuf, buffer);
	blen = strlen(buffer);
	old_blen = blen;
	PR_Unlock(ts_time_lock);
    }

	vlen = PR_vsnprintf(vbuf, SLAPI_LOG_BUFSIZ, fmt, ap);
    if (! vlen) {
		return -1;
    }
    
    if (SLAPI_LOG_BUFSIZ - blen < vlen) {
		return -1;
    }

    log_append_buffer2(tnl, loginfo.log_access_buffer, buffer, blen, vbuf, vlen);    

    return( 0 );
}

int
slapi_log_access( int level,
    char	*fmt,
    ... )
{
	va_list	ap;
	int	rc=0;

	if (!(loginfo.log_access_state & LOGGING_ENABLED)) {
		return 0;
	}
	va_start( ap, fmt );
	if (( level & loginfo.log_access_level ) && 
			( loginfo.log_access_fdes != NULL ) && (loginfo.log_access_file != NULL) ) { 
   	    rc = vslapd_log_access(fmt, ap);
	} 
	va_end( ap );
	return( rc );
}

/******************************************************************************
* access_log_openf
*
* 	Open the access log file
*	
*  Returns:
*	0 -- success
*       1 -- fail
******************************************************************************/ 
int access_log_openf(char *pathname, int locked)
{
	int	rv=0;
	int	logfile_type = 0;

	if (!locked) LOG_ACCESS_LOCK_WRITE( );

	/* store the path name */
	slapi_ch_free_string(&loginfo.log_access_file);
	loginfo.log_access_file = slapi_ch_strdup ( pathname );

	/* store the rotation info fiel path name */
	slapi_ch_free_string(&loginfo.log_accessinfo_file);
	loginfo.log_accessinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

	/*
	** Check if we have a log file already. If we have it then
	** we need to parse the header info and update the loginfo
	** struct.
	*/
	logfile_type = log__access_rotationinfof(loginfo.log_accessinfo_file);

	if (log__open_accesslogfile(logfile_type, 1/* got lock*/) != LOG_SUCCESS) {
		rv = 1;
	}

	if (!locked) LOG_ACCESS_UNLOCK_WRITE();


	return rv;
}

/******************************************************************************
* log__open_accesslogfile
*
*	Open a new log file. If we have run out of the max logs we can have
*	then delete the oldest file.
******************************************************************************/ 
static int
log__open_accesslogfile(int logfile_state, int locked)
{

	time_t			now;
	LOGFD			fp;
	LOGFD			fpinfo = NULL;
	char			tbuf[TBUFSIZE];
	struct logfileinfo	*logp;
	char			buffer[BUFSIZ];

	if (!locked) LOG_ACCESS_LOCK_WRITE( );

	/* 
	** Here we are trying to create a new log file.
	** If we alredy have one, then we need to rename it as
	** "filename.time",  close it and update it's information
	** in the array stack.
	*/
	if (loginfo.log_access_fdes != NULL) {
		struct logfileinfo *log;
		char               newfile[BUFSIZ];
		PRInt64            f_size;

		/* get rid of the old one */
		if ((f_size = log__getfilesize(loginfo.log_access_fdes)) == -1) {
			/* Then assume that we have the max size (in bytes) */
			f_size = loginfo.log_access_maxlogsize;
		}

		/* Check if I have to delete any old file, delete it if it is required. 
		** If there is just one file, then  access and access.rotation files 
		** are deleted. After that we start fresh
		*/
		while (log__delete_access_logfile());

		/* close the file */
		LOG_CLOSE(loginfo.log_access_fdes);
		/*
		 * loginfo.log_access_fdes is not set to NULL here, otherwise
		 * slapi_log_access() will not send a message to the access log
		 * if it is called between this point and where this field is
		 * set again after calling LOG_OPEN_APPEND.
		 */
		if ( loginfo.log_access_maxnumlogs > 1 ) {
			log = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			log->l_ctime = loginfo.log_access_ctime;
			log->l_size = f_size;

			log_convert_time (log->l_ctime, tbuf, 1 /*short */);
			PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_access_file, tbuf);
			if (PR_Rename (loginfo.log_access_file, newfile) != PR_SUCCESS) {
				PRErrorCode prerr = PR_GetError();
				/* Make "FILE EXISTS" error an exception.
				   Even if PR_Rename fails with the error, we continue logging.
				 */
				if (PR_FILE_EXISTS_ERROR != prerr) {
					loginfo.log_access_fdes = NULL;
    					if (!locked)  LOG_ACCESS_UNLOCK_WRITE();
					slapi_ch_free((void**)&log);
					return LOG_UNABLE_TO_OPENFILE;
				}
			}
			/* add the log to the chain */
			log->l_next = loginfo.log_access_logchain;
			loginfo.log_access_logchain = log;
			loginfo.log_numof_access_logs++;
		}
	} 

	/* open a new log file */
	if (! LOG_OPEN_APPEND(fp, loginfo.log_access_file, loginfo.log_access_mode)) {
		int oserr = errno;
		loginfo.log_access_fdes = NULL;
		if (!locked)  LOG_ACCESS_UNLOCK_WRITE();
		LDAPDebug(LDAP_DEBUG_ANY, "access file open %s failed errno %d (%s)\n",
				  loginfo.log_access_file, oserr, slapd_system_strerror(oserr));
		return LOG_UNABLE_TO_OPENFILE;
	}

	loginfo.log_access_fdes = fp;
	if (logfile_state == LOGFILE_REOPENED) {
		/* we have all the information */
		if (!locked) LOG_ACCESS_UNLOCK_WRITE( );
		return LOG_SUCCESS;
	}

	loginfo.log_access_state |= LOGGING_NEED_TITLE;

	if (! LOG_OPEN_WRITE(fpinfo, loginfo.log_accessinfo_file, loginfo.log_access_mode)) {
		int oserr = errno;
		if (!locked) LOG_ACCESS_UNLOCK_WRITE();
		LDAPDebug( LDAP_DEBUG_ANY, "accessinfo file open %s failed errno %d (%s)\n",
					    loginfo.log_accessinfo_file,
				            oserr, slapd_system_strerror(oserr));
		return LOG_UNABLE_TO_OPENFILE;
	}


	/* write the header in the log */
	now = current_time();
	log_convert_time (now, tbuf, 2 /* long */);
	PR_snprintf (buffer,sizeof(buffer),"LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
	LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

	logp = loginfo.log_access_logchain;
	while ( logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short*/);
		PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%"
			NSPRI64 "d)\n", PREVLOGFILE, loginfo.log_access_file, tbuf, 
			logp->l_ctime, logp->l_size);
		LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
		logp = logp->l_next;
	}
	/* Close the info file. We need only when we need to rotate to the
	** next log file.
	*/
	if (fpinfo)  LOG_CLOSE(fpinfo);

	/* This is now the current access log */
	loginfo.log_access_ctime = now;

	if (!locked) LOG_ACCESS_UNLOCK_WRITE( );
	return LOG_SUCCESS;
}
/******************************************************************************
* log__needrotation
*
*	Do we need to rotate the log file ?
*	Find out based on rotation time and the max log size;
*
*  Return:
*	LOG_CONTINUE    	-- Use the same one
*	LOG_ROTATE  		-- log need to be rotated
*
* Note:
*	A READ LOCK is obtained.
********************************************************************************/
#define LOG_SIZE_EXCEEDED 	1
#define LOG_EXPIRED		2 
static int
log__needrotation(LOGFD fp, int logtype)
{
	time_t	curr_time;
	time_t	log_createtime= 0;
	time_t	syncclock = 0;
	int	type = LOG_CONTINUE;
	PRInt64	f_size = 0;
	PRInt64	maxlogsize;
	int	nlogs;
	int	rotationtime_secs = -1;
	int	sync_enabled = 0, timeunit = 0;

	if (fp == NULL) {
		return LOG_ROTATE;
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		nlogs = loginfo.log_access_maxnumlogs;
		maxlogsize = loginfo.log_access_maxlogsize;
		sync_enabled = loginfo.log_access_rotationsync_enabled;
		syncclock = loginfo.log_access_rotationsyncclock;
		timeunit = loginfo.log_access_rotationunit;
		rotationtime_secs = loginfo.log_access_rotationtime_secs;
		log_createtime = loginfo.log_access_ctime;
		break;
 	   case SLAPD_ERROR_LOG:
		nlogs = loginfo.log_error_maxnumlogs;
		maxlogsize = loginfo.log_error_maxlogsize;
		sync_enabled = loginfo.log_error_rotationsync_enabled;
		syncclock = loginfo.log_error_rotationsyncclock;
		timeunit = loginfo.log_error_rotationunit;
		rotationtime_secs = loginfo.log_error_rotationtime_secs;
		log_createtime = loginfo.log_error_ctime;
		break;
	   case SLAPD_AUDIT_LOG:
		nlogs = loginfo.log_audit_maxnumlogs;
		maxlogsize = loginfo.log_audit_maxlogsize;
		sync_enabled = loginfo.log_audit_rotationsync_enabled;
		syncclock = loginfo.log_audit_rotationsyncclock;
		timeunit = loginfo.log_audit_rotationunit;
		rotationtime_secs = loginfo.log_audit_rotationtime_secs;
		log_createtime = loginfo.log_audit_ctime;
		break;
	   default: /* error */
		maxlogsize = -1;
		nlogs = 1;
		
	}

	/* If we have one log then can't rotate at all */
	if (nlogs == 1)
		return LOG_CONTINUE;

	if ((f_size = log__getfilesize(fp)) == -1) {
		/* The next option is to rotate based on the rotation time */
		f_size = 0;
	}

	/* If the log size is more than the limit, then it's time to rotate. */
	if ((maxlogsize > 0) && (f_size >= maxlogsize)) {
		type = LOG_SIZE_EXCEEDED;
		goto log_rotate;
	}

	/* If rotation interval <= 0 then can't rotate by time */
	if (rotationtime_secs <= 0)
		return LOG_CONTINUE;
		
	/*
	** If the log is older than the time allowed to be active,
	** then it's time to move on (i.e., rotate).
	*/
	time (&curr_time);

	if ( !sync_enabled || timeunit == LOG_UNIT_HOURS || timeunit == LOG_UNIT_MINS ) {
		if (curr_time - log_createtime > rotationtime_secs) {
			type = LOG_EXPIRED;
			goto log_rotate;
		}

	} else if (curr_time > syncclock) {
		type = LOG_EXPIRED;
		goto log_rotate;
	}

log_rotate:
	/*
	** Don't send messages to the error log whilst we're rotating it.
	** This'll lead to a recursive call to the logging function, and
	** an assertion trying to relock the write lock.
	*/
	if (logtype!=SLAPD_ERROR_LOG)
	{
		if (type == LOG_SIZE_EXCEEDED) {
			LDAPDebug (LDAP_DEBUG_TRACE,
				   "LOGINFO:End of Log because size exceeded(Max:%" 
				   NSPRI64 "d bytes) (Is:%" NSPRI64 "d bytes)\n",
				   maxlogsize, f_size, 0);
		} else  if ( type == LOG_EXPIRED) {
			LDAPDebug(LDAP_DEBUG_TRACE,
				   "LOGINFO:End of Log because time exceeded(Max:%d secs) (Is:%ld secs)\n",
					rotationtime_secs, curr_time - log_createtime,0);
		}
	}
	return (type == LOG_CONTINUE) ? LOG_CONTINUE : LOG_ROTATE;
}

/******************************************************************************
* log__delete_access_logfile
*
*	Do we need to delete  a logfile. Find out if we need to delete the log
*	file based on expiration time, max diskspace, and minfreespace. 
*	Delete the file if we need to.
*
*	Assumption: A WRITE lock has been acquired for the ACCESS
******************************************************************************/ 

static int
log__delete_access_logfile()
{

	struct logfileinfo *logp = NULL;
	struct logfileinfo *delete_logp = NULL;
	struct logfileinfo *p_delete_logp = NULL;
	struct logfileinfo *prev_logp = NULL;
	PRInt64            total_size=0;
	time_t             cur_time;
	PRInt64            f_size;
	int                numoflogs=loginfo.log_numof_access_logs;
	int                rv = 0;
	char               *logstr;
	char               buffer[BUFSIZ];
	char               tbuf[TBUFSIZE];
	
	/* If we have only one log, then  will delete this one */
	if (loginfo.log_access_maxnumlogs == 1) {
		LOG_CLOSE(loginfo.log_access_fdes);
                loginfo.log_access_fdes = NULL;
		PR_snprintf (buffer, sizeof(buffer), "%s", loginfo.log_access_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s\n", loginfo.log_access_file,0,0);
		}

		/* Delete the rotation file also. */
		PR_snprintf (buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_access_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s.rotationinfo\n", loginfo.log_access_file,0,0);
		}
		return 0;
	}

	/* If we have already the maximum number of log files, we
	** have to delete one any how.
	*/
	if (++numoflogs > loginfo.log_access_maxnumlogs) {
		logstr = "Exceeded max number of logs allowed";
		goto delete_logfile;
	}

	/* Now check based on the maxdiskspace */
	if (loginfo.log_access_maxdiskspace > 0) {
		logp = loginfo.log_access_logchain;
		while (logp) {
			total_size += logp->l_size;
			logp = logp->l_next;
		}
		if ((f_size = log__getfilesize(loginfo.log_access_fdes)) == -1) {
			/* then just assume the max size */
			total_size += loginfo.log_access_maxlogsize;
		} else {
			total_size += f_size;
		}

		/* If we have exceeded the max disk space or we have less than the
  		** minimum, then we have to delete a file.
		*/
		if (total_size >= loginfo.log_access_maxdiskspace)  {
			logstr = "exceeded maximum log disk space";
			goto delete_logfile;
		}
	}
	
	/* Now check based on the free space */
	if ( loginfo.log_access_minfreespace > 0) {
		rv = log__enough_freespace(loginfo.log_access_file);
		if ( rv == 0) {
			/* Not enough free space */
			logstr = "Not enough free disk space";
			goto delete_logfile;
		}
	}

	/* Now check based on the expiration time */
	if ( loginfo.log_access_exptime_secs > 0 ) {
		/* is the file old enough */
		time (&cur_time);
		prev_logp = logp = loginfo.log_access_logchain;
		while (logp) {
			if ((cur_time - logp->l_ctime) > loginfo.log_access_exptime_secs) {
				delete_logp = logp;
				p_delete_logp = prev_logp;
				logstr = "The file is older than the log expiration time";
				goto delete_logfile;
			}
			prev_logp = logp;
			logp = logp->l_next;
		}
	}

	/* No log files to delete */	
	return 0;

delete_logfile:
	if (delete_logp == NULL) {
		time_t	oldest;

		time(&oldest);
		
		prev_logp = logp = loginfo.log_access_logchain;
		while (logp) {
			if (logp->l_ctime <= oldest) {
				oldest = logp->l_ctime;
				delete_logp = logp;
				p_delete_logp = prev_logp;
			}
			prev_logp = logp;
			logp = logp->l_next;
		}
		/* We might face this case if we have only one log file and
		** trying to delete it because of deletion requirement.
		*/
		if (!delete_logp) {
			return 0;
		}
	} 
	
	if (p_delete_logp == delete_logp) {
		/* then we are deleteing the first one */
		loginfo.log_access_logchain = delete_logp->l_next;
	} else {
		p_delete_logp->l_next = delete_logp->l_next;
	}


	/* Delete the access file */
	log_convert_time (delete_logp->l_ctime, tbuf, 1 /*short */);
	PR_snprintf (buffer, sizeof(buffer), "%s.%s", loginfo.log_access_file, tbuf);
	if (PR_Delete(buffer) != PR_SUCCESS) {
		LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s.%s\n",
				   loginfo.log_access_file,tbuf,0);

	} else {
		LDAPDebug(LDAP_DEBUG_TRACE, 
			   "LOGINFO:Removed file:%s.%s because of (%s)\n",
					loginfo.log_access_file, tbuf,
					logstr);
	}
	slapi_ch_free((void**)&delete_logp);
	loginfo.log_numof_access_logs--;
    
	return 1;
}

/*
 *  This function is used by the disk monitoring thread (daemon.c)
 *
 *  When we get close to running out of disk space we delete the rotated logs
 *  as a last resort to help keep the server up and running.
 */
void
log__delete_rotated_logs()
{
	struct logfileinfo *logp = NULL;
	char buffer[BUFSIZ];
	char tbuf[TBUFSIZE];

	/*
	 *  Access Log
	 */
	logp = loginfo.log_access_logchain;
	while (logp) {
		tbuf[0] = buffer[0] = '\0';
		log_convert_time (logp->l_ctime, tbuf, 1);
		PR_snprintf (buffer, sizeof(buffer), "%s.%s", loginfo.log_access_file, tbuf);

		LDAPDebug(LDAP_DEBUG_ANY,"Deleted Rotated Log: %s\n",buffer,0,0);  /* MARK */

		if (PR_Delete(buffer) != PR_SUCCESS) {
			logp = logp->l_next;
			continue;
		}
		loginfo.log_numof_access_logs--;
		logp = logp->l_next;
	}
	/*
	 *  Audit Log
	 */
	logp = loginfo.log_audit_logchain;
	while (logp) {
		tbuf[0] = buffer[0] = '\0';
		log_convert_time (logp->l_ctime, tbuf, 1);
		PR_snprintf (buffer, sizeof(buffer), "%s.%s", loginfo.log_audit_file, tbuf);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			logp = logp->l_next;
			continue;
		}
		loginfo.log_numof_audit_logs--;
		logp = logp->l_next;
	}
	/*
	 *  Error log
	 */
	logp = loginfo.log_error_logchain;
	while (logp) {
		tbuf[0] = buffer[0] = '\0';
		log_convert_time (logp->l_ctime, tbuf, 1);
		PR_snprintf (buffer, sizeof(buffer), "%s.%s", loginfo.log_error_file, tbuf);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			logp = logp->l_next;
			continue;
		}
		loginfo.log_numof_error_logs--;
		logp = logp->l_next;
	}
}

#define ERRORSLOG 1
#define ACCESSLOG 2
#define AUDITLOG  3

static int
log__fix_rotationinfof(char *pathname)
{
	char		*logsdir = NULL;
	time_t		now;
	PRDir		*dirptr = NULL;
	PRDirEntry	*dirent = NULL;
	PRDirFlags	dirflags = PR_SKIP_BOTH & PR_SKIP_HIDDEN;
	char		*log_type = NULL;
	int			log_type_id;
	int			rval = LOG_ERROR;
	char		*p;
	char		*rotated_log = NULL;
	int			rotated_log_len = 0;

	/* rotation info file is broken; can't trust the contents */
	time (&now);
	loginfo.log_error_ctime = now;
	logsdir = slapi_ch_strdup(pathname);
	p = strrchr(logsdir, _PSEP);
	if (NULL == p) /* pathname is not path/filename.rotationinfo; do nothing */
		goto done;

	*p = '\0';
	log_type = ++p;
	p = strchr(log_type, '.');
	if (NULL == p) /* file is not rotationinfo; do nothing */
		goto done;
	*p = '\0';

	if (0 == strcmp(log_type, "errors"))
		log_type_id = ERRORSLOG;
	else if (0 == strcmp(log_type, "access"))
		log_type_id = ACCESSLOG;
	else if (0 == strcmp(log_type, "audit"))
		log_type_id = AUDITLOG;
	else 
		goto done; /* file is not errors nor access nor audit; do nothing */

	if (!(dirptr = PR_OpenDir(logsdir)))
		goto done;

	switch (log_type_id) {
	case ERRORSLOG:
		loginfo.log_numof_error_logs = 0;
		loginfo.log_error_logchain = NULL;
		break;
	case ACCESSLOG:
		loginfo.log_numof_access_logs = 0;
		loginfo.log_access_logchain = NULL;
		break;
	case AUDITLOG:
		loginfo.log_numof_audit_logs = 0;
		loginfo.log_audit_logchain = NULL;
		break;
	}
	/* length of (pathname + .YYYYMMDD-hhmmss)
	 * pathname includes ".rotationinfo", but that's fine. */
	rotated_log_len = strlen(pathname) + 17;
	rotated_log = (char *)slapi_ch_malloc(rotated_log_len);
	/* read the directory entries into a linked list */
	for (dirent = PR_ReadDir(dirptr, dirflags); dirent ;
		dirent = PR_ReadDir(dirptr, dirflags)) {
		if (0 == strcmp(log_type, dirent->name)) {
			switch (log_type_id) {
			case ERRORSLOG:
				loginfo.log_numof_error_logs++;
				break;
			case ACCESSLOG:
				loginfo.log_numof_access_logs++;
				break;
			case AUDITLOG:
				loginfo.log_numof_audit_logs++;
				break;
			}
		} else if (0 == strncmp(log_type, dirent->name, strlen(log_type)) &&
			(p = strrchr(dirent->name, '.')) != NULL &&
			15 == strlen(++p) &&
			NULL != strchr(p, '-')) { /* e.g., errors.20051123-165135 */
			struct	logfileinfo	*logp;
			char *q;
			int ignoreit = 0;

			for (q = p; q && *q; q++) {
				if (*q != '-' && !isdigit(*q))
					ignoreit = 1;
			}
			if (ignoreit)
				continue;

			logp = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			logp->l_ctime = log_reverse_convert_time(p);

			PR_snprintf(rotated_log, rotated_log_len, "%s/%s",
							logsdir, dirent->name);
			switch (log_type_id) {
			case ERRORSLOG:
				logp->l_size = log__getfilesize_with_filename(rotated_log);
				logp->l_next = loginfo.log_error_logchain;
				loginfo.log_error_logchain = logp;
				loginfo.log_numof_error_logs++;
				break;
			case ACCESSLOG:
				logp->l_size = log__getfilesize_with_filename(rotated_log);
				logp->l_next = loginfo.log_access_logchain;
				loginfo.log_access_logchain = logp;
				loginfo.log_numof_access_logs++;
				break;
			case AUDITLOG:
				logp->l_size = log__getfilesize_with_filename(rotated_log);
				logp->l_next = loginfo.log_audit_logchain;
				loginfo.log_audit_logchain = logp;
				loginfo.log_numof_audit_logs++;
				break;
			}
		}
	}
	rval = LOG_SUCCESS;
done:
	if (NULL != dirptr)
		PR_CloseDir(dirptr);
	slapi_ch_free_string(&logsdir);
	slapi_ch_free_string(&rotated_log);
	return rval;
}
#undef ERRORSLOG
#undef ACCESSLOG
#undef AUDITLOG

/******************************************************************************
* log__access_rotationinfof
*
*	Try to open the log file. If we have one already, then try to read the
*	header and update the information.
*
*	Assumption: Lock has been acquired already
******************************************************************************/ 
static int
log__access_rotationinfof(char *pathname)
{
	long	f_ctime;
	PRInt64	f_size;
	int		main_log = 1;
	time_t	now;
	FILE	*fp;
	int		rval, logfile_type = LOGFILE_REOPENED;
	
	/*
	** Okay -- I confess, we want to use NSPR calls but I want to
	** use fgets and not use PR_Read() and implement a complicated
	** parsing module. Since this will be called only during the startup
	** and never aftre that, we can live by it.
	*/
	
	if ((fp = fopen (pathname, "r")) == NULL) {
		return LOGFILE_NEW;
	}

	loginfo.log_numof_access_logs = 0;

	/* 
	** We have reopened the log access file. Now we need to read the
	** log file info and update the values.
	*/
	while ((rval = log__extract_logheader(fp, &f_ctime, &f_size)) == LOG_CONTINUE) {
		/* first we would get the main log info */
		if (f_ctime == 0 && f_size == 0)
			continue;

		time (&now);
		if (main_log) {
			if (f_ctime > 0L)
				loginfo.log_access_ctime = f_ctime;
			else {
				loginfo.log_access_ctime = now;
			}
			main_log = 0;
		} else {
			struct	logfileinfo	*logp;

			logp = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			if (f_ctime > 0L)
				logp->l_ctime = f_ctime;
			else
				logp->l_ctime = now;
			if (f_size > 0)
				logp->l_size = f_size;
			else  {
				/* make it the max log size */
				logp->l_size = loginfo.log_access_maxlogsize;
			}

			logp->l_next = loginfo.log_access_logchain;
			loginfo.log_access_logchain = logp;
		}
		loginfo.log_numof_access_logs++;
	}
	if (LOG_DONE == rval)
		rval = log__check_prevlogs(fp, pathname);
	fclose (fp);

	if (LOG_ERROR == rval)
		if (LOG_SUCCESS == log__fix_rotationinfof(pathname))
			logfile_type = LOGFILE_NEW;

	/* Check if there is a rotation overdue */
	if (loginfo.log_access_rotationsync_enabled &&
		loginfo.log_access_rotationunit != LOG_UNIT_HOURS &&
		loginfo.log_access_rotationunit != LOG_UNIT_MINS &&
		loginfo.log_access_ctime < loginfo.log_access_rotationsyncclock - PR_ABS(loginfo.log_access_rotationtime_secs)) {
		loginfo.log_access_rotationsyncclock -= PR_ABS(loginfo.log_access_rotationtime_secs);
	}
	return logfile_type;
}

/*
* log__check_prevlogs
* 
* check if a given prev log file (e.g., /var/log/dirsrv/slapd-fe/logs/errors.20051201-101347)
* is found in the rotationinfo file.
*/
static int
log__check_prevlogs (FILE *fp, char *pathname)
{
	char		buf[BUFSIZ], *p;
	char		*logsdir = NULL;
	int			rval = LOG_CONTINUE;
	char		*log_type = NULL;
	PRDir		*dirptr = NULL;
	PRDirEntry	*dirent = NULL;
	PRDirFlags	dirflags = PR_SKIP_BOTH & PR_SKIP_HIDDEN;

	logsdir = slapi_ch_strdup(pathname);
	p = strrchr(logsdir, _PSEP);
	if (NULL == p) /* pathname is not path/filename.rotationinfo; do nothing */
		goto done;

	*p = '\0';
	log_type = ++p;
	p = strchr(log_type, '.');
	if (NULL == p) /* file is not rotationinfo; do nothing */
		goto done;
	*p = '\0';

	if (0 != strcmp(log_type, "errors") &&
		0 != strcmp(log_type, "access") &&
		0 != strcmp(log_type, "audit"))
		goto done; /* file is not errors nor access nor audit; do nothing */

	if (!(dirptr = PR_OpenDir(logsdir)))
		goto done;

	for (dirent = PR_ReadDir(dirptr, dirflags); dirent ;
		dirent = PR_ReadDir(dirptr, dirflags)) {
		if (0 == strncmp(log_type, dirent->name, strlen(log_type)) &&
			(p = strrchr(dirent->name, '.')) != NULL &&
			15 == strlen(++p) &&
			NULL != strchr(p, '-')) { /* e.g., errors.20051123-165135 */
			char *q;
			int ignoreit = 0;

			for (q = p; q && *q; q++) {
				if (*q != '-' && !isdigit(*q))
					ignoreit = 1;
			}
			if (ignoreit)
				continue;

			fseek(fp, 0 ,SEEK_SET);
			buf[BUFSIZ-1] = '\0';
			rval = LOG_ERROR; /* pessmistic default */
			while (fgets(buf, BUFSIZ - 1, fp)) {
				if (strstr(buf, dirent->name)) {
					rval = LOG_CONTINUE;	/* found in .rotationinfo */
					break;
				}
			}
			if(LOG_ERROR == rval) {
				goto done;
			}
		}
	}
done:
	if (NULL != dirptr)
		PR_CloseDir(dirptr);
	slapi_ch_free_string(&logsdir);
	return rval;
}

/******************************************************************************
* log__extract_logheader
*
*	Extract each LOGINFO heder line. From there extract the time and
*	size info of all the old log files.
******************************************************************************/ 
static int
log__extract_logheader (FILE *fp, long *f_ctime, PRInt64 *f_size)
{

	char		buf[BUFSIZ];
	char		*p, *s, *next;

	if (NULL == f_ctime || NULL == f_size) {
		return LOG_ERROR;
	}
	*f_ctime = 0L;
	*f_size = 0L;

	if ( fp == NULL)
		return LOG_ERROR;

	buf[BUFSIZ-1] = '\0'; /* for safety */
	if (fgets(buf, BUFSIZ - 1, fp) == NULL) {
		return LOG_DONE;
	}

	if ((p=strstr(buf, "LOGINFO")) == NULL) {
		return LOG_ERROR;
	}

	s = p;
	if ((p = strchr(p, '(')) == NULL) {
		return LOG_CONTINUE;
	}
	if ((next= strchr(p, ')')) == NULL) {
		return LOG_CONTINUE;
	}

	p++;
	s = next;
	next++;
	*s = '\0';
	
	/* Now p must hold the ctime value */
	*f_ctime = strtol(p, (char **)NULL, 0); 

	if ((p = strchr(next, '(')) == NULL) {
		/* that's fine -- it means we have no size info */
		*f_size = 0L;
		return LOG_CONTINUE;
	}

	if ((next= strchr(p, ')')) == NULL) {
		return LOG_CONTINUE;
	}

	p++;
	*next = '\0';
	
	/* Now p must hold the size value */
	*f_size = strtoll(p, (char **)NULL, 0);

	/* check if the Previous Log file really exists */
	if ((p = strstr(buf, PREVLOGFILE)) != NULL) {
		p += strlen(PREVLOGFILE);
		s = strchr(p, ' ');
		if (NULL == s) {
			s = strchr(p, '(');
			if (NULL != s) {
				*s = '\0';
			}
		} else {
			*s = '\0';
		}
		if (PR_SUCCESS != PR_Access(p, PR_ACCESS_EXISTS)) {
			return LOG_ERROR;
		}
	}

	return LOG_CONTINUE;
	
}

/******************************************************************************
* log__getfilesize
*	Get the file size
*
* Assumption: Lock has been acquired already.
******************************************************************************/ 
/* this kinda has to be diff't on each platform :( */
/* using an int implies that all logfiles will be under 2G.  this is 
 * probably a safe assumption for now.
 */
#ifdef XP_WIN32
static PRInt64 
log__getfilesize(LOGFD fp)
{
	struct stat	info;
	int		rv;

	if ((rv = fstat(fileno(fp), &info)) != 0) {
		return -1;
	}
	return (PRInt64)info.st_size;
} 
#else
static PRInt64
log__getfilesize(LOGFD fp)
{
	PRFileInfo64      info;
 
	if (PR_GetOpenFileInfo64 (fp, &info) == PR_FAILURE) {
		return -1;
	}
	return (PRInt64)info.size;	/* type of size is PROffset64 */
}

static PRInt64
log__getfilesize_with_filename(char *filename)
{
	PRFileInfo64      info;
 
	if (PR_GetFileInfo64 ((const char *)filename, &info) == PR_FAILURE) {
		return -1;
	}
	return (PRInt64)info.size;	/* type of size is PROffset64 */
}
#endif


/******************************************************************************
* log__enough_freespace
* 
* Returns:
*	1	- we have enough space
*	0	- No the avialable space is less than recomended
* Assumption: Lock has been acquired already.
******************************************************************************/ 
static int
log__enough_freespace(char  *path)
{

#ifdef _WIN32
DWORD	sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
char rootpath[4];
#else
#ifdef LINUX
	struct  statfs		buf;
#else
	struct	statvfs		buf;
#endif   /* LINUX */
#endif
	PRInt64			freeBytes;
	PRInt64 tmpval;


#ifdef _WIN32
    strncpy(rootpath, path, 3);
    rootpath[3] = '\0';
    /* we should consider using GetDiskFreeSpaceEx here someday */
	if ( !GetDiskFreeSpace(rootpath, &sectorsPerCluster, &bytesPerSector,
				&freeClusters, &totalClusters)) {
		LDAPDebug(LDAP_DEBUG_ANY, 
			  "log__enough_freespace: Unable to get the free space\n",0,0,0);
		return 1;
	} else {
	    LL_UI2L(freeBytes, freeClusters);
	    LL_UI2L(tmpval, sectorsPerCluster);
        LL_MUL(freeBytes, freeBytes, tmpval);
	    LL_UI2L(tmpval, bytesPerSector);
        LL_MUL(freeBytes, freeBytes, tmpval);
/*		freeBytes = freeClusters * sectorsPerCluster * bytesPerSector; */

	}
		
#else
#ifdef LINUX
	if (statfs(path, &buf) == -1)
#else
	if (statvfs(path, &buf) == -1)
#endif
	{
		char buffer[BUFSIZ];
		PR_snprintf(buffer, sizeof(buffer),
			  		"log__enough_freespace: Unable to get the free space (errno:%d)\n",
					errno);
		log__error_emergency(buffer, 0, 1);
		return 1;
	} else {
	    LL_UI2L(freeBytes, buf.f_bavail);
	    LL_UI2L(tmpval, buf.f_bsize);
	    LL_MUL(freeBytes, freeBytes, tmpval);
	  /*		freeBytes = buf.f_bavail * buf.f_bsize; */
	}
#endif
	LL_UI2L(tmpval, loginfo.log_access_minfreespace);
	if (LL_UCMP(freeBytes, <, tmpval)) {
	/*	if (freeBytes < loginfo.log_access_minfreespace) { */
	  return 0;
	}
	return 1;
}
/******************************************************************************
* log__getaccesslist
*  Update the previous access files in the slapdFrontendConfig_t.
* Returns:
*	num > 1  -- how many are there
*	0        -- otherwise
******************************************************************************/ 
char **
log_get_loglist(int logtype)
{
	char		**list=NULL;
	int		num, i;
	LogFileInfo	*logp = NULL;
	char		buf[BUFSIZ];
	char		tbuf[TBUFSIZE];
	char		*file;

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_LOCK_READ( );
		num = loginfo.log_numof_access_logs;
		logp = loginfo.log_access_logchain;
		file = loginfo.log_access_file; 
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_READ( );
		num = loginfo.log_numof_error_logs;
		logp = loginfo.log_error_logchain;
		file = loginfo.log_error_file; 
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_READ( );
		num = loginfo.log_numof_audit_logs;
		logp = loginfo.log_audit_logchain;
		file = loginfo.log_audit_file; 
		break;
	   default:
		return NULL;
	}
	list = (char **) slapi_ch_calloc(1, (num + 1) * sizeof(char *));
	i = 0;
	while (logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short */);
		PR_snprintf(buf, sizeof(buf), "%s.%s", file, tbuf);
		list[i] = slapi_ch_strdup(buf);
		i++;
		if (i == num) { /* mismatch b/w num and logchain;
						   cut the chain and save the process */
			break;
		}
		logp = logp->l_next;
	}
	list[i] = NULL;

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		LOG_ACCESS_UNLOCK_READ();
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_UNLOCK_READ();
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_UNLOCK_READ();
		break;
	}
	return list;
}

/******************************************************************************
* log__delete_error_logfile
*
*	Do we need to delete  a logfile. Find out if we need to delete the log
*	file based on expiration time, max diskspace, and minfreespace. 
*	Delete the file if we need to.
*
*	Assumption: A WRITE lock has been acquired for the error log.
******************************************************************************/ 

static int
log__delete_error_logfile(int locked)
{

	struct logfileinfo *logp = NULL;
	struct logfileinfo *delete_logp = NULL;
	struct logfileinfo *p_delete_logp = NULL;
	struct logfileinfo *prev_logp = NULL;
	PRInt64            total_size=0;
	time_t             cur_time;
	PRInt64            f_size;
	int                numoflogs=loginfo.log_numof_error_logs;
	int                rv = 0;
	char               *logstr;
	char               buffer[BUFSIZ];
	char               tbuf[TBUFSIZE];


	/* If we have only one log, then  will delete this one */
	if (loginfo.log_error_maxnumlogs == 1) {
		LOG_CLOSE(loginfo.log_error_fdes);
		loginfo.log_error_fdes = NULL;
		PR_snprintf (buffer, sizeof(buffer), "%s", loginfo.log_error_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			if (!locked) {
				/* if locked, we should not call LDAPDebug, 
				   which tries to get a lock internally. */
				LDAPDebug(LDAP_DEBUG_TRACE, 
					"LOGINFO:Unable to remove file:%s\n", loginfo.log_error_file,0,0);
			}
		}

		/* Delete the rotation file also. */
		PR_snprintf (buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_error_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			if (!locked) {
				/* if locked, we should not call LDAPDebug, 
				   which tries to get a lock internally. */
				LDAPDebug(LDAP_DEBUG_TRACE, 
					"LOGINFO:Unable to remove file:%s.rotationinfo\n", 
					loginfo.log_error_file,0,0);
			}
		}
		return 0;
	}

	/* If we have already the maximum number of log files, we
	** have to delete one any how.
	*/
	if (++numoflogs > loginfo.log_error_maxnumlogs) {
		logstr = "Exceeded max number of logs allowed";
		goto delete_logfile;
	}

	/* Now check based on the maxdiskspace */
	if (loginfo.log_error_maxdiskspace > 0) {
		logp = loginfo.log_error_logchain;
		while (logp) {
			total_size += logp->l_size;
			logp = logp->l_next;
		}
		if ((f_size = log__getfilesize(loginfo.log_error_fdes)) == -1) {
			/* then just assume the max size */
			total_size += loginfo.log_error_maxlogsize;
		} else {
			total_size += f_size;
		}

		/* If we have exceeded the max disk space or we have less than the
		** minimum, then we have to delete a file.
		*/
		if (total_size >= loginfo.log_error_maxdiskspace)  {
			logstr = "exceeded maximum log disk space";
			goto delete_logfile;
		}
	}
	
	/* Now check based on the free space */
	if ( loginfo.log_error_minfreespace > 0) {
		rv = log__enough_freespace(loginfo.log_error_file);
		if ( rv == 0) {
			/* Not enough free space */
			logstr = "Not enough free disk space";
			goto delete_logfile;
		}
	}

	/* Now check based on the expiration time */
	if ( loginfo.log_error_exptime_secs > 0 ) {
		/* is the file old enough */
		time (&cur_time);
		prev_logp = logp = loginfo.log_error_logchain;
		while (logp) {
			if ((cur_time - logp->l_ctime) > loginfo.log_error_exptime_secs) {
				delete_logp = logp;
				p_delete_logp = prev_logp;
				logstr = "The file is older than the log expiration time";
				goto delete_logfile;
			}
			prev_logp = logp;
			logp = logp->l_next;
		}
	}

	/* No log files to delete */	
	return 0;

delete_logfile:
	if (delete_logp == NULL) {
		time_t	oldest;

		time(&oldest);
		
		prev_logp = logp = loginfo.log_error_logchain;
		while (logp) {
			if (logp->l_ctime <= oldest) {
				oldest = logp->l_ctime;
				delete_logp = logp;
				p_delete_logp = prev_logp;
			}
			prev_logp = logp;
			logp = logp->l_next;
		}
		/* We might face this case if we have only one log file and
		** trying to delete it because of deletion requirement.
		*/
		if (!delete_logp) {
			return 0;
		}
	} 
	memset(tbuf, 0, sizeof(tbuf));
	log_convert_time (delete_logp->l_ctime, tbuf, 1 /*short */);
	if (!locked) {
		/* if locked, we should not call LDAPDebug, 
		   which tries to get a lock internally. */
		LDAPDebug(LDAP_DEBUG_TRACE, 
			   "LOGINFO:Removing file:%s.%s because of (%s)\n",
					loginfo.log_error_file, tbuf,
					logstr);
	}

	if (p_delete_logp == delete_logp) {
		/* then we are deleteing the first one */
		loginfo.log_error_logchain = delete_logp->l_next;
	} else {
		p_delete_logp->l_next = delete_logp->l_next;
	}

	/* Delete the error file */
	PR_snprintf (buffer, sizeof(buffer), "%s.%s", loginfo.log_error_file, tbuf);
	if (PR_Delete(buffer) != PR_SUCCESS) {
		PRErrorCode prerr = PR_GetError();
		PR_snprintf(buffer, sizeof(buffer),
				"LOGINFO:Unable to remove file:%s.%s error %d (%s)\n",
				loginfo.log_error_file, tbuf, prerr, slapd_pr_strerror(prerr));
		log__error_emergency(buffer, 0, locked);
	}
	slapi_ch_free((void**)&delete_logp);
	loginfo.log_numof_error_logs--;
	
	return 1;
}

/******************************************************************************
* log__delete_audit_logfile
*
*	Do we need to delete  a logfile. Find out if we need to delete the log
*	file based on expiration time, max diskspace, and minfreespace. 
*	Delete the file if we need to.
*
*	Assumption: A WRITE lock has been acquired for the audit
******************************************************************************/ 

static int
log__delete_audit_logfile()
{
	struct logfileinfo *logp = NULL;
	struct logfileinfo *delete_logp = NULL;
	struct logfileinfo *p_delete_logp = NULL;
	struct logfileinfo *prev_logp = NULL;
	PRInt64     total_size=0;
	time_t      cur_time;
	PRInt64     f_size;
	int         numoflogs=loginfo.log_numof_audit_logs;
	int         rv = 0;
	char        *logstr;
	char        buffer[BUFSIZ];
	char        tbuf[TBUFSIZE];

	/* If we have only one log, then  will delete this one */
	if (loginfo.log_audit_maxnumlogs == 1) {
		LOG_CLOSE(loginfo.log_audit_fdes);
                loginfo.log_audit_fdes = NULL;
		PR_snprintf(buffer, sizeof(buffer), "%s", loginfo.log_audit_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s\n", loginfo.log_audit_file,0,0);
		}

		/* Delete the rotation file also. */
		PR_snprintf(buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_audit_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s.rotationinfo\n", loginfo.log_audit_file,0,0);
		}
		return 0;
	}

	/* If we have already the maximum number of log files, we
	** have to delete one any how.
	*/
	if (++numoflogs > loginfo.log_audit_maxnumlogs) {
		logstr = "Delete Error Log File: Exceeded max number of logs allowed";
		goto delete_logfile;
	}

	/* Now check based on the maxdiskspace */
	if (loginfo.log_audit_maxdiskspace > 0) {
		logp = loginfo.log_audit_logchain;
		while (logp) {
			total_size += logp->l_size;
			logp = logp->l_next;
		}
		if ((f_size = log__getfilesize(loginfo.log_audit_fdes)) == -1) {
			/* then just assume the max size */
			total_size += loginfo.log_audit_maxlogsize;
		} else {
			total_size += f_size;
		}

		/* If we have exceeded the max disk space or we have less than the
  		** minimum, then we have to delete a file.
		*/
		if (total_size >= loginfo.log_audit_maxdiskspace)  {
			logstr = "exceeded maximum log disk space";
			goto delete_logfile;
		}
	}
	
	/* Now check based on the free space */
	if ( loginfo.log_audit_minfreespace > 0) {
		rv = log__enough_freespace(loginfo.log_audit_file);
		if ( rv == 0) {
			/* Not enough free space */
			logstr = "Not enough free disk space";
			goto delete_logfile;
		}
	}

	/* Now check based on the expiration time */
	if ( loginfo.log_audit_exptime_secs > 0 ) {
		/* is the file old enough */
		time (&cur_time);
		prev_logp = logp = loginfo.log_audit_logchain;
		while (logp) {
			if ((cur_time - logp->l_ctime) > loginfo.log_audit_exptime_secs) {
				delete_logp = logp;
				p_delete_logp = prev_logp;
				logstr = "The file is older than the log expiration time";
				goto delete_logfile;
			}
			prev_logp = logp;
			logp = logp->l_next;
		}
	}

	/* No log files to delete */	
	return 0;

delete_logfile:
	if (delete_logp == NULL) {
		time_t	oldest;

		time(&oldest);
		
		prev_logp = logp = loginfo.log_audit_logchain;
		while (logp) {
			if (logp->l_ctime <= oldest) {
				oldest = logp->l_ctime;
				delete_logp = logp;
				p_delete_logp = prev_logp;
			}
			prev_logp = logp;
			logp = logp->l_next;
		}
		/* We might face this case if we have only one log file and
		** trying to delete it because of deletion requirement.
		*/
		if (!delete_logp) {
			return 0;
		}
	} 

	if (p_delete_logp == delete_logp) {
		/* then we are deleteing the first one */
		loginfo.log_audit_logchain = delete_logp->l_next;
	} else {
		p_delete_logp->l_next = delete_logp->l_next;
	}

	/* Delete the audit file */
	log_convert_time (delete_logp->l_ctime, tbuf, 1 /*short */);
	PR_snprintf(buffer, sizeof(buffer), "%s.%s", loginfo.log_audit_file, tbuf );
	if (PR_Delete(buffer) != PR_SUCCESS) {
		LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s.%s\n",
				   loginfo.log_audit_file, tbuf,0);

	} else {
		LDAPDebug(LDAP_DEBUG_TRACE, 
			   "LOGINFO:Removed file:%s.%s because of (%s)\n",
					loginfo.log_audit_file, tbuf,
					logstr);
	}
	slapi_ch_free((void**)&delete_logp);
	loginfo.log_numof_audit_logs--;
	
	return 1;
}

/******************************************************************************
* log__error_rotationinfof
*
*	Try to open the log file. If we have one already, then try to read the
*	header and update the information.
*
*	Assumption: Lock has been acquired already
******************************************************************************/ 
static int
log__error_rotationinfof( char *pathname)
{
	long	f_ctime;
	PRInt64	f_size;
	int		main_log = 1;
	time_t	now;
	FILE	*fp;
	int		rval, logfile_type = LOGFILE_REOPENED;
	
	/*
	** Okay -- I confess, we want to use NSPR calls but I want to
	** use fgets and not use PR_Read() and implement a complicated
	** parsing module. Since this will be called only during the startup
	** and never aftre that, we can live by it.
	*/
	
	if ((fp = fopen (pathname, "r")) == NULL) {
		return LOGFILE_NEW;
	}

	loginfo.log_numof_error_logs = 0;

	/* 
	** We have reopened the log error file. Now we need to read the
	** log file info and update the values.
	*/
	while ((rval = log__extract_logheader(fp, &f_ctime, &f_size)) == LOG_CONTINUE) {
		/* first we would get the main log info */
		if (f_ctime == 0 && f_size == 0)
			continue;

		time (&now);
		if (main_log) {
			if (f_ctime > 0L)
				loginfo.log_error_ctime = f_ctime;
			else {
				loginfo.log_error_ctime = now;
			}
			main_log = 0;
		} else {
			struct	logfileinfo	*logp;

			logp = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			if (f_ctime > 0L)
				logp->l_ctime = f_ctime;
			else
				logp->l_ctime = now;
			if (f_size > 0)
				logp->l_size = f_size;
			else  {
				/* make it the max log size */
				logp->l_size = loginfo.log_error_maxlogsize;
			}

			logp->l_next = loginfo.log_error_logchain;
			loginfo.log_error_logchain = logp;
		}
		loginfo.log_numof_error_logs++;
	}
	if (LOG_DONE == rval)
		rval = log__check_prevlogs(fp, pathname);
	fclose (fp);

	if (LOG_ERROR == rval)
		if (LOG_SUCCESS == log__fix_rotationinfof(pathname))
			logfile_type = LOGFILE_NEW;

	/* Check if there is a rotation overdue */
	if (loginfo.log_error_rotationsync_enabled &&
		loginfo.log_error_rotationunit != LOG_UNIT_HOURS &&
		loginfo.log_error_rotationunit != LOG_UNIT_MINS &&
		loginfo.log_error_ctime < loginfo.log_error_rotationsyncclock - PR_ABS(loginfo.log_error_rotationtime_secs)) {
		loginfo.log_error_rotationsyncclock -= PR_ABS(loginfo.log_error_rotationtime_secs);
	}

	return logfile_type;
}

/******************************************************************************
* log__audit_rotationinfof
*
*	Try to open the log file. If we have one already, then try to read the
*	header and update the information.
*
*	Assumption: Lock has been acquired already
******************************************************************************/ 
static int
log__audit_rotationinfof( char *pathname)
{
	long	f_ctime;
	PRInt64	f_size;
	int		main_log = 1;
	time_t	now;
	FILE	*fp;
	int		rval, logfile_type = LOGFILE_REOPENED;
	
	/*
	** Okay -- I confess, we want to use NSPR calls but I want to
	** use fgets and not use PR_Read() and implement a complicated
	** parsing module. Since this will be called only during the startup
	** and never aftre that, we can live by it.
	*/
	
	if ((fp = fopen (pathname, "r")) == NULL) {
		return LOGFILE_NEW;
	}

	loginfo.log_numof_audit_logs = 0;

	/* 
	** We have reopened the log audit file. Now we need to read the
	** log file info and update the values.
	*/
	while ((rval = log__extract_logheader(fp, &f_ctime, &f_size)) == LOG_CONTINUE) {
		/* first we would get the main log info */
		if (f_ctime == 0 && f_size == 0)
			continue;

		time (&now);
		if (main_log) {
			if (f_ctime > 0L)
				loginfo.log_audit_ctime = f_ctime;
			else {
				loginfo.log_audit_ctime = now;
			}
			main_log = 0;
		} else {
			struct	logfileinfo	*logp;

			logp = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			if (f_ctime > 0L)
				logp->l_ctime = f_ctime;
			else
				logp->l_ctime = now;
			if (f_size > 0)
				logp->l_size = f_size;
			else  {
				/* make it the max log size */
				logp->l_size = loginfo.log_audit_maxlogsize;
			}

			logp->l_next = loginfo.log_audit_logchain;
			loginfo.log_audit_logchain = logp;
		}
		loginfo.log_numof_audit_logs++;
	}
	if (LOG_DONE == rval)
		rval = log__check_prevlogs(fp, pathname);
	fclose (fp);

	if (LOG_ERROR == rval)
		if (LOG_SUCCESS == log__fix_rotationinfof(pathname))
			logfile_type = LOGFILE_NEW;

	/* Check if there is a rotation overdue */
	if (loginfo.log_audit_rotationsync_enabled &&
		loginfo.log_audit_rotationunit != LOG_UNIT_HOURS &&
		loginfo.log_audit_rotationunit != LOG_UNIT_MINS &&
		loginfo.log_audit_ctime < loginfo.log_audit_rotationsyncclock - PR_ABS(loginfo.log_audit_rotationtime_secs)) {
		loginfo.log_audit_rotationsyncclock -= PR_ABS(loginfo.log_audit_rotationtime_secs);
	}

	return logfile_type;
}

static void
log__error_emergency(const char *errstr, int reopen, int locked)
{
	syslog(LOG_ERR, "%s\n", errstr);

	/* emergency open */
	if (!reopen) {
		return;
	}
	if (NULL != loginfo.log_error_fdes) {
		LOG_CLOSE(loginfo.log_error_fdes);
	}
	if (! LOG_OPEN_APPEND(loginfo.log_error_fdes, 
						  loginfo.log_error_file, loginfo.log_error_mode)) {
		PRErrorCode prerr = PR_GetError();
		syslog(LOG_ERR, "Failed to reopen errors log file, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n", prerr, slapd_pr_strerror(prerr));
	} else {
		vslapd_log_emergency_error(loginfo.log_error_fdes, errstr, locked);
	}
	return;
}

/******************************************************************************
* log__open_errorlogfile
*
*	Open a new log file. If we have run out of the max logs we can have
*	then delete the oldest file.
******************************************************************************/ 
static int
log__open_errorlogfile(int logfile_state, int locked)
{

	time_t			now;
	LOGFD			fp = NULL;
	LOGFD			fpinfo = NULL;
	char			tbuf[TBUFSIZE];
	struct logfileinfo	*logp;
	char			buffer[BUFSIZ];
	struct passwd	*pw = NULL;

	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

#ifndef _WIN32
	if ( slapdFrontendConfig->localuser != NULL &&
	     slapdFrontendConfig->localuserinfo != NULL ) {
		pw = slapdFrontendConfig->localuserinfo;
	}
	else {
		PR_snprintf(buffer, sizeof(buffer),
					"Invalid nsslapd-localuser. Cannot open the errors log. Exiting...");
		log__error_emergency(buffer, 0, locked);
		return LOG_UNABLE_TO_OPENFILE;
	}
#endif

	if (!locked) LOG_ERROR_LOCK_WRITE( );

	/* 
	** Here we are trying to create a new log file.
	** If we alredy have one, then we need to rename it as
	** "filename.time",  close it and update it's information
	** in the array stack.
	*/
	if (loginfo.log_error_fdes != NULL) {
		struct logfileinfo *log;
		char               newfile[BUFSIZ];
		PRInt64            f_size;

		/* get rid of the old one */
		if ((f_size = log__getfilesize(loginfo.log_error_fdes)) == -1) {
			/* Then assume that we have the max size */
			f_size = loginfo.log_error_maxlogsize;
		}


		/*  Check if I have to delete any old file, delete it if it is required.*/
		while (log__delete_error_logfile(1));

		/* close the file */
		if ( loginfo.log_error_fdes != NULL ) {
			LOG_CLOSE(loginfo.log_error_fdes);
		}
		loginfo.log_error_fdes = NULL;

		if ( loginfo.log_error_maxnumlogs > 1 ) {
			log = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			log->l_ctime = loginfo.log_error_ctime;
			log->l_size = f_size;

			log_convert_time (log->l_ctime, tbuf, 1/*short */);
			PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_error_file, tbuf);
			if (PR_Rename (loginfo.log_error_file, newfile) != PR_SUCCESS) {
				PRErrorCode prerr = PR_GetError();
				/* Make "FILE EXISTS" error an exception.
				   Even if PR_Rename fails with the error, we continue logging.
				 */
				if (PR_FILE_EXISTS_ERROR != prerr) {
					PR_snprintf(buffer, sizeof(buffer),
						"Failed to rename errors log file, " 
						SLAPI_COMPONENT_NAME_NSPR " error %d (%s). Exiting...", 
						prerr, slapd_pr_strerror(prerr));
					log__error_emergency(buffer, 1, 1);
					slapi_ch_free((void **)&log);
					if (!locked) LOG_ERROR_UNLOCK_WRITE();
					return LOG_UNABLE_TO_OPENFILE;
				}
			}

			/* add the log to the chain */
			log->l_next = loginfo.log_error_logchain;
			loginfo.log_error_logchain = log;
			loginfo.log_numof_error_logs++;
		}
	} 

	/* open a new log file */
	if (! LOG_OPEN_APPEND(fp, loginfo.log_error_file, loginfo.log_error_mode)) {
		PR_snprintf(buffer, sizeof(buffer),
				"Failed to open errors log file %s: error %d (%s); Exiting...", 
				loginfo.log_error_file, errno, slapd_system_strerror(errno));
		log__error_emergency(buffer, 1, locked);
		if (!locked) LOG_ERROR_UNLOCK_WRITE();
		/* failed to write to the errors log.  should not continue. */
		g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
		/*if I have an old log file -- I should log a message
		** that I can't open the new file. Let the caller worry
		** about logging message. 
		*/
		return LOG_UNABLE_TO_OPENFILE;
	}

#ifndef _WIN32
	/* make sure the logfile is owned by the localuser.  If one of the
	 * alternate ns-slapd modes, such as db2bak, tries to log an error
	 * at startup, it will create the logfile as root! 
	 */
	slapd_chown_if_not_owner(loginfo.log_error_file, pw->pw_uid, -1);
#endif

	loginfo.log_error_fdes = fp;
	if (logfile_state == LOGFILE_REOPENED) {
		/* we have all the information */
		if (!locked) LOG_ERROR_UNLOCK_WRITE( );
		return LOG_SUCCESS;
	}

	loginfo.log_error_state |= LOGGING_NEED_TITLE;

	if (! LOG_OPEN_WRITE(fpinfo, loginfo.log_errorinfo_file, loginfo.log_error_mode)) {
		PR_snprintf(buffer, sizeof(buffer),
				"Failed to open/write to errors log file %s: error %d (%s). Exiting...", 
				loginfo.log_error_file, errno, slapd_system_strerror(errno));
		log__error_emergency(buffer, 1, locked);
		if (!locked) LOG_ERROR_UNLOCK_WRITE();
		return LOG_UNABLE_TO_OPENFILE;
	}

	/* write the header in the log */
	now = current_time();
	log_convert_time (now, tbuf, 2 /*long */);
	PR_snprintf(buffer, sizeof(buffer),"LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
	LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

	logp = loginfo.log_error_logchain;
	while (logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short */);
		PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%" 
			NSPRI64 "d)\n", PREVLOGFILE, loginfo.log_error_file, tbuf,
			logp->l_ctime, logp->l_size);
		LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
		logp = logp->l_next;
	}
	/* Close the info file. We need only when we need to rotate to the
	** next log file.
	*/
	if (fpinfo) LOG_CLOSE(fpinfo);

	/* This is now the current error log */
	loginfo.log_error_ctime = now;

	if (!locked) LOG_ERROR_UNLOCK_WRITE( );
	return LOG_SUCCESS;
}

/******************************************************************************
* log__open_auditlogfile
*
*	Open a new log file. If we have run out of the max logs we can have
*	then delete the oldest file.
******************************************************************************/ 
static int
log__open_auditlogfile(int logfile_state, int locked)
{

	time_t			now;
	LOGFD			fp;
	LOGFD			fpinfo = NULL;
	char			tbuf[TBUFSIZE];
	struct logfileinfo	*logp;
	char			buffer[BUFSIZ];

	if (!locked) LOG_AUDIT_LOCK_WRITE( );

	/* 
	** Here we are trying to create a new log file.
	** If we alredy have one, then we need to rename it as
	** "filename.time",  close it and update it's information
	** in the array stack.
	*/
	if (loginfo.log_audit_fdes != NULL) {
		struct  logfileinfo *log;
		char                newfile[BUFSIZ];
		PRInt64             f_size;


		/* get rid of the old one */
		if ((f_size = log__getfilesize(loginfo.log_audit_fdes)) == -1) {
			/* Then assume that we have the max size */
			f_size = loginfo.log_audit_maxlogsize;
		}

		/* Check if I have to delete any old file, delete it if it is required. */
		while (log__delete_audit_logfile());

		/* close the file */
		LOG_CLOSE(loginfo.log_audit_fdes);
		loginfo.log_audit_fdes = NULL;

		if ( loginfo.log_audit_maxnumlogs > 1 ) {
			log = (struct logfileinfo *) slapi_ch_malloc (sizeof (struct logfileinfo));
			log->l_ctime = loginfo.log_audit_ctime;
			log->l_size = f_size;

			log_convert_time (log->l_ctime, tbuf, 1 /*short */);
			PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_audit_file, tbuf);
			if (PR_Rename (loginfo.log_audit_file, newfile) != PR_SUCCESS) {
				PRErrorCode prerr = PR_GetError();
				/* Make "FILE EXISTS" error an exception.
				   Even if PR_Rename fails with the error, we continue logging.
				 */
				if (PR_FILE_EXISTS_ERROR != prerr) {
					if (!locked) LOG_AUDIT_UNLOCK_WRITE();
					slapi_ch_free((void**)&log);
					return LOG_UNABLE_TO_OPENFILE;
				}
			}

			/* add the log to the chain */
			log->l_next = loginfo.log_audit_logchain;
			loginfo.log_audit_logchain = log;
			loginfo.log_numof_audit_logs++;
		}
	} 

	/* open a new log file */
	if (! LOG_OPEN_APPEND(fp, loginfo.log_audit_file, loginfo.log_audit_mode)) {
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				loginfo.log_audit_file, errno, slapd_system_strerror(errno));
		if (!locked) LOG_AUDIT_UNLOCK_WRITE();
		/*if I have an old log file -- I should log a message
		** that I can't open the  new file. Let the caller worry
		** about logging message. 
		*/
		return LOG_UNABLE_TO_OPENFILE;
	}

	loginfo.log_audit_fdes = fp;
	if (logfile_state == LOGFILE_REOPENED) {
		/* we have all the information */
		if (!locked) LOG_AUDIT_UNLOCK_WRITE();
		return LOG_SUCCESS;
	}

	loginfo.log_audit_state |= LOGGING_NEED_TITLE;

	if (! LOG_OPEN_WRITE(fpinfo, loginfo.log_auditinfo_file, loginfo.log_audit_mode)) {
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				loginfo.log_auditinfo_file, errno, slapd_system_strerror(errno));
		if (!locked) LOG_AUDIT_UNLOCK_WRITE();
		return LOG_UNABLE_TO_OPENFILE;
	}

	/* write the header in the log */
	now = current_time();
	log_convert_time (now, tbuf, 2 /*long */);	
	PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
	LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

	logp = loginfo.log_audit_logchain;
	while ( logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short */);	
		PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%"
			NSPRI64 "d)\n", PREVLOGFILE, loginfo.log_audit_file, tbuf, 
			logp->l_ctime, logp->l_size);
		LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
		logp = logp->l_next;
	}
	/* Close the info file. We need only when we need to rotate to the
	** next log file.
	*/
	if (fpinfo)  LOG_CLOSE(fpinfo);

	/* This is now the current audit log */
	loginfo.log_audit_ctime = now;

	if (!locked) LOG_AUDIT_UNLOCK_WRITE( );
	return LOG_SUCCESS;
}

/* 
** Log Buffering 
** only supports access log at this time 
*/

static LogBufferInfo *log_create_buffer(size_t sz)
{
    LogBufferInfo *lbi;
    
    lbi = (LogBufferInfo *) slapi_ch_malloc(sizeof(LogBufferInfo));
    lbi->top = (char *) slapi_ch_malloc(sz);
    lbi->current = lbi->top;
    lbi->maxsize = sz;
	lbi->refcount = 0;
    return lbi;
}    

#if 0
/* for some reason, we never call this. */
static void log_destroy_buffer(LogBufferInfo *lbi)
{
    slapi_ch_free((void *)&(lbi->top));
    slapi_ch_free((void *)&lbi);
}
#endif

/*
 Some notes about this function. It is written the 
 way it is for performance reasons.
 Tests showed that on 4 processor systems, there is 
 significant contention for the
 lbi->lock. This is because the lock was held for 
 the duration of the copy of the
 log message into the buffer. Therefore the routine 
 was re-written to avoid holding
 the lock for that time. Instead we gain the lock, 
 take a copy of the buffer pointer
 where we need to copy our message, increase the 
 size, move the current pointer beyond
 our portion of the buffer, then increment a reference 
 count. 
 Then we release the lock and do the actual copy 
 in to the reserved buffer area.
 We then atomically decrement the reference count.
 The reference count is used to ensure that when 
 the buffer is flushed to the
 filesystem, there are no threads left copying 
 data into the buffer.
 The wait on zero reference count is implemented 
 in the flush routine because
 it is also called from log_access_flush().
 Tests show this speeds up searches by 10% on 4-way systems.
 */

static void log_append_buffer2(time_t tnl, LogBufferInfo *lbi, char *msg1, size_t size1, char *msg2, size_t size2)
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	size_t size = size1 + size2;
	char* insert_point = NULL;

	/* While holding the lock, we determine if there is space in the buffer for our payload, 
	   and if we need to flush.
	 */
    PR_Lock(lbi->lock);
    if ( ((lbi->current - lbi->top) + size > lbi->maxsize) ||
		(tnl >= loginfo.log_access_rotationsyncclock &&
		loginfo.log_access_rotationsync_enabled) ) {
		
        log_flush_buffer(lbi, SLAPD_ACCESS_LOG,
				0 /* do not sync to disk right now */ );
		
    }
	insert_point = lbi->current;
	lbi->current += size;
	/* Increment the copy refcount */
	PR_AtomicIncrement(&(lbi->refcount));
	PR_Unlock(lbi->lock);

	/* Now we can copy without holding the lock */
    memcpy(insert_point, msg1, size1);
    memcpy(insert_point + size1, msg2, size2);
	
	/* Decrement the copy refcount */
	PR_AtomicDecrement(&(lbi->refcount));
	
	/* If we are asked to sync to disk immediately, do so */
    if (!slapdFrontendConfig->accesslogbuffering) {
		PR_Lock(lbi->lock);
        log_flush_buffer(lbi, SLAPD_ACCESS_LOG, 1 /* sync to disk now */ );
		PR_Unlock(lbi->lock);
	}

}    

/* this function assumes the lock is already acquired */
/* if sync_now is non-zero, data is flushed to physical storage */
static void log_flush_buffer(LogBufferInfo *lbi, int type, int sync_now)
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int err = 0;
	
    if (type == SLAPD_ACCESS_LOG) {

		/* It is only safe to flush once any other threads which are copying are finished */
		while (lbi->refcount > 0) {
			/* It's ok to sleep for a while because we only flush every second or so */
			DS_Sleep (PR_MillisecondsToInterval(1));
		} 
		
        if ((lbi->current - lbi->top) == 0) return;
    
        if (log__needrotation(loginfo.log_access_fdes,
				SLAPD_ACCESS_LOG) == LOG_ROTATE) {
    		if (log__open_accesslogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
    			LDAPDebug(LDAP_DEBUG_ANY,
    				"LOGINFO: Unable to open access file:%s\n",
    				loginfo.log_access_file,0,0);
			lbi->current = lbi->top; /* reset counter to prevent overwriting rest of lbi struct */
    			return;
			}
			while (loginfo.log_access_rotationsyncclock <= loginfo.log_access_ctime) {
				loginfo.log_access_rotationsyncclock += PR_ABS(loginfo.log_access_rotationtime_secs);
			}
		}

		if (loginfo.log_access_state & LOGGING_NEED_TITLE) {
			log_write_title(loginfo.log_access_fdes);
			loginfo.log_access_state &= ~LOGGING_NEED_TITLE; 
		}
		if (!sync_now && slapdFrontendConfig->accesslogbuffering) {
			LOG_WRITE(loginfo.log_access_fdes, lbi->top, lbi->current - lbi->top, 0);
		} else {
			LOG_WRITE_NOW(loginfo.log_access_fdes, lbi->top,
								lbi->current - lbi->top, 0, err);
		}

        lbi->current = lbi->top;
    }
}    

void log_access_flush()
{
    LOG_ACCESS_LOCK_WRITE();
    log_flush_buffer(loginfo.log_access_buffer, SLAPD_ACCESS_LOG,
				1 /* sync to disk now */ );
    LOG_ACCESS_UNLOCK_WRITE();
}    

/*
 *
 * log_convert_time
 *	returns the time converted  into the string format.
 *
 */
static void
log_convert_time (time_t ctime, char *tbuf, int type)
{
	struct tm               *tmsp, tms;

#ifdef _WIN32
	{
		struct tm *pt = localtime( &ctime );
		tmsp = &tms;
		memcpy(&tms, pt, sizeof(struct tm) );
	}
#else
	(void)localtime_r( &ctime, &tms );
	tmsp = &tms;
#endif
	if (type == 1)	/* get the short form */
		(void) strftime (tbuf, (size_t) TBUFSIZE, "%Y%m%d-%H%M%S",tmsp);
	else	/* wants the long form */
		(void) strftime (tbuf, (size_t) TBUFSIZE, "%d/%b/%Y:%H:%M:%S",tmsp);
}

/*
 * log_reverse_convert_time
 *	convert the given string formatted time (output from log_convert_time)
 *  into time_t
 */
static time_t
log_reverse_convert_time(char *tbuf)
{
	struct tm tm = {0};

	if (strchr(tbuf, '-') && strlen(tbuf) >= 15) {
		/* short format: YYYYmmdd-HHMMSS
		   strptime requires whitespace or non-alpha characters between format
		   specifiers on some platforms, so convert to an ISO8601-like format
		   with separators */
		char tbuf_with_sep[] = "yyyy-mm-dd HH:MM:SS";
		if( sscanf(tbuf, "%4c%2c%2c-%2c%2c%2c", tbuf_with_sep,
			tbuf_with_sep+5, tbuf_with_sep+8, tbuf_with_sep+11,
			tbuf_with_sep+14, tbuf_with_sep+17) != 6 ) {
			return 0;
		}
		strptime(tbuf_with_sep, "%Y-%m-%d %H:%M:%S", &tm);
	} else if (strchr(tbuf, '/') && strchr(tbuf, ':')) { /* long format */
		strptime(tbuf, "%d/%b/%Y:%H:%M:%S", &tm);
	} else {
		return 0;
	}
	tm.tm_isdst = -1;
	return mktime(&tm);
}

int
check_log_max_size( char *maxdiskspace_str,
                    char *mlogsize_str,
                    int maxdiskspace, /* in megabytes */
                    int mlogsize,     /* in megabytes */
                    char * returntext,
                    int logtype)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int rc = LDAP_SUCCESS;
    int     current_mlogsize = -1;     /* in megabytes */
    int     current_maxdiskspace = -1; /* in megabytes */
    PRInt64 mlogsizeB;                 /* in bytes */
    PRInt64 maxdiskspaceB;             /* in bytes */
 
    switch (logtype)
    {
        case SLAPD_ACCESS_LOG:
            current_mlogsize = slapdFrontendConfig->accesslog_maxlogsize;
            current_maxdiskspace = slapdFrontendConfig->accesslog_maxdiskspace;
            break;
        case SLAPD_ERROR_LOG:
            current_mlogsize = slapdFrontendConfig->errorlog_maxlogsize;
            current_maxdiskspace = slapdFrontendConfig->errorlog_maxdiskspace;
            break;
        case SLAPD_AUDIT_LOG:
            current_mlogsize = slapdFrontendConfig->auditlog_maxlogsize;
            current_maxdiskspace = slapdFrontendConfig->auditlog_maxdiskspace;
            break;
        default:
            current_mlogsize = -1;
            current_maxdiskspace = -1;
    }
 
    if ( maxdiskspace == -1 ) {
        maxdiskspace = current_maxdiskspace;
    }

    if ( maxdiskspace == -1 ) {
        maxdiskspaceB = -1;
    } else {
        maxdiskspaceB = (PRInt64)maxdiskspace * LOG_MB_IN_BYTES;
    }

    if ( mlogsize == -1 ) {
        mlogsize = current_mlogsize;
    }

    if ( mlogsize == -1 ) {
        mlogsizeB = -1;
    } else {
        mlogsizeB = (PRInt64)mlogsize * LOG_MB_IN_BYTES;
    }
 
    /* If maxdiskspace is negative, it is unlimited.  There is
     * no need to compate it to the logsize in this case. */
    if (( maxdiskspace >= 0 ) && ( maxdiskspace < mlogsize ))
    {
        /* fail */
        PR_snprintf ( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
          "%s: maxdiskspace \"%d (MB)\" is less than max log size \"%d (MB)\"",
          maxdiskspace_str, maxdiskspace, mlogsize );
        rc = LDAP_OPERATIONS_ERROR;
    }
    switch (logtype)
    {
        case SLAPD_ACCESS_LOG:
            loginfo.log_access_maxlogsize = mlogsizeB;
            loginfo.log_access_maxdiskspace = maxdiskspaceB;
            break;
        case SLAPD_ERROR_LOG:
            loginfo.log_error_maxlogsize = mlogsizeB;
            loginfo.log_error_maxdiskspace = maxdiskspaceB;
            break;
        case SLAPD_AUDIT_LOG:
            loginfo.log_audit_maxlogsize = mlogsizeB;
            loginfo.log_audit_maxdiskspace = maxdiskspaceB;
            break;
        default:
            break;
    }
 
    return rc;
}

/************************************************************************************/
/*				E	N	D				    */
/************************************************************************************/

