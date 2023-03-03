/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdint.h>
#if defined(HAVE_MNTENT_H)
#include <mntent.h>
#endif
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#define TCPLEN_T int
#ifdef NEED_FILIO
#include <sys/filio.h>
#else /* NEED_FILIO */
#include <sys/ioctl.h>
#endif /* NEED_FILIO */
/* for some reason, linux tty stuff defines CTIME */
#include <stdio.h>
#if defined(LINUX) || defined(__FreeBSD__)
#ifdef LINUX
#undef CTIME
#endif /* linux*/
#include <sys/param.h>
#include <sys/mount.h>
#else /* Linux or fbsd */
#include <sys/mnttab.h>
#endif
#include <sys/statvfs.h>
#include "slap.h"
#include "slapi-plugin.h"
#include "snmp_collator.h"
#include <private/pprio.h>
#include <ssl.h>
#include "fe.h"

#if defined(LDAP_IOCP)
#define SLAPD_WAKEUP_TIMER 250
#define SLAPD_ACCEPT_WAKEUP_TIMER 250
#else
#define SLAPD_WAKEUP_TIMER 250
#define SLAPD_ACCEPT_WAKEUP_TIMER 250
#endif

int slapd_wakeup_timer = SLAPD_WAKEUP_TIMER; /* time in ms to wakeup */
int slapd_accept_wakeup_timer = SLAPD_ACCEPT_WAKEUP_TIMER; /* time in ms to wakeup */
int slapd_ct_thread_wakeup_timer = SLAPD_WAKEUP_TIMER; /* time in ms to wakeup */
#ifdef notdef                                /* GGOODREPL */
/*
 * time in secs to do housekeeping:
 * this must be greater than slapd_wakeup_timer
 */
short slapd_housekeeping_timer = 10;
#endif /* notdef GGOODREPL */

#define FDS_SIGNAL_PIPE 0
#define FDS_PROCESS_MAX 64000

static signal_pipe *signalpipes;
static PRInt32 ct_shutdown = 0;
static PRThread *disk_thread_p = NULL;
static PRThread *accept_thread_p = NULL;
static pthread_cond_t diskmon_cvar;
static pthread_mutex_t diskmon_mutex;

void disk_monitoring_stop(void);
static void init_ct_list_threads(void);
static void ct_thread_cleanup(void);

typedef struct listener_info
{
    PRStackElem stackelem; /* must be first in struct for PRStack to work */
    int idx;               /* index of this listener in the ct->fd array */
    PRFileDesc *listenfd;  /* the listener fd */
    int secure;
    int local;
    Connection_Table *ct;    /* for listen job callback */
    struct ns_job_t *ns_job; /* the ns accept job */
} listener_info;

static size_t listeners = 0;                /* number of listener sockets */
static listener_info *listener_idxs = NULL; /* array of indexes of listener sockets in the ct->fd array */
static PRFileDesc *tls_listener = NULL; /* Stashed tls listener for get_ssl_listener_fd */

#define SLAPD_POLL_LISTEN_READY(xxflagsxx) (xxflagsxx & PR_POLL_READ)

static int get_connection_table_size(void);
#ifdef RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS
static void get_loopback_by_addr(void);
#endif

static PRFileDesc **createprlistensockets(unsigned short port,
                                          PRNetAddr **listenaddr,
                                          int secure,
                                          int local);
static const char *netaddr2string(const PRNetAddr *addr, char *addrbuf, size_t addrbuflen);
static void set_shutdown(int);
static void setup_pr_ct_firsttime_pds(Connection_Table *ct);
static PRIntn setup_pr_accept_pds(PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix, struct POLL_STRUCT **fds);
static PRIntn setup_pr_read_pds(Connection_Table *ct, int num_ct_lists);

#ifdef HPUX10
static void *catch_signals();
#endif

static int createsignalpipe(void);
static int destroysignalpipe(void);

static char *
get_pid_file(void)
{
    return (pid_file);
}

static int
accept_and_configure(int s __attribute__((unused)), PRFileDesc *pr_acceptfd, PRNetAddr *pr_netaddr, int addrlen __attribute__((unused)), int secure, int local, PRFileDesc **pr_clonefd)
{
    int ns = 0;
    PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_accept_wakeup_timer);

    (*pr_clonefd) = PR_Accept(pr_acceptfd, pr_netaddr, pr_timeout);
    if (!(*pr_clonefd)) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "accept_and_configure", "PR_Accept() failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return (SLAPD_INVALID_SOCKET);
    }
    ns = configure_pr_socket(pr_clonefd, secure, local);

    return ns;
}

/*
 * This is the shiny new re-born daemon function, without all the hair
 */
static int handle_new_connection(Connection_Table *ct, int tcps, PRFileDesc *pr_acceptfd, int secure, int local, Connection **newconn);
static void handle_pr_read_ready(Connection_Table *ct, int list_id, PRIntn num_poll);
static int clear_signal(struct POLL_STRUCT *fds, int list_id);
static void unfurl_banners(Connection_Table *ct, daemon_ports_t *ports, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix);
static int write_pid_file(void);
static int init_shutdown_detect(void);

/* Globals which are used to store the sockets between
 * calls to daemon_pre_setuid_init() and the daemon thread
 * creation. */

int
daemon_pre_setuid_init(daemon_ports_t *ports)
{
    int rc = 0;

    if (0 != ports->n_port) {
        ports->n_socket = createprlistensockets(ports->n_port,
                                                ports->n_listenaddr, 0, 0);
    }

    if (config_get_security() && (0 != ports->s_port)) {
        ports->s_socket = createprlistensockets((unsigned short)ports->s_port,
                                                ports->s_listenaddr, 1, 0);
    } else {
        ports->s_socket = SLAPD_INVALID_SOCKET;
    }

#if defined(ENABLE_LDAPI)
    /* ldapi */
    if (0 != ports->i_port) {
        ports->i_socket = createprlistensockets(1, ports->i_listenaddr, 0, 1);
    }
#endif /* ENABLE_LDAPI */

    return (rc);
}

/*
 * The time_shutdown static variable is used to signal the time thread
 * to shutdown.  We used to shut down the time thread when g_get_shutdown()
 * returned a non-zero value, but that caused the clock to stop, so to speak,
 * and all error log entries to have the same timestamp once the shutdown
 * process began.
 */
static int time_shutdown = 0;

/*
 *  Return a copy of the mount point for the specified directory
 */

#if LINUX
char *
disk_mon_get_mount_point(char *dir)
{
    struct mntent mntbuf;
    struct mntent *mnt;
    char buf[4096 + 1] = {0}; /* enough for 2 paths */
    struct stat s;
    dev_t dev_id;
    FILE *fp;

    if (stat(dir, &s) != 0) {
        return NULL;
    }

    dev_id = s.st_dev;
    if ((fp = setmntent("/proc/mounts", "r")) == NULL) {
        return NULL;
    }
    while ((mnt = getmntent_r(fp, &mntbuf, buf, 4096))) {
        if (stat(mnt->mnt_dir, &s) != 0) {
            continue;
        }
        if (s.st_dev == dev_id) {
            endmntent(fp);

            if ((strncmp(mnt->mnt_dir, "/dev", 4) == 0 && strncmp(mnt->mnt_dir, "/dev/shm", 8) != 0) ||
                strncmp(mnt->mnt_dir, "/proc", 4) == 0 ||
                strncmp(mnt->mnt_dir, "/sys", 4) == 0)
            {
                /*
                 * Ignore "mount directories" starting with /dev (except
                 * /dev/shm), /proc, /sys  For some reason these mounts are
                 * occasionally/incorrectly returned.  Only seen this at a
                 * customer site once.  When it happens it causes disk
                 * monitoring to think the server has 0 disk space left, and
                 * it abruptly/unexpectedly shuts the server down.  At this
                 * point it looks like a bug in stat(), setmntent(), or
                 * getmntent(), but there is no way to prove that since there
                 * is no way to reproduce the original issue.  For now just
                 * return NULL to be safe.
                 */
                return NULL;
            } else {
                return (slapi_ch_strdup(mnt->mnt_dir));
            }
        }
    }
    endmntent(fp);

    return NULL;
}
#elif __FreeBSD__
char *
disk_mon_get_mount_point(char *dir)
{
    struct statfs sb;
    if (statfs(dir, &sb) != 0) {
        return NULL;
    }

    return slapi_ch_strdup(sb.f_mntonname);
}
#endif

/*
 *  Get the mount point of the directory, and add it to the
 *  list.  Skip duplicate mount points.
 */
void
disk_mon_add_dir(char ***list, char *directory)
{
    char *dir = disk_mon_get_mount_point(directory);

    if (dir == NULL) {
        return;
    }

    if (!charray_inlist(*list, dir)) {
        slapi_ch_array_add(list, dir);
    } else {
        slapi_ch_free((void **)&dir);
    }
}

/*
 *  We gather all the log, txn log, config, and db directories
 */
void
disk_mon_get_dirs(char ***list)
{
    slapdFrontendConfig_t *config = getFrontendConfig();
    Slapi_Backend *be = NULL;
    char *cookie = NULL;
    char *dir = NULL;

    /* Add /var just to be safe */
#ifdef LOCALSTATEDIR
    disk_mon_add_dir(list, LOCALSTATEDIR);
#else
    disk_mon_add_dir(list, "/var");
#endif

    /* config and backend directories */
    CFG_LOCK_READ(config);
    disk_mon_add_dir(list, config->configdir);
    disk_mon_add_dir(list, config->accesslog);
    disk_mon_add_dir(list, config->errorlog);
    disk_mon_add_dir(list, config->auditlog);
    disk_mon_add_dir(list, config->auditfaillog);
    CFG_UNLOCK_READ(config);

    be = slapi_get_first_backend(&cookie);
    while (be) {
        if (slapi_back_get_info(be, BACK_INFO_DIRECTORY, (void **)&dir) == LDAP_SUCCESS) {
            /* db directory */
            disk_mon_add_dir(list, dir);
        }
        if (slapi_back_get_info(be, BACK_INFO_LOG_DIRECTORY, (void **)&dir) == LDAP_SUCCESS) {
            /* txn log dir */
            disk_mon_add_dir(list, dir);
        }
        be = (backend *)slapi_get_next_backend(cookie);
    }
    slapi_ch_free((void **)&cookie);
}

/*
 *  This function gets the stats of the directory and returns total space,
 *  available space, and used space of the directory.
 */
int32_t
disk_get_info(char *dir, uint64_t *total_space, uint64_t *avail_space, uint64_t *used_space)
{
    int32_t rc = LDAP_SUCCESS;
    struct statvfs buf;
    uint64_t freeBytes = 0;
    uint64_t blockSize = 0;
    uint64_t blocks = 0;

    if (statvfs(dir, &buf) != -1) {
        LL_UI2L(freeBytes, buf.f_bavail);
        LL_UI2L(blockSize, buf.f_bsize);
        LL_UI2L(blocks, buf.f_blocks);
        LL_MUL(*total_space, blocks, blockSize);
        LL_MUL(*avail_space, freeBytes, blockSize);
        *used_space = *total_space - *avail_space;
    } else {
        *total_space = 0;
        *avail_space = 0;
        *used_space = 0;
        rc = -1;
    }
    return rc;
}

/*
 *  This function checks the list of directories to see if any are below the
 *  threshold.  We return the directory/free disk space of the most critical
 *  directory.
 */
