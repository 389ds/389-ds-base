/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#if defined(NET_SSL)
#include <ldap.h>
#undef OFF
#undef LITTLE_ENDIAN
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#if !defined(_WIN32) && !defined(aix)
#include <sys/fcntl.h>
#else
#include <fcntl.h>
#endif
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>
#if defined( _WIN32 )
#include "ntslapdmessages.h"
#include "proto-ntutil.h"
#else
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h> /* getpwnam */
#if !defined(IRIX) && !defined(LINUX)
union semun {
    int val;
    struct semid_ds *buf;
    ushort *array;
};
#endif
#endif
#if !defined(_WIN32)
#include <unistd.h> /* dup2 */
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/param.h> /* MAXPATHLEN */
#endif
#if defined(__sun)
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#endif
#include "slap.h"
#include "slapi-plugin.h"
#include "prinit.h"
#include "snmp_collator.h"
#include "fe.h" /* client_auth_init() */
#include "protect_db.h"
#include "getopt_ext.h"
#include "fe.h"

#ifndef LDAP_DONT_USE_SMARTHEAP
#include "smrtheap.h"
#endif

#if defined( XP_WIN32 )
void dostounixpath(char *szText);
#endif

/* Forward Declarations */
static void register_objects();
static void process_command_line(int argc, char **argv, char *myname, char **extraname);
static int slapd_exemode_ldif2db();
static int slapd_exemode_db2ldif(int argc, char **argv);
static int slapd_exemode_db2index();
static int slapd_exemode_archive2db();
static int slapd_exemode_db2archive();
#if defined(UPGRADEDB)
static int slapd_exemode_upgradedb();
#endif
static int slapd_exemode_dbtest();
static int slapd_exemode_suffix2instance();
static int slapd_debug_level_string2level( const char *s );
static void slapd_debug_level_log( int level );
static void slapd_debug_level_usage( void );
static void cmd_set_shutdown(int);
/*
 * global variables
 */

static int slapd_exemode = SLAPD_EXEMODE_UNKNOWN;

static int init_cmd_shutdown_detect()
{

#ifndef _WIN32
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

#if defined ( HPUX10 )
    PR_CreateThread ( PR_USER_THREAD,
                      catch_signals,
                      NULL,
                      PR_PRIORITY_NORMAL,
                      PR_GLOBAL_THREAD,
                      PR_UNJOINABLE_THREAD,
                      SLAPD_DEFAULT_THREAD_STACKSIZE);
#elif defined ( HPUX11 )
        /* In the optimized builds for HPUX, the signal handler doesn't seem
         * to get set correctly unless the primordial thread gets a chance
         * to run before we make the call to SIGNAL.  (At this point the
         * the primordial thread has spawned the daemon thread which called
         * this function.)  The call to DS_Sleep will give the primordial
         * thread a chance to run. */
        DS_Sleep(0);
#endif
#ifndef _WIN32
        (void) SIGNAL( SIGPIPE, SIG_IGN );
        (void) SIGNAL( SIGCHLD, slapd_wait4child );
#ifndef LINUX
        /* linux uses USR1/USR2 for thread synchronization, so we aren't
         * allowed to mess with those.
         */
        (void) SIGNAL( SIGUSR1, slapd_do_nothing );
        (void) SIGNAL( SIGUSR2, cmd_set_shutdown );
#endif
        (void) SIGNAL( SIGTERM, cmd_set_shutdown );
        (void) SIGNAL( SIGHUP,  cmd_set_shutdown );
        (void) SIGNAL( SIGINT,  cmd_set_shutdown );
#endif /* _WIN32 */
        return 0;
}

static void
cmd_set_shutdown (int sig)
{
    /* don't log anything from a signal handler:
     * you could be holding a lock when the signal was trapped.  more
     * specifically, you could be holding the logfile lock (and deadlock
     * yourself).
     */

#if 0
    LDAPDebug( LDAP_DEBUG_ANY, "slapd got shutdown signal\n", 0, 0, 0 );
#endif
        c_set_shutdown();
#ifndef _WIN32
#ifndef LINUX
        /* don't mess with USR1/USR2 on linux, used by libpthread */
        (void) SIGNAL( SIGUSR2, cmd_set_shutdown );
#endif
        (void) SIGNAL( SIGTERM, cmd_set_shutdown );
        (void) SIGNAL( SIGHUP,  cmd_set_shutdown );
#endif
}

#ifdef HPUX10
extern void collation_init();
#endif

#ifndef WIN32

/* Changes the ownership of the given file/directory iff not
   already the owner
   Returns 0 upon success or non-zero otherwise, usually -1 if
   some system error occurred
*/
static int
chown_if_not_owner(const char *filename, uid_t uid, gid_t gid)
{
	struct stat statbuf;
	int result = 1;
	if (!filename)
		return result;

	memset(&statbuf, '\0', sizeof(statbuf));
	if (!(result = stat(filename, &statbuf)))
	{
		if (((uid != -1) && (uid != statbuf.st_uid)) ||
			((gid != -1) && (gid != statbuf.st_gid)))
		{
			result = chown(filename, uid, gid);
		}
	}

	return result;
}

/* 
   Four cases:
    - change ownership of all files in directory (strip_fn=PR_FALSE)
    - change ownership of all files in directory; but trailing fn needs to be stripped (strip_fn=PR_TRUE)
    - fn is relative to root directory (/access); we print error message and let user shoot his foot
    - fn is relative to current directory (access); we print error message and let user shoot his other foot

    The docs say any valid filename.
*/

static void
chown_dir_files(char *name, struct passwd *pw, PRBool strip_fn)
{
  PRDir *dir;
  PRDirEntry *entry;
  char dirname[MAXPATHLEN + 1], file[MAXPATHLEN + 1];
  char *log=NULL, *ptr=NULL;
  int rc=0;

  log=strdup(name);
  if(strip_fn) 
  {
    if((ptr=strrchr(log,'/'))==NULL)
    {
      LDAPDebug(LDAP_DEBUG_ANY, "Caution changing ownership of ./%s \n",name,0,0);
      chown_if_not_owner(log, pw->pw_uid, -1 ); 
      rc=1;
    } else if(log==ptr) {
      LDAPDebug(LDAP_DEBUG_ANY, "Caution changing ownership of / directory and its contents to %s\n",pw->pw_name,0,0);
      *(++ptr)='\0';
    } else {
      *ptr='\0';
    }
  }   
  if ((!rc) && ((dir = PR_OpenDir(log)) != NULL ))
  {
    /* change the owner for each of the files in the dir */
    while( (entry = PR_ReadDir(dir , PR_SKIP_BOTH )) !=NULL ) 
    {
	sprintf(file,"%s/%s",log,entry->name);
	chown_if_not_owner( file, pw->pw_uid, -1 ); 
    }
    PR_CloseDir( dir );
  }
  free(log);
}

/* Changes the owner of the files in the logs and
 * config directorys to the user that the server runs as. 
*/

static void
fix_ownership() 
{
	int len, n;
	struct passwd* pw=NULL;
	char dirname[MAXPATHLEN + 1];

	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();


	if ( slapdFrontendConfig->localuser != NULL )  {
	        if ( (pw = getpwnam( slapdFrontendConfig->localuser )) == NULL ) 
		      return;
	}
	else {
		return;
	}

	/* The instance directory needs to be owned by the local user */
	chown_if_not_owner( slapdFrontendConfig->instancedir, pw->pw_uid, -1 );
	sprintf(dirname,"%s/config",slapdFrontendConfig->instancedir);
	chown_dir_files(dirname, pw, PR_FALSE); /* config directory */
	chown_dir_files(slapdFrontendConfig->accesslog, pw, PR_TRUE); /* do access log directory */
	chown_dir_files(slapdFrontendConfig->auditlog, pw, PR_TRUE);  /* do audit log directory */
	chown_dir_files(slapdFrontendConfig->errorlog, pw, PR_TRUE);  /* do error log directory */

}
#endif  

/* Changes identity to the named user 
 * If username == NULL, does nothing.
 * Does nothing on NT regardless. 
 */
static int main_setuid(char *username)
{
#ifndef _WIN32
	if (username != NULL) {
	    struct passwd *pw;
	    /* Make sure everything in the log and config directory 
	     * is owned by the correct user */
	    fix_ownership();
	    pw = getpwnam (username);
	    if (pw == NULL) {
		int oserr = errno;

		LDAPDebug (LDAP_DEBUG_ANY, "getpwnam(%s) == NULL, error %d (%s)\n",
			   username, oserr, slapd_system_strerror(oserr));
	    } else {
		if (setgid (pw->pw_gid) != 0) {
		    int oserr = errno;

		    LDAPDebug (LDAP_DEBUG_ANY, "setgid(%li) != 0, error %d (%s)\n",
			       (long)pw->pw_gid, oserr, slapd_system_strerror(oserr));
			return -1;
		}
		if (setuid (pw->pw_uid) != 0) {
		    int oserr = errno;

		    LDAPDebug (LDAP_DEBUG_ANY, "setuid(%li) != 0, error %d (%s)\n",
			       (long)pw->pw_uid, oserr, slapd_system_strerror(oserr));
			return -1;
		}
	    }
	}
#endif
	return 0;
}

/* set good defaults for front-end config in referral mode */
static void referral_set_defaults(void)
{
#if !defined(_WIN32) && !defined(AIX)
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    config_set_maxdescriptors( CONFIG_MAXDESCRIPTORS_ATTRIBUTE, "1024", errorbuf, 1);
#endif
}

static int
name2exemode( char *progname, char *s, int exit_if_unknown )
{
	int	exemode;

	if ( strcmp( s, "db2ldif" ) == 0 ) {
		exemode = SLAPD_EXEMODE_DB2LDIF;
	} else if ( strcmp( s, "ldif2db" ) == 0 ) {
		exemode = SLAPD_EXEMODE_LDIF2DB;
	} else if ( strcmp( s, "archive2db" ) == 0 ) {
		exemode = SLAPD_EXEMODE_ARCHIVE2DB;
	} else if ( strcmp( s, "db2archive" ) == 0 ) {
		exemode = SLAPD_EXEMODE_DB2ARCHIVE;
	} else if ( strcmp( s, "server" ) == 0 ) {
		exemode = SLAPD_EXEMODE_SLAPD;
	} else if ( strcmp( s, "dbtest" ) == 0 ) {
		exemode = SLAPD_EXEMODE_DBTEST;
	} else if ( strcmp( s, "db2index" ) == 0 ) {
		exemode = SLAPD_EXEMODE_DB2INDEX;
	} else if ( strcmp( s, "refer" ) == 0 ) {
		exemode = SLAPD_EXEMODE_REFERRAL;
	} else if ( strcmp( s, "suffix2instance" ) == 0 ) {
		exemode = SLAPD_EXEMODE_SUFFIX2INSTANCE;
	}
#if defined(UPGRADEDB)
	else if ( strcmp( s, "upgradedb" ) == 0 )
	{
		exemode = SLAPD_EXEMODE_UPGRADEDB;
	}
#endif
	else if ( exit_if_unknown ) {
		fprintf( stderr, "usage: %s -D instancedir "
				 "[ldif2db | db2ldif | archive2db "
				 "| db2archive | db2index | refer | suffix2instance"
#if defined(UPGRADEDB)
				 " | upgradedb] "
#else
				 "] "
#endif
				 "[options]\n", progname );
		exit( 1 );
	} else {
		exemode = SLAPD_EXEMODE_UNKNOWN;
	}

	return( exemode );
}


