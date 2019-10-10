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

/*
 * the "new" ("deluxe") backend import code
 *
 * please make sure you use 4-space indentation on this file.
 */

#include "back-ldbm.h"
#include "vlv_srch.h"
#include "dblayer.h"
#include "import.h"

#define ERR_IMPORT_ABORTED -23
#define NEED_DN_NORM -24
#define NEED_DN_NORM_SP -25
#define NEED_DN_NORM_BT -26


/********** routines to manipulate the entry fifo **********/

/********** logging stuff **********/

#define LOG_BUFFER 512


/* this adds a line to the 'nsTaskLog' value, which is cumulative (anything
 * logged here is added to the end)
 */
void
import_log_notice(ImportJob *job, int log_level, char *subsystem, char *format, ...)
{
    va_list ap;
    char buffer[LOG_BUFFER];

    va_start(ap, format);
    PR_vsnprintf(buffer, LOG_BUFFER, format, ap);
    va_end(ap);

    if (job->task) {
        slapi_task_log_notice(job->task, "%s", buffer);
    }
    /* also save it in the logs for posterity */
    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        slapi_log_err(log_level, subsystem, "upgradedn %s: %s\n",
                      job->inst->inst_name, buffer);
    } else if (job->flags & FLAG_REINDEXING) {
        slapi_log_err(log_level, subsystem, "reindex %s: %s\n",
                      job->inst->inst_name, buffer);
    } else {
        slapi_log_err(log_level, subsystem, "import %s: %s\n",
                      job->inst->inst_name, buffer);
    }
}


/********** starting threads and stuff **********/

/* Solaris is weird---we need an LWP per thread but NSPR doesn't give us
 * one unless we make this magic belshe-call */
/* Fixed on Solaris 8; NSPR supports PR_GLOBAL_BOUND_THREAD */
#define CREATE_THREAD PR_CreateThread

/********** monitoring the worker threads **********/



#define IMPORT_CHUNK_TEST_HOLDOFF_TIME (5 * 60) /* Seconds */

/* Got to be lower than this: */
#define IMPORT_CHUNK_TEST_CACHE_HIT_RATIO (0.99)
/* Less than half as fast as we were doing: */
#define IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_A (0.5)
/* A lot less fast than we were doing: */
#define IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_B (0.1)


/* find the rate (ids/time) of work from a worker thread between history
 * marks A and B.
 */
#define HISTORY(N) (job->progress_history[N])
#define TIMES(N) (job->progress_times[N])
#define PROGRESS(A, B) ((HISTORY(B) > HISTORY(A)) ? ((double)(HISTORY(B) - HISTORY(A)) / \
                                                     (double)(TIMES(B) - TIMES(A)))      \
                                                  : (double)0)

/********** running passes **********/


/* Task here is as follows:
 * First, if this is pass #1, check for the presence of a merge
 *     directory. If it is not present, create it.
 * If it is present, delete all the files in it.
 * Then, flush the dblayer and close files.
 * Now create a numbered subdir of the merge directory for this pass.
 * Next, move the index files, except entrydn, parentid and id2entry to
 *     the merge subdirectory. Important to move if we can, because
 *     that can be millions of times faster than a copy.
 * Finally open the dblayer back up because the caller expects
 *     us to not muck with it.
 */

int
import_main_offline(void *arg)
{
    ImportJob *job = (ImportJob *)arg;
    ldbm_instance *inst = job->inst;
    dblayer_private *priv = (dblayer_private *)inst->inst_li->li_dblayer_private;

    return priv->dblayer_import_fn(arg);

}
