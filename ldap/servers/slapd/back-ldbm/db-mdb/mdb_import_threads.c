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
 * the threads that make up an import:
 * main (0 or 1)
 * producer (0 or 1)
 * worker (N: 1 for each cpus)
 * writer (1)
 *
 * a wire import (aka "fast replica" import) won't have a producer thread.
 */

#include <stddef.h>
#include "mdb_import.h"
#include "../vlv_srch.h"

#define CV_TIMEOUT    10000000  /* 10 milli seconds timeout */

/* Value to determine when to wait before adding item to write queue and
 * when to wait until having enough item in queue to start emptying it
 * Note: the thresholds applies to a single writing queue slot (i.e dbi)
 */
#define MAX_WEIGHT        (256*1024)        /* queue full threshold */
#define MIN_WEIGHT        (MAX_WEIGHT/4)    /* enough data to open a txn threshold */
#define BASE_WEIGHT       256               /* minimum weight of a queue element */

#define MII_TOMBSTONE         0x01
#define MII_OBJECTCLASS       0x02
#define MII_TOMBSTONE_CSN     0x04
#define MII_SKIP              0x08
#define MII_NOATTR            0x10

 /* Compute the padding size needed to get aligned on long integer */
#define ALIGN_TO_LONG(pos)            ((-(long)(pos))&((sizeof(long))-1))

typedef struct {
    back_txn txn;
    ImportCtx_t *ctx;
} PseudoTxn_t;

typedef enum { PEA_OK, PEA_ABORT, PEA_RENAME, PEA_DUPDN, PEA_SKIP, PEA_TOMBSTONE } ProcessEntryAction_t;

typedef struct backentry backentry;
static PseudoTxn_t init_pseudo_txn(ImportCtx_t *ctx);
static int cmp_mii(caddr_t data1, caddr_t data2);
static int have_workers_finished(ImportJob *job);
static void dbmdb_import_writeq_push(ImportCtx_t *ctx, WriterQueueData_t *wqd);
struct backentry *dbmdb_import_prepare_worker_entry(WorkerQueueData_t *wqelmnt);

static inline void __attribute__((always_inline))
set_data(MDB_val *to, dbi_val_t *from)
{
    to->mv_size = from->size;
    to->mv_data = from->data;
}

static inline void __attribute__((always_inline))
dup_data(MDB_val *to, MDB_val *from)
{
    to->mv_size = from->mv_size;
    if (from->mv_data) {
        to->mv_data = slapi_ch_malloc(from->mv_size);
        memcpy(to->mv_data, from->mv_data, from->mv_size);
    } else {
        to->mv_data = NULL;
    }
}

int
dbmdb_import_workerq_init(ImportJob *job, ImportQueue_t *q, int slot_size, int max_slots)
{
    q->job = job;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cv, NULL);
    q->slot_size = slot_size;
    q->max_slots = max_slots;
    q->used_slots = 0;
    q->slots = (WorkerQueueData_t*)slapi_ch_calloc(max_slots, slot_size);
    return 0;
}

void thread_abort(ImportWorkerInfo *info)
{
    info->state = ABORTED;
}


void safe_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex)
{
        struct timespec cvtimeout;
        clock_gettime(CLOCK_REALTIME, &cvtimeout);
        cvtimeout.tv_nsec += 100 * 1000 * 1000;
        pthread_cond_timedwait(cond, mutex, &cvtimeout);
}

int
dbmdb_import_workerq_push(ImportQueue_t *q, WorkerQueueData_t *data)
{
    WorkerQueueData_t *slot =NULL;

    pthread_mutex_lock(&q->mutex);
    if (q->used_slots < q->max_slots) {
        slot = &q->slots[q->used_slots++];
    } else {
        while ((slot = dbmdb_get_free_worker_slot(q)) == 0 || (q->job->flags & FLAG_ABORT)) {
            safe_cond_wait(&q->cv, &q->mutex);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    if (q->job->flags & FLAG_ABORT) {
        return -1;
    }
    dbmdb_dup_worker_slot(q, data, slot);
    return 0;
}

struct backentry *
dbmdb_import_make_backentry(Slapi_Entry *e, ID id)
{
    struct backentry *ep = backentry_alloc();

    if (NULL != ep) {
        ep->ep_entry = e;
        ep->ep_id = id;
    }
    return ep;
}

/* generate uniqueid if requested */
int
dbmdb_import_generate_uniqueid(ImportJob *job, Slapi_Entry *e)
{
    const char *uniqueid = slapi_entry_get_uniqueid(e);
    int rc = UID_SUCCESS;

    if (!uniqueid && (job->uuid_gen_type != SLAPI_UNIQUEID_GENERATE_NONE)) {
        char *newuniqueid;

        /* generate id based on dn */
        if (job->uuid_gen_type == SLAPI_UNIQUEID_GENERATE_NAME_BASED) {
            char *dn = slapi_entry_get_dn(e);

            rc = slapi_uniqueIDGenerateFromNameString(&newuniqueid,
                                                      job->uuid_namespace, dn, strlen(dn));
        } else {
            /* time based */
            rc = slapi_uniqueIDGenerateString(&newuniqueid);
        }

        if (rc == UID_SUCCESS) {
            slapi_entry_set_uniqueid(e, newuniqueid);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_generate_uniqueid",
                          "Failed to generate uniqueid for %s; error=%d.\n",
                          slapi_entry_get_dn_const(e), rc);
        }
    }

    return (rc);
}

/*
 * Check if the tombstone csn is missing, if so add it.
 */
static void
dbmdb_import_generate_tombstone_csn(Slapi_Entry *e)
{
    if (e->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
        if (attrlist_find(e->e_attrs, SLAPI_ATTR_TOMBSTONE_CSN) == NULL) {
            const CSN *tombstone_csn = NULL;
            char tombstone_csnstr[CSN_STRSIZE];

            /* Add the tombstone csn str */
            if ((tombstone_csn = entry_get_deletion_csn(e))) {
                csn_as_string(tombstone_csn, PR_FALSE, tombstone_csnstr);
                slapi_entry_add_string(e, SLAPI_ATTR_TOMBSTONE_CSN, tombstone_csnstr);
            }
        }
    }
}


/**********  BETTER LDIF PARSER  **********/


/* like the function in libldif, except this one doesn't need to use
 * FILE (which breaks on various platforms for >4G files or large numbers
 * of open files)
 */
#define LDIF_BUFFER_SIZE 8192

typedef struct
{
    char *b;       /* buffer */
    size_t size;   /* how full the buffer is */
    size_t offset; /* where the current entry starts */
} ldif_context;

static void
dbmdb_import_init_ldif(ldif_context *c)
{
    c->size = c->offset = 0;
    c->b = NULL;
}

static void
dbmdb_import_free_ldif(ldif_context *c)
{
    if (c->b)
        FREE(c->b);
    dbmdb_import_init_ldif(c);
}

static char *
dbmdb_import_get_entry(ldif_context *c, int fd, int *lineno)
{
    int ret;
    int done = 0, got_lf = 0;
    size_t bufSize = 0, bufOffset = 0, i;
    char *buf = NULL;

    while (!done) {
        /* If there's no data in the buffer, get some */
        if ((c->size == 0) || (c->offset == c->size)) {
            /* Do we even have a buffer ? */
            if (!c->b) {
                c->b = slapi_ch_malloc(LDIF_BUFFER_SIZE);
                if (!c->b)
                    return NULL;
            }
            ret = read(fd, c->b, LDIF_BUFFER_SIZE);
            if (ret < 0) {
                /* Must be error */
                goto error;
            } else if (ret == 0) {
                /* eof */
                if (buf) {
                    /* last entry */
                    buf[bufOffset] = 0;
                    return buf;
                }
                return NULL;
            } else {
                /* read completed OK */
                c->size = ret;
                c->offset = 0;
            }
        }

        /* skip blank lines at start of entry */
        if (bufOffset == 0) {
            size_t n;
            char *p;

            for (n = c->offset, p = c->b + n; n < c->size; n++, p++) {
                if (!(*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t'))
                    break;
            }
            c->offset = n;
            if (c->offset == c->size)
                continue;
        }

        i = c->offset;
        while (!done && (i < c->size)) {
            /* scan forward in the buffer, looking for the end of the entry */
            while ((i < c->size) && (c->b[i] != '\n'))
                i++;

            if ((i < c->size) && (c->b[i] == '\n')) {
                if (got_lf && ((i == 0) || ((i == 1) && (c->b[0] == '\r')))) {
                    /* saw an lf at the end of the last buffer */
                    i++, (*lineno)++;
                    done = 1;
                    got_lf = 0;
                    break;
                }
                got_lf = 0;
                (*lineno)++;
                /* is this the end?  (need another linefeed) */
                if (++i < c->size) {
                    if (c->b[i] == '\n') {
                        /* gotcha! */
                        i++, (*lineno)++;
                        done = 1;
                    } else if (c->b[i] == '\r') {
                        if (++i < c->size) {
                            if (c->b[i] == '\n') {
                                /* gotcha! (nt) */
                                i++, (*lineno)++;
                                done = 1;
                            }
                        } else {
                            got_lf = 1;
                        }
                    }
                } else {
                    /* lf at the very end of the buffer */
                    got_lf = 1;
                }
            }
        }

        /* copy what we did so far into the output buffer */
        /* (first, make sure the output buffer is large enough) */
        if (bufSize - bufOffset < i - c->offset + 1) {
            char *newbuf = NULL;
            size_t newsize = (buf ? bufSize * 2 : LDIF_BUFFER_SIZE);

            newbuf = slapi_ch_malloc(newsize);
            if (!newbuf)
                goto error;
            /* copy over the old data (if there was any) */
            if (buf) {
                memmove(newbuf, buf, bufOffset);
                slapi_ch_free((void **)&buf);
            }
            buf = newbuf;
            bufSize = newsize;
        }
        if (!buf) {
            /* Test always false (buf get initialized in first iteration
             * but it makes gcc -fanalyzer happy
             */
            return NULL;
        }
        memmove(buf + bufOffset, c->b + c->offset, i - c->offset);
        bufOffset += (i - c->offset);
        c->offset = i;
    }

    /* add terminating NUL char */
    buf[bufOffset] = 0;
    return buf;

error:
    if (buf)
        slapi_ch_free((void **)&buf);
    return NULL;
}


/**********  THREADS  **********/

/*
 * add CreatorsName, ModifiersName, CreateTimestamp, ModifyTimestamp to entry
 */
static void
dbmdb_import_add_created_attrs(Slapi_Entry *e)
{
    char buf[SLAPI_TIMESTAMP_BUFSIZE];
    struct berval bv;
    struct berval *bvals[2];

    bvals[0] = &bv;
    bvals[1] = NULL;

    bv.bv_val = "";
    bv.bv_len = 0;
    if (!attrlist_find(e->e_attrs, "creatorsname")) {
        slapi_entry_attr_replace(e, "creatorsname", bvals);
    }
    if (!attrlist_find(e->e_attrs, "modifiersname")) {
        slapi_entry_attr_replace(e, "modifiersname", bvals);
    }

    slapi_timestamp_utc_hr(buf, SLAPI_TIMESTAMP_BUFSIZE);

    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    if (!attrlist_find(e->e_attrs, "createtimestamp")) {
        slapi_entry_attr_replace(e, "createtimestamp", bvals);
    }
    if (!attrlist_find(e->e_attrs, "modifytimestamp")) {
        slapi_entry_attr_replace(e, "modifytimestamp", bvals);
    }
}

/* Tell whrether a job thread is asked to stop */
static inline int __attribute__((always_inline))
info_is_finished(ImportWorkerInfo *info)
{
    return (info->command == ABORT) ||
           (info->command == STOP) ||
           (info->state == ABORTED) ||
           (info->state == FINISHED) ||
           (info->job->flags & FLAG_ABORT);
}

/* Tell whether all worker threads have finished */
static int
have_workers_finished(ImportJob *job)
{
    ImportWorkerInfo *current_worker = NULL;

    for (current_worker = job->worker_list; current_worker != NULL;
         current_worker = current_worker->next) {
        if (current_worker->work_type == WORKER &&
            !(current_worker->state & FINISHED)) {
            return 0;
        }
    }
    return 1;
}

/* Set state when a job thread finish */
static inline void __attribute__((always_inline))
info_set_state(ImportWorkerInfo *info)
{
    if (info->state & ABORTED) {
        info->state = FINISHED | ABORTED;
    } else {
        info->state = FINISHED;
    }
}

/* producer thread for ldif import case:
 * read through the given file list, parsing entries (str2entry), assigning
 * them IDs and queueing them on the worker threads slots.
 * (Worker threads are in charge of decoding the entries updating operationnal
 * attributes and indexing the entries)
 * Note unlike bdb a worker thread handles all index for a given entry
 */
void
dbmdb_import_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    ID id = job->first_ID, id_filestart = id;
    PRIntervalTime sleeptime;
    int detected_eof = 0;
    int fd, curr_file, curr_lineno = 0;
    char *curr_filename = NULL;
    int idx;
    ldif_context c;
    WorkerQueueData_t wqelmt = {0};

    PR_ASSERT(info != NULL);
    PR_ASSERT(job->inst != NULL);


    ctx->wgc.str2entry_flags = SLAPI_STR2ENTRY_TOMBSTONE_CHECK |
                      SLAPI_STR2ENTRY_REMOVEDUPVALS |
                      SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
                      SLAPI_STR2ENTRY_ADDRDNVALS |
                      SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    /* pause until we're told to run */
    while ((info->command == PAUSE) && !info_is_finished(info)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;
    dbmdb_import_init_ldif(&c);

    /* Get entryusn, if needed. */
    _get_import_entryusn(job, &(job->usn_value));

    /* jumpstart by opening the first file */
    curr_file = 0;
    fd = -1;
    detected_eof = 0;

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    while (!info_is_finished(info)) {
        /* move on to next file? */
        if (detected_eof) {
            /* check if the file can still be read, whine if so... */
            if (read(fd, (void *)&idx, 1) > 0) {
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Unexpected end of file found at line %d of file \"%s\"",
                                  curr_lineno, curr_filename);
            }

            if (fd == STDIN_FILENO) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer",
                                  "Finished scanning file stdin (%lu entries)",
                                  (u_long)(id - id_filestart));
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer",
                                 "Finished scanning file \"%s\" (%lu entries)",
                                  curr_filename, (u_long)(id - id_filestart));
            }
            close(fd);
            fd = -1;
            detected_eof = 0;
            id_filestart = id;
            curr_file++;
            if (job->task) {
                job->task->task_progress++;
                slapi_task_status_changed(job->task);
            }
            if (job->input_filenames[curr_file] == NULL) {
                /* done! */
                break;
            }
        }

        /* separate from above, because this is also triggered when we
         * start (to open the first file)
         */
        if (fd < 0) {
            curr_lineno = 0;
            curr_filename = job->input_filenames[curr_file];
            wqelmt.filename = curr_filename;
            if (strcmp(curr_filename, "-") == 0) {
                fd = STDIN_FILENO;
            } else {
                int o_flag = O_RDONLY;
                fd = dbmdb_open_huge_file(curr_filename, o_flag, 0);
            }
            if (fd < 0) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer",
                                  "Could not open LDIF file \"%s\", errno %d (%s)",
                                  curr_filename, errno, slapd_system_strerror(errno));
                thread_abort(info);
                break;
            }
            if (fd == STDIN_FILENO) {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer", "Processing file stdin");
            } else {
                import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_producer",
                                  "Processing file \"%s\"", curr_filename);
            }
        }
        while ((info->command == PAUSE) && !info_is_finished(info)) {
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;
        if (info_is_finished(info)) {
            break;
        }

        wqelmt.wait_id = id;
        wqelmt.lineno = curr_lineno;
        wqelmt.data = dbmdb_import_get_entry(&c, fd, &curr_lineno);
        wqelmt.nblines = curr_lineno - wqelmt.lineno;
        wqelmt.datalen = 0;
        if (!wqelmt.data) {
            /* error reading entry, or end of file */
            detected_eof = 1;
            continue;
        }


        dbmdb_import_workerq_push(&ctx->workerq, &wqelmt);

       /* Update our progress meter too */
        info->last_ID_processed = id;
        job->lead_ID = id;
        id++;
    }

    /* capture skipped entry warnings for this task */
    if((job) && (job->skipped)) {
        slapi_task_set_warning(job->task, WARN_SKIPPED_IMPORT_ENTRY);
    }

    if (fd >= 0)
        close(fd);
    slapi_value_free(&(job->usn_value));
    dbmdb_import_free_ldif(&c);
    info_set_state(info);
}

