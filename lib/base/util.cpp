/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

#include "base/dbtbase.h"
#include "base/ereport.h"


#ifdef XP_UNIX
#include <sys/types.h>
#endif /* WIN32 */

/* ----------------------------- util_getline ----------------------------- */

#define LF 10
#define CR 13

NSAPI_PUBLIC int util_getline(filebuf_t *buf, int lineno, int maxlen, char *l) {
    int i, x;

    x = 0;
    while(1) {
	i = filebuf_getc(buf);
        switch(i) {
          case IO_EOF:
            l[x] = '\0';
            return 1;
          case LF:
            if(x && (l[x-1] == '\\')) {
                --x;
                continue;
            }
            l[x] = '\0';
            return 0;
          case IO_ERROR:
            util_sprintf(l, "I/O error reading file at line %d", lineno);
            return -1;
          case CR:
            continue;
          default:
            l[x] = (char) i;
            if(++x == maxlen) {
                util_sprintf(l, "line %d is too long", lineno);
                return -1;
            }
            break;
        }
    }
}


/* ---------------------------- util_can_exec ----------------------------- */

#ifdef XP_UNIX
NSAPI_PUBLIC int util_can_exec(struct stat *fi, uid_t uid, gid_t gid) 
{
    if(!uid)
       return 1;
    if((fi->st_mode & S_IXOTH) || 
       ((gid == fi->st_gid) && (fi->st_mode & S_IXGRP)) ||
       ((uid == fi->st_uid) && (fi->st_mode & S_IXUSR)))
        return 1;
    return 0;
}
#endif /* XP_UNIX */


/* --------------------------- util_env_create ---------------------------- */


NSAPI_PUBLIC char **util_env_create(char **env, int n, int *pos)
{
    int x;

    if(!env) {
        *pos = 0;
        return (char **) MALLOC((n + 1)*sizeof(char *));
    }
    else {
        for(x = 0; (env[x]); x++);
        env = (char **) REALLOC(env, (n + x + 1)*(sizeof(char *)));
        *pos = x;
        return env;
    }
}


/* ---------------------------- util_env_free ----------------------------- */


NSAPI_PUBLIC void util_env_free(char **env)
{
    register char **ep = env;

    for(ep = env; *ep; ep++)
        FREE(*ep);
    FREE(env);
}

/* ----------------------------- util_env_str ----------------------------- */


NSAPI_PUBLIC char *util_env_str(char *name, char *value) {
    char *t,*tp;

    t = (char *) MALLOC(strlen(name)+strlen(value)+2); /* 2: '=' and '\0' */

    for(tp=t; (*tp = *name); tp++,name++);
    for(*tp++ = '='; (*tp = *value); tp++,value++);
    return t;
}


/* --------------------------- util_env_replace --------------------------- */


NSAPI_PUBLIC void util_env_replace(char **env, char *name, char *value)
{
    int x, y, z;
    char *i;

    for(x = 0; env[x]; x++) {
        i = strchr(env[x], '=');
        *i = '\0';
        if(!strcmp(env[x], name)) {
            y = strlen(env[x]);
            z = strlen(value);

            env[x] = (char *) REALLOC(env[x], y + z + 2);
            util_sprintf(&env[x][y], "=%s", value);
            return;
        }
        *i = '=';
    }
}


/* ---------------------------- util_env_find ----------------------------- */


NSAPI_PUBLIC char *util_env_find(char **env, char *name)
{
    char *i;
    int x, r;

    for(x = 0; env[x]; x++) {
        i = strchr(env[x], '=');
        *i = '\0';
        r = !strcmp(env[x], name);
        *i = '=';
        if(r)
            return i + 1;
    }
    return NULL;
}


/* ---------------------------- util_env_copy ----------------------------- */


NSAPI_PUBLIC char **util_env_copy(char **src, char **dst)
{
    char **src_ptr;
    int src_cnt;
    int index;

    if (!src)
        return NULL;

    for (src_cnt = 0, src_ptr = src; *src_ptr; src_ptr++, src_cnt++);

    if (!src_cnt)
        return NULL;

    dst = util_env_create(dst, src_cnt, &index);

    for (src_ptr = src, index=0; *src_ptr; index++, src_ptr++)
        dst[index] = STRDUP(*src_ptr);
    dst[index] = NULL;
    
    return dst;
}

