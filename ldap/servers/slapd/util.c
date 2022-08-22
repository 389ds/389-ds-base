/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* util.c   -- utility functions -- functions available form libslapd */
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <stdint.h>
#include <fcntl.h>
#include <libgen.h>
#include <pk11func.h>
#include "slap.h"
#include "prtime.h"
#include "prinrval.h"
#include "snmp_collator.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

#define UTIL_ESCAPE_NONE 0
#define UTIL_ESCAPE_HEX 1
#define UTIL_ESCAPE_BACKSLASH 2

#define _PSEP "/"
#define _PSEP2 "//"
#define _CSEP '/'

/* slapi_filter_sprintf macros */
#define ATTRSIZE 256   /* size allowed for an attr name */
#define FILTER_BUF 128 /* initial buffer size for attr value */
#define BUF_INCR 16    /* the amount to increase the FILTER_BUF once it fills up */

/* slapi-private contains the pal. */
#include <slapi-private.h>

static int
special_filename(unsigned char c)
{
    if ((c < 45) || (c == '/') || ((c > 57) && (c < 65)) ||
        ((c > 90) && (c < 95)) || (c == 96) || (c > 122)) {
        return UTIL_ESCAPE_HEX;
    }
    return UTIL_ESCAPE_NONE;
}

static int
special_np(unsigned char c)
{
    if (c == '\\') {
        return UTIL_ESCAPE_BACKSLASH;
    }
    if (c < 32 || c > 126 || c == '"') {
        return UTIL_ESCAPE_HEX;
    }
    return UTIL_ESCAPE_NONE;
}

static int
special_np_and_punct(unsigned char c)
{
    if (c == '\\') {
        return UTIL_ESCAPE_BACKSLASH;
    }
    if (c < 32 || c > 126 || c == '"' || c == '*') {
        return UTIL_ESCAPE_HEX;
    }
    return UTIL_ESCAPE_NONE;
}

/*
 *  Used by filter_stuff_func to help extract an attribute so we know
 *  how to normalize the value.
 */
static int
special_attr_char(unsigned char c)
{
    return (c < 32 ||
            c > 126 ||
            c == '*' ||
            c == '|' ||
            c == '&' ||
            c == '!' ||
            c == '(' ||
            c == ')' ||
            c == '\\' ||
            c == '=' ||
            c == '"');
}

/* No '\\' */
#define DOESCAPE_FLAGS_HEX_NOESC 0x1

static const char *
do_escape_string(
    const char *str,
    int len, /* -1 means str is nul-terminated */
    char buf[BUFSIZ],
    int (*special)(unsigned char),
    int flags)
{
    const char *s;
    const char *last;
    int esc;

    if (str == NULL) {
        *buf = '\0';
        return buf;
    }

    if (len == -1)
        len = strlen(str);
    if (len == 0)
        return str;

    last = str + len - 1;
    for (s = str; s <= last; ++s) {
        if ((esc = (*special)((unsigned char)*s))) {
            const char *first = str;
            char *bufNext = buf;
            int bufSpace = BUFSIZ - 4;
            while (1) {
                if (bufSpace < (s - first))
                    s = first + bufSpace - 1;
                if (s > first) {
                    memcpy(bufNext, first, s - first);
                    bufNext += (s - first);
                    bufSpace -= (s - first);
                }
                if (s > last) {
                    break;
                }
                do {
                    if (bufSpace < 4) {
                        memcpy(bufNext, "..", 2);
                        bufNext += 2;
                        goto bail;
                    }
                    if (esc == UTIL_ESCAPE_BACKSLASH) {
                        /* *s is '\\' */
                        /* If *(s+1) and *(s+2) are both hex digits,
                         * the char is already escaped. */
                        if (isxdigit(*(s + 1)) && isxdigit(*(s + 2))) {
                            memcpy(bufNext, s, 3);
                            bufNext += 3;
                            bufSpace -= 3;
                            s += 2;
                        } else {
                            *bufNext++ = *s;
                            --bufSpace;
                        }
                    } else { /* UTIL_ESCAPE_HEX */
                        if (!(flags & DOESCAPE_FLAGS_HEX_NOESC)) {
                            *bufNext++ = '\\';
                            --bufSpace;
                        }
                        PR_snprintf(bufNext, 3, "%02x", *(unsigned char *)s);
                        bufNext += 2;
                        bufSpace -= 2;
                    }
                } while (++s <= last &&
                         (esc = (*special)((unsigned char)*s)));
                if (s > last)
                    break;
                first = s;
                while ((esc = (*special)((unsigned char)*s)) == UTIL_ESCAPE_NONE && s <= last)
                    ++s;
            }
        bail:
            *bufNext = '\0';
            return buf;
        }
    } /* for */
    return str;
}

/*
 * Function: escape_string
 * Arguments: str: string
 *            buf: a char array of BUFSIZ length, in which the escaped string will
 *                 be returned.
 * Returns: a pointer to buf, if str==NULL or it needed to be escaped, or
 *          str itself otherwise.
 *
 * This function should only be used for generating loggable strings.
 */
const char *
escape_string(const char *str, char buf[BUFSIZ])
{
    return do_escape_string(str, -1, buf, special_np, 0);
}

const char *
escape_string_with_punctuation(const char *str, char buf[BUFSIZ])
{
    return do_escape_string(str, -1, buf, special_np_and_punct, 0);
}

const char *
escape_string_for_filename(const char *str, char buf[BUFSIZ])
{
    return do_escape_string(str, -1, buf, special_filename, DOESCAPE_FLAGS_HEX_NOESC);
}

#define ESCAPE_FILTER 1
#define NORM_FILTER 2

struct filter_ctx
{
    char *buf;
    char *attr;
    int attr_position;
    int attr_found;
    size_t attr_size;
    int buf_size;
    int buf_len;
    int next_arg_needs_esc_norm;
    int skip_escape;
};

/*
 *  This function is called by slapi_filter_sprintf to escape/normalize certain values
 */