/*
 * prepare entry callbacks are called from worker thread and
 *  in charge of transforming the producer data into a backentry
 * before it get processed to generates the indexes and stored in db
 * This is the callback for ldif import case
 */
struct backentry *
dbmdb_import_prepare_worker_entry(WorkerQueueData_t *wqelmnt)
{
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    ID id = wqelmnt->wait_id;
    Slapi_Entry *e = NULL;
    struct backentry *ep = NULL;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    char *estr = wqelmnt->data;
    int curr_lineno = wqelmnt->lineno;
    char *curr_filename = wqelmnt->filename;
    int lines_in_entry = wqelmnt->nblines;
    Slapi_Attr *attr = NULL;
    int syntax_err;
    int flags;


    ctx->wgc.str2entry_flags = SLAPI_STR2ENTRY_TOMBSTONE_CHECK |
                      SLAPI_STR2ENTRY_REMOVEDUPVALS |
                      SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
                      SLAPI_STR2ENTRY_ADDRDNVALS |
                      SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;

    if (0 == ctx->wgc.version_found && 0 == strncmp(estr, "version:", 8)) {
        sscanf(estr, "version: %d", &ctx->wgc.my_version);
        ctx->wgc.str2entry_flags |= SLAPI_STR2ENTRY_INCLUDE_VERSION_STR;
        ctx->wgc.version_found = 1;
        FREE(estr);
        wqelmnt->data = NULL;
        return NULL;
    }

    /* If there are more than so many lines in the entry, we tell
     * str2entry to optimize for a large entry.
     */
    if (lines_in_entry > STR2ENTRY_ATTRIBUTE_PRESENCE_CHECK_THRESHOLD) {
        flags = ctx->wgc.str2entry_flags | SLAPI_STR2ENTRY_BIGENTRY;
    } else {
        flags = ctx->wgc.str2entry_flags;
    }
    if (!(ctx->wgc.str2entry_flags & SLAPI_STR2ENTRY_INCLUDE_VERSION_STR)) { /* subtree-rename: on */
        char *dn = NULL;
        char *normdn = NULL;
        int rc = 0; /* estr should start with "dn: " or "dn:: " */
        if (strncmp(estr, "dn: ", 4) &&
            NULL == strstr(estr, "\ndn: ") && /* in case comments precedes the entry */
            strncmp(estr, "dn:: ", 5) &&
            NULL == strstr(estr, "\ndn:: ")) { /* ditto */
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_prepare_worker_entry",
                              "Skipping bad LDIF entry (not starting with \"dn: \") ending line %d of file \"%s\"",
                              curr_lineno, curr_filename);
            FREE(estr);
            wqelmnt->data = NULL;
            job->skipped++;
            return NULL;
        }
        /* get_value_from_string decodes base64 if it is encoded. */
        rc = get_value_from_string((const char *)estr, "dn", &dn);
        if (rc) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                              "Skipping bad LDIF entry (dn has no value\n");
            FREE(estr);
            wqelmnt->data = NULL;
            job->skipped++;
            return NULL;
        }
        normdn = slapi_create_dn_string("%s", dn);
        slapi_ch_free_string(&dn);
        e = slapi_str2entry_ext(normdn, NULL, estr,
                                flags | SLAPI_STR2ENTRY_NO_ENTRYDN);
        slapi_ch_free_string(&normdn);
    } else {
        e = slapi_str2entry(estr, flags);
    }
    FREE(estr);
    wqelmnt->data = NULL;
    if (!e) {
        if (!(ctx->wgc.str2entry_flags & SLAPI_STR2ENTRY_INCLUDE_VERSION_STR)) {
            import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                              "Skipping bad LDIF entry ending line %d of file \"%s\"",
                              curr_lineno, curr_filename);
        }
        job->skipped++;
        return NULL;
    }
    /* From here, e != NULL */

    if (!dbmdb_import_entry_belongs_here(e, inst->inst_be)) {
        /* silently skip */
        job->not_here_skipped++;
        slapi_entry_free(e);
        return NULL;
    }

    if (slapi_entry_schema_check(NULL, e) != 0) {
        import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_prepare_worker_entry",
                          "Skipping entry \"%s\" which violates schema, ending line %d of file \"%s\"",
                          slapi_entry_get_dn(e), curr_lineno, curr_filename);
        slapi_entry_free(e);

        job->skipped++;
        return NULL;
    }

    /* If we are importing pre-encrypted attributes, we need
     * to skip syntax checks for the encrypted values. */
    if (!(job->encrypt) && inst->attrcrypt_configured) {
        Slapi_Entry *e_copy = NULL;

        /* Scan through the entry to see if any present
         * attributes are configured for encryption. */
        slapi_entry_first_attr(e, &attr);
        while (attr) {
            char *type = NULL;
            struct attrinfo *ai = NULL;

            slapi_attr_get_type(attr, &type);

            /* Check if this type is configured for encryption. */
            ainfo_get(be, type, &ai);
            if (ai->ai_attrcrypt != NULL) {
                /* Make a copy of the entry to use for syntax
                 * checking if a copy has not been made yet. */
                if (e_copy == NULL) {
                    e_copy = slapi_entry_dup(e);
                }

                /* Delete the enrypted attribute from the copy. */
                slapi_entry_attr_delete(e_copy, type);
            }

            slapi_entry_next_attr(e, attr, &attr);
        }

        if (e_copy) {
            syntax_err = slapi_entry_syntax_check(NULL, e_copy, 0);
            slapi_entry_free(e_copy);
        } else {
            syntax_err = slapi_entry_syntax_check(NULL, e, 0);
        }
    } else {
        syntax_err = slapi_entry_syntax_check(NULL, e, 0);
    }

    /* Check attribute syntax */
    if (syntax_err != 0) {
        import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_prepare_worker_entry",
                          "Skipping entry \"%s\" which violates attribute syntax, ending line %d of file \"%s\"",
                          slapi_entry_get_dn(e), curr_lineno, curr_filename);
        slapi_entry_free(e);

        job->skipped++;
        return NULL;
    }

    /* generate uniqueid if necessary */
    if (dbmdb_import_generate_uniqueid(job, e) != UID_SUCCESS) {
        thread_abort(info);
        return NULL;
    }

    if (g_get_global_lastmod()) {
        dbmdb_import_add_created_attrs(e);
    }
    /* Add nsTombstoneCSN to tombstone entries unless it's already present */
    dbmdb_import_generate_tombstone_csn(e);

    ep = dbmdb_import_make_backentry(e, id);
    if ((ep == NULL) || (ep->ep_entry == NULL)) {
        thread_abort(info);
        slapi_entry_free(e);
        backentry_free(&ep);
        return NULL;
    }

    /* check for include/exclude subtree lists */
    if (!dbmdb_back_ok_to_dump(backentry_get_ndn(ep),
                              job->include_subtrees,
                              job->exclude_subtrees)) {
        backentry_free(&ep);
        return NULL;
    }

    /* not sure what this does, but it looked like it could be
     * simplified.  if it's broken, it's my fault.  -robey
     */
    if (slapi_entry_attr_find(ep->ep_entry, "userpassword", &attr) == 0) {
        Slapi_Value **va = attr_get_present_values(attr);

        pw_encodevals((Slapi_Value **)va); /* jcm - cast away const */
    }

    if (job->flags & FLAG_ABORT) {
        backentry_free(&ep);
        return NULL;
    }

    /* if usn_value is available AND the entry does not have it, */
    if (job->usn_value && slapi_entry_attr_find(ep->ep_entry,
                                                SLAPI_ATTR_ENTRYUSN, &attr)) {
        slapi_entry_add_value(ep->ep_entry, SLAPI_ATTR_ENTRYUSN,
                              job->usn_value);
    }

    /* Now we have this new entry, all decoded */
    return ep;
}

static int dbmdb_get_aux_id2entry(backend*be, dbmdb_dbi_t **dbi, char **path)
{
     return dbmdb_open_dbi_from_filename(dbi, be, ID2ENTRY, NULL, 0);
}

/* producer thread for re-indexing:
 * read id2entry, parsing entries (str2entry) (needed???), assigning
 * them IDs (again, needed???) and queueing them on the entry FIFO.
 * other threads will do the indexing -- same as in import.
 */
void
dbmdb_index_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    ldbm_instance *inst = job->inst;
    PRIntervalTime sleeptime;
    dbmdb_dbi_t *db = NULL;
    MDB_cursor *dbc = NULL;
    MDB_val datacopy = {0};
    char *id2entry = NULL;
    MDB_txn *txn = NULL;
    MDB_val data = {0};
    MDB_val key = {0};
    int rc = 0;
    enum { TXS_NONE, TXS_VALID, TXS_RESET } txn_state = TXS_NONE;
    char *errinfo = NULL;
    char lastid[sizeof (ID)] = {0};

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);

    ctx->wgc.str2entry_flags = SLAPI_STR2ENTRY_NO_ENTRYDN | SLAPI_STR2ENTRY_INCLUDE_VERSION_STR;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    /* pause until we're told to run */
    while ((info->command == PAUSE) && !info_is_finished(info)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;

    /* Get entryusn, if needed. */
    _get_import_entryusn(job, &(job->usn_value));

    /* open id2entry with dedicated db env and db handler */
    if (dbmdb_get_aux_id2entry(inst->inst_be, &db, &id2entry) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer", "Could not open id2entry\n");
        thread_abort(info);
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    while (rc == 0 && !info_is_finished(info)) {
        WorkerQueueData_t *slot = dbmdb_get_free_worker_slot(&ctx->workerq);
        if (slot == NULL) {
            if (txn_state == TXS_VALID) {
                TXN_RESET(txn);
                txn_state = TXS_RESET;
            }
            pthread_mutex_lock(&ctx->workerq.mutex);
            while (slot == NULL && !info_is_finished(info)) {
                safe_cond_wait(&ctx->workerq.cv, &ctx->workerq.mutex);
                slot = dbmdb_get_free_worker_slot(&ctx->workerq);
            }
            pthread_mutex_unlock(&ctx->workerq.mutex);
        }
        if (slot == NULL) {
            /* We are asked to finish */
            break;
        }
        switch (txn_state) {
            case TXS_NONE:
                rc = TXN_BEGIN(ctx->ctx->env, NULL, MDB_RDONLY, &txn);
                if (rc) {
                    errinfo = "open transaction";
                    continue;
                }
                txn_state = TXS_VALID;
                rc = MDB_CURSOR_OPEN(txn, db->dbi, &dbc);
                if (rc) {
                    errinfo = "open cursor";
                    continue;
                }
                rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_FIRST);
                break;
            case TXS_RESET:
                rc = TXN_RENEW(txn);
                if (rc) {
                    errinfo = "reset transaction";
                    continue;
                }
                txn_state = TXS_VALID;
                rc = MDB_CURSOR_RENEW(txn, dbc);
                if (rc) {
                    errinfo = "reset cursor";
                    continue;
                }
                key.mv_data = lastid;
                rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_SET_RANGE);
                if (rc == 0 && memcmp(key.mv_data, lastid, sizeof (ID)) == 0) {
                    rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_NEXT);
                }
                break;
            case TXS_VALID:
                rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_NEXT);
                break;
        }

        if (rc == MDB_NOTFOUND) {
            rc = 0;
            break;
        }
        if (rc) {
            errinfo = "read entry from cursor";
            break;
        }
        /* Push the entry to the worker thread queue */
        dup_data(&datacopy, &data);
        memcpy(lastid, key.mv_data, sizeof (ID));
        slot->data = datacopy.mv_data;
        slot->datalen = datacopy.mv_size;
        /* wait_id should be the last field updated */
        slot->wait_id = id_stored_to_internal((char *)key.mv_data);
        pthread_cond_broadcast(&ctx->workerq.cv);
    }
    if (txn_state != TXS_NONE) {
        MDB_CURSOR_CLOSE(dbc);
        TXN_ABORT(txn);
    }
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer",
                      "%s: Failed to read database: failed to %s, errno=%d (%s)\n",
                      inst->inst_name, errinfo, rc, dblayer_strerror(rc));
        if (job->task) {
            slapi_task_log_notice(job->task,
                                  "%s: Failed to read database, err %d (%s)",
                                  inst->inst_name, rc,
                                  dblayer_strerror(rc));
        }
        thread_abort(info);
    }
    slapi_ch_free_string(&id2entry);
    info_set_state(info);
}

/*
 * prepare entry callbacks are called from worker thread and
 *  in charge of transforming the producer data into a backentry
 * before it get processed to generates the indexes and stored in db
 */
struct backentry *
dbmdb_import_index_prepare_worker_entry(WorkerQueueData_t *wqelmnt)
{
    const char *suffix = slapi_sdn_get_dn(wqelmnt->winfo.job->inst->inst_be->be_suffix);
    ImportWorkerInfo *info = &wqelmnt->winfo;
    uint entry_len = wqelmnt->datalen;
    char *entry_str = wqelmnt->data;
    struct backentry *ep = NULL;
    ID id = wqelmnt->wait_id;
    Slapi_Entry *e = NULL;
    char *normdn = NULL;
    char *rdn = NULL;

    /* call post-entry plugin */
    plugin_call_entryfetch_plugins(&entry_str, &entry_len);

    /*
     * dn is yet unknown so lets use the rdn instead.
     * but slapi_str2entry_ext needs that entry is in the suffix
     * (to avoid error while processing tombstone)
     * if needed (upgrade case) dn could be recomputed when walking
     * the ancestors in process_entryrdn_byrdn
     */
    if (get_value_from_string(entry_str, "rdn", &rdn)) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_index_prepare_worker_entry",
                "Invalid entry (no rdn) in database for id %d entry: %s\n",
                id, entry_str);
        slapi_ch_free(&wqelmnt->data);
        thread_abort(info);
        return NULL;
    }
    if (strcasecmp(rdn, suffix) == 0) {
        normdn = slapi_ch_strdup(rdn);
    } else {
        normdn = slapi_ch_smprintf("%s,%s", rdn, suffix);
    }
    e = slapi_str2entry_ext(normdn, NULL, entry_str, SLAPI_STR2ENTRY_NO_ENTRYDN);
    slapi_ch_free_string(&normdn);
    slapi_ch_free_string(&rdn);
    if (e==NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_index_prepare_worker_entry",
                "Invalid entry (Conversion failed) in database for id %d entry: %s\n",
                id, entry_str);
    }
    slapi_ch_free(&wqelmnt->data);
    ep = dbmdb_import_make_backentry(e, id);
    if ((ep == NULL) || (ep->ep_entry == NULL)) {
        thread_abort(info);
        slapi_entry_free(e);
        backentry_free(&ep);
        return NULL;
    }

    return ep;
}

struct upgradedn_attr
{
    char *ud_type;
    char *ud_value;
    struct upgradedn_attr *ud_next;
    int ud_flags;
#define OLD_DN_NORMALIZE 0x1
};

