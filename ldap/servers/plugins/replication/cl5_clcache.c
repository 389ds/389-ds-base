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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include "errno.h"			/* ENOMEM, EVAL used by Berkeley DB */
#include "db.h"				/* Berkeley DB */
#include "cl5.h"			/* changelog5Config */
#include "cl5_clcache.h"

/* newer bdb uses DB_BUFFER_SMALL instead of ENOMEM as the
   error return if the given buffer in which to load a
   key or value is too small - if it is not defined, define
   it here to ENOMEM
*/
#ifndef DB_BUFFER_SMALL
#define DB_BUFFER_SMALL ENOMEM
#endif

/*
 * Constants for the buffer pool:
 *
 * DEFAULT_CLC_BUFFER_PAGE_COUNT
 *		Little performance boost if it is too small.
 *
 * DEFAULT_CLC_BUFFER_PAGE_SIZE
 * 		Its value is determined based on the DB requirement that
 *		the buffer size should be the multiple of 1024.
 */
#define DEFAULT_CLC_BUFFER_COUNT_MIN		10
#define DEFAULT_CLC_BUFFER_COUNT_MAX		0
#define DEFAULT_CLC_BUFFER_PAGE_COUNT		32
#define DEFAULT_CLC_BUFFER_PAGE_SIZE		1024

enum {
	CLC_STATE_READY = 0,		/* ready to iterate */
	CLC_STATE_UP_TO_DATE,		/* remote RUV already covers the CSN */
	CLC_STATE_CSN_GT_RUV,		/* local RUV doesn't conver the CSN */
	CLC_STATE_NEW_RID,			/* unknown RID to local RUVs */
	CLC_STATE_UNSAFE_RUV_CHANGE,/* (RUV1 < maxcsn-in-buffer) && (RUV1 < RUV1') */
	CLC_STATE_DONE,				/* no more change */
	CLC_STATE_ABORTING			/* abort replication session */
};

typedef struct clc_busy_list CLC_Busy_List;

struct csn_seq_ctrl_block {
	ReplicaId	rid;				/* RID this block serves */
	CSN			*consumer_maxcsn;	/* Don't send CSN <= this */
	CSN			*local_maxcsn;		/* Don't send CSN > this */
	CSN			*prev_local_maxcsn;	/* */
	int			state;				/* CLC_STATE_* */
};

/*
 * Each cl5replayiterator acquires a buffer from the buffer pool
 * at the beginning of a replication session, and returns it back
 * at the end.
 */
struct clc_buffer {
	char		*buf_agmt_name;		/* agreement acquired this buffer */
	ReplicaId	 buf_consumer_rid;	/* help checking threshold csn */
	const RUV	*buf_consumer_ruv;	/* used to skip change */
	const RUV	*buf_local_ruv;		/* used to refresh local_maxcsn */

	/*
	 * fields for retriving data from DB
	 */
	int			 buf_state;
	CSN			*buf_current_csn;
	int			 buf_load_flag;		/* db flag DB_MULTIPLE_KEY, DB_SET, DB_NEXT */
	DBC			*buf_cursor;
	DBT			 buf_key;			/* current csn string */
	DBT			 buf_data;			/* data retrived from db */
	void		*buf_record_ptr;	/* ptr to the current record in data */
	CSN			*buf_missing_csn;	/* used to detect persistent missing of CSN */

	/* fields for control the CSN sequence sent to the consumer */
	struct csn_seq_ctrl_block *buf_cscbs [MAX_NUM_OF_MASTERS];
	int			 buf_num_cscbs;		/* number of csn sequence ctrl blocks */

	/* fields for debugging stat */
	int		 	 buf_load_cnt;		/* number of loads for session */
	int		 	 buf_record_cnt;	/* number of changes for session */
	int		 	 buf_record_skipped;	/* number of changes skipped */

	/*
	 * fields that should be accessed via bl_lock or pl_lock
	 */
	CLC_Buffer	*buf_next;			/* next buffer in the same list */
	CLC_Busy_List *buf_busy_list;	/* which busy list I'm in */
};

/*
 * Each changelog has a busy buffer list
 */
struct clc_busy_list {
	PRLock			*bl_lock;
	DB				*bl_db;				/* changelog db handle */
	CLC_Buffer		*bl_buffers;		/* busy buffers of this list */
	CLC_Busy_List	*bl_next;			/* next busy list in the pool */
};

/*
 * Each process has a buffer pool
 */ 