/* ---------------------------- util_hostname ----------------------------- */


/*
 * MOVED TO NET.C TO AVOID INTERDEPENDENCIES
 */


/* --------------------------- util_chdir2path ---------------------------- */


NSAPI_PUBLIC int util_chdir2path(char *path) 
{  
	/* use FILE_PATHSEP to accomodate WIN32 */
    char *t = strrchr(path, FILE_PATHSEP);
    int ret;

    if(!t)
        return -1;

    *t = '\0';
#ifdef XP_UNIX
    ret = chdir(path);
#else /* WIN32 */
	ret = SetCurrentDirectory(path);
#endif /* XP_UNIX */

	/* use FILE_PATHSEP instead of chdir to accomodate WIN32 */
    *t = FILE_PATHSEP;

    return ret;
}


/* --------------------------- util_is_mozilla ---------------------------- */


NSAPI_PUBLIC int util_is_mozilla(char *ua, char *major, char *minor)
{
    if((!ua) || strncasecmp(ua, "Mozilla/", 8))
        return 0;

    /* Major version. I punted on supporting versions like 10.0 */
    if(ua[8] > major[0])
        return 1;
    else if((ua[8] < major[0]) || (ua[9] != '.'))
        return 0;

    /* Minor version. Support version numbers like 0.96 */
    if(ua[10] < minor[0])
        return 0;
    else if((ua[10] > minor[0]) || (!minor[1]))
        return 1;

    if((!isdigit(ua[11])) || (ua[11] < minor[1]))
        return 0;
    else
        return 1;
}


/* ----------------------------- util_is_url ------------------------------ */


#include <ctype.h>     /* isalpha */

NSAPI_PUBLIC int util_is_url(char *url)
{
    char *t = url;

    while(*t) {
        if(*t == ':')
            return 1;
        if(!isalpha(*t))
            return 0;
        ++t;
    }
    return 0;
}


/* --------------------------- util_later_than ---------------------------- */


int _mstr2num(char *str) {
    if(!strcasecmp(str, "Jan")) return 0;
    if(!strcasecmp(str, "Feb")) return 1;
    if(!strcasecmp(str, "Mar")) return 2;
    if(!strcasecmp(str, "Apr")) return 3;
    if(!strcasecmp(str, "May")) return 4;
    if(!strcasecmp(str, "Jun")) return 5;
    if(!strcasecmp(str, "Jul")) return 6;
    if(!strcasecmp(str, "Aug")) return 7;
    if(!strcasecmp(str, "Sep")) return 8;
    if(!strcasecmp(str, "Oct")) return 9;
    if(!strcasecmp(str, "Nov")) return 10;
    if(!strcasecmp(str, "Dec")) return 11;
    return -1;
}

