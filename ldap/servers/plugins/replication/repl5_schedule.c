/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* repl5_schedule.c */
/*

The schedule object implements the scheduling policy for a DS 5.0 replication
supplier.

Methods:
schedule_set() - sets the schedule
schedule_get() - gets the schedule
schedule_in_window_now() - returns TRUE if a replication session
  should commence.
schedule_next() - returns the next time that replication is
  scheduled to occur.
schedule_notify() - called to inform the scheduler when entries
  have been updated.
schedule_set_priority_attributes() - sets the attributes that are
  considered "high priority". A modification to one of these attributes
  will cause replication to commence asap, overriding the startup
  delay and maximum backlog. Also includes an additional parameter
  that controls whether priority attributes are propagated regardless
  of the scheduling window, e.g. it's possible to configure things
  so that password changes get propagated even if we're not in a
  replication window.
schedule_set_startup_delay() - sets the time that replication should
  wait before commencing replication sessions.
schedule_set_maximum_backlog() - sets the maximum number of updates
  which can occur before replication will commence. If the backlog
  threshhold is exceeded, then replication will commence ASAP,
  overriding the startup delay.

*/

/* ONREPL - I made a simplifying assumption that a schedule item does not
   cross day boundaries. Implementing this is hard because we search
   for the items for a particular day only based on the item's staring time.
   For instance if the current time is tuesday morning, we would not consider
   the item that started on monday and continued through tuesday.
   To simulate an item that crosses day boundaries, you can create 2 items - 
   one for the time in the first day and one for the time in the second. 
   We could do this internally by allowing items do span 2 days and
   splitting them ourselves. This, however, is not currently implemented */    

#include "slapi-plugin.h"
#include "repl5.h"

#include <ctype.h> /* For isdigit() */

/* from proto-slap.h */
char *get_timestring(time_t *t);
void free_timestring(char *timestr);

typedef struct schedule_item {
	struct schedule_item *next;
	PRUint32 start; /* Start time, given as seconds after midnight */
	PRUint32 end;  /* End time */
	unsigned char dow; /* Days of week, LSB = Sunday */
} schedule_item;

typedef struct schedule {
	const char *session_id;
	size_t max_backlog;
	size_t startup_delay;
	schedule_item *schedule_list; /* Linked list of schedule windows */
	char **prio_attrs; /* Priority attributes - start replication now */
	int prio_attrs_override_schedule;
	PRTime last_session_end;
	int last_session_status;
	PRTime last_successful_session_end;
    window_state_change_callback callback_fn; /* function to call when window opens/closes */
    void *callback_arg;                       /* argument to pass to the window state change callback */
    Slapi_Eq_Context pending_event;           /* event scheduled with the event queue */
	PRLock *lock;
} schedule;

/* Forward declarations */
static schedule_item *parse_schedule_value(const Slapi_Value *v);
static void schedule_window_state_change_event (Schedule *sch);
static void unschedule_window_state_change_event (Schedule *sch);
static void window_state_changed (time_t when, void *arg);
static int schedule_in_window_now_nolock(Schedule *sch);
static time_t PRTime2time_t (PRTime tm);
static PRTime schedule_next_nolock (Schedule *sch, PRBool start);
static void free_schedule_list(schedule_item **schedule_list);

#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR (60 * SECONDS_PER_MINUTE)
#define SECONDS_PER_DAY (24 * SECONDS_PER_HOUR)
#define DAYS_PER_WEEK   7
#define ALL_DAYS 0x7F /* Bit mask */



/*
 * Create a new schedule object and return a pointer to it.
 */
Schedule*
schedule_new(window_state_change_callback callback_fn, void *callback_arg, const char *session_id)
{
	Schedule *sch = NULL;
	sch = (Schedule *)slapi_ch_calloc(1, sizeof(struct schedule));

	sch->session_id = session_id ? session_id : "";
    sch->callback_fn  = callback_fn;
    sch->callback_arg = callback_arg; 
    
	if ((sch->lock = PR_NewLock()) == NULL)
	{
		slapi_ch_free((void **)&sch);        
	}
    
	return sch;
}


