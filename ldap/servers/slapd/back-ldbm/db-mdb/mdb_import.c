/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
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

#include "mdb_import.h"
#include "../vlv_srch.h"

#define ERR_IMPORT_ABORTED -23
#define NEED_DN_NORM -24
#define NEED_DN_NORM_SP -25
#define NEED_DN_NORM_BT -26

static char *sourcefile = "dbmdb_import.c";

static int dbmdb_import_update_entry_subcount(backend *be, ID parentid, size_t sub_count, int isencrypted, back_txn *txn);

/********** routines to manipulate the entry fifo **********/

/********** logging stuff **********/

#define LOG_BUFFER 512

/* this changes the 'nsTaskStatus' value, which is transient (anything logged
 * here wipes out any previous status)
 */
static void
dbmdb_import_log_status_start(ImportJob *job)
{
    if (!job->task_status)
        job->task_status = (char *)slapi_ch_malloc(10 * LOG_BUFFER);
    if (!job->task_status)
        return; /* out of memory? */

    job->task_status[0] = 0;
}

static void
dbmdb_import_log_status_add_line(ImportJob *job, char *format, ...)
{
    va_list ap;
    int len = 0;

    if (!job->task_status)
        return;
    len = strlen(job->task_status);
    if (len + 5 > (10 * LOG_BUFFER))
        return; /* no room */

    if (job->task_status[0])
        strcat(job->task_status, "\n");

    va_start(ap, format);
    PR_vsnprintf(job->task_status + len, (10 * LOG_BUFFER) - len, format, ap);
    va_end(ap);
}

static void
dbmdb_import_log_status_done(ImportJob *job)
{
    if (job->task) {
        slapi_task_log_status(job->task, "%s", job->task_status);
    }
}

static void
dbmdb_import_task_destroy(Slapi_Task *task)
{
    ImportJob *job = (ImportJob *)slapi_task_get_data(task);

    if (!job) {
        return;
    }

    while (task->task_state == SLAPI_TASK_RUNNING) {
        /* wait for the job to finish before freeing it */
        DS_Sleep(PR_SecondsToInterval(1));
    }
    if (job->task_status) {
        slapi_ch_free((void **)&job->task_status);
        job->task_status = NULL;
    }
    FREE(job);
    slapi_task_set_data(task, NULL);
}

static void
dbmdb_import_task_abort(Slapi_Task *task)
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

static int
dbmdb_import_update_entry_subcount(backend *be, ID parentid, size_t sub_count, int isencrypted, back_txn *txn)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int ret = 0;
    modify_context mc = {0};
    char value_buffer[22] = {0}; /* enough digits for 2^64 children */
    struct backentry *e = NULL;
    int isreplace = 0;
    char *numsub_str = numsubordinates;

    /* Get hold of the parent */
    e = id2entry(be, parentid, txn, &ret);
    if ((NULL == e) || (0 != ret)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_update_entry_subcount", "failed to read entry with ID %d ret=%d\n",
                parentid, ret);
        ldbm_nasty("dbmdb_import_update_entry_subcount", sourcefile, 5, ret);
        return (0 == ret) ? -1 : ret;
    }
    /* Lock it (not really required since we're single-threaded here, but
     * let's do it so we can reuse the modify routines) */
    cache_lock_entry(&inst->inst_cache, e);
    modify_init(&mc, e);
    mc.attr_encrypt = isencrypted;
    sprintf(value_buffer, "%lu", (long unsigned int)sub_count);
    /* If it is a tombstone entry, add tombstonesubordinates instead of
     * numsubordinates. */
    if (slapi_entry_flag_is_set(e->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE)) {
        numsub_str = LDBM_TOMBSTONE_NUMSUBORDINATES_STR;
    }
    /* attr numsubordinates/tombstonenumsubordinates could already exist in
     * the entry, let's check whether it's already there or not */
    isreplace = (attrlist_find(e->ep_entry->e_attrs, numsub_str) != NULL);
    {
        int op = isreplace ? LDAP_MOD_REPLACE : LDAP_MOD_ADD;
        Slapi_Mods *smods = slapi_mods_new();

        slapi_mods_add(smods, op | LDAP_MOD_BVALUES, numsub_str,
                       strlen(value_buffer), value_buffer);
        ret = modify_apply_mods(&mc, smods); /* smods passed in */
    }
    if (0 == ret || LDAP_TYPE_OR_VALUE_EXISTS == ret) {
        /* This will correctly index subordinatecount: */
        ret = modify_update_all(be, NULL, &mc, txn);
        if (0 == ret) {
            modify_switch_entries(&mc, be);
        }
    }
    /* entry is unlocked and returned to the cache in modify_term */
    modify_term(&mc, be);
    return ret;
}

/*
 * Function: dbmdb_update_subordinatecounts
 *
 * Returns: Nothing
 *
 */
