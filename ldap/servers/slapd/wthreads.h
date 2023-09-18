/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/* This file contains the data about the worker threads handling the operations */

#ifndef _WTHREADS_H_
#define _WTHREADS_H_

#include "slap.h"



/* List element */
typedef struct ll_list_t {
    struct ll_head_t *head;
    void *data;
    struct ll_list_t *prev;
    struct ll_list_t *next;
} ll_list_t;

/* List header */
typedef struct ll_head_t {
    ll_list_t h;
    int size;
    int hwm; /* Size high water mark */
} ll_head_t;

/* Worker thread context */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    pthread_t tid;
    ll_list_t q;        /* The element chained in waiting_threads/busy_threads */
    struct conn *conn;  /* The connection on which there are job to process */
    Slapi_Operation *op;
    int closing;
    int idx;
    struct snmp_vars_t snmp_vars;  /* The snmp counters */
} pc_tinfo_t;

/* Monitoring statistics */
typedef struct {
    int waitingthreads;
    int busythreads;
    int maxbusythreads;
    int waitingjobs;
    int maxwaitingjobs;
} op_thread_stats_t;

/* Operation worker thread Producer/Consumer global context */
typedef struct {
    pthread_mutex_t mutex;
    ll_head_t waiting_threads;
    ll_head_t busy_threads;
    ll_head_t waiting_jobs;
    ll_head_t jobs_free_list;
    int shutdown;
    int nbcpus;
    pthread_key_t tinfo_key;
    void (*threadnumber_cb)(int);
    void (*getstats_cb)(op_thread_stats_t*);
    struct {
        pthread_mutex_t mutex;
        int nbthreads;
        pc_tinfo_t **threads;
    } snmp;
} pc_t;

/* Connection status values returned by
    connection_wait_for_new_work(), connection_read_operation(), etc. */
typedef enum {
    CONN_FOUND_WORK_TO_DO,
    CONN_SHUTDOWN,
    CONN_NOWORK,
    CONN_DONE,
    CONN_TIMEDOUT,
} conn_status_t;

/* Defined in libglobs.c */
extern pc_t g_pc;
pc_tinfo_t *g_get_thread_info();


#endif /* _WTHREADS_H_ */