void
schedule_destroy(Schedule *s)
{
	int i;

    /* unschedule update window event if exists */
    unschedule_window_state_change_event (s);

    if (s->schedule_list)
    {
        free_schedule_list (&s->schedule_list);
    }
    
	if (NULL != s->prio_attrs)
	{
		for (i = 0; NULL != s->prio_attrs[i]; i++)
		{
			slapi_ch_free((void **)&(s->prio_attrs[i]));
		}
		slapi_ch_free((void **)&(s->prio_attrs));
	}
	PR_DestroyLock(s->lock);
	s->lock = NULL;
	slapi_ch_free((void **)&s);
}

static void
free_schedule_list(schedule_item **schedule_list)
{
	schedule_item *si = *schedule_list;
	schedule_item *tmp_si;
	while (NULL != si)
	{
		tmp_si = si->next;
		slapi_ch_free((void **)&si);
		si = tmp_si;
	}
	*schedule_list = NULL;
}



/* 
 * Sets the schedule.  Returns 0 if all of the schedule lines were
 * correctly parsed and the new schedule has been put into effect.
 * Returns -1 if one or more of the schedule items could not be
 * parsed. If -1 is returned, then no changes have been made to the
 * current schedule.
 */
int
schedule_set(Schedule *sch, Slapi_Attr *attr)
{
	int return_value;
	schedule_item *si = NULL;
	schedule_item *new_schedule_list = NULL;
	int valid = 1;
	
	if (NULL != attr)
	{
		int ind;
		Slapi_Value *sval;
		ind = slapi_attr_first_value(attr, &sval);
		while (ind >= 0)
		{
			si = parse_schedule_value(sval);
			if (NULL == si)
			{
				valid = 0;
				break;
			}
			/* Put at head of linked list */
			si->next =  new_schedule_list;
			new_schedule_list = si;
			ind = slapi_attr_next_value(attr, ind, &sval);
		}
	}

	if (!valid)
	{
		/* deallocate any new schedule items */
		free_schedule_list(&new_schedule_list);
		return_value = -1;
	}
	else
	{
		PR_Lock(sch->lock);

        /* if there is an update window event scheduled - unschedule it */
        unschedule_window_state_change_event (sch);

		free_schedule_list(&sch->schedule_list);
		sch->schedule_list = new_schedule_list;

        /* schedule an event to notify the caller about openning/closing of the update window */
        schedule_window_state_change_event (sch);

		PR_Unlock(sch->lock);
		return_value = 0;
	}
	return return_value;
}



/*
 * Returns the schedule.
 */
char **
schedule_get(Schedule *sch)
{
	char **return_value = NULL;

	return return_value;
}



/*
 * Return an integer corresponding to the day of the week for
 * "when".
 */
static PRInt32
day_of_week(PRTime when)
{

	PRExplodedTime exp;

	PR_ExplodeTime(when, PR_LocalTimeParameters, &exp);
	return(exp.tm_wday);
}


/*
 * Return the number of seconds between "when" and the 
 * most recent midnight.
 */
static PRUint32
seconds_since_midnight(PRTime when)
{
	PRExplodedTime exp;

	PR_ExplodeTime(when, PR_LocalTimeParameters, &exp);
	return(exp.tm_hour * 3600 + exp.tm_min * 60 + exp.tm_sec);
}


/*
 * Return 1 if "now" is within the schedule window
 * specified by "si", 0 otherwise.
 */
static int
time_in_window(PRTime now, schedule_item *si)
{
	unsigned char dow = 1 << day_of_week(now);
	int return_value = 0;

	if (dow & si->dow)
	{
        PRUint32 nowsec = seconds_since_midnight(now);       

		return_value = (nowsec >= si->start) && (nowsec <= si->end);
	}

	return return_value;
}