static int
dbmdb_update_subordinatecounts(backend *be, ImportJob *job, dbi_txn_t *txn)
{
    int isencrypted = job->encrypt;
    int started_progress_logging = 0;
    int key_count = 0;
    int ret = 0;
    dbmdb_dbi_t*db = NULL;
    MDB_cursor *dbc = NULL;
    struct attrinfo *ai = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    dbmdb_cursor_t cursor = {0};
    struct ldbminfo *li = (struct ldbminfo*)be->be_database->plg_private;
	back_txn btxn = {0};

    /* Open the parentid index */
    ainfo_get(be, LDBM_PARENTID_STR, &ai);

    /* Open the parentid index file */
    if ((ret = dblayer_get_index_file(be, ai, (dbi_db_t**)&db, DBOPEN_CREATE)) != 0) {
        ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 67, ret);
        return (ret);
    }
    /* Get a cursor with r/w txn so we can walk through the parentid */
    ret = dbmdb_open_cursor(&cursor, MDB_CONFIG(li), db, 0);
    if (ret != 0) {
        ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 68, ret);
        dblayer_release_index_file(be, ai, db);
        return ret;
    }
    dbc = cursor.cur;
    txn = cursor.txn;
    btxn.back_txn_txn = txn;
    ret = MDB_CURSOR_GET(dbc, &key, &data, MDB_FIRST);

    /* Walk along the index */
    while (ret != MDB_NOTFOUND) {
        size_t sub_count = 0;
        ID parentid = 0;

        if (0 != ret) {
            key.mv_data=NULL;
            ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 62, ret);
            break;
        }
        /* check if we need to abort */
        if (job->flags & FLAG_ABORT) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_update_subordinatecounts",
                              "numsubordinate generation aborted.");
            break;
        }
        /*
         * Do an update count
         */
        key_count++;
        if (!(key_count % PROGRESS_INTERVAL)) {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_update_subordinatecounts",
                              "numsubordinate generation: processed %d entries...",
                              key_count);
            started_progress_logging = 1;
        }

        if (*(char *)key.mv_data == EQ_PREFIX) {
            char tmp[11];

            /* construct the parent's ID from the key */
            if (key.mv_size >= sizeof tmp) {
                ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 66, ret);
                break;
            }
            memcpy(tmp, key.mv_data, key.mv_size);
            tmp[key.mv_size] = 0;
            parentid = (ID)atol(tmp+1);
            PR_ASSERT(0 != parentid);
            /* Get number of records having the same key */
            ret = mdb_cursor_count(dbc, &sub_count);
            if (ret) {
                ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 63, ret);
                break;
            }
            PR_ASSERT(0 != sub_count);
            ret = dbmdb_import_update_entry_subcount(be, parentid, sub_count, isencrypted, &btxn);
            if (ret) {
                ldbm_nasty("dbmdb_update_subordinatecounts", sourcefile, 64, ret);
                break;
            }
        }
        ret = MDB_CURSOR_GET(dbc, &key, &data, MDB_NEXT_NODUP);
    }
    if (started_progress_logging) {
        /* Finish what we started... */
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_update_subordinatecounts",
                          "numsubordinate generation: processed %d entries.",
                          key_count);
        job->numsubordinates = key_count;
    }
    if (ret == MDB_NOTFOUND) {
        ret = 0;
    }

    dbmdb_close_cursor(&cursor, ret);
    dblayer_release_index_file(be, ai, db);

    return (ret);
}

/* Function used to gather a list of indexed attrs */
static int32_t
dbmdb_import_attr_callback(caddr_t n, caddr_t p)
{
    void *node = (void *)n;
    void *param  = (void *)p;
    ImportJob *job = (ImportJob *)param;
    struct attrinfo *a = (struct attrinfo *)node;

    if (job->flags & FLAG_DRYRUN) { /* dryrun; we don't need the workers */
        return 0;
    }
    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
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

    if (IS_INDEXED(a->ai_indexmask)) {
        /* Make an import_index_info structure, fill it in and insert into the
         * job's list */
        IndexInfo *info = CALLOC(IndexInfo);

        info->name = slapi_ch_strdup(a->ai_type);
        info->ai = a;
        info->next = job->index_list;
        job->index_list = info;
        job->number_indexers++;
    }
    return 0;
}

void
dbmdb_import_free_job(ImportJob *job)
{
    /* DBMDB_dbifree the lists etc */
    IndexInfo *index = job->index_list;

    while (index != NULL) {
        IndexInfo *asabird = index;
        index = index->next;
        slapi_ch_free((void **)&asabird->name);
        slapi_ch_free((void **)&asabird);
    }
    job->index_list = NULL;
    if (NULL != job->mothers) {
        import_subcount_stuff_term(job->mothers);
        slapi_ch_free((void **)&job->mothers);
    }

    dbmdb_back_free_incl_excl(job->include_subtrees, job->exclude_subtrees);

    if (NULL != job->uuid_namespace) {
        slapi_ch_free((void **)&job->uuid_namespace);
    }
    pthread_mutex_destroy(&job->wire_lock);
    pthread_cond_destroy(&job->wire_cv);
    charray_free(job->input_filenames);
    slapi_ch_free((void **)&job->task_status);
}

/* determine if we are the correct backend for this entry
 * (in a distributed suffix, some entries may be for other backends).
 * if the entry's dn actually matches one of the suffixes of the be, we
 * automatically take it as a belonging one, for such entries must be
 * present in EVERY backend independently of the distribution applied.
 */
int
dbmdb_import_entry_belongs_here(Slapi_Entry *e, backend *be)
{
    Slapi_Backend *retbe;
    Slapi_DN *sdn = slapi_entry_get_sdn(e);

    if (slapi_be_issuffix(be, sdn))
        return 1;

    retbe = slapi_mapping_tree_find_backend_for_sdn(sdn);
    return (retbe == be);
}


/********** starting threads and stuff **********/

static int
dbmdb_import_start_threads(ImportJob *job)
{
    ImportWorkerInfo *winfo = NULL;
    ImportCtx_t *ctx = job->writer_ctx;
    VFP fn = NULL;

    for (winfo=job->worker_list; winfo; winfo=winfo->next) {
        fn = NULL;
        switch (winfo->work_type) {
            case PRODUCER:
                fn = ctx->producer_fn;
                break;
            case WORKER:
                fn = dbmdb_import_worker;
                break;
            case WRITER:
                fn = dbmdb_import_writer;
                break;
            default:
                PR_ASSERT(0);
                break;
        }
        winfo->state = PAUSE;
        if (!PR_CreateThread(PR_USER_THREAD, fn, winfo,
                PR_PRIORITY_NORMAL, PR_GLOBAL_BOUND_THREAD,
                PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE)) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_start_threads",
                          "Unable to spawn import %s thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          winfo->name, prerr, slapd_pr_strerror(prerr));
            winfo->state = ABORT;
            return -1;
        }
    }
    return 0;
}

/********** monitoring the worker threads **********/

static void
dbmdb_import_clear_progress_history(ImportJob *job)
{
    int i = 0;

    for (i = 0; i < IMPORT_JOB_PROG_HISTORY_SIZE /*- 1*/; i++) {
        job->progress_history[i] = job->first_ID;
        job->progress_times[i] = job->start_time;
    }
    /* reset limdb cache stats */
    job->inst->inst_cache_hits = job->inst->inst_cache_misses = 0;
}

static char *
dbmdb_import_decode_worker_state(int state)
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

static void
dbmdb_import_print_worker_status(ImportWorkerInfo *info)
{
    dbmdb_import_log_status_add_line(info->job,
                               "%-25s %s%10ld %7.1f", info->name,
                               dbmdb_import_decode_worker_state(info->state),
                               info->last_ID_processed, info->rate);
}



