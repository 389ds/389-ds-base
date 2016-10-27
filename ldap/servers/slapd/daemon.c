/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
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
#define TCPLEN_T	int
#ifdef NEED_FILIO
#include <sys/filio.h>
#else /* NEED_FILIO */
#include <sys/ioctl.h>
#endif /* NEED_FILIO */
/* for some reason, linux tty stuff defines CTIME */
#include <stdio.h>
#ifdef LINUX
#undef CTIME
#include <sys/statfs.h>
#else
#include <sys/statvfs.h>
#include <sys/mnttab.h>
#endif
#include "slap.h"
#include "slapi-plugin.h"
#include "snmp_collator.h"
#include <private/pprio.h>
#include <ssl.h>
#include "fe.h"

#if defined(ENABLE_LDAPI)
#include "getsocketpeer.h"
#endif /* ENABLE_LDAPI */

#if defined (LDAP_IOCP)
#define	SLAPD_WAKEUP_TIMER	250
#else
#define	SLAPD_WAKEUP_TIMER	250
#endif

int slapd_wakeup_timer = SLAPD_WAKEUP_TIMER; /* time in ms to wakeup */
#ifdef notdef /* GGOODREPL */
/* 
 * time in secs to do housekeeping: 
 * this must be greater than slapd_wakeup_timer 
 */
short	slapd_housekeeping_timer = 10;
#endif /* notdef GGOODREPL */

PRFileDesc*		signalpipe[2];
static int writesignalpipe = SLAPD_INVALID_SOCKET;
static int readsignalpipe = SLAPD_INVALID_SOCKET;
#define FDS_SIGNAL_PIPE 0

static PRThread *disk_thread_p = NULL;
static PRCondVar *diskmon_cvar = NULL;
static PRLock *diskmon_mutex = NULL;
void disk_monitoring_stop(void);

typedef struct listener_info {
#ifdef ENABLE_NUNC_STANS
	PRStackElem stackelem; /* must be first in struct for PRStack to work */
#endif
	int idx; /* index of this listener in the ct->fd array */
	PRFileDesc *listenfd; /* the listener fd */
	int secure;
	int local;
#ifdef ENABLE_NUNC_STANS
	Connection_Table *ct; /* for listen job callback */
	struct ns_job_t *ns_job; /* the ns accept job */
#endif
} listener_info;

static int listeners = 0; /* number of listener sockets */
static listener_info *listener_idxs = NULL; /* array of indexes of listener sockets in the ct->fd array */

static int enable_nunc_stans = 0; /* if nunc-stans is set to enabled, set to 1 in slapd_daemon */

#define SLAPD_POLL_LISTEN_READY(xxflagsxx) (xxflagsxx & PR_POLL_READ)

static int get_configured_connection_table_size(void);
#ifdef RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS
static void get_loopback_by_addr( void );
#endif

static PRFileDesc **createprlistensockets(unsigned short port,
	PRNetAddr **listenaddr, int secure, int local);
static const char *netaddr2string(const PRNetAddr *addr, char *addrbuf,
	size_t addrbuflen);
static void	set_shutdown (int);
#ifdef ENABLE_NUNC_STANS
struct ns_job_t *ns_signal_job[6];
static void ns_set_shutdown (struct ns_job_t *job);
static void ns_set_user (struct ns_job_t *job);
#endif
static void setup_pr_read_pds(Connection_Table *ct, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix, PRIntn *num_to_read);

#ifdef HPUX10
static void* catch_signals();
#endif

static int createsignalpipe( void );

static char *
get_pid_file(void)
{
    return(pid_file);
}

static int
accept_and_configure(int s, PRFileDesc *pr_acceptfd, PRNetAddr *pr_netaddr,
	int addrlen, int secure, int local, PRFileDesc **pr_clonefd)
{
	int ns = 0;
	PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_wakeup_timer);

	(*pr_clonefd) = PR_Accept(pr_acceptfd, pr_netaddr, pr_timeout);
	if( !(*pr_clonefd) ) {
		PRErrorCode prerr = PR_GetError();
		slapi_log_err(SLAPI_LOG_ERR, "accept_and_configure", "PR_Accept() failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror(prerr));
		return(SLAPD_INVALID_SOCKET);
	}
	ns = configure_pr_socket( pr_clonefd, secure, local );

	return ns;
}

/* 
 * This is the shiny new re-born daemon function, without all the hair
 */
/* GGOODREPL static void handle_timeout( void ); */
static int handle_new_connection(Connection_Table *ct, int tcps, PRFileDesc *pr_acceptfd, int secure, int local, Connection **newconn );
#ifdef ENABLE_NUNC_STANS
static void ns_handle_new_connection(struct ns_job_t *job);
#endif
static void handle_pr_read_ready(Connection_Table *ct, PRIntn num_poll);
static int clear_signal(struct POLL_STRUCT *fds);
static void unfurl_banners(Connection_Table *ct,daemon_ports_t *ports, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix);
static int write_pid_file(void);
static int init_shutdown_detect(void);

/*
 * NSPR has different implementations for PRMonitor, depending
 * on the availble threading model
 * The PR_TestAndEnterMonitor is not available for pthreads
 * so this is a implementation based on the code in
 * prmon.c adapted to resemble the implementation in ptsynch.c
 *
 * The function needs access to the elements of the PRMonitor struct.
 * Therfor the pthread variant of PRMonitor is copied here.
 */
typedef struct MY_PRMonitor {
    const char* name;
    pthread_mutex_t lock;
    pthread_t owner;
    pthread_cond_t entryCV;
    pthread_cond_t waitCV;
    PRInt32 refCount;
    PRUint32 entryCount;
    PRIntn notifyTimes;
} MY_PRMonitor;

static PRBool MY_TestAndEnterMonitor(MY_PRMonitor *mon)
{
    pthread_t self = pthread_self();
    PRStatus rv;
    PRBool rc = PR_FALSE;

    PR_ASSERT(mon != NULL);
    rv = pthread_mutex_lock(&mon->lock);
    if (rv != 0) {
	slapi_log_err(SLAPI_LOG_ERR, "TestAndEnterMonitor",
                        "Failed to acquire monitor mutex, error (%d)\n", rv);
	return rc;
    }
    if (mon->entryCount != 0) {
        if (pthread_equal(mon->owner, self))
            goto done;
        rv = pthread_mutex_unlock(&mon->lock);
	if (rv != 0) {
	    slapi_log_err(SLAPI_LOG_ERR,"TestAndEnterMonitor",
                        "Failed to release monitor mutex, error (%d)\n", rv);
	}
        return PR_FALSE;
    }
    /* and now I have the monitor */
    PR_ASSERT(mon->notifyTimes == 0);
    PR_ASSERT((mon->owner) == 0);
    mon->owner = self;

done:
    mon->entryCount += 1;
    rv = pthread_mutex_unlock(&mon->lock);
    if (rv == PR_SUCCESS) {
	rc = PR_TRUE;
    } else {
	slapi_log_err(SLAPI_LOG_ERR,"TestAndEnterMonitor",
                        "Failed to release monitor mutex, error (%d)\n", rv);
	rc = PR_FALSE;
    }
    return rc;
}
/* Globals which are used to store the sockets between
 * calls to daemon_pre_setuid_init() and the daemon thread
 * creation. */

int daemon_pre_setuid_init(daemon_ports_t *ports)
{
	int	rc = 0;

	if (0 != ports->n_port) {
		ports->n_socket = createprlistensockets(ports->n_port,
											   ports->n_listenaddr, 0, 0);
	}

	if ( config_get_security() && (0 != ports->s_port) ) {
		ports->s_socket = createprlistensockets((unsigned short)ports->s_port,
		    									ports->s_listenaddr, 1, 0);
	} else {
	    ports->s_socket = SLAPD_INVALID_SOCKET;
	}

#if defined(ENABLE_LDAPI)
	/* ldapi */
	if(0 != ports->i_port) {
		ports->i_socket = createprlistensockets(1, ports->i_listenaddr, 0, 1);
	}
#endif /* ENABLE_LDAPI */

	return( rc );
}

/*
 * The time_shutdown static variable is used to signal the time thread
 * to shutdown.  We used to shut down the time thread when g_get_shutdown()
 * returned a non-zero value, but that caused the clock to stop, so to speak,
 * and all error log entries to have the same timestamp once the shutdown
 * process began.
 */
static int time_shutdown = 0;

void * 
time_thread(void *nothing)
{
    PRIntervalTime    interval;

    interval = PR_SecondsToInterval(1);

    while(!time_shutdown) {
        poll_current_time();
        csngen_update_time ();
        DS_Sleep(interval);
    }

    /*NOTREACHED*/
    return(NULL);
}

/*
 *  Return a copy of the mount point for the specified directory
 */
#ifdef SOLARIS
char *
disk_mon_get_mount_point(char *dir)
{
    struct mnttab mnt;
    struct stat s;
    dev_t dev_id;
    FILE *fp;

    fp = fopen("/etc/mnttab", "r");

    if (fp == NULL || stat(dir, &s) != 0) {
        return NULL;
    }

    dev_id = s.st_dev;

    while((0 == getmntent(fp, &mnt))){
        if (stat(mnt.mnt_mountp, &s) != 0) {
            continue;
        }
        if (s.st_dev == dev_id) {
            return (slapi_ch_strdup(mnt.mnt_mountp));
        }
    }

    return NULL;
}
#elif HPUX
char *
disk_mon_get_mount_point(char *dir)
{
    struct mntent *mnt;
    struct stat s;
    dev_t dev_id;
    FILE *fp;

    if ((fp = setmntent("/etc/mnttab", "r")) == NULL) {
        return NULL;
    }

    if (stat(dir, &s) != 0) {
        return NULL;
    }

    dev_id = s.st_dev;

    while((mnt = getmntent(fp))){
        if (stat(mnt->mnt_dir, &s) != 0) {
            continue;
        }
        if (s.st_dev == dev_id) {
            endmntent(fp);
            return (slapi_ch_strdup(mnt->mnt_dir));
        }
    }
    endmntent(fp);

    return NULL;
}
#else /* Linux */
char *
disk_mon_get_mount_point(char *dir)
{
    struct mntent *mnt;
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
    while((mnt = getmntent(fp))){
        if (stat(mnt->mnt_dir, &s) != 0) {
            continue;
        }
        if (s.st_dev == dev_id) {
            endmntent(fp);
            return (slapi_ch_strdup(mnt->mnt_dir));
        }
    }
    endmntent(fp);

    return NULL;
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

    if(dir == NULL)
        return;

    if(!charray_inlist(*list,dir)){
        slapi_ch_array_add(list, dir);
    } else {
        slapi_ch_free((void **)&dir);
    }
}

/*
 *  We gather all the log, txn log, config, and db directories
 */
void
disk_mon_get_dirs(char ***list, int logs_critical){
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

    be = slapi_get_first_backend (&cookie);
    while (be) {
        if(slapi_back_get_info(be, BACK_INFO_DIRECTORY, (void **)&dir) == LDAP_SUCCESS){  /* db directory */
        	disk_mon_add_dir(list, dir);
        }
        if(slapi_back_get_info(be, BACK_INFO_LOG_DIRECTORY, (void **)&dir) == LDAP_SUCCESS){  /*  txn log dir */
        	disk_mon_add_dir(list, dir);
        }
        be = (backend *)slapi_get_next_backend (cookie);
    }
    slapi_ch_free((void **)&cookie);
}

/*
 *  This function checks the list of directories to see if any are below the
 *  threshold.  We return the the directory/free disk space of the most critical
 *  directory.
 */
