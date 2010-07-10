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
 * the "new" ("deluxe") backend import code
 *
 * please make sure you use 4-space indentation on this file.
 */

#include "back-ldbm.h"
#include "vlv_srch.h"
#include "import.h"

#define ERR_IMPORT_ABORTED      -23
#define DRYRUN_QUIT             -24


/********** routines to manipulate the entry fifo **********/

/* this is pretty bogus -- could be a HUGE amount of memory */
/* Not anymore with the Import Queue Adaptative Algorithm (Regulation) */
#define MAX_FIFO_SIZE    8000

static int import_fifo_init(ImportJob *job)
{
    ldbm_instance *inst = job->inst;

    /* Work out how big the entry fifo can be */
    if (inst->inst_cache.c_maxentries > 0)
    job->fifo.size = inst->inst_cache.c_maxentries;
    else
    job->fifo.size = inst->inst_cache.c_maxsize / 1024;    /* guess */

    /* byte limit that should be respected to avoid memory starvation */
    /* conservative computing: multiply by .8 to allow for reasonable overflow */
    job->fifo.bsize = (inst->inst_cache.c_maxsize/10) << 3;

    job->fifo.c_bsize = 0;
    
    if (job->fifo.size > MAX_FIFO_SIZE)
    job->fifo.size = MAX_FIFO_SIZE;
    /* has to be at least 1 or 2, and anything less than about 100 destroys
     * the point of doing all this optimization in the first place. */
    if (job->fifo.size < 100)
        job->fifo.size = 100;

    /* Get memory for the entry fifo */
    /* This is used to keep a ref'ed pointer to the last <cachesize> 
     * processed entries */
    PR_ASSERT(NULL == job->fifo.item);
    job->fifo.item = (FifoItem *)slapi_ch_calloc(job->fifo.size,
                         sizeof(FifoItem));
    if (NULL == job->fifo.item) {
    /* Memory allocation error */
    return -1;
    }
    return 0;
}

FifoItem *import_fifo_fetch(ImportJob *job, ID id, int worker)
{
    int idx = id % job->fifo.size;
    FifoItem *fi;

    if (job->fifo.item) {
        fi = &(job->fifo.item[idx]);
    } else {
        return NULL;
    }
    if (fi->entry) {
        if (worker) {
            if (fi->bad) {
                import_log_notice(job, "WARNING: bad entry: ID %d", id);
                return NULL;
            }
            PR_ASSERT(fi->entry->ep_refcnt > 0);
        }
    }
    return fi;
}

static void import_fifo_destroy(ImportJob *job)
{
    /* Free any entries in the fifo first */
    struct backentry *be = NULL;
    size_t i = 0;

    for (i = 0; i < job->fifo.size; i++) {
        be = job->fifo.item[i].entry;
        backentry_free(&be);
        job->fifo.item[i].entry = NULL;
        job->fifo.item[i].filename = NULL;
    }
    slapi_ch_free((void **)&job->fifo.item);
    job->fifo.item = NULL;
}


/********** logging stuff **********/

#define LOG_BUFFER        512

/* this changes the 'nsTaskStatus' value, which is transient (anything logged
 * here wipes out any previous status)
 */
static void import_log_status_start(ImportJob *job)
{
    if (! job->task_status)
    job->task_status = (char *)slapi_ch_malloc(10 * LOG_BUFFER);
    if (! job->task_status)
    return;        /* out of memory? */

    job->task_status[0] = 0;
}

static void import_log_status_add_line(ImportJob *job, char *format, ...)
{
    va_list ap;
    int len = 0;

    if (! job->task_status)
        return;
    len = strlen(job->task_status);
    if (len + 5 > (10 * LOG_BUFFER))
        return;         /* no room */

    if (job->task_status[0])
        strcat(job->task_status, "\n");

    va_start(ap, format);
    PR_vsnprintf(job->task_status + len, (10 * LOG_BUFFER) - len, format, ap);
    va_end(ap);
}

static void import_log_status_done(ImportJob *job)
{
    if (job->task) {
        slapi_task_log_status(job->task, "%s", job->task_status);
    }
}

/* this adds a line to the 'nsTaskLog' value, which is cumulative (anything
 * logged here is added to the end)
 */
void import_log_notice(ImportJob *job, char *format, ...)
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
    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        LDAPDebug(LDAP_DEBUG_ANY, "upgradedn %s: %s\n", job->inst->inst_name,
                  buffer, 0);
    } else if (job->flags & FLAG_REINDEXING) {
        LDAPDebug(LDAP_DEBUG_ANY, "reindex %s: %s\n", job->inst->inst_name,
                  buffer, 0);
    } else {
        LDAPDebug(LDAP_DEBUG_ANY, "import %s: %s\n", job->inst->inst_name,
                  buffer, 0);
    }
}

static void import_task_destroy(Slapi_Task *task)
{
    ImportJob *job = (ImportJob *)slapi_task_get_data(task);

    if (job && job->task_status) {
        slapi_ch_free((void **)&job->task_status);
        job->task_status = NULL;
    }
    FREE(job);
    slapi_task_set_data(task, NULL);
}

static void import_task_abort(Slapi_Task *task)
{
    ImportJob *job;

    /* don't log anything from here, because we're still holding the
     * DSE lock for modify...
     */

    if (slapi_task_get_state(task) == SLAPI_TASK_FINISHED) {
        /* too late */
    }

    /*
     * Race condition.
     * If the import thread happens to finish right now we're in trouble
     * because it will free the job.
     */

    job = (ImportJob *)slapi_task_get_data(task);

    import_abort_all(job, 0); 
    while (slapi_task_get_state(task) != SLAPI_TASK_FINISHED)
         DS_Sleep(PR_MillisecondsToInterval(100));

}


/********** helper functions for importing **********/


