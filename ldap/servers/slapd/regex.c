/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2022 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "slap.h"
#include "slapi-plugin.h"
#include <pcre2.h>

#define OVEC_MATCH_LIMIT 30 /* should be a multiple of 3; store up to \9 */
#define CPRE_ERR_MSG_SIZE 120

struct slapi_regex_handle
{
    pcre2_code *re_pcre;
    pcre2_match_data *match_data; /* Contains the output vector */
    pcre2_match_context *mcontext; /* Stores the max element limit */
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
slapi_re_comp(const char *pat, char **error)
{
    Slapi_Regex *re_handle = NULL;
    pcre2_code *re = NULL;
    int32_t myerror;
    PCRE2_SIZE erroffset;
    PCRE2_UCHAR errormsg[CPRE_ERR_MSG_SIZE];

    re = pcre2_compile((PCRE2_SPTR)pat, strlen(pat), 0,
                       &myerror, &erroffset, NULL);
    if (re == NULL) {
        pcre2_get_error_message(myerror, errormsg, CPRE_ERR_MSG_SIZE);
        *error = slapi_ch_strdup((char *)errormsg);
    } else {
        re_handle = (Slapi_Regex *)slapi_ch_calloc(sizeof(Slapi_Regex), 1);
        re_handle->re_pcre = re;
        *error = NULL;
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
slapi_re_exec(Slapi_Regex *re_handle, const char *subject, time_t time_up)
{
    int32_t rc;
    time_t curtime = slapi_current_rel_time_t();

    if (NULL == re_handle || NULL == re_handle->re_pcre || NULL == subject) {
        return LDAP_PARAM_ERROR;
    }

    if (time_up != -1 && curtime > time_up) {
        return LDAP_TIMELIMIT_EXCEEDED;
    }

    if (re_handle->match_data) {
        pcre2_match_data_free(re_handle->match_data);
    }
    re_handle->match_data = pcre2_match_data_create_from_pattern(re_handle->re_pcre, NULL);

    if (re_handle->mcontext == NULL) {
        re_handle->mcontext = pcre2_match_context_create(NULL);
        pcre2_set_match_limit(re_handle->mcontext, OVEC_MATCH_LIMIT);
    }


    rc = pcre2_match(re_handle->re_pcre,    /* the compiled pattern */
                     (PCRE2_SPTR)subject,   /* the subject string */
                     strlen(subject),       /* the length of the subject */
                     0,                     /* start at offset 0 in the subject */
                     0,                     /* default options */
                     re_handle->match_data, /* contains the resulting output vector */
                     re_handle->mcontext);  /* stores the max element limit */

    if (rc >= 0) {
        return 1; /* matched */
    } else {
        return 0; /* did not match */
    }
}

/**
 * Matches a compiled regular expression pattern against a given string.
 * A thin wrapper of pcre_exec.
 *
 * unlike slapi_re_exec, this has no timeout. The timeout was only checked
 * at the start of the function, not during or after, so was essentially
 * meaningless.
 *
 * \param re_handle The regex handler returned from slapi_re_comp.
 * \param subject A string to be checked against the compiled pattern.
 * \return This function returns 0 if the string did not match.
 * \return This function returns 1 if the string matched.
 * \return This function returns other values if any error occurred.
 * \warning The regex handler should be released by slapi_re_free().
 */
int32_t
slapi_re_exec_nt(Slapi_Regex *re_handle, const char *subject)
{
    int32_t rc = 0;

    if (NULL == re_handle || NULL == re_handle->re_pcre || NULL == subject) {
        return LDAP_PARAM_ERROR;
    }

    if (re_handle->match_data) {
        pcre2_match_data_free(re_handle->match_data);
    }
    re_handle->match_data = pcre2_match_data_create_from_pattern(re_handle->re_pcre, NULL);

    if (re_handle->mcontext == NULL) {
        re_handle->mcontext = pcre2_match_context_create(NULL);
        pcre2_set_match_limit(re_handle->mcontext, OVEC_MATCH_LIMIT);
    }
    rc = pcre2_match(re_handle->re_pcre,    /* the compiled pattern */
                     (PCRE2_SPTR)subject,   /* the subject string */
                     strlen(subject),       /* the length of the subject */
                     0,                     /* start at offset 0 in the subject */
                     0,                     /* default options */
                     re_handle->match_data, /* contains the resulting output vector */
                     re_handle->mcontext);  /* stores the max element limit */

    if (rc >= 0) {
        return 1; /* matched */
    } else {
        return 0; /* did not match */
    }
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
slapi_re_subs(Slapi_Regex *re_handle, const char *subject, const char *src, char **dst, unsigned long dstlen)
{
    return slapi_re_subs_ext(re_handle, subject, src, dst, dstlen, 0 /* not a filter */);
}

int
slapi_re_subs_ext(Slapi_Regex *re_handle, const char *subject, const char *src, char **dst, unsigned long dstlen, int filter)
{
    PCRE2_SIZE thislen = 0;
    PCRE2_SIZE len = 0;
    int32_t pin;
    PCRE2_SIZE *ovector;
    char *mydst;
    const char *prev;
    const char *substring_start;
    const char *p;

    if (NULL == src || NULL == re_handle || NULL == re_handle->match_data) {
        memset(*dst, '\0', dstlen);
        return -1;
    } else if (NULL == dst || NULL == *dst || 0 == dstlen) {
        return -1;
    }

    ovector = pcre2_get_ovector_pointer(re_handle->match_data);
    mydst = *dst;
    prev = src;

    for (p = src; *p != '\0'; p++) {
        if ('&' == *p) {
            /* Don't replace '&' if it's a filter AND: "(&(cn=a)(sn=b))"  */
            if (!filter || !(*prev == '(' && *(p + 1) == '(')) {
                substring_start = subject + ovector[0];
                thislen = ovector[1] - ovector[0];
                len += thislen;
            } else { /* is a filter AND clause */
                /* just copy it into the filter */
                substring_start = p;
                thislen = 1;
                len++;
            }
        } else if (('\\' == *p) && ('0' <= *(p + 1) && *(p + 1) <= '9')) {
            pin = *(++p) - '0';
            if (OVEC_MATCH_LIMIT <= 2 * pin + 1) {
                memset(*dst, '\0', dstlen);
                return -1;
            }
            substring_start = subject + ovector[2 * pin];
            thislen = ovector[2 * pin + 1] - ovector[2 * pin];
            len += thislen;
        } else {
            substring_start = p;
            thislen = 1;
            len++;
        }
        if (len >= dstlen) {
            int32_t offset = mydst - *dst;
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
            pcre2_code_free(re_handle->re_pcre);
        }
        if (re_handle->match_data) {
            pcre2_match_data_free(re_handle->match_data);
        }
        if (re_handle->mcontext) {
            pcre2_match_context_free(re_handle->mcontext);
        }
        slapi_ch_free((void **)&re_handle);
    }
}
