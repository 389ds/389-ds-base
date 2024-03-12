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
 * producer (1)
 * worker (N: based on the number of available cpus)
 * writer (1)
 *
 * With lmdb, bulk import has a producer thread  (to populate the entryinfo private db)
 * N is max(MIN_WORKER_SLOTS, min(nbcpu-NB_EXTRA_THREAD , MAX_WORKER_SLOTS))
 * (i.e: max(4, min(nbcpu-3 , 64)))
 */

#include <stddef.h>
#include <assert.h>
#include "mdb_import.h"
#include "../vlv_srch.h"
#include <sys/time.h>
#include <time.h>

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

#define EIP_NONE     0
#define EIP_RDN      1
#define EIP_WAIT     2

 /* Compute the padding size needed to get aligned on long integer */
#define ALIGN_TO_LONG(pos)            ((-(long)(pos))&((sizeof(long))-1))
#define ALIGN_TO_ID(pos)            ((-(ID)(pos))&((sizeof(ID))-1))
#define ALIGN_TO_ID_SLOT(len)       (((len)+ALIGN_TO_ID((len)))/sizeof(ID))

#define LMDB_MIN_DB_SIZE            (1024*1024*1024)


/* import thread usage statistics */
#define MDB_STAT_INIT(stats)    { mdb_stat_collect(&stats, MDB_STAT_RUN, 1); }
#define MDB_STAT_END(stats)     { mdb_stat_collect(&stats, MDB_STAT_RUN, 0); }
#define MDB_STAT_STEP(stats, step)    { mdb_stat_collect(&stats, (step), 0); }

typedef enum {
    MDB_STAT_RUN,
    MDB_STAT_READ,
    MDB_STAT_WRITE,
    MDB_STAT_PAUSE,
    MDB_STAT_TXNSTART,
    MDB_STAT_TXNSTOP,
    MDB_STAT_LAST_STEP  /* Last item in this enum */
} mdb_stat_step_t;

/* Should be kept in sync with mdb_stat_step_t */
#define MDB_STAT_STEP_NAMES { "run", "read", "write", "pause", "txnbegin", "txncommit" }

/* Per thread per step statistics */
typedef struct {
  struct timespec realtime;  /* Cumulated time spend in this step */
  /* Possible improvment: aggregate here some statistic from getrusage(RUSAGE_THREAD,stats) syscall */
} mdb_stat_slot_t;

/* Per thread statistics */
typedef struct {
    mdb_stat_step_t last_step;
    mdb_stat_slot_t last;
    mdb_stat_slot_t steps[MDB_STAT_LAST_STEP];
} mdb_stat_info_t;

void mdb_stat_collect(mdb_stat_info_t *sinfo, mdb_stat_step_t step, int init);
char *mdb_stat_summarize(mdb_stat_info_t *sinfo, char *buf, size_t bufsize);


/* The private db records data (i.e entry_info) format is:
 *      ID: entry ID      [0]
 *      ID: nb ancestors  [1]
 *      ID: nrdn len      [2] ( The len include the final \0 )
 *      ID: rdn len       [3] ( The len include the final \0 )
 *      ID: dn len        [4] ( The len include the final \0 )
 *      ID[]: ancestors
 *      char[]: null terminated nrdn
 *      char[]: null terminated rdn
 *      char[]: null terminated ndn
 *      Note: the IDs are stored directly as this database is transient and
 *       not shared across different hardware.
 *     Note: all these data for parent and current entry are needed
 *     by the worker thread to be able to build the entryrdn index records
 *     and ancestorid index records associated with the entry
 */
#define INFO_IDX_ENTRY_ID       0
#define INFO_IDX_NB_ANCESTORS   1
#define INFO_IDX_NRDN_LEN       2
#define INFO_IDX_RDN_LEN        3
#define INFO_IDX_DN_LEN         4
#define INFO_IDX_ANCESTORS      5
#define INFO_NRDN(info)         ((char*)(&((ID*)(info))[INFO_IDX_ANCESTORS+((ID*)(info))[INFO_IDX_NB_ANCESTORS]]))
#define INFO_RDN(info)          (INFO_NRDN(info)+((ID*)(info))[INFO_IDX_NRDN_LEN])
#define INFO_DN(info)           (INFO_RDN(info)+((ID*)(info))[INFO_IDX_RDN_LEN])
#define INFO_RECORD_LEN(info)   ((INFO_DN(info)-(char*)(info))+(info)[INFO_IDX_DN_LEN])  /* Total lenght of a record */

typedef struct {
    back_txn txn;
    ImportCtx_t *ctx;
} PseudoTxn_t;

typedef struct {
    mdb_privdb_t *db;    /* private db handler */
    MDB_val ekey;        /* private db record key for entry */
    MDB_val pkey;        /* private db record key for parent entry */
    Slapi_DN sdn;        /* Entry dn (or rdn) */
    ID eid;              /* entry ID */
    int flags;           /* EIP_* */
    char *trdn;          /* Tombstone entry rdn */
    char *tnrdn;         /* Tombstone entry normalized rdn */
    char *uuid;          /* Entry uuid */
    char *puuid;         /* Parent Entry uuid */
} EntryInfoParam_t;

typedef struct wait4id_queue {
    ID id;
    ID wait4id;
    MDB_val entry;
    struct wait4id_queue *next;
} wait4id_queue_t;


typedef struct backentry backentry;
static PseudoTxn_t init_pseudo_txn(ImportCtx_t *ctx);
static int cmp_mii(caddr_t data1, caddr_t data2);
static void dbmdb_import_writeq_push(ImportCtx_t *ctx, WriterQueueData_t *wqd);
static int have_workers_finished(ImportJob *job);
struct backentry *dbmdb_import_prepare_worker_entry(WorkerQueueData_t *wqelmnt);

/***************************************************************************/
/**************************** utility functions ****************************/
/***************************************************************************/

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

static inline int __attribute__((always_inline))
cmp_data(MDB_val *d1, MDB_val *d2)
{
    if (d1->mv_size != d2->mv_size) {
        return d1->mv_size - d2->mv_size;
    }
    return memcmp(d1->mv_data, d2->mv_data, d1->mv_size);
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

void
thread_abort(ImportWorkerInfo *info)
{
    info->state = ABORTED;
}

void
safe_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex)
{
	struct timespec cvtimeout;
    clock_gettime(CLOCK_REALTIME, &cvtimeout);
    cvtimeout.tv_nsec += 100 * 1000 * 1000;
    pthread_cond_timedwait(cond, mutex, &cvtimeout);
}

void
wait_for_starting(ImportWorkerInfo *info)
{
    PRIntervalTime sleeptime;
    sleeptime = PR_MillisecondsToInterval(import_sleep_time);
    /* pause until we're told to run */
    while ((info->command == PAUSE) && !info_is_finished(info)) {
        info->state = WAITING;
        DS_Sleep(sleeptime);
    }
    info->state = RUNNING;
}

/***************************************************************************/
/************************ ImportNto1Queue functions ************************/
/***************************************************************************/

/* generic code for a queue used by N provider threads to a single consumer */


/* typical shouldwait callback for ImportNto1Queue */
int
generic_shouldwait(ImportNto1Queue_t *q)
{
    ImportWorkerInfo *info = q->info;
    return (q->nbitems < q->minitems && !info_is_finished(info));
}

/* After initing the struct the caller still needs to initialize the callbacks
 * and the context that they may need.
 */
void
dbmdb_import_q_init(ImportNto1Queue_t *q, ImportWorkerInfo *info, int maxitems)
{
    q->info = info;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cv, NULL);
    q->list = NULL;
    q->maxitems = maxitems;
    /* Setting minitems to 1 because tests showed we get better performance with this value */
    q->minitems = 1;
    q->nbitems  = 0;
    q->dupitem_cb = NULL;
    q->freeitem_cb = NULL;
    q->shouldwait_cb = generic_shouldwait;
}

/* push an item in a queue */
void
dbmdb_import_q_push(ImportNto1Queue_t *q, void *item)
{
    /* Copy data in the new element */
    struct { void *next; } *curitem = q->dupitem_cb(item);

    /* Perform flow control (wait if queue is full and writer thread is busy)
     * The test is a bit loosy but is enough to ensure that queue is not growing
     * out of control.
     */
    pthread_mutex_lock(&q->mutex);
    while (q->nbitems >= q->maxitems && !info_is_finished(q->info)) {
        safe_cond_wait(&q->cv, &q->mutex);
    }
    curitem->next = (WriterQueueData_t*)(q->list);
    q->list = curitem;
    /* Check whether writer thread must be waken up */
    q->nbitems++;
    if (q->nbitems >= q->minitems) {
        pthread_cond_signal(&q->cv);
    }
    pthread_mutex_unlock(&q->mutex);
}

