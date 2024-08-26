/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "errno.h" /* ENOMEM, EVAL used by Berkeley DB */
#include "cl5.h"   /* changelog5Config */
#include "cl5_clcache.h"
#include "slap.h"
#include "proto-slap.h"

/*
 * Constants for the buffer pool:
 *
 * DEFAULT_CLC_BUFFER_PAGE_COUNT
 *        Little performance boost if it is too small.
 *
 * DEFAULT_CLC_BUFFER_PAGE_SIZE
 *         Its value is determined based on the DB requirement that
 *        the buffer size should be the multiple of 1024.
 */
#define DEFAULT_CLC_BUFFER_COUNT_MIN 10
#define DEFAULT_CLC_BUFFER_COUNT_MAX 0
#define DEFAULT_CLC_BUFFER_PAGE_COUNT 32
#define DEFAULT_CLC_BUFFER_PAGE_SIZE 1024
#define WORK_CLC_BUFFER_PAGE_SIZE 8 * DEFAULT_CLC_BUFFER_PAGE_SIZE

enum
{
    CLC_STATE_READY = 0,         /* ready to iterate */
    CLC_STATE_UP_TO_DATE,        /* remote RUV already covers the CSN */
    CLC_STATE_CSN_GT_RUV,        /* local RUV doesn't conver the CSN */
    CLC_STATE_NEW_RID,           /* unknown RID to local RUVs */
    CLC_STATE_UNSAFE_RUV_CHANGE, /* (RUV1 < maxcsn-in-buffer) && (RUV1 < RUV1') */
    CLC_STATE_DONE,              /* no more change */
    CLC_STATE_ABORTING           /* abort replication session */
};

typedef struct clc_busy_list CLC_Busy_List;

struct csn_seq_ctrl_block
{
    ReplicaId rid;          /* RID this block serves */
    CSN *consumer_maxcsn;   /* Don't send CSN <= this */
    CSN *local_maxcsn;      /* Don't send CSN > this */
    CSN *prev_local_maxcsn; /* Copy of last state at buffer loading */
    CSN *local_mincsn;      /* Used to determin anchor csn*/
    int state;              /* CLC_STATE_* */
};

/*
 * Each cl5replayiterator acquires a buffer from the buffer pool
 * at the beginning of a replication session, and returns it back
 * at the end.
 */
struct clc_buffer
{
    char *buf_agmt_name;         /* agreement acquired this buffer */
    ReplicaId buf_consumer_rid;  /* help checking threshold csn */
    const RUV *buf_consumer_ruv; /* used to skip change */
    const RUV *buf_local_ruv;    /* used to refresh local_maxcsn */
    int buf_ignoreConsumerRID;   /* how to handle updates from consumer */
    int buf_load_cnt;            /* number of loads for session */

    /*
     * fields for retriving data from DB
     */
    int buf_state;
    CSN *buf_current_csn;
    dbi_cursor_t buf_cursor;
    dbi_val_t buf_key;         /* current csn string */
    dbi_bulk_t buf_bulk;       /* bulk operation buffer */
    CSN *buf_missing_csn;      /* used to detect persistent missing of CSN */
    CSN *buf_prev_missing_csn; /* used to surpress the repeated messages */
    char buf_bulkdata[WORK_CLC_BUFFER_PAGE_SIZE];  /* default buf_bulk storage */
    char buf_keydata[CSN_STRSIZE+1];               /* buf_key storage */

    /* fields for control the CSN sequence sent to the consumer */
    struct csn_seq_ctrl_block **buf_cscbs;
    int buf_num_cscbs; /* number of csn sequence ctrl blocks */
    int buf_max_cscbs;

    /* fields for debugging stat */
    int buf_record_cnt;                 /* number of changes for session */
    int buf_record_skipped;             /* number of changes skipped */
    int buf_skipped_new_rid;            /* number of changes skipped due to new_rid */
    int buf_skipped_csn_gt_cons_maxcsn; /* number of changes skipped due to csn greater than consumer maxcsn */
    int buf_skipped_up_to_date;         /* number of changes skipped due to consumer being up-to-date for the given rid */
    int buf_skipped_csn_gt_ruv;         /* number of changes skipped due to preceedents are not covered by local RUV snapshot */
    int buf_skipped_csn_covered;        /* number of changes skipped due to CSNs already covered by consumer RUV */

    /*
     * fields that should be accessed via bl_lock or pl_lock
     */
    CLC_Buffer *buf_next;         /* next buffer in the same list */
    CLC_Busy_List *buf_busy_list; /* which busy list I'm in */
};

/*
 * Each changelog has a busy buffer list
 */
struct clc_busy_list
{
    PRLock *bl_lock;
    dbi_db_t *bl_db;        /* changelog db handle */
    CLC_Buffer *bl_buffers; /* busy buffers of this list */
    CLC_Busy_List *bl_next; /* next busy list in the pool */
    Slapi_Backend *bl_be;   /* backend (to use dbimpl API) */
};

/*
 * Each process has a buffer pool
 */
struct clc_pool
{
    Slapi_RWLock *pl_lock;        /* cl writer and agreements */
    CLC_Busy_List *pl_busy_lists; /* busy buffer lists, one list per changelog file */
    int pl_buffer_cnt_now;        /* total number of buffers */
    int pl_buffer_cnt_min;        /* free a newly returned buffer if _now > _min */
    int pl_buffer_cnt_max;        /* no use */
    int pl_buffer_default_pages;  /* num of pages in a new buffer */
};

/* static variables */
static struct clc_pool *_pool = NULL; /* process's buffer pool */