/* 
 * Returns a non-zero value if the current time is within a
 * replication window and if scheduling constraints are all met.
 * Otherwise, returns zero.
 */

int
schedule_in_window_now (Schedule *sch)
{
    int rc;

    PR_ASSERT(NULL != sch);
	PR_Lock(sch->lock);

    rc = schedule_in_window_now_nolock(sch);

    PR_Unlock(sch->lock);

    return rc;
}

/* Must be called under sch->lock */
static int
schedule_in_window_now_nolock(Schedule *sch)
{
	int return_value = 0;
	
	if (NULL == sch->schedule_list)
	{
		/* Absence of a schedule is the same as 0000-2359 0123456 */
		return_value = 1;
	}
	else
	{
		schedule_item *si = sch->schedule_list;
		PRTime now;
		now = PR_Now();
		while (NULL != si)
		{
			if (time_in_window(now, si))
			{
				/* XXX check backoff timers??? */
				return_value = 1;
				break;
			}
			si = si->next;
		}
	}

	return return_value;
}



/*
 * Calculate the next time (expressed as a PRTime) when this
 * schedule item will change state (from open to close or vice versa).
 */
static PRTime
next_change_time(schedule_item *si, PRTime now, PRBool start)
{
	PRUint32 nowsec = seconds_since_midnight(now);
	PRUint32 sec_til_change;
    PRUint32 change_time;
	PRExplodedTime exp;
    PRInt32 dow = day_of_week(now);
    unsigned char dow_bit = 1 << dow;
    unsigned char next_dow;

    if (start) /* we are looking for the next window opening */
    {
        change_time = si->start;
    }
    else /* we are looking for the next window closing */
    {
        /* open range is inclusive - so we need to add a minute if we are looking for close time */
        change_time = si->end + SECONDS_PER_MINUTE;
    }

    /* we are replicating today and next change is also today */
    if ((dow_bit & si->dow) && (nowsec < change_time))
    {
        sec_til_change = change_time - nowsec;
	}
	else    /* not replicating today or the change already occured today */
	{
        int i;

        /* find next day when we replicate */
        for (i = 1; i <= DAYS_PER_WEEK; i++)
        {
            next_dow = 1 << ((dow + i) % DAYS_PER_WEEK);
            if (next_dow & si->dow)
                break;
        }
        
		sec_til_change = change_time + i * SECONDS_PER_DAY - nowsec;
	}
   
	PR_ExplodeTime(now, PR_LocalTimeParameters, &exp);
	exp.tm_sec += sec_til_change;

    
	PR_NormalizeTime(&exp, PR_LocalTimeParameters);
	return PR_ImplodeTime(&exp);
}

	

/*
 * Returns the next time that replication is scheduled to occur.
 * Returns 0 if there is no time in the future that replication
 * will begin (e.g. there's no schedule at all).
 */
PRTime
schedule_next(Schedule *sch)
{
	PRTime tm;

	PR_ASSERT(NULL != sch);
	PR_Lock(sch->lock);
	
	tm = schedule_next_nolock (sch, PR_TRUE);

	PR_Unlock(sch->lock);

	return tm;
}

/* Must be called under sch->lock */
static PRTime
schedule_next_nolock (Schedule *sch, PRBool start)
{

	PRTime closest_time = LL_Zero();

	if (NULL != sch->schedule_list)
	{
		schedule_item *si = sch->schedule_list;
		PRTime now = PR_Now();

		while (NULL != si)
		{
			PRTime tmp_time;
            
			/* Check if this item's change time is sooner than the others */
			tmp_time = next_change_time(si, now, start);
			if (LL_IS_ZERO(closest_time))
			{
				LL_ADD(closest_time, tmp_time, LL_Zero()); /* Really just an asignment */
			}
			else if (LL_CMP(tmp_time, <, closest_time))
			{
				LL_ADD(closest_time, tmp_time, LL_Zero()); /* Really just an asignment */
			}
		
			si = si->next;
		}
	}
	
	return closest_time;
}