static void
usage( char *name, char *extraname )
{
    char *usagestr = NULL;
    char *extraspace;

    if ( extraname == NULL ) {
	extraspace = extraname = "";
    } else {
	extraspace = " ";
    }
	
    switch( slapd_exemode ) {
    case SLAPD_EXEMODE_DB2LDIF:
	usagestr = "usage: %s %s%s-D instancedir [-n backend-instance-name] [-d debuglevel] "
		"[-N] [-a outputfile] [-r] [-C] [{-s includesuffix}*] "
		"[{-x excludesuffix}*] [-u] [-U] [-m] [-M] [-E]\n"
		"Note: either \"-n backend_instance_name\" or \"-s includesuffix\" is required.\n";
	break;
    case SLAPD_EXEMODE_LDIF2DB:
	usagestr = "usage: %s %s%s-D instancedir [-d debuglevel] "
		"[-n backend_instance_name] [-O] [-g uniqueid_type] [--namespaceid uniqueID]"
		"[{-s includesuffix}*] [{-x excludesuffix}*]  [-E] {-i ldif-file}*\n"
		"Note: either \"-n backend_instance_name\" or \"-s includesuffix\" is required.\n";
	break;
    case SLAPD_EXEMODE_DB2ARCHIVE:
	usagestr = "usage: %s %s%s-D instancedir [-d debuglevel] -a archivedir\n";
	break;
    case SLAPD_EXEMODE_ARCHIVE2DB:
	usagestr = "usage: %s %s%s-D instancedir [-d debuglevel] -a archivedir\n";
	break;
    case SLAPD_EXEMODE_DB2INDEX:
	usagestr = "usage: %s %s%s-D instancedir -n backend-instance-name "
		"[-d debuglevel] {-t attributetype}* {-T VLV Search Name}*\n";
	/* JCM should say 'Address Book' or something instead of VLV */
	break;
    case SLAPD_EXEMODE_REFERRAL:
	usagestr = "usage: %s %s%s-D instancedir -r referral-url [-p port]\n";
	break;
    case SLAPD_EXEMODE_DBTEST:
	usagestr = "usage: %s %s%s-D instancedir -n backend-instance-name "
		"[-d debuglevel] [-S] [-v]\n";
	break;
    case SLAPD_EXEMODE_SUFFIX2INSTANCE:
	usagestr = "usage: %s %s%s -D instancedir {-s suffix}*\n";
	break;
#if defined(UPGRADEDB)
    case SLAPD_EXEMODE_UPGRADEDB:
	usagestr = "usage: %s %s%s-D instancedir [-d debuglevel] [-f] -a archivedir\n";
	break;
#endif

    default:	/* SLAPD_EXEMODE_SLAPD */
	usagestr = "usage: %s %s%s-D instancedir [-d debuglevel] "
		"[-i pidlogfile] [-v] [-V]\n";
    }

    fprintf( stderr, usagestr, name, extraname, extraspace );
}


/*
 * These nasty globals are the settings collected from the
 * command line by the process_command_line function. The
 * various slapd_exemode functions read these to drive their
 * execution.
 */
static char *extraname;
static char *myname;
static int n_port = 0;
static int s_port = 0;
static char **ldif_file = NULL;
static int ldif_files = 0;
static int ldif_backend = 0;
static char *cmd_line_instance_name = NULL;
static char **cmd_line_instance_names = NULL;
static int skip_db_protect_check = 0;
static char **db2ldif_include = NULL;
static char **db2ldif_exclude = NULL;
static int ldif2db_removedupvals = 1;
static int ldif2db_noattrindexes = 0;
static char **db2index_attrs = NULL;
static int ldif_printkey = EXPORT_PRINTKEY|EXPORT_APPENDMODE;
static char *archive_name = NULL;
static int db2ldif_dump_replica = 0;
static int db2ldif_dump_uniqueid = 1;
static int ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_TIME_BASED;	
static int ldif2db_load_state= 1;
static char *ldif2db_namespaceid = NULL;
int importexport_encrypt = 0;
#if defined(UPGRADEDB)
static int upgradedb_force = 0;
#endif

/* taken from idsktune */
#if defined(__sun)
static void ids_get_platform_solaris(char *buf)
{
    struct utsname u;
    char sbuf[128];
    FILE *fp;
  
#if defined(sparc) || defined(__sparc)
    int is_u = 0;
    
    sbuf[0] = '\0';
    sysinfo(SI_MACHINE,sbuf,128);
    
    if (strcmp(sbuf,"sun4u") == 0) {
      is_u = 1;
    }
    
    sbuf[0] = '\0';
    sysinfo(SI_PLATFORM,sbuf,128);
    
    sprintf(buf,"%ssparc%s-%s-solaris",
	    is_u ? "u" : "",
	    sizeof(long) == 4 ? "" : "v9",
	    sbuf);
#else
#if defined(i386) || defined(__i386)
    sprintf(buf,"i386-unknown-solaris");
#else
    sprintf(buf,"unknown-unknown-solaris");
#endif /* not i386 */
#endif /* not sparc */

    uname(&u);
    if (isascii(u.release[0]) && isdigit(u.release[0])) strcat(buf,u.release);

    fp = fopen("/etc/release","r");

    if (fp != NULL) {
      char *rp;

      sbuf[0] = '\0';
      fgets(sbuf,128,fp);
      fclose(fp);
      rp = strstr(sbuf,"Solaris");
      if (rp) {
	rp += 8;
	while(*rp != 's' && *rp != '\0') rp++;
	if (*rp == 's') {
	  char *rp2;
	  rp2 = strchr(rp,' ');
	  if (rp2) *rp2 = '\0';
	  strcat(buf,"_");
	  strcat(buf,rp);
	}
      }
    }
}
#endif

static void slapd_print_version(int verbose)
{
#if defined(__sun)
	char buf[8192];
#endif
  	char *versionstring = config_get_versionstring();
	char *buildnum = config_get_buildnum();

	printf( SLAPD_VENDOR_NAME "\n%s B%s\n", versionstring, buildnum);

	/* not here in Win32 */
#if !defined(_WIN32)
	if (strcmp(buildnum,BUILD_NUM) != 0) {
	  printf( "ns-slapd: B%s\n", BUILD_NUM);
	}
#endif

	slapi_ch_free( (void **)&versionstring);
	slapi_ch_free( (void **)&buildnum);

	if (verbose == 0) return;

#if defined(__sun)
	ids_get_platform_solaris(buf);
	printf("System: %s\n",buf);
#endif
	
	/* this won't print much with the -v flag as the dse.ldif file 
	 * hasn't be read yet. 
	 */
	plugin_print_versions();
}