/* static prototypes */
static int clcache_initial_anchorcsn(CLC_Buffer *buf, dbi_op_t *dbop);
static int clcache_adjust_anchorcsn(CLC_Buffer *buf, dbi_op_t *dbop);
static void clcache_refresh_consumer_maxcsns(CLC_Buffer *buf);
static int clcache_refresh_local_maxcsns(CLC_Buffer *buf);
static int clcache_skip_change(CLC_Buffer *buf);
static int clcache_load_buffer_bulk(CLC_Buffer *buf, dbi_op_t dbop);
static int clcache_open_cursor(dbi_txn_t *txn, CLC_Buffer *buf, dbi_cursor_t *cursor);
static int clcache_cursor_get(dbi_cursor_t *cursor, CLC_Buffer *buf, dbi_op_t dbop);
static struct csn_seq_ctrl_block *clcache_new_cscb(void);
static void clcache_free_cscb(struct csn_seq_ctrl_block **cscb);
static CLC_Buffer *clcache_new_buffer(ReplicaId consumer_rid);
static void clcache_delete_buffer(CLC_Buffer **buf);
static CLC_Busy_List *clcache_new_busy_list(void);
static void clcache_delete_busy_list(CLC_Busy_List **bl);
static int clcache_enqueue_busy_list(Replica *replica, dbi_db_t *db, CLC_Buffer *buf);
static void csn_dup_or_init_by_csn(CSN **csn1, CSN *csn2);

/*
 * Initiates the process buffer pool. This should be done
 * once and only once when process starts.
 */
int
clcache_init(void)
{
    if (_pool) {
        return 0; /* already initialized */
    }
    _pool = (struct clc_pool *)slapi_ch_calloc(1, sizeof(struct clc_pool));
    _pool->pl_buffer_cnt_min = DEFAULT_CLC_BUFFER_COUNT_MIN;
    _pool->pl_buffer_cnt_max = DEFAULT_CLC_BUFFER_COUNT_MAX;
    _pool->pl_buffer_default_pages = DEFAULT_CLC_BUFFER_COUNT_MAX;
    _pool->pl_lock = slapi_new_rwlock();
    return 0;
}

/*
 * This is part of a callback function when changelog configuration
 * is read or updated.
 */
void
clcache_set_config()
{
    slapi_rwlock_wrlock(_pool->pl_lock);

    _pool->pl_buffer_cnt_max = CL5_DEFAULT_CONFIG_CACHESIZE;

    /*
     * Berkeley database: According to http://www.sleepycat.com/docs/api_c/dbc_get.html,
     * data buffer should be a multiple of 1024 bytes in size
     * for DB_MULTIPLE_KEY operation.
     */
    _pool->pl_buffer_default_pages = CL5_DEFAULT_CONFIG_CACHEMEMSIZE / DEFAULT_CLC_BUFFER_PAGE_SIZE + 1;
    if (_pool->pl_buffer_default_pages <= 0) { /* this never be true... */
        _pool->pl_buffer_default_pages = DEFAULT_CLC_BUFFER_PAGE_COUNT;
    }

    slapi_rwlock_unlock(_pool->pl_lock);
}

/*
 * Gets the pointer to a thread dedicated buffer, or allocates
 * a new buffer if there is no buffer allocated yet for this thread.
 *
 * This is called when a cl5replayiterator is created for
 * a replication session.
 */
int
clcache_get_buffer(Replica *replica, CLC_Buffer **buf, dbi_db_t *db, ReplicaId consumer_rid, const RUV *consumer_ruv, const RUV *local_ruv)
{
    int rc = 0;
    int need_new;
    static const dbi_cursor_t cursor0 = {0};

    if (buf == NULL)
        return CL5_BAD_DATA;

    *buf = NULL;

    /* if the pool was re-initialized, the thread private cache will be invalid,
       so we must get a new one */
    need_new = (!_pool || !_pool->pl_busy_lists || !_pool->pl_busy_lists->bl_buffers);

    if ((!need_new) && (NULL != (*buf = (CLC_Buffer *)get_thread_private_cache()))) {
        slapi_log_err(SLAPI_LOG_REPL, get_thread_private_agmtname(),
                      "clcache_get_buffer - found thread private buffer cache %p\n", *buf);
        slapi_log_err(SLAPI_LOG_REPL, get_thread_private_agmtname(),
                      "clcache_get_buffer - _pool is %p _pool->pl_busy_lists is %p _pool->pl_busy_lists->bl_buffers is %p\n",
                      _pool, _pool ? _pool->pl_busy_lists : NULL,
                      (_pool && _pool->pl_busy_lists) ? _pool->pl_busy_lists->bl_buffers : NULL);
        (*buf)->buf_state = CLC_STATE_READY;
        (*buf)->buf_load_cnt = 0;
        (*buf)->buf_record_cnt = 0;
        (*buf)->buf_record_skipped = 0;
        (*buf)->buf_cursor = cursor0;
        (*buf)->buf_skipped_new_rid = 0;
        (*buf)->buf_skipped_csn_gt_cons_maxcsn = 0;
        (*buf)->buf_skipped_up_to_date = 0;
        (*buf)->buf_skipped_csn_gt_ruv = 0;
        (*buf)->buf_skipped_csn_covered = 0;
        (*buf)->buf_cscbs = (struct csn_seq_ctrl_block **)slapi_ch_calloc(MAX_NUM_OF_SUPPLIERS + 1,
                                                                          sizeof(struct csn_seq_ctrl_block *));
        (*buf)->buf_num_cscbs = 0;
        (*buf)->buf_max_cscbs = MAX_NUM_OF_SUPPLIERS;
    } else {
        *buf = clcache_new_buffer(consumer_rid);
        if (*buf) {
            if (0 == clcache_enqueue_busy_list(replica, db, *buf)) {
                Slapi_Backend *be = (*buf)->buf_busy_list->bl_be;
                /* buf_busy_list is now set, and we can get the backend. So lets initialize the dbimpl buffers */
                
                dblayer_bulk_set_buffer(be, &(*buf)->buf_bulk, &(*buf)->buf_bulkdata,
                        WORK_CLC_BUFFER_PAGE_SIZE, DBI_VF_BULK_RECORD);
                dblayer_value_set_buffer(be, &(*buf)->buf_key, (*buf)->buf_keydata, CSN_STRSIZE +1);
                (*buf)->buf_key.size = CSN_STRSIZE;
                set_thread_private_cache((void *)(*buf));
            } else {
                clcache_delete_buffer(buf);
            }
        }
    }

    if (NULL != *buf) {
        CSN *c_csn = NULL;
        CSN *l_csn = NULL;
        (*buf)->buf_consumer_ruv = consumer_ruv;
        (*buf)->buf_local_ruv = local_ruv;
        ruv_get_largest_csn_for_replica(consumer_ruv, consumer_rid, &c_csn);
        ruv_get_largest_csn_for_replica(local_ruv, consumer_rid, &l_csn);
        if (l_csn && csn_compare(l_csn, c_csn) > 0) {
            /* the supplier has updates for the consumer RID and
             * these updates are newer than on the consumer
             */
            (*buf)->buf_ignoreConsumerRID = 0;
        } else {
            (*buf)->buf_ignoreConsumerRID = 1;
        }
        csn_free(&c_csn);
        csn_free(&l_csn);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, get_thread_private_agmtname(),
                      "clcache_get_buffer - Can't allocate new buffer\n");
        rc = CL5_MEMORY_ERROR;
    }

    return rc;
}

