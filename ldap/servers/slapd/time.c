/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* time.c - various time routines */

#include <stdio.h>
#include <sys/types.h>
#ifndef _WIN32
#ifdef AIX
#include <time.h>
#else
#include <sys/time.h>
#endif
#endif /* _WIN32 */

#include "slap.h"
#include "fe.h"

LDAP_API(unsigned long) strntoul( char *from, size_t len, int base );
#define mktime_r(from) mktime (from) /* possible bug: is this thread-safe? */

static time_t	currenttime;
static int	currenttime_set = 0;
/* XXX currenttime and currenttime_set are used by multiple threads,
 * concurrently (one thread sets them, many threads read them),
 * WITHOUT SYNCHRONIZATION.  If assignment to currenttime especially
 * is not atomic, current_time() will return bogus values, and
 * bogus behavior may ensue.  We think this isn't a problem, because
 * currenttime is a static variable, and defined first in this module;
 * consequently it's aligned, and doesn't cross cache lines or
 * otherwise run afoul of multiprocessor weirdness that might make
 * assignment to it non-atomic.
 */

#ifndef HAVE_TIME_R
PRLock  *time_func_mutex;

int gmtime_r(
    const time_t *timer,
    struct tm *result
)
{
    if ( result == NULL ) {
	return -1;
    }
    PR_Lock( time_func_mutex );
    memcpy( (void *) result, (const void *) gmtime( timer ),
		sizeof( struct tm ));
    PR_Unlock( time_func_mutex );
    return 0;
}

int localtime_r(
    const time_t *timer,
    struct tm *result
)
{
    if ( result == NULL ) {
	return -1;
    }
    PR_Lock( time_func_mutex );
    memcpy( (void *) result, (const void *) localtime( timer ),
		sizeof( struct tm ));
    PR_Unlock( time_func_mutex );
    return 0;
}


int ctime_r(
    const time_t *timer,
    char *buffer,
    int buflen
)
{
    if (( buffer == NULL ) || ( buflen < 26)) {
	return -1;
    }
    PR_Lock( time_func_mutex );
    memset( buffer, 0, buflen );
    memcpy( buffer, ctime( timer ), 26 );
    PR_Unlock( time_func_mutex );
    return 0;
}
#endif /* HAVE_TIME_R */


char *
get_timestring(time_t *t)
{
    char	*timebuf;

#if defined( _WIN32 )
    timebuf = ctime( t );
#else
    if ( (timebuf = slapi_ch_malloc(32)) == NULL )
	return("No memory for get_timestring");
    CTIME(t, timebuf, 32);
#endif
    timebuf[strlen(timebuf) - 1] = '\0';	/* strip out return */
    return(timebuf);
}

void
free_timestring(char *timestr)
{
#if defined( _WIN32 )
    return;
#else
    if ( timestr != NULL )
        slapi_ch_free((void**)&timestr);
#endif
}

/*
 * poll_current_time() is called at least once every second by the time
 * thread (see daemon.c:time_thread()).  current_time() returns the time
 * that poll_current_time() last stored.  This approach is used to avoid
 * calling the time() system call many times per second.
 *
 * Note: during server startup, poll_current_time() is not called at all so
 * current_time() just calls through to time() until poll_current_time() starts
 * to be called.
 */
time_t
poll_current_time()
{
    if ( !currenttime_set ) {
	currenttime_set = 1;
    }

    time( &currenttime );
    return( currenttime );
}

time_t
current_time( void )
{
    if ( currenttime_set ) {
        return( currenttime );
    } else {
        return( time( (time_t *)0 ));
    }
}

time_t
time_plus_sec (time_t l, long r)
    /* return the point in time 'r' seconds after 'l'. */
{
    /* On many (but not all) platforms this is simply l + r;
       perhaps it would be better to implement it that way. */
    struct tm t;
    if (r == 0) return l; /* performance optimization */
#ifdef _WIN32
    {
        struct tm *pt = localtime( &l );
        memcpy(&t, pt, sizeof(struct tm) );
    }
#else
    localtime_r (&l, &t);
#endif
    /* Conceptually, we want to do: t.tm_sec += r;
       but to avoid overflowing fields: */
    r += t.tm_sec;  t.tm_sec  = r % 60; r /= 60;
    r += t.tm_min;  t.tm_min  = r % 60; r /= 60;
    r += t.tm_hour; t.tm_hour = r % 24; r /= 24;
    t.tm_mday += r; /* may be > 31; mktime_r() must handle this */

    /* These constants are chosen to work when the maximum
       field values are 127 (the worst case) or more.
       Perhaps this is excessively conservative. */
    return mktime_r (&t);
}

char*
format_localTime (time_t from)
    /* return a newly-allocated string containing the given time, expressed
       in the syntax of a generalizedTime, except without the time zone. */
{
    char* into;
    struct tm t;
#ifdef _WIN32
    {
        struct tm *pt = localtime( &from );
        memcpy(&t, pt, sizeof(struct tm) );
    }
#else
    localtime_r (&from, &t);
#endif
    into = slapi_ch_malloc (15);
    sprintf (into, "%.4li%.2i%.2i%.2i%.2i%.2i",
             1900L + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.
tm_sec);
    return into;
}

