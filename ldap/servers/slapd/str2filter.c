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

/* slapi_str2filter.c - parse an rfc 1588 string filter */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

static struct slapi_filter *
str2list(char *str, unsigned long ftype);
static int
str2subvals(char *val, struct slapi_filter *f, int unescape_filter);

struct slapi_filter *
slapi_str2filter(char *str)
{
    struct slapi_filter *f = NULL;
    char *end;

    slapi_log_err(SLAPI_LOG_FILTER, "slapi_str2filter", "\"%s\"\n", str);

    if (str == NULL || *str == '\0') {
        return (NULL);
    }

    switch (*str) {
    case '(':
        if ((end = slapi_find_matching_paren(str)) == NULL) {
            slapi_filter_free(f, 1);
            return (NULL);
        }
        *end = '\0';

        str++;
        switch (*str) {
        case '&':
            slapi_log_err(SLAPI_LOG_FILTER, "slapi_str2filter", "AND\n");

            str++;
            f = str2list(str, LDAP_FILTER_AND);
            break;

        case '|':
            slapi_log_err(SLAPI_LOG_FILTER, "slapi_str2filter", "OR\n");

            str++;
            f = str2list(str, LDAP_FILTER_OR);
            break;

        case '!':
            slapi_log_err(SLAPI_LOG_FILTER, "slapi_str2filter", "NOT\n");

            str++;
            f = str2list(str, LDAP_FILTER_NOT);
            break;

        default:
            slapi_log_err(SLAPI_LOG_FILTER, "slapi_str2filter", "simple\n");

            f = str2simple(str, 1 /* unescape_filter */);
            break;
        }
        *end = ')';
        break;

    default: /* assume it's a simple type=value filter */
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_str2filter", "default\n");

        f = str2simple(str, 1 /* unescape_filter */);
        break;
    }

    return (f);
}

/*
 * Put a list of filters like this "(filter1)(filter2)..."
 */

static struct slapi_filter *
str2list(char *str, unsigned long ftype)
{
    struct slapi_filter *f;
    struct slapi_filter **fp;
    char *next;
    char save;

    slapi_log_err(SLAPI_LOG_FILTER, "str2list", "\"%s\"\n", str);

    f = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));
    f->f_choice = ftype;
    fp = &f->f_list;

    while (*str) {
        while (*str && ldap_utf8isspace(str))
            LDAP_UTF8INC(str);
        if (*str == '\0')
            break;

        if ((next = slapi_find_matching_paren(str)) == NULL) {
            slapi_filter_free(f, 1);
            return (NULL);
        }
        save = *++next;
        *next = '\0';

        /* now we have "(filter)" with str pointing to it */
        if ((*fp = slapi_str2filter(str)) == NULL) {
            slapi_filter_free(f, 1);
            *next = save;
            return (NULL);
        }
        *next = save;

        str = next;
        f->f_flags |= ((*fp)->f_flags & SLAPI_FILTER_LDAPSUBENTRY);
        f->f_flags |= ((*fp)->f_flags & SLAPI_FILTER_TOMBSTONE);
        f->f_flags |= ((*fp)->f_flags & SLAPI_FILTER_RUV);
        fp = &(*fp)->f_next;
    }
    *fp = NULL;
    filter_compute_hash(f);

    return (f);
}

static char *
str_find_star(char *s)
/* Like strchr(s, '*'), except ignore "\*" */
{
    char *r;
    if (s == NULL)
        return s;
    r = strchr(s, '*');
    if (r != s)
        while (r != NULL && r[-1] == '\\') {
            r = strchr(r + 1, '*');
        }
    return r;
}


/*
 * unescape a string into another buffer -- note that an unescaped ldap
 * string may contain nulls if 'binary' is set!  this is sort of a mix
 * between the LDAP SDK version and terry hayes' version.  optimally it
 * would be nice if the LDAP SDK exported something like this.
 *
 * if 'binary' is set, "\00" is allowed, otherwise it's not.
 *
 * returns: 0 on error, 1 on success
 *          (*outlen) is the actual length of the unescaped string
 */
static int
filt_unescape_str(const char *instr, char *outstr, size_t outsize, size_t *outlen, int binary)
{
    const char *inp;
    char *outp;
    int ival;
    *outlen = 0;

    if (!outstr)
        return -1;
    for (inp = instr, outp = outstr; *inp; inp++) {
        if (!outsize)
            return 0; /* fail */
        if (*inp == '\\') {
            if (!*(++inp))
                return 0; /* fail */
            if (((ival = slapi_hexchar2int(inp[0])) < 0) || (slapi_hexchar2int(inp[1]) < 0)) {
                /* LDAPv2 (RFC1960) escape sequence */
                *outp++ = *inp;
                (*outlen)++, outsize--;
            } else {
                /* LDAPv3 hex escape sequence */
                if (!*(++inp))
                    return 0; /* fail */
                *outp = (ival << 4) | slapi_hexchar2int(*inp);
                if ((!binary) && (!*outp))
                    return 0; /* fail: "\00" not allowed unless it's binary */
                outp++;
                (*outlen)++, outsize--;
            }
        } else {
            *outp++ = *inp;
            (*outlen)++, outsize--;
        }
    }
    return 1; /* ok */
}