/*
 * Returns a buffer back to the buffer pool.
 */
void
clcache_return_buffer(CLC_Buffer **buf)
{
    int i;

    slapi_log_err(SLAPI_LOG_REPL, (*buf)->buf_agmt_name,
                  "clcache_return_buffer - session end: state=%d load=%d sent=%d skipped=%d skipped_new_rid=%d "
                  "skipped_csn_gt_cons_maxcsn=%d skipped_up_to_date=%d "
                  "skipped_csn_gt_ruv=%d skipped_csn_covered=%d\n",
                  (*buf)->buf_state,
                  (*buf)->buf_load_cnt,
                  (*buf)->buf_record_cnt - (*buf)->buf_record_skipped,
                  (*buf)->buf_record_skipped, (*buf)->buf_skipped_new_rid,
                  (*buf)->buf_skipped_csn_gt_cons_maxcsn,
                  (*buf)->buf_skipped_up_to_date, (*buf)->buf_skipped_csn_gt_ruv,
                  (*buf)->buf_skipped_csn_covered);

    for (i = 0; i < (*buf)->buf_num_cscbs; i++) {
        clcache_free_cscb(&(*buf)->buf_cscbs[i]);
    }
    slapi_ch_free((void **)&(*buf)->buf_cscbs);

    dblayer_cursor_op(&(*buf)->buf_cursor, DBI_OP_CLOSE, NULL, NULL);
}

/*
 * Loads a buffer from DB.
 *
 * anchorcsn - passed in for the first load of a replication session;
 * continue_on_miss - tells whether session continue if a csn is missing
 * initial_starting_csn
 *              This is the starting_csn computed at the beginning of
 *              the replication session. It never change during a session
 *              (aka iterator creation).
 *              This is used for safety checking that the next CSN use
 *              for bulk load is not before the initial csn
 * return    - DB error code instead of cl5 one because of the
 *               historic reason.
 */
int
clcache_load_buffer(CLC_Buffer *buf, CSN **anchorCSN, int *continue_on_miss, char *initial_starting_csn)
{
    int rc = 0;
    dbi_op_t dbop = DBI_OP_NEXT;
    CSN limit_csn = {0};

    if (anchorCSN)
        *anchorCSN = NULL;
    clcache_refresh_local_maxcsns(buf);

    if (buf->buf_load_cnt == 0) {
        clcache_refresh_consumer_maxcsns(buf);
        rc = clcache_initial_anchorcsn(buf, &dbop);
    } else {
        rc = clcache_adjust_anchorcsn(buf, &dbop);
    }

    /* safety checking, we do not want to (re)start replication before
     * the inital computed starting point
     */
    if (initial_starting_csn) {
        csn_init_by_string(&limit_csn, initial_starting_csn);
        if (csn_compare(&limit_csn, buf->buf_current_csn) > 0) {
            char curr[CSN_STRSIZE];
            int loglevel = SLAPI_LOG_REPL;

            if (csn_time_difference(&limit_csn, buf->buf_current_csn) > (24 * 60 * 60)) {
                /* This is a big jump (more than a day) behind the
                 * initial starting csn. Log a warning before ending
                 * the session
                 */
                loglevel = SLAPI_LOG_WARNING;
            }
            csn_as_string(buf->buf_current_csn, 0, curr);
            slapi_log_err(loglevel, buf->buf_agmt_name,
                      "clcache_load_buffer - bulk load cursor (%s) is lower than starting csn %s. Ending session.\n", curr, initial_starting_csn);
            /* it just end the session with UPDATE_NO_MORE_UPDATES */
            rc = CLC_STATE_DONE;
        }
    }

    if (rc == 0) {

        buf->buf_state = CLC_STATE_READY;
        if (anchorCSN)
            *anchorCSN = buf->buf_current_csn;
        rc = clcache_load_buffer_bulk(buf, dbop);

        if (rc == DBI_RC_NOTFOUND && continue_on_miss && *continue_on_miss) {
            /* make replication going using next best startcsn */
            slapi_log_err(SLAPI_LOG_ERR, buf->buf_agmt_name,
                          "clcache_load_buffer - Can't load changelog buffer starting at CSN %s with operation(%s). "
                          "Trying to use an alterantive start CSN.\n",
                          (char *)buf->buf_key.data,
                          dblayer_op2str(dbop));
            rc = clcache_load_buffer_bulk(buf, DBI_OP_MOVE_NEAR_KEY);
            if (rc == 0) {
                slapi_log_err(SLAPI_LOG_ERR, buf->buf_agmt_name,
                              "clcache_load_buffer - Using alternative start iteration csn: %s \n",
                              (char *)buf->buf_key.data);
            }
            /* the use of alternative start csns can be limited, record its usage */
            (*continue_on_miss)--;

            if (initial_starting_csn) {
                if (csn_compare(&limit_csn, buf->buf_current_csn) > 0) {
                    char curr[CSN_STRSIZE];
                    int loglevel = SLAPI_LOG_REPL;

                    if (csn_time_difference(&limit_csn, buf->buf_current_csn) > (24 * 60 * 60)) {
                        /* This is a big jump (more than a day) behind the
                         * initial starting csn. Log a warning before ending
                         * the session
                         */
                        loglevel = SLAPI_LOG_WARNING;
                    }
                    csn_as_string(buf->buf_current_csn, 0, curr);
                    slapi_log_err(loglevel, buf->buf_agmt_name,
                            "clcache_load_buffer - (DBI_OP_MOVE_NEAR_KEY) bulk load cursor (%s) is lower than starting csn %s.\n", curr, initial_starting_csn);
                }
            }
        }
        /* Reset some flag variables */
        if (rc == 0) {
            int i;
            for (i = 0; i < buf->buf_num_cscbs; i++) {
                buf->buf_cscbs[i]->state = CLC_STATE_READY;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, buf->buf_agmt_name,
                          "clcache_load_buffer - Can't locate CSN %s in the changelog (DB rc=%d). "
                          "If replication stops, the consumer may need to be reinitialized.\n",
                          (char *)buf->buf_key.data, rc);
        }
    } else if (rc == CLC_STATE_DONE) {
        rc = DBI_RC_NOTFOUND;
    }

    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                      "clcache_load_buffer - rc=%d\n", rc);
    }

    return rc;
}

