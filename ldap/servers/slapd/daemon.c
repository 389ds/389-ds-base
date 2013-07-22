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

#include <string.h>
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h> /* for getpid */
#include "proto-ntutil.h"
#include "ntslapdmessages.h"
#else
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
#include <mntent.h>
#endif
#include <time.h>
#include <signal.h>
#if defined(IRIX6_2) || defined(IRIX6_3)
#include <sys/param.h>
#endif
#if defined(_AIX)
#include <sys/select.h>
#include <sys/param.h>
#endif
#include <fcntl.h>
#define TCPLEN_T	int
#if !defined( _WIN32 )
#ifdef NEED_FILIO
#include <sys/filio.h>
#else /* NEED_FILIO */
#include <sys/ioctl.h>
#endif /* NEED_FILIO */
#endif /* !defined( _WIN32 ) */
/* for some reason, linux tty stuff defines CTIME */
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
#include <stdio.h>
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

/* Do we support timeout on socket send() ? */
int have_send_timeouts = 0;

PRFileDesc*		signalpipe[2];
static int writesignalpipe = SLAPD_INVALID_SOCKET;
static int readsignalpipe = SLAPD_INVALID_SOCKET;

static PRThread *disk_thread_p = NULL;
static PRCondVar *diskmon_cvar = NULL;
static PRLock *diskmon_mutex = NULL;
void disk_monitoring_stop();

#define FDS_SIGNAL_PIPE 0

typedef struct listener_info {
	int idx; /* index of this listener in the ct->fd array */
	PRFileDesc *listenfd; /* the listener fd */
	int secure;
	int local;
} listener_info;

#define SLAPD_POLL_LISTEN_READY(xxflagsxx) (xxflagsxx & PR_POLL_READ)

static int get_configured_connection_table_size();
#ifdef RESOLVER_NEEDS_LOW_FILE_DESCRIPTORS
static void get_loopback_by_addr( void );
#endif

#ifdef XP_WIN32
static int createlistensocket(unsigned short port, const PRNetAddr *listenaddr);
#endif
static PRFileDesc **createprlistensockets(unsigned short port,
	PRNetAddr **listenaddr, int secure, int local);
static const char *netaddr2string(const PRNetAddr *addr, char *addrbuf,
	size_t addrbuflen);
static void	set_shutdown (int);
static void setup_pr_read_pds(Connection_Table *ct, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix, PRIntn *num_to_read, listener_info *listener_idxs, int max_listeners);

#ifdef HPUX10
static void* catch_signals();
#endif

#if defined( _WIN32 )
HANDLE  hServDoneEvent = NULL;
#endif

static int createsignalpipe( void );

#if defined( _WIN32 )
/* Set an event to hook the NT Service termination */
void *slapd_service_exit_wait()
{
#if defined( PURIFYING )

#include <sys/types.h> 
#include <sys/stat.h>

	char module[_MAX_FNAME];
	char exit_file_name[_MAX_FNAME];
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];
	struct stat statbuf;

	memset( module, 0, sizeof( module ) );
	memset( exit_file_name, 0, sizeof( exit_file_name ) );

	GetModuleFileName(GetModuleHandle( NULL ), module, sizeof( module ) );

	_splitpath( module, drive, dir, fname, ext );

	PR_snprintf( exit_file_name, sizeof(exit_file_name), "%s%s%s", drive, dir, "exitnow.txt" );

    LDAPDebug( LDAP_DEBUG_ANY, "PURIFYING - Create %s to terminate the process.\n", exit_file_name, 0, 0 );

	while ( TRUE )
	{
		if( stat( exit_file_name, &statbuf ) < 0)
		{
			Sleep( 5000 );  /* 5 Seconds */
			continue;
		}
	    LDAPDebug( LDAP_DEBUG_ANY, "slapd shutting down immediately, "
		"\"%s\" exists - don't forget to delete it\n", exit_file_name, 0, 0 );
		g_set_shutdown( SLAPI_SHUTDOWN_SIGNAL );
		return NULL;
	}

#else /*  PURIFYING  */

	DWORD dwWait;
	char szDoneEvent[256];

	PR_snprintf(szDoneEvent, sizeof(szDoneEvent), "NS_%s", pszServerName);

	hServDoneEvent = CreateEvent( NULL,			// default security attributes (LocalSystem)
								  TRUE,			// manual reset event
								  FALSE,		// not-signalled
								  szDoneEvent );// named after the service itself.

    /*  Wait indefinitely until hServDoneEvent is signaled. */
    dwWait = WaitForSingleObject( hServDoneEvent,  // event object
								  INFINITE );      // wait indefinitely

	/* The termination event has been signalled, log this occurrence, and signal to exit. */
	ReportSlapdEvent( EVENTLOG_INFORMATION_TYPE, MSG_SERVER_SHUTDOWN_STARTING, 0, NULL );

	g_set_shutdown( SLAPI_SHUTDOWN_SIGNAL );
	return NULL;
#endif /* PURIFYING  */
}
#endif /* _WIN32 */

static char *
get_pid_file()
{
    return(pid_file);
}

static int daemon_configure_send_timeout(int s,size_t timeout /* Miliseconds*/)
{
	/* Currently this function is only good for NT, and expects the s argument to be a SOCKET */
#if defined(_WIN32)
	return setsockopt(
		s,
		SOL_SOCKET,
		SO_SNDTIMEO,
		(char*) &timeout,
		sizeof(timeout)
		);
#else
	return 0;
#endif
}

#if defined (_WIN32)
/* This function is a workaround for accept problem on NT. 
   Accept call fires on NT during syn scan even though the connection is not
   open. This causes a resource leak. For more details, see bug 391414.
   Experimentally, we determined that, in case of syn scan, the local     
   address is set to 0. This in undocumented and my change in the future
    
   The function returns 0 if this is normal connection
                        1 if this is syn_scan connection
                       -1 in case of any other error
 */
static int 
syn_scan (int sock)
{
    int rc;
    struct sockaddr_in addr;
    int size = sizeof (addr);

    if (sock == SLAPD_INVALID_SOCKET)
        return -1;

    rc = getsockname (sock, (struct sockaddr*)&addr,  &size); 
    if (rc != 0)
        return -1;
    else if (addr.sin_addr.s_addr == 0)
        return 1;
    else
        return 0;
}

#endif

static int
accept_and_configure(int s, PRFileDesc *pr_acceptfd, PRNetAddr *pr_netaddr, 
	int addrlen, int secure, int local, PRFileDesc **pr_clonefd)
{
	int ns = 0;

	PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_wakeup_timer);

#if !defined( XP_WIN32 ) /* UNIX */
	(*pr_clonefd) = PR_Accept(pr_acceptfd, pr_netaddr, pr_timeout);
	if( !(*pr_clonefd) ) {
		PRErrorCode prerr = PR_GetError();
		LDAPDebug( LDAP_DEBUG_ANY, "PR_Accept() failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror(prerr), 0 );
		return(SLAPD_INVALID_SOCKET);
	}

	ns = configure_pr_socket( pr_clonefd, secure, local );

#else /* Windows */
	if( secure ) {
		(*pr_clonefd) = PR_Accept(pr_acceptfd, pr_netaddr, pr_timeout);
		if( !(*pr_clonefd) ) {
			PRErrorCode prerr = PR_GetError();
			LDAPDebug( LDAP_DEBUG_ANY, "PR_Accept() failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n", 
			    prerr, slapd_pr_strerror(prerr), 0 );

			/* Bug 613324: Call PR_NT_CancelIo if an error occurs */
			if( (prerr == PR_IO_TIMEOUT_ERROR ) ||
			    (prerr == PR_PENDING_INTERRUPT_ERROR) ) {
				if( (PR_NT_CancelIo( pr_acceptfd )) != PR_SUCCESS) {
					prerr = PR_GetError();
					LDAPDebug( LDAP_DEBUG_ANY, 
						"PR_NT_CancelIo() failed, "
						SLAPI_COMPONENT_NAME_NSPR 
						" error %d (%s)\n",
						prerr, slapd_pr_strerror(prerr), 0 );
				}
			}
			return(SLAPD_INVALID_SOCKET);
		}

		ns = configure_pr_socket( pr_clonefd, secure, local );

	} else { /* !secure */
		struct sockaddr *addr; /* NOT IPv6 enabled */

		addr = (struct sockaddr *) slapi_ch_malloc( sizeof(struct sockaddr) );
		ns = accept (s, addr, (TCPLEN_T *)&addrlen);

		if (ns == SLAPD_INVALID_SOCKET) {
			int oserr = errno;
			
			LDAPDebug( LDAP_DEBUG_ANY,
				   "accept(%d) failed errno %d (%s)\n",
				   s, oserr, slapd_system_strerror(oserr));
		}

		else if (syn_scan (ns))
		{
			/* this is a work around for accept problem with SYN scan on NT.
			See bug 391414 for more details */
			LDAPDebug(LDAP_DEBUG_ANY, "syn-scan request is received - ignored\n", 0, 0, 0);				
			closesocket (ns);
			ns = SLAPD_INVALID_SOCKET;
		}

		PRLDAP_SET_PORT( pr_netaddr, ((struct sockaddr_in *)addr)->sin_port );
		PR_ConvertIPv4AddrToIPv6(((struct sockaddr_in *)addr)->sin_addr.s_addr, &(pr_netaddr->ipv6.ip));

		(*pr_clonefd) = NULL;

		slapi_ch_free( (void **)&addr );
		configure_ns_socket( &ns );
	}
#endif

	return ns;
}

