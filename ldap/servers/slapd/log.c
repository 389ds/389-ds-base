/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2010 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
**
** log.c
**
**    Routines for writing access and error/debug logs
**
**
**    History:
**        As of DS 4.0, we support log rotation for the ACCESS/ERROR/AUDIT log.
*/

/* Use the syslog level names (prioritynames) */
#define SYSLOG_NAMES 1

#include "log.h"
#include "fe.h"
#include <pwd.h> /* getpwnam */
#include "zlib.h"
#define _PSEP '/'
#include <json-c/json.h>
#include <assert.h>
#include <execinfo.h>

#ifdef SYSTEMTAP
#include <sys/sdt.h>
#endif

/**************************************************************************
 * GLOBALS, defines, and ...
 *************************************************************************/
/* main struct which contains all the information about logging */
PRUintn logbuf_tsdindex;
struct logbufinfo *logbuf_accum;
static struct logging_opts loginfo;
static int detached = 0;
static int logging_hr_timestamps_enabled = 1;

//extern int slapd_ldap_debug;

/*
 * Note: the order of the values in the slapi_log_map array must exactly
 * match that of the SLAPI_LOG_XXX #defines found in slapi-plugin.h (this is
 * so we can use the SLAPI_LOG_XXX values to index directly into the array).
 */
static int slapi_log_map[] = {
    LDAP_DEBUG_ANY,        /* SLAPI_LOG_FATAL */
    LDAP_DEBUG_TRACE,      /* SLAPI_LOG_TRACE */
    LDAP_DEBUG_PACKETS,    /* SLAPI_LOG_PACKETS */
    LDAP_DEBUG_ARGS,       /* SLAPI_LOG_ARGS */
    LDAP_DEBUG_CONNS,      /* SLAPI_LOG_CONNS */
    LDAP_DEBUG_BER,        /* SLAPI_LOG_BER */
    LDAP_DEBUG_FILTER,     /* SLAPI_LOG_FILTER */
    LDAP_DEBUG_CONFIG,     /* SLAPI_LOG_CONFIG */
    LDAP_DEBUG_ACL,        /* SLAPI_LOG_ACL */
    LDAP_DEBUG_SHELL,      /* SLAPI_LOG_SHELL */
    LDAP_DEBUG_PARSE,      /* SLAPI_LOG_PARSE */
    LDAP_DEBUG_HOUSE,      /* SLAPI_LOG_HOUSE */
    LDAP_DEBUG_REPL,       /* SLAPI_LOG_REPL */
    LDAP_DEBUG_CACHE,      /* SLAPI_LOG_CACHE */
    LDAP_DEBUG_PLUGIN,     /* SLAPI_LOG_PLUGIN */
    LDAP_DEBUG_TIMING,     /* SLAPI_LOG_TIMING */
    LDAP_DEBUG_BACKLDBM,   /* SLAPI_LOG_BACKLDBM */
    LDAP_DEBUG_ACLSUMMARY, /* SLAPI_LOG_ACLSUMMARY */
    LDAP_DEBUG_PWDPOLICY,  /* SLAPI_LOG_PWDPOLICY */
    LDAP_DEBUG_EMERG,      /* SLAPI_LOG_EMERG */
    LDAP_DEBUG_ALERT,      /* SLAPI_LOG_ALERT */
    LDAP_DEBUG_CRIT,       /* SLAPI_LOG_CRIT */
    LDAP_DEBUG_ERR,        /* SLAPI_LOG_ERR */
    LDAP_DEBUG_WARNING,    /* SLAPI_LOG_WARNING */
    LDAP_DEBUG_NOTICE,     /* SLAPI_LOG_NOTICE */
    LDAP_DEBUG_INFO,       /* SLAPI_LOG_INFO */
    LDAP_DEBUG_DEBUG       /* SLAPI_LOG_DEBUG */

};

#define SLAPI_LOG_MIN SLAPI_LOG_FATAL /* from slapi-plugin.h */
#define SLAPI_LOG_MAX SLAPI_LOG_DEBUG /* from slapi-plugin.h */
#define LOG_CHUNK 16384 /* zlib compression */

/**************************************************************************
 * PROTOTYPES
 *************************************************************************/
static int log__open_accesslogfile(int logfile_type, int locked);
static int log__open_securitylogfile(int logfile_type, int locked);
static int log__open_errorlogfile(int logfile_type, int locked);
static int log__open_auditlogfile(int logfile_type, int locked);
static int log__open_auditfaillogfile(int logfile_type, int locked);
static int log__needrotation(LOGFD fp, int logtype);
static int log__delete_access_logfile(void);
static int log__delete_security_logfile(void);
static int log__delete_error_logfile(int locked);
static int log__delete_audit_logfile(void);
static int log__delete_auditfail_logfile(void);
static int log__access_rotationinfof(char *pathname);
static int log__security_rotationinfof(char *pathname);
static int log__error_rotationinfof(char *pathname);
static int log__audit_rotationinfof(char *pathname);
static int log__auditfail_rotationinfof(char *pathname);
static int log__extract_logheader(FILE *fp, long *f_ctime, PRInt64 *f_size, PRBool *compressed);
static int log__check_prevlogs(FILE *fp, char *filename);
static PRInt64 log__getfilesize(LOGFD fp);
static PRInt64 log__getfilesize_with_filename(char *filename);
static int log__enough_freespace(char *path);
static int vslapd_log_error(LOGFD fp, int sev_level, const char *subsystem, const char *fmt, va_list ap, int locked);
static int vslapd_log_access(const char *fmt, va_list ap);
static int vslapd_log_security(const char *log_data);
static void log_convert_time(time_t ctime, char *tbuf, int type);
static time_t log_reverse_convert_time(char *tbuf);
static LogBufferInfo *log_create_buffer(size_t sz);
static void log_append_buffer2(time_t tnl, LogBufferInfo *lbi, char *msg1, size_t size1, char *msg2, size_t size2);
static void log_append_security_buffer(time_t tnl, LogBufferInfo *lbi, char *msg, size_t size);
static void log_flush_buffer(LogBufferInfo *lbi, int type, int sync_now);
static void log_write_title(LOGFD fp);
static void log__error_emergency(const char *errstr, int reopen, int locked);
static void vslapd_log_emergency_error(LOGFD fp, const char *msg, int locked);
static int get_syslog_loglevel(int loglevel);
static void log_external_libs_debug_openldap_print(char *buffer);
static int log__fix_rotationinfof(char *pathname);

static int
get_syslog_loglevel(int loglevel)
{
    int default_level = LOG_DEBUG;

    if (loglevel == SLAPI_LOG_EMERG || loglevel == LDAP_DEBUG_EMERG) {
        return LOG_EMERG;
    }
    if (loglevel == SLAPI_LOG_ALERT || loglevel == LDAP_DEBUG_ALERT) {
        return LOG_ALERT;
    }
    if (loglevel == SLAPI_LOG_CRIT || loglevel == LDAP_DEBUG_CRIT) {
        return LOG_CRIT;
    }
    if (loglevel == SLAPI_LOG_ERR ||
        loglevel == SLAPI_LOG_FATAL ||
        loglevel == LDAP_DEBUG_ANY) {
        return LOG_ERR;
    }
    if (loglevel == SLAPI_LOG_WARNING || loglevel == LDAP_DEBUG_WARNING) {
        return LOG_WARNING;
    }
    if (loglevel == SLAPI_LOG_NOTICE || loglevel == LDAP_DEBUG_NOTICE) {
        return LOG_NOTICE;
    }
    if (loglevel == SLAPI_LOG_INFO || loglevel == LDAP_DEBUG_INFO) {
        return LOG_INFO;
    }
    if (loglevel == SLAPI_LOG_DEBUG || loglevel == LDAP_DEBUG_DEBUG) {
        return LOG_DEBUG;
    }

    return default_level;
}

static int
compress_log_file(char *log_name)
{
    char gzip_log[BUFSIZ] = {0};
    char buf[LOG_CHUNK] = {0};
    size_t bytes_read = 0;
    gzFile outfile = NULL;
    FILE *source = NULL;

    PR_snprintf(gzip_log, sizeof(gzip_log), "%s.gz", log_name);
    if ((outfile = gzopen(gzip_log,"wb")) == NULL) {
        /* Failed to open new gzip file */
        return -1;
    }

    if ((source = fopen(log_name, "r")) == NULL) {
        /* Failed to open log file */
        gzclose(outfile);
        return -1;
    }
    bytes_read = fread(buf, 1, LOG_CHUNK, source);
    while (bytes_read > 0) {
        int bytes_written = gzwrite(outfile, buf, bytes_read);
        if (bytes_written == 0)
        {
            fclose(source);
            gzclose(outfile);
            return -1;
        }
        bytes_read = fread(buf, 1, LOG_CHUNK, source);
    }
    gzclose(outfile);
    fclose(source);
    PR_Delete(log_name); /* remove the old uncompressed log */

    return 0;
}

int
loglevel_is_set(int level)
{
    return (0 != (slapd_ldap_debug & level));
}


static int
slapd_log_error_proc_internal(
    int loglevel,
    const char *subsystem, /* omitted if NULL */
    const char *fmt,
    va_list ap_err,
    va_list ap_file);

/*
 * these macros are used for opening a log file, closing a log file, and
 * writing out to a log file.  we have to do this because currently NSPR
 * is extremely under-performant on NT, while fopen/fwrite fail on several
 * unix platforms if there are more than 128 files open.
 *
 * LOG_OPEN_APPEND(fd, filename, mode) returns true if successful.  'fd' should
 *    be of type LOGFD (check log.h).  the file is open for appending to.
 * LOG_OPEN_WRITE(fd, filename, mode) is the same but truncates the file and
 *    starts writing at the beginning of the file.
 * LOG_WRITE(fd, buffer, size, headersize) writes into a LOGFD
 * LOG_WRITE_NOW(fd, buffer, size, headersize, err) writes into a LOGFD and
 *  flushes the buffer if necessary
 * LOG_CLOSE(fd) closes the logfile
 */
#define LOG_OPEN_APPEND(fd, filename, mode)                              \
    (((fd) = PR_Open((filename), PR_WRONLY | PR_APPEND | PR_CREATE_FILE, \
                     mode)) != NULL)
#define LOG_OPEN_WRITE(fd, filename, mode)                 \
    (((fd) = PR_Open((filename), PR_WRONLY | PR_TRUNCATE | \
                                     PR_CREATE_FILE,       \
                     mode)) != NULL)
#define LOG_WRITE(fd, buffer, size, headersize)                                                                                                              \
    if (slapi_write_buffer((fd), (buffer), (PRInt32)(size)) != (PRInt32)(size)) {                                                                            \
        PRErrorCode prerr = PR_GetError();                                                                                                                   \
        syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer) + (headersize)); \
    }
#define LOG_WRITE_NOW(fd, buffer, size, headersize, err)                                                                                                         \
    do {                                                                                                                                                         \
        (err) = 0;                                                                                                                                               \
        if (slapi_write_buffer((fd), (buffer), (PRInt32)(size)) != (PRInt32)(size)) {                                                                            \
            PRErrorCode prerr = PR_GetError();                                                                                                                   \
            syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer) + (headersize)); \
            (err) = prerr;                                                                                                                                       \
        }                                                                                                                                                        \
        /* Should be a flush in here ?? Yes because PR_SYNC doesn't work ! */                                                                                    \
        PR_Sync(fd);                                                                                                                                             \
    } while (0)
#define LOG_WRITE_NOW_NO_ERR(fd, buffer, size, headersize)                                                                                                       \
    do {                                                                                                                                                         \
        if (slapi_write_buffer((fd), (buffer), (PRInt32)(size)) != (PRInt32)(size)) {                                                                            \
            PRErrorCode prerr = PR_GetError();                                                                                                                   \
            syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer) + (headersize)); \
        }                                                                                                                                                        \
        /* Should be a flush in here ?? Yes because PR_SYNC doesn't work ! */                                                                                    \
        PR_Sync(fd);                                                                                                                                             \
    } while (0)
#define LOG_CLOSE(fd) \
    PR_Close((fd))


/******************************************************************************
* Set the access level
******************************************************************************/
void
g_set_accesslog_level(int val)
{
    LOG_ACCESS_LOCK_WRITE();
    loginfo.log_access_level = val;
    LOG_ACCESS_UNLOCK_WRITE();
}

/******************************************************************************
* Set the stat level
******************************************************************************/
void
g_set_statlog_level(int val)
{
    LOG_ACCESS_LOCK_WRITE();
    loginfo.log_access_stat_level = val;
    LOG_ACCESS_UNLOCK_WRITE();
}

/******************************************************************************
* Set the security level
******************************************************************************/
void
g_set_securitylog_level(int val)
{
    LOG_SECURITY_LOCK_WRITE();
    loginfo.log_security_level = val;
    LOG_SECURITY_UNLOCK_WRITE();
}

/******************************************************************************
* Set whether the process is alive or dead
* If it is detached, then we write the error in 'stderr'
******************************************************************************/
void
g_set_detached(int val)
{
    detached = val;
}

/******************************************************************************
* Tell me whether logging begins or not
******************************************************************************/
void
g_log_init()
{
    slapdFrontendConfig_t *cfg = getFrontendConfig();
    CFG_LOCK_READ(cfg);

    /* ACCESS LOG */
    loginfo.log_access_state = cfg->accesslog_logging_enabled;
    loginfo.log_access_mode = SLAPD_DEFAULT_FILE_MODE;
    loginfo.log_access_maxnumlogs = cfg->accesslog_maxnumlogs;
    loginfo.log_access_maxlogsize = cfg->accesslog_maxlogsize * LOG_MB_IN_BYTES;
    loginfo.log_access_rotationsync_enabled = cfg->accesslog_rotationsync_enabled;
    loginfo.log_access_rotationsynchour = cfg->accesslog_rotationsynchour;
    loginfo.log_access_rotationsyncmin = cfg->accesslog_rotationsyncmin;
    loginfo.log_access_rotationsyncclock = -1;
    loginfo.log_access_rotationtime = cfg->accesslog_rotationtime; /* default: 1 */
    loginfo.log_access_rotationunit = LOG_UNIT_DAYS;               /* default: day */
    loginfo.log_access_rotationtime_secs = _SEC_PER_DAY;           /* default: 1 day */
    loginfo.log_access_maxdiskspace = cfg->accesslog_maxdiskspace * LOG_MB_IN_BYTES;
    loginfo.log_access_minfreespace = cfg->accesslog_minfreespace * LOG_MB_IN_BYTES;
    loginfo.log_access_exptime = cfg->accesslog_exptime; /* default: -1 */
    loginfo.log_access_exptimeunit = LOG_UNIT_MONTHS;    /* default: month */
    loginfo.log_access_exptime_secs = -1;                /* default: -1 */
    loginfo.log_access_level = cfg->accessloglevel;
    loginfo.log_access_ctime = 0L;
    loginfo.log_access_fdes = NULL;
    loginfo.log_access_file = NULL;
    loginfo.log_accessinfo_file = NULL;
    loginfo.log_numof_access_logs = 1;
    loginfo.log_access_logchain = NULL;
    loginfo.log_access_buffer = log_create_buffer(LOG_BUFFER_MAXSIZE);
    loginfo.log_access_compress = cfg->accesslog_compress;
    if (loginfo.log_access_buffer == NULL) {
        exit(-1);
    }
    if ((loginfo.log_access_buffer->lock = PR_NewLock()) == NULL) {
        exit(-1);
    }
    loginfo.log_access_stat_level = cfg->statloglevel;

    /* SECURITY LOG */
    loginfo.log_security_state = cfg->securitylog_logging_enabled;
    loginfo.log_security_mode = SLAPD_DEFAULT_FILE_MODE;
    loginfo.log_security_maxnumlogs = cfg->securitylog_maxnumlogs;
    loginfo.log_security_maxlogsize = cfg->securitylog_maxlogsize * LOG_MB_IN_BYTES;
    loginfo.log_security_rotationsync_enabled = cfg->securitylog_rotationsync_enabled;
    loginfo.log_security_rotationsynchour = cfg->securitylog_rotationsynchour;
    loginfo.log_security_rotationsyncmin = cfg->securitylog_rotationsyncmin;
    loginfo.log_security_rotationsyncclock = -1;
    loginfo.log_security_rotationtime = cfg->securitylog_rotationtime; /* default: 1 */
    loginfo.log_security_rotationunit = LOG_UNIT_WEEKS;                /* default: week */
    loginfo.log_security_rotationtime_secs = 604800;                   /* default: 1 day */
    loginfo.log_security_maxdiskspace = cfg->securitylog_maxdiskspace * LOG_MB_IN_BYTES;
    loginfo.log_security_minfreespace = cfg->securitylog_minfreespace * LOG_MB_IN_BYTES;
    loginfo.log_security_exptime = cfg->securitylog_exptime; /* default: -1 */
    loginfo.log_security_exptimeunit = LOG_UNIT_MONTHS;      /* default: month */
    loginfo.log_security_exptime_secs = -1;                  /* default: -1 */
    loginfo.log_security_level = cfg->securityloglevel;
    loginfo.log_security_ctime = 0L;
    loginfo.log_security_fdes = NULL;
    loginfo.log_security_file = NULL;
    loginfo.log_securityinfo_file = NULL;
    loginfo.log_numof_access_logs = 1;
    loginfo.log_security_logchain = NULL;
    loginfo.log_security_buffer = log_create_buffer(LOG_BUFFER_MAXSIZE);
    loginfo.log_security_compress = cfg->securitylog_compress;
    if (loginfo.log_security_buffer == NULL) {
        exit(-1);
    }
    if ((loginfo.log_security_buffer->lock = PR_NewLock()) == NULL) {
        exit(-1);
    }

    /* ERROR LOG */
    loginfo.log_error_state = cfg->errorlog_logging_enabled;
    loginfo.log_error_mode = SLAPD_DEFAULT_FILE_MODE;
    loginfo.log_error_maxnumlogs = cfg->errorlog_maxnumlogs;
    loginfo.log_error_maxlogsize = cfg->errorlog_maxlogsize * LOG_MB_IN_BYTES;
    loginfo.log_error_rotationsync_enabled = cfg->errorlog_rotationsync_enabled;
    loginfo.log_error_rotationsynchour = cfg->errorlog_rotationsynchour;
    loginfo.log_error_rotationsyncmin = cfg->errorlog_rotationsyncmin;
    loginfo.log_error_rotationsyncclock = -1;
    loginfo.log_error_rotationtime = cfg->errorlog_rotationtime; /* default: 1 */
    loginfo.log_error_rotationunit = LOG_UNIT_WEEKS;             /* default: week */
    loginfo.log_error_rotationtime_secs = 604800;                /* default: 1 week */
    loginfo.log_error_maxdiskspace = cfg->errorlog_maxdiskspace * LOG_MB_IN_BYTES;
    loginfo.log_error_minfreespace = cfg->errorlog_minfreespace * LOG_MB_IN_BYTES;
    loginfo.log_error_exptime = cfg->errorlog_exptime; /* default: -1 */
    loginfo.log_error_exptimeunit = LOG_UNIT_MONTHS;   /* default: month */
    loginfo.log_error_exptime_secs = -1;               /* default: -1 */
    loginfo.log_error_ctime = 0L;
    loginfo.log_error_file = NULL;
    loginfo.log_errorinfo_file = NULL;
    loginfo.log_error_fdes = NULL;
    loginfo.log_numof_error_logs = 1;
    loginfo.log_error_logchain = NULL;
    loginfo.log_error_compress = cfg->errorlog_compress;
    if ((loginfo.log_error_rwlock = slapi_new_rwlock()) == NULL) {
        exit(-1);
    }

    /* AUDIT LOG */
    loginfo.log_audit_state = cfg->auditlog_logging_enabled;
    loginfo.log_audit_mode = SLAPD_DEFAULT_FILE_MODE;
    loginfo.log_audit_maxnumlogs = cfg->auditlog_maxnumlogs;
    loginfo.log_audit_maxlogsize = cfg->auditlog_maxlogsize * LOG_MB_IN_BYTES;
    loginfo.log_audit_rotationsync_enabled = cfg->auditlog_rotationsync_enabled;
    loginfo.log_audit_rotationsynchour = cfg->auditlog_rotationsynchour;
    loginfo.log_audit_rotationsyncmin = cfg->auditlog_rotationsyncmin;
    loginfo.log_audit_rotationsyncclock = -1;
    loginfo.log_audit_rotationtime = cfg->auditlog_rotationtime; /* default: 1 */
    loginfo.log_audit_rotationunit = LOG_UNIT_WEEKS;             /* default: week */
    loginfo.log_audit_rotationtime_secs = 604800;                /* default: 1 week */
    loginfo.log_audit_maxdiskspace = cfg->auditlog_maxdiskspace * LOG_MB_IN_BYTES;
    loginfo.log_audit_minfreespace = cfg->auditlog_minfreespace * LOG_MB_IN_BYTES;
    loginfo.log_audit_exptime = cfg->auditlog_exptime; /* default: -1 */
    loginfo.log_audit_exptimeunit = LOG_UNIT_WEEKS;    /* default: week */
    loginfo.log_audit_exptime_secs = -1;               /* default: -1 */
    loginfo.log_audit_ctime = 0L;
    loginfo.log_audit_file = NULL;
    loginfo.log_auditinfo_file = NULL;
    loginfo.log_numof_audit_logs = 1;
    loginfo.log_audit_fdes = NULL;
    loginfo.log_audit_logchain = NULL;
    loginfo.log_audit_compress = cfg->auditlog_compress;
    if ((loginfo.log_audit_rwlock = slapi_new_rwlock()) == NULL) {
        exit(-1);
    }

    /* AUDIT FAIL LOG */
    loginfo.log_auditfail_state = cfg->auditfaillog_logging_enabled;
    loginfo.log_auditfail_mode = SLAPD_DEFAULT_FILE_MODE;
    loginfo.log_auditfail_maxnumlogs = cfg->auditfaillog_maxnumlogs;
    loginfo.log_auditfail_maxlogsize = cfg->auditfaillog_maxlogsize * LOG_MB_IN_BYTES;
    loginfo.log_auditfail_rotationsync_enabled = cfg->auditfaillog_rotationsync_enabled;
    loginfo.log_auditfail_rotationsynchour = cfg->auditfaillog_rotationsynchour;
    loginfo.log_auditfail_rotationsyncmin = cfg->auditfaillog_rotationsyncmin;
    loginfo.log_auditfail_rotationsyncclock = -1;
    loginfo.log_auditfail_rotationtime = cfg->auditfaillog_rotationtime; /* default: 1 */
    loginfo.log_auditfail_rotationunit = LOG_UNIT_WEEKS;                 /* default: week */
    loginfo.log_auditfail_rotationtime_secs = 604800;                    /* default: 1 week */
    loginfo.log_auditfail_maxdiskspace = cfg->auditfaillog_maxdiskspace * LOG_MB_IN_BYTES;
    loginfo.log_auditfail_minfreespace = cfg->auditfaillog_minfreespace * LOG_MB_IN_BYTES;
    loginfo.log_auditfail_exptime = cfg->auditfaillog_exptime; /* default: -1 */
    loginfo.log_auditfail_exptimeunit = LOG_UNIT_WEEKS;        /* default: week */
    loginfo.log_auditfail_exptime_secs = -1;                   /* default: -1 */
    loginfo.log_auditfail_ctime = 0L;
    loginfo.log_auditfail_file = NULL;
    loginfo.log_auditfailinfo_file = NULL;
    loginfo.log_numof_auditfail_logs = 1;
    loginfo.log_auditfail_fdes = NULL;
    loginfo.log_auditfail_logchain = NULL;
    loginfo.log_backend = LOGGING_BACKEND_INTERNAL;
    loginfo.log_auditfail_compress = cfg->auditfaillog_compress;
    if ((loginfo.log_auditfail_rwlock = slapi_new_rwlock()) == NULL) {
        exit(-1);
    }
    CFG_UNLOCK_READ(cfg);
}