/* Set a cursor to a specific key (buf->buf_key) then load the buffer
 * This function handles the following error case:
 *  DBI_RC_BUFFER_SMALL: (realloc the buffer and retry the operation)
 *  DBI_RC_RETRY: close the index and retry the opeartion:
 *
 * The function returns the dbimpl API return code.
 */
static int
clcache_load_buffer_bulk(CLC_Buffer *buf, dbi_op_t dbop)
{
    dbi_op_t use_dbop = dbop;
    dbi_cursor_t cursor = {0};
    dbi_val_t data = {0};
    dbi_txn_t *txn = NULL;
    int tries = 0;
    int rc = 0;

    if (NULL == buf) {
        slapi_log_err(SLAPI_LOG_ERR, get_thread_private_agmtname(),
                      "clcache_load_buffer_bulk - NULL buf\n");
        return rc;
    }
    if (NULL == buf->buf_busy_list) {
        slapi_log_err(SLAPI_LOG_ERR, buf->buf_agmt_name, "clcache_load_buffer_bulk - "
                                                         "%s%sno buf_busy_list\n",
                      buf->buf_agmt_name ? buf->buf_agmt_name : "",
                      buf->buf_agmt_name ? ": " : "");
        return rc;
    }

    PR_Lock(buf->buf_busy_list->bl_lock);
retry:
    if (0 == (rc = clcache_open_cursor(txn, buf, &cursor))) {

        if (use_dbop == DBI_OP_NEXT) {
            /* For bulk read, position the cursor before read the next block */
            /* As data is not used afterwards lets try to avoid alloc buffer and reuse the bulk data buffer first */
            dblayer_value_set_buffer(cursor.be, &data, buf->buf_bulkdata, WORK_CLC_BUFFER_PAGE_SIZE);
            rc = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, &buf->buf_key, &data);
            if (DBI_RC_BUFFER_SMALL == rc) {
                /* Not enough space in buffer ==> Lets retry but this time alloc a new buffer and free it afterwards */
                dblayer_value_init(cursor.be, &data);
                rc = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, &buf->buf_key, &data);
                dblayer_value_free(cursor.be, &data);
            }
        }

        if (0 == rc) {
            rc = clcache_cursor_get(&cursor, buf, use_dbop);
        }
        dblayer_bulk_start(&buf->buf_bulk);
    }

    /*
     * Don't keep a cursor open across the whole replication session.
     * That had caused noticeable DB resource contention.
     */
    dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if ((rc == DBI_RC_RETRY) && (tries < MAX_TRIALS)) {
        PRIntervalTime interval;

        tries++;
        slapi_log_err(SLAPI_LOG_TRACE, buf->buf_agmt_name, "clcache_load_buffer_bulk - "
                                                           "deadlock number [%d] - retrying\n",
                      tries);
        /* back off */
        interval = PR_MillisecondsToInterval(slapi_rand() % 100);
        DS_Sleep(interval);
        use_dbop = dbop;
        goto retry;
    }
    if ((rc == DBI_RC_RETRY) && (tries >= MAX_TRIALS)) {
        slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name, "clcache_load_buffer_bulk - "
                                                          "could not load buffer from changelog after %d tries\n",
                      tries);
    }

    PR_Unlock(buf->buf_busy_list->bl_lock);

    if (0 == rc) {
        buf->buf_load_cnt++;
    }

    return rc;
}

/*
 * Gets the next change from the buffer.
 * *key    : output - key of the next change, or NULL if no more change
 * *data: output - data of the next change, or NULL if no more change
 */
