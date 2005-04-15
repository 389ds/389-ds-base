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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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