char *
disk_mon_check_diskspace(char **dirs, uint64_t threshold, uint64_t *disk_space)
{
    struct statvfs buf;
    uint64_t worst_disk_space = threshold;
    uint64_t freeBytes = 0;
    uint64_t blockSize = 0;
    char *worst_dir = NULL;
    int32_t hit_threshold = 0;
    int32_t i = 0;

    for (i = 0; dirs && dirs[i]; i++) {
        if (statvfs(dirs[i], &buf) != -1) {
            LL_UI2L(freeBytes, buf.f_bavail);
            LL_UI2L(blockSize, buf.f_bsize);
            LL_MUL(freeBytes, freeBytes, blockSize);

            if (LL_UCMP(freeBytes, <, threshold)) {
                hit_threshold = 1;
                if (LL_UCMP(freeBytes, <, worst_disk_space)) {
                    worst_disk_space = freeBytes;
                    worst_dir = dirs[i];
                }
            }
        }
    }

    if (hit_threshold) {
        *disk_space = worst_disk_space;
        return worst_dir;
    } else {
        *disk_space = 0;
        return NULL;
    }
}

#define LOGGING_OFF 0
#define LOGGING_ON 1
/*
 *  Disk Space Monitoring Thread
 *
 *  We need to monitor the free disk space of critical disks.
 *
 *  If we get below the free disk space threshold, start taking measures
 *  to avoid additional disk space consumption by stopping verbose logging,
 *  access/audit logging, and deleting rotated logs.
 *
 *  If this is not enough, then we need to shut slapd down to avoid
 *  possibly corrupting the db.
 *
 *  Future - it would be nice to be able to email an alert.
 */
void
disk_monitoring_thread(void *nothing __attribute__((unused)))
{
    char **dirs = NULL;
    char *dirstr = NULL;
    uint64_t previous_mark = 0;
    uint64_t disk_space = 0;
    uint64_t threshold = 0;
    uint64_t halfway = 0;
    time_t start = 0;
    time_t now = 0;
    int deleted_rotated_logs = 0;
    int readonly_on_threshold = 0;
    int logging_critical = 0;
    int passed_threshold = 0;
    int verbose_logging = 0;
    int using_accesslog = 0;
    int using_auditlog = 0;
    int using_auditfaillog = 0;
    int using_external_libs_debug = 0;
    int logs_disabled = 0;
    int grace_period = 0;
    int first_pass = 1;
    int ok_now = 0;
    int32_t immediate_shutdown = 0;
    Slapi_Backend *be = NULL;
    char *cookie = NULL;
    int32_t be_list_count = 0; /* Has the function scope and used to track adding new backends to read-only */
    int32_t be_index = 0; /* Is used locally to free backends and set back to read-write */
    Slapi_Backend *be_list[BE_LIST_SIZE + 1] = {0};

    while (!g_get_shutdown()) {
        char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];

        if (!first_pass) {
            struct timespec current_time = {0};

            pthread_mutex_lock(&diskmon_mutex);
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += 10;
            pthread_cond_timedwait(&diskmon_cvar, &diskmon_mutex, &current_time);
            pthread_mutex_unlock(&diskmon_mutex);
            /*
             *  We need to subtract from disk_space to account for the
             *  logging we just did, it doesn't hurt if we subtract a
             *  little more than necessary.
             */
            previous_mark = disk_space - 512;
            ok_now = 0;
        } else {
            first_pass = 0;
        }
        /*
         *  Get the config settings, as they could have changed
         */
        readonly_on_threshold = config_get_disk_threshold_readonly();
        logging_critical = config_get_disk_logging_critical();
        grace_period = 60 * config_get_disk_grace_period(); /* convert it to seconds */
        verbose_logging = config_get_errorlog_level();
        threshold = config_get_disk_threshold();
        halfway = threshold / 2;

        if (config_get_auditlog_logging_enabled()) {
            using_auditlog = 1;
        }
        if (config_get_auditfaillog_logging_enabled()) {
            using_auditfaillog = 1;
        }
        if (config_get_accesslog_logging_enabled()) {
            using_accesslog = 1;
        }
        if (config_get_external_libs_debug_enabled()) {
            using_external_libs_debug = 1;
        }
        /*
         *  Check the disk space.  Always refresh the list, as backends can be added
         */
        slapi_ch_array_free(dirs);
        dirs = NULL;
        disk_mon_get_dirs(&dirs);
        dirstr = disk_mon_check_diskspace(dirs, threshold, &disk_space);
        if (dirstr == NULL) {
            /*
             *  Good, none of our disks are within the threshold,
             *  disable readonly mode if it's on and reset the logging if we turned it off
             */
            if (passed_threshold) {
                if (readonly_on_threshold) {
                    be_index = 0;
                    if (be_list[be_index] != NULL) {
                        while ((be = be_list[be_index++])) {
                            slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread",
                                          "Putting the backend '%s' back to read-write mode\n", be->be_name);
                            slapi_mtn_be_set_readonly(be, 0);
                        }
                    }
                }
                if (logs_disabled) {
                    slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread",
                            "Disk space is now within acceptable levels.  Restoring the log settings.\n");
                    if (using_accesslog) {
                        config_set_accesslog_enabled(LOGGING_ON);
                    }
                    if (using_auditlog) {
                        config_set_auditlog_enabled(LOGGING_ON);
                    }
                    if (using_auditfaillog) {
                        config_set_auditfaillog_enabled(LOGGING_ON);
                    }
                    if (using_external_libs_debug) {
                        if (config_set_external_libs_debug_enabled(CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED,
                                "on", errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                            slapi_log_err(SLAPI_LOG_ERR, "disk_monitoring_thread", "setting on: %s: %s\n",
                                          CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED, errorbuf);
                        }
                    }
                } else {
                    slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread", "Disk space is now within acceptable levels.\n");
                }
                deleted_rotated_logs = 0;
                passed_threshold = 0;
                previous_mark = 0;
                logs_disabled = 0;
                be_list_count = 0;
            }
            continue;
        } else {
            passed_threshold = 1;
        }

        /*
         *  Check if we are already critical
         */
        if (disk_space < 4096) { /* 4 k */
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space is critically low on disk (%s), remaining space: %" PRIu64 " Kb.  Signaling slapd for shutdown...\n",
                    dirstr, (disk_space / 1024));
            immediate_shutdown = 1;
            goto cleanup;
        }

        /* If we are low, set all of the backends to readonly mode
         * Some file system, hosting backend, are possibly not full but we switch them readonly as well.
         * Only exception are in memory backend dse, schema, defaut_backend.
         */
        if (readonly_on_threshold) {
            be = slapi_get_first_backend(&cookie);
            while (be) {
                if (strcasecmp(be->be_name, DSE_BACKEND) != 0 &&
                    strcasecmp(be->be_name, DSE_SCHEMA) != 0 &&
                    strcasecmp(be->be_name, DEFBACKEND_NAME) != 0 &&
                    !slapi_be_get_readonly(be))
                {
                    if (be_list_count == BE_LIST_SIZE) { /* error - too many backends */
                        slapi_log_err(SLAPI_LOG_ERR, "disk_monitoring_thread",
                                      "Too many backends match search request - cannot proceed");
                    } else {
                        slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                                      "Putting the backend '%s' to read-only mode\n", be->be_name);
                        slapi_mtn_be_set_readonly(be, 1);
                        be_list[be_list_count++] = be;
                    }
                }
                be = (Slapi_Backend *)slapi_get_next_backend(cookie);
            }
            be_list[be_list_count] = NULL;
            slapi_ch_free_string(&cookie);
        }
        /*
         *  If we are low, see if we are using verbose error logging, and turn it off
         *  if logging is not critical
         */
        if (verbose_logging != 0 &&
            verbose_logging != LDAP_DEBUG_ANY &&
            verbose_logging != SLAPD_DEFAULT_FE_ERRORLOG_LEVEL &&
            verbose_logging != SLAPD_DEFAULT_ERRORLOG_LEVEL)
        {
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space is low on disk (%s), remaining space: %" PRIu64 " Kb, "
                    "temporarily setting error loglevel to the default level.\n",
                    dirstr, (disk_space / 1024));
            /* Setting the log level back to zero, actually sets the value to LDAP_DEBUG_ANY */
            config_set_errorlog_level(CONFIG_LOGLEVEL_ATTRIBUTE, "0", NULL, CONFIG_APPLY);
            continue;
        }
        /*
         *  If we are low, there's no verbose logging, logs are not critical, then disable the
         *  access/audit logs, log another error, and continue.
         */
        if (!logs_disabled && !logging_critical) {
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space is too low on disk (%s), remaining space: %" PRIu64 " Kb, disabling access and audit logging.\n",
                    dirstr, (disk_space / 1024));
            config_set_accesslog_enabled(LOGGING_OFF);
            config_set_auditlog_enabled(LOGGING_OFF);
            config_set_auditfaillog_enabled(LOGGING_OFF);
            if (config_set_external_libs_debug_enabled(CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED,
                    "off", errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "disk_monitoring_thread", "setting off: %s: %s\n",
                              CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED, errorbuf);
            }

            logs_disabled = 1;
            continue;
        }
        /*
         *  If we are low, we turned off verbose logging, logs are not critical, and we disabled
         *  access/audit logging, then delete the rotated logs, log another error, and continue.
         */
        if (!deleted_rotated_logs && !logging_critical) {
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space is too low on disk (%s), remaining space: %" PRIu64 " Kb, deleting rotated logs.\n",
                    dirstr, (disk_space / 1024));
            log__delete_rotated_logs();
            deleted_rotated_logs = 1;
            continue;
        }
        /*
         *  Ok, we've done what we can, log a message if we continue to lose available disk space
         */
        if (disk_space < previous_mark) {
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space is too low on disk (%s), remaining space: %" PRIu64 " Kb\n",
                    dirstr, (disk_space / 1024));
        }
        /*
         *  If we are below the halfway mark, and we did everything else,
         *  go into shutdown mode. If the disk space doesn't get critical,
         *  wait for the grace period before shutting down.  This gives an
         *  admin the chance to clean things up.
         */
        if (disk_space < halfway) {
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space on (%s) is too far below the threshold(%" PRIu64 " bytes).  "
                    "Waiting %d minutes for disk space to be cleaned up before shutting slapd down...\n",
                    dirstr, threshold, (grace_period / 60));
            start = slapi_current_rel_time_t();
            now = start;
            while ((now - start) < grace_period) {
                if (g_get_shutdown()) {
                    slapi_ch_array_free(dirs);
                    dirs = NULL;
                    return;
                }
                /*
                 *  Sleep for a little bit, but we don't want to run out of disk space
                 *  while sleeping for the entire grace period
                 */
                DS_Sleep(PR_SecondsToInterval(1));
                /*
                 *  Now check disk space again in hopes some space was freed up
                 */
                dirstr = disk_mon_check_diskspace(dirs, threshold, &disk_space);
                if (!dirstr) {
                    /*
                     *  Excellent, we are back to acceptable levels, reset everything...
                     *
                     */
                    if (readonly_on_threshold) {
                        be_index = 0;
                        if (be_list[be_index] != NULL) {
                            while ((be = be_list[be_index++])) {
                                slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread",
                                              "Putting the backend '%s' back to read-write mode\n", be->be_name);
                                slapi_mtn_be_set_readonly(be, 0);
                            }
                        }
                    }
                    slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread",
                            "Available disk space is now acceptable (%" PRIu64 " bytes).  Aborting shutdown, and restoring the log settings.\n",
                            disk_space);
                    if (logs_disabled && using_accesslog) {
                        config_set_accesslog_enabled(LOGGING_ON);
                    }
                    if (logs_disabled && using_auditlog) {
                        config_set_auditlog_enabled(LOGGING_ON);
                    }
                    if (logs_disabled && using_auditfaillog) {
                        config_set_auditfaillog_enabled(LOGGING_ON);
                    }
                    if (logs_disabled && using_external_libs_debug) {
                        if (config_set_external_libs_debug_enabled(CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED,
                                "on", errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                            slapi_log_err(SLAPI_LOG_ERR, "disk_monitoring_thread", "setting on: %s: %s\n",
                                          CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED, errorbuf);
                        }
                    }
                    deleted_rotated_logs = 0;
                    passed_threshold = 0;
                    logs_disabled = 0;
                    previous_mark = 0;
                    ok_now = 1;
                    start = 0;
                    now = 0;
                    be_list_count = 0;
                    break;
                } else if (disk_space < 4096) { /* 4 k */
                    /*
                     *  Disk space is critical, log an error, and shut it down now!
                     */
                    slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                            "Disk space is critically low on disk (%s), remaining space: %" PRIu64 " Kb.  Signaling slapd for shutdown...\n",
                            dirstr, (disk_space / 1024));
                    immediate_shutdown = 1;
                    goto cleanup;
                }
                now = slapi_current_rel_time_t();
            }

            if (ok_now) {
                /*
                 *  Disk space is acceptable, resume normal processing
                 */
                continue;
            }
            /*
             *  If disk space was freed up we would of detected in the above while loop.  So shut it down.
             */
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                    "Disk space is still too low (%" PRIu64 " Kb).  Signaling slapd for shutdown...\n",
                    (disk_space / 1024));
            goto cleanup;
        }
    }
    cleanup:
        if (readonly_on_threshold) {
            be_index = 0;
            if (be_list[be_index] != NULL) {
                while ((be = be_list[be_index++])) {
                    if (immediate_shutdown) {
                        slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread",
                                      "'%s' backend is set to read-only mode. "
                                      "It should be set manually to read-write mode after the instance's start.\n", be->be_name);
                    } else {
                        slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread",
                                      "Putting the backend '%s' back to read-write mode\n", be->be_name);
                        slapi_mtn_be_set_readonly(be, 0);
                    }
                }
            }
        }

        slapi_ch_array_free(dirs);
        dirs = NULL; /* now it is not needed but the code may be changed in the future and it'd better be more robust */
        g_set_shutdown(SLAPI_SHUTDOWN_DISKFULL);
        return;
}

