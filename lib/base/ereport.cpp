/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ereport.c:  Records transactions, reports errors to administrators, etc.
 * 
 * Rob McCool
 */


#include "private/pprio.h" /* for nspr20 binary release */
#include "netsite.h"
#include "file.h"      /* system_fopenWA, system_write_atomic */
#include "pblock.h"
#include "session.h"
#include "util.h"      /* util_vsprintf */
#include "ereport.h"

#include "base/dbtbase.h"

#include <stdarg.h>
#include <stdio.h>      /* vsprintf */
#include <string.h>     /* strcpy */
#include <time.h>       /* localtime */

#ifdef XP_UNIX
#include <syslog.h>     /* error logging to syslog */

static SYS_FILE _error_fd;
#else /* WIN32 */
#include <nt/regparms.h>
#include <nt/messages.h>
#include "eventlog.h"

static SYS_FILE _error_fd;
#endif /* XP_UNIX */

static PRBool _ereport_initialized = PR_FALSE;


NSAPI_PUBLIC SYS_FILE ereport_getfd(void) { return _error_fd; }



/* ----------------------------- ereport_init ----------------------------- */


NSAPI_PUBLIC char *ereport_init(char *err_fn, char *email, 
                                PASSWD pwuser, char *version)
{
    char err[MAGNUS_ERROR_LEN];
    SYS_FILE new_fd;

#ifdef XP_UNIX
    if(!strcmp(err_fn, "SYSLOG")) {
#ifdef NET_SSL
        openlog("secure-httpd", LOG_PID, LOG_DAEMON);
#else
        openlog("httpd", LOG_PID, LOG_DAEMON);
#endif
	_error_fd = PR_ImportFile(ERRORS_TO_SYSLOG);

        _ereport_initialized = PR_TRUE;
        return NULL;
    }
#endif /* XP_UNIX */
    if( (new_fd = system_fopenWA(err_fn)) == SYS_ERROR_FD) {
        util_snprintf(err, MAGNUS_ERROR_LEN, "can't open error log %s (%s)", err_fn,
                     system_errmsg());
#ifdef XP_UNIX
	_error_fd = PR_ImportFile(STDERR_FILENO);
#else
	_error_fd = PR_ImportFile(NULL);
#endif
        return STRDUP(err);
    }
    _error_fd = new_fd;

#ifdef XP_UNIX
    if(pwuser)
        chown(err_fn, pwuser->pw_uid, pwuser->pw_gid);
#endif /* XP_UNIX */

    _ereport_initialized = PR_TRUE;

    ereport(LOG_INFORM, XP_GetAdminStr(DBT_successfulServerStartup_));
    ereport(LOG_INFORM, XP_GetAdminStr(DBT_SBS_), MAGNUS_VERSION_STRING, BUILD_NUM);
    if (strcasecmp(BUILD_NUM, version)) {
        ereport(LOG_WARN, XP_GetAdminStr(DBT_netscapeExecutableAndSharedLibra_));
        ereport(LOG_WARN, XP_GetAdminStr(DBT_executableVersionIsS_), version);
        ereport(LOG_WARN, XP_GetAdminStr(DBT_sharedLibraryVersionIsS_), BUILD_NUM);

    }

    /* Initialize thread-specific error handling */
    system_errmsg_init();

    return NULL;
}



/* -------------------------- ereport_terminate --------------------------- */


NSAPI_PUBLIC void ereport_terminate(void)
{
    if (!_ereport_initialized)
        return;

#ifdef XP_UNIX
    ereport(LOG_INFORM, XP_GetAdminStr(DBT_errorReportingShuttingDown_));
    if(PR_FileDesc2NativeHandle(_error_fd) != ERRORS_TO_SYSLOG)
        system_fclose(_error_fd);
    else
        closelog();
#else /* WIN32 */
    if(PR_FileDesc2NativeHandle(_error_fd) != NULL)
        system_fclose(_error_fd);
#endif /* XP_UNIX */

}


/* ------------------------------- ereport -------------------------------- */