struct clc_pool {
	PRRWLock		*pl_lock;				/* cl writer and agreements */
	DB_ENV			**pl_dbenv;				/* pointer to DB_ENV for all the changelog files */
	CLC_Busy_List	*pl_busy_lists;			/* busy buffer lists, one list per changelog file */
	int				 pl_buffer_cnt_now;		/* total number of buffers */
	int				 pl_buffer_cnt_min;		/* free a newly returned buffer if _now > _min */
	int				 pl_buffer_cnt_max;		/* no use */
	int				 pl_buffer_default_pages;	/* num of pages in a new buffer */
};

/* static variables */
static struct clc_pool *_pool = NULL;	/* process's buffer pool */

/* static prototypes */
static int	clcache_adjust_anchorcsn ( CLC_Buffer *buf );
static void	clcache_refresh_consumer_maxcsns ( CLC_Buffer *buf );
static int	clcache_refresh_local_maxcsns ( CLC_Buffer *buf );
static int	clcache_skip_change ( CLC_Buffer *buf );
static int	clcache_load_buffer_bulk ( CLC_Buffer *buf, int flag );
static int	clcache_open_cursor ( DB_TXN *txn, CLC_Buffer *buf, DBC **cursor );
static int	clcache_cursor_get ( DBC *cursor, CLC_Buffer *buf, int flag );
static struct csn_seq_ctrl_block *clcache_new_cscb ();
static void	clcache_free_cscb ( struct csn_seq_ctrl_block ** cscb );
static CLC_Buffer	*clcache_new_buffer ( ReplicaId consumer_rid );
static void	clcache_delete_buffer ( CLC_Buffer **buf );
static CLC_Busy_List *clcache_new_busy_list ();
static void	clcache_delete_busy_list ( CLC_Busy_List **bl );
static int	clcache_enqueue_busy_list( DB *db, CLC_Buffer *buf );
static void csn_dup_or_init_by_csn ( CSN **csn1, CSN *csn2 );

/*
 * Initiates the process buffer pool. This should be done
 * once and only once when process starts.
 */
int
clcache_init ( DB_ENV **dbenv )
{
	if (_pool) {
		return 0; /* already initialized */
	}
	_pool = (struct clc_pool*) slapi_ch_calloc ( 1, sizeof ( struct clc_pool ));
	_pool->pl_dbenv = dbenv;
	_pool->pl_buffer_cnt_min = DEFAULT_CLC_BUFFER_COUNT_MIN;
	_pool->pl_buffer_cnt_max = DEFAULT_CLC_BUFFER_COUNT_MAX;
	_pool->pl_buffer_default_pages = DEFAULT_CLC_BUFFER_COUNT_MAX;
	_pool->pl_lock = PR_NewRWLock (PR_RWLOCK_RANK_NONE, "clcache_pl_lock");
	return 0;
}

/*
 * This is part of a callback function when changelog configuration
 * is read or updated.
 */
void
clcache_set_config ( CL5DBConfig *config )
{
	if ( config == NULL ) return;

	PR_RWLock_Wlock ( _pool->pl_lock );

	_pool->pl_buffer_cnt_max = config->maxChCacheEntries;

	/*
	 * According to http://www.sleepycat.com/docs/api_c/dbc_get.html,
	 * data buffer should be a multiple of 1024 bytes in size
	 * for DB_MULTIPLE_KEY operation.
	 */
	_pool->pl_buffer_default_pages = config->maxChCacheSize / DEFAULT_CLC_BUFFER_PAGE_SIZE + 1;
	_pool->pl_buffer_default_pages = DEFAULT_CLC_BUFFER_PAGE_COUNT;
	if ( _pool->pl_buffer_default_pages <= 0 ) {
		_pool->pl_buffer_default_pages = DEFAULT_CLC_BUFFER_PAGE_COUNT;
	}

	PR_RWLock_Unlock ( _pool->pl_lock );
}

/*
 * Gets the pointer to a thread dedicated buffer, or allocates
 * a new buffer if there is no buffer allocated yet for this thread.
 *
 * This is called when a cl5replayiterator is created for
 * a replication session.
 */ 
