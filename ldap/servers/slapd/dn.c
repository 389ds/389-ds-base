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

/* dn.c - routines for dealing with distinguished names */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include "slap.h"
#include <plhash.h>

#ifdef RUST_ENABLE
#include <rust-slapi-private.h>
#else
/* For the ndn cache - this gives up siphash13 */
#include <sds.h>
#endif

#undef SDN_DEBUG

static void add_rdn_av(char *avstart, char *avend, int *rdn_av_countp, struct berval **rdn_avsp, struct berval *avstack);
static void reset_rdn_avs(struct berval **rdn_avsp, int *rdn_av_countp);
static void sort_rdn_avs(struct berval *avs, int count, int escape);
static int rdn_av_cmp(struct berval *av1, struct berval *av2);
static void rdn_av_swap(struct berval *av1, struct berval *av2, int escape);
static int does_cn_uses_dn_syntax_in_dns(char *type, char *dn);

/* normalized dn cache related definitions*/
struct ndn_cache_stats {
    Slapi_Counter *cache_hits;
    Slapi_Counter *cache_tries;
    Slapi_Counter *cache_count;
    Slapi_Counter *cache_size;
    Slapi_Counter *cache_evicts;
    uint64_t max_size;
    uint64_t thread_max_size;
    uint64_t slots;
};

#ifndef RUST_ENABLE
struct ndn_cache_value {
    uint64_t size;
    uint64_t slot;
    char *dn;
    char *ndn;
    struct ndn_cache_value *next;
    struct ndn_cache_value *prev;
    struct ndn_cache_value *child;
};
#endif

/*
 * This uses a similar alloc trick to IDList to keep
 * The amount of derefs small.
 */
struct ndn_cache {
    /*
     * We keep per thread stats and flush them occasionally
     */
#ifdef RUST_ENABLE
    ARCacheChar *cache;
#else
    /* Need to track this because we need to provide diffs to counter */
    uint64_t last_count;
    uint64_t count;
    /* Number of ops */
    uint64_t tries;
    /* hit vs miss. in theroy miss == tries - hits.*/
    uint64_t hits;
    /* How many values we kicked out */
    uint64_t evicts;
    /* Need to track this because we need to provide diffs to counter */
    uint64_t last_size;
    uint64_t size;
    uint64_t slots;
    /* The per-thread max size */
    uint64_t max_size;
    /*
     * This is used by siphash to prevent hash bucket attacks
     */
    char key[16];

    struct ndn_cache_value *head;
    struct ndn_cache_value *tail;
    struct ndn_cache_value *table[1];
#endif
};

/*
 * This means we need 1 MB minimum per thread
 * 
 */
#define NDN_CACHE_MINIMUM_CAPACITY 1048576
/*
 * This helps us define the number of hashtable slots
 * to create. We assume an average DN is 64 chars long
 * This way we end up we a ht entry of:
 * 8 bytes: from the table pointing to us.
 * 8 bytes: next ptr
 * 8 bytes: prev ptr
 * 8 bytes + 64: dn
 * 8 bytes + 64: ndn itself.
 * This gives us 168 bytes. In theory this means
 * 6241 entries, but we have to clamp this to a power of
 * two, so we have 8192 slots. In reality, dns may be
 * shorter *and* the dn may be the same as the ndn
 * so we *may* store more ndns that this. Again, a good reason
 * to round the ht size up!
 */
#define NDN_ENTRY_AVG_SIZE 168
/*
 * After how many operations do we sync our per-thread stats.
 */
#define NDN_STAT_COMMIT_FREQUENCY 256

static int ndn_cache_lookup(char *dn, size_t dn_len, char **result, char **udn, int *rc);
static void ndn_cache_add(char *dn, size_t dn_len, char *ndn, size_t ndn_len);

#define ISBLANK(c) ((c) == ' ')
#define ISBLANKSTR(s) (((*(s)) == '2') && (*((s) + 1) == '0'))
#define ISSPACE(c) (ISBLANK(c) || ((c) == '\n') || ((c) == '\r')) /* XXX 518524 */

#define ISEQUAL(c) ((c) == '=')
#define ISEQUALSTR(s) \
    ((*(s) == '3') && ((*((s) + 1) == 'd') || (*((s) + 1) == 'D')))

#define ISPLUS(c) ((c) == '+')
#define ISPLUSSTR(s) \
    ((*(s) == '2') && ((*((s) + 1) == 'b') || (*((s) + 1) == 'B')))

#define ISESCAPE(c) ((c) == '\\')
#define ISQUOTE(c) ((c) == '"')

#define DNSEPARATOR(c) (((c) == ',') || ((c) == ';'))
#define DNSEPARATORSTR(s)                                               \
    (((*(s) == '2') && ((*((s) + 1) == 'c') || (*((s) + 1) == 'C'))) || \
     ((*(s) == '3') && ((*((s) + 1) == 'b') || (*((s) + 1) == 'B'))))

#define SEPARATOR(c) (DNSEPARATOR(c) || ISPLUS(c))
#define SEPARATORSTR(s) (DNSEPARATORSTR(s) || ISPLUSSTR(s))

#define NEEDSESCAPE(c) (ISESCAPE(c) || ISQUOTE(c) || SEPARATOR(c) || \
                        ((c) == '<') || ((c) == '>') || ISEQUAL(c))
#define NEEDSESCAPESTR(s)                                                \
    (((*(s) == '2') && ((*((s) + 1) == '2') ||                           \
                        (*((s) + 1) == 'b') || (*((s) + 1) == 'B') ||    \
                        (*((s) + 1) == 'c') || (*((s) + 1) == 'C'))) ||  \
     ((*(s) == '3') && (((*((s) + 1) >= 'b') && (*((s) + 1) < 'f')) ||   \
                        ((*((s) + 1) >= 'B') && (*((s) + 1) < 'F')))) || \
     ((*(s) == '5') && ((*((s) + 1) == 'c') || (*((s) + 1) == 'C'))))

#define LEADNEEDSESCAPE(c) (ISBLANK(c) || ((c) == '#') || NEEDSESCAPE(c))
#define LEADNEEDSESCAPESTR(s) (NEEDSESCAPESTR(s) || \
                               ((*(s) == '2') && (*((s) + 1) == '3')))

#define ISCLOSEBRACKET(c) (((c) == ')') || ((c) == ']'))

#define MAYBEDN(eq) (                 \
    (eq) && ((eq) != subtypestart) && \
    ((eq) != subtypestart + strlen(subtypestart) - 3))

#define B4TYPE 0
#define INTYPE 1
#define B4EQUAL 2
#define B4VALUE 3
#define INVALUE 4
#define INQUOTEDVALUE 5
#define B4SEPARATOR 6
#define INVALUE1ST 7
#define INQUOTEDVALUE1ST 8

#define SLAPI_DNNORM_INITIAL_RDN_AVS 10
#define SLAPI_DNNORM_SMALL_RDN_AV 512

/*
 * substr_dn_normalize - map a DN to a canonical form.
 * The DN is read from *dn through *(end-1) and normalized in place.
 * The new end is returned; that is, the canonical form is in
 * *dn through *(the_return_value-1).
 */

/* The goals of this function are:
 * 1. be compatible with previous implementations.  Especially, enable
 *    a server running this code to find database index keys that were
 *    computed by Directory Server 3.0 with a prior version of this code.
 * 2. Normalize in place; that is, avoid allocating memory to contain
 *    the canonical form.
 * 3. eliminate insignificant differences; that is, any two DNs are
 *    not significantly different if and only if their canonical forms
 *    are identical (ignoring upper/lower case).
 * 4. handle a DN in the syntax defined by RFC 2253.
 * 5. handle a DN in the syntax defined by RFC 1779.
 *
 * Goals 3 through 5 are not entirely achieved by this implementation,
 * because it can't be done without violating goal 1.  Specifically,
 * DNs like cn="a,b" and cn=a\,b are not mapped to the same canonical form,
 * although they're not significantly different.  Likewise for any pair
 * of DNs that differ only in their choice of quoting convention.
 * A previous version of this code changed all DNs to the most compact
 * quoting convention, but that violated goal 1, since Directory Server
 * 3.0 did not.
 *
 * Also, this implementation handles the \xx convention of RFC 2253 and
 * consequently violates RFC 1779, according to which this type of quoting
 * would be interpreted as a sequence of 2 numerals (not a single byte).
 *
 * Finally, if the DN contains any RDNs that are multivalued, we sort
 * the values in the RDN(s) to help meet goal 3.  Ordering is based on a
 * case-insensitive comparison of the "attribute=value" pairs.
 *
 * This function does not support UTF-8 multi-byte encoding for attribute
 * values, in particular it does not support UTF-8 whitespace.  First the
 * ISSPACE macro above is limited, but also its frequent use of '-1' indexing
 * into a char[] may hit the middle of a multi-byte UTF-8 whitespace character
 * encoding (518524).
 */

char *
substr_dn_normalize_orig(char *dn, char *end)
{
    /* \xx is changed to \c.
     * \c is changed to c, unless this would change its meaning.
     * All values that contain 2 or more separators are "enquoted";
     * all other values are not enquoted.
     */
    char *value = NULL;
    char *value_separator = NULL;
    char *d = NULL;
    char *s = NULL;
    char *typestart = NULL;
    char *lastesc = NULL;
    int gotesc = 0;
    int state = B4TYPE;
    int rdn_av_count = 0;
    struct berval *rdn_avs = NULL;
    struct berval initial_rdn_av_stack[SLAPI_DNNORM_INITIAL_RDN_AVS];

    for (d = s = dn; s != end; s++) {
        switch (state) {
        case B4TYPE:
            if (!ISSPACE(*s)) {
                state = INTYPE;
                typestart = d;
                *d++ = *s;
            }
            break;
        case INTYPE:
            if (*s == '=') {
                state = B4VALUE;
                *d++ = *s;
            } else if (ISSPACE(*s)) {
                state = B4EQUAL;
            } else {
                *d++ = *s;
            }
            break;
        case B4EQUAL:
            if (*s == '=') {
                state = B4VALUE;
                *d++ = *s;
            } else if (!ISSPACE(*s)) {
                /* not a valid dn - but what can we do here? */
                *d++ = *s;
            }
            break;
        case B4VALUE:
            if (*s == '"' || !ISSPACE(*s)) {
                value_separator = NULL;
                value = d;
                state = (*s == '"') ? INQUOTEDVALUE : INVALUE1ST;
                lastesc = NULL;
                *d++ = *s;
            }
            break;
        case INVALUE1ST:
        case INVALUE:
            if (gotesc) {
                if (SEPARATOR(*s)) {
                    value_separator = d;
                }
                if (INVALUE1ST == state) {
                    if (!LEADNEEDSESCAPE(*s)) {
                        /* checking the leading char + special chars */
                        --d; /* eliminate the \ */
                    }
                } else if (!NEEDSESCAPE(*s)) {
                    --d; /* eliminate the \ */
                    lastesc = d;
                }
            } else if (SEPARATOR(*s)) {
                /* handling a trailing escaped space */
                /* assuming a space is the only an extra character which
                 * is not escaped if it appears in the middle, but should
                 * be if it does at the end of the RDN value */
                /* e.g., ou=ABC  \   ,o=XYZ --> ou=ABC  \ ,o=XYZ */
                if (lastesc) {
                    while (ISSPACE(*(d - 1)) && d > lastesc) {
                        d--;
                    }
                    if (d == lastesc) {
                        *d++ = '\\';
                        *d++ = ' '; /* escaped trailing space */
                    }
                } else {
                    while (ISSPACE(*(d - 1))) {
                        d--;
                    }
                }
                if (value_separator == dn) { /* 2 or more separators */
                    /* convert to quoted value: */
                    char *L = NULL;       /* char after last seperator */
                    char *R;              /* value character iterator */
                    int escape_skips = 0; /* number of escapes we have seen after the first */

                    for (R = value; (R = strchr(R, '\\')) && (R < d); L = ++R) {
                        if (SEPARATOR(R[1])) {
                            if (L == NULL) {
                                /* executes once, at first escape, adds opening quote */
                                const size_t len = R - value;

                                /* make room for quote by covering escape */
                                if (len > 0) {
                                    memmove(value + 1, value, len);
                                }

                                *value = '"';  /* opening quote */
                                value = R + 1; /* move passed what has been parsed */
                            } else {
                                const size_t len = R - L;
                                if (len > 0) {
                                    /* remove the seperator */
                                    memmove(value, L, len);
                                    value += len; /* move passed what has been parsed */
                                }
                                --d;
                                ++escape_skips;
                            }
                        } /* if ( SEPARATOR( R[1] )) */
                    }     /* for */
                    memmove(value, L, d - L + escape_skips);
                    *d++ = '"'; /* closing quote */
                }               /* if (value_separator == dn) */
                state = B4TYPE;

                /*
                 * Track and sort attribute values within
                 * multivalued RDNs.
                 */
                if (*s == '+' || rdn_av_count > 0) {
                    add_rdn_av(typestart, d, &rdn_av_count,
                               &rdn_avs, initial_rdn_av_stack);
                }
                if (*s != '+') { /* at end of this RDN */
                    if (rdn_av_count > 1) {
                        sort_rdn_avs(rdn_avs, rdn_av_count, 0);
                    }
                    if (rdn_av_count > 0) {
                        reset_rdn_avs(&rdn_avs, &rdn_av_count);
                    }
                }

                *d++ = (*s == '+') ? '+' : ',';
                break;
            } /* else if ( SEPARATOR( *s ) ) */
            if (INVALUE1ST == state) {
                state = INVALUE;
            }
            *d++ = *s;
            break;
        case INQUOTEDVALUE:
            if (gotesc) {
                if (!NEEDSESCAPE(*s)) {
                    --d; /* eliminate the \ */
                }
            } else if (*s == '"') {
                state = B4SEPARATOR;
                if (!value) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "substr_dn_normalize_orig", "Missing value\n");
                    break;
                }
                if (value_separator == dn /* 2 or more separators */
                    || ISSPACE(value[1]) || ISSPACE(d[-1])) {
                    *d++ = *s;
                } else {
                    /* convert to non-quoted value: */
                    if (value_separator == NULL) { /* no separators */
                        memmove(value, value + 1, (d - value) - 1);
                        --d;
                    } else { /* 1 separator */
                        memmove(value, value + 1, (value_separator - value) - 1);
                        *(value_separator - 1) = '\\';
                    }
                }
                break;
            }
            if (SEPARATOR(*s)) {
                if (value_separator)
                    value_separator = dn;
                else
                    value_separator = d;
            }
            *d++ = *s;
            break;
        case B4SEPARATOR:
            if (SEPARATOR(*s)) {
                state = B4TYPE;

                /*
                 * Track and sort attribute values within
                 * multivalued RDNs.
                 */
                if (*s == '+' || rdn_av_count > 0) {
                    add_rdn_av(typestart, d, &rdn_av_count,
                               &rdn_avs, initial_rdn_av_stack);
                }
                if (*s != '+') { /* at end of this RDN */
                    if (rdn_av_count > 1) {
                        sort_rdn_avs(rdn_avs, rdn_av_count, 0);
                    }
                    if (rdn_av_count > 0) {
                        reset_rdn_avs(&rdn_avs, &rdn_av_count);
                    }
                }

                *d++ = (*s == '+') ? '+' : ',';
            }
            break;
        default:
            slapi_log_err(SLAPI_LOG_ERR,
                          "substr_dn_normalize_orig", "Unknown state %d\n", state);
            break;
        }
        if (*s == '\\') {
            if (gotesc) { /* '\\', again */
                /* <type>=XXX\\\\7AYYY; we should keep \\\\. */
                gotesc = 0;
            } else {
                gotesc = 1;
                if (s + 2 < end) {
                    int n = slapi_hexchar2int(s[1]);
                    if (n >= 0 && n < 16) {
                        int n2 = slapi_hexchar2int(s[2]);
                        if (n2 >= 0) {
                            n = (n << 4) + n2;
                            if (n == 0) { /* don't change \00 */
                                *d++ = *++s;
                                *d++ = *++s;
                                gotesc = 0;
                            } else { /* change \xx to a single char */
                                ++s;
                                *(unsigned char *)(s + 1) = n;
                            }
                        }
                    }
                }
            }
        } else {
            gotesc = 0;
        }
    }

    /*
     * Track and sort attribute values within multivalued RDNs.
     */
    /* We may still be in an unexpected state, such as B4TYPE if
     * we encountered something odd like a '+' at the end of the
     * rdn.  If this is the case, we don't want to add this bogus
     * rdn to our list to sort.  We should only be in the INVALUE
     * or B4SEPARATOR state if we have a valid rdn component to
     * be added. */
    if ((rdn_av_count > 0) && ((state == INVALUE1ST) ||
                               (state == INVALUE) || (state == B4SEPARATOR))) {
        add_rdn_av(typestart, d, &rdn_av_count,
                   &rdn_avs, initial_rdn_av_stack);
    }
    if (rdn_av_count > 1) {
        sort_rdn_avs(rdn_avs, rdn_av_count, 0);
    }
    if (rdn_av_count > 0) {
        reset_rdn_avs(&rdn_avs, &rdn_av_count);
    }
    /* Trim trailing spaces */
    while (d != dn && *(d - 1) == ' ')
        d--; /* XXX 518524 */

    return (d);
}