/* 
 * This is the shiny new re-born daemon function, without all the hair
 */
#ifdef _WIN32
static void setup_read_fds(Connection_Table *ct, fd_set *readfds, int n_tcps, int s_tcps );
static void handle_read_ready(Connection_Table *ct, fd_set *readfds);
static void set_timeval_ms(struct timeval *t, int ms);
#endif
/* GGOODREPL static void handle_timeout( void ); */
static void handle_pr_read_ready(Connection_Table *ct, PRIntn num_poll);
static int handle_new_connection(Connection_Table *ct, int tcps, PRFileDesc *pr_acceptfd, int secure, int local );
#ifdef _WIN32
static void unfurl_banners(Connection_Table *ct,daemon_ports_t *ports, int n_tcps, PRFileDesc *s_tcps);
#else
static void unfurl_banners(Connection_Table *ct,daemon_ports_t *ports, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix);
#endif
static int write_pid_file();
static int init_shutdown_detect();
#ifdef _WIN32
static int clear_signal(fd_set *readfdset);
#else
static int clear_signal(struct POLL_STRUCT *fds);
#endif

/* Globals which are used to store the sockets between
 * calls to daemon_pre_setuid_init() and the daemon thread
 * creation. */

int daemon_pre_setuid_init(daemon_ports_t *ports)
{
	int	rc = 0;

	if (0 != ports->n_port) {
#if defined( XP_WIN32 )
		ports->n_socket = createlistensocket((unsigned short)ports->n_port,
											 &ports->n_listenaddr);
#else
		ports->n_socket = createprlistensockets(ports->n_port,
											   ports->n_listenaddr, 0, 0);
#endif
	}

	if ( config_get_security() && (0 != ports->s_port) ) {
		ports->s_socket = createprlistensockets((unsigned short)ports->s_port,
		    									ports->s_listenaddr, 1, 0);
#ifdef XP_WIN32
		ports->s_socket_native = PR_FileDesc2NativeHandle(ports->s_socket);
#endif
	} else {
	    ports->s_socket = SLAPD_INVALID_SOCKET;
#ifdef XP_WIN32
	    ports->s_socket_native = SLAPD_INVALID_SOCKET;
#endif
	}

#ifndef XP_WIN32
#if defined(ENABLE_LDAPI)
	/* ldapi */
	if(0 != ports->i_port) {
		ports->i_socket = createprlistensockets(1, ports->i_listenaddr, 0, 1);
	}
#endif /* ENABLE_LDAPI */
#endif

	return( rc );
}


/* Decide whether we're running on a platform which supports send with timeouts */
static void detect_timeout_support()
{
	/* Currently we know that NT4.0 or higher DOES support timeouts */
#if defined _WIN32
	/* Get the OS revision */
	OSVERSIONINFO ver;
	ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&ver);
	if (ver.dwPlatformId == VER_PLATFORM_WIN32_NT && ver.dwMajorVersion >= 4) {
		have_send_timeouts = 1;
	}
#else
	/* Some UNIXen do, but for now I don't feel confident which , and whether timeouts really work there */
#endif
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
    struct mnttab *mnt;
    struct stat s;
    dev_t dev_id;
    FILE *fp;

    fp = fopen("/etc/mnttab", "r");

    if (fp == NULL || stat(dir, &s) != 0) {
        return NULL;
    }

    dev_id = s.st_dev;

    while((mnt = getmntent(fp))){
        if (stat(mnt->mnt_mountp, &s) != 0) {
            continue;
        }
        if (s.st_dev == dev_id) {
            return (slapi_ch_strdup(mnt->mnt_mountp));
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
    slapi_rwlock_rdlock(config->cfg_rwlock);
    disk_mon_add_dir(list, config->configdir);
    disk_mon_add_dir(list, config->accesslog);
    disk_mon_add_dir(list, config->errorlog);
    disk_mon_add_dir(list, config->auditlog);
    slapi_rwlock_unlock(config->cfg_rwlock);

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
    char errorbuf[BUFSIZ];
    char **dirs = NULL;
    char *dirstr = NULL;
    PRUint64 previous_mark = 0;
    PRUint64 disk_space = 0;
    PRUint64 threshold = 0;
    PRUint64 halfway = 0;
    time_t start = 0;
    time_t now = 0;
    int deleted_rotated_logs = 0;
    int logging_critical = 0;
    int passed_threshold = 0;
    int verbose_logging = 0;
    int using_accesslog = 0;
    int using_auditlog = 0;
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
            		LDAPDebug(LDAP_DEBUG_ANY, "Disk space is now within acceptable levels.  "
                        "Restoring the log settings.\n",0,0,0);
                    if(using_accesslog){
                        config_set_accesslog_enabled(LOGGING_ON);
                    }
                    if(using_auditlog){
                        config_set_auditlog_enabled(LOGGING_ON);
                    }
                } else {
                	LDAPDebug(LDAP_DEBUG_ANY, "Disk space is now within acceptable levels.\n",0,0,0);
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
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space is critically low on disk (%s), remaining space: %" NSPRIu64 " Kb.  "
                "Signaling slapd for shutdown...\n", dirstr , (disk_space / 1024), 0);
            g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
            return;
        }
        /*
         *  If we are low, see if we are using verbose error logging, and turn it off
         *  if logging is not critical
         */
        if(verbose_logging != 0 && verbose_logging != LDAP_DEBUG_ANY){
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space is low on disk (%s), remaining space: %" NSPRIu64 " Kb, "
                "temporarily setting error loglevel to zero.\n", dirstr,
                (disk_space / 1024), 0);
            /* Setting the log level back to zero, actually sets the value to LDAP_DEBUG_ANY */
            config_set_errorlog_level(CONFIG_LOGLEVEL_ATTRIBUTE, "0", errorbuf, CONFIG_APPLY);
            continue;
        }
        /*
         *  If we are low, there's no verbose logging, logs are not critical, then disable the
         *  access/audit logs, log another error, and continue.
         */
        if(!logs_disabled && !logging_critical){
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space is too low on disk (%s), remaining space: %" NSPRIu64 " Kb, "
                "disabling access and audit logging.\n", dirstr, (disk_space / 1024), 0);
            config_set_accesslog_enabled(LOGGING_OFF);
            config_set_auditlog_enabled(LOGGING_OFF);
            logs_disabled = 1;
            continue;
        }
        /*
         *  If we are low, we turned off verbose logging, logs are not critical, and we disabled
         *  access/audit logging, then delete the rotated logs, log another error, and continue.
         */
        if(!deleted_rotated_logs && !logging_critical){
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space is too low on disk (%s), remaining space: %" NSPRIu64 " Kb, "
                "deleting rotated logs.\n", dirstr, (disk_space / 1024), 0);
            log__delete_rotated_logs();
            deleted_rotated_logs = 1;
            continue;
        }
        /*
         *  Ok, we've done what we can, log a message if we continue to lose available disk space
         */
        if(disk_space < previous_mark){
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space is too low on disk (%s), remaining space: %" NSPRIu64 " Kb\n",
                dirstr, (disk_space / 1024), 0);
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
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space on (%s) is too far below the threshold(%" NSPRIu64 " bytes).  "
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
                    LDAPDebug(LDAP_DEBUG_ANY, "Available disk space is now acceptable (%" NSPRIu64 " bytes).  Aborting"
                                              " shutdown, and restoring the log settings.\n",disk_space,0,0);
                    if(logs_disabled && using_accesslog){
                        config_set_accesslog_enabled(LOGGING_ON);
                    }
                    if(logs_disabled && using_auditlog){
                        config_set_auditlog_enabled(LOGGING_ON);
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
                    LDAPDebug(LDAP_DEBUG_ANY, "Disk space is critically low on disk (%s), remaining space: %" NSPRIu64 " Kb."
                        "  Signaling slapd for shutdown...\n", dirstr, (disk_space / 1024), 0);
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
            LDAPDebug(LDAP_DEBUG_ANY, "Disk space is still too low (%" NSPRIu64 " Kb).  Signaling slapd for shutdown...\n",
                (disk_space / 1024), 0, 0);
            g_set_shutdown( SLAPI_SHUTDOWN_DISKFULL );

            return;
        }
    }
}

static void
handle_listeners(Connection_Table *ct, listener_info *listener_idxs, int n_listeners)
{
	int idx;
	for (idx = 0; idx < n_listeners; ++idx) {
		int fdidx = listener_idxs[idx].idx;
		PRFileDesc *listenfd = listener_idxs[idx].listenfd;
		int secure = listener_idxs[idx].secure;
		int local = listener_idxs[idx].local;
		if (fdidx && listenfd) {
			if (SLAPD_POLL_LISTEN_READY(ct->fd[fdidx].out_flags)) {
				/* accept() the new connection, put it on the active list for handle_pr_read_ready */
				int rc = handle_new_connection(ct, SLAPD_INVALID_SOCKET, listenfd, secure, local);
				if (rc) {
					LDAPDebug1Arg(LDAP_DEBUG_CONNS, "Error accepting new connection listenfd=%d\n",
					              PR_FileDesc2NativeHandle(listenfd));
					continue;
				}
			}
		}
	}
	return;
}

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