int _time_compare(struct tm *lms, char *ims, int later_than_op)
{
    int y = 0, mnum = 0, d = 0, h = 0, m = 0, s = 0, x;
    char t[128];

    /* Supported formats start with weekday (which we don't care about) */
    /* The sizeof(t) is to avoid buffer overflow with t */
    if((!(ims = strchr(ims,' '))) || (strlen(ims) > (sizeof(t) - 2)))
        return 0;

    while(*ims && isspace(*ims)) ++ims;
    if((!(*ims)) || (strlen(ims) < 2))
        return 0;

    /* Standard HTTP (RFC 850) starts with dd-mon-yy */
    if(ims[2] == '-') {
        sscanf(ims, "%s %d:%d:%d", t, &h, &m, &s);
        if(strlen(t) < 6)
            return 0;
        t[2] = '\0';
        t[6] = '\0';
        d = atoi(t);
        mnum = _mstr2num(&t[3]);
        x = atoi(&t[7]);
        /* Postpone wraparound until 2070 */
        y = x + (x < 70 ? 2000 : 1900);
    }
    /* The ctime format starts with a month name */
    else if(isalpha(*ims)) {
        sscanf(ims,"%s %d %d:%d:%d %*s %d", t, &d, &h, &m, &s, &y);
        mnum = _mstr2num(t);
    }
    /* RFC 822 */
    else {
        sscanf(ims, "%d %s %d %d:%d:%d", &d, t, &y, &h, &m, &s);
        mnum = _mstr2num(t);
    }

    if (later_than_op) {
	if( (x = (1900 + lms->tm_year) - y) )
	    return x < 0;

	if(mnum == -1)
	    return 0;

	/* XXXMB - this will fail if you check if december 31 1996 is later
	 * than january 1 1997
	 */
	if((x = lms->tm_mon - mnum) || (x = lms->tm_mday - d) || 
	   (x = lms->tm_hour - h) || (x = lms->tm_min - m) || 
	   (x = lms->tm_sec - s))
	  return x < 0;

	return 1;
    }
    else {
	return (mnum != -1 &&
		1900 + lms->tm_year == y    &&
		lms->tm_mon         == mnum &&
		lms->tm_mday        == d    &&
		lms->tm_hour        == h    &&
		lms->tm_min         == m    &&
		lms->tm_sec         == s);
    }
}


/* Returns 0 if lms later than ims
 * Returns 1 if equal
 * Returns 1 if ims later than lms
 */
NSAPI_PUBLIC int util_later_than(struct tm *lms, char *ims)
{
    return _time_compare(lms, ims, 1);
}


NSAPI_PUBLIC int util_time_equal(struct tm *lms, char *ims)
{
    return _time_compare(lms, ims, 0);
}

/* util_str_time_equal()
 *
 * Function to compare if two time strings are equal
 *
 * Acceptible date formats:
 *      Saturday, 17-Feb-96 19:41:34 GMT        <RFC850>
 *      Sat, 17 Mar 1996 19:41:34 GMT           <RFC1123>
 *
 * Argument t1 MUST be RFC1123 format.
 *
 * Note- it is not the intention of this routine to *always* match
 *       There are cases where we would return != when the strings might
 *       be equal (especially with case).  The converse should not be true.
 *
 * Return 0 if equal, -1 if not equal.
 */
#define MINIMUM_LENGTH  18
#define RFC1123_DAY      5
#define RFC1123_MONTH    8
#define RFC1123_YEAR     12
#define RFC1123_HOUR     17
#define RFC1123_MINUTE   20
#define RFC1123_SECOND   23
NSAPI_PUBLIC int util_str_time_equal(char *t1, char *t2)
{
    int index;

    /* skip over leading whitespace... */
    while(*t1 && isspace(*t1)) ++t1;
    while(*t2 && isspace(*t2)) ++t2;

    /* Check weekday */
    if ( (t1[0] != t2[0]) || (t1[1] != t2[1]) )
        return -1;

    /* Skip to date */
    while(*t2 && !isspace(*t2)) ++t2;
    t2++;

    /* skip if not strings not long enough */
    if ( (strlen(t1) < MINIMUM_LENGTH) || (strlen(t2) < MINIMUM_LENGTH) )
        return -1;

    if ( (t1[RFC1123_DAY] != t2[0]) || (t1[RFC1123_DAY+1] != t2[1]) )
        return -1;

    /* Skip to the month */
    t2 += 3;

    if ( (t1[RFC1123_MONTH] != t2[0]) || (t1[RFC1123_MONTH+1] != t2[1]) ||
        (t1[RFC1123_MONTH+2] != t2[2]) )
        return -1;

    /* Skip to year */
    t2 += 4;

    if ( (t1[RFC1123_YEAR] != t2[0]) ) {
        /* Assume t2 is RFC 850 format */
        if ( (t1[RFC1123_YEAR+2] != t2[0]) || (t1[RFC1123_YEAR+3] != t2[1]) )
            return -1;

        /* skip to hour */
        t2 += 3;
    } else {
        /* Assume t2 is RFC 1123 format */
        if ( (t1[RFC1123_YEAR+1] != t2[1]) || (t1[RFC1123_YEAR+2] != t2[2]) ||
            (t1[RFC1123_YEAR+3] != t2[3]) )
            return -1;

        /* skip to hour */
        t2 += 5;
    }

    /* check date */
    for (index=0; index<8; index++) {
        if ( t1[RFC1123_HOUR+index] != t2[index] )
            return -1;
    }

    /* Ignore timezone */

    return 0;
}