int
clcache_get_next_change(CLC_Buffer *buf, void **key, size_t *keylen, void **data, size_t *datalen, CSN **csn, char *initial_starting_csn)
{
    dbi_val_t dbi_key = { 0 };
    dbi_val_t dbi_data = { 0 };
    int skip = 1;
    int rc = 0;

    do {
        rc = dblayer_bulk_nextrecord(&buf->buf_bulk, &dbi_key, &dbi_data);
        if (rc == DBI_RC_NOTFOUND && CLC_STATE_READY == buf->buf_state) {
            /*
             * We're done with the current buffer. Now load the next chunk.
             */
            rc = clcache_load_buffer(buf, NULL, NULL, initial_starting_csn);
            if (0 == rc) {
                rc = dblayer_bulk_nextrecord(&buf->buf_bulk, &dbi_key, &dbi_data);
            }
        }

        *key = dbi_key.data;
        *data = dbi_data.data;

        /* Compare the new change to the local and remote RUVs */
        if (NULL != *key) {
            buf->buf_record_cnt++;
            csn_init_by_string(buf->buf_current_csn, (char *)*key);
            skip = clcache_skip_change(buf);
            if (skip)
                buf->buf_record_skipped++;
        }
    } while (rc == 0 && *key && skip);

    if (NULL == *key) {
        *key = NULL;
        *csn = NULL;
        rc = DBI_RC_NOTFOUND;
    } else {
        *csn = buf->buf_current_csn;
        slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                      "clcache_get_next_change - load=%d rec=%d csn=%s\n",
                      buf->buf_load_cnt, buf->buf_record_cnt, (char *)*key);
    }

    return rc;
}

static void
clcache_refresh_consumer_maxcsns(CLC_Buffer *buf)
{
    int i;

    for (i = 0; i < buf->buf_num_cscbs; i++) {
        csn_free(&buf->buf_cscbs[i]->consumer_maxcsn);
        ruv_get_largest_csn_for_replica(
            buf->buf_consumer_ruv,
            buf->buf_cscbs[i]->rid,
            &buf->buf_cscbs[i]->consumer_maxcsn);
    }
}

static int
clcache_refresh_local_maxcsn(const ruv_enum_data *rid_data, void *data)
{
    struct clc_buffer *buf = (struct clc_buffer *)data;
    ReplicaId rid;
    int rc = 0;
    int i;

    rid = csn_get_replicaid(rid_data->csn);
    /* we do not handle updates originated at the consumer if not required
     * and we ignore RID which have been cleaned
     */
    if ((rid == buf->buf_consumer_rid && buf->buf_ignoreConsumerRID) ||
        is_cleaned_rid(rid))
        return rc;

    for (i = 0; i < buf->buf_num_cscbs; i++) {
        if (buf->buf_cscbs[i]->rid == rid)
            break;
    }
    if (i >= buf->buf_num_cscbs) {
        if (i + 1 > buf->buf_max_cscbs) {
            buf->buf_cscbs = (struct csn_seq_ctrl_block **)slapi_ch_realloc((char *)buf->buf_cscbs,
                                                                            (i + 2) * sizeof(struct csn_seq_ctrl_block *));
            buf->buf_max_cscbs = i + 1;
        }
        buf->buf_cscbs[i] = clcache_new_cscb();
        if (buf->buf_cscbs[i] == NULL) {
            return -1;
        }
        buf->buf_cscbs[i]->rid = rid;
        buf->buf_num_cscbs++;
        /* this is the first time we have a local change for the RID
         * we need to check what the consumer knows about it.
         */
        ruv_get_largest_csn_for_replica(
            buf->buf_consumer_ruv,
            buf->buf_cscbs[i]->rid,
            &buf->buf_cscbs[i]->consumer_maxcsn);
    }

    if (buf->buf_cscbs[i]->local_maxcsn)
        csn_dup_or_init_by_csn(&buf->buf_cscbs[i]->prev_local_maxcsn, buf->buf_cscbs[i]->local_maxcsn);

    csn_dup_or_init_by_csn(&buf->buf_cscbs[i]->local_maxcsn, rid_data->csn);
    csn_dup_or_init_by_csn(&buf->buf_cscbs[i]->local_mincsn, rid_data->min_csn);

    if (buf->buf_cscbs[i]->consumer_maxcsn &&
        csn_compare(buf->buf_cscbs[i]->consumer_maxcsn, rid_data->csn) >= 0) {
        /* No change need to be sent for this RID */
        buf->buf_cscbs[i]->state = CLC_STATE_UP_TO_DATE;
    }

    return rc;
}

static int
clcache_refresh_local_maxcsns(CLC_Buffer *buf)
{

    return ruv_enumerate_elements(buf->buf_local_ruv, clcache_refresh_local_maxcsn, buf, 0 /* all_elements */);
}

/*
 * Algorithm:
 *
 *    1. Determine anchorcsn for each RID:
 *    2. Determine anchorcsn for next load:
 *       Anchor-CSN = min { all Next-Anchor-CSN, Buffer-MaxCSN }
 */
static int
clcache_initial_anchorcsn(CLC_Buffer *buf, dbi_op_t *dbop)
{
    PRBool hasChange = PR_FALSE;
    struct csn_seq_ctrl_block *cscb;
    int i;
    CSN *anchorcsn = NULL;

    if (buf->buf_state == CLC_STATE_READY) {
        for (i = 0; i < buf->buf_num_cscbs; i++) {
            CSN *rid_anchor = NULL;
            dbi_op_t rid_dbop = DBI_OP_NEXT;
            cscb = buf->buf_cscbs[i];

            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                char prevmax[CSN_STRSIZE];
                char local[CSN_STRSIZE];
                char curr[CSN_STRSIZE];
                char conmaxcsn[CSN_STRSIZE];
                csn_as_string(cscb->prev_local_maxcsn, 0, prevmax);
                csn_as_string(cscb->local_maxcsn, 0, local);
                csn_as_string(buf->buf_current_csn, 0, curr);
                csn_as_string(cscb->consumer_maxcsn, 0, conmaxcsn);
                slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                              "clcache_initial_anchorcsn - "
                              "%s - (cscb %d - state %d) - csnPrevMax (%s) "
                              "csnMax (%s) csnBuf (%s) csnConsumerMax (%s)\n",
                              buf->buf_agmt_name, i, cscb->state, prevmax, local,
                              curr, conmaxcsn);
            }

            if (cscb->consumer_maxcsn == NULL) {
                /* the consumer hasn't seen changes for this RID */
                rid_anchor = cscb->local_mincsn;
                rid_dbop = DBI_OP_MOVE_TO_KEY;
            } else if (csn_compare(cscb->local_maxcsn, cscb->consumer_maxcsn) > 0) {
                rid_anchor = cscb->consumer_maxcsn;
            }

            if (rid_anchor && (anchorcsn == NULL ||
                               (csn_compare(rid_anchor, anchorcsn) < 0))) {
                anchorcsn = rid_anchor;
                *dbop = rid_dbop;
                hasChange = PR_TRUE;
            }
        }
    }

    if (!hasChange) {
        buf->buf_state = CLC_STATE_DONE;
    } else {
        csn_init_by_csn(buf->buf_current_csn, anchorcsn);
        buf->buf_key.data = csn_as_string(buf->buf_current_csn, 0, (char *)buf->buf_key.data);
        buf->buf_key.size = CSN_STRSIZE;
        slapi_log_err(SLAPI_LOG_REPL, "clcache_initial_anchorcsn",
                      "anchor is now: %s\n", (char *)buf->buf_key.data);
    }

    return buf->buf_state;
}