/******************************************************************************
* Tell me if log is enabled or disabled
******************************************************************************/
int
log_set_logging(const char *attrname, char *value, int logtype, char *errorbuf, int apply)
{
    int v;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (NULL == value) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: NULL value; valid values are \"on\" or \"off\"", attrname);
        return LDAP_OPERATIONS_ERROR;
    }

    if (strcasecmp(value, "on") == 0) {
        v = LOGGING_ENABLED;
    } else if (strcasecmp(value, "off") == 0) {
        v = 0;
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", valid values are \"on\" or \"off\"",
                              attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return LDAP_SUCCESS;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        fe_cfg->accesslog_logging_enabled = v;
        if (v) {
            loginfo.log_access_state |= LOGGING_ENABLED;
        } else {
            loginfo.log_access_state &= ~LOGGING_ENABLED;
        }
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        fe_cfg->securitylog_logging_enabled = v;
        if (v) {
            loginfo.log_security_state |= LOGGING_ENABLED;
        } else {
            loginfo.log_security_state &= ~LOGGING_ENABLED;
        }
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        fe_cfg->errorlog_logging_enabled = v;
        if (v) {
            loginfo.log_error_state |= LOGGING_ENABLED;
        } else {
            loginfo.log_error_state &= ~LOGGING_ENABLED;
        }
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        fe_cfg->auditlog_logging_enabled = v;
        if (v) {
            loginfo.log_audit_state |= LOGGING_ENABLED;
        } else {
            loginfo.log_audit_state &= ~LOGGING_ENABLED;
        }
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        fe_cfg->auditfaillog_logging_enabled = v;
        if (v) {
            loginfo.log_auditfail_state |= LOGGING_ENABLED;
        } else {
            loginfo.log_auditfail_state &= ~LOGGING_ENABLED;
        }
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
    }

    return LDAP_SUCCESS;
}

/******************************************************************************
* Tell me if log is going to be compressed
******************************************************************************/
int
log_set_compression(const char *attrname, char *value, int logtype, char *errorbuf, int apply)
{
    int v;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (NULL == value) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: NULL value; valid values are \"on\" or \"off\"", attrname);
        return LDAP_OPERATIONS_ERROR;
    }

    if (strcasecmp(value, "on") == 0) {
        v = LOGGING_COMPRESS_ENABLED;
    } else if (strcasecmp(value, "off") == 0) {
        v = 0;
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", valid values are \"on\" or \"off\"",
                              attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return LDAP_SUCCESS;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        fe_cfg->accesslog_compress = v;
        if (v) {
            loginfo.log_access_compress |= LOGGING_COMPRESS_ENABLED;
        } else {
            loginfo.log_access_compress &= ~LOGGING_COMPRESS_ENABLED;
        }
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        fe_cfg->securitylog_compress = v;
        if (v) {
            loginfo.log_security_compress |= LOGGING_COMPRESS_ENABLED;
        } else {
            loginfo.log_security_compress &= ~LOGGING_COMPRESS_ENABLED;
        }
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        fe_cfg->errorlog_compress = v;
        if (v) {
            loginfo.log_error_compress |= LOGGING_COMPRESS_ENABLED;
        } else {
            loginfo.log_error_compress &= ~LOGGING_COMPRESS_ENABLED;
        }
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        fe_cfg->auditlog_compress = v;
        if (v) {
            loginfo.log_audit_compress |= LOGGING_COMPRESS_ENABLED;
        } else {
            loginfo.log_audit_compress &= ~LOGGING_COMPRESS_ENABLED;
        }
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        fe_cfg->auditfaillog_compress = v;
        if (v) {
            loginfo.log_auditfail_compress |= LOGGING_COMPRESS_ENABLED;
        } else {
            loginfo.log_auditfail_compress &= ~LOGGING_COMPRESS_ENABLED;
        }
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
    }

    return LDAP_SUCCESS;
}

static void
log_external_libs_debug_openldap_print(char *buffer)
{
    slapi_log_error(SLAPI_LOG_WARNING, "libldap/libber", "%s", buffer);
}

int
log_external_libs_debug_set_log_fn(void)
{
    int rc = ber_set_option(NULL, LBER_OPT_LOG_PRINT_FN, log_external_libs_debug_openldap_print);
    if (rc != LBER_OPT_SUCCESS) {
        slapi_log_error(SLAPI_LOG_WARNING, "libldap/libber",
              "Failed to init Log Function, err = %d\n", rc);
    }
    return rc;
}

int
log_set_backend(const char *attrname __attribute__((unused)), char *value, int logtype __attribute__((unused)), char *errorbuf __attribute__((unused)), int apply)
{

    int backend_flags = 0;
    char *backendstr = NULL; /* The backend we are looking at */
    char *token = NULL;      /* String to tokenise, need to dup value */
    char *next = NULL;       /* The next value */

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* We don't need to bother checking log type ... */
    if (!value || !*value) {
        return LDAP_OPERATIONS_ERROR;
    }

    /* We have a comma seperated list. So split it up */
    token = slapi_ch_strdup(value);
    for (backendstr = ldap_utf8strtok_r(token, ",", &next);
         backendstr != NULL;
         backendstr = ldap_utf8strtok_r(NULL, ",", &next)) {
        if (strlen(backendstr) == 0) {
            /* Probably means someone did ",,"*/
            continue;
        } else if (slapi_UTF8NCASECMP(backendstr, "dirsrv-log", 10) == 0) {
            backend_flags |= LOGGING_BACKEND_INTERNAL;
        } else if (slapi_UTF8NCASECMP(backendstr, "syslog", 6) == 0) {
            backend_flags |= LOGGING_BACKEND_SYSLOG;
#ifdef HAVE_JOURNALD
        } else if (slapi_UTF8NCASECMP(backendstr, "journald", 8) == 0) {
            backend_flags |= LOGGING_BACKEND_JOURNALD;
#endif
        }
    }
    slapi_ch_free_string(&token);

    if (!(backend_flags & LOGGING_BACKEND_INTERNAL) && !(backend_flags & LOGGING_BACKEND_SYSLOG)
#ifdef HAVE_JOURNALD
        && !(backend_flags & LOGGING_BACKEND_JOURNALD)
#endif
            ) {
        /* There is probably a better error here .... */
        return LDAP_OPERATIONS_ERROR;
    }
    if (apply) {
        /* We have a valid backend, set it */
        /*
         * We just need to use any lock here, doesn't matter which.
         */
        LOG_ACCESS_LOCK_WRITE();
        loginfo.log_backend = backend_flags;
        slapi_ch_free_string(&(slapdFrontendConfig->logging_backend));
        slapdFrontendConfig->logging_backend = slapi_ch_strdup(value);
        LOG_ACCESS_UNLOCK_WRITE();
    }

    return LDAP_SUCCESS;
}
/******************************************************************************
* Tell me  the access log file name inc path
******************************************************************************/
char *
g_get_access_log()
{
    char *logfile = NULL;

    LOG_ACCESS_LOCK_READ();
    if (loginfo.log_access_file)
        logfile = slapi_ch_strdup(loginfo.log_access_file);
    LOG_ACCESS_UNLOCK_READ();

    return logfile;
}

/******************************************************************************
* Point to a new access logdir
*
* Returns:
*    LDAP_SUCCESS -- success
*    LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*    LDAP_LOCAL_ERRO  -- some error
******************************************************************************/
int
log_update_accesslogdir(char *pathname, int apply)
{
    int rv = LDAP_SUCCESS;
    LOGFD fp;

    /* try to open the file, we may have a incorrect path */
    if (!LOG_OPEN_APPEND(fp, pathname, loginfo.log_access_mode)) {
        slapi_log_err(SLAPI_LOG_WARNING, "log_update_accesslogdir - Can't open file %s. "
                                         "errno %d (%s)\n",
                      pathname, errno, slapd_system_strerror(errno));
        /* stay with the current log file */
        return LDAP_UNWILLING_TO_PERFORM;
    }
    LOG_CLOSE(fp);

    /* skip the rest if we aren't doing this for real */
    if (!apply) {
        return LDAP_SUCCESS;
    }

    /*
    ** The user has changed the access log directory. That means we
    ** need to start fresh.
    */
    LOG_ACCESS_LOCK_WRITE();
    if (loginfo.log_access_fdes) {
        LogFileInfo *logp, *d_logp;

        slapi_log_err(SLAPI_LOG_TRACE,
                      "LOGINFO:Closing the access log file. "
                      "Moving to a new access log file (%s)\n",
                      pathname, 0, 0);

        LOG_CLOSE(loginfo.log_access_fdes);
        loginfo.log_access_fdes = 0;
        loginfo.log_access_ctime = 0;
        logp = loginfo.log_access_logchain;
        while (logp) {
            d_logp = logp;
            logp = logp->l_next;
            slapi_ch_free((void **)&d_logp);
        }
        loginfo.log_access_logchain = NULL;
        slapi_ch_free((void **)&loginfo.log_access_file);
        loginfo.log_access_file = NULL;
        loginfo.log_numof_access_logs = 1;
    }

    /* Now open the new access log file */
    if (access_log_openf(pathname, 1 /* locked */)) {
        rv = LDAP_LOCAL_ERROR; /* error Unable to use the new dir */
    }
    LOG_ACCESS_UNLOCK_WRITE();
    return rv;
}

/******************************************************************************
* Tell me  the error log file name inc path
******************************************************************************/
char *
g_get_error_log()
{
    char *logfile = NULL;

    LOG_ERROR_LOCK_READ();
    if (loginfo.log_error_file)
        logfile = slapi_ch_strdup(loginfo.log_error_file);
    LOG_ERROR_UNLOCK_READ();

    return logfile;
}
/******************************************************************************
* Point to a new error logdir
*
* Returns:
*    LDAP_SUCCESS -- success
*    LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*    LDAP_LOCAL_ERRO  -- some error
******************************************************************************/
int
log_update_errorlogdir(char *pathname, int apply)
{

    int rv = LDAP_SUCCESS;
    LOGFD fp;

    /* try to open the file, we may have a incorrect path */
    if (!LOG_OPEN_APPEND(fp, pathname, loginfo.log_error_mode)) {
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
    if (!apply) {
        return LDAP_SUCCESS;
    }

    /*
    ** The user has changed the error log directory. That means we
    ** need to start fresh.
    */
    LOG_ERROR_LOCK_WRITE();
    if (loginfo.log_error_fdes) {
        LogFileInfo *logp, *d_logp;

        LOG_CLOSE(loginfo.log_error_fdes);
        loginfo.log_error_fdes = 0;
        loginfo.log_error_ctime = 0;
        logp = loginfo.log_error_logchain;
        while (logp) {
            d_logp = logp;
            logp = logp->l_next;
            slapi_ch_free((void **)&d_logp);
        }
        loginfo.log_error_logchain = NULL;
        slapi_ch_free((void **)&loginfo.log_error_file);
        loginfo.log_error_file = NULL;
        loginfo.log_numof_error_logs = 1;
    }

    /* Now open the new errorlog */
    if (error_log_openf(pathname, 1 /* obtained lock */)) {
        rv = LDAP_LOCAL_ERROR; /* error: Unable to use the new dir */
    }

    LOG_ERROR_UNLOCK_WRITE();
    return rv;
}
/******************************************************************************
* Tell me  the audit log file name inc path
******************************************************************************/
char *
g_get_audit_log()
{
    char *logfile = NULL;

    LOG_AUDIT_LOCK_READ();
    if (loginfo.log_audit_file)
        logfile = slapi_ch_strdup(loginfo.log_audit_file);
    LOG_AUDIT_UNLOCK_READ();

    return logfile;
}
/******************************************************************************
* Point to a new audit logdir
*
* Returns:
*    LDAP_SUCCESS -- success
*    LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*    LDAP_LOCAL_ERRO  -- some error
******************************************************************************/
int
log_update_auditlogdir(char *pathname, int apply)
{
    int rv = LDAP_SUCCESS;
    LOGFD fp;

    /* try to open the file, we may have a incorrect path */
    if (!LOG_OPEN_APPEND(fp, pathname, loginfo.log_audit_mode)) {
        slapi_log_err(SLAPI_LOG_WARNING, "log_update_auditlogdir - Can't open file %s. "
                                         "errno %d (%s)\n",
                      pathname, errno, slapd_system_strerror(errno));
        /* stay with the current log file */
        return LDAP_UNWILLING_TO_PERFORM;
    }
    LOG_CLOSE(fp);

    /* skip the rest if we aren't doing this for real */
    if (!apply) {
        return LDAP_SUCCESS;
    }

    /*
    ** The user has changed the audit log directory. That means we
    ** need to start fresh.
    */
    LOG_AUDIT_LOCK_WRITE();
    if (loginfo.log_audit_fdes) {
        LogFileInfo *logp, *d_logp;
        slapi_log_err(SLAPI_LOG_TRACE,
                      "LOGINFO:Closing the audit log file. "
                      "Moving to a new audit file (%s)\n",
                      pathname, 0, 0);

        LOG_CLOSE(loginfo.log_audit_fdes);
        loginfo.log_audit_fdes = 0;
        loginfo.log_audit_ctime = 0;
        logp = loginfo.log_audit_logchain;
        while (logp) {
            d_logp = logp;
            logp = logp->l_next;
            slapi_ch_free((void **)&d_logp);
        }
        loginfo.log_audit_logchain = NULL;
        slapi_ch_free((void **)&loginfo.log_audit_file);
        loginfo.log_audit_file = NULL;
        loginfo.log_numof_audit_logs = 1;
    }

    /* Now open the new auditlog */
    if (audit_log_openf(pathname, 1 /* locked */)) {
        rv = LDAP_LOCAL_ERROR; /* error: Unable to use the new dir */
    }
    LOG_AUDIT_UNLOCK_WRITE();
    return rv;
}

/******************************************************************************
* Tell me  the audit fail log file name inc path
******************************************************************************/
char *
g_get_auditfail_log()
{
    char *logfile = NULL;

    LOG_AUDITFAIL_LOCK_READ();
    if (loginfo.log_auditfail_file) {
        logfile = slapi_ch_strdup(loginfo.log_auditfail_file);
    }
    LOG_AUDITFAIL_UNLOCK_READ();

    return logfile;
}
/******************************************************************************
* Point to a new auditfail logdir
*
* Returns:
*    LDAP_SUCCESS -- success
*    LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*    LDAP_LOCAL_ERRO  -- some error
******************************************************************************/
int
log_update_auditfaillogdir(char *pathname, int apply)
{
    int rv = LDAP_SUCCESS;
    LOGFD fp;

    /* try to open the file, we may have a incorrect path */
    if (!LOG_OPEN_APPEND(fp, pathname, loginfo.log_auditfail_mode)) {
        slapi_log_err(SLAPI_LOG_WARNING, "log_update_auditfaillogdir - Can't open file %s. "
                                         "errno %d (%s)\n",
                      pathname, errno, slapd_system_strerror(errno));
        /* stay with the current log file */
        return LDAP_UNWILLING_TO_PERFORM;
    }
    LOG_CLOSE(fp);

    /* skip the rest if we aren't doing this for real */
    if (!apply) {
        return LDAP_SUCCESS;
    }

    /*
    ** The user has changed the audit log directory. That means we
    ** need to start fresh.
    */
    LOG_AUDITFAIL_LOCK_WRITE();
    if (loginfo.log_auditfail_fdes) {
        LogFileInfo *logp, *d_logp;
        slapi_log_err(SLAPI_LOG_TRACE,
                      "LOGINFO:Closing the auditfail log file. "
                      "Moving to a new auditfail file (%s)\n",
                      pathname, 0, 0);

        LOG_CLOSE(loginfo.log_auditfail_fdes);
        loginfo.log_auditfail_fdes = 0;
        loginfo.log_auditfail_ctime = 0;
        logp = loginfo.log_auditfail_logchain;
        while (logp) {
            d_logp = logp;
            logp = logp->l_next;
            slapi_ch_free((void **)&d_logp);
        }
        loginfo.log_auditfail_logchain = NULL;
        slapi_ch_free((void **)&loginfo.log_auditfail_file);
        loginfo.log_auditfail_file = NULL;
        loginfo.log_numof_auditfail_logs = 1;
    }

    /* Now open the new auditlog */
    if (auditfail_log_openf(pathname, 1 /* locked */)) {
        rv = LDAP_LOCAL_ERROR; /* error: Unable to use the new dir */
    }
    LOG_AUDITFAIL_UNLOCK_WRITE();
    return rv;
}

int
log_set_mode(const char *attrname, char *value, int logtype, char *errorbuf, int apply)
{
    int64_t v = 0;
    int retval = LDAP_SUCCESS;
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (NULL == value) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: null value; valid values are are of the format \"yz-yz-yz-\" where y could be 'r' or '-',"
                              " and z could be 'w' or '-'",
                              attrname);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return LDAP_SUCCESS;
    }

    errno = 0;
    v = strtol(value, &endp, 8);
    if (*endp != '\0' || errno == ERANGE ||
        strlen(value) != 3 ||
        v > 0777 /* octet of 777 511 */ ||
        v < 0)
    {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s) (%ld), value must be three digits between 000 and 777",
                value, attrname, v);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        if (loginfo.log_access_file &&
            (chmod(loginfo.log_access_file, v) != 0)) {
            int oserr = errno;
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "%s: Failed to chmod access log file to %s: errno %d (%s)",
                                  attrname, value, oserr, slapd_system_strerror(oserr));
            retval = LDAP_UNWILLING_TO_PERFORM;
        } else { /* only apply the changes if no file or if successful */
            slapi_ch_free((void **)&fe_cfg->accesslog_mode);
            fe_cfg->accesslog_mode = slapi_ch_strdup(value);
            loginfo.log_access_mode = v;
        }
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        if (loginfo.log_security_file &&
            (chmod(loginfo.log_security_file, v) != 0)) {
            int oserr = errno;
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "%s: Failed to chmod security audit log file to %s: errno %d (%s)",
                                  attrname, value, oserr, slapd_system_strerror(oserr));
            retval = LDAP_UNWILLING_TO_PERFORM;
        } else { /* only apply the changes if no file or if successful */
            slapi_ch_free((void **)&fe_cfg->securitylog_mode);
            fe_cfg->securitylog_mode = slapi_ch_strdup(value);
            loginfo.log_security_mode = v;
        }
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        if (loginfo.log_error_file &&
            (chmod(loginfo.log_error_file, v) != 0)) {
            int oserr = errno;
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "%s: Failed to chmod error log file to %s: errno %d (%s)",
                                  attrname, value, oserr, slapd_system_strerror(oserr));
            retval = LDAP_UNWILLING_TO_PERFORM;
        } else { /* only apply the changes if no file or if successful */
            slapi_ch_free((void **)&fe_cfg->errorlog_mode);
            fe_cfg->errorlog_mode = slapi_ch_strdup(value);
            loginfo.log_error_mode = v;
        }
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        if (loginfo.log_audit_file &&
            (chmod(loginfo.log_audit_file, v) != 0)) {
            int oserr = errno;
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "%s: Failed to chmod audit log file to %s: errno %d (%s)",
                                  attrname, value, oserr, slapd_system_strerror(oserr));
            retval = LDAP_UNWILLING_TO_PERFORM;
        } else { /* only apply the changes if no file or if successful */
            slapi_ch_free((void **)&fe_cfg->auditlog_mode);
            fe_cfg->auditlog_mode = slapi_ch_strdup(value);
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
    char *endp = NULL;
    int rv = LDAP_SUCCESS;
    int64_t numlogs;

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        rv = LDAP_OPERATIONS_ERROR;
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "%s: invalid log type %d", attrname, logtype);
    }
    if (!apply || !numlogs_str || !*numlogs_str) {
        return rv;
    }

    errno = 0;
    numlogs = strtol(numlogs_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || numlogs < 1) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be between 1 and 2147483647",
                numlogs_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    if (numlogs >= 1) {
        switch (logtype) {
        case SLAPD_ACCESS_LOG:
            LOG_ACCESS_LOCK_WRITE();
            loginfo.log_access_maxnumlogs = numlogs;
            fe_cfg->accesslog_maxnumlogs = numlogs;
            LOG_ACCESS_UNLOCK_WRITE();
            break;
        case SLAPD_SECURITY_LOG:
            LOG_SECURITY_LOCK_WRITE();
            loginfo.log_security_maxnumlogs = numlogs;
            fe_cfg->securitylog_maxnumlogs = numlogs;
            LOG_SECURITY_UNLOCK_WRITE();
            break;
        case SLAPD_ERROR_LOG:
            LOG_ERROR_LOCK_WRITE();
            loginfo.log_error_maxnumlogs = numlogs;
            fe_cfg->errorlog_maxnumlogs = numlogs;
            LOG_ERROR_UNLOCK_WRITE();
            break;
        case SLAPD_AUDIT_LOG:
            LOG_AUDIT_LOCK_WRITE();
            loginfo.log_audit_maxnumlogs = numlogs;
            fe_cfg->auditlog_maxnumlogs = numlogs;
            LOG_AUDIT_UNLOCK_WRITE();
            break;
        case SLAPD_AUDITFAIL_LOG:
            LOG_AUDITFAIL_LOCK_WRITE();
            loginfo.log_auditfail_maxnumlogs = numlogs;
            fe_cfg->auditfaillog_maxnumlogs = numlogs;
            LOG_AUDITFAIL_UNLOCK_WRITE();
            break;
        default:
            rv = LDAP_OPERATIONS_ERROR;
            slapi_log_err(SLAPI_LOG_ERR, "log_set_numlogsperdir",
                          "Invalid log type %d", logtype);
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
    int rv = LDAP_SUCCESS;
    int64_t max_logsize; /* in bytes */
    int64_t logsize;     /* in megabytes */
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (!apply || !logsize_str || !*logsize_str)
        return rv;

    errno = 0;
    logsize = strtol(logsize_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || logsize < -1 || logsize == 0) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be \"-1\" or greater than 0",
                logsize_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /* convert it to bytes */
    max_logsize = logsize * LOG_MB_IN_BYTES;

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        if (apply) {
            LOG_ACCESS_LOCK_WRITE();
            loginfo.log_access_maxlogsize = max_logsize;
            fe_cfg->accesslog_maxlogsize = logsize;
            LOG_ACCESS_UNLOCK_WRITE();
        }
        break;
    case SLAPD_SECURITY_LOG:
        if (apply) {
            LOG_SECURITY_LOCK_WRITE();
            loginfo.log_security_maxlogsize = max_logsize;
            fe_cfg->securitylog_maxlogsize = logsize;
            LOG_SECURITY_UNLOCK_WRITE();
        }
        break;
    case SLAPD_ERROR_LOG:
        if (apply) {
            LOG_ERROR_LOCK_WRITE();
            loginfo.log_error_maxlogsize = max_logsize;
            fe_cfg->errorlog_maxlogsize = logsize;
            LOG_ERROR_UNLOCK_WRITE();
        }
        break;
    case SLAPD_AUDIT_LOG:
        if (apply) {
            LOG_AUDIT_LOCK_WRITE();
            loginfo.log_audit_maxlogsize = max_logsize;
            fe_cfg->auditlog_maxlogsize = logsize;
            LOG_AUDIT_UNLOCK_WRITE();
        }
        break;
    case SLAPD_AUDITFAIL_LOG:
        if (apply) {
            LOG_AUDITFAIL_LOCK_WRITE();
            loginfo.log_auditfail_maxlogsize = max_logsize;
            fe_cfg->auditfaillog_maxlogsize = logsize;
            LOG_AUDITFAIL_UNLOCK_WRITE();
        }
        break;
    default:
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "%s: invalid logtype %d", attrname, logtype);
        rv = LDAP_OPERATIONS_ERROR;
    }

    return rv;
}

time_t
log_get_rotationsyncclock(int synchour, int syncmin)
{
    struct tm *currtm;
    time_t currclock;
    time_t syncclock;
    int hours, minutes;

    time(&currclock);
    currtm = localtime(&currclock);

    if (syncmin < currtm->tm_min) {
        minutes = syncmin + 60 - currtm->tm_min;
        hours = synchour - 1 - currtm->tm_hour;
    } else {
        minutes = syncmin - currtm->tm_min;
        hours = synchour - currtm->tm_hour;
    }
    if (hours < 0)
        hours += 24;

    syncclock = currclock + hours * 3600 + minutes * 60;
    return syncclock;
}

int
log_set_rotationsync_enabled(const char *attrname, char *value, int logtype, char *errorbuf, int apply)
{
    int v;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (NULL == value) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: NULL value; valid values are \"on\" or \"off\"", attrname);
        return LDAP_OPERATIONS_ERROR;
    }

    if (strcasecmp(value, "on") == 0) {
        v = LDAP_ON;
    } else if (strcasecmp(value, "off") == 0) {
        v = LDAP_OFF;
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", valid values are \"on\" or \"off\"", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return LDAP_SUCCESS;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        fe_cfg->accesslog_rotationsync_enabled = v;
        loginfo.log_access_rotationsync_enabled = v;
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        fe_cfg->securitylog_rotationsync_enabled = v;
        loginfo.log_security_rotationsync_enabled = v;
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        fe_cfg->errorlog_rotationsync_enabled = v;
        loginfo.log_error_rotationsync_enabled = v;
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        fe_cfg->auditlog_rotationsync_enabled = v;
        loginfo.log_audit_rotationsync_enabled = v;
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        fe_cfg->auditfaillog_rotationsync_enabled = v;
        loginfo.log_auditfail_rotationsync_enabled = v;
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
    }
    return LDAP_SUCCESS;
}