#if defined( XP_WIN32 )
	int n_tcps = 0;
	int s_tcps_native = 0;
	PRFileDesc *s_tcps = NULL; 
#else
	PRFileDesc **n_tcps = NULL; 
	PRFileDesc **s_tcps = NULL; 
	PRFileDesc **i_unix = NULL;
	PRFileDesc **fdesp = NULL; 
#endif
	PRIntn num_poll = 0;
	PRIntervalTime pr_timeout = PR_MillisecondsToInterval(slapd_wakeup_timer);	
	PRThread *time_thread_p;
	int threads;
	int in_referral_mode = config_check_referral_mode();
	int n_listeners = 0; /* number of listener sockets */
	listener_info *listener_idxs = NULL; /* array of indexes of listener sockets in the ct->fd array */

	int connection_table_size = get_configured_connection_table_size();
	the_connection_table= connection_table_new(connection_table_size);

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
#ifdef XP_WIN32
	s_tcps_native = ports->s_socket_native;
#else
#if defined(ENABLE_LDAPI)
	i_unix = ports->i_socket;
#endif /* ENABLE_LDAPI */
#endif
	
	createsignalpipe();

	init_shutdown_detect();

	if (
#if defined( XP_WIN32 )
		(n_tcps == SLAPD_INVALID_SOCKET) && 
#else
		(n_tcps == NULL) &&
#if defined(ENABLE_LDAPI)
		(i_unix == NULL) &&
#endif /* ENABLE_LDAPI */
#endif
	    (s_tcps == NULL) ) {	/* nothing to do */
	    LDAPDebug( LDAP_DEBUG_ANY,
		"no port to listen on\n", 0, 0, 0 );
	    exit( 1 );
	}

	unfurl_banners(the_connection_table,ports,n_tcps,s_tcps,i_unix);
	init_op_threads ();
	detect_timeout_support();

    /* Start the time thread */
    time_thread_p = PR_CreateThread(PR_SYSTEM_THREAD,
		(VFP) (void *) time_thread, NULL,
        PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, 
        PR_JOINABLE_THREAD, 
        SLAPD_DEFAULT_THREAD_STACKSIZE);
    if ( NULL == time_thread_p ) {
		PRErrorCode errorCode = PR_GetError();
		LDAPDebug(LDAP_DEBUG_ANY, "Unable to create time thread - Shutting Down ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
				errorCode, slapd_pr_strerror(errorCode), 0);
		g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
	}

    /*
     *  If we are monitoring disk space, then create the mutex, the cvar,
     *  and the monitoring thread.
     */
    if( config_get_disk_monitoring() ){
        if ( ( diskmon_mutex = PR_NewLock() ) == NULL ) {
            slapi_log_error(SLAPI_LOG_FATAL, NULL,
                "Cannot create new lock for disk space monitoring. "
                SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                PR_GetError(), slapd_pr_strerror( PR_GetError() ));
            g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
        }
        if ( diskmon_mutex ){
            if(( diskmon_cvar = PR_NewCondVar( diskmon_mutex )) == NULL ) {
                slapi_log_error(SLAPI_LOG_FATAL, NULL,
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
                LDAPDebug(LDAP_DEBUG_ANY, "Unable to create disk monitoring thread - Shutting Down ("
                    SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                    errorCode, slapd_pr_strerror(errorCode), 0);
                g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
            }
        }
    }

	/* We are now ready to accept incoming connections */
#if defined( XP_WIN32 )
	if ( n_tcps != SLAPD_INVALID_SOCKET
				&& listen( n_tcps, config_get_listen_backlog_size() ) == -1 ) {
		int		oserr = errno;
		char	addrbuf[ 256 ];

		slapi_log_error(SLAPI_LOG_FATAL, "slapd_daemon",
			"listen() on %s port %d failed: OS error %d (%s)\n",
			netaddr2string(&ports->n_listenaddr, addrbuf, sizeof(addrbuf)),
			ports->n_port, oserr, slapd_system_strerror( oserr ) );
		g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
		n_listeners++;
	}
#else
	if ( n_tcps != NULL ) {
		PRFileDesc **fdesp;
		PRNetAddr  **nap = ports->n_listenaddr;
		for (fdesp = n_tcps; fdesp && *fdesp; fdesp++, nap++) {
			if ( PR_Listen( *fdesp, config_get_listen_backlog_size() ) == PR_FAILURE ) {
				PRErrorCode prerr = PR_GetError();
				char		addrbuf[ 256 ];

				slapi_log_error(SLAPI_LOG_FATAL, "slapd_daemon",
					"PR_Listen() on %s port %d failed: %s error %d (%s)\n",
					netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
					ports->n_port, SLAPI_COMPONENT_NAME_NSPR, prerr,
					slapd_pr_strerror( prerr ));
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
			}
			n_listeners++;
		}
	}
#endif

	if ( s_tcps != NULL ) {
		PRFileDesc **fdesp;
		PRNetAddr  **sap = ports->s_listenaddr;
		for (fdesp = s_tcps; fdesp && *fdesp; fdesp++, sap++) {
			if ( PR_Listen( *fdesp, config_get_listen_backlog_size() ) == PR_FAILURE ) {
				PRErrorCode prerr = PR_GetError();
				char		addrbuf[ 256 ];

				slapi_log_error(SLAPI_LOG_FATAL, "slapd_daemon",
					"PR_Listen() on %s port %d failed: %s error %d (%s)\n",
					netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
					ports->s_port, SLAPI_COMPONENT_NAME_NSPR, prerr,
					slapd_pr_strerror( prerr ));
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
			}
			n_listeners++;
		}
	}

#if !defined( XP_WIN32 )
#if defined(ENABLE_LDAPI)
	if( i_unix != NULL ) {
		PRFileDesc **fdesp;
		PRNetAddr  **iap = ports->i_listenaddr;
		for (fdesp = i_unix; fdesp && *fdesp; fdesp++, iap++) {
			if ( PR_Listen(*fdesp, config_get_listen_backlog_size()) == PR_FAILURE) {
				PRErrorCode prerr = PR_GetError();
				slapi_log_error(SLAPI_LOG_FATAL, "slapd_daemon",
					"listen() on %s failed: error %d (%s)\n",
					(*iap)->local.path,
					prerr,
					slapd_pr_strerror( prerr ));
				g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
			}
			n_listeners++;
		}
	}
#endif /* ENABLE_LDAPI */
#endif

	listener_idxs = (listener_info *)slapi_ch_calloc(n_listeners, sizeof(*listener_idxs));
	/* Now we write the pid file, indicating that the server is finally and listening for connections */
	write_pid_file();

	/* The meat of the operation is in a loop on a call to select */
	while(!g_get_shutdown())
	{
#ifdef _WIN32
		fd_set			readfds;
		struct timeval	wakeup_timer;
		int			oserr;
#endif
		int select_return = 0;

#ifndef _WIN32
		PRErrorCode prerr;
#endif

#ifdef _WIN32
		set_timeval_ms(&wakeup_timer, slapd_wakeup_timer);
		setup_read_fds(the_connection_table,&readfds,n_tcps, s_tcps_native);
		/* This select needs to timeout to give the server a chance to test for shutdown */
		select_return = select(connection_table_size, &readfds, NULL, 0, &wakeup_timer);
#else
		setup_pr_read_pds(the_connection_table,n_tcps,s_tcps,i_unix,&num_poll,listener_idxs,n_listeners);
		select_return = POLL_FN(the_connection_table->fd, num_poll, pr_timeout);
#endif
		switch (select_return) {
		case 0: /* Timeout */
			/* GGOODREPL handle_timeout(); */
			break;
		case -1: /* Error */
#ifdef _WIN32
			oserr = errno;

			LDAPDebug( LDAP_DEBUG_TRACE,
			    "select failed errno %d (%s)\n", oserr,
			    slapd_system_strerror(oserr), 0 );
#else
			prerr = PR_GetError();
			LDAPDebug( LDAP_DEBUG_TRACE, "PR_Poll() failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_system_strerror(prerr), 0 );
#endif
			break;
		default: /* either a new connection or some new data ready */
			/* Figure out if we are dealing with one of the listen sockets */
#ifdef _WIN32
			/* If so, then handle a new connection */
			if ( n_tcps != SLAPD_INVALID_SOCKET && FD_ISSET( n_tcps,&readfds ) ) {
				handle_new_connection(the_connection_table,n_tcps,NULL,0,0);
			} 
			/* If so, then handle a new connection */
			if ( s_tcps != SLAPD_INVALID_SOCKET && FD_ISSET( s_tcps_native,&readfds ) ) {
				handle_new_connection(the_connection_table,SLAPD_INVALID_SOCKET,s_tcps,1,0);
			} 
			/* handle new data ready */
			handle_read_ready(the_connection_table,&readfds);
			clear_signal(&readfds);
#else
			/* handle new connections from the listeners */
			handle_listeners(the_connection_table, listener_idxs, n_listeners);
			/* handle new data ready */
			handle_pr_read_ready(the_connection_table, connection_table_size);
			clear_signal(the_connection_table->fd);
#endif
			break;
		}

	}
	/* We get here when the server is shutting down */
	/* Do what we have to do before death */

	connection_table_abandon_all_operations(the_connection_table);	/* abandon all operations in progress */
	
	if ( ! in_referral_mode ) {
		ps_stop_psearch_system(); /* stop any persistent searches */
	}

#ifdef _WIN32
	if ( n_tcps != SLAPD_INVALID_SOCKET ) {
		closesocket( n_tcps );
	}
	if ( s_tcps != NULL ) {
 		PR_Close( s_tcps );
	}
#else
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
#endif

	/* Might compete with housecleaning thread, but so far so good */
	be_flushall();
	op_thread_cleanup();
	housekeeping_stop(); /* Run this after op_thread_cleanup() logged sth */
	disk_monitoring_stop(disk_thread_p);

#ifndef _WIN32
	threads = g_get_active_threadcnt();
	if ( threads > 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY,
			"slapd shutting down - waiting for %d thread%s to terminate\n",
			threads, ( threads > 1 ) ? "s" : "", 0 );
	}
#endif

	threads = g_get_active_threadcnt();
	while ( threads > 0 ) {
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
				LDAPDebug( LDAP_DEBUG_ANY, "listener could not clear signal pipe, " 
						SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
						prerr, slapd_system_strerror(prerr), 0 );
			break;
		    }
		} else if (spe == -1) {
		    PRErrorCode prerr = PR_GetError();
		    LDAPDebug( LDAP_DEBUG_ANY, "PR_Poll() failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_system_strerror(prerr), 0 );
		    break;
		} else {
		    /* no data */
		}
		DS_Sleep(PR_INTERVAL_NO_WAIT);
		if ( threads != g_get_active_threadcnt() )  {
			LDAPDebug( LDAP_DEBUG_TRACE,
					"slapd shutting down - waiting for %d threads to terminate\n",
					g_get_active_threadcnt(), 0, 0 );
			threads = g_get_active_threadcnt();
		}
	}

	LDAPDebug( LDAP_DEBUG_ANY,
	    "slapd shutting down - closing down internal subsystems and plugins\n",
	    0, 0, 0 );

    log_access_flush();

	/* let backends do whatever cleanup they need to do */
	LDAPDebug( LDAP_DEBUG_TRACE,"slapd shutting down - waiting for backends to close down\n", 0, 0,0 );

	eq_stop();
	if ( ! in_referral_mode ) {
		task_shutdown();
		uniqueIDGenCleanup ();   
	}

	plugin_closeall( 1 /* Close Backends */, 1 /* Close Gloabls */); 

	if ( ! in_referral_mode ) {
		/* Close SNMP collator after the plugins closed... 
		 * Replication plugin still performs internal ops that
		 * may try to increment snmp stats.
		 * Fix for defect 523780
		 */
		snmp_collator_stop();
		mapping_tree_free ();
	}

	/* 
	 * connection_table_free could use callbacks in the backend.
	 * (e.g., be_search_results_release)
	 * Thus, it needs to be called before be_cleanupall.
	 */
	connection_table_free(the_connection_table);
	the_connection_table= NULL;

	be_cleanupall (); 
	LDAPDebug( LDAP_DEBUG_TRACE, "slapd shutting down - backends closed down\n",
			0, 0, 0 );
	referrals_free();

	/* tell the time thread to shutdown and then wait for it */
	time_shutdown = 1;
	PR_JoinThread( time_thread_p );

