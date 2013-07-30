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
 * util.c: A hodge podge of utility functions and standard functions which 
 *         are unavailable on certain systems
 * 
 * Rob McCool
 */

#ifdef XP_UNIX
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "prthread.h"
#endif /* XP_UNIX */

#include "base/util.h"

#ifdef XP_UNIX
#include <sys/types.h>
#endif /* WIN32 */

/* ------------------------------ util_itoa ------------------------------- */


/* 
 * Assumption: Reversing the digits will be faster in the general case 
 * than doing a log10 or some nasty trick to find the # of digits.
 */

NSAPI_PUBLIC int util_itoa(int i, char *a)
{
    register int x, y, p;
    register char c;
    int negative;

    negative = 0;
    if(i < 0) {
        *a++ = '-';
        negative = 1;
        i = -i;
    }
    p = 0;
    while(i > 9) {
        a[p++] = (i%10) + '0';
        i /= 10;
    }
    a[p++] = i + '0';

    if(p > 1) {
        for(x = 0, y = p - 1; x < y; ++x, --y) {
            c = a[x];
            a[x] = a[y];
            a[y] = c;
        }
    }
    a[p] = '\0';
    return p + negative;
}


/* ----------------------------- util_sprintf ----------------------------- */


#include "prprf.h"

/* 
   XXXrobm the NSPR interfaces don't allow me to just pass in a buffer 
   without a size
 */
#define UTIL_PRF_MAXSIZE 1048576

NSAPI_PUBLIC int util_vsnprintf(char *s, int n, register const char *fmt, 
                                va_list args)
{
    return PR_vsnprintf(s, n, fmt, args);
}

NSAPI_PUBLIC int util_snprintf(char *s, int n, const char *fmt, ...)
{
    int rc;
    va_list args;
    va_start(args, fmt);
    rc = PR_vsnprintf(s, n, fmt, args);
    va_end(args);
    return rc;
}

NSAPI_PUBLIC int util_vsprintf(char *s, register const char *fmt, va_list args)
{
    return PR_vsnprintf(s, UTIL_PRF_MAXSIZE, fmt, args);
}

NSAPI_PUBLIC int util_sprintf(char *s, const char *fmt, ...)
{
    int rc;
    va_list args;
    va_start(args, fmt);
    rc = PR_vsnprintf(s, UTIL_PRF_MAXSIZE, fmt, args);
    va_end(args);
    return rc;
}

/* --------------------------- util_strcasecmp ---------------------------- */


#ifdef NEED_STRCASECMP
/* These are stolen from mcom/lib/xp */
NSAPI_PUBLIC 
int util_strcasecmp(CASECMPARG_T char *one, CASECMPARG_T char *two)
{
    CASECMPARG_T char *pA;
    CASECMPARG_T char *pB;

    for(pA=one, pB=two; *pA && *pB; pA++, pB++)
      {
        int tmp = tolower(*pA) - tolower(*pB);
        if (tmp)
            return tmp;
      }
    if (*pA)
        return 1;
    if (*pB)
        return -1;
    return 0;
}
#endif /* NEED_STRCASECMP */

#ifdef NEED_STRNCASECMP
NSAPI_PUBLIC 
int util_strncasecmp(CASECMPARG_T char *one, CASECMPARG_T char *two, int n)
{
    CASECMPARG_T char *pA;
    CASECMPARG_T char *pB;

    for(pA=one, pB=two;; pA++, pB++)
      {
        int tmp;
        if (pA == one+n)
            return 0;
        if (!(*pA && *pB))
            return *pA - *pB;
        tmp = tolower(*pA) - tolower(*pB);
        if (tmp)
            return tmp;
      }
}
#endif /* NEED_STRNCASECMP */

/* ------------------------------ util_strftime --------------------------- */
/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)strftime.c	5.11 (Berkeley) 2/24/91";
#endif /* LIBC_SCCS and not lint */