#if 0
/*
 * Return value: count of max consecutive spaces
 */
static int
dbmdb_has_spaces(const char *str)
{
    char *p = (char *)str;
    char *endp = p + strlen(str);
    int wcnt;
    int maxwcnt = 0;
    while (p < endp) {
        p += strcspn(p, " \t");
        wcnt = 0;
        while ((p < endp) && isspace(*p)) {
            wcnt++;
            p++;
        }
        if (maxwcnt < wcnt) {
            maxwcnt = wcnt;
        }
    }
    return maxwcnt;
}
#endif

/* upgrade producer thread (same as index producer */
void
dbmdb_upgradedn_producer(void *param)
{
    /* Same as reindexing */
    dbmdb_index_producer(param);
}


/*
 * prepare entry callbacks are called from worker thread and
 *  in charge of transforming the producer data into a backentry
 * before it get processed to generates the indexes and stored in db
 *
 * Producer thread for upgrading dn format
 * FLAG_UPGRADEDNFORMAT | FLAG_DRYRUN -- check the necessity of dn upgrade
 * FLAG_UPGRADEDNFORMAT -- execute dn upgrade
 *
 * Read id2entry,
 * Check the DN syntax attributes if it contains '\' or not AND
 * Check the RDNs of the attributes if the value is surrounded by
 * double-quotes or not.
 * If both are false, skip the entry and go to next
 * If either is true, create an entry which contains a correctly normalized
 *     DN attribute values in e_attr list and the original entrydn in the
 *     deleted attribute list e_aux_attrs.
 *
 * If FLAG_UPGRADEDNFORMAT is set, worker_threads for indexing DN syntax
 * attributes (+ cn & ou) are brought up.  Foreman thread updates entrydn
 * or entryrd index as well as the entry itself in the id2entry.db#.
 *
 * Note: QUIT state for info->state is introduced for DRYRUN mode to
 *       distinguish the intentional QUIT (found the dn upgrade candidate)
 *       from ABORTED (aborted or error) and FINISHED (scan all the entries
 *       and found no candidate to upgrade)
 */
/*
 * FLAG_UPGRADEDNFORMAT: need to take care space reduction
 * dn: cn=TEST   USER0 --> dn: cn=TEST USER0
 * If multiple DNs are found having different count of spaces,
 * remove the second and later with exporting them into ldif file.
 * In the dry run, it retrieves all the entries having spaces in dn and
 * store the DN and OID in a file.  Mark, conflicts if any.
 * In the real run, store only no conflict entries and first conflicted one.
 * The rest are stored in an ldif file.
 *
 * Cases:
 *   FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1
 *     ==> 1 & 2-1
 *   FLAG_DRYRUN | FLAG_UPGRADEDNFORMAT_V1
 *     ==> 2-1
 *   FLAG_UPGRADEDNFORMAT | FLAG_UPGRADEDNFORMAT_V1
 *     ==> 1 & 2-2,3
 *   FLAG_UPGRADEDNFORMAT_V1
 *     ==> 2-2,3
 *
 *       1) dn normalization
 *       2) space handling
 *       2-1) upgradedn dry-run: output into a file (in the instance db dir?)
 *            format:
 *            <original dn>:OID
 *       2-2) upgrade script sorts the file; retrieve duplicated DNs
 *            <dn>:OID0
 *            <dupped dn>:OID1
 *            <dupped dn>:OID2
 *
 *            format:
 *            OID0:OID1 OID2 ...
 *       2-3) upgradedn: compare the OID with the OID1,OID2,
 *            if match, rename the rdn value to "value <entryid>".
 */