static PRIntn
filter_stuff_func(void *arg, const char *val, PRUint32 slen)
{
    struct filter_ctx *ctx = (struct filter_ctx *)arg;
    struct berval escaped_filter;
    struct berval raw_filter;
    char *buf = (char *)val;
    int extra_space;
    int filter_len = (int)slen;

    /* look at val - if val is one of our special keywords, and make a note of it for the next pass */
    if (strcmp(val, ESC_NEXT_VAL) == 0) {
        ctx->next_arg_needs_esc_norm |= ESCAPE_FILTER;
        return 0;
    }
    if (strcmp(val, NORM_NEXT_VAL) == 0) {
        ctx->next_arg_needs_esc_norm |= NORM_FILTER;
        return 0;
    }
    if (strcmp(val, ESC_AND_NORM_NEXT_VAL) == 0) {
        ctx->next_arg_needs_esc_norm = NORM_FILTER | ESCAPE_FILTER;
        return 0;
    }
    /*
     *  Start collecting the attribute name so we can use the correct
     *  syntax normalization func.
     */
    if (ctx->attr_found == 0 && ctx->attr_position < (ctx->attr_size - 1)) {
        if (ctx->attr[0] == '\0') {
            if (strstr(val, "=")) {
                /* we have an attr we need to record */
                if (!special_attr_char(val[0])) {
                    memcpy(ctx->attr, val, 1);
                    ctx->attr_position++;
                }
            } else {
                /*
                 *  We have passed in an attribute as a arg - so we can just set the
                 *  attr with val.  The next pass should be '=', otherwise we will
                 *  reset it.
                 */
                if (slen > ctx->attr_size) {
                    if (ctx->attr_size == ATTRSIZE) {
                        ctx->attr = slapi_ch_calloc(sizeof(char), slen+1);
                    } else {
                        ctx->attr = slapi_ch_realloc(ctx->attr, sizeof(char) * (slen+1));
                    }
                    ctx->attr_size = slen+1;
                }
                memcpy(ctx->attr, val, slen);
                ctx->attr_position = slen;
            }
        } else {
            if (val[0] == '=') { /* hit the end of the attribute name */
                ctx->attr_found = 1;
            } else {
                if (special_attr_char(val[0])) {
                    /* this is not an attribute, we should not be collecting this, reset everything */
                    memset(ctx->attr, '\0', ctx->attr_size);
                    ctx->attr_position = 0;
                } else {
                    /* we can be adding char by char and overrun allocated size */
                    if (ctx->attr_position >= ctx->attr_size) {
                        if (ctx->attr_size == ATTRSIZE) {
                            char *ctxattr = slapi_ch_calloc(sizeof(char), ctx->attr_size + ATTRSIZE);
                            memcpy(ctxattr, ctx->attr, ctx->attr_size);
                            ctx->attr = ctxattr;
                        } else {
                            ctx->attr = slapi_ch_realloc(ctx->attr, sizeof(char) * (ctx->attr_size + ATTRSIZE));
                        }
                        ctx->attr_size = ctx->attr_size + ATTRSIZE;
                    }
                    memcpy(ctx->attr + ctx->attr_position, val, 1);
                    ctx->attr_position++;
                }
            }
        }
    }

    if (ctx->next_arg_needs_esc_norm && !ctx->skip_escape) {
        /*
         *  Normalize the filter value first
         */
        if (ctx->next_arg_needs_esc_norm & NORM_FILTER) {
            char *norm_val = NULL;

            if (ctx->attr_found) {
                slapi_attr_value_normalize(NULL, NULL, ctx->attr, buf, 1, &norm_val);
                if (norm_val) {
                    buf = norm_val;
                    filter_len = strlen(buf);
                }
            }
        }
        /*
         *  Escape the filter value
         */
        if (ctx->next_arg_needs_esc_norm & ESCAPE_FILTER) {
            raw_filter.bv_val = (char *)buf;
            raw_filter.bv_len = filter_len;
            if (ldap_bv2escaped_filter_value(&raw_filter, &escaped_filter) != 0) {
                slapi_log_err(SLAPI_LOG_TRACE, "filter_stuff_func", "Failed to escape filter value(%s)\n", val);
                ctx->next_arg_needs_esc_norm = 0;
                return -1;
            } else {
                filter_len = escaped_filter.bv_len;
                buf = escaped_filter.bv_val;
            }
        }

        /*
         *  Now add the new value to the buffer, and allocate more memory if needed
         */
        if (ctx->buf_size + filter_len >= ctx->buf_len) {
            /* increase buffer for this filter */
            extra_space = (ctx->buf_len + filter_len + BUF_INCR);
            ctx->buf = slapi_ch_realloc(ctx->buf, sizeof(char) * extra_space);
            ctx->buf_len = extra_space;
        }

        /* append the escaped value */
        memcpy(ctx->buf + ctx->buf_size, buf, filter_len);
        ctx->buf_size += filter_len;

        /* done with the value, reset everything */
        ctx->next_arg_needs_esc_norm = 0;
        ctx->attr_found = 0;
        ctx->attr_position = 0;
        memset(ctx->attr, '\0', ctx->attr_size);
        slapi_ch_free_string(&buf);

        return filter_len;
    } else { /* process arg as is */
        /* check if we have enough room in our buffer */
        if (ctx->buf_size + (int)slen >= ctx->buf_len) {
            /* increase buffer for this filter */
            extra_space = (ctx->buf_len + slen + BUF_INCR);
            ctx->buf = slapi_ch_realloc((char *)ctx->buf, sizeof(char) * extra_space);
            ctx->buf_len = extra_space;
        }
        memcpy(ctx->buf + ctx->buf_size, buf, slen);
        ctx->buf_size += slen;

        return slen;
    }
}

/*
 *  This is basically like slapi_ch_smprintf() except it can handle special
 *  keywords that will cause the next value to be escaped and/or normalized.
 *
 *  ESC_NEXT_VAL - escape the next value
 *  NORM_NEXT_VAL -  normalize the next value
 *  ESC_AND_NORM_NEXT_VAL - escape and normalize the next value
 *
 *  Example:
 *
 *     slapi_filter_sprintf("cn=%s%s", ESC_NEXT_VAL, value);
 *     slapi_filter_sprintf("(|(cn=%s%s)(sn=%s%s))", ESC_NEXT_VAL, value, NORM_NEXT_VAL, value);
 *
 *  Note: you need a string format specifier(%s) for each keyword
 */
char *
slapi_filter_sprintf(const char *fmt, ...)
{
    struct filter_ctx ctx = {0};
    va_list args;
    char attr_static[ATTRSIZE] = {0};
    char *buf;
    int rc;

    buf = slapi_ch_calloc(sizeof(char), FILTER_BUF + 1);
    ctx.buf = buf;
    ctx.attr = attr_static;
    ctx.attr_size = ATTRSIZE;
    ctx.attr_position = 0;
    ctx.attr_found = 0;
    ctx.buf_len = FILTER_BUF;
    ctx.buf_size = 0;
    ctx.next_arg_needs_esc_norm = 0;
    ctx.skip_escape = 0;

    va_start(args, fmt);
    rc = PR_vsxprintf(filter_stuff_func, &ctx, fmt, args);
    if (rc == -1) {
        /* transformation failed, just return non-normalized/escaped string */
        ctx.skip_escape = 1;
        PR_vsxprintf(filter_stuff_func, &ctx, fmt, args);
    }
    va_end(args);

    if (ctx.attr_size > ATTRSIZE) {
        slapi_ch_free_string(&ctx.attr);
    }

    return ctx.buf;
}

/*
 *  escape special characters in values used in search filters
 *
 *  caller must free the returned value
 */
char *
slapi_escape_filter_value(char *filter_str, int len)
{
    struct berval escaped_filter;
    struct berval raw_filter;
    int filter_len;

    /*
     *  Check the length for special cases
     */
    if (len == -1) {
        /* filter str is null terminated */
        filter_len = strlen(filter_str);
    } else if (len == 0) {
        /* return the filter as is */
        return slapi_ch_strdup(filter_str);
    } else {
        /* the len is the length */
        filter_len = len;
    }
    /*
     *  Construct the berval and escape it
     */
    raw_filter.bv_val = filter_str;
    raw_filter.bv_len = filter_len;
    if (ldap_bv2escaped_filter_value(&raw_filter, &escaped_filter) != 0) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_escape_filter_value",
                      "Failed to escape filter value(%s)\n", filter_str);
        return NULL;
    } else {
        return escaped_filter.bv_val;
    }
}

