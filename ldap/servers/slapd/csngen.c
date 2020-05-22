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

/*
 *  csngen.c - CSN Generator
 */
#include <string.h>
#include "prcountr.h"
#include "slap.h"

#define CSN_MAX_SEQNUM 0xffff              /* largest sequence number */
#define CSN_MAX_TIME_ADJUST 24 * 60 * 60   /* maximum allowed time adjustment (in seconds) = 1 day */
#define ATTR_CSN_GENERATOR_STATE "nsState" /* attribute that stores csn state information */
#define STATE_FORMAT "%8x%8x%8x%4hx%4hx"
#define STATE_LENGTH 32
#define MAX_VAL(x, y) ((x) > (y) ? (x) : (y))
#define CSN_CALC_TSTAMP(gen) ((gen)->state.sampled_time + \
                              (gen)->state.local_offset + \
                              (gen)->state.remote_offset)

/*
 * **************************************************************************
 * data structures
 * **************************************************************************
 */

/* callback node */
typedef struct callback_node
{
    GenCSNFn gen_fn;     /* function to be called when new csn is generated */
    void *gen_arg;       /* argument to pass to gen_fn function */
    AbortCSNFn abort_fn; /* function to be called when csn is aborted */
    void *abort_arg;     /* argument to pass to abort_fn function */
} callback_node;

typedef struct callback_list
{
    Slapi_RWLock *lock;
    DataList *list; /* list of callback_node structures */
} callback_list;

/* persistently stored generator's state */
typedef struct csngen_state
{
    ReplicaId rid;        /* replica id of the replicated area to which it is attached */
    time_t sampled_time;  /* time last obtained from time() */
    time_t local_offset;  /* offset due to the local clock being set back */
    time_t remote_offset; /* offset due to clock difference with remote systems */
    PRUint16 seq_num;     /* used to allow to generate multiple csns within a second */
} csngen_state;

/* data maintained for each generator */
struct csngen
{
    csngen_state state;      /* persistent state of the generator */
    callback_list callbacks; /* list of callbacks registered with the generator */
    Slapi_RWLock *lock;      /* concurrency control */
};

/*
 * **************************************************************************
 * forward declarations    of helper functions
 * **************************************************************************
 */

static int _csngen_parse_state(CSNGen *gen, Slapi_Attr *state);
static int _csngen_init_callbacks(CSNGen *gen);
static void _csngen_call_callbacks(const CSNGen *gen, const CSN *csn, PRBool abort);
static int _csngen_cmp_callbacks(const void *el1, const void *el2);
static void _csngen_free_callbacks(CSNGen *gen);
static int _csngen_adjust_local_time(CSNGen *gen, time_t cur_time);

/*
 * **************************************************************************
 * forward declarations    of tester functions
 * **************************************************************************
 */

static int _csngen_start_test_threads(CSNGen *gen);
static void _csngen_stop_test_threads(void);
static void _csngen_gen_tester_main(void *data);
static void _csngen_local_tester_main(void *data);
static void _csngen_remote_tester_main(void *data);

/*
 * **************************************************************************
 * API
 * **************************************************************************
 */
CSNGen *
csngen_new(ReplicaId rid, Slapi_Attr *state)
{
    int rc = CSN_SUCCESS;
    CSNGen *gen = NULL;

    gen = (CSNGen *)slapi_ch_calloc(1, sizeof(CSNGen));
    if (gen == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "csngen_new", "Memory allocation failed\n");
        return NULL;
    }

    /* create lock to control the access to the state information */
    gen->lock = slapi_new_rwlock();
    if (gen->lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "csngen_new", "Failed to create lock\n");
        rc = CSN_NSPR_ERROR;
        goto done;
    }

    /* initialize callback list */
    _csngen_init_callbacks(gen);

    gen->state.rid = rid;

    if (state) {
        rc = _csngen_parse_state(gen, state);
        if (rc != CSN_SUCCESS) {
            goto done;
        }
    } else {
        /* new generator */
        gen->state.sampled_time = slapi_current_utc_time();
        gen->state.local_offset = 0;
        gen->state.remote_offset = 0;
        gen->state.seq_num = 0;
    }