/* Function used to gather a list of indexed attrs */
static int import_attr_callback(void *node, void *param)
{
    ImportJob *job = (ImportJob *)param;
    struct attrinfo *a = (struct attrinfo *)node;

    if (job->flags & FLAG_DRYRUN) { /* dryrun; we don't need the workers */
        return 0;
    }
    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        /* Bring up import workers just for indexes having DN syntax 
         * attribute type. (except entrydn -- taken care below) */
        int rc = 0;
        Slapi_Attr attr = {0};

        /* 
         * Treat cn and ou specially.  Bring up the import workers for
         * cn and ou even though they are not DN syntax attribute.
         * This is done because they have some exceptional case to store
         * DN format in the admin entries such as UserPreferences.
         */
        if ((0 == PL_strcasecmp("cn", a->ai_type)) || 
            (0 == PL_strcasecmp("commonname", a->ai_type)) ||
            (0 == PL_strcasecmp("ou", a->ai_type)) ||
            (0 == PL_strcasecmp("organizationalUnit", a->ai_type))) {
            ;
        } else {
            slapi_attr_init(&attr, a->ai_type);
            rc = slapi_attr_is_dn_syntax_attr(&attr);
            attr_done(&attr);
            if (0 == rc) {
                return 0;
            }
        }
    }

    /* OK, so we now have hold of the attribute structure and the job info, 
     * let's see what we have.  Remember that although this function is called
     * many times, all these calls are in the context of a single thread, so we
     * don't need to worry about protecting the data in the job structure.
     */

    /* We need to specifically exclude the (entrydn, entryrdn) & parentid &
     * ancestorid indexes because we build those in the foreman thread.
     */
    if (IS_INDEXED(a->ai_indexmask) &&
        (strcasecmp(a->ai_type, LDBM_ENTRYDN_STR) != 0) &&
        (strcasecmp(a->ai_type, LDBM_ENTRYRDN_STR) != 0) &&
        (strcasecmp(a->ai_type, LDBM_PARENTID_STR) != 0) &&
        (strcasecmp(a->ai_type, LDBM_ANCESTORID_STR) != 0) &&
        (strcasecmp(a->ai_type, numsubordinates) != 0)) {
        /* Make an import_index_info structure, fill it in and insert into the
         * job's list */
        IndexInfo *info = CALLOC(IndexInfo);
    
        if (NULL == info) {
            /* Memory allocation error */
            return -1;
        }
        info->name = slapi_ch_strdup(a->ai_type);
        info->ai = a;
        if (NULL == info->name) {
            /* Memory allocation error */
            FREE(info);
            return -1;
        }
        info->next = job->index_list;
        job->index_list = info;
        job->number_indexers++;
    }
    return 0;
}

static void import_set_index_buffer_size(ImportJob *job)
{
    IndexInfo *current_index = NULL;
    size_t substring_index_count = 0;
    size_t proposed_size = 0;

    /* Count the substring indexes we have */
    for (current_index = job->index_list; current_index != NULL;
     current_index = current_index->next)  {
    if (current_index->ai->ai_indexmask & INDEX_SUB) {
        substring_index_count++;
    }
    }
    if (substring_index_count > 0) {
    /* Make proposed size such that if all substring indices were
     * reasonably full, we'd hit the target space */
    proposed_size = (job->job_index_buffer_size / substring_index_count) /
        IMPORT_INDEX_BUFFER_SIZE_CONSTANT;
    if (proposed_size > IMPORT_MAX_INDEX_BUFFER_SIZE) {
        proposed_size = IMPORT_MAX_INDEX_BUFFER_SIZE;
    }
    if (proposed_size < IMPORT_MIN_INDEX_BUFFER_SIZE) {
        proposed_size = 0;
    }
    }

    job->job_index_buffer_suggestion = proposed_size;
}

static void import_free_thread_data(ImportJob *job)
{
    /* DBDB free the lists etc */
    ImportWorkerInfo *worker = job->worker_list;

    while (worker != NULL) {
    ImportWorkerInfo *asabird = worker;
    worker = worker->next;
    if (asabird->work_type != PRODUCER)
        slapi_ch_free( (void**)&asabird);
    }
}

void import_free_job(ImportJob *job)
{
    /* DBDB free the lists etc */
    IndexInfo *index = job->index_list;

    import_free_thread_data(job);
    while (index != NULL) {
        IndexInfo *asabird = index;
        index = index->next;
        slapi_ch_free( (void**)&asabird->name);
        slapi_ch_free( (void**)&asabird);
    }
    job->index_list = NULL;
    if (NULL != job->mothers) {
        import_subcount_stuff_term(job->mothers);
        slapi_ch_free( (void**)&job->mothers);
    }
    
    ldbm_back_free_incl_excl(job->include_subtrees, job->exclude_subtrees);
    charray_free(job->input_filenames);
    if (job->fifo.size)
        import_fifo_destroy(job);
    if (NULL != job->uuid_namespace)
        slapi_ch_free((void **)&job->uuid_namespace);
    if (job->wire_lock)
        PR_DestroyLock(job->wire_lock);
    if (job->wire_cv)
        PR_DestroyCondVar(job->wire_cv);
    slapi_ch_free((void **)&job->task_status);
}

/* determine if we are the correct backend for this entry
 * (in a distributed suffix, some entries may be for other backends).
 * if the entry's dn actually matches one of the suffixes of the be, we 
 * automatically take it as a belonging one, for such entries must be 
 * present in EVERY backend independently of the distribution applied.
 */
int import_entry_belongs_here(Slapi_Entry *e, backend *be)
{
    Slapi_Backend *retbe;
    Slapi_DN *sdn = slapi_entry_get_sdn(e);

    if (slapi_be_issuffix(be, sdn))
        return 1;

    retbe = slapi_mapping_tree_find_backend_for_sdn(sdn);
    return (retbe == be);
}


/********** starting threads and stuff **********/

/* Solaris is weird---we need an LWP per thread but NSPR doesn't give us
 * one unless we make this magic belshe-call */
/* Fixed on Solaris 8; NSPR supports PR_GLOBAL_BOUND_THREAD */
#define CREATE_THREAD PR_CreateThread

static void import_init_worker_info(ImportWorkerInfo *info, ImportJob *job)
{
    info->command = PAUSE; 
    info->job = job;
    info->first_ID = job->first_ID;
    info->index_buffer_size = job->job_index_buffer_suggestion;
}

static int import_start_threads(ImportJob *job)
{
    IndexInfo *current_index = NULL;
    ImportWorkerInfo *foreman = NULL, *worker = NULL;

    foreman = CALLOC(ImportWorkerInfo);
    if (!foreman)
    goto error;

    /* start the foreman */
    import_init_worker_info(foreman, job);
    foreman->work_type = FOREMAN;
    if (! CREATE_THREAD(PR_USER_THREAD, (VFP)import_foreman, foreman,
                        PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                        PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE)) {
        PRErrorCode prerr = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY, "unable to spawn import foreman thread, "
                  SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                  prerr, slapd_pr_strerror(prerr), 0);
        FREE(foreman);
        goto error;
    }

    foreman->next = job->worker_list;
    job->worker_list = foreman;
    
    /* Start follower threads, if we are doing attribute indexing */
    current_index = job->index_list;
    if (job->flags & FLAG_INDEX_ATTRS) {
    while (current_index) {
        /* make a new thread info structure */
        worker = CALLOC(ImportWorkerInfo);
        if (! worker)
        goto error;

        /* fill it in */
        import_init_worker_info(worker, job);
        worker->index_info = current_index;
        worker->work_type = WORKER;

        /* Start the thread */
        if (! CREATE_THREAD(PR_USER_THREAD, (VFP)import_worker, worker,
                PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                PR_UNJOINABLE_THREAD,
                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
            PRErrorCode prerr = PR_GetError();
            LDAPDebug(LDAP_DEBUG_ANY, "unable to spawn import worker thread, "
                    SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                    prerr, slapd_pr_strerror(prerr), 0);
            FREE(worker);
            goto error;
        }

        /* link it onto the job's thread list */
        worker->next = job->worker_list;
        job->worker_list = worker;
        current_index = current_index->next;
    }
    }
    return 0;

error:
    import_log_notice(job, "Import thread creation failed.");
    import_log_notice(job, "Aborting all import threads...");
    import_abort_all(job, 1);
    import_log_notice(job, "Import threads aborted.");
    return -1;
}