static void
handle_listeners(struct POLL_STRUCT *fds)
{
    Connection_Table *ct = the_connection_table;
    size_t idx;
    int ctlist = 0;
    for (idx = 0; idx < listeners; ++idx) {
        int fdidx = listener_idxs[idx].idx;
        PRFileDesc *listenfd = listener_idxs[idx].listenfd;
        int secure = listener_idxs[idx].secure;
        int local = listener_idxs[idx].local;
        if (listenfd) {
            PR_ASSERT(fds != NULL);
            PR_ASSERT(listenfd == fds[fdidx].fd);
            if (SLAPD_POLL_LISTEN_READY(fds[fdidx].out_flags)) {
                /* accept() the new connection, put it on the active list for handle_pr_read_ready */
                ctlist = handle_new_connection(ct, SLAPD_INVALID_SOCKET, listenfd, secure, local, NULL);
                if (ctlist < 0) {
                    slapi_log_err(SLAPI_LOG_CONNS, "handle_listeners", "Error accepting new connection listenfd=%d\n",
                                  PR_FileDesc2NativeHandle(listenfd));
                    continue;
                } else {
                    /* Wake up the main event loop to handle this immediately. */
                    signal_listner(ctlist);
                }
            }
        }
    }
    return;
}

void
accept_thread(void *vports)
{
    daemon_ports_t *ports = (daemon_ports_t *)vports;
    Connection_Table *ct = the_connection_table;
    PRIntn num_poll = 0;
    struct POLL_STRUCT *fds = NULL;
    int select_return = 0;
    PRErrorCode prerr;
    int last_accept_new_connections = -1;
    PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_accept_wakeup_timer);
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    PRFileDesc **n_tcps = NULL;
    PRFileDesc **s_tcps = NULL;
    PRFileDesc **i_unix = NULL;
    n_tcps = ports->n_socket;
    s_tcps = ports->s_socket;
#if defined(ENABLE_LDAPI)
    i_unix = ports->i_socket;
#endif /* ENABLE_LDAPI */

    num_poll = setup_pr_accept_pds(n_tcps, s_tcps, i_unix, &fds);

    while (!g_get_shutdown()) {
        /* Do we need to accept new connections? */
        int accept_new_connections = ((ct->size - g_get_current_conn_count()) > slapdFrontendConfig->reservedescriptors);
        if (!accept_new_connections) {
            if (last_accept_new_connections) {
                slapi_log_err(SLAPI_LOG_ERR, "accept_thread",
                              "Not listening for new connections - too many fds open\n");
            }
            /* Need a sleep delay here. */
            PR_Sleep(pr_timeout);
            continue;
        } else {
            /* Log that we are now listening again */
            if (!last_accept_new_connections && last_accept_new_connections != -1) {
                slapi_log_err(SLAPI_LOG_ERR, "accept_thread",
                              "Listening for new connections again\n");
            }
        }

        select_return = POLL_FN(fds, num_poll, pr_timeout);
        switch (select_return) {
        case 0: /* Timeout */
            break;
        case -1: /* Error */
            prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_TRACE, "accept_thread", "PR_Poll() failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_system_strerror(prerr));
            break;
        default: /* a new connection */
            handle_listeners(fds);
            break;
        }
        last_accept_new_connections = accept_new_connections;
    }

    /* free the listener indexes */
    slapi_ch_free((void **)&listener_idxs);
    slapd_sockets_ports_free(ports);
    slapi_ch_free((void **)&fds);
}

void
slapd_sockets_ports_free(daemon_ports_t *ports_info)
{
    /* freeing PRFileDescs */
    PRFileDesc **fdesp = NULL;
    for (fdesp = ports_info->n_socket; fdesp && *fdesp; fdesp++) {
        if (*fdesp) {
            PR_Close(*fdesp);
            *fdesp = NULL;
        }
    }
    slapi_ch_free((void **)&ports_info->n_socket);

    for (fdesp = ports_info->s_socket; fdesp && *fdesp; fdesp++) {
        if (*fdesp) {
            PR_Close(*fdesp);
            *fdesp = NULL;
        }
    }
    slapi_ch_free((void **)&ports_info->s_socket);
#if defined(ENABLE_LDAPI)
    for (fdesp = ports_info->i_socket; fdesp && *fdesp; fdesp++) {
        if (*fdesp) {
            PR_Close(*fdesp);
            *fdesp = NULL;
        }
    }
    slapi_ch_free((void **)&ports_info->i_socket);
#endif /* ENABLE_LDAPI */

    /* freeing NetAddrs */
    PRNetAddr **nap;
    for (nap = ports_info->n_listenaddr; nap && *nap; nap++) {
        slapi_ch_free((void **)nap);
    }
    slapi_ch_free((void **)&ports_info->n_listenaddr);

    for (nap = ports_info->s_listenaddr; nap && *nap; nap++) {
        slapi_ch_free((void **)nap);
    }
    slapi_ch_free((void **)&ports_info->s_listenaddr);
#if defined(ENABLE_LDAPI)
    for (nap = ports_info->i_listenaddr; nap && *nap; nap++) {
        slapi_ch_free((void **)nap);
    }
    slapi_ch_free((void **)&ports_info->i_listenaddr);
#endif
}

void
slapd_daemon(daemon_ports_t *ports)
{
    /* We are passed some ports---one for regular connections, one
     * for SSL connections, one for ldapi connections.
     */
    /* Previously there was a ton of code #defined on NET_SSL.
     * This looked horrible, so now I'm doing it this way:
     * If you want me to do SSL, pass me something in the ssl port number.
     * If you don't, pass me zero.
     */
    PRFileDesc **n_tcps = NULL;
    PRFileDesc **s_tcps = NULL;
    PRFileDesc **i_unix = NULL;
    PRFileDesc **fdesp = NULL;
    uint64_t threads;
    int in_referral_mode = config_check_referral_mode();
    int connection_table_size = get_connection_table_size();
    the_connection_table = connection_table_new(connection_table_size);

    /*
     * Log a warning if we detect nunc-stans
     */
    if (config_get_enable_nunc_stans()) {
        slapi_log_err(SLAPI_LOG_WARNING, "slapd_daemon", "cn=config: nsslapd-enable-nunc-stans is on. nunc-stans has been deprecated and this flag is now ignored.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "slapd_daemon", "cn=config: nsslapd-enable-nunc-stans should be set to off or deleted from cn=config.\n");
    }

#ifdef RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS
    /*
     * Some DNS resolver implementations, such as the one built into
     * Solaris <= 8, need to use one or more low numbered file
     * descriptors internally (probably because they use a deficient
     * implementation of stdio).  So we make a call now that uses the
     * resolver so it has an opportunity to grab whatever low file
     * descriptors it needs (before we use up all of the low numbered
     * ones for incoming client connections and so on).
     */
    get_loopback_by_addr();
#endif

    /* Retrieve the sockets from their hiding place */
    n_tcps = ports->n_socket;
    s_tcps = ports->s_socket;

#if defined(ENABLE_LDAPI)
    i_unix = ports->i_socket;
#endif /* ENABLE_LDAPI */

    createsignalpipe();
    /* Setup our signal interception. */
    init_shutdown_detect();

    if (
        (n_tcps == NULL) &&
#if defined(ENABLE_LDAPI)
        (i_unix == NULL) &&
#endif                      /* ENABLE_LDAPI */
        (s_tcps == NULL)) { /* nothing to do */
        slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "No port to listen on\n");
        exit(1);
    }

    init_ct_list_threads();
    init_op_threads();

    /* Start the SNMP collator if counters are enabled. */
    if (config_get_slapi_counters()) {
        snmp_collator_start();
    }

    /*
     *  If we are monitoring disk space, then create the mutex, the cvar,
     *  and the monitoring thread.
     */
    if (config_get_disk_monitoring()) {
        pthread_condattr_t condAttr;
        int rc = 0;

        if ((rc = pthread_mutex_init(&diskmon_mutex, NULL)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "cannot create new lock.  error %d (%s)\n",
                          rc, strerror(rc));
            g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        }
        if ((rc = pthread_condattr_init(&condAttr)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon",
                          "cannot create new condition attribute variable.  error %d (%s)\n",
                          rc, strerror(rc));
            g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        }
        if ((rc = pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon",
                          "cannot set condition attr clock.  error %d (%s)\n",
                          rc, strerror(rc));
            g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        }
        if ((rc = pthread_cond_init(&diskmon_cvar, &condAttr)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon",
                          "cannot create new condition variable.  error %d (%s)\n",
                          rc, strerror(rc));
            g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        }
        pthread_condattr_destroy(&condAttr);
        if (rc == 0) {
            disk_thread_p = PR_CreateThread(PR_SYSTEM_THREAD,
                                            (VFP)(void *)disk_monitoring_thread, NULL,
                                            PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                            PR_JOINABLE_THREAD,
                                            SLAPD_DEFAULT_THREAD_STACKSIZE);
            if (NULL == disk_thread_p) {
                PRErrorCode errorCode = PR_GetError();
                slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon", "Unable to create disk monitoring thread - Shutting Down (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                              errorCode, slapd_pr_strerror(errorCode));
                g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
            }
        }
    }
    slapi_referral_check_init();

    /* We are now ready to accept incoming connections */
    if (n_tcps != NULL) {
        PRNetAddr **nap = ports->n_listenaddr;
        for (fdesp = n_tcps; fdesp && *fdesp; fdesp++, nap++) {
            if (PR_Listen(*fdesp, config_get_listen_backlog_size()) == PR_FAILURE) {
                PRErrorCode prerr = PR_GetError();
                char addrbuf[256];

                slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
                              "PR_Listen() on %s port %d failed: %s error %d (%s)\n",
                              netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
                              ports->n_port, SLAPI_COMPONENT_NAME_NSPR, prerr,
                              slapd_pr_strerror(prerr));
                g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
            }
            listeners++;
        }
    }

    if (s_tcps != NULL) {
        PRNetAddr **sap = ports->s_listenaddr;
        for (fdesp = s_tcps; fdesp && *fdesp; fdesp++, sap++) {
            if (PR_Listen(*fdesp, config_get_listen_backlog_size()) == PR_FAILURE) {
                PRErrorCode prerr = PR_GetError();
                char addrbuf[256];

                slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
                              "PR_Listen() on %s port %d failed: %s error %d (%s)\n",
                              netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
                              ports->s_port, SLAPI_COMPONENT_NAME_NSPR, prerr,
                              slapd_pr_strerror(prerr));
                g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
            }
            listeners++;
        }
    }

