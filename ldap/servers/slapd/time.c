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

/* time.c - various time routines */

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>

#include "slap.h"
#include "fe.h"

unsigned long strntoul(char *from, size_t len, int base);
#define mktime_r(from) mktime(from) /* possible bug: is this thread-safe? */

char *
get_timestring(time_t *t)
{
    char *timebuf;

    if ((timebuf = slapi_ch_malloc(32)) == NULL)
        return ("No memory for get_timestring");
    CTIME(t, timebuf, 32);
    timebuf[strlen(timebuf) - 1] = '\0'; /* strip out return */
    return (timebuf);
}

void
free_timestring(char *timestr)
{
    if (timestr != NULL) {
        slapi_ch_free((void **)&timestr);
    }
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
 *
 * WARNING: THIS IS NOT THREADSAFE.
 */
time_t
poll_current_time()
{
    return 0;
}

/*
 * Check if the time function returns an error.  If so return the errno
 */
int32_t
slapi_clock_gettime(struct timespec *tp)
{
    int32_t rc = 0;

    PR_ASSERT(tp && tp->tv_nsec == 0 && tp->tv_sec == 0);

    if (clock_gettime(CLOCK_REALTIME, tp) != 0) {
        rc = errno;
    }

    PR_ASSERT(rc == 0);

    return rc;
}

time_t
current_time(void)
{
    /*
     * For now wrap UTC time, but this interface
     * but this should be removed in favour of the
     * more accurately named slapi_current_utc_time
     */
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec;
}

time_t
slapi_current_time(void)
{
    return slapi_current_utc_time();
}

struct timespec
slapi_current_rel_time_hr(void)
{
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
}

struct timespec
slapi_current_utc_time_hr(void)
{
    struct timespec ltnow = {0};
    clock_gettime(CLOCK_REALTIME, &ltnow);
    return ltnow;
}

time_t
slapi_current_utc_time(void)
{
    struct timespec ltnow = {0};
    clock_gettime(CLOCK_REALTIME, &ltnow);
    return ltnow.tv_sec;
}

void
slapi_timestamp_utc_hr(char *buf, size_t bufsize)
{
    PR_ASSERT(bufsize >= SLAPI_TIMESTAMP_BUFSIZE);
    struct timespec ltnow = {0};
    struct tm utctm = {0};
    clock_gettime(CLOCK_REALTIME, &ltnow);
    gmtime_r(&(ltnow.tv_sec), &utctm);
    strftime(buf, bufsize, "%Y%m%d%H%M%SZ", &utctm);
}

time_t
time_plus_sec(time_t l, long r)
/* return the point in time 'r' seconds after 'l'. */
{
    PR_ASSERT(r >= 0);
    return l + (time_t)r;
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

    long tz;
    struct tm *tmsp, tms = {0};
    char tbuf[*bufsize];
    char sign;
    /* make sure our buffer will be big enough. Need at least 29 */
    if (*bufsize < 29) {
        return 1;
    }
    /* nope... painstakingly create the new strftime buffer */
    (void)localtime_r(&t, &tms);
    tmsp = &tms;

#ifdef BSD_TIME
    tz = tmsp->tm_gmtoff;
#else  /* BSD_TIME */
    tz = -timezone;
    if (tmsp->tm_isdst) {
        tz += 3600;
    }
#endif /* BSD_TIME */
    sign = (tz >= 0 ? '+' : '-');
    if (tz < 0) {
        tz = -tz;
    }
    if (strftime(tbuf, (size_t)*bufsize, "%d/%b/%Y:%H:%M:%S", tmsp) == 0) {
        return 1;
    }
    if (PR_snprintf(buf, *bufsize, "[%s %c%02d%02d] ", tbuf, sign,
                    (int)(tz / 3600), (int)(tz % 3600)) == (PRUint32)-1) {
        return 1;
    }
    *bufsize = strlen(buf);
    return 0;
}

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

    long tz;
    struct tm *tmsp, tms = {0};
    char tbuf[*bufsize];
    char sign;
    /* make sure our buffer will be big enough. Need at least 39 */
    if (*bufsize < 39) {
        /* Should this set the buffer to be something? */
        return 1;
    }
    (void)localtime_r(&t, &tms);
    tmsp = &tms;

#ifdef BSD_TIME
    tz = tmsp->tm_gmtoff;
#else  /* BSD_TIME */
    tz = -timezone;
    if (tmsp->tm_isdst) {
        tz += 3600;
    }
#endif /* BSD_TIME */
    sign = (tz >= 0 ? '+' : '-');
    if (tz < 0) {
        tz = -tz;
    }
    if (strftime(tbuf, (size_t)*bufsize, "%d/%b/%Y:%H:%M:%S", tmsp) == 0) {
        return 1;
    }
    if (PR_snprintf(buf, *bufsize, "[%s.%09ld %c%02d%02d] ", tbuf, nsec, sign,
                    (int)(tz / 3600), (int)(tz % 3600)) == (PRUint32)-1) {
        return 1;
    }
    *bufsize = strlen(buf);
    return 0;
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
        /* And set nsec to to a whole value
         * nsec is negative => nsec = 1s - abs(nsec)
         */
        nsec = 1000000000 + nsec;
    }

    diff->tv_sec = sec;
    diff->tv_nsec = nsec;
}