char *
substr_dn_normalize(char *dn __attribute__((unused)), char *end)
{
    /* no op */
    return end;
}

static int
ISEOV(char *s, char *ends)
{
    char *p;
    for (p = s; p && *p && p < ends; p++) {
        if (SEPARATOR(*p)) {
            return 1;
        } else if (!ISBLANK(*p)) {
            return 0; /* not the end of the value */
        }
    }
    return 1;
}

/*
 * 1) Escaped NEEDSESCAPE chars (e.g., ',', '<', '=', etc.) are converted to
 * ESC HEX HEX (e.g., \2C, \3C, \3D, etc.)
 * Input could be a string in double quotes
 * (= old DN format: dn: cn="x=x,y=y",... --> dn: cn=x\3Dx\2Cy\3Dy,...) or
 * an escaped string
 * (= new DN format dn: cn=x\=x\,y\=y,... -> dn: cn=x\3Dx\2Cy\3Dy,...)
 *
 * 2) All the other ESC HEX HEX are converted to the real characters.
 *
 * 3) Spaces around separator ',', ';', and '+' are removed.
 *
 * Input:
 * src: src DN
 * src_len: length of src; 0 is acceptable if src is NULL terminated.
 * Output:
 * dest: address of the converted string; NULL terminated
 *       (could store src address if no need to convert)
 * dest_len: length of dest
 *
 * Return values:
 *  0: nothing was done; dest is identical to src (src is passed in).
 *  1: successfully escaped; dest is different from src. src needs to be freed.
 * -1: failed; dest is NULL; invalid DN
 */