struct backentry *
dbmdb_upgrade_prepare_worker_entry(WorkerQueueData_t *wqelmnt)
{
    /* so far only new format is generatrd so no need to upgrade */
    return NULL;
#if 0
    struct backentry *ep = dbmdb_import_index_prepare_worker_entry(wqelmnt);
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    int is_dryrun = 0;      /* FLAG_DRYRUN */
    int chk_dn_norm = 0;    /* FLAG_UPGRADEDNFORMAT */
    int chk_dn_norm_sp = 0; /* FLAG_UPGRADEDNFORMAT_V1 */
    Slapi_Entry *e = NULL;
    char 8rdn = NULL;

    if (!ep) {
        return NULL;
    }

    e = ep->ep_entry;
    is_dryrun = job->flags & FLAG_DRYRUN;
    chk_dn_norm = job->flags & FLAG_UPGRADEDNFORMAT;
    chk_dn_norm_sp = job->flags & FLAG_UPGRADEDNFORMAT_V1;

    if (!chk_dn_norm && !chk_dn_norm_sp) {
        /* Nothing to do... */
        slapi_log_err(SLAPI_LOG_INFO, "dbmdb_upgradedn_producer",
                      "UpgradeDnFormat is not required.\n");
        info->state = FINISHED;
        return NULL;
    }

    rdn = slapi_ch_strdup(slapi_entry_get_rdn_const(e));
/* HERE IS THE OLD CODE - SHOULD BE CHANGED TO FIX RDN IF NEEDED */


    ImportWorkerInfo *info = (&wqelmnt->winfo;
    ImportJob *job = info->job;
    ID id = job->first_ID;
    struct backentry *ep = NULL, *old_ep = NULL;
    ldbm_instance *inst = job->inst;
    struct ldbminfo* li = (struct ldbminfo*)inst->inst_be->be_database->plg_private;
    dbmdb_ctx_t *ctx = MDB_CONFIG(li);
    dbmdb_cursor_t dbc={0};
    PRIntervalTime sleeptime;
    int finished = 0;
    int idx;
    int rc = 0;
    Slapi_Attr *a = NULL;
    Slapi_DN *sdn = NULL;
    char *workdn = NULL;
    struct upgradedn_attr *ud_list = NULL;
    char **ud_vals = NULL;
    char **ud_valp = NULL;
    struct upgradedn_attr *ud_ptr = NULL;
    Slapi_Attr *ud_attr = NULL;
    char *ecopy = NULL;
    char *normdn = NULL;
    char *rdn = NULL;       /* original rdn */
    int is_dryrun = 0;      /* FLAG_DRYRUN */
    int chk_dn_norm = 0;    /* FLAG_UPGRADEDNFORMAT */
    int chk_dn_norm_sp = 0; /* FLAG_UPGRADEDNFORMAT_V1 */
    ID **dn_norm_sp_conflicts = NULL;
    int do_dn_norm = 0;    /* do dn_norm */
    int do_dn_norm_sp = 0; /* do dn_norm_sp */
    int rdn_dbmdb_has_spaces = 0;
    int info_state = 0; /* state to add to info->state (for dryrun only) */
    int skipit = 0;
    struct backdn *bdn = NULL;
    ID pid;

    /* vars for Berkeley MDB_dbi*/
    dbmdb_dbi_t *db = NULL;
    MDB_val key = {0};
    MDB_val data = {0};
    int db_rval = -1;
    backend *be = inst->inst_be;
    int isfirst = 1;
    int curr_entry = 0;
    size_t newesize = 0;
     char *entry_str = NULL;
     uint entry_len = 0;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);
    PR_ASSERT(be != NULL);

        }
        curr_entry++;
        temp_id = id_stored_to_internal(key.mv_data);

        /* call post-entry plugin */
         entry_str = data.mv_data;
         entry_len = data.mv_size;
         plugin_call_entryfetch_plugins(&entry_str, &entry_len);

        slapi_ch_free_string(&ecopy);
        ecopy = (char *)slapi_ch_malloc(entry_len + 1);
        memcpy(ecopy, entry_str, entry_len);
        ecopy[entry_len] = '\0';
        normdn = NULL;
        do_dn_norm = 0;
        do_dn_norm_sp = 0;
        rdn_dbmdb_has_spaces = 0;
        dn_in_cache = 0;
        if (entryrdn_get_switch()) {

            /* original rdn is allocated in get_value_from_string */
            rc = get_value_from_string(entry_str, "rdn", &rdn);
            if (rc) {
                /* data.dptr may not include rdn: ..., try "dn: ..." */
                e = slapi_str2entry(entry_str, SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
            } else {
                bdn = dncache_find_id(&inst->inst_dncache, temp_id);
                if (bdn) {
                    /* don't free normdn */
                    normdn = (char *)slapi_sdn_get_dn(bdn->dn_sdn);
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                    dn_in_cache = 1;
                } else {
                    /* free normdn */
                    rc = entryrdn_lookup_dn(be, rdn, temp_id,
                                            (char **)&normdn, NULL, NULL);
                    if (rc) {
                        /* We cannot use the entryrdn index;
                         * Compose dn from the entries in id2entry */
                        Slapi_RDN psrdn = {0};
                        char *pid_str = NULL;
                        char *pdn = NULL;

                        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedn_producer",
                                      "entryrdn is not available; composing dn (rdn: %s, ID: %d)\n",
                                      rdn, temp_id);
                        rc = get_value_from_string(entry_str, LDBM_PARENTID_STR, &pid_str);
                        if (rc) {
                            rc = 0; /* assume this is a suffix */
                        } else {
                            pid = (ID)strtol(pid_str, (char **)NULL, 10);
                            slapi_ch_free_string(&pid_str);
                            /* if pid is larger than the current pid temp_id,
                             * the parent entry hasn't */
                            rc = dbmdb_import_get_and_add_parent_rdns(info, inst, &db,dbc.txn,
                                                                pid, &id, &psrdn, &curr_entry);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR,
                                              "upgradedn: Failed to compose dn for "
                                              "(rdn: %s, ID: %d)\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                slapi_rdn_done(&psrdn);
                                continue;
                            }
                            /* Generate DN string from Slapi_RDN */
                            rc = slapi_rdn_get_dn(&psrdn, &pdn);
                            slapi_rdn_done(&psrdn);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                              "Failed to compose dn for (rdn: %s, ID: %d) from Slapi_RDN\n",
                                              rdn, temp_id);
                                slapi_ch_free_string(&rdn);
                                continue;
                            }
                        }
                        /* free normdn */
                        normdn = slapi_ch_smprintf("%s%s%s",
                                                   rdn, pdn ? "," : "", pdn ? pdn : "");
                        slapi_ch_free_string(&pdn);
                    }
                    if (is_dryrun) {
                        /* if not dryrun, we may change the DN, In such case ,
                         * we need to put the new value to cache.*/
                        /* dn is dup'ed in slapi_sdn_new_dn_byval.
                         * It's set to bdn and put in the dn cache. */
                        /* normdn is allocated in this scope.
                         * Thus, we can just passin. */
                        sdn = slapi_sdn_new_normdn_passin(normdn);
                        bdn = backdn_init(sdn, temp_id, 0);
                        CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                        /* don't free this normdn  */
                        normdn = (char *)slapi_sdn_get_dn(sdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_upgradedn_producer",
                                      "entryrdn_lookup_dn returned: %s, "
                                      "and set to dn cache\n",
                                      normdn);
                        dn_in_cache = 1;
                    }
                }
                e = slapi_str2entry_ext(normdn, NULL, entry_str,
                                        SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
                slapi_ch_free_string(&rdn);
            }
        } else {
            e = slapi_str2entry(entry_str, SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT);
            rdn = slapi_ch_strdup(slapi_entry_get_rdn_const(e));
            if (NULL == rdn) {
                Slapi_RDN srdn;
                slapi_rdn_init_dn(&srdn, slapi_entry_get_dn_const(e));
                rdn = (char *)slapi_rdn_get_rdn(&srdn); /* rdn is allocated in
                                                         * slapi_rdn_init_dn */
            }
        }
        if (NULL == e) {
            if (job->task) {
                slapi_task_log_notice(job->task,
                                      "%s: WARNING: skipping badly formatted entry (id %lu)",
                                      inst->inst_name, (u_long)temp_id);
            }
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedn_producer",
                          "%s: Skipping badly formatted entry (id %lu)\n",
                          inst->inst_name, (u_long)temp_id);
            slapi_ch_free_string(&rdn);
            continue;
        }

        /* From here, e != NULL */
        /*
         * treat dn specially since the entry was generated with the flag
         * SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT
         * -- normalize it with the new format
         */
        if (!normdn) {
            /* No rdn in id2entry or entrydn */
            normdn = (char *)slapi_sdn_get_dn(&(e->e_sdn));
        }

        /*
         * If dryrun && check_dn_norm_sp,
         * check if rdn's containing multi spaces exist or not.
         * If any, output the DN:ID into a file INST_dn_norm_sp.txt in the
         * ldifdir. We have to continue checking all the entries.
         */
        if (chk_dn_norm_sp) {
            char *dn_id;
            char *path; /* <ldifdir>/<inst>_dn_norm_sp.txt is used for
                           the temp work file */
            /* open "path" once, and set FILE* to upgradefd */
            if (NULL == job->upgradefd) {
                char *ldifdir = config_get_ldifdir();
                if (ldifdir) {
                    path = slapi_ch_smprintf("%s/%s_dn_norm_sp.txt",
                                             ldifdir, inst->inst_name);
                    slapi_ch_free_string(&ldifdir);
                } else {
                    path = slapi_ch_smprintf("/var/tmp/%s_dn_norm_sp.txt",
                                             inst->inst_name);
                }
                if (is_dryrun) {
                    job->upgradefd = fopen(path, "w");
                    if (NULL == job->upgradefd) {
                        if (job->task) {
                            slapi_task_log_notice(job->task,
                                                  "%s: No DNs to fix.\n", inst->inst_name);
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: No DNs to fix.\n", inst->inst_name);
                        slapi_ch_free_string(&path);
                        goto bail;
                    }
                } else {
                    job->upgradefd = fopen(path, "r");
                    if (NULL == job->upgradefd) {
                        if (job->task) {
                            slapi_task_log_notice(job->task,
                                                  "%s: Error: failed to open a file \"%s\"",
                                                  inst->inst_name, path);
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: Error: failed to open a file \"%s\"\n",
                                      inst->inst_name, path);
                        slapi_ch_free_string(&path);
                        goto error;
                    }
                }
            }
            slapi_ch_free_string(&path);
            if (is_dryrun) {
                rdn_dbmdb_has_spaces = dbmdb_has_spaces(rdn);
                if (rdn_dbmdb_has_spaces > 0) {
                    dn_id = slapi_ch_smprintf("%s:%u\n",
                                              slapi_entry_get_dn_const(e), temp_id);
                    if (EOF == fputs(dn_id, job->upgradefd)) {
                        if (job->task) {
                            slapi_task_log_notice(job->task,
                                                  "%s: Error: failed to write a line \"%s\"",
                                                  inst->inst_name, dn_id);
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: Error: failed to write a line \"%s\"",
                                      inst->inst_name, dn_id);
                        slapi_ch_free_string(&dn_id);
                        goto error;
                    }
                    slapi_ch_free_string(&dn_id);
                    if (rdn_dbmdb_has_spaces > 1) {
                        /* If an rdn containing multi spaces exists,
                         * let's check the conflict. */
                        do_dn_norm_sp = 1;
                    }
                }
            } else { /* !is_dryrun */
                /* check the oid and parentid. */
                /* Set conflict list once, and refer it laster. */
                static int my_idx = 0;
                ID alt_id;
                if (NULL == dn_norm_sp_conflicts) {
                    char buf[BUFSIZ];
                    int my_max = 8;
                    while (fgets(buf, sizeof(buf) - 1, job->upgradefd)) {
                        /* search "OID0: OID1 OID2 ... */
                        if (!isdigit(*buf) || (NULL == PL_strchr(buf, ':'))) {
                            continue;
                        }
                        if (dbmdb_add_IDs_to_IDarray(&dn_norm_sp_conflicts, &my_max,
                                               my_idx, buf)) {
                            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                          "Failed to set IDs %s to conflict list\n", buf);
                            goto error;
                        }
                        my_idx++;
                    }
                }
                alt_id = dbmdb_is_conflict_ID(dn_norm_sp_conflicts, my_idx, temp_id);
                if (alt_id) {
                    if (alt_id != temp_id) {
                        char *newrdn = slapi_create_dn_string("%s %u", rdn, temp_id);
                        char *parentdn = slapi_dn_parent(normdn);
                        /* This entry is a conflict of alt_id */
                        slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer",
                                      "Entry %s (%u) is a conflict of (%u)\n",
                                      normdn, temp_id, alt_id);
                        slapi_log_err(SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer",
                                      "Renaming \"%s\" to \"%s\"\n", rdn, newrdn);
                        if (!dn_in_cache) {
                            /* If not in dn cache, normdn needs to be freed. */
                            slapi_ch_free_string(&normdn);
                        }
                        normdn = slapi_ch_smprintf("%s,%s", newrdn, parentdn);
                        slapi_ch_free_string(&newrdn);
                        slapi_ch_free_string(&parentdn);
                        /* Reset DN and RDN in the entry */
                        slapi_sdn_done(&(e->e_sdn));
                        slapi_sdn_init_normdn_byval(&(e->e_sdn), normdn);
                    }
                    info_state |= DN_NORM_SP;
                    dbmdb_upgradedn_add_to_list(&ud_list,
                                          slapi_ch_strdup(LDBM_ENTRYRDN_STR),
                                          slapi_ch_strdup(rdn), 0);
                    rc = slapi_entry_add_rdn_values(e);
                    if (rc) {
                        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                      "%s: Failed to add rdn values to an entry: %s (id %lu)\n",
                                      inst->inst_name, normdn, (u_long)temp_id);
                        goto error;
                    }
                } /* !alt_id */
            }     /* !is_dryrun */
        }         /* if (chk_dn_norm_sp) */

        /* dn is dup'ed in slapi_sdn_new_dn_byval.
         * It's set to bdn and put in the dn cache. */
        /* Waited to put normdn into dncache until it could be modified in
         * chk_dn_norm_sp. */
        if (!dn_in_cache) {
            sdn = slapi_sdn_new_normdn_passin(normdn);
            bdn = backdn_init(sdn, temp_id, 0);
            CACHE_ADD(&inst->inst_dncache, bdn, NULL);
            CACHE_RETURN(&inst->inst_dncache, &bdn);
            slapi_log_err(SLAPI_LOG_CACHE, "dbmdb_upgradedn_producer",
                          "set dn %s to dn cache\n", normdn);
        }
        /* Check DN syntax attr values if it contains '\\' or not */
        /* Start from the rdn */
        if (chk_dn_norm) {
            char *endrdn = NULL;
            char *rdnp = NULL;
            endrdn = rdn + strlen(rdn) - 1;

            rdnp = PL_strchr(rdn, '=');
            if (NULL == rdnp) {
                slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedn_producer",
                              "%s: Skipping an entry with corrupted RDN \"%s\" (id %lu)\n",
                              inst->inst_name, rdn, (u_long)temp_id);
                slapi_entry_free(e);
                e = NULL;
                continue;
            }
            /* rdn contains '\\'.  We have to update the value */
            if (PL_strchr(rdnp, '\\')) {
                do_dn_norm = 1;
            } else {
                while ((++rdnp <= endrdn) && (*rdnp == ' ' || *rdnp == '\t'))
                    ;
                /* DN contains an RDN <type>="<value>" ? */
                if ((rdnp != endrdn) && ('"' == *rdnp) && ('"' == *endrdn)) {
                    do_dn_norm = 1;
                }
            }
            if (do_dn_norm) {
                dbmdb_upgradedn_add_to_list(&ud_list,
                                      slapi_ch_strdup(LDBM_ENTRYRDN_STR),
                                      slapi_ch_strdup(rdn), 0);
                slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedn_producer",
                              "%s: Found upgradedn candidate: (id %lu)\n",
                              inst->inst_name, (u_long)temp_id);
                /*
                 * In case rdn is type="<RDN>" or type=<\D\N>,
                 * add the rdn value if it's not there.
                 */
                rc = slapi_entry_add_rdn_values(e);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                  "%s: Failed to add rdn values to an entry: %s (id %lu)\n",
                                  inst->inst_name, normdn, (u_long)temp_id);
                    slapi_entry_free(e);
                    e = NULL;
                    continue;
                }
            }
            /* checking the DN sintax values in the attribute list */
            for (a = e->e_attrs; a; a = a->a_next) {
                if (!slapi_attr_is_dn_syntax_attr(a)) {
                    continue; /* not a syntax dn attr */
                }

                /* dn syntax attr */
                rc = get_values_from_string((const char *)ecopy,
                                            a->a_type, &ud_vals);
                if (rc || (NULL == ud_vals)) {
                    continue; /* empty; ignore it */
                }

                for (ud_valp = ud_vals; ud_valp && *ud_valp; ud_valp++) {
                    char **rdns = NULL;
                    char **rdnsp = NULL;
                    char *valueptr = NULL;
                    char *endvalue = NULL;
                    int isentrydn = 0;

                    /* Also check RDN contains double quoted values */
                    if (strcasecmp(a->a_type, "entrydn")) {
                        /* except entrydn */
                        workdn = slapi_ch_strdup(*ud_valp);
                        isentrydn = 0;
                    } else {
                        /* entrydn: Get Slapi DN */
                        sdn = slapi_entry_get_sdn(e);
                        workdn = slapi_ch_strdup(slapi_sdn_get_dn(sdn));
                        isentrydn = 1;
                    }
                    rdns = slapi_ldap_explode_dn(workdn, 0);
                    skipit = 0;
                    for (rdnsp = rdns; rdnsp && *rdnsp; rdnsp++) {
                        valueptr = PL_strchr(*rdnsp, '=');
                        if (NULL == valueptr) {
                            skipit = 1;
                            break;
                        }
                        endvalue = *rdnsp + strlen(*rdnsp) - 1;
                        while ((++valueptr <= endvalue) &&
                               ((' ' == *valueptr) || ('\t' == *valueptr)))
                            ;
                        if (0 == strlen(valueptr)) {
                            skipit = 1;
                            break;
                        }
                        /* DN syntax value contains an RDN <type>="<value>" or
                         * '\\' in the value ?
                         * If yes, let's upgrade the dn format. */
                        if ((('"' == *valueptr) && ('"' == *endvalue)) ||
                            PL_strchr(valueptr, '\\')) {
                            do_dn_norm = 1;
                            dbmdb_upgradedn_add_to_list(&ud_list,
                                                  slapi_ch_strdup(a->a_type),
                                                  slapi_ch_strdup(*ud_valp),
                                                  isentrydn ? 0 : OLD_DN_NORMALIZE);
                            slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_upgradedn_producer",
                                          "%s: Found upgradedn candidate: %s (id %lu)\n",
                                          inst->inst_name, valueptr, (u_long)temp_id);
                            if (!entryrdn_get_switch() && isentrydn) {
                                /* entrydn format */
                                /*
                                 * In case entrydn is type="<DN>",<REST> or
                                 *                    type=<\D\N>,<REST>,
                                 * add the rdn value if it's not there.
                                 */
                                rc = slapi_entry_add_rdn_values(e);
                                if (rc) {
                                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_upgradedn_producer",
                                                  "%s: Failed to add rdn values to an entry: %s (id %lu)\n",
                                                  inst->inst_name, normdn, (u_long)temp_id);
                                    slapi_entry_free(e);
                                    e = NULL;
                                    continue;
                                }
                            }
                            break;
                        }
                        /*
                         * else if (the rdn contains multiple spaces)?
                         * if yes, they are reduced to one.
                         * SET HAVE_MULTI_SPACES???
                         */
                    } /* for (rdnsp = rdns; rdnsp && *rdnsp; rdnsp++) */
                    if (rdns) {
                        slapi_ldap_value_free(rdns);
                    } else {
                        skipit = 1;
                    }
                    if (skipit) {
                        break;
                    }
                    slapi_ch_free_string(&workdn);
                } /* for (ud_valp = ud_vals; ud_valp && *ud_valp; ud_valp++) */
                charray_free(ud_vals);
                ud_vals = NULL;
                if (skipit) {
                    slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_upgradedn_producer",
                                  "%s: Skipping an entry with a corrupted dn (syntax value): %s (id %lu)\n",
                                  inst->inst_name, workdn ? workdn : "unknown", (u_long)temp_id);
                    slapi_ch_free_string(&workdn);
                    dbmdb_upgradedn_free_list(&ud_list);
                    break;
                }
            } /* for (a = e->e_attrs; a; a = a->a_next)  */
            if (skipit) {
                dbmdb_upgradedn_free_list(&ud_list);
                slapi_entry_free(e);
                e = NULL;
                continue;
            }
        } /* end of if (chk_do_norm) */
        slapi_ch_free_string(&rdn);

        if (is_dryrun) {
            if (do_dn_norm) {
                info_state |= DN_NORM;
                /*
                 * If dryrun AND (found we need to do dn norm) AND
                 * (no need to check spaces),
                 * then we can quit here to return.
                 */
                if (!chk_dn_norm_sp) {
                    finished = 0; /* make it sure ... */
                    dbmdb_upgradedn_free_list(&ud_list);
                    slapi_entry_free(e);
                    e = NULL;
                    /* found upgrade dn candidates */
                    goto bail;
                }
            }
            if (do_dn_norm_sp) {
                info_state |= DN_NORM_SP;
            }
            /* We don't have to update dn syntax values. */
            dbmdb_upgradedn_free_list(&ud_list);
            slapi_entry_free(e);
            e = NULL;
            continue;
        }

        skipit = 0;
        for (ud_ptr = ud_list; ud_ptr; ud_ptr = ud_ptr->ud_next) {
            Slapi_Value *value = NULL;
            /* Move the current value to e_aux_attrs. */
            /* entryrdn is special since it does not have an attribute in db */
            if (0 == strcmp(ud_ptr->ud_type, LDBM_ENTRYRDN_STR)) {
                /* entrydn contains half normalized value in id2entry,
                   thus we have to replace it in id2entry.
                   The other DN syntax attribute values store
                   the originals.  They are taken care by the normalizer.
                 */
                a = slapi_attr_new();
                slapi_attr_init(a, ud_ptr->ud_type);
                value = slapi_value_new_string(ud_ptr->ud_value);
                slapi_attr_add_value(a, value);
                slapi_value_free(&value);
                attrlist_add(&e->e_aux_attrs, a);
            } else { /* except "entryrdn" */
                ud_attr = attrlist_find(e->e_attrs, ud_ptr->ud_type);
                if (ud_attr) {
                    /* We have to normalize the orignal string to generate
                       the key in the index.
                     */
                    a = attrlist_find(e->e_aux_attrs, ud_ptr->ud_type);
                    if (!a) {
                        a = slapi_attr_new();
                        slapi_attr_init(a, ud_ptr->ud_type);
                    } else {
                        a = attrlist_remove(&e->e_aux_attrs,
                                            ud_ptr->ud_type);
                    }
                    slapi_dn_normalize_case_original(ud_ptr->ud_value);
                    value = slapi_value_new_string(ud_ptr->ud_value);
                    slapi_attr_add_value(a, value);
                    slapi_value_free(&value);
                    attrlist_add(&e->e_aux_attrs, a);
                }
            }
        }
        dbmdb_upgradedn_free_list(&ud_list);

        ep = dbmdb_import_make_backentry(e, temp_id);
        if (!ep) {
            slapi_entry_free(e);
            e = NULL;
            goto error;
        }

        /* Add the newly case -normalized dn to entrydn in the e_attrs list. */
        add_update_entrydn_operational_attributes(ep);

        if (job->flags & FLAG_ABORT)
            goto error;

        /* Now we have this new entry, all decoded
         * Next thing we need to do is:
         * (1) see if the appropriate fifo location contains an
         *     entry which had been processed by the indexers.
         *     If so, proceed.
         *     If not, spin waiting for it to become free.
         * (2) free the old entry and store the new one there.
         * (3) Update the job progress indicators so the indexers
         *     can use the new entry.
         */
        idx = id % job->fifo.size;
        old_ep = job->fifo.item[idx].entry;
        if (old_ep) {
            /* for the slot to be recycled, it needs to be already absorbed
             * by the foreman (id >= ready_EID), and all the workers need to
             * be finished with it (refcount = 0).
             */
            while (((old_ep->ep_refcnt > 0) ||
                    (old_ep->ep_id >= job->ready_EID)) &&
                   (info->command != ABORT) && !(job->flags & FLAG_ABORT)) {
                info->state = WAITING;
                DS_Sleep(sleeptime);
            }
            if (job->flags & FLAG_ABORT)
                goto error;

            info->state = RUNNING;
            PR_ASSERT(old_ep == job->fifo.item[idx].entry);
            job->fifo.item[idx].entry = NULL;
            if (job->fifo.c_bsize > job->fifo.item[idx].esize)
                job->fifo.c_bsize -= job->fifo.item[idx].esize;
            else
                job->fifo.c_bsize = 0;
            backentry_free(&old_ep);
        }

        newesize = (slapi_entry_size(ep->ep_entry) + sizeof(struct backentry));
        if (dbmdb_import_fifo_validate_capacity_or_expand(job, newesize) == 1) {
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer", "Skipping entry \"%s\"",
                              slapi_entry_get_dn(e));
            import_log_notice(job, SLAPI_LOG_NOTICE, "dbmdb_upgradedn_producer",
                              "REASON: entry too large (%lu bytes) for "
                              "the buffer size (%lu bytes), and we were UNABLE to expand buffer.",
                              (long unsigned int)newesize, (long unsigned int)job->fifo.bsize);
            backentry_free(&ep);
            job->skipped++;
            continue;
        }
        /* Now check if fifo has enough space for the new entry */
        if ((job->fifo.c_bsize + newesize) > job->fifo.bsize) {
        }

        /* We have enough space */
        job->fifo.item[idx].filename = ID2ENTRY LDBM_FILENAME_SUFFIX;
        job->fifo.item[idx].line = curr_entry;
        job->fifo.item[idx].entry = ep;
        job->fifo.item[idx].bad = 0;
        job->fifo.item[idx].esize = newesize;

        /* Add the entry size to total fifo size */
        job->fifo.c_bsize += ep->ep_entry ? job->fifo.item[idx].esize : 0;

        /* Update the job to show our progress */
        job->lead_ID = id;
        if ((id - info->first_ID) <= job->fifo.size) {
            job->trailing_ID = info->first_ID;
        } else {
            job->trailing_ID = id - job->fifo.size;
        }

        /* Update our progress meter too */
        info->last_ID_processed = id;
        id++;
        if (job->flags & FLAG_ABORT)
            goto error;
        if (info->command == STOP) {
            finished = 1;
        }
    }
bail:
    info->state = FINISHED | info_state;
    goto done;

error:
    thread_abort(info);

done:
    dbmdb_close_cursor(&dbc, 1 /* Abort txn */);
    dbmdb_free_IDarray(&dn_norm_sp_conflicts);
    slapi_ch_free_string(&ecopy);
    if(data.mv_data!=entry_str){
        slapi_ch_free_string(&entry_str);
    }
    slapi_ch_free_string(&rdn);
    if (job->upgradefd) {
        fclose(job->upgradefd);
    }
    return ep;
#endif
}

/*
 * prepare entry callbacks are called from worker thread and
 *  in charge of transforming the producer data into a backentry
 * before it get processed to generates the indexes and stored in db
 */