/********** monitoring the worker threads **********/

static void import_clear_progress_history(ImportJob *job)
{
    int i = 0;

    for (i = 0; i < IMPORT_JOB_PROG_HISTORY_SIZE /*- 1*/; i++) {
    job->progress_history[i] = job->first_ID;
    job->progress_times[i] = job->start_time;
    }
    /* reset libdb cache stats */
    job->inst->inst_cache_hits = job->inst->inst_cache_misses = 0;
}

static double import_grok_db_stats(ldbm_instance *inst)
{
    DB_MPOOL_STAT *mpstat = NULL;
    DB_MPOOL_FSTAT **mpfstat = NULL;
    int return_value = -1;
    double cache_hit_ratio = 0.0;

    return_value = dblayer_memp_stat_instance(inst, &mpstat, &mpfstat);

    if (0 == return_value) {
    unsigned long current_cache_hits = mpstat->st_cache_hit;
    unsigned long current_cache_misses = mpstat->st_cache_miss;
    
    if (inst->inst_cache_hits) {
        unsigned long hit_delta, miss_delta;

        hit_delta = current_cache_hits - inst->inst_cache_hits;
        miss_delta = current_cache_misses - inst->inst_cache_misses;
        if (hit_delta != 0) {
        cache_hit_ratio = (double)hit_delta /
            (double)(hit_delta + miss_delta);
        }
    }
    inst->inst_cache_misses = current_cache_misses;
    inst->inst_cache_hits = current_cache_hits;

    if (mpstat)
        slapi_ch_free((void **)&mpstat);
    if (mpfstat) {
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR + DB_VERSION_PATCH <= 3204
            /* In DB 3.2.4 and earlier, we need to free each element */
        DB_MPOOL_FSTAT **tfsp;
        for (tfsp = mpfstat; *tfsp; tfsp++)
        slapi_ch_free((void **)tfsp);
#endif
        slapi_ch_free((void **)&mpfstat);
    }
    }
    return cache_hit_ratio;
}

static char* import_decode_worker_state(int state)
{
    switch (state) {
    case WAITING:
        return "W";
    case RUNNING:
        return "R";
    case FINISHED:
        return "F";
    case ABORTED:
        return "A";
    default:
        return "?";
    }
}

static void import_print_worker_status(ImportWorkerInfo *info)
{
    char *name = (info->work_type == PRODUCER ? "Producer" :
                  (info->work_type == FOREMAN ? "Foreman" :
                   info->index_info->name));

    import_log_status_add_line(info->job,
                               "%-25s %s%10ld %7.1f", name,
                               import_decode_worker_state(info->state),
                               info->last_ID_processed, info->rate);
}


#define IMPORT_CHUNK_TEST_HOLDOFF_TIME (5*60)    /* Seconds */

/* Got to be lower than this: */
#define IMPORT_CHUNK_TEST_CACHE_HIT_RATIO (0.99)
/* Less than half as fast as we were doing: */
#define IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_A (0.5)
/* A lot less fast than we were doing: */
#define IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_B (0.1)

static int import_throw_in_towel(ImportJob *job, time_t current_time,
                                 ID trailing_ID)
{
    static int number_of_times_here = 0;

    /* secret -c option allows specific chunk size to be set... */
    if (job->merge_chunk_size != 0) {
    if ((0 != job->lead_ID) &&
            (trailing_ID > job->first_ID) &&
        (trailing_ID - job->first_ID > job->merge_chunk_size)) {
        return 1;
    }
    return 0;
    }

    /* Check stats to decide whether we're getting bogged down and should
     * terminate this pass.
     */

    /* Check #1 : are we more than 10 minutes into the chunk ? */
    if (current_time - job->start_time > IMPORT_CHUNK_TEST_HOLDOFF_TIME) {
    /* Check #2 : Have we slowed down considerably recently ? */
    if ((job->recent_progress_rate / job->average_progress_rate) < 
        IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_A) {
        /* Check #3: Cache performing poorly---the puported reason
         * for the slowdown */
        if (job->cache_hit_ratio < IMPORT_CHUNK_TEST_CACHE_HIT_RATIO) {
        /* We have a winner ! */
        import_log_notice(job, "Decided to end this pass because "
                  "the progress rate has dropped below "
                  "the %.0f%% threshold.",
                  IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_A*100.0);
        return 1;
        }
    } else {
        if ((job->recent_progress_rate / job->average_progress_rate) <
        IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_B) {
        /* Alternative check: have we really, really slowed down, 
         * without the test for cache overflow? */
        /* This is designed to catch the case where the cache has
         * been misconfigured too large */
        if (number_of_times_here > 10) {
            /* Got to get here ten times at least */
            import_log_notice(job, "Decided to end this pass "
                      "because the progress rate "
                      "plummeted below %.0f%%",
                      IMPORT_CHUNK_TEST_SLOWDOWN_RATIO_B*100.0);
            return 1;
        }
        number_of_times_here++;
        }
    }
    }

    number_of_times_here = 0;
    return 0;
}

static void import_push_progress_history(ImportJob *job, ID current_id,
                     time_t current_time)
{
    int i = 0;

    for (i = 0; i < IMPORT_JOB_PROG_HISTORY_SIZE - 1; i++) {
    job->progress_history[i] = job->progress_history[i+1];
    job->progress_times[i] = job->progress_times[i+1];
    }
    job->progress_history[i] = current_id;
    job->progress_times[i] = current_time;
}

static void import_calc_rate(ImportWorkerInfo *info, int time_interval)
{
    size_t ids = info->last_ID_processed - info->previous_ID_counted;
    double rate = (double)ids / time_interval;
    
    if ( (info->previous_ID_counted != 0) && (info->last_ID_processed != 0) ) {
    info->rate = rate;
    } else {
    info->rate = 0;
    }
    info->previous_ID_counted = info->last_ID_processed;
}

