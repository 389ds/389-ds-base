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

#include "repl5_prot_private.h"
 
#ifndef _WINDOWS_PROT_PRIVATE_H_
#define _WINDOWS_PROT_PRIVATE_H_

#define ACQUIRE_SUCCESS 101
#define ACQUIRE_REPLICA_BUSY 102
#define ACQUIRE_FATAL_ERROR 103
#define ACQUIRE_CONSUMER_WAS_UPTODATE 104
#define ACQUIRE_TRANSIENT_ERROR 105

typedef struct windows_private_repl_protocol
{
	void (*delete)(struct windows_private_repl_protocol **);
	void (*run)(struct windows_private_repl_protocol *);
	int (*stop)(struct windows_private_repl_protocol *);
	int (*status)(struct windows_private_repl_protocol *);
	void (*notify_update)(struct windows_private_repl_protocol *);
	void (*notify_agmt_changed)(struct windows_private_repl_protocol *);
        void (*notify_window_opened)(struct windows_private_repl_protocol *);
        void (*notify_window_closed)(struct windows_private_repl_protocol *);
	void (*update_now)(struct windows_private_repl_protocol *);
	PRLock *lock;
	PRCondVar *cvar;
	int stopped;
	int terminate;
	PRUint32 eventbits;
	Repl_Connection *conn;
	int last_acquire_response_code;
	Repl_Agmt *agmt;
	Object *replica_object;
	void *private;
    PRBool replica_acquired;
} Windows_Private_Repl_Protocol;

/*
extern Windows_Private_Repl_Protocol *Windows_Inc_Protocol_new();
extern Windows_Private_Repl_Protocol *Windows_Tot_Protocol_new();
*/

#define PROTOCOL_TERMINATION_NORMAL 301
#define PROTOCOL_TERMINATION_ABNORMAL 302
#define PROTOCOL_TERMINATION_NEEDS_TOTAL_UPDATE 303

#define RESUME_DO_TOTAL_UPDATE 401
#define RESUME_DO_INCREMENTAL_UPDATE 402
#define RESUME_TERMINATE 403
#define RESUME_SUSPEND 404

/* Backoff timer settings for reconnect */
#define PROTOCOL_BACKOFF_MINIMUM 3 /* 3 seconds */
#define PROTOCOL_BACKOFF_MAXIMUM (60 * 5) /* 5 minutes */
/* Backoff timer settings for replica busy reconnect */
#define PROTOCOL_BUSY_BACKOFF_MINIMUM PROTOCOL_BACKOFF_MINIMUM
#define PROTOCOL_BUSY_BACKOFF_MAXIMUM PROTOCOL_BUSY_BACKOFF_MINIMUM

/* protocol related functions */

CSN *get_current_csn(Slapi_DN *replarea_sdn);
char* protocol_response2string (int response);

void windows_dirsync_inc_run(Private_Repl_Protocol *prp);
ConnResult windows_replay_update(Private_Repl_Protocol *prp, slapi_operation_parameters *op);
int windows_process_total_entry(Private_Repl_Protocol *prp,Slapi_Entry *e);

#endif /* _REPL5_PROT_PRIVATE_H_ */