#if defined( _WIN32 )
/* On Windows, we signal the SCM when we're still starting up */
static int
write_start_pid_file()
{
	if( SlapdIsAService() )
	{
		/* Initialization complete and successful. Set service to running */
		LDAPServerStatus.dwCurrentState	= SERVICE_START_PENDING;
		LDAPServerStatus.dwCheckPoint = 1;
		LDAPServerStatus.dwWaitHint = 1000;

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
write_start_pid_file()
{
	FILE *fp = NULL;
	/*
	 * The following section of code is closely coupled with the
	 * admin programs. Please do not make changes here without
	 * consulting the start/stop code for the admin code.
	 */
	if ( (fp = fopen( start_pid_file, "w" )) != NULL ) {
		fprintf( fp, "%d\n", getpid() );
		fclose( fp );
		return 0;
	} else
	{
		return -1;
	}
}
#endif /* WIN32 */

int
main( int argc, char **argv)
{
	int return_value = 0;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	daemon_ports_t arg = {0};
   	Slapi_Backend *be = NULL;
	int init_ssl;
#ifndef __LP64__ 
#if defined(__hpux)
	/* for static constructors */
	_main();
#endif
#endif
	/*
	 * Initialize NSPR very early. NSPR supports implicit initialization,
	 * but it is not bulletproof -- so it is better to be explicit.
	 */
	PR_Init( PR_USER_THREAD, PR_PRIORITY_NORMAL, 0 );
	FrontendConfig_init();
	
#ifdef _WIN32
	/* Break into the debugger if DEBUG_BREAK is set in the environment
	   to "slapd" */
	{
		char *s = getenv( "DEBUG_BREAK" );
		if ( (s != NULL) && !stricmp(s, "slapd") )
			DebugBreak();
	}

	/* do module debug level init for slapd, and libslapd */
	module_ldap_debug = &slapd_ldap_debug;
	libldap_init_debug_level(&slapd_ldap_debug);
  
	dostounixpath( argv[0] );
	_strlwr( argv[0] );

#else /* _WIN32 */
	/* Pause for the debugger if DEBUG_SLEEP is set in the environment */
	{
		char *s = getenv( "DEBUG_SLEEP" );
		if ( (s != NULL) && isdigit(*s) ) {
			int secs = atoi(s);
			printf("slapd pid is %d\n", getpid());
			sleep(secs);
		}
	}


/* used to set configfile to the default config file name here */

#endif /* _WIN32 */

	if ( (myname = strrchr( argv[0], '/' )) == NULL ) {
		myname = slapi_ch_strdup( argv[0] );
	} else {
		myname = slapi_ch_strdup( myname + 1 );
	}

#if defined( XP_WIN32 )
	/* Strip ".exe" if it's there */
	{
	char *pdot;
	if ( (pdot = strrchr( myname, '.' )) != NULL ) {
		*pdot = '\0';
	}
	}
#endif

    process_command_line(argc,argv,myname,&extraname);

	if (!slapdFrontendConfig->instancedir ||
		!slapdFrontendConfig->configdir) {
		usage( myname, extraname );
		exit( 1 );
	}


	/* display debugging level if it is anything other than the default */
	if ( 0 != ( slapd_ldap_debug & ~LDAP_DEBUG_ANY )) {
			slapd_debug_level_log( slapd_ldap_debug );
	}

#ifndef LDAP_DONT_USE_SMARTHEAP
        MemRegisterTask();
#endif

	slapd_init();
	g_log_init(1);
	vattr_init();

	if (slapd_exemode == SLAPD_EXEMODE_REFERRAL) {
	    /* make up the config stuff */
	    referral_set_defaults();
	    n_port = config_get_port();
	    s_port = config_get_secureport();
   		register_objects();

	} else {
    	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	/* The 2 calls below have been moved to this place to make sure that they
	 * are called before setup_internal_backends to avoid bug 524439 */
	/*
	 * The 2 calls below where being sometimes called AFTER ldapi_register_extended_op
	 * (such fact was being stated and reproducible for some optimized installations
	 * at startup (bug 524439)... Such bad call was happening in the context of
	 * setup_internal_backends -> dse_read_file -> load_plugin_entry ->
	 * plugin_setup -> replication_multimaster_plugin_init ->
	 * slapi_register_plugin -> plugin_setup -> multimaster_start_extop_init ->
	 * slapi_pblock_set -> ldapi_register_extended_op... Unfortunately, the server
	 * design is such that it is assumed that ldapi_init_extended_ops is always
	 * called first.
	 * THE FIX: Move the two calls below before a call to setup_internal_backends
	 * (down in this same function)
	 */
	init_saslmechanisms();
	ldapi_init_extended_ops();

		
	    /*
	     * Initialize the default backend.  This should be done before we
	     * process the config. files
	     */
	    defbackend_init();
	    
		/*
		 * Register the extensible objects with the factory.
		 */
   		register_objects();
		/* 
		 * Register the controls that we support.
		 */
    	init_controls();

	    /*
	     * Process the config files.
	     */
	    if (0 == slapd_bootstrap_config(slapdFrontendConfig->configdir)) {
			slapi_log_error(SLAPI_LOG_FATAL, "startup",
							"The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
							slapdFrontendConfig->configdir);
			exit(1);
		}

		/* -sduloutre: must be done before any internal search */
        /* do it before splitting off to other modes too -robey */
		/* -richm: must be done before reading config files */
		if (0 != (return_value = compute_init())) {
			LDAPDebug(LDAP_DEBUG_ANY, "Initialization Failed 0 %d\n",return_value,0,0);
			exit (1);
		}
		entry_computed_attr_init();

		if (0 == setup_internal_backends(slapdFrontendConfig->configdir)) {
			slapi_log_error(SLAPI_LOG_FATAL, "startup",
							"The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
							slapdFrontendConfig->configdir);
			exit(1);
		}

	    n_port = config_get_port();
	    s_port = config_get_secureport();
	}

	raise_process_limits();	/* should be done ASAP once config file read */

#ifdef PUMPKIN_HOUR
	if ( time( NULL ) > (PUMPKIN_HOUR - 10) ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		"ERROR: ** This beta software has expired **\n", 0, 0, 0 );
		exit( 1 );
	}
#endif

	/* Set entry points in libslapd */
	set_entry_points();

	/*
	 * Initialise NSS once for the whole slapd process, whether SSL
	 * is enabled or not. We use NSS for random number generation and
	 * other things even if we are not going to accept SSL connections.
	 * We also need NSS for attribute encryption/decryption on import and export.
	 */
	init_ssl = ( (slapd_exemode == SLAPD_EXEMODE_SLAPD) || importexport_encrypt) 
				&& config_get_security()
				&& (0 != s_port) && (s_port <= LDAP_PORT_MAX);
	/* As of DS 6.1, always do a full initialization so that other
	 * modules can assume NSS is available
     */
	if ( slapd_nss_init((slapd_exemode == SLAPD_EXEMODE_SLAPD),
			(slapd_exemode != SLAPD_EXEMODE_REFERRAL) /* have config? */ )) {
		 LDAPDebug(LDAP_DEBUG_ANY,
					"ERROR: NSS Initialization Failed.\n", 0, 0, 0);
		 exit (1);
	}

	if (slapd_exemode == SLAPD_EXEMODE_SLAPD) {
		client_auth_init();
	}

	if ( init_ssl && ( 0 != slapd_ssl_init())) {
		LDAPDebug(LDAP_DEBUG_ANY,
					"ERROR: SSL Initialization Failed.\n", 0, 0, 0 );
		exit( 1 );
	}

	/*
	 * if we were called upon to do special database stuff, do it and be
	 * done.
	 */
	switch ( slapd_exemode ) {
	case SLAPD_EXEMODE_LDIF2DB:
	    return slapd_exemode_ldif2db();

	case SLAPD_EXEMODE_DB2LDIF:
	    return slapd_exemode_db2ldif(argc,argv);

	case SLAPD_EXEMODE_DB2INDEX:
	    return slapd_exemode_db2index();

	case SLAPD_EXEMODE_ARCHIVE2DB:
	    return slapd_exemode_archive2db();

	case SLAPD_EXEMODE_DB2ARCHIVE:
	    return slapd_exemode_db2archive();

	case SLAPD_EXEMODE_DBTEST:
	    return slapd_exemode_dbtest();
		
	case SLAPD_EXEMODE_REFERRAL:
		/* check that all the necessary info was given, then go on */
        if (! config_check_referral_mode()) {
		    LDAPDebug(LDAP_DEBUG_ANY,
			      "ERROR: No referral URL supplied\n", 0, 0, 0);
			usage( myname, extraname );
		    exit(1);
		}
		break;

	case SLAPD_EXEMODE_SUFFIX2INSTANCE:
	    return slapd_exemode_suffix2instance();

#if defined(UPGRADEDB)
	case SLAPD_EXEMODE_UPGRADEDB:
	    return slapd_exemode_upgradedb();
#endif

	case SLAPD_EXEMODE_PRINTVERSION:
	    slapd_print_version(1);
	    exit(1);
	}

#if defined( XP_WIN32 )
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

	/* Check to ensure there isn't a copy of this server already running. */
	if( MultipleInstances() ) 
		exit( 1 );
#endif

	/*
	 * Detach ourselves from the terminal (unless running in debug mode).
	 * We must detach before we start any threads since detach forks() on
	 * UNIX.
	 */
	detach();

  /*
   * Now write our PID to the startup PID file.
   * This is used by the start up script to determine our PID quickly
   * after we fork, without needing to wait for the 'real' pid file to be
   * written. That could take minutes. And the start script will wait
   * that long looking for it. With this new 'early pid' file, it can avoid
   * doing that, by detecting the pid and watching for the process exiting.
   * This removes the blank stares all round from start-slapd when the server
   * fails to start for some reason
   */
   write_start_pid_file();
		
	/* Make sure we aren't going to run slapd in 
	 * a mode that is going to conflict with other
 	 * slapd processes that are currently running
 	 */
	if ((slapd_exemode != SLAPD_EXEMODE_REFERRAL) &&
	    ( add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
				    skip_db_protect_check) == -1 ))  {
 		LDAPDebug( LDAP_DEBUG_ANY, 
				"Shutting down due to possible conflicts with other slapd processes\n",
				0, 0, 0 );
		exit(1);
	}


	/*
	 * Now it is safe to log our first startup message.  If we were to
	 * log anything earlier than now it would appear on the admin startup
	 * screen twice because before we detach everything is sent to both
	 * stderr and our error log.  Yuck.
	 */
	if (1) {
	  char *versionstring = config_get_versionstring();
	  char *buildnum = config_get_buildnum();
	  LDAPDebug( LDAP_DEBUG_ANY, "%s B%s starting up\n",
		     versionstring, buildnum, 0 );
	  slapi_ch_free((void **)&buildnum);
	  slapi_ch_free((void **)&versionstring);
	}

	/*
	 * After we read the config file we should make
	 * sure that everything we needed to read in has 
	 * been read in and we'll start whatever threads, 
	 * etc the backends need to start
	 */


	/* Important: up 'till here we could be running as root (on unix).
	 * we believe that we've not created any files before here, otherwise
	 * they'd be owned by root, which is bad. We're about to change identity
	 * to some non-root user, but before we do, we call the daemon code
	 * to let it open the listen sockets. If these sockets are low-numbered,
	 * we need to be root in order to open them. 
	 */

	{
		arg.n_port = (unsigned short)n_port;
		if ( slapd_listenhost2addr( config_get_listenhost(),
				&arg.n_listenaddr ) != 0 ) {
			return(1);
		}

		arg.s_port = (unsigned short)s_port;
		if ( slapd_listenhost2addr( config_get_securelistenhost(),
				&arg.s_listenaddr ) != 0 ) {
			return(1);
		}

		return_value = daemon_pre_setuid_init(&arg);
		if (0 != return_value) {
		    LDAPDebug( LDAP_DEBUG_ANY, "Failed to init daemon\n",
			       0, 0, 0 );
		    exit(1);
		}
	}

	/* Now, sockets are open, so we can safely change identity now */

#ifndef _WIN32
	return_value = main_setuid(slapdFrontendConfig->localuser);
	if (0 != return_value) {
	    LDAPDebug( LDAP_DEBUG_ANY, "Failed to change user and group identity to that of %s\n",
				   slapdFrontendConfig->localuser, 0, 0 );
	    exit(1);
	}
#endif


	/* -sduloutre: compute_init() and entry_computed_attr_init() moved up */

	if (slapd_exemode != SLAPD_EXEMODE_REFERRAL) {
		int rc;
		Slapi_DN *sdn;

		fedse_create_startOK(DSE_FILENAME, DSE_STARTOKFILE,
				slapdFrontendConfig->configdir);

		eq_init();					/* must be done before plugins started */
	    snmp_collator_start();
	    ps_init_psearch_system();   /* must come before plugin_startall() */

		/* Initailize the mapping tree */

		if (mapping_tree_init())
		{
			LDAPDebug( LDAP_DEBUG_ANY, "Failed to init mapping tree\n",
				0, 0, 0 );
        	exit(1);
		}


		/* initialize UniqueID generator - must be done once backends are started
           and event queue is initialized but before plugins are started */
		sdn = slapi_sdn_new_dn_byval ("cn=uniqueid generator,cn=config");
		rc = uniqueIDGenInit (NULL, sdn, slapd_exemode == SLAPD_EXEMODE_SLAPD);
		slapi_sdn_free (&sdn);
		if (rc != UID_SUCCESS)
		{
			LDAPDebug( LDAP_DEBUG_ANY,
				"Fatal Error---Failed to initialize uniqueid generator; error = %d. "
                "Exiting now.\n", rc, 0, 0 );
			exit( 1 );
		}

		/* --ugaston: register the start-tls plugin */
#ifndef _WIN32		
		if ( slapd_security_library_is_initialized() != 0 ) {
	
		
		         start_tls_register_plugin();
			 LDAPDebug( LDAP_DEBUG_PLUGIN, 
				    "Start TLS plugin registered.\n",
				    0, 0, 0 );
		} 
#endif
	    passwd_modify_register_plugin();
	    LDAPDebug( LDAP_DEBUG_PLUGIN, 
				    "Password Modify plugin registered.\n", 0, 0, 0 );

	    plugin_startall(argc, argv, 1 /* Start Backends */, 1 /* Start Globals */); 
		if (housekeeping_start((time_t)0, NULL) == NULL) {
			exit (1);
		}

		eq_start();					/* must be done after plugins started */

#ifdef HPUX10
	    /* HPUX linker voodoo */
	    if (collation_init == NULL) {
		exit (1);
	    }
	    
#endif /* HPUX */

	    normalize_oc();

	    if (n_port) {
#if defined(NET_SSL)
	    } else if ( config_get_security()) {
#endif
	    } else {
#ifdef _WIN32	
    		if( SlapdIsAService() )
    		{
    			LDAPServerStatus.dwCurrentState = SERVICE_STOPPED;
    			LDAPServerStatus.dwCheckPoint = 0;
    			LDAPServerStatus.dwWaitHint = 0;
    			LDAPServerStatus.dwWin32ExitCode = 1;
    			LDAPServerStatus.dwServiceSpecificExitCode = 1;

    			SetServiceStatus(hLDAPServerServiceStatus, &LDAPServerStatus);

    			/* Log this event */
    			ReportSlapdEvent(EVENTLOG_INFORMATION_TYPE, MSG_SERVER_START_FAILED, 1, 
    				"Check server port specification");
    		}
    		else
    		{
    			char szMessage[256];
    			sprintf( szMessage, "The Directory Server %s is terminating due to an error. Check server port specification", pszServerName);
    			MessageBox(GetDesktopWindow(), szMessage,	" ", MB_ICONEXCLAMATION | MB_OK);
    		}
#endif
    		exit(1);
	    }
	}

    {
    	Slapi_PBlock pb;
    	memset( &pb, '\0', sizeof(pb) );
    	pb.pb_backend = be;
	}

	if (slapd_exemode != SLAPD_EXEMODE_REFERRAL) {
		/* else do this after seteuid() */
		lite_entries_init();

		/* setup cn=tasks tree */
		task_init();

		/* pw_init() needs to be here since it uses aci function calls.  */
		pw_init();
		/* Initialize the sasl mapping code */
		if (sasl_map_init()) {
			LDAPDebug( LDAP_DEBUG_ANY, "Failed to initialize sasl mapping code\n", 0, 0, 0 );
		}
	}

	/*
	 * search_register_reslimits() and daemon_register_reslimits() can
	 * be called any time before we start accepting client connections.
	 * We call these even when running in referral mode because they
	 * do little harm and registering at least one resource limit forces
	 * the reslimit subsystem to initialize itself... which prevents
	 * strange error messages from being logged to the error log for
	 * the first LDAP connection.
	 */
	if ( search_register_reslimits() != SLAPI_RESLIMIT_STATUS_SUCCESS ||
				daemon_register_reslimits() != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
		exit( 1 );
	}

	{
		time( &starttime );

		slapd_daemon(&arg);
	}
 	LDAPDebug( LDAP_DEBUG_ANY, "slapd stopped.\n", 0, 0, 0 );
	reslimit_cleanup();
	compute_terminate();
	vattr_cleanup();
	sasl_map_done();
	PR_Cleanup();
#ifdef _WIN32
	/* Clean up the mutex used to interlock processes, before we exit */
	remove_slapd_process();
#endif
#if ( defined( hpux ) || defined( irix ) || defined( aix ) || defined( OSF1 ))
	exit( 0 );
#else
	return 0;
#endif
}