char *
disk_mon_check_diskspace(char **dirs, PRUint64 threshold, PRUint64 *disk_space)
{
#ifdef LINUX
    struct statfs buf;
#else
    struct statvfs buf;
#endif
    PRUint64 worst_disk_space = threshold;
    PRUint64 freeBytes = 0;
    PRUint64 blockSize = 0;
    char *worst_dir = NULL;
    int hit_threshold = 0;
    int i = 0;

    for(i = 0; dirs && dirs[i]; i++){
#ifndef LINUX
        if (statvfs(dirs[i], &buf) != -1)
#else
        if (statfs(dirs[i], &buf) != -1)
#endif
        {
            LL_UI2L(freeBytes, buf.f_bavail);
            LL_UI2L(blockSize, buf.f_bsize);
            LL_MUL(freeBytes, freeBytes, blockSize);

            if(LL_UCMP(freeBytes, <, threshold)){
                hit_threshold = 1;
                if(LL_UCMP(freeBytes, <, worst_disk_space)){
                    worst_disk_space = freeBytes;
                    worst_dir = dirs[i];
                }
            }
        }
    }

    if(hit_threshold){
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
disk_monitoring_thread(void *nothing)
{
    char **dirs = NULL;
    char *dirstr = NULL;
    PRUint64 previous_mark = 0;
    PRUint64 disk_space = 0;
    PRInt64 threshold = 0;
    PRUint64 halfway = 0;
    time_t start = 0;
    time_t now = 0;
    int deleted_rotated_logs = 0;
    int logging_critical = 0;
    int passed_threshold = 0;
    int verbose_logging = 0;
    int using_accesslog = 0;
    int using_auditlog = 0;
    int using_auditfaillog = 0;
    int logs_disabled = 0;
    int grace_period = 0;
    int first_pass = 1;
    int ok_now = 0;

    while(!g_get_shutdown()) {
        if(!first_pass){
            PR_Lock(diskmon_mutex);
            PR_WaitCondVar(diskmon_cvar, PR_SecondsToInterval(10));
            PR_Unlock(diskmon_mutex);
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
        logging_critical = config_get_disk_logging_critical();
        grace_period = 60 * config_get_disk_grace_period(); /* convert it to seconds */
        verbose_logging = config_get_errorlog_level();
        threshold = config_get_disk_threshold();
        halfway = threshold / 2;

        if(config_get_auditlog_logging_enabled()){
            using_auditlog = 1;
        }
        if(config_get_auditfaillog_logging_enabled()){
            using_auditfaillog = 1;
        }
        if(config_get_accesslog_logging_enabled()){
            using_accesslog = 1;
        }
        /*
         *  Check the disk space.  Always refresh the list, as backends can be added
         */
        slapi_ch_array_free(dirs);
        dirs = NULL;
        disk_mon_get_dirs(&dirs, logging_critical);
        dirstr = disk_mon_check_diskspace(dirs, threshold, &disk_space);
        if(dirstr == NULL){
            /*
             *  Good, none of our disks are within the threshold,
             *  reset the logging if we turned it off
             */
            if(passed_threshold){
            	if(logs_disabled){
            		slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread", "Disk space is now within acceptable levels.  "
                        "Restoring the log settings.\n");
                    if(using_accesslog){
                        config_set_accesslog_enabled(LOGGING_ON);
                    }
                    if(using_auditlog){
                        config_set_auditlog_enabled(LOGGING_ON);
                    }
                    if(using_auditfaillog){
                        config_set_auditfaillog_enabled(LOGGING_ON);
                    }
                } else {
                	slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread", "Disk space is now within acceptable levels.\n");
                }
            	deleted_rotated_logs = 0;
            	passed_threshold = 0;
            	previous_mark = 0;
            	logs_disabled = 0;
            }
            continue;
        } else {
            passed_threshold = 1;
        }
        /*
         *  Check if we are already critical
         */
        if(disk_space < 4096){ /* 4 k */
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is critically low on disk (%s), "
            	"remaining space: %" NSPRIu64 " Kb.  Signaling slapd for shutdown...\n", dirstr , (disk_space / 1024));
            g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
            return;
        }
        /*
         *  If we are low, see if we are using verbose error logging, and turn it off
         *  if logging is not critical
         */
        if(verbose_logging != 0 && verbose_logging != LDAP_DEBUG_ANY){
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is low on disk (%s), remaining space: "
            	"%" NSPRIu64 " Kb, temporarily setting error loglevel to the default level(%d).\n", dirstr,
                (disk_space / 1024), SLAPD_DEFAULT_ERRORLOG_LEVEL);
            /* Setting the log level back to zero, actually sets the value to LDAP_DEBUG_ANY */
            config_set_errorlog_level(CONFIG_LOGLEVEL_ATTRIBUTE,
                                      STRINGIFYDEFINE(SLAPD_DEFAULT_ERRORLOG_LEVEL),
                                      NULL, CONFIG_APPLY);
            continue;
        }
        /*
         *  If we are low, there's no verbose logging, logs are not critical, then disable the
         *  access/audit logs, log another error, and continue.
         */
        if(!logs_disabled && !logging_critical){
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is too low on disk (%s), remaining "
            	"space: %" NSPRIu64 " Kb, disabling access and audit logging.\n", dirstr, (disk_space / 1024));
            config_set_accesslog_enabled(LOGGING_OFF);
            config_set_auditlog_enabled(LOGGING_OFF);
            config_set_auditfaillog_enabled(LOGGING_OFF);
            logs_disabled = 1;
            continue;
        }
        /*
         *  If we are low, we turned off verbose logging, logs are not critical, and we disabled
         *  access/audit logging, then delete the rotated logs, log another error, and continue.
         */
        if(!deleted_rotated_logs && !logging_critical){
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is too low on disk (%s), remaining "
            	"space: %" NSPRIu64 " Kb, deleting rotated logs.\n", dirstr, (disk_space / 1024));
            log__delete_rotated_logs();
            deleted_rotated_logs = 1;
            continue;
        }
        /*
         *  Ok, we've done what we can, log a message if we continue to lose available disk space
         */
        if(disk_space < previous_mark){
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is too low on disk (%s), remaining "
            	"space: %" NSPRIu64 " Kb\n", dirstr, (disk_space / 1024));
        }
        /*
         *
         *  If we are below the halfway mark, and we did everything else,
         *  go into shutdown mode. If the disk space doesn't get critical,
         *  wait for the grace period before shutting down.  This gives an
         *  admin the chance to clean things up.
         *
         */
        if(disk_space < halfway){
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space on (%s) is too far below the threshold(%" NSPRIu64 " bytes).  "
                "Waiting %d minutes for disk space to be cleaned up before shutting slapd down...\n",
                dirstr, threshold, (grace_period / 60));
            time(&start);
            now = start;
            while( (now - start) < grace_period ){
                if(g_get_shutdown()){
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
                if(!dirstr){
                    /*
                     *  Excellent, we are back to acceptable levels, reset everything...
                     */
                    slapi_log_err(SLAPI_LOG_INFO, "disk_monitoring_thread", "Available disk space is now "
                    	"acceptable (%" NSPRIu64 " bytes).  Aborting shutdown, and restoring the log settings.\n",
                    	disk_space);
                    if(logs_disabled && using_accesslog){
                        config_set_accesslog_enabled(LOGGING_ON);
                    }
                    if(logs_disabled && using_auditlog){
                        config_set_auditlog_enabled(LOGGING_ON);
                    }
                    if(logs_disabled && using_auditfaillog){
                        config_set_auditfaillog_enabled(LOGGING_ON);
                    }
                    deleted_rotated_logs = 0;
                    passed_threshold = 0;
                    logs_disabled = 0;
                    previous_mark = 0;
                    ok_now = 1;
                    start = 0;
                    now = 0;
                    break;
                } else if(disk_space < 4096){ /* 4 k */
                    /*
                     *  Disk space is critical, log an error, and shut it down now!
                     */
                    slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is critically low "
                    	"on disk (%s), remaining space: %" NSPRIu64 " Kb.  Signaling slapd for shutdown...\n", 
                    	dirstr, (disk_space / 1024));
                    g_set_shutdown( SLAPI_SHUTDOWN_DISKFULL );
                    return;
                }
                time(&now);
            }

            if(ok_now){
                /*
                 *  Disk space is acceptable, resume normal processing
                 */
                continue;
            }
            /*
             *  If disk space was freed up we would of detected in the above while loop.  So shut it down.
             */
            slapi_log_err(SLAPI_LOG_ALERT, "disk_monitoring_thread", "Disk space is still too low "
            	"(%" NSPRIu64 " Kb).  Signaling slapd for shutdown...\n", (disk_space / 1024));
            g_set_shutdown( SLAPI_SHUTDOWN_DISKFULL );

            return;
        }
    }
}

static void
handle_listeners(Connection_Table *ct)
{
	int idx;
	for (idx = 0; idx < listeners; ++idx) {
		int fdidx = listener_idxs[idx].idx;
		PRFileDesc *listenfd = listener_idxs[idx].listenfd;
		int secure = listener_idxs[idx].secure;
		int local = listener_idxs[idx].local;
		if (fdidx && listenfd) {
			if (SLAPD_POLL_LISTEN_READY(ct->fd[fdidx].out_flags)) {
				/* accept() the new connection, put it on the active list for handle_pr_read_ready */
				int rc = handle_new_connection(ct, SLAPD_INVALID_SOCKET, listenfd, secure, local, NULL);
				if (rc) {
					slapi_log_err(SLAPI_LOG_CONNS, "handle_listeners", "Error accepting new connection listenfd=%d\n",
					              PR_FileDesc2NativeHandle(listenfd));
					continue;
				}
			}
		}
	}
	return;
}

/*
 * Convert any pre-existing DES passwords to AES.
 *
 * Grab the "password" attributes and search all the backends for
 * these attributes and convert them to AES if they are DES encoded.
 */
static void
convert_pbe_des_to_aes(void)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry **entries = NULL;
    struct slapdplugin *plugin = NULL;
    char **attrs = NULL;
    char *val = NULL;
    int converted_des_passwd = 0;
    int result = -1;
    int have_aes = 0;
    int have_des = 0;
    int i = 0, ii = 0;

    /*
     * Check that AES plugin is enabled, and grab all the unique
     * password attributes.
     */
    for ( plugin = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME);
          plugin != NULL;
          plugin = plugin->plg_next )
    {
        char *arg = NULL;

        if(plugin->plg_started && strcasecmp(plugin->plg_name, "AES") == 0){
            /* We have the AES plugin, and its enabled */
            have_aes = 1;
        }
        if(plugin->plg_started && strcasecmp(plugin->plg_name, "DES") == 0){
            /* We have the DES plugin, and its enabled */
            have_des = 1;
        }
        /* Gather all the unique password attributes from all the PBE plugins */
        for ( i = 0, arg = plugin->plg_argv[i];
              i < plugin->plg_argc;
              arg = plugin->plg_argv[++i] )
        {
            if(charray_inlist(attrs, arg)){
                continue;
            }
            charray_add(&attrs, slapi_ch_strdup(arg));
        }
    }

    if(have_aes && have_des){
        /*
         * Find any entries in cn=config that contain DES passwords and convert
         * them to AES
         */
        slapi_log_err(SLAPI_LOG_HOUSE, "convert_pbe_des_to_aes",
                "Converting DES passwords to AES...\n");

        for (i = 0; attrs && attrs[i]; i++){
            char *filter = PR_smprintf("%s=*", attrs[i]);

            pb = slapi_pblock_new();
            slapi_search_internal_set_pb(pb, "cn=config",
                LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL,
                (void *)plugin_get_default_component_id(),
                SLAPI_OP_FLAG_IGNORE_UNINDEXED);
            slapi_search_internal_pb(pb);
            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            for (ii = 0; entries && entries[ii]; ii++){
                if((val = slapi_entry_attr_get_charptr(entries[ii], attrs[i]))){
                    if(strlen(val) >= 5 && strncmp(val,"{DES}", 5) == 0){
                        /*
                         * We have a DES encoded password, convert it to AES
                         */
                        Slapi_PBlock *mod_pb = NULL;
                        Slapi_Value *sval = NULL;
                        LDAPMod mod_replace;
                        LDAPMod *mods[2];
                        char *replace_val[2];
                        char *passwd = NULL;
                        int rc = 0;

                        /* decode the DES password */
                        if(pw_rever_decode(val, &passwd, attrs[i]) == -1){
                            slapi_log_err(SLAPI_LOG_ERR,"convert_pbe_des_to_aes",
                                    "Failed to decode existing DES password for (%s)\n",
                                    slapi_entry_get_dn(entries[ii]));
                            rc = -1;
                        }

                        /* encode the password */
                        if (rc == 0){
                            sval = slapi_value_new_string(passwd);
                            if(pw_rever_encode(&sval, attrs[i]) == -1){
                                slapi_log_err(SLAPI_LOG_ERR, "convert_pbe_des_to_aes",
                                        "failed to encode AES password for (%s)\n",
                                        slapi_entry_get_dn(entries[ii]));
                                rc = -1;
                            }
                        }

                        if (rc == 0){
                            /* replace the attribute in the entry */
                            replace_val[0] = (char *)slapi_value_get_string(sval);
                            replace_val[1] = NULL;
                            mod_replace.mod_op = LDAP_MOD_REPLACE;
                            mod_replace.mod_type = attrs[i];
                            mod_replace.mod_values = replace_val;
                            mods[0] = &mod_replace;
                            mods[1] = 0;

                            mod_pb = slapi_pblock_new();
                            slapi_modify_internal_set_pb(mod_pb, slapi_entry_get_dn(entries[ii]),
                                    mods, 0, 0, (void *)plugin_get_default_component_id(), 0);
                            slapi_modify_internal_pb(mod_pb);

                            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
                            if (LDAP_SUCCESS != result) {
                                slapi_log_err(SLAPI_LOG_ERR, "convert_pbe_des_to_aes",
                                        "Failed to convert password for (%s) error (%d)\n",
                                        slapi_entry_get_dn(entries[ii]), result);
                            } else {
                                slapi_log_err(SLAPI_LOG_HOUSE, "convert_pbe_des_to_aes",
                                        "Successfully converted password for (%s)\n",
                                        slapi_entry_get_dn(entries[ii]));
                                converted_des_passwd = 1;
                            }
                        }
                        slapi_ch_free_string(&passwd);
                        slapi_value_free(&sval);
                        slapi_pblock_destroy(mod_pb);
                    }
                    slapi_ch_free_string(&val);
                }
            }
            slapi_free_search_results_internal(pb);
            slapi_pblock_destroy(pb);
            pb = NULL;
            slapi_ch_free_string(&filter);
        }
        if (!converted_des_passwd){
            slapi_log_err(SLAPI_LOG_HOUSE, "convert_pbe_des_to_aes",
                "No DES passwords found to convert.\n");
        }
    }
    charray_free(attrs);
}