static int
clcache_adjust_anchorcsn(CLC_Buffer *buf, dbi_op_t *dbop)
{
    PRBool hasChange = PR_FALSE;
    struct csn_seq_ctrl_block *cscb;
    int i;
    CSN *anchorcsn = NULL;

    if (buf->buf_state == CLC_STATE_READY) {
        for (i = 0; i < buf->buf_num_cscbs; i++) {
            CSN *rid_anchor = NULL;
            int rid_dbop = DBI_OP_NEXT;
            cscb = buf->buf_cscbs[i];

            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                char prevmax[CSN_STRSIZE];
                char local[CSN_STRSIZE];
                char curr[CSN_STRSIZE];
                char conmaxcsn[CSN_STRSIZE];
                csn_as_string(cscb->prev_local_maxcsn, 0, prevmax);
                csn_as_string(cscb->local_maxcsn, 0, local);
                csn_as_string(buf->buf_current_csn, 0, curr);
                csn_as_string(cscb->consumer_maxcsn, 0, conmaxcsn);
                slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name, "clcache_adjust_anchorcsn - "
                                                                  "%s - (cscb %d - state %d) - csnPrevMax (%s) "
                                                                  "csnMax (%s) csnBuf (%s) csnConsumerMax (%s)\n",
                              buf->buf_agmt_name, i, cscb->state, prevmax, local,
                              curr, conmaxcsn);
            }

            if (csn_compare(cscb->local_maxcsn, cscb->consumer_maxcsn) > 0) {
                /* We have something to send for this RID */

                if (csn_compare(cscb->local_maxcsn, cscb->prev_local_maxcsn) == 0 ||
                    csn_compare(cscb->prev_local_maxcsn, buf->buf_current_csn) > 0) {
                    /* No new changes or it remains, in the buffer, updates to send  */
                    rid_anchor = buf->buf_current_csn;
                } else {
                    /* prev local max csn < csnBuffer AND different from local maxcsn */
                    if (cscb->consumer_maxcsn == NULL) {
                        /* the consumer hasn't seen changes for this RID */
                        rid_anchor = cscb->local_mincsn;
                        rid_dbop = DBI_OP_MOVE_TO_KEY;
                    } else {
                        rid_anchor = cscb->consumer_maxcsn;
                    }
                }
            }

            if (rid_anchor && (anchorcsn == NULL ||
                               (csn_compare(rid_anchor, anchorcsn) < 0))) {
                anchorcsn = rid_anchor;
                *dbop = rid_dbop;
                hasChange = PR_TRUE;
            }
        }
    }

    if (!hasChange) {
        buf->buf_state = CLC_STATE_DONE;
    } else {
        csn_init_by_csn(buf->buf_current_csn, anchorcsn);
        buf->buf_key.data = csn_as_string(buf->buf_current_csn, 0, (char *)buf->buf_key.data);
        buf->buf_key.size = CSN_STRSIZE;
        slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                      "clcache_adjust_anchorcsn - anchor is now: %s\n", (char *)buf->buf_key.data);
    }

    return buf->buf_state;
}