/* find the rate (ids/time) of work from a worker thread between history
 * marks A and B.
 */
#define HISTORY(N)    (job->progress_history[N])
#define TIMES(N)    (job->progress_times[N])
#define PROGRESS(A, B)    ((HISTORY(B) > HISTORY(A)) ? \
                           ((double)(HISTORY(B) - HISTORY(A)) /    \
                            (double)(TIMES(B) - TIMES(A))) : \
                           (double)0)

static int import_monitor_threads(ImportJob *job, int *status)
{
    PRIntervalTime tenthsecond = PR_MillisecondsToInterval(100);
    ImportWorkerInfo *current_worker = NULL;
    ImportWorkerInfo *producer = NULL, *foreman = NULL;
    int finished = 0;
    int giveup = 0;
    int count = 1;              /* 1 to prevent premature status report */
    int producer_done = 0;
    const int display_interval = 200;
    time_t time_now = 0;
    time_t last_time = 0;
    time_t time_interval = 0;
    int rc = 0;

    for (current_worker = job->worker_list; current_worker != NULL; 
         current_worker = current_worker->next) {
        current_worker->command = RUN;
        if (current_worker->work_type == PRODUCER)
            producer = current_worker;
        if (current_worker->work_type == FOREMAN)
            foreman = current_worker;
    }


    if (job->flags & FLAG_USE_FILES)
        PR_ASSERT(producer != NULL);

    PR_ASSERT(foreman != NULL);

    if (!foreman) {
        goto error_abort;
    }

    time(&last_time);
    job->start_time = last_time;
    import_clear_progress_history(job);

    while (!finished) {
        ID trailing_ID = NOID;

        DS_Sleep(tenthsecond);
        finished = 1;

        /* First calculate the time interval since last reported */
        if (0 == (count % display_interval)) {
            time(&time_now);
            time_interval = time_now - last_time;
            last_time = time_now;
            /* Now calculate our rate of progress overall for this chunk */
            if (time_now != job->start_time) {
                /* log a cute chart of the worker progress */
                import_log_status_start(job);
                import_log_status_add_line(job,
                    "Index status for import of %s:", job->inst->inst_name);
                import_log_status_add_line(job,
                    "-------Index Task-------State---Entry----Rate-");

                import_push_progress_history(job, foreman->last_ID_processed,
                                             time_now);
                job->average_progress_rate = 
                    (double)(HISTORY(IMPORT_JOB_PROG_HISTORY_SIZE-1)+1 - foreman->first_ID) /
                    (double)(TIMES(IMPORT_JOB_PROG_HISTORY_SIZE-1) - job->start_time);
                job->recent_progress_rate =
                    PROGRESS(0, IMPORT_JOB_PROG_HISTORY_SIZE-1);
                job->cache_hit_ratio = import_grok_db_stats(job->inst);
            }
        }

        for (current_worker = job->worker_list; current_worker != NULL;
             current_worker = current_worker->next) {
            /* Calculate the ID at which the slowest worker is currently
             * processing */
            if ((trailing_ID > current_worker->last_ID_processed) &&
                (current_worker->work_type == WORKER)) {
                trailing_ID = current_worker->last_ID_processed;
            }
            if (0 == (count % display_interval) && time_interval) {
                import_calc_rate(current_worker, time_interval);
                import_print_worker_status(current_worker);
            }
            if (current_worker->state == QUIT) {
                rc = DRYRUN_QUIT; /* Set the RC; Don't abort now; 
                                     We have to stop other threads */
            } else if (current_worker->state != FINISHED) {
                finished = 0;
            }
            if (current_worker->state == ABORTED) {
                goto error_abort;
            }
        }

        if ((0 == (count % display_interval)) &&
            (job->start_time != time_now)) {
            char buffer[256], *p = buffer;

            import_log_status_done(job);
            p += sprintf(p, "Processed %lu entries ", (u_long)job->ready_ID);
            if (job->total_pass > 1)
                p += sprintf(p, "(pass %d) ", job->total_pass);

            p += sprintf(p, "-- average rate %.1f/sec, ",
                         job->average_progress_rate);
            p += sprintf(p, "recent rate %.1f/sec, ",
                         job->recent_progress_rate);
            p += sprintf(p, "hit ratio %.0f%%", job->cache_hit_ratio * 100.0);
            import_log_notice(job, "%s", buffer);
        }

        /* Then let's see if it's time to complete this import pass */
        if (!giveup) {
            giveup = import_throw_in_towel(job, time_now, trailing_ID);
            if (giveup) {
                /* If so, signal the lead thread to stop */
                import_log_notice(job, "Ending pass number %d ...",
                                  job->total_pass);
                foreman->command = STOP;
                while (foreman->state != FINISHED) {
                    DS_Sleep(tenthsecond);
                }
                import_log_notice(job, "Foreman is done; waiting for "
                                  "workers to finish...");
            }
        }

        /* if the producer is finished, and the foreman has caught up... */
        if (producer) {
            producer_done = (producer->state == FINISHED) ||
                            (producer->state == QUIT);
        } else {
            /* set in ldbm_back_wire_import */
            producer_done = (job->flags & FLAG_PRODUCER_DONE);
        }
        if (producer_done && (job->lead_ID == job->ready_ID)) {
            /* tell the foreman to stop if he's still working. */
            if (foreman->state != FINISHED)
                foreman->command = STOP;

            /* if all the workers are caught up too, we're done */
            if (trailing_ID == job->lead_ID)
                break;
        }

        /* if the foreman is done (end of pass) and the worker threads
         * have caught up...
         */
        if ((foreman->state == FINISHED) && (job->ready_ID == trailing_ID)) {
            break;
        }

        count++;
    }

    import_log_notice(job, "Workers finished; cleaning up...");

    /* Now tell all the workers to stop */
    for (current_worker = job->worker_list; current_worker != NULL;
         current_worker = current_worker->next) {
        if (current_worker->work_type != PRODUCER)
            current_worker->command = STOP;
    }

    /* Having done that, wait for them to say that they've stopped */
    for (current_worker = job->worker_list; current_worker != NULL; ) {
        if ((current_worker->state != FINISHED) &&
            (current_worker->state != ABORTED) &&
            (current_worker->state != QUIT) &&
            (current_worker->work_type != PRODUCER)) {
            DS_Sleep(tenthsecond);    /* Only sleep if we hit a thread that is still not done */
            continue;
        } else {
            current_worker = current_worker->next;
        }
    }
    import_log_notice(job, "Workers cleaned up.");

    /* If we're here and giveup is true, and the primary hadn't finished
     * processing the input files, we need to return IMPORT_INCOMPLETE_PASS */
    if (giveup && (job->input_filenames || (job->flags & FLAG_ONLINE) ||
                   (job->flags & FLAG_REINDEXING /* support multi-pass */))) {
        if (producer_done && (job->ready_ID == job->lead_ID)) {
            /* foreman caught up with the producer, and the producer is
             * done.
             */
            *status = IMPORT_COMPLETE_PASS;
        } else {
            *status = IMPORT_INCOMPLETE_PASS;
        }
    } else {
        *status = IMPORT_COMPLETE_PASS;
    }
    return rc;

error_abort:
    return ERR_IMPORT_ABORTED;
}