int
slapi_dn_normalize_ext(char *src, size_t src_len, char **dest, size_t *dest_len)
{
    int rc = -1;
    int state = B4TYPE;
    char *s = NULL; /* work pointer for src */
    char *d = NULL; /* work pointer for dest */
    char *ends = NULL;
    char *endd = NULL;
    char *lastesc = NULL;
    char *udn = NULL;
    /* rdn avs for the main DN */
    char *typestart = NULL;
    int rdn_av_count = 0;
    struct berval *rdn_avs = NULL;
    struct berval initial_rdn_av_stack[SLAPI_DNNORM_INITIAL_RDN_AVS];
    /* rdn avs for the nested DN */
    char *subtypestart = NULL; /* used for nested rdn avs */
    int subrdn_av_count = 0;
    struct berval *subrdn_avs = NULL;
    struct berval subinitial_rdn_av_stack[SLAPI_DNNORM_INITIAL_RDN_AVS];
    int chkblank = 0;
    int is_dn_syntax = 0;

    if ((NULL == dest) || (NULL == dest_len)) {
        goto bail;
    }
    if (NULL == src) {
        *dest = NULL;
        *dest_len = 0;
        goto bail;
    }
    if (0 == src_len) {
        src_len = strlen(src);
    }
    /*
     *  Check the normalized dn cache
     */
    if (ndn_cache_lookup(src, src_len, dest, &udn, &rc)) {
        *dest_len = strlen(*dest);
        return rc;
    }

    s = PL_strnchr(src, '\\', src_len);
    if (s) {
        *dest_len = src_len * 3;
        *dest = slapi_ch_malloc(*dest_len); /* max length */
        rc = 1;
    } else {
        s = PL_strnchr(src, '"', src_len);
        if (s) {
            *dest_len = src_len * 3;
            *dest = slapi_ch_malloc(*dest_len); /* max length */
            rc = 1;
        } else {
            *dest_len = src_len;
            *dest = src; /* just removing spaces around separators */
            rc = 0;
        }
    }
    if (0 == src_len) { /* src == "" */
        goto bail;      /* need to bail after setting up *dest and rc */
    }

    ends = src + src_len;
    endd = *dest + *dest_len;
    for (s = src, d = *dest; s < ends && d < endd;) {
        switch (state) {
        case B4TYPE: /* before type; cn=... */
                     /*             ^       */
            if (ISSPACE(*s)) {
                s++; /* skip leading spaces */
            } else {
                state = INTYPE;
                typestart = d;
                *d++ = *s++;
            }
            break;
        case INTYPE: /* in type; cn=... */
                     /*          ^      */
            if (ISEQUAL(*s)) {
                /* See if the type is defined to use
                 * the Distinguished Name syntax. */
                char savechar;

                /* We need typestart to be a string containing only
                 * the type.  We terminate the type and then reset
                 * the string after we check the syntax. */
                savechar = *d;
                *d = '\0';

                is_dn_syntax = slapi_attr_is_dn_syntax_type(typestart);

                /* Reset the character we modified. */
                *d = savechar;

                if (!is_dn_syntax) {
                    is_dn_syntax = does_cn_uses_dn_syntax_in_dns(typestart, src);
                }

                state = B4VALUE;
                *d++ = *s++;
            } else if (ISCLOSEBRACKET(*s)) { /* special care for ACL macro */
                /* See if the type is defined to use
                 * the Distinguished Name syntax. */
                char savechar;

                /* We need typestart to be a string containing only
                 * the type.  We terminate the type and then reset
                 * the string after we check the syntax. */
                savechar = *d;
                *d = '\0';

                is_dn_syntax = slapi_attr_is_dn_syntax_type(typestart);

                /* Reset the character we modified. */
                *d = savechar;

                if (!is_dn_syntax) {
                    is_dn_syntax = does_cn_uses_dn_syntax_in_dns(typestart, src);
                }

                state = INVALUE; /* skip a trailing space */
                *d++ = *s++;
            } else if (ISSPACE(*s)) {
                /* See if the type is defined to use
                 * the Distinguished Name syntax. */
                char savechar;

                /* We need typestart to be a string containing only
                 * the type.  We terminate the type and then reset
                 * the string after we check the syntax. */
                savechar = *d;
                *d = '\0';

                is_dn_syntax = slapi_attr_is_dn_syntax_type(typestart);

                /* Reset the character we modified. */
                *d = savechar;

                if (!is_dn_syntax) {
                    is_dn_syntax = does_cn_uses_dn_syntax_in_dns(typestart, src);
                }

                state = B4EQUAL; /* skip a trailing space */
            } else if (ISQUOTE(*s) || SEPARATOR(*s)) {
                /* type includes quote / separator; not a valid dn */
                rc = -1;
                goto bail;
            } else {
                *d++ = *s++;
            }
            break;
        case B4EQUAL: /* before equal; cn =... */
                      /*                 ^     */
            if (ISEQUAL(*s)) {
                state = B4VALUE;
                *d++ = *s++;
            } else if (ISSPACE(*s)) {
                s++; /* skip trailing spaces */
            } else {
                /* type includes spaces; not a valid dn */
                rc = -1;
                goto bail;
            }
            break;
        case B4VALUE: /* before value; cn= ABC */
                      /*                  ^    */
            if (ISSPACE(*s)) {
                s++;
            } else {
                if (ISQUOTE(*s)) {
                    s++; /* start with the first char in quotes */
                    state = INQUOTEDVALUE1ST;
                } else {
                    state = INVALUE1ST;
                }
                lastesc = NULL;
                /* process *s in INVALUE or INQUOTEDVALUE */
            }
            break;
        case INVALUE1ST:       /* 1st char in value; cn=ABC */
                               /*                       ^   */
            if (ISSPACE(*s)) { /* skip leading spaces */
                s++;
                continue;
            } else if (SEPARATOR(*s)) {
                /* 1st char in value is separator; invalid dn */
                rc = -1;
                goto bail;
            } /* otherwise, go through */
            if (!is_dn_syntax || ISESCAPE(*s)) {
                subtypestart = NULL; /* if escaped, can't be multivalued dn */
            } else {
                subtypestart = d; /* prepare for '+' in the nested DN, if any */
            }
            subrdn_av_count = 0;
            /* FALLTHRU */
        case INVALUE: /* in value; cn=ABC */
                      /*               ^  */
            if (ISESCAPE(*s)) {
                if (s + 1 >= ends) {
                    /* DN ends with '\'; invalid dn */
                    rc = -1;
                    goto bail;
                }
                if (((state == INVALUE1ST) && LEADNEEDSESCAPE(*(s + 1))) ||
                    ((state == INVALUE) && NEEDSESCAPE(*(s + 1)))) {
                    if (d + 2 >= endd) {
                        /* Not enough space for dest; this never happens! */
                        rc = -1;
                        goto bail;
                    } else {
                        if (ISEQUAL(*(s + 1)) && is_dn_syntax) {
                            while (ISSPACE(*(d - 1))) {
                                /* remove trailing spaces */
                                d--;
                            }
                        } else if (SEPARATOR(*(s + 1)) && is_dn_syntax) {
                            /* separator is a subset of needsescape */
                            while (ISSPACE(*(d - 1))) {
                                /* remove trailing spaces */
                                d--;
                                chkblank = 1;
                            }
                            if (chkblank && ISESCAPE(*(d - 1)) && ISBLANK(*d)) {
                                /* last space is escaped "cn=A\ ,ou=..." */
                                /*                             ^         */
                                PR_snprintf(d, 3, "%X", *d); /* hexpair */
                                d += 2;
                                chkblank = 0;
                            }
                            /*
                             * Track and sort attribute values within
                             * multivalued RDNs.
                             */
                            if (subtypestart &&
                                (ISPLUS(*(s + 1)) || subrdn_av_count > 0)) {
                                /* if subtypestart is not valid DN,
                                  * we do not do sorting.*/
                                char *p = PL_strcasestr(subtypestart, "\\3d");
                                if (MAYBEDN(p)) {
                                    add_rdn_av(subtypestart, d,
                                               &subrdn_av_count,
                                               &subrdn_avs,
                                               subinitial_rdn_av_stack);
                                } else {
                                    reset_rdn_avs(&subrdn_avs,
                                                  &subrdn_av_count);
                                    subtypestart = NULL;
                                }
                            }
                            if (!ISPLUS(*(s + 1))) { /* at end of this RDN */
                                if (subrdn_av_count > 1) {
                                    sort_rdn_avs(subrdn_avs,
                                                 subrdn_av_count, 1);
                                }
                                if (subrdn_av_count > 0) {
                                    reset_rdn_avs(&subrdn_avs,
                                                  &subrdn_av_count);
                                    subtypestart = NULL;
                                }
                            }
                        }
                        /* dn: cn=x\=x\,... -> dn: cn=x\3Dx\2C,... */
                        *d++ = *s++;                 /* '\\' */
                        PR_snprintf(d, 3, "%X", *s); /* hexpair */
                        d += 2;
                        if (ISPLUS(*s) && is_dn_syntax) {
                            /* next type start of multi values */
                            /* should not be a escape char AND should be
                             * followed by \\= or \\3D */
                            if ((PL_strnstr(s, "\\=", ends - s) ||
                                 PL_strncaserstr(s, "\\3D", ends - s))) {
                                subtypestart = d;
                            } else {
                                subtypestart = NULL;
                            }
                        }
                        if ((SEPARATOR(*s) || ISEQUAL(*s)) && is_dn_syntax) {
                            while (ISSPACE(*(s + 1)))
                                s++; /* remove leading spaces */
                            s++;
                        } else {
                            s++;
                        }
                    }
                } else if (((state == INVALUE1ST) &&
                            (s + 2 < ends) && LEADNEEDSESCAPESTR(s + 1)) ||
                           ((state == INVALUE) &&
                            (((s + 2 < ends) && NEEDSESCAPESTR(s + 1)) ||
                             (ISEOV(s + 3, ends) && ISBLANKSTR(s + 1))))) {
                    /* e.g., cn=abc\20 ,... */
                    /*             ^        */
                    if (ISEQUALSTR(s + 1) && is_dn_syntax) {
                        while (ISSPACE(*(d - 1))) {
                            /* remove trailing spaces */
                            d--;
                        }
                    } else if (SEPARATORSTR(s + 1) && is_dn_syntax) {
                        /* separator is a subset of needsescape */
                        while (ISSPACE(*(d - 1))) {
                            /* remove trailing spaces */
                            d--;
                            chkblank = 1;
                        }
                        if (chkblank && ISESCAPE(*(d - 1)) && ISBLANK(*d)) {
                            /* last space is escaped "cn=A\ ,ou=..." */
                            /*                             ^         */
                            PR_snprintf(d, 3, "%X", *d); /* hexpair */
                            d += 2;
                            chkblank = 0;
                        }
                        /*
                         * Track and sort attribute values within
                         * multivalued RDNs.
                         */
                        if (subtypestart &&
                            (ISPLUSSTR(s + 1) || subrdn_av_count > 0)) {
                            /* if subtypestart is not valid DN,
                             * we do not do sorting.*/
                            char *p = PL_strcasestr(subtypestart, "\\3d");
                            if (MAYBEDN(p)) {
                                add_rdn_av(subtypestart, d, &subrdn_av_count,
                                           &subrdn_avs, subinitial_rdn_av_stack);
                            } else {
                                reset_rdn_avs(&subrdn_avs, &subrdn_av_count);
                                subtypestart = NULL;
                            }
                        }
                        if (!ISPLUSSTR(s + 1)) { /* at end of this RDN */
                            if (subrdn_av_count > 1) {
                                sort_rdn_avs(subrdn_avs, subrdn_av_count, 1);
                            }
                            if (subrdn_av_count > 0) {
                                reset_rdn_avs(&subrdn_avs, &subrdn_av_count);
                                subtypestart = NULL;
                            }
                        }
                    }
                    *d++ = *s++; /* '\\' */
                    *d++ = *s++; /* HEX */
                    *d++ = *s++; /* HEX */
                    if (ISPLUSSTR(s - 2) && is_dn_syntax) {
                        /* next type start of multi values */
                        /* should not be a escape char AND should be followed
                         * by \\= or \\3D */
                        if (!ISESCAPE(*s) && (PL_strnstr(s, "\\=", ends - s) ||
                                              PL_strncaserstr(s, "\\3D", ends - s))) {
                            subtypestart = d;
                        } else {
                            subtypestart = NULL;
                        }
                    }
                    if ((SEPARATORSTR(s - 2) || ISEQUALSTR(s - 2)) && is_dn_syntax) {
                        while (ISSPACE(*s)) { /* remove leading spaces */
                            s++;
                        }
                    }
                } else if (s + 2 < ends && isxdigit(*(s + 1)) && isxdigit(*(s + 2))) {
                    /* esc hexpair ==> real character */
                    int n = slapi_hexchar2int(*(s + 1));
                    int n2 = slapi_hexchar2int(*(s + 2));
                    n = (n << 4) + n2;
                    if (n == 0) { /* don't change \00 */
                        *d++ = *++s;
                        *d++ = *++s;
                    } else if (n == 32) { /* leave \20 (space) intact */
                        *d++ = *s;
                        *d++ = *++s;
                        *d++ = *++s;
                        s++;
                    } else {
                        *d++ = n;
                        s += 3;
                    }
                } else {
                    /* ignore an escape for now */
                    lastesc = d; /* position of the previous escape */
                    s++;
                }
            } else if (SEPARATOR(*s)) { /* cn=ABC , ... */
                                        /*        ^     */
                /* handling a trailing escaped space */
                /* assuming a space is the only an extra character which
                 * is not escaped if it appears in the middle, but should
                 * be if it does at the end of the RDN value */
                /* e.g., ou=ABC  \   ,o=XYZ --> ou=ABC  \ ,o=XYZ */
                if (lastesc) {
                    while (ISSPACE(*(d - 1)) && d > lastesc) {
                        d--;
                    }
                    if (d == lastesc) {
                        /* esc hexpair of space: \20 */
                        *d++ = '\\';
                        *d++ = '2';
                        *d++ = '0';
                    }
                } else {
                    while (ISSPACE(*(d - 1))) {
                        d--;
                    }
                }
                state = B4SEPARATOR;
                break;
            } else if (ISSPACE(*s)) {
                /* remove extra spaces, e.g., "cn=ABC   DEF" --> "cn=ABC DEF" */
                *d++ = *s++;
                while (ISSPACE(*s))
                    s++;
            } else {
                *d++ = *s++;
            }
            if (state == INVALUE1ST) {
                state = INVALUE;
            }
            break;
        case INQUOTEDVALUE1ST:
            if (ISSPACE(*s) && (s + 1 < ends && ISSPACE(*(s + 1)))) {
                /* skip leading spaces but need to leave one */
                s++;
                continue;
            }
            if (is_dn_syntax) {
                subtypestart = d; /* prepare for '+' in the quoted value, if any */
            }
            subrdn_av_count = 0;
            /* FALLTHRU */
        case INQUOTEDVALUE:
            if (ISQUOTE(*s)) {
                if (ISESCAPE(*(d - 1))) {            /* the quote is escaped */
                    PR_snprintf(d, 3, "%X", *(s++)); /* hexpair */
                } else {                             /* end of INQUOTEVALUE */
                    if (is_dn_syntax) {
                        while (ISSPACE(*(d - 1))) { /* eliminate trailing spaces */
                            d--;
                            chkblank = 1;
                        }
                        /* We have to keep the last ' ' of a value in quotes.
                         * The same idea as the escaped last space:
                         * "cn=A,ou=B " */
                        /*           ^  */
                        if (chkblank && ISBLANK(*d)) {
                            PR_snprintf(d, 4, "\\%X", *d); /* hexpair */
                            d += 3;
                            chkblank = 0;
                        }
                    } else if (ISSPACE(*(d - 1))) {
                        /* Convert last trailing space to hex code */
                        d--;
                        PR_snprintf(d, 4, "\\%X", *d); /* hexpair */
                        d += 3;
                    }

                    state = B4SEPARATOR;
                    s++;
                }
            } else if (((state == INQUOTEDVALUE1ST) && LEADNEEDSESCAPE(*s)) ||
                       (state == INQUOTEDVALUE && NEEDSESCAPE(*s))) {
                if (d + 2 >= endd) {
                    /* Not enough space for dest; this never happens! */
                    rc = -1;
                    goto bail;
                } else {
                    if (ISEQUAL(*s) && is_dn_syntax) {
                        while (ISSPACE(*(d - 1))) { /* remove trailing spaces */
                            d--;
                        }
                    } else if (SEPARATOR(*s) && is_dn_syntax) {
                        /* separator is a subset of needsescape */
                        while (ISSPACE(*(d - 1))) { /* remove trailing spaces */
                            d--;
                            chkblank = 1;
                        }
                        /* We have to keep the last ' ' of a value in quotes.
                         * The same idea as the escaped last space:
                         * "cn=A\ ,ou=..." */
                        /*       ^         */
                        if (chkblank && ISBLANK(*d)) {
                            PR_snprintf(d, 4, "\\%X", *d); /* hexpair */
                            d += 3;
                            chkblank = 0;
                        }
                        /*
                         * Track and sort attribute values within
                         * multivalued RDNs.
                         */
                        if (subtypestart &&
                            (ISPLUS(*s) || subrdn_av_count > 0)) {
                            /* if subtypestart is not valid DN,
                             * we do not do sorting.*/
                            char *p = PL_strcasestr(subtypestart, "\\3d");
                            if (MAYBEDN(p)) {
                                add_rdn_av(subtypestart, d, &subrdn_av_count,
                                           &subrdn_avs, subinitial_rdn_av_stack);
                            } else {
                                reset_rdn_avs(&subrdn_avs, &subrdn_av_count);
                                subtypestart = NULL;
                            }
                        }
                        if (!ISPLUS(*s)) { /* at end of this RDN */
                            if (subrdn_av_count > 1) {
                                sort_rdn_avs(subrdn_avs, subrdn_av_count, 1);
                            }
                            if (subrdn_av_count > 0) {
                                reset_rdn_avs(&subrdn_avs, &subrdn_av_count);
                                subtypestart = NULL;
                            }
                        }
                    }

                    /* dn: cn="x=x,..",... -> dn: cn=x\3Dx\2C,... */
                    *d++ = '\\';
                    PR_snprintf(d, 3, "%X", *s); /* hexpair */
                    d += 2;
                    if (ISPLUS(*s++) && is_dn_syntax) {
                        subtypestart = d; /* next type start of multi values */
                    }
                    if ((SEPARATOR(*(s - 1)) || ISEQUAL(*(s - 1))) && is_dn_syntax) {
                        while (ISSPACE(*s)) /* remove leading spaces */
                            s++;
                    }
                }
            } else if (ISSPACE(*s)) {
                while (ISSPACE(*s)) {
                    s++;
                }
                /*
                 * dn_syntax_attr=ABC,   XYZ --> dn_syntax_attr=ABC,XYZ
                 * non_dn_syntax_attr=ABC,   XYZ --> dn_syntax_attr=ABC, XYZ
                 */
                if (!is_dn_syntax) {
                    --s;
                    *d++ = *s++;
                }
            } else {
                *d++ = *s++;
            }
            if (state == INQUOTEDVALUE1ST) {
                state = INQUOTEDVALUE;
            }
            break;
        case B4SEPARATOR:
            if (SEPARATOR(*s)) {
                state = B4TYPE;

                /*
                 * Track and sort attribute values within
                 * multivalued RDNs.
                 */
                if (typestart &&
                    (ISPLUS(*s) || rdn_av_count > 0)) {
                    add_rdn_av(typestart, d, &rdn_av_count,
                               &rdn_avs, initial_rdn_av_stack);
                }
                /* Sub type sorting might be also ongoing */
                if (subtypestart && subrdn_av_count > 0) {
                    add_rdn_av(subtypestart, d, &subrdn_av_count,
                               &subrdn_avs, subinitial_rdn_av_stack);
                }
                if (!ISPLUS(*s)) { /* at end of this RDN */
                    if (rdn_av_count > 1) {
                        sort_rdn_avs(rdn_avs, rdn_av_count, 0);
                    }
                    if (rdn_av_count > 0) {
                        reset_rdn_avs(&rdn_avs, &rdn_av_count);
                        typestart = NULL;
                    }
                    /* If in the middle of sub type sorting, finish it. */
                    if (subrdn_av_count > 1) {
                        sort_rdn_avs(subrdn_avs, subrdn_av_count, 1);
                    }
                    if (subrdn_av_count > 0) {
                        reset_rdn_avs(&subrdn_avs, &subrdn_av_count);
                        subtypestart = NULL;
                    }
                }

                *d++ = (ISPLUS(*s++)) ? '+' : ',';
            } else {
                s++;
            }
            break;
        default:
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_dn_normalize_ext", "Unknown state %d\n", state);
            break;
        }
    }

    /*
     * Track and sort attribute values within multivalued RDNs.
     */
    /* We may still be in an unexpected state, such as B4TYPE if
     * we encountered something odd like a '+' at the end of the
     * rdn.  If this is the case, we don't want to add this bogus
     * rdn to our list to sort.  We should only be in the INVALUE
     * or B4SEPARATOR state if we have a valid rdn component to
     * be added. */
    if (typestart && (rdn_av_count > 0) && ((state == INVALUE1ST) ||
                                            (state == INVALUE) || (state == B4SEPARATOR))) {
        add_rdn_av(typestart, d, &rdn_av_count, &rdn_avs, initial_rdn_av_stack);
    }
    if (rdn_av_count > 1) {
        sort_rdn_avs(rdn_avs, rdn_av_count, 0);
    }
    if (rdn_av_count > 0) {
        reset_rdn_avs(&rdn_avs, &rdn_av_count);
    }
    /* Trim trailing spaces */
    while (d > *dest && ISBLANK(*(d - 1))) {
        --d; /* XXX 518524 */
    }
    *dest_len = d - *dest;
bail:
    if (rc < 0) {
        if (dest != NULL) {
            if (*dest != src) {
                slapi_ch_free_string(dest);
            } else {
                *dest = NULL;
            }
        }
        if (dest_len != NULL) {
            *dest_len = 0;
        }
    } else if (d && rc > 0) {
        /* We terminate the str with NULL only when we allocate the str */
        *d = '\0';
    }
    /* add this dn to the normalized dn cache */
    if (udn) {
        if (dest && *dest && dest_len && *dest_len) {
            ndn_cache_add(udn, src_len, *dest, *dest_len);
        } else {
            slapi_ch_free_string(&udn);
        }
    }

    return rc;
}

char *
slapi_create_dn_string(const char *fmt, ...)
{
    char *src = NULL;
    char *dest = NULL;
    size_t dest_len = 0;
    va_list ap;
    int rc = 0;

    if (NULL == fmt) {
        return NULL;
    }

    va_start(ap, fmt);
    src = PR_vsmprintf(fmt, ap);
    va_end(ap);

    rc = slapi_dn_normalize_ext(src, strlen(src), &dest, &dest_len);
    if (rc < 0) {
        slapi_ch_free_string(&src);
        return NULL;
    } else if (rc == 0) { /* src is passed in. */
        *(dest + dest_len) = '\0';
    } else {
        slapi_ch_free_string(&src);
    }
    return dest;
}