int
clcache_get_buffer ( CLC_Buffer **buf, DB *db, ReplicaId consumer_rid, const RUV *consumer_ruv, const RUV *local_ruv )
{
	int rc = 0;
	int need_new;

	if ( buf == NULL ) return CL5_BAD_DATA;

	*buf = NULL;

	/* if the pool was re-initialized, the thread private cache will be invalid,
	   so we must get a new one */
	need_new = (!_pool || !_pool->pl_busy_lists || !_pool->pl_busy_lists->bl_buffers);

	if ( (!need_new) && (NULL != ( *buf = (CLC_Buffer*) get_thread_private_cache())) ) {
		slapi_log_error ( SLAPI_LOG_REPL, get_thread_private_agmtname(),
						  "clcache_get_buffer: found thread private buffer cache %p\n", *buf);
		slapi_log_error ( SLAPI_LOG_REPL, get_thread_private_agmtname(),
						  "clcache_get_buffer: _pool is %p _pool->pl_busy_lists is %p _pool->pl_busy_lists->bl_buffers is %p\n",
						  _pool, _pool ? _pool->pl_busy_lists : NULL,
						  (_pool && _pool->pl_busy_lists) ? _pool->pl_busy_lists->bl_buffers : NULL);
		(*buf)->buf_state = CLC_STATE_READY;
		(*buf)->buf_load_cnt = 0;
		(*buf)->buf_record_cnt = 0;
		(*buf)->buf_record_skipped = 0;
		(*buf)->buf_cursor = NULL;
		(*buf)->buf_num_cscbs = 0;
	}
	else {
		*buf = clcache_new_buffer ( consumer_rid );
		if ( *buf ) {
			if ( 0 == clcache_enqueue_busy_list ( db, *buf ) ) {
				set_thread_private_cache ( (void*) (*buf) );
			}
			else {
				clcache_delete_buffer ( buf );
			}
		}
	}

	if ( NULL != *buf ) {
		(*buf)->buf_consumer_ruv = consumer_ruv;
		(*buf)->buf_local_ruv = local_ruv;
	}
	else {
		slapi_log_error ( SLAPI_LOG_FATAL, get_thread_private_agmtname(),
			"clcache_get_buffer: can't allocate new buffer\n" );
		rc = CL5_MEMORY_ERROR;
	}

	return rc;
}

/*
 * Returns a buffer back to the buffer pool.
 */
void
clcache_return_buffer ( CLC_Buffer **buf )
{
	int i;

	slapi_log_error ( SLAPI_LOG_REPL, (*buf)->buf_agmt_name,
			"session end: state=%d load=%d sent=%d skipped=%d\n",
			 (*buf)->buf_state,
			 (*buf)->buf_load_cnt,
			 (*buf)->buf_record_cnt - (*buf)->buf_record_skipped,
			 (*buf)->buf_record_skipped );

	for ( i = 0; i < (*buf)->buf_num_cscbs; i++ ) {
		clcache_free_cscb ( &(*buf)->buf_cscbs[i] );
	}
	(*buf)->buf_num_cscbs = 0;

	if ( (*buf)->buf_cursor ) {

		(*buf)->buf_cursor->c_close ( (*buf)->buf_cursor );
		(*buf)->buf_cursor = NULL;
	}
}

/*
 * Loads a buffer from DB.
 *
 * anchorcsn - passed in for the first load of a replication session;
 * flag	     - DB_SET to load in the key CSN record.
 * 		       DB_NEXT to load in the records greater than key CSN.
 * return    - DB error code instead of cl5 one because of the
 *		       historic reason.
 */
int
clcache_load_buffer ( CLC_Buffer *buf, CSN *anchorcsn, int flag )
{
	int rc = 0;

	clcache_refresh_local_maxcsns ( buf );

	/* Set the loading key */
	if ( anchorcsn ) {
		clcache_refresh_consumer_maxcsns ( buf );
		buf->buf_load_flag = DB_MULTIPLE_KEY;
		csn_as_string ( anchorcsn, 0, (char*)buf->buf_key.data );
		slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
				"session start: anchorcsn=%s\n", (char*)buf->buf_key.data );
	}
	else if ( csn_get_time(buf->buf_current_csn) == 0 ) {
		/* time == 0 means this csn has never been set */
		rc = DB_NOTFOUND;
	}
	else if ( clcache_adjust_anchorcsn ( buf ) != 0 ) {
		rc = DB_NOTFOUND;
	}
	else {
		csn_as_string ( buf->buf_current_csn, 0, (char*)buf->buf_key.data );
		slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
				"load next: anchorcsn=%s\n", (char*)buf->buf_key.data );
	}

	if ( rc == 0 ) {

		buf->buf_state = CLC_STATE_READY;
		rc = clcache_load_buffer_bulk ( buf, flag );

		/* Reset some flag variables */
		if ( rc == 0 ) {
			int i;
			for ( i = 0; i < buf->buf_num_cscbs; i++ ) {
				buf->buf_cscbs[i]->state = CLC_STATE_READY;
			}
		}
		else if ( anchorcsn ) {
			/* Report error only when the missing is persistent */
			if ( buf->buf_missing_csn && csn_compare (buf->buf_missing_csn, anchorcsn) == 0 ) {
				slapi_log_error ( SLAPI_LOG_FATAL, buf->buf_agmt_name,
					"Can't locate CSN %s in the changelog (DB rc=%d). The consumer may need to be reinitialized.\n",
					(char*)buf->buf_key.data, rc );
			}
			else {
				csn_dup_or_init_by_csn (&buf->buf_missing_csn, anchorcsn);
			}
		}
	}
	if ( rc != 0 ) {
		slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
				"clcache_load_buffer: rc=%d\n", rc );
	}

	return rc;
}