#ifdef _WIN32
	WSACleanup();
#else
	if ( g_get_shutdown() == SLAPI_SHUTDOWN_DISKFULL ){
		/* This is a server-induced shutdown, we need to manually remove the pid file */
		if( unlink(get_pid_file()) ){
			LDAPDebug( LDAP_DEBUG_ANY, "Failed to remove pid file %s\n", get_pid_file(), 0, 0 );
		}
	}
#endif
}

int signal_listner()
{
	/* Replaces previous macro---called to bump the thread out of select */
#if defined( _WIN32 )
	if ( PR_Write( signalpipe[1], "", 1) != 1 ) {
			/* this now means that the pipe is full
			 * this is not a problem just go-on
			 */
			LDAPDebug( LDAP_DEBUG_CONNS,
				"listener could not write to signal pipe %d\n",
				errno, 0, 0 );
	}
	
#else
	if ( write( writesignalpipe, "", 1) != 1 ) {
			/* this now means that the pipe is full
			 * this is not a problem just go-on
			 */
			LDAPDebug( LDAP_DEBUG_CONNS,
				"listener could not write to signal pipe %d\n",
				errno, 0, 0 );
	}
#endif
	return( 0 );
}

#ifdef _WIN32
static int clear_signal(fd_set *readfdset)
#else
static int clear_signal(struct POLL_STRUCT *fds)
#endif
{
#ifdef _WIN32
	if ( FD_ISSET(readsignalpipe, readfdset)) {
#else
	if ( fds[FDS_SIGNAL_PIPE].out_flags & SLAPD_POLL_FLAGS ) {
#endif
		char	buf[200];

		LDAPDebug( LDAP_DEBUG_CONNS,
			"listener got signaled\n",
			0, 0, 0 );
#ifdef _WIN32
		if ( PR_Read( signalpipe[0], buf, 20 ) < 1 ) {
#else
		if ( read( readsignalpipe, buf, 200 ) < 1 ) {
#endif
			LDAPDebug( LDAP_DEBUG_ANY,
				"listener could not clear signal pipe\n",
				0, 0, 0 );
		}
	} 
	return 0;
}

#ifdef _WIN32
static void set_timeval_ms(struct timeval *t, int ms)
{
	t->tv_sec = ms/1000;
	t->tv_usec = (ms % 1000)*1000;
}
#endif

#ifdef _WIN32
static void setup_read_fds(Connection_Table *ct, fd_set *readfds, int n_tcps, int s_tcps)
{
	Connection *c= NULL;
	Connection *next= NULL;
	int accept_new_connections;
	static int last_accept_new_connections = -1;
   	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	LBER_SOCKET socketdesc = SLAPD_INVALID_SOCKET;

	FD_ZERO( readfds );

	accept_new_connections = ((ct->size - g_get_current_conn_count())
	    > slapdFrontendConfig->reservedescriptors);
	if ( ! accept_new_connections ) {
		if ( last_accept_new_connections ) {
			LDAPDebug( LDAP_DEBUG_ANY, "Not listening for new "
			    "connections - too many fds open\n", 0, 0, 0 );
		}
	} else {
		if ( ! last_accept_new_connections &&
		    last_accept_new_connections != -1 ) {
			LDAPDebug( LDAP_DEBUG_ANY, "Listening for new "
			    "connections again\n", 0, 0, 0 );
		}
	}
	last_accept_new_connections = accept_new_connections;
	if (n_tcps != SLAPD_INVALID_SOCKET && accept_new_connections) {
		FD_SET( n_tcps, readfds );
		LDAPDebug( LDAP_DEBUG_HOUSE,
			"listening for connections on %d\n", n_tcps, 0, 0 );
	}
	if (s_tcps != SLAPD_INVALID_SOCKET && accept_new_connections) {
		FD_SET( s_tcps, readfds );
		LDAPDebug( LDAP_DEBUG_HOUSE,
			"listening for connections on %d\n", s_tcps, 0, 0 );
	}

	if ((s_tcps != SLAPD_INVALID_SOCKET)
		 && (readsignalpipe != SLAPD_INVALID_SOCKET)) {
		FD_SET( readsignalpipe, readfds );
	}

	/* Walk down the list of active connections to find 
	 * out which connections we should poll over.  If a connection
	 * is no longer in use, we should remove it from the linked 
	 * list. */
	c= connection_table_get_first_active_connection (ct);
	while (c)
    {
	    next = connection_table_get_next_active_connection (ct, c);
	    if ( c->c_mutex == NULL )
	    {
		    connection_table_move_connection_out_of_active_list(ct,c);
	    }
	    else
	    {
	        PR_Lock( c->c_mutex );
			if ( c->c_flags & CONN_FLAG_CLOSING )
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
			else
			{
#if defined(LDAP_IOCP)	 /* When we have IO completion ports, we don't want to do this */
			    if ( !c->c_gettingber && (c->c_flags & CONN_FLAG_SSL) )
#else
			    if ( !c->c_gettingber )
#endif
				{
					FD_SET( c->c_sd, readfds );
			    }
			}
			PR_Unlock( c->c_mutex );
	    }
		c = next;
	}
}
#endif   /* _WIN32 */

static int first_time_setup_pr_read_pds = 1;
static int listen_addr_count = 0;
static void
setup_pr_read_pds(Connection_Table *ct, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix, PRIntn *num_to_read, listener_info *listener_idxs, int max_listeners)
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
			LDAPDebug( LDAP_DEBUG_ANY, "Not listening for new "
				"connections - too many fds open\n", 0, 0, 0 );
			/* reinitialize n_tcps and s_tcps to the pds */
			first_time_setup_pr_read_pds = 1;
		}
	} else {
		if ( ! last_accept_new_connections &&
			last_accept_new_connections != -1 ) {
			LDAPDebug( LDAP_DEBUG_ANY, "Listening for new "
				"connections again\n", 0, 0, 0 );
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

		/* The fds entry for the signalpipe is always FDS_SIGNAL_PIPE (== 0) */
		count = FDS_SIGNAL_PIPE;
#if !defined(_WIN32)
		ct->fd[count].fd = signalpipe[0];
		ct->fd[count].in_flags = SLAPD_POLL_FLAGS;
		ct->fd[count].out_flags = 0;
#else
		ct->fd[count].fd = NULL;
#endif
		count++;

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
				LDAPDebug( LDAP_DEBUG_HOUSE, 
					"listening for connections on %d\n", socketdesc, 0, 0 );
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
				LDAPDebug( LDAP_DEBUG_HOUSE, 
					"listening for SSL connections on %d\n", socketdesc, 0, 0 );
			}
		} else {
			ct->fd[count].fd = NULL;
			count++;
		}
		ct->s_tcpe = count;