static void
dbmdb_import_push_progress_history(ImportJob *job, ID current_id, time_t current_time)
{
    int i = 0;

    for (i = 0; i < IMPORT_JOB_PROG_HISTORY_SIZE - 1; i++) {
        job->progress_history[i] = job->progress_history[i + 1];
        job->progress_times[i] = job->progress_times[i + 1];
    }
    job->progress_history[i] = current_id;
    job->progress_times[i] = current_time;
}

static const char *dbmdb_import_role(ImportJob *job)
{
    ImportCtx_t *ctx = job->writer_ctx;
    switch (ctx->role) {
        case IM_IMPORT:
            return "import";
        case IM_BULKIMPORT:
            return "bulk import";
        case IM_INDEX:
            return "reindex";
        case IM_UPGRADE:
            return "upgrade";
        default:
            return "???";
    }
}

/* find the rate (ids/time) of work from a worker thread between history
 * marks A and B.
 */
#define HISTORY(N) (job->progress_history[N])
#define TIMES(N) (job->progress_times[N])
#define PROGRESS(A, B) ((HISTORY(B) > HISTORY(A)) ? ((double)(HISTORY(B) - HISTORY(A)) / \
                                                     (double)(TIMES(B) - TIMES(A)))      \
                                                  : (double)0)

static int
dbmdb_import_monitor_threads(ImportJob *job, int *status)
{
    PRIntervalTime tenthsecond = PR_MillisecondsToInterval(100);
    ImportCtx_t *ctx = job->writer_ctx;
    WorkerQueueData_t *slots = (WorkerQueueData_t*)(ctx->workerq.slots);
    ImportWorkerInfo *current_worker = NULL;
    ImportWorkerInfo *producer = NULL;
    ImportWorkerInfo *writer = NULL;
    u_long entry_processed = 0;
    int finished = 0;
    int count = 1; /* 1 to prevent premature status report */
    const int display_interval = 200;
    time_t time_now = 0;
    int i = 0;

    for (current_worker = job->worker_list; current_worker != NULL;
         current_worker = current_worker->next)
    {
        current_worker->command = RUN;
        if (current_worker->work_type == PRODUCER)
            producer = current_worker;
        if (current_worker->work_type == WRITER)
            writer = current_worker;
    }


    if ((job->flags & FLAG_USE_FILES) && producer == NULL) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_monitor_threads","No producer ==> Aborting %s.\n",
                          dbmdb_import_role(job));
        return ERR_IMPORT_ABORTED;
    }
    if (!writer) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_monitor_threads","No writer ==> Aborting %s.\n",
                          dbmdb_import_role(job));
        return ERR_IMPORT_ABORTED;
    }

    time_now = slapi_current_rel_time_t();
    job->start_time = time_now;
    dbmdb_import_clear_progress_history(job);

    while (!finished) {
        DS_Sleep(tenthsecond);
        finished = 1;

        /* Compute the number of entries processed by the workers */
        entry_processed = 0;
        for (i=0; i<ctx->workerq.max_slots; i++) {
            entry_processed += slots[i].count;
        }

        if (0 == (count % display_interval)) {
            time_now = slapi_current_rel_time_t();
            /* Now calculate our rate of progress overall for this chunk */
            if (time_now != job->start_time) {
                /* log a cute chart of the worker progress */
                dbmdb_import_log_status_start(job);
                dbmdb_import_log_status_add_line(job,
                                           "Index status for %s of %s:", dbmdb_import_role(job), job->inst->inst_name);
                dbmdb_import_log_status_add_line(job,
                                           "-------Index Task-------State---Entry----Rate-");

                dbmdb_import_push_progress_history(job, entry_processed, time_now);
                job->average_progress_rate =
                    (double)(HISTORY(IMPORT_JOB_PROG_HISTORY_SIZE - 1)) /
                    (double)(TIMES(IMPORT_JOB_PROG_HISTORY_SIZE - 1) - job->start_time);
                job->recent_progress_rate =
                    PROGRESS(0, IMPORT_JOB_PROG_HISTORY_SIZE - 1);
                job->cache_hit_ratio = 0;
            }
        }

        for (current_worker = job->worker_list; current_worker != NULL;
             current_worker = current_worker->next) {
            if ((current_worker->state & ABORTED) && !(job->flags & FLAG_ABORT)) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_monitor_threads", "thread %s aborted ==> Aborting %s.\n", current_worker->name, dbmdb_import_role(job));
                job->flags |= FLAG_ABORT;
            }
            if ((current_worker->state & FINISHED) == 0) {
                finished = 0;
            }
            if ((0 == (count % display_interval)) && (job->start_time != time_now)) {
                dbmdb_import_print_worker_status(current_worker);
            }
        }

        if ((0 == (count % display_interval)) &&
            (job->start_time != time_now)) {
            char buffer[256], *p = buffer;

            dbmdb_import_log_status_done(job);
            p += sprintf(p, "Processed %lu entries ", entry_processed);
            if (job->total_pass > 1)
                p += sprintf(p, "(pass %d) ", job->total_pass);

            p += sprintf(p, "-- average rate %.1f/sec, ",
                         job->average_progress_rate);
            sprintf(p, "recent rate %.1f/sec, ",
                         job->recent_progress_rate);
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads", "%s", buffer);
        }

        count++;
    }

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads",
                      "Workers finished; cleaning up...");

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads",
                      "Workers cleaned up.");

    *status = IMPORT_COMPLETE_PASS;
    return (job->flags & FLAG_ABORT) ? ERR_IMPORT_ABORTED : 0;
}


/********** startcfg passes **********/

static int
dbmdb_import_run_pass(ImportJob *job, int *status)
{
    int ret = 0;

    /* Start the threads startcfg */
    ret = dbmdb_import_start_threads(job);
    if (ret != 0) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_run_pass", "Starting threads failed: %d\n", ret);
        goto error;
    }

    /* Monitor the threads until we're done or fail */
    ret = dbmdb_import_monitor_threads(job, status);
    if ((ret == ERR_IMPORT_ABORTED) || (ret == NEED_DN_NORM) ||
        (ret == NEED_DN_NORM_SP) || (ret == NEED_DN_NORM_BT)) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_run_pass", "Thread monitoring returned: %d\n", ret);
        goto error;
    } else if (ret != 0) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_run_pass", "Thread monitoring aborted: %d\n", ret);
        goto error;
    }

