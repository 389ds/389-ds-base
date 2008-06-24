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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*
 *  csngen.c - CSN Generator
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0400
#include <windows.h>
#endif

#include <string.h>
#include "prcountr.h"
#include "slap.h"

#define CSN_MAX_SEQNUM			 0xffff		/* largest sequence number */
#define CSN_MAX_TIME_ADJUST		 24*60*60	/* maximum allowed time adjustment (in seconds) = 1 day */ 
#define ATTR_CSN_GENERATOR_STATE "nsState"	/* attribute that stores csn state information */
#define STATE_FORMAT			 "%8x%8x%8x%4hx%4hx"
#define STATE_LENGTH			 32
#define MAX_VAL(x,y)			 ((x)>(y)?(x):(y))
#define CSN_CALC_TSTAMP(gen)     ((gen)->state.sampled_time + \
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
	GenCSNFn	gen_fn;		/* function to be called when new csn is generated */
	void		*gen_arg;	/* argument to pass to gen_fn function */
	AbortCSNFn	abort_fn;	/* function to be called when csn is aborted */
	void		*abort_arg;	/* argument to pass to abort_fn function */
} callback_node;

typedef struct callback_list 
{
	PRRWLock *lock;
	DataList *list;	/* list of callback_node structures */
} callback_list;

/* persistently stored generator's state */
typedef struct csngen_state
{
    ReplicaId	rid;			/* replica id of the replicated area to which it is attached */
	time_t		sampled_time;	/* time last obtained from time() */
	time_t		local_offset;	/* offset due to the local clock being set back */
	time_t		remote_offset;	/* offset due to clock difference with remote systems */
	PRUint16	seq_num;		/* used to allow to generate multiple csns within a second */
}csngen_state;

/* data maintained for each generator */
struct csngen
{
	csngen_state  state;        /* persistent state of the generator */
	callback_list callbacks;	/* list of callbacks registered with the generator */
	PRRWLock	  *lock;		/* concurrency control */
};

/*
 * **************************************************************************
 * global data
 * **************************************************************************
 */

static time_t g_sampled_time;	/* time obtained from time() call */

/*
 * **************************************************************************
 * forward declarations	of helper functions
 * **************************************************************************
 */

static int _csngen_parse_state (CSNGen *gen, Slapi_Attr *state);
static int _csngen_init_callbacks (CSNGen *gen);
static void _csngen_call_callbacks (const CSNGen *gen, const CSN *csn, PRBool abort);
static int _csngen_cmp_callbacks (const void *el1, const void *el2);
static void _csngen_free_callbacks (CSNGen *gen);
static int _csngen_adjust_local_time (CSNGen *gen, time_t cur_time);

/*
 * **************************************************************************
 * forward declarations	of tester functions
 * **************************************************************************
 */

static int _csngen_start_test_threads (CSNGen *gen);
static void _csngen_stop_test_threads ();
static void _csngen_gen_tester_main (void *data); 
static void _csngen_local_tester_main (void *data); 
static void _csngen_remote_tester_main (void *data); 

/*
 * **************************************************************************
 * API
 * **************************************************************************
 */
CSNGen* 
csngen_new (ReplicaId rid, Slapi_Attr *state)
{
	int rc = CSN_SUCCESS;
   	CSNGen *gen = NULL;

	gen = (CSNGen*)slapi_ch_calloc (1, sizeof (CSNGen));
	if (gen == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "csngen_new: memory allocation failed\n");
		return NULL;
	}

	/* create lock to control the access to the state information */
	gen->lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "state_lock");	
	if (gen->lock == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "csngen_new: failed to create lock\n");
		rc = CSN_NSPR_ERROR;
		goto done;
	}

	/* initialize callback list */
	_csngen_init_callbacks (gen);

	gen->state.rid  = rid;

	if (state)
	{
		rc = _csngen_parse_state (gen, state);
		if (rc != CSN_SUCCESS)
		{
			goto done;
		}
	}
	else
	{
		/* new generator */
		gen->state.sampled_time = current_time ();
		gen->state.local_offset = 0;
		gen->state.remote_offset = 0;
		gen->state.seq_num = 0;
	}

done:
	if (rc != CSN_SUCCESS)
	{
		if (gen)
		{
			csngen_free (&gen);	
		}

		return NULL;
	}

    return gen;
}

void 
csngen_free (CSNGen **gen)
{
    if (gen == NULL || *gen == NULL)
		return;

	_csngen_free_callbacks (*gen);	
	
	if ((*gen)->lock)
		PR_DestroyRWLock ((*gen)->lock);
}