/* replace c with c2 in str */
void
replace_char(char *str, char c, char c2)
{
    for (size_t i = 0; (str != NULL) && (str[i] != '\0'); i++) {
        if (c == str[i]) {
            str[i] = c2;
        }
    }
}

/*
** This function takes a quoted attribute value of the form "abc",
** and strips off the enclosing quotes.  It also deals with quoted
** characters by removing the preceeding '\' character.
**
*/
void
strcpy_unescape_value(char *d, const char *s)
{
    int gotesc = 0;
    const char *end = s + strlen(s);
    for (; *s; s++) {
        switch (*s) {
        case '"':
            break;
        case '\\':
            gotesc = 1;
            if (s + 2 < end) {
                int n = slapi_hexchar2int(s[1]);
                if (n >= 0 && n < 16) {
                    int n2 = slapi_hexchar2int(s[2]);
                    if (n2 >= 0) {
                        n = (n << 4) + n2;
                        if (n == 0) { /* don't change \00 */
                            *d++ = *s++;
                            *d++ = *s++;
                            *d++ = *s;
                        } else { /* change \xx to a single char */
                            *d++ = (char)n;
                            s += 2;
                        }
                        gotesc = 0;
                    }
                }
            }
            /* This is an escaped single character (like \"), so
             * just copy the special character and not the escape.
             * We need to be careful to not go past the end of
             * the string when the loop increments s. */
            if (gotesc && (s + 1 < end)) {
                s++;
                *d++ = *s;
                gotesc = 0;
            }
            break;
        default:
            *d++ = *s;
            break;
        }
    }
    *d = '\0';
}

/* functions to convert between an entry and a set of mods */
int
slapi_mods2entry(Slapi_Entry **e, const char *idn, LDAPMod **iattrs)
{
    int i, rc = LDAP_SUCCESS;
    LDAPMod **attrs = NULL;

    PR_ASSERT(idn);
    PR_ASSERT(iattrs);
    PR_ASSERT(e);

    attrs = normalize_mods2bvals((const LDAPMod **)iattrs);
    PR_ASSERT(attrs);

    /* Construct an entry */
    *e = slapi_entry_alloc();
    PR_ASSERT(*e);
    slapi_entry_init(*e, slapi_ch_strdup(idn), NULL);

    for (i = 0; rc == LDAP_SUCCESS && attrs && attrs[i]; i++) {
        char *normtype;
        Slapi_Value **vals;

        /*
         * slapi_entry_apply_mod_extension applys mod and stores
         * the result in the extension
         * return value:  1 - mod is applied and stored in extension
         *               -1 - mod is applied and failed
         *                0 - mod is nothing to do with extension
         */
        rc = slapi_entry_apply_mod_extension(*e, attrs[i], -1);
        if (rc) {
            if (1 == rc) {
                rc = LDAP_SUCCESS;
            } else {
                rc = LDAP_OPERATIONS_ERROR;
            }
#if !defined(USE_OLD_UNHASHED)
            /* In case USE_OLD_UNHASHED,
             * unhashed pw needs to be in attr, too. */
            continue;
#endif
        }

        normtype = slapi_attr_syntax_normalize(attrs[i]->mod_type);
        valuearray_init_bervalarray(attrs[i]->mod_bvalues, &vals);
        if (strcasecmp(normtype, SLAPI_USERPWD_ATTR) == 0) {
            pw_encodevals(vals);
        }

        /* set entry uniqueid - also adds attribute to the list */
        if (strcasecmp(normtype, SLAPI_ATTR_UNIQUEID) == 0) {
            if (vals) {
                slapi_entry_set_uniqueid(*e,
                                         slapi_ch_strdup(slapi_value_get_string(vals[0])));
            } else {
                rc = LDAP_NO_SUCH_ATTRIBUTE;
            }
        } else {
            rc = slapi_entry_add_values_sv(*e, normtype, vals);
        }

        valuearray_free(&vals);
        if (rc != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_mods2entry", "Add_values for type %s failed (rc: %d)\n",
                          normtype, rc);
            slapi_entry_free(*e);
            *e = NULL;
        }
        slapi_ch_free((void **)&normtype);
    }
    freepmods(attrs);

    return rc;
}

int
slapi_entry2mods(const Slapi_Entry *e, char **dn, LDAPMod ***attrs)
{
    Slapi_Mods smods;
    Slapi_Attr *attr;
    Slapi_Value **va;
    char *type;
    int rc;

    PR_ASSERT(e && attrs);

    if (dn)
        *dn = slapi_ch_strdup(slapi_entry_get_dn((Slapi_Entry *)e));
    slapi_mods_init(&smods, 0);

    rc = slapi_entry_first_attr(e, &attr);
    while (rc == 0) {
        if (NULL != (va = attr_get_present_values(attr))) {
            slapi_attr_get_type(attr, &type);
            slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD, type, va);
        }
        rc = slapi_entry_next_attr(e, attr, &attr);
    }

#if !defined(USE_OLD_UNHASHED)
    if (SLAPD_UNHASHED_PW_ON == config_get_unhashed_pw_switch()) {
        /* store unhashed passwd is enabled */
        /* In case USE_OLD_UNHASHED, unhashed pw is already in mods */
        /* add extension to mods */
        rc = slapi_pw_get_entry_ext((Slapi_Entry *)e, &va);
        if (LDAP_SUCCESS == rc) {
            /* va is copied and set to smods */
            slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD,
                                      PSEUDO_ATTR_UNHASHEDUSERPASSWORD, va);
        }
    }
#endif

    *attrs = slapi_mods_get_ldapmods_passout(&smods);
    slapi_mods_done(&smods);

    return 0;
}

/******************************************************************************
*
*  normalize_mods2bvals
*
*  Return value: normalized mods
*  The values/bvals are all duplicated in this function since
*  the normalized mods are freed with ldap_mods_free by the caller.
*
*******************************************************************************/