#if !defined(_WIN32)
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
				LDAPDebug( LDAP_DEBUG_HOUSE,
					"listening for LDAPI connections on %d\n", socketdesc, 0, 0 );
			}
		} else {
			ct->fd[count].fd = NULL;
			count++;
		}
		ct->i_unixe = count;
#endif
#endif
 
		first_time_setup_pr_read_pds = 0;
		listen_addr_count = count;

		if (n_listeners < max_listeners) {
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
			PR_Lock( c->c_mutex );
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
					c->c_fdi = SLAPD_INVALID_SOCKET_INDEX;
				}
			}
			PR_Unlock( c->c_mutex );
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


/*
 * Compute the idle timeout for the connection.
 *
 * Note: this function must always be called with conn->c_mutex locked.
 */
static int
compute_idletimeout( slapdFrontendConfig_t *fecfg, Connection *conn )
{
	int		idletimeout;

	if ( slapi_reslimit_get_integer_limit( conn, idletimeout_reslimit_handle,
            &idletimeout ) != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
		/*
		 * No limit associated with binder/connection or some other error
		 * occurred.  If the user is anonymous and anonymous limits are
		 * set, attempt to set the bind based resource limits.  We do this
		 * here since a BIND operation is not required prior to other
		 * operations.  We want to set the anonymous limits early on so
		 * that they are put into effect if a BIND is never sent.  If
		 * this is not an anonymous user and no bind-based limits are set,
		 * use the default idle timeout.
	 	 */
		char *anon_dn = config_get_anon_limits_dn();

		if ((conn->c_dn == NULL) && anon_dn && (strlen(anon_dn) > 0)) {
			Slapi_DN *anon_sdn = slapi_sdn_new_dn_byref( anon_dn );

			reslimit_update_from_dn( conn, anon_sdn );

			if ( slapi_reslimit_get_integer_limit( conn,
			    idletimeout_reslimit_handle, &idletimeout ) !=
			    SLAPI_RESLIMIT_STATUS_SUCCESS ) {
				idletimeout = fecfg->idletimeout;
			}

			slapi_sdn_free( &anon_sdn );
		} else if ( conn->c_isroot ) {
			idletimeout = 0;	/* no limit for Directory Manager */
		} else {
			idletimeout = fecfg->idletimeout;
		}

		slapi_ch_free_string( &anon_dn );
	}

	return( idletimeout );
}


#ifdef _WIN32
static void
handle_read_ready(Connection_Table *ct, fd_set *readfds)
{
	Connection *c= NULL;
	time_t curtime = current_time();
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int idletimeout;

#ifdef LDAP_DEBUG
	if ( slapd_ldap_debug & LDAP_DEBUG_CONNS )
	{
		connection_table_dump_activity_to_errors_log(ct);
	}
#endif /* LDAP_DEBUG */


	/* Instead of going through the whole connection table to see which
	 * connections we can read from, we'll only check the slots in the
	 * linked list */
	c = connection_table_get_first_active_connection (ct);
	while ( c!=NULL )
	{
	    if ( c->c_mutex != NULL )
		{
		    PR_Lock( c->c_mutex );
		    if (connection_is_active_nolock (c) && c->c_gettingber == 0 )
		    {
		        /* read activity */
		        short readready= ( FD_ISSET( c->c_sd, readfds ) );

				/* read activity */
				if ( readready )
				{
					LDAPDebug( LDAP_DEBUG_CONNS, "read activity on %d\n", c->c_ci, 0, 0 );
					c->c_idlesince = curtime;

					/* This is where the work happens ! */
					connection_activity( c );

					/* idle timeout */
				}
				else if (( idletimeout = compute_idletimeout(
						slapdFrontendConfig, c )) > 0 &&
						(curtime - c->c_idlesince) >= idletimeout &&
						NULL == c->c_ops )
				{
					disconnect_server_nomutex( c, c->c_connid, -1,
								   SLAPD_DISCONNECT_IDLE_TIMEOUT, EAGAIN );
				}
			}
			PR_Unlock( c->c_mutex );
		}
		c = connection_table_get_next_active_connection (ct, c);
	}
}
#endif   /* _WIN32 */


static void
handle_pr_read_ready(Connection_Table *ct, PRIntn num_poll)
{
	Connection *c;
	time_t curtime = current_time();
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int idletimeout;
#if defined( XP_WIN32 )
	int i;
#endif

#if LDAP_DEBUG 
	if ( slapd_ldap_debug & LDAP_DEBUG_CONNS )
	{
		connection_table_dump_activity_to_errors_log(ct);
	}
#endif /* LDAP_DEBUG */

#if defined( XP_WIN32 )
	/*
	 * WIN32: this function is only called for SSL connections and
	 * num_poll indicates exactly how many PR fds we polled on.
	 */
	for ( i = 0; i < num_poll; i++ )
	{
		short readready;
		readready = (ct->fd[i].out_flags & SLAPD_POLL_FLAGS);

		/* Find the connection we are referring to */
		for ( c = connection_table_get_first_active_connection (ct); c != NULL; 
              c = connection_table_get_next_active_connection (ct, c) )
		{
			if ( c->c_mutex != NULL )
			{
				PR_Lock( c->c_mutex );
				if ( c->c_prfd == ct->fd[i].fd )
				{
					break;	/* c_mutex is still locked! */
				}
				PR_Unlock( c->c_mutex );
			}
		}

		if ( c == NULL )
		{	/* connection not found! */
			LDAPDebug( LDAP_DEBUG_CONNS, "handle_pr_read_ready: "
			    "connection not found for poll slot %d\n", i,0,0 );
		}
		else
		{
			/* c_mutex is still locked... check for activity and errors */
			if ( !readready && ct->fd[i].out_flags && c->c_prfd == ct->fd[i].fd )
			{
				/* some error occured */
				LDAPDebug( LDAP_DEBUG_CONNS,
					"poll says connection on sd %d is bad "
					"(closing)\n", c->c_sd, 0, 0 );
				disconnect_server_nomutex( c, c->c_connid, -1, SLAPD_DISCONNECT_POLL, EPIPE );
			}
			else if ( readready && c->c_prfd == ct->fd[i].fd )
			{
				/* read activity */
				LDAPDebug( LDAP_DEBUG_CONNS,
					"read activity on %d\n", i, 0, 0 );
				c->c_idlesince = curtime;

				/* This is where the work happens ! */
				connection_activity( c );
			}
			else if (( idletimeout = compute_idletimeout( slapdFrontendConfig,
					c )) > 0 &&
					c->c_prfd == ct->fd[i].fd &&
					(curtime - c->c_idlesince) >= idletimeout &&
					NULL == c->c_ops )
			{
				/* idle timeout */
				disconnect_server_nomutex( c, c->c_connid, -1,
							   SLAPD_DISCONNECT_IDLE_TIMEOUT, EAGAIN );
			}

			PR_Unlock( c->c_mutex );
		}
	}
#else

	/*
	 * non-WIN32: this function is called for all connections, so we
	 * traverse the entire active connection list to find any errors,
	 * activity, etc.
	 */
	for ( c = connection_table_get_first_active_connection (ct); c != NULL; 
          c = connection_table_get_next_active_connection (ct, c) )
	{
		if ( c->c_mutex != NULL )
		{
			PR_Lock( c->c_mutex );
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
					LDAPDebug( LDAP_DEBUG_CONNS,
					    "POLL_FN() says connection on sd %d is bad "
					    "(closing)\n", c->c_sd, 0, 0 );
					disconnect_server_nomutex( c, c->c_connid, -1,
								   SLAPD_DISCONNECT_POLL, EPIPE );
				}
				else if ( readready )
				{
					/* read activity */
					LDAPDebug( LDAP_DEBUG_CONNS,
					    "read activity on %d\n", c->c_ci, 0, 0 );
					c->c_idlesince = curtime;

					/* This is where the work happens ! */
					/* MAB: 25 jan 01, error handling added */
					if ((connection_activity( c )) == -1) {
						/* This might happen as a result of
						 * trying to acquire a closing connection
						 */
						LDAPDebug (LDAP_DEBUG_ANY,
							"connection_activity: abandoning conn %" NSPRIu64 " as fd=%d is already closing\n",
							c->c_connid,c->c_sd,0); 
						/* The call disconnect_server should do nothing,
						 * as the connection c should be already set to CLOSING */
						disconnect_server_nomutex( c, c->c_connid, -1,
									   SLAPD_DISCONNECT_POLL, EPIPE );
					}
				}
				else if (( idletimeout = compute_idletimeout(
						slapdFrontendConfig, c )) > 0 &&
						(curtime - c->c_idlesince) >= idletimeout &&
						NULL == c->c_ops )
				{
					/* idle timeout */
					disconnect_server_nomutex( c, c->c_connid, -1,
								   SLAPD_DISCONNECT_IDLE_TIMEOUT, EAGAIN );
				}
			}
			PR_Unlock( c->c_mutex );
		}
	}