#if defined(ENABLE_LDAPI)
    if (i_unix != NULL) {
        PRNetAddr **iap = ports->i_listenaddr;
        for (fdesp = i_unix; fdesp && *fdesp; fdesp++, iap++) {
            if (PR_Listen(*fdesp, config_get_listen_backlog_size()) == PR_FAILURE) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
                              "listen() on %s failed: error %d (%s)\n",
                              (*iap)->local.path,
                              prerr,
                              slapd_pr_strerror(prerr));
                g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
            }
            listeners++;
        }
        initialize_ldapi_auth_dn_mappings(LDAPI_STARTUP);
    }
#endif /* ENABLE_LDAPI */

    listener_idxs = (listener_info *)slapi_ch_calloc(listeners, sizeof(*listener_idxs));

    /* Now we write the pid file, indicating that the server is finally and listening for connections */
    write_pid_file();

    /* Prepare the CT for first use */
    setup_pr_ct_firsttime_pds(the_connection_table);

    /* The server is ready and listening for connections. Logging "slapd started" message. */
    unfurl_banners(the_connection_table, ports, n_tcps, s_tcps, i_unix);

    /* Create a thread to accept new connections */
    accept_thread_p = PR_CreateThread(PR_SYSTEM_THREAD,
                                     (VFP)(void *)accept_thread, (void*)ports,
                                     PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                     PR_JOINABLE_THREAD,
                                     SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (NULL == accept_thread_p) {
        PRErrorCode errorCode = PR_GetError();
        slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon", "Unable to fd accept thread - Shutting Down (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                      errorCode, slapd_pr_strerror(errorCode));
        g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
    }

#ifdef WITH_SYSTEMD
    sd_notifyf(0, "READY=1\n"
                  "STATUS=slapd started: Ready to process requests\n"
                  "MAINPID=%lu",
                  (unsigned long)getpid());
#endif
    /* The meat of the operation is in a loop on a call to select */
    while (!g_get_shutdown()) {

        usleep(1000);
    }
    /* We get here when the server is shutting down */
    /* Do what we have to do before death */

#ifdef WITH_SYSTEMD
    sd_notify(0, "STOPPING=1");
#endif

    connection_table_abandon_all_operations(the_connection_table); /* abandon all operations in progress */

    if (!in_referral_mode) {
        ps_stop_psearch_system(); /* stop any persistent searches */
    }

    ct_thread_cleanup();
    op_thread_cleanup();
    housekeeping_stop(); /* Run this after op_thread_cleanup() logged sth */
    disk_monitoring_stop();
    slapi_referral_check_stop();

    /*
     * Now that they are abandonded, we need to mark them as done.
     * In NS while it's safe to allow excess jobs to be cleaned by
     * by the walk and ns_job_done of remaining queued events, the
     * issue is that if we allow something to live past this point
     * the CT is freed from underneath, and bad things happen (tm).
     *
     * NOTE: We do this after we stop psearch, because there could
     * be a race between flagging the psearch done, and users still
     * try to send on the connection. Similar with op_threads.
     */
    connection_table_disconnect_all(the_connection_table);

    if (!in_referral_mode) {
        /* signal tasks to start shutting down */
        task_cancel_all();
    }

    threads = g_get_active_threadcnt();
    if (threads > 0) {
        slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                      "slapd shutting down - waiting for %" PRIu64 " thread%s to terminate\n",
                      threads, (threads > 1) ? "s" : "");
    }

    threads = g_get_active_threadcnt();
    while (threads > 0) {
        PRPollDesc xpd;
        char x;
        int spe = 0;
        int i = 0;

        /* try to read from the signal pipe, in case threads are
         * blocked on it. */
        for(i = 0; i < the_connection_table->list_num; i++) {
            xpd.fd = signalpipes[i].signalpipe[0];
            xpd.in_flags = PR_POLL_READ;
            xpd.out_flags = 0;
            spe = PR_Poll(&xpd, 1, PR_INTERVAL_NO_WAIT);
            if (spe > 0) {
                spe = PR_Read(signalpipes[i].signalpipe[0], &x, 1);
                if (spe < 0) {
                    PRErrorCode prerr = PR_GetError();
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "listener could not clear signal pipe, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                                  prerr, slapd_system_strerror(prerr));
                    break;
                }
            } else if (spe == -1) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "PR_Poll() failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              prerr, slapd_system_strerror(prerr));
                break;
            } else {
                /* no data */
            }
        }
        DS_Sleep(PR_INTERVAL_NO_WAIT);
        if (threads != g_get_active_threadcnt()) {
            slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon",
                          "slapd shutting down - waiting for %" PRIu64 " threads to terminate\n",
                          g_get_active_threadcnt());
            threads = g_get_active_threadcnt();
        }
    }

    slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                  "slapd shutting down - closing down internal subsystems and plugins\n");
    /* let backends do whatever cleanup they need to do */
    slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon",
                  "slapd shutting down - waiting for backends to close down\n");

    eq_stop(); /* deprecated */
    eq_stop_rel();
    if (!in_referral_mode) {
        task_shutdown();
        uniqueIDGenCleanup();
    }

    plugin_closeall(1 /* Close Backends */, 1 /* Close Globals */);

    destroysignalpipe();
    /*
     * connection_table_free could use callbacks in the backend.
     * (e.g., be_search_results_release)
     * Thus, it needs to be called before be_cleanupall.
     */
    connection_table_free(the_connection_table);
    the_connection_table = NULL;

#if defined(ENABLE_LDAPI)
    /* Free LDAPI mappings */
    free_ldapi_auth_dn_mappings(LDAPI_SHUTDOWN);
#endif

    if (!in_referral_mode) {
        /* Close SNMP collator (if counters are enabled) after the plugins closed...
         * Replication plugin still performs internal ops that
         * may try to increment snmp stats.
         * Fix for defect 523780
         */
        if (config_get_slapi_counters()) {
            snmp_collator_stop();
        }
        mapping_tree_free();
    }

    /*
     * In theory, threads could be working "up to" this point so we only flush
     * access & security logs when we can guarantee that the buffered content
     * is "complete".
     */
    logs_flush();

    be_cleanupall();
    plugin_dependency_freeall();
    connection_post_shutdown_cleanup();
    slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon", "slapd shutting down - backends closed down\n");
    referrals_free();
    schema_destroy_dse_lock();

    /* tell the time thread to shutdown and then wait for it */
    time_shutdown = 1;

    if (g_get_shutdown() == SLAPI_SHUTDOWN_DISKFULL) {
        /* This is a server-induced shutdown, we need to manually remove the pid file */
        if (unlink(get_pid_file())) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "Failed to remove pid file %s\n", get_pid_file());
        }
    }
}


void
ct_thread_cleanup(void)
{
    slapi_log_err(SLAPI_LOG_INFO, "ct_thread_cleanup",
                  "slapd shutting down - signaling connection table threads\n");

    PR_AtomicIncrement(&ct_shutdown);
}

void
ct_list_thread(uint64_t threadnum)
{
    uint64_t threadid = (uint64_t) threadnum;

    while (!slapi_is_shutting_down()) {
         int select_return = 0;
         PRIntn num_poll = 0;
         PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_ct_thread_wakeup_timer);
         PRErrorCode prerr;
         num_poll = setup_pr_read_pds(the_connection_table, threadid);
         select_return = POLL_FN(the_connection_table->fd[threadid], num_poll, pr_timeout);
         switch (select_return) {
             case 0: /* Timeout */
                break;
             case -1: /* Error */
                prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_TRACE, "ct_list_thread", "PR_Poll() failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              prerr, slapd_system_strerror(prerr));
                 break;
             default: /* some new data ready */
                /* handle new data ready */
                handle_pr_read_ready(the_connection_table, threadid, 0);
                clear_signal(the_connection_table->fd[threadid], threadid);
                break;
         }
    }
    g_decr_active_threadcnt();
}

/* Create thread for each connection table list */
void
init_ct_list_threads(void)
{
    int ctlists = the_connection_table->list_num;

    /* start the connection table threads, one thread per CT list */
    for (uint64_t i = 0; i < ctlists; i++) {
        if(PR_CreateThread(PR_SYSTEM_THREAD,
                            (VFP)(void *)ct_list_thread, (void *) i,
                            PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                            PR_UNJOINABLE_THREAD,
                            SLAPD_DEFAULT_THREAD_STACKSIZE) == NULL) {
            int prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "init_ct_list_threads",
                          "PR_CreateThread failed - Shutting Down (" SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
                          g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
        } else {
            g_incr_active_threadcnt();
        }
    }
}

int
signal_listner(int list_num)
{
    /* Replaces previous macro---called to bump the thread out of select */
    if (write(signalpipes[list_num].writesignalpipe, "", 1) != 1) {
        /* this now means that the pipe is full
        * this is not a problem just go-on
        */
        slapi_log_err(SLAPI_LOG_CONNS,
                    "signal_listner", "Listener could not write to signal pipe %d\n",
                    errno);
    }
    return (0);
}

static int
clear_signal(struct POLL_STRUCT *fds, int list_num)
{
    if (fds[FDS_SIGNAL_PIPE].out_flags & SLAPD_POLL_FLAGS) {
        char buf[200];

        if (read(signalpipes[list_num].readsignalpipe, buf, 200) < 1) {
            slapi_log_err(SLAPI_LOG_ERR, "clear_signal", "Listener %d could not clear signal pipe\n",
            list_num);
        }
    }
    return 0;
}

static PRIntn
setup_pr_accept_pds(PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix,
    struct POLL_STRUCT **fds)
{
    LBER_SOCKET socketdesc = SLAPD_INVALID_SOCKET;
    PRIntn count = 0;
    size_t n_listeners = 0;
    struct POLL_STRUCT *myfds = NULL;

    /* How many fds do we have? */
    if (n_tcps != NULL) {
        PRFileDesc **fdesc = NULL;
        for (fdesc = n_tcps; fdesc && *fdesc; fdesc++, count++) { }
    }
    if (s_tcps != NULL) {
        PRFileDesc **fdesc = NULL;
        for (fdesc = s_tcps; fdesc && *fdesc; fdesc++, count++) { }
    }
#if defined(ENABLE_LDAPI)
    if (i_unix != NULL) {
        PRFileDesc **fdesc = NULL;
        for (fdesc = i_unix; fdesc && *fdesc; fdesc++, count++) { }
    }
#endif