LDAPMod **
normalize_mods2bvals(const LDAPMod **mods)
{
    int w, x, vlen, num_values, num_mods;
    LDAPMod **normalized_mods;

    if (mods == NULL) {
        return NULL;
    }

    /* first normalize the mods so they are bvalues */
    /* count the number of mods -- sucks but should be small */
    num_mods = 1;
    for (w = 0; mods[w] != NULL; w++)
        num_mods++;

    normalized_mods = (LDAPMod **)slapi_ch_calloc(num_mods, sizeof(LDAPMod *));

    for (w = 0; mods[w] != NULL; w++) {
        Slapi_Attr a = {0};
        slapi_attr_init(&a, mods[w]->mod_type);
        int is_dn_syntax = 0;
        struct berval **normmbvp = NULL;

        /* Check if the type of the to-be-added values has DN syntax
         * or not. */
        if (slapi_attr_is_dn_syntax_attr(&a)) {
            is_dn_syntax = 1;
        }
        attr_done(&a);

        /* copy each mod into a normalized modbvalue */
        normalized_mods[w] = (LDAPMod *)slapi_ch_calloc(1, sizeof(LDAPMod));
        normalized_mods[w]->mod_op = mods[w]->mod_op | LDAP_MOD_BVALUES;

        normalized_mods[w]->mod_type = slapi_ch_strdup(mods[w]->mod_type);

        /*
         * count the number of values -- kinda sucks but probably
         * less expensive then reallocing, and num_values
         * should typically be very small
         */
        num_values = 0;
        if (mods[w]->mod_op & LDAP_MOD_BVALUES) {
            for (x = 0; mods[w]->mod_bvalues != NULL &&
                        mods[w]->mod_bvalues[x] != NULL;
                 x++) {
                num_values++;
            }
        } else {
            for (x = 0; mods[w]->mod_values != NULL &&
                        mods[w]->mod_values[x] != NULL;
                 x++) {
                num_values++;
            }
        }

        if (num_values > 0) {
            normalized_mods[w]->mod_bvalues = (struct berval **)
                slapi_ch_calloc(num_values + 1, sizeof(struct berval *));
        } else {
            normalized_mods[w]->mod_bvalues = NULL;
        }

        if (mods[w]->mod_op & LDAP_MOD_BVALUES) {
            struct berval **mbvp = NULL;

            for (mbvp = mods[w]->mod_bvalues,
                 normmbvp = normalized_mods[w]->mod_bvalues;
                 normmbvp && mbvp && *mbvp; mbvp++, normmbvp++)
            {
                if (is_dn_syntax) {
                    Slapi_DN *sdn = slapi_sdn_new_dn_byref((*mbvp)->bv_val);
                    if (slapi_sdn_get_dn(sdn)) {
                        *normmbvp =
                            (struct berval *)slapi_ch_malloc(sizeof(struct berval));
                        (*normmbvp)->bv_val =
                            slapi_ch_strdup(slapi_sdn_get_dn(sdn));
                        (*normmbvp)->bv_len = slapi_sdn_get_ndn_len(sdn);
                    } else {
                        /* normalization failed; use the original */
                        *normmbvp = ber_bvdup(*mbvp);
                    }
                    slapi_sdn_free(&sdn);
                } else {
                    *normmbvp = ber_bvdup(*mbvp);
                }
            }
        } else {
            char **mvp = NULL;

            for (mvp = mods[w]->mod_values,
                 normmbvp = normalized_mods[w]->mod_bvalues;
                 normmbvp && mvp && *mvp; mvp++, normmbvp++)
            {
                vlen = strlen(*mvp);

                *normmbvp =
                    (struct berval *)slapi_ch_malloc(sizeof(struct berval));
                if (is_dn_syntax) {
                    Slapi_DN *sdn = slapi_sdn_new_dn_byref(*mvp);
                    if (slapi_sdn_get_dn(sdn)) {
                        (*normmbvp)->bv_val =
                            slapi_ch_strdup(slapi_sdn_get_dn(sdn));
                        (*normmbvp)->bv_len = slapi_sdn_get_ndn_len(sdn);
                    } else {
                        /* normalization failed; use the original */
                        (*normmbvp)->bv_val = slapi_ch_malloc(vlen + 1);
                        memcpy((*normmbvp)->bv_val, *mvp, vlen);
                        (*normmbvp)->bv_val[vlen] = '\0';
                        (*normmbvp)->bv_len = vlen;
                    }
                    slapi_sdn_free(&sdn);
                } else {
                    (*normmbvp)->bv_val = slapi_ch_malloc(vlen + 1);
                    memcpy((*normmbvp)->bv_val, *mvp, vlen);
                    (*normmbvp)->bv_val[vlen] = '\0';
                    (*normmbvp)->bv_len = vlen;
                }
            }
        }

        PR_ASSERT(normmbvp - normalized_mods[w]->mod_bvalues <= num_values);

        /* don't forget to null terminate it */
        if (num_values > 0 && normmbvp) {
            *normmbvp = NULL;
        }
    }

    /* don't forget to null terminate the normalize list of mods */
    normalized_mods[w] = NULL;

    return (normalized_mods);
}

/*
 * Return true if the given path is an absolute path, false otherwise
 */
int
is_abspath(const char *path)
{
    if (path == NULL || *path == '\0') {
        return 0; /* empty path is not absolute? */
    }

    if (path[0] == '/') {
        return 1; /* unix abs path */
    }

    return 0; /* not an abs path */
}

static void
clean_path(char **norm_path)
{
    char **np;

    for (np = norm_path; np && *np; np++)
        slapi_ch_free_string(np);
    slapi_ch_free((void **)&norm_path);
}

static char **
normalize_path(char *path)
{
    char *dname = NULL;
    char *dnamep = NULL;
    char **dirs = NULL;
    char **rdirs = NULL;
    char **dp = NULL;
    char **rdp;
    int elimdots = 0;

    if (NULL == path || '\0' == *path) {
        return NULL;
    }

    dirs = (char **)slapi_ch_calloc(strlen(path), sizeof(char *));
    rdirs = (char **)slapi_ch_calloc(strlen(path), sizeof(char *));

    dp = dirs;
    dname = slapi_ch_strdup(path);
    do {
        dnamep = strrchr(dname, _CSEP);
        if (NULL == dnamep) {
            dnamep = dname;
        } else {
            *dnamep = '\0';
            dnamep++;
        }
        if (0 != strcmp(dnamep, ".") && strlen(dnamep) > 0) {
            *dp++ = slapi_ch_strdup(dnamep); /* rm "/./" and "//" in the path */
        }
    } while (dnamep > dname /* == -> no more _CSEP */);
    slapi_ch_free_string(&dname);

    /* remove "xxx/.." in the path */
    for (dp = dirs, rdp = rdirs; dp && *dp; dp++) {
        while (*dp && 0 == strcmp(*dp, "..")) {
            dp++;
            elimdots++;
        }
        if (elimdots > 0) {
            elimdots--;
        } else if (*dp) {
            *rdp++ = slapi_ch_strdup(*dp);
        }
    }
    /* reverse */
    for (--rdp, dp = rdirs; rdp >= dp && rdp >= rdirs; --rdp, dp++) {
        char *tmpp = *dp;
        *dp = *rdp;
        *rdp = tmpp;
    }

    clean_path(dirs);
    return rdirs;
}

/*
 * Take "relpath" and prepend the current working directory to it
 * if it isn't an absolute pathname already.  The caller is responsible
 * for freeing the returned string.
 */
char *
rel2abspath(char *relpath)
{
    return rel2abspath_ext(relpath, NULL);
}