struct backentry *
dbmdb_bulkimport_prepare_worker_entry(WorkerQueueData_t *wqelmnt)
{
    struct backentry *ep = wqelmnt->data;
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    Slapi_Attr *attr = NULL;

    /* encode the password */
    if (slapi_entry_attr_find(ep->ep_entry, "userpassword", &attr) == 0) {
        Slapi_Value **va = attr_get_present_values(attr);

        pw_encodevals((Slapi_Value **)va); /* jcm - had to cast away const */
    }

    /* if usn_value is available AND the entry does not have it, */
    if (job->usn_value && slapi_entry_attr_find(ep->ep_entry,
                                                SLAPI_ATTR_ENTRYUSN, &attr)) {
        slapi_entry_add_value(ep->ep_entry, SLAPI_ATTR_ENTRYUSN,
                              job->usn_value);
    }

    /* Now we have this new entry, all decoded
     * Is subtree-rename on? And is this a tombstone?
     * If so, need a special treatment */
    if (ep->ep_entry->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
        char *tombstone_rdn =
            slapi_ch_strdup(slapi_entry_get_dn_const(ep->ep_entry));
        if ((0 == PL_strncasecmp(tombstone_rdn, SLAPI_ATTR_UNIQUEID,
                                 sizeof(SLAPI_ATTR_UNIQUEID) - 1)) &&
            /* dn starts with "nsuniqueid=" */
            (NULL == PL_strstr(tombstone_rdn, RUV_STORAGE_ENTRY_UNIQUEID))) {
            /* and this is not an RUV */
            char *sepp = PL_strchr(tombstone_rdn, ',');
            /* dn looks like this:
             * nsuniqueid=042d8081-...-ca8fe9f7,uid=tuser,o=abc.com
             * create a new srdn for the original dn
             * uid=tuser,o=abc.com
             */
            if (sepp) {
                Slapi_RDN mysrdn = {0};
                if (slapi_rdn_init_all_dn(&mysrdn, sepp + 1)) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_bulk_import_queue",
                                  "Failed to convert DN %s to RDN\n", sepp + 1);
                    slapi_ch_free_string(&tombstone_rdn);
                    /* entry is released in the frontend on failure*/
                    backentry_clear_entry(ep);
                    backentry_free(&ep); /* release the backend wrapper */
                    pthread_mutex_unlock(&job->wire_lock);
                    return NULL;
                }
                sepp = PL_strchr(sepp + 1, ',');
                if (sepp) {
                    Slapi_RDN *srdn = slapi_entry_get_srdn(ep->ep_entry);
                    /* nsuniqueid=042d8081-...-ca8fe9f7,uid=tuser, */
                    /*                                           ^ */
                    *sepp = '\0';
                    slapi_rdn_replace_rdn(&mysrdn, tombstone_rdn);
                    slapi_rdn_done(srdn);
                    slapi_entry_set_srdn(ep->ep_entry, &mysrdn);
                    slapi_rdn_done(&mysrdn);
                }
            }
        }
        slapi_ch_free_string(&tombstone_rdn);
    }
    return ep;
}

/* foreman
 * now part of the worker thread - and responsible for adding operationnal attributes
 * and pushing the entry to the writing thread queue.
 */
int
process_foreman(backentry *ep, WorkerQueueData_t *wqelmnt)
{
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    /* Pseudo txn redirects database write towards import_txn_callback callback */
    PseudoTxn_t txn = init_pseudo_txn(job->writer_ctx);
    int ret = 0;

    PR_ASSERT(info != NULL);
    PR_ASSERT(inst != NULL);

    if (!(job->flags & FLAG_REINDEXING)) /* reindex reads data from id2entry */
    {
        /* insert into the id2entry index
         * (that isn't really an index -- it's the storehouse of the entries
         * themselves.)
         */
        /* id2entry_add_ext replaces an entry if it already exists.
         * therefore, the Entry ID stays the same.
         */
        ret = id2entry_add_ext(be, ep, (dbi_txn_t*)&txn, job->encrypt, NULL);
        if (ret) {
            /* DB_RUNRECOVERY usually occurs if disk fills */
            if (LDBM_OS_ERR_IS_DISKFULL(ret)) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                  "OUT OF SPACE ON DISK or FILE TOO LARGE -- "
                                  "Could not store the entry ending at line %d of file \"%s\"",
                                  wqelmnt->lineno, wqelmnt->filename);
            } else if (ret == MDB_PANIC) {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                  "(LARGEFILE SUPPORT NOT ENABLED? OUT OF SPACE ON DISK?) -- "
                                  "Could not store the entry starting at line %d of file \"%s\"",
                                  wqelmnt->lineno, wqelmnt->filename);
            } else {
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_foreman",
                                  "Could not store the entry starting at line %d of file \"%s\" -- error %d",
                                  wqelmnt->lineno, wqelmnt->filename, ret);
            }
            return -1;
        }
        CACHE_REMOVE(&inst->inst_cache, ep);
    }
    return 0;
}

static int
err(const char *func, WorkerQueueData_t *wqelmnt, const Slapi_DN *sdn, char *msg)
{
    if (wqelmnt->filename) {
        slapi_log_err(SLAPI_LOG_WARNING, func,
                "Entry ignored because %s while importing entry at line %d from ldif file %s with DN: %s\n",
                msg, wqelmnt->lineno, wqelmnt->filename, slapi_sdn_get_dn(sdn));
    }
    return -1;
}

static MdbIndexInfo_t*
look4indexinfo(ImportCtx_t *ctx, const char *attrname)
{
    MdbIndexInfo_t searched_mii = {0};
    searched_mii.name = (char*) attrname;
    return (MdbIndexInfo_t *)avl_find(ctx->indexes, &searched_mii, cmp_mii);
}

/* Prepare key and data for updating parentid or ancestorid indexes */
static void
prepare_ids(WriterQueueData_t *wqd, ID idkey, const ID *iddata)
{
    sprintf(wqd->key.mv_data, "=%d", idkey);
    wqd->key.mv_size = strlen(wqd->key.mv_data)+1;
    wqd->data.mv_size = sizeof(ID);
    wqd->data.mv_data = (void*)iddata;
}

static ProcessEntryAction_t
rename_duplicate_entry(const char *funcname, ImportJob *job, backentry *ep)
{
    /*
     * Duplicated DN is detected.
     *
     * Rename <DN> to nsuniqueid=<uuid>+<DN>
     * E.g., uid=tuser,dc=example,dc=com ==>
     * nsuniqueid=<uuid>+uid=tuser,dc=example,dc=com
     *
     * Note: FLAG_UPGRADEDNFORMAT only.
     */
    Slapi_Attr *orig_entrydn = NULL;
    Slapi_Attr *new_entrydn = NULL;
    Slapi_Attr *nsuniqueid = NULL;
    const char *uuidstr = NULL;
    char *new_dn = NULL;
    char *orig_dn = slapi_ch_strdup(slapi_entry_get_dn(ep->ep_entry));

    nsuniqueid = attrlist_find(ep->ep_entry->e_attrs, "nsuniqueid");
    if (nsuniqueid) {
        Slapi_Value *uival = NULL;
        slapi_attr_first_value(nsuniqueid, &uival);
        uuidstr = slapi_value_get_string(uival);
    } else {
        import_log_notice(job, SLAPI_LOG_ERR, (char*)funcname,
                          "Failed to get nsUniqueId of the duplicated entry %s; Entry ID: %d",
                          orig_dn, ep->ep_id);
        slapi_ch_free_string(&orig_dn);
        return PEA_SKIP;
    }
    new_entrydn = slapi_attr_new();
    new_dn = slapi_create_dn_string("nsuniqueid=%s+%s",
                                    uuidstr, orig_dn);
    /* releasing original dn */
    slapi_sdn_done(&ep->ep_entry->e_sdn);
    /* setting new dn; pass in */
    slapi_sdn_init_dn_passin(&ep->ep_entry->e_sdn, new_dn);

    /* Replacing entrydn attribute value */
    orig_entrydn = attrlist_remove(&ep->ep_entry->e_attrs,
                                   "entrydn");
    /* released in forman_do_entrydn */
    attrlist_add(&ep->ep_entry->e_aux_attrs, orig_entrydn);

    /* Setting new entrydn attribute value */
    slapi_attr_init(new_entrydn, "entrydn");
    valueset_add_string(new_entrydn, &new_entrydn->a_present_values,
                        /* new_dn: duped in valueset_add_string */
                        (const char *)new_dn,
                        CSN_TYPE_UNKNOWN, NULL);
    attrlist_add(&ep->ep_entry->e_attrs, new_entrydn);

    import_log_notice(job, SLAPI_LOG_WARNING, (char*)funcname,
                      "Duplicated entry %s is renamed to %s; Entry ID: %d",
                      orig_dn, new_dn, ep->ep_id);
    slapi_ch_free_string(&orig_dn);
    return PEA_RENAME;
}

static ProcessEntryAction_t
dbmdb_import_handle_duplicate_dn(const char *funcname, WorkerQueueData_t *wqelmnt, const Slapi_DN *sdn, backentry *ep, ID parentid, ID altID)
{
    ImportJob *job = wqelmnt->winfo.job;
    char *msg = NULL;

    /* Todo if bulkimport turn the entry to tombstone. Then fix the rdn cache and returns 0 */

    if (job->flags & FLAG_UPGRADEDNFORMAT) {
        return rename_duplicate_entry(funcname, job, ep);
    }

    msg = slapi_ch_smprintf("Duplicated DN detected: \"%s\": Entry ID: (%d) and (%d)",
                            slapi_sdn_get_udn(sdn), altID, ep->ep_id);
    err(funcname, wqelmnt, sdn, msg);
    slapi_ch_free_string(&msg);
    return PEA_DUPDN;
}

/*
 * dbmdb_add_op_attrs - add the parentid, entryid, dncomp,
 * and entrydn operational attributes to an entry.
 * Also---new improved washes whiter than white version
 * now removes any bogus operational attributes you're not
 * allowed to specify yourself on entries.
 * Currenty the list of these is: numSubordinates, hasSubordinates
 */
void
dbmdb_add_op_attrs(ImportJob *job, struct backentry *ep, ID pid)
{
    ImportCtx_t *ctx = job->writer_ctx;

    /*
     * add the parentid and entryid operational attributes
     */

    /* Get rid of attributes you're not allowed to specify yourself */
    slapi_entry_delete_values(ep->ep_entry, hassubordinates, NULL);
    slapi_entry_delete_values(ep->ep_entry, numsubordinates, NULL);

    /* Upgrade DN format only */
    /* Set current parentid to e_aux_attrs to remove it from the index file. */
    if (ctx->role == IM_UPGRADE) {
        Slapi_Attr *pid_attr = NULL;
        pid_attr = attrlist_remove(&ep->ep_entry->e_attrs, "parentid");
        if (pid_attr) {
            attrlist_add(&ep->ep_entry->e_aux_attrs, pid_attr);
        }
    }

    /* Add the entryid, parentid and entrydn operational attributes */
    /* Note: This function is provided by the Add code */
    add_update_entry_operational_attributes(ep, pid);
}

/* Store RUV in entryrdn when RUV entry is before the suffix one */
void
dbmdb_store_ruv_in_entryrdn(WorkerQueueData_t *wqelmnt, ID idruv, ID idsuffix, const char *nrdn, const char *rdn)
{
    const char *ruvrdn = SLAPI_ATTR_UNIQUEID "=" RUV_STORAGE_ENTRY_UNIQUEID;
    int ruvrdnlen = strlen(ruvrdn);
    WriterQueueData_t wqd = {0};
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;

    /* It is time to handle the tombstone previously seen */
    rdncache_add_elem(ctx->rdncache, wqelmnt, idruv, idsuffix, ruvrdnlen+1, ruvrdn, ruvrdnlen+1, ruvrdn);

    wqd.dbi = ctx->entryrdn->dbi;
    wqd.key.mv_data = slapi_ch_smprintf("C%d", idsuffix);
    wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
    wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, idruv, ruvrdn, ruvrdn);
    dbmdb_import_writeq_push(ctx, &wqd);
    slapi_ch_free(&wqd.key.mv_data);
    slapi_ch_free(&wqd.data.mv_data);

    wqd.key.mv_data = slapi_ch_smprintf("P%d", idruv);
    wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
    wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, idsuffix, nrdn, rdn);
    dbmdb_import_writeq_push(ctx, &wqd);
    slapi_ch_free(&wqd.key.mv_data);
    slapi_ch_free(&wqd.data.mv_data);

    wqd.key.mv_data = slapi_ch_smprintf("%d", idruv);
    wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
    wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, idruv, ruvrdn, ruvrdn);
    dbmdb_import_writeq_push(ctx, &wqd);
    slapi_ch_free(&wqd.key.mv_data);
    slapi_ch_free(&wqd.data.mv_data);
}


/*
 * Compute entry rdn and parentid and store entyrdn related keys:
 *       id --> id nrdn rdn
 *       'C'+parentid --> id nrdn rdn
 *       'P'+id --> parentid parentnrdn parentrdn
 *
 * Note: process_entryrdn uses the entry dn
 *       process_entryrdn_byrdn uses the entry rdn and parentid
 */
static ProcessEntryAction_t
process_entryrdn_byrdn(backentry *ep, WorkerQueueData_t *wqelmnt)
{
    int istombstone = slapi_entry_flag_is_set(ep->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    Slapi_RDN srdn = {0};
    char *nrdn = NULL;
    char *rdn = NULL;
    RDNcacheElem_t *elem = NULL;
    ID pid = 0;
    WriterQueueData_t wqd = {0};
    char key_id[10];
    int isruv = 0;

    rdn = slapi_ch_strdup(slapi_entry_get_rdn_const(ep->ep_entry));
    if (!rdn) {
        slapi_log_err(SLAPI_LOG_ERR, "process_entryrdn_by_rdn", "Unable to get rdn from entry with id %d\n", ep->ep_id);
        return PEA_ABORT;
    }
    pid = slapi_entry_attr_get_int(ep->ep_entry, LDBM_PARENTID_STR);
    isruv = (istombstone && PL_strstr(rdn, RUV_STORAGE_ENTRY_UNIQUEID));
    if (isruv && ctx->idsuffix == 0) {
        /* Special case:  if entry is the RUV and is before the suffix, postpone its
         * handling after the suffix get created.
         * (I prefer to postpone because I am not sure that suffix id is always
         *  current id + 1 (I suspects that tombstone entry may be before the
         *  suffix - typically if suffix entry got deleted then recreated)
         */
        ctx->idruv = ep->ep_id;
        slapi_ch_free_string(&rdn);
        dbmdb_add_op_attrs(job, ep, pid);
        return PEA_OK;
    }
    srdn.rdn = rdn;
    nrdn = (char*) slapi_rdn_get_nrdn(&srdn);
slapi_log_error(SLAPI_LOG_ERR, "process_entryrdn_by_rdn", "rdn=%s nrdn=%s pid=%d\n", rdn, nrdn, pid);

    /* Add entry in rdn cache */
    rdncache_add_elem(ctx->rdncache, wqelmnt, ep->ep_id, pid, strlen(nrdn)+1, nrdn, strlen(rdn)+1, rdn);
    if (pid == 0) {
        /* Special case: ep is the suffix entry */
        /*  lets only add nrdn -> id nrdn rdn  */
        wqd.dbi = ctx->entryrdn->dbi;
        wqd.key.mv_data = slapi_ch_strdup(rdn);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, ep->ep_id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.data.mv_data);
        dbmdb_add_op_attrs(job, ep, pid);
        ctx->idsuffix = ep->ep_id;
        if (ctx->idruv) {
            /* It is time to store the RUV in entryrdn.db */
            dbmdb_store_ruv_in_entryrdn(wqelmnt, ctx->idruv, ep->ep_id, nrdn, rdn);
        }
        slapi_rdn_done(&srdn);
        return PEA_OK;
    } else {
        /* Standard case: ep is not the suffix entry */
        /* Standard case: add the entry, parent and child records */
        elem = rdncache_id_lookup(ctx->rdncache, wqelmnt, pid);
        if (!elem) {
            slapi_log_err(SLAPI_LOG_ERR, "process_entryrdn_by_rdn", "Unable to get parent id %d for entry id %d\n", pid, ep->ep_id);
            slapi_rdn_done(&srdn);
            return PEA_ABORT;
        }
        wqd.dbi = ctx->entryrdn->dbi;
        wqd.key.mv_data = slapi_ch_smprintf("C%d", pid);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, ep->ep_id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.key.mv_data);
        slapi_ch_free(&wqd.data.mv_data);

        wqd.key.mv_data = slapi_ch_smprintf("P%d", ep->ep_id);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, pid, elem->nrdn, ELEMRDN(elem));
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.key.mv_data);
        slapi_ch_free(&wqd.data.mv_data);

        wqd.key.mv_data = slapi_ch_smprintf("%d", ep->ep_id);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, ep->ep_id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.key.mv_data);
        slapi_ch_free(&wqd.data.mv_data);

        wqd.key.mv_data = key_id;
        /* Update parentid */
        wqd.dbi = ctx->parentid->dbi;
        prepare_ids(&wqd, pid, &ep->ep_id);
        dbmdb_import_writeq_push(ctx, &wqd);
        dbmdb_add_op_attrs(job, ep, pid);  /* Before loosing the pid */

        /* Update ancestorid */
        wqd.dbi = ctx->ancestorid->dbi;
        while (elem) {
            prepare_ids(&wqd, elem->eid, &ep->ep_id);
            dbmdb_import_writeq_push(ctx, &wqd);
            pid = elem->pid;
            rdncache_elem_release(&elem);
            elem = rdncache_id_lookup(ctx->rdncache, wqelmnt, pid);
       }
    }

    slapi_rdn_done(&srdn);
    return PEA_OK;
}