/* extract all items out of a queue */
void *
dbmdb_import_q_getall(ImportNto1Queue_t *q)
{
    void *items = NULL;

    /* Wait until enough items get queued */
    pthread_mutex_lock(&q->mutex);
    while (q->shouldwait_cb(q)) {
        safe_cond_wait(&q->cv, &q->mutex);
    }
    items = (WriterQueueData_t*)(q->list);
    q->list = NULL;
    q->nbitems = 0;
    /* Lets wake up threads waiting for a slot in dbmdb_import_writeq_push */
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mutex);
    return items;
}

/* Clear the list and free the items */
void
dbmdb_import_q_flush(ImportNto1Queue_t *q)
{
    struct { void *next; } *item;
    void *items = NULL;
    pthread_mutex_lock(&q->mutex);
    items = (WriterQueueData_t*)(q->list);
    q->list = NULL;
    q->nbitems = 0;
    pthread_mutex_unlock(&q->mutex);

    for (item = items; item; item = items) {
         items = item->next;
         q->freeitem_cb((void**)&item);
    }
}

/* release all resources associated with a queue */
void
dbmdb_import_q_destroy(ImportNto1Queue_t *q)
{
    dbmdb_import_q_flush(q);
    pthread_cond_destroy(&q->cv);
    pthread_mutex_destroy(&q->mutex);
    memset(q, 0, sizeof *q);
}

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

/***************************************************************************/
/************************** WorkerQueue functions **************************/
/***************************************************************************/

/* i.e the queue used by provider thread to push entries towards worker threads */

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

void
dbmdb_import_workerq_free_data(WorkerQueueData_t *data)
{
    ImportCtx_t *ctx = data->winfo.job->writer_ctx;
    if (ctx->role == IM_BULKIMPORT) {
       backentry_free((struct backentry **)&data->data);
    } else {
        slapi_ch_free(&data->data);
    }
    data->datalen = 0;
    slapi_ch_free((void**)&data->parent_info);
    slapi_ch_free((void**)&data->entry_info);
}