done:
    if (rc != CSN_SUCCESS) {
        if (gen) {
            csngen_free(&gen);
        }

        return NULL;
    }

    return gen;
}

void
csngen_free(CSNGen **gen)
{
    if (gen == NULL || *gen == NULL)
        return;

    _csngen_free_callbacks(*gen);

    if ((*gen)->lock)
        slapi_destroy_rwlock((*gen)->lock);

    slapi_ch_free((void **)gen);
}

int
csngen_new_csn(CSNGen *gen, CSN **csn, PRBool notify)
{
    struct timespec now = {0};
    int rc = CSN_SUCCESS;
    time_t cur_time;
    int delta;

    if (gen == NULL || csn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "csngen_new_csn", "Invalid argument\n");
        return CSN_INVALID_PARAMETER;
    }

    *csn = csn_new();
    if (*csn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "csngen_new_csn", "Memory allocation failed\n");
        return CSN_MEMORY_ERROR;
    }

    if ((rc = slapi_clock_gettime(&now)) != 0) {
        /* Failed to get system time, we must abort */
        slapi_log_err(SLAPI_LOG_ERR, "csngen_new_csn",
                "Failed to get system time (%s)\n",
                slapd_system_strerror(rc));
        return CSN_TIME_ERROR;
    }
    cur_time = now.tv_sec;

    slapi_rwlock_wrlock(gen->lock);

    /* check if the time should be adjusted */
    delta = cur_time - gen->state.sampled_time;
    if (delta > _SEC_PER_DAY || delta < (-1 * _SEC_PER_DAY)) {
        /* We had a jump larger than a day */
        slapi_log_err(SLAPI_LOG_INFO, "csngen_new_csn",
                "Detected large jump in CSN time.  Delta: %d (current time: %ld  vs  previous time: %ld)\n",
                delta, cur_time, gen->state.sampled_time);
    }
    if (delta > 0) {
        rc = _csngen_adjust_local_time(gen, cur_time);
        if (rc != CSN_SUCCESS) {
            slapi_rwlock_unlock(gen->lock);
            return rc;
        }
    }
    /* if (delta < 0) this means the local system time was set back
     * the new csn will be generated based on sampled time, which is
     * ahead of system time and previously generated csns.
     * the time stamp of the csn will not change until system time
     * catches up or is corrected by remote csns.
     * But we need to ensure that the seq_num does not overflow.
     */

    if (gen->state.seq_num == CSN_MAX_SEQNUM) {
        slapi_log_err(SLAPI_LOG_INFO, "csngen_new_csn", "Sequence rollover; "
                                                        "local offset updated.\n");
        gen->state.local_offset++;
        gen->state.seq_num = 0;
    }

    (*csn)->tstamp = CSN_CALC_TSTAMP(gen);
    (*csn)->seqnum = gen->state.seq_num++;
    (*csn)->rid = gen->state.rid;
    (*csn)->subseqnum = 0;

    /* The lock is intentionally unlocked before callbacks are called.
       This is to prevent deadlocks. The callback management code has
       its own lock */
    slapi_rwlock_unlock(gen->lock);

    /* notify modules that registered interest in csn generation */
    if (notify) {
        _csngen_call_callbacks(gen, *csn, 0);
    }

    return rc;
}

/* this function should be called for csns generated with non-zero notify
   that were unused because the corresponding operation was aborted.
   The function calls "abort" functions registered through
   csngen_register_callbacks call */
void
csngen_abort_csn(CSNGen *gen, const CSN *csn)
{
    _csngen_call_callbacks(gen, csn, 1);
}

void
csngen_rewrite_rid(CSNGen *gen, ReplicaId rid)
{
    if (gen == NULL) {
        return;
    }
    slapi_rwlock_wrlock(gen->lock);
    gen->state.rid = rid;
    slapi_rwlock_unlock(gen->lock);
}