/*
 * Return value: 0 -- success
 *             : 1 -- failure
 */
static int
_parse_ext_filter(char *str, char *p0, char **type, char **oid, char *dnAttrs)
{
    char *p1 = NULL;
    char *p2 = NULL;

    if (!type || !oid || !dnAttrs) {
        return 1;
    }
    *type = NULL;
    *oid = NULL;
    *dnAttrs = '\0';
    /*
     * RFC 4515 examples
     * 1 - (cn:caseExactMatch:=Fred Flintstone)
     * 2 - (cn:=Betty Rubble)
     * 3 - (sn:dn:2.4.6.8.10:=Barney Rubble)
     * 4 - (o:dn:=Ace Industry)
     * 5 - (:1.2.3:=Wilma Flintstone)
     * 6 - (:DN:2.4.6.8.10:=Dino)
     */
    p1 = strchr(p0 + 1, ':');
    if (p1) {
        p2 = strchr(p1 + 1, ':');
    }
    if (p0 == str) {
        *type = slapi_ch_strdup(""); /* no type */
        if (p2) {
            /* example 6 */
            *p1 = *p2 = '\0';
            if (strcasecmp(p0 + 1, "dn") == 0) {
                *dnAttrs = -1;
                *oid = slapi_ch_strdup(p1 + 1);
            } else {
                goto error;
            }
        } else if (p1) {
            /* example 5 */
            *p1 = '\0';
            if (strcasecmp(p0 + 1, "dn") == 0) {
                *dnAttrs = -1;
            } else {
                *oid = slapi_ch_strdup(p0 + 1);
            }
        } else {
            goto error;
        }
    } else if (p2) {
        /* example 3 */
        *p0 = *p1 = *p2 = '\0';
        *type = slapi_ch_strdup(str);
        if (strcasecmp(p0 + 1, "dn") == 0) {
            *dnAttrs = -1;
            *oid = slapi_ch_strdup(p1 + 1);
        } else {
            goto error;
        }
    } else if (p1) {
        *p0 = *p1 = '\0';
        *type = slapi_ch_strdup(str);
        if (strcasecmp(p0 + 1, "dn") == 0) {
            /* example 4 */
            *dnAttrs = -1;
        } else {
            /* example 1 */
            *oid = slapi_ch_strdup(p0 + 1);
        }
    } else {
        /* example 2 */
        *type = slapi_ch_strdup(str);
        *p0 = '\0';
    }
    if (p1) {
        *p1 = ':';
    }
    if (p2) {
        *p2 = ':';
    }
    return 0;

error:
    slapi_ch_free_string(type);
    slapi_ch_free_string(oid);
    if (p1) {
        *p1 = ':';
    }
    if (p2) {
        *p2 = ':';
    }
    return 1;
}

/*
 *  The caller unescapes it if unescape_filter  == 0.
 */