    /* Setup the return ptr and alloc the struct */
    myfds = (struct POLL_STRUCT *)slapi_ch_calloc(1, (count + 1) * sizeof(struct POLL_STRUCT));
    *fds = myfds;

    /* Reset count. */
    count = 0;

    if (n_tcps != NULL) {
        PRFileDesc **fdesc = NULL;
        for (fdesc = n_tcps; fdesc && *fdesc; fdesc++, count++) {
            myfds[count].fd = *fdesc;
            myfds[count].in_flags = SLAPD_POLL_FLAGS;
            myfds[count].out_flags = 0;
            listener_idxs[n_listeners].listenfd = *fdesc;
            listener_idxs[n_listeners].idx = count;
            n_listeners++;
            slapi_log_err(SLAPI_LOG_HOUSE,
                          "setup_pr_accept_pds", "Listening for plaintext (LDAP) connections on %d\n", socketdesc);
        }
    }

    if (s_tcps != NULL) {
        PRFileDesc **fdesc = NULL;
        /*
         * To enable get_ssl_listener_fd to work, we need to stash the first
         * TLS listener that we have.
         */
        tls_listener = *s_tcps;

        for (fdesc = s_tcps; fdesc && *fdesc; fdesc++, count++) {
            myfds[count].fd = *fdesc;
            myfds[count].in_flags = SLAPD_POLL_FLAGS;
            myfds[count].out_flags = 0;
            listener_idxs[n_listeners].listenfd = *fdesc;
            listener_idxs[n_listeners].idx = count;
            listener_idxs[n_listeners].secure = 1;
            n_listeners++;
            slapi_log_err(SLAPI_LOG_HOUSE,
                          "setup_pr_accept_pds", "Listening for TLS (LDAPS) connections on %d\n", socketdesc);
        }
    }

#if defined(ENABLE_LDAPI)
    if (i_unix != NULL) {
        PRFileDesc **fdesc = NULL;
        for (fdesc = i_unix; fdesc && *fdesc; fdesc++, count++) {
            myfds[count].fd = *fdesc;
            myfds[count].in_flags = SLAPD_POLL_FLAGS;
            myfds[count].out_flags = 0;
            listener_idxs[n_listeners].listenfd = *fdesc;
            listener_idxs[n_listeners].idx = count;
            listener_idxs[n_listeners].local = 1;
            n_listeners++;
            slapi_log_err(SLAPI_LOG_HOUSE,
                          "setup_pr_accept_pds", "Listening for LDAPI connections on %d\n", socketdesc);
        }
    }
#endif
    if (n_listeners < listeners) {
        listener_idxs[n_listeners].idx = 0;
        listener_idxs[n_listeners].listenfd = NULL;
    }

    return count;
}


static void
setup_pr_ct_firsttime_pds(Connection_Table *ct)
{
    for (size_t j = 0; j < ct->list_num; j++) {
        for (size_t i = 0; i < ct->list_size; i++) {
            ct->c[j][i].c_fdi = SLAPD_INVALID_SOCKET_INDEX;
        }

        /* The fds entry for the signalpipe is always FDS_SIGNAL_PIPE (== 0) */
        PRIntn count = FDS_SIGNAL_PIPE;
        ct->fd[j][count].fd = signalpipes[j].signalpipe[0];
        ct->fd[j][count].in_flags = SLAPD_POLL_FLAGS;
        ct->fd[j][count].out_flags = 0;
    }
}

static PRIntn
setup_pr_read_pds(Connection_Table *ct, int listnum)
{
    Connection *c = NULL;
    Connection *next = NULL;
    /*
     * Start at + 1 because Signal pipe is always present at 0.
     * This is setup by setup_pr_ct_firsttime_pds.
     */
    PRIntn count = FDS_SIGNAL_PIPE + 1;
    /* Walk down the list of active connections to find
     * out which connections we should poll over.  If a connection
     * is no longer in use, we should remove it from the linked
     * list. */
    c = connection_table_get_first_active_connection(ct, listnum);
    while (c && count < ct->size) {
        next = connection_table_get_next_active_connection(ct, c);
        if (c->c_state == CONN_STATE_FREE) {
            connection_table_move_connection_out_of_active_list(ct, c);
        } else {
            /* we try to acquire the connection mutex, if it is already
             * acquired by another thread, don't wait
             */
            if (pthread_mutex_trylock(&(c->c_mutex)) == EBUSY) {
                c = next;
                continue;
            }
            if (c->c_flags & CONN_FLAG_CLOSING) {
                /* A worker thread has marked that this connection
                 * should be closed by calling disconnect_server.
                 * move this connection out of the active list
                 * the last thread to use the connection will close it
                 */
                connection_table_move_connection_out_of_active_list(ct, c);
            } else if (c->c_sd == SLAPD_INVALID_SOCKET) {
                connection_table_move_connection_out_of_active_list(ct, c);
            } else if (c->c_prfd != NULL) {
                if ((!c->c_gettingber) && (c->c_threadnumber < c->c_max_threads_per_conn)) {
                    int add_fd = 1;
                    /* check timeout for PAGED RESULTS */
                    if (pagedresults_is_timedout_nolock(c)) {
                        /* Exceeded the timelimit; disconnect the client */
                        disconnect_server_nomutex(c, c->c_connid, -1,
                                                  SLAPD_DISCONNECT_IO_TIMEOUT,
                                                  0);
                        connection_table_move_connection_out_of_active_list(ct,
                                                                            c);
                        add_fd = 0; /* do not poll on this fd */
                    }
                    if (add_fd) {
                        ct->fd[listnum][count].fd = c->c_prfd;
                        ct->fd[listnum][count].in_flags = SLAPD_POLL_FLAGS;
                        /* slot i of the connection table is mapped to slot
                         * count of the fds array */
                        c->c_fdi = count;
                        count++;
                    }
                } else {
                    if (c->c_threadnumber >= c->c_max_threads_per_conn) {
                        c->c_maxthreadsblocked++;
                        if (c->c_maxthreadsblocked == 1 && connection_has_psearch(c)) {
                            slapi_log_err(SLAPI_LOG_NOTICE, "connection_threadmain",
                                    "Connection (conn=%" PRIu64 ") has a running persistent search "
                                    "that has exceeded the maximum allowed threads per connection. "
                                    "New operations will be blocked.\n",
                                    c->c_connid);
                        }
                    }
                    c->c_fdi = SLAPD_INVALID_SOCKET_INDEX;
                }
            }
            pthread_mutex_unlock(&(c->c_mutex));
        }
        c = next;
    }

    return count;
}

static int idletimeout_reslimit_handle = -1;

/*
 * Register the idletimeout with the binder-based resource limits
 * subsystem. A SLAPI_RESLIMIT_STATUS_... code is returned.
 */
int
daemon_register_reslimits(void)
{
    return (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT, "nsIdleTimeout",
                                    &idletimeout_reslimit_handle));
}

static void
handle_pr_read_ready(Connection_Table *ct, int list_num, PRIntn num_poll __attribute__((unused)))
{
    Connection *c;
    time_t curtime = slapi_current_rel_time_t();

#if LDAP_ERROR_LOGGING
    if (slapd_ldap_debug & LDAP_DEBUG_CONNS) {
        connection_table_dump_activity_to_errors_log(ct);
    }
#endif /* LDAP_ERROR_LOGGING */


    /*
     * This function is called for all connections, so we traverse the entire
     * active connection list to find any errors, activity, etc.
     */
    for (c = connection_table_get_first_active_connection(ct, list_num); c != NULL;
         c = connection_table_get_next_active_connection(ct, c)) {
        if (c->c_state != CONN_STATE_FREE) {
            /* this check can be done without acquiring the mutex */
            if (c->c_gettingber) {
                continue;
            }

            pthread_mutex_lock(&(c->c_mutex));
            if (connection_is_active_nolock(c) && c->c_gettingber == 0) {
                PRInt16 out_flags;
                short readready;

                if (c->c_fdi != SLAPD_INVALID_SOCKET_INDEX) {
                    out_flags = ct->fd[list_num][c->c_fdi].out_flags;
                } else {
                    out_flags = 0;
                }

                readready = (out_flags & SLAPD_POLL_FLAGS);

                if (!readready && out_flags) {
                    /* some error occured */
                    slapi_log_err(SLAPI_LOG_CONNS,
                                  "handle_pr_read_ready", "POLL_FN() says connection on sd %d is bad "
                                                          "(closing)\n",
                                  c->c_sd);
                    disconnect_server_nomutex(c, c->c_connid, -1,
                                              SLAPD_DISCONNECT_POLL, EPIPE);
                } else if (readready) {
                    /* read activity */
                    slapi_log_err(SLAPI_LOG_CONNS,
                                  "handle_pr_read_ready", "read activity on %d\n", c->c_ci);
                    c->c_idlesince = curtime;

                    /* This is where the work happens ! */
                    /* MAB: 25 jan 01, error handling added */
                    if ((connection_activity(c, c->c_max_threads_per_conn)) == -1) {
                        /* This might happen as a result of
                         * trying to acquire a closing connection
                         */
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "handle_pr_read_ready", "connection_activity: abandoning conn %" PRIu64 " as "
                                                              "fd=%d is already closing\n",
                                      c->c_connid, c->c_sd);
                        /* The call disconnect_server should do nothing,
                         * as the connection c should be already set to CLOSING */
                        disconnect_server_nomutex(c, c->c_connid, -1,
                                                  SLAPD_DISCONNECT_POLL, EPIPE);
                    }
                } else if (c->c_idletimeout > 0 &&
                           (curtime - c->c_idlesince) >= c->c_idletimeout &&
                           NULL == c->c_ops) {
                    /* idle timeout */
                    disconnect_server_nomutex(c, c->c_connid, -1,
                                              SLAPD_DISCONNECT_IDLE_TIMEOUT, ETIMEDOUT);
                }
            }
            pthread_mutex_unlock(&(c->c_mutex));
        }
    }
}

/*
 * wrapper functions required so we can implement ioblock_timeout and
 * avoid blocking forever.
 */

#define SLAPD_POLLIN 0
#define SLAPD_POLLOUT 1

/* Return 1 if the given handle is ready for input or output,
 * or if it becomes ready within g_ioblock_timeout [msec].
 * Return -1 if handle is not ready and g_ioblock_timeout > 0,
 * or something goes seriously wrong.  Otherwise, return 0.
 * If -1 is returned, PR_GetError() explains why.
 * Revision: handle changed to void * to allow 64bit support
 *
 * Caller (flush_ber) must hold conn->c_pdumutex
 */
static int
slapd_poll(void *handle, int output)
{
    int rc;
    int ioblock_timeout = config_get_ioblocktimeout();
    struct POLL_STRUCT pr_pd;
    PRIntervalTime timeout = PR_MillisecondsToInterval(ioblock_timeout);

    pr_pd.fd = (PRFileDesc *)handle;
    pr_pd.in_flags = output ? PR_POLL_WRITE : PR_POLL_READ;
    pr_pd.out_flags = 0;
    rc = POLL_FN(&pr_pd, 1, timeout);

    if (rc < 0) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "slapd_poll",
                      "(%d) - %s error %d (%s)\n",
                      (int)(uintptr_t)handle, SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
        if (prerr == PR_PENDING_INTERRUPT_ERROR ||
            SLAPD_PR_WOULD_BLOCK_ERROR(prerr)) {
            rc = 0; /* try again */
        }
    } else if (rc == 0 && ioblock_timeout > 0) {
        PRIntn ihandle;
        ihandle = PR_FileDesc2NativeHandle((PRFileDesc *)handle);
        slapi_log_err(SLAPI_LOG_ERR, "slapd_poll", "(%d) - Timed out\n", ihandle);
        PR_SetError(PR_IO_TIMEOUT_ERROR, EAGAIN); /* timeout */
        rc = -1;
    }
    return rc;
}