/* this function should be called when a remote CSN for the same part of
 * the dit becomes known to the server (for instance, as part of RUV during
 * replication session. In response, the generator would adjust its notion
 * of time so that it does not generate smaller csns
 *
 * The following counters are updated
 *   - when a new csn is generated
 *   - when csngen is adjusted (beginning of a incoming (extop) or outgoing
 *     (inc_protocol) session)
 *
 * sampled_time: It takes the value of current system time.
 *
 * remote offset: it is updated when 'csn' argument is ahead of the next csn
 * that the csn generator will generate. It is the MAX jump ahead, it is not
 * cumulative counter (e.g. if remote_offset=7 and 'csn' is 5sec ahead
 * remote_offset stays the same. The jump ahead (5s) pour into the local offset.
 * It is not clear of the interest of this counter. It gives an indication of
 * the maximum jump ahead but not much.
 *
 * local offset: it is increased if
 *   - system time is going backward (compare sampled_time)
 *   - if 'csn' argument is ahead of csn that the csn generator would generate
 *     AND diff('csn', csngen.new_csn) < remote_offset
 *     then the diff "pour" into local_offset
 *  It is decreased as the clock is ticking, local offset is "consumed" as
 *  sampled_time progresses.
 */
int
csngen_adjust_time(CSNGen *gen, const CSN *csn)
{
    time_t remote_time, remote_offset, cur_time;
    PRUint16 remote_seqnum;
    int rc;
    extern int config_get_ignore_time_skew(void);
    int ignore_time_skew = config_get_ignore_time_skew();

    if (gen == NULL || csn == NULL)
        return CSN_INVALID_PARAMETER;

    remote_time = csn_get_time(csn);
    remote_seqnum = csn_get_seqnum(csn);

    slapi_rwlock_wrlock(gen->lock);

    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        cur_time = CSN_CALC_TSTAMP(gen);
        slapi_log_err(SLAPI_LOG_REPL, "csngen_adjust_time",
                      "gen state before %08lx%04x:%ld:%ld:%ld\n",
                      cur_time, gen->state.seq_num,
                      gen->state.sampled_time,
                      gen->state.local_offset,
                      gen->state.remote_offset);
    }
    /* make sure we have the current time */
    cur_time = slapi_current_utc_time();

    /* make sure sampled_time is current */
    /* must only call adjust_local_time if the current time is greater than
       the generator state time */
    if ((cur_time > gen->state.sampled_time) &&
        (CSN_SUCCESS != (rc = _csngen_adjust_local_time(gen, cur_time)))) {
        /* _csngen_adjust_local_time will log error */
        slapi_rwlock_unlock(gen->lock);
        csngen_dump_state(gen, SLAPI_LOG_DEBUG);
        return rc;
    }

    cur_time = CSN_CALC_TSTAMP(gen);
    if (remote_time >= cur_time) {
        time_t new_time = 0;

        if (remote_seqnum > gen->state.seq_num) {
            if (remote_seqnum < CSN_MAX_SEQNUM) {
                gen->state.seq_num = remote_seqnum + 1;
            } else {
                remote_time++;
            }
        }

        remote_offset = remote_time - cur_time;
        if (remote_offset > gen->state.remote_offset) {
            if (ignore_time_skew || (remote_offset <= CSN_MAX_TIME_ADJUST)) {
                gen->state.remote_offset = remote_offset;
            } else /* remote_offset > CSN_MAX_TIME_ADJUST */
            {
                slapi_log_err(SLAPI_LOG_ERR, "csngen_adjust_time",
                              "Adjustment limit exceeded; value - %ld, limit - %ld\n",
                              remote_offset, (long)CSN_MAX_TIME_ADJUST);
                slapi_rwlock_unlock(gen->lock);
                csngen_dump_state(gen, SLAPI_LOG_DEBUG);
                return CSN_LIMIT_EXCEEDED;
            }
        } else if (remote_offset > 0) { /* still need to account for this */
            gen->state.local_offset += remote_offset;
        }

        new_time = CSN_CALC_TSTAMP(gen);
        /* let's revisit the seq num - if the new time is > the old
           tiem, we should reset the seq number to remote + 1 if
           this won't cause a wrap around */
        if (new_time >= cur_time) {
            /* just set seq_num regardless of whether the current one
               is < or > than the remote one - the goal of this function
               is to make sure we generate CSNs > the remote CSN - if
               we have increased the time, we can decrease the seqnum
               and still guarantee that any new CSNs generated will be
               > any current CSNs we have generated */
            if (remote_seqnum < gen->state.seq_num) {
                gen->state.seq_num ++;
            } else {
                gen->state.seq_num = remote_seqnum + 1;
            }
        }
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_err(SLAPI_LOG_REPL, "csngen_adjust_time",
                          "gen state after %08lx%04x:%ld:%ld:%ld\n",
                          new_time, gen->state.seq_num,
                          gen->state.sampled_time,
                          gen->state.local_offset,
                          gen->state.remote_offset);
        }
    } else if (gen->state.remote_offset > 0) {
        /* decrease remote offset? */
        /* how to decrease remote offset but ensure that we don't
           generate a duplicate CSN, or a CSN smaller than one we've already
           generated? */
    }

    slapi_rwlock_unlock(gen->lock);

    return CSN_SUCCESS;
}

