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
#define IMPORT_INDEX_BUFFER_SIZE_CONSTANT (20*20*20*sizeof(ID))

static const int import_sleep_time = 200;	/* in millisecs */

extern char *numsubordinates;
extern char *hassubordinates;

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
typedef struct {
    struct backentry *entry;
    char *filename;		/* or NULL */
    int line;			/* filename/line are used to report errors */
    int bad;                    /* foreman did not like the entry */
	size_t esize;		/* entry size */
} FifoItem;

typedef struct {
    FifoItem *item;
    size_t size;    /* Queue size in entries (computed in import_fifo_init). */
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
typedef struct {
    ldbm_instance *inst;	/* db instance we're importing to */
    Slapi_Task *task;           /* cn=tasks entry ptr */
    int flags;			/* (see below) */
    char **input_filenames;	/* NULL-terminated list of charz pointers */
    IndexInfo *index_list;	/* A list of indexing jobs to do */
    ImportWorkerInfo *worker_list;	/* A list of threads to work on the
					 * indexes */
    size_t number_indexers;	/* count of the indexer threads (not including
				 * the primary) */
    ID starting_ID;		/* Import starts work at this ID */
    ID first_ID;		/* Import pass starts at this ID */
    ID lead_ID;			/* Highest ID available in the cache */
    ID ready_ID;		/* Highest ID the foreman is done with */
    ID ready_EID;		/* Highest Entry ID the foreman is done with */
    ID trailing_ID;		/* Lowest ID still available in the cache */
    int current_pass;		/* un-merged pass number in a multi-pass import */
    int total_pass;		/* total pass number in a multi-pass import */
    int skipped;                /* # entries skipped because they were bad */
    int not_here_skipped;      /* # entries skipped because they belong
								* to another backend */
    size_t merge_chunk_size;	/* Allows us to manually override the magic
				 * voodoo logic for deciding when to begin
				 * another pass */
    int uuid_gen_type;          /* kind of uuid to generate */
    char *uuid_namespace;       /* namespace for name-generated uuid */
    import_subcount_stuff *mothers;
    double average_progress_rate;
    double recent_progress_rate;
    double cache_hit_ratio;
    time_t start_time;
    ID progress_history[IMPORT_JOB_PROG_HISTORY_SIZE];
    time_t progress_times[IMPORT_JOB_PROG_HISTORY_SIZE];
    size_t job_index_buffer_size;	/* Suggested size of index buffering 
					 * for all indexes */
    size_t job_index_buffer_suggestion;	/* Suggested size of index buffering
					 * for one index */
    char **include_subtrees;	/* list of subtrees to import */
    char **exclude_subtrees;	/* list of subtrees to NOT import */
    Fifo fifo;			/* entry fifo for indexing */
    char *task_status;		/* transient state info for the end-user */
    PRLock *wire_lock;          /* lock for serializing wire imports */
    PRCondVar *wire_cv;         /* ... and ordering the startup */
    PRThread *main_thread;      /* for FRI: import_main() thread id */
	int encrypt;
} ImportJob;

#define FLAG_INDEX_ATTRS	0x01	/* should we index the attributes? */
#define FLAG_USE_FILES		0x02	/* import from files */
#define FLAG_PRODUCER_DONE      0x04 /* frontend is done sending entries
                                      * for replica initialization */
#define FLAG_ABORT              0x08 /* import has been aborted */
#define FLAG_ONLINE             0x10 /* bring backend online when done */
#define FLAG_REINDEXING         0x20 /* read from id2entry and do indexing */
#define FLAG_UPGRADEDNFORMAT    0x40 /* read from id2entry and do upgrade dn */
#define FLAG_DRYRUN             0x80 /* dryrun for upgrade dn */


/* Structure holding stuff about a worker thread and what it's up to */
struct _import_worker_info {
    int work_type;		/* What sort of work is this ? */
    int command;		/* Used to control the thread */
    int state;			/* Thread indicates its state here */
    IndexInfo *index_info;	/* info on what we're asked to do */
    ID last_ID_processed;
    ID previous_ID_counted;	/* Used by the monitor to calculate progress
				 * rate */
    double rate;		/* Number of IDs processed per second */
    ID first_ID;		/* Tell the thread to start at this ID */
    ImportJob *job;
    ImportWorkerInfo *next;
    size_t index_buffer_size;	/* Size of index buffering for this index */
};

/* Values for work_type */
#define WORKER		1
#define FOREMAN		2
#define PRODUCER	3

/* Values for command */
#define RUN 1
#define PAUSE 2
#define ABORT 3
#define STOP 4

/* Values for state */
#define WAITING 1
#define RUNNING 2
#define FINISHED 3
#define ABORTED 4
#define QUIT 5 /* quit intentionally. to distinguish from ABORTED & FINISHED */

/* this is just a convenience, because the slapi_ch_* calls are annoying */
#define CALLOC(name)	(name *)slapi_ch_calloc(1, sizeof(name))
#define FREE(x)		slapi_ch_free((void **)&(x))


/* import.c */
FifoItem *import_fifo_fetch(ImportJob *job, ID id, int worker);
void import_free_job(ImportJob *job);
void import_log_notice(ImportJob *job, char *format, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 2, 3)));
#else
        ;
#endif

void import_abort_all(ImportJob *job, int wait_for_them);
int import_entry_belongs_here(Slapi_Entry *e, backend *be);
int import_make_merge_filenames(char *directory, char *indexname, int pass,
				char **oldname, char **newname);
void import_main(void *arg);
int import_main_offline(void *arg);
int ldbm_back_ldif2ldbm_deluxe(Slapi_PBlock *pb);

/* import-merge.c */
int import_mega_merge(ImportJob *job);

/* ldif2ldbm.c */
void reset_progress( void );
void report_progress( int count, int done );
int add_op_attrs(Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *ep,
		 int *status);

/* import-threads.c */
void import_producer(void *param);
void index_producer(void *param);
void upgradedn_producer(void *param);
void import_foreman(void *param);
void import_worker(void *param);