/********** running passes **********/

static int import_run_pass(ImportJob *job, int *status)
{
    int ret = 0;

    /* Start the threads running */
    ret = import_start_threads(job);
    if (ret != 0) {
        import_log_notice(job, "Starting threads failed: %d\n", ret);
        goto error;
    }

    /* Monitor the threads until we're done or fail */
    ret = import_monitor_threads(job, status);
    if ((ret == ERR_IMPORT_ABORTED) || (ret == DRYRUN_QUIT)) {
        goto error;
    } else if (ret != 0) {
        import_log_notice(job, "Thread monitoring aborted: %d\n", ret);
        goto error;
    }

error:
    return ret;
}

static void import_set_abort_flag_all(ImportJob *job, int wait_for_them)
{

    ImportWorkerInfo *worker;

    /* tell all the worker threads to abort */
    job->flags |= FLAG_ABORT;

    /* setting of the flag in the job will be detected in the worker, foreman
     * threads and if there are any threads which have a sleeptime  200 msecs
     * = import_sleep_time; after that time, they will examine the condition
     * (job->flags & FLAG_ABORT) which will unblock the thread to proceed to
     * abort. Hence, we will sleep here for atleast 3 sec to make sure clean
     * up occurs */
        /* allow all the aborts to be processed */
             DS_Sleep(PR_MillisecondsToInterval(3000)); 

    if (wait_for_them) {
        /* Having done that, wait for them to say that they've stopped */
        for (worker = job->worker_list; worker != NULL; ) {
            DS_Sleep(PR_MillisecondsToInterval(100));
            if ((worker->state != FINISHED) && (worker->state != ABORTED) &&
                (worker->state != QUIT)){
                continue;
            } else {
                worker = worker->next;
            }
        }
    }
}


/* tell all the threads to abort */
void import_abort_all(ImportJob *job, int wait_for_them)
{
    ImportWorkerInfo *worker;

    /* tell all the worker threads to abort */
    job->flags |= FLAG_ABORT;
    
    for (worker = job->worker_list; worker; worker = worker->next)
        worker->command = ABORT;

    if (wait_for_them) {
        /* Having done that, wait for them to say that they've stopped */
        for (worker = job->worker_list; worker != NULL; ) {
            DS_Sleep(PR_MillisecondsToInterval(100));
            if ((worker->state != FINISHED) && (worker->state != ABORTED) &&
                (worker->state != QUIT)) {
                continue;
            } else {
                worker = worker->next;
            }
        }
    }
}

/* Helper function to make up filenames */
int import_make_merge_filenames(char *directory, char *indexname, int pass,
                char **oldname, char **newname)
{
    /* Filenames look like this: attributename<LDBM_FILENAME_SUFFIX> 
       and need to be renamed to: attributename<LDBM_FILENAME_SUFFIX>.n
       where n is the pass number.
       */
    *oldname = slapi_ch_smprintf("%s/%s%s", directory, indexname, LDBM_FILENAME_SUFFIX);
    *newname = slapi_ch_smprintf("%s/%s.%d%s", directory, indexname, pass,
        LDBM_FILENAME_SUFFIX);
    if (!*oldname || !*newname) {
        slapi_ch_free_string(oldname);
        slapi_ch_free_string(newname);
        return -1;
    }
    return 0;
}

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
static int import_sweep_after_pass(ImportJob *job)
{
    backend *be = job->inst->inst_be;
    int ret = 0;

    import_log_notice(job, "Sweeping files for merging later...");

    ret = dblayer_instance_close(be);
    
    if (0 == ret) {
    /* Walk the list of index jobs */
    ImportWorkerInfo *current_worker = NULL;
    
    for (current_worker = job->worker_list; current_worker != NULL;
         current_worker = current_worker->next) {
        /* Foreach job, rename the file to <filename>.n, where n is the 
         * pass number */
        if ((current_worker->work_type != FOREMAN) && 
        (current_worker->work_type != PRODUCER) && 
        (strcasecmp(current_worker->index_info->name, LDBM_PARENTID_STR) != 0)) {
        char *newname = NULL;
        char *oldname = NULL;

        ret = import_make_merge_filenames(job->inst->inst_dir_name,
            current_worker->index_info->name, job->current_pass,
            &oldname, &newname);
        if (0 != ret) {
            break;
        }
        if (PR_Access(oldname, PR_ACCESS_EXISTS) == PR_SUCCESS) {
            ret = PR_Rename(oldname, newname);
            if (ret != PR_SUCCESS) {
                PRErrorCode prerr = PR_GetError();
                import_log_notice(job, "Failed to rename file \"%s\" to \"%s\", "
                    SLAPI_COMPONENT_NAME_NSPR " error %d (%s)",
                    oldname, newname, prerr, slapd_pr_strerror(prerr));
                slapi_ch_free( (void**)&newname);
                slapi_ch_free( (void**)&oldname);
                break;
            }
        }
        slapi_ch_free( (void**)&newname);
        slapi_ch_free( (void**)&oldname);
        }
    }

    ret = dblayer_instance_start(be, DBLAYER_IMPORT_MODE);
    }

    if (0 == ret) {
    import_log_notice(job, "Sweep done.");
    } else {
    if (ENOSPC == ret) {
        import_log_notice(job, "ERROR: NO DISK SPACE LEFT in sweep phase");
    } else {
        import_log_notice(job, "ERROR: Sweep phase error %d (%s)", ret,
                  dblayer_strerror(ret));
    }
    }
    
    return ret;
}

/* when the import is done, this function is called to bring stuff back up.
 * returns 0 on success; anything else is an error
 */