int
dbmdb_import_workerq_push(ImportQueue_t *q, WorkerQueueData_t *data)
{
    WorkerQueueData_t *slot =NULL;

    pthread_mutex_lock(&q->mutex);
    if (q->used_slots < q->max_slots) {
        slot = &q->slots[q->used_slots++];
    } else {
        while ((slot = dbmdb_get_free_worker_slot(q)) == 0 && !(q->job->flags & FLAG_ABORT)) {
            safe_cond_wait(&q->cv, &q->mutex);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    if (q->job->flags & FLAG_ABORT) {
        /* in this case worker thread does not free the data so we should do it */
        dbmdb_import_workerq_free_data(data);
        return -1;
    }
    dbmdb_dup_worker_slot(q, data, slot);
    return 0;
}


/***************************************************************************/
/******************************  LDIF PARSER  ******************************/
/***************************************************************************/


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


/***************************************************************************/
/********************************* THREADS *********************************/
/***************************************************************************/

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

mdb_privdb_t *
dbmdb_import_init_entrydn_dbs(ImportCtx_t *ctx)
{
    /* Compute total ldif size */
    ImportJob *job = ctx->job;
    size_t dbsize = 0;
    int curr_file=0;
    if (!job->input_filenames || strcmp(job->input_filenames[0], "-") == 0) {
        /* Use mdb main db size / 10 ) */
        dbsize = ctx->ctx->startcfg.max_size / 10;
    } else {
        /* let evaluate the ldif size */
        for (curr_file=0; job->input_filenames[curr_file]; curr_file++) {
            struct stat st = {0};
            if (stat(job->input_filenames[curr_file], &st) == 0) {
                dbsize += st.st_size;
            }
        }
    }
    if (dbsize < LMDB_MIN_DB_SIZE) {
        dbsize = LMDB_MIN_DB_SIZE;
    }

    return dbmdb_privdb_create(ctx->ctx,  dbsize, "ndn", NULL);
}

dnrc_t
get_entry_type(WorkerQueueData_t *wqelmt, Slapi_DN *sdn)
{
    backend *be = wqelmt->winfo.job->inst->inst_be;
    int len = SLAPI_ATTR_UNIQUEID_LENGTH;
    const char *ndn = slapi_sdn_get_ndn(sdn);

    if (slapi_be_issuffix(be, sdn)) {
        return DNRC_SUFFIX;
    }
    if (PL_strncasecmp(ndn, SLAPI_ATTR_UNIQUEID, len) || ndn[len] != '=') {
            return DNRC_OK;
    }
    /* Maybe a tombstone or a ruv */
    if (wqelmt->datalen) {
        /* Check objectclass */
        char *pt0, *pt1, *pt2;
        int len2 = (sizeof SLAPI_ATTR_OBJECTCLASS) -1;
        for (pt0 = pt1 = wqelmt->data; (pt1 = strcasestr(pt1, ": " SLAPI_ATTR_VALUE_TOMBSTONE "\n")); pt1++) {
            /* Get start of line */
            for (pt2 = pt1; pt2 >= pt0 && *pt2 != '\n'; pt2--);
            pt2++;
            if (PL_strncasecmp(pt2, SLAPI_ATTR_OBJECTCLASS, len2)) {
                continue;
            }
            if (pt2[len2] != ';' && pt2[len2] != ':') {
                continue;
            }
            break;  /* Found it */
        }
        if (!pt1) {
            return DNRC_OK;
        }
    } else {
        struct backentry *ep = wqelmt->data;
        if (!slapi_entry_flag_is_set(ep->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE)) {
            return DNRC_OK;
        }
    }
    if (0 != PL_strncasecmp(&ndn[len+1], RUV_STORAGE_ENTRY_UNIQUEID, sizeof(RUV_STORAGE_ENTRY_UNIQUEID) - 1)) {
        return DNRC_TOMBSTONE;
    } else {
        return DNRC_RUV;
    }
}

/*
 * Compute trdn and tnrdn and set keys according to the nsuniqueids
 */
static dnrc_t
entryinfo_param_4_tombstone(EntryInfoParam_t *param)
{
    dnrc_t dnrc = DNRC_TOMBSTONE;
    /* Tombstone entry: "entryrdn rdn" is two first rdn */
    const char *ava1 = NULL;
    const char *ava2 = NULL;
    const char *nava1 = NULL;
    const char *nava2 = NULL;
    Slapi_RDN srdn = {0};
    int rc = slapi_rdn_init_all_sdn(&srdn, &param->sdn);

    if (rc) {
        dnrc = DNRC_BADDN;
    } else {
        int rdnidx = slapi_rdn_get_first_ext(&srdn, &ava1, FLAG_ALL_RDNS);
        int nrdnidx = slapi_rdn_get_first_ext(&srdn, &nava1, FLAG_ALL_NRDNS);
        if (rdnidx >= 0 && nrdnidx>= 0) {
            rdnidx = slapi_rdn_get_next_ext(&srdn, rdnidx, &ava2, FLAG_ALL_RDNS);
            nrdnidx = slapi_rdn_get_next_ext(&srdn, nrdnidx, &nava2, FLAG_ALL_NRDNS);
        }
        if (rdnidx >= 0 && nrdnidx>= 0) {
            param->trdn = slapi_ch_smprintf("%s,%s", ava1, ava2);
            param->tnrdn = slapi_ch_smprintf("%s,%s", nava1, nava2);
        } else {
            dnrc = DNRC_NOPARENT_DN;
        }
    }
    if (!param->uuid || !param->puuid) {
        dnrc = DNRC_BAD_TOMBSTONE;
    } else {
        param->ekey.mv_data = param->uuid;
        param->ekey.mv_size = strlen(param->uuid) + 1;
        param->pkey.mv_data = param->puuid;
        param->pkey.mv_size = strlen(param->puuid) + 1;
    }
    slapi_rdn_done(&srdn);
    return dnrc;
}

/*
 * Compute
 * key -> entryinfo for both entry and parent entry.
 * param->ekey and param->pkey should be set by call in EIP_RDN case otherwise they are computed from the sdn
 * ( and then point to sdn data so their is no need to freed them )
 */
static dnrc_t
dbmdb_import_entry_info_by_param(EntryInfoParam_t *param, WorkerQueueData_t *wqelmt)
{
    MDB_val data = {0};
    Slapi_RDN srdn = {0};
    const char *rdn = NULL;
    const char *nrdn = NULL;
    dnrc_t dnrc = DNRC_OK;
    size_t len = 0;
    static const ID pinfozero[INFO_IDX_ANCESTORS+2] = {0};
    const ID *pinfo = pinfozero;
    int pidfromkey = 0;
    int rc = 0;

    wqelmt->parent_info = NULL;
    wqelmt->entry_info = NULL;
    if (param->flags & EIP_RDN) {
        memcpy(&pidfromkey, param->pkey.mv_data, sizeof (ID));
    }

    dnrc = get_entry_type(wqelmt, &param->sdn);
    if (dnrc == DNRC_SUFFIX) {
        if ( param->eid != 1) {
            dnrc = DNRC_BAD_SUFFIX_ID;
        } else {
            rdn = slapi_sdn_get_dn(&param->sdn);
            nrdn = slapi_sdn_get_ndn(&param->sdn);
            if (!(param->flags & EIP_RDN)) {
                param->ekey.mv_data = (void*)nrdn;
                param->ekey.mv_size = strlen(nrdn)+1;
                param->pkey.mv_data = NULL;
                param->pkey.mv_size = 0;
            }
        }
    } else if (DNRC_IS_ENTRY(dnrc)) {
        if (param->flags & EIP_RDN) {
            if (!pidfromkey) {
                dnrc = DNRC_NOPARENT_ID;
            }
            rdn = slapi_sdn_get_dn(&param->sdn);
            nrdn = slapi_sdn_get_ndn(&param->sdn);
        } else if (dnrc == DNRC_TOMBSTONE) {
            /* Tombstone entry: "entryrdn rdn" is two first rdn and nsuniqueid/nsparentuniqueid is used as key */
            dnrc = entryinfo_param_4_tombstone(param);
            rdn = param->trdn;
            nrdn = param->tnrdn;
        } else {
            /* regular entry or RUV: "entryrdn rdn" is first rdn and ndn/parentndn is used as key */
            slapi_sdn_get_rdn(&param->sdn, &srdn);
            rdn = slapi_rdn_get_rdn(&srdn);
            nrdn = slapi_rdn_get_nrdn(&srdn);
            /* Check for DN validity */
            if (nrdn == NULL) {
                /* ldap_explode failed */
                dnrc = DNRC_BADDN;
            } else {
                param->ekey.mv_data = (void*)slapi_sdn_get_ndn(&param->sdn);
                param->ekey.mv_size = strlen(param->ekey.mv_data)+1;
                param->pkey.mv_data = (void*)slapi_dn_find_parent_ext(param->ekey.mv_data, dnrc != DNRC_OK);
                param->pkey.mv_size = strlen(param->pkey.mv_data)+1;
                if (!param->pkey.mv_data) {
                    dnrc = DNRC_NOPARENT_DN;
                }
            }
        }
    }
    if (param->eid == 1 && dnrc == DNRC_RUV) {
        /* RUV is first entry lets return it as it */
        slapi_rdn_done(&srdn);
        return (param->flags & EIP_WAIT) ? DNRC_WAIT : DNRC_POSPONE_RUV;
    }
    if (dnrc != DNRC_SUFFIX && DNRC_IS_ENTRY(dnrc)) {
        /* Regular entry should have a parent info in the private db */
        /* lets lookup for the parent ndn */
        rc = dbmdb_privdb_get(param->db, 0, &param->pkey, &data);
        if (rc == MDB_NOTFOUND) {
            if (param->flags & EIP_WAIT) {
                dnrc = DNRC_WAIT;
            } else {
                dnrc = DNRC_NOPARENT_ID;
            }
        } else if (rc) {
            dnrc = DNRC_ERROR;
        } else {
            /* Everything is ok, lets duplicate the parent info for the worker thread  */
            wqelmt->parent_info = (ID*)slapi_ch_calloc(ALIGN_TO_ID_SLOT(data.mv_size), sizeof(ID));
            memcpy(wqelmt->parent_info, data.mv_data, data.mv_size);
            pinfo = wqelmt->parent_info;
        }
    }
    if (DNRC_IS_ENTRY(dnrc)) {
        size_t rdnlen = strlen(rdn);
        size_t nrdnlen = strlen(nrdn);
        size_t dnlen = 0;
        if (param->flags & EIP_RDN) {
            /* In reindex case, dn must be rebuilt to be able to scope properly the vlv index */
            dnlen = rdnlen + 1 + pinfo[INFO_IDX_DN_LEN];  /* dn len (including final \0) */
        }

        len = rdnlen + nrdnlen + 2 + dnlen + (INFO_IDX_ANCESTORS + 1 + pinfo[INFO_IDX_NB_ANCESTORS]) * sizeof(ID);
        data.mv_data = wqelmt->entry_info = (ID*)slapi_ch_calloc(ALIGN_TO_ID_SLOT(len), sizeof(ID));
        data.mv_size = len;
        wqelmt->entry_info[INFO_IDX_ENTRY_ID] = param->eid;
        if (pinfo[INFO_IDX_ENTRY_ID] == 0) {
            wqelmt->entry_info[INFO_IDX_NB_ANCESTORS] = 0;
        } else {
            wqelmt->entry_info[INFO_IDX_NB_ANCESTORS] = pinfo[INFO_IDX_NB_ANCESTORS] + 1; /* parent ancestors + parent id */
        }
        wqelmt->entry_info[INFO_IDX_NRDN_LEN] = nrdnlen+1;
        wqelmt->entry_info[INFO_IDX_RDN_LEN] = rdnlen+1;
        wqelmt->entry_info[INFO_IDX_DN_LEN] = dnlen;
        if (pinfo[INFO_IDX_NB_ANCESTORS]) {
            /* Copy parent ancestors */
            memcpy(&wqelmt->entry_info[INFO_IDX_ANCESTORS], &pinfo[INFO_IDX_ANCESTORS], pinfo[INFO_IDX_NB_ANCESTORS]*sizeof(ID));
        }
        if (pinfo[INFO_IDX_ENTRY_ID] != 0) {
            /* Then add parent id to ancestors */
            wqelmt->entry_info[INFO_IDX_ANCESTORS+pinfo[INFO_IDX_NB_ANCESTORS]] = pinfo[INFO_IDX_ENTRY_ID];
        }
        memcpy(INFO_NRDN(wqelmt->entry_info), nrdn, wqelmt->entry_info[INFO_IDX_NRDN_LEN]);
        memcpy(INFO_RDN(wqelmt->entry_info), rdn, wqelmt->entry_info[INFO_IDX_RDN_LEN]);
        if (dnlen>0) {
            char *dn = INFO_DN(wqelmt->entry_info);
            memcpy(dn, rdn, rdnlen);
            if (pinfo[INFO_IDX_DN_LEN]) {
                dn[rdnlen] = ',';
                memcpy(dn+rdnlen+1, INFO_DN(pinfo), pinfo[INFO_IDX_DN_LEN]);
            } else {
                /* Should be the suffix entry */
                dn[rdnlen] = '\0';
            }
        }
        rc = dbmdb_privdb_put(param->db, 0, &param->ekey, &data);
        if (rc == MDB_KEYEXIST) {
            dnrc = DNRC_DUP;
        } else if (rc) {
            dnrc = DNRC_ERROR;
        }
    }
    if ((dnrc == DNRC_OK || dnrc == DNRC_SUFFIX) && param->uuid) {
        /* Add nsuniqueid key that is needed if a child is a tombstone entry */
        MDB_val key = {0};
        key.mv_data = param->uuid;
        key.mv_size = strlen(param->uuid) + 1;
        rc = dbmdb_privdb_put(param->db, 0, &key, &data);
        if (rc == MDB_KEYEXIST) {
            dnrc = DNRC_DUP;
        } else if (rc) {
            dnrc = DNRC_ERROR;
        }
    }
    if (!DNRC_IS_ENTRY(dnrc)) {
        slapi_ch_free((void**)&wqelmt->parent_info);
        slapi_ch_free((void**)&wqelmt->entry_info);
    }
    slapi_rdn_done(&srdn);
    return dnrc;
}

void
entryinfoparam_cleanup(EntryInfoParam_t *param)
{
    slapi_sdn_done(&param->sdn);
    slapi_ch_free_string(&param->trdn);
    slapi_ch_free_string(&param->tnrdn);
    slapi_ch_free_string(&param->uuid);
    slapi_ch_free_string(&param->puuid);
}

/* Extract the dn from entry, compute nrdn, rdn, parent ndn and ancestors ids
 * store ndn -> entryinfo in a private db (to retrieve the parent infos)
 * Note: we just use raw ID without taking care of endianess as
 * the dn db is temporary and could not move to other hardware.
 */
dnrc_t
dbmdb_import_entry_info_by_ldifentry(mdb_privdb_t *db, WorkerQueueData_t *wqelmt)
{
    EntryInfoParam_t param = {0};
    dnrc_t dnrc = DNRC_OK;
    char *dn = NULL;

    wqelmt->parent_info = NULL;
    wqelmt->entry_info = NULL;
    if (get_value_from_string(wqelmt->data, "dn", &dn)) {
        if (strncmp(wqelmt->data, "version:", 8) == 0 && wqelmt->lineno<=1) {
            return DNRC_VERSION;
        } else {
            return DNRC_NODN;
        }
    }
    get_value_from_string(wqelmt->data, SLAPI_ATTR_UNIQUEID, &param.uuid);
    if (PL_strncasecmp(dn, SLAPI_ATTR_UNIQUEID, SLAPI_ATTR_UNIQUEID_LENGTH) == 0) {
        get_value_from_string(wqelmt->data, "nsparentuniqueid", &param.puuid);
    }
    param.db = db;
    slapi_sdn_init_dn_byval(&param.sdn, dn);
    param.eid = wqelmt->wait_id;
    param.flags = EIP_NONE;
    wqelmt->dn = dn;
    dnrc = dbmdb_import_entry_info_by_param(&param, wqelmt);
    entryinfoparam_cleanup(&param);
    return dnrc;
}


/* Extract the rdn and parentid from entry, compute nrdn, parent ndn and ancestors ids
 * store id -> entryinfo in a private db (to retrieve the parent infos)
 * Note: we just use raw ID without taking care of endianess as
 * the dn db is temporary and could not move to other hardware.
 */
dnrc_t
dbmdb_import_entry_info_by_rdn(mdb_privdb_t *db, WorkerQueueData_t *wqelmt)
{
    EntryInfoParam_t param = {0};
    dnrc_t dnrc = DNRC_OK;
    char *rdn = NULL;
    ID pid = 0;
    char *pidstr = NULL;

    wqelmt->wait4id = 0;
    wqelmt->parent_info = NULL;
    wqelmt->entry_info = NULL;
    if (wqelmt->wait_id != 1) {
        if (!get_value_from_string(wqelmt->data, "parentid", &pidstr)) {
            pid = atoi(pidstr);
            slapi_ch_free_string(&pidstr);
        } else {
            pid = 1;
        }
    }
    if (get_value_from_string(wqelmt->data, "rdn", &rdn)) {
        return DNRC_NORDN;
    }

    param.db = db;
    slapi_sdn_set_dn_passin(&param.sdn, rdn);
    param.eid = wqelmt->wait_id;
    param.ekey.mv_data = &param.eid;
    param.ekey.mv_size = sizeof param.eid;
    param.pkey.mv_data = &pid;
    param.pkey.mv_size = sizeof pid;

    param.flags = EIP_RDN + EIP_WAIT;
    dnrc = dbmdb_import_entry_info_by_param(&param, wqelmt);
    entryinfoparam_cleanup(&param);
    if (dnrc == DNRC_WAIT) {
        wqelmt->wait4id = pid;
    }
    return dnrc;
}

/* Extract the dn from backentry,
 * store ndn -> entryinfo in a private db and retrieve the parent entry entryinfo
 */
dnrc_t
dbmdb_import_entry_info_by_backentry(mdb_privdb_t *db, BulkQueueData_t *bqdata, WorkerQueueData_t *wqelmt)
{
    EntryInfoParam_t param = {0};
    dnrc_t dnrc = DNRC_OK;
    const Slapi_Entry *e = bqdata->ep->ep_entry;

    wqelmt->parent_info = NULL;
    wqelmt->entry_info = NULL;
    param.db = db;
    if (e->e_uniqueid) {
        param.uuid = slapi_ch_strdup(e->e_uniqueid);
        if (e->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
            param.puuid = slapi_entry_attr_get_charptr(e, "nsparentuniqueid");
        }
    }
    slapi_sdn_init_dn_byref(&param.sdn, slapi_entry_get_dn_const(e));
    param.eid = wqelmt->wait_id;
    param.flags = EIP_WAIT;
    dnrc = dbmdb_import_entry_info_by_param(&param, wqelmt);
    if (dnrc == DNRC_WAIT) {
        dup_data(&bqdata->wait4key, &param.pkey);
    } else {
        bqdata->wait4key.mv_data = NULL;
        bqdata->wait4key.mv_size = 0;
    }
    dup_data(&bqdata->key, &param.ekey);
    entryinfoparam_cleanup(&param);
    return dnrc;
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
    int detected_eof = 0;
    int fd, curr_file, curr_lineno = 0;
    char *curr_filename = NULL;
    int idx;
    ldif_context c;
    WorkerQueueData_t wqelmt = {0};
    mdb_privdb_t *dndb = NULL;
    WorkerQueueData_t ruvwqelmt = {0};

    PR_ASSERT(info != NULL);
    PR_ASSERT(job->inst != NULL);


    ctx->wgc.str2entry_flags = SLAPI_STR2ENTRY_TOMBSTONE_CHECK |
                      SLAPI_STR2ENTRY_REMOVEDUPVALS |
                      SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
                      SLAPI_STR2ENTRY_ADDRDNVALS |
                      SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;

    wait_for_starting(info);
    dbmdb_import_init_ldif(&c);

    /* Get entryusn, if needed. */
    _get_import_entryusn(job, &(job->usn_value));

    /* jumpstart by opening the first file */
    curr_file = 0;
    fd = -1;
    detected_eof = 0;
    dndb = dbmdb_import_init_entrydn_dbs(ctx);
    if (!dndb) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Failed to create normalized dn private db.");
        thread_abort(info);
    }
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
        wait_for_starting(info);
        wqelmt.winfo.job = job;
        wqelmt.wait_id = id;
        wqelmt.lineno = curr_lineno + 1;  /* Human tends to start counting from 1 rather than 0 */
        wqelmt.data = dbmdb_import_get_entry(&c, fd, &curr_lineno);
        wqelmt.nblines = curr_lineno - wqelmt.lineno;
        wqelmt.datalen = 0;
        if (!wqelmt.data) {
            /* error reading entry, or end of file */
            detected_eof = 1;
            continue;
        }
        wqelmt.datalen = strlen(wqelmt.data);
        wqelmt.dnrc = dbmdb_import_entry_info_by_ldifentry(dndb, &wqelmt);
        switch (wqelmt.dnrc) {
            default:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer",
                                  "ns_slapd software error: unexpected dbmdb_import_entry_info return code: %d.",
                                  wqelmt.dnrc);
                abort();
            case DNRC_OK:
            case DNRC_SUFFIX:
            case DNRC_TOMBSTONE:
                break;
            case DNRC_RUV:
                break;
            case DNRC_POSPONE_RUV:
                /* If ldif file starts with the RUV, lets process after last entry */
                ruvwqelmt = wqelmt;
                continue;
            case DNRC_VERSION:
                slapi_ch_free(&wqelmt.data);
                continue;
            case DNRC_NODN:
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping entry with ID %d which has no DN and is around line %d in file \"%s\"", wqelmt.wait_id, curr_lineno, curr_filename);
                slapi_ch_free(&wqelmt.data);
                job->skipped++;
                continue;
            case DNRC_BADDN:
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping entry \"%s\" which has an invalid DN. The entry ID is %d and is around line %d in file \"%s\"",
                                  wqelmt.dn, wqelmt.wait_id, curr_lineno, curr_filename);
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                job->skipped++;
                continue;
            case DNRC_DUP:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer",
                                  "Duplicated DN detected: \"%s\": Entry ID: (%d)", wqelmt.dn, wqelmt.wait_id);
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                thread_abort(info);
                continue;
            case DNRC_NOPARENT_DN:
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping entry \"%s\" because parent dn cannot be extracted from the entry dn. The entry ID is %d and is around line %d in file \"%s\"",
                                  wqelmt.dn, wqelmt.wait_id, curr_lineno, curr_filename);
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                job->skipped++;
                continue;
            case DNRC_BAD_SUFFIX_ID:
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping suffix entry \"%s\" with entry ID %d around line %d in file \"%s\" because suffix should be the first entry.",
                                  wqelmt.dn, wqelmt.wait_id, curr_lineno, curr_filename);
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                job->skipped++;
                continue;
            case DNRC_NOPARENT_ID:
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping entry \"%s\" which has no parent. The entry ID is %d and is around line %d in file \"%s\"",
                                  wqelmt.dn, wqelmt.wait_id, curr_lineno, curr_filename);
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                job->skipped++;
                continue;
            case DNRC_ERROR:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer",
                                  "Import is arborted because a LMDB database error was detected. Please check the error log for more details.");
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                thread_abort(info);
                continue;
            case DNRC_BAD_TOMBSTONE:
                import_log_notice(job, SLAPI_LOG_WARNING, "dbmdb_import_producer",
                                  "Skipping tombsone entry \"%s\" which has no nsparentuniqueid or no nsuniqueid attributes. The entry ID is %d and is around line %d in file \"%s\"",
                                  wqelmt.dn, wqelmt.wait_id, curr_lineno, curr_filename);
                slapi_ch_free_string(&wqelmt.dn);
                slapi_ch_free(&wqelmt.data);
                job->skipped++;
                continue;
        }

        slapi_ch_free_string(&wqelmt.dn);
        dbmdb_import_workerq_push(&ctx->workerq, &wqelmt);

       /* Update our progress meter too */
        info->last_ID_processed = id;
        job->lead_ID = id;
        id++;
    }
    /* And finally add the pending RUV */
    if (ruvwqelmt.dnrc) {
        slapi_ch_free_string(&ruvwqelmt.dn);
        ruvwqelmt.wait_id = id;
        /* Process again the entry to fill up the entry and parent entry info */
        (void) dbmdb_import_entry_info_by_ldifentry(dndb, &ruvwqelmt);
        dbmdb_import_workerq_push(&ctx->workerq, &ruvwqelmt);
        ruvwqelmt.dnrc = 0;
        info->last_ID_processed = id;
        job->lead_ID = id;
    }
    dbmdb_privdb_destroy(&dndb);

    /* capture skipped entry warnings for this task */
    if (job->skipped) {
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

static int
dbmdb_get_aux_id2entry(backend*be, dbmdb_dbi_t **dbi, char **path)
{
     return dbmdb_open_dbi_from_filename(dbi, be, ID2ENTRY, NULL, 0);
}

void
wait4id_queue_push(wait4id_queue_t **queue, ID id, const MDB_val *data)
{
    wait4id_queue_t *elmt = (void*) slapi_ch_malloc(sizeof (wait4id_queue_t));

    elmt->id = id;
    elmt->wait4id = 0;
    elmt->entry.mv_data = slapi_ch_malloc(data->mv_size);
    elmt->entry.mv_size = data->mv_size;
    memcpy(elmt->entry.mv_data, data->mv_data, data->mv_size);
    elmt->next = *queue;
    *queue = elmt;
}

void
wait4q_flush(wait4id_queue_t **q)
{
    wait4id_queue_t *e;
    while (*q) {
        e = *q;
        *q = e->next;
        slapi_ch_free(&e->entry.mv_data);
        slapi_ch_free((void**)&e);
    }
}

/* Read some entries from id2entry database and push them in processing queue */
int
fill_processingq(ImportJob *job, MDB_dbi dbi, wait4id_queue_t **processingq, char *lastid)
{
    ImportCtx_t *ctx = job->writer_ctx;
    MDB_cursor *dbc = NULL;
    MDB_txn *txn = NULL;
    MDB_val data = {0};
    MDB_val key = {0};
    char zero[sizeof (ID)] = {0};
    int count = 64;
    int rc = 0;

    key.mv_data = lastid;
    key.mv_size = sizeof (ID);
    rc = TXN_BEGIN(ctx->ctx->env, NULL, MDB_RDONLY, &txn);
    if (rc) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Failed to begin a database txn. Error %d: %s", rc, mdb_strerror(rc));
        return rc;
    }
    rc = MDB_CURSOR_OPEN(txn, dbi, &dbc);
    if (rc) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Failed to open a database cursor. Error %d: %s", rc, mdb_strerror(rc));
        TXN_ABORT(txn);
        return rc;
    }
    if (memcmp(lastid, zero, sizeof zero) == 0) {
        /* Set curseur on first entry and push it in queue */
        rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_FIRST);
	    if (rc == MDB_NOTFOUND) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Database without entries cannot be indexed.");
            rc = MDB_CORRUPTED;
        } else if (rc) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Error while reading the database. Error %d: %s", rc, mdb_strerror(rc));
        } else {
            wait4id_queue_push(processingq, id_stored_to_internal((char *)key.mv_data), &data);
            count--;
        }
    } else {
        /* Set curseur on last entry without pushing it in queue (as it has been processed) */
        rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_SET);
	    if (rc == MDB_NOTFOUND) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Database inconsistency fail to find entryid %s that was found at previous iteration.");
            rc = MDB_CORRUPTED;
        } else if (rc) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Error while reading the database. Error %d: %s", rc, mdb_strerror(rc));
        }
    }
        /* Iterate on the cursor to push entries in the queue */
    while (!rc && --count>0) {
        rc = MDB_CURSOR_GET(dbc, &key, &data, MDB_NEXT);
        if (!rc) {
            wait4id_queue_push(processingq, id_stored_to_internal((char *)key.mv_data), &data);
        } else if (rc != MDB_NOTFOUND) {
            import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Error while reading the database. Error %d: %s", rc, mdb_strerror(rc));
        }
    }
    MDB_CURSOR_CLOSE(dbc);
    TXN_ABORT(txn);
    if (!rc) {
        /* save last key for next iteration */
        memcpy(lastid, key.mv_data, sizeof (ID));
    }
    return rc;
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
    dbmdb_dbi_t *db = NULL;
    char *id2entry = NULL;
    int rc = 0;
    char lastid[sizeof (ID)] = {0};
    mdb_privdb_t *dndb = NULL;
    wait4id_queue_t *processingq = NULL;
    wait4id_queue_t *waitingq = NULL;
    wait4id_queue_t *entry = NULL;
    wait4id_queue_t **q, *e;
    WorkerQueueData_t tmpslot = {0};

    PR_ASSERT(info != NULL);
    PR_ASSERT(job->inst != NULL);

    ctx->wgc.str2entry_flags = SLAPI_STR2ENTRY_NO_ENTRYDN | SLAPI_STR2ENTRY_INCLUDE_VERSION_STR;

    wait_for_starting(info);

    /* open id2entry with dedicated db env and db handler */
    if (dbmdb_get_aux_id2entry(inst->inst_be, &db, &id2entry) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_index_producer", "Could not open id2entry\n");
        thread_abort(info);
    }

    dndb = dbmdb_import_init_entrydn_dbs(ctx);
    if (!dndb) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Failed to create normalized dn private db.");
        thread_abort(info);
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    for(;;) {
        wait_for_starting(info);
        /* Perform cleanup for aborted entries */
        if (entry) {
            wait4q_flush(&entry);
        }
        memset(&tmpslot, 0, sizeof tmpslot);
        tmpslot.winfo = *info;

        if (info_is_finished(info)) {
            break;
        }
        if (!processingq && !rc) {
            rc = fill_processingq(job, db->dbi, &processingq, lastid);
        }
        if (rc && (rc != MDB_NOTFOUND || !processingq)) {
            break;
        }

        entry = processingq;
        processingq = entry->next;
        entry->next = NULL;

        tmpslot.wait_id = entry->id;
        tmpslot.data = entry->entry.mv_data;
        tmpslot.datalen = entry->entry.mv_size;

        tmpslot.dnrc = dbmdb_import_entry_info_by_rdn(dndb, &tmpslot);
        switch (tmpslot.dnrc) {
            case DNRC_BAD_TOMBSTONE:
            default:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_index_producer",
                                  "ns_slapd software error: unexpected dbmdb_import_entry_info return code: %d.",
                                  tmpslot.dnrc);
                abort();
            case DNRC_OK:
            case DNRC_SUFFIX:
            case DNRC_TOMBSTONE:
            case DNRC_RUV:
                break;
            case DNRC_NORDN:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_index_producer",
                                  "Inconsistent id2entry database. (Entry with id %d has no rdn).", entry->id);
                slapi_ch_free(&tmpslot.data);
                thread_abort(info);
                continue;
            case DNRC_DUP:
                /* Weird: Either the id2entry db is seriously corrupted or we have queued twice the same entry */
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_index_producer",
                                  "Inconsistent id2entry database. (Entry id %d is duplicated).", entry->id);
                slapi_ch_free(&tmpslot.data);
                thread_abort(info);
                continue;
            case DNRC_BAD_SUFFIX_ID:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_index_producer",
                                  "Inconsistent id2entry database. (Suffix ID is %d instead of 1).", entry->id);
                thread_abort(info);
                continue;
            case DNRC_NOPARENT_ID:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_index_producer",
                                  "Inconsistent id2entry database. (Entry with ID %d has no parentid).", entry->id);
                thread_abort(info);
                continue;
            case DNRC_ERROR:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_index_producer",
                                  "Reindex is arborted because a LMDB database error was detected. Please check the error log for more details.");
                thread_abort(info);
                continue;
            case DNRC_WAIT:
                entry->wait4id = tmpslot.wait4id;
                entry->next = waitingq;
                waitingq = entry;
                entry = NULL;
                continue;
        }
        /* Let move the entries that are waiting for this entry into processing queue */
        for (q = &waitingq; *q;) {
            if ((*q)->wait4id == entry->id) {
                e = *q;
                *q = e->next;
                e->next = processingq;
                processingq = e;
            } else {
               q = &(*q)->next;
            }
        }

        /* Push the entry to the worker thread queue */
        dbmdb_import_workerq_push(&ctx->workerq, &tmpslot);
        slapi_ch_free((void**)&entry);
        pthread_cond_broadcast(&ctx->workerq.cv);
    }
    wait4q_flush(&processingq);
    wait4q_flush(&waitingq);
    dbmdb_privdb_destroy(&dndb);
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
    /* Set the full dn from the entry_info data */
    slapi_entry_set_dn(ep->ep_entry, slapi_ch_strdup(INFO_DN(wqelmnt->entry_info)));

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
    char *rdn = NULL;

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