#ifdef ENABLE_NUNC_STANS
/*
 * Nunc stans logging function.
 */
static void
nunc_stans_logging(int severity, const char *format, va_list varg)
{
	va_list varg_copy;
	int loglevel = SLAPI_LOG_ERR;

	if (severity == LOG_DEBUG){
		loglevel = SLAPI_LOG_NUNCSTANS;
	} else if(severity == LOG_INFO){
		loglevel = SLAPI_LOG_CONNS;
	}
	va_copy(varg_copy, varg);
	slapi_log_error_ext(loglevel, "nunc-stans", (char *)format, varg, varg_copy);
	va_end(varg_copy);
}

static void*
nunc_stans_malloc(size_t size)
{
	return (void*)slapi_ch_malloc((unsigned long)size);
}

static void*
nunc_stans_memalign(size_t size, size_t alignment)
{
    return (void*)slapi_ch_memalign(size, alignment);
}

static void*
nunc_stans_calloc(size_t count, size_t size)
{
	return (void*)slapi_ch_calloc((unsigned long)count, (unsigned long)size);
}

static void*
nunc_stans_realloc(void *block, size_t size)
{
	return (void*)slapi_ch_realloc((char *)block, (unsigned long)size);
}

static void
nunc_stans_free(void *ptr)
{
	slapi_ch_free((void **)&ptr);
}
#endif /* ENABLE_NUNC_STANS */

void slapd_daemon( daemon_ports_t *ports )
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
	PRIntn num_poll = 0;
	PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_wakeup_timer);
	PRThread *time_thread_p;
	int threads;
	int in_referral_mode = config_check_referral_mode();
#ifdef ENABLE_NUNC_STANS
	ns_thrpool_t *tp = NULL;
	struct ns_thrpool_config tp_config;
#endif
	int connection_table_size = get_configured_connection_table_size();
	the_connection_table= connection_table_new(connection_table_size);

#ifdef ENABLE_NUNC_STANS
	enable_nunc_stans = config_get_enable_nunc_stans();
#endif

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

    if (!enable_nunc_stans) {
        createsignalpipe();
        /* Setup our signal interception. */
        init_shutdown_detect();
#ifdef ENABLE_NUNC_STANS
    } else {
        PRInt32 maxthreads = (PRInt32)config_get_threadnumber();
        /* Set the nunc-stans thread pool config */
        ns_thrpool_config_init(&tp_config);

        tp_config.max_threads = maxthreads;
        tp_config.stacksize = SLAPD_DEFAULT_THREAD_STACKSIZE;
#ifdef LDAP_ERROR_LOGGING
        tp_config.log_fct = nunc_stans_logging;
#else
        tp_config.log_fct = NULL;
#endif
        tp_config.log_start_fct = NULL;
        tp_config.log_close_fct = NULL;
        tp_config.malloc_fct = nunc_stans_malloc;
        tp_config.memalign_fct = nunc_stans_memalign;
        tp_config.calloc_fct = nunc_stans_calloc;
        tp_config.realloc_fct = nunc_stans_realloc;
        tp_config.free_fct = nunc_stans_free;

        tp = ns_thrpool_new(&tp_config);

        /* We mark these as persistent so they keep blocking signals forever. */
        /* These *must* be in the event thread (ie not ns_job_thread) to prevent races */
        ns_add_signal_job(tp, SIGINT,  NS_JOB_PERSIST, ns_set_shutdown, NULL, &ns_signal_job[0]);
        ns_add_signal_job(tp, SIGTERM, NS_JOB_PERSIST, ns_set_shutdown, NULL, &ns_signal_job[1]);
        ns_add_signal_job(tp, SIGTSTP, NS_JOB_PERSIST, ns_set_shutdown, NULL, &ns_signal_job[3]);
        ns_add_signal_job(tp, SIGHUP,  NS_JOB_PERSIST, ns_set_user, NULL, &ns_signal_job[2]);
        ns_add_signal_job(tp, SIGUSR1, NS_JOB_PERSIST, ns_set_user, NULL, &ns_signal_job[4]);
        ns_add_signal_job(tp, SIGUSR2, NS_JOB_PERSIST, ns_set_user, NULL, &ns_signal_job[5]);
#endif /* ENABLE_NUNC_STANS */
    }

	if (
		(n_tcps == NULL) &&
#if defined(ENABLE_LDAPI)
		(i_unix == NULL) &&
#endif /* ENABLE_LDAPI */
	    (s_tcps == NULL) ) {	/* nothing to do */
	    slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "No port to listen on\n");
	    exit( 1 );
	}

	init_op_threads();

    /* Start the time thread */
    time_thread_p = PR_CreateThread(PR_SYSTEM_THREAD,
		(VFP) (void *) time_thread, NULL,
        PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, 
        PR_JOINABLE_THREAD, 
        SLAPD_DEFAULT_THREAD_STACKSIZE);
    if ( NULL == time_thread_p ) {
		PRErrorCode errorCode = PR_GetError();
		slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon", "Unable to create time thread - Shutting Down ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
				errorCode, slapd_pr_strerror(errorCode));
		g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
	}

    /*
     *  If we are monitoring disk space, then create the mutex, the cvar,
     *  and the monitoring thread.
     */
    if( config_get_disk_monitoring() ){
        if ( ( diskmon_mutex = PR_NewLock() ) == NULL ) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon",
                "Cannot create new lock for disk space monitoring. "
                SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                PR_GetError(), slapd_pr_strerror( PR_GetError() ));
            g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
        }
        if ( diskmon_mutex ){
            if(( diskmon_cvar = PR_NewCondVar( diskmon_mutex )) == NULL ) {
                slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
                    "Cannot create new condition variable for disk space monitoring. "
                    SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                    PR_GetError(), slapd_pr_strerror( PR_GetError() ));
                g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
            }
        }
        if( diskmon_mutex && diskmon_cvar ){
            disk_thread_p = PR_CreateThread(PR_SYSTEM_THREAD,
                (VFP) (void *) disk_monitoring_thread, NULL,
                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                PR_JOINABLE_THREAD,
                SLAPD_DEFAULT_THREAD_STACKSIZE);
            if ( NULL == disk_thread_p ) {
                PRErrorCode errorCode = PR_GetError();
                slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon", "Unable to create disk monitoring thread - Shutting Down ("
                    SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                    errorCode, slapd_pr_strerror(errorCode));
                g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
            }
        }
    }

	/* We are now ready to accept incoming connections */
	if ( n_tcps != NULL ) {
		PRFileDesc **fdesp;
		PRNetAddr  **nap = ports->n_listenaddr;
		for (fdesp = n_tcps; fdesp && *fdesp; fdesp++, nap++) {
			if ( PR_Listen( *fdesp, config_get_listen_backlog_size() ) == PR_FAILURE ) {
				PRErrorCode prerr = PR_GetError();
				char		addrbuf[ 256 ];

				slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
					"PR_Listen() on %s port %d failed: %s error %d (%s)\n",
					netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
					ports->n_port, SLAPI_COMPONENT_NAME_NSPR, prerr,
					slapd_pr_strerror( prerr ));
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
			}
			listeners++;
		}
	}

	if ( s_tcps != NULL ) {
		PRFileDesc **fdesp;
		PRNetAddr  **sap = ports->s_listenaddr;
		for (fdesp = s_tcps; fdesp && *fdesp; fdesp++, sap++) {
			if ( PR_Listen( *fdesp, config_get_listen_backlog_size() ) == PR_FAILURE ) {
				PRErrorCode prerr = PR_GetError();
				char		addrbuf[ 256 ];

				slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
					"PR_Listen() on %s port %d failed: %s error %d (%s)\n",
					netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
					ports->s_port, SLAPI_COMPONENT_NAME_NSPR, prerr,
					slapd_pr_strerror( prerr ));
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
			}
			listeners++;
		}
	}

#if defined(ENABLE_LDAPI)
	if( i_unix != NULL ) {
		PRFileDesc **fdesp;
		PRNetAddr  **iap = ports->i_listenaddr;
		for (fdesp = i_unix; fdesp && *fdesp; fdesp++, iap++) {
			if ( PR_Listen(*fdesp, config_get_listen_backlog_size()) == PR_FAILURE) {
				PRErrorCode prerr = PR_GetError();
				slapi_log_err(SLAPI_LOG_EMERG, "slapd_daemon",
					"listen() on %s failed: error %d (%s)\n",
					(*iap)->local.path,
					prerr,
					slapd_pr_strerror( prerr ));
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
			}
			listeners++;
		}
	}
#endif /* ENABLE_LDAPI */

	listener_idxs = (listener_info *)slapi_ch_calloc(listeners, sizeof(*listener_idxs));
	/*
	 * Convert old DES encoded passwords to AES
	 */
	convert_pbe_des_to_aes();

#ifdef ENABLE_NUNC_STANS
	if (enable_nunc_stans && !g_get_shutdown()) {
		setup_pr_read_pds(the_connection_table,n_tcps,s_tcps,i_unix,&num_poll);
		for (size_t ii = 0; ii < listeners; ++ii) {
			listener_idxs[ii].ct = the_connection_table; /* to pass to handle_new_connection */
			ns_add_io_job(tp, listener_idxs[ii].listenfd, NS_JOB_ACCEPT|NS_JOB_PERSIST|NS_JOB_PRESERVE_FD,
				      ns_handle_new_connection, &listener_idxs[ii], &listener_idxs[ii].ns_job);

		}
	}
#endif /* ENABLE_NUNC_STANS */
	/* Now we write the pid file, indicating that the server is finally and listening for connections */
	write_pid_file();

	/* The server is ready and listening for connections. Logging "slapd started" message. */
	unfurl_banners(the_connection_table,ports,n_tcps,s_tcps,i_unix);

#ifdef WITH_SYSTEMD
	sd_notifyf(0, "READY=1\n"
		"STATUS=slapd started: Ready to process requests\n"
		"MAINPID=%lu",
		(unsigned long) getpid()
	);
#endif

#ifdef ENABLE_NUNC_STANS
	if (enable_nunc_stans && ns_thrpool_wait(tp)) {
		slapi_log_err(SLAPI_LOG_ERR,
			   "slapd-daemon", "ns_thrpool_wait failed errno %d (%s)\n", errno,
			   slapd_system_strerror(errno));
	}
#endif
	/* The meat of the operation is in a loop on a call to select */
	while(!enable_nunc_stans && !g_get_shutdown())
	{
		int select_return = 0;
		PRErrorCode prerr;

		setup_pr_read_pds(the_connection_table,n_tcps,s_tcps,i_unix,&num_poll);
		select_return = POLL_FN(the_connection_table->fd, num_poll, pr_timeout);
		switch (select_return) {
		case 0: /* Timeout */
			/* GGOODREPL handle_timeout(); */
			break;
		case -1: /* Error */
			prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon", "PR_Poll() failed, "
				   SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				   prerr, slapd_system_strerror(prerr));
			break;
		default: /* either a new connection or some new data ready */
			/* handle new connections from the listeners */
			handle_listeners(the_connection_table);
			/* handle new data ready */
			handle_pr_read_ready(the_connection_table, connection_table_size);
			clear_signal(the_connection_table->fd);
			break;
		}
	}
	/* We get here when the server is shutting down */
	/* Do what we have to do before death */

#ifdef WITH_SYSTEMD
	sd_notify(0, "STOPPING=1");