static int import_all_done(ImportJob *job, int ret)
{
    ldbm_instance *inst = job->inst;

    /* Writing this file indicates to future server startups that
     * the db is OK unless it's in the dry run mode. */
    if ((ret == 0) && !(job->flags & FLAG_DRYRUN)) {
        char inst_dir[MAXPATHLEN*2];
        char *inst_dirp = NULL;
        inst_dirp = dblayer_get_full_inst_dir(inst->inst_li, inst,
                                              inst_dir, MAXPATHLEN*2);
        ret = dbversion_write(inst->inst_li, inst_dirp, NULL, DBVERSION_ALL);
        if (inst_dirp != inst_dir)
            slapi_ch_free_string(&inst_dirp);
    }

    if ((job->task != NULL) && (0 == slapi_task_get_refcount(job->task))) {
        slapi_task_finish(job->task, ret);
    }

    if (job->flags & FLAG_ONLINE) {
        /* make sure the indexes are online as well */
        /* richm 20070919 - if index entries are added online, they
           are created and marked as INDEX_OFFLINE, in anticipation
           of someone doing a db2index.  In this case, the db2index
           code will correctly unset the INDEX_OFFLINE flag.
           However, if import is used to create the indexes, the
           INDEX_OFFLINE flag will not be cleared.  So, we do that
           here
        */
        IndexInfo *index = job->index_list;
        while (index != NULL) {
            index->ai->ai_indexmask &= ~INDEX_OFFLINE;
            index = index->next;
        }
        /* start up the instance */
        ret = dblayer_instance_start(job->inst->inst_be, DBLAYER_NORMAL_MODE);
        if (ret != 0)
            return ret;

        /* Reset USN slapi_counter with the last key of the entryUSN index */
        ldbm_set_last_usn(inst->inst_be);

        /* bring backend online again */
        slapi_mtn_be_enable(inst->inst_be);
    }

    return ret;
}