#endif
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
 */
static int
slapd_poll( void *handle, int output )
{
    int		rc;
	int ioblock_timeout = config_get_ioblocktimeout();
	
#if defined( XP_WIN32 )
	if( !secure ) {
		fd_set		handle_set;
		struct timeval	timeout;
		int windows_handle = (int) handle;

		memset (&timeout, 0, sizeof(timeout));
		if (ioblock_timeout > 0) {
			timeout.tv_sec = ioblock_timeout / 1000;
			timeout.tv_usec = (ioblock_timeout % 1000) * 1000;
		}
		FD_ZERO(&handle_set);
		FD_SET(windows_handle, &handle_set);
		rc = output ? select(FD_SETSIZE, NULL, &handle_set, NULL, &timeout)
			: select(FD_SETSIZE, &handle_set, NULL, NULL, &timeout);
	} else {
		struct POLL_STRUCT	pr_pd;
		PRIntervalTime	timeout = PR_MillisecondsToInterval( ioblock_timeout );

		if (timeout < 0) timeout = 0;
		pr_pd.fd = (PRFileDesc *)handle;
		pr_pd.in_flags = output ? PR_POLL_WRITE : PR_POLL_READ;
		pr_pd.out_flags = 0;
		rc = POLL_FN(&pr_pd, 1, timeout);
	}
#else
    struct POLL_STRUCT	pr_pd;
    PRIntervalTime	timeout = PR_MillisecondsToInterval(ioblock_timeout);

    pr_pd.fd = (PRFileDesc *)handle;
    pr_pd.in_flags = output ? PR_POLL_WRITE : PR_POLL_READ;
    pr_pd.out_flags = 0;
    rc = POLL_FN(&pr_pd, 1, timeout);
#endif

    if (rc < 0) {
#if defined( XP_WIN32 )
	if( !secure ) {
		int	oserr = errno;

		LDAPDebug(LDAP_DEBUG_CONNS, "slapd_poll(%d) error %d (%s)\n",
			  handle, oserr, slapd_system_strerror(oserr));
		if ( SLAPD_SYSTEM_WOULD_BLOCK_ERROR(oserr)) {
		    rc = 0;		/* try again */
		}
	} else {
		PRErrorCode prerr = PR_GetError();
		LDAPDebug(LDAP_DEBUG_CONNS, "slapd_poll(%d) "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				handle, prerr, slapd_pr_strerror(prerr));
		if ( prerr == PR_PENDING_INTERRUPT_ERROR ||
			SLAPD_PR_WOULD_BLOCK_ERROR(prerr)) {
		    rc = 0;		/* try again */
		}
	}
#else
	PRErrorCode prerr = PR_GetError();
	LDAPDebug(LDAP_DEBUG_ANY, "slapd_poll(%d) "
			SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
			handle, prerr, slapd_pr_strerror(prerr));
	if ( prerr == PR_PENDING_INTERRUPT_ERROR ||
		SLAPD_PR_WOULD_BLOCK_ERROR(prerr)) {
	    rc = 0;		/* try again */
	}
#endif

    } else if (rc == 0 && ioblock_timeout > 0) {
	PRIntn ihandle;
#if !defined( XP_WIN32 )
	ihandle = PR_FileDesc2NativeHandle((PRFileDesc *)handle);
#else
	if( secure )
		ihandle = PR_FileDesc2NativeHandle((PRFileDesc *)handle);
	else
		ihandle = (PRIntn)handle;
#endif
	LDAPDebug(LDAP_DEBUG_ANY, "slapd_poll(%d) timed out\n",
		  ihandle, 0, 0);
#if defined( XP_WIN32 )
	/*
	 * Bug 624303 - This connection will be cleaned up soon.
	 * During cleanup (see connection_cleanup()), SSL3_SendAlert()
	 * will be called by PR_Close(), and its default wTimeout
	 * in sslSocket associated with the handle
	 * is no time out (I gave up after waited for 30 minutes).
	 * It was during this closing period that server won't
	 * response to new connection requests.
	 * PR_Send() null is a hack here to change the default wTimeout
	 * (see ssl_Send()) to one second which affects PR_Close()
	 * only in the current scenario.
	 */ 
	if( secure ) {
		PR_Send ((PRFileDesc *)handle, NULL, 0, 0, PR_SecondsToInterval(1));
	}
#endif
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
                LDAPDebug(LDAP_DEBUG_CONNS, "PR_Write(%d) "
                          SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          fd, prerr, slapd_pr_strerror( prerr ));
                if ( !SLAPD_PR_WOULD_BLOCK_ERROR(prerr)) {
                    if (prerr != PR_CONNECT_RESET_ERROR) {
                        /* 'TCP connection reset by peer': no need to log */
                        LDAPDebug(LDAP_DEBUG_ANY, "PR_Write(%d) "
                                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                                  fd, prerr, slapd_pr_strerror( prerr ));
                    }
                    if (sentbytes < count) {
                        LDAPDebug(LDAP_DEBUG_CONNS,
                                  "PR_Write(%d) - wrote only %d bytes (expected %d bytes) - 0 (EOF)\n", /* disconnected */
                                  fd, sentbytes, count);
                    }
                    break;		/* fatal error */
                }
            } else if (bytes == 0) { /* disconnect */
                PRErrorCode prerr = PR_GetError();
                LDAPDebug(LDAP_DEBUG_CONNS,
                          "PR_Write(%d) - 0 (EOF) %d:%s\n", /* disconnected */
                          fd, prerr, slapd_pr_strerror(prerr));
                PR_SetError(PR_PIPE_ERROR, EPIPE);
                break;
            } 

            if (sentbytes == count) { /* success */
                return count;
            } else if (sentbytes > count) { /* too many bytes */
                LDAPDebug(LDAP_DEBUG_ANY,
                          "PR_Write(%d) overflow - sent %d bytes (expected %d bytes) - error\n",
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
	ber_sockbuf_remove_io(conn->c_sb, &openldap_sockbuf_io,
		LBER_SBIOD_LEVEL_PROVIDER);
}

/* NOTE: this routine is not reentrant */
static int
handle_new_connection(Connection_Table *ct, int tcps, PRFileDesc *pr_acceptfd, int secure, int local)
{
	int ns = 0;
	Connection *conn = NULL;
	/*	struct sockaddr_in	from;*/
	PRNetAddr from;
	PRFileDesc *pr_clonefd = NULL;

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
	PR_Lock( conn->c_mutex );

#if defined( XP_WIN32 )
	if( !secure )
		ber_sockbuf_set_option(conn->c_sb,LBER_SOCKBUF_OPT_DESC,&ns);
#endif

	conn->c_sd = ns;
	conn->c_prfd = pr_clonefd;
	conn->c_flags &= ~CONN_FLAG_CLOSING;

	/* Store the fact that this new connection is an SSL connection */
	if (secure) {
		conn->c_flags |= CONN_FLAG_SSL;
	}

#ifndef _WIN32
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
#endif

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
#ifdef _WIN32
		func_pointers.lbextiofn_socket_arg = (struct lextiof_socket_private *) ns;	
#else
		func_pointers.lbextiofn_socket_arg = (struct lextiof_socket_private *) pr_clonefd;	
#endif
		ber_sockbuf_set_option( conn->c_sb,
			LBER_SOCKBUF_OPT_EXT_IO_FNS, &func_pointers);	
	}
#endif /* !USE_OPENLDAP */

	if( secure && config_get_SSLclientAuth() != SLAPD_SSLCLIENTAUTH_OFF ) { 
	    /* Prepare to handle the client's certificate (if any): */
		int rv;

		rv = slapd_ssl_handshakeCallback (conn->c_prfd, (void*)handle_handshake_done, conn);
        
		if (rv < 0) {
			PRErrorCode prerr = PR_GetError();
			LDAPDebug (LDAP_DEBUG_ANY, "SSL_HandshakeCallback() %d "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					rv, prerr, slapd_pr_strerror( prerr ));
		}
		rv = slapd_ssl_badCertHook (conn->c_prfd, (void*)handle_bad_certificate, conn);

		if (rv < 0) {
			PRErrorCode prerr = PR_GetError();
			LDAPDebug (LDAP_DEBUG_ANY, "SSL_BadCertHook(%i) %i "
					SLAPI_COMPONENT_NAME_NSPR " error %d\n",
					conn->c_sd, rv, prerr);
		}
	}

	connection_reset(conn, ns, &from, sizeof(from), secure);

	/* Call the plugin extension constructors */
	conn->c_extension = factory_create_extension(connection_type,conn,NULL /* Parent */);

#if defined(ENABLE_LDAPI)
#if !defined( XP_WIN32 )
	/* ldapi */
	if( local )
	{
		conn->c_unix_local = 1;
		conn->c_local_ssf = config_get_localssf();
		slapd_identify_local_user(conn);
	}
#endif
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

	PR_Unlock( conn->c_mutex );

	g_increment_current_conn_count();

	return 0;
}

static int init_shutdown_detect()
{

#ifdef _WIN32
	PRThread *service_exit_wait_tid;
#else
  /* First of all, we must reset the signal mask to get rid of any blockages
   * the process may have inherited from its parent (such as the console), which
   * might result in the process not delivering those blocked signals, and thus, 
   * misbehaving.... 
   */
  {
    int rc;
    sigset_t proc_mask;
        
    LDAPDebug( LDAP_DEBUG_TRACE, "Reseting signal mask....\n", 0, 0, 0);
    (void)sigemptyset( &proc_mask );
    rc = pthread_sigmask( SIG_SETMASK, &proc_mask, NULL );
    LDAPDebug( LDAP_DEBUG_TRACE, " %s \n", 
	       rc ? "Failed to reset signal mask":"....Done (signal mask reset)!!", 0, 0 );
  }
#endif
  
#ifdef _WIN32

	/* Create a thread to wait on the Win32 event which will 
	   be signalled by the watchdog when the Service is 
	   being halted. */
	service_exit_wait_tid = PR_CreateThread( PR_USER_THREAD, 
		(VFP) (void *) slapd_service_exit_wait, (void *) NULL, 
		PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, 
		SLAPD_DEFAULT_THREAD_STACKSIZE);
	if( service_exit_wait_tid == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		"Error: PR_CreateThread(slapd_service_exit_wait) failed\n", 0, 0, 0 );
	}
#elif defined ( HPUX10 )
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
	(void) SIGNAL( SIGTERM, set_shutdown );
	(void) SIGNAL( SIGHUP,  set_shutdown );
#endif /* _WIN32 */
	return 0;
}