error:
    return ret;
}

/* Helper function to make up filenames */

/* Ensure that the task status and exit code is properly set */
static void
dbmdb_task_finish(ImportJob *job, int ret)
{
    ldbm_instance *inst = job->inst;
    char *opname = "importing";
    char *task_dn = "";

    if (job->flags & (FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1)) {
        opname = "upgrading dn";
    } else if (job->flags & FLAG_REINDEXING) {
        opname = "indexing";
    }

    if (job->task != NULL) {
        /*
         * freeipa expect that finished tasks have both a status and
         * an exit code.
         * So lets ensure that both are set.
         */
        if (!job->task_status) {
            dbmdb_import_log_status_start(job);
        }
        dbmdb_import_log_status_add_line(job, "%s: Finished %s task",
                                         inst->inst_name, opname);
        dbmdb_import_log_status_done(job);
        slapi_task_finish(job->task, ret);
        task_dn = slapi_ch_smprintf(" task '%s'", job->task->task_dn);
    }
    slapi_log_err(SLAPI_LOG_INFO, "dbmdb_task_finish",
                  "%s: Finished %s%s. Exit code is %d\n",
                  inst->inst_name, opname, task_dn, ret);
    if (*task_dn) {
        slapi_ch_free_string(&task_dn);
    }
}

/* when the import is done, this function is called to bring stuff back up.
 * returns 0 on success; anything else is an error
 */
static int
dbmdb_import_all_done(ImportJob *job, int ret)
{
    ldbm_instance *inst = job->inst;
    int rc = 0;

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
        rc = dbmdb_instance_start(job->inst->inst_be, DBLAYER_NORMAL_MODE);
        if (rc == 0) {
            /* Reset USN slapi_counter with the last key of the entryUSN index */
            ldbm_set_last_usn(inst->inst_be);

            /* Bring backend online again:
             * In lmdb case, the import framework is also used for reindexing
             * while in bdb case reindexing uses its own code.
             * So dbmdb_import_all_done is called either after
             * dbmdb_ldif2db or after dbmdb_db2index while
             * bdb_import_all_done is only called after bdb_ldif2db.
             *
             * dbmdb_db2index uses instance_set_busy_and_readonly
             * while dbmdb_ldif2db uses slapi_mtn_be_disable
             * and these functions have to be reverted accordingly.
             */
            if (job->flags & FLAG_REINDEXING) {
                instance_set_not_busy(inst);
            } else {
                slapi_mtn_be_enable(inst->inst_be);
            }
            slapi_log_err(SLAPI_LOG_INFO, "dbmdb_import_all_done",
                          "Backend %s is now online.\n",
                          slapi_be_get_name(inst->inst_be));

        }
        ret |= rc;
    }

    /* Tell that task is finished once everything is back online
     * (to avoid race condition in CI)
     */
    if ((job->task != NULL) && (0 == slapi_task_get_refcount(job->task))) {
        dbmdb_task_finish(job, ret & ~WARN_SKIPPED_IMPORT_ENTRY);
    }

    return ret;
}

/* vlv_getindices callback that truncate vlv index (in reindex case) */
static int
truncate_index_dbi(caddr_t a, caddr_t c)
{
    struct attrinfo *ai = (struct attrinfo *)a;
    ImportCtx_t *ctx = (ImportCtx_t *)c;
    int rc = 0;

    if (is_reindexed_attr(ai->ai_type, ctx, ctx->indexVlvs)) {
        backend *be = ctx->job->inst->inst_be;
        dbmdb_dbi_t *dbi = NULL;
        rc = dbmdb_open_dbi_from_filename(&dbi, be, ai->ai_type, ai, MDB_TRUNCATE_DBI);
        if (!rc) {
            char *dbname = dbmdb_recno_cache_get_dbname(dbi->dbname);
            rc = dbmdb_open_dbi_from_filename(&dbi, be, dbname, ai, MDB_TRUNCATE_DBI);
            slapi_ch_free_string(&dbname);
        }
    }
    return rc;
}

int
dbmdb_public_dbmdb_import_main(void *arg)
{
    ImportJob *job = (ImportJob *)arg;
    ldbm_instance *inst = job->inst;
    ImportCtx_t *ctx = job->writer_ctx;
    backend *be = inst->inst_be;
    int ret = 0;
    time_t beginning = 0;
    time_t end = 0;
    int status = 0;
    int verbose = 1;
    int aborted = 0;
    ImportWorkerInfo *producer = NULL;
    char *opstr = "Import";

    if (job->task) {
        slapi_task_inc_refcount(job->task);
    }

    if (job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
        if (job->flags & FLAG_DRYRUN) {
            opstr = "Upgrade Dn Dryrun";
        } else if ((job->flags & (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) == (FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1)) {
            opstr = "Upgrade Dn (Full)";
        } else if (job->flags & FLAG_UPGRADEDNFORMAT_V1) {
            opstr = "Upgrade Dn (Spaces)";
        } else {
            opstr = "Upgrade Dn (RFC 4514)";
        }
    } else if (job->flags & FLAG_REINDEXING) {
        opstr = "Reindexing";
    }
    PR_ASSERT(inst != NULL);
    beginning = slapi_current_rel_time_t();

    /* Decide which indexes are needed */
    if (job->flags & FLAG_INDEX_ATTRS) {
        /* Here, we get an AVL tree which contains nodes for all attributes
         * in the schema.  Given this tree, we need to identify those nodes
         * which are marked for indexing. */
        avl_apply(job->inst->inst_attrs, dbmdb_import_attr_callback,
                  (caddr_t)job, -1, AVL_INORDER);
        vlv_getindices(dbmdb_import_attr_callback, (void *)job, be);
    }

    /* insure all dbi get open */
    dbmdb_open_all_files(NULL, job->inst->inst_be);
    dbmdb_build_import_index_list(ctx);
    /* Disable ndn cache because it greatly decrease the import performance */
    ndn_cache_inc_import_task();

    switch (ctx->role) {
        case IM_IMPORT:
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Beginning import job...");
            break;
        case IM_BULKIMPORT:
            /* release the startup lock and let the entries start queueing up
             * in for import */
            pthread_mutex_lock(&job->wire_lock);
            pthread_cond_signal(&job->wire_cv);
            pthread_mutex_unlock(&job->wire_lock);
            break;
        case IM_INDEX:
            vlv_getindices(truncate_index_dbi, ctx, job->inst->inst_be);
        default:
            break;
    }

    /* Run a single pass as we need to complete the job or die honourably in
     * the attempt */
    job->current_pass++;
    job->total_pass++;
    ret = dbmdb_import_run_pass(job, &status);
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
     * Anyway in all cases all threads with ImportWorkerInfo struct
     *  are finished (mdb code only).
     */
    if (ret == ERR_IMPORT_ABORTED) {
        /* at least one of the threads has aborted -- shut down ALL
         * of the threads */
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "Aborting all %s threads...", opstr);
        /* this abort sets the  abort flag on the threads and will block for
         * the exit of all threads
         */
        job->flags |= FLAG_ABORT;
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "%s threads aborted.", opstr);
        aborted = 1;
        goto error;
    }
    if ((ret == NEED_DN_NORM) || (ret == NEED_DN_NORM_SP) ||
        (ret == NEED_DN_NORM_BT)) {
        goto error;
    } else if (0 != ret) {
        /* Some horrible fate has befallen the import */
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "Fatal pass error %d", ret);
        goto error;
    }


    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Indexing complete.  Post-processing...");

    if (ctx->numsubordinates) {
        /* Now do the numsubordinates attribute */
        /* [610066] reindexed db cannot be used in the following backup/restore */
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                          "Generating numsubordinates (this may take several minutes to complete)...");
        ret = dbmdb_update_subordinatecounts(be, job, NULL);
        if (ret != 0) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                              "Failed to update numsubordinates attributes");
            goto error;
        }
        import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                          "Generating numSubordinates complete.");
    }

    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Flushing caches...");