static int
clcache_load_buffer_bulk ( CLC_Buffer *buf, int flag )
{
	DB_TXN *txn = NULL;
	DBC *cursor = NULL;
	int rc;

#if 0 /* txn control seems not improving anything so turn it off */
	if ( *(_pool->pl_dbenv) ) {
		txn_begin( *(_pool->pl_dbenv), NULL, &txn, 0 );
	}
#endif

	PR_Lock ( buf->buf_busy_list->bl_lock );
	if ( 0 == ( rc = clcache_open_cursor ( txn, buf, &cursor )) ) {

		if ( flag == DB_NEXT ) {
			/* For bulk read, position the cursor before read the next block */
			rc = cursor->c_get ( cursor,
								 & buf->buf_key,
								 & buf->buf_data,
								 DB_SET );
		}

		/*
		 * Continue if the error is no-mem since we don't need to
		 * load in the key record anyway with DB_SET.
		 */
		if ( 0 == rc || DB_BUFFER_SMALL == rc )
			rc = clcache_cursor_get ( cursor, buf, flag );

	}

	/*
	 * Don't keep a cursor open across the whole replication session.
	 * That had caused noticable DB resource contention.
	 */
	if ( cursor ) {
		cursor->c_close ( cursor );
	}

#if 0 /* txn control seems not improving anything so turn it off */
	if ( txn ) {
		txn->commit ( txn, DB_TXN_NOSYNC );
	}
#endif

	PR_Unlock ( buf->buf_busy_list->bl_lock );

	buf->buf_record_ptr = NULL;
	if ( 0 == rc ) {
		DB_MULTIPLE_INIT ( buf->buf_record_ptr, &buf->buf_data );
		if ( NULL == buf->buf_record_ptr )
			rc = DB_NOTFOUND;
		else
			buf->buf_load_cnt++;
	}

	return rc;
}

/*
 * Gets the next change from the buffer.
 * *key	: output - key of the next change, or NULL if no more change
 * *data: output - data of the next change, or NULL if no more change
 */
int
clcache_get_next_change ( CLC_Buffer *buf, void **key, size_t *keylen, void **data, size_t *datalen, CSN **csn )
{
	int skip = 1;
	int rc = 0;

	do {
		*key = *data = NULL;
		*keylen = *datalen = 0;

		if ( buf->buf_record_ptr ) {
			DB_MULTIPLE_KEY_NEXT ( buf->buf_record_ptr, &buf->buf_data,
								   *key, *keylen, *data, *datalen );
		}

		/*
		 * We're done with the current buffer. Now load the next chunk.
		 */
		if ( NULL == *key && CLC_STATE_READY == buf->buf_state ) {
			rc = clcache_load_buffer ( buf, NULL, DB_NEXT );
			if ( 0 == rc && buf->buf_record_ptr ) {
				DB_MULTIPLE_KEY_NEXT ( buf->buf_record_ptr, &buf->buf_data,
								   *key, *keylen, *data, *datalen );
			}
		}

		/* Compare the new change to the local and remote RUVs */
		if ( NULL != *key ) {
			buf->buf_record_cnt++;
			csn_init_by_string ( buf->buf_current_csn, (char*)*key );
			skip = clcache_skip_change ( buf );
			if (skip) buf->buf_record_skipped++;
		}
	}
	while ( rc == 0 && *key && skip );

	if ( NULL == *key ) {
		*key = NULL;
		*csn = NULL;
		rc = DB_NOTFOUND;
	}
	else {
		*csn = buf->buf_current_csn;
		slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
			"load=%d rec=%d csn=%s\n",
			buf->buf_load_cnt, buf->buf_record_cnt, (char*)*key );
	}

	return rc;
}

