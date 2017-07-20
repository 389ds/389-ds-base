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

/* value.c - routines for dealing with values */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

/*
 * Do not use the SDK ldap_utf8isspace directly until it is faster
 * than this one.
 */
static int
utf8isspace_fast(char *s)
{
    register unsigned char c = *(unsigned char *)s;
    if (0x80 & c)
        return (ldap_utf8isspace(s));
    switch (c) {
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case 0x20:
        return 1;
    default:
        break;
    }
    return 0;
}

/*
** This function is used to normalizes search filter components,
** and attribute values.
**
** jcm: I added the trim_spaces flag since this function
** was incorrectly modifying search filter components.  A search
** of the form "cn=a* b*" (note the space) would be wrongly
** normalized into "cn=a*b*", because this function is called
** once for "a" and once for " b".
** richm 20070917 - added integer syntax - note that this implementation
** of integer syntax tries to mimic the old implementation (atol) as much
** as possible - leading spaces are ignored, then the optional hyphen for
** negative numbers, then leading 0s.  That is
** "    -0000000000001" should normalize to "-1" which is what atol() does
** Also note that this deviates from rfc 4517 INTEGER syntax, but we must
** support legacy clients for the time being
*/
/*
 * alt stores the normalized value in case the normalized value is longer
 * than the original value.  It may happen the value is DN.
 */
void
value_normalize_ext(
    char *s,
    int syntax,
    int trim_spaces,
    char **alt)
{
    char *head = s;
    char *d;
    int prevspace, curspace;

    if (NULL == alt) {
        return;
    }
    *alt = NULL;

    if (NULL == s) {
        return;
    }

    if (!(syntax & SYNTAX_CIS) && !(syntax & SYNTAX_CES)) {
        return;
    }

    if (syntax & SYNTAX_DN) {
        char *dest = NULL;
        size_t dlen = 0;
        int rc = slapi_dn_normalize_case_ext(s, 0, &dest, &dlen);
        if (rc > 0) {
            *alt = dest;
        } else if (rc == 0) { /* normalized in line; not terminated */
            *(dest + dlen) = '\0';
        }
        return;
    }

    d = s;
    if (trim_spaces) {
        /* strip leading blanks */
        while (utf8isspace_fast(s)) {
            LDAP_UTF8INC(s);
        }
    }

    /* for int syntax, look for leading sign, then trim 0s */
    /* have to do this after trimming spaces */
    if (syntax & SYNTAX_INT) {
        int foundsign = 0;
        int foundzero = 0;

        if (*s == '-') {
            foundsign = 1;
            LDAP_UTF8INC(s);
        }

        while (*s && (*s == '0')) {
            foundzero = 1;
            LDAP_UTF8INC(s);
        }

        if (foundzero && !*s) { /* value is all zeros */
            *d++ = '0';         /* set value to a single zero */
        } else if (foundsign && (s > d)) {
            /* if there is a hyphen, make sure it is just to the left
               of the first significant (i.e. non-zero) digit e.g.
               convert -00000001 to -1 */
            *d++ = '-';
        }
        /* s should now point at the first significant digit/char */
    }

    /* handle value of all spaces - turn into single space */
    /* unless space insensitive syntax or int - turn into zero length string */
    if (*s == '\0' && s != d) {
        if (!(syntax & SYNTAX_SI) && !(syntax & SYNTAX_INT)) {
            *d++ = ' ';
        }
        *d = '\0';
        return;
    }
    prevspace = 0;
    while (*s) {
        curspace = utf8isspace_fast(s);

        /* ignore spaces and '-' in telephone numbers */
        if ((syntax & SYNTAX_TEL) && (curspace || *s == '-')) {
            LDAP_UTF8INC(s);
            continue;
        }

        /* ignore all spaces if this is a space insensitive value */
        if ((syntax & SYNTAX_SI) && curspace) {
            LDAP_UTF8INC(s);
            continue;
        }

        /* compress multiple blanks */
        if (prevspace && curspace) {
            LDAP_UTF8INC(s);
            continue;
        }
        prevspace = curspace;
        if (syntax & SYNTAX_CIS) {
            int ssz, dsz;
            slapi_utf8ToLower((unsigned char *)s, (unsigned char *)d, &ssz, &dsz);
            s += ssz;
            d += dsz;
        } else {
            char *np;
            int sz;

            np = ldap_utf8next(s);
            if (np == NULL || np == s)
                break;
            sz = np - s;
            memmove(d, s, sz);
            d += sz;
            s += sz;
        }
    }
    *d = '\0';
    /* strip trailing blanks */
    if (prevspace && trim_spaces) {
        char *nd;

        nd = ldap_utf8prev(d);
        while (nd && nd >= head && utf8isspace_fast(nd)) {
            d = nd;
            nd = ldap_utf8prev(d);
            *d = '\0';
        }
    }
}

void
value_normalize(
    char *s __attribute__((unused)),
    int syntax __attribute__((unused)),
    int trim_spaces __attribute__((unused)))
{
    /* deprecated */
}