#if defined( XP_WIN32 )
static void
unfurl_banners(Connection_Table *ct,daemon_ports_t *ports, int n_tcps, PRFileDesc *s_tcps)
#else
static void
unfurl_banners(Connection_Table *ct,daemon_ports_t *ports, PRFileDesc **n_tcps, PRFileDesc **s_tcps, PRFileDesc **i_unix)
#endif
{
	slapdFrontendConfig_t	*slapdFrontendConfig = getFrontendConfig();
	char					addrbuf[ 256 ];
	int			isfirsttime = 1;

	if ( ct->size <= slapdFrontendConfig->reservedescriptors ) {
#ifdef _WIN32
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ERROR: Not enough descriptors to accept any connections. "
		    "This may be because the maxdescriptors configuration "
		    "directive is too small, or the reservedescriptors "
		    "configuration directive is too large. "
		    "Try increasing the number of descriptors available to "
		    "the slapd process. The current value is %d. %d "
		    "descriptors are currently reserved for internal "
		    "slapd use, so the total number of descriptors available "
		    "to the process must be greater than %d.\n",
		    ct->size, slapdFrontendConfig->reservedescriptors, slapdFrontendConfig->reservedescriptors );
#else /* _WIN32 */
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ERROR: Not enough descriptors to accept any connections. "
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
#endif /* _WIN32 */
		exit( 1 );
	}

	/*
	 * This final startup message gives a definite signal to the admin
	 * program that the server is up.  It must contain the string
	 * "slapd started." because some of the administrative programs
	 * depend on this.  See ldap/admin/lib/dsalib_updown.c.
	 */
#if !defined( XP_WIN32 )
	if ( n_tcps != NULL ) {					/* standard LDAP */
		PRNetAddr   **nap = NULL;

		for (nap = ports->n_listenaddr; nap && *nap; nap++) {
			if (isfirsttime) {
				LDAPDebug( LDAP_DEBUG_ANY,
				"slapd started.  Listening on %s port %d for LDAP requests\n",
					netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
					ports->n_port, 0 );
				isfirsttime = 0;
			} else {
				LDAPDebug( LDAP_DEBUG_ANY,
				"Listening on %s port %d for LDAP requests\n",
					netaddr2string(*nap, addrbuf, sizeof(addrbuf)),
					ports->n_port, 0 );
			}
		}
	}

	if ( s_tcps != NULL ) {					/* LDAP over SSL; separate port */
		PRNetAddr   **sap = NULL;

		for (sap = ports->s_listenaddr; sap && *sap; sap++) {
			if (isfirsttime) {
				LDAPDebug( LDAP_DEBUG_ANY,
					"slapd started.  Listening on %s port %d for LDAPS requests\n",
					netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
					ports->s_port, 0 );
				isfirsttime = 0;
			} else {
				LDAPDebug( LDAP_DEBUG_ANY,
					"Listening on %s port %d for LDAPS requests\n",
					netaddr2string(*sap, addrbuf, sizeof(addrbuf)),
					ports->s_port, 0 );
			}
		}
	}
#else
	if ( n_tcps != SLAPD_INVALID_SOCKET ) {	/* standard LDAP; XP_WIN32 */
		LDAPDebug( LDAP_DEBUG_ANY,
			"slapd started.  Listening on %s port %d for LDAP requests\n",
			netaddr2string(&ports->n_listenaddr, addrbuf, sizeof(addrbuf)),
		    ports->n_port, 0 );
	}

	if ( s_tcps != NULL ) {					/* LDAP over SSL; separate port */
		LDAPDebug( LDAP_DEBUG_ANY,
			"Listening on %s port %d for LDAPS requests\n",
			netaddr2string(&ports->s_listenaddr, addrbuf, sizeof(addrbuf)),
		    ports->s_port, 0 );
	}
#endif

#if !defined( XP_WIN32 )
#if defined(ENABLE_LDAPI)
	if ( i_unix != NULL ) {                                 /* LDAPI */
		PRNetAddr   **iap = ports->i_listenaddr;

		LDAPDebug( LDAP_DEBUG_ANY,
			"%sListening on %s for LDAPI requests\n", isfirsttime?"slapd started.  ":"",
			(*iap)->local.path, 0 );
	}
#endif /* ENABLE_LDAPI */
#endif

}

#if defined( _WIN32 )
/* On Windows, we signal the SCM when we're ready to accept connections */
static int
write_pid_file()
{
	if( SlapdIsAService() )
	{
		/* Initialization complete and successful. Set service to running */
		LDAPServerStatus.dwCurrentState	= SERVICE_RUNNING;
		LDAPServerStatus.dwCheckPoint = 0;
		LDAPServerStatus.dwWaitHint = 0;
			
		if (!SetServiceStatus(hLDAPServerServiceStatus, &LDAPServerStatus)) {
			ReportSlapdEvent(EVENTLOG_INFORMATION_TYPE, MSG_SERVER_START_FAILED, 1, 
				"Could not set Service status.");
			exit(1);
		}
	}

	ReportSlapdEvent(EVENTLOG_INFORMATION_TYPE, MSG_SERVER_STARTED, 0, NULL );
	return 0;
}
#else /* WIN32 */
/* On UNIX, we create a file with our PID in it */
static int
write_pid_file()
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
#endif /* WIN32 */

static void
set_shutdown (int sig)
{ 
    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */
#if 0
    LDAPDebug( LDAP_DEBUG_ANY, "slapd got shutdown signal\n", 0, 0, 0 );
#endif
	g_set_shutdown( SLAPI_SHUTDOWN_SIGNAL );
#ifndef _WIN32
#ifndef LINUX
	/* don't mess with USR1/USR2 on linux, used by libpthread */
	(void) SIGNAL( SIGUSR2, set_shutdown );
#endif
	(void) SIGNAL( SIGTERM, set_shutdown );
	(void) SIGNAL( SIGHUP,  set_shutdown );
#endif
}

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
	LDAPDebug( LDAP_DEBUG_TRACE, "slapd got SIGUSR1\n", 0, 0, 0 );
#endif
#ifndef _WIN32
	(void) SIGNAL( SIGUSR1, slapd_do_nothing );
#endif

#if 0
	/*
	 * Actually do a little more: dump the conn struct and 
	 * send it to a tmp file
	 */
	connection_table_dump(connection_table);
#endif
}
#endif   /* LINUX */

#ifndef _WIN32
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
        LDAPDebug( LDAP_DEBUG_ARGS, "listener: catching SIGCHLD\n", 0, 0, 0 );
#endif
#ifdef USE_WAITPID
        while (waitpid ((pid_t) -1, 0, WAIT_FLAGS) > 0)
#else /* USE_WAITPID */
        while ( wait3( &status, WAIT_FLAGS, 0 ) > 0 )
#endif /* USE_WAITPID */
                ;       /* NULL */

        (void) SIGNAL( SIGCHLD, slapd_wait4child );
}
#endif