int 
csngen_new_csn (CSNGen *gen, CSN **csn, PRBool notify)
{
	int rc = CSN_SUCCESS;
    time_t cur_time;
	int delta;

	if (gen == NULL || csn == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "csngen_new_csn: invalid argument\n");
		return CSN_INVALID_PARAMETER;
	}

	*csn = csn_new ();
	if (*csn == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "csngen_new_csn: memory allocation failed\n");
		return CSN_MEMORY_ERROR;
	}

    PR_RWLock_Wlock (gen->lock);

    if (g_sampled_time == 0)
        csngen_update_time ();

    cur_time = g_sampled_time;

    /* check if the time should be adjusted */
	delta = cur_time - gen->state.sampled_time;
    if (delta > 0)
    {
        rc = _csngen_adjust_local_time (gen, cur_time);
        if (rc != CSN_SUCCESS)
        {
            PR_RWLock_Unlock (gen->lock);
            return rc;
        }
    }
	else if (delta < -300) {
		/*
		 * The maxseqnum could support up to 65535 CSNs per second.
		 * That means that we could avoid duplicated CSN's for
		 * delta up to 300 secs if update rate is 200/sec (usually
		 * the max rate is below 20/sec).
		 * Beyond 300 secs, we advance gen->state.sampled_time by
		 * one sec to recycle seqnum.
		 */
        slapi_log_error (SLAPI_LOG_FATAL, "csngen_new_csn", "Warning: too much time skew (%d secs). Current seqnum=%0x\n", delta, gen->state.seq_num );
        rc = _csngen_adjust_local_time (gen, gen->state.sampled_time+1);
        if (rc != CSN_SUCCESS)
        {
            PR_RWLock_Unlock (gen->lock);
            return rc;
        }
		
	}

    if (gen->state.seq_num == CSN_MAX_SEQNUM)
    {
        slapi_log_error (SLAPI_LOG_FATAL, NULL, "csngen_new_csn: sequence rollover; "
                         "local offset updated.\n");
        gen->state.local_offset ++;
        gen->state.seq_num = 0;
    }

    (*csn)->tstamp = CSN_CALC_TSTAMP(gen);
    (*csn)->seqnum = gen->state.seq_num ++;
    (*csn)->rid = gen->state.rid;
	(*csn)->subseqnum = 0;

    /* The lock is intentionally unlocked before callbacks are called.
       This is to prevent deadlocks. The callback management code has
       its own lock */
    PR_RWLock_Unlock (gen->lock);

    /* notify modules that registered interest in csn generation */
    if (notify)
    {
        _csngen_call_callbacks (gen, *csn, 0);
    }
    
	return rc;
}

/* this function should be called for csns generated with non-zero notify
   that were unused because the corresponding operation was aborted.
   The function calls "abort" functions registered through
   csngen_register_callbacks call */
void csngen_abort_csn (CSNGen *gen, const CSN *csn)
{
    _csngen_call_callbacks (gen, csn, 1);
}

/* this function should be called when a remote CSN for the same part of
   the dit becomes known to the server (for instance, as part of RUV during
   replication session. In response, the generator would adjust its notion
   of time so that it does not generate smaller csns */
int csngen_adjust_time (CSNGen *gen, const CSN* csn)
{
    time_t remote_time, remote_offset, cur_time;
	PRUint16 remote_seqnum;
    int rc;

    if (gen == NULL || csn == NULL)
        return CSN_INVALID_PARAMETER;

    remote_time = csn_get_time (csn);
	remote_seqnum = csn_get_seqnum (csn);

    PR_RWLock_Wlock (gen->lock);

    /* make sure we have the current time */
    csngen_update_time();
    cur_time = g_sampled_time;

    /* make sure sampled_time is current */
    /* must only call adjust_local_time if the current time is greater than
       the generator state time */
    if ((cur_time > gen->state.sampled_time) &&
        (CSN_SUCCESS != (rc = _csngen_adjust_local_time(gen, cur_time))))
    {
        /* _csngen_adjust_local_time will log error */
        PR_RWLock_Unlock (gen->lock);
        csngen_dump_state(gen);
        return rc;
    }

    cur_time = CSN_CALC_TSTAMP(gen);
    if (remote_time >= cur_time)
    {
        if (remote_seqnum > gen->state.seq_num )
        {
            if (remote_seqnum < CSN_MAX_SEQNUM)
            {
                gen->state.seq_num = remote_seqnum + 1;
            }
            else
            {
                remote_time++;
            }
        }

        remote_offset = remote_time - cur_time;
		if (remote_offset > gen->state.remote_offset)
		{
			if (remote_offset <= CSN_MAX_TIME_ADJUST)
			{
	        	gen->state.remote_offset = remote_offset;
			}
	    	else /* remote_offset > CSN_MAX_TIME_ADJUST */
			{
				slapi_log_error (SLAPI_LOG_FATAL, NULL, "csngen_adjust_time: "
                            "adjustment limit exceeded; value - %ld, limit - %ld\n",
                             remote_offset, (long)CSN_MAX_TIME_ADJUST);
				PR_RWLock_Unlock (gen->lock);
				csngen_dump_state(gen);
				return CSN_LIMIT_EXCEEDED;
			}
		}
	}
	else if (gen->state.remote_offset > 0)
	{
		/* decrease remote offset? */
		/* how to decrease remote offset but ensure that we don't
		   generate a duplicate CSN, or a CSN smaller than one we've already
		   generated? */
	}

    PR_RWLock_Unlock (gen->lock);

    return CSN_SUCCESS;
}

