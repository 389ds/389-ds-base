/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "slap.h"

#define SLAPD_HOUSEKEEPING_INTERVAL 30	/* seconds */

static PRThread *housekeeping_tid = NULL;
static PRLock *housekeeping_mutex = NULL;
static PRCondVar *housekeeping_cvar = NULL;


static void
housecleaning(void *cur_time)
{
	int interval;

	interval = PR_SecondsToInterval( SLAPD_HOUSEKEEPING_INTERVAL );
	while ( !g_get_shutdown() ) {
		/*
		 * Looks simple, but could potentially take a long time.
		 */
		be_flushall();

		log_access_flush();

		if ( g_get_shutdown() ) {
			break;
		}
		PR_Lock( housekeeping_mutex );
        PR_WaitCondVar( housekeeping_cvar, interval );
        PR_Unlock( housekeeping_mutex );
	}
}

PRThread*
housekeeping_start(time_t cur_time, void *arg)
{
	static time_t	thread_start_time;

	if ( housekeeping_tid ) {
		return housekeeping_tid;
	}

	if ( ( housekeeping_mutex = PR_NewLock()) == NULL ) {
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
				"housekeeping cannot create new lock. "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				PR_GetError(), slapd_pr_strerror( PR_GetError() ));
	}
	else if ( ( housekeeping_cvar = PR_NewCondVar( housekeeping_mutex )) == NULL ) {
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
				"housekeeping cannot create new condition variable. "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				PR_GetError(), slapd_pr_strerror( PR_GetError() ));
	}
	else {
		thread_start_time = cur_time;
		if ((housekeeping_tid = PR_CreateThread(PR_USER_THREAD, 
				(VFP) housecleaning, (void*)&thread_start_time,
				PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 
				SLAPD_DEFAULT_THREAD_STACKSIZE)) == NULL) {
			slapi_log_error(SLAPI_LOG_FATAL, NULL,
					"housekeeping PR_CreateThread failed. "
					SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					PR_GetError(), slapd_pr_strerror( PR_GetError() ));
		}
	}

	return housekeeping_tid;
}

void
housekeeping_stop()
{
	if ( housekeeping_tid ) {
		PR_Lock( housekeeping_mutex );
		PR_NotifyCondVar( housekeeping_cvar );
		PR_Unlock( housekeeping_mutex );
		(void)PR_JoinThread( housekeeping_tid );
	}
}