static void
clcache_refresh_consumer_maxcsns ( CLC_Buffer *buf )
{
	int i;

	for ( i = 0; i < buf->buf_num_cscbs; i++ ) {
		csn_free(&buf->buf_cscbs[i]->consumer_maxcsn);
		ruv_get_largest_csn_for_replica (
				buf->buf_consumer_ruv,
				buf->buf_cscbs[i]->rid,
				&buf->buf_cscbs[i]->consumer_maxcsn );
	}
}

static int
clcache_refresh_local_maxcsn ( const ruv_enum_data *rid_data, void *data )
{
	CLC_Buffer *buf = (CLC_Buffer*) data;
	ReplicaId rid;
	int rc = 0;
	int i;

	rid = csn_get_replicaid ( rid_data->csn );

	/*
	 * No need to create cscb for consumer's RID.
	 * If RID==65535, the CSN is originated from a
	 * legacy consumer. In this case the supplier
	 * and the consumer may have the same RID.
	 */
	if ( rid == buf->buf_consumer_rid && rid != MAX_REPLICA_ID )
		return rc;

	for ( i = 0; i < buf->buf_num_cscbs; i++ ) {
		if ( buf->buf_cscbs[i]->rid == rid )
			break;
	}
	if ( i >= buf->buf_num_cscbs ) {
		buf->buf_cscbs[i] = clcache_new_cscb ();
		if ( buf->buf_cscbs[i] == NULL ) {
			return -1;
		}
		buf->buf_cscbs[i]->rid = rid;
		buf->buf_num_cscbs++;
	}

	csn_dup_or_init_by_csn ( &buf->buf_cscbs[i]->local_maxcsn, rid_data->csn );

	if ( buf->buf_cscbs[i]->consumer_maxcsn &&
		 csn_compare (buf->buf_cscbs[i]->consumer_maxcsn, rid_data->csn) >= 0 ) {
		/* No change need to be sent for this RID */
		buf->buf_cscbs[i]->state = CLC_STATE_UP_TO_DATE;
	}

	return rc;
}

static int
clcache_refresh_local_maxcsns ( CLC_Buffer *buf )
{
	int i;

	for ( i = 0; i < buf->buf_num_cscbs; i++ ) {
		csn_dup_or_init_by_csn ( &buf->buf_cscbs[i]->prev_local_maxcsn,
								  buf->buf_cscbs[i]->local_maxcsn );
	}
	return ruv_enumerate_elements ( buf->buf_local_ruv, clcache_refresh_local_maxcsn, buf );
}

/*
 * Algorithm:
 *
 *	1. Snapshot local RUVs;
 *	2. Load buffer;
 *	3. Send to the consumer only those CSNs that are covered
 *	   by the RUVs snapshot taken in the first step;
 *	   All CSNs that are covered by the RUVs snapshot taken in the
 *	   first step are guaranteed in consecutive order for the respected
 *	   RIDs because of the the CSN pending list control;
 *	   A CSN that is not covered by the RUVs snapshot may be out of order
 *	   since it is possible that a smaller CSN might not have committed 
 *	   yet by the time the buffer was loaded.
 *	4. Determine anchorcsn for each RID:
 *
 *	   Case|  Local vs. Buffer | New Local |       Next
 *	       | MaxCSN     MaxCSN |    MaxCSN | Anchor-CSN
 *	   ----+-------------------+-----------+----------------
 *       1 |   Cl    >=   Cb   |     *     | Cb
 *       2 |   Cl    <    Cb   |     Cl    | Cb
 *       3 |   Cl    <    Cb   |     Cl2   | Cl 
 *
 *	5. Determine anchorcsn for next load:
 *	   Anchor-CSN = min { all Next-Anchor-CSN, Buffer-MaxCSN }
 */
static int
clcache_adjust_anchorcsn ( CLC_Buffer *buf )
{
	PRBool hasChange = PR_FALSE;
	struct csn_seq_ctrl_block *cscb;
	int i;

	if ( buf->buf_state == CLC_STATE_READY ) {
		for ( i = 0; i < buf->buf_num_cscbs; i++ ) {
			cscb = buf->buf_cscbs[i];

			if ( cscb->state == CLC_STATE_UP_TO_DATE )
				continue;

			/*
			 * Case 3 unsafe ruv change: next buffer load should start
			 * from where the maxcsn in the old ruv was. Since each
			 * cscb has remembered the maxcsn sent to the consumer,
			 * CSNs that may be loaded again could easily be skipped.
			 */
			if ( cscb->prev_local_maxcsn &&
				 csn_compare (cscb->prev_local_maxcsn, buf->buf_current_csn) < 0 &&
				 csn_compare (cscb->local_maxcsn, cscb->prev_local_maxcsn) != 0 ) {
				hasChange = PR_TRUE;
				cscb->state = CLC_STATE_READY;
				csn_init_by_csn ( buf->buf_current_csn, cscb->prev_local_maxcsn );
				csn_as_string ( cscb->prev_local_maxcsn, 0, (char*)buf->buf_key.data );
				slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
						"adjust anchor csn upon %s\n",
						( cscb->state == CLC_STATE_CSN_GT_RUV ? "out of sequence csn" : "unsafe ruv change") );
				continue;
			}

			/*
			 * check if there are still changes to send for this RID
			 * Assume we had compared the local maxcsn and the consumer
			 * max csn before this function was called and hence the
			 * cscb->state had been set accordingly.
			 */ 
			if ( hasChange == PR_FALSE &&
				 csn_compare (cscb->local_maxcsn, buf->buf_current_csn) > 0 ) {
				hasChange = PR_TRUE;
			}
		}
	}

	if ( !hasChange ) {
		buf->buf_state = CLC_STATE_DONE;
	}

	return buf->buf_state;
}