/* --------------------------- util_uri_is_evil --------------------------- */


NSAPI_PUBLIC int util_uri_is_evil(char *t)
{
    register int x;

    for(x = 0; t[x]; ++x) {
        if(t[x] == '/') {
            if(t[x+1] == '/')
                return 1;
            if(t[x+1] == '.') {
                switch(t[x+2]) {
                case '.':
                    if((!t[x+3]) || (t[x+3] == '/'))
                        return 1;
                case '/':
                case '\0':
                    return 1;
                }
            }
        }
#ifdef XP_WIN32
        /* On NT, the directory "abc...." is the same as "abc"
         * The only cheap way to catch this globally is to disallow
         * names with the trailing "."s.  Hopefully this is not over
         * restrictive
         */
        if ((t[x] == '.') && ( (t[x+1] == '/') || (t[x+1] == '\0') )) {
            return 1;
        }
#endif
    }
    return 0;
}

/* ---------------------------- util_uri_parse ---------------------------- */

NSAPI_PUBLIC void util_uri_parse(char *uri)  
{
    int spos = 0, tpos = 0;
    int l = strlen(uri);

    while(uri[spos])  {
        if(uri[spos] == '/')  {
            if((spos != l) && (uri[spos+1] == '.'))  {
                if(uri[spos+2] == '/')
                    spos += 2;
                else
                    if((spos <= (l-3)) && 
                       (uri[spos+2] == '.') && (uri[spos+3] == '/'))  {
                        spos += 3;
                        while((tpos > 0) && (uri[--tpos] != '/'))    
                            uri[tpos] = '\0';
                    }  else
                        uri[tpos++] = uri[spos++];
            }  else  {
                if(uri[spos+1] != '/')
                    uri[tpos++] = uri[spos++];
                else 
                    spos++;
            }
        }  else
            uri[tpos++] = uri[spos++];
    }
    uri[tpos] = '\0';
}


/* -------------------- util_uri_unescape_and_normalize -------------------- */

#ifdef XP_WIN32
/* The server calls this function to unescape the URI and also normalize 
 * the uri.  Normalizing the uri converts all "\" characters in the URI
 * and pathinfo portion to "/".  Does not touch "\" in query strings.
 */
void util_uri_unescape_and_normalize(char *s)
{
    char *t, *u;

    for(t = s, u = s; *t; ++t, ++u) {
        if((*t == '%') && t[1] && t[2]) {
            *u = ((t[1] >= 'A' ? ((t[1] & 0xdf) - 'A')+10 : (t[1] - '0'))*16) +
                  (t[2] >= 'A' ? ((t[2] & 0xdf) - 'A')+10 : (t[2] - '0'));
            t += 2;
        }
        else
            if(u != t)
                *u = *t;
        if (*u == '\\')		/* normalize */
            *u = '/';
    }
    *u = *t;
}
#endif /* XP_WIN32 */

/* -------------------------- util_uri_unescape --------------------------- */

NSAPI_PUBLIC void util_uri_unescape(char *s)
{
    char *t, *u;

    for(t = s, u = s; *t; ++t, ++u) {
        if((*t == '%') && t[1] && t[2]) {
            *u = ((t[1] >= 'A' ? ((t[1] & 0xdf) - 'A')+10 : (t[1] - '0'))*16) +
                  (t[2] >= 'A' ? ((t[2] & 0xdf) - 'A')+10 : (t[2] - '0'));
            t += 2;
        }
        else
            if(u != t)
                *u = *t;
    }
    *u = *t;
}


/* --------------------------- util_uri_escape ---------------------------- */