static int
clcache_skip_change(CLC_Buffer *buf)
{
    struct csn_seq_ctrl_block *cscb = NULL;
    ReplicaId rid;
    int skip = 1;
    int i;
    char buf_cur_csn_str[CSN_STRSIZE];

    do {

        rid = csn_get_replicaid(buf->buf_current_csn);

        /*
         * Skip CSN that is originated from the consumer,
         * unless the CSN is newer than the maxcsn.
         * If RID==65535, the CSN is originated from a
         * legacy consumer. In this case the supplier
         * and the consumer may have the same RID.
         */
        if (rid == buf->buf_consumer_rid && buf->buf_ignoreConsumerRID) {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                csn_as_string(buf->buf_current_csn, 0, buf_cur_csn_str);
                slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                              "clcache_skip_change - Skipping %s because the consumer with Rid: [%d] is ignored\n", buf_cur_csn_str, rid);
                buf->buf_skipped_csn_gt_cons_maxcsn++;
            }
            break;
        }

        /* Skip helper entry (ENTRY_COUNT, PURGE_RUV and so on) */
        if (cl5HelperEntry(NULL, buf->buf_current_csn) == PR_TRUE) {
            slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                          "clcache_skip_change - Skip helper entry type=%ld\n", csn_get_time(buf->buf_current_csn));
            break;
        }

        /* Find csn sequence control block for the current rid */
        for (i = 0; i < buf->buf_num_cscbs && buf->buf_cscbs[i]->rid != rid; i++)
            ;

        /* Skip CSN whose RID is unknown to the local RUV snapshot */
        if (i >= buf->buf_num_cscbs) {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                csn_as_string(buf->buf_current_csn, 0, buf_cur_csn_str);
                slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                              "clcache_skip_change - Skipping update because the changelog buffer current csn [%s] rid "
                              "[%d] is not in the list of changelog csn buffers (length %d)\n",
                              buf_cur_csn_str, rid, buf->buf_num_cscbs);
            }
            buf->buf_skipped_new_rid++;
            break;
        }

        cscb = buf->buf_cscbs[i];

        /* Skip if the consumer is already up-to-date for the RID */
        if (cscb->state == CLC_STATE_UP_TO_DATE) {
            buf->buf_skipped_up_to_date++;
            break;
        }

        /* Skip CSN whose preceedents are not covered by local RUV snapshot */
        if (cscb->state == CLC_STATE_CSN_GT_RUV) {
            buf->buf_skipped_csn_gt_ruv++;
            break;
        }

        /* Skip CSNs already covered by consumer RUV */
        if (cscb->consumer_maxcsn &&
            csn_compare(buf->buf_current_csn, cscb->consumer_maxcsn) <= 0) {
            buf->buf_skipped_csn_covered++;
            break;
        }

        /* Send CSNs that are covered by the local RUV snapshot */
        if (csn_compare(buf->buf_current_csn, cscb->local_maxcsn) <= 0) {
            skip = 0;
            csn_dup_or_init_by_csn(&cscb->consumer_maxcsn, buf->buf_current_csn);
            break;
        }

        /*
         * Promote the local maxcsn to its next neighbor
         * to keep the current session going. Skip if we
         * are not sure if current_csn is the neighbor.
         */
        if (csn_time_difference(buf->buf_current_csn, cscb->local_maxcsn) == 0 &&
            (csn_get_seqnum(buf->buf_current_csn) == csn_get_seqnum(cscb->local_maxcsn) + 1))
        {
            csn_init_by_csn(cscb->local_maxcsn, buf->buf_current_csn);
            if (cscb->consumer_maxcsn) {
                csn_init_by_csn(cscb->consumer_maxcsn, buf->buf_current_csn);
            }
            skip = 0;
            break;
        }

        /* Skip CSNs not covered by local RUV snapshot */
        cscb->state = CLC_STATE_CSN_GT_RUV;
        buf->buf_skipped_csn_gt_ruv++;

    } while (0);

#ifdef DEBUG
    if (skip && cscb) {
        char consumer[24] = {'\0'};
        char local[24] = {'\0'};
        char current[24] = {'\0'};

        if (cscb->consumer_maxcsn)
            csn_as_string(cscb->consumer_maxcsn, PR_FALSE, consumer);
        if (cscb->local_maxcsn)
            csn_as_string(cscb->local_maxcsn, PR_FALSE, local);
        csn_as_string(buf->buf_current_csn, PR_FALSE, current);
        slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                      "clcache_skip_change - Skip %s consumer=%s local=%s\n", current, consumer, local);
    }
#endif

    return skip;
}

static struct csn_seq_ctrl_block *
clcache_new_cscb(void)
{
    struct csn_seq_ctrl_block *cscb;

    cscb = (struct csn_seq_ctrl_block *)slapi_ch_calloc(1, sizeof(struct csn_seq_ctrl_block));
    if (cscb == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, NULL, "clcache: malloc failure\n");
    }
    return cscb;
}

static void
clcache_free_cscb(struct csn_seq_ctrl_block **cscb)
{
    csn_free(&(*cscb)->consumer_maxcsn);
    csn_free(&(*cscb)->local_maxcsn);
    csn_free(&(*cscb)->prev_local_maxcsn);
    csn_free(&(*cscb)->local_mincsn);
    slapi_ch_free((void **)cscb);
}

/*
 * Allocate and initialize a new buffer
 * It is called when there is a request for a buffer while
 * buffer free list is empty.
 */
static CLC_Buffer *
clcache_new_buffer(ReplicaId consumer_rid)
{
    CLC_Buffer *buf = NULL;
    int welldone = 0;

    do {

        buf = (CLC_Buffer *)slapi_ch_calloc(1, sizeof(CLC_Buffer));
        if (NULL == buf)
            break;

        if (NULL == (buf->buf_current_csn = csn_new()))
            break;

        buf->buf_state = CLC_STATE_READY;
        buf->buf_agmt_name = get_thread_private_agmtname();
        buf->buf_consumer_rid = consumer_rid;
        buf->buf_num_cscbs = 0;
        buf->buf_max_cscbs = MAX_NUM_OF_SUPPLIERS;
        buf->buf_cscbs = (struct csn_seq_ctrl_block **)slapi_ch_calloc(MAX_NUM_OF_SUPPLIERS + 1,
                                                                       sizeof(struct csn_seq_ctrl_block *));

        welldone = 1;

    } while (0);

    if (!welldone) {
        clcache_delete_buffer(&buf);
    }

    return buf;
}

/*
 * Deallocates a buffer.
 * It is called when a buffer is returned to the buffer pool
 * and the pool size is over the limit.
 */
static void
clcache_delete_buffer(CLC_Buffer **buf)
{
    if (buf && *buf) {
        dbi_val_t *bulkdata = &(*buf)->buf_bulk.v;

        if (bulkdata->data != (*buf)->buf_bulkdata) {
            slapi_ch_free(&bulkdata->data);
        }
        csn_free(&((*buf)->buf_current_csn));
        csn_free(&((*buf)->buf_missing_csn));
        csn_free(&((*buf)->buf_prev_missing_csn));
        slapi_ch_free((void **)buf);
    }
}

static CLC_Busy_List *
clcache_new_busy_list(void)
{
    CLC_Busy_List *bl;
    int welldone = 0;

    do {
        if (NULL == (bl = (CLC_Busy_List *)slapi_ch_calloc(1, sizeof(CLC_Busy_List))))
            break;

        if (NULL == (bl->bl_lock = PR_NewLock()))
            break;

        /*
        if ( NULL == (bl->bl_max_csn = csn_new ()) )
            break;
        */

        welldone = 1;
    } while (0);

    if (!welldone) {
        clcache_delete_busy_list(&bl);
    }

    return bl;
}