/* returns PR_TRUE if the csn was generated by this generator and 
   PR_FALSE otherwise. */
PRBool csngen_is_local_csn(const CSNGen *gen, const CSN *csn)
{
    return (gen && csn && gen->state.rid == csn_get_replicaid(csn));
}

/* returns current state of the generator so that it can be saved in the DIT */
int csngen_get_state (const CSNGen *gen, Slapi_Mod *state)
{
    struct berval bval;

    if (gen == NULL || state == NULL)
        return CSN_INVALID_PARAMETER;

    PR_RWLock_Rlock (gen->lock);

    slapi_mod_init (state, 1);
    slapi_mod_set_type (state, ATTR_CSN_GENERATOR_STATE);
    slapi_mod_set_operation (state, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
    bval.bv_val = (char*)&gen->state;
    bval.bv_len = sizeof (gen->state);
    slapi_mod_add_value(state, &bval);

    PR_RWLock_Unlock (gen->lock);

    return CSN_SUCCESS;
}

/* registers callbacks to be called when csn is created or aborted */
void* csngen_register_callbacks(CSNGen *gen, GenCSNFn genFn, void *genArg, 
						        AbortCSNFn abortFn, void *abortArg)
{
    callback_node *node;
    if (gen == NULL || (genFn == NULL && abortFn == NULL))
        return NULL;

    node = (callback_node *)slapi_ch_malloc (sizeof (callback_node));
    node->gen_fn = genFn;
    node->gen_arg = genArg;
    node->abort_fn = abortFn;
    node->abort_arg = abortArg;

    PR_RWLock_Wlock (gen->callbacks.lock);
    dl_add (gen->callbacks.list, node);
    PR_RWLock_Unlock (gen->callbacks.lock);

    return node;
}

/* unregisters callbacks registered via call to csngenRegisterCallbacks */
void csngen_unregister_callbacks(CSNGen *gen, void *cookie)
{
    if (gen && cookie)
    {
        PR_RWLock_Wlock (gen->callbacks.lock);
        dl_delete (gen->callbacks.list, cookie, _csngen_cmp_callbacks, slapi_ch_free);
        PR_RWLock_Unlock (gen->callbacks.lock);
    }
}

/* this functions is periodically called from daemon.c to
   update time used by all generators */
void csngen_update_time ()
{
    g_sampled_time = current_time ();
}

/* debugging function */
void csngen_dump_state (const CSNGen *gen)
{
    if (gen)
    {
        PR_RWLock_Rlock (gen->lock);
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "CSN generator's state:\n");
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "\treplica id: %d\n", gen->state.rid);
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "\tsampled time: %ld\n", gen->state.sampled_time);
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "\tlocal offset: %ld\n", gen->state.local_offset);
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "\tremote offset: %ld\n", gen->state.remote_offset);
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "\tsequence number: %d\n", gen->state.seq_num);
        PR_RWLock_Unlock (gen->lock);
    }
}

#define TEST_TIME   600     /* 10 minutes */
/* This function tests csn generator. It verifies that csn's are generated in
   monotnically increasing order in the face of local and remote time skews */
void csngen_test ()
{
    int rc;
    CSNGen *gen = csngen_new (255, NULL);

    slapi_log_error(SLAPI_LOG_FATAL, NULL, "staring csn generator test ...");
    csngen_dump_state (gen);

    rc = _csngen_start_test_threads(gen);
    if (rc == 0)
    {
        DS_Sleep(PR_SecondsToInterval(TEST_TIME));
    }
    
    _csngen_stop_test_threads(gen);
    csngen_dump_state (gen);    
    slapi_log_error(SLAPI_LOG_FATAL, NULL, "csn generator test is complete...");
}