static int
clcache_skip_change ( CLC_Buffer *buf )
{
	struct csn_seq_ctrl_block *cscb = NULL;
	ReplicaId rid;
	int skip = 1;
	int i;

	do {

		rid = csn_get_replicaid ( buf->buf_current_csn );

		/*
		 * Skip CSN that is originated from the consumer.
		 * If RID==65535, the CSN is originated from a
		 * legacy consumer. In this case the supplier
		 * and the consumer may have the same RID.
		 */
		if (rid == buf->buf_consumer_rid && rid != MAX_REPLICA_ID)
			break;

		/* Skip helper entry (ENTRY_COUNT, PURGE_RUV and so on) */
		if ( cl5HelperEntry ( NULL, buf->buf_current_csn ) == PR_TRUE ) {
			slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
				"Skip helper entry type=%ld\n", csn_get_time( buf->buf_current_csn ));
			break;
		}

		/* Find csn sequence control block for the current rid */
		for (i = 0; i < buf->buf_num_cscbs && buf->buf_cscbs[i]->rid != rid; i++);

		/* Skip CSN whose RID is unknown to the local RUV snapshot */
		if ( i >= buf->buf_num_cscbs ) {
			buf->buf_state = CLC_STATE_NEW_RID;
			break;
		}

		cscb = buf->buf_cscbs[i];

		/* Skip if the consumer is already up-to-date for the RID */
		if ( cscb->state == CLC_STATE_UP_TO_DATE ) {
			break;
		}

		/* Skip CSN whose preceedents are not covered by local RUV snapshot */
		if ( cscb->state == CLC_STATE_CSN_GT_RUV ) {
			break;
		}

		/* Skip CSNs already covered by consumer RUV */
		if ( cscb->consumer_maxcsn &&
			 csn_compare ( buf->buf_current_csn, cscb->consumer_maxcsn ) <= 0 ) {
				break;
		}

		/* Send CSNs that are covered by the local RUV snapshot */
		if ( csn_compare ( buf->buf_current_csn, cscb->local_maxcsn ) <= 0 ) {
			skip = 0;
			csn_dup_or_init_by_csn ( &cscb->consumer_maxcsn, buf->buf_current_csn );
			break;
		}

		/*
		 * Promote the local maxcsn to its next neighbor
		 * to keep the current session going. Skip if we
		 * are not sure if current_csn is the neighbor.
		 */
		if ( csn_time_difference(buf->buf_current_csn, cscb->local_maxcsn) == 0 &&
			 (csn_get_seqnum(buf->buf_current_csn) ==
				csn_get_seqnum(cscb->local_maxcsn) + 1) ) {
			csn_init_by_csn ( cscb->local_maxcsn, buf->buf_current_csn );
			csn_init_by_csn ( cscb->consumer_maxcsn, buf->buf_current_csn );
			skip = 0;
			break;
		}

		/* Skip CSNs not covered by local RUV snapshot */
		cscb->state = CLC_STATE_CSN_GT_RUV;

	} while (0);

#ifdef DEBUG
	if (skip && cscb) {
		char consumer[24] = {'\0'};
		char local[24] = {'\0'};
		char current[24] = {'\0'};

		if ( cscb->consumer_maxcsn )
			csn_as_string ( cscb->consumer_maxcsn, PR_FALSE, consumer );
		if ( cscb->local_maxcsn )
			csn_as_string ( cscb->local_maxcsn, PR_FALSE, local );
		csn_as_string ( buf->buf_current_csn, PR_FALSE, current );
		slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
				"Skip %s consumer=%s local=%s\n", current, consumer, local );
	}