/* similar to id2entry_add_ext without dn cache management (because parent entry may not exist
 *  and just queue the entry in the wrique queue rather than doing a pseudo db op
 */
int dbmdb_import_add_id2entry_add(ImportJob *job, backend *be, struct backentry *e)
{
    int encrypt = job->encrypt;
    WriterQueueData_t wqd = {0};
    int len = 0;
    int rc = 0;
    char temp_id[sizeof(ID)];
    struct backentry *encrypted_entry = NULL;
    ImportCtx_t *ctx = job->writer_ctx;
    uint32_t esize = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_import_add_id2entry_add", "=> ( %lu, \"%s\" )\n",
                  (u_long)e->ep_id, backentry_get_ndn(e));

    wqd.dbi = ctx->id2entry->dbi;
    id_internal_to_stored(e->ep_id, temp_id);
    wqd.key.mv_data = temp_id;
    wqd.key.mv_size = sizeof(temp_id);

    /* Encrypt attributes in this entry if necessary */
    if (encrypt) {
        rc = attrcrypt_encrypt_entry(be, e, &encrypted_entry);
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_add_id2entry_add",
                          "attrcrypt_encrypt_entry failed\n");
            rc = -1;
            goto done;
        }
    }
    {
        int options = SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID | SLAPI_DUMP_RDN_ENTRY;
        Slapi_Entry *entry_to_use = encrypted_entry ? encrypted_entry->ep_entry : e->ep_entry;
        wqd.data.mv_data = slapi_entry2str_with_options(entry_to_use, &len, options);
        esize = (uint32_t)len+1;
        plugin_call_entrystore_plugins((char **)&wqd.data.mv_data, &esize);
        wqd.data.mv_size = esize;
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.data.mv_data);
    }