/*
 * **************************************************************************
 * Helper functions
 * **************************************************************************
 */
static int 
_csngen_parse_state (CSNGen *gen, Slapi_Attr *state)
{
	int rc;
	Slapi_Value *val;
    const struct berval *bval;
	ReplicaId rid = gen->state.rid;
	
	PR_ASSERT (gen && state);

	rc = slapi_attr_first_value(state, &val);
	if (rc != 0)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "_csngen_parse_state: invalid state format\n");
		return CSN_INVALID_FORMAT;
	}

    bval = slapi_value_get_berval(val);
    memcpy (&gen->state, bval->bv_val, bval->bv_len);

	/* replicaid does not match */
	if (rid != gen->state.rid)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "_csngen_parse_state: replica id"
                         " mismatch; current id - %d, replica id in the state - %d\n", 
                          rid, gen->state.rid);
		return CSN_INVALID_FORMAT;
	}

	return CSN_SUCCESS;
}

static int 
_csngen_init_callbacks (CSNGen *gen)
{
	/* create a lock to control access to the callback list */
	gen->callbacks.lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "callback_lock");	
	if (gen->callbacks.lock == NULL)
	{
		return CSN_NSPR_ERROR;
	}

    gen->callbacks.list = dl_new ();
	dl_init (gen->callbacks.list, 0);

    return CSN_SUCCESS;
}

static void 
_csngen_free_callbacks (CSNGen *gen)
{	
	PR_ASSERT (gen);

	if (gen->callbacks.list)
	{
		dl_cleanup (gen->callbacks.list, slapi_ch_free);
		dl_free (&(gen->callbacks.list));
	}

	if (gen->callbacks.lock)
		PR_DestroyRWLock (gen->callbacks.lock);
}

static void 
_csngen_call_callbacks (const CSNGen *gen, const CSN *csn, PRBool abort)
{
	int cookie;
	callback_node* node;

	PR_ASSERT (gen && csn);
	
	PR_RWLock_Rlock (gen->callbacks.lock);
	node = (callback_node*)dl_get_first (gen->callbacks.list, &cookie);
	while (node)
	{
		if (abort)
		{
			if (node->abort_fn)
				node->abort_fn (csn, node->abort_arg);
		}
		else
		{
			if (node->gen_fn)
				node->gen_fn (csn, node->gen_arg);
		}
		node = (callback_node*)dl_get_next (gen->callbacks.list, &cookie);
	}

	PR_RWLock_Unlock (gen->callbacks.lock);
}

/* el1 is just a pointer to the callback_node */
static int 
_csngen_cmp_callbacks (const void *el1, const void *el2)
{
	if (el1 == el2)
		return 0;
	
	if (el1 < el2)
		return -1;
	else
		return 1;
}

static int 
_csngen_adjust_local_time (CSNGen *gen, time_t cur_time)
{
    time_t time_diff = cur_time - gen->state.sampled_time;

    if (time_diff == 0) {
        /* This is a no op - _csngen_adjust_local_time should never be called
           in this case, because there is nothing to adjust - but just return
           here to protect ourselves
        */
        return CSN_SUCCESS;
    }
    else if (time_diff > 0)
    {
        gen->state.sampled_time = cur_time;
        if (time_diff > gen->state.local_offset)
            gen->state.local_offset = 0;
        else
            gen->state.local_offset = gen->state.local_offset - time_diff;

        gen->state.seq_num = 0;

        return CSN_SUCCESS;
    }
    else   /* time was turned back */
    {
        if (abs (time_diff) > CSN_MAX_TIME_ADJUST)
        {
            slapi_log_error (SLAPI_LOG_FATAL, NULL, "_csngen_adjust_local_time: "
                             "adjustment limit exceeded; value - %d, limit - %d\n",
                             abs (time_diff), CSN_MAX_TIME_ADJUST);
            return CSN_LIMIT_EXCEEDED;
        }    

        gen->state.sampled_time = cur_time;
        gen->state.local_offset = MAX_VAL (gen->state.local_offset, abs (time_diff));
        gen->state.seq_num = 0;

        return CSN_SUCCESS;
    }
}

/*
 * **************************************************************************
 * test code
 * **************************************************************************
 */

/* 
 * The defult thread stacksize for nspr21 is 64k. For OSF, we require
 * a larger stacksize as actual storage allocation is higher i.e
 * pointers are allocated 8 bytes but lower 4 bytes are used.
 * The value 0 means use the default stacksize.
 */
#if defined (OSF1) || defined(__LP64__) || defined (_LP64) /* 64-bit architectures need large stacks */
#define DEFAULT_THREAD_STACKSIZE 	131072L
#else
#define DEFAULT_THREAD_STACKSIZE 	0
#endif