static void
clcache_delete_busy_list(CLC_Busy_List **bl)
{
    if (bl && *bl) {
        CLC_Buffer *buf = NULL;
        if ((*bl)->bl_lock) {
            PR_Lock((*bl)->bl_lock);
        }
        buf = (*bl)->bl_buffers;
        while (buf) {
            CLC_Buffer *next = buf->buf_next;
            clcache_delete_buffer(&buf);
            buf = next;
        }
        (*bl)->bl_buffers = NULL;
        (*bl)->bl_db = NULL;
        if ((*bl)->bl_lock) {
            PR_Unlock((*bl)->bl_lock);
            PR_DestroyLock((*bl)->bl_lock);
            (*bl)->bl_lock = NULL;
        }
        /* csn_free (&( (*bl)->bl_max_csn )); */
        slapi_ch_free((void **)bl);
    }
}

static int
clcache_enqueue_busy_list(Replica *replica, dbi_db_t *db, CLC_Buffer *buf)
{
    CLC_Busy_List *bl;
    int rc = 0;

    slapi_rwlock_rdlock(_pool->pl_lock);
    for (bl = _pool->pl_busy_lists; bl && bl->bl_db != db; bl = bl->bl_next)
        ;
    slapi_rwlock_unlock(_pool->pl_lock);

    if (NULL == bl) {
        if (NULL == (bl = clcache_new_busy_list())) {
            rc = CL5_MEMORY_ERROR;
        } else {
            slapi_rwlock_wrlock(_pool->pl_lock);
            bl->bl_db = db;
            bl->bl_be = slapi_be_select(replica_get_root(replica));
            bl->bl_next = _pool->pl_busy_lists;
            _pool->pl_busy_lists = bl;
            slapi_rwlock_unlock(_pool->pl_lock);
        }
    }

    if (NULL != bl) {
        PR_Lock(bl->bl_lock);
        buf->buf_busy_list = bl;
        buf->buf_next = bl->bl_buffers;
        bl->bl_buffers = buf;
        PR_Unlock(bl->bl_lock);
    }

    return rc;
}

static int
clcache_open_cursor(dbi_txn_t *txn, CLC_Buffer *buf, dbi_cursor_t *cursor)
{
    int rc;

    rc = dblayer_new_cursor(buf->buf_busy_list->bl_be, buf->buf_busy_list->bl_db, txn, cursor);
    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, get_thread_private_agmtname(),
                      "clcache: failed to open cursor; db error - %d %s\n",
                      rc, dblayer_strerror(rc));
    }

    return rc;
}

static int
clcache_cursor_get(dbi_cursor_t *cursor, CLC_Buffer *buf, dbi_op_t dbop)
{
    dbi_val_t *bulkdata = &buf->buf_bulk.v;
    int rc;

    if (bulkdata->data != buf->buf_bulkdata) {
        /*
         * The buffer size had been increased,
         * reset it to a smaller working size,
         * if not sufficient it will be increased again
         */
        slapi_ch_free(&bulkdata->data);
        dblayer_bulk_set_buffer(cursor->be, &buf->buf_bulk, buf->buf_bulkdata, WORK_CLC_BUFFER_PAGE_SIZE, DBI_VF_BULK_RECORD);
    }

    rc = dblayer_cursor_bulkop(cursor, dbop, &buf->buf_key, &buf->buf_bulk);
    if (DBI_RC_BUFFER_SMALL == rc) {
        /*
         * The record takes more space than the current size of the
         * buffer. Fortunately, buf->buf_bulk.v.size has been set by
         * dblayer_bulk_set_buffer() to the actual data size needed. So we can
         * reallocate the data buffer and try to read again.
         */
        bulkdata->ulen = (bulkdata->size / DEFAULT_CLC_BUFFER_PAGE_SIZE + 1) * DEFAULT_CLC_BUFFER_PAGE_SIZE;
        if (bulkdata->data != buf->buf_bulkdata) {
            bulkdata->data = slapi_ch_realloc(bulkdata->data, bulkdata->ulen);
        } else {
            bulkdata->data = slapi_ch_malloc(bulkdata->ulen);
        }
        rc = dblayer_cursor_bulkop(cursor, dbop, &buf->buf_key, &buf->buf_bulk);
        slapi_log_err(SLAPI_LOG_REPL, buf->buf_agmt_name,
                      "clcache_cursor_get - clcache: (%s) buf key len %lu reallocated and retry returns %d\n", dblayer_op2str(dbop), buf->buf_key.size, rc);
    }

    switch (rc) {
    case EINVAL:
        slapi_log_err(SLAPI_LOG_ERR, buf->buf_agmt_name,
                      "clcache_cursor_get - invalid parameter\n");
        break;

    case DBI_RC_BUFFER_SMALL:
        slapi_log_err(SLAPI_LOG_ERR, buf->buf_agmt_name,
                      "clcache_cursor_get - can't allocate %lu bytes\n", bulkdata->ulen);
        break;

    default:
        break;
    }

    return rc;
}

static void
csn_dup_or_init_by_csn(CSN **csn1, CSN *csn2)
{
    if (*csn1 == NULL)
        *csn1 = csn_new();
    csn_init_by_csn(*csn1, csn2);
}

void
clcache_destroy()
{
    if (_pool) {
        CLC_Busy_List *bl = NULL;
        if (_pool->pl_lock) {
            slapi_rwlock_wrlock(_pool->pl_lock);
        }

        bl = _pool->pl_busy_lists;
        while (bl) {
            CLC_Busy_List *next = bl->bl_next;
            clcache_delete_busy_list(&bl);
            bl = next;
        }
        _pool->pl_busy_lists = NULL;
        if (_pool->pl_lock) {
            slapi_rwlock_unlock(_pool->pl_lock);
            slapi_destroy_rwlock(_pool->pl_lock);
            _pool->pl_lock = NULL;
        }
        slapi_ch_free((void **)&_pool);
    }
}