done:
    /* If we had an encrypted entry, we no longer need it.
     * Note: encrypted_entry is not in the entry cache. */
    if (encrypted_entry) {
        backentry_free(&encrypted_entry);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "id2entry_add_ext", "<= %d\n", rc);
    return (rc);
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
        ret = dbmdb_import_add_id2entry_add(job, be, ep);
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
    }
    return 0;
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
    WriterQueueData_t wqd = {0};
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;

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
static void
process_entryrdn(backentry *ep, WorkerQueueData_t *wqelmnt)
{
    ImportWorkerInfo *info = &wqelmnt->winfo;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    backend *be = job->inst->inst_be;
    WriterQueueData_t wqd = {0};
    char key_id[10];
    char *nrdn, *rdn;
    ID id, pid;
    int n;

   if (ctx->entryrdn && wqelmnt->entry_info) {
        wqd.dbi = ctx->entryrdn->dbi;
        id = wqelmnt->entry_info[0];
        nrdn = INFO_NRDN(wqelmnt->entry_info);
        rdn = INFO_RDN(wqelmnt->entry_info);

        /* id --> id nrdn rdn */
        if (wqelmnt->dnrc == DNRC_SUFFIX) {
            wqd.key.mv_data = nrdn;
        } else {
            wqd.key.mv_data = key_id;
            PR_snprintf(key_id, (sizeof key_id), "%d", id);
        }
        wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
        wqd.data.mv_data = entryrdn_encode_data(be, &wqd.data.mv_size, id, nrdn, rdn);
        dbmdb_import_writeq_push(ctx, &wqd);
        slapi_ch_free(&wqd.data.mv_data);
        if (wqelmnt->parent_info) {
            /* 'C'+parentid --> id nrdn rdn */
            pid = wqelmnt->parent_info[0];
            PR_snprintf(key_id, (sizeof key_id), "C%d", pid);
            wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
            wqd.data.mv_data = entryrdn_encode_data(be, &wqd.data.mv_size, id, nrdn, rdn);
            dbmdb_import_writeq_push(ctx, &wqd);
            slapi_ch_free(&wqd.data.mv_data);
            /* 'P'+id --> parentid parentnrdn parentrdn */
            nrdn = INFO_NRDN(wqelmnt->parent_info);
            rdn = INFO_RDN(wqelmnt->parent_info);
            PR_snprintf(key_id, (sizeof key_id), "P%d", id);
            wqd.key.mv_size = strlen(wqd.key.mv_data)+1;
            wqd.data.mv_data = entryrdn_encode_data(be, &wqd.data.mv_size, pid, nrdn, rdn);
            dbmdb_import_writeq_push(ctx, &wqd);
            slapi_ch_free(&wqd.data.mv_data);
        }
    }

    if (ctx->parentid && wqelmnt->parent_info) {
        /* Update parentid */
        pid = wqelmnt->parent_info[0];
        wqd.dbi = ctx->parentid->dbi;
        prepare_ids(&wqd, pid, &id);
        dbmdb_import_writeq_push(ctx, &wqd);
        dbmdb_add_op_attrs(job, ep, pid);  /* Before loosing the pid */
    }

    if (ctx->ancestorid && wqelmnt->entry_info) {
        /* Update ancestorids */
        wqd.dbi = ctx->ancestorid->dbi;
        for (n=0; n<wqelmnt->entry_info[INFO_IDX_NB_ANCESTORS]; n++) {
            prepare_ids(&wqd, wqelmnt->entry_info[INFO_IDX_ANCESTORS+n], &id);
            dbmdb_import_writeq_push(ctx, &wqd);
        }
    }
}