/* returns PR_TRUE if the csn was generated by this generator and
   PR_FALSE otherwise. */
PRBool
csngen_is_local_csn(const CSNGen *gen, const CSN *csn)
{
    return (gen && csn && gen->state.rid == csn_get_replicaid(csn));
}

/* returns current state of the generator so that it can be saved in the DIT */
int
csngen_get_state(const CSNGen *gen, Slapi_Mod *state)
{
    struct berval bval;

    if (gen == NULL || state == NULL)
        return CSN_INVALID_PARAMETER;

    slapi_rwlock_rdlock(gen->lock);

    slapi_mod_init(state, 1);
    slapi_mod_set_type(state, ATTR_CSN_GENERATOR_STATE);
    slapi_mod_set_operation(state, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
    bval.bv_val = (char *)&gen->state;
    bval.bv_len = sizeof(gen->state);
    slapi_mod_add_value(state, &bval);

    slapi_rwlock_unlock(gen->lock);

    return CSN_SUCCESS;
}

/* registers callbacks to be called when csn is created or aborted */
void *
csngen_register_callbacks(CSNGen *gen, GenCSNFn genFn, void *genArg, AbortCSNFn abortFn, void *abortArg)
{
    callback_node *node;
    if (gen == NULL || (genFn == NULL && abortFn == NULL))
        return NULL;

    node = (callback_node *)slapi_ch_malloc(sizeof(callback_node));
    node->gen_fn = genFn;
    node->gen_arg = genArg;
    node->abort_fn = abortFn;
    node->abort_arg = abortArg;

    slapi_rwlock_wrlock(gen->callbacks.lock);
    dl_add(gen->callbacks.list, node);
    slapi_rwlock_unlock(gen->callbacks.lock);

    return node;
}

/* unregisters callbacks registered via call to csngenRegisterCallbacks */
void
csngen_unregister_callbacks(CSNGen *gen, void *cookie)
{
    if (gen && cookie) {
        slapi_rwlock_wrlock(gen->callbacks.lock);
        dl_delete(gen->callbacks.list, cookie, _csngen_cmp_callbacks, slapi_ch_free);
        slapi_rwlock_unlock(gen->callbacks.lock);
    }
}

/* debugging function */
void
csngen_dump_state(const CSNGen *gen, int severity)
{
    if (gen) {
        slapi_rwlock_rdlock(gen->lock);
        slapi_log_err(severity, "csngen_dump_state", "CSN generator's state:\n");
        slapi_log_err(severity, "csngen_dump_state", "\treplica id: %d\n", gen->state.rid);
        slapi_log_err(severity, "csngen_dump_state", "\tsampled time: %ld\n", gen->state.sampled_time);
        slapi_log_err(severity, "csngen_dump_state", "\tlocal offset: %ld\n", gen->state.local_offset);
        slapi_log_err(severity, "csngen_dump_state", "\tremote offset: %ld\n", gen->state.remote_offset);
        slapi_log_err(severity, "csngen_dump_state", "\tsequence number: %d\n", gen->state.seq_num);
        slapi_rwlock_unlock(gen->lock);
    }
}

#define TEST_TIME 600 /* 10 minutes */
/* This function tests csn generator. It verifies that csn's are generated in
   monotnically increasing order in the face of local and remote time skews */
void
csngen_test()
{
    int rc;
    CSNGen *gen = csngen_new(255, NULL);

    slapi_log_err(SLAPI_LOG_DEBUG, "csngen_test", "staring csn generator test ...");
    csngen_dump_state(gen, SLAPI_LOG_INFO);

    rc = _csngen_start_test_threads(gen);
    if (rc == 0) {
        DS_Sleep(PR_SecondsToInterval(TEST_TIME));
    }

    _csngen_stop_test_threads();
    csngen_dump_state(gen, SLAPI_LOG_INFO);
    slapi_log_err(SLAPI_LOG_DEBUG, "csngen_test", "csn generator test is complete...");
}

/*
 * **************************************************************************
 * Helper functions
 * **************************************************************************
 */
static int
_csngen_parse_state(CSNGen *gen, Slapi_Attr *state)
{
    int rc;
    Slapi_Value *val;
    const struct berval *bval;
    ReplicaId rid = gen->state.rid;

    PR_ASSERT(gen && state);

    rc = slapi_attr_first_value(state, &val);
    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "_csngen_parse_state", "Invalid state format\n");
        return CSN_INVALID_FORMAT;
    }

    bval = slapi_value_get_berval(val);
    memcpy(&gen->state, bval->bv_val, bval->bv_len);

    /* replicaid does not match */
    if (rid != gen->state.rid) {
        slapi_log_err(SLAPI_LOG_ERR, "_csngen_parse_state", "Replica id"
                                                            " mismatch; current id - %d, replica id in the state - %d\n",
                      rid, gen->state.rid);
        return CSN_INVALID_FORMAT;
    }

    return CSN_SUCCESS;
}