#if ( defined( hpux ) || defined( irix ))
void *
signal2sigaction( int s, void *a )
{
    struct sigaction act;

    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = (VFP)a;
    act.sa_flags = 0;
    (void)sigemptyset( &act.sa_mask );
    (void)sigaddset( &act.sa_mask, s );
    (void)sigaction( s, &act, NULL );
}
#endif /* hpux || irix */

static void
register_objects()
{
	get_operation_object_type();
    daemon_register_connection();
    get_entry_object_type();
    mapping_tree_get_extension_type ();
}

static void
process_command_line(int argc, char **argv, char *myname,
					 char **extraname)
{
	int i;
	char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
	char *opts;
	static struct opt_ext *long_opts;
	int longopt_index=0;

	/* 
	 * Refer to the file getopt_ext.h for an overview of how to use the
	 * long option names
	 *
	 */


	/*
	 * when a new option letter is used, please move it from the "available"
	 * list to the "used" list.
	 *
	 */
	/*
	 * single-letter options already in use:
	 *
	 * a C c D E d f G g i
	 * L l N m n O o P p r S s T t
	 * u v V w x Z z
	 *
	 * 1
	 *
	 */

	/*
	 * single-letter options still available:
	 *
	 * A B b e F H h I J j
	 * K k M Q q R
	 * W  X Y y
	 *
	 * 2 3 4 5 6 7 8 9 0
	 *
	 */

	char *opts_dbtest = "vd:n:SD:";
	struct opt_ext long_options_dbtest[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"backend",ArgRequired,'n'},
		{"allowMultipleProcesses",ArgNone,'S'},
		{"instanceDir",ArgRequired,'D'},
		{0,0,0}};
	
	
	char *opts_db2ldif = "vd:D:ENa:rs:x:CSut:n:UmMo1";
	struct opt_ext long_options_db2ldif[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"dontPrintKey",ArgNone,'n'},
		{"archive",ArgRequired,'a'},
		{"replica",ArgNone,'r'},
		{"include",ArgRequired,'s'},
		{"exclude",ArgRequired,'x'},
		/*{"whatshouldwecallthis",ArgNone,'C'},*/
		{"allowMultipleProcesses",ArgNone,'S'},
		{"noUniqueIds",ArgNone,'u'},
		{"instanceDir",ArgRequired,'D'},
		{"encrypt",ArgOptional,'E'},
		{"nowrap",ArgNone,'U'},
		{"minimalEncode",ArgNone,'m'},
		{"oneOutputFile",ArgNone,'o'},
		{"multipleOutputFile",ArgNone,'M'},
		{"noVersionNum",ArgNone,'1'},
		{0,0,0}};
	
	char *opts_ldif2db = "vd:i:g:G:n:s:x:NOCc:St:D:E"; 
	struct opt_ext long_options_ldif2db[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"ldiffile",ArgRequired,'i'},
		{"generateUniqueId",ArgOptional,'g'},
		{"backend",ArgRequired,'n'},
		{"include",ArgRequired,'s'},
		{"exclude",ArgRequired,'x'},
		{"noindex",ArgNone,'O'},
		/*{"whatshouldwecallthis",ArgNone,'C'},*/
		/*{"whatshouldwecallthis",ArgRequired,'c'},*/
		{"allowMultipleProcesses",ArgNone,'S'},
		{"namespaceid", ArgRequired, 'G'},
		{"nostate",ArgNone,'Z'},
		{"instanceDir",ArgRequired,'D'},
		{"encrypt",ArgOptional,'E'},
		{0,0,0}};

	char *opts_archive2db = "vd:i:a:n:SD:";
	struct opt_ext long_options_archive2db[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"pidfile",ArgRequired,'i'},
		{"archive",ArgRequired,'a'},
		{"backEndInstName",ArgRequired,'n'},
		{"allowMultipleProcesses",ArgNone,'S'},		
		{"instanceDir",ArgRequired,'D'},
		{0,0,0}};


	char *opts_db2archive = "vd:i:a:SD:";
	struct opt_ext long_options_db2archive[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"pidfile",ArgRequired,'i'},
		{"archive",ArgRequired,'a'},
		{"allowMultipleProcesses",ArgNone,'S'},		
		{"instanceDir",ArgRequired,'D'},
		{0,0,0}};

	char *opts_db2index = "vd:a:t:T:SD:n:s:x:"; 
	struct opt_ext long_options_db2index[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"backend",ArgRequired,'n'},
		{"archive",ArgRequired,'a'},
		{"indexAttribute",ArgRequired,'t'},
		{"vlvIndex",ArgRequired,'T'},
		{"allowMultipleProcesses",ArgNone,'S'},		
		{"instanceDir",ArgRequired,'D'},
		{"include",ArgRequired,'s'},
		{"exclude",ArgRequired,'x'},
		{0,0,0}};

#if defined(UPGRADEDB)
	char *opts_upgradedb = "vfd:a:D:"; 
	struct opt_ext long_options_upgradedb[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"force",ArgNone,'f'},
		{"archive",ArgRequired,'a'},
		{"instanceDir",ArgRequired,'D'},
		{0,0,0}};