char *
slapi_create_rdn_value(const char *fmt, ...)
{
    char *src = NULL;
    char *dest = NULL;
    size_t dest_len = 0;
    va_list ap;
    int rc = 0;
    char *dnfmt;

    if (NULL == fmt) {
        return NULL;
    }

    dnfmt = slapi_ch_smprintf("cn=%s", fmt);
    va_start(ap, fmt);
    src = PR_vsmprintf(dnfmt, ap);
    va_end(ap);
    slapi_ch_free_string(&dnfmt);

    rc = slapi_dn_normalize_ext(src, strlen(src), &dest, &dest_len);
    if (rc == 0) { /* src is passed in. */
        *(dest + dest_len) = '\0';
        dest = slapi_ch_strdup(dest + 3);
    } else if (rc > 0) {
        char *odest = dest;
        dest = slapi_ch_strdup(dest + 3);
        slapi_ch_free_string(&odest);
    }
    slapi_ch_free_string(&src);
    return dest;
}

/*
 * Append previous AV to the attribute value array if multivalued RDN.
 * We use a stack based array at first and if we overflow that, we
 * allocate a larger one from the heap, copy the stack based data in,
 * and continue to grow the heap based one as needed.
 */
static void
add_rdn_av(char *avstart, char *avend, int *rdn_av_countp, struct berval **rdn_avsp, struct berval *avstack)
{
    if (*rdn_av_countp == 0) {
        *rdn_avsp = avstack;
    } else if (*rdn_av_countp == SLAPI_DNNORM_INITIAL_RDN_AVS) {
        struct berval *tmpavs;

        tmpavs = (struct berval *)slapi_ch_calloc(
            SLAPI_DNNORM_INITIAL_RDN_AVS * 2, sizeof(struct berval));
        memcpy(tmpavs, *rdn_avsp,
               SLAPI_DNNORM_INITIAL_RDN_AVS * sizeof(struct berval));
        *rdn_avsp = tmpavs;
    } else if ((*rdn_av_countp % SLAPI_DNNORM_INITIAL_RDN_AVS) == 0) {
        *rdn_avsp = (struct berval *)slapi_ch_realloc((char *)*rdn_avsp,
                                                      (*rdn_av_countp +
                                                       SLAPI_DNNORM_INITIAL_RDN_AVS) *
                                                          sizeof(struct berval));
    }

    /*
     * Note: The bv_val's are just pointers into the dn itself.  Also,
     * we DO NOT zero-terminate the bv_val's.  The sorting code in
     * sort_rdn_avs() takes all of this into account.
     */
    (*rdn_avsp)[*rdn_av_countp].bv_val = avstart;
    (*rdn_avsp)[*rdn_av_countp].bv_len = avend - avstart;
    ++(*rdn_av_countp);
}


/*
 * Reset RDN attribute value array, freeing memory if any was allocated.
 */
static void
reset_rdn_avs(struct berval **rdn_avsp, int *rdn_av_countp)
{
    if (*rdn_av_countp > SLAPI_DNNORM_INITIAL_RDN_AVS) {
        slapi_ch_free((void **)rdn_avsp);
    }
    *rdn_avsp = NULL;
    *rdn_av_countp = 0;
}


/*
 * Perform an in-place, case-insensitive sort of RDN attribute=value pieces.
 * This function is always called with more than one element in "avs".
 *
 * Note that this is used by the DN normalization code, so if any changes
 * are made to the comparison function used for sorting customers will need
 * to rebuild their database/index files.
 *
 * Also note that the bv_val's in the "avas" array are not zero-terminated.
 */
static void
sort_rdn_avs(struct berval *avs, int count, int escape)
{
    int i, j, swaps;

    /*
     * Since we expect there to be a small number of AVs, we use a
     * simple bubble sort.  rdn_av_swap() only works correctly on
     * adjacent values anyway.
     */
    for (i = 0; i < count - 1; ++i) {
        swaps = 0;
        for (j = 0; j < count - 1; ++j) {
            if (rdn_av_cmp(&avs[j], &avs[j + 1]) > 0) {
                rdn_av_swap(&avs[j], &avs[j + 1], escape);
                ++swaps;
            }
        }
        if (swaps == 0) {
            break; /* stop early if no swaps made during the last pass */
        }
    }
}


/*
 * strcasecmp()-like function for RDN attribute values.
 */
static int
rdn_av_cmp(struct berval *av1, struct berval *av2)
{
    int rc;

    rc = strncasecmp(av1->bv_val, av2->bv_val,
                     (av1->bv_len < av2->bv_len) ? av1->bv_len : av2->bv_len);

    if (rc == 0) {
        return (av1->bv_len - av2->bv_len); /* longer is greater */
    } else {
        return (rc);
    }
}


/*
 * Swap two adjacent attribute=value pieces within an (R)DN.
 * Avoid allocating any heap memory for reasonably small AVs.
 */
static void
rdn_av_swap(struct berval *av1, struct berval *av2, int escape)
{
    char *buf1, *buf2;
    char stackbuf1[SLAPI_DNNORM_SMALL_RDN_AV];
    char stackbuf2[SLAPI_DNNORM_SMALL_RDN_AV];
    int len1, len2;

    /*
     * Copy the two avs into temporary buffers.  We use stack-based buffers
     * if the avs are small and allocate buffers from the heap to hold
     * large values.
     */
    if ((len1 = av1->bv_len) <= SLAPI_DNNORM_SMALL_RDN_AV) {
        buf1 = stackbuf1;
    } else {
        buf1 = slapi_ch_malloc(len1);
    }
    memcpy(buf1, av1->bv_val, len1);

    if ((len2 = av2->bv_len) <= SLAPI_DNNORM_SMALL_RDN_AV) {
        buf2 = stackbuf2;
    } else {
        buf2 = slapi_ch_malloc(len2);
    }
    memcpy(buf2, av2->bv_val, len2);

    /*
     * Copy av2 over av1 and reset length of av1.
     */
    memcpy(av1->bv_val, buf2, av2->bv_len);
    av1->bv_len = len2;

    /*
     * Add separator character (+) and copy av1 into place.
     * Also reset av2 pointer and length.
     */
    av2->bv_val = av1->bv_val + len2;
    if (escape) {
        *(av2->bv_val)++ = '\\';
        PR_snprintf(av2->bv_val, 3, "%X", '+'); /* hexpair */
        av2->bv_val += 2;
    } else {
        *(av2->bv_val)++ = '+';
    }
    memcpy(av2->bv_val, buf1, len1);
    av2->bv_len = len1;

    /*
     * Clean up.
     */
    if (len1 > SLAPI_DNNORM_SMALL_RDN_AV) {
        slapi_ch_free((void **)&buf1);
    }
    if (len2 > SLAPI_DNNORM_SMALL_RDN_AV) {
        slapi_ch_free((void **)&buf2);
    }
}

/* Introduced for the upgrade tool. DON'T USE THIS API! */
char *
slapi_dn_normalize_original(char *dn)
{
    /* slapi_log_err(SLAPI_LOG_TRACE, "=> slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
    *(substr_dn_normalize_orig(dn, dn + strlen(dn))) = '\0';
    /* slapi_log_err(SLAPI_LOG_TRACE, "<= slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */

    return (dn);
}

/* Introduced for the upgrade tool. DON'T USE THIS API! */
char *
slapi_dn_normalize_case_original(char *dn)
{
    /* slapi_log_err(SLAPI_LOG_TRACE, "=> slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
    *(substr_dn_normalize_orig(dn, dn + strlen(dn))) = '\0';
    /* slapi_log_err(SLAPI_LOG_TRACE, "<= slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */

    /* normalize case */
    return (slapi_dn_ignore_case(dn));
}

/*
 * DEPRECATED: this function does nothing.
 * slapi_dn_normalize - put dn into a canonical format.  the dn is
 * normalized in place, as well as returned.
 */
