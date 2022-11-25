/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
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
 * structures & constants used for the import code
 */


/* Number of lines in the entry above which we switch to
   using a tree to check for attribute presence in str2entry().
 */
#define STR2ENTRY_ATTRIBUTE_PRESENCE_CHECK_THRESHOLD 100

#define IMPORT_ADD_OP_ATTRS_OK 0
#define IMPORT_ADD_OP_ATTRS_NO_PARENT 1
#define IMPORT_ADD_OP_ATTRS_SAVE_OLD_PID 2

#define IMPORT_COMPLETE_PASS 1
#define IMPORT_INCOMPLETE_PASS 2

/* Constants for index buffering */
#define IMPORT_MAX_INDEX_BUFFER_SIZE 100
#define IMPORT_MIN_INDEX_BUFFER_SIZE 5
#define IMPORT_INDEX_BUFFER_SIZE_CONSTANT (20 * 20 * 20 * sizeof(ID))

#define WORKER_NAME_LEN 50

static const int import_sleep_time = 200; /* in millisecs */

extern char *numsubordinates;
extern char *hassubordinates;
extern char *tombstone_numsubordinates;

typedef struct _import_worker_info ImportWorkerInfo;
typedef struct _import_index_info IndexInfo;


/* structure which describes an indexing job */
struct _import_index_info
{
    char *name;
    struct attrinfo *ai;
    IndexInfo *next;
};

/* item on the entry FIFO */
typedef struct
{
    struct backentry *entry;
    char *filename; /* or NULL */
    int line;       /* filename/line are used to report errors */
    int bad;        /* foreman did not like the entry */
    size_t esize;   /* entry size */
} FifoItem;

#define FIFOITEM_BAD 1
#define FIFOITEM_BAD_PRINTED 2

typedef struct
{
    FifoItem *item;
    size_t size;    /* Queue size in entries (computed in bdb_import_fifo_init). */
    size_t bsize;   /* Queue limitation in max bytes */
    size_t c_bsize; /* Current queue size in bytes */
} Fifo;

/* notes on the import gang:
 * 1. producer: reads the file(s), performs str2entry() and assigns IDs.
 *    job->lead_ID is the last entry in the FIFO it's decoded.  as it
 *    circles the FIFO, it pauses whenever it runs into an entry with a
 *    non-zero refcount, and waits for the worker threads to finish.
 * 2. foreman: reads the FIFO (up to lead_ID), adding operational attrs,
 *    and creating the entrydn & id2entry indexes.  job->ready_ID is the
 *    last entry in the FIFO it's finished with.  (workers can't browse
 *    the entries it's working on because it's effectively modifying the
 *    entry.)
 * 3. workers (one for each other index): read the FIFO (up to ready_ID),
 *    creating the index for a particular attribute.
 */

/* Structure holding stuff about the whole import job */
#define IMPORT_JOB_PROG_HISTORY_SIZE 3
typedef struct _ImportJob
{
    ldbm_instance *inst;           /* db instance we're importing to */
    Slapi_Task *task;              /* cn=tasks entry ptr */
    int flags;                     /* (see below) */
    char **input_filenames;        /* NULL-terminated list of charz pointers */
    IndexInfo *index_list;         /* A list of indexing jobs to do */
    ImportWorkerInfo *worker_list; /* A list of threads context for
                                    * producer,foreman,worker and writer threads */
    size_t number_indexers;        /* count of the indexer threads (not including
                                    * the primary) */
    ID starting_ID;                /* Import starts work at this ID */
    ID first_ID;                   /* Import pass starts at this ID */
    ID lead_ID;                    /* Highest ID available in the cache */
    ID ready_ID;                   /* Highest ID the foreman is done with */
    ID ready_EID;                  /* Highest Entry ID the foreman is done with */
    ID trailing_ID;                /* Lowest ID still available in the cache */
    int current_pass;              /* un-merged pass number in a multi-pass import */
    int total_pass;                /* total pass number in a multi-pass import */
    int skipped;                   /* # entries skipped because they were bad */
    int not_here_skipped;          /* # entries skipped because they belong
                                    * to another backend */
    size_t merge_chunk_size;       /* Allows us to manually override the magic
                                    * voodoo logic for deciding when to begin
                                    * another pass */
    int uuid_gen_type;             /* kind of uuid to generate */
    char *uuid_namespace;          /* namespace for name-generated uuid */
    import_subcount_stuff *mothers;
    double average_progress_rate;
    double recent_progress_rate;
    double cache_hit_ratio;
    time_t start_time;
    ID progress_history[IMPORT_JOB_PROG_HISTORY_SIZE];
    time_t progress_times[IMPORT_JOB_PROG_HISTORY_SIZE];
    size_t job_index_buffer_size;       /* Suggested size of index buffering
                     * for all indexes */
    size_t job_index_buffer_suggestion; /* Suggested size of index buffering
                     * for one index */
    char **include_subtrees;            /* list of subtrees to import */
    char **exclude_subtrees;            /* list of subtrees to NOT import */
    Fifo fifo;                          /* entry fifo for indexing */
    char *task_status;                  /* transient state info for the end-user */
    pthread_mutex_t wire_lock;          /* lock for serializing wire imports */
    pthread_cond_t wire_cv;             /* ... and ordering the startup */
    PRThread *main_thread;              /* for FRI: bdb_import_main() thread id */
    int encrypt;
    Slapi_Value *usn_value; /* entryusn for import */
    FILE *upgradefd;        /* used for the upgrade */
    int numsubordinates;
    void *writer_ctx;        /* Context used to push data in worker thread */
} ImportJob;