int
log_set_rotationsynchour(const char *attrname, char *rhour_str, int logtype, char *returntext, int apply)
{
    int64_t rhour = -1;
    int rv = LDAP_SUCCESS;
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "%s: invalid log type: %d", attrname, logtype);
        return LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply || !rhour_str || !*rhour_str) {
        return rv;
    }

    errno = 0;
    rhour = strtol(rhour_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || rhour < 0 || rhour > 23) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be \"0\" thru \"23\"",
                rhour_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        loginfo.log_access_rotationsynchour = rhour;
        loginfo.log_access_rotationsyncclock = log_get_rotationsyncclock(rhour, loginfo.log_access_rotationsyncmin);
        fe_cfg->accesslog_rotationsynchour = rhour;
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        loginfo.log_security_rotationsynchour = rhour;
        loginfo.log_security_rotationsyncclock = log_get_rotationsyncclock(rhour, loginfo.log_security_rotationsyncmin);
        fe_cfg->accesslog_rotationsynchour = rhour;
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        loginfo.log_error_rotationsynchour = rhour;
        loginfo.log_error_rotationsyncclock = log_get_rotationsyncclock(rhour, loginfo.log_error_rotationsyncmin);
        fe_cfg->errorlog_rotationsynchour = rhour;
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        loginfo.log_audit_rotationsynchour = rhour;
        loginfo.log_audit_rotationsyncclock = log_get_rotationsyncclock(rhour, loginfo.log_audit_rotationsyncmin);
        fe_cfg->auditlog_rotationsynchour = rhour;
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        loginfo.log_auditfail_rotationsynchour = rhour;
        loginfo.log_auditfail_rotationsyncclock = log_get_rotationsyncclock(rhour, loginfo.log_auditfail_rotationsyncmin);
        fe_cfg->auditfaillog_rotationsynchour = rhour;
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
    }

    return rv;
}

int
log_set_rotationsyncmin(const char *attrname, char *rmin_str, int logtype, char *returntext, int apply)
{
    int64_t rmin = -1;
    int rv = LDAP_SUCCESS;
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "%s: invalid log type: %d", attrname, logtype);
        return LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply || !rmin_str || !*rmin_str) {
        return rv;
    }

    errno = 0;
    rmin = strtol(rmin_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || rmin < 0 || rmin > 59) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be between \"0\" and \"59\"",
                rmin_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        loginfo.log_access_rotationsyncmin = rmin;
        fe_cfg->accesslog_rotationsyncmin = rmin;
        loginfo.log_access_rotationsyncclock = log_get_rotationsyncclock(loginfo.log_access_rotationsynchour, rmin);
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        loginfo.log_security_rotationsyncmin = rmin;
        fe_cfg->securitylog_rotationsyncmin = rmin;
        loginfo.log_security_rotationsyncclock = log_get_rotationsyncclock(loginfo.log_security_rotationsynchour, rmin);
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        loginfo.log_error_rotationsyncmin = rmin;
        loginfo.log_error_rotationsyncclock = log_get_rotationsyncclock(loginfo.log_error_rotationsynchour, rmin);
        fe_cfg->errorlog_rotationsyncmin = rmin;
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        loginfo.log_audit_rotationsyncmin = rmin;
        fe_cfg->auditlog_rotationsyncmin = rmin;
        loginfo.log_audit_rotationsyncclock = log_get_rotationsyncclock(loginfo.log_audit_rotationsynchour, rmin);
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        loginfo.log_auditfail_rotationsyncmin = rmin;
        fe_cfg->auditfaillog_rotationsyncmin = rmin;
        loginfo.log_auditfail_rotationsyncclock = log_get_rotationsyncclock(loginfo.log_auditfail_rotationsynchour, rmin);
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
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

    int runit = 0;
    int64_t value, rtime;
    int rv = LDAP_SUCCESS;
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "%s: invalid log type: %d", attrname, logtype);
        return LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply || !rtime_str || !*rtime_str) {
        return rv;
    }

    errno = 0;
    rtime = strtol(rtime_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || rtime < -1 || rtime == 0) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be \"-1\" or greater than \"0\"",
                rtime_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        loginfo.log_access_rotationtime = rtime;
        runit = loginfo.log_access_rotationunit;
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        loginfo.log_security_rotationtime = rtime;
        runit = loginfo.log_security_rotationunit;
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        loginfo.log_error_rotationtime = rtime;
        runit = loginfo.log_error_rotationunit;
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        loginfo.log_audit_rotationtime = rtime;
        runit = loginfo.log_audit_rotationunit;
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        loginfo.log_auditfail_rotationtime = rtime;
        runit = loginfo.log_auditfail_rotationunit;
        break;
    }

    /* find out the rotation unit we have se right now */
    if (runit == LOG_UNIT_MONTHS) {
        value = 31 * 24 * 60 * 60 * rtime;
    } else if (runit == LOG_UNIT_WEEKS) {
        value = 7 * 24 * 60 * 60 * rtime;
    } else if (runit == LOG_UNIT_DAYS) {
        value = 24 * 60 * 60 * rtime;
    } else if (runit == LOG_UNIT_HOURS) {
        value = 3600 * rtime;
    } else if (runit == LOG_UNIT_MINS) {
        value = 60 * rtime;
    } else {
        /* In this case we don't rotate */
        value = -1;
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
    case SLAPD_SECURITY_LOG:
        fe_cfg->securitylog_rotationtime = rtime;
        loginfo.log_security_rotationtime_secs = value;
        LOG_SECURITY_UNLOCK_WRITE();
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
    case SLAPD_AUDITFAIL_LOG:
        fe_cfg->auditfaillog_rotationtime = rtime;
        loginfo.log_auditfail_rotationtime_secs = value;
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
    }
    return rv;
}
/******************************************************************************
* ROTATION TIME UNIT
* Return Values:
*   1    -- fail
*   0    -- success
******************************************************************************/
int
log_set_rotationtimeunit(const char *attrname, char *runit, int logtype, char *errorbuf, int apply)
{
    int origvalue = 0, value = 0;
    int runitType;
    int rv = 0;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid log type: %d", attrname, logtype);
        return LDAP_OPERATIONS_ERROR;
    }

    if ((strcasecmp(runit, "month") == 0) ||
        (strcasecmp(runit, "week") == 0) ||
        (strcasecmp(runit, "day") == 0) ||
        (strcasecmp(runit, "hour") == 0) ||
        (strcasecmp(runit, "minute") == 0)) {
        /* all good values */
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: unknown unit \"%s\"", attrname, runit);
        rv = LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply) {
        return rv;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        origvalue = loginfo.log_access_rotationtime;
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        origvalue = loginfo.log_security_rotationtime;
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        origvalue = loginfo.log_error_rotationtime;
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        origvalue = loginfo.log_audit_rotationtime;
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        origvalue = loginfo.log_auditfail_rotationtime;
        break;
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
    } else {
        /* In this case we don't rotate */
        runitType = LOG_UNIT_UNKNOWN;
        value = -1;
    }

    if (origvalue > 0 && value < 0) {
        value = PR_INT32_MAX; /* overflown */
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        loginfo.log_access_rotationtime_secs = value;
        loginfo.log_access_rotationunit = runitType;
        slapi_ch_free_string(&fe_cfg->accesslog_rotationunit);
        fe_cfg->accesslog_rotationunit = slapi_ch_strdup(runit);
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        loginfo.log_security_rotationtime_secs = value;
        loginfo.log_security_rotationunit = runitType;
        slapi_ch_free_string(&fe_cfg->securitylog_rotationunit);
        fe_cfg->securitylog_rotationunit = slapi_ch_strdup(runit);
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        loginfo.log_error_rotationtime_secs = value;
        loginfo.log_error_rotationunit = runitType;
        slapi_ch_free((void **)&fe_cfg->errorlog_rotationunit);
        fe_cfg->errorlog_rotationunit = slapi_ch_strdup(runit);
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        loginfo.log_audit_rotationtime_secs = value;
        loginfo.log_audit_rotationunit = runitType;
        slapi_ch_free_string(&fe_cfg->auditlog_rotationunit);
        fe_cfg->auditlog_rotationunit = slapi_ch_strdup(runit);
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        loginfo.log_auditfail_rotationtime_secs = value;
        loginfo.log_auditfail_rotationunit = runitType;
        slapi_ch_free_string(&fe_cfg->auditfaillog_rotationunit);
        fe_cfg->auditfaillog_rotationunit = slapi_ch_strdup(runit);
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
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
    int rv = 0;
    int64_t mlogsize = 0;   /* in bytes */
    int64_t maxdiskspace;   /* in bytes */
    int64_t s_maxdiskspace; /* in megabytes */
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid log type: %d", attrname, logtype);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply || !maxdiskspace_str || !*maxdiskspace_str)
        return rv;

    errno = 0;
    s_maxdiskspace = strtol(maxdiskspace_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || s_maxdiskspace < -1 || s_maxdiskspace == 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be \"-1\" or greater than 0",
                maxdiskspace_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /* Disk space are in MB  but store in bytes */
    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        mlogsize = loginfo.log_access_maxlogsize;
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        mlogsize = loginfo.log_security_maxlogsize;
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        mlogsize = loginfo.log_error_maxlogsize;
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        mlogsize = loginfo.log_audit_maxlogsize;
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        mlogsize = loginfo.log_auditfail_maxlogsize;
        break;
    }
    maxdiskspace = (PRInt64)s_maxdiskspace * LOG_MB_IN_BYTES;
    if (maxdiskspace < 0) {
        maxdiskspace = -1;
    } else if (maxdiskspace < mlogsize) {
        rv = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%d (MB)\" is less than max log size \"%d (MB)\"",
                              attrname, s_maxdiskspace, (int)(mlogsize / LOG_MB_IN_BYTES));
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        if (rv == 0 && apply) {
            loginfo.log_access_maxdiskspace = maxdiskspace;  /* in bytes */
            fe_cfg->accesslog_maxdiskspace = s_maxdiskspace; /* in megabytes */
        }
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        if (rv == 0 && apply) {
            loginfo.log_security_maxdiskspace = maxdiskspace;  /* in bytes */
            fe_cfg->securitylog_maxdiskspace = s_maxdiskspace; /* in megabytes */
        }
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        if (rv == 0 && apply) {
            loginfo.log_error_maxdiskspace = maxdiskspace;  /* in bytes */
            fe_cfg->errorlog_maxdiskspace = s_maxdiskspace; /* in megabytes */
        }
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        if (rv == 0 && apply) {
            loginfo.log_audit_maxdiskspace = maxdiskspace;  /* in bytes */
            fe_cfg->auditlog_maxdiskspace = s_maxdiskspace; /* in megabytes */
        }
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        if (rv == 0 && apply) {
            loginfo.log_auditfail_maxdiskspace = maxdiskspace;  /* in bytes */
            fe_cfg->auditfaillog_maxdiskspace = s_maxdiskspace; /* in megabytes */
        }
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
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
    int rv = LDAP_SUCCESS;
    int64_t minfreespace;  /* in megabytes */
    int64_t minfreespaceB; /* in bytes */
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid log type: %d", attrname, logtype);
        rv = LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply || !minfreespace_str || !*minfreespace_str) {
        return rv;
    }

    errno = 0;
    minfreespace = strtol(minfreespace_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || minfreespace < -1 || minfreespace == 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be \"-1\" or greater than 0",
                minfreespace_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /* Disk space are in MB  but store in bytes */
    if (minfreespace >= 1) {
        minfreespaceB = minfreespace * LOG_MB_IN_BYTES;
        switch (logtype) {
        case SLAPD_ACCESS_LOG:
            LOG_ACCESS_LOCK_WRITE();
            loginfo.log_access_minfreespace = minfreespaceB;
            fe_cfg->accesslog_minfreespace = minfreespace;
            LOG_ACCESS_UNLOCK_WRITE();
            break;
        case SLAPD_SECURITY_LOG:
            LOG_SECURITY_LOCK_WRITE();
            loginfo.log_security_minfreespace = minfreespaceB;
            fe_cfg->securitylog_minfreespace = minfreespace;
            LOG_SECURITY_UNLOCK_WRITE();
            break;
        case SLAPD_ERROR_LOG:
            LOG_ERROR_LOCK_WRITE();
            loginfo.log_error_minfreespace = minfreespaceB;
            fe_cfg->errorlog_minfreespace = minfreespace;
            LOG_ERROR_UNLOCK_WRITE();
            break;
        case SLAPD_AUDIT_LOG:
            LOG_AUDIT_LOCK_WRITE();
            loginfo.log_audit_minfreespace = minfreespaceB;
            fe_cfg->auditlog_minfreespace = minfreespace;
            LOG_AUDIT_UNLOCK_WRITE();
            break;
        case SLAPD_AUDITFAIL_LOG:
            LOG_AUDITFAIL_LOCK_WRITE();
            loginfo.log_auditfail_minfreespace = minfreespaceB;
            fe_cfg->auditfaillog_minfreespace = minfreespace;
            LOG_AUDITFAIL_UNLOCK_WRITE();
            break;
        default:
            /* This is unreachable ... */
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
    int64_t eunit, value, exptime;
    int rsec = 0;
    int rv = 0;
    char *endp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid log type: %d", attrname, logtype);
        rv = LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply || !exptime_str || !*exptime_str) {
        return rv;
    }

    errno = 0;
    exptime = strtol(exptime_str, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || exptime < -1 || exptime == 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Invalid value \"%s\" for attribute (%s), value must be \"-1\" or greater than 0",
                exptime_str, attrname);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        loginfo.log_access_exptime = exptime;
        eunit = loginfo.log_access_exptimeunit;
        rsec = loginfo.log_access_rotationtime_secs;
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        loginfo.log_security_exptime = exptime;
        eunit = loginfo.log_security_exptimeunit;
        rsec = loginfo.log_security_rotationtime_secs;
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        loginfo.log_error_exptime = exptime;
        eunit = loginfo.log_error_exptimeunit;
        rsec = loginfo.log_error_rotationtime_secs;
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        loginfo.log_audit_exptime = exptime;
        eunit = loginfo.log_audit_exptimeunit;
        rsec = loginfo.log_audit_rotationtime_secs;
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        loginfo.log_auditfail_exptime = exptime;
        eunit = loginfo.log_auditfail_exptimeunit;
        rsec = loginfo.log_auditfail_rotationtime_secs;
        break;
    default:
        /* This is unreachable */
        rv = 1;
        eunit = -1;
    }

    value = -1; /* never expires, by default */
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
    }


    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        loginfo.log_access_exptime_secs = value;
        fe_cfg->accesslog_exptime = exptime;
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        loginfo.log_security_exptime_secs = value;
        fe_cfg->securitylog_exptime = exptime;
        LOG_SECURITY_UNLOCK_WRITE();
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
    case SLAPD_AUDITFAIL_LOG:
        loginfo.log_auditfail_exptime_secs = value;
        fe_cfg->auditfaillog_exptime = exptime;
        LOG_AUDITFAIL_UNLOCK_WRITE();
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
    int value = 0;
    int rv = 0;
    int exptime = 0, rsecs = 0;
    int *exptimeunitp = NULL;
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();

    if (logtype != SLAPD_ACCESS_LOG &&
        logtype != SLAPD_SECURITY_LOG &&
        logtype != SLAPD_ERROR_LOG &&
        logtype != SLAPD_AUDIT_LOG &&
        logtype != SLAPD_AUDITFAIL_LOG) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid log type: %d", attrname, logtype);
        return LDAP_OPERATIONS_ERROR;
    }

    if (NULL == expunit) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: NULL value", attrname);
        return LDAP_OPERATIONS_ERROR;
    }

    if ((strcasecmp(expunit, "month") == 0) ||
        (strcasecmp(expunit, "week") == 0) ||
        (strcasecmp(expunit, "day") == 0)) {
        /* we have good values */
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid time unit \"%s\"", attrname, expunit);
        rv = LDAP_OPERATIONS_ERROR;
    }

    /* return if we aren't doing this for real */
    if (!apply) {
        return rv;
    }

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_WRITE();
        exptime = loginfo.log_access_exptime;
        rsecs = loginfo.log_access_rotationtime_secs;
        exptimeunitp = &(loginfo.log_access_exptimeunit);
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_WRITE();
        exptime = loginfo.log_security_exptime;
        rsecs = loginfo.log_security_rotationtime_secs;
        exptimeunitp = &(loginfo.log_security_exptimeunit);
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_WRITE();
        exptime = loginfo.log_error_exptime;
        rsecs = loginfo.log_error_rotationtime_secs;
        exptimeunitp = &(loginfo.log_error_exptimeunit);
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_WRITE();
        exptime = loginfo.log_audit_exptime;
        rsecs = loginfo.log_audit_rotationtime_secs;
        exptimeunitp = &(loginfo.log_audit_exptimeunit);
        break;
    case SLAPD_AUDITFAIL_LOG:
        LOG_AUDITFAIL_LOCK_WRITE();
        exptime = loginfo.log_auditfail_exptime;
        rsecs = loginfo.log_auditfail_rotationtime_secs;
        exptimeunitp = &(loginfo.log_auditfail_exptimeunit);
        break;
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
        slapi_ch_free_string(&(fe_cfg->accesslog_exptimeunit));
        fe_cfg->accesslog_exptimeunit = slapi_ch_strdup(expunit);
        LOG_ACCESS_UNLOCK_WRITE();
        break;
    case SLAPD_SECURITY_LOG:
        loginfo.log_security_exptime_secs = value;
        slapi_ch_free_string(&(fe_cfg->securitylog_exptimeunit));
        fe_cfg->securitylog_exptimeunit = slapi_ch_strdup(expunit);
        LOG_SECURITY_UNLOCK_WRITE();
        break;
    case SLAPD_ERROR_LOG:
        loginfo.log_error_exptime_secs = value;
        slapi_ch_free_string(&(fe_cfg->errorlog_exptimeunit));
        fe_cfg->errorlog_exptimeunit = slapi_ch_strdup(expunit);
        LOG_ERROR_UNLOCK_WRITE();
        break;
    case SLAPD_AUDIT_LOG:
        loginfo.log_audit_exptime_secs = value;
        slapi_ch_free_string(&(fe_cfg->auditlog_exptimeunit));
        fe_cfg->auditlog_exptimeunit = slapi_ch_strdup(expunit);
        LOG_AUDIT_UNLOCK_WRITE();
        break;
    case SLAPD_AUDITFAIL_LOG:
        loginfo.log_auditfail_exptime_secs = value;
        slapi_ch_free_string(&(fe_cfg->auditfaillog_exptimeunit));
        fe_cfg->auditfaillog_exptimeunit = slapi_ch_strdup(expunit);
        LOG_AUDITFAIL_UNLOCK_WRITE();
        break;
    }

    return rv;
}

/*
 * Enables HR timestamps in logs.
 */
void
log_enable_hr_timestamps()
{
    logging_hr_timestamps_enabled = 1;
}

/*
 * Disables HR timestamps in logs.
 */
void
log_disable_hr_timestamps()
{
    logging_hr_timestamps_enabled = 0;
}

/******************************************************************************
 * Write title line in log file
 *****************************************************************************/
static void
log_write_title(LOGFD fp)
{
    slapdFrontendConfig_t *fe_cfg = getFrontendConfig();
    char *buildnum = config_get_buildnum();
    char buff[512];
    int bufflen = sizeof(buff);

    PR_snprintf(buff, bufflen, "\t%s B%s\n",
                fe_cfg->versionstring ? fe_cfg->versionstring : CAPBRAND "-Directory/" DS_PACKAGE_VERSION,
                buildnum ? buildnum : "");
    LOG_WRITE_NOW_NO_ERR(fp, buff, strlen(buff), 0);

    if (fe_cfg->localhost) {
        PR_snprintf(buff, bufflen, "\t%s:%d (%s)\n\n",
                    fe_cfg->localhost,
                    fe_cfg->security ? fe_cfg->secureport : fe_cfg->port,
                    fe_cfg->configdir ? fe_cfg->configdir : "");
    } else {
        /* If fe_cfg->localhost is not set, ignore fe_cfg->port since
         * it is the default and might be misleading.
         */
        PR_snprintf(buff, bufflen, "\t<host>:<port> (%s)\n\n",
                    fe_cfg->configdir ? fe_cfg->configdir : "");
    }
    LOG_WRITE_NOW_NO_ERR(fp, buff, strlen(buff), 0);
    slapi_ch_free((void **)&buildnum);
}

/******************************************************************************
*  init function for the error log
*  Returns:
*    0    - success
*    1    - fail
******************************************************************************/
int
error_log_openf(char *pathname, int locked)
{

    int rv = 0;
    int logfile_type = 0;

    if (!locked)
        LOG_ERROR_LOCK_WRITE();
    /* save the file name */
    slapi_ch_free_string(&loginfo.log_error_file);
    loginfo.log_error_file = slapi_ch_strdup(pathname);

    /* store the rotation info file path name */
    slapi_ch_free_string(&loginfo.log_errorinfo_file);
    loginfo.log_errorinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

    /*
    ** Check if we have a log file already. If we have it then
    ** we need to parse the header info and update the loginfo
    ** struct.
    */
    logfile_type = log__error_rotationinfof(loginfo.log_errorinfo_file);

    if (log__open_errorlogfile(logfile_type, 1 /* got lock*/) != LOG_SUCCESS) {
        rv = 1;
    }

    if (!locked)
        LOG_ERROR_UNLOCK_WRITE();
    return rv;
}
/******************************************************************************
*  init function for the audit log
*  Returns:
*    0    - success
*    1    - fail
******************************************************************************/
int
audit_log_openf(char *pathname, int locked)
{

    int rv = 0;
    int logfile_type = 0;

    if (!locked)
        LOG_AUDIT_LOCK_WRITE();

    /* store the path name */
    slapi_ch_free_string(&loginfo.log_audit_file);
    loginfo.log_audit_file = slapi_ch_strdup(pathname);

    /* store the rotation info file path name */
    slapi_ch_free_string(&loginfo.log_auditinfo_file);
    loginfo.log_auditinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

    /*
    ** Check if we have a log file already. If we have it then
    ** we need to parse the header info and update the loginfo
    ** struct.
    */
    logfile_type = log__audit_rotationinfof(loginfo.log_auditinfo_file);

    if (log__open_auditlogfile(logfile_type, 1 /* got lock*/) != LOG_SUCCESS) {
        rv = 1;
    }

    if (!locked)
        LOG_AUDIT_UNLOCK_WRITE();

    return rv;
}

/******************************************************************************
*  init function for the auditfail log
*  Returns:
*    0    - success
*    1    - fail
******************************************************************************/
int
auditfail_log_openf(char *pathname, int locked)
{

    int rv = 0;
    int logfile_type = 0;

    if (!locked)
        LOG_AUDITFAIL_LOCK_WRITE();

    /* store the path name */
    slapi_ch_free_string(&loginfo.log_auditfail_file);
    loginfo.log_auditfail_file = slapi_ch_strdup(pathname);

    /* store the rotation info file path name */
    slapi_ch_free_string(&loginfo.log_auditfailinfo_file);
    loginfo.log_auditfailinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

    /*
    ** Check if we have a log file already. If we have it then
    ** we need to parse the header info and update the loginfo
    ** struct.
    */
    logfile_type = log__auditfail_rotationinfof(loginfo.log_auditfailinfo_file);

    if (log__open_auditfaillogfile(logfile_type, 1 /* got lock*/) != LOG_SUCCESS) {
        rv = 1;
    }

    if (!locked)
        LOG_AUDITFAIL_UNLOCK_WRITE();

    return rv;
}
/******************************************************************************
* write in the audit log
******************************************************************************/

int
slapd_log_audit(
    char *buffer,
    int buf_len,
    int sourcelog)
{
    /* We use this to route audit log entries to where they need to go */
    int retval = LDAP_SUCCESS;
    int lbackend = loginfo.log_backend; /* We copy this to make these next checks atomic */

    int *state;
    if (sourcelog == SLAPD_AUDIT_LOG) {
        state = &loginfo.log_audit_state;
    } else if (sourcelog == SLAPD_AUDITFAIL_LOG) {
        state = &loginfo.log_auditfail_state;
    } else {
        /* How did we even get here! */
        return 1;
    }

    if (lbackend & LOGGING_BACKEND_INTERNAL) {
        retval = slapd_log_audit_internal(buffer, buf_len, state);
    }

    if (retval != LDAP_SUCCESS) {
        return retval;
    }
    if (lbackend & LOGGING_BACKEND_SYSLOG) {
        /* This returns void, so we hope it worked */
        syslog(LOG_NOTICE, "%s", buffer);
    }
#ifdef HAVE_JOURNALD
    if (lbackend & LOGGING_BACKEND_JOURNALD) {
        retval = sd_journal_print(LOG_NOTICE, "%s", buffer);
    }
#endif
    return retval;
}