static ProcessEntryAction_t
process_entryrdn(backentry *ep, WorkerQueueData_t *wqelmnt)
{
    int istombstone = slapi_entry_flag_is_set(ep->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
    const Slapi_DN *sdn = slapi_entry_get_sdn(ep->ep_entry);
    ImportJob *job = wqelmnt->winfo.job;
    ImportCtx_t *ctx = job->writer_ctx;
    Slapi_RDN srdn;
    ID *ancestors = NULL;
    ID pid = 0;
    int rc, idx;
    int isruv = 0;
    WriterQueueData_t wqd;
    const char *rdn = NULL;
    const char *nrdn = NULL;
    RDNcacheElem_t *child = NULL;
    RDNcacheElem_t *elem = NULL;
    char key_id[10];

    if (!(ctx->entryrdn)) {
        /* the indexes handled by this function are not being reindexed */
        return PEA_OK;
    }

    if (ctx->role == IM_INDEX || ctx->role == IM_UPGRADE) {
        /* No valid entry dn ==> should be in reindex/upgrade case and have rdn and parentid within the entry */
        return process_entryrdn_byrdn(ep, wqelmnt);
    }

    isruv = (istombstone && PL_strstr(slapi_sdn_get_dn(sdn), RUV_STORAGE_ENTRY_UNIQUEID));
    rc = slapi_rdn_init_all_sdn_ext(&srdn, sdn, istombstone && !isruv);
    if (rc) {
        /* Invalid DN:
         * should not be in IM_BULKIMPORT mode because supplier would send valid DN
         * So wqelmnt->filename should be set (but lets double check
         */
        err(__FUNCTION__, wqelmnt, sdn, "Badly formatted DN");
        return PEA_SKIP;
    }

    /* normalize the rdns and get get suffix index */
    idx = slapi_rdn_get_last_ext(&srdn, &rdn, FLAG_ALL_NRDNS);
    if (isruv && ctx->idsuffix == 0) {
        /* Special case:  if entry is the RUV and is before the suffix, postpone its
         * handling after the suffix get created.
         * (I prefer to postpone because I am not sure that suffix id is always
         *  current id + 1 (I suspects that tombstone entry may be before the
         *  suffix - typically if suffix entry got deleted then recreated)
         */
        ctx->idruv = ep->ep_id;
        slapi_rdn_done(&srdn);
        /* We do not yet know the parentid so do not add it.
         * If this trick cause trouble we will have to replace the RUV parentid once
         * the export is finished.
         * or postpone the whole entry processing until suffix is added.
         */
        dbmdb_add_op_attrs(job, ep, 0);
        return PEA_OK;
    }
    ancestors = (ID*) slapi_ch_calloc(idx+1, sizeof (ID));
    ancestors[idx] = 0;
    /* Then walks up the ancestors one by one */
    while (idx > 0) {
        /* Lookup for the child */
        rdncache_elem_release(&child);
        child = rdncache_rdn_lookup(ctx->rdncache, wqelmnt, pid, srdn.all_nrdns[idx]);
        if (!child) {
            slapi_log_err(SLAPI_LOG_ERR, "process_entryrdn",
                          "Skipping entry \"%s\" which has no parent.\n",
                          slapi_sdn_get_udn(sdn));
            err(__FUNCTION__, wqelmnt, sdn, "No parent entry ");
            slapi_ch_free((void**)&ancestors);
            slapi_rdn_done(&srdn);
            return PEA_SKIP;
        }
        if (child->pid != pid) {
            slapi_log_err(SLAPI_LOG_ERR, "process_entryrdn",
                "[%d]:  thread %s wrong ancestor found looking for pid: %d rdn: %s but found pid: %d rdn: %s \n",
                __LINE__, wqelmnt->winfo.name, pid, srdn.all_nrdns[idx], child->pid, child->nrdn);
            PR_ASSERT(child->pid == pid);
        }
        /* And gets its id */
        pid = child->eid;
        ancestors[idx-1] = pid;
        idx--;
    }

    /* Time to update entryrdn index and cache */
    nrdn = srdn.all_nrdns[0];
    rdn = srdn.all_rdns[0];
    elem = rdncache_add_elem(ctx->rdncache, wqelmnt, ep->ep_id, pid, strlen(nrdn)+1, nrdn, strlen(rdn)+1, rdn);
    if (elem->eid != ep->ep_id) {
        /* Another entry with same DN was found.
         * (it could be because we are importing online export and replication
         *  added the entry while export was taking place. )
         *  In this case we should turn the entry as tombstone and let URP resolve
         *  the issue when the add will be replayed by replication
         */
        rc = dbmdb_import_handle_duplicate_dn(__FUNCTION__, wqelmnt, sdn, ep, pid, elem->eid);
        rdncache_elem_release(&elem);
        switch (rc) {
            case PEA_TOMBSTONE:
            case PEA_DUPDN:
            case PEA_SKIP:
                slapi_ch_free((void**)&ancestors);
                rdncache_elem_release(&child);
                slapi_rdn_done(&srdn);
                return rc;
            case PEA_RENAME:
                rdn = slapi_entry_get_rdn_const(ep->ep_entry);
                nrdn = slapi_entry_get_nrdn_const(ep->ep_entry);
                elem = rdncache_add_elem(ctx->rdncache, wqelmnt, ep->ep_id, pid, strlen(nrdn)+1, nrdn, strlen(rdn)+1, rdn);
                if (elem->eid != ep->ep_id) {
                    err(__FUNCTION__, wqelmnt, sdn, "Failed to rename entry");
                    slapi_ch_free((void**)&ancestors);
                    rdncache_elem_release(&child);
                    slapi_rdn_done(&srdn);
                    return PEA_DUPDN;
                }
                break;    /* Continue with new rdn */
        }
    }
    if (pid == 0) {
        /* Special case: ep is the suffix entry */
        /*  lets only add nrdn -> id nrdn rdn  */
        ctx->idsuffix = ep->ep_id;
        wqd.dbi = ctx->entryrdn->dbi;
        wqd.key.mv_data = (void*)nrdn;
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, ep->ep_id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.data.mv_data);
        if (ctx->idruv) {
            dbmdb_store_ruv_in_entryrdn(wqelmnt, ctx->idruv, ep->ep_id, nrdn, rdn);
        }
    } else {
        /* Standard case: add the entry, parent and child records */
        wqd.dbi = ctx->entryrdn->dbi;
        wqd.key.mv_data = slapi_ch_smprintf("C%d", pid);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, ep->ep_id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.key.mv_data);
        slapi_ch_free(&wqd.data.mv_data);

        wqd.key.mv_data = slapi_ch_smprintf("P%d", ep->ep_id);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, pid, child->nrdn, ELEMRDN(child));
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.key.mv_data);
        slapi_ch_free(&wqd.data.mv_data);

        wqd.key.mv_data = slapi_ch_smprintf("%d", ep->ep_id);
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(job->inst->inst_be, &wqd.data.mv_size, ep->ep_id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.key.mv_data);
        slapi_ch_free(&wqd.data.mv_data);
    }

    /* Update the entry with entryid, entryrdn and parentid */
    dbmdb_add_op_attrs(job, ep, pid);

    if (!istombstone) {
        /* Time to update parentid index */
        wqd.key.mv_data = key_id;
        if (ancestors[0]) {
            wqd.dbi = ctx->parentid->dbi;
            prepare_ids(&wqd, pid, &ep->ep_id);
            dbmdb_import_writeq_push(ctx, &wqd);
        }
        /* And ancestorid index */
        while (ancestors[idx]) {
            wqd.dbi = ctx->ancestorid->dbi;
            prepare_ids(&wqd, ancestors[idx], &ep->ep_id);
            dbmdb_import_writeq_push(ctx, &wqd);
            idx++;
        }
    }

    /* Note: wqd fields get freed by the writer */
    slapi_ch_free((void**)&ancestors);
    rdncache_elem_release(&child);
    rdncache_elem_release(&elem);
    slapi_rdn_done(&srdn);
    return PEA_OK;
}


/*
 * Note: the index_addordel functions are poorly designed form lmdb
 * Should probably rewrite them to separate the index keys/data computation from the actual
 *  db opeartions ...
 * i.e: new_index_addordel_values_sv(be, type, vals, evals, id, flags, int (*action_fn)(ctx, flags, key, data), void *ctx)
 *  ( flags in callback contains at least a flag telling whether we should add or remove the value) )
 *
 * Such a change would:
 *   eliminate the ugly pseudo_txn trick.
 *   avoid to reopen the dbi (that involve a lock and a lookup in table) at each call
 *   while we aleady have it in lmdb case.
 */