char *
rel2abspath_ext(char *relpath, char *cwd)
{
    char abspath[MAXPATHLEN + 1];
    char *retpath = NULL;

    if (relpath == NULL) {
        return NULL;
    }

    if (relpath[0] == _CSEP) { /* absolute path */
        PR_snprintf(abspath, sizeof(abspath), "%s", relpath);
    } else { /* relative path */
        if (NULL == cwd) {
            if (getcwd(abspath, MAXPATHLEN) == NULL) {
                perror("getcwd");
                slapi_log_err(SLAPI_LOG_ERR, "rel2abspath_ext", "Cannot determine current directory\n");
                exit(1);
            }
        } else {
            PR_snprintf(abspath, sizeof(abspath), "%s", cwd);
        }

        if (strlen(relpath) + strlen(abspath) + 1 > MAXPATHLEN) {
            slapi_log_err(SLAPI_LOG_ERR, "rel2abspath_ext", "Pathname \"%s" _PSEP "%s\" too long\n",
                          abspath, relpath);
            exit(1);
        }

        if (strcmp(relpath, ".")) {
            if (abspath[0] != '\0' &&
                abspath[strlen(abspath) - 1] != _CSEP) {
                PL_strcatn(abspath, sizeof(abspath), _PSEP);
            }
            PL_strcatn(abspath, sizeof(abspath), relpath);
        }
    }
    retpath = slapi_ch_strdup(abspath);
    /* if there's no '.' or separators, no need to call normalize_path */
    if (NULL != strchr(abspath, '.') || NULL != strstr(abspath, _PSEP)) {
        char **norm_path = normalize_path(abspath);
        char **np, *rp;
        int pathlen = strlen(abspath) + 1;
        int usedlen = 0;
        for (np = norm_path, rp = retpath; np && *np; np++) {
            int thislen = strlen(*np) + 1;
            if (0 != strcmp(*np, _PSEP))
                PR_snprintf(rp, pathlen - usedlen, "%c%s", _CSEP, *np);
            rp += thislen;
            usedlen += thislen;
        }
        clean_path(norm_path);
    }
    return retpath;
}


/*
 * Allocate a buffer large enough to hold a berval's
 * value and a terminating null byte. The returned buffer
 * is null-terminated. Returns NULL if bval is NULL or if
 * bval->bv_val is NULL.
 */
char *
slapi_berval_get_string_copy(const struct berval *bval)
{
    char *return_value = NULL;
    if (NULL != bval && NULL != bval->bv_val) {
        return_value = slapi_ch_malloc(bval->bv_len + 1);
        memcpy(return_value, bval->bv_val, bval->bv_len);
        return_value[bval->bv_len] = '\0';
    }
    return return_value;
}

/* Prints an advice to errors log when cert is not found.
   It may happen when nsSSLPersonalitySSL value is different from actual NSS DB content*/
void
slapd_cert_not_found_error_help(char *cert_name)
{
    char *certdir = config_get_certdir();
    slapi_log_err(SLAPI_LOG_ERR, "slapd_cert_not_found_error_help",
                  "Please, make sure that nsSSLPersonalitySSL value is correctly set to the certificate from"
                  " NSS database (currently, nsSSLPersonalitySSL attribute is set to '%1$s'). You can get the certificate list"
                  " for your NSS DB by running 'certutil -L -d \"%2$s\"' command. Then, you can either set nsSSLPersonalitySSL attribute"
                  " to the correct certificate from the list, either you can change the certificate Nickname in your NSS DB"
                  " by running 'certutil -d \"%2$s\" --rename -n \"OLD_NICKNAME\" --new-n \"%1$s\" '\n",
                  cert_name, certdir);
    slapi_ch_free_string(&certdir);
}

/* Takes a return code supposed to be errno or from a plugin
   which we don't expect to see and prints a handy log message */
void
slapd_nasty(char *str, int c, int err)
{
    char *msg = NULL;
    char buffer[100];
    PR_snprintf(buffer, sizeof(buffer), "%s BAD %d", str, c);
    slapi_log_err(SLAPI_LOG_ERR, "%s, err=%d %s\n", buffer, err, (msg = strerror(err)) ? msg : "");
}

/* ***************************************************
    Random function (very similar to rand_r())
   *************************************************** */
int
slapi_rand_r(unsigned int *randx)
{
    if (*randx) {
        PK11_RandomUpdate(randx, sizeof(*randx));
    }
    PK11_GenerateRandom((unsigned char *)randx, (int)sizeof(*randx));
    return (int)(*randx & 0x7FFFFFFF);
}

/* ***************************************************
    Random function (very similar to rand_r() but takes and returns an array)
    Note: there is an identical function in plugins/pwdstorage/ssha_pwd.c.
    That module can't use a libslapd function because the module is included
    in libds_admin, which doesn't link to libslapd. Eventually, shared
    functions should be moved to a shared library.
   *************************************************** */
void
slapi_rand_array(void *randx, size_t len)
{
    PK11_RandomUpdate(randx, len);
    PK11_GenerateRandom((unsigned char *)randx, (int)len);
}

/* ***************************************************
    Random function (very similar to rand()...)
   *************************************************** */
int
slapi_rand()
{
    unsigned int randx = 0;
    return slapi_rand_r(&randx);
}


/************************************************************************
Function:    DS_Sleep(PRIntervalTime ticks)

Purpose:    To replace PR_Sleep()

Author:        Scott Hopson <shopson@netscape.com>

Description:
        Causes the current thread to wait for ticks number of intervals.

        In UNIX this is accomplished by using select()
        which should be supported across all UNIX platforms.

************************************************************************/
void
DS_Sleep(PRIntervalTime ticks)
{
    long mSecs = PR_IntervalToMilliseconds(ticks);
    struct timeval tm;

    tm.tv_sec = mSecs / 1000;
    tm.tv_usec = (mSecs % 1000) * 1000;

    select(0, NULL, NULL, NULL, &tm);
}


/*****************************************************************************
 * strarray2str(): convert the array of strings in "a" into a single
 * space-separated string like:
 *        str1 str2 str3
 * If buf is too small, the result will be truncated and end with "...".
 * If include_quotes is non-zero, double quote marks are included around
 * the string, e.g.,
 *        "str2 str2 str3"
 *
 * Returns: 0 if completely successful and -1 if result is truncated.
 */
int
strarray2str(char **a, char *buf, size_t buflen, int include_quotes)
{
    int rc = 0; /* optimistic */
    char *p = buf;
    size_t totlen = 0;


    if (include_quotes) {
        if (buflen < 3) {
            return -1; /* not enough room for the quote marks! */
        }
        *p++ = '"';
        ++totlen;
    }

    if (NULL != a) {
        int ii;
        size_t len = 0;
        for (ii = 0; a[ii] != NULL; ii++) {
            if (ii > 0) {
                *p++ = ' ';
                totlen++;
            }
            len = strlen(a[ii]);
            if (totlen + len > buflen - 5) {
                strcpy(p, "...");
                p += 3;
                totlen += 3;
                rc = -1;
                break; /* result truncated */
            } else {
                strcpy(p, a[ii]);
                p += len;
                totlen += len;
            }
        }
    }

    if (include_quotes) {
        *p++ = '"';
        ++totlen;
    }
    buf[totlen] = '\0';

    return (rc);
}
/*****************************************************************************/