#endif

	return skip;
}

static struct csn_seq_ctrl_block *
clcache_new_cscb ()
{
	struct csn_seq_ctrl_block *cscb;

	cscb = (struct csn_seq_ctrl_block *) slapi_ch_calloc ( 1, sizeof (struct csn_seq_ctrl_block) );
	if (cscb == NULL) {
		slapi_log_error ( SLAPI_LOG_FATAL, NULL, "clcache: malloc failure\n" );
	}
	return cscb;
}

static void
clcache_free_cscb ( struct csn_seq_ctrl_block ** cscb )
{
	csn_free ( & (*cscb)->consumer_maxcsn );
	csn_free ( & (*cscb)->local_maxcsn );
	csn_free ( & (*cscb)->prev_local_maxcsn );
	slapi_ch_free ( (void **) cscb );
}

/*
 * Allocate and initialize a new buffer
 * It is called when there is a request for a buffer while
 * buffer free list is empty.
 */
static CLC_Buffer *
clcache_new_buffer ( ReplicaId consumer_rid )
{
	CLC_Buffer *buf = NULL;
	int welldone = 0;

	do {

		buf = (CLC_Buffer*) slapi_ch_calloc (1, sizeof(CLC_Buffer));
		if ( NULL == buf )
			break;

		buf->buf_key.flags = DB_DBT_USERMEM;
		buf->buf_key.ulen = CSN_STRSIZE + 1;
		buf->buf_key.size = CSN_STRSIZE;
		buf->buf_key.data = slapi_ch_calloc( 1, buf->buf_key.ulen );
		if ( NULL == buf->buf_key.data )
			break;

		buf->buf_data.flags = DB_DBT_USERMEM;
		buf->buf_data.ulen = _pool->pl_buffer_default_pages * DEFAULT_CLC_BUFFER_PAGE_SIZE;
		buf->buf_data.data = slapi_ch_malloc( buf->buf_data.ulen );
		if ( NULL == buf->buf_data.data )
			break;

		if ( NULL == ( buf->buf_current_csn = csn_new()) )
			break;

		buf->buf_state = CLC_STATE_READY;
		buf->buf_agmt_name = get_thread_private_agmtname();
		buf->buf_consumer_rid = consumer_rid;
		buf->buf_num_cscbs = 0;

		welldone = 1;

	} while (0);

	if ( !welldone ) {
		clcache_delete_buffer ( &buf );
	}

	return buf;
}

/*
 * Deallocates a buffer.
 * It is called when a buffer is returned to the buffer pool
 * and the pool size is over the limit.
 */
static void
clcache_delete_buffer ( CLC_Buffer **buf )
{
	if ( buf && *buf ) {
		slapi_ch_free (&( (*buf)->buf_key.data ));
		slapi_ch_free (&( (*buf)->buf_data.data ));
		csn_free (&( (*buf)->buf_current_csn ));
		csn_free (&( (*buf)->buf_missing_csn ));
		slapi_ch_free ( (void **) buf );
	}
}

static CLC_Busy_List *
clcache_new_busy_list ()
{
	CLC_Busy_List *bl;
	int welldone = 0;

	do {
		if ( NULL == (bl = ( CLC_Busy_List* ) slapi_ch_calloc (1, sizeof(CLC_Busy_List)) ))
			break;

		if ( NULL == (bl->bl_lock = PR_NewLock ()) )
			break;

		/*
		if ( NULL == (bl->bl_max_csn = csn_new ()) )
			break;
		*/

		welldone = 1;
	}
	while (0);

	if ( !welldone ) {
		clcache_delete_busy_list ( &bl );
	}

	return bl;
}

static void
clcache_delete_busy_list ( CLC_Busy_List **bl )
{
	if ( bl && *bl ) {
		CLC_Buffer *buf = NULL;
		if ( (*bl)->bl_lock ) {
			PR_Lock ( (*bl)->bl_lock );
		}
		buf = (*bl)->bl_buffers;
		while (buf) {
			CLC_Buffer *next = buf->buf_next;
			clcache_delete_buffer(&buf);
			buf = next;
		}
		(*bl)->bl_buffers = NULL;
		(*bl)->bl_db = NULL;
		if ( (*bl)->bl_lock ) {
			PR_Unlock ( (*bl)->bl_lock );
			PR_DestroyLock ( (*bl)->bl_lock );
			(*bl)->bl_lock = NULL;
		}
		/* csn_free (&( (*bl)->bl_max_csn )); */
		slapi_ch_free ( (void **) bl );
	}
}