/*
 * Note: the index_addordel functions are poorly designed for lmdb
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
                    "Import %s thread aborted after failing to update index %s.\n",
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

/* Check if attrlist and vlvlist is empty or if attrname is in vlvlist */
int
is_reindexed_attr(const char *attrname, const ImportCtx_t *ctx, char **list)
{
    if (!ctx->indexAttrs && !ctx->indexVlvs) {
        return  ((ctx->job->flags & FLAG_INDEX_ATTRS) == FLAG_INDEX_ATTRS);
    }
    return (list && attr_in_list(attrname, list));
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
        if (vlv_index && vlv_index->vlv_attrinfo &&
            is_reindexed_attr(vlv_index->vlv_attrinfo->ai_type , ctx, ctx->indexVlvs)) {
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
    WriterQueueData_t wqd = {0};

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
    ID id = info->first_ID;

    PR_ASSERT(NULL != info);
    PR_ASSERT(NULL != job->inst);


    info->state = RUNNING;
    info->last_ID_processed = id;

    while (!info_is_finished(info)) {
        wait_for_starting(info);

        /* Wait until data get queued */
        pthread_mutex_lock(&ctx->workerq.mutex);
        while (wqelmnt->wait_id == 0 && !info_is_finished(info) && ctx->producer.state != FINISHED) {
            safe_cond_wait(&ctx->workerq.cv, &ctx->workerq.mutex);
        }
        pthread_mutex_unlock(&ctx->workerq.mutex);
        if (wqelmnt->wait_id == 0) {
             break;
        }

        wqelmnt->count++;
        /* Format the backentry from the queued data */
        ep = ctx->prepare_worker_entry_fn(wqelmnt);
        if (!ep) {
            /* skipped counter is increased (or not in some cases) by the callback */
            pthread_mutex_lock(&ctx->workerq.mutex);
            wqelmnt->wait_id = 0;
            pthread_cond_broadcast(&ctx->workerq.cv);
            pthread_mutex_unlock(&ctx->workerq.mutex);
            continue;
        }
        if (info_is_finished(info)) {
             break;
        }
        process_entryrdn(ep, wqelmnt);
        slapi_ch_free((void**)&wqelmnt->entry_info);
        slapi_ch_free((void**)&wqelmnt->parent_info);
        /* At this point, entry rdn is stored in cache (and maybe in dbi) so:
         * - lets signal the producer that it can enqueue a new item.
         * - lets signal the other worker threads that parent id may
         *  be available.
         */
        pthread_mutex_lock(&ctx->workerq.mutex);
        wqelmnt->wait_id = 0;
        pthread_cond_broadcast(&ctx->workerq.cv);
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
    assert(ii);  /* System indexes should always be in the list */

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
            if (ii->ai->ai_indexmask == INDEX_VLV) {
                if (job->task) {
                    slapi_task_log_notice(job->task, "%s: Indexing VLV: %s", job->inst->inst_name, mii->name);
                }
                slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index",
                              "%s: Indexing VLV: %s\n", job->inst->inst_name, mii->name);
            } else {
                if (job->task) {
                    slapi_task_log_notice(job->task, "%s: Indexing attribute: %s", job->inst->inst_name, mii->name);
                }
                slapi_log_err(SLAPI_LOG_INFO, "dbmdb_db2index",
                              "%s: Indexing attribute: %s\n", job->inst->inst_name, mii->name);
            }
        }
    }

    dbmdb_open_dbi_from_filename(&mii->dbi, job->inst->inst_be, mii->name, mii->ai, dbi_flags);
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
                if (is_reindexed_attr(ii->ai->ai_type, ctx, ctx->indexVlvs)) {
                    dbmdb_add_import_index(ctx, NULL, ii);
                }
            } else if (is_reindexed_attr(ii->ai->ai_type, ctx, ctx->indexAttrs)){
                dbmdb_add_import_index(ctx, NULL, ii);
            }
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
    if (ctx->role != IM_INDEX) {
        ctx->id2entry = CALLOC(MdbIndexInfo_t);
        ctx->id2entry->name = (char*) slapi_utf8StrToLower((unsigned char*)ID2ENTRY);
        dbmdb_open_dbi_from_filename(&ctx->id2entry->dbi, job->inst->inst_be, ctx->id2entry->name,
                                 NULL, MDB_CREATE|MDB_MARK_DIRTY_DBI|MDB_OPEN_DIRTY_DBI|MDB_TRUNCATE_DBI);
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

/* writer.thread:
 * i go through the writer queue (unlike the other worker threads),
 * i'm responsible to write data in mdb database as I am the only
 * import thread allowed to start a read-write transaction.
 * (The other threads open read-only txn)
 */
void
dbmdb_import_writer(void*param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo*)param;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    WriterQueueData_t *nextslot = NULL;
    WriterQueueData_t *slot = NULL;
    MDB_txn *txn = NULL;
    int count = 0;
    int rc = 0;
    mdb_stat_info_t stats = {0};

    MDB_STAT_INIT(stats);
    while (!rc && !info_is_finished(info)) {
        MDB_STAT_STEP(stats, MDB_STAT_PAUSE);
        wait_for_starting(info);
        MDB_STAT_STEP(stats, MDB_STAT_READ);
        slot = dbmdb_import_q_getall(&ctx->writerq);
        MDB_STAT_STEP(stats, MDB_STAT_RUN);
        if (info_is_finished(info)) {
            dbmdb_import_q_flush(&ctx->writerq);
            break;
        }
        if (slot==NULL && have_workers_finished(job)) {
            break;
        }

        for (; slot; slot = nextslot) {
            if (!txn) {
                MDB_STAT_STEP(stats, MDB_STAT_TXNSTART);
                rc = TXN_BEGIN(ctx->ctx->env, NULL, 0, &txn);
            }
            if (!rc) {
                MDB_STAT_STEP(stats, MDB_STAT_WRITE);
                rc = MDB_PUT(txn, slot->dbi->dbi, &slot->key, &slot->data, 0);
            }
            MDB_STAT_STEP(stats, MDB_STAT_RUN);
            nextslot = slot->next;
            slapi_ch_free((void**)&slot);
        }
        if (rc) {
            break;
        }
        if  (count++ >= WRITER_MAX_OPS_IN_TXN) {
            MDB_STAT_STEP(stats, MDB_STAT_TXNSTOP);
            rc = TXN_COMMIT(txn);
            MDB_STAT_STEP(stats, MDB_STAT_RUN);
            if (rc) {
                break;
            }
            count = 0;
            txn = NULL;
        }
    }
    if (txn && !rc) {
        MDB_STAT_STEP(stats, MDB_STAT_TXNSTOP);
        rc = TXN_COMMIT(txn);
        MDB_STAT_STEP(stats, MDB_STAT_RUN);
        if (!rc) {
            txn = NULL;
        }
    }
    if (txn) {
        MDB_STAT_STEP(stats, MDB_STAT_TXNSTOP);
        TXN_ABORT(txn);
        MDB_STAT_STEP(stats, MDB_STAT_RUN);
        txn = NULL;
    }
    MDB_STAT_STEP(stats, MDB_STAT_WRITE);
    if (!rc) {
        /* Ensure that all data are written on disk */
        rc = mdb_env_sync(ctx->ctx->env, 1);
    }
    MDB_STAT_END(stats);

    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_import_writer",
                "Failed to write in the database. Error is 0x%x: %s.\n",
                rc, mdb_strerror(rc));
        thread_abort(info);
    } else {
        char buf[200];
        char *summary = mdb_stat_summarize(&stats, buf, sizeof buf);
        if (summary) {
            import_log_notice(job, SLAPI_LOG_INFO, "dbmdb_import_monitor_threads",
                              "Import writer thread usage: %s", summary);
        }
    }
    info_set_state(info);
}