char *
slapi_dn_normalize(char *dn)
{
    /* slapi_log_err(SLAPI_LOG_TRACE, "=> slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
    *(substr_dn_normalize(dn, dn + strlen(dn))) = '\0';
    /* slapi_log_err(SLAPI_LOG_TRACE, "<= slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
    return dn;
}

/*
 * DEPRECATED: this function does nothing.
 * Note that this routine  normalizes to the end and doesn't null terminate
 */
char *
slapi_dn_normalize_to_end(char *dn, char *end)
{
    return (substr_dn_normalize(dn, end ? end : dn + strlen(dn)));
}

/*
 * dn could contain UTF-8 multi-byte characters,
 * which also need to be converted to the lower case.
 */
char *
slapi_dn_ignore_case(char *dn)
{
    unsigned char *s = NULL, *d = NULL;
    int ssz, dsz;
    /* normalize case (including UTF-8 multi-byte chars) */
    for (s = d = (unsigned char *)dn; s && *s; s += ssz, d += dsz) {
        slapi_utf8ToLower(s, d, &ssz, &dsz);
    }
    if (d) {
        *d = '\0'; /* utf8ToLower result may be shorter than the original */
    }
    return (dn);
}

char *
dn_ignore_case_to_end(char *dn, char *end)
{
    unsigned char *s = NULL, *d = NULL;
    int ssz, dsz;
    /* normalize case (including UTF-8 multi-byte chars) */
    for (s = d = (unsigned char *)dn; s && s < (unsigned char *)end && *s;
         s += ssz, d += dsz) {
        slapi_utf8ToLower(s, d, &ssz, &dsz);
    }
    if (d) {
        *d = '\0'; /* utf8ToLower result may be shorter than the original */
    }
    return (dn);
}

/*
 * slapi_dn_normalize_case - put dn into a canonical form suitable for storing
 * in a hash database.  this involves normalizing the case as well as
 * the format.  the dn is normalized in place as well as returned.
 * (DEPRECATED)
 */

char *
slapi_dn_normalize_case(char *dn)
{
    /* normalize format (DEPRECATED) noop */
    slapi_dn_normalize(dn);

    /* normalize case */
    return (slapi_dn_ignore_case(dn));
}

int
slapi_dn_normalize_case_ext(char *src, size_t src_len, char **dest, size_t *dest_len)
{
    int rc = slapi_dn_normalize_ext(src, src_len, dest, dest_len);

    if (rc >= 0) {
        dn_ignore_case_to_end(*dest, *dest + *dest_len);
    }
    return rc;
}

char *
slapi_create_dn_string_case(const char *fmt, ...)
{
    char *src = NULL;
    char *dest = NULL;
    size_t dest_len = 0;
    va_list ap;
    int rc = 0;

    if (NULL == fmt) {
        return NULL;
    }

    va_start(ap, fmt);
    src = PR_vsmprintf(fmt, ap);
    va_end(ap);

    rc = slapi_dn_normalize_ext(src, strlen(src), &dest, &dest_len);
    if (rc < 0) {
        slapi_ch_free_string(&src);
    } else if (rc == 0) { /* src is passed in. */
        *(dest + dest_len) = '\0';
    } else {
        slapi_ch_free_string(&src);
    }

    return slapi_dn_ignore_case(dest);
}

/*
 * slapi_dn_beparent - return a copy of the dn of dn's parent,
 *                     NULL if the DN is a suffix of the backend.
 */
char *
slapi_dn_beparent(
    Slapi_PBlock *pb,
    const char *dn)
{
    char *r = NULL;
    if (dn != NULL && *dn != '\0') {
        if (!slapi_dn_isbesuffix(pb, dn)) {
            r = slapi_dn_parent(dn);
        }
    }
    return r;
}

/*
 * This function is used for speed.  Instead of returning a newly allocated
 * dn string that contains the parent, this function just returns a pointer
 * to the address _within_ the given string where the parent dn of the
 * given dn starts e.g. if you call this with "dc=example,dc=com", the
 * function will return "dc=com" - that is, the char* returned will be the
 * address of the 'd' after the ',' in "dc=example,dc=com".  This function
 * also checks for bogus things like consecutive ocurrances of unquoted
 * separators e.g. DNs like cn=foo,,,,,,,,,,,cn=bar,,,,,,,
 * This function is useful for "interating" over a DN returning the ancestors
 * of the given dn e.g.
 *
 * const char *dn = somedn;
 * while (dn = slapi_dn_find_parent(dn)) {
 *   see if parent exists
 *   etc.
 * }
 */
const char *
slapi_dn_find_parent_ext(const char *dn, int is_tombstone)
{
    const char *s;
    int inquote;
    char *head;

    if (dn == NULL || *dn == '\0') {
        return (NULL);
    }

    /*
     * An X.500-style distinguished name looks like this:
     * foo=bar,sha=baz,...
     */
    head = (char *)dn;
    if (is_tombstone) {
        /* if it's a tombstone entry's DN,
         * skip nsuniqueid=* part and do the job. */
        if (0 == strncasecmp(dn, SLAPI_ATTR_UNIQUEID, 10)) {
            /* exception: RUV_STORAGE_ENTRY_UNIQUEID */
            /* dn is normalized */
            if (0 == strncasecmp(dn + 11, RUV_STORAGE_ENTRY_UNIQUEID,
                                 sizeof(RUV_STORAGE_ENTRY_UNIQUEID) - 1)) {
                head = (char *)dn;
            } else {
                head = strchr(dn, ',');
                if (head) {
                    head++;
                } else {
                    head = (char *)dn;
                }
            }
        }
    }

    inquote = 0;
    for (s = head; *s; s++) {
        if (*s == '\\') {
            if (*(s + 1))
                s++;
            continue;
        }
        if (inquote) {
            if (*s == '"')
                inquote = 0;
        } else {
            if (*s == '"')
                inquote = 1;
            else {
                if (DNSEPARATOR(*s)) {
                    while (*s && DNSEPARATOR(*s)) {
                        ++s;
                    }
                    if (*s) {
                        return (s);
                    }
                }
            }
        }
    }

    return (NULL);
}

const char *
slapi_dn_find_parent(const char *dn)
{
    return slapi_dn_find_parent_ext(dn, 0);
}

char *
slapi_dn_parent_ext(const char *dn, int is_tombstone)
{
    const char *s = slapi_dn_find_parent_ext(dn, is_tombstone);

    if (s == NULL || *s == '\0') {
        return (NULL);
    }

    return (slapi_ch_strdup(s));
}

char *
slapi_dn_parent(const char *dn)
{
    const char *s = slapi_dn_find_parent(dn);

    if (s == NULL || *s == '\0') {
        return (NULL);
    }

    return (slapi_ch_strdup(s));
}

/*
 * slapi_dn_issuffix - tells whether suffix is a suffix of dn.  both dn
 * and suffix must be normalized.
 */
int
slapi_dn_issuffix(const char *dn, const char *suffix)
{
    int dnlen, suffixlen;

    if (dn == NULL || suffix == NULL) {
        return (0);
    }

    suffixlen = strlen(suffix);
    dnlen = strlen(dn);

    if (suffixlen > dnlen) {
        return (0);
    }

    if (suffixlen == 0) {
        return (1);
    }

    return ((slapi_utf8casecmp((unsigned char *)(dn + dnlen - suffixlen),
                               (unsigned char *)suffix) == 0) &&
            ((dnlen == suffixlen) || DNSEPARATOR(dn[dnlen - suffixlen - 1])));
}

int
slapi_dn_isbesuffix(Slapi_PBlock *pb, const char *dn)
{
    int r;
    Slapi_DN sdn;
    slapi_sdn_init_dn_byref(&sdn, dn);
    Slapi_Backend *pb_backend = NULL;
    slapi_pblock_get(pb, SLAPI_BACKEND, &pb_backend);
    r = slapi_be_issuffix(pb_backend, &sdn);
    slapi_sdn_done(&sdn);
    return r;
}

/*
 * slapi_dn_isparent - returns non-zero if parentdn is the parent of childdn,
 * 0 otherwise
 */
int
slapi_dn_isparent(const char *parentdn, const char *childdn)
{
    char *realparentdn, *copyparentdn;
    int rc;

    /* child is root - has no parent */
    if (childdn == NULL || *childdn == '\0') {
        return (0);
    }

    /* construct the actual parent dn and normalize it */
    if ((realparentdn = slapi_dn_parent(childdn)) == NULL) {
        return (parentdn == NULL || *parentdn == '\0');
    }
    slapi_dn_normalize(realparentdn);

    /* normalize the purported parent dn */
    copyparentdn = slapi_ch_strdup((char *)parentdn);
    slapi_dn_normalize(copyparentdn);

    /* compare them */
    rc = !strcasecmp(realparentdn, copyparentdn);
    slapi_ch_free((void **)&copyparentdn);
    slapi_ch_free((void **)&realparentdn);

    return (rc);
}

/*
 * Function: slapi_dn_isroot
 *
 * Returns: 1 if "dn" is the root dn
 *          0 otherwise.
 * dn must be normalized
 *
 */
int
slapi_dn_isroot(const char *dn)
{
    int rc;
    char *rootdn;

    if (NULL == dn) {
        return (0);
    }
    if (NULL == (rootdn = config_get_rootdn())) {
        return (0);
    }

    /* note:  global root dn is normalized when read from config. file */
    rc = (strcasecmp(rootdn, dn) == 0);
    slapi_ch_free((void **)&rootdn);
    return (rc);
}

int32_t
slapi_sdn_isroot(const Slapi_DN *sdn)
{
    return slapi_dn_isroot(slapi_sdn_get_ndn(sdn));
}

int
slapi_is_rootdse(const char *dn)
{
    if (NULL != dn) {
        if (*dn == '\0') {
            return 1;
        }
    }
    return 0;
}

int
slapi_rdn2typeval(
    char *rdn,
    char **type,
    struct berval *bv)
{
    char *s;

    if ((s = strchr(rdn, '=')) == NULL) {
        return (-1);
    }
    *s++ = '\0';

    *type = rdn;

    /* MAB 9 Oct 00 : explicit bug fix of 515715
                      implicit bug fix of 394800 (can't reproduce anymore)
       When adding the rdn attribute in the entry, we need to remove
       all special escaped characters included in the value itself,
       i.e., strings like "\;" must be converted to ";" and so on... */
    strcpy_unescape_value(s, s);

    bv->bv_val = s;
    bv->bv_len = strlen(s);

    return (0);
}

/*
 * Add an RDN to a DN, getting back the new DN.
 */
char *
slapi_dn_plus_rdn(const char *dn, const char *rdn)
{
    /* rdn + separator + dn + null */
    char *newdn = slapi_ch_smprintf("%s,%s", rdn, dn);
    return newdn;
}

/* ======  Slapi_DN functions ====== */

#ifdef SDN_DEBUG
#define SDN_DUMP(sdn, name) sdn_dump(sdn, name)
static void sdn_dump(const Slapi_DN *sdn, const char *text);
#else
#define SDN_DUMP(sdn, name) ((void)0)
#endif

#ifndef SLAPI_DN_COUNTERS
#undef DEBUG /* disable counters */
#endif
#include <prcountr.h>

static int counters_created = 0;
PR_DEFINE_COUNTER(slapi_sdn_counter_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_exist);
PR_DEFINE_COUNTER(slapi_sdn_counter_dn_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_dn_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_dn_exist);
PR_DEFINE_COUNTER(slapi_sdn_counter_ndn_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_ndn_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_ndn_exist);
PR_DEFINE_COUNTER(slapi_sdn_counter_udn_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_udn_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_udn_exist);

static void
sdn_create_counters(void)
{
    PR_CREATE_COUNTER(slapi_sdn_counter_created, "Slapi_DN", "created", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_deleted, "Slapi_DN", "deleted", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_exist, "Slapi_DN", "exist", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_dn_created, "Slapi_DN", "internal_dn_created", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_dn_deleted, "Slapi_DN", "internal_dn_deleted", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_dn_exist, "Slapi_DN", "internal_dn_exist", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_ndn_created, "Slapi_DN", "internal_ndn_created", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_ndn_deleted, "Slapi_DN", "internal_ndn_deleted", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_ndn_exist, "Slapi_DN", "internal_ndn_exist", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_udn_created, "Slapi_DN", "internal_udn_created", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_udn_deleted, "Slapi_DN", "internal_udn_deleted", "");
    PR_CREATE_COUNTER(slapi_sdn_counter_udn_exist, "Slapi_DN", "internal_udn_exist", "");
    counters_created = 1;
}

#define FLAG_ALLOCATED 0
#define FLAG_DN 1
#define FLAG_NDN 2
#define FLAG_UDN 3

Slapi_DN *
slapi_sdn_new(void)
{
    Slapi_DN *sdn = (Slapi_DN *)slapi_ch_malloc(sizeof(Slapi_DN));
    slapi_sdn_init(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_ALLOCATED);
    SDN_DUMP(sdn, "slapi_sdn_new");
    PR_INCREMENT_COUNTER(slapi_sdn_counter_created);
    PR_INCREMENT_COUNTER(slapi_sdn_counter_exist);
    return sdn;
}

/*
 * WARNING:
 * Do not call slapi_sdn_init and its sibling APIs against Slapi_DN
 * allocated by slapi_sdn_new.  slapi_sdn_init clears all bits in the flag.
 * If sdn is allocated by slapi_sdn_new, the FLAG_ALLOCATED bit is cleared
 * and slapi_sdn_free won't free Slapi_DN.
 */
Slapi_DN *
slapi_sdn_init(Slapi_DN *sdn)
{
    sdn->flag = 0;
    sdn->udn = NULL;
    sdn->dn = NULL;
    sdn->ndn = NULL;
    sdn->ndn_len = 0;
    if (!counters_created) {
        sdn_create_counters();
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_init_dn_byref(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_dn_byref(sdn, dn);
    return sdn;
}

Slapi_DN *
slapi_sdn_init_dn_byval(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_dn_byval(sdn, dn);
    return sdn;
}

Slapi_DN *
slapi_sdn_init_dn_passin(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_dn_passin(sdn, dn);
    return sdn;
}

/* use when dn is already normalized (but case is yet touched) */
Slapi_DN *
slapi_sdn_init_normdn_byref(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    if (dn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(dn);
        sdn->dn = dn;
        sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_DN);
    }
    return sdn;
}

/* use when dn is already normalized (but case is yet touched) */
Slapi_DN *
slapi_sdn_init_normdn_byval(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    if (dn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(dn);
        sdn->dn = slapi_ch_strdup(dn);
        sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_DN);
    }
    return sdn;
}

/* use when dn is already normalized (but case is yet touched) */
Slapi_DN *
slapi_sdn_init_normdn_passin(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    if (dn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(dn);
        sdn->dn = dn;
        sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_DN);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_init_ndn_byref(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_ndn_byref(sdn, dn);
    return sdn;
}

Slapi_DN *
slapi_sdn_init_ndn_byval(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_ndn_byval(sdn, dn);
    return sdn;
}

Slapi_DN *
slapi_sdn_new_dn_byval(const char *dn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_dn_byval(sdn, dn);
    SDN_DUMP(sdn, "slapi_sdn_new_dn_byval");
    return sdn;
}

/* This is a much clearer name for what we want to achieve. */
Slapi_DN *
slapi_sdn_new_from_char_dn(const char *dn) __attribute__((weak, alias("slapi_sdn_new_dn_byval")));

Slapi_DN *
slapi_sdn_new_ndn_byval(const char *ndn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_ndn_byval(sdn, ndn);
    SDN_DUMP(sdn, "slapi_sdn_new_ndn_byval");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_dn_byref(const char *dn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_dn_byref(sdn, dn);
    SDN_DUMP(sdn, "slapi_sdn_new_dn_byref");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_dn_passin(const char *dn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_dn_passin(sdn, dn);
    SDN_DUMP(sdn, "slapi_sdn_new_dn_passin");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_ndn_byref(const char *ndn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_ndn_byref(sdn, ndn);
    SDN_DUMP(sdn, "slapi_sdn_new_ndn_byref");
    return sdn;
}

/* use when dn is already fully normalized */
Slapi_DN *
slapi_sdn_new_ndn_passin(const char *ndn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_ndn_passin(sdn, ndn);
    SDN_DUMP(sdn, "slapi_sdn_new_ndn_passin");
    return sdn;
}


/* use when dn is already normalized */
Slapi_DN *
slapi_sdn_new_normdn_byref(const char *normdn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_normdn_byref(sdn, normdn);
    SDN_DUMP(sdn, "slapi_sdn_new_normdn_byref");
    return sdn;
}

/* use when dn is already normalized */
Slapi_DN *
slapi_sdn_new_normdn_passin(const char *normdn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_normdn_passin(sdn, normdn);
    SDN_DUMP(sdn, "slapi_sdn_new_normdn_passin");
    return sdn;
}

/* use when dn is already normalized */
Slapi_DN *
slapi_sdn_new_normdn_byval(const char *normdn)
{
    Slapi_DN *sdn = slapi_sdn_new();
    slapi_sdn_set_normdn_byval(sdn, normdn);
    SDN_DUMP(sdn, "slapi_sdn_new_normdn_byval");
    return sdn;
}

Slapi_DN *
slapi_sdn_set_dn_byval(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_UDN);
    if (dn != NULL) {
        sdn->udn = slapi_ch_strdup(dn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_exist);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_dn_byref(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_UDN);
    sdn->udn = dn;
    return sdn;
}

Slapi_DN *
slapi_sdn_set_dn_passin(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_UDN);
    sdn->udn = dn;
    if (dn != NULL) {
        PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_exist);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_normdn_byref(Slapi_DN *sdn, const char *normdn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_DN);
    sdn->dn = normdn;
    if (normdn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(normdn);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_normdn_passin(Slapi_DN *sdn, const char *normdn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_DN);
    sdn->dn = normdn;
    if (normdn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(normdn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_normdn_byval(Slapi_DN *sdn, const char *normdn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_DN);
    if (normdn == NULL) {
        sdn->dn = NULL;
        sdn->ndn_len = 0;
    } else {
        sdn->dn = slapi_ch_strdup(normdn);
        sdn->ndn_len = strlen(normdn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_ndn_byval(Slapi_DN *sdn, const char *ndn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_NDN);
    if (ndn != NULL) {
        sdn->ndn = slapi_ch_strdup(ndn);
        sdn->ndn_len = strlen(ndn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_ndn_byref(Slapi_DN *sdn, const char *ndn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_NDN);
    sdn->ndn = ndn;
    if (ndn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(ndn);
    }
    return sdn;
}

Slapi_DN *
slapi_sdn_set_ndn_passin(Slapi_DN *sdn, const char *ndn)
{
    slapi_sdn_done(sdn);
    sdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_NDN);
    sdn->ndn = ndn;
    if (ndn == NULL) {
        sdn->ndn_len = 0;
    } else {
        sdn->ndn_len = strlen(ndn);
    }
    return sdn;
}

/*
 * Set the RDN of the DN.
 */
Slapi_DN *
slapi_sdn_set_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn)
{
    const char *rawrdn = slapi_rdn_get_rdn(rdn);
    if (slapi_sdn_isempty(sdn)) {
        slapi_sdn_set_dn_byval(sdn, rawrdn);
    } else {
        /* NewDN= NewRDN + OldParent */
        char *parentdn = slapi_dn_parent(slapi_sdn_get_dn(sdn));
        /*
         * using slapi_ch_smprintf is okay since
         * newdn is set to sdn as a pre-normalized dn.
         */
        char *newdn = slapi_ch_smprintf("%s,%s", rawrdn, parentdn);
        slapi_ch_free((void **)&parentdn);
        slapi_sdn_set_dn_passin(sdn, newdn);
    }
    return sdn;
}

/*
 * Add the RDN to the DN.
 */
Slapi_DN *
slapi_sdn_add_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn)
{
    const char *rawrdn = slapi_rdn_get_rdn(rdn);
    if (slapi_sdn_isempty(sdn)) {
        slapi_sdn_set_dn_byval(sdn, rawrdn);
    } else {
        /* NewDN= NewRDN + DN */
        const char *dn = slapi_sdn_get_dn(sdn);
        /*
         * using slapi_ch_smprintf is okay since
         * newdn is set to sdn as a pre-normalized dn.
         */
        char *newdn = slapi_ch_smprintf("%s,%s", rawrdn, dn);
        slapi_sdn_set_dn_passin(sdn, newdn);
    }
    return sdn;
}

/*
 * Set the parent of the DN.
 */
Slapi_DN *
slapi_sdn_set_parent(Slapi_DN *sdn, const Slapi_DN *parentdn)
{
    if (slapi_sdn_isempty(sdn)) {
        slapi_sdn_copy(parentdn, sdn);
    } else {
        /* NewDN= OldRDN + NewParent */
        Slapi_RDN rdn;
        const char *rawrdn;
        slapi_rdn_init_dn(&rdn, slapi_sdn_get_dn(sdn));
        rawrdn = slapi_rdn_get_rdn(&rdn);
        if (slapi_sdn_isempty(parentdn)) {
            slapi_sdn_set_dn_byval(sdn, rawrdn);
        } else {
            char *newdn =
                slapi_ch_smprintf("%s,%s", rawrdn, slapi_sdn_get_dn(parentdn));
            slapi_sdn_set_dn_passin(sdn, newdn);
        }
        slapi_rdn_done(&rdn);
    }
    return sdn;
}

void
slapi_sdn_done(Slapi_DN *sdn)
{
    /* sdn_dump( sdn, "slapi_sdn_done"); */
    if (sdn == NULL) {
        return;
    }
    if (sdn->dn != NULL) {
        if (slapi_isbitset_uchar(sdn->flag, FLAG_DN)) {
            slapi_ch_free((void **)&(sdn->dn));
            PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_deleted);
            PR_DECREMENT_COUNTER(slapi_sdn_counter_dn_exist);
        } else {
            sdn->dn = NULL;
        }
    }
    sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_DN);
    if (sdn->ndn != NULL) {
        if (slapi_isbitset_uchar(sdn->flag, FLAG_NDN)) {
            slapi_ch_free((void **)&(sdn->ndn));
            PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_deleted);
            PR_DECREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
        } else {
            sdn->ndn = NULL;
        }
    }
    sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_NDN);
    sdn->ndn_len = 0;
    if (sdn->udn != NULL) {
        if (slapi_isbitset_uchar(sdn->flag, FLAG_UDN)) {
            slapi_ch_free((void **)&(sdn->udn));
            PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_deleted);
            PR_DECREMENT_COUNTER(slapi_sdn_counter_udn_exist);
        } else {
            sdn->udn = NULL;
        }
    }
    sdn->flag = slapi_unsetbit_uchar(sdn->flag, FLAG_UDN);
}

void
slapi_sdn_free(Slapi_DN **sdn)
{
    if (sdn != NULL && *sdn != NULL) {
        int is_allocated = 0;
        SDN_DUMP(*sdn, "slapi_sdn_free");
        is_allocated = slapi_isbitset_uchar((*sdn)->flag, FLAG_ALLOCATED);
        slapi_sdn_done(*sdn);
        if (is_allocated) {
            slapi_ch_free((void **)sdn);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_deleted);
            PR_DECREMENT_COUNTER(slapi_sdn_counter_exist);
        }
    }
}

const char *
slapi_sdn_get_dn(const Slapi_DN *sdn)
{
    if (NULL == sdn) {
        return NULL;
    }
    if (sdn->dn) {
        return sdn->dn;
    } else if (sdn->ndn) {
        return sdn->ndn;
    } else if (sdn->udn) {
        char *udn = slapi_ch_strdup(sdn->udn);
        char *normed = NULL;
        size_t dnlen = 0;
        Slapi_DN *ncsdn = (Slapi_DN *)sdn; /* non-const Slapi_DN */
        int rc = slapi_dn_normalize_ext(udn, 0, &normed, &dnlen);
        if (rc == 0) { /* udn is passed in */
            *(normed + dnlen) = '\0';
            ncsdn->dn = normed;
            ncsdn->ndn_len = dnlen;
            ncsdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_DN);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
        } else if (rc > 0) { /* rc > 0 */
            slapi_ch_free_string(&udn);
            ncsdn->dn = normed;
            ncsdn->ndn_len = dnlen;
            ncsdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_DN);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
        } else { /* else (rc < 0); normalization failed. return NULL */
            slapi_ch_free_string(&udn);
        }
        return sdn->dn;
    } else {
        return NULL;
    }
}

