/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <slapi_pal.h>

/* Structure definition for performance data */
/* This stuff goes in shared memory, so make sure the packing is consistent */

struct _performance_counters
{
#ifdef TODO
    uint64_t sequence_number;
    uint64_t lock_region_wait_rate;
    uint64_t deadlock_rate;
    uint64_t configured_locks;
    uint64_t current_locks;
    uint64_t max_locks;
    uint64_t lockers;
    uint64_t current_lock_objects;
    uint64_t max_lock_objects;
    uint64_t lock_conflicts;
    uint64_t lock_request_rate;
    uint64_t log_region_wait_rate;
    uint64_t log_write_rate;
    uint64_t log_bytes_since_checkpoint;
    uint64_t cache_size_bytes;
    uint64_t cache_hit;
    uint64_t cache_try;
    uint64_t page_create_rate;
    uint64_t page_read_rate;
    uint64_t page_write_rate;
    uint64_t page_ro_evict_rate;
    uint64_t page_rw_evict_rate;
    uint64_t hash_buckets;
    uint64_t hash_search_rate;
    uint64_t longest_chain_length;
    uint64_t hash_elements_examine_rate;
    uint64_t pages_in_use;
    uint64_t dirty_pages;
    uint64_t clean_pages;
    uint64_t page_trickle_rate;
    uint64_t cache_region_wait_rate;
    uint64_t active_txns;
    uint64_t commit_rate;
    uint64_t abort_rate;
    uint64_t txn_region_wait_rate;
#endif /* TODO */
};
typedef struct _performance_counters performance_counters;

#define PERFCTRS_REGION_SUFFIX "-sm"
#define PERFCTRS_MUTEX_SUFFIX "-mx"