#ifdef XP_UNIX
#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#endif

static const char *afmt[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
static const char *Afmt[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
	"Saturday",
};

static const char *bfmt[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
	"Oct", "Nov", "Dec",
};
static const char *Bfmt[] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December",
};

#define TM_YEAR_BASE 1900

static void _util_strftime_conv(char *, int, int, char);

#define _util_strftime_add(str) for (;(*pt = *str++); pt++)
#define _util_strftime_copy(str, len) memcpy(pt, str, len); pt += len;
#define _util_strftime_fmt util_strftime

/* util_strftime()
 * This is an optimized version of strftime for speed.  Avoids the thread
 * unsafeness of BSD strftime calls. 
 */
int
util_strftime(char *pt, const char *format, const struct tm *t)
{
    char *start = pt;
	const char *scrap;

	for (; *format; ++format) {
		if (*format == '%')
			switch(*++format) {
			case 'a': /* abbreviated weekday name */
				*pt++ = afmt[t->tm_wday][0];
				*pt++ = afmt[t->tm_wday][1];
				*pt++ = afmt[t->tm_wday][2];
				continue;
			case 'd': /* day of month */
				_util_strftime_conv(pt, t->tm_mday, 2, '0');
				pt += 2;
				continue;
			case 'S':
				 _util_strftime_conv(pt, t->tm_sec, 2, '0');
				pt += 2;
				continue;
			case 'M':
				 _util_strftime_conv(pt, t->tm_min, 2, '0');
				pt += 2;
				continue;
			case 'H':
				_util_strftime_conv(pt, t->tm_hour, 2, '0');
				pt += 2;
				continue;
			case 'Y':
				if (t->tm_year < 100) {
					*pt++ = '1';
					*pt++ = '9';
					_util_strftime_conv(pt, t->tm_year, 2, '0');
				} else {
					/* will fail after 2100; but who cares? */
					*pt++ = '2';
					*pt++ = '0';
					_util_strftime_conv(pt, t->tm_year-100, 2, '0');
				}
				pt += 2;
				continue;
			case 'b': /* abbreviated month name */
			case 'h':
				*pt++ = bfmt[t->tm_mon][0];
				*pt++ = bfmt[t->tm_mon][1];
				*pt++ = bfmt[t->tm_mon][2];
				continue;
			case 'T':
			case 'X':
				pt += _util_strftime_fmt(pt, "%H:%M:%S", t);
				continue;
			case '\0':
				--format;
				break;
			case 'A':
				if (t->tm_wday < 0 || t->tm_wday > 6)
					return(0);
				scrap = Afmt[t->tm_wday];
				_util_strftime_add(scrap);
				continue;
			case 'B':
				if (t->tm_mon < 0 || t->tm_mon > 11)
					return(0);
				scrap = Bfmt[t->tm_mon];
				_util_strftime_add(scrap);
				continue;
			case 'C':
				pt += _util_strftime_fmt(pt, "%a %b %e %H:%M:%S %Y", t);
				continue;
			case 'c':
				pt += _util_strftime_fmt(pt, "%m/%d/%y %H:%M:%S", t);
				continue;
			case 'D':
				pt += _util_strftime_fmt(pt, "%m/%d/%y", t);
				continue;
			case 'e':
				_util_strftime_conv(pt, t->tm_mday, 2, ' ');
				pt += 2;
				continue;
			case 'I':
				_util_strftime_conv(pt, t->tm_hour % 12 ?
				    t->tm_hour % 12 : 12, 2, '0');
				pt += 2;
				continue;
			case 'j':
				_util_strftime_conv(pt, t->tm_yday + 1, 3, '0');
				pt += 3;
				continue;
			case 'k':
				_util_strftime_conv(pt, t->tm_hour, 2, ' ');
				pt += 2;
				continue;
			case 'l':
				 _util_strftime_conv(pt, t->tm_hour % 12 ?
				    t->tm_hour % 12 : 12, 2, ' ');
				pt += 2;
				continue;
			case 'm':
				 _util_strftime_conv(pt, t->tm_mon + 1, 2, '0');
				pt += 2;
				continue;
			case 'n':
				*pt = '\n';
				pt++;
				continue;
			case 'p':
				if (t->tm_hour >= 12) {
					*pt = 'P';
					pt++;
				} else {
					*pt = 'A';
					pt++;
				}
				*pt = 'M';
				pt++;
				continue;
			case 'R':
				pt += _util_strftime_fmt(pt, "%H:%M", t);
				continue;
			case 'r':
				pt += _util_strftime_fmt(pt, "%I:%M:%S %p", t);
				continue;
			case 't':
				*pt = '\t';
				pt++;
				continue;
			case 'U':
				 _util_strftime_conv(pt, (t->tm_yday + 7 - t->tm_wday) / 7,
				    2, '0');
				pt += 2;
				continue;
			case 'W':
				 _util_strftime_conv(pt, (t->tm_yday + 7 -
				    (t->tm_wday ? (t->tm_wday - 1) : 6))
				    / 7, 2, '0');
				pt += 2;
				continue;
			case 'w':
				 _util_strftime_conv(pt, t->tm_wday, 1, '0');
				pt += 1;
				continue;
			case 'x':
				pt += _util_strftime_fmt(pt, "%m/%d/%y", t);
				continue;
			case 'y':
				 _util_strftime_conv(pt, (t->tm_year + TM_YEAR_BASE)
				    % 100, 2, '0');
				pt += 2;
				continue;
			case '%':
			/*
			 * X311J/88-090 (4.12.3.5): if conversion char is
			 * undefined, behavior is undefined.  Print out the
			 * character itself as printf(3) does.
			 */
			default:
				break;
		}
		*pt = *format;
		pt++;
	}

    start[pt-start] = '\0';

	return pt - start;
}