/*
 * Called by the enclosing object (replsupplier) when a change within the
 * replicated area has occurred.  This allows the scheduler to update its
 * internal counters, timers, etc. Returns a non-zero value if replication
 * should commence, zero if it should not.
 */
int
schedule_notify(Schedule *sch, Slapi_PBlock *pb)
{
	int return_value = 0;

	return return_value;
}




/*
 * Provide a list of attributes which, if changed,
 * will cause replication to commence as soon as possible.  There
 * is also a flag that tells the scheduler if the update of a
 * priority attribute should cause the schedule to be overridden,
 * e.g. if the administrator wants password changes to propagate
 * even if not in a replication window.
 *
 * This function consumes "prio_attrs" and assumes management
 * of the memory.
 */
void
schedule_set_priority_attributes(Schedule *sch, char **prio_attrs, int override_schedule)
{
	PR_ASSERT(NULL != sch);
	PR_Lock(sch->lock);
	if (NULL != sch->prio_attrs)
	{
		int i;
		for (i = 0; NULL != prio_attrs[i]; i++) {
			slapi_ch_free((void **)&sch->prio_attrs[i]);
		}
		slapi_ch_free((void **)&sch->prio_attrs);
	}
	sch->prio_attrs = prio_attrs;
	sch->prio_attrs_override_schedule = override_schedule;

	PR_Unlock(sch->lock);
}





/* 
 * Set the time, in seconds, that replication will wait after a change is
 * available before propagating it.  This capability will allow multiple
 * updates to be coalesced into a single replication session.
 */
void
schedule_set_startup_delay(Schedule *sch, size_t startup_delay)
{
	PR_ASSERT(NULL != sch);
	PR_Lock(sch->lock);
	sch->startup_delay = startup_delay;
	PR_Unlock(sch->lock);
}





/*
 * Set the maximum number of pending changes allowed to accumulate 
 * before a replication session is begun.
 */
void
schedule_set_maximum_backlog(Schedule *sch, size_t max_backlog)
{
	PR_ASSERT(NULL != sch);
	PR_Lock(sch->lock);
	sch->max_backlog = max_backlog;
	PR_Unlock(sch->lock);
}





/* 
 * Notify the scheduler that a replication session completed at a certain
 * time.  There is also a status argument that says more about the session's
 * termination (normal, abnormal), which the scheduler uses in determining
 * the backoff strategy.
 */
void
schedule_notify_session(Schedule *sch, PRTime session_end_time, unsigned int status)
{
	PR_ASSERT(NULL != sch);
	PR_Lock(sch->lock);
	sch->last_session_end = session_end_time;
	sch->last_session_status = status;
	if (REPLICATION_SESSION_SUCCESS == status)
	{
		sch->last_successful_session_end = session_end_time;
	}
	PR_Unlock(sch->lock);
}

/* schedule an event that will fire the next time the update window state 
   changes from open to closed or vice versa */
static void 
schedule_window_state_change_event (Schedule *sch)
{
    time_t wakeup_time;
    PRTime tm;
    int window_opened;
    char *timestr = NULL;

    /* if we have a schedule and a callback function is registerd -
       register an event with the event queue */
    if (sch->schedule_list && sch->callback_fn)
    {
        /* ONREPL what if the window is really small and by the time we are done
           with the computation - we cross window boundary.
           I think we should put some constrains on schedule to avoid that */

        window_opened = schedule_in_window_now_nolock(sch);

        tm = schedule_next_nolock(sch, !window_opened);
        
        wakeup_time = PRTime2time_t (tm);

        /* schedule the event */
        sch->pending_event = slapi_eq_once(window_state_changed, sch, wakeup_time);        

        timestr = get_timestring(&wakeup_time);
        slapi_log_error (SLAPI_LOG_REPL, repl_plugin_name, "%s: Update window will %s at %s\n",
						sch->session_id,
                         window_opened ? "close" : "open", timestr);
        free_timestring(timestr);
        timestr = NULL;
    }
}