static int
clcache_enqueue_busy_list ( DB *db, CLC_Buffer *buf )
{
	CLC_Busy_List *bl;
	int rc = 0;

	PR_RWLock_Rlock ( _pool->pl_lock );
	for ( bl = _pool->pl_busy_lists; bl && bl->bl_db != db; bl = bl->bl_next );
	PR_RWLock_Unlock ( _pool->pl_lock );

	if ( NULL == bl ) {
		if ( NULL == ( bl = clcache_new_busy_list ()) ) {
			rc = CL5_MEMORY_ERROR;
		}
		else {
			PR_RWLock_Wlock ( _pool->pl_lock );
			bl->bl_db = db;
			bl->bl_next = _pool->pl_busy_lists;
			_pool->pl_busy_lists = bl;
			PR_RWLock_Unlock ( _pool->pl_lock );
		}
	}

	if ( NULL != bl ) {
		PR_Lock ( bl->bl_lock );
		buf->buf_busy_list = bl;
		buf->buf_next = bl->bl_buffers;
		bl->bl_buffers = buf;
		PR_Unlock ( bl->bl_lock );
	}

	return rc;
}

static int
clcache_open_cursor ( DB_TXN *txn, CLC_Buffer *buf, DBC **cursor )
{
	int rc;

	rc = buf->buf_busy_list->bl_db->cursor ( buf->buf_busy_list->bl_db, txn, cursor, 0 );
	if ( rc != 0 ) {
		slapi_log_error ( SLAPI_LOG_FATAL, get_thread_private_agmtname(),
			"clcache: failed to open cursor; db error - %d %s\n",
			rc, db_strerror(rc));
	}

	return rc;
}

static int
clcache_cursor_get ( DBC *cursor, CLC_Buffer *buf, int flag )
{
	int rc;

	rc = cursor->c_get ( cursor,
						 & buf->buf_key,
						 & buf->buf_data,
						 buf->buf_load_flag | flag );
	if ( DB_BUFFER_SMALL == rc ) {
		/*
		 * The record takes more space than the current size of the
		 * buffer. Fortunately, buf->buf_data.size has been set by
		 * c_get() to the actual data size needed. So we can
		 * reallocate the data buffer and try to read again.
		 */
		buf->buf_data.ulen = ( buf->buf_data.size / DEFAULT_CLC_BUFFER_PAGE_SIZE + 1 ) * DEFAULT_CLC_BUFFER_PAGE_SIZE;
		buf->buf_data.data = slapi_ch_realloc ( buf->buf_data.data, buf->buf_data.ulen );
		if ( buf->buf_data.data != NULL ) {
			rc = cursor->c_get ( cursor,
								 &( buf->buf_key ),
								 &( buf->buf_data ),
							 	 buf->buf_load_flag | flag );
			slapi_log_error ( SLAPI_LOG_REPL, buf->buf_agmt_name,
				"clcache: (%d | %d) buf key len %d reallocated and retry returns %d\n", buf->buf_load_flag, flag, buf->buf_key.size, rc );
		}
	}

	switch ( rc ) {
		case EINVAL:
			slapi_log_error ( SLAPI_LOG_FATAL, buf->buf_agmt_name,
					"clcache_cursor_get: invalid parameter\n" );
			break;

		case DB_BUFFER_SMALL:
			slapi_log_error ( SLAPI_LOG_FATAL, buf->buf_agmt_name,
					"clcache_cursor_get: can't allocate %u bytes\n", buf->buf_data.ulen );
			break;

		default:
			break;
	}

	return rc;
}

static void
csn_dup_or_init_by_csn ( CSN **csn1, CSN *csn2 )
{
	if ( *csn1 == NULL )
		*csn1 = csn_new();
	csn_init_by_csn ( *csn1, csn2 );
}

void
clcache_destroy()
{
	if (_pool) {
		CLC_Busy_List *bl = NULL;
		if (_pool->pl_lock) {
			PR_RWLock_Wlock (_pool->pl_lock);
		}

		bl = _pool->pl_busy_lists;
		while (bl) {
			CLC_Busy_List *next = bl->bl_next;
			clcache_delete_busy_list(&bl);
			bl = next;
		}
		_pool->pl_busy_lists = NULL;
		_pool->pl_dbenv = NULL;
		if (_pool->pl_lock) {
			PR_RWLock_Unlock(_pool->pl_lock);
			PR_DestroyRWLock(_pool->pl_lock);
			_pool->pl_lock = NULL;
		}
		slapi_ch_free ( (void **) &_pool );
	}
}
