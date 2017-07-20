/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "repl5_prot_private.h"

#ifndef _WINDOWS_PROT_PRIVATE_H_
#define _WINDOWS_PROT_PRIVATE_H_

#define ACQUIRE_SUCCESS 101
#define ACQUIRE_REPLICA_BUSY 102
#define ACQUIRE_FATAL_ERROR 103
#define ACQUIRE_CONSUMER_WAS_UPTODATE 104
#define ACQUIRE_TRANSIENT_ERROR 105

#define PROTOCOL_TERMINATION_NORMAL 301
#define PROTOCOL_TERMINATION_ABNORMAL 302
#define PROTOCOL_TERMINATION_NEEDS_TOTAL_UPDATE 303

#define RESUME_DO_TOTAL_UPDATE 401
#define RESUME_DO_INCREMENTAL_UPDATE 402
#define RESUME_TERMINATE 403
#define RESUME_SUSPEND 404

/* Backoff timer settings for reconnect */
#define PROTOCOL_BACKOFF_MINIMUM 3        /* 3 seconds */
#define PROTOCOL_BACKOFF_MAXIMUM (60 * 5) /* 5 minutes */
/* Backoff timer settings for replica busy reconnect */
#define PROTOCOL_BUSY_BACKOFF_MINIMUM PROTOCOL_BACKOFF_MINIMUM
#define PROTOCOL_BUSY_BACKOFF_MAXIMUM PROTOCOL_BUSY_BACKOFF_MINIMUM

/* protocol related functions */

CSN *get_current_csn(Slapi_DN *replarea_sdn);
char *protocol_response2string(int response);

void windows_dirsync_inc_run(Private_Repl_Protocol *prp);
ConnResult windows_replay_update(Private_Repl_Protocol *prp, slapi_operation_parameters *op);
int windows_process_total_entry(Private_Repl_Protocol *prp, Slapi_Entry *e);

PRBool windows_ignore_error_and_keep_going(int error);

#endif /* _REPL5_PROT_PRIVATE_H_ */