NSAPI_PUBLIC char *util_uri_escape(char *od, char *s)
{
    char *d;

    if(!od)
        od = (char *) MALLOC((strlen(s)*3) + 1);
    d = od;

    while(*s) {
        if(strchr("% ?#:+&*\"<>\r\n", *s)) {
            sprintf(d, "%%%2x", *s);
            ++s; d += 3;
        }
        else
            *d++ = *s++;
    }
    *d = '\0';
    return od;
}


/* --------------------------- util_url_escape ---------------------------- */


NSAPI_PUBLIC char *util_url_escape(char *od, char *s)
{
    char *d;

    if(!od)
        od = (char *) MALLOC((strlen(s)*3) + 1);
    d = od;

    while(*s) {
        if(strchr("% +*\"<>\r\n", *s)) {
            sprintf(d, "%%%.2x", *s);
            ++s; d += 3;
        }
        else
            *d++ = *s++;
    }
    *d = '\0';
    return od;
}


/* ------------------------- util_mime_separator -------------------------- */


NSAPI_PUBLIC int util_mime_separator(char *sep)
{
    srand(time(NULL));
    return util_sprintf(sep, "%c%c--%d%d%d", CR, LF, rand(), rand(), rand());
}


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
    va_list args;
    va_start(args, fmt);
    return PR_vsnprintf(s, n, fmt, args);
}

NSAPI_PUBLIC int util_vsprintf(char *s, register const char *fmt, va_list args)
{
    return PR_vsnprintf(s, UTIL_PRF_MAXSIZE, fmt, args);
}

NSAPI_PUBLIC int util_sprintf(char *s, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return PR_vsnprintf(s, UTIL_PRF_MAXSIZE, fmt, args);
}

/* ---------------------------- util_sh_escape ---------------------------- */


