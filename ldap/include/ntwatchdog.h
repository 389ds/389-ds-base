/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