void
slapi_timespec_expire_at(time_t timeout, struct timespec *expire)
{
    if (timeout <= 0) {
        expire->tv_sec = 0;
        expire->tv_nsec = 0;
    } else {
        clock_gettime(CLOCK_MONOTONIC, expire);
        expire->tv_sec += timeout;
    }
}

void
slapi_timespec_expire_rel(time_t timeout, struct timespec *start, struct timespec *expire)
{
    if (timeout <= 0) {
        expire->tv_sec = 0;
        expire->tv_nsec = 0;
    } else {
        expire->tv_sec = start->tv_sec + timeout;
        expire->tv_nsec = start->tv_nsec;
    }
}

slapi_timer_result
slapi_timespec_expire_check(struct timespec *expire)
{
    /*
     * Check this first, as it makes no timeout virutally free.
     */
    if (expire->tv_sec == 0 && expire->tv_nsec == 0) {
        return TIMER_CONTINUE;
    }
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > expire->tv_sec ||
        (expire->tv_sec == now.tv_sec && now.tv_sec > expire->tv_nsec)) {
        return TIMER_EXPIRED;
    }
    return TIMER_CONTINUE;
}

char *
format_localTime(time_t from)
/* return a newly-allocated string containing the given time, expressed
       in the syntax of a generalizedTime, except without the time zone. */
{
    char *into;
    struct tm t = {0};

    localtime_r(&from, &t);

    into = slapi_ch_smprintf("%.4li%.2i%.2i%.2i%.2i%.2i",
                             1900L + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return into;
}

time_t
read_localTime(struct berval *from)
/* the inverse of write_localTime */
{
    return (read_genTime(from));
}


time_t
parse_localTime(char *from)
/* the inverse of format_localTime */
{
    return (parse_genTime(from));
}

void
write_localTime(time_t from, struct berval *into)
/* like format_localTime, except it returns a berval */
{
    into->bv_val = format_localTime(from);
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
unsigned long
strntoul(char *from, size_t len, int base)
{
    unsigned long result;
    char c = from[len];

    from[len] = '\0';
    result = strtoul(from, NULL, base);
    from[len] = c;
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
format_genTime(time_t from)
/* return a newly-allocated string containing the given time, expressed
       in the syntax of a generalizedTime. */
{
    char *into;
    struct tm t = {0};

    gmtime_r(&from, &t);
    into = slapi_ch_malloc(SLAPI_TIMESTAMP_BUFSIZE);
    strftime(into, 20, "%Y%m%d%H%M%SZ", &t);
    return into;
}

void
write_genTime(time_t from, struct berval *into)
/* like format_localTime, except it returns a berval */
{
    into->bv_val = format_genTime(from);
    into->bv_len = strlen(into->bv_val);
}

time_t
read_genTime(struct berval *from)
{
    struct tm t = {0};
    time_t retTime = {0};
    time_t diffsec = 0;
    int i, gflag = 0, havesec = 0;

    t.tm_isdst = -1;
    t.tm_year = strntoul(from->bv_val, 4, 10) - 1900L;
    t.tm_mon = strntoul(from->bv_val + 4, 2, 10) - 1;
    t.tm_mday = strntoul(from->bv_val + 6, 2, 10);
    t.tm_hour = strntoul(from->bv_val + 8, 2, 10);
    t.tm_min = strntoul(from->bv_val + 10, 2, 10);
    i = 12;
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
            diffsec -= strntoul(from->bv_val + i, 4, 10);
            gflag = 1;
            i += 4;
            break;
        case '-': /* Offset from GMT is on 4 digits */
            i++;
            diffsec += strntoul(from->bv_val + i, 4, 10);
            gflag = 1;
            i += 4;
            break;
        default:
            if (havesec) {
                /* Ignore milliseconds */
                i++;
            } else {
                t.tm_sec = strntoul(from->bv_val + i, 2, 10);
                havesec = 1;
                i += 2;
            }
        } /* end switch */
    }
    if (gflag) {
        PRTime pt;
        PRExplodedTime expt = {0};
        unsigned long year = strntoul(from->bv_val, 4, 10);

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
        return mktime_r(&t);
    }
}

time_t
parse_genTime(char *from)
/* the inverse of format_genTime
 * Because read_localTime has been rewriten to take into
 * account generalizedTime, parse_time is similar to parse_localTime.
 * The new call is
 */
{
    struct berval tbv;
    tbv.bv_val = from;
    tbv.bv_len = strlen(from);

    return read_genTime(&tbv);
}

/*
 * Return Value:
 *   Success: duration in seconds
 *   Failure: -1
 */
long
parse_duration_32bit(char *value)
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
    switch (*endp) {
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
    if (*endp != '\0' || errno == ERANGE) {
        duration = -1;
        goto bail;
    }
    duration *= times;
bail:
    if (duration == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "parse_duration",
                      "Invalid duration (%s)\n", value ? value : "null");
    }
    slapi_ch_free_string(&input);
    return duration;
}

time_t
parse_duration_time_t(char *value)
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
    switch (*endp) {
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
    if (*endp != '\0' || errno == ERANGE) {
        duration = -1;
        goto bail;
    }
    duration *= times;
bail:
    if (duration == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "parse_duration_time_t",
                      "Invalid duration (%s)\n", value ? value : "null");
    }
    slapi_ch_free_string(&input);
    return duration;
}

time_t
slapi_parse_duration(const char *value)
{
    return parse_duration_time_t((char *)value);
}

long long
slapi_parse_duration_longlong(const char *value)
{
    return parse_duration_time_t((char *)value);
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