static int
_csngen_init_callbacks(CSNGen *gen)
{
    /* create a lock to control access to the callback list */
    gen->callbacks.lock = slapi_new_rwlock();
    if (gen->callbacks.lock == NULL) {
        return CSN_NSPR_ERROR;
    }

    gen->callbacks.list = dl_new();
    dl_init(gen->callbacks.list, 0);

    return CSN_SUCCESS;
}

static void
_csngen_free_callbacks(CSNGen *gen)
{
    PR_ASSERT(gen);

    if (gen->callbacks.list) {
        dl_cleanup(gen->callbacks.list, slapi_ch_free);
        dl_free(&(gen->callbacks.list));
    }

    if (gen->callbacks.lock)
        slapi_destroy_rwlock(gen->callbacks.lock);
}

static void
_csngen_call_callbacks(const CSNGen *gen, const CSN *csn, PRBool abort)
{
    int cookie;
    callback_node *node;

    PR_ASSERT(gen && csn);

    slapi_rwlock_rdlock(gen->callbacks.lock);
    node = (callback_node *)dl_get_first(gen->callbacks.list, &cookie);
    while (node) {
        if (abort) {
            if (node->abort_fn)
                node->abort_fn(csn, node->abort_arg);
        } else {
            if (node->gen_fn)
                node->gen_fn(csn, node->gen_arg);
        }
        node = (callback_node *)dl_get_next(gen->callbacks.list, &cookie);
    }

    slapi_rwlock_unlock(gen->callbacks.lock);
}

/* el1 is just a pointer to the callback_node */
static int
_csngen_cmp_callbacks(const void *el1, const void *el2)
{
    if (el1 == el2)
        return 0;

    if (el1 < el2)
        return -1;
    else
        return 1;
}