time_t
read_localTime (struct berval* from)
/* the inverse of write_localTime */
{
	return (read_genTime(from));
}


time_t
parse_localTime (char* from)
    /* the inverse of format_localTime */
{
	return (parse_genTime(from));
}

void
write_localTime (time_t from, struct berval* into)
    /* like format_localTime, except it returns a berval */
{
    into->bv_val = format_localTime (from);
    into->bv_len = 14; /* strlen (into->bv_val) */
}

/*
 * Function: strntoul
 *
 * Returns: the value of "from", converted to an unsigned long integer.
 *
 * Arguments: from - a pointer to the string to be converted.
 *            len  - the maximum number of characters to process.
 *            base - the base to use for conversion.  See strtoul(3).
 *
 * Returns:   See strtoul(3).
 */
LDAP_API(unsigned long) strntoul( char *from, size_t len, int base )
{
    unsigned long       result;
    char                c = from[ len ];

    from[ len ] = '\0';
    result = strtoul( from, NULL, base );
    from[ len ] = c;
    return result;
}

/*
 * New time functions.
 * The format and write functions, express time as 
 * generalizedTime (in the Z form).
 * The parse and read functions, can read either 
 * localTime or generalizedTime.
 */

char * 
format_genTime (time_t from)
    /* return a newly-allocated string containing the given time, expressed
       in the syntax of a generalizedTime. */
{
    char* into;
    struct tm t;
#ifdef _WIN32
    {
        struct tm *pt = gmtime( &from );
        memcpy(&t, pt, sizeof(struct tm) );
    }
#else
    gmtime_r (&from, &t);
#endif
    into = slapi_ch_malloc (20);
    strftime(into, 20, "%Y%m%d%H%M%SZ", &t);
    return into;
}

void
write_genTime (time_t from, struct berval* into)
    /* like format_localTime, except it returns a berval */
{
    into->bv_val = format_genTime (from);
    into->bv_len = strlen (into->bv_val);
}

time_t
read_genTime(struct berval*from)
{
	struct tm t;
	time_t retTime;
	time_t diffsec = 0;
	int i, gflag = 0, havesec = 0;
	
    memset (&t, 0, sizeof(t));
    t.tm_isdst = -1;
    t.tm_year = strntoul (from->bv_val     , 4, 10) - 1900L;
    t.tm_mon  = strntoul (from->bv_val +  4, 2, 10) - 1;
    t.tm_mday = strntoul (from->bv_val +  6, 2, 10);
    t.tm_hour = strntoul (from->bv_val +  8, 2, 10);
    t.tm_min  = strntoul (from->bv_val + 10, 2, 10);
	i =12;
	/*
	 * the string can end with Z or -xxxx or +xxxx
	 * or before to have the Z/+/- we may have two digits for the seconds.
	 * If there's no Z/+/-, it means it's been expressed as local time 
	 * (not standard).
	 */
	while (from->bv_val[i]) {
        switch (from->bv_val[i]) {
		case 'Z':
		case 'z':
			gflag = 1;
			++i;
			break;
		case '+': /* Offset from GMT is on 4 digits */
			i++;
			diffsec -= strntoul (from->bv_val + i, 4, 10);
			gflag = 1;
			i += 4;
			break;
		case '-': /* Offset from GMT is on 4 digits */
			i++;
			diffsec += strntoul (from->bv_val + i, 4, 10);
			gflag = 1;
			i += 4;
			break;
		default:
			if (havesec){
				/* Ignore milliseconds */
				i++;
			} else {
				t.tm_sec = strntoul (from->bv_val + i, 2, 10);
				havesec = 1;
				i += 2;
			}
        } /* end switch */
    }
	if (gflag){
		PRTime pt;
		PRExplodedTime expt = {0};
		unsigned long year = strntoul (from->bv_val , 4, 10);

		expt.tm_year = (PRInt16)year;
		expt.tm_month = t.tm_mon;
		expt.tm_mday = t.tm_mday;
		expt.tm_hour = t.tm_hour;
		expt.tm_min = t.tm_min;
		expt.tm_sec = t.tm_sec;
		/* This is a GMT time */
		expt.tm_params.tp_gmt_offset = 0;
		expt.tm_params.tp_dst_offset = 0;
		/* PRTime is expressed in microseconds */
		pt = PR_ImplodeTime(&expt) / 1000000L;
		retTime = (time_t)pt;
		return (retTime + diffsec);
	} else {
		return mktime_r (&t);
	}
}

time_t
parse_genTime (char* from)
/* the inverse of format_genTime
 * Because read_localTime has been rewriten to take into 
 * account generalizedTime, parse_time is similar to parse_localTime.
 * The new call is
 */
{
    struct berval tbv;
    tbv.bv_val = from;
    tbv.bv_len = strlen (from);

	return read_genTime(&tbv);
}