int import_main_offline(void *arg)
{
    ImportJob *job = (ImportJob *)arg;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    int ret = 0;
    time_t beginning = 0;
    time_t end = 0;
    int finished = 0;
    int status = 0;
    int verbose = 1;
    int aborted = 0;
    ImportWorkerInfo *producer = NULL;
    char *opstr = "Import";

    if (job->task)
        slapi_task_inc_refcount(job->task);

    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        if (job->flags & FLAG_DRYRUN) {
            opstr = "Upgrade Dn Dryrun";
        } else {
            opstr = "Upgrade Dn";
        }
    } else if (job->flags & FLAG_REINDEXING) {
        opstr = "Reindexing";
    }
    PR_ASSERT(inst != NULL);
    time(&beginning);

    /* Decide which indexes are needed */
    if (job->flags & FLAG_INDEX_ATTRS) {
        /* Here, we get an AVL tree which contains nodes for all attributes
         * in the schema.  Given this tree, we need to identify those nodes
         * which are marked for indexing. */
        avl_apply(job->inst->inst_attrs, (IFP)import_attr_callback,
                  (caddr_t)job, -1, AVL_INORDER);
        vlv_getindices((IFP)import_attr_callback, (void *)job, be);
    }

    /* Determine how much index buffering space to allocate to each index */
    import_set_index_buffer_size(job);

    /* initialize the entry FIFO */
    ret = import_fifo_init(job);
    if (ret) {
        if (! (job->flags & FLAG_USE_FILES)) {
            PR_Lock(job->wire_lock);
            PR_NotifyCondVar(job->wire_cv);
            PR_Unlock(job->wire_lock);
        }
        goto error;
    }

    if (job->flags & FLAG_USE_FILES) {
        /* importing from files: start up a producer thread to read the
         * files and queue them
         */
        producer = CALLOC(ImportWorkerInfo);
        if (! producer)
            goto error;
        
        /* start the producer */
        import_init_worker_info(producer, job);
        producer->work_type = PRODUCER;
        if (job->flags & FLAG_UPGRADEDNFORMAT)
        {
            if (! CREATE_THREAD(PR_USER_THREAD, (VFP)upgradedn_producer, 
                producer, PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                PRErrorCode prerr = PR_GetError();
                LDAPDebug(LDAP_DEBUG_ANY,
                          "unable to spawn upgrade dn producer thread, "
                          SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr), 0);
                goto error;
            }
        }
        else if (job->flags & FLAG_REINDEXING)
        {
            if (! CREATE_THREAD(PR_USER_THREAD, (VFP)index_producer, producer,
                PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                PR_UNJOINABLE_THREAD,
                SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                PRErrorCode prerr = PR_GetError();
                LDAPDebug(LDAP_DEBUG_ANY,
                          "unable to spawn index producer thread, "
                          SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr), 0);
                goto error;
            }
        }
        else
        {
            import_log_notice(job, "Beginning import job...");
            if (! CREATE_THREAD(PR_USER_THREAD, (VFP)import_producer, producer,
                            PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                            PR_UNJOINABLE_THREAD,
                            SLAPD_DEFAULT_THREAD_STACKSIZE)) {
                        PRErrorCode prerr = PR_GetError();
                LDAPDebug(LDAP_DEBUG_ANY,
                        "unable to spawn import producer thread, "
                        SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                        prerr, slapd_pr_strerror(prerr), 0);
                goto error;
            }
        }

        if (0 == job->job_index_buffer_suggestion)
                import_log_notice(job, "Index buffering is disabled.");
        else
                import_log_notice(job,
                                "Index buffering enabled with bucket size %lu", 
                                job->job_index_buffer_suggestion);

        job->worker_list = producer;
    } else {
        /* release the startup lock and let the entries start queueing up
         * in for import */
        PR_Lock(job->wire_lock);
        PR_NotifyCondVar(job->wire_cv);
        PR_Unlock(job->wire_lock);
    }

    /* Run as many passes as we need to complete the job or die honourably in
     * the attempt */
    while (! finished) {
        job->current_pass++;
        job->total_pass++;
        ret = import_run_pass(job, &status);
        /* The following could have happened: 
         *     (a) Some error happened such that we're hosed.
         *         This is indicated by a non-zero return code.
         *     (b) We finished the complete file without needing a second pass
         *         This is indicated by a zero return code and a status of 
         *         IMPORT_COMPLETE_PASS and current_pass == 1;
         *     (c) We completed a pass and need at least another one
         *         This is indicated by a zero return code and a status of
         *         IMPORT_INCOMPLETE_PASS
         *     (d) We just completed what turned out to be the last in a
         *         series of passes
         *         This is indicated by a zero return code and a status of
         *         IMPORT_COMPLETE_PASS and current_pass > 1
         */
        if (ret == ERR_IMPORT_ABORTED) {
            /* at least one of the threads has aborted -- shut down ALL
             * of the threads */
            import_log_notice(job, "Aborting all %s threads...", opstr);
            /* this abort sets the  abort flag on the threads and will block for 
             * the exit of all threads 
             */
            import_set_abort_flag_all(job, 1); 
            import_log_notice(job, "%s threads aborted.", opstr);
            aborted = 1;
            goto error;
        }
        if (ret == DRYRUN_QUIT) {
            goto error; /* Found the candidate; close the db files and quit */
        }

        if (0 != ret) {
            /* Some horrible fate has befallen the import */
            import_log_notice(job, "Fatal pass error %d", ret);
            goto error;
        }

        /* No error, but a number of possibilities */
        if ( IMPORT_COMPLETE_PASS == status ) {
            if (1 == job->current_pass) {
                /* We're done !!!! */ ;
            } else {
                /* Save the files, then merge */
                ret = import_sweep_after_pass(job);
                if (0 != ret) {
                    goto error;
                }
                ret = import_mega_merge(job);
                if (0 != ret) {
                    goto error;
                }
            }
            finished = 1;
        } else {
            if (IMPORT_INCOMPLETE_PASS == status) {
                /* Need to go round again */
                /* Time to save the files we've built for later */
                ret = import_sweep_after_pass(job);
                if (0 != ret) {
                    goto error;
                }
                if ( (inst->inst_li->li_maxpassbeforemerge != 0) &&
                        (job->current_pass > inst->inst_li->li_maxpassbeforemerge) )
                {
                        ret = import_mega_merge(job);
                        if (0 != ret) {
                                   goto error;
                        }
                        job->current_pass = 1;
                        ret = import_sweep_after_pass(job);
                        if (0 != ret) {
                                   goto error;
                        }
                }

                /* Fixup the first_ID value to reflect previous work */
                job->first_ID = job->ready_ID + 1;
                import_free_thread_data(job);
                job->worker_list = producer;
                import_log_notice(job, "Beginning pass number %d",
                                  job->total_pass+1);
            } else {
                /* Bizarro-slapd */
                goto error;
            }
        }
    }

    /* kill the producer now; we're done */
    if (producer) {
        import_log_notice(job, "Cleaning up producer thread...");
        producer->command = STOP;
        /* wait for the lead thread to stop */
        while (producer->state != FINISHED) {
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
    }

    /* Now do the numsubordinates attribute */
    import_log_notice(job, "Indexing complete.  Post-processing...");
    /* [610066] reindexed db cannot be used in the following backup/restore */
    if ( !(job->flags & FLAG_REINDEXING) &&
         (ret = update_subordinatecounts(be, job->mothers, job->encrypt, NULL))
         != 0 ) {
        import_log_notice(job, "Failed to update numsubordinates attributes");
        goto error;
    }

    if (!entryrdn_get_noancestorid()) {
        /* And the ancestorid index */
        /* Creating ancestorid from the scratch; delete the index file first. */
        struct attrinfo *ai = NULL;
        ainfo_get(be, "ancestorid", &ai);
        dblayer_erase_index_file(be, ai, 0);
 
        if ((ret = ldbm_ancestorid_create_index(be)) != 0) {
            import_log_notice(job, "Failed to create ancestorid index");
            goto error;
        }
    }

    import_log_notice(job, "Flushing caches...");
    if (0 != (ret = dblayer_flush(job->inst->inst_li)) ) {
        import_log_notice(job, "Failed to flush database");
        goto error;
    }

    /* New way to exit the routine: check the return code.
     * If it's non-zero, delete the database files. 
     * Otherwise don't, but always close the database layer properly.
     * Then return. This ensures that we can't make a half-good/half-bad
     * Database. */
        
error:
    /* If we fail, the database is now in a mess, so we delete it 
       except dry run mode */
    import_log_notice(job, "Closing files...");
    cache_clear(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
    if (entryrdn_get_switch()) {
        cache_clear(&job->inst->inst_dncache, CACHE_TYPE_DN);
    }
    if (aborted) {
        /* If aborted, it's safer to rebuild the caches. */
        cache_destroy_please(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            cache_destroy_please(&job->inst->inst_dncache, CACHE_TYPE_DN);
        }
        /* initialize the entry cache */
        if (! cache_init(&(inst->inst_cache), DEFAULT_CACHE_SIZE,
                         DEFAULT_CACHE_ENTRIES, CACHE_TYPE_ENTRY)) {
            LDAPDebug0Args(LDAP_DEBUG_ANY, "import_main_offline: "
                        "cache_init failed.  Server should be restarted.\n");
        }

        /* initialize the dn cache */
        if (! cache_init(&(inst->inst_dncache), DEFAULT_DNCACHE_SIZE,
                     DEFAULT_DNCACHE_MAXCOUNT, CACHE_TYPE_DN)) {
            LDAPDebug0Args(LDAP_DEBUG_ANY, "import_main_offline: "
                        "dn cache_init failed.  Server should be restarted.\n");
        }
    }
    if (0 != ret) {
        if (!(job->flags & FLAG_DRYRUN)) { /* If not dryrun */
            /* if running in the dry run mode, don't touch the db */
            dblayer_delete_instance_dir(be);
        }
        dblayer_instance_close(job->inst->inst_be);
    } else {
        if (0 != (ret = dblayer_instance_close(job->inst->inst_be)) ) {
            import_log_notice(job, "Failed to close database");
        }
    }
    if (!(job->flags & FLAG_ONLINE))
        dblayer_close(job->inst->inst_li, DBLAYER_IMPORT_MODE);
    
    time(&end);
    if (verbose && (0 == ret)) {
        int seconds_to_import = end - beginning;
        size_t entries_processed = job->lead_ID - (job->starting_ID - 1);
        double entries_per_second = 
                    seconds_to_import ?
                    (double)entries_processed / (double)seconds_to_import : 0;

        if (job->not_here_skipped) {
            if (job->skipped) {
                import_log_notice(job, 
                            "%s complete.  Processed %lu entries " 
                            "(%d bad entries were skipped, "
                            "%d entries were skipped because they don't "
                            "belong to this database) in %d seconds. "
                            "(%.2f entries/sec)", 
                            opstr, entries_processed,
                            job->skipped, job->not_here_skipped,
                            seconds_to_import, entries_per_second);
            } else {
                import_log_notice(job, 
                            "%s complete.  Processed %lu entries "
                            "(%d entries were skipped because they don't "
                            "belong to this database) "
                            "in %d seconds. (%.2f entries/sec)",
                            opstr, entries_processed, 
                            job->not_here_skipped, seconds_to_import, 
                            entries_per_second);
            }
        } else {
            if (job->skipped) {
                import_log_notice(job, 
                            "%s complete.  Processed %lu entries "
                            "(%d were skipped) in %d seconds. "
                            "(%.2f entries/sec)", 
                            opstr, entries_processed,
                            job->skipped, seconds_to_import,
                            entries_per_second);
            } else {
                import_log_notice(job, 
                            "%s complete.  Processed %lu entries "
                            "in %d seconds. (%.2f entries/sec)",
                            opstr, entries_processed, 
                            seconds_to_import, entries_per_second);
            }
        }
    }

    if (job->flags & FLAG_DRYRUN) {
        if (0 == ret) {
            import_log_notice(job, "%s complete.  %s is up-to-date.", 
                              opstr, job->inst->inst_name);
            ret = 1;
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            import_all_done(job, ret);
        } else if (DRYRUN_QUIT == ret) {
            import_log_notice(job, "%s complete.  %s needs upgradednformat.", 
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            import_all_done(job, ret);
            ret = 0;
        } else {
            ret = -1;
            if (job->task != NULL) {
                slapi_task_finish(job->task, ret);
            }
        }
    } else if (0 != ret) {
        import_log_notice(job, "%s failed.", opstr);
        if (job->task != NULL) {
            slapi_task_finish(job->task, ret);
        }
    } else {
        if (job->task) {
            slapi_task_dec_refcount(job->task);
        }
        import_all_done(job, ret);
    }

    /* This instance isn't busy anymore */
    instance_set_not_busy(job->inst);
    
    import_free_job(job);
    if (producer)
        FREE(producer);
    
    return(ret);
}

/*
 * to be called by online import using PR_CreateThread()
 * offline import directly calls import_main_offline()
 *
 */
void import_main(void *arg)
{
    import_main_offline(arg);
}

int ldbm_back_ldif2ldbm_deluxe(Slapi_PBlock *pb)
{
    backend *be = NULL;
    int noattrindexes = 0;
    ImportJob *job = NULL;
    char **name_array = NULL;
    int total_files, i;
    int up_flags = 0;
    PRThread *thread = NULL;

    job = CALLOC(ImportJob);
    if (job == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "not enough memory to do import job\n",
                  0, 0, 0);
        return -1;
    }

    slapi_pblock_get( pb, SLAPI_BACKEND, &be);
    PR_ASSERT(NULL != be);
    job->inst = (ldbm_instance *)be->be_instance_info;
    slapi_pblock_get( pb, SLAPI_LDIF2DB_NOATTRINDEXES, &noattrindexes );
    slapi_pblock_get( pb, SLAPI_LDIF2DB_FILE, &name_array );
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &up_flags); /* For upgrade dn and
                                                        dn2rdn */

    /* the removedupvals field is blatantly overloaded here to mean
     * the chunk size too.  (chunk size = number of entries that should
     * be imported before starting a new pass.  usually for debugging.)
     */
    slapi_pblock_get(pb, SLAPI_LDIF2DB_REMOVEDUPVALS, &job->merge_chunk_size);
    if (job->merge_chunk_size == 1)
        job->merge_chunk_size = 0;
    /* get list of specifically included and/or excluded subtrees from
     * the front-end */
    ldbm_back_fetch_incl_excl(pb, &job->include_subtrees,
                  &job->exclude_subtrees);
    /* get cn=tasks info, if any */
    slapi_pblock_get(pb, SLAPI_BACKEND_TASK, &job->task);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_ENCRYPT, &job->encrypt);
    /* get uniqueid info */
    slapi_pblock_get(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &job->uuid_gen_type);
    if (job->uuid_gen_type == SLAPI_UNIQUEID_GENERATE_NAME_BASED) {
        char *namespaceid;

        slapi_pblock_get(pb, SLAPI_LDIF2DB_NAMESPACEID, &namespaceid);
        job->uuid_namespace = slapi_ch_strdup(namespaceid);
    }

    job->flags = FLAG_USE_FILES;
    if (NULL == name_array) {    /* no ldif file is given -> reindexing or
                                                             upgradedn */
        if (up_flags & SLAPI_UPGRADEDNFORMAT) {
            job->flags |= FLAG_UPGRADEDNFORMAT;
            if (up_flags & SLAPI_DRYRUN) {
                job->flags |= FLAG_DRYRUN;
            }
        } else {
            job->flags |= FLAG_REINDEXING; /* call index_producer */
            if (up_flags & SLAPI_UPGRADEDB_DN2RDN) {
                if (entryrdn_get_switch()) {
                    job->flags |= FLAG_DN2RDN; /* migrate to the rdn format */
                } else {
                    LDAPDebug1Arg(LDAP_DEBUG_ANY,
                                  "DN to RDN option is specified, "
                                  "but %s is not enabled\n",
                                  CONFIG_ENTRYRDN_SWITCH);
                    import_free_job(job);
                    FREE(job);
                    return -1;
                }
            }
        }
    }
    if (!noattrindexes) {
        job->flags |= FLAG_INDEX_ATTRS;
    }
    for (i = 0; name_array && name_array[i] != NULL; i++) {
        charray_add(&job->input_filenames, slapi_ch_strdup(name_array[i]));
    }
    job->starting_ID = 1;
    job->first_ID = 1;
    job->mothers = CALLOC(import_subcount_stuff);

    /* how much space should we allocate to index buffering? */
    job->job_index_buffer_size = import_get_index_buffer_size();
    if (job->job_index_buffer_size == 0) {
        /* 10% of the allocated cache size + one meg */
        PR_Lock(job->inst->inst_li->li_config_mutex);
        job->job_index_buffer_size = 
               (job->inst->inst_li->li_import_cachesize/10) + (1024*1024);
        PR_Unlock(job->inst->inst_li->li_config_mutex);
    }
    import_subcount_stuff_init(job->mothers);

    if (job->task != NULL) {
        /* count files, use that to track "progress" in cn=tasks */
        total_files = 0;
        while (name_array && name_array[total_files] != NULL)
            total_files++;
        /* add 1 to account for post-import cleanup (which can take a
         * significant amount of time)
         */
        /* NGK - This should eventually be cleaned up to use the public
         * task API. */
        if (0 == total_files) {    /* reindexing */
            job->task->task_work = 2;
        } else {
            job->task->task_work = total_files + 1;
        }
        job->task->task_progress = 0;
        job->task->task_state = SLAPI_TASK_RUNNING;
        slapi_task_set_data(job->task, job);
        slapi_task_set_destructor_fn(job->task, import_task_destroy);
        slapi_task_set_cancel_fn(job->task, import_task_abort);
        job->flags |= FLAG_ONLINE;

        /* create thread for import_main, so we can return */
        thread = PR_CreateThread(PR_USER_THREAD, import_main, (void *)job,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD,
                                 SLAPD_DEFAULT_THREAD_STACKSIZE);
        if (thread == NULL) {
            PRErrorCode prerr = PR_GetError();
            LDAPDebug(LDAP_DEBUG_ANY, "unable to spawn import thread, "
                        SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                        prerr, slapd_pr_strerror(prerr), 0);
            import_free_job(job);
            FREE(job);
            return -2;
        }
        return 0;
    }

    /* old style -- do it all synchronously (THIS IS GOING AWAY SOON) */
    return import_main_offline((void *)job);
}
