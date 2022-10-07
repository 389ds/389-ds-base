/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H

#include <config.h>
#endif
#include "mdb_layer.h"

#define LONGALIGN(len)         (((len)+(sizeof (long))-1) & ~((sizeof (long))-1))
#define ELEMRDN(elem)          (&elem->nrdn[elem->nrdnlen])
#define WRITER_SLOTS           2000
#define WRITER_MAX_OPS_IN_TXN  2000
#define NB_EXTRA_THREAD        3    /* monitoring, producer and writer */
#define MIN_WORKER_SLOTS       4
#define MAX_WORKER_SLOTS       64

#define ERR_DUPLICATE_DN    0x10


typedef enum { IM_UNKNOWN, IM_IMPORT, IM_INDEX, IM_UPGRADE, IM_BULKIMPORT } ImportRole_t;

typedef struct importctx ImportCtx_t;

typedef enum {
    DNRC_OK,
    DNRC_NODN,
    DNRC_SUFFIX,
    DNRC_BAD_SUFFIX_ID,
    DNRC_NOPARENT_DN,
    DNRC_NOPARENT_ID,
    DNRC_TOMBSTONE,
    DNRC_DUP,
    DNRC_RUV,
    DNRC_BADDN,
    DNRC_ERROR,
} dnrc_t;

/******************** Queues ********************/

typedef struct {
    ImportWorkerInfo winfo;
    volatile int count; /* Number of processed entries since thread is started */
    volatile int wait_id; /* current entry ID */
    int lineno;         /* entry first line number in ldif file */
    int nblines;        /* number of lines of the entry within ldif file */
    char *filename;     /* ldif file name */
    void *data;         /* entry string extracted from ldif line */
    int datalen;        /* len of data in bytes */
    ID *parent_info;    /* private database record of parent entry */
    ID *entry_info;     /* private database record of current entry */
    dnrc_t dnrc;        /* current entry status */
    char *dn;           /* current entry dn */
    char padding[56];   /* Lets try to align on 64 bytes cache line */
} WorkerQueueData_t;

typedef struct writerqueuedata {
    dbmdb_dbi_t *dbi;
    MDB_val key;
    MDB_val data;
    struct writerqueuedata *next;
} WriterQueueData_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    volatile WriterQueueData_t *list;       /* Incoming list */
    volatile int count;                     /* Approximative number of items in incomming list */
    volatile WriterQueueData_t *outlist;    /* processing list */
} WriterQueue_t;


#define IQ_GET_SLOT(q, idx, _struct)    ((_struct *)(&(q)->slots[(idx)*(q)->slot_size]))

typedef struct importqueue {
    ImportJob *job;
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    int slot_size;
    int max_slots;
    int used_slots;
    WorkerQueueData_t *slots;
} ImportQueue_t;

/******************** Global context ********************/

typedef struct _mdb_index_info {
    char *name;
    struct attrinfo *ai;
    int flags;
    dbmdb_dbi_t *dbi;
    struct _mdb_index_info *next;
} MdbIndexInfo_t;

typedef struct {
    int str2entry_flags;
    int my_version;
    int version_found;
} ImportWorkerGlobalContext_t;

/* and one to control them all ... */
struct importctx {
    ImportJob *job;
    dbmdb_ctx_t *ctx;
    MdbIndexInfo_t *entryrdn;
    MdbIndexInfo_t *parentid;
    MdbIndexInfo_t *ancestorid;
    MdbIndexInfo_t *numsubordinates;
    MdbIndexInfo_t *id2entry;
    ImportRole_t role;
    ImportQueue_t workerq;
    WriterQueue_t writerq;;
    Avlnode *indexes;  /* btree of MdbIndexInfo_t */
    ImportWorkerInfo producer;
    struct backentry *(*prepare_worker_entry_fn)(WorkerQueueData_t *wqelmnt);
    void (*producer_fn)(void *arg);
    ImportWorkerInfo writer;
    ImportWorkerGlobalContext_t wgc;
    char **indexAttrs;  /* reindex index to rebuild */
    ID idsuffix;
    ID idruv;
    int dupdn;
};

/******************** Functions ********************/

/* mdb_import.c */
int dbmdb_run_ldif2db(Slapi_PBlock *pb);


/* mdb_import_threads.c */
void safe_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);

struct backentry *dbmdb_import_make_backentry(Slapi_Entry *e, ID id);
int _get_import_entryusn(ImportJob *job, Slapi_Value **usn_value);
int dbmdb_import_generate_uniqueid(ImportJob *job, Slapi_Entry *e);


int dbmdb_import_workerq_init(ImportJob *job, ImportQueue_t *q, int slotelmtsize, int maxslots);
int dbmdb_import_workerq_push(ImportQueue_t *q, WorkerQueueData_t *data);


void *dbmdb_get_free_worker_slot(struct importqueue *q);
void dbmdb_dup_worker_slot(struct importqueue *q, void *from_slot, void *to_slot);
void dbmdb_free_worker_slot(struct importqueue *q, void *slot);


int dbmdb_import_init_writer(ImportJob *job, ImportRole_t role);
void dbmdb_free_import_ctx(ImportJob *job);
void dbmdb_build_import_index_list(ImportCtx_t *ctx);
