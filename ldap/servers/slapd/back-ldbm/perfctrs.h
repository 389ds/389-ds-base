/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* Structure definition for performance data */
/* This stuff goes in shared memory, so make sure the packing is consistent */

struct _performance_counters {
	PRUint32	sequence_number;
	PRUint32    lock_region_wait_rate;
	PRUint32    deadlock_rate;
	PRUint32    configured_locks;
	PRUint32    current_locks;
	PRUint32    max_locks;
	PRUint32    lockers;
	PRUint32    current_lock_objects;
	PRUint32    max_lock_objects;
	PRUint32    lock_conflicts;
	PRUint32    lock_request_rate;
	PRUint32    log_region_wait_rate;
	PRUint32    log_write_rate;
	PRUint32    log_bytes_since_checkpoint;
	PRUint32    cache_size_bytes;
	PRUint32    page_access_rate;
	PRUint32    cache_hit;
	PRUint32    cache_try;
	PRUint32    page_create_rate;
	PRUint32    page_read_rate;
	PRUint32    page_write_rate;
	PRUint32    page_ro_evict_rate;
	PRUint32    page_rw_evict_rate;
	PRUint32    hash_buckets;
	PRUint32    hash_search_rate;
	PRUint32    longest_chain_length;
	PRUint32    hash_elements_examine_rate;
	PRUint32    pages_in_use;
	PRUint32    dirty_pages;
	PRUint32    clean_pages;
	PRUint32    page_trickle_rate;
	PRUint32    cache_region_wait_rate;
	PRUint32    active_txns;
	PRUint32    commit_rate;
	PRUint32    abort_rate;
	PRUint32    txn_region_wait_rate;
};
typedef struct _performance_counters performance_counters;

#define PERFCTRS_REGION_SUFFIX "-sm"
#define PERFCTRS_MUTEX_SUFFIX "-mx"