static void
process_regular_index(backentry *ep, ImportWorkerInfo *info)
{
    int is_tombstone = slapi_entry_flag_is_set(ep->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    MdbIndexInfo_t *mii = NULL;
    Slapi_Attr *attr = NULL;
    char *attrname = NULL;
    PseudoTxn_t txn = init_pseudo_txn(ctx);

    for (slapi_entry_first_attr(ep->ep_entry, &attr); attr; slapi_entry_next_attr(ep->ep_entry, attr, &attr)) {
        Slapi_Value val = {0};
        Slapi_Value *vals[2] = {&val, 0};
        Slapi_Value **svals = vals;
        char tomb_csnstr[CSN_STRSIZE];

        int ret = 0;

        slapi_attr_get_type(attr, &attrname);
        mii = look4indexinfo(ctx, attrname);
        if (!mii || (is_tombstone && !(mii->flags & MII_TOMBSTONE))) {
            continue;
        }
        if (!mii || (mii->flags & MII_SKIP)) {
            /* These indexes are either already handled (i.e: in process_entryrdn)
             * or will be handled later on (i.e numsubordinates
             */
            continue;
        }
        if (job->flags & FLAG_ABORT) {
            break;
        }
        if (valueset_isempty(&(attr->a_present_values))) {
            continue;
        }
        /*
         * Lets handle indexes based on special values
         */
        if (is_tombstone && (mii->flags & MII_TOMBSTONE_CSN)) {
            const CSN *tomb_csn = entry_get_deletion_csn(ep->ep_entry);
            if (tomb_csn) {
                csn_as_string(tomb_csn, PR_FALSE, tomb_csnstr);
                slapi_value_set_string_passin(&val, tomb_csnstr);
            } else {
                /* No deletion csn in objectclass so lets use the nstombstonecsn value */
                svals = attr_get_present_values(attr);
            }
        } else if (is_tombstone && (mii->flags & MII_OBJECTCLASS)) {
            slapi_value_set_string_passin(&val, SLAPI_ATTR_VALUE_TOMBSTONE);
        } else {
            /* Regular case */
            svals = attr_get_present_values(attr);
        }
        ret = index_addordel_values_sv(be, mii->name,
                                           svals, NULL, ep->ep_id,
                                           BE_INDEX_ADD | (job->encrypt ? 0 : BE_INDEX_DONT_ENCRYPT),
                                           (dbi_txn_t*)&txn);
        if (0 != ret) {
            /* Something went wrong, eg disk filled up */
            slapi_log_err(SLAPI_LOG_ERR, "process_regular_index",
                    "Import %s thread aborted after index_addordel_values_ext_sv failure on attribute %s.\n",
                    info->name, attrname);
            thread_abort(info);
            return;
        }
    }
}

const char *
attr_in_list(const char *search, char **list)
{
    while (*list) {
        if (strcasecmp(search, *list++) == 0) {
            return *--list;
        }
    }
    return NULL;
}

static void
process_vlv_index(backentry *ep, ImportWorkerInfo *info)
{
    int is_tombstone = slapi_entry_flag_is_set(ep->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    ldbm_instance *inst = job->inst;
    backend *be = inst->inst_be;
    PseudoTxn_t txn = init_pseudo_txn(ctx);
    struct vlvSearch *ps;
    int ret = 0;

    if (is_tombstone) {
        return;
    }
    slapi_rwlock_rdlock(be->vlvSearchList_lock);
    for (ps = (struct vlvSearch *)be->vlvSearchList; ps != NULL; ps = ps->vlv_next) {
        struct vlvIndex *vlv_index = ps->vlv_index;
        Slapi_PBlock *pb = slapi_pblock_new();
        slapi_pblock_set(pb, SLAPI_BACKEND, be);
        if (vlv_index && (ctx->indexAttrs==NULL || attr_in_list(vlv_index->vlv_name, ctx->indexAttrs))) {
            ret = vlv_update_index(vlv_index, (dbi_txn_t*)&txn, inst->inst_li, pb, NULL, ep);
        }
        if (0 != ret) {
            /* Something went wrong, eg disk filled up */
            slapi_log_err(SLAPI_LOG_ERR, "process_regular_index", "index_addordel_values_ext_sv failed.\n");
            thread_abort(info);
            break;
        }
        slapi_pblock_destroy(pb);
    }
    slapi_rwlock_unlock(be->vlvSearchList_lock);
}

static int
import_txn_callback(backend *be, back_txn_action flags, dbi_db_t *db, dbi_val_t *key, dbi_val_t *data, back_txn *txn)
{
    PseudoTxn_t *t = (PseudoTxn_t*)txn;
    WriterQueueData_t wqd;

    wqd.dbi = (dbmdb_dbi_t *)db;
    set_data(&wqd.key, key);
    set_data(&wqd.data, data);
    /* Just keep the id field within the index_update_t struct */
    if (wqd.data.mv_size == sizeof (index_update_t)) {
        wqd.data.mv_size = sizeof (ID);
    }
    dbmdb_import_writeq_push(t->ctx, &wqd);
    return 0;
}

static PseudoTxn_t
init_pseudo_txn(ImportCtx_t *ctx)
{
    PseudoTxn_t t;
    t.txn.back_txn_txn = (dbi_txn_t *) 0xBadCafef;   /* Make sure the txn is not used */
    t.txn.back_special_handling_fn = import_txn_callback;
    t.ctx = ctx;
    return t;
}


/* worker thread:
 * given an attribute, this worker plows through the entry FIFO, building
 * up the attribute index.
 */
void
dbmdb_import_worker(void *param)
{
    WorkerQueueData_t *wqelmnt = (WorkerQueueData_t*)param;
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    backentry *ep = NULL;
    PRIntervalTime sleeptime;
    ID id = info->first_ID;

    PR_ASSERT(NULL != info);
    PR_ASSERT(NULL != job->inst);


    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    info->state = RUNNING;
    info->last_ID_processed = id;

    while (!info_is_finished(info)) {
        while ((info->command == PAUSE) && !info_is_finished(info)) {
            /* Check to see if we've been told to stop */
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        info->state = RUNNING;

        /* Wait until data get queued */
        if (wqelmnt->wait_id == 0) {
            pthread_mutex_lock(&ctx->workerq.mutex);
            while (wqelmnt->wait_id == 0 && !info_is_finished(info) && ctx->producer.state != FINISHED) {
                safe_cond_wait(&ctx->workerq.cv, &ctx->workerq.mutex);
            }
            pthread_mutex_unlock(&ctx->workerq.mutex);
        }
        if (wqelmnt->wait_id == 0) {
             break;
        }

        wqelmnt->count++;
        /* Format the backentry from the queued data */
        ep = ctx->prepare_worker_entry_fn(wqelmnt);
        if (!ep) {
            /* skipped counter is increased (or not in some cases) by the callback */
            wqelmnt->wait_id = 0;
            continue;
        }
        if (info_is_finished(info)) {
             break;
        }
        switch (process_entryrdn(ep, wqelmnt)) {
            case PEA_OK:
            case PEA_RENAME:
            case PEA_TOMBSTONE:
                break;
            case PEA_ABORT:
                thread_abort(info);
                backentry_free(&ep);
                wqelmnt->wait_id = 0;
                continue;
            case PEA_DUPDN:
                ctx->dupdn++;
                backentry_free(&ep);
                job->skipped++;
                wqelmnt->wait_id = 0;
                continue;
            case PEA_SKIP:
                /* Parent entry does not exists or Invalid DN */
                backentry_free(&ep);
                job->skipped++;
                wqelmnt->wait_id = 0;
                continue;
        }
        /* At this point, entry rdn is stored in cache (and maybe in dbi) so:
         * - lets signal the producer that it can enqueue a new item.
         * - lets signal the other worker threads that parent id may
         *  be available.
         */
        pthread_mutex_lock(&ctx->workerq.mutex);
        wqelmnt->wait_id = 0;
        pthread_cond_broadcast(&ctx->workerq.cv);
        pthread_cond_broadcast(&ctx->rdncache->condvar);
        pthread_mutex_unlock(&ctx->workerq.mutex);

        if (process_foreman(ep, wqelmnt)) {
            backentry_free(&ep);
            thread_abort(info);
            break;
        }

        if (!info_is_finished(info)) {
            process_regular_index(ep, info);
        }
        if (!info_is_finished(info)) {
            process_vlv_index(ep, info);
        }
        backentry_free(&ep);
    }
    info_set_state(info);
}

static int
cmp_mii(caddr_t data1, caddr_t data2)
{
    static char conv[256];
    MdbIndexInfo_t *v1 = (MdbIndexInfo_t*)data1;
    MdbIndexInfo_t *v2 = (MdbIndexInfo_t*)data2;
    unsigned char *n1 = (unsigned char *)(v1->name);
    unsigned char *n2 = (unsigned char *)(v2->name);
    int i;

    if (conv[1] == 0) {
        /* initialize conv table */
        memset(conv, '?', sizeof conv);
        for (i='0'; i<='9'; i++) {
            conv[i] = i;
        }
        for (i='a'; i<='z'; i++) {
            conv[i-'a'+'A'] = i;
            conv[i] = i;
        }
        conv['-'] = '-';
        conv[0] = 0;
        conv[';'] = 0;
    }
    while (conv[*n1] == conv[*n2] && conv[*n1] != 0) {
        n1++;
        n2++;
    }
    return conv[*n1] - conv[*n2];
}

/* Create MdbIndexInfo_t for the naming attributes that are missing */
void
dbmdb_add_import_index(ImportCtx_t *ctx, const char *name, IndexInfo *ii)
{
    int dbi_flags = MDB_CREATE|MDB_MARK_DIRTY_DBI|MDB_OPEN_DIRTY_DBI|MDB_TRUNCATE_DBI;
    ImportJob *job = ctx->job;
    MdbIndexInfo_t *mii;
    static const struct {
        char *name;
        int flags;
        int offset_dbi;
    } *a, actions[] = {
        { LDBM_ENTRYRDN_STR, MII_SKIP | MII_NOATTR, offsetof(ImportCtx_t, entryrdn) },
        { LDBM_PARENTID_STR, MII_SKIP, offsetof(ImportCtx_t, parentid) },
        { LDBM_ANCESTORID_STR, MII_SKIP | MII_NOATTR, offsetof(ImportCtx_t, ancestorid) },
        { LDBM_ENTRYDN_STR, MII_SKIP | MII_NOATTR, 0 },
        { LDBM_NUMSUBORDINATES_STR, MII_SKIP, offsetof(ImportCtx_t, numsubordinates) },
        { SLAPI_ATTR_OBJECTCLASS, MII_TOMBSTONE | MII_OBJECTCLASS, 0 },
        { SLAPI_ATTR_TOMBSTONE_CSN, MII_TOMBSTONE | MII_TOMBSTONE_CSN, 0 },
        { SLAPI_ATTR_UNIQUEID, MII_TOMBSTONE, 0 },
        { SLAPI_ATTR_NSCP_ENTRYDN, MII_TOMBSTONE, 0 },
        { 0 }
    };

    if (name) {
        for (ii=job->index_list; ii && strcasecmp(ii->ai->ai_type, name); ii=ii->next);
    }
    PR_ASSERT(ii);  /* System indexes should always be in the list */

    mii = CALLOC(MdbIndexInfo_t);
    mii->name = (char*) slapi_utf8StrToLower((unsigned char*)(ii->ai->ai_type));
    mii->ai = ii->ai;
    for (a=actions; a->name && strcasecmp(mii->name, a->name); a++);
    mii->flags |= a->flags;
    if (a->offset_dbi) {
        *(MdbIndexInfo_t**)(((char*)ctx)+a->offset_dbi) = mii;
    }
    /* Following logs are needed as is by CI basic test */
    if (ctx->role == IM_INDEX) {
        /* Required by CI test */
        if (a->flags & MII_NOATTR) {
            slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index",
                          "%s: Indexing %s\n", job->inst->inst_name, mii->name);
        } else {
            if (job->task) {
                slapi_task_log_notice(job->task, "%s: Indexing attribute: %s", job->inst->inst_name, mii->name);
            }
            slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index",
                          "%s: Indexing attribute: %s\n", job->inst->inst_name, mii->name);
        }
    }

    dbmdb_open_dbi_from_filename(&mii->dbi, job->inst->inst_be, mii->name, NULL, dbi_flags);
    avl_insert(&ctx->indexes, mii, cmp_mii, NULL);
}

void
dbmdb_build_import_index_list(ImportCtx_t *ctx)
{
    ImportJob *job = ctx->job;
    IndexInfo *ii;

    if (ctx->role != IM_UPGRADE) {
            for (ii=job->index_list; ii; ii=ii->next) {
            if (ii->ai->ai_indexmask == INDEX_VLV) {
                continue;
            }
            /* Keep only the index in reindex  list */
            if (ctx->indexAttrs && !(attr_in_list(ii->ai->ai_type, ctx->indexAttrs))) {
                continue;
            }
            dbmdb_add_import_index(ctx, NULL, ii);
        }
    }

    /* If a naming attribute is present, make sure that all of the are rebuilt */
    if (ctx->entryrdn || ctx->parentid || ctx->ancestorid || ctx->role != IM_INDEX) {
        if (!ctx->entryrdn) {
            dbmdb_add_import_index(ctx, LDBM_ENTRYRDN_STR, NULL);
        }
        if (!ctx->parentid) {
            dbmdb_add_import_index(ctx, LDBM_PARENTID_STR, NULL);
        }
        if (!ctx->ancestorid) {
            dbmdb_add_import_index(ctx, LDBM_ANCESTORID_STR, NULL);
        }
    }
}

void
free_ii(MdbIndexInfo_t *ii)
{
    slapi_ch_free_string(&ii->name);
    slapi_ch_free((void**)&ii);
}

/*
 * backup index configuration
 * this function is called from dblayer_backup (ldbm2archive)
 * [547427] index config must not change between backup and restore
 */
#define DSE_INDEX_FILTER "(objectclass=nsIndex)"
#define DSE_INSTANCE_FILTER "(objectclass=nsBackendInstance)"
static int
dbmdb_dse_conf_backup_core(struct ldbminfo *li, char *dest_dir, char *file_name, char *filter)
{
    Slapi_PBlock *srch_pb = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry **ep = NULL;
    Slapi_Attr *attr = NULL;
    char *attr_name;
    char *filename = NULL;
    PRFileDesc *prfd = NULL;
    int rval = 0;
    int dlen = 0;
    PRInt32 prrval;
    char tmpbuf[BUFSIZ];
    char *tp = NULL;

    dlen = strlen(dest_dir);
    if (0 == dlen) {
        filename = file_name;
    } else {
        filename = slapi_ch_smprintf("%s/%s", dest_dir, file_name);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_dse_conf_backup_core",
                  "(%s): backup file %s\n", filter, filename);

    /* Open the file to write */
    if ((prfd = PR_Open(filename, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE,
                        SLAPD_DEFAULT_FILE_MODE)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                      "(%s): open %s failed: (%s)\n",
                      filter, filename, slapd_pr_strerror(PR_GetError()));
        rval = -1;
        goto out;
    }

    srch_pb = slapi_pblock_new();
    if (!srch_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                      "(%s): out of memory\n", filter);
        rval = -1;
        goto out;
    }

    slapi_search_internal_set_pb(srch_pb, li->li_plugin->plg_dn,
                                 LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(srch_pb);
    slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    for (ep = entries; ep != NULL && *ep != NULL; ep++) {
        int32_t l = strlen(slapi_entry_get_dn_const(*ep)) + 5 /* "dn: \n" */;
        slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_dse_conf_backup_core",
                      "dn: %s\n", slapi_entry_get_dn_const(*ep));

        if (l <= sizeof(tmpbuf))
            tp = tmpbuf;
        else
            tp = (char *)slapi_ch_malloc(l); /* should be very rare ... */
        sprintf(tp, "dn: %s\n", slapi_entry_get_dn_const(*ep));
        prrval = PR_Write(prfd, tp, l);
        if (prrval != l) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                          "(%s): write %" PRId32 " failed: %d (%s)\n",
                          filter, l, PR_GetError(), slapd_pr_strerror(PR_GetError()));
            rval = -1;
            if (l > sizeof(tmpbuf))
                slapi_ch_free_string(&tp);
            goto out;
        }
        if (l > sizeof(tmpbuf))
            slapi_ch_free_string(&tp);

        for (slapi_entry_first_attr(*ep, &attr); attr;
             slapi_entry_next_attr(*ep, attr, &attr)) {
            int i;
            Slapi_Value *sval = NULL;
            const struct berval *attr_val;
            int attr_name_len;

            slapi_attr_get_type(attr, &attr_name);
            /* numsubordinates should not be backed up */
            if (!strcasecmp(LDBM_NUMSUBORDINATES_STR, attr_name))
                continue;
            attr_name_len = strlen(attr_name);
            for (i = slapi_attr_first_value(attr, &sval); i != -1;
                 i = slapi_attr_next_value(attr, i, &sval)) {
                attr_val = slapi_value_get_berval(sval);
                l = strlen(attr_val->bv_val) + attr_name_len + 3; /* : \n" */
                slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_dse_conf_backup_core",
                              "%s: %s\n", attr_name, attr_val->bv_val);
                if (l <= sizeof(tmpbuf))
                    tp = tmpbuf;
                else
                    tp = (char *)slapi_ch_malloc(l);
                sprintf(tp, "%s: %s\n", attr_name, attr_val->bv_val);
                prrval = PR_Write(prfd, tp, l);
                if (prrval != l) {
                    slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                                  "(%s): write %" PRId32 " failed: %d (%s)\n",
                                  filter, l, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                    rval = -1;
                    if (l > sizeof(tmpbuf))
                        slapi_ch_free_string(&tp);
                    goto out;
                }
                if (l > sizeof(tmpbuf))
                    slapi_ch_free_string(&tp);
            }
        }
        if (ep != NULL && ep[1] != NULL) {
            prrval = PR_Write(prfd, "\n", 1);
            if (prrval != 1) {
                slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_backup_core",
                              "(%s): write %" PRId32 " failed: %d (%s)\n",
                              filter, l, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                rval = -1;
                goto out;
            }
        }
    }

out:
    if (srch_pb) {
        slapi_free_search_results_internal(srch_pb);
        slapi_pblock_destroy(srch_pb);
    }

    if (0 != dlen) {
        slapi_ch_free_string(&filename);
    }

    if (prfd) {
        prrval = PR_Close(prfd);
        if (PR_SUCCESS != prrval) {
            slapi_log_err(SLAPI_LOG_CRIT, "dbmdb_dse_conf_backup_core",
                          "Failed to back up dse indexes %d (%s)\n",
                          PR_GetError(), slapd_pr_strerror(PR_GetError()));
            rval = -1;
        }
    }

    return rval;
}

int
dbmdb_dse_conf_backup(struct ldbminfo *li, char *dest_dir)
{
    int rval = 0;
    rval = dbmdb_dse_conf_backup_core(li, dest_dir, DSE_INSTANCE, DSE_INSTANCE_FILTER);
    rval += dbmdb_dse_conf_backup_core(li, dest_dir, DSE_INDEX, DSE_INDEX_FILTER);
    return rval;
}

/*
 * read the backed up index configuration
 * this function is called from dblayer_restore (archive2ldbm)
 * these functions are placed here to borrow import_get_entry
 * [547427] index config must not change between backup and restore
 */
Slapi_Entry **
dbmdb_read_ldif_entries(struct ldbminfo *li, char *src_dir, char *file_name)
{
    int fd = -1;
    int curr_lineno = 0;
    Slapi_Entry **backup_entries = NULL;
    char *filename = NULL;
    char *estr = NULL;
    int max_entries = 0;
    int nb_entries = 0;
    ldif_context c;

    dbmdb_import_init_ldif(&c);
    filename = slapi_ch_smprintf("%s/%s", src_dir, file_name);
    if (PR_SUCCESS != PR_Access(filename, PR_ACCESS_READ_OK)) {
        slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_read_ldif_entries",
                      "Config backup file %s not found in backup\n",
                      file_name);
        goto out;
    }
    fd = dbmdb_open_huge_file(filename, O_RDONLY, 0);
    if (fd < 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_read_ldif_entries",
                      "Can't open config backup file: %s\n", filename);
        goto out;
    }

    while ((estr = dbmdb_import_get_entry(&c, fd, &curr_lineno))) {
        Slapi_Entry *e = slapi_str2entry(estr, 0);
        slapi_ch_free_string(&estr);
        if (!e) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_read_ldif_entries",
                          "Skipping bad LDIF entry ending line %d of file \"%s\"",
                          curr_lineno, filename);
            continue;
        }
        if (nb_entries+1 >= max_entries) { /* Reserve enough space to add the final NULL element */
            max_entries = max_entries ? 2 * max_entries : 256;
            backup_entries = (Slapi_Entry **)slapi_ch_realloc((char *)backup_entries, max_entries * sizeof(Slapi_Entry *));
        }
        backup_entries[nb_entries++] = e;
    }
    if (!backup_entries) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_read_ldif_entries",
                      "No entry found in backup config file \"%s\"",
                      filename);
        goto out;
    }
    backup_entries[nb_entries] = NULL;