static void
_util_strftime_conv(char *pt, int n, int digits, char pad)
{
	static char buf[10];
	register char *p;

	if (n >= 100) {
		p = buf + sizeof(buf)-2;
		for (; n > 0 && p > buf; n /= 10, --digits)
			*p-- = n % 10 + '0';
    	while (p > buf && digits-- > 0)
		  	*p-- = pad;
		p++;
		_util_strftime_add(p);
    } else {
		int tens;
		int ones = n;

        tens = 0;
        if ( ones >= 10 ) {
            while ( ones >= 10 ) {
                tens++;
                ones = ones - 10;
            }
            *pt++ = '0'+tens;
            digits--;
        }
		else 
			*pt++ = '0';
        *pt++ = '0'+ones;
        digits--;
		while(digits--)
			*pt++ = pad;
    }
	return;
}

/*
 * Various reentrant routines by mikep.  See util.h and systems.h
 */

/*
 * These are only necessary if we turn on interrupts in NSPR
 */
#ifdef NEED_RELOCKS
#include "crit.h"
#define RE_LOCK(name) \
    static CRITICAL name##_crit = 0; \
    if (name##_crit == 0) name##_crit = crit_init(); \
    crit_enter(name##_crit)

#define RE_UNLOCK(name)  crit_exit(name##_crit)

#else
#define RE_LOCK(name) /* nada */
#define RE_UNLOCK(name) /* nil */
#endif


NSAPI_PUBLIC struct tm *
util_localtime(const time_t *clock, struct tm *res)
{
#ifdef HAVE_TIME_R
    return localtime_r(clock, res);
#else
    struct tm *rv;
    time_t zero = 0x7fffffff;

    RE_LOCK(localtime);
    RE_UNLOCK(localtime);
    rv = localtime(clock);
    if (!rv)
        rv = localtime(&zero);
    if (rv)
        *res = *rv;
    else
        return NULL;
    return res;
#endif
}