#endif

	connection_table_abandon_all_operations(the_connection_table);	/* abandon all operations in progress */
	
	if ( ! in_referral_mode ) {
		ps_stop_psearch_system(); /* stop any persistent searches */
	}

	/* free the listener indexes */
	slapi_ch_free((void **)&listener_idxs);

	for (fdesp = n_tcps; fdesp && *fdesp; fdesp++) {
		PR_Close( *fdesp );
	}
	slapi_ch_free ((void**)&n_tcps);

	for (fdesp = i_unix; fdesp && *fdesp; fdesp++) {
		PR_Close( *fdesp );
	}
	slapi_ch_free ((void**)&i_unix);

	for (fdesp = s_tcps; fdesp && *fdesp; fdesp++) {
		PR_Close( *fdesp );
	}
	slapi_ch_free ((void**)&s_tcps);

	/* freeing NetAddrs */
	{
		PRNetAddr **nap;
		for (nap = ports->n_listenaddr; nap && *nap; nap++) {
			slapi_ch_free ((void**)nap);
		}
		slapi_ch_free ((void**)&ports->n_listenaddr);

		for (nap = ports->s_listenaddr; nap && *nap; nap++) {
			slapi_ch_free ((void**)nap);
		}
		slapi_ch_free ((void**)&ports->s_listenaddr);
#if defined(ENABLE_LDAPI)
		for (nap = ports->i_listenaddr; nap && *nap; nap++) {
			slapi_ch_free ((void**)nap);
		}
		slapi_ch_free ((void**)&ports->i_listenaddr);
#endif
	}

	/* Might compete with housecleaning thread, but so far so good */
	be_flushall();
	op_thread_cleanup();
	housekeeping_stop(); /* Run this after op_thread_cleanup() logged sth */
       disk_monitoring_stop();

	threads = g_get_active_threadcnt();
	if ( threads > 0 ) {
		slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
			"slapd shutting down - waiting for %d thread%s to terminate\n",
			threads, ( threads > 1 ) ? "s" : "");
	}

	threads = g_get_active_threadcnt();
	while ( threads > 0 ) {
		if (!enable_nunc_stans) {
			PRPollDesc xpd;
			char x;
			int spe = 0;

			/* try to read from the signal pipe, in case threads are
			 * blocked on it. */
			xpd.fd = signalpipe[0];
			xpd.in_flags = PR_POLL_READ;
			xpd.out_flags = 0;
			spe = PR_Poll(&xpd, 1, PR_INTERVAL_NO_WAIT);
			if (spe > 0) {
				spe = PR_Read(signalpipe[0], &x, 1);
				if (spe < 0) {
					PRErrorCode prerr = PR_GetError();
					slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "listener could not clear signal pipe, "
							SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
							prerr, slapd_system_strerror(prerr));
					break;
				}
			} else if (spe == -1) {
				PRErrorCode prerr = PR_GetError();
				slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "PR_Poll() failed, "
						SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
						prerr, slapd_system_strerror(prerr));
				break;
			} else {
				/* no data */
			}
		}
		DS_Sleep(PR_INTERVAL_NO_WAIT);
		if ( threads != g_get_active_threadcnt() )  {
			slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon",
					"slapd shutting down - waiting for %d threads to terminate\n",
					g_get_active_threadcnt());
			threads = g_get_active_threadcnt();
		}
	}

	slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
	    "slapd shutting down - closing down internal subsystems and plugins\n");
	/* let backends do whatever cleanup they need to do */
	slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon",
		"slapd shutting down - waiting for backends to close down\n");

	eq_stop();
	if ( ! in_referral_mode ) {
		task_shutdown();
		uniqueIDGenCleanup ();   
	}

	plugin_closeall( 1 /* Close Backends */, 1 /* Close Globals */);

	if ( ! in_referral_mode ) {
		/* Close SNMP collator after the plugins closed... 
		 * Replication plugin still performs internal ops that
		 * may try to increment snmp stats.
		 * Fix for defect 523780
		 */
		snmp_collator_stop();
		mapping_tree_free ();
	}

    /* In theory, threads could be working "up to" this point
     * so we only flush access logs when we can guarantee that the buffered
     * content is "complete".
     */
    log_access_flush();

#ifdef ENABLE_NUNC_STANS

    /*
     * We need to shutdown the nunc-stans thread pool before we free the
     * connection table.
     */
    if (enable_nunc_stans) {
        /* Now we free the signal jobs. We do it late here to keep intercepting
         * them for as long as possible .... Later we need to rethink this to
         * have plugins and such destroy while the tp is still active.
         */
        ns_job_done(ns_signal_job[0]);
        ns_job_done(ns_signal_job[1]);
        ns_job_done(ns_signal_job[2]);
        ns_job_done(ns_signal_job[3]);
        ns_job_done(ns_signal_job[4]);
        ns_job_done(ns_signal_job[5]);

        ns_thrpool_destroy(tp);
    }
#endif
	/* 
	 * connection_table_free could use callbacks in the backend.
	 * (e.g., be_search_results_release)
	 * Thus, it needs to be called before be_cleanupall.
	 */
	connection_table_free(the_connection_table);
	the_connection_table= NULL;

	be_cleanupall ();
	plugin_dependency_freeall();
	connection_post_shutdown_cleanup();
	slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon", "slapd shutting down - backends closed down\n");
	referrals_free();
	schema_destroy_dse_lock();

	/* tell the time thread to shutdown and then wait for it */
	time_shutdown = 1;
	PR_JoinThread( time_thread_p );

	if ( g_get_shutdown() == SLAPI_SHUTDOWN_DISKFULL ){
		/* This is a server-induced shutdown, we need to manually remove the pid file */
		if( unlink(get_pid_file()) ){
			slapi_log_err(SLAPI_LOG_ERR, "slapd_daemon", "Failed to remove pid file %s\n", get_pid_file());
		}
	}
}

int signal_listner()
{
	if (enable_nunc_stans) {
		return( 0 );
	}
	/* Replaces previous macro---called to bump the thread out of select */
	if ( write( writesignalpipe, "", 1) != 1 ) {
		/* this now means that the pipe is full
		 * this is not a problem just go-on
		 */
		slapi_log_err(SLAPI_LOG_CONNS,
			"signal_listner", "Listener could not write to signal pipe %d\n",
			errno);
	}
	return( 0 );
}

static int clear_signal(struct POLL_STRUCT *fds)
{
	if (enable_nunc_stans) {
		return 0;
	}
	if ( fds[FDS_SIGNAL_PIPE].out_flags & SLAPD_POLL_FLAGS ) {
		char	buf[200];

		slapi_log_err(SLAPI_LOG_CONNS, "clear_signal", "Listener got signaled\n");
		if ( read( readsignalpipe, buf, 200 ) < 1 ) {
			slapi_log_err(SLAPI_LOG_ERR, "clear_signal", "Listener could not clear signal pipe\n");
		}
	} 
	return 0;
}

static int first_time_setup_pr_read_pds = 1;
static int listen_addr_count = 0;

static void
setup_pr_read_pds(Connection_Table *ct, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix, PRIntn *num_to_read)
{
	Connection *c= NULL;
	Connection *next= NULL;
	LBER_SOCKET socketdesc = SLAPD_INVALID_SOCKET;
	int accept_new_connections;
	static int last_accept_new_connections = -1;
	PRIntn count = 0;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int max_threads_per_conn = config_get_maxthreadsperconn();
	int n_listeners = 0;

	accept_new_connections = ((ct->size - g_get_current_conn_count())
		> slapdFrontendConfig->reservedescriptors);
	if ( ! accept_new_connections ) {
		if ( last_accept_new_connections ) {
			slapi_log_err(SLAPI_LOG_ERR, "setup_pr_read_pds", 
				"Not listening for new connections - too many fds open\n");
			/* reinitialize n_tcps and s_tcps to the pds */
			first_time_setup_pr_read_pds = 1;
		}
	} else {
		if ( ! last_accept_new_connections &&
			last_accept_new_connections != -1 ) {
			slapi_log_err(SLAPI_LOG_ERR, "setup_pr_read_pds",
				"Listening for new connections again\n");
			/* reinitialize n_tcps and s_tcps to the pds */
			first_time_setup_pr_read_pds = 1;
		}
	}
	last_accept_new_connections = accept_new_connections;


	/* initialize the mapping from connection table entries to fds entries */
	if (first_time_setup_pr_read_pds)
	{
		int i;
		for (i = 0; i < ct->size; i++)
		{
			ct->c[i].c_fdi = SLAPD_INVALID_SOCKET_INDEX;
		}

		if (!enable_nunc_stans) {
			/* The fds entry for the signalpipe is always FDS_SIGNAL_PIPE (== 0) */
			count = FDS_SIGNAL_PIPE;
			ct->fd[count].fd = signalpipe[0];
			ct->fd[count].in_flags = SLAPD_POLL_FLAGS;
			ct->fd[count].out_flags = 0;
			count++;
		}
		/* The fds entry for n_tcps starts with n_tcps and less than n_tcpe */
		ct->n_tcps = count;
		if (n_tcps != NULL && accept_new_connections)
		{
			PRFileDesc **fdesc = NULL;
			for (fdesc = n_tcps; fdesc && *fdesc; fdesc++, count++) {
				ct->fd[count].fd = *fdesc;
				ct->fd[count].in_flags = SLAPD_POLL_FLAGS;
				ct->fd[count].out_flags = 0;
				listener_idxs[n_listeners].listenfd = *fdesc;
				listener_idxs[n_listeners].idx = count;
				n_listeners++;
				slapi_log_err(SLAPI_LOG_HOUSE, 
					"setup_pr_read_pds", "Listening for connections on %d\n", socketdesc);
			}
		} else {
			ct->fd[count].fd = NULL;
			count++;
		}
		ct->n_tcpe = count;
	
		ct->s_tcps = count;
		/* The fds entry for s_tcps starts with s_tcps and less than s_tcpe */
		if (s_tcps != NULL && accept_new_connections)
		{
			PRFileDesc **fdesc = NULL;
			for (fdesc = s_tcps; fdesc && *fdesc; fdesc++, count++) {
				ct->fd[count].fd = *fdesc;
				ct->fd[count].in_flags = SLAPD_POLL_FLAGS;
				ct->fd[count].out_flags = 0;
				listener_idxs[n_listeners].listenfd = *fdesc;
				listener_idxs[n_listeners].idx = count;
				listener_idxs[n_listeners].secure = 1;
				n_listeners++;
				slapi_log_err(SLAPI_LOG_HOUSE, 
					"setup_pr_read_pds", "Listening for SSL connections on %d\n", socketdesc);
			}
		} else {
			ct->fd[count].fd = NULL;
			count++;
		}
		ct->s_tcpe = count;


#if defined(ENABLE_LDAPI)
		ct->i_unixs = count;
		/* The fds entry for i_unix starts with i_unixs and less than i_unixe */
		if (i_unix != NULL && accept_new_connections)
		{
			PRFileDesc **fdesc = NULL;
			for (fdesc = i_unix; fdesc && *fdesc; fdesc++, count++) {
				ct->fd[count].fd = *fdesc;
				ct->fd[count].in_flags = SLAPD_POLL_FLAGS;
				ct->fd[count].out_flags = 0;
				listener_idxs[n_listeners].listenfd = *fdesc;
				listener_idxs[n_listeners].idx = count;
				listener_idxs[n_listeners].local = 1;
				n_listeners++;
				slapi_log_err(SLAPI_LOG_HOUSE,
					"setup_pr_read_pds", "Listening for LDAPI connections on %d\n", socketdesc);
			}
		} else {
			ct->fd[count].fd = NULL;
			count++;
		}
		ct->i_unixe = count;
#endif
 
		first_time_setup_pr_read_pds = 0;
		listen_addr_count = count;

		if (n_listeners < listeners) {
			listener_idxs[n_listeners].idx = 0;
			listener_idxs[n_listeners].listenfd = NULL;
		}
	}

	/* count is the number of entries we've place in the fds array.
	 * listen_addr_count is counted up when 
	 * first_time_setup_pr_read_pds is TURE. */
	count = listen_addr_count;
	
	/* Walk down the list of active connections to find 
	 * out which connections we should poll over.  If a connection
	 * is no longer in use, we should remove it from the linked 
	 * list. */
	c = connection_table_get_first_active_connection (ct);
	while (c) 
	{
		next = connection_table_get_next_active_connection (ct, c);
		if ( c->c_mutex == NULL )
		{
			connection_table_move_connection_out_of_active_list(ct,c);
		}
		else
		{
			/* we try to acquire the connection mutex, if it is already
			 * acquired by another thread, don't wait
			 */
			if (PR_FALSE == MY_TestAndEnterMonitor((MY_PRMonitor *)c->c_mutex)) {
				c = next;
				continue;
			}
			if (c->c_flags & CONN_FLAG_CLOSING)
			{
				/* A worker thread has marked that this connection
				 * should be closed by calling disconnect_server. 
				 * move this connection out of the active list
				 * the last thread to use the connection will close it
				 */
				connection_table_move_connection_out_of_active_list(ct,c);
			}
			else if ( c->c_sd == SLAPD_INVALID_SOCKET )
			{
				connection_table_move_connection_out_of_active_list(ct,c);
			}
			else if ( c->c_prfd != NULL)
			{
				if ((!c->c_gettingber)
						 && (c->c_threadnumber < max_threads_per_conn))
				{
					int add_fd = 1;
					/* check timeout for PAGED RESULTS */
                    if (pagedresults_is_timedout_nolock(c))
					{
						/* Exceeded the timelimit; disconnect the client */
						disconnect_server_nomutex(c, c->c_connid, -1,
						                          SLAPD_DISCONNECT_IO_TIMEOUT,
						                          0);
						connection_table_move_connection_out_of_active_list(ct,
						                                                    c);
						add_fd = 0; /* do not poll on this fd */
					}
					if (add_fd)
					{
						ct->fd[count].fd = c->c_prfd;
						ct->fd[count].in_flags = SLAPD_POLL_FLAGS;
						/* slot i of the connection table is mapped to slot
						 * count of the fds array */
						c->c_fdi = count;
						count++;
					}
				}
				else
				{
					if(c->c_threadnumber >= max_threads_per_conn){
						c->c_maxthreadsblocked++;
					}
					c->c_fdi = SLAPD_INVALID_SOCKET_INDEX;
				}
			}
			PR_ExitMonitor(c->c_mutex);
		}
		c = next;
	}

	if( num_to_read )
		(*num_to_read) = count;

}

