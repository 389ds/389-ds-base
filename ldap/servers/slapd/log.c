/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

#if defined( XP_WIN32 )
#include <fcntl.h>
#include "ntslapdmessages.h"
#include "proto-ntutil.h"
extern HANDLE hSlapdEventSource;
extern LPTSTR pszServerName;
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
    LDAP_DEBUG_ACLSUMMARY,	/* SLAPI_LOG_ACLSUMMARY */
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
static int	log__delete_error_logfile();
static int	log__delete_audit_logfile();
static int 	log__access_rotationinfof(char *pathname);
static int 	log__error_rotationinfof(char *pathname);
static int 	log__audit_rotationinfof(char *pathname);
static int 	log__extract_logheader (FILE *fp, long  *f_ctime, int *f_size);
static int 	log__getfilesize(LOGFD fp);
static int 	log__enough_freespace(char  *path);

static int 	vslapd_log_error(LOGFD fp, char *subsystem, char *fmt, va_list ap );
static int 	vslapd_log_access(char *fmt, va_list ap );
static void	log_convert_time (time_t ctime, char *tbuf, int type);
static LogBufferInfo *log_create_buffer(size_t sz);
static void	log_append_buffer2(time_t tnl, LogBufferInfo *lbi, char *msg1, size_t size1, char *msg2, size_t size2);
static void	log_flush_buffer(LogBufferInfo *lbi, int type, int sync_now);
static void	log_write_title(LOGFD fp);


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
 * LOG_WRITE_NOW(fd, buffer, size, headersize) writes into a LOGFD and flushes the
 *	buffer if necessary
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
#define LOG_WRITE_NOW(fd, buffer, size, headersize) do {\
		if ( fwrite((buffer), (size), 1, (fd)) != 1 ) \
		{ \
			ReportSlapdEvent(EVENTLOG_INFORMATION_TYPE, MSG_SERVER_FAILED_TO_WRITE_LOG, 1, (buffer)); \
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
#define LOG_WRITE_NOW(fd, buffer, size, headersize) do {\
	if ( slapi_write_buffer((fd), (buffer), (size)) != (size) ) \
	{ \
		PRErrorCode prerr = PR_GetError(); \
		syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer)+(headersize) ); \
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
	char * instancedir = NULL;

	ts_time_lock = PR_NewLock();
	if (! ts_time_lock)
	    exit(-1);