int
value_cmp(
    struct berval *v1,
    struct berval *v2,
    int syntax,
    int normalize)
{
    int rc = 0;
    struct berval bvcopy1;
    struct berval bvcopy2;
    char little_buffer[64];
    size_t buffer_space = sizeof(little_buffer);
    int buffer_offset = 0;
    int free_v1 = 0;
    int free_v2 = 0;
    int v1sign = 1, v2sign = 1; /* default to positive */
    char *alt = NULL;

    // check NULL values before normalization
    if (!v1->bv_val) {
        if (v2->bv_val)
            rc = -1;
        goto done;
    }
    if (!v2->bv_val) {
        rc = 1;
        goto done;
    }

    /* This code used to call malloc up to four times in the copying
     * of attributes to be normalized. Now we attempt to keep everything
     * on the stack and only malloc if the data is big
     */
    if (normalize & 1) {
        /* Do we have space in the little buffer ? */
        if (v1->bv_len < buffer_space) {
            bvcopy1.bv_len = v1->bv_len;
            SAFEMEMCPY(&little_buffer[buffer_offset], v1->bv_val, v1->bv_len);
            bvcopy1.bv_val = &little_buffer[buffer_offset];
            bvcopy1.bv_val[v1->bv_len] = '\0';
            v1 = &bvcopy1;
        } else {
            v1 = ber_bvdup(v1);
            free_v1 = 1;
        }
        value_normalize_ext(v1->bv_val, syntax,
                            1 /* trim leading blanks */, &alt);
        if (alt) {
            int inserted = 0;

            if (free_v1) {
                slapi_ch_free_string(&v1->bv_val);
                v1->bv_val = alt;
                v1->bv_len = strlen(alt);
                inserted = 1;
            } else {
                if (strlen(alt) < buffer_space) {
                    v1->bv_len = strlen(alt);
                    /* Copying to little_buffer */
                    SAFEMEMCPY(v1->bv_val, alt, v1->bv_len);
                    *(v1->bv_val + v1->bv_len) = '\0';
                } else {
                    free_v1 = 1;
                    v1 = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
                    v1->bv_val = alt;
                    v1->bv_len = strlen(alt);
                    inserted = 1;
                }
            }
            if (!inserted) {
                slapi_ch_free_string(&alt);
            }
        }
        if (!free_v1) {
            buffer_space -= v1->bv_len + 1;
            buffer_offset += v1->bv_len + 1;
        }
    }
    if (normalize & 2) {
        /* Do we have space in the little buffer ? */
        if (v2->bv_len < buffer_space) {
            bvcopy2.bv_len = v2->bv_len;
            SAFEMEMCPY(&little_buffer[buffer_offset], v2->bv_val, v2->bv_len);
            bvcopy2.bv_val = &little_buffer[buffer_offset];
            bvcopy2.bv_val[v2->bv_len] = '\0';
            v2 = &bvcopy2;
        } else {
            v2 = ber_bvdup(v2);
            free_v2 = 1;
        }
        value_normalize_ext(v2->bv_val, syntax,
                            1 /* trim leading blanks */, &alt);
        if (alt) {
            int inserted = 0;

            if (free_v2) {
                slapi_ch_free_string(&v2->bv_val);
                v2->bv_val = alt;
                v2->bv_len = strlen(alt);
                inserted = 1;
            } else {
                if (strlen(alt) < buffer_space) {
                    v2->bv_len = strlen(alt);
                    /* Copying to little_buffer */
                    SAFEMEMCPY(v2->bv_val, alt, v2->bv_len);
                    *(v2->bv_val + v2->bv_len) = '\0';
                } else {
                    free_v2 = 1;
                    v2 = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
                    v2->bv_val = alt;
                    v2->bv_len = strlen(alt);
                    inserted = 1;
                }
            }
            if (!inserted) {
                slapi_ch_free_string(&alt);
            }
        }
        if (!free_v2) {
            buffer_space -= v2->bv_len + 1;
            buffer_offset += v2->bv_len + 1;
        }
    }

    if (normalize) {
        // check NULL values after normalization
        if (!v1->bv_val) {
            if (v2->bv_val)
                rc = -1;
            goto done;
        }
        if (!v2->bv_val) {
            rc = 1;
            goto done;
        }
    }

    if (syntax & SYNTAX_INT) {
        v1sign = *v1->bv_val != '-';
        v2sign = *v2->bv_val != '-';
        rc = v1sign - v2sign;
        if (rc) { /* one is positive, one is negative */
            goto done;
        }

        /* check magnitude */
        /* unfortunately, bv_len cannot be trusted - bv_len is not
           updated during or after value_normalize */
        rc = (strlen(v1->bv_val) - strlen(v2->bv_val));
        if (rc) {
            rc = (rc > 0) ? 1 : -1;
            if (!v1sign && !v2sign) { /* both negative */
                rc = 0 - rc;          /* flip it */
            }
            goto done;
        }
    }

    if (syntax & SYNTAX_CIS) {
        rc = slapi_utf8casecmp((unsigned char *)v1->bv_val,
                               (unsigned char *)v2->bv_val);
    } else if (syntax & SYNTAX_CES) {
        rc = strcmp(v1->bv_val, v2->bv_val);
    } else { /* error - unknown syntax */
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM,
                      "value_cmp - invalid syntax [%d]\n", syntax);
    }

    if ((syntax & SYNTAX_INT) && !v1sign && !v2sign) { /* both negative */
        rc = 0 - rc;                                   /* flip it */
    }

done:
    if ((normalize & 1) && free_v1) {
        ber_bvfree(v1);
    }
    if ((normalize & 2) && free_v2) {
        ber_bvfree(v2);
    }

    return (rc);
}