/* Changes the ownership of the given file/directory if not
   already the owner
   Returns 0 upon success or non-zero otherwise, usually -1 if
   some system error occurred
*/
int
slapd_chown_if_not_owner(const char *filename, uid_t uid, gid_t gid)
{
    int fd = -1;
    struct stat statbuf = {0};
    int result = 1;
    if (!filename) {
        return result;
    }

    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return result;
    }
    if (!(result = fstat(fd, &statbuf))) {
        if (((uid != -1) && (uid != statbuf.st_uid)) ||
            ((gid != -1) && (gid != statbuf.st_gid))) {
            result = fchown(fd, uid, gid);
        }
    }

    close(fd);
    return result;
}

/*
 * Compare 2 paths
 * Paths could contain ".", "..", "//" in the path, thus normalize them first.
 * One or two of the paths could be a relative path.
 */
int
slapd_comp_path(char *p0, char *p1)
{
    int rval = 0;
    char *norm_p0;
    char *norm_p1;

    /*
     * Neither path should be NULL, but it's possible when bad thing happen.
     * Return 0 which triggers an error in the caller
     */
    if (p0 == NULL || p1 == NULL){
        return 0;
    }

    norm_p0 = rel2abspath(p0);
    norm_p1 = rel2abspath(p1);
    rval = strcmp(norm_p0, norm_p1);
    slapi_ch_free_string(&norm_p0);
    slapi_ch_free_string(&norm_p1);
    return rval;
}

/*
  Takes an unsigned char value and converts it to a hex string.
  The string s is written, and the caller must ensure s has enough
  space.  For hex numbers, the upper argument says to use a-f or A-F.
  The return value is the address of the next char after the last one written.
*/
char *
slapi_u8_to_hex(uint8_t val, char *s, uint8_t upper)
{
    static char ldigits[] = "0123456789abcdef";
    static char udigits[] = "0123456789ABCDEF";
    char *digits;

    if (upper) {
        digits = udigits;
    } else {
        digits = ldigits;
    }
    s[0] = digits[val >> 4];
    s[1] = digits[val & 0xf];
    return &s[2];
}

char *
slapi_u16_to_hex(uint16_t val, char *s, uint8_t upper)
{
    static char ldigits[] = "0123456789abcdef";
    static char udigits[] = "0123456789ABCDEF";
    char *digits;

    if (upper) {
        digits = udigits;
    } else {
        digits = ldigits;
    }
    s[0] = digits[val >> 12];
    s[1] = digits[(val >> 8) & 0xf];
    s[2] = digits[(val >> 4) & 0xf];
    s[3] = digits[val & 0xf];
    return &s[4];
}

char *
slapi_u32_to_hex(uint32_t val, char *s, uint8_t upper)
{
    static char ldigits[] = "0123456789abcdef";
    static char udigits[] = "0123456789ABCDEF";
    char *digits;

    if (upper) {
        digits = udigits;
    } else {
        digits = ldigits;
    }
    s[0] = digits[val >> 28];
    s[1] = digits[(val >> 24) & 0xf];
    s[2] = digits[(val >> 20) & 0xf];
    s[3] = digits[(val >> 16) & 0xf];
    s[4] = digits[(val >> 12) & 0xf];
    s[5] = digits[(val >> 8) & 0xf];
    s[6] = digits[(val >> 4) & 0xf];
    s[7] = digits[val & 0xf];
    return &s[8];
}

char *
slapi_u64_to_hex(uint64_t val, char *s, uint8_t upper)
{
    static char ldigits[] = "0123456789abcdef";
    static char udigits[] = "0123456789ABCDEF";
    char *digits;

    if (upper) {
        digits = udigits;
    } else {
        digits = ldigits;
    }
    s[0] = digits[val >> 60];
    s[1] = digits[(val >> 56) & 0xf];
    s[2] = digits[(val >> 52) & 0xf];
    s[3] = digits[(val >> 48) & 0xf];
    s[4] = digits[(val >> 44) & 0xf];
    s[5] = digits[(val >> 40) & 0xf];
    s[6] = digits[(val >> 36) & 0xf];
    s[7] = digits[(val >> 32) & 0xf];
    s[8] = digits[(val >> 28) & 0xf];
    s[9] = digits[(val >> 24) & 0xf];
    s[10] = digits[(val >> 20) & 0xf];
    s[11] = digits[(val >> 16) & 0xf];
    s[12] = digits[(val >> 12) & 0xf];
    s[13] = digits[(val >> 8) & 0xf];
    s[14] = digits[(val >> 4) & 0xf];
    s[15] = digits[val & 0xf];
    return &s[16];
}