/*
 * Revision: handle changed to void * and first
 * argument which used to be integer system fd is now ignored.
 */
static int
write_function(int ignore __attribute__((unused)), void *buffer, int count, void *handle)
{
    int sentbytes = 0;
    int bytes;
    int fd = PR_FileDesc2NativeHandle((PRFileDesc *)handle);

    if (handle == SLAPD_INVALID_SOCKET) {
        PR_SetError(PR_NOT_SOCKET_ERROR, EBADF);
    } else {
        while (1) {
            bytes = PR_Write((PRFileDesc *)handle, (char *)buffer + sentbytes,
                             count - sentbytes);
            if (bytes > 0) {
                sentbytes += bytes;
            } else if (bytes < 0) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_CONNS, "write_function", "PR_Write(%d) " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              fd, prerr, slapd_pr_strerror(prerr));
                if (!SLAPD_PR_WOULD_BLOCK_ERROR(prerr)) {
                    if (prerr != PR_CONNECT_RESET_ERROR) {
                        /* 'TCP connection reset by peer': no need to log */
                        slapi_log_err(SLAPI_LOG_ERR, "write_function", "PR_Write(%d) " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                                      fd, prerr, slapd_pr_strerror(prerr));
                    }
                    if (sentbytes < count) {
                        slapi_log_err(SLAPI_LOG_CONNS,
                                      "write_function", "PR_Write(%d) - wrote only %d bytes (expected %d bytes) - 0 (EOF)\n", /* disconnected */
                                      fd, sentbytes, count);
                    }
                    break; /* fatal error */
                } else {
                    /* The purpose of that call is to manage ioblocktimeout */
                    if (slapd_poll(handle, SLAPD_POLLOUT) < 0) {
                        break; /* fatal error */
                    }
                }
            } else if (bytes == 0) { /* disconnect */
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_CONNS,
                              "write_function", "PR_Write(%d) - 0 (EOF) %d:%s\n", /* disconnected */
                              fd, prerr, slapd_pr_strerror(prerr));
                PR_SetError(PR_PIPE_ERROR, EPIPE);
                break;
            }

            if (sentbytes == count) { /* success */
                return count;
            } else if (sentbytes > count) { /* too many bytes */
                slapi_log_err(SLAPI_LOG_ERR,
                              "write_function", "PR_Write(%d) overflow - sent %d bytes (expected %d bytes) - error\n",
                              fd, sentbytes, count);
                PR_SetError(PR_BUFFER_OVERFLOW_ERROR, EMSGSIZE);
                break;
            }
        }
    }
    return -1;
}

/* The argument is a pointer to the socket descriptor */
static int
openldap_io_setup(Sockbuf_IO_Desc *sbiod, void *arg)
{
    PR_ASSERT(sbiod);

    if (arg != NULL) {
        sbiod->sbiod_pvt = arg;
    }
    return 0;
}

static ber_slen_t
openldap_write_function(Sockbuf_IO_Desc *sbiod, void *buf, ber_len_t len)
{
    Connection *conn = NULL;
    PRFileDesc *fd = NULL;

    PR_ASSERT(sbiod);
    PR_ASSERT(sbiod->sbiod_pvt);

    conn = (Connection *)sbiod->sbiod_pvt;

    PR_ASSERT(conn->c_prfd);

    fd = (PRFileDesc *)conn->c_prfd;

    PR_ASSERT(fd != SLAPD_INVALID_SOCKET);

    return write_function(0, buf, len, fd);
}

static int
openldap_io_ctrl(Sockbuf_IO_Desc *sbiod __attribute__((unused)), int opt __attribute__((unused)), void *arg __attribute__((unused)))
{
    PR_ASSERT(0); /* not sure if this is needed */
    return -1;
}

static int
openldap_io_close(Sockbuf_IO_Desc *sbiod __attribute__((unused)))
{
    return 0; /* closing done in connection_cleanup() */
}

static Sockbuf_IO openldap_sockbuf_io = {
    openldap_io_setup,                     /* sbi_setup */
    NULL,                                  /* sbi_remove */
    openldap_io_ctrl,                      /* sbi_ctrl */
    openldap_read_function, /* sbi_read */ /* see connection.c */
    openldap_write_function,               /* sbi_write */
    openldap_io_close                      /* sbi_close */
};


int connection_type = -1; /* The type number assigned by the Factory for 'Connection' */

void
daemon_register_connection()
{
    if (connection_type == -1) {
        /* The factory is given the name of the object type, in
         * return for a type handle. Whenever the object is created
         * or destroyed the factory is called with the handle so
         * that it may call the constructors or destructors registered
         * with it.
         */
        connection_type = factory_register_type(SLAPI_EXT_CONNECTION, offsetof(Connection, c_extension));
    }
}


void
handle_closed_connection(Connection *conn)
{
    ber_sockbuf_remove_io(conn->c_sb, &openldap_sockbuf_io, LBER_SBIOD_LEVEL_PROVIDER);
}

/* NOTE: this routine is not reentrant
 * this function returns the connection table list the new connection is in
 */
static int
handle_new_connection(Connection_Table *ct, int tcps, PRFileDesc *pr_acceptfd, int secure, int local, Connection **newconn)
{
    int ns = 0;
    Connection *conn = NULL;
    /*    struct sockaddr_in    from;*/
    PRNetAddr from = {{0}};
    PRFileDesc *pr_clonefd = NULL;
    slapdFrontendConfig_t *fecfg = getFrontendConfig();
    ber_len_t maxbersize;

    if (newconn) {
        *newconn = NULL;
    }
    if ((ns = accept_and_configure(tcps, pr_acceptfd, &from,
                                   sizeof(from), secure, local, &pr_clonefd)) == SLAPD_INVALID_SOCKET) {
        return -1;
    }

    /* get a new Connection from the Connection Table */
    conn = connection_table_get_connection(ct, ns);
    if (conn == NULL) {
        PR_Close(pr_acceptfd);
        return -1;
    }
    pthread_mutex_lock(&(conn->c_mutex));

    /*
     * Set the default idletimeout and the handle.  We'll update c_idletimeout
     * after each bind so we can correctly set the resource limit.
     */
    conn->c_idletimeout = fecfg->idletimeout;
    conn->c_idletimeout_handle = idletimeout_reslimit_handle;
    conn->c_sd = ns;
    conn->c_prfd = pr_clonefd;
    conn->c_flags &= ~CONN_FLAG_CLOSING;

    /* Set per connection static config */
    conn->c_maxbersize = config_get_maxbersize();
    conn->c_ioblocktimeout = config_get_ioblocktimeout();
    conn->c_minssf = config_get_minssf();
    conn->c_enable_nagle = config_get_nagle();
    conn->c_minssf_exclude_rootdse = config_get_minssf_exclude_rootdse();
    conn->c_anon_access = config_get_anon_access_switch();
    conn->c_max_threads_per_conn = config_get_maxthreadsperconn();

    /* Store the fact that this new connection is an SSL connection */
    if (secure) {
        conn->c_flags |= CONN_FLAG_SSL;
    }

/*
     * clear the "returned events" field in ns' slot within the poll fds
     * array so that handle_read_ready() doesn't look at out_flags for an
     * old connection by mistake and do something bad such as close the
     * connection we just accepted.
     */

/* Dont have to worry about this now because of our mapping from
     * the connection table to the fds array.  This new connection
     * won't have a mapping. */
/* fds[ns].out_flags = 0; */

    ber_sockbuf_add_io(conn->c_sb, &openldap_sockbuf_io,
                       LBER_SBIOD_LEVEL_PROVIDER, conn);
    maxbersize = conn->c_maxbersize;
    ber_sockbuf_ctrl(conn->c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &maxbersize);
    if (secure && config_get_SSLclientAuth() != SLAPD_SSLCLIENTAUTH_OFF) {
        /* Prepare to handle the client's certificate (if any): */
        int rv;

        rv = slapd_ssl_handshakeCallback(conn->c_prfd, (void *)handle_handshake_done, conn);

        if (rv < 0) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "handle_new_connection", "SSL_HandshakeCallback() %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          rv, prerr, slapd_pr_strerror(prerr));
        }
        rv = slapd_ssl_badCertHook(conn->c_prfd, (void *)handle_bad_certificate, conn);

        if (rv < 0) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "handle_new_connection", "SSL_BadCertHook(%i) %i " SLAPI_COMPONENT_NAME_NSPR " error %d\n",
                          conn->c_sd, rv, prerr);
        }
    }

    connection_reset(conn, ns, &from, sizeof(from), secure);

    /* Call the plugin extension constructors */
    conn->c_extension = factory_create_extension(connection_type, conn, NULL /* Parent */);

#if defined(ENABLE_LDAPI)
    /* ldapi */
    if (local) {
        conn->c_unix_local = 1;
        conn->c_local_ssf = config_get_localssf();
        slapd_identify_local_user(conn);
    }
#endif /* ENABLE_LDAPI */

    connection_new_private(conn);

    /* Add this connection slot to the doubly linked list of active connections.  This
     * list is used to find the connections that should be used in the poll call. This
     * connection will be added directly after slot 0 which serves as the head of the list.
     * This must be done as the very last thing before we unlock the mutex, because once it
     * is added to the active list, it is live. */
    if (conn != NULL && conn->c_next == NULL && conn->c_prev == NULL) {
        /* Now give the new connection to the connection code*/
        connection_table_move_connection_on_to_active_list(the_connection_table, conn);
    }

    pthread_mutex_unlock(&(conn->c_mutex));

    g_increment_current_conn_count();

    if (newconn) {
        *newconn = conn;
    }
    return conn->c_ct_list;
}

static int
init_shutdown_detect(void)
{
    /* First of all, we must reset the signal mask to get rid of any blockages
   * the process may have inherited from its parent (such as the console), which
   * might result in the process not delivering those blocked signals, and thus,
   * misbehaving....
   */
    {
        int rc;
        sigset_t proc_mask;

        slapi_log_err(SLAPI_LOG_TRACE, "init_shutdown_detect", "Reseting signal mask....\n");
        (void)sigemptyset(&proc_mask);
        rc = pthread_sigmask(SIG_SETMASK, &proc_mask, NULL);
        slapi_log_err(SLAPI_LOG_TRACE, "init_shutdown_detect", "%s \n",
                      rc ? "Failed to reset signal mask" : "....Done (signal mask reset)!!");
    }

#if defined(HPUX10)
    PR_CreateThread(PR_USER_THREAD,
                    catch_signals,
                    NULL,
                    PR_PRIORITY_NORMAL,
                    PR_GLOBAL_THREAD,
                    PR_UNJOINABLE_THREAD,
                    SLAPD_DEFAULT_THREAD_STACKSIZE);
#else
#ifdef HPUX11
    /* In the optimized builds for HPUX, the signal handler doesn't seem
     * to get set correctly unless the primordial thread gets a chance
     * to run before we make the call to SIGNAL.  (At this point the
     * the primordial thread has spawned the daemon thread which called
     * this function.)  The call to DS_Sleep will give the primordial
     * thread a chance to run.
     */
    DS_Sleep(0);
#endif
    (void)SIGNAL(SIGPIPE, SIG_IGN);
    (void)SIGNAL(SIGCHLD, slapd_wait4child);
#ifndef LINUX
    /* linux uses USR1/USR2 for thread synchronization, so we aren't
     * allowed to mess with those.
     */
    (void)SIGNAL(SIGUSR1, slapd_do_nothing);
    (void)SIGNAL(SIGUSR2, set_shutdown);
#endif
    (void)SIGNAL(SIGTERM, set_shutdown);
    (void)SIGNAL(SIGINT, set_shutdown);
    (void)SIGNAL(SIGHUP, set_shutdown);
#endif /* HPUX */
    return 0;
}