/***************************************************************************/
/************************** Bulk import functions **************************/
/***************************************************************************/

static int
has_bulk_finished(ImportJob *job)
{
    ImportCtx_t *ctx = job->writer_ctx;
    return (ctx->bulkq_state == FINISHED);
}

void
free_bulk_queue_item(BulkQueueData_t **q)
{
    if ((*q)) {
        backentry_free(&(*q)->ep); /* release the backend wrapper, here */
        slapi_ch_free(&(*q)->key.mv_data);
        slapi_ch_free(&(*q)->wait4key.mv_data);
    }
    slapi_ch_free((void**)q);
}

void
free_bulk_queue_list(BulkQueueData_t **q)
{
    BulkQueueData_t *n;
    while (*q) {
        n = *q;
        *q = n->next;
        free_bulk_queue_item(&n);
    }
}

BulkQueueData_t *
dup_bulk_queue_item(BulkQueueData_t *wqd)
{
    /* Copy data in the new element */
    BulkQueueData_t *elmt = (BulkQueueData_t*)slapi_ch_calloc(1, sizeof *wqd);
    *elmt = *wqd;
    elmt->next = NULL;
    return elmt;
}

void
dbmdb_bulkq_push(ImportCtx_t *ctx, WriterQueueData_t *wqd)
{
    dbmdb_import_q_push(&ctx->writerq, wqd);
}

int
bulk_shouldwait(ImportNto1Queue_t *q)
{
    ImportJob *job = q->info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    return (ctx->bulkq_state != FINISHED && generic_shouldwait(q));
}

/* producer thread for bulk import:
 * pick up the backentries from the bulk queue
 * Compute their entry_info and parent_info then
 * queue them on the entry FIFO.
 * other threads will do the indexing -- same as in import.
 */
void
dbmdb_bulk_producer(void *param)
{
    ImportWorkerInfo *info = (ImportWorkerInfo *)param;
    ImportJob *job = info->job;
    ImportCtx_t *ctx = job->writer_ctx;
    BulkQueueData_t *processingq = NULL;
    BulkQueueData_t *waitingq = NULL;
    BulkQueueData_t *entry = NULL;
    BulkQueueData_t **q, *e;
    WorkerQueueData_t tmpslot = {0};
    mdb_privdb_t *dndb = NULL;

    PR_ASSERT(info != NULL);
    PR_ASSERT(job->inst != NULL);

    wait_for_starting(info);
    dndb = dbmdb_import_init_entrydn_dbs(ctx);
    if (!dndb) {
        import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_import_producer", "Failed to create normalized dn private db.");
        thread_abort(info);
    }

    /* we loop around reading the input files and processing each entry
     * as we read it.
     */
    for(;;) {
        wait_for_starting(info);
        /* Perform cleanup for aborted entries */
        if (entry) {
           free_bulk_queue_item(&entry);
        }
        memset(&tmpslot, 0, sizeof tmpslot);
        tmpslot.winfo.job = job;
        if (!processingq) {
            processingq = dbmdb_import_q_getall(&ctx->bulkq);
        }
        if (info_is_finished(info) || (!processingq && has_bulk_finished(job))) {
            break;
        }
        if (!processingq) {
            continue;
        }

        entry = processingq;
        processingq = entry->next;
        entry->next = NULL;

        tmpslot.wait_id = entry->id;
        tmpslot.data = entry->ep;
        tmpslot.datalen = 0;

        tmpslot.dnrc = dbmdb_import_entry_info_by_backentry(dndb, entry, &tmpslot);
        switch (tmpslot.dnrc) {
            default:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "ns_slapd software error: unexpected dbmdb_import_entry_info return code: %d.",
                                  tmpslot.dnrc);
                abort();
            case DNRC_OK:
            case DNRC_SUFFIX:
            case DNRC_TOMBSTONE:
            case DNRC_RUV:
                break;
            case DNRC_NORDN:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "Supplier's entry is inconsistent. (Entry with id %d has no rdn).", entry->id);
                slapi_ch_free(&tmpslot.data);
                thread_abort(info);
                continue;
            case DNRC_DUP:
                /* Weird: Either the id2entry db is seriously corrupted or we have queued twice the same entry */
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "Supplier's entry is inconsistent. (Entry id %d is duplicated).", entry->id);
                slapi_ch_free(&tmpslot.data);
                thread_abort(info);
                continue;
            case DNRC_BAD_SUFFIX_ID:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "Supplier's entry is inconsistent. (Suffix ID is %d instead of 1).", entry->id);
                thread_abort(info);
                continue;
            case DNRC_NOPARENT_ID:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "Supplier's entry is inconsistent. (Entry with ID %d has no parentid).", entry->id);
                thread_abort(info);
                continue;
            case DNRC_BAD_TOMBSTONE:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "Supplier's entry is inconsistent. (Tombstone Entry with ID %d has no nsparentuniqueid or no nsuniqueid attributes).", entry->id);
                thread_abort(info);
                continue;
            case DNRC_ERROR:
                import_log_notice(job, SLAPI_LOG_ERR, "dbmdb_bulk_producer",
                                  "Reindex is arborted because a LMDB database error was detected. Please check the error log for more details.");
                thread_abort(info);
                continue;
            case DNRC_WAIT:
                entry->next = waitingq;
                waitingq = entry;
                entry = NULL;
                continue;
        }
        /* Let move the entries that are waiting for this entry into processing queue */
        for (q = &waitingq; *q;) {
            if (cmp_data(&(*q)->wait4key, &entry->key) == 0) {
                e = *q;
                slapi_ch_free(&e->wait4key.mv_data);
                e->wait4key.mv_size = 0;
                *q = e->next;
                e->next = processingq;
                processingq = e;
            } else {
               q = &(*q)->next;
            }
        }

        /* Push the entry to the worker thread queue */
        dbmdb_import_workerq_push(&ctx->workerq, &tmpslot);
        entry->ep = NULL; /* Should not free the backentry which is now owned by worker queue */
        free_bulk_queue_item(&entry);
        pthread_cond_broadcast(&ctx->workerq.cv);
    }
    free_bulk_queue_list(&processingq);
    free_bulk_queue_list(&waitingq);
    dbmdb_privdb_destroy(&dndb);
    info_set_state(info);
}