int
slapd_log_audit_internal(
    char *buffer,
    int buf_len,
    int *state)
{
    if ((*state & LOGGING_ENABLED) && (loginfo.log_audit_file != NULL)) {
        LOG_AUDIT_LOCK_WRITE();
        if (log__needrotation(loginfo.log_audit_fdes,
                              SLAPD_AUDIT_LOG) == LOG_ROTATE) {
            if (log__open_auditlogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "slapd_log_audit_internal",
                              "Unable to open audit file:%s\n", loginfo.log_audit_file);
                LOG_AUDIT_UNLOCK_WRITE();
                return 0;
            }
            while (loginfo.log_audit_rotationsyncclock <= loginfo.log_audit_ctime) {
                loginfo.log_audit_rotationsyncclock += PR_ABS(loginfo.log_audit_rotationtime_secs);
            }
        }
        if (*state & LOGGING_NEED_TITLE) {
            log_write_title(loginfo.log_audit_fdes);
            *state &= ~LOGGING_NEED_TITLE;
        }
        LOG_WRITE_NOW_NO_ERR(loginfo.log_audit_fdes, buffer, buf_len, 0);
        LOG_AUDIT_UNLOCK_WRITE();
        return 0;
    }
    return 0;
}
/******************************************************************************
* write in the audit fail log
******************************************************************************/
int
slapd_log_auditfail(
    char *buffer,
    int buf_len)
{
    /* We use this to route audit log entries to where they need to go */
    int retval = LDAP_SUCCESS;
    int lbackend = loginfo.log_backend; /* We copy this to make these next checks atomic */
    if (lbackend & LOGGING_BACKEND_INTERNAL) {
        retval = slapd_log_auditfail_internal(buffer, buf_len);
    }
    if (retval != LDAP_SUCCESS) {
        return retval;
    }
    if (lbackend & LOGGING_BACKEND_SYSLOG) {
        /* This returns void, so we hope it worked */
        syslog(LOG_NOTICE, "%s", buffer);
    }
#ifdef HAVE_JOURNALD
    if (lbackend & LOGGING_BACKEND_JOURNALD) {
        retval = sd_journal_print(LOG_NOTICE, "%s", buffer);
    }
#endif
    return retval;
}

int
slapd_log_auditfail_internal(
    char *buffer,
    int buf_len)
{
    if ((loginfo.log_auditfail_state & LOGGING_ENABLED) && (loginfo.log_auditfail_file != NULL)) {
        LOG_AUDITFAIL_LOCK_WRITE();
        if (log__needrotation(loginfo.log_auditfail_fdes,
                              SLAPD_AUDITFAIL_LOG) == LOG_ROTATE) {
            if (log__open_auditfaillogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "slapd_log_auditfail_internal",
                              "Unable to open auditfail file:%s\n", loginfo.log_auditfail_file);
                LOG_AUDITFAIL_UNLOCK_WRITE();
                return 0;
            }
            while (loginfo.log_auditfail_rotationsyncclock <= loginfo.log_auditfail_ctime) {
                loginfo.log_auditfail_rotationsyncclock += PR_ABS(loginfo.log_auditfail_rotationtime_secs);
            }
        }
        if (loginfo.log_auditfail_state & LOGGING_NEED_TITLE) {
            log_write_title(loginfo.log_auditfail_fdes);
            loginfo.log_auditfail_state &= ~LOGGING_NEED_TITLE;
        }
        LOG_WRITE_NOW_NO_ERR(loginfo.log_auditfail_fdes, buffer, buf_len, 0);
        LOG_AUDITFAIL_UNLOCK_WRITE();
        return 0;
    }
    return 0;
}
/******************************************************************************
* write in the error log
******************************************************************************/
int
slapd_log_error_proc(
    int sev_level,
    const char *subsystem, /* omitted if NULL */
    const char *fmt,
    ...)
{
    int rc = LDAP_SUCCESS;
    va_list ap_err;
    va_list ap_file;

    if (loginfo.log_backend & LOGGING_BACKEND_INTERNAL) {
        va_start(ap_err, fmt);
        va_start(ap_file, fmt);
        rc = slapd_log_error_proc_internal(sev_level, subsystem, fmt, ap_err, ap_file);
        va_end(ap_file);
        va_end(ap_err);
    }
    if (rc != LDAP_SUCCESS) {
        return (rc);
    }
    if (loginfo.log_backend & LOGGING_BACKEND_SYSLOG) {
        va_start(ap_err, fmt);
        /* va_start( ap_file, fmt ); */
        /* This returns void, so we hope it worked */
        vsyslog(sev_level, fmt, ap_err);
        /* vsyslog(LOG_ERROR, fmt, ap_file); */
        /* va_end(ap_file); */
        va_end(ap_err);
    }
#ifdef HAVE_JOURNALD
    if (loginfo.log_backend & LOGGING_BACKEND_JOURNALD) {
        va_start(ap_err, fmt);
        /* va_start( ap_file, fmt ); */
        /* This isn't handling RC nicely ... */
        rc = sd_journal_printv(sev_level, fmt, ap_err);
        /* rc = sd_journal_printv(LOG_ERROR, fmt, ap_file); */
        /* va_end(ap_file); */
        va_end(ap_err);
    }
#endif
    return rc;
}

static int
slapd_log_error_proc_internal(
    int sev_level,
    const char *subsystem, /* omitted if NULL */
    const char *fmt,
    va_list ap_err,
    va_list ap_file)
{
    int rc = LDAP_SUCCESS;

    if ((loginfo.log_error_state & LOGGING_ENABLED) && (loginfo.log_error_file != NULL)) {
        LOG_ERROR_LOCK_WRITE();
        if (log__needrotation(loginfo.log_error_fdes,
                              SLAPD_ERROR_LOG) == LOG_ROTATE) {
            if (log__open_errorlogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
                LOG_ERROR_UNLOCK_WRITE();
                /* shouldn't continue. error is syslog'ed in open_errorlogfile */
                g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
                return 0;
            }
            while (loginfo.log_error_rotationsyncclock <= loginfo.log_error_ctime) {
                loginfo.log_error_rotationsyncclock += PR_ABS(loginfo.log_error_rotationtime_secs);
            }
        }

        if (!(detached)) {
            rc = vslapd_log_error(NULL, sev_level, subsystem, fmt, ap_err, 1);
        }
        if (loginfo.log_error_fdes != NULL) {
            if (loginfo.log_error_state & LOGGING_NEED_TITLE) {
                log_write_title(loginfo.log_error_fdes);
                loginfo.log_error_state &= ~LOGGING_NEED_TITLE;
            }
            rc = vslapd_log_error(loginfo.log_error_fdes, sev_level, subsystem, fmt, ap_file, 1);
        }
        LOG_ERROR_UNLOCK_WRITE();
    } else {
        /* log the problem in the stderr */
        rc = vslapd_log_error(NULL, sev_level, subsystem, fmt, ap_err, 0);
    }
    return (rc);
}

/*
 *  Directly write the already formatted message to the error log
 */
static void
vslapd_log_emergency_error(LOGFD fp, const char *msg, int locked)
{
    char tbuf[TBUFSIZE];
    char buffer[SLAPI_LOG_BUFSIZ];
    int size = TBUFSIZE;

#ifdef HAVE_CLOCK_GETTIME
    if (logging_hr_timestamps_enabled == 1) {
        struct timespec tsnow;
        if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
            syslog(LOG_EMERG, "vslapd_log_emergency_error, Unable to determine system time for message :: %s\n", msg);
            return;
        }
        if (format_localTime_hr_log(tsnow.tv_sec, tsnow.tv_nsec, sizeof(tbuf), tbuf, &size) != 0) {
            syslog(LOG_EMERG, "vslapd_log_emergency_error, Unable to format system time for message :: %s\n", msg);
            return;
        }
    } else {
#endif
        time_t tnl;
        tnl = slapi_current_utc_time();
        if (format_localTime_log(tnl, sizeof(tbuf), tbuf, &size) != 0) {
            syslog(LOG_EMERG, "vslapd_log_emergency_error, Unable to format system time for message :: %s\n", msg);
            return;
        }
#ifdef HAVE_CLOCK_GETTIME
    }
#endif

    PR_snprintf(buffer, sizeof(buffer), "%s- EMERG - %s\n", tbuf, msg);
    size = strlen(buffer);

    if (!locked) {
        LOG_ERROR_LOCK_WRITE();
    }

    slapi_write_buffer((fp), (buffer), (size));
    PR_Sync(fp);

    if (!locked) {
        LOG_ERROR_UNLOCK_WRITE();
    }
}

static void
strToUpper(char *str, char *upper)
{
    while (*str != '\0') {
        *upper = toupper(*str);
        upper++;
        str++;
    }
}

static char *
get_log_sev_name(int loglevel, char *sev_name)
{
    int i;
    int sev_level = get_syslog_loglevel(loglevel);

    for (i = 0; prioritynames[i].c_val != -1; i++) {
        if (prioritynames[i].c_val == sev_level) {
            memset(sev_name, '\0', 10);
            strToUpper(prioritynames[i].c_name, sev_name);
            return sev_name;
        }
    }
    return "";
}

static int
vslapd_log_error(
    LOGFD fp,
    int sev_level,
    const char *subsystem, /* omitted if NULL */
    const char *fmt,
    va_list ap,
    int locked)
{
    char buffer[SLAPI_LOG_BUFSIZ];
    char sev_name[10];
    int blen = TBUFSIZE;
    char *vbuf = NULL;
    int header_len = 0;
    int err = 0;

    if (vasprintf(&vbuf, fmt, ap) == -1) {
        log__error_emergency("vslapd_log_error, Unable to format message", 1, locked);
        return -1;
    }

#ifdef HAVE_CLOCK_GETTIME
    if (logging_hr_timestamps_enabled == 1) {
        struct timespec tsnow;
        if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
            PR_snprintf(buffer, sizeof(buffer), "vslapd_log_error, Unable to determine system time for message :: %s", vbuf);
            log__error_emergency(buffer, 1, locked);
            return -1;
        }
        if (format_localTime_hr_log(tsnow.tv_sec, tsnow.tv_nsec, sizeof(buffer), buffer, &blen) != 0) {
            /* MSG may be truncated */
            PR_snprintf(buffer, sizeof(buffer), "vslapd_log_error, Unable to format system time for message :: %s", vbuf);
            log__error_emergency(buffer, 1, locked);
            return -1;
        }
    } else {
#endif
        time_t tnl;
        tnl = slapi_current_utc_time();
        if (format_localTime_log(tnl, sizeof(buffer), buffer, &blen) != 0) {
            PR_snprintf(buffer, sizeof(buffer), "vslapd_log_error, Unable to format system time for message :: %s", vbuf);
            log__error_emergency(buffer, 1, locked);
            return -1;
        }
#ifdef HAVE_CLOCK_GETTIME
    }
#endif

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

    /* Due to the change to use format_localTime_log, this is now blen */
    header_len = blen;

    /* blen = strlen(buffer); */
    /* This truncates again .... But we have the nice smprintf above! */
    if (subsystem == NULL) {
        snprintf(buffer + blen, sizeof(buffer) - blen, "- %s - %s",
                 get_log_sev_name(sev_level, sev_name), vbuf);
    } else {
        snprintf(buffer + blen, sizeof(buffer) - blen, "- %s - %s - %s",
                 get_log_sev_name(sev_level, sev_name), subsystem, vbuf);
    }

    buffer[sizeof(buffer) - 1] = '\0';

    if (fp)
        do {
            int size = strlen(buffer);
            (err) = 0;
            if (slapi_write_buffer((fp), (buffer), (size)) != (size)) {
                PRErrorCode prerr = PR_GetError();
                syslog(LOG_ERR, "Failed to write log, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s): %s\n", prerr, slapd_pr_strerror(prerr), (buffer) + (header_len));
                (err) = prerr;
            }
            /* Should be a flush in here ?? Yes because PR_SYNC doesn't work ! */
            PR_Sync(fp);
        } while (0);
        else /* stderr is always unbuffered */
            fprintf(stderr, "%s", buffer);

    if (err) {
        PR_snprintf(buffer, sizeof(buffer),
                    "Writing to the errors log failed.  Exiting...");
        log__error_emergency(buffer, 1, locked);
        /* failed to write to the errors log.  should not continue. */
        g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
    }

    slapi_ch_free_string(&vbuf);
    return (0);
}

/*
 * Log a message to the errors log
 *
 * loglevel - The logging level:  replication, plugin, etc
 * severity - LOG_ERR, LOG_WARNING, LOG_INFO, etc
 */
int
slapi_log_error(int loglevel, const char *subsystem, const char *fmt, ...)
{
    va_list ap_err;
    va_list ap_file;
    int rc = LDAP_SUCCESS;
    int lbackend = loginfo.log_backend; /* We copy this to make these next checks atomic */

    if (loglevel < SLAPI_LOG_MIN || loglevel > SLAPI_LOG_MAX) {
        (void)slapd_log_error_proc(loglevel, subsystem,
                                   "slapi_log_err: invalid log level %d (message %s)\n",
                                   loglevel, fmt);
        return (-1);
    }

    if (slapd_ldap_debug & slapi_log_map[loglevel]) {
        if (lbackend & LOGGING_BACKEND_INTERNAL) {
            va_start(ap_err, fmt);
            va_start(ap_file, fmt);
            rc = slapd_log_error_proc_internal(loglevel, subsystem, fmt, ap_err, ap_file);
            va_end(ap_file);
            va_end(ap_err);
        }
        if (rc != LDAP_SUCCESS) {
            return (rc);
        }
#ifdef DEBUG
        if (lbackend ==0) {
			/* useful for dbscan */
            va_start(ap_err, fmt);
			vfprintf(stderr, fmt, ap_err);
            va_end(ap_err);
		}
#endif
        if (lbackend & LOGGING_BACKEND_SYSLOG) {
            va_start(ap_err, fmt);
            /* va_start( ap_file, fmt ); */
            /* This returns void, so we hope it worked */
            vsyslog(get_syslog_loglevel(loglevel), fmt, ap_err);
            /* vsyslog(LOG_ERROR, fmt, ap_file); */
            /* va_end(ap_file); */
            va_end(ap_err);
        }
#ifdef HAVE_JOURNALD
        if (lbackend & LOGGING_BACKEND_JOURNALD) {
            va_start(ap_err, fmt);
            /* va_start( ap_file, fmt ); */
            /* This isn't handling RC nicely ... */
            rc = sd_journal_printv(get_syslog_loglevel(loglevel), fmt, ap_err);
            /* rc = sd_journal_printv(LOG_ERROR, fmt, ap_file); */
            /* va_end(ap_file); */
            va_end(ap_err);
        }
#endif
    } else {
        rc = LDAP_SUCCESS; /* nothing to be logged --> always return success */
    }

    return (rc);
}

int
slapi_log_error_ext(int loglevel, const char *subsystem, const char *fmt, va_list varg1, va_list varg2)
{
    int rc = 0;

    if (loglevel < SLAPI_LOG_MIN || loglevel > SLAPI_LOG_MAX) {
        (void)slapd_log_error_proc(loglevel, subsystem, "slapi_log_error: invalid severity %d (message %s)\n",
                                   loglevel, fmt);
        return (-1);
    }

    if (slapd_ldap_debug & slapi_log_map[loglevel]) {
        rc = slapd_log_error_proc_internal(loglevel, subsystem, fmt, varg1, varg2);
    } else {
        rc = 0; /* nothing to be logged --> always return success */
    }

    return (rc);
}

int
slapi_is_loglevel_set(const int loglevel)
{
    return (slapd_ldap_debug & slapi_log_map[loglevel] ? 1 : 0);
}

/*
 * Log current thread stack backtrace to the errors log.
 *
 * loglevel - The logging level:  replication, plugin, etc
 */
void
slapi_log_backtrace(int loglevel)
{
    if (slapi_is_loglevel_set(loglevel)) {
        void *frames[100];
        int nbframes = backtrace(frames, (sizeof frames)/sizeof frames[0]);
        char **symbols = backtrace_symbols(frames, nbframes);
        if (symbols) {
            /* Logs 1 line per frames to avoid risking log message truncation */
            for (size_t i=0; i<nbframes; i++) {
               slapi_log_err(loglevel, "slapi_log_backtrace", "\t[%ld]\t%s\n", i, symbols[i]);
            }
            free(symbols);
        }
    }
}


/******************************************************************************
* write in the access log
******************************************************************************/
static int
vslapd_log_access(const char *fmt, va_list ap)
{
    char buffer[SLAPI_LOG_BUFSIZ];
    char vbuf[SLAPI_LOG_BUFSIZ];
    int32_t blen = TBUFSIZE;
    int32_t vlen;
    int32_t rc = LDAP_SUCCESS;
    time_t tnl;

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, vslapd_log_access__entry);
#endif

    /* We do this sooner, because that we we can use the message in other calls */
    if ((vlen = vsnprintf(vbuf, SLAPI_LOG_BUFSIZ, fmt, ap)) == -1) {
        log__error_emergency("vslapd_log_access, Unable to format message", 1, 0);
        return -1;
    }

#ifdef HAVE_CLOCK_GETTIME
    if (logging_hr_timestamps_enabled == 1) {
        struct timespec tsnow;
        if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
            /* Make an error */
            PR_snprintf(buffer, sizeof(buffer), "vslapd_log_access, Unable to determine system time for message :: %s", vbuf);
            log__error_emergency(buffer, 1, 0);
            return -1;
        }
        tnl = tsnow.tv_sec;
        if (format_localTime_hr_log(tsnow.tv_sec, tsnow.tv_nsec, sizeof(buffer), buffer, &blen) != 0) {
            /* MSG may be truncated */
            PR_snprintf(buffer, sizeof(buffer), "vslapd_log_access, Unable to format system time for message :: %s", vbuf);
            log__error_emergency(buffer, 1, 0);
            return -1;
        }
    } else {
#endif
        tnl = slapi_current_utc_time();
        if (format_localTime_log(tnl, sizeof(buffer), buffer, &blen) != 0) {
            /* MSG may be truncated */
            PR_snprintf(buffer, sizeof(buffer), "vslapd_log_access, Unable to format system time for message :: %s", vbuf);
            log__error_emergency(buffer, 1, 0);
            return -1;
        }
#ifdef HAVE_CLOCK_GETTIME
    }
#endif

    if (SLAPI_LOG_BUFSIZ - blen < vlen) {
        /* We won't be able to fit the message in! Uh-oh! */
        /* If the issue is not resolved during the fmt string creation (see op_shared_search()),
         * we truncate the line and still log the message allowing the admin to check if
         * someone is trying to do something bad. */
        vlen = strlen(vbuf);                 /* Truncated length */
        memcpy(&vbuf[vlen-4], "...\n", 4);   /* Replace last characters with three dots and a new line character */
        slapi_log_err(SLAPI_LOG_ERR, "vslapd_log_access", "Insufficient buffer capacity to fit timestamp and message! The line in the access log was truncated\n");
        rc = -1;
    }

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, vslapd_log_access__prepared);
#endif

    log_append_buffer2(tnl, loginfo.log_access_buffer, buffer, blen, vbuf, vlen);

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, vslapd_log_access__buffer);
#endif

    return (rc);
}
int
slapi_log_stat(int loglevel, const char *fmt, ...)
{
    char buf[2048];
    va_list args;
    int rc = LDAP_SUCCESS;

    if (loglevel & loginfo.log_access_stat_level) {
            va_start(args, fmt);
            PR_vsnprintf(buf, sizeof(buf), fmt, args);
            rc = slapi_log_access(LDAP_DEBUG_STATS, "%s", buf);
            va_end(args);
    }
    return rc;
}
int
slapi_log_access(int level,
                 const char *fmt,
                 ...)
{
    va_list ap;
    int rc = 0;
    int lbackend = loginfo.log_backend; /* We copy this to make these next checks atomic */

    if (!(loginfo.log_access_state & LOGGING_ENABLED)) {
        return 0;
    }

    if ((level & loginfo.log_access_level) &&
        (loginfo.log_access_fdes != NULL) && (loginfo.log_access_file != NULL)) {
        /* How do we handle the RC?
         *
         * What we do is we log to the "best" backend first going down.
         * "best" meaning most reliable.
         * As we descend, if we encounter an issue, we bail before the "lesser"
         * backends.
         */
        if (lbackend & LOGGING_BACKEND_INTERNAL) {
            va_start(ap, fmt);
            rc = vslapd_log_access(fmt, ap);
            va_end(ap);
        }
        if (rc != LDAP_SUCCESS) {
            return rc;
        }
        if (lbackend & LOGGING_BACKEND_SYSLOG) {
            va_start(ap, fmt);
            /* This returns void, so we hope it worked */
            vsyslog(LOG_INFO, fmt, ap);
            va_end(ap);
        }
#ifdef HAVE_JOURNALD
        if (lbackend & LOGGING_BACKEND_JOURNALD) {
            va_start(ap, fmt);
            rc = sd_journal_printv(LOG_INFO, fmt, ap);
            va_end(ap);
        }
#endif
    }
    return (rc);
}

/******************************************************************************
* access_log_openf
*
*     Open the access log file
*
*  Returns:
*    0 -- success
*       1 -- fail
******************************************************************************/
int
access_log_openf(char *pathname, int locked)
{
    int rv = 0;
    int logfile_type = 0;

    if (!locked)
        LOG_ACCESS_LOCK_WRITE();

    /* store the path name */
    slapi_ch_free_string(&loginfo.log_access_file);
    loginfo.log_access_file = slapi_ch_strdup(pathname);

    /* store the rotation info fiel path name */
    slapi_ch_free_string(&loginfo.log_accessinfo_file);
    loginfo.log_accessinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

    /*
    ** Check if we have a log file already. If we have it then
    ** we need to parse the header info and update the loginfo
    ** struct.
    */
    logfile_type = log__access_rotationinfof(loginfo.log_accessinfo_file);

    if (log__open_accesslogfile(logfile_type, 1 /* got lock*/) != LOG_SUCCESS) {
        rv = 1;
    }

    if (!locked)
        LOG_ACCESS_UNLOCK_WRITE();


    return rv;
}