static const int char2intarray[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

int
slapi_hexchar2int(char c)
{
    return char2intarray[(unsigned char)c];
}

uint8_t
slapi_str_to_u8(const char *s)
{
    uint8_t v0 = (uint8_t)slapi_hexchar2int(s[0]);
    uint8_t v1 = (uint8_t)slapi_hexchar2int(s[1]);
    return (v0 << 4) | v1;
}

uint16_t
slapi_str_to_u16(const char *s)
{
    uint16_t v0 = (uint16_t)slapi_hexchar2int(s[0]);
    uint16_t v1 = (uint16_t)slapi_hexchar2int(s[1]);
    uint16_t v2 = (uint16_t)slapi_hexchar2int(s[2]);
    uint16_t v3 = (uint16_t)slapi_hexchar2int(s[3]);
    return (v0 << 12) | (v1 << 8) | (v2 << 4) | v3;
}

uint32_t
slapi_str_to_u32(const char *s)
{
    uint32_t v0 = (uint32_t)slapi_hexchar2int(s[0]);
    uint32_t v1 = (uint32_t)slapi_hexchar2int(s[1]);
    uint32_t v2 = (uint32_t)slapi_hexchar2int(s[2]);
    uint32_t v3 = (uint32_t)slapi_hexchar2int(s[3]);
    uint32_t v4 = (uint32_t)slapi_hexchar2int(s[4]);
    uint32_t v5 = (uint32_t)slapi_hexchar2int(s[5]);
    uint32_t v6 = (uint32_t)slapi_hexchar2int(s[6]);
    uint32_t v7 = (uint32_t)slapi_hexchar2int(s[7]);
    return (v0 << 28) | (v1 << 24) | (v2 << 20) | (v3 << 16) | (v4 << 12) | (v5 << 8) | (v6 << 4) | v7;
}

uint64_t
slapi_str_to_u64(const char *s)
{
    uint64_t v0 = (uint64_t)slapi_hexchar2int(s[0]);
    uint64_t v1 = (uint64_t)slapi_hexchar2int(s[1]);
    uint64_t v2 = (uint64_t)slapi_hexchar2int(s[2]);
    uint64_t v3 = (uint64_t)slapi_hexchar2int(s[3]);
    uint64_t v4 = (uint64_t)slapi_hexchar2int(s[4]);
    uint64_t v5 = (uint64_t)slapi_hexchar2int(s[5]);
    uint64_t v6 = (uint64_t)slapi_hexchar2int(s[6]);
    uint64_t v7 = (uint64_t)slapi_hexchar2int(s[7]);
    uint64_t v8 = (uint64_t)slapi_hexchar2int(s[8]);
    uint64_t v9 = (uint64_t)slapi_hexchar2int(s[9]);
    uint64_t v10 = (uint64_t)slapi_hexchar2int(s[10]);
    uint64_t v11 = (uint64_t)slapi_hexchar2int(s[11]);
    uint64_t v12 = (uint64_t)slapi_hexchar2int(s[12]);
    uint64_t v13 = (uint64_t)slapi_hexchar2int(s[13]);
    uint64_t v14 = (uint64_t)slapi_hexchar2int(s[14]);
    uint64_t v15 = (uint64_t)slapi_hexchar2int(s[15]);
    return (v0 << 60) | (v1 << 56) | (v2 << 52) | (v3 << 48) | (v4 << 44) | (v5 << 40) |
           (v6 << 36) | (v7 << 32) | (v8 << 28) | (v9 << 24) | (v10 << 20) | (v11 << 16) |
           (v12 << 12) | (v13 << 8) | (v14 << 4) | v15;
}

/* PR_GetLibraryName does almost everything we need, and unfortunately
   a little bit more - it adds "lib" to be beginning of the library
   name if the library name does not end with the current platform
   DLL suffix - so
   foo.so -> /path/foo.so
   libfoo.so -> /path/libfoo.so
   BUT
   foo -> /path/libfoo.so
   libfoo -> /path/liblibfoo.so
   /path/libfoo -> lib/path/libfoo.so
*/
char *
slapi_get_plugin_name(const char *path, const char *lib)
{
    const char *libstr = "/lib";
    size_t libstrlen = 4;
    char *fullname = PR_GetLibraryName(path, lib);
    char *ptr = PL_strrstr(fullname, lib);

    /* see if /lib was added */
    /* This check is ridiculous and hard to comprehend ... */
    if (ptr && ((ptr - fullname) >= (int)libstrlen)) {
        /* ptr is at the libname in fullname, and there is something before it */
        ptr -= libstrlen; /* ptr now points at the "/" in "/lib" if it is there */
        if (0 == PL_strncmp(ptr, libstr, libstrlen)) {
            /* just copy the remainder of the string on top of here */
            ptr++; /* ptr now points at the "l" in "/lib" - keep the "/" */
            memmove(ptr, ptr + 3, strlen(ptr + 3) + 1);
        }
    } else if ((NULL == path) && (0 == strncmp(fullname, "lib", 3))) {
        /*
         * case: /path/libfoo -> lib/path/libfoo.so
         * remove "lib".
         */
        memmove(fullname, fullname + 3, strlen(fullname) - 2);
    }

    return fullname;
}

/*
 * Check if rdn is a slecial rdn/dn or not.
 * If flag is IS_TOMBSTONE, returns 1 if rdn is a tombstone rdn/dn.
 * If flag is IS_CONFLICT, returns 1 if rdn is a conflict rdn/dn.
 * Otherwise returns 0.
 */
static int util_uniqueidlen = 0;
int
slapi_is_special_rdn(const char *rdn, int flag)
{
    char *rp;
    int plus = 0;
    if (!util_uniqueidlen) {
        util_uniqueidlen = SLAPI_ATTR_UNIQUEID_LENGTH + slapi_uniqueIDSize() + 1 /*=*/;
    }

    if ((RDN_IS_TOMBSTONE != flag) && (RDN_IS_CONFLICT != flag)) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_is_special_rdn", "Invalid flag %d\n", flag);
        return 0; /* not a special rdn/dn */
    }
    if (!rdn) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_is_special_rdn", "NULL rdn\n");
        return 0; /* not a special rdn/dn */
    }

    if (strlen(rdn) < (size_t)util_uniqueidlen) {
        return 0; /* not a special rdn/dn */
    }
    rp = (char *)rdn;
    while (rp) {
        char *endp = NULL;
        if (!PL_strncasecmp(rp, SLAPI_ATTR_UNIQUEID, SLAPI_ATTR_UNIQUEID_LENGTH) &&
            (*(rp + SLAPI_ATTR_UNIQUEID_LENGTH) == '=')) {
            if (RDN_IS_TOMBSTONE == flag) {
                if ((*(rp + util_uniqueidlen) == ',') ||
                    (*(rp + util_uniqueidlen) == '\0')) {
                    return 1;
                } else {
                    return 0;
                }
            } else {
                if ((*(rp + util_uniqueidlen) == '+') ||
                    (plus && ((*(rp + util_uniqueidlen) == ',') ||
                              (*(rp + util_uniqueidlen) == '\0')))) {
                    return 1;
                }
            }
        } else if (RDN_IS_TOMBSTONE == flag) {
            /* If the first part of rdn does not start with SLAPI_ATTR_UNIQUEID,
             * it's not a tombstone RDN. */
            return 0;
        }
        endp = PL_strchr(rp, ',');
        if (!endp) {
            endp = rp + strlen(rp);
        }
        rp = PL_strchr(rp, '+');
        if (rp && (rp < endp)) {
            plus = 1;
            rp++;
        }
    }
    return 0;
}

int
slapi_uniqueIDRdnSize(void)
{
    if (!util_uniqueidlen) {
        util_uniqueidlen = SLAPI_ATTR_UNIQUEID_LENGTH + slapi_uniqueIDSize() + 1 /*=*/;
    }
    return util_uniqueidlen;
}

util_cachesize_result
util_is_cachesize_sane(slapi_pal_meminfo *mi, uint64_t *cachesize)
{
    /* Check we have a valid meminfo struct */
    if (mi->system_available_bytes == 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "util_is_cachesize_sane", "Invalid system memory info, can not proceed.");
        return UTIL_CACHESIZE_ERROR;
    }

    util_cachesize_result result = UTIL_CACHESIZE_VALID;
    slapi_log_err(SLAPI_LOG_TRACE, "util_is_cachesize_sane", "Available bytes %" PRIu64 ", requested bytes %" PRIu64 "\n", mi->system_available_bytes, *cachesize);
    if (*cachesize > mi->system_available_bytes) {
        /* Since we are ask for more than what's available, we give 1/2 of the remaining.
         * the remaining system mem to the cachesize instead, and log a warning
         */
        uint64_t adjust_cachesize = (mi->system_available_bytes * 0.5);
        if (adjust_cachesize > *cachesize) {
            slapi_log_err(SLAPI_LOG_CRIT, "util_is_cachesize_sane", "Invalid adjusted cachesize is greater than request %" PRIu64, adjust_cachesize);
            return UTIL_CACHESIZE_ERROR;
        }
        if (adjust_cachesize < (16 * mi->pagesize_bytes)) {
            /* At minimum respond with 16 pages - that's 64k on x86_64 */
            adjust_cachesize = 16 * mi->pagesize_bytes;
        }
        *cachesize = adjust_cachesize;
        slapi_log_err(SLAPI_LOG_TRACE, "util_is_cachesize_sane", "Adjusted cachesize down to %" PRIu64 "\n", *cachesize);
        result = UTIL_CACHESIZE_REDUCED;
    }
    return result;
}