const char *
slapi_sdn_get_ndn(const Slapi_DN *sdn)
{
    if (NULL == sdn) {
        return NULL;
    }
    if (sdn->ndn) {
        return sdn->ndn;
    } else if (sdn->dn || sdn->udn) {
        Slapi_DN *ncsdn = (Slapi_DN *)sdn; /* non-const Slapi_DN */
        char *ndn = slapi_ch_strdup(slapi_sdn_get_dn(sdn));
        slapi_dn_ignore_case(ndn); /* ignore case */
        ncsdn->ndn = ndn;
        ncsdn->flag = slapi_setbit_uchar(sdn->flag, FLAG_NDN);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
        return ndn;
    } else {
        return NULL;
    }
}

const char *
slapi_sdn_get_udn(const Slapi_DN *sdn)
{
    if (sdn->udn) {
        return sdn->udn;
    } else if (sdn->dn) {
        return sdn->dn;
    } else if (sdn->ndn) {
        return sdn->ndn;
    } else {
        return NULL;
    }
}

void
slapi_sdn_get_parent_ext(const Slapi_DN *sdn,
                         Slapi_DN *sdn_parent,
                         int is_tombstone)
{
    const char *parentdn =
        slapi_dn_parent_ext(slapi_sdn_get_dn(sdn), is_tombstone);
    slapi_sdn_set_normdn_passin(sdn_parent, parentdn);
    sdn_parent->flag = slapi_setbit_uchar(sdn_parent->flag, FLAG_DN);
    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
}

void
slapi_sdn_get_parent(const Slapi_DN *sdn, Slapi_DN *sdn_parent)
{
    slapi_sdn_get_parent_ext(sdn, sdn_parent, 0);
}

void
slapi_sdn_get_backend_parent_ext(const Slapi_DN *sdn,
                                 Slapi_DN *sdn_parent,
                                 const Slapi_Backend *be,
                                 int is_tombstone)
{
    if (slapi_sdn_isempty(sdn) || slapi_be_issuffix(be, sdn)) {
        slapi_sdn_done(sdn_parent);
    } else {
        slapi_sdn_get_parent_ext(sdn, sdn_parent, is_tombstone);
    }
}

void
slapi_sdn_get_backend_parent(const Slapi_DN *sdn, Slapi_DN *sdn_parent, const Slapi_Backend *be)
{
    slapi_sdn_get_backend_parent_ext(sdn, sdn_parent, be, 0);
}

void
slapi_sdn_get_rdn(const Slapi_DN *sdn, Slapi_RDN *rdn)
{
    slapi_rdn_set_dn(rdn, slapi_sdn_get_dn(sdn));
}

void
slapi_sdn_get_rdn_ext(const Slapi_DN *sdn, Slapi_RDN *rdn, int is_tombstone)
{
    slapi_rdn_set_dn_ext(rdn, slapi_sdn_get_dn(sdn), is_tombstone);
}

Slapi_DN *
slapi_sdn_dup(const Slapi_DN *sdn)
{
    Slapi_DN *tmp;
    SDN_DUMP(sdn, "slapi_sdn_dup");
    tmp = slapi_sdn_new_normdn_byval(slapi_sdn_get_dn(sdn));
    return tmp;
}

void
slapi_sdn_copy(const Slapi_DN *from, Slapi_DN *to)
{
    SDN_DUMP(from, "slapi_sdn_copy from");
    SDN_DUMP(to, "slapi_sdn_copy to");

    if (to == NULL || from == NULL){
        slapi_log_err(SLAPI_LOG_ERR, "slapi_sdn_copy",
                      "NULL param: from (0x%p) to (0x%p)\n", from, to);
        return;
    }

    slapi_sdn_done(to);
    if (from->udn) {
        to->flag = slapi_setbit_uchar(to->flag, FLAG_UDN);
        to->udn = slapi_ch_strdup(from->udn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_udn_exist);
    }
    if (from->dn) {
        to->flag = slapi_setbit_uchar(to->flag, FLAG_DN);
        to->dn = slapi_ch_strdup(from->dn);
        /* dn is normalized; strlen(dn) == strlen(ndn) */
        to->ndn_len = strlen(to->dn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
    }
    if (from->ndn) {
        to->flag = slapi_setbit_uchar(to->flag, FLAG_NDN);
        to->ndn = slapi_ch_strdup(from->ndn);
        to->ndn_len = strlen(to->ndn);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_created);
        PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
    }
}

int
slapi_sdn_compare(const Slapi_DN *sdn1, const Slapi_DN *sdn2)
{
    int rc;
    const char *ndn1 = slapi_sdn_get_ndn(sdn1);
    const char *ndn2 = slapi_sdn_get_ndn(sdn2);
    if (ndn1 == ndn2) {
        rc = 0;
    } else {
        if (ndn1 == NULL) {
            rc = -1;
        } else {
            if (ndn2 == NULL) {
                rc = 1;
            } else {
                rc = strcmp(ndn1, ndn2);
            }
        }
    }
    return rc;
}

int
slapi_sdn_isempty(const Slapi_DN *sdn)
{
    const char *dn = NULL;
    if (sdn) {
        dn = slapi_sdn_get_dn(sdn);
    }
    return (dn == NULL || dn[0] == '\0');
}

int
slapi_sdn_issuffix(const Slapi_DN *sdn, const Slapi_DN *suffixsdn)
{
    int rc;
    const char *dn = slapi_sdn_get_ndn(sdn);
    const char *suffixdn = slapi_sdn_get_ndn(suffixsdn);
    if (dn != NULL && suffixdn != NULL) {
        int dnlen = slapi_sdn_get_ndn_len(sdn);
        int suffixlen = slapi_sdn_get_ndn_len(suffixsdn);
        if (dnlen < suffixlen) {
            rc = 0;
        } else {
            if (suffixlen == 0) {
                return (1);
            }

            rc = (((dnlen == suffixlen) || DNSEPARATOR(dn[dnlen - suffixlen - 1])) && (strcasecmp(suffixdn, dn + dnlen - suffixlen) == 0));
        }
    } else {
        rc = 0;
    }
    return rc;
}

/* normalizes sdn if it hasn't already been done */
int
slapi_sdn_get_ndn_len(const Slapi_DN *sdn)
{
    int r = 0;
    (void)slapi_sdn_get_dn(sdn); /* does the normalization if needed */
    if (sdn->dn || sdn->ndn) {
        r = sdn->ndn_len;
    }
    return r;
}

int
slapi_sdn_isparent(const Slapi_DN *parent, const Slapi_DN *child)
{
    int rc = 0;

    /* child is root - has no parent */
    if (!slapi_sdn_isempty(child)) {
        Slapi_DN childparent;
        slapi_sdn_init(&childparent);
        slapi_sdn_get_parent(child, &childparent);
        rc = (slapi_sdn_compare(parent, &childparent) == 0);
        slapi_sdn_done(&childparent);
    }
    return (rc);
}

int
slapi_sdn_isgrandparent(const Slapi_DN *parent, const Slapi_DN *child)
{
    int rc = 0;

    /* child is root - has no parent */
    if (!slapi_sdn_isempty(child)) {
        Slapi_DN childparent;
        slapi_sdn_init(&childparent);
        slapi_sdn_get_parent(child, &childparent);
        if (!slapi_sdn_isempty(&childparent)) {
            Slapi_DN childchildparent;
            slapi_sdn_init(&childchildparent);
            slapi_sdn_get_parent(&childparent, &childchildparent);
            rc = (slapi_sdn_compare(parent, &childchildparent) == 0);
            slapi_sdn_done(&childchildparent);
        }
        slapi_sdn_done(&childparent);
    }
    return (rc);
}

/*
 * Return non-zero if "dn" matches the scoping criteria
 * given by "base" and "scope".
 */
int
slapi_sdn_scope_test(const Slapi_DN *dn, const Slapi_DN *base, int scope)
{
    int rc = 0;

    switch (scope) {
    case LDAP_SCOPE_BASE:
        rc = (slapi_sdn_compare(dn, base) == 0);
        break;
    case LDAP_SCOPE_ONELEVEL:
        rc = (slapi_sdn_isparent(base, dn) != 0);
        break;
    case LDAP_SCOPE_SUBTREE:
        rc = (slapi_sdn_issuffix(dn, base) != 0);
        break;
    }
    return rc;
}

/*
 * Return non-zero if "dn" matches the scoping criteria
 * given by "base" and "scope".
 * If SLAPI_ENTRY_FLAG_TOMBSTONE is set to flags,
 * DN without "nsuniqueid=...," is examined.
 */
int
slapi_sdn_scope_test_ext(const Slapi_DN *dn, const Slapi_DN *base, int scope, int flags)
{
    int rc = 0;

    switch (scope) {
    case LDAP_SCOPE_BASE:
        if (flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
            Slapi_DN parent;
            slapi_sdn_init(&parent);
            slapi_sdn_get_parent(dn, &parent);
            rc = (slapi_sdn_compare(dn, &parent) == 0);
            slapi_sdn_done(&parent);
        } else {
            rc = (slapi_sdn_compare(dn, base) == 0);
        }
        break;
    case LDAP_SCOPE_ONELEVEL:
#define RUVRDN SLAPI_ATTR_UNIQUEID "=" RUV_STORAGE_ENTRY_UNIQUEID ","
        if ((flags & SLAPI_ENTRY_FLAG_TOMBSTONE) &&
            (strncmp(slapi_sdn_get_ndn(dn), RUVRDN, sizeof(RUVRDN) - 1))) {
            /* tombstones except RUV tombstone */
            Slapi_DN parent;
            slapi_sdn_init(&parent);
            slapi_sdn_get_parent(dn, &parent);
            rc = (slapi_sdn_isparent(base, &parent) != 0);
            slapi_sdn_done(&parent);
        } else {
            rc = (slapi_sdn_isparent(base, dn) != 0);
        }
        break;
    case LDAP_SCOPE_SUBTREE:
        rc = (slapi_sdn_issuffix(dn, base) != 0);
        break;
    }
    return rc;
}