#if defined( XP_WIN32 )
	pszServerName = slapi_ch_malloc( MAX_SERVICE_NAME );
	instancedir = config_get_instancedir();
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
        sprintf( szMessage, "Directory Server %s is terminating. Failed "
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
	loginfo.log_access_rotationtime = -1;
	loginfo.log_access_rotationunit =  -1;
	loginfo.log_access_rotationtime_secs = -1;
	loginfo.log_access_maxdiskspace =  -1;
	loginfo.log_access_minfreespace =  -1;
	loginfo.log_access_exptime =  -1;
	loginfo.log_access_exptimeunit =  -1;
	loginfo.log_access_exptime_secs = -1;
	loginfo.log_access_level = LDAP_DEBUG_STATS;
	loginfo.log_access_ctime = 0L;
	loginfo.log_access_fdes = NULL;
	loginfo.log_access_file = NULL;
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
	loginfo.log_error_rotationtime = -1;
	loginfo.log_error_rotationunit =  -1;
	loginfo.log_error_rotationtime_secs = -1;
	loginfo.log_error_maxdiskspace =  -1;
	loginfo.log_error_minfreespace =  -1;
	loginfo.log_error_exptime =  -1;
	loginfo.log_error_exptimeunit =  -1;
	loginfo.log_error_exptime_secs = -1;
	loginfo.log_error_ctime = 0L;
	loginfo.log_error_file = NULL;
	loginfo.log_error_fdes = NULL;
	loginfo.log_numof_error_logs = 1;
	loginfo.log_error_logchain = NULL;
	if ((loginfo.log_error_rwlock =rwl_new())== NULL ) {
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
	loginfo.log_audit_rotationtime = -1;
	loginfo.log_audit_rotationunit =  -1;
	loginfo.log_audit_rotationtime_secs = -1;
	loginfo.log_audit_maxdiskspace =  -1;
	loginfo.log_audit_minfreespace =  -1;
	loginfo.log_audit_exptime =  -1;
	loginfo.log_audit_exptimeunit =  -1;
	loginfo.log_audit_exptime_secs = -1;
	loginfo.log_audit_ctime = 0L;
	loginfo.log_audit_file = NULL;
	loginfo.log_numof_audit_logs = 1;
	loginfo.log_audit_fdes = NULL;
	loginfo.log_audit_logchain = NULL;
	if ((loginfo.log_audit_rwlock =rwl_new())== NULL ) {
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
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				pathname, errno, slapd_system_strerror(errno));
		/* stay with the current log file */
		return LDAP_UNWILLING_TO_PERFORM;
	}
	
	/* skip the rest if we aren't doing this for real */
	if ( !apply ) {
	  return LDAP_SUCCESS;
	}
	LOG_CLOSE(fp);

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

	/* skip the rest if we aren't doing this for real */
	if ( !apply ) {
	  return LDAP_SUCCESS;
	}
	LOG_CLOSE(fp);

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

	/* Now open the new errorlog */
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
			slapi_ch_free ( (void **) &fe_cfg->accesslog_mode );
			fe_cfg->accesslog_mode = slapi_ch_strdup (value);
			loginfo.log_access_mode = v;
			LOG_ACCESS_UNLOCK_WRITE();
			break;
		case SLAPD_ERROR_LOG:
			LOG_ERROR_LOCK_WRITE( );
			slapi_ch_free ( (void **) &fe_cfg->errorlog_mode );
			fe_cfg->errorlog_mode = slapi_ch_strdup (value);
			loginfo.log_error_mode = v;
			LOG_ERROR_UNLOCK_WRITE();
			break;
		case SLAPD_AUDIT_LOG:
			LOG_AUDIT_LOCK_WRITE( );
			slapi_ch_free ( (void **) &fe_cfg->auditlog_mode );
			fe_cfg->auditlog_mode = slapi_ch_strdup (value);
			loginfo.log_audit_mode = v;
			LOG_AUDIT_UNLOCK_WRITE();
			break;
	}
	return LDAP_SUCCESS;
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
	int mdiskspace= 0;
	int	max_logsize;
	int logsize;
	slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

	if (!apply || !logsize_str || !*logsize_str)
		return rv;

	logsize = atoi(logsize_str);
	
	/* convert it to bytes */
	max_logsize = logsize * LOG_MB_IN_BYTES;

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
	/* logsize will be in n MB. Convert it to bytes  */
	if (rv == 2) {
		LDAPDebug (LDAP_DEBUG_ANY, 
			   "Invalid value for Maximum log size:"
			   "Maxlogsize:%d MB  Maxdisksize:%d MB\n", 
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
  int value= 0;
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
	value = loginfo.log_access_rotationtime;
	break;
  case SLAPD_ERROR_LOG:
	LOG_ERROR_LOCK_WRITE( );
	value = loginfo.log_error_rotationtime;
	break;
  case SLAPD_AUDIT_LOG:
	LOG_AUDIT_LOCK_WRITE( );
	value = loginfo.log_audit_rotationtime;
	break;
  default:
	rv = 1;
  }
  
  if (strcasecmp(runit, "month") == 0) {
	runitType = LOG_UNIT_MONTHS;
	value *= 31 * 24 * 60 * 60;
  } else if (strcasecmp(runit, "week") == 0) {
	runitType = LOG_UNIT_WEEKS;
	value *= 7 * 24 * 60 * 60;
  } else if (strcasecmp(runit, "day") == 0) {
	runitType = LOG_UNIT_DAYS;
	value *= 24 * 60 * 60;
  } else if (strcasecmp(runit, "hour") == 0) { 
	runitType = LOG_UNIT_HOURS;
	value *= 3600;
  } else if (strcasecmp(runit, "minute") == 0) {
	runitType = LOG_UNIT_MINS;
	value *=  60;
  } else  {
	/* In this case we don't rotate */
	runitType = LOG_UNIT_UNKNOWN;
	value =  -1;
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
  	int	mlogsize;
	int maxdiskspace;
	int	s_maxdiskspace;
  
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

	maxdiskspace = atoi(maxdiskspace_str);
	s_maxdiskspace = maxdiskspace;

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
	maxdiskspace *=  LOG_MB_IN_BYTES;
	if (maxdiskspace < 0) {
		maxdiskspace = -1;
	}
	else if (maxdiskspace  < mlogsize) {
		rv = LDAP_OPERATIONS_ERROR;
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: maxdiskspace \"%d\" is less than max log size \"%d\"",
				attrname, maxdiskspace, mlogsize );
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		if (rv== 0 && apply) {
		  loginfo.log_access_maxdiskspace = maxdiskspace;
		  fe_cfg->accesslog_maxdiskspace = s_maxdiskspace  ;
		}
		LOG_ACCESS_UNLOCK_WRITE();
		break;
	   case SLAPD_ERROR_LOG:
		if (rv== 0 && apply) {
		  loginfo.log_error_maxdiskspace = maxdiskspace;
		  fe_cfg->errorlog_maxdiskspace = s_maxdiskspace;
		}
		LOG_ERROR_UNLOCK_WRITE();
		break;
	   case SLAPD_AUDIT_LOG:
		if (rv== 0 && apply) {
		  loginfo.log_audit_maxdiskspace = maxdiskspace;
		  fe_cfg->auditlog_maxdiskspace = s_maxdiskspace;
		}
		LOG_AUDIT_UNLOCK_WRITE();
		break;
	   default:
		 PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
				 "%s: invalid maximum log disk size:"
				 "Maxdiskspace:%d MB Maxlogsize:%d MB \n",
				 attrname, maxdiskspace, mlogsize);
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
	int minfreespaceB; 
	int minfreespace;
	
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
		minfreespaceB = minfreespace * LOG_MB_IN_BYTES;
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

	exptime = atoi(exptime_str);

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
	
	if (eunit == LOG_UNIT_MONTHS) {
		value = 31 * 24 * 60 * 60 * exptime;
	} else if (eunit == LOG_UNIT_WEEKS) {
		value = 7 * 24 * 60 * 60 * exptime;
	} else if (eunit == LOG_UNIT_DAYS) {
		value = 24 * 60 * 60 * exptime;
	} else  {
		/* In this case we don't expire */
		value = -1;
	}
	
	if (value  < rsec) {
		value = rsec;
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
	int	eunit, etimeunit, rsecs;
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
		etimeunit = loginfo.log_access_exptime;
		rsecs = loginfo.log_access_rotationtime_secs;
		break;
	   case SLAPD_ERROR_LOG:
		LOG_ERROR_LOCK_WRITE( );
		etimeunit = loginfo.log_error_exptime;
		rsecs = loginfo.log_error_rotationtime_secs;
		break;
	   case SLAPD_AUDIT_LOG:
		LOG_AUDIT_LOCK_WRITE( );
		etimeunit = loginfo.log_audit_exptime;
		rsecs = loginfo.log_audit_rotationtime_secs;
		break;
	   default:
		rv = 1;
		etimeunit = -1;
		rsecs = -1;
	}

	if (strcasecmp(expunit, "month") == 0) {
		eunit = LOG_UNIT_MONTHS;
		value = 31 * 24 * 60 * 60 * etimeunit;
	} else if (strcasecmp(expunit, "week") == 0) {
	 	eunit = LOG_UNIT_WEEKS;
	 	value = 7 * 24 * 60 * 60 * etimeunit;
	} else if (strcasecmp(expunit, "day") == 0) {
	 	eunit = LOG_UNIT_DAYS;
	 	value = 24 * 60 * 60 * etimeunit;
	} else { 
	  eunit = LOG_UNIT_UNKNOWN;
	  value = -1;
	}

	if ((value> 0)  && value   < rsecs ) {
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

	PR_snprintf(buff, bufflen, "\t%s B%s\n",
				fe_cfg->versionstring ? fe_cfg->versionstring : "Netscape-Directory",
				buildnum ? buildnum : "");
	LOG_WRITE_NOW(fp, buff, strlen(buff), 0);

	if (fe_cfg->localhost) {
		PR_snprintf(buff, bufflen, "\t%s:%d (%s)\n\n",
				fe_cfg->localhost,
				fe_cfg->security ? fe_cfg->secureport : fe_cfg->port,
				fe_cfg->instancedir ? fe_cfg->instancedir : "");
	}
	else {
		/* If fe_cfg->localhost is not set, ignore fe_cfg->port since
		 * it is the default and might be misleading.
		 */
		PR_snprintf(buff, bufflen, "\t<host>:<port> (%s)\n\n",
				fe_cfg->instancedir ? fe_cfg->instancedir : "");
	}
	LOG_WRITE_NOW(fp, buff, strlen(buff), 0);
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
	char	buf[BUFSIZ];

	if (!locked) LOG_ERROR_LOCK_WRITE ();
	/* save the file name */
	slapi_ch_free ((void**)&loginfo.log_error_file);
	loginfo.log_error_file = slapi_ch_strdup(pathname); 		

	/* store the rotation info fiel path name */
	 sprintf (buf, "%s.rotationinfo",pathname);
	slapi_ch_free ((void**)&loginfo.log_errorinfo_file);
	loginfo.log_errorinfo_file = slapi_ch_strdup ( buf );

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
	char	buf[BUFSIZ];

	if (!locked) LOG_AUDIT_LOCK_WRITE( );

	/* store the path name */
	loginfo.log_audit_file = slapi_ch_strdup ( pathname );

	/* store the rotation info file path name */
	sprintf (buf, "%s.rotationinfo",pathname);
	loginfo.log_auditinfo_file = slapi_ch_strdup ( buf );

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
				loginfo.log_audit_rotationsyncclock += loginfo.log_audit_rotationtime_secs;
			}
		}
		if (loginfo.log_audit_state & LOGGING_NEED_TITLE) {
			log_write_title( loginfo.log_audit_fdes);
			loginfo.log_audit_state &= ~LOGGING_NEED_TITLE;
		}
	    LOG_WRITE_NOW(loginfo.log_audit_fdes, buffer, buf_len, 0);
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
				return 0;
			}
			while (loginfo.log_error_rotationsyncclock <= loginfo.log_error_ctime) {
				loginfo.log_error_rotationsyncclock += loginfo.log_error_rotationtime_secs;
			}
		}

		if (!(detached)) { 
			rc = vslapd_log_error( NULL, subsystem, fmt, ap_err ); 
		} 
		if ( loginfo.log_error_fdes != NULL ) { 
			if (loginfo.log_error_state & LOGGING_NEED_TITLE) {
				log_write_title(loginfo.log_error_fdes);
				loginfo.log_error_state &= ~LOGGING_NEED_TITLE;
			}
			rc = vslapd_log_error( loginfo.log_error_fdes, subsystem, fmt, ap_file ); 
		} 
		LOG_ERROR_UNLOCK_WRITE();
	} else {
		/* log the problem in the stderr */
    	rc = vslapd_log_error( NULL, subsystem, fmt, ap_err ); 
	}
	return( rc );
}