/* New way to exit the routine: check the return code.
     * If it's non-zero, delete the database files.
     * Otherwise don't, but always close the database layer properly.
     * Then return. This ensures that we can't make a half-good/half-bad
     * Database. */

error:
    /* If we fail, the database is now in a mess, so we delete it
       except dry run mode */
    import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "Closing files...");
    cache_clear(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
    cache_clear(&job->inst->inst_dncache, CACHE_TYPE_DN);
    if (aborted) {
        /* If aborted, it's safer to rebuild the caches. */
        cache_destroy_please(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
        cache_destroy_please(&job->inst->inst_dncache, CACHE_TYPE_DN);
        /* initialize the entry cache */
        if (!cache_init(&(inst->inst_cache), inst, inst->inst_cache.c_stats.maxsize,
                        DEFAULT_CACHE_ENTRIES, CACHE_TYPE_ENTRY)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "cache_init failed.  Server should be restarted.\n");
        }

        /* initialize the dn cache */
        if (!cache_init(&(inst->inst_dncache), inst, inst->inst_dncache.c_stats.maxsize,
                        DEFAULT_DNCACHE_MAXCOUNT, CACHE_TYPE_DN)) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main",
                          "dn cache_init failed.  Server should be restarted.\n");
        }
    }
    if (0 != ret) {
        dblayer_instance_close(job->inst->inst_be);
        if (!(job->flags & (FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1))) {
            /* If not dryrun NOR upgradedn space */
            /* if startcfg in the dry run mode, don't touch the db */
            dbmdb_delete_instance_dir(be);
        }
    } else {
        if (0 != (ret = dblayer_instance_close(job->inst->inst_be))) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_public_dbmdb_import_main", "Failed to close database");
        }
    }
    end = slapi_current_rel_time_t();
    if (verbose && (0 == ret)) {
        int seconds_to_import = end - beginning;
        size_t entries_processed = 0;
        int i;
        for (i=0; i<ctx->workerq.max_slots; i++) {
            entries_processed += ctx->workerq.slots[i].count;
        }

        double entries_per_second =
            seconds_to_import ? (double)entries_processed / (double)seconds_to_import : 0;

        if (job->not_here_skipped) {
            if (job->skipped) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "(%d bad entries were skipped, "
                                  "%d entries were skipped because they don't "
                                  "belong to this database) in %d seconds. "
                                  "(%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  job->skipped, job->not_here_skipped,
                                  seconds_to_import, entries_per_second);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "(%d entries were skipped because they don't "
                                  "belong to this database) "
                                  "in %d seconds. (%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  job->not_here_skipped, seconds_to_import,
                                  entries_per_second);
            }
        } else {
            if (job->skipped) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "(%d were skipped) in %d seconds. "
                                  "(%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  job->skipped, seconds_to_import,
                                  entries_per_second);
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main",
                                  "%s complete.  Processed %lu entries "
                                  "in %d seconds. (%.2f entries/sec)",
                                  opstr, (long unsigned int)entries_processed,
                                  seconds_to_import, entries_per_second);
            }
        }
    }

    if (job->flags & (FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1)) {
        if (0 == ret) {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_public_dbmdb_import_main", "%s complete.  %s is up-to-date.",
                              opstr, job->inst->inst_name);
            ret = 0;
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
        } else if (NEED_DN_NORM_BT == ret) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_public_dbmdb_import_main",
                              "%s complete. %s needs upgradednformat all.",
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
            ret |= WARN_UPGRADE_DN_FORMAT_ALL;
        } else if (NEED_DN_NORM == ret) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_public_dbmdb_import_main",
                              "%s complete. %s needs upgradednformat.",
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
            ret |= WARN_UPGRADE_DN_FORMAT;
        } else if (NEED_DN_NORM_SP == ret) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_public_dbmdb_import_main",
                              "%s complete. %s needs upgradednformat spaces.",
                              opstr, job->inst->inst_name);
            if (job->task) {
                slapi_task_dec_refcount(job->task);
            }
            dbmdb_import_all_done(job, ret);
            ret |= WARN_UPGRADE_DN_FORMAT_SPACE;
        } else {
            ret = -1;
            dbmdb_task_finish(job, ret);
        }
    } else if (0 != ret) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_public_dbmdb_import_main", "%s failed.", opstr);
        dbmdb_task_finish(job, ret);
    } else {
        if (job->task) {
            /* set task warning if there are no errors */
            if (job->skipped) {
                slapi_task_set_warning(job->task, WARN_SKIPPED_IMPORT_ENTRY);
            }
            slapi_task_dec_refcount(job->task);
        }
        if (job->skipped) {
            ret |= WARN_SKIPPED_IMPORT_ENTRY;
        }
        if (ctx->dupdn) {
            ret |= ERR_DUPLICATE_DN;
        }
        dbmdb_import_all_done(job, ret);
    }

    /* Re-enable the ndn cache */
    ndn_cache_dec_import_task();
    dbmdb_clear_dirty_flags(be);


    /* This instance isn't busy anymore */
    instance_set_not_busy(job->inst);

    dbmdb_free_import_ctx(job);
    dbmdb_import_free_job(job);
    if (!job->task) {
        FREE(job);
    }
    if (producer)
        FREE(producer);

    return (ret);
}

