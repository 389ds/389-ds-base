/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Remove the server
 * 
 * Prasanta Behera
 */
#ifdef XP_WIN32
#include <windows.h>
#include <io.h>
#include "regparms.h"
extern BOOL DeleteServer(LPCSTR pszServiceId);
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#ifdef XP_UNIX
#include <sys/errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#else
#endif /* WIN32? */
#include <sys/stat.h>

#include "dsalib.h"
#include "init_ds_env.h"
#include "ds_remove_uninst.h"


#include "nspr.h"

/* this will be set to 1 if we need to retry the
   rm -rf of the instance directory again */
static int try_rm_rf_again = 0;

static int
rm_rf_err_func(const char *path, const char *op, void *arg)
{
	PRInt32 errcode = PR_GetError();
	char *msg;
	const char *errtext;

	if (!errcode || (errcode == PR_UNKNOWN_ERROR)) {
		errcode = PR_GetOSError();
		errtext = ds_system_errmsg();
	} else {
		errtext = PR_ErrorToString(errcode, PR_LANGUAGE_I_DEFAULT);
	}

	/* ignore "file or directory already removed" errors */
	if (errcode != PR_FILE_NOT_FOUND_ERROR) {
		msg = PR_smprintf("%s %s: error code %d (%s)", op, path, errcode, errtext);
		ds_send_error(msg, 0);
		PR_smprintf_free(msg);
	}

	/* On Windows and HPUX, if the file/directory to remove is opened by another
	   application, it cannot be removed and will generate a busy error
	   This usually happens when we attempt to stop slapd then remove the
	   instance directory, but for some reason the process still has some
	   open files
	   In this case, we need to wait for some period of time then attempt to
	   remove the instance directory again
	*/
	if (errcode == PR_FILE_IS_BUSY_ERROR) {
		try_rm_rf_again = 1;
		return 0; /* just abort the operation */
	}

#ifdef XP_WIN32
	/* on windows, err 145 means dir not empty
	   145 The directory is not empty.  ERROR_DIR_NOT_EMPTY 
	   If there was a busy file, it wasn't able to be
	   removed, so when we go to remove the directory, it
	   won't be empty
	*/
	if (errcode == ERROR_DIR_NOT_EMPTY) {
		if (try_rm_rf_again) {
			return 0; /* don't continue */
		}
	}
#else /* unix */
	if (errcode == EEXIST) { /* not empty */
		if (try_rm_rf_again) {
			return 0; /* don't continue */
		}
	}
#endif

	return 1; /* just continue */	
}

int main(int argc, char *argv[])
{
    int 	status = -1;
    char	*servername;
    char	*installroot;
    int		isRunning;
#ifndef __LP64__  
#ifdef hpux
    _main();
#endif
#endif

#ifdef XP_WIN32
    if ( getenv("DEBUG_DSINST") )
	DebugBreak();
#endif

	/* case 1: being called as program -f inffile */
	if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'f')
	{
		FILE *infFile = fopen(argv[2], "r");
		if (!infFile)
		{
			ds_report_error (DS_INCORRECT_USAGE, argv[2],
							 "This file could not be opened.  A valid file must be given.");
			status = 1;

			return status;
		}
		else
			fclose(infFile);

		ds_uninst_set_cgi_env(argv[2]);
	} else if (getenv("REQUEST_METHOD")) { /* case 2: called as a CGI */
		fprintf(stdout, "Content-type: text/html\n\n");
		fflush(stdout);
	} else { /* case 3: run from the command line */
		/* when being run from the command line, we require many command line arguments */
		/* we need to do 2 or three things:
		   1 - stop the server and remove the server instance directory
		   2 - remove the server's information from the config ds
		   3 - On Windows, remove the registry information
		   We require the instance name as an argument.  We also need the following:
		   For 1, we need the server root
		   For 2, we need the config ds host, port, admin domain, admin dn, admin password
		   For 3, just the instance name

		   There are two other arguments that are optional.  -force will ignore errors and just keep
		   going.  On Windows, -allreg will clean up all known registry information for all instances
		   of DS on this machine
		*/
	}
		

	if ( init_ds_env() ) {
		return 1;
	}

	/*
 	 * Get the server pathto delete.
	 * serevrpath = /export/serevrs/dirserv/slapd-talac
	 */
	if (!(servername = ds_get_cgi_var("InstanceName")))
		servername = ds_get_server_name();

	/* Check again if the serevr is down or not */
	if((isRunning = ds_get_updown_status()) == DS_SERVER_UP) {
		if ((status = ds_bring_down_server()) != DS_SERVER_DOWN) {
			char buf[1024];
			sprintf(buf, "Could not stop server: error %d", status);
			ds_report_error (DS_GENERAL_FAILURE, servername, buf);
			return 1;
		}
	}

	if (servername) {
		char line[1024];
		int busy_retries = 3; /* if busy, retry this many times */
		installroot = ds_get_install_root();
		/* We may get busy errors if files are in use when we try
		   to remove them, so if that happens, sleep for 30 seconds
		   and try again */
		status = ds_rm_rf(installroot, rm_rf_err_func, NULL);
		while (status && try_rm_rf_again && busy_retries) {
			sprintf(line, "Some files or directories in %s are still in use.  Will sleep for 30 seconds and try again.",
					installroot);
			ds_show_message(line);
			PR_Sleep(PR_SecondsToInterval(30));
			try_rm_rf_again = 0;
			--busy_retries;
			status = ds_rm_rf(installroot, rm_rf_err_func, NULL);
		}
		if (status) {
			sprintf(line, "Could not remove %s.  Please check log messages and try again.",
					installroot);
			ds_send_error(line, 0);
		}
	}
#ifdef XP_WIN32
	if (servername) {
		status += ds_remove_reg_key(HKEY_LOCAL_MACHINE, "%s\\%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE,
									DS_NAME_SHORT, DS_VERSION, servername);

		/* also try to remove version key in case this is the last instance */
		status += ds_remove_reg_key(HKEY_LOCAL_MACHINE, "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE,
									DS_NAME_SHORT, DS_VERSION);

		/* also try to remove product key in case this is the last instance */
		status += ds_remove_reg_key(HKEY_LOCAL_MACHINE, "%s\\%s", KEY_SOFTWARE_NETSCAPE,
									DS_NAME_SHORT);

		/* also need to remove service */
		if (!DeleteServer(servername)) {
			status += 1;
		}

		/* Remove Event Log Key */
		status += ds_remove_reg_key(HKEY_LOCAL_MACHINE, "%s\\%s\\%s", KEY_SERVICES, KEY_EVENTLOG_APP, servername);
	}
#endif

	if (status == 0) {
		char buf[1024];
		sprintf(buf, "Server %s was successfully removed", servername);
		ds_show_message(buf);
		rpt_success("");
	} else {
		char buf[1024];
		sprintf(buf, "Could not remove server %s", servername);
		ds_send_error(buf, 0);
	}

	return status;
}