static void
unfurl_banners(Connection_Table *ct, daemon_ports_t *ports, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char addrbuf[256];
    int isfirsttime = 1;

    if (ct->size <= slapdFrontendConfig->reservedescriptors) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon",
                      "Not enough descriptors to accept any connections. "
                      "This may be because the maxdescriptors configuration "
                      "directive is too small, the hard limit on descriptors is "
                      "too small (see limit(1)), or the reservedescriptors "
                      "configuration directive is too large. "
                      "Try increasing the number of descriptors available to "
                      "the slapd process. The current value is %d. %d "
                      "descriptors are currently reserved for internal "
                      "slapd use, so the total number of descriptors available "
                      "to the process must be greater than %d.\n",
                      ct->size, slapdFrontendConfig->reservedescriptors, slapdFrontendConfig->reservedescriptors);
        exit(1);
    }

    /*
     * This final startup message gives a definite signal to the admin
     * program that the server is up.  It must contain the string
     * "slapd started." because some of the administrative programs
     * depend on this.  See ldap/admin/lib/dsalib_updown.c.
     */
    if (n_tcps != NULL) { /* standard LDAP */
        PRNetAddr **nap = NULL;

        for (nap = ports->n_listenaddr; nap && *nap; nap++) {
            if (isfirsttime) {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                              "slapd started.  Listening on %s port %d for LDAP requests\n",
                              netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
                              ports->n_port);
                isfirsttime = 0;
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                              "Listening on %s port %d for LDAP requests\n",
                              netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
                              ports->n_port);
            }
        }
    }

    if (s_tcps != NULL) { /* LDAP over SSL; separate port */
        PRNetAddr **sap = NULL;

        for (sap = ports->s_listenaddr; sap && *sap; sap++) {
            if (isfirsttime) {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                              "slapd started.  Listening on %s port %d for LDAPS requests\n",
                              netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
                              ports->s_port);
                isfirsttime = 0;
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                              "Listening on %s port %d for LDAPS requests\n",
                              netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
                              ports->s_port);
            }
        }
    }

#if defined(ENABLE_LDAPI)
    if (i_unix != NULL) { /* LDAPI */
        PRNetAddr **iap = ports->i_listenaddr;

        slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
                      "%sListening on %s for LDAPI requests\n", isfirsttime ? "slapd started.  " : "",
                      (*iap)->local.path);
    }
#endif /* ENABLE_LDAPI */
}

/* On UNIX, we create a file with our PID in it */
static int
write_pid_file(void)
{
    FILE *fp = NULL;
    /*
     * The following section of code is closely coupled with the
     * admin programs. Please do not make changes here without
     * consulting the start/stop code for the admin code.
     */
    if ((fp = fopen(get_pid_file(), "w")) != NULL) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
        if (chmod(get_pid_file(), S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0) {
            unlink(get_pid_file());
        } else {
            return 0;
        }
    }
    return -1;
}

static void
set_shutdown(int sig __attribute__((unused)))
{
    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
    if (g_get_shutdown() == 0) {
        g_set_shutdown(SLAPI_SHUTDOWN_SIGNAL);
    }
#ifndef LINUX
    /* don't mess with USR1/USR2 on linux, used by libpthread */
    (void)SIGNAL(SIGUSR2, set_shutdown);
#endif
    (void)SIGNAL(SIGTERM, set_shutdown);
    (void)SIGNAL(SIGHUP, set_shutdown);
}

#ifndef LINUX
void
slapd_do_nothing(int sig)
{
    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
    (void)SIGNAL(SIGUSR1, slapd_do_nothing);

#if 0
    /*
     * Actually do a little more: dump the conn struct and
     * send it to a tmp file
     */
    connection_table_dump(connection_table);
#endif
}
#endif /* LINUX */

void
slapd_wait4child(int sig __attribute__((unused)))
{
    WAITSTATUSTYPE status;

    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
#ifdef USE_WAITPID
    while (waitpid((pid_t)-1, 0, WAIT_FLAGS) > 0)
#else     /* USE_WAITPID */
    while (wait3(&status, WAIT_FLAGS, 0) > 0)
#endif    /* USE_WAITPID */
        ; /* NULL */

    (void)SIGNAL(SIGCHLD, slapd_wait4child);
}

static PRFileDesc **
createprlistensockets(PRUint16 port, PRNetAddr **listenaddr, int secure __attribute__((unused)), int local)
{
    PRFileDesc **sock;
    PRNetAddr sa_server;
    PRErrorCode prerr = 0;
    PRSocketOptionData pr_socketoption;
    char addrbuf[256];
    char *logname = "createprlistensockets";
    int sockcnt = 0;
    int socktype;
    char *socktype_str = NULL;
    PRNetAddr **lap;
    int i;

    if (!port)
        goto suppressed;

    PR_ASSERT(listenaddr != NULL);

    /* need to know the count */
    sockcnt = 0;
    for (lap = listenaddr; lap && *lap; lap++) {
        sockcnt++;
    }

    if (0 == sockcnt) {
        slapi_log_err(SLAPI_LOG_ERR, logname,
                      "There is no address to listen\n");
        goto failed;
    }
    sock = (PRFileDesc **)slapi_ch_calloc(sockcnt + 1, sizeof(PRFileDesc *));
    pr_socketoption.option = PR_SockOpt_Reuseaddr;
    pr_socketoption.value.reuse_addr = 1;
    for (i = 0, lap = listenaddr; lap && *lap && i < sockcnt; i++, lap++) {
        /* create TCP socket */
        socktype = PR_NetAddrFamily(*lap);
#if defined(ENABLE_LDAPI)
        if (PR_AF_LOCAL == socktype) {
            socktype_str = "PR_AF_LOCAL";
        } else
#endif
            if (PR_AF_INET6 == socktype) {
            socktype_str = "PR_AF_INET6";
        } else {
            socktype_str = "PR_AF_INET";
        }
        if ((sock[i] = PR_OpenTCPSocket(socktype)) == SLAPD_INVALID_SOCKET) {
            prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, logname,
                          "PR_OpenTCPSocket(%s) failed: %s error %d (%s)\n",
                          socktype_str,
                          SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
            goto failed;
        }

        if (PR_SetSocketOption(sock[i], &pr_socketoption) == PR_FAILURE) {
            prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, logname,
                          "PR_SetSocketOption(PR_SockOpt_Reuseaddr) failed: %s error %d (%s)\n",
                          SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
            goto failed;
        }

        /* set up listener address, including port */
        memcpy(&sa_server, *lap, sizeof(sa_server));

        if (!local)
            PRLDAP_SET_PORT(&sa_server, port);

        if (PR_Bind(sock[i], &sa_server) == PR_FAILURE) {
            prerr = PR_GetError();
            if (!local) {
                slapi_log_err(SLAPI_LOG_ERR, logname,
                              "PR_Bind() on %s port %d failed: %s error %d (%s)\n",
                              netaddr2string(&sa_server, addrbuf, sizeof(addrbuf)), port,
                              SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
            }
#if defined(ENABLE_LDAPI)
            else {
                slapi_log_err(SLAPI_LOG_ERR, logname,
                              "PR_Bind() on %s file %s failed: %s error %d (%s)\n",
                              netaddr2string(&sa_server, addrbuf, sizeof(addrbuf)),
                              sa_server.local.path,
                              SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
            }
#endif /* ENABLE_LDAPI */
            goto failed;
        }
    }

#if defined(ENABLE_LDAPI)
    if (local) { /* ldapi */
        if (chmod((*listenaddr)->local.path,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) {
            slapi_log_err(SLAPI_LOG_ERR, logname, "err: %d", errno);
        }
    }
#endif /* ENABLE_LDAPI */

    return (sock);

failed:
    exit(1);

suppressed:
    return (PRFileDesc **)-1;
} /* createprlistensockets */


/*
 * Initialize the *addr structure based on listenhost.
 * Returns: 0 if successful and -1 if not (after logging an error message).
 */
int
slapd_listenhost2addr(const char *listenhost, PRNetAddr ***addr)
{
    char *logname = "slapd_listenhost2addr";
    PRErrorCode prerr = 0;
    int rval = 0;
    PRNetAddr *netaddr = (PRNetAddr *)slapi_ch_calloc(1, sizeof(PRNetAddr));

    PR_ASSERT(addr != NULL);
    *addr = NULL;

    if (NULL == listenhost) {
        /* listen on all interfaces */
        if (PR_SUCCESS != PR_SetNetAddr(PR_IpAddrAny, PR_AF_INET6, 0, netaddr)) {
            prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, logname,
                          "PR_SetNetAddr(PR_IpAddrAny) failed - %s error %d (%s)\n",
                          SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
            rval = -1;
            slapi_ch_free((void **)&netaddr);
        }
        *addr = (PRNetAddr **)slapi_ch_calloc(2, sizeof(PRNetAddr *));
        (*addr)[0] = netaddr;
    } else if (PR_SUCCESS == PR_StringToNetAddr(listenhost, netaddr)) {
        /* PR_StringNetAddr newer than NSPR v4.6.2 supports both IPv4&v6 */;
        *addr = (PRNetAddr **)slapi_ch_calloc(2, sizeof(PRNetAddr *));
        (*addr)[0] = netaddr;
    } else {
        PRAddrInfo *infop = PR_GetAddrInfoByName(listenhost,
                                                 PR_AF_UNSPEC, (PR_AI_ADDRCONFIG | PR_AI_NOCANONNAME));
        if (NULL != infop) {
            void *iter = NULL;
            int addrcnt = 0;
            int i = 0;
            /* need to count the address, first */
            while ((iter = PR_EnumerateAddrInfo(iter, infop, 0, netaddr)) != NULL) {
                addrcnt++;
            }
            if (0 == addrcnt) {
                slapi_log_err(SLAPI_LOG_ERR, logname,
                              "PR_EnumerateAddrInfo for %s failed - %s error %d (%s)\n",
                              listenhost, SLAPI_COMPONENT_NAME_NSPR, prerr,
                              slapd_pr_strerror(prerr));
                rval = -1;
            } else {
                char **strnetaddrs = NULL;
                *addr = (PRNetAddr **)slapi_ch_calloc(addrcnt + 1, sizeof(PRNetAddr *));
                iter = NULL; /* from the beginning */
                memset(netaddr, 0, sizeof(PRNetAddr));
                for (i = 0; i < addrcnt; i++) {
                    char abuf[256];
                    char *abp = abuf;
                    iter = PR_EnumerateAddrInfo(iter, infop, 0, netaddr);
                    if (NULL == iter) {
                        break;
                    }
                    /*
                     * Check if the netaddr is duplicated or not.
                     * IPv4 mapped IPv6 could be the identical to IPv4 addr.
                     */
                    netaddr2string(netaddr, abuf, sizeof(abuf));
                    if (PR_IsNetAddrType(netaddr, PR_IpAddrV4Mapped)) {
                        /* IPv4 mapped IPv6; redundant to IPv4;
                         * cut the "::ffff:" part. */
                        abp = strrchr(abuf, ':');
                        if (abp) {
                            abp++;
                        } else {
                            abp = abuf;
                        }
                    }
                    if (charray_inlist(strnetaddrs, abp)) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "slapd_listenhost2addr",
                                      "detected duplicated address %s "
                                      "[%s]\n",
                                      abuf, abp);
                    } else {
                        slapi_log_err(SLAPI_LOG_TRACE,
                                      "slapd_listenhost2addr",
                                      "registering address %s\n", abp);
                        slapi_ch_array_add(&strnetaddrs, slapi_ch_strdup(abp));
                        (*addr)[i] = netaddr;
                        netaddr =
                            (PRNetAddr *)slapi_ch_calloc(1, sizeof(PRNetAddr));
                    }
                }
                slapi_ch_free((void **)&netaddr); /* not used */
                slapi_ch_array_free(strnetaddrs);
            }
            PR_FreeAddrInfo(infop);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, logname,
                          "PR_GetAddrInfoByName(%s) failed - %s error %d (%s)\n",
                          listenhost, SLAPI_COMPONENT_NAME_NSPR, prerr,
                          slapd_pr_strerror(prerr));
            rval = -1;
        }
    }

    return rval;
}