/******************************************************************************
* log__open_accesslogfile
*
*    Open a new log file. If we have run out of the max logs we can have
*    then delete the oldest file.
******************************************************************************/
static int
log__open_accesslogfile(int logfile_state, int locked)
{

    time_t now;
    LOGFD fp;
    LOGFD fpinfo = NULL;
    char tbuf[TBUFSIZE];
    struct logfileinfo *logp;
    char buffer[BUFSIZ];

    if (!locked)
        LOG_ACCESS_LOCK_WRITE();

    /*
    ** Here we are trying to create a new log file.
    ** If we alredy have one, then we need to rename it as
    ** "filename.time",  close it and update it's information
    ** in the array stack.
    */
    if (loginfo.log_access_fdes != NULL) {
        struct logfileinfo *log;
        char newfile[BUFSIZ];
        PRInt64 f_size;

        /* get rid of the old one */
        if ((f_size = log__getfilesize(loginfo.log_access_fdes)) == -1) {
            /* Then assume that we have the max size (in bytes) */
            f_size = loginfo.log_access_maxlogsize;
        }

        /* Check if I have to delete any old file, delete it if it is required.
        ** If there is just one file, then  access and access.rotation files
        ** are deleted. After that we start fresh
        */
        while (log__delete_access_logfile())
            ;

        /* close the file */
        LOG_CLOSE(loginfo.log_access_fdes);
        /*
         * loginfo.log_access_fdes is not set to NULL here, otherwise
         * slapi_log_access() will not send a message to the access log
         * if it is called between this point and where this field is
         * set again after calling LOG_OPEN_APPEND.
         */
        if (loginfo.log_access_maxnumlogs > 1) {
            log = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            log->l_ctime = loginfo.log_access_ctime;
            log->l_size = f_size;
            log->l_compressed = PR_FALSE;
            log_convert_time(log->l_ctime, tbuf, 1 /*short */);
            PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_access_file, tbuf);
            if (PR_Rename(loginfo.log_access_file, newfile) != PR_SUCCESS) {
                PRErrorCode prerr = PR_GetError();
                /* Make "FILE EXISTS" error an exception.
                   Even if PR_Rename fails with the error, we continue logging.
                 */
                if (PR_FILE_EXISTS_ERROR != prerr) {
                    loginfo.log_access_fdes = NULL;
                    if (!locked)
                        LOG_ACCESS_UNLOCK_WRITE();
                    slapi_ch_free((void **)&log);
                    return LOG_UNABLE_TO_OPENFILE;
                }
            } else if (loginfo.log_access_compress) {
                if (compress_log_file(newfile) != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "log__open_auditfaillogfile",
                            "failed to compress rotated access log (%s)\n",
                            newfile);
                } else {
                    log->l_compressed = PR_TRUE;
                }
            }
            /* add the log to the chain */
            log->l_next = loginfo.log_access_logchain;
            loginfo.log_access_logchain = log;
            loginfo.log_numof_access_logs++;
        }
    }

    /* open a new log file */
    if (!LOG_OPEN_APPEND(fp, loginfo.log_access_file, loginfo.log_access_mode)) {
        int oserr = errno;
        loginfo.log_access_fdes = NULL;
        if (!locked)
            LOG_ACCESS_UNLOCK_WRITE();
        slapi_log_err(SLAPI_LOG_ERR, "log__open_accesslogfile", "Access file open %s failed errno %d (%s)\n",
                      loginfo.log_access_file, oserr, slapd_system_strerror(oserr));
        return LOG_UNABLE_TO_OPENFILE;
    }

    loginfo.log_access_fdes = fp;
    if (logfile_state == LOGFILE_REOPENED) {
        /* we have all the information */
        if (!locked)
            LOG_ACCESS_UNLOCK_WRITE();
        return LOG_SUCCESS;
    }

    loginfo.log_access_state |= LOGGING_NEED_TITLE;

    if (!LOG_OPEN_WRITE(fpinfo, loginfo.log_accessinfo_file, loginfo.log_access_mode)) {
        int oserr = errno;
        if (!locked)
            LOG_ACCESS_UNLOCK_WRITE();
        slapi_log_err(SLAPI_LOG_ERR, "log__open_accesslogfile", "accessinfo file open %s failed errno %d (%s)\n",
                      loginfo.log_accessinfo_file,
                      oserr, slapd_system_strerror(oserr));
        return LOG_UNABLE_TO_OPENFILE;
    }


    /* write the header in the log */
    now = slapi_current_utc_time();
    log_convert_time(now, tbuf, 2 /* long */);
    PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
    LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

    logp = loginfo.log_access_logchain;
    while (logp) {
        log_convert_time(logp->l_ctime, tbuf, 1 /* short */);
        if (logp->l_compressed) {
            char logfile[BUFSIZ] = {0};

            /* reset tbuf to include .gz extension */
            PR_snprintf(tbuf, sizeof(tbuf), "%s.gz", tbuf);

            /* get and set the size of the new gziped file */
            PR_snprintf(logfile, sizeof(tbuf), "%s.%s", loginfo.log_access_file, tbuf);
            if ((logp->l_size = log__getfilesize_with_filename(logfile)) == -1) {
                /* Then assume that we have the max size */
                logp->l_size = loginfo.log_access_maxlogsize;
            }
        }
        PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%" PRId64 ")\n", PREVLOGFILE, loginfo.log_access_file, tbuf,
                    logp->l_ctime, logp->l_size);
        LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
        logp = logp->l_next;
    }
    /* Close the info file. We need only when we need to rotate to the
    ** next log file.
    */
    if (fpinfo)
        LOG_CLOSE(fpinfo);

    /* This is now the current access log */
    loginfo.log_access_ctime = now;

    if (!locked)
        LOG_ACCESS_UNLOCK_WRITE();
    return LOG_SUCCESS;
}

/******************************************************************************
* log__open_securitylogfile
*
*    Open a new log file. If we have run out of the max logs we can have
*    then delete the oldest file.
******************************************************************************/
static int
log__open_securitylogfile(int logfile_state, int locked)
{

    time_t now;
    LOGFD fp;
    LOGFD fpinfo = NULL;
    char tbuf[TBUFSIZE];
    struct logfileinfo *logp;
    char buffer[BUFSIZ];

    if (!locked)
        LOG_SECURITY_LOCK_WRITE();

    /*
    ** Here we are trying to create a new log file.
    ** If we alredy have one, then we need to rename it as
    ** "filename.time",  close it and update it's information
    ** in the array stack.
    */
    if (loginfo.log_security_fdes != NULL) {
        struct logfileinfo *log;
        char newfile[BUFSIZ];
        PRInt64 f_size;

        /* get rid of the old one */
        if ((f_size = log__getfilesize(loginfo.log_security_fdes)) == -1) {
            /* Then assume that we have the max size (in bytes) */
            f_size = loginfo.log_security_maxlogsize;
        }

        /* Check if I have to delete any old file, delete it if it is required.
        ** If there is just one file, then security and security.rotation files
        ** are deleted. After that we start fresh
        */
        while (log__delete_security_logfile())
            ;

        /* close the file */
        LOG_CLOSE(loginfo.log_security_fdes);
        /*
         * loginfo.log_security_fdes is not set to NULL here, otherwise
         * slapi_log_security() will not send a message to the security log
         * if it is called between this point and where this field is
         * set again after calling LOG_OPEN_APPEND.
         */
        if (loginfo.log_security_maxnumlogs > 1) {
            log = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            log->l_ctime = loginfo.log_security_ctime;
            log->l_size = f_size;
            log->l_compressed = PR_FALSE;
            log_convert_time(log->l_ctime, tbuf, 1 /*short */);
            PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_security_file, tbuf);
            if (PR_Rename(loginfo.log_security_file, newfile) != PR_SUCCESS) {
                PRErrorCode prerr = PR_GetError();
                /* Make "FILE EXISTS" error an exception.
                   Even if PR_Rename fails with the error, we continue logging.
                 */
                if (PR_FILE_EXISTS_ERROR != prerr) {
                    loginfo.log_security_fdes = NULL;
                    if (!locked)
                        LOG_SECURITY_UNLOCK_WRITE();
                    slapi_ch_free((void **)&log);
                    return LOG_UNABLE_TO_OPENFILE;
                }
            } else if (loginfo.log_security_compress) {
                if (compress_log_file(newfile) != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "log__open_securitylogfile",
                            "failed to compress rotated security audit log (%s)\n",
                            newfile);
                } else {
                    log->l_compressed = PR_TRUE;
                }
            }
            /* add the log to the chain */
            log->l_next = loginfo.log_security_logchain;
            loginfo.log_security_logchain = log;
            loginfo.log_numof_security_logs++;
        }
    }

    /* open a new log file */
    if (!LOG_OPEN_APPEND(fp, loginfo.log_security_file, loginfo.log_security_mode)) {
        int oserr = errno;
        loginfo.log_security_fdes = NULL;
        if (!locked)
            LOG_SECURITY_UNLOCK_WRITE();
        slapi_log_err(SLAPI_LOG_ERR, "log__open_securitylogfile", "Security Audit file open %s failed errno %d (%s)\n",
                      loginfo.log_security_file, oserr, slapd_system_strerror(oserr));
        return LOG_UNABLE_TO_OPENFILE;
    }

    loginfo.log_security_fdes = fp;
    if (logfile_state == LOGFILE_REOPENED) {
        /* we have all the information */
        if (!locked)
            LOG_SECURITY_UNLOCK_WRITE();
        return LOG_SUCCESS;
    }

    /*
     * Do not write the title for the JSON security log
     * loginfo.log_security_state |= LOGGING_NEED_TITLE;
     */

    if (!LOG_OPEN_WRITE(fpinfo, loginfo.log_securityinfo_file, loginfo.log_security_mode)) {
        int oserr = errno;
        if (!locked)
            LOG_SECURITY_UNLOCK_WRITE();
        slapi_log_err(SLAPI_LOG_ERR, "log__open_securitylogfile", "securityinfo file open %s failed errno %d (%s)\n",
                      loginfo.log_securityinfo_file,
                      oserr, slapd_system_strerror(oserr));
        return LOG_UNABLE_TO_OPENFILE;
    }


    /* write the header in the log */
    now = slapi_current_utc_time();
    log_convert_time(now, tbuf, 2 /* long */);
    PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
    LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

    logp = loginfo.log_security_logchain;
    while (logp) {
        log_convert_time(logp->l_ctime, tbuf, 1 /* short */);
        if (logp->l_compressed) {
            char logfile[BUFSIZ] = {0};

            /* reset tbuf to include .gz extension */
            PR_snprintf(tbuf, sizeof(tbuf), "%s.gz", tbuf);

            /* get and set the size of the new gziped file */
            PR_snprintf(logfile, sizeof(tbuf), "%s.%s", loginfo.log_security_file, tbuf);
            if ((logp->l_size = log__getfilesize_with_filename(logfile)) == -1) {
                /* Then assume that we have the max size */
                logp->l_size = loginfo.log_security_maxlogsize;
            }
        }
        PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%" PRId64 ")\n",
                    PREVLOGFILE, loginfo.log_security_file, tbuf,
                    logp->l_ctime, logp->l_size);
        LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
        logp = logp->l_next;
    }
    /* Close the info file. We need only when we need to rotate to the
    ** next log file.
    */
    if (fpinfo)
        LOG_CLOSE(fpinfo);

    /* This is now the current security log */
    loginfo.log_security_ctime = now;

    if (!locked)
        LOG_SECURITY_UNLOCK_WRITE();
    return LOG_SUCCESS;
}

/******************************************************************************
* log__delete_security_logfile
*
*    Do we need to delete a logfile. Find out if we need to delete the log
*    file based on expiration time, max diskspace, and minfreespace.
*    Delete the file if we need to.
*
*    Assumption: A WRITE lock has been acquired for the ACCESS
******************************************************************************/

static int
log__delete_security_logfile(void)
{

    struct logfileinfo *logp = NULL;
    struct logfileinfo *delete_logp = NULL;
    struct logfileinfo *p_delete_logp = NULL;
    struct logfileinfo *prev_logp = NULL;
    PRInt64 total_size = 0;
    time_t cur_time;
    PRInt64 f_size;
    int numoflogs = loginfo.log_numof_security_logs;
    int rv = 0;
    char *logstr;
    char buffer[BUFSIZ];
    char tbuf[TBUFSIZE];

    /* If we have only one log, then  will delete this one */
    if (loginfo.log_security_maxnumlogs == 1) {
        LOG_CLOSE(loginfo.log_security_fdes);
        loginfo.log_security_fdes = NULL;
        PR_snprintf(buffer, sizeof(buffer), "%s", loginfo.log_security_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                              "File %s already removed\n", loginfo.log_security_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                              "Unable to remove file:%s error %d (%s)\n",
                              loginfo.log_security_file, prerr, slapd_pr_strerror(prerr));
            }
        }

        /* Delete the rotation file also. */
        PR_snprintf(buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_security_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                              "File %s already removed\n", loginfo.log_security_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                              "Unable to remove file:%s.rotationinfo error %d (%s)\n",
                              loginfo.log_security_file, prerr, slapd_pr_strerror(prerr));
            }
        }
        return 0;
    }

    /* If we have already the maximum number of log files, we
    ** have to delete one any how.
    */
    if (++numoflogs > loginfo.log_security_maxnumlogs) {
        logstr = "Exceeded max number of logs allowed";
        goto delete_logfile;
    }

    /* Now check based on the maxdiskspace */
    if (loginfo.log_security_maxdiskspace > 0) {
        logp = loginfo.log_security_logchain;
        while (logp) {
            total_size += logp->l_size;
            logp = logp->l_next;
        }
        if ((f_size = log__getfilesize(loginfo.log_security_fdes)) == -1) {
            /* then just assume the max size */
            total_size += loginfo.log_security_maxlogsize;
        } else {
            total_size += f_size;
        }

        /* If we have exceeded the max disk space or we have less than the
          ** minimum, then we have to delete a file.
        */
        if (total_size >= loginfo.log_security_maxdiskspace) {
            logstr = "exceeded maximum log disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the free space */
    if (loginfo.log_security_minfreespace > 0) {
        rv = log__enough_freespace(loginfo.log_security_file);
        if (rv == 0) {
            /* Not enough free space */
            logstr = "Not enough free disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the expiration time */
    if (loginfo.log_security_exptime_secs > 0) {
        /* is the file old enough */
        time(&cur_time);
        prev_logp = logp = loginfo.log_security_logchain;
        while (logp) {
            if ((cur_time - logp->l_ctime) > loginfo.log_security_exptime_secs) {
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
        time_t oldest;

        time(&oldest);

        prev_logp = logp = loginfo.log_security_logchain;
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
        /* then we are deleting the first one */
        loginfo.log_security_logchain = delete_logp->l_next;
    } else {
        p_delete_logp->l_next = delete_logp->l_next;
    }


    /* Delete the security file */
    log_convert_time(delete_logp->l_ctime, tbuf, 1 /*short */);
    PR_snprintf(buffer, sizeof(buffer), "%s.%s", loginfo.log_security_file, tbuf);
    if (PR_Delete(buffer) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (PR_FILE_NOT_FOUND_ERROR == prerr) {
            /*
             * Log not found, perhaps log was compressed, try .gz extension
             */
            PR_snprintf(buffer, sizeof(buffer), "%s.gz", buffer);
            if (PR_Delete(buffer) != PR_SUCCESS) {
                prerr = PR_GetError();
                if (PR_FILE_NOT_FOUND_ERROR != prerr) {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                            "Unable to remove file: %s error %d (%s)\n",
                            buffer, prerr, slapd_pr_strerror(prerr));
                } else {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                            "File %s already removed\n",
                            loginfo.log_security_file);
                }
            }
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                    "Unable to remove file: %s error %d (%s)\n",
                    buffer, prerr, slapd_pr_strerror(prerr));
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "log__delete_security_logfile",
                      "Removed file:%s.%s because of (%s)\n",
                      loginfo.log_security_file, tbuf, logstr);
    }
    slapi_ch_free((void **)&delete_logp);
    loginfo.log_numof_security_logs--;

    return 1;
}

/******************************************************************************
* log__security_rotationinfof
*
*    Try to open the log file. If we have one already, then try to read the
*    header and update the information.
*
*    Assumption: Lock has been acquired already
******************************************************************************/
static int
log__security_rotationinfof(char *pathname)
{
    long f_ctime;
    PRInt64 f_size;
    int main_log = 1;
    time_t now;
    FILE *fp;
    PRBool compressed = PR_FALSE;
    int rval, logfile_type = LOGFILE_REOPENED;

    /*
    ** Okay -- I confess, we want to use NSPR calls but I want to
    ** use fgets and not use PR_Read() and implement a complicated
    ** parsing module. Since this will be called only during the startup
    ** and never aftre that, we can live by it.
    */
    if ((fp = fopen(pathname, "r")) == NULL) {
        return LOGFILE_NEW;
    }

    loginfo.log_numof_security_logs = 0;

    /*
    ** We have reopened the log security file. Now we need to read the
    ** log file info and update the values.
    */
    while ((rval = log__extract_logheader(fp, &f_ctime, &f_size, &compressed)) == LOG_CONTINUE) {
        /* first we would get the main log info */
        if (f_ctime == 0 && f_size == 0) {
            continue;
        }

        time(&now);
        if (main_log) {
            if (f_ctime > 0L) {
                loginfo.log_security_ctime = f_ctime;
            } else {
                loginfo.log_security_ctime = now;
            }
            main_log = 0;
        } else {
            struct logfileinfo *logp;

            logp = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            if (f_ctime > 0L) {
                logp->l_ctime = f_ctime;
            } else {
                logp->l_ctime = now;
            }
            if (f_size > 0) {
                logp->l_size = f_size;
            } else {
                /* make it the max log size */
                logp->l_size = loginfo.log_security_maxlogsize;
            }
            logp->l_compressed = compressed;
            logp->l_next = loginfo.log_security_logchain;
            loginfo.log_security_logchain = logp;
        }
        loginfo.log_numof_security_logs++;
    }
    if (LOG_DONE == rval)
        rval = log__check_prevlogs(fp, pathname);
    fclose(fp);

    if (LOG_ERROR == rval)
        if (LOG_SUCCESS == log__fix_rotationinfof(pathname))
            logfile_type = LOGFILE_NEW;

    /* Check if there is a rotation overdue */
    if (loginfo.log_security_rotationsync_enabled &&
        loginfo.log_security_rotationunit != LOG_UNIT_HOURS &&
        loginfo.log_security_rotationunit != LOG_UNIT_MINS &&
        loginfo.log_security_ctime < loginfo.log_security_rotationsyncclock - PR_ABS(loginfo.log_security_rotationtime_secs)) {
        loginfo.log_security_rotationsyncclock -= PR_ABS(loginfo.log_security_rotationtime_secs);
    }
    return logfile_type;
}

/******************************************************************************
* Point to a new security logdir
*
* Returns:
*    LDAP_SUCCESS -- success
*    LDAP_UNWILLING_TO_PERFORM -- when trying to open a invalid log file
*    LDAP_LOCAL_ERRO  -- some error
******************************************************************************/
int
log_update_securitylogdir(char *pathname, int apply)
{
    int rv = LDAP_SUCCESS;
    LOGFD fp;

    /* try to open the file, we may have a incorrect path */
    if (!LOG_OPEN_APPEND(fp, pathname, loginfo.log_security_mode)) {
        slapi_log_err(SLAPI_LOG_WARNING,
                "log_update_securitylogdir - Can't open file %s. errno %d (%s)\n",
                pathname, errno, slapd_system_strerror(errno));
        /* stay with the current log file */
        return LDAP_UNWILLING_TO_PERFORM;
    }
    LOG_CLOSE(fp);

    /* skip the rest if we aren't doing this for real */
    if (!apply) {
        return LDAP_SUCCESS;
    }

    /*
     * The user has changed the security log directory. That means we need to
     * start fresh.
     */
    LOG_SECURITY_LOCK_WRITE();
    if (loginfo.log_security_fdes) {
        LogFileInfo *logp, *d_logp;

        slapi_log_err(SLAPI_LOG_TRACE,
                      "LOGINFO:Closing the security log file. "
                      "Moving to a new security log file (%s)\n",
                      pathname, 0, 0);

        LOG_CLOSE(loginfo.log_security_fdes);
        loginfo.log_security_fdes = 0;
        loginfo.log_security_ctime = 0;
        logp = loginfo.log_security_logchain;
        while (logp) {
            d_logp = logp;
            logp = logp->l_next;
            slapi_ch_free((void **)&d_logp);
        }
        loginfo.log_security_logchain = NULL;
        slapi_ch_free_string(&loginfo.log_security_file);
        loginfo.log_security_file = NULL;
        loginfo.log_numof_security_logs = 1;
    }

    /* Now open the new security log file */
    if (security_log_openf(pathname, 1 /* locked */)) {
        rv = LDAP_LOCAL_ERROR; /* error Unable to use the new dir */
    }
    LOG_SECURITY_UNLOCK_WRITE();
    return rv;
}

/******************************************************************************
* security_log_openf
*
*     Open the security log file
*
*  Returns:
*    0 -- success
*    1 -- fail
******************************************************************************/
int
security_log_openf(char *pathname, int locked)
{
    int rv = 0;
    int logfile_type = 0;

    if (!locked)
        LOG_SECURITY_LOCK_WRITE();

    /* store the path name */
    slapi_ch_free_string(&loginfo.log_security_file);
    loginfo.log_security_file = slapi_ch_strdup(pathname);

    /* store the rotation info fiel path name */
    slapi_ch_free_string(&loginfo.log_securityinfo_file);
    loginfo.log_securityinfo_file = slapi_ch_smprintf("%s.rotationinfo", pathname);

    /*
     * Check if we have a log file already. If we have it then we need to parse
     * the header info and update the loginfo struct.
     */
    logfile_type = log__security_rotationinfof(loginfo.log_securityinfo_file);

    if (log__open_securitylogfile(logfile_type, 1 /* got lock*/) != LOG_SUCCESS) {
        rv = 1;
    }

    if (!locked)
        LOG_SECURITY_UNLOCK_WRITE();

    return rv;
}

static void
log_append_security_buffer(time_t tnl, LogBufferInfo *lbi, char *msg, size_t size)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *insert_point = NULL;

    /* While holding the lock, we determine if there is space in the buffer for our payload,
       and if we need to flush.
     */
    PR_Lock(lbi->lock);
    if (((lbi->current - lbi->top) + size > lbi->maxsize) ||
        (tnl >= loginfo.log_security_rotationsyncclock &&
         loginfo.log_security_rotationsync_enabled))
    {
        log_flush_buffer(lbi, SLAPD_SECURITY_LOG, 0 /* do not sync to disk */);
    }
    insert_point = lbi->current;
    lbi->current += size;
    /* Increment the copy refcount */
    slapi_atomic_incr_64(&(lbi->refcount), __ATOMIC_RELEASE);
    PR_Unlock(lbi->lock);

    /* Now we can copy without holding the lock */
    memcpy(insert_point, msg, size);

    /* Decrement the copy refcount */
    slapi_atomic_decr_64(&(lbi->refcount), __ATOMIC_RELEASE);

    /* If we are asked to sync to disk immediately, do so */
    if (!slapdFrontendConfig->securitylogbuffering) {
        PR_Lock(lbi->lock);
        log_flush_buffer(lbi, SLAPD_SECURITY_LOG, 1 /* sync to disk now */);
        PR_Unlock(lbi->lock);
    }
}

/******************************************************************************
* write in the security log
******************************************************************************/
static int
vslapd_log_security(const char *log_data)
{
    char buffer[SLAPI_LOG_BUFSIZ];
    time_t tnl;
    int32_t blen = TBUFSIZE;
    int32_t rc = LDAP_SUCCESS;

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, vslapd_log_security__entry);
#endif

    /* We do this sooner, because that we can use the message in other calls */
    if ((blen = PR_snprintf(buffer, SLAPI_LOG_BUFSIZ, "%s\n", log_data)) == -1) {
        log__error_emergency("vslapd_log_security, Unable to format message", 1, 0);
        return -1;
    }

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, vslapd_log_security__prepared);
#endif
    tnl = slapi_current_utc_time();
    log_append_security_buffer(tnl, loginfo.log_security_buffer, buffer, blen);

#ifdef SYSTEMTAP
    STAP_PROBE(ns-slapd, vslapd_log_security__buffer);
#endif

    return (rc);
}

