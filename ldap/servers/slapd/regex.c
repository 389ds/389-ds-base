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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* number of elements in the output vector */
#define OVECCOUNT 30    /* should be a multiple of 3; store up to \9 */

#include "slap.h"
#include "slapi-plugin.h"

/* Perl Compatible Regular Expression */
#include <pcre.h>

struct slapi_regex_handle {
    pcre *re_pcre;            /* contains the compiled pattern */
    int  *re_ovector;        /* output vector */
    int  re_oveccount;        /* count of the elements in output vector */
};

/**
 * Compiles a regular expression pattern.
 *
 * \param pat Pattern to be compiled.
 * \param error The error string is set if the compile fails.
 * \return This function returns a pointer to the regex handler which stores
 * the compiled pattern. NULL if the compile fails.
 * \warning The regex handler should be released by slapi_re_free().
 */
Slapi_Regex *
slapi_re_comp( const char *pat, const char **error )
{
    Slapi_Regex *re_handle = NULL;
    pcre        *re = NULL;
    const char  *myerror = NULL;
    int         erroffset;

    re = pcre_compile(pat, 0, &myerror, &erroffset, NULL);
    if (error) {
        *error = myerror;
    }
    if (re) {
        re_handle = (Slapi_Regex *)slapi_ch_calloc(sizeof(Slapi_Regex), 1);
        re_handle->re_pcre = re;
    }

    return re_handle;
}

/**
 * Matches a compiled regular expression pattern against a given string.
 * A thin wrapper of pcre_exec.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string to be checked against the compiled pattern.
 * \param time_up If the current time is larger than the value, this function
 * returns immediately.  (-1) means no time limit.
 * \return This function returns 0 if the string did not match.
 * \return This function returns 1 if the string matched.
 * \return This function returns other values if any error occurred.
 * \warning The regex handler should be released by slapi_re_free().
 */
int
slapi_re_exec( Slapi_Regex *re_handle, const char *subject, time_t time_up )
{
    int rc;
    time_t curtime = current_time();

    if (NULL == re_handle || NULL == re_handle->re_pcre || NULL == subject) {
        return LDAP_PARAM_ERROR;
    }

    if ( time_up != -1 && curtime > time_up ) {
        return LDAP_TIMELIMIT_EXCEEDED;
    }

    if (NULL == re_handle->re_ovector) {
        re_handle->re_oveccount = OVECCOUNT;
        re_handle->re_ovector = (int *)slapi_ch_malloc(sizeof(int) * OVECCOUNT);
    }

    rc = pcre_exec( re_handle->re_pcre, /* the compiled pattern */
                    NULL,            /* no extra data */
                    subject,         /* the subject string */
                    strlen(subject), /* the length of the subject */
                    0,               /* start at offset 0 in the subject */
                    0,               /* default options */
                    re_handle->re_ovector, /* output vector for substring info */
                    re_handle->re_oveccount ); /* number of elems in the ovector */

    if (rc >= 0) {
        return 1;    /* matched */
    } else {
        return 0;    /* did not match */
    }

    return rc;
}

/**
 * Substitutes '&' or '\#' in the param src with the matched string.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string checked against the compiled pattern.
 * \param src A given string which could contain the substitution symbols.
 * \param dst A pointer pointing to the memory which stores the output string.
 * \param dstlen Size of the memory dst.
 * \return This function returns 0 if the substitution was successful.
 * \return This function returns -1 if the substitution failed.
 * \warning The regex handler should be released by slapi_re_free().
 */
int
slapi_re_subs( Slapi_Regex *re_handle, const char *subject,
        const char *src, char **dst, unsigned long dstlen)
{
	return slapi_re_subs_ext(re_handle, subject, src, dst, dstlen, 0 /* not a filter */);
}

int
slapi_re_subs_ext( Slapi_Regex *re_handle, const char *subject,
               const char *src, char **dst, unsigned long dstlen, int filter )
{
    int  thislen = 0;
    int  len = 0;
    int  pin;
    int  *ovector;
    char *mydst;
    const char *prev;
    const char *substring_start;
    const char *p;

    if (NULL == src || NULL == re_handle || NULL == re_handle->re_ovector) {
        memset(*dst, '\0', dstlen);
        return -1;
    } else if (NULL == dst || NULL == *dst || 0 == dstlen) {
        return -1;
    }

    ovector = re_handle->re_ovector;
    mydst = *dst;
    prev = src;

    for (p = src; *p != '\0'; p++) {
        if ('&' == *p) {
            /* Don't replace '&' if it's a filter AND: "(&(cn=a)(sn=b))"  */
            if(!filter || !(*prev == '(' && *(p+1) == '(')){
                if (re_handle->re_oveccount <= 1) {
                    memset(*dst, '\0', dstlen);
                    return -1;
                }
                substring_start = subject + ovector[0];
                thislen = ovector[1] - ovector[0];
                len += thislen;
            } else { /* is a filter AND clause */
                /* just copy it into the filter */
                substring_start = p;
                thislen = 1;
                len++;
            }
        } else if (('\\' == *p) && ('0' <= *(p+1) && *(p+1) <= '9')) {
            pin = *(++p) - '0';
            if (re_handle->re_oveccount <= 2*pin+1) {
                memset(*dst, '\0', dstlen);
                return -1;
            }
            substring_start = subject + ovector[2*pin];
            thislen = ovector[2*pin+1] - ovector[2*pin];
            len += thislen;
        } else {
            substring_start = p;
            thislen = 1;
            len++;
        }
        if (len >= dstlen) {
            int offset = mydst - *dst;
            dstlen = len * 2;
            *dst = (char *)slapi_ch_realloc(*dst, dstlen);
            mydst = *dst + offset;
        }
        memcpy(mydst, substring_start, thislen);
        mydst += thislen;
        prev = p;
    }
    *mydst = '\0';
    return 0;
}

/**
 * Releases the regex handler which was returned from slapi_re_comp.
 *
 * \param re_handle The regex handler to be released.
 * \return nothing
 */
void
slapi_re_free(Slapi_Regex *re_handle)
{
    if (re_handle) {
        if (re_handle->re_pcre) {
            pcre_free(re_handle->re_pcre);
        }
        slapi_ch_free((void **)&re_handle->re_ovector);
        slapi_ch_free((void **)&re_handle);
    }
}