struct slapi_filter *
str2simple(char *str, int unescape_filter)
{
    struct slapi_filter *f;
    char *s;
    char *value, savechar;
    char *extp = NULL;

    slapi_log_err(SLAPI_LOG_FILTER, "str2simple", "\"%s\"\n", str);

    PR_ASSERT(str);

    if ((s = strchr(str, '=')) == NULL) {
        return (NULL);
    }
    value = s;
    LDAP_UTF8INC(value);
    LDAP_UTF8DEC(s);

    f = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));

    switch (*s) {
    case '<':
        f->f_choice = LDAP_FILTER_LE;
        break;
    case '>':
        f->f_choice = LDAP_FILTER_GE;
        break;
    case '~':
        f->f_choice = LDAP_FILTER_APPROX;
        break;
    default:
        LDAP_UTF8INC(s);
        if ((extp = strchr(str, ':')) && (extp < value)) {
            int rc;
            char *endp = s; /* '=' */
            *endp = '\0';
            rc = _parse_ext_filter(str, extp, &f->f_mr_type, &f->f_mr_oid, &f->f_mr_dnAttrs);
            if (rc) {
                slapi_filter_free(f, 1);
                return NULL; /* error */
            } else {
                f->f_choice = LDAP_FILTER_EXTENDED;
                if (f->f_mr_oid) {
                    /* apply the MR indexers */
                    rc = plugin_mr_filter_create(&f->f_mr);
                    if (rc) {
                        slapi_filter_free(f, 1);
                        return NULL; /* error */
                    }
                }
            }
        } else if (str_find_star(value) == NULL) {
            f->f_choice = LDAP_FILTER_EQUALITY;
        } else if (strcmp(value, "*") == 0) {
            f->f_choice = LDAP_FILTER_PRESENT;
        } else {
            f->f_choice = LDAP_FILTER_SUBSTRINGS;
            savechar = *s;
            *s = 0;
            f->f_sub_type = slapi_ch_strdup(str);
            *s = savechar;
            if (str2subvals(value, f, unescape_filter) != 0) {
                slapi_filter_free(f, 1);
                return (NULL);
            }
            filter_compute_hash(f);
            return (f);
        }
        break;
    }

    if (f->f_choice == LDAP_FILTER_PRESENT) {
        savechar = *s;
        *s = 0;
        f->f_type = slapi_ch_strdup(str);
        *s = savechar;
    } else if (unescape_filter) {
        int r;
        char *unqstr;
        size_t len = strlen(value), len2;

        /* dup attr */
        savechar = *s;
        *s = 0;
        if (f->f_choice != LDAP_FILTER_EXTENDED) {
            f->f_avtype = slapi_ch_strdup(str);
        }
        *s = savechar;

        /* dup value */
        savechar = value[len];
        value[len] = 0;
        unqstr = slapi_ch_calloc(1, len + 1);
        r = filt_unescape_str(value, unqstr, len, &len2, 1);
        value[len] = savechar;
        if (!r) {
            slapi_filter_free(f, 1);
            slapi_ch_free((void **)&unqstr);
            return NULL;
        }
        f->f_avvalue.bv_val = unqstr;
        f->f_avvalue.bv_len = len2;

        if ((f->f_choice == LDAP_FILTER_EQUALITY) &&
            (0 == strncasecmp(str, "objectclass", strlen("objectclass")))) {
            if (0 == strcasecmp(unqstr, "ldapsubentry"))
                f->f_flags |= SLAPI_FILTER_LDAPSUBENTRY;
            if (0 == strcasecmp(unqstr, SLAPI_ATTR_VALUE_TOMBSTONE))
                f->f_flags |= SLAPI_FILTER_TOMBSTONE;
        }

        if ((f->f_choice == LDAP_FILTER_EQUALITY) &&
            (0 == strncasecmp(str, "nsuniqueid", strlen("nsuniqueid")))) {
            if (0 == strcasecmp(unqstr, "ffffffff-ffffffff-ffffffff-ffffffff"))
                f->f_flags |= SLAPI_FILTER_RUV;
        }

    } else if (!unescape_filter) {
        if (f->f_choice != LDAP_FILTER_EXTENDED) {
            f->f_avtype = slapi_ch_strdup(str);
        }
        f->f_avvalue.bv_val = slapi_ch_strdup(value);
        f->f_avvalue.bv_len = strlen(f->f_avvalue.bv_val);
    }

    filter_compute_hash(f);
    return (f);
}

static int
str2subvals(char *val, struct slapi_filter *f, int unescape_filter)
{
    char *nextstar, *unqval;
    int gotstar;
    size_t len, outlen;

    slapi_log_err(SLAPI_LOG_FILTER, "str2subvals", "\"%s\"\n", val);

    gotstar = 0;
    while (val != NULL && *val) {
        if ((nextstar = str_find_star(val)) != NULL)
            *nextstar = '\0';

        if (unescape_filter) {
            len = strlen(val);
            unqval = slapi_ch_malloc(len + 1);
            if (!filt_unescape_str(val, unqval, len, &outlen, 0)) {
                slapi_ch_free((void **)&unqval);
                return -1;
            }
            unqval[outlen] = '\0';
        } else {
            unqval = slapi_ch_strdup(val);
        }
        if (unqval && unqval[0]) {
            if (gotstar == 0) {
                f->f_sub_initial = unqval;
            } else if (nextstar == NULL) {
                f->f_sub_final = unqval;
            } else {
                charray_add(&f->f_sub_any, unqval);
            }
        } else {
            slapi_ch_free((void **)&unqval);
        }

        gotstar = 1;
        if (nextstar != NULL)
            *nextstar++ = '*';
        val = nextstar;
    }

    return (0);
}

/*
 * find_matching_paren - return a pointer to the right paren in s matching
 * the left paren to which *s currently points
 */

char *
slapi_find_matching_paren(const char *s)
{
    int balance, escape;

    balance = 0;
    escape = 0;
    for (; *s; s++) {
        if (escape == 0) {
            if (*s == '(')
                balance++;
            else if (*s == ')')
                balance--;
        }
        if (balance == 0) {
            return ((char *)s);
        }
        if (*s == '\\' && !escape)
            escape = 1;
        else
            escape = 0;
    }

    return (NULL);
}
