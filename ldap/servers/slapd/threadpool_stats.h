/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

#include <stdint.h>

typedef struct slapi_entry Slapi_Entry;

#define TP_STATS_MAGIC 0x54504f4f4c535431ULL /* "TPOOLST1" */
#define TP_STATS_VER_MAJOR 1
#define TP_STATS_VER_MINOR 0
#define TP_STATS_HEADER_SIZE 4096
#define TP_STATS_WORKER_SLOT_SIZE 64
#define TP_STATS_ATTR_THREADPOOL_WORKER "threadpoolworker"

typedef enum {
    TP_WORKER_STATE_UNUSED = 0,
    TP_WORKER_STATE_IDLE = 1,
    TP_WORKER_STATE_BUSY = 2,
    TP_WORKER_STATE_EXITED = 3,
} tp_worker_state_t;

typedef struct {
    uint64_t cur_work_queue;
    uint64_t max_work_queue;
    uint64_t cur_busy_workers;
    uint64_t max_busy_workers;
    uint64_t ops_initiated;
    uint64_t ops_completed;
    uint64_t cur_connections;
} tp_gauges_t;

/*
 * Thread-pool status mmap ABI.
 *
 * The file is machine-local only: all integers are host-endian, fixed width,
 * and naturally aligned. It contains no time_t, pointers, strings, DNs, IPs,
 * filters, or other request content.
 *
 * start_ns doubles as the operation-in-flight sentinel. op_id 0 is a valid
 * value (the first operation on a connection) and must not be used as one.
 */
typedef struct __attribute__((aligned(64))) tp_worker_slot {
    uint32_t state;
    uint32_t op_tag;
    uint64_t conn_id;
    uint64_t op_id;
    uint64_t start_ns;
} tp_worker_slot_t;

typedef struct tp_stats_header {
    uint64_t magic;
    uint16_t ver_major;
    uint16_t ver_minor;
    uint32_t header_size;
    uint32_t worker_slot_size;
    uint32_t max_workers;
    uint64_t server_pid;
    uint64_t start_wall_sec;
    uint64_t heartbeat_mono_ns;
    uint64_t heartbeat_wall_sec;
    uint32_t shutdown_clean;
    uint32_t pad0;
    uint64_t cur_work_queue;
    uint64_t max_work_queue;
    uint64_t cur_busy_workers;
    uint64_t max_busy_workers;
    uint64_t ops_initiated;
    uint64_t ops_completed;
    uint64_t cur_connections;
    uint8_t reserved[3976];
} tp_stats_header_t;

_Static_assert(sizeof(tp_worker_slot_t) == TP_STATS_WORKER_SLOT_SIZE,
               "tp_worker_slot_t size must remain ABI-stable");
_Static_assert(sizeof(tp_stats_header_t) == TP_STATS_HEADER_SIZE,
               "tp_stats_header_t size must remain ABI-stable");

void tp_collect_gauges(tp_gauges_t *out);
int tp_stats_init(uint32_t max_workers);
void tp_stats_start_heartbeat(void);
void tp_stats_close(void);
void tp_stats_worker_idle(uint32_t worker_idx);
void tp_stats_worker_busy(uint32_t worker_idx);
void tp_stats_worker_operation_start(uint32_t worker_idx, uint64_t conn_id, uint64_t op_id, uint32_t op_tag);
void tp_stats_worker_operation_done(uint32_t worker_idx);
void tp_stats_worker_exited(uint32_t worker_idx);
void tp_stats_as_entry(Slapi_Entry *e);