static int
_csngen_adjust_local_time(CSNGen *gen, time_t cur_time)
{
    extern int config_get_ignore_time_skew(void);
    int ignore_time_skew = config_get_ignore_time_skew();
    time_t time_diff = cur_time - gen->state.sampled_time;

    if (time_diff == 0) {
        /* This is a no op - _csngen_adjust_local_time should never be called
           in this case, because there is nothing to adjust - but just return
           here to protect ourselves
        */
        return CSN_SUCCESS;
    } else if (time_diff > 0) {
        time_t ts_before = CSN_CALC_TSTAMP(gen);
        time_t ts_after = 0;
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            time_t new_time = CSN_CALC_TSTAMP(gen);
            slapi_log_err(SLAPI_LOG_REPL, "_csngen_adjust_local_time",
                          "gen state before %08lx%04x:%ld:%ld:%ld\n",
                          new_time, gen->state.seq_num,
                          gen->state.sampled_time,
                          gen->state.local_offset,
                          gen->state.remote_offset);
        }

        gen->state.sampled_time = cur_time;
        if (time_diff > gen->state.local_offset)
            gen->state.local_offset = 0;
        else
            gen->state.local_offset = gen->state.local_offset - time_diff;

        /* only reset the seq_num if the new timestamp part of the CSN
           is going to be greater than the old one - if they are the
           same after the above adjustment (which can happen if
           csngen_adjust_time has to store the offset in the
           local_offset field) we must not allow the CSN to regress or
           generate duplicate numbers */
        ts_after = CSN_CALC_TSTAMP(gen);
        if (ts_after > ts_before) {
            gen->state.seq_num = 0; /* only reset if new time > old time */
        }

        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            time_t new_time = CSN_CALC_TSTAMP(gen);
            slapi_log_err(SLAPI_LOG_REPL, "_csngen_adjust_local_time",
                          "gen state after %08lx%04x:%ld:%ld:%ld\n",
                          new_time, gen->state.seq_num,
                          gen->state.sampled_time,
                          gen->state.local_offset,
                          gen->state.remote_offset);
        }
        return CSN_SUCCESS;
    } else /* time was turned back */
    {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            time_t new_time = CSN_CALC_TSTAMP(gen);
            slapi_log_err(SLAPI_LOG_REPL, "_csngen_adjust_local_time",
                          "gen state back before %08lx%04x:%ld:%ld:%ld\n",
                          new_time, gen->state.seq_num,
                          gen->state.sampled_time,
                          gen->state.local_offset,
                          gen->state.remote_offset);
        }

        if (!ignore_time_skew && (labs(time_diff) > CSN_MAX_TIME_ADJUST)) {
            slapi_log_err(SLAPI_LOG_ERR, "_csngen_adjust_local_time",
                          "Adjustment limit exceeded; value - %ld, limit - %d\n",
                          labs(time_diff), CSN_MAX_TIME_ADJUST);
            return CSN_LIMIT_EXCEEDED;
        }

        gen->state.sampled_time = cur_time;
        gen->state.local_offset = MAX_VAL(gen->state.local_offset, labs(time_diff));
        gen->state.seq_num = 0;

        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            time_t new_time = CSN_CALC_TSTAMP(gen);
            slapi_log_err(SLAPI_LOG_REPL, "_csngen_adjust_local_time",
                          "gen state back after %08lx%04x:%ld:%ld:%ld\n",
                          new_time, gen->state.seq_num,
                          gen->state.sampled_time,
                          gen->state.local_offset,
                          gen->state.remote_offset);
        }

        return CSN_SUCCESS;
    }
}

/*
 * **************************************************************************
 * test code
 * **************************************************************************
 */
#define DEFAULT_THREAD_STACKSIZE 0

#define GEN_TREAD_COUNT 20
int s_thread_count;
int s_must_exit;

