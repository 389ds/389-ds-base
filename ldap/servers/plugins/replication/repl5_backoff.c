/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* repl5_backoff.c */
/*

 The backoff object implements a backoff timer. The timer can operate
 with a fixed interval, an expontially increasing interval, or a
 random interval.

 The caller creates a new backoff timer, specifying the backoff behavior
 desired (fixed, exponential, or random), the initial backoff value,
 and the maximum backoff interval. This does not start the timer - the
 backoff_reset() function must be used to actually start the timer.

 The backoff_reset() function takes an optional function that
 will be called when the backoff time has expired, and a void *
 that can be used to pass arguments into the callback function.

 When the time expires, the callback function will be called. If no
 callback function has been provided, the timer simply expires.
 A timer does not recompute the next interval and begin timing until
 the backoff_step() function is called. Therefore, callers that
 do not install a callback function may use the timer by polling.
 When a callback function is provided, the timer is typically reset
 inside the callback function.

*/

#include "repl5.h"


typedef struct backoff_timer {
	int type;
	int running;
	slapi_eq_fn_t callback;
	void *callback_arg;
	time_t initial_interval;
	time_t next_interval;
	time_t max_interval;
	time_t last_fire_time;
	Slapi_Eq_Context pending_event;
	PRLock *lock;
	
} backoff_timer;

/* Forward declarations */
static PRIntervalTime random_interval_in_range(time_t lower_bound, time_t upper_bound);


/*
 Create a new backoff timer. The timer is initialized, but is not
 started.
 */
Backoff_Timer *
backoff_new(int timer_type, int initial_interval, int max_interval)
{
	Backoff_Timer *bt;

	bt = (Backoff_Timer *)slapi_ch_calloc(1, sizeof(struct backoff_timer));
	bt->type = timer_type;
	bt->initial_interval = initial_interval;
	bt->next_interval = bt->initial_interval;
	bt->max_interval = max_interval;
	bt->running = 0;
	if ((bt->lock = PR_NewLock()) == NULL)
	{
		slapi_ch_free((void **)&bt);
	}
	return bt;
}


/*
 * Reset and start the timer. Returns the time (as a time_t) when the
 * time will next expire.
 */
time_t
backoff_reset(Backoff_Timer *bt, slapi_eq_fn_t callback, void *callback_data)
{
	time_t return_value = 0UL;

	PR_ASSERT(NULL != bt);
	PR_ASSERT(NULL != callback);

	PR_Lock(bt->lock);
	bt->running = 1;
	bt->callback = callback;
	bt->callback_arg = callback_data;
	/* Cancel any pending events in the event queue */
	if (NULL != bt->pending_event)
	{
		slapi_eq_cancel(bt->pending_event);
		bt->pending_event = NULL;
	}
	/* Compute the first fire time */
	if (BACKOFF_RANDOM == bt->type)
	{
		bt->next_interval = random_interval_in_range(bt->initial_interval,
			bt->max_interval);
	}
	else
	{
		bt->next_interval = bt->initial_interval;
	}
	/* Schedule the callback */
	time(&bt->last_fire_time);
	return_value = bt->last_fire_time + bt->next_interval;
	bt->pending_event = slapi_eq_once(bt->callback, bt->callback_arg,
		return_value);
	PR_Unlock(bt->lock);
	return return_value;
}


/*
 Step the timer - compute the new backoff interval and start
 counting. Note that the next expiration time is based on the
 last timer expiration time, *not* the current time. 

 Returns the time (as a time_t) when the timer will next expire.
 */
time_t
backoff_step(Backoff_Timer *bt)
{
	time_t return_value = 0UL;

	PR_ASSERT(NULL != bt);

	/* If the timer has never been reset, then return 0 */
	PR_Lock(bt->lock);
	if (bt->running)
	{
		time_t previous_interval = bt->next_interval;
		switch (bt->type) {
		case BACKOFF_FIXED:
			/* Interval stays the same */
			break;
		case BACKOFF_EXPONENTIAL:
			/* Interval doubles, up to a maximum */
			if (bt->next_interval < bt->max_interval)
			{
				bt->next_interval *= 2;
				if (bt->next_interval > bt->max_interval)
				{
					bt->next_interval = bt->max_interval;
				}
			}
			break;
		case BACKOFF_RANDOM:
			/* Compute the new random interval time */
			bt->next_interval = random_interval_in_range(bt->initial_interval,
				bt->max_interval);
			break;
		}
		/* Schedule the callback, if any */
		bt->last_fire_time += previous_interval;
		return_value = bt->last_fire_time + bt->next_interval;
		bt->pending_event = slapi_eq_once(bt->callback, bt->callback_arg,
			return_value);
	}
	PR_Unlock(bt->lock);
	return return_value;
}


/*
 * Return 1 if the backoff timer has expired, 0 otherwise.
 */
int
backoff_expired(Backoff_Timer *bt, int margin)
{
	int return_value = 0;

	PR_ASSERT(NULL != bt);
	PR_Lock(bt->lock);
	return_value = (current_time() >= (bt->last_fire_time + bt->next_interval + margin));
	PR_Unlock(bt->lock);
	return return_value;
}


/*
 Destroy and deallocate a timer object
 */
void
backoff_delete(Backoff_Timer **btp)
{
	Backoff_Timer *bt;

	PR_ASSERT(NULL != btp && NULL != *btp);
	bt = *btp;
	PR_Lock(bt->lock);
	/* Cancel any pending events in the event queue */
	if (NULL != bt->pending_event)
	{
		slapi_eq_cancel(bt->pending_event);
	}
	PR_Unlock(bt->lock);
	PR_DestroyLock(bt->lock);
	slapi_ch_free((void **)btp);
}


/*
 * Return the next fire time for the timer.
 */
time_t
backoff_get_next_fire_time(Backoff_Timer *bt)
{
	time_t return_value;

	PR_ASSERT(NULL != bt);
	PR_Lock(bt->lock);
	return_value = bt->last_fire_time + bt->next_interval;
	PR_Unlock(bt->lock);
	return return_value;
}

static PRIntervalTime
random_interval_in_range(time_t lower_bound, time_t upper_bound)
{
	/*
	 * slapi_rand() provides some entropy from two or three system timer
	 * calls (depending on the platform) down in NSS. If more entropy is
	 * required, slapi_rand_r(unsigned int *seed) can be called instead.
 	 */
	return(lower_bound + (slapi_rand() % (upper_bound - lower_bound)));
}

