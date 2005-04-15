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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/******************************************************
 *
 *
 *  ntwatchdog.h - Defs for NT Watchdog Service.
 *
 ******************************************************/

#if defined( _WIN32 )

#if !defined( _NTWATCHDOG_H_ )
#define	_NTWATCHDOG_H_

#define FILE_PATHSEP '/'

#define SLAPD_ROOT             "SLAPD_ROOT"   // environment variable holding server root path
#define MORTALITY_KEY          "MortalityTimeSecs"
#define MINRAMFREE_KEY         "MinRamFree"
#define MINRAMTOTAL_KEY        "MinRamTotal"
#define MINRAMPERSERVER_KEY    "MinRamPerServer"
#define DEFAULT_MORTALITY_TIME  60              // seconds after startup up until server will NOT be restarted
#define DEFAULT_KILL_TIME       600             // seconds to wait for httpd.exe to shutdown
#define DEFAULT_CRON_TIME       60              // seconds to wait before rechecking cron.conf
#define DEFAULT_RESTART_TIME    10              // seconds to wait before restarting server
#define DEFAULT_MINRAMFREE      0               // KB free physical memory remaining
#define DEFAULT_MINRAMTOTAL     (30 * 1024)     // KB free physical memory installed
#define DEFAULT_MINRAMPERSERVER (15 * 1024)     // KB free physical memory per server

#define MSG_RESOURCES         "Not enough physical memory to start server."

// offsets for extra window bytes, used in Set/GetWindowLong()
#define GWL_PROCESS_HANDLE  (sizeof(LONG) * 0)
#define GWL_PASSWORD_ADDR   (sizeof(LONG) * 1)
#define GWL_PASSWORD_LENGTH (sizeof(LONG) * 2)

#define MAX_LINE      512
#define MAX_PASSWORD  256
#define MAX_TOKENNAME 50

typedef struct PK11_PIN
{
	char TokenName[MAX_TOKENNAME];
	int  TokenLength;
	char Password[MAX_PASSWORD];
	int  PasswordLength;
}PK11_PIN;

#define CLOSEHANDLE(X) \
{ \
	if(X) \
	{ \
		CloseHandle(X); \
		X = 0; \
	} \
}

// in ntcron.c
LPTHREAD_START_ROUTINE CRON_ThreadProc(HANDLE hevWatchDogExit);

// in watchdog.c
BOOL WD_SysLog(WORD fwEventType, DWORD IDEvent, char *szData);

#endif /* _NTWATCHDOG_H_ */
#endif /* _WIN32 */