long
util_get_capped_hardware_threads(long min, long max)
{
    long threads = min;
#ifdef LINUX
    /* A new version that rely on sched_getaffinity rather than on sysconf
     * to take in account the affinity
     */
    cpu_set_t cs = {0};
    sched_getaffinity(0, sizeof(cs), &cs);
    threads = CPU_COUNT(&cs);
    if (threads == 0) {
        threads = sysconf(_SC_NPROCESSORS_ONLN);
	}
    slapi_log_err(SLAPI_LOG_TRACE, "util_get_hardware_threads",
             "Detected %ld hardware threads\n", threads);

    if (threads < min){
        if (threads == -1) {
            /* sysconf failed, use MIN_THREADS */
            slapi_log_err(SLAPI_LOG_ERR, "util_get_hardware_threads",
                    "Failed to get hardware threads.  Error (%d) %s\n",
                    errno, slapd_system_strerror(errno));
        } else if (threads == 0) {
            /* sysconf failed to find any processors, use MIN_THREADS */
            slapi_log_err(SLAPI_LOG_ERR, "util_get_hardware_threads",
                    "Cannot detect any hardware threads.\n");
        }
        threads = min;
    } else if (threads > max) {
        threads = max;
    }
#else
    slapi_log_err(SLAPI_LOG_ERR, "util_get_hardware_threads",
            "ERROR: Cannot detect hardware threads on this platform. This is probably a bug!\n");
    /* Can't detect threads on this platform!  Return the default thread number */
#endif
    return threads;
}

long
util_get_hardware_threads(void)
{
    /* Cap at 512 for now ... */
    return util_get_capped_hardware_threads(MIN_THREADS, MAX_THREADS);
}

void
slapi_create_errormsg(
    char *errorbuf,
    size_t len,
    const char *fmt,
    ...)
{
    if (errorbuf) {
        va_list ap;
        va_start(ap, fmt);
        (void)PR_vsnprintf(errorbuf, len ? len - 1 : sizeof(errorbuf) - 1, fmt, ap);
        va_end(ap);
    }
}

void
get_internal_conn_op (uint64_t *connid, int32_t *op_id, int32_t *op_internal_id, int32_t *op_nested_count) {
    struct slapi_td_log_op_state_t *op_state = slapi_td_get_log_op_state();

    if (op_state != NULL) {
        *connid = op_state->conn_id;
        *op_id = op_state->op_id;
        *op_internal_id = op_state->op_int_id;
        *op_nested_count = op_state->op_nest_count;

    } else {
        *connid = 0;
        *op_id = 0;
        *op_internal_id = 0;
        *op_nested_count = 0;
    }
}

/*
 * Extract a single value from an entry (as a string) -- if it's not in the
 * entry, the default will be returned (which can be NULL).  You do not need
 * to free the returned string value.
 */
const char *
slapi_fetch_attr(Slapi_Entry *e, char *attrname, char *default_val)
{
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;

    if (slapi_entry_attr_find(e, attrname, &attr) != 0)
        return default_val;
    slapi_attr_first_value(attr, &val);
    return slapi_value_get_string(val);
}

char
get_sep(char *path)
{
    if (NULL == path)
        return '/'; /* default */
    if (NULL != strchr(path, '/'))
        return '/';
    if (NULL != strchr(path, '\\'))
        return '\\';
    return '/'; /* default */
}

/* mkdir -p */
int
mkdir_p(char *dir, unsigned int mode)
{
    PRFileInfo64 info;
    int rval;
    char sep = get_sep(dir);

    rval = PR_GetFileInfo64(dir, &info);
    if (PR_SUCCESS == rval) {
        if (PR_FILE_DIRECTORY != info.type) /* not a directory */
        {
            PR_Delete(dir);
            if (PR_SUCCESS != PR_MkDir(dir, mode)) {
                slapi_log_err(SLAPI_LOG_ERR, "mkdir_p", "%s: error %d (%s)\n",
                              dir, PR_GetError(), slapd_pr_strerror(PR_GetError()));
                return -1;
            }
        }
        return 0;
    } else {
        /* does not exist */
        char *p, *e;
        char c[2] = {0, 0};
        int len = strlen(dir);
        rval = 0;

        e = dir + len - 1;
        if (*e == sep) {
            c[1] = *e;
            *e = '\0';
        }

        c[0] = '/';
        p = strrchr(dir, sep);
        if (NULL != p) {
            *p = '\0';
            rval = mkdir_p(dir, mode);
            *p = c[0];
        }
        if (c[1])
            *e = c[1];
        if (0 != rval)
            return rval;
        if (PR_SUCCESS != PR_MkDir(dir, mode)) {
            slapi_log_err(SLAPI_LOG_ERR, "mkdir_p", "%s: error %d (%s)\n",
                          dir, PR_GetError(), slapd_pr_strerror(PR_GetError()));
            return -1;
        }
        return 0;
    }
}


/*
 * Standard openldap ldif_getline function does modify the input buffer in place
 * which leads to SIGSEGV if buffer is directly comming from mdb data
 *  (because the database memory is write protected). So lets have a read only version.
 */
const char *ldif_getline_ro( const char **next)
{
    const char *line = NULL;
    const char *pt = *next;

    do {
        if ( pt == NULL || *pt == '\n' || *pt == '\0' ) {
            *next = NULL;
            return( line );
        }

        line = pt;
        while ( (pt = strchr( pt, '\n' )) != NULL && *++pt == ' ') {
            pt ++;  /* skip continuation line leading space */
        }
    } while( *line == '#' );
    *next = pt;
    return( line );
}

/*
 * Duplicate the contents of an ldif "line" generated by ldif_getline_ro
 * removing \r or \n and leading space marking continuation on next line
 *  copy->bv_val may get realloc if copy->bv_len is too small)
 * Note: copy->bv_len is the buffer size the line lenght is smaller
 *  and marked by \0
 */
void dup_ldif_line(struct berval *copy, const char *line, const char *endline)
{
    const char *ptend = endline ? endline : line + strlen(line) + 1;
    char *buf = copy->bv_val;
    const char *pt = line;
    int copylen = 0;
    int pos = 0;

    /* Realloc the buffer if it is too small */
    if (buf == NULL ||  (ptend - line) > copy->bv_len) {
        buf = copy->bv_val = slapi_ch_realloc(buf, ptend - line);
        copy->bv_len = (ptend - line);
    }

    /* endline was computed by ldif_getline_ro and is either the start of next line or the end of string */
    PR_ASSERT( endline == NULL || *endline == 0 || endline[-1] == '\n' );

    while (pt && pt < ptend) {
        line = pt;
        /* Search end of line */
		while (pt < ptend && *pt != '\n' && *pt != 0) {
			pt++;
		}
        PR_ASSERT(pt <= ptend);
        copylen = pt - line;
        if (copylen>0 && line[copylen-1] == '\r') {
            copylen--;
        }
        if (copylen == 0) {
            break;
        }
        if (*line == '#' && pos == 0) {
            pt++;
            continue;
        }
        strncpy(buf+pos, line, copylen);
        pos += copylen;
        pt += 2;        /* Skip \n and continuation line space */
    }
    buf[pos] = 0;
    copy->bv_len = copylen;
}