#endif

	char *opts_referral = "vd:p:r:SD:"; 
	struct opt_ext long_options_referral[] = {
		{"version",ArgNone,'v'},
		{"debug",ArgRequired,'d'},
		{"port",ArgRequired,'p'},
		{"referralMode",ArgRequired,'r'},
		{"allowMultipleProcesses",ArgNone,'S'},		
		{"instanceDir",ArgRequired,'D'},
		{0,0,0}};

	char *opts_suffix2instance = "s:D:";
	struct opt_ext long_options_suffix2instance[] = {
		{"suffix",ArgRequired,'s'},
		{"instanceDir",ArgRequired,'D'},
		{0,0,0}};

	char *opts_slapd = "vVd:i:SD:w:";
	struct opt_ext long_options_slapd[] = {
		{"version",ArgNone,'v'},
		{"versionFull",ArgNone,'V'},
		{"debug",ArgRequired,'d'},
		{"pidfile",ArgRequired,'i'},
		{"allowMultipleProcesses",ArgNone,'S'},		
		{"instanceDir",ArgRequired,'D'},
		{"startpidfile",ArgRequired,'w'},
		{0,0,0}};

	/*
	 * determine which of serveral modes we are executing in.
	 */
	*extraname = NULL;
	if (( slapd_exemode = name2exemode( myname, myname, 0 ))
	    == SLAPD_EXEMODE_UNKNOWN ) {


		if ( argv[1] != NULL && argv[1][0] != '-' ) {
			slapd_exemode = name2exemode( myname, argv[1], 1 );
			*extraname = argv[1];
			optind_ext = 2;	/* make getopt() skip argv[1] */
			optind = 2;
		}
	}
	if ( slapd_exemode == SLAPD_EXEMODE_UNKNOWN ) {
		slapd_exemode = SLAPD_EXEMODE_SLAPD;	/* default */
	}
	/*
	 * richm: If running in regular slapd server mode, allow the front
	 * end dse files (dse.ldif and ldbm.ldif) to be written in case of
	 * additions or modifications.  In all other modes, these files
	 * should only be read and never written.
	 */

	if (slapd_exemode == SLAPD_EXEMODE_SLAPD ||
	    slapd_exemode == SLAPD_EXEMODE_ARCHIVE2DB || /* bak2db adjusts config */
	    slapd_exemode == SLAPD_EXEMODE_UPGRADEDB)        /* update idl-switch */
		dse_unset_dont_ever_write_dse_files();

	/* maintain compatibility with pre-5.x options */
	switch( slapd_exemode ) {
	case SLAPD_EXEMODE_DBTEST:
		opts = opts_dbtest;
		long_opts = long_options_dbtest;
		break;
	case SLAPD_EXEMODE_DB2LDIF:
		opts = opts_db2ldif;
		long_opts = long_options_db2ldif;
		break;
	case SLAPD_EXEMODE_LDIF2DB:
		opts = opts_ldif2db;
		long_opts = long_options_ldif2db;
		break;
	case SLAPD_EXEMODE_ARCHIVE2DB:
		opts = opts_archive2db;
		long_opts = long_options_archive2db;
		break;
	case SLAPD_EXEMODE_DB2ARCHIVE:
		init_cmd_shutdown_detect();
		opts = opts_db2archive;
		long_opts = long_options_db2archive;
		break;
	case SLAPD_EXEMODE_DB2INDEX:
		opts = opts_db2index;
		long_opts = long_options_db2index;
		break;
	case SLAPD_EXEMODE_REFERRAL:
		opts = opts_referral;
		long_opts = long_options_referral;
		break;
	case SLAPD_EXEMODE_SUFFIX2INSTANCE:
		opts = opts_suffix2instance;
		long_opts = long_options_suffix2instance;
		break;
#if defined(UPGRADEDB)
	case SLAPD_EXEMODE_UPGRADEDB:
		opts = opts_upgradedb;
		long_opts = long_options_upgradedb;
		break;
#endif
	default:	/* SLAPD_EXEMODE_SLAPD */
		opts = opts_slapd;
		long_opts = long_options_slapd;
	}

	while ( (i = getopt_ext( argc, argv, opts,
							 long_opts, &longopt_index)) != EOF ) {
		char *instancedir = 0;
		switch ( i ) {
#ifdef LDAP_DEBUG
		case 'd':	/* turn on debugging */
			if ( optarg_ext[0] == '?'
						|| 0 == strcasecmp( optarg_ext, "help" )) {
				slapd_debug_level_usage();
				exit( 1 );
			} else {
				should_detach = 0;
				slapd_ldap_debug = slapd_debug_level_string2level( optarg_ext );
				if ( slapd_ldap_debug < 0 ) {
					slapd_debug_level_usage();
					exit( 1 );
				}
				slapd_ldap_debug |= LDAP_DEBUG_ANY;
			}
			break;
#else
		case 'd':	/* turn on debugging */
			fprintf( stderr,
			    "must compile with LDAP_DEBUG for debugging\n" );
			break;
#endif

		case 'D':	/* config dir */
			instancedir = rel2abspath( optarg_ext );

#if defined( XP_WIN32 )
			pszServerName = slapi_ch_malloc( MAX_SERVICE_NAME );
			if( !SlapdGetServerNameFromCmdline(pszServerName, instancedir, 1) )
			{
				MessageBox(GetDesktopWindow(), "Failed to get the Directory"
					" Server name from the command-line argument.",
					" ", MB_ICONEXCLAMATION | MB_OK);
				exit( 1 );
			}  
#endif
			if ( config_set_instancedir( "instancedir (-D)",
					instancedir, errorbuf, 1) != LDAP_SUCCESS ) {
				fprintf( stderr, "%s: aborting now\n", errorbuf );
			    usage( myname, *extraname );
			    exit( 1 );
			}
			slapi_ch_free((void **)&instancedir);

			break;

		case 'p':	/* port on which to listen (referral mode only) */
		  if ( config_set_port ( "portnumber (-p)", optarg_ext,
					errorbuf, CONFIG_APPLY ) != LDAP_SUCCESS ) {
				fprintf( stderr, "%s: aborting now\n", errorbuf );
			    usage( myname, *extraname );
			    exit( 1 );
			}
			break;

		case 'i':	/* set pid log file or ldif2db LDIF file */
			if ( slapd_exemode == SLAPD_EXEMODE_LDIF2DB ) {
			        char *p;
					/* if LDIF comes through standard input, skip path checking */
					if ( optarg_ext[0] != '-' || strlen(optarg_ext) != 1) {
#if defined( XP_WIN32 )
						if ( optarg_ext[0] != '/' && optarg_ext[0] != '\\'
						&& (!isalpha( optarg_ext[0] ) || (optarg_ext[1] != ':')) ) {
							fprintf( stderr, "%s file could not be opened: absolute path "
							" required.\n", optarg_ext );
						break;
						}
#else
						if ( optarg_ext[ 0 ] != '/' ) {
							fprintf( stderr, "%s file could not be opened: absolute path "
							" required.\n", optarg_ext );
						break;
						}
#endif
					}
				p = (char *) slapi_ch_malloc(strlen(optarg_ext) + 1);

				strcpy(p, optarg_ext);
				charray_add(&ldif_file, p);
				ldif_files++;
			} else {
				pid_file = rel2abspath( optarg_ext );
			}
			break;
		case 'w':	/* set startup pid file */
			start_pid_file = rel2abspath( optarg_ext );
			break;
		case 'n':	/* which backend to do ldif2db/bak2db for */
			if (slapd_exemode == SLAPD_EXEMODE_LDIF2DB ||
				slapd_exemode == SLAPD_EXEMODE_DBTEST ||
				slapd_exemode == SLAPD_EXEMODE_DB2INDEX ||
				slapd_exemode == SLAPD_EXEMODE_ARCHIVE2DB) {
				/* The -n argument will give the name of a backend instance. */
				cmd_line_instance_name = optarg_ext;
			} else if (slapd_exemode == SLAPD_EXEMODE_DB2LDIF) {
			    char *s = slapi_ch_strdup(optarg_ext);
			    charray_add(&cmd_line_instance_names, s);
			} else { 
				ldif_backend = atoi( optarg_ext );
			}
			break;
		case 's':       /* which suffix to include in import/export */
			{
			    char *s=  slapi_dn_normalize ( slapi_ch_strdup(optarg_ext) );
			    charray_add(&db2ldif_include,s);
			}
			break;
		case 'x':       /* which suffix to exclude in import/export */
			{
			    char *s= slapi_dn_normalize ( slapi_ch_strdup(optarg_ext) );
			    charray_add(&db2ldif_exclude,s);
			}
			break;
		case 'r':       /* db2ldif for replication */
			if (slapd_exemode == SLAPD_EXEMODE_REFERRAL) {
			    if (config_set_referral_mode( "referral (-r)", optarg_ext,
						errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
				fprintf(stderr, "%s: aborting now\n",
					errorbuf);
				usage(myname, *extraname);
				exit(1);
			    }
			    break;
			}
		        if (slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
			        usage( myname, *extraname );
				exit( 1 );
			}
			db2ldif_dump_replica = 1;
			break;
		case 'N':	/* do not do ldif2db duplicate value check */
			if ( slapd_exemode != SLAPD_EXEMODE_LDIF2DB && 
				slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
				usage( myname, *extraname );
				exit( 1 );
			}
			/*
			 * -N flag is obsolete, but we silently accept it
			 * so we don't break customer's scripts.
			 */
			
			/* The -N flag now does what the -n flag used to do for db2ldif.  
			 * This is so -n cane be used for the instance name just like 
			 * with ldif2db. */
			if ( slapd_exemode == SLAPD_EXEMODE_DB2LDIF ) {
				ldif_printkey &= ~EXPORT_PRINTKEY;
			}

			break;

		case 'U':	/* db2ldif only */
			if ( slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			
			/*
			 * don't fold (wrap) long lines (default is to fold),
			 * as of ldapsearch -T
			 */
			ldif_printkey |= EXPORT_NOWRAP;

			break;

		case 'm':	/* db2ldif only */
			if ( slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			
			/* minimal base64 encoding */
			ldif_printkey |= EXPORT_MINIMAL_ENCODING;

			break;

		case 'M':	/* db2ldif only */
			if ( slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			
			/*
			 * output ldif is stored in several file called intance_filename.
			 * by default, all instances are stored in the single filename.
			 */
			ldif_printkey &= ~EXPORT_APPENDMODE;

			break;

		case 'o':	/* db2ldif only */
			if ( slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			
			/*
			 * output ldif is stored in one file.
			 * by default, each instance is stored in instance_filename.
			 */
			ldif_printkey |= EXPORT_APPENDMODE;

			break;

		case 'C':
			if (slapd_exemode == SLAPD_EXEMODE_LDIF2DB) {
			    /* used to mean "Cool new import" (which is now
			     * the default) -- ignore
			     */
			    break;
			}
			if (slapd_exemode == SLAPD_EXEMODE_DB2LDIF) {
			    /* possibly corrupted db -- don't look at any
			     * file except id2entry.  yet another overloaded
			     * flag.
			     */
			    ldif_printkey |= EXPORT_ID2ENTRY_ONLY;
			    break;
			}
			usage( myname, *extraname );
			exit( 1 );

		case 'c':	/* merge chunk size for Cool new import */
			if ( slapd_exemode != SLAPD_EXEMODE_LDIF2DB ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			ldif2db_removedupvals = atoi(optarg_ext); /* We overload this flag---ok since we always check for dupes in the new code */
			break;

		case 'O':	/* only create core db, no attr indexes */
			if ( slapd_exemode != SLAPD_EXEMODE_LDIF2DB ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			ldif2db_noattrindexes = 1;
			break;

		case 't':	/* attribute type to index - may be repeated */
		case 'T':	/* VLV Search to index - may be repeated */
			if ( slapd_exemode == SLAPD_EXEMODE_DB2INDEX ) {
                char *p= slapi_ch_malloc(strlen(optarg_ext) + 2);
                sprintf(p,"%c%s",i,optarg_ext);
    			charray_add( &db2index_attrs, p);
				break;
            }
			usage( myname, *extraname );
			exit(1);

		case 'v':	/* print version and exit */
		  	slapd_print_version(0);
			exit( 1 );
			break;

		case 'V':
		  	slapd_exemode = SLAPD_EXEMODE_PRINTVERSION;
			break;

		case 'a':	/* archive pathname for db */
			archive_name = optarg_ext;
			break;

		case 'Z':
			if (slapd_exemode == SLAPD_EXEMODE_LDIF2DB)
			{
				ldif2db_load_state= 0;
				break;
			}
			usage( myname, *extraname );
			exit(1);			
		case 'S':       /* skip the check for slad running in conflicting modes */
		        skip_db_protect_check = 1;
			break;
		case 'u': /* do not dump uniqueid for db2ldif */
			if ( slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			db2ldif_dump_uniqueid = 0;
			break;
		case 'g': /* generate uniqueid for ldif2db */
			if ( slapd_exemode != SLAPD_EXEMODE_LDIF2DB ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			if (optarg_ext == NULL){
				printf ("ldif2db: generation type is not specified for -g; "
						"random generation is used\n");
				ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_TIME_BASED;						
			}
			else if (strcasecmp (optarg_ext, "none") == 0)
				ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_NONE;
			else if (strcasecmp (optarg_ext, "deterministic") == 0) /* name based */
				ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_NAME_BASED;
			else /* default - time based */
				ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
			break;
		case 'G': /* namespace id for name based uniqueid generation for ldif2db */
		    if ( slapd_exemode != SLAPD_EXEMODE_LDIF2DB ) {
				usage( myname, *extraname );
				exit( 1 );
			}

			ldif2db_namespaceid = optarg_ext;
			break;			
		case 'E': /* encrypt data if importing, decrypt if exporting */
		    if ( (slapd_exemode != SLAPD_EXEMODE_LDIF2DB) && (slapd_exemode != SLAPD_EXEMODE_DB2LDIF)) {
				usage( myname, *extraname );
				exit( 1 );
			}
			importexport_encrypt = 1;
			break;			
#if defined(UPGRADEDB)
		case 'f':	/* upgradedb only */
		    if ( slapd_exemode != SLAPD_EXEMODE_UPGRADEDB ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			upgradedb_force = SLAPI_UPGRADEDB_FORCE;
			break;			
#endif
		case '1':	/* db2ldif only */
			if ( slapd_exemode != SLAPD_EXEMODE_DB2LDIF ) {
				usage( myname, *extraname );
				exit( 1 );
			}
			
			/*
			 * do not output "version: 1" to the ldif file
			 */
			ldif_printkey |= EXPORT_NOVERSION;

			break;
		default:
			usage( myname, *extraname );
			exit( 1 );
		}
	}

	if ((NULL != cmd_line_instance_names)
		 && (NULL != cmd_line_instance_names[1])
		 && (ldif_printkey & EXPORT_APPENDMODE))
	{
		fprintf(stderr, "WARNING: several backends are being"
						" exported to a single ldif file\n");
		fprintf(stderr, "         use option -M to export to"
						" multiple ldif files\n");
	}
	/* Any leftover arguments? */
	if ( optind_last > optind ) {
		usage( myname, *extraname );
		exit( 1 );
	}

	return;
}

static int
lookup_instance_name_by_suffix(char *suffix,
							   char ***suffixes, char ***instances, int isexact)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Entry **entries = NULL, **ep;
    char *query;
	char *backend;
	char *fullsuffix;
	int rval = -1;

    if (pb == NULL)
        goto done;

    query = slapi_ch_malloc(strlen((const char *)suffix) + 80);         /* round up */

    if (query == NULL)
		goto done;

	if (isexact)
    	sprintf(query, "(&(objectclass=nsmappingtree)(|(cn=\"%s\")(cn=%s)))", suffix, suffix);
	else
    	sprintf(query, "(&(objectclass=nsmappingtree)(|(cn=*%s\")(cn=*%s)))", suffix, suffix);

    slapi_search_internal_set_pb(pb, "cn=mapping tree,cn=config",
        LDAP_SCOPE_SUBTREE, query, NULL, 0, NULL, NULL,
        (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free((void **)&query);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
    if (rval != LDAP_SUCCESS)
		goto done;

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL))
		goto done;

	rval = 0;
	for (ep = entries; *ep; ep++) {
		backend = slapi_entry_attr_get_charptr(*ep, "nsslapd-backend");
		if (backend) {
			charray_add(instances, backend);
			if (suffixes) {
				fullsuffix = slapi_entry_attr_get_charptr(*ep, "cn");
				charray_add(suffixes, fullsuffix);	/* NULL is ok */
			}
		}
	}

done:
	slapi_free_search_results_internal(pb);
	slapi_pblock_destroy(pb);
	return rval;
}

int
lookup_instance_name_by_suffixes(char **included, char **excluded,
								 char ***instances)
{
	char **incl_instances, **excl_instances;
	char **p;
	int rval = -1;

	incl_instances = NULL;
	for (p = included; p && *p; p++) {
		if (lookup_instance_name_by_suffix(*p, NULL, &incl_instances, 0) < 0)
			return rval;
	}

	excl_instances = NULL;
	for (p = excluded; p && *p; p++) {
		if (lookup_instance_name_by_suffix(*p, NULL, &excl_instances, 0) < 0)
			return rval;
	}

	rval = 0;
	charray_subtract(incl_instances, excl_instances, NULL);
	charray_free(excl_instances);
	*instances = incl_instances;
	return rval;
}

/* helper function for ldif2db & friends -- given an instance name, lookup
 * the plugin name in the DSE.  this assumes the DSE has already been loaded.
 */
static struct slapdplugin *lookup_plugin_by_instance_name(const char *name)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *pb = slapi_pblock_new();
    struct slapdplugin *plugin;
    char *query, *dn, *cn;
    int ret = 0;

    if (pb == NULL)
        return NULL;

    query = slapi_ch_malloc(strlen(name) + 80);         /* round up */
    if (query == NULL) {
        slapi_pblock_destroy(pb);
        return NULL;
    }
    sprintf(query, "(&(cn=%s)(objectclass=nsBackendInstance))", name);

    slapi_search_internal_set_pb(pb, "cn=plugins,cn=config",
        LDAP_SCOPE_SUBTREE, query, NULL, 0, NULL, NULL,
        (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free((void **)&query);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL)) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }

    /* okay -- have the entry for this instance, now let's chop up the dn */
    /* parent dn is the plugin */
    dn = slapi_dn_parent(slapi_entry_get_dn(entries[0]));

	/* clean up */
    slapi_free_search_results_internal(pb);
	entries = NULL;
	slapi_pblock_destroy(pb);
	pb = NULL; /* this seems redundant . . . until we add code after this line */

    /* now... look up the parent */
	pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_BASE,
        "(objectclass=nsSlapdPlugin)", NULL, 0, NULL, NULL,
        (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free((void **)&dn);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL)) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }

    cn = slapi_entry_attr_get_charptr(entries[0], "cn");
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    plugin = plugin_get_by_name(cn);
    slapi_ch_free((void **)&cn);

    return plugin;
}

static int
slapd_exemode_ldif2db()
{
    int return_value= 0;
    Slapi_PBlock pb;
    struct slapdplugin *plugin;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if ( ldif_file == NULL ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "ERROR: Required argument -i <ldiffile> missing\n",
                   0, 0, 0 );
        usage( myname, extraname );
        exit( 1 );
    }

    /* this should be the first time this are called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

	/*
	 * if instance is given, just use it to get the backend.
	 * otherwise, we use included/excluded suffix list to specify a backend.
	 */
    if (NULL == cmd_line_instance_name) {
		char **instances, **ip;
		int counter;

		if (lookup_instance_name_by_suffixes(db2ldif_include, db2ldif_exclude,
													&instances) < 0) {
			LDAPDebug(LDAP_DEBUG_ANY, 
				"ERROR: backend instances name [-n <name>] or "
				"included suffix [-s <suffix>] need to be specified.\n",
				0, 0, 0);
			exit(1);
		}

		if (instances) {
			for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
				;

			if (counter == 0) {
				LDAPDebug(LDAP_DEBUG_ANY, 
					"ERROR 1: There is no backend instance to import to.\n",
					0, 0, 0);
				exit(1);
			} else if (counter > 1) {
				int i;
				LDAPDebug(LDAP_DEBUG_ANY, 
					"ERROR: There are multiple backend instances specified:\n",
					0, 0, 0);
				for (i = 0; i < counter; i++)
					LDAPDebug(LDAP_DEBUG_ANY, "     : %s\n",
											  instances[i], 0, 0);
        		exit(1);
			} else {
				LDAPDebug(LDAP_DEBUG_ANY, "Backend Instance: %s\n",
					*instances, 0, 0);
				cmd_line_instance_name = *instances;
			}
		} else {
			LDAPDebug(LDAP_DEBUG_ANY, 
				"ERROR 2: There is no backend instance to import to.\n",
				0, 0, 0);
			exit(1);
		}
    }

    plugin = lookup_plugin_by_instance_name(cmd_line_instance_name);
    if (plugin == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, 
                  "ERROR: Could not find backend '%s'.\n",
                  cmd_line_instance_name, 0, 0);
        exit(1);
    }

    /* Make sure we aren't going to run slapd in 
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if ( add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
                               skip_db_protect_check) == -1 )  {

        LDAPDebug( LDAP_DEBUG_ANY, 
                   "Shutting down due to possible conflicts with other slapd processes\n",
                   0, 0, 0 );
        exit(1);
    }
    /* check for slapi v2 support */
    if (! SLAPI_PLUGIN_IS_V2(plugin)) {
        LDAPDebug(LDAP_DEBUG_ANY, "ERROR: %s is too old to do imports.\n",
                  plugin->plg_name, 0, 0);
        exit(1);
    }

    memset( &pb, '\0', sizeof(pb) );
    pb.pb_backend = NULL;
    pb.pb_plugin = plugin;
    pb.pb_removedupvals = ldif2db_removedupvals;
    pb.pb_ldif2db_noattrindexes = ldif2db_noattrindexes;
    pb.pb_ldif_generate_uniqueid = ldif2db_generate_uniqueid;
    pb.pb_ldif_namespaceid = ldif2db_namespaceid;
    pb.pb_ldif_encrypt = importexport_encrypt;
/*    pb.pb_ldif_load_state = ldif2db_load_state; */
    pb.pb_instance_name = cmd_line_instance_name;
    pb.pb_ldif_files = ldif_file;
    pb.pb_ldif_include = db2ldif_include;
    pb.pb_ldif_exclude = db2ldif_exclude;
    pb.pb_task_flags = TASK_RUNNING_FROM_COMMANDLINE;
#ifndef _WIN32
    main_setuid(slapdFrontendConfig->localuser);
#endif
    if ( plugin->plg_ldif2db != NULL ) {
        return_value = (*plugin->plg_ldif2db)( &pb );
    } else {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "ERROR: no ldif2db function defined for "
                   "%s\n", plugin->plg_name, 0, 0 );
        return_value = -1;
    }
    slapi_ch_free((void**)&myname );
    charray_free( db2index_attrs );
    charray_free(ldif_file);
    return( return_value );
}

static int
slapd_exemode_db2ldif(int argc, char** argv)
{
    int return_value= 0;
    Slapi_PBlock pb;
    struct slapdplugin *plugin;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *my_ldiffile;
	char **instp;

    /* this should be the first time this are called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

	/*
	 * if instance is given, just pass it to the backend.
	 * otherwise, we use included/excluded suffix list to specify a backend.
	 */
    if (NULL == cmd_line_instance_names) {
		char **instances, **ip;
		int counter;

		if (lookup_instance_name_by_suffixes(db2ldif_include, db2ldif_exclude,
													&instances) < 0) {
			LDAPDebug(LDAP_DEBUG_ANY, 
				"ERROR: backend instances name [-n <name>] or "
				"included suffix [-s <suffix>] need to be specified.\n",
				0, 0, 0);
			exit(1);
		}

		if (instances) {
			for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
				;

			if (counter == 0) {
				LDAPDebug(LDAP_DEBUG_ANY, 
					"ERROR 1: There is no backend instance to export from.\n",
					0, 0, 0);
				exit(1);
			} else {
				LDAPDebug(LDAP_DEBUG_ANY, "Backend Instance: %s\n",
					*instances, 0, 0);
				cmd_line_instance_names = instances;
			}
		} else {
			LDAPDebug(LDAP_DEBUG_ANY, 
				"ERROR 2: There is no backend instance to export from.\n",
				0, 0, 0);
			exit(1);
		}
    }

#ifndef _WIN32
	/* [622984] db2lidf -r changes database file ownership
	 * should call setuid before "db2ldif_dump_replica" */
	main_setuid(slapdFrontendConfig->localuser);
#endif
	for (instp = cmd_line_instance_names; instp && *instp; instp++) {
		int release_me = 0;

	    plugin = lookup_plugin_by_instance_name(*instp);
	    if (plugin == NULL) {
	        LDAPDebug(LDAP_DEBUG_ANY, 
	                  "ERROR: Could not find backend '%s'.\n", 
	                  *instp, 0, 0);
	        exit(1);
	    }
	
		if (plugin->plg_db2ldif == NULL) {
			LDAPDebug(LDAP_DEBUG_ANY, "ERROR: no db2ldif function defined for "
					  "backend %s - cannot export\n", *instp, 0, 0);
			exit(1);
		}

	    /* Make sure we aren't going to run slapd in 
	     * a mode that is going to conflict with other
	     * slapd processes that are currently running
	     */
	    if ( add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
								   skip_db_protect_check) == -1 )  {
	        LDAPDebug( LDAP_DEBUG_ANY, 
	                   "Shutting down due to possible conflicts "
					   "with other slapd processes\n",
	                   0, 0, 0 );
	        exit(1);
	    }
	    if ( config_is_slapd_lite () &&
			 !slapi_config_get_readonly () && is_slapd_running() ) {
	        LDAPDebug( LDAP_DEBUG_ANY, "%s\n", LITE_BACKUP_ERR, 0, 0);
	        exit ( 1 );
	    }
	
	    if (! (SLAPI_PLUGIN_IS_V2(plugin))) {
	        LDAPDebug(LDAP_DEBUG_ANY, "ERROR: %s is too old to do exports.\n",
	                  plugin->plg_name, 0, 0);
	        exit(1);
	    }
	
	    memset( &pb, '\0', sizeof(pb) );
	    pb.pb_backend = NULL;
	    pb.pb_plugin = plugin;
	    pb.pb_ldif_include = db2ldif_include;
	    pb.pb_ldif_exclude = db2ldif_exclude;
	    pb.pb_ldif_dump_replica = db2ldif_dump_replica;
	    pb.pb_ldif_dump_uniqueid = db2ldif_dump_uniqueid;
		pb.pb_ldif_encrypt = importexport_encrypt;
	    pb.pb_instance_name = *instp;
	    pb.pb_task_flags = TASK_RUNNING_FROM_COMMANDLINE;
		if (is_slapd_running())
			pb.pb_server_running = 1;
		else
			pb.pb_server_running = 0;
	
	    if (db2ldif_dump_replica) {
			eq_init();					/* must be done before plugins started */
	        ps_init_psearch_system();   /* must come before plugin_startall() */
	        plugin_startall(argc, argv, 1 /* Start Backends */,
								  1 /* Start Globals */); 
			eq_start();					/* must be done after plugins started */
	    }
	  
	    pb.pb_ldif_file = NULL;
	    if ( archive_name ) { /* redirect stdout to this file: */
            char *p, *q;
#if defined( XP_WIN32 )
			char sep = '\\';
			if (NULL != strchr(archive_name, '/'))
				sep = '/';
#else
			char sep = '/';
#endif
			my_ldiffile = archive_name;
			if (ldif_printkey & EXPORT_APPENDMODE) {
				if (instp == cmd_line_instance_names) {	/* first export */
					ldif_printkey |= EXPORT_APPENDMODE_1;
				} else {
					ldif_printkey &= ~EXPORT_APPENDMODE_1;
				}
			} else {	/* not APPENDMODE */
				if (strcmp(archive_name, "-")) {	/* not '-' */
            		my_ldiffile =
                	(char *)slapi_ch_malloc((unsigned long)(strlen(archive_name)
													+ strlen(*instp) + 2));
            		p = strrchr(archive_name, sep);
            		if (NULL == p) {
                		sprintf(my_ldiffile, "%s_%s", *instp, archive_name);
            		} else {
                		q = p + 1;
                		*p = '\0';
                		sprintf(my_ldiffile, "%s%c%s_%s",
										 archive_name, sep, *instp, q);
                		*p = sep;
            		}
					release_me = 1;
				}
			}

			fprintf(stderr, "ldiffile: %s\n", my_ldiffile);
	        /* just send the filename to the backend and let
	         * the backend open it (so they can do special
	         * stuff for 64-bit fs)
	         */
	        pb.pb_ldif_file = my_ldiffile;
	    	pb.pb_ldif_printkey = ldif_printkey;
	    }
	
	    return_value = (plugin->plg_db2ldif)( &pb );

		if (release_me) {
			slapi_ch_free((void **)&my_ldiffile);
		}
	}
	slapi_ch_free( (void**)&myname );
    if (db2ldif_dump_replica) {
        plugin_closeall( 1 /* Close Backends */, 1 /* Close Globals */);
    }
    return( return_value );
}