#ifdef XP_WIN32
static int
createlistensocket(unsigned short port, const PRNetAddr *listenaddr)
{
	int					tcps;
	struct sockaddr_in	addr;
	char				*logname = "createlistensocket";
	char				addrbuf[ 256 ];

	if (!port) goto suppressed;

	PR_ASSERT( listenaddr != NULL );

	/* create TCP socket */
	if ((tcps = socket(AF_INET, SOCK_STREAM, 0))
		== SLAPD_INVALID_SOCKET) {
		int oserr = errno;

		slapi_log_error(SLAPI_LOG_FATAL, logname,
			"socket() failed: OS error %d (%s)\n",
			oserr, slapd_system_strerror( oserr ));
		goto failed;
	}
	
	/* initialize listener address */
	(void) memset( (void *) &addr, '\0', sizeof(addr) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( port );
	if (listenaddr->raw.family == PR_AF_INET) {
		addr.sin_addr.s_addr = listenaddr->inet.ip;
	} else if (PR_IsNetAddrType(listenaddr,PR_IpAddrAny)) {
		addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		if (!PR_IsNetAddrType(listenaddr,PR_IpAddrV4Mapped)) {
			/*
			 * When Win32 supports IPv6, we will be able to use IPv6
			 * addresses here. But not yet.
			 */
			slapi_log_error(SLAPI_LOG_FATAL, logname,
					"unable to listen on %s port %d (IPv6 addresses "
					"are not supported on this platform)\n",
					netaddr2string(listenaddr, addrbuf, sizeof(addrbuf)),
					port );
			goto failed;
		}

		addr.sin_addr.s_addr = listenaddr->ipv6.ip.pr_s6_addr32[3];
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "%s - binding to %s:%d\n",
	    logname, inet_ntoa( addr.sin_addr ), port )

	if ( bind( tcps, (struct sockaddr *) &addr, sizeof(addr) ) == -1 ) {
		int oserr = errno;

		slapi_log_error(SLAPI_LOG_FATAL, logname,
			"bind() on %s port %d failed: OS error %d (%s)\n",
			inet_ntoa( addr.sin_addr ), port, oserr,
			slapd_system_strerror( oserr ));
		goto failed;
	}

	return tcps;

failed:
	WSACleanup();
	exit( 1 );
suppressed:
	return -1;
}  /* createlistensocket */
#endif   /* XP_WIN32 */


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
		slapi_log_error(SLAPI_LOG_FATAL, logname,
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
			slapi_log_error(SLAPI_LOG_FATAL, logname,
		    	"PR_OpenTCPSocket(%s) failed: %s error %d (%s)\n",
		    	socktype_str,
		    	SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
			goto failed;
		}

		if ( PR_SetSocketOption(sock[i], &pr_socketoption ) == PR_FAILURE) {
			prerr = PR_GetError();
			slapi_log_error(SLAPI_LOG_FATAL, logname,
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
				slapi_log_error(SLAPI_LOG_FATAL, logname,
					"PR_Bind() on %s port %d failed: %s error %d (%s)\n",
					netaddr2string(&sa_server, addrbuf, sizeof(addrbuf)), port,
					SLAPI_COMPONENT_NAME_NSPR, prerr, slapd_pr_strerror(prerr));
			}
#if defined(ENABLE_LDAPI)
			else
			{
				slapi_log_error(SLAPI_LOG_FATAL, logname,
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
			slapi_log_error(SLAPI_LOG_FATAL, logname, "err: %d", errno);
		}
	}
#endif /* ENABLE_LDAPI */

	return( sock );

failed:
#ifdef XP_WIN32
	WSACleanup();
#endif   /* XP_WIN32 */
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
			slapi_log_error( SLAPI_LOG_FATAL, logname,
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
				slapi_log_error( SLAPI_LOG_FATAL, logname,
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
						LDAPDebug2Args(LDAP_DEBUG_ANY, 
						               "slapd_listenhost2addr: "
						               "detected duplicated address %s "
						               "[%s]\n", abuf, abp);
					} else {
						LDAPDebug1Arg(LDAP_DEBUG_TRACE,
						              "slapd_listenhost2addr: "
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
			slapi_log_error( SLAPI_LOG_FATAL, logname,
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
#if defined( _WIN32 )
	if ( PR_NewTCPSocketPair(&signalpipe[0])) {
		PRErrorCode prerr = PR_GetError();
		LDAPDebug( LDAP_DEBUG_ANY, "PR_CreatePipe() failed, "
			SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
			prerr, slapd_pr_strerror(prerr), SLAPD_DEFAULT_THREAD_STACKSIZE );
		return( -1 );
	}
	writesignalpipe = PR_FileDesc2NativeHandle(signalpipe[1]);
	readsignalpipe = PR_FileDesc2NativeHandle(signalpipe[0]);
#else
	if ( PR_CreatePipe( &signalpipe[0], &signalpipe[1] ) != 0 ) {
		PRErrorCode prerr = PR_GetError();
		LDAPDebug( LDAP_DEBUG_ANY, "PR_CreatePipe() failed, "
			SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
		    prerr, slapd_pr_strerror(prerr), SLAPD_DEFAULT_THREAD_STACKSIZE );
		return( -1 );
	}
	writesignalpipe = PR_FileDesc2NativeHandle(signalpipe[1]);
	readsignalpipe = PR_FileDesc2NativeHandle(signalpipe[0]);
	if(fcntl(writesignalpipe, F_SETFD, O_NONBLOCK) == -1){
		LDAPDebug( LDAP_DEBUG_ANY,"createsignalpipe: failed to set FD for write pipe (%d).\n",
				errno, 0, 0 );
	}
	if(fcntl(readsignalpipe, F_SETFD, O_NONBLOCK) == -1){
		LDAPDebug( LDAP_DEBUG_ANY,"createsignalpipe: failed to set FD for read pipe (%d).\n",
				errno, 0, 0);
	}
#endif

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
            LDAPDebug( LDAP_DEBUG_ANY, "catch_signals: sigwait returned -1\n",
                    0, 0, 0 );
            continue;
        } else {
            LDAPDebug( LDAP_DEBUG_TRACE, "catch_signals: detected signal %d\n",
                    sig, 0, 0 );
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
                LDAPDebug( LDAP_DEBUG_ANY,
                    "catch_signals: unknown signal (%d) received\n",
                    sig, 0, 0 );
            }
        }
    }
}
#endif /* HPUX */
 
static int
get_configured_connection_table_size()
{
	int size;
	size = config_get_conntablesize();

/*
 * Cap the table size at nsslapd-maxdescriptors.
 */
#if !defined(_WIN32) && !defined(AIX)
	{
		int maxdesc = config_get_maxdescriptors();

		if ( maxdesc >= 0 && size > maxdesc ) {
			size = maxdesc;
		}
	}
#endif

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
  
#if defined(LINUX)
	/* On Linux we use TCP_CORK so we must enable nagle */
	enable_nagle = 1;
#endif

	ns = PR_FileDesc2NativeHandle( *pr_socket );
	
#if !defined(_WIN32)
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
				slapi_log_error( SLAPI_LOG_FATAL, "configure_pr_socket",
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
			slapi_log_error(SLAPI_LOG_FATAL, "configure_pr_socket",
				"Unable to move socket file descriptor %d above %d:"
				" OS error %d (%s)\n", ns, reservedescriptors, oserr,
				slapd_system_strerror( oserr ) );
		}
	}
#endif /* !_WIN32 */

	/* Set keep_alive to keep old connections from lingering */
	pr_socketoption.option = PR_SockOpt_Keepalive;
	pr_socketoption.value.keep_alive = 1;
	if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE ) {
		PRErrorCode prerr = PR_GetError();
		LDAPDebug( LDAP_DEBUG_ANY,
				"PR_SetSocketOption(PR_SockOpt_Keepalive failed, "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror(prerr), 0 );
	}

	if ( secure ) {
	  
		pr_socketoption.option = PR_SockOpt_Nonblocking;
		pr_socketoption.value.non_blocking = 0;
		if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE ) {
			PRErrorCode prerr = PR_GetError();
			LDAPDebug( LDAP_DEBUG_ANY,
					"PR_SetSocketOption(PR_SockOpt_Nonblocking) failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_pr_strerror(prerr), 0 );
		}
	} else {
		/* We always want to have non-blocking I/O */
			pr_socketoption.option = PR_SockOpt_Nonblocking;
			pr_socketoption.value.non_blocking = 1;
			if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE ) {
			     PRErrorCode prerr = PR_GetError();
			     LDAPDebug( LDAP_DEBUG_ANY,
					"PR_SetSocketOption(PR_SockOpt_Nonblocking) failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_pr_strerror(prerr), 0 );
			}
		 
		 if ( have_send_timeouts ) {
		        daemon_configure_send_timeout(ns,config_get_ioblocktimeout());
		 }

	} /* else (secure) */


	if ( !enable_nagle && !local ) {

		 pr_socketoption.option = PR_SockOpt_NoDelay;
		 pr_socketoption.value.no_delay = 1;
		 if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE) {
			PRErrorCode prerr = PR_GetError();
			LDAPDebug( LDAP_DEBUG_ANY,
				   "PR_SetSocketOption(PR_SockOpt_NoDelay) failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_pr_strerror( prerr ), 0 );
		 }
	} else if( !local) {
		 pr_socketoption.option = PR_SockOpt_NoDelay;
		 pr_socketoption.value.no_delay = 0;
		 if ( PR_SetSocketOption( *pr_socket, &pr_socketoption ) == PR_FAILURE) {
			PRErrorCode prerr = PR_GetError();
			LDAPDebug( LDAP_DEBUG_ANY,
				   "PR_SetSocketOption(PR_SockOpt_NoDelay) failed, "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					prerr, slapd_pr_strerror( prerr ), 0 );
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

	if ( have_send_timeouts ) {
		daemon_configure_send_timeout( *ns, config_get_ioblocktimeout() );
	}
	/* set the nagle */
	if ( !enable_nagle ) {
		on = 1;
	} else {
		on = 0;
	}
	/* check for errors */
	if((rc = setsockopt( *ns, IPPROTO_TCP, TCP_NODELAY, (char * ) &on, sizeof(on) ) != 0)){
		LDAPDebug( LDAP_DEBUG_ANY,"configure_ns_socket: Failed to configure socket (%d).\n", rc, 0, 0);
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
disk_monitoring_stop()
{
	if ( disk_thread_p ) {
		PR_Lock( diskmon_mutex );
		PR_NotifyCondVar( diskmon_cvar );
		PR_Unlock( diskmon_mutex );
	}
}