/*
 * build the new dn of an entry for moddn operations
 */
char *
slapi_moddn_get_newdn(Slapi_DN *dn_olddn, const char *newrdn, const char *newsuperiordn)
{
    char *newdn;

    if (newsuperiordn != NULL) {
        /* construct the new dn */
        newdn = slapi_dn_plus_rdn(newsuperiordn, newrdn); /* JCM - Use Slapi_RDN */
    } else {
        /* construct the new dn */
        char *pdn;
        const char *dn = slapi_sdn_get_dn(dn_olddn);
        pdn = slapi_dn_parent(dn);
        if (pdn != NULL) {
            newdn = slapi_dn_plus_rdn(pdn, newrdn); /* JCM - Use Slapi_RDN */
        } else {
            newdn = slapi_ch_strdup(newrdn);
        }
        slapi_ch_free((void **)&pdn);
    }
    return newdn;
}

/* JCM slapi_sdn_get_first ? */
/* JCM slapi_sdn_get_next ? */

#ifdef SDN_DEBUG
static void
sdn_dump(const Slapi_DN *sdn, const char *text)
{
    slapi_log_err(SLAPI_LOG_DEBUG, "sdn_dump", "SDN %s ptr=%lx dn=%s\n",
                  text, sdn, (sdn->dn == NULL ? "NULL" : sdn->dn));
}
#endif

size_t
slapi_sdn_get_size(const Slapi_DN *sdn)
{
    size_t sz = 0;
    /* slapi_sdn_get_ndn_len returns the normalized dn length
     * if dn or ndn exists.  If both does not exist, it
     * normalizes udn and set it to dn and returns the length.
     */
    if (NULL == sdn) {
        return sz;
    }
    sz += slapi_sdn_get_ndn_len(sdn) + 1 /* '\0' */;
    if (sdn->dn && sdn->ndn) {
        sz *= 2;
    }
    if (sdn->udn) {
        sz += strlen(sdn->udn) + 1;
    }
    sz += sizeof(Slapi_DN);
    return sz;
}

/*
 *
 *  Normalized DN Cache
 *
 */

/*
 * siphash13 provides a uint64_t hash of the incoming data. This means that in theory,
 * the hash is anything between 0 -> 0xFFFFFFFFFFFFFFFF. The result is distributed
 * evenly over the set of numbers, so we can assume that given some infinite set of
 * inputs, we'll have even distribution of all values across 0 -> 0xFFFFFFFFFFFFFFF.
 * In mathematics, given a set of integers, when you modulo them by a factor of the
 * input, the result retains the same distribution properties. IE, if you modulo the
 * numbers say 0 - 32 and they are evenly distributed, when do 0 -> 32 % 16, you will
 * still see even distribution (but duplicates now).
 *
 * So we use this property - we scale the number of slots by powers of 2 so that we
 * can do hash % slots and retain even distribution. IE:
 *
 * (0 -> 0xFFFFFFFFFFFFFFFF) % slots = ....
 *
 * This means we get even distribution: *but* it creates a scenario where we are more
 * likely to cause a collision as a result.
 *
 * Anyway, so lets imagine our small hashtable:
 *
 * *t_cache
 * -------------------------
 * |  0  |  1  |  2  |  3  |
 * -------------------------
 *
 * So any incoming DN will be put through:
 *
 * (0 -> 0xFFFFFFFFFFFFFFFF) % 4 = slot
 *
 * So lets say we add the values a, b, c (in that order)
 *
 * *t_cache
 * head: A
 * tail: C
 * -------------------------
 * |  0  |  1  |  2  |  3  |
 * -------------------------
 * -------------------------
 * |  A  |  B  |  C  |     |
 * |n:   |n: A |n: B |     |
 * |p: B |p: C |p:   |     |
 * -------------------------
 *
 * Because C was accessed (rather inserted) last, it's at the "tail". This is the
 * *most recently used* element. Because A was inserted first, it's the *least*
 * used. So lets do a look up on A:
 *
 * *t_cache
 * head: B
 * tail: A
 * -------------------------
 * |  0  |  1  |  2  |  3  |
 * -------------------------
 * -------------------------
 * |  A  |  B  |  C  |     |
 * |n: C |n:   |n: B |     |
 * |p:   |p: C |p: A |     |
 * -------------------------
 *
 * Because we did a lookup on A, we cut it out from the list and moved it to the tail.
 * This has pushed B to the head.
 *
 * Now lets say we need to do a removal to fit "D". We would remove elements from the
 * head until we have space. Here we just need to trim B.
 *
 * *t_cache
 * head: C
 * tail: D
 * -------------------------
 * |  0  |  1  |  2  |  3  |
 * -------------------------
 * -------------------------
 * |  A  |     |  C  |  D  |
 * |n: C |     |n:   |n: A |
 * |p: D |     |p: A |p:   |
 * -------------------------
 *
 * So we have removed B, and inserted D to the tail. Again, because A was more recently
 * read than C, C is now at the head. This process continues until the heat death of
 * the universe, or the server stops, what ever comes first (we write very stable code here ;)
 *
 * Now, lets discuss the "child" pointers in the nodes.
 *
 * Because of:
 *
 * (0 -> 0xFFFFFFFFFFFFFFFF) % 4 = slot
 *
 * The *smaller* the table, the more *likely* a hash collision is to occur (and in theory
 * even at a full table size they could still occur ...).
 *
 * So when you have a small table like this one I originally did *not* have the ability
 * to have multiple values per bucket, and just let the evictions take place. I did some
 * tests and found in this case, the LL behaviours were faster than repeated evictions.
 *
 * So lets say in our table we add another value, and it conflicts with the hash of C:
 *
 * *t_cache
 * head: C
 * tail: E
 * -------------------------
 * |  0  |  1  |  2  |  3  |
 * -------------------------
 * -------------------------
 * |  A  |     |  E  |  D  |
 * |n: C |     |n: D |n: A |
 * |p: D |     |p:   |p: E |
 * -------------------------
 *             |  C  |
 *             |n:   |
 *             |p: A |
 *             -------
 *
 * Now when we do a look up of "C" we'll collide on bucket 2, and then descend down til
 * we exhaust, or find our element. If we were to remove E, we would just promote C to
 * be the head of that slot.
 *
 * It's slightly quicker to insert at the head of the slot, and means that given we
 * *just* added the element, we are likely to use it again sooner, so we reduce the
 * number of comparisons.
 *
 * Again, I did test both with and without this - with was much faster, and relies on
 * how even our hash distribution is *and* that generally with small table sizes we
 * have small capacity, so we evict some values and keep these chains short.
 *
 */


#ifndef RUST_ENABLE
static pthread_key_t ndn_cache_key;
static pthread_once_t ndn_cache_key_once = PTHREAD_ONCE_INIT;
static struct ndn_cache_stats t_cache_stats = {0};
#endif
/*
 * WARNING: For some reason we try to use the NDN cache *before*
 * we have a chance to configure it. As a result, we need to rely
 * on a trick in the way we start, that we start in one thread
 * so we can manipulate ints as though they were atomics, then
 * we start in *one* thread, so it's set, then when threads
 * fork the get barriers, so we can go from there. However we *CANNOT*
 * change this at runtime without expensive atomics per op, so lets
 * not bother until we improve libglobs to be COW.
 */
static int32_t ndn_enabled = 0;
#ifdef RUST_ENABLE
static ARCacheChar *cache = NULL;
#endif

static struct ndn_cache *
ndn_thread_cache_create(size_t thread_max_size, size_t slots) {
    size_t t_cache_size = sizeof(struct ndn_cache) + (slots * sizeof(struct ndn_cache_value *));
    struct ndn_cache *t_cache = (struct ndn_cache *)slapi_ch_calloc(1, t_cache_size);

#ifdef RUST_ENABLE
    t_cache->cache = cache;
#else
    t_cache->max_size = thread_max_size;
    t_cache->slots = slots;
#endif

    return t_cache;
}

#ifdef RUST_ENABLE
#else
static void
ndn_thread_cache_commit_status(struct ndn_cache *t_cache) {
    /*
     * Every so often we commit these atomically. We do this infrequently
     * to avoid the costly atomics.
     */
    if (t_cache->tries % NDN_STAT_COMMIT_FREQUENCY == 0) {
        /* We can just add tries and hits. */
        slapi_counter_add(t_cache_stats.cache_evicts, t_cache->evicts);
        slapi_counter_add(t_cache_stats.cache_tries, t_cache->tries);
        slapi_counter_add(t_cache_stats.cache_hits, t_cache->hits);
        t_cache->hits = 0;
        t_cache->tries = 0;
        t_cache->evicts = 0;
        /* Count and size need diff */
        // Get the size from the main cache.
        int64_t diff = (t_cache->size - t_cache->last_size);
        if (diff > 0) {
            // We have more ....
            slapi_counter_add(t_cache_stats.cache_size, (uint64_t)diff);
        } else if (diff < 0) {
            slapi_counter_subtract(t_cache_stats.cache_size, (uint64_t)llabs(diff));
        }
        t_cache->last_size = t_cache->size;

        diff = (t_cache->count - t_cache->last_count);
        if (diff > 0) {
            // We have more ....
            slapi_counter_add(t_cache_stats.cache_count, (uint64_t)diff);
        } else if (diff < 0) {
            slapi_counter_subtract(t_cache_stats.cache_count, (uint64_t)llabs(diff));
        }
        t_cache->last_count = t_cache->count;

    }
}
#endif

#ifndef RUST_ENABLE
static void
ndn_thread_cache_value_destroy(struct ndn_cache *t_cache, struct ndn_cache_value *v) {
    /* Update stats */
    t_cache->size = t_cache->size - v->size;
    t_cache->count--;
    t_cache->evicts++;

    if (v == t_cache->head) {
        t_cache->head = v->prev;
    }
    if (v == t_cache->tail) {
        t_cache->tail = v->next;
    }

    /* Cut the node out. */
    if (v->next != NULL) {
        v->next->prev = v->prev;
    }
    if (v->prev != NULL) {
        v->prev->next = v->next;
    }
    /* Set the pointer in the table to NULL */
    /* Now see if we were in a list */
    struct ndn_cache_value *slot_node = t_cache->table[v->slot];
    if (slot_node == v) {
        t_cache->table[v->slot] = v->child;
    } else {
        struct ndn_cache_value *former_slot_node = NULL;
        do {
            former_slot_node = slot_node;
            slot_node = slot_node->child;
        } while(slot_node != v);
        /* Okay, now slot_node is us, and former is our parent */
        former_slot_node->child = v->child;
    }

    slapi_ch_free((void **)&(v->dn));
    slapi_ch_free((void **)&(v->ndn));
    slapi_ch_free((void **)&v);
}
#endif

static void
ndn_thread_cache_destroy(void *v_cache) {
    struct ndn_cache *t_cache = (struct ndn_cache *)v_cache;
#ifndef RUST_ENABLE
    /*
     * FREE ALL THE NODES!!!
     */
    struct ndn_cache_value *node = t_cache->tail;
    struct ndn_cache_value *next_node = NULL;
    while (node) {
        next_node = node->next;
        ndn_thread_cache_value_destroy(t_cache, node);
        node = next_node;
    }
#endif
    slapi_ch_free((void **)&t_cache);
}

#ifndef RUST_ENABLE
static void
ndn_cache_key_init() {
    if (pthread_key_create(&ndn_cache_key, ndn_thread_cache_destroy) != 0) {
        /* Log a scary warning? */
        slapi_log_err(SLAPI_LOG_ERR, "ndn_cache_init", "Failed to create pthread key, aborting.\n");
    }
}
#endif

int32_t
ndn_cache_init()
{
    ndn_enabled = config_get_ndn_cache_enabled();
    if (ndn_enabled == 0) {
        /*
         * Don't configure the keys or anything, need a restart
         * to enable. We'll just never use ndn cache in this
         * run.
         */
        return 0;
    }

#ifdef RUST_ENABLE
    uint64_t max_size = config_get_ndn_cache_size();
    if (max_size < NDN_CACHE_MINIMUM_CAPACITY) {
        max_size = NDN_CACHE_MINIMUM_CAPACITY;
    }
    uintptr_t max_estimate = max_size / NDN_ENTRY_AVG_SIZE;
    /*
     * Since we currently only do one op per read, we set 0 because there
     * is no value in having the read thread cache.
     */
    uintptr_t max_thread_read = 0;
    /* Setup the main cache which all other caches will inherit. */
    cache = cache_char_create(max_estimate, max_thread_read);
#else
    /* Create the pthread key */
    (void)pthread_once(&ndn_cache_key_once, ndn_cache_key_init);
    /* Get thread numbers and calc the per thread size */
    int32_t maxthreads = (int32_t)config_get_threadnumber();
    size_t tentative_size = t_cache_stats.max_size / maxthreads;
    if (tentative_size < NDN_CACHE_MINIMUM_CAPACITY) {
        tentative_size = NDN_CACHE_MINIMUM_CAPACITY;
        t_cache_stats.max_size = NDN_CACHE_MINIMUM_CAPACITY * maxthreads;
    }
    t_cache_stats.thread_max_size = tentative_size;

    /*
     * Slots *must* be a power of two, even if the number of entries
     * we store will be *less* than this.
     */
    size_t possible_elements = tentative_size / NDN_ENTRY_AVG_SIZE;
    /*
     * So this is like 1048576 / 168, so we get 6241. Now we need to
     * shift this to get the number of bits.
     */
    size_t shifts = 0;
    while (possible_elements > 0) {
        shifts++;
        possible_elements = possible_elements >> 1;
    }
    /*
     * So now we can use this to make the slot count.
     */
    t_cache_stats.slots = 1 << shifts;
    /* Create the global stats. */
    t_cache_stats.max_size = config_get_ndn_cache_size();
    t_cache_stats.cache_evicts = slapi_counter_new();
    t_cache_stats.cache_tries = slapi_counter_new();
    t_cache_stats.cache_hits = slapi_counter_new();
    t_cache_stats.cache_count = slapi_counter_new();
    t_cache_stats.cache_size = slapi_counter_new();
#endif
    /* Done? */
    return 0;
}