/***************************************************************************/
/************************* WRITER thread functions *************************/
/***************************************************************************/

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

void dbmdb_dup_writer_slot(struct importqueue *q, void *from_slot, void *to_slot)
{
	/* Copy the WriterQueueData_t slot */
    WriterQueueData_t *from = from_slot;
    WriterQueueData_t *to = to_slot;
    *to = *from;
}

void
free_writer_queue_item(WriterQueueData_t **q)
{
    slapi_ch_free((void**)q);
}

WriterQueueData_t *
dup_writer_queue_item(WriterQueueData_t *wqd)
{
    /* Copy data in the new element */
    int len = sizeof (WriterQueueData_t) + wqd->key.mv_size + wqd->data.mv_size;
    WriterQueueData_t *elmt = (WriterQueueData_t*)slapi_ch_calloc(1, len);
    *elmt = *wqd;
    elmt->key.mv_data = &elmt[1];
    memcpy(elmt->key.mv_data, wqd->key.mv_data, wqd->key.mv_size);
    elmt->data.mv_data = ((char*)&elmt[1])+wqd->key.mv_size;
    memcpy(elmt->data.mv_data, wqd->data.mv_data, wqd->data.mv_size);
    return elmt;
}

void dbmdb_import_writeq_push(ImportCtx_t *ctx, WriterQueueData_t *wqd)
{
    dbmdb_import_q_push(&ctx->writerq, wqd);
}

int
writer_shouldwait(ImportNto1Queue_t *q)
{
    ImportJob *job = q->info->job;
    return (!have_workers_finished(job) && generic_shouldwait(q));
}

int
dbmdb_import_init_writer(ImportJob *job, ImportRole_t role)
{
    ImportCtx_t *ctx = CALLOC(ImportCtx_t);
    struct ldbminfo *li = (struct ldbminfo *)job->inst->inst_be->be_database->plg_private;
    int nbcpus = util_get_capped_hardware_threads(0, 0x7fffffff);
    int nbworkers = nbcpus - NB_EXTRA_THREAD;
    WorkerQueueData_t *s = NULL;
    int i;

    job->writer_ctx = ctx;
    ctx->job = job;
    ctx->ctx = MDB_CONFIG(li);
    ctx->role = role;
    if (nbworkers > MAX_WORKER_SLOTS) {
        nbworkers = MAX_WORKER_SLOTS;
    }
    if (nbworkers < MIN_WORKER_SLOTS) {
        nbworkers = MIN_WORKER_SLOTS;
    }

    /* Lets initialize the worker infos and the queues */
    dbmdb_import_workerq_init(job, &ctx->workerq, (sizeof (WorkerQueueData_t)), nbworkers);
    dbmdb_import_init_worker_info(&ctx->writer, job, WRITER, "writer", 0);
    /* Initialize writer queue while job->worker_list is still the writer info */
    dbmdb_import_q_init(&ctx->writerq, job->worker_list, WRITER_SLOTS);
    ctx->writerq.dupitem_cb = (void*(*)(void*))dup_writer_queue_item;
    ctx->writerq.freeitem_cb = (void (*)(void **))free_writer_queue_item;
    ctx->writerq.shouldwait_cb = writer_shouldwait;
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
            dbmdb_import_init_worker_info(&ctx->producer, job, PRODUCER, "bulk import producer", 0);
            ctx->prepare_worker_entry_fn = dbmdb_bulkimport_prepare_worker_entry;
            ctx->producer_fn = dbmdb_bulk_producer ;
            dbmdb_import_q_init(&ctx->bulkq, job->worker_list, nbworkers);
            ctx->bulkq.dupitem_cb = (void*(*)(void*))dup_bulk_queue_item;
            ctx->bulkq.freeitem_cb = (void (*)(void **))free_bulk_queue_item;
            ctx->bulkq.shouldwait_cb = bulk_shouldwait;
            break;
   }
   return 0;
}

void
dbmdb_free_import_ctx(ImportJob *job)
{
    if (job->writer_ctx) {
        ImportCtx_t *ctx = job->writer_ctx;
        job->writer_ctx = NULL;
        pthread_mutex_destroy(&ctx->workerq.mutex);
        pthread_cond_destroy(&ctx->workerq.cv);
        slapi_ch_free((void**)&ctx->workerq.slots);
        dbmdb_import_q_destroy(&ctx->writerq);
        dbmdb_import_q_destroy(&ctx->bulkq);
        slapi_ch_free((void**)&ctx->id2entry->name);
        slapi_ch_free((void**)&ctx->id2entry);
        avl_free(ctx->indexes, (IFP) free_ii);
        ctx->indexes = NULL;
        charray_free(ctx->indexAttrs);
        charray_free(ctx->indexVlvs);
        slapi_ch_free((void**)&ctx);
    }
}

/***************************************************************************/
/******************** Performance statistics functions *********************/
/***************************************************************************/

static inline void __attribute__((always_inline))
_add_delta_time(struct timespec *cumul, struct timespec *now, struct timespec *last)
{
    struct timespec tmp1;
    struct timespec tmp2;
    #define NSINS 1000000000

    /* tmp1 = now -last */
    if (now->tv_nsec < last->tv_nsec) {
        now->tv_sec--;
        now->tv_nsec += NSINS;
    }
    tmp1.tv_sec = now->tv_sec - last->tv_sec;
    tmp1.tv_nsec = now->tv_nsec - last->tv_nsec;
    /* tmp2 = cumul + tmp1 */
    tmp2.tv_sec = cumul->tv_sec + tmp1.tv_sec;
    tmp2.tv_nsec = cumul->tv_nsec + tmp1.tv_nsec;
    if (tmp2.tv_nsec > NSINS) {
        tmp2.tv_nsec -= NSINS;
        tmp2.tv_sec++;
    }
    /* cumul = tmp2 */
    *cumul = tmp2;
}

static inline double __attribute__((always_inline))
_time_to_double(struct timespec *t)
{
    double res = t->tv_sec;
    res += t->tv_nsec / 1000000000.0;
    return res;
}

void
mdb_stat_collect(mdb_stat_info_t *sinfo, mdb_stat_step_t step, int init)
{
    struct timespec now;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    if (!init) {
        _add_delta_time(&sinfo->steps[sinfo->last_step].realtime, &now,  &sinfo->last.realtime);
    }
    sinfo->last.realtime = now;
    sinfo->last_step = step;
}

char *
mdb_stat_summarize(mdb_stat_info_t *sinfo, char *buf, size_t bufsize)
{
    static const char *names[MDB_STAT_LAST_STEP] = MDB_STAT_STEP_NAMES;
    double v[MDB_STAT_LAST_STEP];
    double total = 0.0;
    char tmp[50];
    int pos = 0;
    int len = 0;

    if (!sinfo) {
        return NULL;
    }

    for (size_t i=0; i<MDB_STAT_LAST_STEP; i++) {
        v[i] = _time_to_double(&sinfo->steps[i].realtime);
        total += v[i];
    }
    if (total > 0.0) {
        for (size_t i=0; i<MDB_STAT_LAST_STEP; i++) {
            double percent = 100.0 * v[i] / total;
            PR_snprintf(tmp, (sizeof tmp), "%s: %.2f%% ", names[i], percent);
            len = strlen(tmp);
            if (pos+len+4 < bufsize) {
                strcpy(buf+pos, tmp);
                pos += len;
            } else {
                strcpy(buf+pos, "...");
                break;
            }
        }
    }
    return buf;
}