/*
 * Map addr to a string equivalent and place the result in addrbuf.
 */
static const char *
netaddr2string(const PRNetAddr *addr, char *addrbuf, size_t addrbuflen)
{
    const char *retstr;

    if (NULL == addr || PR_IsNetAddrType(addr, PR_IpAddrAny)) {
        retstr = "All Interfaces";
    } else if (PR_IsNetAddrType(addr, PR_IpAddrLoopback)) {
        if (addr->raw.family == PR_AF_INET6 &&
            !PR_IsNetAddrType(addr, PR_IpAddrV4Mapped)) {
            retstr = "IPv6 Loopback";
        } else {
            retstr = "Loopback";
        }
    } else if (PR_SUCCESS == PR_NetAddrToString(addr, addrbuf, addrbuflen)) {
        if (0 == strncmp(addrbuf, "::ffff:", 7)) {
            /* IPv4 address mapped into IPv6 address space */
            retstr = addrbuf + 7;
        } else {
            /* full blown IPv6 address */
            retstr = addrbuf;
        }
    } else { /* punt */
        retstr = "address conversion failed";
    }

    return (retstr);
}

/* Create a signal pipe for each listener thread */
static int
createsignalpipe(void)
{
    /* Now we know how many listeners there are, setup signalpipes */
    signalpipes = (signal_pipe*) slapi_ch_calloc(1, (sizeof(signal_pipe) * the_connection_table->list_num));

    /* there is a signal pipe for each ct list/thread mapping */
    for (size_t i = 0; i < the_connection_table->list_num; i++) {
        if (PR_CreatePipe(&signalpipes[i].signalpipe[0], &signalpipes[i].signalpipe[1]) != 0) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "createsignalpipe",
                          "PR_CreatePipe() failed, %s error %d (%s)\n",
                          SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
            return (-1);
        }

        signalpipes[i].readsignalpipe = PR_FileDesc2NativeHandle(signalpipes[i].signalpipe[0]);
        signalpipes[i].writesignalpipe = PR_FileDesc2NativeHandle(signalpipes[i].signalpipe[1]);

        if (fcntl(signalpipes[i].readsignalpipe, F_SETFD, O_NONBLOCK) == -1) {
            slapi_log_err(SLAPI_LOG_ERR, "createsignalpipe",
                          "Failed to set FD for read pipe (%d).\n", errno);
        }

        if (fcntl(signalpipes[i].writesignalpipe, F_SETFD, O_NONBLOCK) == -1) {
            slapi_log_err(SLAPI_LOG_ERR, "createsignalpipe",
                          "Failed to set FD for write pipe (%d).\n", errno);
        }
    }
    return (0);
}

static int
destroysignalpipe(void)
{
    for (size_t i = 0; i < the_connection_table->list_num; i++) {
        (void) PR_Close(signalpipes[i].signalpipe[0]);
        (void) PR_Close(signalpipes[i].signalpipe[1]);
    }
    slapi_ch_free((void **)&signalpipes);
    return (0);
}

#ifdef HPUX10
#include <pthread.h> /* for sigwait */
/*
 * Set up a thread to catch signals
 * SIGUSR1 (ignore), SIGCHLD (call slapd_wait4child),
 * SIGUSR2 (set slapd_shutdown), SIGTERM (set slapd_shutdown),
 * SIGHUP (set slapd_shutdown)
 */
static void *
catch_signals()
{
    sigset_t caught_signals;
    int sig;

    sigemptyset(&caught_signals);

    while (!g_get_shutdown()) {

        /* Set the signals we're interested in catching */
        sigaddset(&caught_signals, SIGUSR1);
        sigaddset(&caught_signals, SIGCHLD);
        sigaddset(&caught_signals, SIGUSR2);
        sigaddset(&caught_signals, SIGTERM);
        sigaddset(&caught_signals, SIGHUP);

        (void)sigprocmask(SIG_BLOCK, &caught_signals, NULL);

        if ((sig = sigwait(&caught_signals)) < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "catch_signals", "sigwait returned -1\n");
            continue;
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "catch_signals", "detected signal %d\n", sig);
            switch (sig) {
            case SIGUSR1:
                continue; /* ignore SIGUSR1 */
            case SIGUSR2: /* fallthrough */
            case SIGTERM: /* fallthrough */
            case SIGHUP:
                g_set_shutdown(SLAPI_SHUTDOWN_SIGNAL);
                return NULL;
            case SIGCHLD:
                slapd_wait4child(sig);
                break;
            default:
                slapi_log_err(SLAPI_LOG_ERR,
                              "catch_signals", "Unknown signal (%d) received\n", sig);
            }
        }
    }
}
#endif /* HPUX */

static int
get_connection_table_size(void)
{
    int size = 0;
    int resrvdesc = 0;
    int maxdesc = config_get_maxdescriptors();

    /* Validate configured reserve descriptors */
    validate_num_config_reservedescriptors();

    resrvdesc = config_get_reservedescriptors();
    if (maxdesc > resrvdesc) {
         size = (maxdesc - resrvdesc);
    }

    /* Verify size does not exceed process max fds */
    if (size > FDS_PROCESS_MAX) {
        size = (FDS_PROCESS_MAX - resrvdesc);
    }

    return size;
}

PRFileDesc *
get_ssl_listener_fd()
{
    return tls_listener;
}

int
configure_pr_socket(PRFileDesc **pr_socket, int secure, int local)
{
    int ns = 0;
    int reservedescriptors = config_get_reservedescriptors();
    int enable_nagle = config_get_nagle();
    int fin_timeout = config_get_tcp_fin_timeout();
    int keepalive_time = config_get_tcp_keepalive_time();

    PRSocketOptionData pr_socketoption;

    ns = PR_FileDesc2NativeHandle(*pr_socket);

    /*
     * Some OS or third party libraries may require that low
     * numbered file descriptors be available, e.g., the DNS resolver
     * library on most operating systems. Therefore, we try to
     * replace the file descriptor returned by accept() with a
     * higher numbered one.  If this fails, we log an error and
     * continue (not considered a truly fatal error).
     */
    if (reservedescriptors > 0 && ns < reservedescriptors) {
        int newfd = fcntl(ns, F_DUPFD, reservedescriptors);

        if (newfd > 0) {
            PRFileDesc *nspr_layer_fd = PR_GetIdentitiesLayer(*pr_socket,
                                                              PR_NSPR_IO_LAYER);
            if (NULL == nspr_layer_fd) {
                slapi_log_err(SLAPI_LOG_ERR, "configure_pr_socket",
                              "Unable to move socket file descriptor %d above %d:"
                              " PR_GetIdentitiesLayer( %p, PR_NSPR_IO_LAYER )"
                              " failed\n",
                              ns, reservedescriptors, *pr_socket);
                close(newfd); /* can't fix things up in NSPR -- close copy */
            } else {
                PR_ChangeFileDescNativeHandle(nspr_layer_fd, newfd);
                close(ns); /* dup succeeded -- close the original */
                ns = newfd;
            }
        } else {
            int oserr = errno;
            slapi_log_err(SLAPI_LOG_ERR, "configure_pr_socket",
                          "Unable to move socket file descriptor %d above %d:"
                          " OS error %d (%s)\n",
                          ns, reservedescriptors, oserr,
                          slapd_system_strerror(oserr));
        }
    }

    /* Set keep_alive to keep old connections from lingering */
    pr_socketoption.option = PR_SockOpt_Keepalive;
    pr_socketoption.value.keep_alive = 1;
    if (PR_SetSocketOption(*pr_socket, &pr_socketoption) == PR_FAILURE) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR,
                      "configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_Keepalive failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
    }

    if (secure) {
        pr_socketoption.option = PR_SockOpt_Nonblocking;
        pr_socketoption.value.non_blocking = 1;
        if (PR_SetSocketOption(*pr_socket, &pr_socketoption) == PR_FAILURE) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_Nonblocking) failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
        }
    } else {
        /* We always want to have non-blocking I/O */
        pr_socketoption.option = PR_SockOpt_Nonblocking;
        pr_socketoption.value.non_blocking = 1;
        if (PR_SetSocketOption(*pr_socket, &pr_socketoption) == PR_FAILURE) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_Nonblocking) failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
        }
    } /* else (secure) */

    if (!enable_nagle && !local) {
        pr_socketoption.option = PR_SockOpt_NoDelay;
        pr_socketoption.value.no_delay = 1;
        if (PR_SetSocketOption(*pr_socket, &pr_socketoption) == PR_FAILURE) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_NoDelay) failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
        }
    } else if (!local) {
        pr_socketoption.option = PR_SockOpt_NoDelay;
        pr_socketoption.value.no_delay = 0;
        if (PR_SetSocketOption(*pr_socket, &pr_socketoption) == PR_FAILURE) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR,
                          "configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_NoDelay) failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
        }
    } /* else (!enable_nagle) */

    if (!local) {
        if (setsockopt(ns, IPPROTO_TCP, TCP_LINGER2, (void *)&fin_timeout, sizeof(fin_timeout)) == -1) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "configure_pr_socket", "setsockopt(TCP_LINGER2) failed, error %d (%s)\n",
                          errno, strerror(errno));
        }

        if (setsockopt(ns, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&keepalive_time, sizeof(keepalive_time)) == -1) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "configure_pr_socket", "setsockopt(TCP_KEEPIDLE) failed, error %d (%s)\n",
                          errno, strerror(errno));
        }
    }

    return ns;
}


#ifdef RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS
/*
 * A function that uses the DNS resolver in a simple way.  This is only
 * used to ensure that the DNS resolver has opened its files, etc.
 * using low numbered file descriptors.
 */
static void
get_loopback_by_addr(void)
{
#ifdef GETHOSTBYADDR_BUF_T
    struct hostent hp = {0};
    GETHOSTBYADDR_BUF_T hbuf;
#endif
    unsigned long ipaddr;
    struct in_addr ia;
    int herrno = 0;
    int rc = 0;

    ipaddr = htonl(INADDR_LOOPBACK);
    (void)GETHOSTBYADDR((char *)&ipaddr, sizeof(ipaddr),
                        AF_INET, &hp, hbuf, sizeof(hbuf), &herrno);
}
#endif /* RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS */

void
disk_monitoring_stop(void)
{
    if (disk_thread_p) {
        pthread_mutex_lock(&diskmon_mutex);
        pthread_cond_signal(&diskmon_cvar);
        pthread_mutex_unlock(&diskmon_mutex);
    }
}