int
slapi_log_security(Slapi_PBlock *pb, const char *event_type, const char *msg)
{
    Connection *pb_conn = NULL;
    Slapi_Operation *operation = NULL;
    Slapi_DN *bind_sdn = NULL;
    ber_tag_t method = LBER_DEFAULT;
    char method_and_mech[TBUFSIZE] = {0};
    char authz_target[TBUFSIZE] = {0};
    char utc_time[TBUFSIZE] = {0};
    char binddn[BUFSIZ] = {0};
    char *mod_dn = NULL;
    char *saslmech = NULL;
    const char *target_dn = NULL;
    char *server_ip = NULL;
    char *client_ip = NULL;
    char local_time[TBUFSIZE] = {0};
    char *msg_text = (char *)msg;
    int32_t ltlen = TBUFSIZE;
    int external_bind = 0;
    int ldap_version = 3;
    int isroot = 0;
    int rc = 0;
    uint64_t conn_id = 0;
    int32_t op_id = 0;
    json_object *log_json = NULL;

    if (!(loginfo.log_security_state & LOGGING_ENABLED) ||
        !loginfo.log_security_fdes ||
        !loginfo.log_security_file)
    {
        return 0;
    }

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &bind_sdn);  // this is not correct for mod ops
    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &target_dn);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method);
    slapi_pblock_get(pb, SLAPI_BIND_SASLMECHANISM, &saslmech);

    assert(pb_conn);
    conn_id = pb_conn->c_connid,
    op_id = operation->o_opid,
    client_ip = pb_conn->c_ipaddr;
    server_ip = pb_conn->c_serveripaddr;
    ldap_version = pb_conn->c_ldapversion;
    if (saslmech) {
        external_bind = !strcasecmp(saslmech, LDAP_SASL_EXTERNAL);
    }

    if (strcmp(event_type, SECURITY_AUTHZ_ERROR) == 0) {
        Slapi_DN *target_sdn = NULL;

        if (target_dn == NULL) {
            /* Add ops use SLAPI_TARGET_SDN */
            slapi_pblock_get(pb, SLAPI_TARGET_SDN, &target_sdn);
            if (target_sdn) {
                target_dn = slapi_sdn_get_ndn(target_sdn);
            }
        }
        if (target_dn == NULL) {
            /* Delete ops use SLAPI_DELETE_TARGET_SDN */
            slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &target_dn);
            if (target_sdn) {
                target_dn = slapi_sdn_get_ndn(target_sdn);
            }
        }
        if (target_dn == NULL) {
            /* Modrdn ops use SLAPI_MODRDN_TARGET_SDN */
            slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &target_dn);
            if (target_sdn) {
                target_dn = slapi_sdn_get_ndn(target_sdn);
            }
        }

        if (target_dn && strlen(target_dn) > 500) {
            PR_snprintf(authz_target, sizeof(authz_target), "target_dn=(%.500s...)",
                        target_dn);
        } else {
            PR_snprintf(authz_target, sizeof(authz_target), "target_dn=(%s)",
                        target_dn ? target_dn : "none");
        }
        msg_text = authz_target;
        /* For modify ops the bind DN is only found in SLAPI_CONN_DN which
         * then needs to be freed by the caller */
        slapi_pblock_get(pb, SLAPI_CONN_DN, &mod_dn);
        PR_snprintf(binddn, sizeof(binddn), "%s", mod_dn ? mod_dn : "" /* anonymous */);
        slapi_ch_free_string(&mod_dn);
    } else {
        /* For authz events we need the connection DN, but for all other events we use
         * normalized bind target */
        PR_snprintf(binddn, sizeof(binddn), "%s", slapi_sdn_get_ndn(bind_sdn));
    }

    /* Determine the bind method and mechanism */
    switch (method) {
    case LDAP_AUTH_SASL:
        if (external_bind && pb_conn->c_client_cert) {
            /* Client Certificate */
            PR_snprintf(method_and_mech, sizeof(method_and_mech), "TLSCLIENTAUTH");
        } else if (external_bind && pb_conn->c_unix_local) {
            /* LDAPI */
            PR_snprintf(method_and_mech, sizeof(method_and_mech), "LDAPI");
        } else if (saslmech) {
            if (!strcasecmp(saslmech, "GSSAPI") || !strcasecmp(saslmech, "DIGEST-MD5")) {
                /* SASL */
                PR_snprintf(method_and_mech, sizeof(method_and_mech), "SASL/%s", saslmech);
            }
        }
        break;
    default:
        /* Simple auth */
        PR_snprintf(method_and_mech, sizeof(method_and_mech), "SIMPLE");
    }

    /* Get the time */
    struct timespec tsnow;
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        /* Make an error */
        PR_snprintf(local_time, sizeof(local_time), "vslapd_log_security, Unable to determine system time");
        log__error_emergency(local_time, 1, 0);
        return -1;
    }
    if (format_localTime_hr_log(tsnow.tv_sec, tsnow.tv_nsec, sizeof(local_time), local_time, &ltlen) != 0) {
        /* MSG may be truncated */
        PR_snprintf(local_time, sizeof(local_time), "vslapd_log_security, Unable to format system time");
        log__error_emergency(local_time, 1, 0);
        return -1;
    }


    /* Truncate the bind dn if it's too long */
    if (strlen(binddn) > 512) {
        PR_snprintf(binddn, sizeof(binddn), "%.512s...", binddn);
    }
    PR_snprintf(utc_time, sizeof(utc_time), "%ld.%.09ld", tsnow.tv_sec, tsnow.tv_nsec);

    log_json = json_object_new_object();
    json_object_object_add(log_json, "date",         json_object_new_string(local_time));
    json_object_object_add(log_json, "utc_time",     json_object_new_string(utc_time));
    json_object_object_add(log_json, "event",        json_object_new_string(event_type));
    json_object_object_add(log_json, "dn",           json_object_new_string(binddn));
    json_object_object_add(log_json, "bind_method",  json_object_new_string(method_and_mech));
    json_object_object_add(log_json, "root_dn",      json_object_new_boolean(isroot));
    json_object_object_add(log_json, "client_ip",    json_object_new_string(client_ip));
    json_object_object_add(log_json, "server_ip",    json_object_new_string(server_ip));
    json_object_object_add(log_json, "ldap_version", json_object_new_int(ldap_version));
    json_object_object_add(log_json, "conn_id",      json_object_new_int64(conn_id));
    json_object_object_add(log_json, "op_id",        json_object_new_int(op_id));
    json_object_object_add(log_json, "msg",          json_object_new_string(msg_text));

    rc = vslapd_log_security(json_object_to_json_string(log_json));

    /* Done with JSON object, this will free it */
    json_object_put(log_json);

    return rc;
}

/*
 * Log a TCP event, since the pblock is not available when this happens we have
 * to rely on just the Connection struct.
 */
int
slapi_log_security_tcp(Connection *pb_conn, PRErrorCode error, const char *msg)
{
    char *server_ip = NULL;
    char *client_ip = NULL;
    char local_time[TBUFSIZE] = {0};
    int32_t ltlen = TBUFSIZE;
    char utc_time[TBUFSIZE] = {0};
    int ldap_version = 3;
    int rc = 0;
    uint64_t conn_id = 0;
    json_object *log_json = NULL;

    if (!(loginfo.log_security_state & LOGGING_ENABLED) ||
        !loginfo.log_security_fdes ||
        !loginfo.log_security_file ||
        (error != SLAPD_DISCONNECT_BAD_BER_TAG && /* We only care about B1, B2, B3 */
         error != SLAPD_DISCONNECT_BER_TOO_BIG &&
         error != SLAPD_DISCONNECT_BER_PEEK))
    {
        return 0;
    }

    conn_id = pb_conn->c_connid,
    client_ip = pb_conn->c_ipaddr;
    server_ip = pb_conn->c_serveripaddr;
    ldap_version = pb_conn->c_ldapversion;

    /* Get the time */
    struct timespec tsnow;
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        /* Make an error */
        PR_snprintf(local_time, sizeof(local_time), "vslapd_log_security, Unable to determine system time");
        log__error_emergency(local_time, 1, 0);
        return -1;
    }
    if (format_localTime_hr_log(tsnow.tv_sec, tsnow.tv_nsec, sizeof(local_time), local_time, &ltlen) != 0) {
        /* MSG may be truncated */
        PR_snprintf(local_time, sizeof(local_time), "vslapd_log_security, Unable to format system time");
        log__error_emergency(local_time, 1, 0);
        return -1;
    }

    PR_snprintf(utc_time, sizeof(utc_time), "%ld.%.09ld", tsnow.tv_sec, tsnow.tv_nsec);

    log_json = json_object_new_object();
    json_object_object_add(log_json, "date",         json_object_new_string(local_time));
    json_object_object_add(log_json, "utc_time",     json_object_new_string(utc_time));
    json_object_object_add(log_json, "event",        json_object_new_string(SECURITY_TCP_ERROR));
    json_object_object_add(log_json, "client_ip",    json_object_new_string(client_ip));
    json_object_object_add(log_json, "server_ip",    json_object_new_string(server_ip));
    json_object_object_add(log_json, "ldap_version", json_object_new_int(ldap_version));
    json_object_object_add(log_json, "conn_id",      json_object_new_int64(conn_id));
    json_object_object_add(log_json, "msg",          json_object_new_string(msg));

    rc = vslapd_log_security(json_object_to_json_string(log_json));

    /* Done with JSON object, this will free it */
    json_object_put(log_json);

    return rc;
}

/******************************************************************************
* log__needrotation
*
*    Do we need to rotate the log file ?
*    Find out based on rotation time and the max log size;
*
*  Return:
*    LOG_CONTINUE        -- Use the same one
*    LOG_ROTATE          -- log need to be rotated
*
* Note:
*    A READ LOCK is obtained.
********************************************************************************/
#define LOG_SIZE_EXCEEDED 1
#define LOG_EXPIRED 2
static int
log__needrotation(LOGFD fp, int logtype)
{
    time_t curr_time;
    time_t log_createtime = 0;
    time_t syncclock = 0;
    int type = LOG_CONTINUE;
    PRInt64 f_size = 0;
    PRInt64 maxlogsize;
    int nlogs;
    int rotationtime_secs = -1;
    int sync_enabled = 0, timeunit = 0;

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
    case SLAPD_SECURITY_LOG:
        nlogs = loginfo.log_security_maxnumlogs;
        maxlogsize = loginfo.log_security_maxlogsize;
        sync_enabled = loginfo.log_security_rotationsync_enabled;
        syncclock = loginfo.log_security_rotationsyncclock;
        timeunit = loginfo.log_security_rotationunit;
        rotationtime_secs = loginfo.log_security_rotationtime_secs;
        log_createtime = loginfo.log_security_ctime;
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
    case SLAPD_AUDITFAIL_LOG:
        nlogs = loginfo.log_auditfail_maxnumlogs;
        maxlogsize = loginfo.log_auditfail_maxlogsize;
        sync_enabled = loginfo.log_auditfail_rotationsync_enabled;
        syncclock = loginfo.log_auditfail_rotationsyncclock;
        timeunit = loginfo.log_auditfail_rotationunit;
        rotationtime_secs = loginfo.log_auditfail_rotationtime_secs;
        log_createtime = loginfo.log_auditfail_ctime;
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
    time(&curr_time);

    if (!sync_enabled || timeunit == LOG_UNIT_HOURS || timeunit == LOG_UNIT_MINS) {
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
    if (logtype != SLAPD_ERROR_LOG) {
        if (type == LOG_SIZE_EXCEEDED) {
            slapi_log_err(SLAPI_LOG_TRACE, "log__needrotation",
                          "LOGINFO:End of Log because size exceeded(Max:%" PRId64 "d bytes) (Is:%" PRId64 "d bytes)\n",
                          maxlogsize, f_size);
        } else if (type == LOG_EXPIRED) {
            slapi_log_err(SLAPI_LOG_TRACE, "log__needrotation",
                          "LOGINFO:End of Log because time exceeded(Max:%d secs) (Is:%ld secs)\n",
                          rotationtime_secs, curr_time - log_createtime);
        }
    }
    return (type == LOG_CONTINUE) ? LOG_CONTINUE : LOG_ROTATE;
}

/******************************************************************************
* log__delete_access_logfile
*
*    Do we need to delete  a logfile. Find out if we need to delete the log
*    file based on expiration time, max diskspace, and minfreespace.
*    Delete the file if we need to.
*
*    Assumption: A WRITE lock has been acquired for the ACCESS
******************************************************************************/

static int
log__delete_access_logfile(void)
{

    struct logfileinfo *logp = NULL;
    struct logfileinfo *delete_logp = NULL;
    struct logfileinfo *p_delete_logp = NULL;
    struct logfileinfo *prev_logp = NULL;
    PRInt64 total_size = 0;
    time_t cur_time;
    PRInt64 f_size;
    int numoflogs = loginfo.log_numof_access_logs;
    int rv = 0;
    char *logstr;
    char buffer[BUFSIZ];
    char tbuf[TBUFSIZE];

    /* If we have only one log, then  will delete this one */
    if (loginfo.log_access_maxnumlogs == 1) {
        LOG_CLOSE(loginfo.log_access_fdes);
        loginfo.log_access_fdes = NULL;
        PR_snprintf(buffer, sizeof(buffer), "%s", loginfo.log_access_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                              "File %s already removed\n", loginfo.log_access_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                              "Unable to remove file:%s error %d (%s)\n",
                              loginfo.log_access_file, prerr, slapd_pr_strerror(prerr));
            }
        }

        /* Delete the rotation file also. */
        PR_snprintf(buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_access_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                              "File %s already removed\n", loginfo.log_access_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                              "Unable to remove file:%s.rotationinfo error %d (%s)\n",
                              loginfo.log_access_file, prerr, slapd_pr_strerror(prerr));
            }
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
        if (total_size >= loginfo.log_access_maxdiskspace) {
            logstr = "exceeded maximum log disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the free space */
    if (loginfo.log_access_minfreespace > 0) {
        rv = log__enough_freespace(loginfo.log_access_file);
        if (rv == 0) {
            /* Not enough free space */
            logstr = "Not enough free disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the expiration time */
    if (loginfo.log_access_exptime_secs > 0) {
        /* is the file old enough */
        time(&cur_time);
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
        time_t oldest;

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
        /* then we are deleting the first one */
        loginfo.log_access_logchain = delete_logp->l_next;
    } else {
        p_delete_logp->l_next = delete_logp->l_next;
    }


    /* Delete the access file */
    log_convert_time(delete_logp->l_ctime, tbuf, 1 /*short */);
    PR_snprintf(buffer, sizeof(buffer), "%s.%s", loginfo.log_access_file, tbuf);
    if (PR_Delete(buffer) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (PR_FILE_NOT_FOUND_ERROR == prerr) {
            /*
             * Log not found, perhaps log was compressed, try .gz extension
             */
            PR_snprintf(buffer, sizeof(buffer), "%s.gz", buffer);
            if (PR_Delete(buffer) != PR_SUCCESS) {
                prerr = PR_GetError();
                if (PR_FILE_NOT_FOUND_ERROR != prerr) {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                            "Unable to remove file: %s error %d (%s)\n",
                            buffer, prerr, slapd_pr_strerror(prerr));
                } else {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                            "File %s already removed\n",
                            loginfo.log_access_file);
                }
            }
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                    "Unable to remove file: %s error %d (%s)\n",
                    buffer, prerr, slapd_pr_strerror(prerr));
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "log__delete_access_logfile",
                      "Removed file:%s.%s because of (%s)\n",
                      loginfo.log_access_file, tbuf, logstr);
    }
    slapi_ch_free((void **)&delete_logp);
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
    struct logfileinfo *prev_log = NULL;
    char buffer[BUFSIZ];
    char tbuf[TBUFSIZE];

    /*
     *  Access Log
     */
    logp = loginfo.log_access_logchain;
    while (logp) {
        tbuf[0] = buffer[0] = '\0';
        log_convert_time(logp->l_ctime, tbuf, 1);
        PR_snprintf(buffer, sizeof(buffer), "%s.%s%s", loginfo.log_access_file, tbuf, logp->l_compressed ? ".gz" : "");
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "log__delete_rotated_logs",
                          "Unable to remove file: %s - error %d (%s)\n",
                          buffer, prerr, slapd_pr_strerror(prerr));
            logp = logp->l_next;
            continue;
        }
        prev_log = logp;
        loginfo.log_numof_access_logs--;
        logp = logp->l_next;
        slapi_ch_free((void **)&prev_log);
    }

    /*
     *  Audit Log
     */
    logp = loginfo.log_audit_logchain;
    while (logp) {
        tbuf[0] = buffer[0] = '\0';
        log_convert_time(logp->l_ctime, tbuf, 1);
        PR_snprintf(buffer, sizeof(buffer), "%s.%s%s", loginfo.log_audit_file, tbuf, logp->l_compressed ? ".gz" : "");
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "log__delete_rotated_logs",
                          "Unable to remove file: %s - error %d (%s)\n",
                          buffer, prerr, slapd_pr_strerror(prerr));
            logp = logp->l_next;
            continue;
        }
        prev_log = logp;
        loginfo.log_numof_audit_logs--;
        logp = logp->l_next;
        slapi_ch_free((void **)&prev_log);
    }

    /*
     *  Audit Fail Log
     */
    logp = loginfo.log_auditfail_logchain;
    while (logp) {
        tbuf[0] = buffer[0] = '\0';
        log_convert_time(logp->l_ctime, tbuf, 1);
        PR_snprintf(buffer, sizeof(buffer), "%s.%s%s", loginfo.log_auditfail_file, tbuf, logp->l_compressed ? ".gz" : "");
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "log__delete_rotated_logs",
                          "Unable to remove file: %s - error %d (%s)\n",
                          buffer, prerr, slapd_pr_strerror(prerr));
            logp = logp->l_next;
            continue;
        }
        prev_log = logp;
        loginfo.log_numof_auditfail_logs--;
        logp = logp->l_next;
        slapi_ch_free((void **)&prev_log);
    }

    /*
     *  Error log
     */
    logp = loginfo.log_error_logchain;
    while (logp) {
        tbuf[0] = buffer[0] = '\0';
        log_convert_time(logp->l_ctime, tbuf, 1);
        PR_snprintf(buffer, sizeof(buffer), "%s.%s%s", loginfo.log_error_file, tbuf, logp->l_compressed ? ".gz" : "");
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "log__delete_rotated_logs",
                          "Unable to remove file: %s - error %d (%s)\n",
                          buffer, prerr, slapd_pr_strerror(prerr));
            logp = logp->l_next;
            continue;
        }
        prev_log = logp;
        loginfo.log_numof_error_logs--;
        logp = logp->l_next;
        slapi_ch_free((void **)&prev_log);
    }

    /* reset the log struct */
    loginfo.log_access_logchain = NULL;
    loginfo.log_audit_logchain = NULL;
    loginfo.log_auditfail_logchain = NULL;
    loginfo.log_error_logchain = NULL;
}

#define ERRORSLOG 1
#define ACCESSLOG 2
#define AUDITLOG 3