NSAPI_PUBLIC char *util_sh_escape(char *s)
{
    char *ns = (char *) MALLOC(strlen(s) * 2 + 1);   /* worst case */
    register char *t, *u;

    for(t = s, u = ns; *t; ++t, ++u) {
        if(strchr("&;`'\"|*?~<>^()[]{}$\\ #!", *t))
            *u++ = '\\';
        *u = *t;
    }
    *u = '\0';
    return ns;
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

#ifdef XP_WIN32


/* util_delete_directory()
 * This routine deletes all the files in a directory.  If delete_directory is 
 * TRUE it will also delete the directory itself.
 */
VOID
util_delete_directory(char *FileName, BOOL delete_directory)
{
    HANDLE firstFile;
    WIN32_FIND_DATA findData;
    char *TmpFile, *NewFile;

    if (FileName == NULL)
        return;

    TmpFile = (char *)MALLOC(strlen(FileName) + 5);
    sprintf(TmpFile, "%s\\*.*", FileName);
    firstFile = FindFirstFile(TmpFile, &findData);
    FREE(TmpFile);

    if (firstFile == INVALID_HANDLE_VALUE) 
        return;

    if(strcmp(findData.cFileName, ".") &&
        strcmp(findData.cFileName, "..")) {
            NewFile = (char *)MALLOC(strlen(FileName) + 1 +
                strlen(findData.cFileName) + 1);
            sprintf(NewFile, "%s\\%s",FileName, findData.cFileName);
            DeleteFile(NewFile);
            FREE(NewFile);
    }
    while (TRUE) {
        if(!(FindNextFile(firstFile, &findData))) {
            if (GetLastError() != ERROR_NO_MORE_FILES) {
                ereport(LOG_WARN, XP_GetAdminStr(DBT_couldNotRemoveTemporaryDirectory_), FileName, GetLastError());
            } else {
                FindClose(firstFile);
				if (delete_directory)
					if(!RemoveDirectory(FileName)) {
						ereport(LOG_WARN,
							XP_GetAdminStr(DBT_couldNotRemoveTemporaryDirectory_1),
							FileName, GetLastError());
					}
                return;
            }
        } else {
            if(strcmp(findData.cFileName, ".") &&
                strcmp(findData.cFileName, "..")) {
                NewFile = (char *)MALLOC(strlen(FileName) + 5 +
                    strlen(findData.cFileName) + 1);
                sprintf(NewFile,"%s\\%s", FileName, findData.cFileName);
                DeleteFile(NewFile);
                FREE(NewFile);
            }
        }
    }
}
#endif

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

static char *afmt[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
static char *Afmt[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
	"Saturday",
};

static char *bfmt[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
	"Oct", "Nov", "Dec",
};
static char *Bfmt[] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December",
};

#define TM_YEAR_BASE 1900

static void _util_strftime_conv(char *, int, int, char);

#define _util_strftime_add(str) for (;(*pt = *str++); pt++);
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
	char *scrap;

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


#ifdef XP_UNIX
/*
 * Local Thread Safe version of waitpid.  This prevents the process
 * from blocking in the system call.
 */
NSAPI_PUBLIC pid_t 
util_waitpid(pid_t pid, int *statptr, int options)
{
    pid_t rv;

    for(rv = 0; !rv; PR_Sleep(500)) {
	rv = waitpid(pid, statptr, options | WNOHANG);
	if (rv == -1) {
	    if (errno == EINTR)
		rv = 0; /* sleep and try again */
	    else
		ereport(LOG_WARN, "waitpid failed for pid %d:%s", pid, system_errmsg());
	}
    }
    return rv;
}
#endif

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


NSAPI_PUBLIC char *
util_strtok(register char *s, 
			register const char *delim, 
			register char **lasts)
{
#ifdef HAVE_STRTOK_R
    return strtok_r(s, delim, lasts);
#else
	/*
 	 * THIS IS THE THREAD SAFE VERSION OF strtok captured from
	 * public NetBSD. Note that no locks are needed
	 */
    register char *spanp;
    register int c, sc;
    char *tok;

    if (s == NULL && (s = *lasts) == NULL)
        return (NULL);

    /*
     * Skip (span) leading delimiters (s += strspn(s, delim), 
	 * sort of).
     */

cont:
    c = *s++;
    for (spanp = (char *)delim; (sc = *spanp++) != 0;) {
        if (c == sc)
            goto cont;
    }

    if (c == 0) {       /* no non-delimiter characters */
        *lasts = NULL;
        return (NULL);
    }
    tok = s - 1;

    /*
     * Scan token (scan for delimiters: s += strcspn(s, delim), 
	 * sort of).
     * Note that delim must have one NUL; we stop if we see that, too.
     */
    for (;;) {
        c = *s++;
        spanp = (char *)delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *lasts = s;
                return (tok);
            }
        } while (sc != 0);
    }
    /* NOTREACHED */
#endif /* no strtok_r */
}

#ifndef XP_WIN32
NSAPI_PUBLIC struct passwd *
util_getpwnam(const char *name, struct passwd *result, char *buffer, 
	int buflen)
{
#ifdef HAVE_PW_R

#ifdef AIX
#if OSVERSION >= 4320
    return ((int)getpwnam_r(name, result, buffer, buflen, 0) == 0 ? result : NULL);
#else
    return ((int)getpwnam_r(name, result, buffer, buflen) == 0 ? result : NULL);
#endif
#else
    return getpwnam_r(name, result, buffer, buflen);
#endif /* AIX */

#else
    char *lastp;
    struct passwd *r;
    RE_LOCK(pw);
    r = getpwnam(name);
    if (!r) 
	return r;

    result->pw_gid = r->pw_gid;
    result->pw_uid = r->pw_uid;
    /* Hope this buffer is long enough */
    if (buffer)
        util_snprintf(buffer, buflen, "%s:%s:%d:%d:%s:%s:%s", r->pw_name, r->pw_passwd,
		r->pw_uid, r->pw_gid, r->pw_gecos, r->pw_dir, r->pw_shell);
    RE_UNLOCK(pw);

    result->pw_name = util_strtok(buffer, ":", &lastp);
    result->pw_passwd = util_strtok(NULL, ":", &lastp);
    (void) util_strtok(NULL, ":", &lastp);
    (void) util_strtok(NULL, ":", &lastp);
    result->pw_gecos = util_strtok(NULL, ":", &lastp);
    result->pw_dir = util_strtok(NULL, ":", &lastp);
    result->pw_shell = util_strtok(NULL, ":", &lastp);
    return result;
#endif
}
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