#ifdef notdef /* GGOODREPL */
static void
handle_timeout( void )
{
	static time_t prevtime = 0;
	static time_t housekeeping_fire_time = 0;
	time_t curtime = current_time();

	if (0 == prevtime) {
		prevtime = time (&housekeeping_fire_time);
	}

	if ( difftime(curtime, prevtime) >= 
		slapd_housekeeping_timer ) {
		int	num_active_threads;

		snmp_collator_update();

		prevtime = curtime;
		num_active_threads = g_get_active_threadcnt();
		if ( (num_active_threads == 0)  || 
			(difftime(curtime, housekeeping_fire_time) >= 
		slapd_housekeeping_timer*3) ) {
		housekeeping_fire_time = curtime;
			housekeeping_start(curtime);
		}
	}

}
#endif /* notdef */


static int	idletimeout_reslimit_handle = -1;

/*
 * Register the idletimeout with the binder-based resource limits
 * subsystem. A SLAPI_RESLIMIT_STATUS_... code is returned.
 */
int
daemon_register_reslimits( void )
{
	return( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT, "nsIdleTimeout",
			&idletimeout_reslimit_handle ));
}

static void
handle_pr_read_ready(Connection_Table *ct, PRIntn num_poll)
{
	Connection *c;
	time_t curtime = current_time();
	int maxthreads = config_get_maxthreadsperconn();

#if LDAP_ERROR_LOGGING
	if ( slapd_ldap_debug & LDAP_DEBUG_CONNS )
	{
		connection_table_dump_activity_to_errors_log(ct);
	}
#endif /* LDAP_ERROR_LOGGING */


	/*
	 * This function is called for all connections, so we traverse the entire
	 * active connection list to find any errors, activity, etc.
	 */
	for ( c = connection_table_get_first_active_connection (ct); c != NULL;
          c = connection_table_get_next_active_connection (ct, c) )
	{
		if ( c->c_mutex != NULL )
		{
			/* this check can be done without acquiring the mutex */
			if (c->c_gettingber) continue;

			PR_EnterMonitor(c->c_mutex);
			if ( connection_is_active_nolock (c) && c->c_gettingber == 0 )
			{
			    PRInt16 out_flags;
				short readready;

	            if (c->c_fdi != SLAPD_INVALID_SOCKET_INDEX)
	            {
	                out_flags = ct->fd[c->c_fdi].out_flags;
	            }
	            else
	            {
	                out_flags = 0;
	            }

				readready = ( out_flags & SLAPD_POLL_FLAGS );

				if ( !readready && out_flags )
				{
					/* some error occured */
					slapi_log_err(SLAPI_LOG_CONNS,
					    "handle_pr_read_ready", "POLL_FN() says connection on sd %d is bad "
					    "(closing)\n", c->c_sd);
					disconnect_server_nomutex( c, c->c_connid, -1,
								   SLAPD_DISCONNECT_POLL, EPIPE );
				}
				else if ( readready )
				{
					/* read activity */
					slapi_log_err(SLAPI_LOG_CONNS,
					    "handle_pr_read_ready", "read activity on %d\n", c->c_ci);
					c->c_idlesince = curtime;

					/* This is where the work happens ! */
					/* MAB: 25 jan 01, error handling added */
					if ((connection_activity( c, maxthreads )) == -1) {
						/* This might happen as a result of
						 * trying to acquire a closing connection
						 */
						slapi_log_err(SLAPI_LOG_ERR,
							"handle_pr_read_ready", "connection_activity: abandoning conn %" NSPRIu64 " as "
							"fd=%d is already closing\n", c->c_connid,c->c_sd);
						/* The call disconnect_server should do nothing,
						 * as the connection c should be already set to CLOSING */
						disconnect_server_nomutex( c, c->c_connid, -1,
									   SLAPD_DISCONNECT_POLL, EPIPE );
					}
				}
				else if (c->c_idletimeout > 0 &&
						(curtime - c->c_idlesince) >= c->c_idletimeout &&
						NULL == c->c_ops )
				{
					/* idle timeout */
					disconnect_server_nomutex( c, c->c_connid, -1,
								   SLAPD_DISCONNECT_IDLE_TIMEOUT, EAGAIN );
				}
			}
			PR_ExitMonitor(c->c_mutex);
		}
	}
}

#ifdef ENABLE_NUNC_STANS
#define CONN_NEEDS_CLOSING(c) (c->c_flags & CONN_FLAG_CLOSING) || (c->c_sd == SLAPD_INVALID_SOCKET)
/* Used internally by ns_handle_closure and ns_handle_pr_read_ready.
 * Returns 0 if the connection was successfully closed, or 1 otherwise.
 * Must be called with the c->c_mutex locked.
 */
static int
ns_handle_closure_nomutex(Connection *c)
{
	int rc = 0;
	PR_ASSERT(c->c_refcnt > 0); /* one for the conn active list, plus possible other threads */
	PR_ASSERT(CONN_NEEDS_CLOSING(c));
	if (connection_table_move_connection_out_of_active_list(c->c_ct, c)) {
		/* not closed - another thread still has a ref */
		rc = 1;
		/* reschedule closure job */
		ns_connection_post_io_or_closing(c);
	}
	return rc;
}
/* This function is called when the connection has been marked
 * as closing and needs to be cleaned up.  It will keep trying
 * and re-arming itself until there are no references.
 */
static void
ns_handle_closure(struct ns_job_t *job)
{
	Connection *c = (Connection *)ns_job_get_data(job);
	int do_yield = 0;

	/* this function must be called from the event loop thread */
#ifdef DEBUG
	PR_ASSERT(0 == NS_JOB_IS_THREAD(ns_job_get_type(job)));
#else
    /* This doesn't actually confirm it's in the event loop thread, but it's a start */
	if (NS_JOB_IS_THREAD(ns_job_get_type(job)) != 0) {
		slapi_log_err(SLAPI_LOG_ERR, "ns_handle_closure", "Attempt to close outside of event loop thread %" NSPRIu64 " for fd=%d\n",
			c->c_connid, c->c_sd);
		return;
	}
#endif
	PR_EnterMonitor(c->c_mutex);
	connection_release_nolock_ext(c, 1); /* release ref acquired for event framework */
	PR_ASSERT(c->c_ns_close_jobs == 1); /* should be exactly 1 active close job - this one */
	c->c_ns_close_jobs--; /* this job is processing closure */
	do_yield = ns_handle_closure_nomutex(c);
	PR_ExitMonitor(c->c_mutex);
	ns_job_done(job);
	if (do_yield) {
		/* closure not done - another reference still outstanding */
		/* yield thread after unlocking conn mutex */
		PR_Sleep(PR_INTERVAL_NO_WAIT); /* yield to allow other thread to release conn */
	}
	return;
}
#endif

/**
 * Schedule more I/O for this connection, or make sure that it
 * is closed in the event loop.
 */
void
ns_connection_post_io_or_closing(Connection *conn)
{
#ifdef ENABLE_NUNC_STANS
	struct timeval tv;

	if (!enable_nunc_stans) {
		return;
	}

	if (CONN_NEEDS_CLOSING(conn)) {
		/* there should only ever be 0 or 1 active closure jobs */
		PR_ASSERT((conn->c_ns_close_jobs == 0) || (conn->c_ns_close_jobs == 1));
		if (conn->c_ns_close_jobs) {
			slapi_log_err(SLAPI_LOG_CONNS, "ns_connection_post_io_or_closing", "Already a close "
				"job in progress on conn %" NSPRIu64 " for fd=%d\n", conn->c_connid, conn->c_sd);
			return;
		} else {
			/* just make sure we schedule the event to be closed in a timely manner */
			tv.tv_sec = 0;
			tv.tv_usec = slapd_wakeup_timer * 1000;
			conn->c_ns_close_jobs++; /* now 1 active closure job */
			connection_acquire_nolock_ext(conn, 1 /* allow acquire even when closing */); /* event framework now has a reference */
			ns_add_timeout_job(conn->c_tp, &tv, NS_JOB_TIMER,
					   ns_handle_closure, conn, NULL);
			slapi_log_err(SLAPI_LOG_CONNS, "ns_connection_post_io_or_closing", "post closure job "
				"for conn %" NSPRIu64 " for fd=%d\n", conn->c_connid, conn->c_sd);
			
		}
	} else {
		/* process event normally - wait for I/O until idletimeout */
		tv.tv_sec = conn->c_idletimeout;
		tv.tv_usec = 0;
#ifdef DEBUG
		PR_ASSERT(0 == connection_acquire_nolock(conn));
#else
		if (connection_acquire_nolock(conn) != 0) { /* event framework now has a reference */
			/* 
			 * This has already been logged as an error in ./ldap/servers/slapd/connection.c
			 * The error occurs when we get a connection in a closing state.
			 * For now we return, but there is probably a better way to handle the error case.
			 */
			return;
		}
#endif
		ns_add_io_timeout_job(conn->c_tp, conn->c_prfd, &tv,
				      NS_JOB_READ|NS_JOB_PRESERVE_FD,
				      ns_handle_pr_read_ready, conn, NULL);
		slapi_log_err(SLAPI_LOG_CONNS, "ns_connection_post_io_or_closing", "post I/O job for "
			"conn %" NSPRIu64 " for fd=%d\n", conn->c_connid, conn->c_sd);
	}
#endif
}

#ifdef ENABLE_NUNC_STANS
/* This function must be called without the thread flag, in the
 * event loop.  This function may free the connection.  This can
 * only be done in the event loop thread.
 */
void
ns_handle_pr_read_ready(struct ns_job_t *job)
{
	int maxthreads = config_get_maxthreadsperconn();
	Connection *c = (Connection *)ns_job_get_data(job);

	/* this function must be called from the event loop thread */
#ifdef DEBUG
	PR_ASSERT(0 == NS_JOB_IS_THREAD(ns_job_get_type(job)));
#else
    /* This doesn't actually confirm it's in the event loop thread, but it's a start */
	if (NS_JOB_IS_THREAD(ns_job_get_type(job)) != 0) {
		slapi_log_err(SLAPI_LOG_ERR, "ns_handle_pr_read_ready", "Attempt to handle read ready outside of event loop thread %" NSPRIu64 " for fd=%d\n",
			c->c_connid, c->c_sd);
		return;
	}
#endif

	PR_EnterMonitor(c->c_mutex);
	slapi_log_err(SLAPI_LOG_CONNS, "ns_handle_pr_read_ready", "activity on conn %" NSPRIu64 " for fd=%d\n",
		       c->c_connid, c->c_sd);
	/* if we were called due to some i/o event, see what the state of the socket is */
	if (slapi_is_loglevel_set(SLAPI_LOG_CONNS) && !NS_JOB_IS_TIMER(ns_job_get_output_type(job)) && c && c->c_sd) {
		/* check socket state */
		char buf[1];
		ssize_t rc = recv(c->c_sd, buf, sizeof(buf), MSG_PEEK);
		if (!rc) {
			slapi_log_err(SLAPI_LOG_CONNS, "ns_handle_pr_read_ready", "socket is closed conn"
				" %" NSPRIu64 " for fd=%d\n", c->c_connid, c->c_sd);
		} else if (rc > 0) {
			slapi_log_err(SLAPI_LOG_CONNS, "ns_handle_pr_read_ready", "socket read data available"
				" for conn %" NSPRIu64 " for fd=%d\n", c->c_connid, c->c_sd);
		} else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			slapi_log_err(SLAPI_LOG_CONNS, "ns_handle_pr_read_ready", "socket has no data available"
				" conn %" NSPRIu64 " for fd=%d\n", c->c_connid, c->c_sd);
		} else {
			slapi_log_err(SLAPI_LOG_CONNS, "ns_handle_pr_read_ready", "socket has error [%d] "
				"conn %" NSPRIu64 " for fd=%d\n", errno, c->c_connid, c->c_sd);
		}
	}
	connection_release_nolock_ext(c, 1); /* release ref acquired when job was added */
	if (CONN_NEEDS_CLOSING(c)) {
		ns_handle_closure_nomutex(c);
	} else if (NS_JOB_IS_TIMER(ns_job_get_output_type(job))) {
		/* idle timeout */
		disconnect_server_nomutex_ext(c, c->c_connid, -1,
					      SLAPD_DISCONNECT_IDLE_TIMEOUT, EAGAIN,
				              0 /* do not schedule closure, do it next */);
		ns_handle_closure_nomutex(c);
	} else if ((connection_activity(c, maxthreads)) == -1) {
		/* This might happen as a result of
		 * trying to acquire a closing connection
		 */
		slapi_log_err(SLAPI_LOG_ERR, "ns_handle_pr_read_ready", "connection_activity: abandoning"
			" conn %" NSPRIu64 " as fd=%d is already closing\n", c->c_connid, c->c_sd);
		/* The call disconnect_server should do nothing,
		 * as the connection c should be already set to CLOSING */
		disconnect_server_nomutex_ext(c, c->c_connid, -1,
				              SLAPD_DISCONNECT_POLL, EPIPE,
				              0 /* do not schedule closure, do it next */);
		ns_handle_closure_nomutex(c);
	} else {
		slapi_log_err(SLAPI_LOG_CONNS, "ns_handle_pr_read_ready", "queued conn %" NSPRIu64 " for fd=%d\n",
			       c->c_connid, c->c_sd);
	}
	PR_ExitMonitor(c->c_mutex);
	ns_job_done(job);
	return;
}
#endif

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
 */