static int
slapd_exemode_suffix2instance()
{
    int return_value = 0;
	char **instances = NULL;
	char **suffixes = NULL;
	char **p, **q, **r;

    /* this should be the first time this are called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

	for (p = db2ldif_include; p && *p; p++) {
		if (lookup_instance_name_by_suffix(*p, &suffixes, &instances, 0) < 0)
			continue;
		fprintf(stderr, "Suffix, Instance name pair(s) under \"%s\":\n", *p);
		if (instances)
			for (q = suffixes, r = instances; *r; q++, r++)
				fprintf(stderr, "\tsuffix %s; instance name \"%s\"\n",
								*q?*q:"-", *r);
		else
			fprintf(stderr, "\tNo instance\n");
		charray_free(suffixes);
		suffixes = NULL;
		charray_free(instances);
		instances = NULL;
	}
	return (return_value);
}

static int slapd_exemode_db2index()
{
    int return_value= 0;
    struct slapdplugin *plugin;
    Slapi_PBlock pb;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    mapping_tree_init();

    /*
     * if instance is given, just use it to get the backend.
     * otherwise, we use included/excluded suffix list to specify a backend.
     */
    if (NULL == cmd_line_instance_name) {
        char **instances, **ip;
        int counter;

        if (lookup_instance_name_by_suffixes(db2ldif_include, db2ldif_exclude,
                                             &instances) < 0) {
            LDAPDebug(LDAP_DEBUG_ANY, 
                      "ERROR: backend instances name [-n <name>] or "
                      "included suffix [-s <suffix>] need to be specified.\n",
                      0, 0, 0);
            exit(1);
        }

        if (instances) {
            for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
                ;

            if (counter == 0) {
                LDAPDebug(LDAP_DEBUG_ANY, 
                          "ERROR 1: There is no backend instance to import to.\n",
                          0, 0, 0);
                exit(1);
            } else if (counter > 1) {
                int i;
                LDAPDebug(LDAP_DEBUG_ANY, 
                          "ERROR: There are multiple backend instances specified:\n",
                          0, 0, 0);
                for (i = 0; i < counter; i++)
                    LDAPDebug(LDAP_DEBUG_ANY, "     : %s\n",
                              instances[i], 0, 0);
                exit(1);
            } else {
                LDAPDebug(LDAP_DEBUG_ANY, "Backend Instance: %s\n",
                          *instances, 0, 0);
                cmd_line_instance_name = *instances;
            }
        } else {
            LDAPDebug(LDAP_DEBUG_ANY, 
                      "ERROR 2: There is no backend instance to import to.\n",
                      0, 0, 0);
            exit(1);
        }
    }

    plugin = lookup_plugin_by_instance_name(cmd_line_instance_name);
    if (plugin == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, 
                  "ERROR: Could not find backend '%s'.\n",
                  cmd_line_instance_name, 0, 0);
        exit(1);
    }

    /* make sure nothing else is running */
    if (add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
                              skip_db_protect_check) == -1) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Shutting down due to possible conflicts with other "
                  "slapd processes.\n", 0, 0, 0);
        exit(1);
    }

    if ( db2index_attrs == NULL ) {
	usage( myname, extraname );
	exit( 1 );
    }
    memset( &pb, '\0', sizeof(pb) );
    pb.pb_backend = NULL;
    pb.pb_plugin = plugin;
    pb.pb_db2index_attrs = db2index_attrs;
    pb.pb_instance_name = cmd_line_instance_name;
    pb.pb_task_flags = TASK_RUNNING_FROM_COMMANDLINE;