static int
vslapd_log_error(
    LOGFD	fp,
    char	*subsystem,	/* omitted if NULL */
    char	*fmt,
    va_list	ap )
{
    time_t	tnl;
    long	tz;
    struct tm	*tmsp, tms;
    char	tbuf[ TBUFSIZE ];
    char	sign;
    char	buffer[SLAPI_LOG_BUFSIZ];
    int		blen;
    char	*vbuf;
	int header_len = 0;

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
    if ((unsigned int)(SLAPI_LOG_BUFSIZ - blen ) < strlen(vbuf)) {
		free (vbuf);
		return -1;
    }
  
    sprintf (buffer+blen, "%s", vbuf);

    if (fp)
	LOG_WRITE_NOW(fp, buffer, strlen(buffer), header_len);


    else  /* stderr is always unbuffered */
	fprintf(stderr, "%s", buffer);

    free (vbuf);
    return( 0 );
}

int
slapi_log_error( int severity, char *subsystem, char *fmt, ... )
{
    va_list	ap1;
    va_list	ap2;
    int		rc;

    if ( severity < SLAPI_LOG_MIN || severity > SLAPI_LOG_MAX ) {
	(void)slapd_log_error_proc( subsystem,
		"slapi_log_error: invalid severity %d (message %s)\n",
		severity, fmt );
	return( -1 );
    }


#ifdef _WIN32
    if ( *module_ldap_debug
#else
    if ( slapd_ldap_debug
#endif
	    & slapi_log_map[ severity ] ) {
	va_start( ap1, fmt );
	va_start( ap2, fmt );
	rc = slapd_log_error_proc_internal( subsystem, fmt, ap1, ap2 );
    	va_end( ap1 );
    	va_end( ap2 );
    } else {
	rc = 0;	/* nothing to be logged --> always return success */
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
	char	buf[BUFSIZ];

	if (!locked) LOG_ACCESS_LOCK_WRITE( );

	/* store the path name */
	loginfo.log_access_file = slapi_ch_strdup ( pathname );

	/* store the rotation info fiel path name */
	sprintf (buf, "%s.rotationinfo",pathname);
	loginfo.log_accessinfo_file = slapi_ch_strdup ( buf );

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
		struct  logfileinfo 	*log;
		char			newfile[BUFSIZ];
		int			f_size;

		/* get rid of the old one */
		if ((f_size = log__getfilesize(loginfo.log_access_fdes)) == -1) {
			/* Then assume that we have the max size */
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
			sprintf(newfile, "%s.%s", loginfo.log_access_file, tbuf);
			if (PR_Rename (loginfo.log_access_file, newfile) != PR_SUCCESS) {
				loginfo.log_access_fdes = NULL;
    				if (!locked)  LOG_ACCESS_UNLOCK_WRITE();
				return LOG_UNABLE_TO_OPENFILE;
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
		LDAPDebug( LDAP_DEBUG_ANY, "access file open %s failed errno %d (%s)\n",
					    loginfo.log_access_file,
				            oserr, slapd_system_strerror(oserr));
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
	sprintf (buffer,"LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
	LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

	logp = loginfo.log_access_logchain;
	while ( logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short*/);
		sprintf(buffer, "LOGINFO:Previous Log File:%s.%s (%lu) (%u)\n",
                        loginfo.log_access_file, tbuf, logp->l_ctime, logp->l_size);
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
	time_t	syncclock;
	int	type = LOG_CONTINUE;
	int	f_size = 0;
	int	maxlogsize, nlogs;
	int	rotationtime_secs = -1;
	int	sync_enabled, synchour, syncmin, timeunit;

	if (fp == NULL) {
		return LOG_ROTATE;
	}

	switch (logtype) {
	   case SLAPD_ACCESS_LOG:
		nlogs = loginfo.log_access_maxnumlogs;
		maxlogsize = loginfo.log_access_maxlogsize;
		sync_enabled = loginfo.log_access_rotationsync_enabled;
		synchour = loginfo.log_access_rotationsynchour;
		syncmin = loginfo.log_access_rotationsyncmin;
		syncclock = loginfo.log_access_rotationsyncclock;
		timeunit = loginfo.log_access_rotationunit;
		rotationtime_secs = loginfo.log_access_rotationtime_secs;
		log_createtime = loginfo.log_access_ctime;
		break;
 	   case SLAPD_ERROR_LOG:
		nlogs = loginfo.log_error_maxnumlogs;
		maxlogsize = loginfo.log_error_maxlogsize;
		sync_enabled = loginfo.log_error_rotationsync_enabled;
		synchour = loginfo.log_error_rotationsynchour;
		syncmin = loginfo.log_error_rotationsyncmin;
		syncclock = loginfo.log_error_rotationsyncclock;
		timeunit = loginfo.log_error_rotationunit;
		rotationtime_secs = loginfo.log_error_rotationtime_secs;
		log_createtime = loginfo.log_error_ctime;
		break;
	   case SLAPD_AUDIT_LOG:
		nlogs = loginfo.log_audit_maxnumlogs;
		maxlogsize = loginfo.log_audit_maxlogsize;
		sync_enabled = loginfo.log_audit_rotationsync_enabled;
		synchour = loginfo.log_audit_rotationsynchour;
		syncmin = loginfo.log_audit_rotationsyncmin;
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
    			   "LOGINFO:End of Log because size exceeded(Max:%d bytes) (Is:%d bytes)\n", maxlogsize, f_size, 0);
    	} else  if ( type == LOG_EXPIRED) {
    		LDAPDebug(LDAP_DEBUG_TRACE,
    			   "LOGINFO:End of Log because time exceeded(Max:%d secs) (Is:%d secs)\n",
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

	struct logfileinfo	*logp = NULL;
	struct logfileinfo	*delete_logp = NULL;
	struct logfileinfo	*p_delete_logp = NULL;
	struct logfileinfo	*prev_logp = NULL;
	int			total_size=0;
	time_t			cur_time;
	int			f_size;
	int			numoflogs=loginfo.log_numof_access_logs;
	int			rv = 0;
	char			*logstr;
	char			buffer[BUFSIZ];
	char			tbuf[TBUFSIZE];
	
	/* If we have only one log, then  will delete this one */
	if (loginfo.log_access_maxnumlogs == 1) {
		LOG_CLOSE(loginfo.log_access_fdes);
                loginfo.log_access_fdes = NULL;
		sprintf (buffer, "%s", loginfo.log_access_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s\n", loginfo.log_access_file,0,0);
		}

		/* Delete the rotation file also. */
		sprintf (buffer, "%s.rotationinfo", loginfo.log_access_file);
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
	sprintf (buffer, "%s.%s", loginfo.log_access_file, tbuf);
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
/******************************************************************************
* log__access_rotationinfof
*
*	Try to open the log file. If we have one already, then try to read the
*	header and update the information.
*
*	Assumption: Lock has been acquired already
******************************************************************************/ 
static int
log__access_rotationinfof( char *pathname)
{
	long		f_ctime;
	int		f_size;
	int		main_log = 1;
	time_t		now;
	FILE		*fp;

	
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
	while (log__extract_logheader(fp, &f_ctime, &f_size) == LOG_CONTINUE) {
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

	/* Check if there is a rotation overdue */
	if (loginfo.log_access_rotationsync_enabled &&
		loginfo.log_access_rotationunit != LOG_UNIT_HOURS &&
		loginfo.log_access_rotationunit != LOG_UNIT_MINS &&
		loginfo.log_access_ctime < loginfo.log_access_rotationsyncclock - loginfo.log_access_rotationtime_secs) {
		loginfo.log_access_rotationsyncclock -= loginfo.log_access_rotationtime_secs;
	}
	fclose (fp);
	return LOGFILE_REOPENED;
}

/******************************************************************************
* log__extract_logheader
*
*	Extract each LOGINFO heder line. From there extract the time and
*	size info of all the old log files.
******************************************************************************/ 
static int
log__extract_logheader (FILE *fp, long  *f_ctime, int *f_size)
{

	char		buf[BUFSIZ];
	char		*p, *s, *next;

	*f_ctime = 0L;
	*f_size = 0;

	if ( fp == NULL)
		return LOG_ERROR;

	if (fgets(buf, BUFSIZ, fp) == NULL) {
		return LOG_ERROR;
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
	*f_ctime = atoi(p);

	if ((p = strchr(next, '(')) == NULL) {
		/* that's fine -- it means we have no size info */
		*f_size = 0;
		return LOG_CONTINUE;
	}

	if ((next= strchr(p, ')')) == NULL) {
		return LOG_CONTINUE;
	}

	p++;
	*next = '\0';
	
	/* Now p must hold the size value */
	*f_size = atoi(p);

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
static int 
log__getfilesize(LOGFD fp)
{
	struct stat	info;
	int		rv;

	if ((rv = fstat(fileno(fp), &info)) != 0) {
		return -1;
	}
	return info.st_size;
} 
#else
static int
log__getfilesize(LOGFD fp)
{
	PRFileInfo      info;
        int             rv;
 
	if ((rv = PR_GetOpenFileInfo (fp, &info)) == PR_FAILURE) {
                return -1;
        }
	return info.size;
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
		int	oserr = errno;
		LDAPDebug(LDAP_DEBUG_ANY, 
			  "log__enough_freespace: Unable to get the free space (errno:%d)\n",
				oserr,0,0);
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
	list = (char **) slapi_ch_calloc(1, num * sizeof(char *));
	i = 0;
	while (logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short */);
		sprintf(buf, "%s.%s", file, tbuf);
		list[i] = slapi_ch_strdup(buf);
		i++;
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
log__delete_error_logfile()
{

	struct logfileinfo	*logp = NULL;
	struct logfileinfo	*delete_logp = NULL;
	struct logfileinfo	*p_delete_logp = NULL;
	struct logfileinfo	*prev_logp = NULL;
	int			total_size=0;
	time_t			cur_time;
	int			f_size;
	int			numoflogs=loginfo.log_numof_error_logs;
	int			rv = 0;
	char			*logstr;
	char			buffer[BUFSIZ];
	char			tbuf[TBUFSIZE];


	/* If we have only one log, then  will delete this one */
	if (loginfo.log_error_maxnumlogs == 1) {
		LOG_CLOSE(loginfo.log_error_fdes);
                loginfo.log_error_fdes = NULL;
		sprintf (buffer, "%s", loginfo.log_error_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s\n", loginfo.log_error_file,0,0);
		}

		/* Delete the rotation file also. */
		sprintf (buffer, "%s.rotationinfo", loginfo.log_error_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s.rotationinfo\n", loginfo.log_error_file,0,0);
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

	if (p_delete_logp == delete_logp) {
		/* then we are deleteing the first one */
		loginfo.log_error_logchain = delete_logp->l_next;
	} else {
		p_delete_logp->l_next = delete_logp->l_next;
	}

	/* Delete the error file */
	log_convert_time (delete_logp->l_ctime, tbuf, 1 /*short */);
	sprintf (buffer, "%s.%s", loginfo.log_error_file, tbuf);
	PR_Delete(buffer);
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
	struct logfileinfo	*logp = NULL;
	struct logfileinfo	*delete_logp = NULL;
	struct logfileinfo	*p_delete_logp = NULL;
	struct logfileinfo	*prev_logp = NULL;
	int			total_size=0;
	time_t			cur_time;
	int			f_size;
	int			numoflogs=loginfo.log_numof_audit_logs;
	int			rv = 0;
	char			*logstr;
	char			buffer[BUFSIZ];
	char			tbuf[TBUFSIZE];

	/* If we have only one log, then  will delete this one */
	if (loginfo.log_audit_maxnumlogs == 1) {
		LOG_CLOSE(loginfo.log_audit_fdes);
                loginfo.log_audit_fdes = NULL;
		sprintf (buffer, "%s", loginfo.log_audit_file);
		if (PR_Delete(buffer) != PR_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_TRACE, 
				"LOGINFO:Unable to remove file:%s\n", loginfo.log_audit_file,0,0);
		}

		/* Delete the rotation file also. */
		sprintf (buffer, "%s.rotationinfo", loginfo.log_audit_file);
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
		logstr = "Exceeded max number of logs allowed";
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
	sprintf (buffer, "%s.%s", loginfo.log_audit_file, tbuf );
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
	long		f_ctime;
	int		f_size;
	int		main_log = 1;
	time_t		now;
	FILE		*fp;

	
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
	while (log__extract_logheader(fp, &f_ctime, &f_size) == LOG_CONTINUE) {
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

	/* Check if there is a rotation overdue */
	if (loginfo.log_error_rotationsync_enabled &&
		loginfo.log_error_rotationunit != LOG_UNIT_HOURS &&
		loginfo.log_error_rotationunit != LOG_UNIT_MINS &&
		loginfo.log_error_ctime < loginfo.log_error_rotationsyncclock - loginfo.log_error_rotationtime_secs) {
		loginfo.log_error_rotationsyncclock -= loginfo.log_error_rotationtime_secs;
	}

	fclose (fp);
	return LOGFILE_REOPENED;
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
	long		f_ctime;
	int		f_size;
	int		main_log = 1;
	time_t		now;
	FILE		*fp;

	
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
	while (log__extract_logheader(fp, &f_ctime, &f_size) == LOG_CONTINUE) {
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

	/* Check if there is a rotation overdue */
	if (loginfo.log_audit_rotationsync_enabled &&
		loginfo.log_audit_rotationunit != LOG_UNIT_HOURS &&
		loginfo.log_audit_rotationunit != LOG_UNIT_MINS &&
		loginfo.log_audit_ctime < loginfo.log_audit_rotationsyncclock - loginfo.log_audit_rotationtime_secs) {
		loginfo.log_audit_rotationsyncclock -= loginfo.log_audit_rotationtime_secs;
	}

	fclose (fp);
	return LOGFILE_REOPENED;
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
	LOGFD			fp;
	LOGFD			fpinfo = NULL;
	char			tbuf[TBUFSIZE];
	struct logfileinfo	*logp;
	char			buffer[BUFSIZ];

	if (!locked) LOG_ERROR_LOCK_WRITE( );

	/* 
	** Here we are trying to create a new log file.
	** If we alredy have one, then we need to rename it as
	** "filename.time",  close it and update it's information
	** in the array stack.
	*/
	if (loginfo.log_error_fdes != NULL) {
		struct  logfileinfo 	*log;
		char			newfile[BUFSIZ];
		int			f_size;

		/* get rid of the old one */
		if ((f_size = log__getfilesize(loginfo.log_error_fdes)) == -1) {
			/* Then assume that we have the max size */
			f_size = loginfo.log_error_maxlogsize;
		}


		/*  Check if I have to delete any old file, delete it if it is required.*/
		while (log__delete_error_logfile());

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
			sprintf(newfile, "%s.%s", loginfo.log_error_file, tbuf);
			if (PR_Rename (loginfo.log_error_file, newfile) != PR_SUCCESS) {
				return LOG_UNABLE_TO_OPENFILE;
			}

			/* add the log to the chain */
			log->l_next = loginfo.log_error_logchain;
			loginfo.log_error_logchain = log;
			loginfo.log_numof_error_logs++;
		}
	} 


	/* open a new log file */
	if (! LOG_OPEN_APPEND(fp, loginfo.log_error_file, loginfo.log_error_mode)) {
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				loginfo.log_error_file, errno, slapd_system_strerror(errno));
		if (!locked) LOG_ERROR_UNLOCK_WRITE();
		/*if I have an old log file -- I should log a message
		** that I can't open the  new file. Let the caller worry
		** about logging message. 
		*/
		return LOG_UNABLE_TO_OPENFILE;
	}

	loginfo.log_error_fdes = fp;
	if (logfile_state == LOGFILE_REOPENED) {
		/* we have all the information */
		if (!locked) LOG_ERROR_UNLOCK_WRITE( );
		return LOG_SUCCESS;
	}

	loginfo.log_error_state |= LOGGING_NEED_TITLE;

	if (! LOG_OPEN_WRITE(fpinfo, loginfo.log_errorinfo_file, loginfo.log_error_mode)) {
		LDAPDebug(LDAP_DEBUG_ANY, "WARNING: can't open file %s. "
				"errno %d (%s)\n",
				loginfo.log_errorinfo_file, errno, slapd_system_strerror(errno));
		if (!locked) LOG_ERROR_UNLOCK_WRITE();
		return LOG_UNABLE_TO_OPENFILE;
	}

	/* write the header in the log */
	now = current_time();
	log_convert_time (now, tbuf, 2 /*long */);
	sprintf (buffer,"LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
	LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

	logp = loginfo.log_error_logchain;
	while ( logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short */);
		sprintf(buffer, "LOGINFO:Previous Log File:%s.%s (%lu) (%u)\n",
                        loginfo.log_error_file,	tbuf, logp->l_ctime, logp->l_size);
		LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
		logp = logp->l_next;
	}
	/* Close the info file. We need only when we need to rotate to the
	** next log file.
	*/
	if (fpinfo)  LOG_CLOSE(fpinfo);

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
		struct  logfileinfo 	*log;
		char			newfile[BUFSIZ];
		int			f_size;


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
			sprintf(newfile, "%s.%s", loginfo.log_audit_file, tbuf);
			if (PR_Rename (loginfo.log_audit_file, newfile) != PR_SUCCESS) {
				if (!locked) LOG_AUDIT_UNLOCK_WRITE();
				return LOG_UNABLE_TO_OPENFILE;
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
	sprintf (buffer,"LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
	LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

	logp = loginfo.log_audit_logchain;
	while ( logp) {
		log_convert_time (logp->l_ctime, tbuf, 1 /*short */);	
		sprintf(buffer, "LOGINFO:Previous Log File:%s.%s (%d) (%d)\n",
                        loginfo.log_audit_file, tbuf, (int)logp->l_ctime, logp->l_size);
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
				loginfo.log_access_rotationsyncclock += loginfo.log_access_rotationtime_secs;
			}
		}

		if (loginfo.log_access_state & LOGGING_NEED_TITLE) {
			log_write_title(loginfo.log_access_fdes);
			loginfo.log_access_state &= ~LOGGING_NEED_TITLE; 
		}
		if (!sync_now && slapdFrontendConfig->accesslogbuffering) {
			LOG_WRITE(loginfo.log_access_fdes, lbi->top, lbi->current - lbi->top, 0);
		} else {
			LOG_WRITE_NOW(loginfo.log_access_fdes, lbi->top, lbi->current - lbi->top, 0);
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

int
check_log_max_size( char *maxdiskspace_str,
                    char *mlogsize_str,
                    int maxdiskspace,
                    int mlogsize,
                    char * returntext,
                    int logtype)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int rc = LDAP_SUCCESS;
    int current_mlogsize = -1;
    int current_maxdiskspace = -1;
 
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
 
    if ( maxdiskspace == -1 )
        maxdiskspace = current_maxdiskspace;
    if ( mlogsize == -1 )
        mlogsize = current_mlogsize;
 
    if ( maxdiskspace < mlogsize )
    {
        /* fail */
        PR_snprintf ( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                "%s: maxdiskspace \"%d\" is less than max log size \"%d\"",
                maxdiskspace_str, maxdiskspace*LOG_MB_IN_BYTES, mlogsize*LOG_MB_IN_BYTES );
        rc = LDAP_OPERATIONS_ERROR;
    }
    switch (logtype)
    {
        case SLAPD_ACCESS_LOG:
			loginfo.log_access_maxlogsize = mlogsize * LOG_MB_IN_BYTES;
			loginfo.log_access_maxdiskspace = maxdiskspace * LOG_MB_IN_BYTES;
            break;
        case SLAPD_ERROR_LOG:
			loginfo.log_error_maxlogsize = mlogsize  * LOG_MB_IN_BYTES;
			loginfo.log_error_maxdiskspace = maxdiskspace  * LOG_MB_IN_BYTES;
            break;
        case SLAPD_AUDIT_LOG:
			loginfo.log_audit_maxlogsize = mlogsize * LOG_MB_IN_BYTES;
			loginfo.log_audit_maxdiskspace = maxdiskspace  * LOG_MB_IN_BYTES;
            break;
        default:
			break;
    }
 
    return rc;
}

/************************************************************************************/
/*				E	N	D				    */
/************************************************************************************/