static int
slapd_poll( void *handle, int output )
{
    int rc;
    int ioblock_timeout = config_get_ioblocktimeout();
    struct POLL_STRUCT	pr_pd;
    PRIntervalTime	timeout = PR_MillisecondsToInterval(ioblock_timeout);

    pr_pd.fd = (PRFileDesc *)handle;
    pr_pd.in_flags = output ? PR_POLL_WRITE : PR_POLL_READ;
    pr_pd.out_flags = 0;
    rc = POLL_FN(&pr_pd, 1, timeout);

    if (rc < 0) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "slapd_poll",
        	"(%d) - %s error %d (%s)\n",
            (int)(uintptr_t)handle, SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
        if ( prerr == PR_PENDING_INTERRUPT_ERROR ||
             SLAPD_PR_WOULD_BLOCK_ERROR(prerr))
        {
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
#if defined(USE_OPENLDAP)
static int
write_function( int ignore, void *buffer, int count, void *handle )
#else
static int
write_function( int ignore, const void *buffer, int count, struct lextiof_socket_private *handle )
#endif
{
    int  sentbytes = 0;
    int     bytes;
    int fd = PR_FileDesc2NativeHandle((PRFileDesc *)handle);

    if (handle == SLAPD_INVALID_SOCKET) {
        PR_SetError(PR_NOT_SOCKET_ERROR, EBADF);
    } else {
        while (1) {
            if (slapd_poll(handle, SLAPD_POLLOUT) < 0) { /* error */
                break;
            }
            bytes = PR_Write((PRFileDesc *)handle, (char *)buffer + sentbytes,
                             count - sentbytes); 
            if (bytes > 0) {
                sentbytes += bytes;
            } else if (bytes < 0) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_CONNS, "write_function", "PR_Write(%d) "
                          SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          fd, prerr, slapd_pr_strerror( prerr ));
                if ( !SLAPD_PR_WOULD_BLOCK_ERROR(prerr)) {
                    if (prerr != PR_CONNECT_RESET_ERROR) {
                        /* 'TCP connection reset by peer': no need to log */
                        slapi_log_err(SLAPI_LOG_ERR, "write_function", "PR_Write(%d) "
                                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                                  fd, prerr, slapd_pr_strerror( prerr ));
                    }
                    if (sentbytes < count) {
                        slapi_log_err(SLAPI_LOG_CONNS,
                                  "write_function", "PR_Write(%d) - wrote only %d bytes (expected %d bytes) - 0 (EOF)\n", /* disconnected */
                                  fd, sentbytes, count);
                    }
                    break;		/* fatal error */
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

#if defined(USE_OPENLDAP)
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
openldap_io_ctrl(Sockbuf_IO_Desc *sbiod, int opt, void *arg)
{
	PR_ASSERT(0); /* not sure if this is needed */
	return -1;
}

static int 
openldap_io_close(Sockbuf_IO_Desc *sbiod)
{
	return 0; /* closing done in connection_cleanup() */
}

static Sockbuf_IO openldap_sockbuf_io = {
	openldap_io_setup, /* sbi_setup */
	NULL, /* sbi_remove */
	openldap_io_ctrl, /* sbi_ctrl */
	openldap_read_function, /* sbi_read */ /* see connection.c */
	openldap_write_function, /* sbi_write */
	openldap_io_close /* sbi_close */
};

#endif /* USE_OPENLDAP */


int connection_type = -1; /* The type number assigned by the Factory for 'Connection' */

void
daemon_register_connection()
{
    if(connection_type==-1)
	{
	    /* The factory is given the name of the object type, in
		 * return for a type handle. Whenever the object is created
		 * or destroyed the factory is called with the handle so
		 * that it may call the constructors or destructors registered
		 * with it.
		 */
        connection_type= factory_register_type(SLAPI_EXT_CONNECTION,offsetof(Connection,c_extension));
	}
}

#if defined(ENABLE_LDAPI)
int
slapd_identify_local_user(Connection *conn)
{
	int ret = -1;
	uid_t uid = 0;
	gid_t gid = 0;
	conn->c_local_valid = 0;

	if(0 == slapd_get_socket_peer(conn->c_prfd, &uid, &gid))
	{
		conn->c_local_uid = uid;
		conn->c_local_gid = gid;
		conn->c_local_valid = 1;

		ret = 0;
	}

	return ret;
}

#if defined(ENABLE_AUTOBIND)
int
slapd_bind_local_user(Connection *conn)
{
	int ret = -1;
	uid_t uid = conn->c_local_uid;
	gid_t gid = conn->c_local_gid;

	if (!conn->c_local_valid)
	{
		goto bail;
	}

	/* observe configuration for auto binding */
	/* bind at all? */
	if(config_get_ldapi_bind_switch())
	{
		/* map users to a dn
		   root may also map to an entry
		*/

		/* require real entry? */
		if(config_get_ldapi_map_entries())
		{
			/* get uid type to map to (e.g. uidNumber) */
			char *utype = config_get_ldapi_uidnumber_type();
			/* get gid type to map to (e.g. gidNumber) */
			char *gtype = config_get_ldapi_gidnumber_type();
			/* get base dn for search */
			char *base_dn = config_get_ldapi_search_base_dn();

			/* search vars */
			Slapi_PBlock *search_pb = 0;
			Slapi_Entry **entries = 0;
			int result;

			/* filter manipulation vars */
			char *one_type = 0;
			char *filter_tpl = 0;
			char *filter = 0;

			/* create filter, matching whatever is given */
			if(utype && gtype)
			{
				filter_tpl = "(&(%s=%u)(%s=%u))";
			}
			else
			{
				if(utype || gtype)
				{
					filter_tpl = "(%s=%u)";
					if(utype)
						one_type = utype;
					else
						one_type = gtype;
				}
				else
				{
					goto entry_map_free;
				}
			}

			if(one_type)
			{
				if(one_type == utype)
					filter = slapi_ch_smprintf(filter_tpl,
						utype, uid);
				else
					filter = slapi_ch_smprintf(filter_tpl,
						gtype, gid);
			}
			else
			{
				filter = slapi_ch_smprintf(filter_tpl, 
					utype, uid, gtype, gid);
			}

			/* search for single entry matching types */
			search_pb = slapi_pblock_new();

			slapi_search_internal_set_pb(
				search_pb, 
				base_dn,
				LDAP_SCOPE_SUBTREE,
               			filter, 
				NULL, 0, NULL, NULL, 
				(void*)plugin_get_default_component_id(), 
				0);

			slapi_search_internal_pb(search_pb);
			slapi_pblock_get(
				search_pb,
				SLAPI_PLUGIN_INTOP_RESULT, 
				&result);
			if(LDAP_SUCCESS == result)
				 slapi_pblock_get(
					search_pb,
					SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
					&entries);

			if(entries)
			{
				/* zero or multiple entries fail */
				if(entries[0] && 0 == entries[1])
				{
					/* observe account locking */
					ret = slapi_check_account_lock(
						0,  /* pb not req */
						entries[0],
						0, /* no response control */
						0, /* don't check password policy */
						0  /* don't send ldap result */
						);

					if(0 == ret)
					{
						char *auth_dn = slapi_ch_strdup(
							slapi_entry_get_ndn(
								entries[0]));

						auth_dn = slapi_dn_normalize(
							auth_dn);

						bind_credentials_set_nolock(
							conn,
							SLAPD_AUTH_OS,
							auth_dn,
							NULL, NULL,
							NULL , entries[0]);

						ret = 0;
					}
				}
			}

entry_map_free:
			/* auth_dn consumed by bind creds set */
			slapi_free_search_results_internal(search_pb);
			slapi_pblock_destroy(search_pb);
			slapi_ch_free_string(&filter);
			slapi_ch_free_string(&utype);
			slapi_ch_free_string(&gtype);
			slapi_ch_free_string(&base_dn);
		}

		if(ret && 0 == uid)
		{
			/* map unix root (uidNumber:0)? */
			char *root_dn = config_get_ldapi_root_dn();

			if(root_dn)
			{
				Slapi_DN *edn = slapi_sdn_new_dn_byref(
					 slapi_dn_normalize(root_dn));
				Slapi_Entry *e = 0;

				/* root might be locked too! :) */
				ret =  slapi_search_internal_get_entry(
					edn, 0,
        				&e,
					(void*)plugin_get_default_component_id()

					);	

				if(0 == ret && e)
				{
					ret = slapi_check_account_lock(
						0, /* pb not req */
						e,
						0, /* no response control */
						0, /* don't check password policy */
						0  /* don't send ldap result */
						);

					if(1 == ret)
						/* sorry root,
						 * just not cool enough
						*/
						goto root_map_free;
				}

				/* it's ok not to find the entry,
				 * dn doesn't have to have an entry
				 * e.g. cn=Directory Manager
				 */
				bind_credentials_set_nolock( 
					conn, SLAPD_AUTH_OS, root_dn,
					NULL, NULL, NULL , e);

root_map_free:
				/* root_dn consumed by bind creds set */
				slapi_sdn_free(&edn);
				slapi_entry_free(e);
				ret = 0;
			}
		}

#if defined(ENABLE_AUTO_DN_SUFFIX)
		if(ret) 
		{
			/* create phony auth dn? */
			char *base = config_get_ldapi_auto_dn_suffix();
			if(base)
			{
				char *tpl = "gidNumber=%u+uidNumber=%u,";
				int len = 
				strlen(tpl) + 
					strlen(base) +
					51 /* uid,gid,null,w/padding */
					;
				char *dn_str = (char*)slapi_ch_malloc(
					len);
				char *auth_dn = (char*)slapi_ch_malloc(
					len);

				dn_str[0] = 0;
				strcpy(dn_str, tpl);
				strcat(dn_str, base);

				sprintf(auth_dn, dn_str, gid, uid);

				auth_dn = slapi_dn_normalize(auth_dn);

				bind_credentials_set_nolock(
					conn,
					SLAPD_AUTH_OS,
					auth_dn,
                               		NULL, NULL, NULL , NULL);

				/* auth_dn consumed by bind creds set */
				slapi_ch_free_string(&dn_str);
				slapi_ch_free_string(&base);
				ret = 0;
			}
		}
#endif
	}

bail:
	/* if all fails, the peer is anonymous */
	if(conn->c_dn)
	{
		/* log the auto bind */
		slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " AUTOBIND dn=\"%s\"\n", conn->c_connid, conn->c_dn);
	}

	return ret;
}	
#endif /* ENABLE_AUTOBIND */
#endif /* ENABLE_LDAPI */

void
handle_closed_connection(Connection *conn)
{
#ifdef USE_OPENLDAP
	ber_sockbuf_remove_io(conn->c_sb, &openldap_sockbuf_io,	LBER_SBIOD_LEVEL_PROVIDER);
#endif
}

/* NOTE: this routine is not reentrant */
static int
handle_new_connection(Connection_Table *ct, int tcps, PRFileDesc *pr_acceptfd, int secure, int local, Connection **newconn)
{
	int ns = 0;
	Connection *conn = NULL;
	/*	struct sockaddr_in	from;*/
	PRNetAddr from;
	PRFileDesc *pr_clonefd = NULL;
	ber_len_t maxbersize;
	slapdFrontendConfig_t *fecfg = getFrontendConfig();

	if (newconn) {
		*newconn = NULL;
	}
	memset(&from, 0, sizeof(from)); /* reset to nulls so we can see what was set */
	if ( (ns = accept_and_configure( tcps, pr_acceptfd, &from,
		sizeof(from), secure, local, &pr_clonefd)) == SLAPD_INVALID_SOCKET ) {
		return -1;
	}

	/* get a new Connection from the Connection Table */
	conn= connection_table_get_connection(ct,ns);
	if(conn==NULL)
	{
		PR_Close(pr_acceptfd);
		return -1;
	}
	PR_EnterMonitor(conn->c_mutex);

	/*
	 * Set the default idletimeout and the handle.  We'll update c_idletimeout
	 * after each bind so we can correctly set the resource limit.
	 */
	conn->c_idletimeout = fecfg->idletimeout;
	conn->c_idletimeout_handle = idletimeout_reslimit_handle;
	conn->c_sd = ns;
	conn->c_prfd = pr_clonefd;
	conn->c_flags &= ~CONN_FLAG_CLOSING;

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

#if defined(USE_OPENLDAP)
	ber_sockbuf_add_io( conn->c_sb, &openldap_sockbuf_io,
						LBER_SBIOD_LEVEL_PROVIDER, conn );
#else /* !USE_OPENLDAP */
    {
		struct lber_x_ext_io_fns func_pointers;
		memset(&func_pointers, 0, sizeof(func_pointers));
		func_pointers.lbextiofn_size = LBER_X_EXTIO_FNS_SIZE;
		func_pointers.lbextiofn_read = NULL; /* see connection_read_function */
		func_pointers.lbextiofn_write = write_function;
		func_pointers.lbextiofn_writev = NULL;
		func_pointers.lbextiofn_socket_arg = (struct lextiof_socket_private *) pr_clonefd;
		ber_sockbuf_set_option(conn->c_sb,
		                       LBER_SOCKBUF_OPT_EXT_IO_FNS, &func_pointers);
	}
#endif /* !USE_OPENLDAP */
	maxbersize = config_get_maxbersize();
#if defined(USE_OPENLDAP)
	ber_sockbuf_ctrl( conn->c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &maxbersize );
#endif
	if( secure && config_get_SSLclientAuth() != SLAPD_SSLCLIENTAUTH_OFF ) { 
	    /* Prepare to handle the client's certificate (if any): */
		int rv;

		rv = slapd_ssl_handshakeCallback (conn->c_prfd, (void*)handle_handshake_done, conn);
        
		if (rv < 0) {
			PRErrorCode prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR, "handle_new_connection", "SSL_HandshakeCallback() %d "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					rv, prerr, slapd_pr_strerror( prerr ));
		}
		rv = slapd_ssl_badCertHook (conn->c_prfd, (void*)handle_bad_certificate, conn);

		if (rv < 0) {
			PRErrorCode prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR, "handle_new_connection", "SSL_BadCertHook(%i) %i "
					SLAPI_COMPONENT_NAME_NSPR " error %d\n",
					conn->c_sd, rv, prerr);
		}
	}

	connection_reset(conn, ns, &from, sizeof(from), secure);

	/* Call the plugin extension constructors */
	conn->c_extension = factory_create_extension(connection_type,conn,NULL /* Parent */);

#if defined(ENABLE_LDAPI)
	/* ldapi */
	if( local )
	{
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
	if ( conn != NULL && conn->c_next == NULL && conn->c_prev == NULL )
	{
		/* Now give the new connection to the connection code */
		connection_table_move_connection_on_to_active_list(the_connection_table,conn);
	}

	PR_ExitMonitor(conn->c_mutex);

	g_increment_current_conn_count();

	if (newconn) {
		*newconn = conn;
	}
	return 0;
}

#ifdef ENABLE_NUNC_STANS
static void
ns_handle_new_connection(struct ns_job_t *job)
{
	int rc;
	Connection *c = NULL;
	listener_info *li = (listener_info *)ns_job_get_data(job);

	/* only accept new connections if we have enough fds, more than
	 * the number of reserved descriptors
	 */
	if ((li->ct->size - g_get_current_conn_count())
	     <= config_get_reservedescriptors()) {
		/* too many open fds - Just return, and hope next time is better. */
		slapi_log_error(SLAPI_LOG_FATAL, "ns_handle_new_connection", "Insufficient Reserve FD: File Descriptor exhaustion has occured! Connections will be silently dropped!\n");
		return;
	}

	rc = handle_new_connection(li->ct, SLAPD_INVALID_SOCKET, li->listenfd, li->secure, li->local, &c);
	if (rc) {
		PRErrorCode prerr = PR_GetError();
		if (PR_PROC_DESC_TABLE_FULL_ERROR == prerr) {
			/* too many open fds - shut off this listener - when an fd is
			 * closed, try to resume this listener.
			 *
			 * WARNING: This generates a lot of false negatives. Why?
			 */
			slapi_log_error(SLAPI_LOG_FATAL, "ns_handle_new_connection", "PR_PROC_DESC_TABLE_FULL_ERROR: File Descriptor exhaustion has occured! Connections will be silently dropped!\n");
		} else {
			slapi_log_err(SLAPI_LOG_FATAL, "ns_handle_new_connection", "Error accepting new connection listenfd=%d [%d:%s]\n",
				PR_FileDesc2NativeHandle(li->listenfd), prerr,
				slapd_pr_strerror(prerr));
		}
		return;
	}
	c->c_tp = ns_job_get_tp(job);
	/* This originally just called ns_handle_pr_read_ready directly - however, there
	 * are certain cases where accept() will return a file descriptor that is not
	 * immediately available for reading - this would cause the poll() in
	 * connection_read_operation() to be hit - it seemed to perform better when
	 * that poll() was avoided, even at the expense of putting this new fd back
	 * in nunc-stans to poll for read ready.
	 */
	ns_connection_post_io_or_closing(c);
	return;
}
#endif

static int init_shutdown_detect(void)
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
    (void)sigemptyset( &proc_mask );
    rc = pthread_sigmask( SIG_SETMASK, &proc_mask, NULL );
    slapi_log_err(SLAPI_LOG_TRACE, "init_shutdown_detect", "%s \n", 
	       rc ? "Failed to reset signal mask":"....Done (signal mask reset)!!");
  }
  
#if defined ( HPUX10 )
    PR_CreateThread ( PR_USER_THREAD,
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
	(void) SIGNAL( SIGPIPE, SIG_IGN );
	(void) SIGNAL( SIGCHLD, slapd_wait4child );
#ifndef LINUX
	/* linux uses USR1/USR2 for thread synchronization, so we aren't
	 * allowed to mess with those.
	 */
	(void) SIGNAL( SIGUSR1, slapd_do_nothing );
	(void) SIGNAL( SIGUSR2, set_shutdown );
#endif
	if (!enable_nunc_stans) {
		(void) SIGNAL( SIGTERM, set_shutdown );
		(void) SIGNAL( SIGHUP,  set_shutdown );
	}
#endif /* HPUX */
	return 0;
}

static void
unfurl_banners(Connection_Table *ct,daemon_ports_t *ports, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix)
{
	slapdFrontendConfig_t	*slapdFrontendConfig = getFrontendConfig();
	char					addrbuf[ 256 ];
	int			isfirsttime = 1;

	if ( ct->size <= slapdFrontendConfig->reservedescriptors ) {
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
		    ct->size, slapdFrontendConfig->reservedescriptors, slapdFrontendConfig->reservedescriptors );
		exit( 1 );
	}

	/*
	 * This final startup message gives a definite signal to the admin
	 * program that the server is up.  It must contain the string
	 * "slapd started." because some of the administrative programs
	 * depend on this.  See ldap/admin/lib/dsalib_updown.c.
	 */
	if ( n_tcps != NULL ) { /* standard LDAP */
		PRNetAddr   **nap = NULL;

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

	if ( s_tcps != NULL ) { /* LDAP over SSL; separate port */
		PRNetAddr   **sap = NULL;

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
	if ( i_unix != NULL ) {                                 /* LDAPI */
		PRNetAddr   **iap = ports->i_listenaddr;

		slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon",
			"%sListening on %s for LDAPI requests\n", isfirsttime?"slapd started.  ":"",
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
	if ( (fp = fopen( get_pid_file(), "w" )) != NULL ) {
		fprintf( fp, "%d\n", getpid() );
		fclose( fp );
		if ( chmod(get_pid_file(), S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH) != 0 ) {
			unlink(get_pid_file());
	 	} else {
			return 0;
		}
	}
	return -1;
}

static void
set_shutdown (int sig)
{ 
    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
#if 0
    slapi_log_err(SLAPI_LOG_INFO, "slapd_daemon", "slapd got shutdown signal\n");
#endif
	g_set_shutdown( SLAPI_SHUTDOWN_SIGNAL );
#ifndef LINUX
	/* don't mess with USR1/USR2 on linux, used by libpthread */
	(void) SIGNAL( SIGUSR2, set_shutdown );
#endif
	(void) SIGNAL( SIGTERM, set_shutdown );
	(void) SIGNAL( SIGHUP,  set_shutdown );
}

#ifdef ENABLE_NUNC_STANS
static void
ns_set_user(struct ns_job_t *job)
{
    /* This literally does nothing. We intercept user signals (USR1, USR2) */
    /* Could be good for a status output, or an easter egg. */
    return;
}

static void
ns_set_shutdown(struct ns_job_t *job)
{
    /* Is there a way to make this a bit more atomic? */
    /* I think NS protects this by only executing one signal job at a time */

    if (g_get_shutdown() == 0) {
        g_set_shutdown(SLAPI_SHUTDOWN_SIGNAL);

        /* Stop all the long running jobs */
        for (size_t i = 0; i < listeners; ++i) {
            ns_job_done(listener_idxs[i].ns_job);
            listener_idxs[i].ns_job = NULL;
        }

        /* Signal all the worker threads to stop */
        ns_thrpool_shutdown(ns_job_get_tp(job));
    }
}
#endif

#ifndef LINUX
void
slapd_do_nothing (int sig)
{
    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
#if 0
	slapi_log_err(SLAPI_LOG_TRACE, "slapd_daemon", "slapd got SIGUSR1\n");
#endif
	(void) SIGNAL( SIGUSR1, slapd_do_nothing );

#if 0
	/*
	 * Actually do a little more: dump the conn struct and 
	 * send it to a tmp file
	 */
	connection_table_dump(connection_table);
#endif
}
#endif   /* LINUX */

void
slapd_wait4child(int sig)
{
        WAITSTATUSTYPE     status;

    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
#if 0
        slapi_log_err(SLAPI_LOG_ARGS, "slapd_daemon", "listener: catching SIGCHLD\n");
#endif
#ifdef USE_WAITPID
        while (waitpid ((pid_t) -1, 0, WAIT_FLAGS) > 0)
#else /* USE_WAITPID */
        while ( wait3( &status, WAIT_FLAGS, 0 ) > 0 )
#endif /* USE_WAITPID */
                ;       /* NULL */

        (void) SIGNAL( SIGCHLD, slapd_wait4child );
}

static PRFileDesc **
createprlistensockets(PRUint16 port, PRNetAddr **listenaddr,
		int secure, int local)
{
	PRFileDesc			**sock;
	PRNetAddr			sa_server;
	PRErrorCode			prerr = 0;
	PRSocketOptionData	pr_socketoption;
	char				addrbuf[ 256 ];
	char				*logname = "createprlistensockets";
	int					sockcnt = 0;
	int					socktype;
	char				*socktype_str = NULL;
	PRNetAddr			**lap;
	int					i;

	if (!port) goto suppressed;

	PR_ASSERT( listenaddr != NULL );

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

		if ( PR_SetSocketOption(sock[i], &pr_socketoption ) == PR_FAILURE) {
			prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR, logname,
				"PR_SetSocketOption(PR_SockOpt_Reuseaddr) failed: %s error %d (%s)\n",
		    	SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror( prerr ));
			goto failed;	
		}

		/* set up listener address, including port */
		memcpy(&sa_server, *lap, sizeof(sa_server));

		if(!local)
			PRLDAP_SET_PORT( &sa_server, port );

		if ( PR_Bind(sock[i], &sa_server) == PR_FAILURE) {
			prerr = PR_GetError();
			if(!local)
			{
				slapi_log_err(SLAPI_LOG_ERR, logname,
					"PR_Bind() on %s port %d failed: %s error %d (%s)\n",
					netaddr2string(&sa_server, addrbuf, sizeof(addrbuf)), port,
					SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
			}
#if defined(ENABLE_LDAPI)
			else
			{
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
	if(local) { /* ldapi */
		if(chmod((*listenaddr)->local.path,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH))
		{
			slapi_log_err(SLAPI_LOG_ERR, logname, "err: %d", errno);
		}
	}
#endif /* ENABLE_LDAPI */

	return( sock );

failed:
	exit( 1 );

suppressed:
	return (PRFileDesc **)-1;
}  /* createprlistensockets */


/*
 * Initialize the *addr structure based on listenhost.
 * Returns: 0 if successful and -1 if not (after logging an error message).
 */
int
slapd_listenhost2addr(const char *listenhost, PRNetAddr ***addr)
{
	char		*logname = "slapd_listenhost2addr";
	PRErrorCode	prerr = 0;
	int			rval = 0;
	PRNetAddr	*netaddr = (PRNetAddr *)slapi_ch_calloc(1, sizeof(PRNetAddr));

	PR_ASSERT( addr != NULL );
	*addr = NULL;

	if (NULL == listenhost) {
		/* listen on all interfaces */
		if ( PR_SUCCESS != PR_SetNetAddr(PR_IpAddrAny, PR_AF_INET6, 0, netaddr)) {
			prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR, logname,
					"PR_SetNetAddr(PR_IpAddrAny) failed - %s error %d (%s)\n",
					SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
			rval = -1;
			slapi_ch_free ((void**)&netaddr);
		}
		*addr = (PRNetAddr **)slapi_ch_calloc(2, sizeof (PRNetAddr *));
		(*addr)[0] = netaddr;
	} else if (PR_SUCCESS == PR_StringToNetAddr(listenhost, netaddr)) {
		/* PR_StringNetAddr newer than NSPR v4.6.2 supports both IPv4&v6 */; 
		*addr = (PRNetAddr **)slapi_ch_calloc(2, sizeof (PRNetAddr *));
		(*addr)[0] = netaddr;
	} else {
		PRAddrInfo *infop = PR_GetAddrInfoByName( listenhost,
						PR_AF_UNSPEC, (PR_AI_ADDRCONFIG|PR_AI_NOCANONNAME) );
		if ( NULL != infop ) {
			void *iter = NULL;
			int addrcnt = 0;
			int i = 0;
			memset( netaddr, 0, sizeof( PRNetAddr ));
			/* need to count the address, first */
			while ( (iter = PR_EnumerateAddrInfo( iter, infop, 0, netaddr ))
							!= NULL ) {
				addrcnt++;
			}
			if ( 0 == addrcnt ) {
				slapi_log_err(SLAPI_LOG_ERR, logname,
					"PR_EnumerateAddrInfo for %s failed - %s error %d (%s)\n",
					listenhost, SLAPI_COMPONENT_NAME_NSPR, prerr,
					slapd_pr_strerror(prerr));
				rval = -1;
			} else {
				char **strnetaddrs = NULL;
				*addr = (PRNetAddr **)slapi_ch_calloc(addrcnt + 1, sizeof (PRNetAddr *));
				iter = NULL; /* from the beginning */
				memset( netaddr, 0, sizeof( PRNetAddr ));
				for  ( i = 0; i < addrcnt; i++ ) {
					char abuf[256];
					char *abp = abuf;
					iter = PR_EnumerateAddrInfo( iter, infop, 0, netaddr );
					if ( NULL == iter ) {
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
						               "[%s]\n", abuf, abp);
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
			PR_FreeAddrInfo( infop );
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
	const char	*retstr;

	if (NULL == addr || PR_IsNetAddrType(addr, PR_IpAddrAny)) {
		retstr = "All Interfaces";
	} else if (PR_IsNetAddrType(addr, PR_IpAddrLoopback)) {
		if ( addr->raw.family == PR_AF_INET6 &&
					!PR_IsNetAddrType(addr, PR_IpAddrV4Mapped)) {
			retstr = "IPv6 Loopback";
		} else {
			retstr = "Loopback";
		}
	} else if (PR_SUCCESS == PR_NetAddrToString( addr, addrbuf, addrbuflen)) {
		if (0 == strncmp( addrbuf, "::ffff:", 7 )) {
			/* IPv4 address mapped into IPv6 address space */
			retstr = addrbuf + 7;
		} else {
			/* full blown IPv6 address */
			retstr = addrbuf;
		}
	} else {	/* punt */
		retstr = "address conversion failed";
	}

	return(retstr);
}


static int
createsignalpipe( void )
{
	if (enable_nunc_stans) {
		return( 0 );
	}
	if ( PR_CreatePipe( &signalpipe[0], &signalpipe[1] ) != 0 ) {
		PRErrorCode prerr = PR_GetError();
		slapi_log_err(SLAPI_LOG_ERR, "createsignalpipe",
			"PR_CreatePipe() failed, %s error %d (%s)\n",
			SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
		return( -1 );
	}
	writesignalpipe = PR_FileDesc2NativeHandle(signalpipe[1]);
	readsignalpipe = PR_FileDesc2NativeHandle(signalpipe[0]);
	if(fcntl(writesignalpipe, F_SETFD, O_NONBLOCK) == -1){
		slapi_log_err(SLAPI_LOG_ERR,"createsignalpipe",
			"Failed to set FD for write pipe (%d).\n", errno);
	}
	if(fcntl(readsignalpipe, F_SETFD, O_NONBLOCK) == -1){
		slapi_log_err(SLAPI_LOG_ERR,"createsignalpipe",
			"Failed to set FD for read pipe (%d).\n", errno);
	}
	return( 0 );
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
    sigset_t    caught_signals;
    int         sig;
 
    sigemptyset( &caught_signals );
 
    while ( !g_get_shutdown() ) {
 
	  /* Set the signals we're interested in catching */
        sigaddset( &caught_signals, SIGUSR1 );
        sigaddset( &caught_signals, SIGCHLD );
        sigaddset( &caught_signals, SIGUSR2 );
        sigaddset( &caught_signals, SIGTERM );
        sigaddset( &caught_signals, SIGHUP );
 
        (void)sigprocmask( SIG_BLOCK, &caught_signals, NULL );
 
        if (( sig = sigwait( &caught_signals )) < 0 ) {
            slapi_log_err(SLAPI_LOG_ERR, "catch_signals", "sigwait returned -1\n");
            continue;
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "catch_signals", "detected signal %d\n", sig);
            switch ( sig ) {
            case SIGUSR1:
                continue;       /* ignore SIGUSR1 */
            case SIGUSR2:       /* fallthrough */
            case SIGTERM:       /* fallthrough */
            case SIGHUP:
                g_set_shutdown( SLAPI_SHUTDOWN_SIGNAL );
                return NULL;
            case SIGCHLD:
                slapd_wait4child( sig );
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
get_configured_connection_table_size(void)
{
	int size = config_get_conntablesize();
	int maxdesc = config_get_maxdescriptors();

	/*
	 * Cap the table size at nsslapd-maxdescriptors.
	 */
	if ( maxdesc >= 0 && size > maxdesc ) {
		size = maxdesc;
	}

	return size;
}

PRFileDesc * get_ssl_listener_fd()
{
  PRFileDesc * listener;

  listener = the_connection_table->fd[the_connection_table->s_tcps].fd;

  return listener;
}

int configure_pr_socket( PRFileDesc **pr_socket, int secure, int local )
{
	int ns = 0;
	int reservedescriptors = config_get_reservedescriptors();
	int enable_nagle = config_get_nagle();

	PRSocketOptionData pr_socketoption;
  
	ns = PR_FileDesc2NativeHandle( *pr_socket );
	
	/*
	 * Some OS or third party libraries may require that low
	 * numbered file descriptors be available, e.g., the DNS resolver
	 * library on most operating systems. Therefore, we try to
	 * replace the file descriptor returned by accept() with a
	 * higher numbered one.  If this fails, we log an error and
	 * continue (not considered a truly fatal error).
	 */
	if ( reservedescriptors > 0 && ns < reservedescriptors ) {
		int		newfd = fcntl( ns, F_DUPFD, reservedescriptors );

		if ( newfd > 0 ) {
			PRFileDesc	*nspr_layer_fd = PR_GetIdentitiesLayer( *pr_socket,
															PR_NSPR_IO_LAYER );
			if ( NULL == nspr_layer_fd ) {
				slapi_log_err(SLAPI_LOG_ERR, "configure_pr_socket",
						"Unable to move socket file descriptor %d above %d:"
						" PR_GetIdentitiesLayer( %p, PR_NSPR_IO_LAYER )"
						" failed\n", ns, reservedescriptors, *pr_socket );
				close( newfd );	/* can't fix things up in NSPR -- close copy */
			} else {
				PR_ChangeFileDescNativeHandle( nspr_layer_fd, newfd );
				close( ns );	/* dup succeeded -- close the original */
				ns = newfd;
			}
		} else {
			int oserr = errno;
			slapi_log_err(SLAPI_LOG_ERR, "configure_pr_socket",
				"Unable to move socket file descriptor %d above %d:"
				" OS error %d (%s)\n", ns, reservedescriptors, oserr,
				slapd_system_strerror( oserr ) );
		}
	}

	/* Set keep_alive to keep old connections from lingering */
	pr_socketoption.option = PR_SockOpt_Keepalive;
	pr_socketoption.value.keep_alive = 1;
	if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE ) {
		PRErrorCode prerr = PR_GetError();
		slapi_log_err(SLAPI_LOG_ERR,
				"configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_Keepalive failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror(prerr));
	}

	if ( secure ) {
		pr_socketoption.option = PR_SockOpt_Nonblocking;
		pr_socketoption.value.non_blocking = 0;
		if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE ) {
			PRErrorCode prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR,
					"configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_Nonblocking) failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_pr_strerror(prerr));
		}
	} else {
		/* We always want to have non-blocking I/O */
		pr_socketoption.option = PR_SockOpt_Nonblocking;
		pr_socketoption.value.non_blocking = 1;
		if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE ) {
			PRErrorCode prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR,
				"configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_Nonblocking) failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror(prerr));
		}
	} /* else (secure) */

	if ( !enable_nagle && !local ) {
		 pr_socketoption.option = PR_SockOpt_NoDelay;
		 pr_socketoption.value.no_delay = 1;
		 if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE) {
			PRErrorCode prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR,
				   "configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_NoDelay) failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_pr_strerror( prerr ));
		 }
	} else if( !local) {
		pr_socketoption.option = PR_SockOpt_NoDelay;
		pr_socketoption.value.no_delay = 0;
		if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE) {
			PRErrorCode prerr = PR_GetError();
			slapi_log_err(SLAPI_LOG_ERR,
				"configure_pr_socket", "PR_SetSocketOption(PR_SockOpt_NoDelay) failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror( prerr ));
		 }
	} /* else (!enable_nagle) */
	
	return ns;
}

void configure_ns_socket( int * ns )
{

	int enable_nagle = config_get_nagle();
	int on, rc;

#if defined(LINUX)
	/* On Linux we use TCP_CORK so we must enable nagle */
	enable_nagle = 1;
#endif

	/* set the nagle */
	if ( !enable_nagle ) {
		on = 1;
	} else {
		on = 0;
	}
	/* check for errors */
	if((rc = setsockopt( *ns, IPPROTO_TCP, TCP_NODELAY, (char * ) &on, sizeof(on) ) != 0)){
		slapi_log_err(SLAPI_LOG_ERR,"configure_ns_socket", "Failed to configure socket (%d).\n", rc);
	}

	return;
}


#ifdef RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS
/*
 * A function that uses the DNS resolver in a simple way.  This is only
 * used to ensure that the DNS resolver has opened its files, etc.
 * using low numbered file descriptors.
 */
static void
get_loopback_by_addr( void )
{
#ifdef GETHOSTBYADDR_BUF_T
    struct hostent		hp;
	GETHOSTBYADDR_BUF_T	hbuf;
#endif
    unsigned long	ipaddr;
    struct in_addr	ia;
    int				herrno, rc = 0;

    memset( (char *)&hp, 0, sizeof(hp));
    ipaddr = htonl( INADDR_LOOPBACK );
    (void) GETHOSTBYADDR( (char *)&ipaddr, sizeof( ipaddr ),
	    AF_INET, &hp, hbuf, sizeof(hbuf), &herrno );
}
#endif /* RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS */

void
disk_monitoring_stop(void)
{
	if ( disk_thread_p ) {
		PR_Lock( diskmon_mutex );
		PR_NotifyCondVar( diskmon_cvar );
		PR_Unlock( diskmon_mutex );
	}
}