#ifndef _WIN32
    main_setuid(slapdFrontendConfig->localuser);
#endif
    return_value = (*plugin->plg_db2index)( &pb );

    slapi_ch_free( (void**)&myname );
    return( return_value );
}


static int 
slapd_exemode_db2archive()
{
    int return_value= 0;
	Slapi_Backend *be = NULL;
	Slapi_PBlock pb;
	struct slapdplugin *backend_plugin;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
	if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
		LDAPDebug(LDAP_DEBUG_ANY, 
			"ERROR: Could not find the ldbm backend plugin.\n",
			0, 0, 0);
		exit(1);
	}
	if (NULL == archive_name) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ERROR: no archive directory supplied\n",
		    0, 0, 0 );
		exit( 1 );
	}

	if ( config_is_slapd_lite ()  && !slapi_config_get_readonly ()  && is_slapd_running ()) {
		LDAPDebug( LDAP_DEBUG_ANY, "%s\n", LITE_BACKUP_ERR, 0, 0);
		exit ( 1 );
	}

	/* Make sure we aren't going to run slapd in 
	 * a mode that is going to conflict with other
 	 * slapd processes that are currently running
 	 */
 	if ( add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
				   skip_db_protect_check) == -1 )  {
	    LDAPDebug( LDAP_DEBUG_ANY, 
		       "Shutting down due to possible conflicts with other slapd processes\n",
		       0, 0, 0 );
	    exit(1);
	}
	if (compute_init()) {
		LDAPDebug(LDAP_DEBUG_ANY, "Initialization Failed 0 %d\n",return_value,0,0);
		exit (1);
	}

	memset( &pb, '\0', sizeof(pb) );
	pb.pb_backend = NULL;
	pb.pb_plugin = backend_plugin;
	pb.pb_instance_name = cmd_line_instance_name;
	pb.pb_seq_val = archive_name;
	pb.pb_task_flags = TASK_RUNNING_FROM_COMMANDLINE;
#ifndef _WIN32
	main_setuid(slapdFrontendConfig->localuser);
#endif
	return_value = (backend_plugin->plg_db2archive)( &pb );
	return return_value;
}

static int
slapd_exemode_archive2db()
{
    int return_value= 0;
	Slapi_Backend *be = NULL;
	Slapi_PBlock pb;
	struct slapdplugin *backend_plugin;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
	if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
		LDAPDebug(LDAP_DEBUG_ANY, 
			"ERROR: Could not find the ldbm backend plugin.\n",
			0, 0, 0);
		exit(1);
	}
	if (NULL == archive_name) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ERROR: no archive directory supplied\n",
		    0, 0, 0 );
		exit( 1 );
	}
	
	/* Make sure we aren't going to run slapd in 
	 * a mode that is going to conflict with other
 	 * slapd processes that are currently running
 	 */
 	if ( add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
				   skip_db_protect_check) == -1 )  {
	    LDAPDebug( LDAP_DEBUG_ANY, 
		       "Shutting down due to possible conflicts with other slapd processes\n",
		       0, 0, 0 );
	    exit(1);
	}
	if (compute_init()) {
		LDAPDebug(LDAP_DEBUG_ANY, "Initialization Failed 0 %d\n",return_value,0,0);
		exit (1);
	}

	memset( &pb, '\0', sizeof(pb) );
	pb.pb_backend = NULL;
	pb.pb_plugin = backend_plugin;
	pb.pb_instance_name = cmd_line_instance_name;
	pb.pb_seq_val = archive_name;
	pb.pb_task_flags = TASK_RUNNING_FROM_COMMANDLINE;