static int
log__fix_rotationinfof(char *pathname)
{
    char *logsdir = NULL;
    time_t now;
    PRDir *dirptr = NULL;
    PRDirEntry *dirent = NULL;
    PRDirFlags dirflags = PR_SKIP_BOTH & PR_SKIP_HIDDEN;
    char *log_type = NULL;
    int log_type_id;
    int rval = LOG_ERROR;
    char *p;
    char *rotated_log = NULL;
    int rotated_log_len = 0;

    /* rotation info file is broken; can't trust the contents */
    time(&now);
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
    rotated_log_len = strlen(pathname) + 20;
    rotated_log = (char *)slapi_ch_malloc(rotated_log_len);
    /* read the directory entries into a linked list */
    for (dirent = PR_ReadDir(dirptr, dirflags); dirent;
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
                   (p = strchr(dirent->name, '.')) != NULL &&
                   NULL != strchr(p, '-')) /* e.g., errors.20051123-165135 */
        {
            struct logfileinfo *logp;
            char *q;
            int ignoreit = 0;

            for (q = ++p; q && *q; q++) {
                if (*q != '-' &&
                    *q != '.' && /* .gz */
                    *q != 'g' &&
                    *q != 'z' &&
                    !isdigit(*q))
                {
                    ignoreit = 1;
                }
            }
            if (ignoreit || (q - p != 15)) {
                continue;
            }
            logp = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            logp->l_ctime = log_reverse_convert_time(p);
            logp->l_compressed = PR_FALSE;
            if (strcmp(p + strlen(p) - 3, ".gz") == 0) {
                logp->l_compressed = PR_TRUE;
            }
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
*    Try to open the log file. If we have one already, then try to read the
*    header and update the information.
*
*    Assumption: Lock has been acquired already
******************************************************************************/
static int
log__access_rotationinfof(char *pathname)
{
    long f_ctime;
    PRInt64 f_size;
    int main_log = 1;
    time_t now;
    FILE *fp;
    PRBool compressed = PR_FALSE;
    int rval, logfile_type = LOGFILE_REOPENED;

    /*
    ** Okay -- I confess, we want to use NSPR calls but I want to
    ** use fgets and not use PR_Read() and implement a complicated
    ** parsing module. Since this will be called only during the startup
    ** and never aftre that, we can live by it.
    */
    if ((fp = fopen(pathname, "r")) == NULL) {
        return LOGFILE_NEW;
    }

    loginfo.log_numof_access_logs = 0;

    /*
    ** We have reopened the log access file. Now we need to read the
    ** log file info and update the values.
    */
    while ((rval = log__extract_logheader(fp, &f_ctime, &f_size, &compressed)) == LOG_CONTINUE) {
        /* first we would get the main log info */
        if (f_ctime == 0 && f_size == 0) {
            continue;
        }

        time(&now);
        if (main_log) {
            if (f_ctime > 0L) {
                loginfo.log_access_ctime = f_ctime;
            } else {
                loginfo.log_access_ctime = now;
            }
            main_log = 0;
        } else {
            struct logfileinfo *logp;

            logp = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            if (f_ctime > 0L) {
                logp->l_ctime = f_ctime;
            } else {
                logp->l_ctime = now;
            }
            if (f_size > 0) {
                logp->l_size = f_size;
            } else {
                /* make it the max log size */
                logp->l_size = loginfo.log_access_maxlogsize;
            }
            logp->l_compressed = compressed;
            logp->l_next = loginfo.log_access_logchain;
            loginfo.log_access_logchain = logp;
        }
        loginfo.log_numof_access_logs++;
    }
    if (LOG_DONE == rval)
        rval = log__check_prevlogs(fp, pathname);
    fclose(fp);

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
log__check_prevlogs(FILE *fp, char *pathname)
{
    char buf[BUFSIZ], *p;
    char *logsdir = NULL;
    int rval = LOG_CONTINUE;
    char *log_type = NULL;
    PRDir *dirptr = NULL;
    PRDirEntry *dirent = NULL;
    PRDirFlags dirflags = PR_SKIP_BOTH & PR_SKIP_HIDDEN;

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

    for (dirent = PR_ReadDir(dirptr, dirflags); dirent;
         dirent = PR_ReadDir(dirptr, dirflags)) {
        if (0 == strncmp(log_type, dirent->name, strlen(log_type)) &&
            (p = strrchr(dirent->name, '.')) != NULL &&
            NULL != strchr(p, '-')) { /* e.g., errors.20051123-165135 */
            char *q;
            int ignoreit = 0;

            for (q = ++p; q && *q; q++) {
                if (*q != '-' &&
                    *q != '.' && /* .gz */
                    *q != 'g' &&
                    *q != 'z' &&
                    !isdigit(*q))
                {
                    ignoreit = 1;
                }
            }
            if (ignoreit || (q - p != 15))
                continue;

            fseek(fp, 0, SEEK_SET);
            buf[BUFSIZ - 1] = '\0';
            rval = LOG_ERROR; /* pessimistic default */
            while (fgets(buf, BUFSIZ - 1, fp)) {
                if (strstr(buf, dirent->name)) {
                    rval = LOG_CONTINUE; /* found in .rotationinfo */
                    break;
                }
            }
            if (LOG_ERROR == rval) {
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
*    Extract each LOGINFO header line. From there extract the time and
*    size info of all the old log files.
******************************************************************************/
static int
log__extract_logheader(FILE *fp, long *f_ctime, PRInt64 *f_size, PRBool *compressed)
{

    char buf[BUFSIZ];
    char *p, *s, *next;

    if (NULL == f_ctime || NULL == f_size) {
        return LOG_ERROR;
    }
    *f_ctime = 0L;
    *f_size = 0L;

    if (fp == NULL)
        return LOG_ERROR;

    buf[BUFSIZ - 1] = '\0'; /* for safety */
    if (fgets(buf, BUFSIZ - 1, fp) == NULL) {
        return LOG_DONE;
    }

    if ((p = strstr(buf, "LOGINFO")) == NULL) {
        return LOG_ERROR;
    }

    s = p;
    if ((p = strchr(p, '(')) == NULL) {
        return LOG_CONTINUE;
    }
    if ((next = strchr(p, ')')) == NULL) {
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

    if ((next = strchr(p, ')')) == NULL) {
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
        if (strcmp(p + strlen(p) - 3, ".gz") == 0) {
            *compressed = PR_TRUE;
        }
    }

    return LOG_CONTINUE;
}

/******************************************************************************
* log__getfilesize
*    Get the file size
*
* Assumption: Lock has been acquired already.
******************************************************************************/
/* this kinda has to be diff't on each platform :( */
/* using an int implies that all logfiles will be under 2G.  this is
 * probably a safe assumption for now.
 */
static PRInt64
log__getfilesize(LOGFD fp)
{
    PRFileInfo64 info;

    if (PR_GetOpenFileInfo64(fp, &info) == PR_FAILURE) {
        return -1;
    }
    return (PRInt64)info.size; /* type of size is PROffset64 */
}

static PRInt64
log__getfilesize_with_filename(char *filename)
{
    PRFileInfo64 info;

    if (PR_GetFileInfo64((const char *)filename, &info) == PR_FAILURE) {
        return -1;
    }
    return (PRInt64)info.size; /* type of size is PROffset64 */
}

/******************************************************************************
* log__enough_freespace
*
* Returns:
*    1    - we have enough space
*    0    - No the avialable space is less than recomended
* Assumption: Lock has been acquired already.
******************************************************************************/
static int
log__enough_freespace(char *path)
{
#ifdef LINUX
    struct statfs buf;
#else
    struct statvfs buf;
#endif /* LINUX */
    PRInt64 freeBytes;
    PRInt64 tmpval;

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
        /*        freeBytes = buf.f_bavail * buf.f_bsize; */
    }
    LL_UI2L(tmpval, loginfo.log_access_minfreespace);
    if (LL_UCMP(freeBytes, <, tmpval)) {
        /*    if (freeBytes < loginfo.log_access_minfreespace) { */
        return 0;
    }
    return 1;
}
/******************************************************************************
* log_get_loglist
*  Update the previous access files in the slapdFrontendConfig_t.
* Returns:
*    num > 1  -- how many are there
*    0        -- otherwise
******************************************************************************/
char **
log_get_loglist(int logtype)
{
    char **list = NULL;
    int num, i;
    LogFileInfo *logp = NULL;
    char buf[BUFSIZ];
    char tbuf[TBUFSIZE];
    char *file;

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        LOG_ACCESS_LOCK_READ();
        num = loginfo.log_numof_access_logs;
        logp = loginfo.log_access_logchain;
        file = loginfo.log_access_file;
        break;
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_LOCK_READ();
        num = loginfo.log_numof_security_logs;
        logp = loginfo.log_security_logchain;
        file = loginfo.log_security_file;
        break;
    case SLAPD_ERROR_LOG:
        LOG_ERROR_LOCK_READ();
        num = loginfo.log_numof_error_logs;
        logp = loginfo.log_error_logchain;
        file = loginfo.log_error_file;
        break;
    case SLAPD_AUDIT_LOG:
        LOG_AUDIT_LOCK_READ();
        num = loginfo.log_numof_audit_logs;
        logp = loginfo.log_audit_logchain;
        file = loginfo.log_audit_file;
        break;
    default:
        return NULL;
    }
    list = (char **)slapi_ch_calloc(1, (num + 1) * sizeof(char *));
    i = 0;
    while (logp) {
        log_convert_time(logp->l_ctime, tbuf, 1 /*short */);
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
    case SLAPD_SECURITY_LOG:
        LOG_SECURITY_UNLOCK_READ();
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
*    Do we need to delete  a logfile. Find out if we need to delete the log
*    file based on expiration time, max diskspace, and minfreespace.
*    Delete the file if we need to.
*
*    Assumption: A WRITE lock has been acquired for the error log.
******************************************************************************/

static int
log__delete_error_logfile(int locked)
{

    struct logfileinfo *logp = NULL;
    struct logfileinfo *delete_logp = NULL;
    struct logfileinfo *p_delete_logp = NULL;
    struct logfileinfo *prev_logp = NULL;
    PRInt64 total_size = 0;
    time_t cur_time;
    PRInt64 f_size;
    int numoflogs = loginfo.log_numof_error_logs;
    int rv = 0;
    char *logstr;
    char buffer[BUFSIZ];
    char tbuf[TBUFSIZE] = {0};

    /* If we have only one log, then  will delete this one */
    if (loginfo.log_error_maxnumlogs == 1) {
        LOG_CLOSE(loginfo.log_error_fdes);
        loginfo.log_error_fdes = NULL;
        PR_snprintf(buffer, sizeof(buffer), "%s", loginfo.log_error_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            if (!locked) {
                /* If locked, we should not call slapi_log_err, which tries to get a lock internally. */
                PRErrorCode prerr = PR_GetError();
                if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_error_logfile", "File %s already removed\n", loginfo.log_error_file);
                } else {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_error_logfile", "Unable to remove file:%s error %d (%s)\n",
                                  loginfo.log_error_file, prerr, slapd_pr_strerror(prerr));
                }
            }
        }

        /* Delete the rotation file also. */
        PR_snprintf(buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_error_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            if (!locked) {
                /* If locked, we should not call slapi_log_err, which tries to get a lock internally. */
                PRErrorCode prerr = PR_GetError();
                if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_error_logfile", "File %s already removed\n", loginfo.log_error_file);
                } else {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_error_logfile", "Unable to remove file:%s.rotationinfo error %d (%s)\n",
                                  loginfo.log_error_file, prerr, slapd_pr_strerror(prerr));
                }
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
        if (total_size >= loginfo.log_error_maxdiskspace) {
            logstr = "exceeded maximum log disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the free space */
    if (loginfo.log_error_minfreespace > 0) {
        rv = log__enough_freespace(loginfo.log_error_file);
        if (rv == 0) {
            /* Not enough free space */
            logstr = "Not enough free disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the expiration time */
    if (loginfo.log_error_exptime_secs > 0) {
        /* is the file old enough */
        time(&cur_time);
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
        time_t oldest;

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
    log_convert_time(delete_logp->l_ctime, tbuf, 1 /*short */);
    if (!locked) {
        /* if locked, we should not call slapi_log_err,
           which tries to get a lock internally. */
        slapi_log_err(SLAPI_LOG_TRACE,
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
    PR_snprintf(buffer, sizeof(buffer), "%s.%s", loginfo.log_error_file, tbuf);
    if (PR_Delete(buffer) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (PR_FILE_NOT_FOUND_ERROR != prerr) {
            PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Unable to remove file:%s.%s error %d (%s)\n",
                        loginfo.log_error_file, tbuf, prerr, slapd_pr_strerror(prerr));
            log__error_emergency(buffer, 0, locked);
        } else {
            /* Log not found, perhaps log was compressed, try .gz extension */
            PR_snprintf(buffer, sizeof(buffer), "%s.gz", buffer);
            PR_Delete(buffer);
        }
    }
    slapi_ch_free((void **)&delete_logp);
    loginfo.log_numof_error_logs--;

    return 1;
}

/******************************************************************************
* log__delete_audit_logfile
*
*    Do we need to delete  a logfile. Find out if we need to delete the log
*    file based on expiration time, max diskspace, and minfreespace.
*    Delete the file if we need to.
*
*    Assumption: A WRITE lock has been acquired for the audit
******************************************************************************/

static int
log__delete_audit_logfile(void)
{
    struct logfileinfo *logp = NULL;
    struct logfileinfo *delete_logp = NULL;
    struct logfileinfo *p_delete_logp = NULL;
    struct logfileinfo *prev_logp = NULL;
    PRInt64 total_size = 0;
    time_t cur_time;
    PRInt64 f_size;
    int numoflogs = loginfo.log_numof_audit_logs;
    int rv = 0;
    char *logstr;
    char buffer[BUFSIZ];
    char tbuf[TBUFSIZE];

    /* If we have only one log, then  will delete this one */
    if (loginfo.log_audit_maxnumlogs == 1) {
        LOG_CLOSE(loginfo.log_audit_fdes);
        loginfo.log_audit_fdes = NULL;
        PR_snprintf(buffer, sizeof(buffer), "%s", loginfo.log_audit_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile", "File %s already removed\n", loginfo.log_audit_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile", "Unable to remove file:%s error %d (%s)\n",
                              loginfo.log_audit_file, prerr, slapd_pr_strerror(prerr));
            }
        }

        /* Delete the rotation file also. */
        PR_snprintf(buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_audit_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile", "File %s already removed\n", loginfo.log_audit_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile", "Unable to remove file:%s.rotatoininfo error %d (%s)\n",
                              loginfo.log_audit_file, prerr, slapd_pr_strerror(prerr));
            }
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
        if (total_size >= loginfo.log_audit_maxdiskspace) {
            logstr = "exceeded maximum log disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the free space */
    if (loginfo.log_audit_minfreespace > 0) {
        rv = log__enough_freespace(loginfo.log_audit_file);
        if (rv == 0) {
            /* Not enough free space */
            logstr = "Not enough free disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the expiration time */
    if (loginfo.log_audit_exptime_secs > 0) {
        /* is the file old enough */
        time(&cur_time);
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
        time_t oldest;

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
    log_convert_time(delete_logp->l_ctime, tbuf, 1 /*short */);
    PR_snprintf(buffer, sizeof(buffer), "%s.%s", loginfo.log_audit_file, tbuf);
    if (PR_Delete(buffer) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (PR_FILE_NOT_FOUND_ERROR == prerr) {
            /*
             * Log not found, perhaps log was compressed, try .gz extension
             */
            PR_snprintf(buffer, sizeof(buffer), "%s.gz", buffer);
            if (PR_Delete(buffer) != PR_SUCCESS) {
                prerr = PR_GetError();
                if (PR_FILE_NOT_FOUND_ERROR != prerr) {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile",
                            "Unable to remove file: %s error %d (%s)\n",
                            buffer, prerr, slapd_pr_strerror(prerr));
                } else {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile",
                            "File %s already removed\n", loginfo.log_auditfail_file);
                }
            }
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile",
                    "Unable to remove file: %s error %d (%s)\n",
                    buffer, prerr, slapd_pr_strerror(prerr));
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "log__delete_audit_logfile",
                "Removed file:%s.%s because of (%s)\n",
                loginfo.log_audit_file, tbuf, logstr);
    }
    slapi_ch_free((void **)&delete_logp);
    loginfo.log_numof_audit_logs--;

    return 1;
}

/******************************************************************************
* log__delete_auditfail_logfile
*
*    Do we need to delete  a logfile. Find out if we need to delete the log
*    file based on expiration time, max diskspace, and minfreespace.
*    Delete the file if we need to.
*
*    Assumption: A WRITE lock has been acquired for the auditfail log
******************************************************************************/

static int
log__delete_auditfail_logfile(void)
{
    struct logfileinfo *logp = NULL;
    struct logfileinfo *delete_logp = NULL;
    struct logfileinfo *p_delete_logp = NULL;
    struct logfileinfo *prev_logp = NULL;
    PRInt64 total_size = 0;
    time_t cur_time;
    PRInt64 f_size;
    int numoflogs = loginfo.log_numof_auditfail_logs;
    int rv = 0;
    char *logstr;
    char buffer[BUFSIZ];
    char tbuf[TBUFSIZE];

    /* If we have only one log, then  will delete this one */
    if (loginfo.log_auditfail_maxnumlogs == 1) {
        LOG_CLOSE(loginfo.log_auditfail_fdes);
        loginfo.log_auditfail_fdes = NULL;
        PR_snprintf(buffer, sizeof(buffer), "%s", loginfo.log_auditfail_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile", "File %s already removed\n", loginfo.log_auditfail_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile", "Unable to remove file:%s error %d (%s)\n",
                              loginfo.log_auditfail_file, prerr, slapd_pr_strerror(prerr));
            }
        }

        /* Delete the rotation file also. */
        PR_snprintf(buffer, sizeof(buffer), "%s.rotationinfo", loginfo.log_auditfail_file);
        if (PR_Delete(buffer) != PR_SUCCESS) {
            PRErrorCode prerr = PR_GetError();
            if (PR_FILE_NOT_FOUND_ERROR == prerr) {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile", "File %s already removed\n", loginfo.log_auditfail_file);
            } else {
                slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile", "Unable to remove file:%s.rotatoininfo error %d (%s)\n",
                              loginfo.log_auditfail_file, prerr, slapd_pr_strerror(prerr));
            }
        }
        return 0;
    }

    /* If we have already the maximum number of log files, we
    ** have to delete one any how.
    */
    if (++numoflogs > loginfo.log_auditfail_maxnumlogs) {
        logstr = "Delete Error Log File: Exceeded max number of logs allowed";
        goto delete_logfile;
    }

    /* Now check based on the maxdiskspace */
    if (loginfo.log_auditfail_maxdiskspace > 0) {
        logp = loginfo.log_auditfail_logchain;
        while (logp) {
            total_size += logp->l_size;
            logp = logp->l_next;
        }
        if ((f_size = log__getfilesize(loginfo.log_auditfail_fdes)) == -1) {
            /* then just assume the max size */
            total_size += loginfo.log_auditfail_maxlogsize;
        } else {
            total_size += f_size;
        }

        /* If we have exceeded the max disk space or we have less than the
          ** minimum, then we have to delete a file.
        */
        if (total_size >= loginfo.log_auditfail_maxdiskspace) {
            logstr = "exceeded maximum log disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the free space */
    if (loginfo.log_auditfail_minfreespace > 0) {
        rv = log__enough_freespace(loginfo.log_auditfail_file);
        if (rv == 0) {
            /* Not enough free space */
            logstr = "Not enough free disk space";
            goto delete_logfile;
        }
    }

    /* Now check based on the expiration time */
    if (loginfo.log_auditfail_exptime_secs > 0) {
        /* is the file old enough */
        time(&cur_time);
        prev_logp = logp = loginfo.log_auditfail_logchain;
        while (logp) {
            if ((cur_time - logp->l_ctime) > loginfo.log_auditfail_exptime_secs) {
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
        time_t oldest;

        time(&oldest);

        prev_logp = logp = loginfo.log_auditfail_logchain;
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
        loginfo.log_auditfail_logchain = delete_logp->l_next;
    } else {
        p_delete_logp->l_next = delete_logp->l_next;
    }

    /* Delete the audit file */
    log_convert_time(delete_logp->l_ctime, tbuf, 1 /*short */);
    PR_snprintf(buffer, sizeof(buffer), "%s.%s", loginfo.log_auditfail_file, tbuf);
    if (PR_Delete(buffer) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (PR_FILE_NOT_FOUND_ERROR == prerr) {
            /*
             * Log not found, perhaps log was compressed, try .gz extension
             */
            PR_snprintf(buffer, sizeof(buffer), "%s.gz", buffer);
            if (PR_Delete(buffer) != PR_SUCCESS) {
                prerr = PR_GetError();
                if (PR_FILE_NOT_FOUND_ERROR != prerr) {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile",
                            "Unable to remove file: %s error %d (%s)\n",
                            buffer, prerr, slapd_pr_strerror(prerr));
                } else {
                    slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile",
                            "File %s already removed\n", loginfo.log_auditfail_file);
                }
            }
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile", "Unable to remove file: %s error %d (%s)\n",
                    buffer, prerr, slapd_pr_strerror(prerr));
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "log__delete_auditfail_logfile",
                "Removed file:%s.%s because of (%s)\n", loginfo.log_auditfail_file, tbuf, logstr);
    }
    slapi_ch_free((void **)&delete_logp);
    loginfo.log_numof_auditfail_logs--;

    return 1;
}

/******************************************************************************
* log__error_rotationinfof
*
*    Try to open the log file. If we have one already, then try to read the
*    header and update the information.
*
*    Assumption: Lock has been acquired already
******************************************************************************/
static int
log__error_rotationinfof(char *pathname)
{
    long f_ctime;
    PRInt64 f_size;
    int main_log = 1;
    time_t now;
    FILE *fp;
    PRBool compressed = PR_FALSE;
    int rval, logfile_type = LOGFILE_REOPENED;

    /*
    ** Okay -- I confess, we want to use NSPR calls but I want to
    ** use fgets and not use PR_Read() and implement a complicated
    ** parsing module. Since this will be called only during the startup
    ** and never after that, we can live by it.
    */

    if ((fp = fopen(pathname, "r")) == NULL) {
        return LOGFILE_NEW;
    }

    loginfo.log_numof_error_logs = 0;

    /*
    ** We have reopened the log error file. Now we need to read the
    ** log file info and update the values.
    */
    while ((rval = log__extract_logheader(fp, &f_ctime, &f_size, &compressed)) == LOG_CONTINUE) {
        /* first we would get the main log info */
        if (f_ctime == 0 && f_size == 0)
            continue;

        time(&now);
        if (main_log) {
            if (f_ctime > 0L)
                loginfo.log_error_ctime = f_ctime;
            else {
                loginfo.log_error_ctime = now;
            }
            main_log = 0;
        } else {
            struct logfileinfo *logp;

            logp = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            if (f_ctime > 0L)
                logp->l_ctime = f_ctime;
            else
                logp->l_ctime = now;
            if (f_size > 0)
                logp->l_size = f_size;
            else {
                /* make it the max log size */
                logp->l_size = loginfo.log_error_maxlogsize;
            }
            logp->l_compressed = compressed;
            logp->l_next = loginfo.log_error_logchain;
            loginfo.log_error_logchain = logp;
        }
        loginfo.log_numof_error_logs++;
    }
    if (LOG_DONE == rval)
        rval = log__check_prevlogs(fp, pathname);
    fclose(fp);

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
*    Try to open the log file. If we have one already, then try to read the
*    header and update the information.
*
*    Assumption: Lock has been acquired already
******************************************************************************/
static int
log__audit_rotationinfof(char *pathname)
{
    long f_ctime;
    PRInt64 f_size;
    int main_log = 1;
    time_t now;
    FILE *fp;
    PRBool compressed = PR_FALSE;
    int rval, logfile_type = LOGFILE_REOPENED;

    /*
    ** Okay -- I confess, we want to use NSPR calls but I want to
    ** use fgets and not use PR_Read() and implement a complicated
    ** parsing module. Since this will be called only during the startup
    ** and never aftre that, we can live by it.
    */

    if ((fp = fopen(pathname, "r")) == NULL) {
        return LOGFILE_NEW;
    }

    loginfo.log_numof_audit_logs = 0;

    /*
    ** We have reopened the log audit file. Now we need to read the
    ** log file info and update the values.
    */
    while ((rval = log__extract_logheader(fp, &f_ctime, &f_size, &compressed)) == LOG_CONTINUE) {
        /* first we would get the main log info */
        if (f_ctime == 0 && f_size == 0)
            continue;

        time(&now);
        if (main_log) {
            if (f_ctime > 0L)
                loginfo.log_audit_ctime = f_ctime;
            else {
                loginfo.log_audit_ctime = now;
            }
            main_log = 0;
        } else {
            struct logfileinfo *logp;

            logp = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            if (f_ctime > 0L)
                logp->l_ctime = f_ctime;
            else
                logp->l_ctime = now;
            if (f_size > 0)
                logp->l_size = f_size;
            else {
                /* make it the max log size */
                logp->l_size = loginfo.log_audit_maxlogsize;
            }
            logp->l_compressed = compressed;
            logp->l_next = loginfo.log_audit_logchain;
            loginfo.log_audit_logchain = logp;
        }
        loginfo.log_numof_audit_logs++;
    }
    if (LOG_DONE == rval)
        rval = log__check_prevlogs(fp, pathname);
    fclose(fp);

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

/******************************************************************************
* log__auditfail_rotationinfof
*
*    Try to open the log file. If we have one already, then try to read the
*    header and update the information.
*
*    Assumption: Lock has been acquired already
******************************************************************************/
static int
log__auditfail_rotationinfof(char *pathname)
{
    long f_ctime;
    PRInt64 f_size;
    int main_log = 1;
    time_t now;
    FILE *fp;
    PRBool compressed = PR_FALSE;
    int rval, logfile_type = LOGFILE_REOPENED;

    /*
    ** Okay -- I confess, we want to use NSPR calls but I want to
    ** use fgets and not use PR_Read() and implement a complicated
    ** parsing module. Since this will be called only during the startup
    ** and never aftre that, we can live by it.
    */

    if ((fp = fopen(pathname, "r")) == NULL) {
        return LOGFILE_NEW;
    }

    loginfo.log_numof_auditfail_logs = 0;

    /*
    ** We have reopened the log audit file. Now we need to read the
    ** log file info and update the values.
    */
    while ((rval = log__extract_logheader(fp, &f_ctime, &f_size, &compressed)) == LOG_CONTINUE) {
        /* first we would get the main log info */
        if (f_ctime == 0 && f_size == 0) {
            continue;
        }
        time(&now);
        if (main_log) {
            if (f_ctime > 0L) {
                loginfo.log_auditfail_ctime = f_ctime;
            } else {
                loginfo.log_auditfail_ctime = now;
            }
            main_log = 0;
        } else {
            struct logfileinfo *logp;

            logp = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            if (f_ctime > 0L) {
                logp->l_ctime = f_ctime;
            } else {
                logp->l_ctime = now;
            }
            if (f_size > 0) {
                logp->l_size = f_size;
            } else {
                /* make it the max log size */
                logp->l_size = loginfo.log_auditfail_maxlogsize;
            }
            logp->l_compressed = compressed;
            logp->l_next = loginfo.log_auditfail_logchain;
            loginfo.log_auditfail_logchain = logp;
        }
        loginfo.log_numof_auditfail_logs++;
    }
    if (LOG_DONE == rval) {
        rval = log__check_prevlogs(fp, pathname);
    }
    fclose(fp);

    if (LOG_ERROR == rval) {
        if (LOG_SUCCESS == log__fix_rotationinfof(pathname)) {
            logfile_type = LOGFILE_NEW;
        }
    }

    /* Check if there is a rotation overdue */
    if (loginfo.log_auditfail_rotationsync_enabled &&
        loginfo.log_auditfail_rotationunit != LOG_UNIT_HOURS &&
        loginfo.log_auditfail_rotationunit != LOG_UNIT_MINS &&
        loginfo.log_auditfail_ctime < loginfo.log_auditfail_rotationsyncclock - PR_ABS(loginfo.log_auditfail_rotationtime_secs)) {
        loginfo.log_auditfail_rotationsyncclock -= PR_ABS(loginfo.log_auditfail_rotationtime_secs);
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
    if (!locked) {
        /*
         * Take the lock because we are closing and reopening the error log (fd),
         * and we don't want any other threads trying to use this fd
         */
        LOG_ERROR_LOCK_WRITE();
    }
    if (NULL != loginfo.log_error_fdes) {
        LOG_CLOSE(loginfo.log_error_fdes);
    }
    if (!LOG_OPEN_APPEND(loginfo.log_error_fdes,
                         loginfo.log_error_file, loginfo.log_error_mode)) {
        PRErrorCode prerr = PR_GetError();
        syslog(LOG_ERR, "Failed to reopen errors log file, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n", prerr, slapd_pr_strerror(prerr));
    } else {
        vslapd_log_emergency_error(loginfo.log_error_fdes, errstr, 1 /* locked */);
    }
    if (!locked) {
        LOG_ERROR_UNLOCK_WRITE();
    }
    return;
}

/******************************************************************************
* log__open_errorlogfile
*
*    Open a new log file. If we have run out of the max logs we can have
*    then delete the oldest file.
******************************************************************************/
static int
log__open_errorlogfile(int logfile_state, int locked)
{

    time_t now;
    LOGFD fp = NULL;
    LOGFD fpinfo = NULL;
    char tbuf[TBUFSIZE];
    struct logfileinfo *logp;
    char buffer[BUFSIZ];
    struct passwd *pw = NULL;
    int rc = 0;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (slapdFrontendConfig->localuser != NULL &&
        slapdFrontendConfig->localuserinfo != NULL) {
        pw = slapdFrontendConfig->localuserinfo;
    } else {
        PR_snprintf(buffer, sizeof(buffer),
                    "Invalid nsslapd-localuser. Cannot open the errors log. Exiting...");
        log__error_emergency(buffer, 0, locked);
        return LOG_UNABLE_TO_OPENFILE;
    }

    if (!locked)
        LOG_ERROR_LOCK_WRITE();

    /*
    ** Here we are trying to create a new log file.
    ** If we alredy have one, then we need to rename it as
    ** "filename.time",  close it and update it's information
    ** in the array stack.
    */
    if (loginfo.log_error_fdes != NULL) {
        struct logfileinfo *log;
        char newfile[BUFSIZ];
        PRInt64 f_size;

        /* get rid of the old one */
        if ((f_size = log__getfilesize(loginfo.log_error_fdes)) == -1) {
            /* Then assume that we have the max size */
            f_size = loginfo.log_error_maxlogsize;
        }


        /*  Check if I have to delete any old file, delete it if it is required.*/
        while (log__delete_error_logfile(1))
            ;

        /* close the file */
        if (loginfo.log_error_fdes != NULL) {
            LOG_CLOSE(loginfo.log_error_fdes);
        }
        loginfo.log_error_fdes = NULL;

        if (loginfo.log_error_maxnumlogs > 1) {
            log = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            log->l_ctime = loginfo.log_error_ctime;
            log->l_size = f_size;

            log_convert_time(log->l_ctime, tbuf, 1 /*short */);
            PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_error_file, tbuf);
            if (PR_Rename(loginfo.log_error_file, newfile) != PR_SUCCESS) {
                PRErrorCode prerr = PR_GetError();
                /* Make "FILE EXISTS" error an exception.
                   Even if PR_Rename fails with the error, we continue logging.
                 */
                if (PR_FILE_EXISTS_ERROR != prerr) {
                    PR_snprintf(buffer, sizeof(buffer), "Failed to rename errors log file, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s). Exiting...\n", prerr, slapd_pr_strerror(prerr));
                    log__error_emergency(buffer, 1, 1);
                    slapi_ch_free((void **)&log);
                    if (!locked)
                        LOG_ERROR_UNLOCK_WRITE();
                    return LOG_UNABLE_TO_OPENFILE;
                }
            } else if (loginfo.log_error_compress) {
                if (compress_log_file(newfile) != 0) {
                    PR_snprintf(buffer, sizeof(buffer), "Failed to compress errors log file (%s)\n", newfile);
                    log__error_emergency(buffer, 1, 1);
                } else {
                    log->l_compressed = PR_TRUE;
                }
            }

            /* add the log to the chain */
            log->l_next = loginfo.log_error_logchain;
            loginfo.log_error_logchain = log;
            loginfo.log_numof_error_logs++;
        }
    }

    /* open a new log file */
    if (!LOG_OPEN_APPEND(fp, loginfo.log_error_file, loginfo.log_error_mode)) {
        PR_snprintf(buffer, sizeof(buffer),
                    "Failed to open errors log file %s: error %d (%s); Exiting...",
                    loginfo.log_error_file, errno, slapd_system_strerror(errno));
        log__error_emergency(buffer, 1, locked);
        if (!locked)
            LOG_ERROR_UNLOCK_WRITE();
        /* failed to write to the errors log.  should not continue. */
        g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        /*if I have an old log file -- I should log a message
        ** that I can't open the new file. Let the caller worry
        ** about logging message.
        */
        return LOG_UNABLE_TO_OPENFILE;
    }

    /* make sure the logfile is owned by the localuser.  If one of the
     * alternate ns-slapd modes, such as db2bak, tries to log an error
     * at startup, it will create the logfile as root!
     */
    if ((rc = slapd_chown_if_not_owner(loginfo.log_error_file, pw->pw_uid, -1)) != 0) {
        PR_snprintf(buffer, sizeof(buffer),
                    "Failed to chown log file %s: error %d (%s); Exiting...",
                    loginfo.log_error_file, errno, slapd_system_strerror(errno));
        log__error_emergency(buffer, 1, locked);
        if (!locked)
            LOG_ERROR_UNLOCK_WRITE();
        /* failed to write to the errors log.  should not continue. */
        g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        /*if I have an old log file -- I should log a message
        ** that I can't open the new file. Let the caller worry
        ** about logging message.
        */
        return LOG_UNABLE_TO_OPENFILE;
    }

    loginfo.log_error_fdes = fp;
    if (logfile_state == LOGFILE_REOPENED) {
        /* we have all the information */
        if (!locked)
            LOG_ERROR_UNLOCK_WRITE();
        return LOG_SUCCESS;
    }

    loginfo.log_error_state |= LOGGING_NEED_TITLE;

    if (!LOG_OPEN_WRITE(fpinfo, loginfo.log_errorinfo_file, loginfo.log_error_mode)) {
        PR_snprintf(buffer, sizeof(buffer),
                    "Failed to open/write to errors log file %s: error %d (%s). Exiting...",
                    loginfo.log_error_file, errno, slapd_system_strerror(errno));
        log__error_emergency(buffer, 1, locked);
        if (!locked)
            LOG_ERROR_UNLOCK_WRITE();
        return LOG_UNABLE_TO_OPENFILE;
    }

    /* write the header in the log */
    now = slapi_current_utc_time();
    log_convert_time(now, tbuf, 2 /*long */);
    PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
    LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

    logp = loginfo.log_error_logchain;
    while (logp) {
        log_convert_time(logp->l_ctime, tbuf, 1 /*short */);
        if (loginfo.log_error_compress) {
            char logfile[BUFSIZ] = {0};

            /* reset tbuf to include .gz extension */
            PR_snprintf(tbuf, sizeof(tbuf), "%s.gz", tbuf);

            /* get and set the size of the new gziped file */
            PR_snprintf(logfile, sizeof(tbuf), "%s.%s", loginfo.log_error_file, tbuf);
            if ((logp->l_size = log__getfilesize_with_filename(logfile)) == -1) {
                /* Then assume that we have the max size */
                logp->l_size = loginfo.log_error_maxlogsize;
            }
        }
        PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%" PRId64 "d)\n", PREVLOGFILE, loginfo.log_error_file, tbuf,
                    logp->l_ctime, logp->l_size);
        LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
        logp = logp->l_next;
    }
    /* Close the info file. We need only when we need to rotate to the
    ** next log file.
    */
    if (fpinfo)
        LOG_CLOSE(fpinfo);

    /* This is now the current error log */
    loginfo.log_error_ctime = now;

    if (!locked)
        LOG_ERROR_UNLOCK_WRITE();
    return LOG_SUCCESS;
}

/******************************************************************************
* log__open_auditlogfile
*
*    Open a new log file. If we have run out of the max logs we can have
*    then delete the oldest file.
******************************************************************************/
static int
log__open_auditlogfile(int logfile_state, int locked)
{

    time_t now;
    LOGFD fp;
    LOGFD fpinfo = NULL;
    char tbuf[TBUFSIZE];
    struct logfileinfo *logp;
    char buffer[BUFSIZ];

    if (!locked)
        LOG_AUDIT_LOCK_WRITE();

    /*
    ** Here we are trying to create a new log file.
    ** If we alredy have one, then we need to rename it as
    ** "filename.time",  close it and update it's information
    ** in the array stack.
    */
    if (loginfo.log_audit_fdes != NULL) {
        struct logfileinfo *log;
        char newfile[BUFSIZ];
        PRInt64 f_size;


        /* get rid of the old one */
        if ((f_size = log__getfilesize(loginfo.log_audit_fdes)) == -1) {
            /* Then assume that we have the max size */
            f_size = loginfo.log_audit_maxlogsize;
        }

        /* Check if I have to delete any old file, delete it if it is required. */
        while (log__delete_audit_logfile())
            ;

        /* close the file */
        LOG_CLOSE(loginfo.log_audit_fdes);
        loginfo.log_audit_fdes = NULL;

        if (loginfo.log_audit_maxnumlogs > 1) {
            log = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            log->l_ctime = loginfo.log_audit_ctime;
            log->l_size = f_size;

            log_convert_time(log->l_ctime, tbuf, 1 /*short */);
            PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_audit_file, tbuf);
            if (PR_Rename(loginfo.log_audit_file, newfile) != PR_SUCCESS) {
                PRErrorCode prerr = PR_GetError();
                /* Make "FILE EXISTS" error an exception.
                   Even if PR_Rename fails with the error, we continue logging.
                 */
                if (PR_FILE_EXISTS_ERROR != prerr) {
                    if (!locked)
                        LOG_AUDIT_UNLOCK_WRITE();
                    slapi_ch_free((void **)&log);
                    return LOG_UNABLE_TO_OPENFILE;
                }
            } else if (loginfo.log_audit_compress) {
                if (compress_log_file(newfile) != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "log__open_auditfaillogfile",
                            "failed to compress rotated audit log (%s)\n",
                            newfile);
                } else {
                    log->l_compressed = PR_TRUE;
                }
            }

            /* add the log to the chain */
            log->l_next = loginfo.log_audit_logchain;
            loginfo.log_audit_logchain = log;
            loginfo.log_numof_audit_logs++;
        }
    }

    /* open a new log file */
    if (!LOG_OPEN_APPEND(fp, loginfo.log_audit_file, loginfo.log_audit_mode)) {
        slapi_log_err(SLAPI_LOG_ERR, "log__open_auditlogfile",
                      "can't open file %s - errno %d (%s)\n",
                      loginfo.log_audit_file, errno, slapd_system_strerror(errno));
        if (!locked)
            LOG_AUDIT_UNLOCK_WRITE();
        /*if I have an old log file -- I should log a message
        ** that I can't open the  new file. Let the caller worry
        ** about logging message.
        */
        return LOG_UNABLE_TO_OPENFILE;
    }

    loginfo.log_audit_fdes = fp;
    if (logfile_state == LOGFILE_REOPENED) {
        /* we have all the information */
        if (!locked)
            LOG_AUDIT_UNLOCK_WRITE();
        return LOG_SUCCESS;
    }

    loginfo.log_audit_state |= LOGGING_NEED_TITLE;

    if (!LOG_OPEN_WRITE(fpinfo, loginfo.log_auditinfo_file, loginfo.log_audit_mode)) {
        slapi_log_err(SLAPI_LOG_ERR, "log__open_auditlogfile",
                      "Can't open file %s - errno %d (%s)\n",
                      loginfo.log_auditinfo_file, errno, slapd_system_strerror(errno));
        if (!locked)
            LOG_AUDIT_UNLOCK_WRITE();
        return LOG_UNABLE_TO_OPENFILE;
    }

    /* write the header in the log */
    now = slapi_current_utc_time();
    log_convert_time(now, tbuf, 2 /*long */);
    PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
    LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

    logp = loginfo.log_audit_logchain;
    while (logp) {
        log_convert_time(logp->l_ctime, tbuf, 1 /*short */);
        if (loginfo.log_audit_compress) {
            char logfile[BUFSIZ] = {0};

            /* reset tbuf to include .gz extension */
            PR_snprintf(tbuf, sizeof(tbuf), "%s.gz", tbuf);

            /* get and set the size of the new gziped file */
            PR_snprintf(logfile, sizeof(tbuf), "%s.%s", loginfo.log_audit_file, tbuf);
            if ((logp->l_size = log__getfilesize_with_filename(logfile)) == -1) {
                /* Then assume that we have the max size */
                logp->l_size = loginfo.log_audit_maxlogsize;
            }
        }
        PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%" PRId64 "d)\n", PREVLOGFILE, loginfo.log_audit_file, tbuf,
                    logp->l_ctime, logp->l_size);
        LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
        logp = logp->l_next;
    }
    /* Close the info file. We need only when we need to rotate to the
    ** next log file.
    */
    if (fpinfo)
        LOG_CLOSE(fpinfo);

    /* This is now the current audit log */
    loginfo.log_audit_ctime = now;

    if (!locked)
        LOG_AUDIT_UNLOCK_WRITE();
    return LOG_SUCCESS;
}
/******************************************************************************
* log__open_auditfaillogfile
*
*   Open a new log file. If we have run out of the max logs we can have
*   then delete the oldest file.
******************************************************************************/
static int
log__open_auditfaillogfile(int logfile_state, int locked)
{

    time_t now;
    LOGFD fp;
    LOGFD fpinfo = NULL;
    char tbuf[TBUFSIZE];
    struct logfileinfo *logp;
    char buffer[BUFSIZ];

    if (!locked)
        LOG_AUDITFAIL_LOCK_WRITE();

    /*
    ** Here we are trying to create a new log file.
    ** If we alredy have one, then we need to rename it as
    ** "filename.time",  close it and update it's information
    ** in the array stack.
    */
    if (loginfo.log_auditfail_fdes != NULL) {
        struct logfileinfo *log;
        char newfile[BUFSIZ];
        PRInt64 f_size;


        /* get rid of the old one */
        if ((f_size = log__getfilesize(loginfo.log_auditfail_fdes)) == -1) {
            /* Then assume that we have the max size */
            f_size = loginfo.log_auditfail_maxlogsize;
        }

        /* Check if I have to delete any old file, delete it if it is required. */
        while (log__delete_auditfail_logfile())
            ;

        /* close the file */
        LOG_CLOSE(loginfo.log_auditfail_fdes);
        loginfo.log_auditfail_fdes = NULL;

        if (loginfo.log_auditfail_maxnumlogs > 1) {
            log = (struct logfileinfo *)slapi_ch_malloc(sizeof(struct logfileinfo));
            log->l_ctime = loginfo.log_auditfail_ctime;
            log->l_size = f_size;

            log_convert_time(log->l_ctime, tbuf, 1 /*short */);
            PR_snprintf(newfile, sizeof(newfile), "%s.%s", loginfo.log_auditfail_file, tbuf);
            if (PR_Rename(loginfo.log_auditfail_file, newfile) != PR_SUCCESS) {
                PRErrorCode prerr = PR_GetError();
                /* Make "FILE EXISTS" error an exception.
                   Even if PR_Rename fails with the error, we continue logging.
                 */
                if (PR_FILE_EXISTS_ERROR != prerr) {
                    if (!locked)
                        LOG_AUDITFAIL_UNLOCK_WRITE();
                    slapi_ch_free((void **)&log);
                    return LOG_UNABLE_TO_OPENFILE;
                }
            } else if (loginfo.log_auditfail_compress) {
                if (compress_log_file(newfile) != 0) {
                    slapi_log_err(SLAPI_LOG_ERR, "log__open_auditfaillogfile",
                            "failed to compress rotated auditfail log (%s)\n",
                            newfile);
                } else {
                    log->l_compressed = PR_TRUE;
                }
            }

            /* add the log to the chain */
            log->l_next = loginfo.log_auditfail_logchain;
            loginfo.log_auditfail_logchain = log;
            loginfo.log_numof_auditfail_logs++;
        }
    }

    /* open a new log file */
    if (!LOG_OPEN_APPEND(fp, loginfo.log_auditfail_file, loginfo.log_auditfail_mode)) {
        slapi_log_err(SLAPI_LOG_ERR, "log__open_auditfaillogfile",
                      "Can't open file %s - errno %d (%s)\n",
                      loginfo.log_auditfail_file, errno, slapd_system_strerror(errno));
        if (!locked)
            LOG_AUDITFAIL_UNLOCK_WRITE();
        /*if I have an old log file -- I should log a message
        ** that I can't open the  new file. Let the caller worry
        ** about logging message.
        */
        return LOG_UNABLE_TO_OPENFILE;
    }

    loginfo.log_auditfail_fdes = fp;
    if (logfile_state == LOGFILE_REOPENED) {
        /* we have all the information */
        if (!locked)
            LOG_AUDITFAIL_UNLOCK_WRITE();
        return LOG_SUCCESS;
    }

    loginfo.log_auditfail_state |= LOGGING_NEED_TITLE;

    if (!LOG_OPEN_WRITE(fpinfo, loginfo.log_auditfailinfo_file, loginfo.log_auditfail_mode)) {
        slapi_log_err(SLAPI_LOG_ERR, "log__open_auditfaillogfile",
                      "Can't open file %s - errno %d (%s)\n",
                      loginfo.log_auditfailinfo_file, errno, slapd_system_strerror(errno));
        if (!locked)
            LOG_AUDITFAIL_UNLOCK_WRITE();
        return LOG_UNABLE_TO_OPENFILE;
    }

    /* write the header in the log */
    now = slapi_current_utc_time();
    log_convert_time(now, tbuf, 2 /*long */);
    PR_snprintf(buffer, sizeof(buffer), "LOGINFO:Log file created at: %s (%lu)\n", tbuf, now);
    LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);

    logp = loginfo.log_auditfail_logchain;
    while (logp) {
        log_convert_time(logp->l_ctime, tbuf, 1 /*short */);
        if (loginfo.log_auditfail_compress) {
            char logfile[BUFSIZ] = {0};

            /* reset tbuf to include .gz extension */
            PR_snprintf(tbuf, sizeof(tbuf), "%s.gz", tbuf);

            /* get and set the size of the new gziped file */
            PR_snprintf(logfile, sizeof(tbuf), "%s.%s", loginfo.log_auditfail_file, tbuf);
            if ((logp->l_size = log__getfilesize_with_filename(logfile)) == -1) {
                /* Then assume that we have the max size */
                logp->l_size = loginfo.log_auditfail_maxlogsize;
            }
        }
        PR_snprintf(buffer, sizeof(buffer), "LOGINFO:%s%s.%s (%lu) (%" PRId64 "d)\n", PREVLOGFILE, loginfo.log_auditfail_file, tbuf,
                    logp->l_ctime, logp->l_size);
        LOG_WRITE(fpinfo, buffer, strlen(buffer), 0);
        logp = logp->l_next;
    }
    /* Close the info file. We need only when we need to rotate to the
    ** next log file.
    */
    if (fpinfo)
        LOG_CLOSE(fpinfo);

    /* This is now the current audit log */
    loginfo.log_auditfail_ctime = now;

    if (!locked)
        LOG_AUDITFAIL_UNLOCK_WRITE();
    return LOG_SUCCESS;
}

/*
** Log Buffering
** only supports access log at this time
*/

static LogBufferInfo *
log_create_buffer(size_t sz)
{
    LogBufferInfo *lbi;

    lbi = (LogBufferInfo *)slapi_ch_malloc(sizeof(LogBufferInfo));
    lbi->top = (char *)slapi_ch_malloc(sz);
    lbi->current = lbi->top;
    lbi->maxsize = sz;
    slapi_atomic_store_64(&(lbi->refcount), 0, __ATOMIC_RELEASE);
    return lbi;
}

/*
 * Some notes about this function. It is written the way it is for performance
 * reasons. Tests showed that on 4 processor systems, there is significant
 * contention for the lbi->lock. This is because the lock was held for the
 * duration of the copy of the log message into the buffer. Therefore the
 * routine was re-written to avoid holding the lock for that time. Instead we
 * gain the lock, take a copy of the buffer pointer where we need to copy our
 * message, increase the size, move the current pointer beyond our portion of
 * the buffer, then increment a reference count.  Then we release the lock and
 * do the actual copy in to the reserved buffer area. We then atomically
 * decrement the reference count. The reference count is used to ensure that
 * when the buffer is flushed to the filesystem, there are no threads left
 * copying data into the buffer. The wait on zero reference count is
 * implemented in the flush routine because it is also called from
 * logs_flush(). Tests show this speeds up searches by 10% on 4-way
 * systems.
 */
static void
log_append_buffer2(time_t tnl, LogBufferInfo *lbi, char *msg1, size_t size1, char *msg2, size_t size2)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    size_t size = size1 + size2;
    char *insert_point = NULL;

    /* While holding the lock, we determine if there is space in the buffer for our payload,
       and if we need to flush.
     */
    PR_Lock(lbi->lock);
    if (((lbi->current - lbi->top) + size > lbi->maxsize) ||
        (tnl >= loginfo.log_access_rotationsyncclock &&
         loginfo.log_access_rotationsync_enabled)) {

        log_flush_buffer(lbi, SLAPD_ACCESS_LOG,
                         0 /* do not sync to disk right now */);
    }
    insert_point = lbi->current;
    lbi->current += size;
    /* Increment the copy refcount */
    slapi_atomic_incr_64(&(lbi->refcount), __ATOMIC_RELEASE);
    PR_Unlock(lbi->lock);

    /* Now we can copy without holding the lock */
    memcpy(insert_point, msg1, size1);
    memcpy(insert_point + size1, msg2, size2);

    /* Decrement the copy refcount */
    slapi_atomic_decr_64(&(lbi->refcount), __ATOMIC_RELEASE);

    /* If we are asked to sync to disk immediately, do so */
    if (!slapdFrontendConfig->accesslogbuffering) {
        PR_Lock(lbi->lock);
        log_flush_buffer(lbi, SLAPD_ACCESS_LOG, 1 /* sync to disk now */);
        PR_Unlock(lbi->lock);
    }
}


/* this function assumes the lock is already acquired */
/* if sync_now is non-zero, data is flushed to physical storage */
static void
log_flush_buffer(LogBufferInfo *lbi, int type, int sync_now)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (type == SLAPD_ACCESS_LOG) {
        /* It is only safe to flush once any other threads which are copying are finished */
        while (slapi_atomic_load_64(&(lbi->refcount), __ATOMIC_ACQUIRE) > 0) {
            /* It's ok to sleep for a while because we only flush every second or so */
            DS_Sleep(PR_MillisecondsToInterval(1));
        }

        if ((lbi->current - lbi->top) == 0)
            return;

        if (log__needrotation(loginfo.log_access_fdes,
                              SLAPD_ACCESS_LOG) == LOG_ROTATE) {
            if (log__open_accesslogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "log_flush_buffer", "Unable to open access file:%s\n",
                              loginfo.log_access_file);
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
            LOG_WRITE_NOW_NO_ERR(loginfo.log_access_fdes, lbi->top,
                                 lbi->current - lbi->top, 0);
        }
        lbi->current = lbi->top;
    } else if (type == SLAPD_SECURITY_LOG) {
        /* It is only safe to flush once any other threads which are copying are finished */
        while (slapi_atomic_load_64(&(lbi->refcount), __ATOMIC_ACQUIRE) > 0) {
            /* It's ok to sleep for a while because we only flush every second or so */
            DS_Sleep(PR_MillisecondsToInterval(1));
        }

        if ((lbi->current - lbi->top) == 0) {
            return;
        }

        if (log__needrotation(loginfo.log_security_fdes, SLAPD_SECURITY_LOG) == LOG_ROTATE) {
            if (log__open_securitylogfile(LOGFILE_NEW, 1) != LOG_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "log_flush_buffer", "Unable to open security audit file:%s\n",
                              loginfo.log_security_file);
                lbi->current = lbi->top; /* reset counter to prevent overwriting rest of lbi struct */
                return;
            }
            while (loginfo.log_security_rotationsyncclock <= loginfo.log_security_ctime) {
                loginfo.log_security_rotationsyncclock += PR_ABS(loginfo.log_security_rotationtime_secs);
            }
        }

        if (loginfo.log_security_state & LOGGING_NEED_TITLE) {
            log_write_title(loginfo.log_security_fdes);
            loginfo.log_security_state &= ~LOGGING_NEED_TITLE;
        }

        if (!sync_now && slapdFrontendConfig->securitylogbuffering) {
            LOG_WRITE(loginfo.log_security_fdes, lbi->top, lbi->current - lbi->top, 0);
        } else {
            LOG_WRITE_NOW_NO_ERR(loginfo.log_security_fdes, lbi->top,
                                 lbi->current - lbi->top, 0);
        }
        lbi->current = lbi->top;
    }
}

void
logs_flush()
{
    LOG_ACCESS_LOCK_WRITE();
    log_flush_buffer(loginfo.log_access_buffer, SLAPD_ACCESS_LOG,
                     1 /* sync to disk now */);
    LOG_ACCESS_UNLOCK_WRITE();
    LOG_SECURITY_LOCK_WRITE();
    log_flush_buffer(loginfo.log_security_buffer, SLAPD_SECURITY_LOG,
                     1 /* sync to disk now */);
    LOG_SECURITY_UNLOCK_WRITE();
}

/*
 *
 * log_convert_time
 *    returns the time converted  into the string format.
 *
 */
static void
log_convert_time(time_t ctime, char *tbuf, int type)
{
    struct tm *tmsp, tms;

    (void)localtime_r(&ctime, &tms);
    tmsp = &tms;
    if (type == 1) /* get the short form */
        (void)strftime(tbuf, (size_t)TBUFSIZE, "%Y%m%d-%H%M%S", tmsp);
    else /* wants the long form */
        (void)strftime(tbuf, (size_t)TBUFSIZE, "%d/%b/%Y:%H:%M:%S", tmsp);
}

/*
 * log_reverse_convert_time
 *    convert the given string formatted time (output from log_convert_time)
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
        if (sscanf(tbuf, "%4c%2c%2c-%2c%2c%2c", tbuf_with_sep,
                   tbuf_with_sep + 5, tbuf_with_sep + 8, tbuf_with_sep + 11,
                   tbuf_with_sep + 14, tbuf_with_sep + 17) != 6)
        {
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
check_log_max_size(char *maxdiskspace_str,
                   char *mlogsize_str __attribute__((unused)),
                   int maxdiskspace, /* in megabytes */
                   int mlogsize,     /* in megabytes */
                   char *returntext,
                   int logtype)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int rc = LDAP_SUCCESS;
    int current_mlogsize = -1;     /* in megabytes */
    int current_maxdiskspace = -1; /* in megabytes */
    PRInt64 mlogsizeB;             /* in bytes */
    PRInt64 maxdiskspaceB;         /* in bytes */

    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        current_mlogsize = slapdFrontendConfig->accesslog_maxlogsize;
        current_maxdiskspace = slapdFrontendConfig->accesslog_maxdiskspace;
        break;
    case SLAPD_SECURITY_LOG:
        current_mlogsize = slapdFrontendConfig->securitylog_maxlogsize;
        current_maxdiskspace = slapdFrontendConfig->securitylog_maxdiskspace;
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

    if (maxdiskspace == -1) {
        maxdiskspace = current_maxdiskspace;
    }

    if (maxdiskspace == -1) {
        maxdiskspaceB = -1;
    } else {
        maxdiskspaceB = (PRInt64)maxdiskspace * LOG_MB_IN_BYTES;
    }

    if (mlogsize == -1) {
        mlogsize = current_mlogsize;
    }

    if (mlogsize == -1) {
        mlogsizeB = -1;
    } else {
        mlogsizeB = (PRInt64)mlogsize * LOG_MB_IN_BYTES;
    }

    /* If maxdiskspace is negative, it is unlimited.  There is
     * no need to compate it to the logsize in this case. */
    if ((maxdiskspace >= 0) && (maxdiskspace < mlogsize)) {
        /* fail */
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "%s: maxdiskspace \"%d (MB)\" is less than max log size \"%d (MB)\"",
                    maxdiskspace_str, maxdiskspace, mlogsize);
        rc = LDAP_OPERATIONS_ERROR;
    }
    switch (logtype) {
    case SLAPD_ACCESS_LOG:
        loginfo.log_access_maxlogsize = mlogsizeB;
        loginfo.log_access_maxdiskspace = maxdiskspaceB;
        break;
    case SLAPD_SECURITY_LOG:
        loginfo.log_security_maxlogsize = mlogsizeB;
        loginfo.log_security_maxdiskspace = maxdiskspaceB;
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
/*                E    N    D                    */
/************************************************************************************/