NSAPI_PUBLIC char *
util_ctime(const time_t *clock, char *buf, int buflen)
{
/* 
 * From cgi-src/restore.c refering to XP_WIN32:
 * 	MLM - gross, but it works, better now FLC
 */
#if !defined(HAVE_TIME_R) || defined(XP_WIN32)
    RE_LOCK(ctime);
    strncpy(buf, ctime(clock), buflen);
    buf[buflen - 1] = '\0';
    RE_UNLOCK(ctime);
    return buf;
#elif HAVE_TIME_R == 2
    return ctime_r(clock, buf);
#else /* HAVE_TIME_R == 3 */
    return ctime_r(clock, buf, buflen);
#endif
}

NSAPI_PUBLIC struct tm *
util_gmtime(const time_t *clock, struct tm *res)
{
#ifdef HAVE_TIME_R
    return gmtime_r(clock, res);
#else
    struct tm *rv;
    time_t zero = 0x7fffffff;

    RE_LOCK(gmtime);
    rv = gmtime(clock);
    RE_UNLOCK(gmtime);
    if (!rv)
        rv = gmtime(&zero);
    if (rv)
        *res = *rv;
    else 
        return NULL;
    
    return res;
#endif
}

NSAPI_PUBLIC char *
util_asctime(const struct tm *tm, char *buf, int buflen)
{
#if HAVE_TIME_R == 2
    return asctime_r(tm, buf);
#elif HAVE_TIME_R == 3
    return asctime_r(tm, buf, buflen);
#else
    RE_LOCK(asctime);
    strncpy(buf, asctime(tm), buflen);
    buf[buflen - 1] = '\0';
    RE_UNLOCK(asctime);
    return buf;
#endif
}

NSAPI_PUBLIC char *
util_strerror(int errnum, char *msg, int buflen)
{
#ifdef HAVE_STRERROR_R
    /* More IBM real-genius */
    return ((int)strerror_r(errnum, msg, buflen) > 0) ? msg : NULL;
#else
    /* RE_LOCK(strerror); I don't think this is worth the trouble */
    (void)strncpy(msg, strerror(errnum), buflen);
    msg[buflen - 1] = '\0';
    return msg;
    /* RE_UNLOCK(strerror); */
#endif
}



/* ------------------------------- OLD CODE ------------------------------- */


#if 0

NSAPI_PUBLIC int util_vsnprintf(char *s, int n, register char *fmt, 
                                va_list args)
{
    register int pos = 0, max = (n > 2 ? n-2 : -1), boundson;
    register char c, *t;

    if((max == -1) && (n != -1))
        goto punt;

    boundson = (n != -1);
    while(*fmt) {
        if(boundson && (pos > max))
            break;
        c = *fmt++;
        switch(c) {
          case '%':
            switch(*fmt++) {
              case 'd':
                if(boundson && ((pos + 10) > max))
                    goto punt;
                pos += util_itoa(va_arg(args, int), &s[pos]);
                break;
              case 's':
                t = va_arg(args, char *);
                while(*t) {
                    s[pos++] = *t++;
                    if(boundson && (pos > max))
                        goto punt;
                }
                break;
              case 'c':
                s[pos++] = (char) va_arg(args, int);
                break;
              case '%':
                s[pos++] = '%';
                break;
            }
            break;
          case '\\':
            if( (s[pos++] = *fmt) )
                ++fmt;
            break;
          default:
            s[pos++] = c;
        }
    }
  punt:
    s[pos] = '\0';

    va_end(args);
    return pos;
}

NSAPI_PUBLIC int util_snprintf(char *s, int n, char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    return util_vsnprintf(s, n, fmt, args);
}

NSAPI_PUBLIC int util_vsprintf(char *s, register char *fmt, va_list args)
{
    return util_vsnprintf(s, -1, fmt, args);
}

NSAPI_PUBLIC int util_sprintf(char *s, char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    return util_vsnprintf(s, -1, fmt, args);
}
#endif