out:
    slapi_ch_free_string(&filename);
    if (fd >= 0) {
        close(fd);
    }
    dbmdb_import_free_ldif(&c);

    return backup_entries;
}

/*
 * read the backed up index configuration
 * adjust them if the current configuration is different from it.
 * this function is called from dblayer_restore (archive2ldbm)
 * these functions are placed here to borrow import_get_entry
 * [547427] index config must not change between backup and restore
 */
int
dbmdb_dse_conf_verify_core(struct ldbminfo *li, char *src_dir, char *file_name, char *filter, int force_update, char *log_str)
{
    Slapi_Entry **backup_entries = dbmdb_read_ldif_entries(li, src_dir, file_name);
    Slapi_Entry **curr_entries = NULL;
    Slapi_Entry **bep = NULL;
    int rval = 0;

    if (!backup_entries) {
        /* Error is already logged */
        return -1;
    }

    char * search_scope = slapi_ch_strdup(li->li_plugin->plg_dn);
    Slapi_PBlock *srch_pb = slapi_pblock_new();

    slapi_search_internal_set_pb(srch_pb, search_scope,
                                 LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb(srch_pb);
    slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &curr_entries);
    if (!curr_entries) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_verify_core",
                      "Failed to get current configuration.\n");
        rval = -1;
        goto out;
    }

    if (0 != slapi_entries_diff(backup_entries, curr_entries, 1 /* test_all */,
                                log_str, force_update, li->li_identity)) {
        if (force_update) {
            slapi_log_err(SLAPI_LOG_WARNING, "dbmdb_dse_conf_verify_core",
                          "Current %s is different from backed up configuration; "
                          "The backup is restored.\n",
                          log_str);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_dse_conf_verify_core",
                          "Current %s is different from backed up configuration; "
                          "The backup is not restored.\n",
                          log_str);
            rval = -1;
        }
    }

    slapi_free_search_results_internal(srch_pb);
    slapi_pblock_destroy(srch_pb);
out:
    for (bep = backup_entries; bep && *bep; bep++) {
        slapi_entry_free(*bep);
    }
    slapi_ch_free((void **)&backup_entries);

    slapi_ch_free_string(&search_scope);

    return rval;
}

int
dbmdb_dse_conf_verify(struct ldbminfo *li, char *src_dir)
{
    int rval;
    char *instance_entry_filter = NULL;

    instance_entry_filter = slapi_ch_strdup(DSE_INSTANCE_FILTER);

    /* instance should not be changed between bakup and restore */
    rval = dbmdb_dse_conf_verify_core(li, src_dir, DSE_INSTANCE, instance_entry_filter,
                                      0 /* force update */, "Instance Config");
    rval += dbmdb_dse_conf_verify_core(li, src_dir, DSE_INDEX, DSE_INDEX_FILTER,
                                       1 /* force update */, "Index Config");

    slapi_ch_free_string(&instance_entry_filter);

    return rval;
}

int
_get_import_entryusn(ImportJob *job, Slapi_Value **usn_value)
{
#define USN_COUNTER_BUF_LEN 64 /* enough size for 64 bit integers */
    static char counter_buf[USN_COUNTER_BUF_LEN] = {0};
    char *usn_init_str = NULL;
    long long usn_init;
    char *endptr = NULL;
    struct berval usn_berval = {0};

    if (NULL == usn_value) {
        return 1;
    }
    *usn_value = NULL;
    /*
     * Check if entryusn plugin is enabled.
     * If yes, get entryusn to set depending upon nsslapd-entryusn-import-init
     */
    if (!plugin_enabled("USN", (void *)plugin_get_default_component_id())) {
        return 1;
    }
    /* get the import_init config param */
    usn_init_str = config_get_entryusn_import_init();
    if (usn_init_str) {
        /* nsslapd-entryusn-import-init has a value */
        usn_init = strtoll(usn_init_str, &endptr, 10);
        if (errno || (0 == usn_init && endptr == usn_init_str)) {
            ldbm_instance *inst = job->inst;
            backend *be = inst->inst_be;
            /* import_init value is not digit.
             * Use the counter which stores the old DB's
             * next entryusn. */
            PR_snprintf(counter_buf, sizeof(counter_buf),
                        "%" PRIu64, slapi_counter_get_value(be->be_usn_counter));
        } else {
            /* import_init value is digit.
             * Initialize the entryusn values with the digit */
            PR_snprintf(counter_buf, sizeof(counter_buf), "%s", usn_init_str);
        }
        slapi_ch_free_string(&usn_init_str);
    } else {
        /* nsslapd-entryusn-import-init is not defined */
        /* Initialize to 0 by default */
        PR_snprintf(counter_buf, sizeof(counter_buf), "0");
    }
    usn_berval.bv_val = counter_buf;
    usn_berval.bv_len = strlen(usn_berval.bv_val);
    *usn_value = slapi_value_new_berval(&usn_berval);
    return 0;
}

/* writer thread */

int
cmp_key_addr(const void *i1, const void *i2)
{
    const WriterQueueData_t *e1 = i1;
    const WriterQueueData_t *e2 = i2;
    return e2->key.mv_data - e1->key.mv_data;
}

static inline int __attribute__((always_inline))
cas_pt(volatile void *addr, volatile void *old, volatile void *new)
{
    PR_ASSERT(sizeof (void*) == 8);
    return __sync_bool_compare_and_swap_8((void**)addr, (int64_t)old, (int64_t)(new));
}

/* writer.thread:
 * i go through the writer queue (unlike the other worker threads),
 * i'm responsible to write data in mdb database as I am the only
 * import thread allowed to start a read-write transaction.
 * (The other threads open read-only txn)
 */
void
dbmdb_import_writer(void*param)
{
    ImportWorkerInfo*info = (ImportWorkerInfo*)param;
    ImportJob*job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    WriterQueueData_t *nextslot = NULL;
    WriterQueueData_t *slot = NULL;
    PRIntervalTime sleeptime;
    MDB_txn *txn = NULL;
    int rc = 0;

    sleeptime = PR_MillisecondsToInterval(import_sleep_time);

    info->state = RUNNING;

    while (!info_is_finished(info)) {
        while((info->command == PAUSE) && !info_is_finished(info)) {
            /* Check to see if we've been told to stop */
            info->state = WAITING;
            DS_Sleep(sleeptime);
        }
        if (info_is_finished(info)) {
            break;
        }
        info->state = RUNNING;

        /* Wait until data get queued */
        pthread_mutex_lock(&ctx->writerq.mutex);
        while (ctx->writerq.count < WRITER_SLOTS && !info_is_finished(info) && !have_workers_finished(job)) {
            safe_cond_wait(&ctx->writerq.cv, &ctx->writerq.mutex);
        }
        do {
            slot = (WriterQueueData_t*)(ctx->writerq.list);
        } while (!cas_pt(&ctx->writerq.list, slot, NULL));
        ctx->writerq.count = 0;
        ctx->writerq.outlist = slot;
        pthread_mutex_unlock(&ctx->writerq.mutex);
        if (slot==NULL) {
            if (info_is_finished(info) || have_workers_finished(job)) {
                break;
            }
            continue;
        }

        TXN_BEGIN(ctx->ctx->env, NULL, 0, &txn);
        /*
         * Note: there may be a way to increase the db write performance by:
         *  using an array of bucket (containing max(dbi)-min(dbi) for the indexed dbis (including id2entry)
         *  walk the list and put the elements in the bucket associated with the element dbi
         *  for each bucket having elements :
         *    open a cursor toward the dbi
         *    use mdb_cursor_put of each element within the bucket (and free the element)
         *    close the cursor
         * (This will avoid opening/closing a cursor for each write element as mdb_put is doing)
         * (another is to play with env file to limt the fsyncs during the import)
         */
        for (; slot; slot = nextslot) {
            if (!rc) {
                rc = MDB_PUT(txn, slot->dbi->dbi, &slot->key, &slot->data, 0);
            }
            nextslot = slot->next;
            slapi_ch_free((void**)&slot);
        }
        if (rc) {
            TXN_ABORT(txn);
        } else {
            rc = TXN_COMMIT(txn);
        }
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_writer",
                    "Failed to write in the database. Error is 0x%x: %s.\n",
                    rc, mdb_strerror(rc));
            thread_abort(info);
        }
        /* Now that queue is safely writen in the database, we can flush the cache */
        rdncache_rotate(ctx->rdncache);
        ctx->writerq.outlist = NULL;
        pthread_cond_broadcast(&ctx->writerq.cv);
    }

    info_set_state(info);
}

/****************************************** NEW ***********************************/

static void
dbmdb_import_init_worker_info(ImportWorkerInfo *info, ImportJob *job, int role, const char *name, int idx)
{
    memset(info, 0, sizeof(ImportWorkerInfo));
    info->command = PAUSE;
    info->work_type = role;
    info->job = job;
    info->first_ID = job->first_ID;
    info->next = job->worker_list;
    job->worker_list = info;
    snprintf(info->name, WORKER_NAME_LEN, name, idx);
}


void dbmdb_dup_worker_slot(struct importqueue *q, void *from_slot, void *to_slot)
{
	/* Copy the WorkerQueueData_t slot except the winfo field */
    char *from = from_slot;
    char *to = to_slot;
    int offset = offsetof(WorkerQueueData_t, wait_id);
    memcpy(to+offset, from+offset, (sizeof (WorkerQueueData_t))-offset);
}

/* Find a free slot once used_slots == max_slots */
void *dbmdb_get_free_worker_slot(struct importqueue *q)
{
    WorkerQueueData_t *slots = (WorkerQueueData_t*)(q->slots);
    int i;

    PR_ASSERT(q->slot_size == sizeof(WorkerQueueData_t));
    for (i=0; i<q->max_slots; i++) {
        if (slots[i].wait_id == 0) {
            return &slots[i];
        }
    }
    return NULL;
}

void dbmdb_dup_writer_slot(struct importqueue *q, void *from_slot, void *to_slot)
{
	/* Copy the WriterQueueData_t slot */
    WriterQueueData_t *from = from_slot;
    WriterQueueData_t *to = to_slot;
    *to = *from;
}

void dbmdb_import_writeq_push(ImportCtx_t *ctx, WriterQueueData_t *wqd)
{
    /* Copy data in the new element */
    int len = sizeof (WriterQueueData_t) + wqd->key.mv_size + wqd->data.mv_size;
    WriterQueueData_t *elmt = (WriterQueueData_t*)slapi_ch_calloc(1, len);
    *elmt = *wqd;
    elmt->key.mv_data = &elmt[1];
    memcpy(elmt->key.mv_data, wqd->key.mv_data, wqd->key.mv_size);
    elmt->data.mv_data = ((char*)&elmt[1])+wqd->key.mv_size;
    memcpy(elmt->data.mv_data, wqd->data.mv_data, wqd->data.mv_size);

    /* Perform flow control (wait if queue is full and writer thread is busy) */
    pthread_mutex_lock(&ctx->writerq.mutex);
    while (ctx->writerq.count > WRITER_SLOTS && ctx->writerq.outlist && !info_is_finished(&ctx->writer)) {
        safe_cond_wait(&ctx->writerq.cv, &ctx->writerq.mutex);
    }
    pthread_mutex_unlock(&ctx->writerq.mutex);

    /* Queue the new element in the list */
    do {
        elmt->next = (WriterQueueData_t*)(ctx->writerq.list);
    } while (!cas_pt(&ctx->writerq.list, elmt->next, elmt));

    /* Check whether writer thread must be waken up */
    slapi_atomic_incr_32((int*)&ctx->writerq.count, __ATOMIC_ACQ_REL);
    if (ctx->writerq.count > WRITER_SLOTS) {
        pthread_cond_signal(&ctx->writerq.cv);
    }
}

int
dbmdb_import_init_writer(ImportJob *job, ImportRole_t role)
{
    ImportCtx_t *ctx = CALLOC(ImportCtx_t);
    struct ldbminfo *li = (struct ldbminfo *)job->inst->inst_be->be_database->plg_private;
    int nbcpus = util_get_capped_hardware_threads(MIN_WORKER_SLOTS, MAX_WORKER_SLOTS);
    WorkerQueueData_t *s = NULL;
    int i;

    job->writer_ctx = ctx;
    ctx->job = job;
    ctx->ctx = MDB_CONFIG(li);
    ctx->role = role;

    dbmdb_import_workerq_init(job, &ctx->workerq, (sizeof (WorkerQueueData_t)), nbcpus);
    pthread_mutex_init(&ctx->writerq.mutex, NULL);
    pthread_cond_init(&ctx->writerq.cv, NULL);
    ctx->rdncache = rdncache_init(ctx);

    /* Lets initialize the worker infos */
    dbmdb_import_init_worker_info(&ctx->writer, job, WRITER, "writer", 0);
    s = (WorkerQueueData_t*)ctx->workerq.slots;
    for(i=0; i<ctx->workerq.max_slots; i++) {
        memset(&s[i], 0, sizeof (WorkerQueueData_t));
        dbmdb_import_init_worker_info(&s[i].winfo, job, WORKER, "worker %d", i);
    }
    switch (role) {
        case IM_UNKNOWN:
            PR_ASSERT(0);
            break;
        case IM_IMPORT:
            dbmdb_import_init_worker_info(&ctx->producer, job, PRODUCER, "import producer", 0);
            ctx->prepare_worker_entry_fn = dbmdb_import_prepare_worker_entry;
            ctx->producer_fn = dbmdb_import_producer ;
            break;
        case IM_INDEX:
            dbmdb_import_init_worker_info(&ctx->producer, job, PRODUCER, "index producer", 0);
            ctx->prepare_worker_entry_fn = dbmdb_import_index_prepare_worker_entry;
            ctx->producer_fn = dbmdb_index_producer ;
            break;
        case IM_UPGRADE:
            dbmdb_import_init_worker_info(&ctx->producer, job, PRODUCER, "upgrade producer", 0);
            ctx->prepare_worker_entry_fn = dbmdb_upgrade_prepare_worker_entry;
            ctx->producer_fn = dbmdb_upgradedn_producer ;
            break;
        case IM_BULKIMPORT:
            ctx->prepare_worker_entry_fn = dbmdb_bulkimport_prepare_worker_entry;
            break;
   }
   return 0;
}

void
free_writer_queue(WriterQueueData_t **q)
{
    WriterQueueData_t *n = *q, *f = NULL;
    *q = NULL;
    while (n) {
        f = n;
        n = n->next;
        slapi_ch_free((void**)&f);
   }
}

void
dbmdb_free_import_ctx(ImportJob *job)
{
    ImportCtx_t *ctx = job->writer_ctx;
    job->writer_ctx = NULL;
    pthread_mutex_destroy(&ctx->workerq.mutex);
    pthread_cond_destroy(&ctx->workerq.cv);
    slapi_ch_free((void**)&ctx->workerq.slots);
    pthread_mutex_destroy(&ctx->writerq.mutex);
    pthread_cond_destroy(&ctx->writerq.cv);
    free_writer_queue((WriterQueueData_t**)&ctx->writerq.list);
    free_writer_queue((WriterQueueData_t**)&ctx->writerq.outlist);
    rdncache_free(&ctx->rdncache);
    avl_free(ctx->indexes, (IFP) free_ii);
    ctx->indexes = NULL;
    charray_free(ctx->indexAttrs);
    slapi_ch_free((void**)&job->writer_ctx);

}
