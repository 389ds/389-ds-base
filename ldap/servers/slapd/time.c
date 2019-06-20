/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* time.c - various time routines */

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>

#include "slap.h"
#include "fe.h"

unsigned long strntoul( char *from, size_t len, int base );
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

    if ( (timebuf = slapi_ch_malloc(32)) == NULL )
        return("No memory for get_timestring");
    CTIME(t, timebuf, 32);
    timebuf[strlen(timebuf) - 1] = '\0';	/* strip out return */
    return(timebuf);
}

void
free_timestring(char *timestr)
{
    if ( timestr != NULL )
        slapi_ch_free((void**)&timestr);
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
slapi_current_time( void )
{
    return current_time();
}

struct timespec
slapi_current_rel_time_hr(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
}

void
slapi_timespec_diff(struct timespec *a, struct timespec *b, struct timespec *diff)
{
    /* Now diff the two */
    time_t sec = a->tv_sec - b->tv_sec;
    int32_t nsec = a->tv_nsec - b->tv_nsec;

    if (nsec < 0) {
        /* It's negative so take one second */
        sec -= 1;
        /* And set nsec to to a whole value */
        nsec = 1000000000 - nsec;
    }

    diff->tv_sec = sec;
    diff->tv_nsec = nsec;
}

time_t
time_plus_sec (time_t l, long r)
    /* return the point in time 'r' seconds after 'l'. */
{
    /* On many (but not all) platforms this is simply l + r;
       perhaps it would be better to implement it that way. */
    struct tm t;
    if (r == 0) return l; /* performance optimization */
    localtime_r (&l, &t);
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


/*
 * format_localTime_log will take a time value, and prepare it for
 * log printing.
 *
 * \param time_t t - the time to convert
 * \param int initsize - the initial buffer size
 * \param char *buf - The destitation string
 * \param int *bufsize - The size of the resulting buffer
 *
 * \return int success - 0 on correct format, >= 1 on error.
 */
int
format_localTime_log(time_t t, int initsize __attribute__((unused)), char *buf, int *bufsize)
{
    
    long	tz;
    struct tm	*tmsp, tms;
    char	tbuf[ *bufsize ];
    char	sign;
    /* make sure our buffer will be big enough. Need at least 29 */
    if (*bufsize < 29) {
        return 1;
    }
    /* nope... painstakingly create the new strftime buffer */
    (void)localtime_r( &t, &tms );
    tmsp = &tms;

#ifdef BSD_TIME
    tz = tmsp->tm_gmtoff;
#else /* BSD_TIME */
    tz = - timezone;
    if ( tmsp->tm_isdst ) {
        tz += 3600;
    }
#endif /* BSD_TIME */
    sign = ( tz >= 0 ? '+' : '-' );
    if ( tz < 0 ) {
        tz = -tz;
    }
    if (strftime( tbuf, (size_t)*bufsize, "%d/%b/%Y:%H:%M:%S", tmsp) == 0) {
        return 1;
    }
    if (PR_snprintf( buf, *bufsize, "[%s %c%02d%02d] ", tbuf, sign,
            (int)( tz / 3600 ), (int)( tz % 3600)) == (PRUint32)-1) {
        return 1;
    }
    *bufsize = strlen(buf);
    return 0;
}

#ifdef HAVE_CLOCK_GETTIME
/*
 * format_localTime_hr_log will take a time value, and prepare it for
 * log printing.
 *
 * \param time_t t - the time to convert
 * \param long nsec - the nanoseconds elapsed in the current second.
 * \param int initsize - the initial buffer size
 * \param char *buf - The destitation string
 * \param int *bufsize - The size of the resulting buffer
 *
 * \return int success - 0 on correct format, >= 1 on error.
 */
int
format_localTime_hr_log(time_t t, long nsec, int initsize __attribute__((unused)), char *buf, int *bufsize)
{
    
    long	tz;
    struct tm	*tmsp, tms;
    char	tbuf[ *bufsize ];
    char	sign;
    /* make sure our buffer will be big enough. Need at least 39 */
    if (*bufsize < 39) {
        /* Should this set the buffer to be something? */
        return 1;
    }
    (void)localtime_r( &t, &tms );
    tmsp = &tms;

#ifdef BSD_TIME
    tz = tmsp->tm_gmtoff;
#else /* BSD_TIME */
    tz = - timezone;
    if ( tmsp->tm_isdst ) {
        tz += 3600;
    }
#endif /* BSD_TIME */
    sign = ( tz >= 0 ? '+' : '-' );
    if ( tz < 0 ) {
        tz = -tz;
    }
    if (strftime( tbuf, (size_t)*bufsize, "%d/%b/%Y:%H:%M:%S", tmsp) == 0) {
        return 1;
    }
    if (PR_snprintf( buf, *bufsize, "[%s.%09ld %c%02d%02d] ", tbuf, nsec, sign,
            (int)( tz / 3600 ), (int)( tz % 3600)) == (PRUint32)-1) {
        return 1;
    }
    *bufsize = strlen(buf);
    return 0;
}
#endif /* HAVE_CLOCK_GETTIME */

char*
format_localTime (time_t from)
    /* return a newly-allocated string containing the given time, expressed
       in the syntax of a generalizedTime, except without the time zone. */
{
    char* into;
    struct tm t;

    localtime_r (&from, &t);

    into = slapi_ch_smprintf("%.4li%.2i%.2i%.2i%.2i%.2i",
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
unsigned long strntoul( char *from, size_t len, int base )
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

    gmtime_r (&from, &t);
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
	struct tm t = {0};
	time_t retTime;
	time_t diffsec = 0;
	int i, gflag = 0, havesec = 0;
	
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

/*
 * Return Value:
 *   Success: duration in seconds
 *   Failure: -1
 */
long
parse_duration(char *value)
{
    char *input = NULL;
    char *endp;
    long duration = -1;
    int times = 1;

    if (NULL == value || '\0' == *value) {
        goto bail;
    }
    input = slapi_ch_strdup(value);
    endp = input + strlen(input) - 1;
    while ((' ' == *endp || '\t' == *endp) && endp > input) {
        endp--;
    }
    if ((endp == input) && !isdigit(*input)) {
        goto bail;
    }
    switch ( *endp ) {
    case 'w':
    case 'W':
      times = 60 * 60 * 24 * 7;
      *endp = '\0';
      break;
    case 'd':
    case 'D':
      times = 60 * 60 * 24;
      *endp = '\0';
      break;
    case 'h':
    case 'H':
      times = 60 * 60;
      *endp = '\0';
      break;
    case 'm':
    case 'M':
      times = 60;
      *endp = '\0';
      break;
    case 's':
    case 'S':
      times = 1;
      *endp = '\0';
      break;
    default:
      if (isdigit(*endp)) {
        times = 1;
        break;
      } else {
        goto bail;
      }
    }
    duration = strtol(input, &endp, 10);
    if ( *endp != '\0' || errno == ERANGE ) {
        duration = -1;
        goto bail;
    }
    duration *= times;
bail:
    if (duration == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "parse_duration",
                "Invalid duration (%s)\n", value?value:"null");
    }
    slapi_ch_free_string(&input);
    return duration;
}

long long
parse_duration_longlong(char *value)
{
    char *input = NULL;
    char *endp;
    long long duration = -1;
    int times = 1;

    if (NULL == value || '\0' == *value) {
        goto bail;
    }
    input = slapi_ch_strdup(value);
    endp = input + strlen(input) - 1;
    while ((' ' == *endp || '\t' == *endp) && endp > input) {
        endp--;
    }
    if ((endp == input) && !isdigit(*input)) {
        goto bail;
    }
    switch ( *endp ) {
    case 'w':
    case 'W':
      times = 60 * 60 * 24 * 7;
      *endp = '\0';
      break;
    case 'd':
    case 'D':
      times = 60 * 60 * 24;
      *endp = '\0';
      break;
    case 'h':
    case 'H':
      times = 60 * 60;
      *endp = '\0';
      break;
    case 'm':
    case 'M':
      times = 60;
      *endp = '\0';
      break;
    case 's':
    case 'S':
      times = 1;
      *endp = '\0';
      break;
    default:
      if (isdigit(*endp)) {
        times = 1;
        break;
      } else {
        goto bail;
      }
    }
    duration = strtoll(input, &endp, 10);
    if ( *endp != '\0' || errno == ERANGE ) {
        duration = -1;
        goto bail;
    }
    duration *= times;
bail:
    if (duration == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "parse_duration_longlong",
                "Invalid duration (%s)\n", value?value:"null");
    }
    slapi_ch_free_string(&input);
    return duration;
}

time_t
slapi_parse_duration(const char *value)
{
    return (time_t)parse_duration((char *)value);
}

long long
slapi_parse_duration_longlong(const char *value)
{
    return parse_duration_longlong((char *)value);
}

static int
is_valid_duration_unit(const char value)
{
    int rc = 0;
    switch (value) {
    case 'w':
    case 'W':
    case 'd':
    case 'D':
    case 'h':
    case 'H':
    case 'm':
    case 'M':
    case 's':
    case 'S':
      rc = 1;
      break;
    }
    return rc;
}

int
slapi_is_duration_valid(const char *value)
{
    int rc = 1; /* valid */
    const char *p = value;
    if (p && *p && isdigit(*p)) { /* 1st character must be digit */
        for (++p; p && *p; p++) {
            if (!isdigit(*p) && !is_valid_duration_unit(*p)) {
                rc = 0;
                goto bail;
            }
        }
    } else {
        rc = 0;
    }
bail:
    return rc;
}
                                       
/*
 * caller is responsible to free the returned string
 */
char *
gen_duration(long duration)
{
    char *duration_str = NULL;
    long remainder = 0;
    long devided = duration;
    int devider[] = {60, 60, 24, 0};
    char *unit[] = {"", "M", "H", "D", NULL};
    int i = 0;

    if (0 > duration) {
        goto bail;
    } else if (0 == duration) {
        duration_str = strdup("0");
        goto bail;
    }
    do { 
        remainder = devided % devider[i];
        if (remainder) {
            break;
        }
        devided /= devider[i++];
    } while (devider[i]);

    duration_str = slapi_ch_smprintf("%ld%s", devided, unit[i]);

bail:
    return duration_str;
}
