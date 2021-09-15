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
 * producer (1)
 * foreman (1)
 * worker (N: 1 for each index)
 * writer (1)
 *
 * a wire import (aka "fast replica" import) won't have a producer thread.
 */

 /* The writer thread context that is provided as aback_txn when handling indexes */
 typedef struct {
     back_txn txn;            /* pseudo back_txn for backend level */
     ImportJob *job;
     ImportWorkerInfo *info;
     dbmdb_wctx_id_t wctx_id;
     int wqslot;              /*Slotindexinglobal_writer_ctx_t*/
 } pseudo_back_txn_t;

 /* Buffer info when reading tmpfile (for writer thread queue in delayed mode (i.e upgrading id2index)) */
 typedef struct {
     char*buf;
     char*bufend;
     char*buflimit;
     char*cur;
 } wq_reader_state_t;

/* wqelem_tflags */
#define WQFL_SYNC_OP        1    /* The operation is synchronous */
#define WQFL_SYNC_DONE      2    /* The synchronous operation is done */

 /* Writer queue element */
 typedef struct wqelem {
     struct wqelem *next;
     dbmdb_waction_t action;
     size_t len;                /* elem total lenght */
     size_t keylen;
     size_t data_len;
     struct wqslot *slot;       /* Associated dbi */
     /* Synchronous write operation specific fields */
     int flags;                 /* Tells that operation is completed */
     int rc;                    /* returncode */
     pthread_mutex_t *response_mutex;
     pthread_cond_t  *response_cv;
     /* Key and data values */
     unsigned char values[1];   /* keylen+data_len-shouldbelastfield */
 } wqelem_t;


 /* Writer slot (holding the queue) and associated to a worker thread (or foreman thread) and a single db instance */
 typedef struct wqslot {
     FILE *tmpfile;            /* storage for delayed slots */
     char *tmpfilepath;        /* tmpfile path */
     char *dbipath;            /* dbi dbname */
     volatile int closed;      /* Tells that slot is clsosed */
     dbmdb_dbi_t dbi;          /* database instanc e*/
     dbi_cursor_t cursor;      /* writer thread cursor associated with dbi */
     int idl_disposition;      /* index disposition stored in slot because the one in worker thread stack is not safe */
 } wqslot_t;

 /* The writer thread global context anchored in ImportJob */
typedef struct {
        /* Queue management */
    pthread_mutex_t mutex;
    pthread_cond_t  data_available_cv;
    pthread_cond_t  queue_full_cv;
    size_t  weight_in;
    size_t  weight_out;
    size_t  min_weight;     /* weight_in - weight_out >= min_weight means that data are available */
    size_t  max_weight;     /* weight_in - weight_out >= max_weight means that queue is full */
    int     flush_queue;    /* Tells to starta txn even if not enough data */
    wqelem_t *first;
    wqelem_t *last;
        /* Slot management */
    wqslot_t *wqslots;            /* The slots associated to each dbi*/
    int max_wqslots;              /* max_dbiconfigparam */
    long last_wqslot;             /* first unused slot - using long because atomic operation is used */
        /* Writer thread management */
    pseudo_back_txn_t *predefined_wctx[WCTX_GENERIC];
    double writer_progress;       /* Writer thread progress */
    int aborted;                  /* Writer aborted ==> should not queue anything more */
} global_writer_ctx_t;