/* this function is called by the even queue the next time
   the window is opened or closed */
static void 
window_state_changed (time_t when, void *arg)
{
    Schedule *sch = (Schedule*)arg;
    int open;

    PR_ASSERT (sch);

    PR_Lock(sch->lock);

    open = schedule_in_window_now_nolock(sch);

    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s: Update window is now %s\n",
					sch->session_id,
                    open ? "open" : "closed");

    /* schedule next event */
    schedule_window_state_change_event (sch);
    
    /* notify the agreement */
    sch->callback_fn (sch->callback_arg, open);
    
    PR_Unlock(sch->lock);
}

/* cancel the event registered with the event queue */
static void 
unschedule_window_state_change_event (Schedule *sch)
{
    if (sch->pending_event)
    {
        slapi_eq_cancel(sch->pending_event);
        sch->pending_event = NULL;
    }
}

static time_t 
PRTime2time_t (PRTime tm)
{
    PRInt64 rt;

    PR_ASSERT (tm);
    
    LL_DIV(rt, tm, PR_USEC_PER_SEC);

    return (time_t)rt;
}

/*
 * Parse a schedule line. 
 * The format is:
 * <start>-<end> <day_of_week>
 * <start> and <end> are in 24-hour time
 * <day_of_week> is like cron(5): 0 = Sunday, 1 = Monday, etc.
 *
 * The schedule item "*" is equivalen to 0000-2359 0123456
 *
 * Returns a pointer to a schedule item on success, NULL if the
 * schedule item cannot be parsed.
 */
static schedule_item *
parse_schedule_value(const Slapi_Value *v)
{
#define RANGE_VALID(p, limit) \
		((p + 9) < limit && \
		isdigit(p[0]) && \
		isdigit(p[1]) && \
		isdigit(p[2]) && \
		isdigit(p[3]) && \
		('-' == p[4]) && \
		isdigit(p[5]) && \
		isdigit(p[6]) && \
		isdigit(p[7]) && \
		isdigit(p[8]))

	schedule_item *si = NULL;
	int valid = 0;
	const struct berval *sch_bval;

	if (NULL != v && (sch_bval = slapi_value_get_berval(v)) != NULL &&
		NULL != sch_bval && sch_bval->bv_len > 0 && NULL != sch_bval->bv_val )
	{
		char *p = sch_bval->bv_val;
		char *limit = p + sch_bval->bv_len;

		si = (schedule_item *)slapi_ch_malloc(sizeof(schedule_item));
		si->next = NULL;
		si->start = 0UL;
		si->end = SECONDS_PER_DAY;
		si->dow = ALL_DAYS;

		if (*p == '*')
		{
			valid = 1;
			goto done;
		}
		else
		{
			if (RANGE_VALID(p, limit))
			{
				si->start = ((strntoul(p, 2, 10) * 60) +
						strntoul(p + 2, 2, 10)) * 60;
				p += 5;
				si->end = ((strntoul(p, 2, 10) * 60) +
						strntoul(p + 2, 2, 10)) * 60;
                p += 4;

                /* ONREPL - for  now wi don't allow items that span multiple days.
                   See note in the beginning of the file for more details. */
                /* ONREPL - we should also decide on the minimum of the item size */
                if (si->start > si->end)
                {
                    valid = 0;
                    goto done;
                }

				if (p < limit &&  ' ' == *p)
				{
					/* Specific days of week */
					si->dow = 0;
					while (++p < limit)
					{
						if (!isdigit(*p))
						{
							valid = 0;
							goto done;
						}
						si->dow |= (1 << strntoul(p, 1, 10));

					}
				}
				valid = 1;
			}
		}
	}

done:
	if (!valid)
	{
		slapi_ch_free((void **)&si);
	}
	return si;
}
