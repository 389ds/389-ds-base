/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Copyright (c) 1990, 1994 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <sys/types.h>
#ifdef SVR4
#include <sys/stat.h>
#endif /* svr4 */
#include <fcntl.h>
#ifndef _WIN32
#include <errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#endif
#include <signal.h>
#ifdef LINUX
#undef CTIME
#endif
#include "slap.h"
#include "fe.h"

#if defined(USE_SYSCONF) || defined(LINUX)
#include <unistd.h>
#endif /* USE_SYSCONF */

void
detach()
{
#ifndef _WIN32
	int		i, sd;
	char *workingdir = 0;
	char *errorlog = 0;
	char *ptr = 0;
	char errorbuf[BUFSIZ];
	extern char *config_get_errorlog(void);
#endif

#ifndef _WIN32
	if ( should_detach ) {
		for ( i = 0; i < 5; i++ ) {
#if defined( sunos5 ) && ( defined( THREAD_SUNOS5_LWP ) || defined( NSPR20 ))
			switch ( fork1() ) {
#else
			switch ( fork() ) {
#endif
			case -1:
				sleep( 5 );
				continue;

			case 0:
				break;

			default:
				_exit( 0 );
			}
			break;
		}

		workingdir = config_get_workingdir();
		if ( NULL == workingdir ) {
			errorlog = config_get_errorlog();
			if ( NULL == errorlog ) {
				(void) chdir( "/" );
			} else {
				if ((ptr = strrchr(errorlog, '/')) ||
					(ptr = strrchr(errorlog, '\\'))) {
					*ptr = 0;
				}
				(void) chdir( errorlog );
				config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, errorlog, errorbuf, 1);
				slapi_ch_free((void**)&errorlog);
			}
		} else {
			/* calling config_set_workingdir to check for validity of directory, don't apply */
			if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, workingdir, errorbuf, 0) == LDAP_OPERATIONS_ERROR) {
				exit(1);
			}
			(void) chdir( workingdir );
			slapi_ch_free((void**)&workingdir);
		}

		if ( (sd = open( "/dev/null", O_RDWR )) == -1 ) {
			perror( "/dev/null" );
			exit( 1 );
		}
		(void) dup2( sd, 0 );
		(void) dup2( sd, 1 );
		(void) dup2( sd, 2 );
		close( sd );

#ifdef USE_SETSID
		setsid();
#else /* USE_SETSID */
		if ( (sd = open( "/dev/tty", O_RDWR )) != -1 ) {
			(void) ioctl( sd, TIOCNOTTY, NULL );
			(void) close( sd );
		}
#endif /* USE_SETSID */

		g_set_detached(1);
	} 

	(void) SIGNAL( SIGPIPE, SIG_IGN );
#endif /* _WIN32 */
}


#ifndef _WIN32
/*
 * close all open files except stdin/out/err
 */
void
close_all_files()
{
	int		i, nbits;

#ifdef USE_SYSCONF
	nbits = sysconf( _SC_OPEN_MAX );
#else /* USE_SYSCONF */
	nbits = getdtablesize();
#endif /* USE_SYSCONF */

	for ( i = 3; i < nbits; i++ ) {
		close( i );
	}
}
#endif	/* !_WIN32 */

/*
 * There is no need to do anything on some platforms (NT) and not try to
 * raise fds on AIX.
 */

static void raise_process_fd_limits(void)
{
#if !defined(_WIN32) && !defined(AIX)
	struct rlimit	rl, setrl;
	RLIM_TYPE	curlim;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	if ( slapdFrontendConfig->maxdescriptors < 0 ) {
		return;
	}

	/*
	 * Try to set our file descriptor limit.  Our basic strategy is:
	 *	1) Try to set the soft limit and the hard limit if
         *         necessary to match our maxdescriptors value.
	 *	2) If that fails and our soft limit is less than our hard
	 *	   limit, we try to raise it to match the hard.
	 */
	if ( getrlimit( RLIMIT_NOFILE, &rl ) != 0 ) {
		int oserr = errno;

		LDAPDebug( LDAP_DEBUG_ANY,
		    "getrlimit of descriptor limit failed - error %d (%s)\n",
		    oserr, slapd_system_strerror( oserr ), 0 );
		return;
	}

	if ( rl.rlim_cur == slapdFrontendConfig->maxdescriptors ) {	/* already correct */
		return;
	}
	curlim = rl.rlim_cur;
	setrl = rl;	/* struct copy */
	setrl.rlim_cur = slapdFrontendConfig->maxdescriptors;
        /* don't lower the hard limit as it's irreversible */
        if (setrl.rlim_cur > setrl.rlim_max) {
            setrl.rlim_max = setrl.rlim_cur;
        }
	if ( setrlimit( RLIMIT_NOFILE, &setrl ) != 0 && curlim < rl.rlim_max ) {
		setrl = rl;	/* struct copy */
		setrl.rlim_cur = setrl.rlim_max;
		if ( setrlimit( RLIMIT_NOFILE, &setrl ) != 0 ) {
			int oserr = errno;

			LDAPDebug( LDAP_DEBUG_ANY, "setrlimit of descriptor "
			    "limit to %d failed - error %d (%s)\n",
			    setrl.rlim_cur, oserr,
			    slapd_system_strerror(oserr));
			return;
		}
	}
		    
	(void)getrlimit( RLIMIT_NOFILE, &rl );
	LDAPDebug( LDAP_DEBUG_TRACE, "descriptor limit changed from %d to %d\n",
	    curlim, rl.rlim_cur, 0 );
#endif /* !_WIN32 && !AIX */
}

/*
 * Try to raise relevant per-process limits 
 */
void
raise_process_limits()
{
#if !defined(_WIN32) 
	struct rlimit	rl;

	raise_process_fd_limits();

#ifdef RLIMIT_DATA
	if (getrlimit(RLIMIT_DATA,&rl) == 0) {
	    rl.rlim_cur = rl.rlim_max;
	    if (setrlimit(RLIMIT_DATA,&rl) != 0) {
	        LDAPDebug(LDAP_DEBUG_TRACE,"setrlimit(RLIMIT_DATA) failed %d\n",
			  errno,0,0);
	    }
	} else {
	    LDAPDebug(LDAP_DEBUG_TRACE,"getrlimit(RLIMIT_DATA) failed %d\n",
		      errno,0,0);
	}
#endif

#ifdef RLIMIT_VMEM
	if (getrlimit(RLIMIT_VMEM,&rl) == 0) {
	    rl.rlim_cur = rl.rlim_max;
	    if (setrlimit(RLIMIT_VMEM,&rl) != 0) {
	        LDAPDebug(LDAP_DEBUG_TRACE,"setrlimit(RLIMIT_VMEM) failed %d\n",
			  errno,0,0);
	    }
	} else {
	    LDAPDebug(LDAP_DEBUG_TRACE,"getrlimit(RLIMIT_VMEM) failed %d\n",
		      errno,0,0);
	}
#endif /* RLIMIT_VMEM */

#endif /* !_WIN32 */
}