/*
 * to be called by online import using PR_CreateThread()
 * offline import directly calls import_main_offline()
 *
 */
void
dbmdb_import_main(void *arg)
{
    /* For online import tasks increment/decrement the global thread count */
    g_incr_active_threadcnt();
    dbmdb_public_dbmdb_import_main(arg);
    g_decr_active_threadcnt();
}

static char *
get_vlv_dbname(const char *attrname)
{
    /* Returns vlv index database name as stored in attribute names
     * freed by the caller .
     */
    char *dbname = slapi_ch_malloc(4+strlen(attrname)+1);
    char *p = dbname;
    *p++ = 'v';
    *p++ = 'l';
    *p++ = 'v';
    *p++ = '#';
    for (; *attrname; attrname++) {
        if (isalnum(*attrname)) {
            *p = TOLOWER(*attrname);
            p++;
        }
    }
    *p = '\0';
    return dbname;
}

void
process_db2index_attrs(Slapi_PBlock *pb, ImportCtx_t *ctx)
{
    /* Work out which indexes we should build */
    /* explanation: for archaic reasons, the list of indexes is passed to
     * ldif2index as a string list, where each string either starts with a
     * 't' (normal index) or a 'T' (vlv index).
     * example: "tcn" (normal index cn)
     */
    /* NOTE (LK): This part in determining the attrs to reindex belongs to the layer above
     * the selection of attributes is independent of the backend implementation.
     * but it requires a method to pass the selection to this lower level indexing function
     * either by extension of the pblock or the argument list
     * TBD
     */
    char **attrs = NULL;
    char *attrname = NULL;
    char *pt = NULL;
    int i;

    slapi_pblock_get(pb, SLAPI_DB2INDEX_ATTRS, &attrs);
    /* Should perhaps add some tests to check that indexes are valid */
    for (i = 0; attrs && attrs[i]; i++) {
        switch (attrs[i][0]) {
        case 't': /* attribute type to index */
            attrname = slapi_ch_strdup(attrs[i] + 1);
            /* Strip index type */
            pt = strchr(attrname, ':');
            if (pt != NULL) {
                *pt = '\0';
            }
            slapi_ch_array_add(&ctx->indexAttrs, attrname);
            break;
        case 'T': /* VLV Search to index */
            slapi_ch_array_add(&ctx->indexVlvs, get_vlv_dbname(attrs[i] + 1));
            break;
        }
    }
}

int
dbmdb_run_ldif2db(Slapi_PBlock *pb)
{
    backend *be = NULL;
    int noattrindexes = 0;
    ImportJob *job = NULL;
    char **name_array = NULL;
    int total_files, i;
    int up_flags = 0;
    PRThread *thread = NULL;
    int ret = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_back_ldif2db", "Backend is not set\n");
        return -1;
    }
    job = CALLOC(ImportJob);
    job->inst = (ldbm_instance *)be->be_instance_info;
    slapi_pblock_get(pb, SLAPI_LDIF2DB_NOATTRINDEXES, &noattrindexes);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_FILE, &name_array);
    slapi_pblock_get(pb, SLAPI_SEQ_TYPE, &up_flags); /* For upgrade dn and
                                                        dn2rdn */

    /* get list of specifically included and/or excluded subtrees from
     * the front-end */
    dbmdb_back_fetch_incl_excl(pb, &job->include_subtrees,
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
    if (NULL == name_array) { /* no ldif file is given -> reindexing or
                                                             upgradedn */
        if (up_flags & (SLAPI_UPGRADEDNFORMAT | SLAPI_UPGRADEDNFORMAT_V1)) {
            if (up_flags & SLAPI_UPGRADEDNFORMAT) {
                job->flags |= FLAG_UPGRADEDNFORMAT;
            }
            if (up_flags & SLAPI_UPGRADEDNFORMAT_V1) {
                job->flags |= FLAG_UPGRADEDNFORMAT_V1;
            }
            if (up_flags & SLAPI_DRYRUN) {
                job->flags |= FLAG_DRYRUN;
            }
            dbmdb_import_init_writer(job, IM_UPGRADE);
        } else {
            job->flags |= FLAG_REINDEXING; /* call dbmdb_index_producer */
            dbmdb_import_init_writer(job, IM_INDEX);
            process_db2index_attrs(pb, job->writer_ctx);
        }
    } else {
        dbmdb_import_init_writer(job, IM_IMPORT);
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
    job->job_index_buffer_size = dbmdb_import_get_index_buffer_size();
    if (job->job_index_buffer_size == 0) {
        /* 10% of the allocated cache size + one meg */
        PR_Lock(job->inst->inst_li->li_config_mutex);
        job->job_index_buffer_size =
            (job->inst->inst_li->li_import_cachesize / 10) + (1024 * 1024);
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
        if (0 == total_files) { /* reindexing */
            job->task->task_work = 2;
        } else {
            job->task->task_work = total_files + 1;
        }
        job->task->task_progress = 0;
        job->task->task_state = SLAPI_TASK_RUNNING;
        slapi_task_set_data(job->task, job);
        slapi_task_set_destructor_fn(job->task, dbmdb_import_task_destroy);
        slapi_task_set_cancel_fn(job->task, dbmdb_import_task_abort);
        job->flags |= FLAG_ONLINE;

        if (job->flags & FLAG_REINDEXING) {
            /* Reindexing task : so we are called by task_index_thread
             *  that runs in a dedicated thread.
             * ==> should process the "import" synchroneously.
             */
            return dbmdb_public_dbmdb_import_main((void *)job);
        }

        /* create thread for dbmdb_import_main, so we can return */
        thread = PR_CreateThread(PR_USER_THREAD, dbmdb_import_main, (void *)job,
                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD,
                                 SLAPD_DEFAULT_THREAD_STACKSIZE);
        if (thread == NULL) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_back_ldif2db",
                          "Unable to spawn import thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
            dbmdb_import_free_job(job);
            FREE(job);
            return -2;
        }
        return 0;
    }

    /* old style -- do it all synchronously (THIS MAY GO AWAY SOON) */
    ret = dbmdb_public_dbmdb_import_main((void *)job);

    return ret;
}