static int degree_msg[] = {
                            DBT_warning_,
                            DBT_config_,
                            DBT_security_,
                            DBT_failure_,
                            DBT_catastrophe_,
                            DBT_info_,
                            DBT_verbose_
                          };

#ifdef XP_UNIX
static int degree_syslog[] = {
    LOG_WARNING, LOG_ERR, LOG_NOTICE, LOG_ALERT, LOG_CRIT, LOG_INFO, LOG_INFO
};
#endif/* XP_UNIX */


NSAPI_PUBLIC int ereport_v(int degree, char *fmt, va_list args)
{
    char errstr[MAX_ERROR_LEN];
    int pos = 0;
    struct tm *tms, tmss;
    time_t t;
#ifdef MCC_PROXY
    char *p;
    int i;
#endif /* MCC_PROXY */

    if (!_ereport_initialized)
        return IO_OKAY;

#ifdef XP_UNIX
    if(PR_FileDesc2NativeHandle(_error_fd) != ERRORS_TO_SYSLOG) {
#endif /* XP_UNIX */
        t = time(NULL);
        tms = system_localtime(&t, &tmss);
        util_strftime(errstr, ERR_TIMEFMT, tms);
        pos = strlen(errstr);

        pos += util_snprintf(&errstr[pos], MAX_ERROR_LEN - pos, " %s: ",
                            XP_GetAdminStr(degree_msg[degree]));
#ifdef XP_UNIX 
    }
#endif /* XP_UNIX */

    pos += util_vsnprintf(&errstr[pos], sizeof(errstr) - pos, fmt, args);

    pos += util_snprintf(&errstr[pos], sizeof(errstr) - pos, ENDLINE);

#ifdef MCC_PROXY
    /* Thanx to netlib, the proxy sometimes generates multiline err msgs */
    for(p=errstr, i=pos-1; i>0; i--, p++) {
	if (*p == '\n' || *p == '\r') *p = ' ';
    }
#endif /* MCC_PROXY */

#if defined XP_UNIX
    if(PR_FileDesc2NativeHandle(_error_fd) != ERRORS_TO_SYSLOG)
        return system_fwrite_atomic(_error_fd, errstr, pos);
    syslog(degree_syslog[degree], errstr);
    return IO_OKAY;
#elif defined XP_WIN32 /* XP_WIN32 */
	if(PR_FileDesc2NativeHandle(_error_fd) != NULL)
	{
#ifdef MCC_HTTPD
	/* also write to NT Event Log if error is serious */
		switch (degree) 
		{
         case LOG_MISCONFIG:
         case LOG_SECURITY:
         case LOG_CATASTROPHE:
           LogErrorEvent(NULL, EVENTLOG_ERROR_TYPE, 
                         0, MSG_BAD_PARAMETER, 
                         errstr, NULL);
           break;
			default:
           break;
		}
#endif
		return system_fwrite_atomic(_error_fd, errstr, pos);
	}
	else { /* log to the EventLogger */
                /* Write to the event logger... */
		switch (degree) {
                  case LOG_WARN:
                    LogErrorEvent(NULL, EVENTLOG_WARNING_TYPE, 
                                  0, MSG_BAD_PARAMETER, 
                                  errstr, NULL);
                    break;
                  case LOG_MISCONFIG:
                  case LOG_SECURITY:
                  case LOG_FAILURE:
                  case LOG_CATASTROPHE:
                    LogErrorEvent(NULL, EVENTLOG_ERROR_TYPE, 
                                  0, MSG_BAD_PARAMETER, 
                                  errstr, NULL);
                    break;

                  case LOG_INFORM:
                  default:
                    LogErrorEvent(NULL, EVENTLOG_INFORMATION_TYPE, 
                                  0, MSG_BAD_PARAMETER, 
                                  errstr, NULL);
                    break;
		}
		return (IO_OKAY);
	}
#endif /* XP_WIN32 */
	
}

NSAPI_PUBLIC int ereport(int degree, char *fmt, ...)
{
    va_list args;
    int rv;
    va_start(args, fmt);
    rv = ereport_v(degree, fmt, args);
    va_end(args);
    return rv;
}