#ifndef _WIN32
	main_setuid(slapdFrontendConfig->localuser);
#endif
	return_value = (backend_plugin->plg_archive2db)( &pb );
	return return_value;
}	

#if defined(UPGRADEDB)
/*
 * functions to convert idl from the old format to the new one
 * (604921) Support a database uprev process any time post-install
 */
static int
slapd_exemode_upgradedb()
{
    int return_value= 0;
    Slapi_PBlock pb;
    struct slapdplugin *backend_plugin;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if ( archive_name == NULL ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "ERROR: Required argument -a <backup_dir> missing\n",
                   0, 0, 0 );
        usage( myname, extraname );
        exit( 1 );
    }

    /* this should be the first time this are called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

    if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, 
            "ERROR: Could not find the ldbm backend plugin.\n",
            0, 0, 0);
        exit(1);
    }

    /* Make sure we aren't going to run slapd in 
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if (add_new_slapd_process(slapd_exemode, 0, skip_db_protect_check) == -1) {
        LDAPDebug( LDAP_DEBUG_ANY, 
                   "Shutting down due to possible conflicts with other slapd processes\n",
                   0, 0, 0 );
        exit(1);
    }
    /* check for slapi v2 support */
    if (! SLAPI_PLUGIN_IS_V2(backend_plugin)) {
        LDAPDebug(LDAP_DEBUG_ANY, "ERROR: %s is too old to do convert idl.\n",
                  backend_plugin->plg_name, 0, 0);
        exit(1);
    }

    memset( &pb, '\0', sizeof(pb) );
    pb.pb_backend = NULL;
    pb.pb_plugin = backend_plugin;
    pb.pb_seq_val = archive_name;
    pb.pb_seq_type = upgradedb_force;
    pb.pb_task_flags = TASK_RUNNING_FROM_COMMANDLINE;
    /* borrowing import code, so need to set up the import variables */
    pb.pb_ldif_generate_uniqueid = ldif2db_generate_uniqueid;
    pb.pb_ldif_namespaceid = ldif2db_namespaceid;
    pb.pb_ldif2db_noattrindexes = 0;
    pb.pb_removedupvals = 0;
#ifndef _WIN32
    main_setuid(slapdFrontendConfig->localuser);
#endif
    if ( backend_plugin->plg_upgradedb != NULL ) {
        return_value = (*backend_plugin->plg_upgradedb)( &pb );
    } else {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "ERROR: no upgradedb function defined for "
                   "%s\n", backend_plugin->plg_name, 0, 0 );
        return_value = -1;
    }
    slapi_ch_free((void**)&myname );
    return( return_value );
}
#endif


static int
slapd_exemode_dbtest()
{	
    int return_value= 0;
    Slapi_PBlock pb;
    struct slapdplugin *plugin;

    if (NULL == cmd_line_instance_name) {
        LDAPDebug(LDAP_DEBUG_ANY, 
                  "dbtest: Required argument -n <instance name> missing\n", 0, 0, 0);
        usage( myname, extraname );
        exit(1);
    }

    mapping_tree_init();

    plugin = lookup_plugin_by_instance_name(cmd_line_instance_name);
    if (plugin == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, 
                  "ERROR: Could not find backend '%s'.\n",
                  cmd_line_instance_name, 0, 0);
        exit(1);
    }

    /* Make sure we aren't going to run slapd in 
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if ( add_new_slapd_process(slapd_exemode, db2ldif_dump_replica,
                               skip_db_protect_check) == -1 )  {

        LDAPDebug( LDAP_DEBUG_ANY, 
                   "Shutting down due to possible conflicts with other slapd processes\n",
                   0, 0, 0 );
        exit(1);
    }

    pb.pb_backend = NULL;
    pb.pb_plugin = plugin;
    pb.pb_instance_name = cmd_line_instance_name;
    /* For dbtest, we do _not_ change identity (no setuid()) */
    return_value= (*plugin->plg_dbtest)( &pb );
    return return_value;
}



#ifdef LDAP_DEBUG
/*
 * Table to associate a string with a debug level.
 */
static struct slapd_debug_level_entry {
	int         dle_level;      /* LDAP_DEBUG_XXX value */
	const char  *dle_string;    /* string equivalent; NULL marks end of list */
	char		dle_hide;
} slapd_debug_level_map[] = {
        { LDAP_DEBUG_TRACE,		"trace",			0	},
        { LDAP_DEBUG_PACKETS,	"packets",			0	},
        { LDAP_DEBUG_ARGS,		"arguments",		0	},
        { LDAP_DEBUG_ARGS,		"args",				1	},
        { LDAP_DEBUG_CONNS,		"connections",		0	},
        { LDAP_DEBUG_CONNS,		"conn",				1	},
        { LDAP_DEBUG_CONNS,		"conns",			1	},
        { LDAP_DEBUG_BER,		"ber",				0	},
        { LDAP_DEBUG_FILTER,    "filters",			0	},
        { LDAP_DEBUG_CONFIG,	"config",			0	},
        { LDAP_DEBUG_ACL,		"accesscontrol",	0	},
        { LDAP_DEBUG_ACL,		"acl",				1	},
        { LDAP_DEBUG_ACL,		"acls",				1	},
        { LDAP_DEBUG_STATS,		"stats",			0	},
        { LDAP_DEBUG_STATS2,    "stats2",			0	},
        { LDAP_DEBUG_SHELL,		"shell",			1	},
        { LDAP_DEBUG_PARSE,		"parsing",			0	},
        { LDAP_DEBUG_HOUSE,		"housekeeping",		0	},
        { LDAP_DEBUG_REPL,		"replication",		0	},
        { LDAP_DEBUG_REPL,		"repl",				1	},
        { LDAP_DEBUG_ANY,       "errors",			0	},
        { LDAP_DEBUG_ANY,       "ANY",				1	},
        { LDAP_DEBUG_ANY,       "error",			1	},
        { LDAP_DEBUG_CACHE,		"caches",			0	},
        { LDAP_DEBUG_CACHE,		"cache",			1	},
        { LDAP_DEBUG_PLUGIN,	"plugins",			0	},
        { LDAP_DEBUG_PLUGIN,	"plugin",			1	},
        { LDAP_DEBUG_TIMING,	"timing",			0	},
        { LDAP_DEBUG_ACLSUMMARY,"accesscontrolsummary", 0  },
        { LDAP_DEBUG_ALL_LEVELS,"ALL",				0	},
        { 0,                    NULL,               0     }
};



/*
 * Given a string represention of a debug level, map it to a integer value
 * and return that value.  -1 is returned upon error, with a message
 * printed to stderr.
 */
static int
slapd_debug_level_string2level( const char *s )
{
        int             level, i;
        char    *cur, *next, *scopy;

        level = 0;
        cur = scopy = slapi_ch_strdup( s );     

        for ( cur = scopy; cur != NULL; cur = next ) {
                if (( next = strchr( cur, '+' )) != NULL ) {
                        *next++ = '\0';
                }

                if ( isdigit( *cur )) {
                        level |= atoi( cur );
                } else {
                        for ( i = 0;  NULL != slapd_debug_level_map[i].dle_string; ++i ) {
                                if ( strcasecmp( cur, slapd_debug_level_map[i].dle_string )
                                                        == 0 ) {
                                        level |= slapd_debug_level_map[i].dle_level;
                                        break;
                                }
                        }

                        if ( NULL == slapd_debug_level_map[i].dle_string ) {
                                fprintf( stderr, "Unrecognized debug level \"%s\"\n", cur );
                                return -1;
                        }
                }
        }

        slapi_ch_free( (void **)&scopy );

        return level;
}


/*
 * Print to stderr the string equivalent of level.
 * The ANY level is omitted because it is always present.
 */
static void
slapd_debug_level_log( int level )
{
        int             i, count, len;
		char			*msg, *p;

        level &= ~LDAP_DEBUG_ANY;

		/* first pass: determine space needed for the debug level string */
		len = 1;	/* room for '\0' terminator */
        count = 0;
        for ( i = 0;  NULL != slapd_debug_level_map[i].dle_string; ++i ) {
                if ( !slapd_debug_level_map[i].dle_hide &&
					slapd_debug_level_map[i].dle_level != LDAP_DEBUG_ALL_LEVELS
					&& 0 != ( level & slapd_debug_level_map[i].dle_level )) {
                        if ( count > 0 ) {
								++len;		/* room for '+' character */
                        }
						len += strlen( slapd_debug_level_map[i].dle_string );
                        ++count;
                }
        }

		/* second pass: construct the debug level string */
		p = msg = slapi_ch_malloc( len );
		count = 0;
        for ( i = 0;  NULL != slapd_debug_level_map[i].dle_string; ++i ) {
                if ( !slapd_debug_level_map[i].dle_hide &&
					slapd_debug_level_map[i].dle_level != LDAP_DEBUG_ALL_LEVELS
					&& 0 != ( level & slapd_debug_level_map[i].dle_level )) {
                        if ( count > 0 ) {
								*p++ = '+';
                        }
						strcpy( p, slapd_debug_level_map[i].dle_string );
						p += strlen( p );
                        ++count;
                }
        }

		slapi_log_error( SLAPI_LOG_FATAL, SLAPD_VERSION_STR,
				"%s: %s (%d)\n", "debug level", msg, level );
		slapi_ch_free( (void **)&msg );
}


/*
 * Display usage/help for the debug level flag (-d)
 */
static void
slapd_debug_level_usage( void )
{
        int             i;

        fprintf( stderr, "Debug levels:\n" );
        for ( i = 0; NULL != slapd_debug_level_map[i].dle_string; ++i ) {
				if ( !slapd_debug_level_map[i].dle_hide
					&& slapd_debug_level_map[i].dle_level
					!= LDAP_DEBUG_ALL_LEVELS) {
						fprintf( stderr, "    %6d - %s%s\n",
								slapd_debug_level_map[i].dle_level,
								slapd_debug_level_map[i].dle_string,
								( 0 == ( slapd_debug_level_map[i].dle_level &
								LDAP_DEBUG_ANY )) ? "" :
								" (always logged)" );
				}
        }
        fprintf( stderr, "To activate multiple levels, add the numeric"
				" values together or separate the\n"
				"values with a + character, e.g., all of the following"
				" have the same effect:\n"
				"    -d connections+filters\n"
				"    -d 8+32\n"
				"    -d 40\n" );
}
#endif /* LDAP_DEBUG */