static int
_csngen_start_test_threads(CSNGen *gen)
{
    int i;

    PR_ASSERT(gen);

    s_thread_count = 0;
    s_must_exit = 0;

    /* create threads that generate csns */
    for (i = 0; i < GEN_TREAD_COUNT; i++) {
        if (PR_CreateThread(PR_USER_THREAD, _csngen_gen_tester_main, gen,
                            PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                            DEFAULT_THREAD_STACKSIZE) == NULL) {
            PRErrorCode prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "_csngen_start_test_threads",
                          "Failed to create a CSN generator thread number %d; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          i, prerr, slapd_pr_strerror(prerr));
            return -1;
        }

        s_thread_count++;
    }

    /* create a thread that modifies remote time */
    if (PR_CreateThread(PR_USER_THREAD, _csngen_remote_tester_main, (void *)gen,
                        PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                        DEFAULT_THREAD_STACKSIZE) == NULL) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "_csngen_start_test_threads",
                      "Failed to create the remote CSN tester thread; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return -1;
    }

    s_thread_count++;

    /* create a thread that modifies local time */
    if (PR_CreateThread(PR_USER_THREAD, _csngen_local_tester_main, (void *)gen,
                        PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                        DEFAULT_THREAD_STACKSIZE) == NULL)

    {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "_csngen_start_test_threads",
                      "Failed to create the local CSN tester thread; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      prerr, slapd_pr_strerror(prerr));
        return -1;
    }

    s_thread_count++;


    return 0;
}

static void
_csngen_stop_test_threads(void)
{
    s_must_exit = 1;

    while (s_thread_count > 0) {
        /* sleep for 30 seconds */
        DS_Sleep(PR_SecondsToInterval(20));
    }
}

/* periodically generate a csn and dump it to the error log */
static void
_csngen_gen_tester_main(void *data)
{
    CSNGen *gen = (CSNGen *)data;
    CSN *csn = NULL;
    char buff[CSN_STRSIZE];
    int rc;

    PR_ASSERT(gen);

    while (!s_must_exit) {
        rc = csngen_new_csn(gen, &csn, PR_FALSE);
        if (rc != CSN_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "_csngen_gen_tester_main",
                          "failed to generate csn; csn error - %d\n", rc);
        } else {
            slapi_log_err(SLAPI_LOG_INFO, "_csngen_gen_tester_main", "generate csn %s\n",
                          csn_as_string(csn, PR_FALSE, buff));
        }
        csn_free(&csn);

        /* sleep for 30 seconds */
        DS_Sleep(PR_SecondsToInterval(10));
    }

    PR_AtomicDecrement(&s_thread_count);
}

/* simulate clock skew with remote servers that causes
   generator to advance its remote offset */
static void
_csngen_remote_tester_main(void *data)
{
    CSNGen *gen = (CSNGen *)data;
    CSN *csn;
    time_t csn_time;
    int rc;

    PR_ASSERT(gen);

    while (!s_must_exit) {
        rc = csngen_new_csn(gen, &csn, PR_FALSE);
        if (rc != CSN_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "_csngen_remote_tester_main",
                          "Failed to generate csn; csn error - %d\n", rc);
        } else {
            csn_time = csn_get_time(csn);
            csn_set_time(csn, csn_time + slapi_rand() % 100);

            rc = csngen_adjust_time(gen, csn);
            if (rc != CSN_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "_csngen_remote_tester_main",
                              "Failed to adjust generator's time; csn error - %d\n", rc);
            }

            csngen_dump_state(gen, SLAPI_LOG_INFO);
        }
        csn_free(&csn);

        /* sleep for 30 seconds */
        DS_Sleep(PR_SecondsToInterval(60));
    }

    PR_AtomicDecrement(&s_thread_count);
}

/* simulate local clock being set back */
static void
_csngen_local_tester_main(void *data)
{
    CSNGen *gen = (CSNGen *)data;

    PR_ASSERT(gen);


    while (!s_must_exit) {
        /* sleep for 30 seconds */
        DS_Sleep(PR_SecondsToInterval(60));

        /*
         * g_sampled_time -= slapi_rand () % 100;
         */

        csngen_dump_state(gen, SLAPI_LOG_INFO);
    }

    PR_AtomicDecrement(&s_thread_count);
}