#define GEN_TREAD_COUNT	20
int s_thread_count;
int s_must_exit;

static int
_csngen_start_test_threads(CSNGen *gen)
{
    int i;

	PR_ASSERT (gen);

	s_thread_count = 0;
	s_must_exit = 0;
    
	/* create threads that generate csns */
    for(i=0; i< GEN_TREAD_COUNT; i++) 
	{ 
		if (PR_CreateThread(PR_USER_THREAD,	_csngen_gen_tester_main, gen, 
							PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, 
							DEFAULT_THREAD_STACKSIZE) == NULL)
		{
			PRErrorCode prerr = PR_GetError();
			slapi_log_error(SLAPI_LOG_FATAL, NULL, 
							"failed to create a CSN generator thread number %d; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n", 
							i, prerr, slapd_pr_strerror(prerr));
			return -1;	
		}

		s_thread_count ++;
    }

	/* create a thread that modifies remote time */
	if (PR_CreateThread(PR_USER_THREAD, _csngen_remote_tester_main, (void *)gen,
						PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
						DEFAULT_THREAD_STACKSIZE) == NULL)
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"failed to create the remote CSN tester thread; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n", 
						prerr, slapd_pr_strerror(prerr));
		return -1;
	}

	s_thread_count ++;

	/* create a thread that modifies local time */
	if (PR_CreateThread(PR_USER_THREAD, _csngen_local_tester_main, (void *)gen,
						PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
						DEFAULT_THREAD_STACKSIZE) == NULL)

	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"failed to create the local CSN tester thread; " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n", 
						prerr, slapd_pr_strerror(prerr));
		return -1;
	}

	s_thread_count ++;

	
    return 0;
}

static void _csngen_stop_test_threads ()
{
	s_must_exit = 1;

	while (s_thread_count > 0)
	{
		/* sleep for 30 seconds */
		DS_Sleep (PR_SecondsToInterval(20));
	}
}

/* periodically generate a csn and dump it to the error log */
static void
_csngen_gen_tester_main (void *data) 
{
	CSNGen *gen = (CSNGen*)data;
	CSN *csn;
	char buff [CSN_STRSIZE];
	int rc;

	PR_ASSERT (gen);

    while (!s_must_exit)
	{
		rc = csngen_new_csn (gen, &csn, PR_FALSE);
		if (rc != CSN_SUCCESS)
		{
			slapi_log_error (SLAPI_LOG_FATAL, NULL, 
							 "failed to generate csn; csn error - %d\n", rc);
		}
		else
		{
			slapi_log_error (SLAPI_LOG_FATAL, NULL, "generate csn %s\n", 
							 csn_as_string(csn, PR_FALSE, buff));
		}	

		/* sleep for 30 seconds */
		DS_Sleep (PR_SecondsToInterval(10));
	}

	PR_AtomicDecrement (&s_thread_count);
}

/* simulate clock skew with remote servers that causes
   generator to advance its remote offset */
static void
_csngen_remote_tester_main (void *data) 
{
	CSNGen *gen = (CSNGen*)data;
	CSN *csn;
	time_t csn_time;
	int rc;

	PR_ASSERT (gen);

	while (!s_must_exit)
	{
		rc = csngen_new_csn (gen, &csn, PR_FALSE);
		if (rc != CSN_SUCCESS)
		{
			slapi_log_error (SLAPI_LOG_FATAL, NULL, 
							 "failed to generate csn; csn error - %d\n", rc);
		}
		else
		{
			csn_time = csn_get_time(csn);			
			csn_set_time (csn, csn_time + slapi_rand () % 100);

			rc = csngen_adjust_time (gen, csn);
			if (rc != CSN_SUCCESS)
			{
				slapi_log_error (SLAPI_LOG_FATAL, NULL, 
								 "failed to adjust generator's time; csn error - %d\n", rc);
			}

			csngen_dump_state (gen);

		}	

		/* sleep for 30 seconds */
		DS_Sleep (PR_SecondsToInterval(60));
	}

	PR_AtomicDecrement (&s_thread_count);
}

/* simulate local clock being set back */
static void
_csngen_local_tester_main (void *data) 
{
	CSNGen *gen = (CSNGen*)data;

	PR_ASSERT (gen);


	while (!s_must_exit)
	{
		/* sleep for 30 seconds */
		DS_Sleep (PR_SecondsToInterval(60));

		g_sampled_time -= slapi_rand () % 100;		

		csngen_dump_state (gen);
	}

	PR_AtomicDecrement (&s_thread_count);
}


