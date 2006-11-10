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