void
ndn_cache_destroy()
{
    if (ndn_enabled == 0) {
        return;
    }
#ifdef RUST_ENABLE
    cache_char_free(cache);
#else
    slapi_counter_destroy(&(t_cache_stats.cache_tries));
    slapi_counter_destroy(&(t_cache_stats.cache_hits));
    slapi_counter_destroy(&(t_cache_stats.cache_count));
    slapi_counter_destroy(&(t_cache_stats.cache_size));
    slapi_counter_destroy(&(t_cache_stats.cache_evicts));
#endif
}

int
ndn_cache_started()
{
    return ndn_enabled;
}

/*
 *  Look up this dn in the ndn cache
 */
#ifdef RUST_ENABLE
/* This is the rust version of the ndn cache */

static int32_t
ndn_cache_lookup(char *dn, size_t dn_len, char **ndn, char **udn, int32_t *rc)
{
    if (ndn_enabled == 0 || NULL == udn) {
        return 0;
    }
    *udn = NULL;

    if (dn_len == 0) {
        *ndn = dn;
        *rc = 0;
        return 1;
    }

    /* Look for it */
    ARCacheCharRead *read_txn = cache_char_read_begin(cache);
    PR_ASSERT(read_txn);

    const char *cache_ndn = cache_char_read_get(read_txn, dn);
    if (cache_ndn != NULL) {
        *ndn = slapi_ch_strdup(cache_ndn);
        /*
         * We have to complete the read after the strdup else it's
         * not safe to access the pointer.
         */
        cache_char_read_complete(read_txn);
        *rc = 1;
        return 1;
    } else {
        cache_char_read_complete(read_txn);
        /* If we miss, we need to duplicate dn to udn here. */
        *udn = slapi_ch_strdup(dn);
        *rc = 0;
        return 0;
    }
}

static void
ndn_cache_add(char *dn, size_t dn_len, char *ndn, size_t ndn_len)
{
    if (ndn_enabled == 0) {
        return;
    }
    if (dn_len == 0) {
        return;
    }
    if (strlen(ndn) > ndn_len) {
        /* we need to null terminate the ndn */
        *(ndn + ndn_len) = '\0';
    }

    ARCacheCharRead *read_txn = cache_char_read_begin(cache);
    PR_ASSERT(read_txn);
    cache_char_read_include(read_txn, dn, ndn);
    cache_char_read_complete(read_txn);
}

#else
static int
ndn_cache_lookup(char *dn, size_t dn_len, char **ndn, char **udn, int *rc)
{
    if (ndn_enabled == 0 || NULL == udn) {
        return 0;
    }
    *udn = NULL;

    if (dn_len == 0) {
        *ndn = dn;
        *rc = 0;
        return 1;
    }

    struct ndn_cache *t_cache = pthread_getspecific(ndn_cache_key);
    if (t_cache == NULL) {
        t_cache = ndn_thread_cache_create(t_cache_stats.thread_max_size, t_cache_stats.slots);
        pthread_setspecific(ndn_cache_key, t_cache);
        /* If we have no cache, we can't look up ... */
        return 0;
    }

    t_cache->tries++;

    /*
     * Hash our DN ...
     */
    uint64_t dn_hash = sds_siphash13(dn, dn_len, t_cache->key);
    /* Where should it be? */
    size_t expect_slot = dn_hash % t_cache->slots;

    /*
     * Is it there?
     */
    if (t_cache->table[expect_slot] != NULL) {
        /*
         * Check it really matches, could be collision.
         */
        struct ndn_cache_value *node = t_cache->table[expect_slot];
        while (node != NULL) {
            if (strcmp(dn, node->dn) == 0) {
                /*
                 * Update LRU
                 * Are we already the tail? If so, we can just skip.
                 * remember, this means in a set of 1, we will always be tail
                 */
                if (t_cache->tail != node) {
                    /*
                     * Okay, we are *not* the tail. We could be anywhere between
                     * tail -> ... -> x -> head
                     * or even, we are the head ourself.
                     */
                    if (t_cache->head == node) {
                        /* We are the head, update head to our predecessor */
                        t_cache->head = node->prev;
                        /* Remember, the head has no next. */
                        t_cache->head->next = NULL;
                    } else {
                        /* Right, we aren't the head, so we have a next node. */
                        node->next->prev = node->prev;
                    }
                    /* Because we must be in the middle somewhere, we can assume next and prev exist. */
                    node->prev->next = node->next;
                    /*
                     * Tail can't be NULL if we have a value in the cache, so we can
                     * just deref this.
                     */
                    node->next = t_cache->tail;
                    t_cache->tail->prev = node;
                    t_cache->tail = node;
                    node->prev = NULL;
                }

                /* Update that we have a hit.*/
                t_cache->hits++;
                /* Cope the NDN to the caller. */
                *ndn = slapi_ch_strdup(node->ndn);
                /* Indicate to the caller to free this. */
                *rc = 1;
                ndn_thread_cache_commit_status(t_cache);
                return 1;
            }
            node = node->child;
        }
    }
    /* If we miss, we need to duplicate dn to udn here. */
    *udn = slapi_ch_strdup(dn);
    *rc = 0;
    ndn_thread_cache_commit_status(t_cache);
    return 0;
}

/*
 *  Add a ndn to the cache.  Try and do as much as possible before taking the write lock.
 */
static void
ndn_cache_add(char *dn, size_t dn_len, char *ndn, size_t ndn_len)
{
    if (ndn_enabled == 0) {
        return;
    }
    if (dn_len == 0) {
        return;
    }
    if (strlen(ndn) > ndn_len) {
        /* we need to null terminate the ndn */
        *(ndn + ndn_len) = '\0';
    }
    /*
     *  Calculate the approximate memory footprint of the hash entry, key, and lru entry.
     */
    struct ndn_cache_value *new_value = (struct ndn_cache_value *)slapi_ch_calloc(1, sizeof(struct ndn_cache_value));
    new_value->size = sizeof(struct ndn_cache_value) + dn_len + ndn_len;
    /* DN is alloc for us */
    new_value->dn = dn;
    /* But we need to copy ndn */
    new_value->ndn = slapi_ch_strdup(ndn);

    /*
     * Get our local cache out.
     */
    struct ndn_cache *t_cache = pthread_getspecific(ndn_cache_key);
    if (t_cache == NULL) {
        t_cache = ndn_thread_cache_create(t_cache_stats.thread_max_size, t_cache_stats.slots);
        pthread_setspecific(ndn_cache_key, t_cache);
    }
    /*
     * Hash the DN
     */
    uint64_t dn_hash = sds_siphash13(new_value->dn, dn_len, t_cache->key);
    /*
     * Get the insert slot: This works because the number spaces of dn_hash is
     * a 64bit int, and slots is a power of two. As a result, we end up with
     * even distribution of the values.
     */
    size_t insert_slot = dn_hash % t_cache->slots;
    /* Track this for free */
    new_value->slot = insert_slot;

    /*
     * Okay, check if we have space, else we need to trim nodes from
     * the LRU
     */
    while (t_cache->head && (t_cache->size + new_value->size) > t_cache->max_size) {
        struct ndn_cache_value *trim_node = t_cache->head;
        ndn_thread_cache_value_destroy(t_cache, trim_node);
    }

    /*
     * Add it!
     */
    if (t_cache->table[insert_slot] == NULL) {
        t_cache->table[insert_slot] = new_value;
    } else {
        /*
         * Hash collision! We need to replace the bucket then ....
         * insert at the head of the slot to make this simpler.
         */
        new_value->child = t_cache->table[insert_slot];
        t_cache->table[insert_slot] = new_value;
    }

    /*
     * Finally, stick this onto the tail because it's the newest.
     */
    if (t_cache->head == NULL) {
        t_cache->head = new_value;
    }
    if (t_cache->tail != NULL) {
        new_value->next = t_cache->tail;
        t_cache->tail->prev = new_value;
    }
    t_cache->tail = new_value;

    /*
     * And update the stats.
     */
    t_cache->size = t_cache->size + new_value->size;
    t_cache->count++;

}
#endif
/* end rust_enable */

/* stats for monitor */
void
ndn_cache_get_stats(uint64_t *hits, uint64_t *tries, uint64_t *size, uint64_t *max_size, uint64_t *thread_size, uint64_t *evicts, uint64_t *slots, uint64_t *count)
{
#ifdef RUST_ENABLE
    /*
     * A pretty big note here - the ARCache stores things by slot, not size, because
     * getting the real byte size is expensive in some cases (that are beyond this
     * project). Additionally, due to the concurrent nature of this, the size is
     * not really accurate to begin with anyway, but you know, this is a best
     * effort for stats for the user to see.
     */
    uint64_t reader_hits;
    uint64_t reader_includes;
    uint64_t write_hits;
    uint64_t write_inc_or_mod;
    uint64_t freq;
    uint64_t recent;
    uint64_t freq_evicts;
    uint64_t recent_evicts;
    uint64_t p_weight;
    cache_char_stats(cache, 
        &reader_hits,
        &reader_includes,
        &write_hits,
        &write_inc_or_mod,
        max_size,
        &freq,
        &recent,
        &freq_evicts,
        &recent_evicts,
        &p_weight,
        slots
    );
    /* ARCache stores by key count not size, so we recombine with the avg entry size */
    *max_size = *max_size * NDN_ENTRY_AVG_SIZE;
    *thread_size = 0;
    *evicts = freq_evicts + recent_evicts;
    *hits = write_hits + reader_hits;
    *tries = *hits + reader_includes + write_inc_or_mod;
    *size = (freq + recent) * NDN_ENTRY_AVG_SIZE;
    *count = (freq + recent);
#else
    *max_size = t_cache_stats.max_size;
    *thread_size = t_cache_stats.thread_max_size;
    *slots = t_cache_stats.slots;
    *evicts = slapi_counter_get_value(t_cache_stats.cache_evicts);
    *hits = slapi_counter_get_value(t_cache_stats.cache_hits);
    *tries = slapi_counter_get_value(t_cache_stats.cache_tries);
    *size = slapi_counter_get_value(t_cache_stats.cache_size);
    *count = slapi_counter_get_value(t_cache_stats.cache_count);
#endif
}

/* Common ancestor sdn is allocated.
 * caller is responsible to free it */
Slapi_DN *
slapi_sdn_common_ancestor(Slapi_DN *dn1, Slapi_DN *dn2)
{
    const char *dn1str = NULL;
    const char *dn2str = NULL;
    char **dns1 = NULL;
    char **dns2 = NULL;
    char **dn1p, **dn2p;
    char **dn1end;
    int dn1len = 0;
    int dn2len = 0;
    char *common = NULL;
    char *cp = 0;
    if ((NULL == dn1) || (NULL == dn2)) {
        return NULL;
    }
    dn1str = slapi_sdn_get_ndn(dn1);
    dn2str = slapi_sdn_get_ndn(dn2);
    if (0 == strcmp(dn1str, dn2str)) {
        /* identical */
        return slapi_sdn_dup(dn1);
    }
    dn1len = strlen(dn1str);
    dn2len = strlen(dn2str);
    if (dn1len > dn2len) {
        if (slapi_sdn_isparent(dn2, dn1)) {
            /* dn2 is dn1's parent */
            return slapi_sdn_dup(dn2);
        }
    } else if (dn1len < dn2len) {
        if (slapi_sdn_isparent(dn1, dn2)) {
            /* dn1 is dn2's parent */
            return slapi_sdn_dup(dn1);
        }
    }
    dns1 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(dn1), 0);
    dns2 = slapi_ldap_explode_dn(slapi_sdn_get_ndn(dn2), 0);
    for (dn1p = dns1; dn1p && *dn1p; dn1p++)
        ;
    for (dn2p = dns2; dn2p && *dn2p; dn2p++)
        ;
    dn1end = dn1p;
    while (--dn1p && --dn2p && (dn1p >= dns1) && (dn2p >= dns2)) {
        if (strcmp(*dn1p, *dn2p)) {
            break;
        }
    }
    if (dn1end == ++dn1p) {
        /* No common ancestor */
        charray_free(dns1);
        charray_free(dns2);
        return NULL;
    }
    dn1len += 1;
    cp = common = slapi_ch_malloc(dn1len);
    *common = '\0';
    do {
        PR_snprintf(cp, dn1len, "%s,", *dn1p);
        cp += strlen(*dn1p) + 1 /*,*/;
    } while (++dn1p < dn1end);
    dn1len = strlen(common);
    if (',' == *(common + dn1len - 1)) {
        *(common + dn1len - 1) = '\0';
    }
    charray_free(dns1);
    charray_free(dns2);
    return slapi_sdn_new_ndn_passin(common);
}

/*
 * Return 1 - if nsslapd-cn-uses-dn-syntax-in-dns is true &&
 *            the type is "cn" && dn is under "cn=config"
 * Return 0 - otherwise
 */
static int
does_cn_uses_dn_syntax_in_dns(char *type, char *dn)
{
    int rc = 0; /* by default off */
    char *ptr = NULL;
    if (type && dn && config_get_cn_uses_dn_syntax_in_dns() &&
        (PL_strcasecmp(type, "cn") == 0) && (ptr = PL_strrchr(dn, ','))) {
        if (PL_strcasecmp(++ptr, "cn=config") == 0) {
            rc = 1;
        }
    }
    return rc;
}