#define FLAG_INDEX_ATTRS 0x01         /* should we index the attributes? */
#define FLAG_USE_FILES 0x02           /* import from files */
#define FLAG_PRODUCER_DONE 0x04       /* frontend is done sending entries \
                                       * for replica initialization */
#define FLAG_ABORT 0x08               /* import has been aborted */
#define FLAG_ONLINE 0x10              /* bring backend online when done */
#define FLAG_REINDEXING 0x20          /* read from id2entry and do indexing */
#define FLAG_DN2RDN 0x40              /* modify backend to the rdn format */
#define FLAG_UPGRADEDNFORMAT 0x80     /* read from id2entry and do upgrade dn */
#define FLAG_DRYRUN 0x100             /* dryrun for upgrade dn */
#define FLAG_UPGRADEDNFORMAT_V1 0x200 /* taking care multiple spaces in dn */


/* Structure holding stuff about a worker thread and what it's up to */
struct _import_worker_info
{
    int work_type;         /* What sort of work is this ? */
    int command;           /* Used to control the thread */
    int state;             /* Thread indicates its state here */
    IndexInfo *index_info; /* info on what we're asked to do */
    ID last_ID_processed;
    ID previous_ID_counted; /* Used by the monitor to calculate progress
                 * rate */
    double rate;            /* Number of IDs processed per second */
    ID first_ID;            /* Tell the thread to start at this ID */
    ImportJob *job;
    ImportWorkerInfo *next;
    size_t index_buffer_size; /* Size of index buffering for this index */
    char name[WORKER_NAME_LEN]; /* For debug */
    void *mdb_stat;         /* Performance statistics in lmdb case */
};

/* Values for work_type */
#define WORKER 1
#define FOREMAN 2
#define PRODUCER 3
#define WRITER 4    /* For MDB */

/* Values for command */
#define RUN 1
#define PAUSE 2
#define ABORT 3
#define STOP 4

/* Values for job state */
#define WAITING 0x1
#define RUNNING 0x2
#define FINISHED 0x4
#define ABORTED 0x8
#define QUIT 0x10 /* quit intentionally. \
                   * introduced to distinguish from ABORTED, FINISHED */

#define PROGRESS_INTERVAL 100000 /* number of IDs processed that triggers an update */

#define CORESTATE 0xff
#define DN_NORM 0x100    /* do dn normalization in upgrade */
#define DN_NORM_SP 0x200 /* do dn normalization for multi spaces in upgrade */
#define DN_NORM_BT (DN_NORM | DN_NORM_SP)

/* this is just a convenience, because the slapi_ch_* calls are annoying */
#define CALLOC(name) (name *)slapi_ch_calloc(1, sizeof(name))
#define FREE(x) slapi_ch_free((void **)&(x))


/* import.c */
void import_log_notice(ImportJob *job, int log_level, char *subsystem, char *format, ...);
int import_main_offline(void *arg);

/* ldif2ldbm.c */
void reset_progress(void);
void report_progress(int count, int done);