int
dbmdb_back_ldif2db(Slapi_PBlock *pb)
{
    /* no error just warning, reset ret */
    return dbmdb_run_ldif2db(pb) & ~WARN_SKIPPED_IMPORT_ENTRY;
}


/****************************************************************************/
/******** Bulk Import (i.e replication total update) specific code **********/
/****************************************************************************/

/*
 * import entries to a backend, over the wire -- entries will arrive
 * asynchronously, so this method has no "producer" thread.  instead, the
 * front-end drops new entries in as they arrive.
 *
 * this is sometimes called "fast replica initialization".
 *
 * some of this code is duplicated from ldif2ldbm, but i don't think we
 * can avoid it.
 */
static int
dbmdb_bulk_import_start(Slapi_PBlock *pb)
{
    struct ldbminfo *li = NULL;
    ImportJob *job = NULL;
    backend *be = NULL;
    PRThread *thread = NULL;
    int ret = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_bulk_import_start", "Backend is not set\n");
        return -1;
    }
    job = CALLOC(ImportJob);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_ENCRYPT, &job->encrypt);
    li = (struct ldbminfo *)(be->be_database->plg_private);
    job->inst = (ldbm_instance *)be->be_instance_info;

    /* check if an import/restore is already ongoing... */
    PR_Lock(job->inst->inst_config_mutex);
    if (job->inst->inst_flags & INST_FLAG_BUSY) {
        PR_Unlock(job->inst->inst_config_mutex);
        slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_bulk_import_start",
                      "'%s' is already in the middle of another task and cannot be disturbed.\n",
                      job->inst->inst_name);
        FREE(job);
        return SLAPI_BI_ERR_BUSY;
    }
    job->inst->inst_flags |= INST_FLAG_BUSY;
    PR_Unlock(job->inst->inst_config_mutex);

    /* take backend offline */
    slapi_mtn_be_disable(be);

    /* get uniqueid info */
    slapi_pblock_get(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &job->uuid_gen_type);
    if (job->uuid_gen_type == SLAPI_UNIQUEID_GENERATE_NAME_BASED) {
        char *namespaceid;

        slapi_pblock_get(pb, SLAPI_LDIF2DB_NAMESPACEID, &namespaceid);
        job->uuid_namespace = slapi_ch_strdup(namespaceid);
    }

    job->flags = 0; /* don't use files */
    job->flags |= FLAG_INDEX_ATTRS;
    job->flags |= FLAG_ONLINE;
    job->starting_ID = 1;
    job->first_ID = 1;

    job->mothers = CALLOC(import_subcount_stuff);
    /* how much space should we allocate to index buffering? */
    job->job_index_buffer_size = dbmdb_import_get_index_buffer_size();
    if (job->job_index_buffer_size == 0) {
        /* 10% of the allocated cache size + one meg */
        job->job_index_buffer_size = (job->inst->inst_li->li_dbcachesize / 10) +
                                     (1024 * 1024);
    }
    import_subcount_stuff_init(job->mothers);
    dbmdb_import_init_writer(job, IM_BULKIMPORT);

    pthread_mutex_init(&job->wire_lock, NULL);
    pthread_cond_init(&job->wire_cv, NULL);

    /* COPIED from ldif2ldbm.c : */

    /* shutdown this instance of the db */
    cache_clear(&job->inst->inst_cache, CACHE_TYPE_ENTRY);
    cache_clear(&job->inst->inst_dncache, CACHE_TYPE_DN);
    dblayer_instance_close(be);

    /* Delete old database files */
    dbmdb_delete_instance_dir(be);
    /* it's okay to fail -- it might already be gone */

    /* dbmdb_instance_start will init the id2entry index and the vlv search list. */
    /* it also (finally) fills in inst_dir_name */
    ret = dbmdb_instance_start(be, DBLAYER_IMPORT_MODE);
    if (ret != 0)
        goto fail;

    /* END OF COPIED SECTION */

    pthread_mutex_lock(&job->wire_lock);

    /* create thread for dbmdb_import_main, so we can return */
    thread = PR_CreateThread(PR_USER_THREAD, dbmdb_import_main, (void *)job,
                             PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_JOINABLE_THREAD,
                             SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_bulk_import_start",
                      "Unable to spawn import thread, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        pthread_mutex_unlock(&job->wire_lock);
        ret = -2;
        goto fail;
    }

    job->main_thread = thread;

    Connection *pb_conn;
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    slapi_set_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle, job);

    /* wait for the dbmdb_import_main to signal that it's ready for entries */
    /* (don't want to send the success code back to the LDAP client until
     * we're ready for the adds to start rolling in)
     */
    pthread_cond_wait(&job->wire_cv, &job->wire_lock);
    pthread_mutex_unlock(&job->wire_lock);
    ((ImportCtx_t*)(job->writer_ctx))->producer.state = RUNNING;

    return 0;

fail:
    PR_Lock(job->inst->inst_config_mutex);
    job->inst->inst_flags &= ~INST_FLAG_BUSY;
    PR_Unlock(job->inst->inst_config_mutex);
    dbmdb_import_free_job(job);
    FREE(job);
    return ret;
}


/* returns 0 on success, or < 0 on error
 *
 * on error, the import process is aborted -- so if this returns an error,
 * don't try to queue any more entries or you'll be sorry.  The caller
 * is also responsible for free'ing the passed in entry on error.  The
 * entry will be consumed on success.
 */
