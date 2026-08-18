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

#define DNRC_IS_ENTRY(dnrc)  (((dnrc) & DNRC_ERROR) == 0)

typedef enum {
    DNRC_OK,              /* Regular entry */
    DNRC_RUV,             /* RUV entry */
    DNRC_SUFFIX,          /* Suffix entry */
    DNRC_TOMBSTONE,       /* Tombstone entry */
    DNRC_ERROR = 0x100,   /* Some lmdb error occured */
    DNRC_BADDN,           /* Invalid DN syntax */
    DNRC_BAD_SUFFIX_ID,   /* DN is the backend suffix and the entry ID != 1 */
    DNRC_DUP,             /* DN already exist in the private database */
    DNRC_NODN,            /* No dn: in entry string */
    DNRC_NOPARENT_DN,     /* Entry DN has a single rdn */
    DNRC_NOPARENT_ID,     /* Parent info record is not found or no parenid: in entry */
    DNRC_NORDN,           /* No rdn: in entry string */
    DNRC_VERSION,         /* Not an entry but initial ldif version string */
    DNRC_WAIT,            /* Parent id not yet in private db */
    DNRC_POSPONE_RUV,     /* RUV entry is before suffix entry */
    DNRC_BAD_TOMBSTONE,   /* Invalid tombstone entry */
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
    ID wait4id;         /* parent ID which is waiting for */
    char padding[46];   /* Lets try to align on 64 bytes cache line */
} WorkerQueueData_t;

typedef struct writerqueuedata {
    struct writerqueuedata *next;  /* Must be first */
    dbmdb_dbi_t *dbi;
    MDB_val key;
    MDB_val data;
} WriterQueueData_t;

typedef struct bulkqueuedata {
    struct bulkqueuedata *next;    /* Must be first */
    struct backentry *ep;
    ID id;
    MDB_val key;
    MDB_val wait4key;
} BulkQueueData_t;

/* A queue used by max_slots consumer threads */
typedef struct importqueue {
    ImportJob *job;
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    int slot_size;
    int max_slots;
    int used_slots;
    WorkerQueueData_t *slots;
} ImportQueue_t;

/* A queue used when having N provider threads but a single consumer thread */
typedef struct importnto1queue ImportNto1Queue_t;
struct importnto1queue {
    ImportWorkerInfo *info; /* Info of the thread that process the queue items */
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    volatile void *list;    /* We do not really care about the exact struct type as far as next is the first field */
    int maxitems;           /* Maximum queue size before waiting when pushing items in queue */
    int minitems;           /* The minimum number of items before the reader tries to get the items */
    volatile int nbitems;   /* Queue size */
    void *(*dupitem_cb)(void *);
    void (*freeitem_cb)(void **);
    int (*shouldwait_cb)(ImportNto1Queue_t *);
};

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
    MdbIndexInfo_t *redirect;
    MdbIndexInfo_t *parentid;
    MdbIndexInfo_t *ancestorid;
    MdbIndexInfo_t *numsubordinates;
    MdbIndexInfo_t *id2entry;
    ImportRole_t role;
    ImportQueue_t workerq;
    ImportNto1Queue_t writerq;
    ImportNto1Queue_t bulkq;
    Avlnode *indexes;  /* btree of MdbIndexInfo_t */
    ImportWorkerInfo producer;
    struct backentry *(*prepare_worker_entry_fn)(WorkerQueueData_t *wqelmnt);
    void (*producer_fn)(void *arg);
    ImportWorkerInfo writer;
    ImportWorkerGlobalContext_t wgc;
    char **indexAttrs;  /* reindex index to rebuild */
    char **indexVlvs;  /* reindex vlv index to rebuild */
    ID idsuffix;
    ID idruv;
    int dupdn;
    int bulkq_state;
};

/******************** Functions ********************/

/* mdb_import.c */
int dbmdb_run_ldif2db(Slapi_PBlock *pb);


/* mdb_import_threads.c */
void safe_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);

struct backentry *dbmdb_import_make_backentry(Slapi_Entry *e, ID id);
int _get_import_entryusn(ImportJob *job, Slapi_Value **usn_value);
int dbmdb_import_generate_uniqueid(ImportJob *job, Slapi_Entry *e);


void dbmdb_import_q_push(ImportNto1Queue_t *q, void *item);
int dbmdb_import_workerq_init(ImportJob *job, ImportQueue_t *q, int slotelmtsize, int maxslots);
int dbmdb_import_workerq_push(ImportQueue_t *q, WorkerQueueData_t *data);


void *dbmdb_get_free_worker_slot(struct importqueue *q);
void dbmdb_dup_worker_slot(struct importqueue *q, void *from_slot, void *to_slot);
void dbmdb_free_worker_slot(struct importqueue *q, void *slot);


int dbmdb_import_init_writer(ImportJob *job, ImportRole_t role);
void dbmdb_free_import_ctx(ImportJob *job);
void dbmdb_build_import_index_list(ImportCtx_t *ctx);

int is_reindexed_attr(const char *attrname, const ImportCtx_t *ctx, char **list);
