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

/*
 * snmpctrl.c - start/stop/restart LDAP-based SNMP subagent
 *
 * Steve Ross -- 08/12/97
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libadminutil/admutil.h"
#include "dsalib.h"
#include "init_ds_env.h"
#include "nspr.h"

#if !defined(_WIN32)
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#else
#include <windows.h>
#endif

#define SUBAGT_PATH     "bin/slapd/server"
#define SUBAGT_NAME     "ns-ldapagt"

#define	START		1
#define	STOP		2
#define	RESTART		3

#define	NSLDAPAGT_PID	"NSLDAPAGT.LK"

#ifdef __cplusplus
extern "C" {
#endif
int nsldapagt_is_running(void);
int nsldapagt_shutdown(void);
int nsldapagt_start(void);
int nsldapagt_restart(void);
#ifdef __cplusplus
}
#endif

int main(int argc, char *argv[])
{
	char *action_type     = NULL;
	int status = 1;

	fprintf(stdout, "Content-type: text/html\n\n");

	if ( init_ds_env() )
		return 1;

	action_type = ds_a_get_cgi_var("ACTION", "Missing Command",
								   "Need to specify Start, Stop, or Restart");
	if (!action_type)
		return 1;

	if (!strcmp(action_type, "START")) {
		status = nsldapagt_start();
	} else if (!strcmp(action_type, "STOP")) {
		status = nsldapagt_shutdown();
	} else if (!strcmp(action_type, "RESTART")) {
		status = nsldapagt_restart();
	} else {
		status = DS_UNKNOWN_SNMP_COMMAND;
	}
	
	if ( !status ) {
		rpt_success("Success!");
		return 0;
	} else {
		rpt_err( status, action_type, NULL,  NULL );
		return 1;
	}
}

#if !defined(_WIN32)
int
get_nsldapagt_pid(pid_t *pid)
{
   char *SLAPD_ROOT;
   char path[PATH_MAX];
   FILE *fp;

   *pid = -1;

   SLAPD_ROOT = ds_get_install_root();
   PR_snprintf(path, sizeof(path), "%s/logs/%s", SLAPD_ROOT, NSLDAPAGT_PID);
   if (!ds_file_exists(path)) {
      return(-1);
   }

   if ((fp = fopen(path, "r")) != (FILE *) NULL) {
      if ((fscanf(fp, "%d\n", (int *) pid)) != -1) {
	    (void) fclose(fp);
	    return(0);
      }
   }

   (void) fclose(fp);
   return(-1);
}
#endif

#if defined(_WIN32)
BOOL isServiceRunning(LPCTSTR szServiceId)
{
    BOOL bReturn = FALSE;
    DWORD dwError = 0;
    SC_HANDLE schService = NULL;
    SC_HANDLE schSCManager = NULL;
    SERVICE_STATUS lpss;

  	if((schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS)))
    {
        if((schService = OpenService(schSCManager,
                                    szServiceId,
                                    SERVICE_ALL_ACCESS)))
        {
	
			bReturn = ControlService(schService, SERVICE_CONTROL_INTERROGATE , &lpss);
     
			if(SERVICE_RUNNING == lpss.dwCurrentState)
			{
				bReturn = TRUE;
			}

			CloseServiceHandle(schService);
        }
        dwError = GetLastError();
        CloseServiceHandle(schSCManager);
    }
    return(bReturn);
}
#endif

/*
 * This routine returns:
 *	0 if nsldapagt is NOT running
 *	1 if nsldapagt is actually running
 */
int
nsldapagt_is_running()
{

#if defined(_WIN32)
   if (FALSE == isServiceRunning("SNMP") )
   {
		return(0);
   }
#else
   pid_t pid;

   if (get_nsldapagt_pid(&pid) != 0) {
      return(0);
   }

   if (kill(pid, 0) == -1) {
      return(0);
   }
#endif
   return(1);
}

#if !defined(_WIN32)
/*
 * This routine returns:
 *	0 if magt is NOT running
 *	1 if magt is actually running
 *
 * The run state is determined whether one can successfully bind to the
 * smux port.
 *
 * this is for UNIX only
 */
int
smux_master_is_running()
{
   struct servent  *pse;
   struct protoent *ppe;
   struct sockaddr_in sin;
   int    s;

   sin.sin_family = AF_INET;
   sin.sin_addr.s_addr = INADDR_ANY;

   if ((pse = getservbyname("smux", "tcp"))) {
      sin.sin_port = ntohs(pse->s_port);
   } else {
      sin.sin_port = 199;
   }

   if ((ppe = getprotobyname("tcp")) == 0) {
      return(0);
   }

   if ((s = socket(AF_INET, SOCK_STREAM, ppe->p_proto)) < 0) {
      return(0);
   }

   /* bind expects port number to be in network order
      we should do this for all platforms, not just OSF. */
   sin.sin_port = htons(sin.sin_port);
   if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
      close(s);
      return(1);
   } else {
   }

   close(s);
   return(0);
}
#endif

int
nsldapagt_start()
{
   if (nsldapagt_is_running()) {
      return(0);
   }

#if defined(_WIN32) 
/* NT version -- just try to start the SNMP service */
/* Bug 612322: redirecting the output to null device */
	system("net start SNMP > nul");

#else

   /*
    * Check if smux master agent is running before firing off the subagent!
    */
   if (!smux_master_is_running()) {
      return(-1);
   } else {
	   char *NETSITE_ROOT = getenv("NETSITE_ROOT");
	   char *SLAPD_ROOT = ds_get_install_root();
	   char command[1024];

	   PR_snprintf(command, sizeof(command), "cd %s/%s; ./%s -d %s", NETSITE_ROOT, SUBAGT_PATH,
           SUBAGT_NAME, SLAPD_ROOT);

	   (void) system(command);
	   sleep(2);
   }
#endif

   if (!nsldapagt_is_running()) {
      return(-1);
   }

   return(0);
}

int
nsldapagt_shutdown()
{
   if (!nsldapagt_is_running()) {
        rpt_success("NOT_RUNNING");
        exit(0);

   } else {
	   int status = -1;

#if defined(_WIN32)
	   /* NT version -- just try to stop the SNMP service */
	   /* Bug 612322: redirecting the output to null device */
	   status = system("net stop SNMP > nul");

#else
	  /* UNIX version */
      pid_t pid;
      if (get_nsldapagt_pid(&pid) == 0) 
	  {
	      if (kill(pid, SIGTERM) == 0) 
		  {
	          sleep(2);
	          if (!nsldapagt_is_running()) 
			  {
	              status = 0;
			  }
		  }
      }
#endif
	  return(status);
   }
   return(0);
}


int
nsldapagt_restart()
{
	int status;
	if ( (status = nsldapagt_shutdown()) != 0 )
		return status;
	else
		return nsldapagt_start();
}