static int
dbmdb_bulk_import_queue(ImportJob *job, Slapi_Entry *entry)
{
    struct backentry *ep = NULL;
    ImportCtx_t *ctx = job->writer_ctx;
    BulkQueueData_t bqelmt = {0};
    ID id = 0;

    if (!entry) {
        return -1;
    }

    /* The import is aborted, just ignore that entry */
    if (job->flags & FLAG_ABORT) {
        return -1;
    }

    pthread_mutex_lock(&job->wire_lock);
    /* Let's do this inside the lock !*/
    id = job->lead_ID + 1;
    /* generate uniqueid if necessary */
    if (dbmdb_import_generate_uniqueid(job, entry) != UID_SUCCESS) {
        import_abort_all(job, 1);
        pthread_mutex_unlock(&job->wire_lock);
        return -1;
    }

    /* make into backentry */
    ep = dbmdb_import_make_backentry(entry, id);
    if ((ep == NULL) || (ep->ep_entry == NULL)) {
        import_abort_all(job, 1);
        backentry_free(&ep); /* release the backend wrapper, here */
        pthread_mutex_unlock(&job->wire_lock);
        return -1;
    }

    bqelmt.id = id;
    bqelmt.ep = ep;
    dbmdb_import_q_push(&ctx->bulkq, &bqelmt);

    job->lead_ID = id;

    pthread_mutex_unlock(&job->wire_lock);
    return 0;
}

/* plugin entry function for replica init
 *
 * For the SLAPI_BI_STATE_ADD state:
 * On success (rc=0), the entry in pb->pb_import_entry will be
 * consumed.  For any other return value, the caller is
 * responsible for freeing the entry in the pb.
 */
int
dbmdb_ldbm_back_wire_import(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    backend *be = NULL;
    ImportJob *job = NULL;
    PRThread *thread;
    int state;
    int rc;
    Connection *pb_conn;

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldbm_back_wire_import",
                      "Backend is not set\n");
        return -1;
    }
    li = (struct ldbminfo *)(be->be_database->plg_private);
    slapi_pblock_get(pb, SLAPI_BULK_IMPORT_STATE, &state);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_ENCRYPT, &li->li_online_import_encrypt);
    if (state == SLAPI_BI_STATE_START) {
        /* starting a new import */
        rc = dbmdb_bulk_import_start(pb);
        if (!rc) {
            /* job must be available since dbmdb_bulk_import_start was successful */
            job = (ImportJob *)slapi_get_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle);
            /* Get entryusn, if needed. */
            _get_import_entryusn(job, &(job->usn_value));
        }
        slapi_log_err(SLAPI_LOG_REPL, "dbmdb_ldbm_back_wire_import", "dbmdb_bulk_import_start returned %d\n", rc);
        return rc;
    }

    PR_ASSERT(pb_conn != NULL);
    if (pb_conn != NULL) {
        job = (ImportJob *)slapi_get_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle);
    }

    if ((job == NULL) || (pb_conn == NULL)) {
        /* import might be aborting */
        return -1;
    }

    if (state == SLAPI_BI_STATE_ADD) {
        Slapi_Entry *pb_import_entry = NULL;
        char buf[BUFSIZ] = "";
        slapi_pblock_get(pb, SLAPI_BULK_IMPORT_ENTRY, &pb_import_entry);
        /* continuing previous import */
        if (!dbmdb_import_entry_belongs_here(pb_import_entry, job->inst->inst_be)) {
            /* silently skip */
            /* We need to consume pb->pb_import_entry on success, so we free it here. */
            slapi_log_err(SLAPI_LOG_REPL, "dbmdb_ldbm_back_wire_import", "skipped entry %s\n",
                          slapi_sdn_get_dn(slapi_entry_get_sdn(pb_import_entry)));
            slapi_entry_free(pb_import_entry);
            return 0;
        }

        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            /*
             * Queued entries are processed (then freed) by another thread
             * so the sdn should be capured before queuing the entry to avoid
             * race condition.
             */
            escape_string(slapi_sdn_get_dn(slapi_entry_get_sdn(pb_import_entry)), buf);
        }
        rc = dbmdb_bulk_import_queue(job, pb_import_entry);
        slapi_log_err(SLAPI_LOG_REPL, "dbmdb_ldbm_back_wire_import",
                      "dbmdb_bulk_import_queue returned %d with entry %s\n", rc, buf);
        return rc;
    }

    thread = job->main_thread;

    if (state == SLAPI_BI_STATE_DONE) {
        slapi_value_free(&(job->usn_value));
        /* finished with an import */
        ((ImportCtx_t*)(job->writer_ctx))->bulkq_state = FINISHED;
        /* "job" struct may vanish at any moment after we set the FINISHED
         * flag, so keep a copy of the thread id in 'thread' for safekeeping.
         */
        /* wait for dbmdb_import_main to finish... */
        PR_JoinThread(thread);
        slapi_set_object_extension(li->li_bulk_import_object, pb_conn, li->li_bulk_import_handle, NULL);
        slapi_log_err(SLAPI_LOG_REPL, "dbmdb_ldbm_back_wire_import", "Bulk import is finished.\n");
        return 0;
    }

    /* ??? unknown state */
    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_ldbm_back_wire_import",
                  "ERROR: unknown state %d\n", state);
    return -1;
}

/*
 * Debug function (to use when debbuging with gdb)
 */

void
dbmdb_dump_worker(ImportWorkerInfo *w)
{
    static char *cmdflags[] = { "UNDEF", "RUN", "PAUSE", "ABORT", "STOP" };
    static char *cmdstate[] = { "WAITING", "RUNNING", "FINISHED", "ABORTED", "QUIT", NULL };
    int i;

    printf ("%s: %s", w->name, cmdflags[w->command%5]);
    for (i=1;cmdstate[i];i++) {
        if (w->state & (1<<i)) printf(" %s", cmdstate[i]);
    }
    if (w->work_type == WORKER) {
        WorkerQueueData_t *ww = (WorkerQueueData_t*)w;
        printf(" wait_id=%d count=%d",ww->wait_id, ww->count);
    }
    printf("\n");
}

void
dbmdb_dump_import_job(ImportJob *job)
{
    ImportWorkerInfo *w;
	printf("job flags: 0x%x\n", job->flags);
    for (w=job->worker_list; w; w=w->next)  dbmdb_dump_worker(w);
}

