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

/* filter.c - routines for parsing and dealing with filters */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include "slap.h"
#include "slapi-plugin.h"

static int
get_filter_list(Connection *conn, BerElement *ber, struct slapi_filter **f, char **fstr, int maxdepth, int curdepth, int *subentry_dont_rewrite, int *has_tombstone_filter, int *has_ruv_filter);
static int
get_substring_filter(Connection *conn, BerElement *ber, struct slapi_filter *f, char **fstr);
static int get_extensible_filter(BerElement *ber, mr_filter_t *);

static int get_filter_internal(Connection *conn, BerElement *ber, struct slapi_filter **filt, char **fstr, int maxdepth, int curdepth, int *subentry_dont_rewrite, int *has_tombstone_filter, int *has_ruv_filter);
static int tombstone_check_filter(Slapi_Filter *f);
static int ruv_check_filter(Slapi_Filter *f);


/*
 * Read a filter off the wire and create a slapi_filter and string representation.
 * Both filt and fstr are allocated by this function, so must be freed by the caller.
 *
 * If the scope is not base and (objectclass=ldapsubentry) does not occur
 * in the filter then we add (!(objectclass=ldapsubentry)) to the filter
 * so that subentries are not returned.
 * If the scope is base or (objectclass=ldapsubentry) occurs in the filter,
 * then the caller is explicitly handling subentries himself and so we leave
 * the filter as is.
 */
int
get_filter(Connection *conn, BerElement *ber, int scope, struct slapi_filter **filt, char **fstr)
{
    int subentry_dont_rewrite = 0; /* Re-write unless we're told not to */
    int has_tombstone_filter = 0;  /* Check if nsTombstone appears */
    int has_ruv_filter = 0;        /* Check if searching for RUV */
    int return_value = 0;
    char *logbuf = NULL;
    size_t logbufsize = 0;

    return_value = get_filter_internal(conn, ber, filt, fstr,
                                       config_get_max_filter_nest_level(), /* maximum depth */
                                       0, /* current depth */ &subentry_dont_rewrite,
                                       &has_tombstone_filter, &has_ruv_filter);

    if (0 == return_value) { /* Don't try to re-write if there was an error */
        if (subentry_dont_rewrite || scope == LDAP_SCOPE_BASE) {
            (*filt)->f_flags |= SLAPI_FILTER_LDAPSUBENTRY;
        }
        if (has_tombstone_filter) {
            (*filt)->f_flags |= SLAPI_FILTER_TOMBSTONE;
        }
        if (has_ruv_filter) {
            (*filt)->f_flags |= SLAPI_FILTER_RUV;
        }
    }

    if (loglevel_is_set(LDAP_DEBUG_FILTER) && *filt != NULL && *fstr != NULL) {
        logbufsize = strlen(*fstr) + 1;
        logbuf = slapi_ch_malloc(logbufsize);
        *logbuf = '\0';
        slapi_log_err(SLAPI_LOG_DEBUG, "get_filter", "before optimize: %s\n",
                      slapi_filter_to_string(*filt, logbuf, logbufsize));
    }

    /*
     * Filter optimise has been moved to the onelevel/subtree candidate dispatch.
     * this is because they inject referrals or other business, that we can optimise
     * and improve.
     */
    /* filter_optimize(*filt); */

    if (NULL != logbuf) {
        slapi_log_err(SLAPI_LOG_DEBUG, "get_filter", " after optimize: %s\n",
                      slapi_filter_to_string(*filt, logbuf, logbufsize));
        slapi_ch_free_string(&logbuf);
    }

    return return_value;
}


#define FILTER_EQ_FMT "(%s=%s%s)"
#define FILTER_GE_FMT "(%s>=%s%s)"
#define FILTER_LE_FMT "(%s<=%s%s)"
#define FILTER_APROX_FMT "(%s~=%s%s)"
#define FILTER_EXTENDED_FMT "(%s%s%s%s:=%s%s)"
#define FILTER_EQ_LEN 4
#define FILTER_GE_LEN 5
#define FILTER_LE_LEN 5
#define FILTER_APROX_LEN 5


/* returns escaped filter string for extended filters only*/

static char *
filter_escape_filter_value_extended(struct slapi_filter *f)
{
    char *ptr;

    ptr = slapi_filter_sprintf(FILTER_EXTENDED_FMT,
                               f->f_mr_type ? f->f_mr_type : "",
                               f->f_mr_dnAttrs ? ":dn" : "",
                               f->f_mr_oid ? ":" : "",
                               f->f_mr_oid ? f->f_mr_oid : "",
                               ESC_NEXT_VAL, f->f_mr_value.bv_val);
    return ptr;
}

/* returns escaped filter string for EQ, LE, GE and APROX filters */

static char *
filter_escape_filter_value(struct slapi_filter *f, const char *fmt, size_t len __attribute__((unused)))
{
    char *ptr;

    filter_compute_hash(f);
    ptr = slapi_filter_sprintf(fmt, f->f_avtype, ESC_NEXT_VAL, f->f_avvalue.bv_val);

    return ptr;
}

/* Escaped an equality filter value (assertionValue) of a given attribute
 * Caller must free allocated escaped filter value
 */
char *
slapi_filter_escape_filter_value(char* filter_attr, char *filter_value)
{
    char *result;
    struct slapi_filter *f;

    if ((filter_attr == NULL) || (filter_value == NULL)) {
        return NULL;
    }
    f = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));
    f->f_choice = LDAP_FILTER_EQUALITY;
    f->f_un.f_un_ava.ava_type = filter_attr;
    f->f_un.f_un_ava.ava_value.bv_len = strlen(filter_value);
    f->f_un.f_un_ava.ava_value.bv_val = filter_value;
    result = filter_escape_filter_value(f, FILTER_EQ_FMT, FILTER_EQ_LEN);
    slapi_ch_free((void**) &f);
    return result;
}

/*
 * get_filter_internal(): extract an LDAP filter from a BerElement and create
 *    a slapi_filter structure (*filt) and a string equivalent (*fstr).
 *
 * This function is recursive. It calls itself (to process NOT filters) and
 *    it calls get_filter_list() for AND and OR filters, and get_filter_list()
 *    calls this function again.
 */
static int
get_filter_internal(Connection *conn, BerElement *ber, struct slapi_filter **filt, char **fstr, int maxdepth, int curdepth, int *subentry_dont_rewrite, int *has_tombstone_filter, int *has_ruv_filter)
{
    ber_len_t len;
    int err;
    struct slapi_filter *f;
    char *ftmp, *type = NULL;

    slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "==>\n");

    /*
     * Track and check the depth of nesting.  Use post-increment on
     * current depth here because this function is called for the
     * top-level filter (which does not count towards the maximum depth).
     */
    if ((curdepth++ > maxdepth) && (maxdepth > 0)) {
        *filt = NULL;
        *fstr = NULL;
        err = LDAP_UNWILLING_TO_PERFORM;
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal",
                      "<== %d (maximum nesting level of %d exceeded)\n",
                      err, maxdepth);
        return (err);
    }

    /*
     * A filter looks like this coming in:
     *    Filter ::= CHOICE {
     *        and        [0]    SET OF Filter,
     *        or        [1]    SET OF Filter,
     *        not        [2]    Filter,
     *        equalityMatch    [3]    AttributeValueAssertion,
     *        substrings    [4]    SubstringFilter,
     *        greaterOrEqual    [5]    AttributeValueAssertion,
     *        lessOrEqual    [6]    AttributeValueAssertion,
     *        present        [7]    AttributeType,
     *        approxMatch    [8]    AttributeValueAssertion,
     *        extensibleMatch    [9]    MatchingRuleAssertion --v3 only
     *    }
     *
     *    SubstringFilter ::= SEQUENCE {
     *        type               AttributeType,
     *        SEQUENCE OF CHOICE {
     *            initial          [0] IA5String,
     *            any              [1] IA5String,
     *            final            [2] IA5String
     *        }
     *    }
     *
     * The extensibleMatch was added in LDAPv3:
     *
     *    MatchingRuleAssertion ::= SEQUENCE {
     *        matchingRule    [1] MatchingRuleID OPTIONAL,
     *        type        [2] AttributeDescription OPTIONAL,
     *        matchValue    [3] AssertionValue,
     *        dnAttributes    [4] BOOLEAN DEFAULT FALSE
     *    }
     */

    f = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));

    err = 0;
    *fstr = NULL;
    f->f_choice = ber_peek_tag(ber, &len);
    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "EQUALITY\n");
        if ((err = get_ava(ber, &f->f_ava)) == 0) {

            if (0 == strcasecmp(f->f_avtype, "objectclass")) {
                /* Process objectclass oid's here */
                if (strchr(f->f_avvalue.bv_val, '.')) {
                    char *ocname = oc_find_name(f->f_avvalue.bv_val);

                    if (NULL != ocname) {
                        slapi_ch_free((void **)&f->f_avvalue.bv_val);
                        f->f_avvalue.bv_val = ocname;
                        f->f_avvalue.bv_len = strlen(f->f_avvalue.bv_val);
                    }
                }

                /*
                 * Process subentry searches here.
                 * Only set (*subentry_dont_rewrite) if it's not already set.
                 */

                if (!(*subentry_dont_rewrite)) {
                    *subentry_dont_rewrite = subentry_check_filter(f);
                }
                /*
                 * Check if it's a Tomstone filter.
                 * We need to do it once per filter, so if flag is already set,
                 * don't bother doing it
                 */
                if (!(*has_tombstone_filter)) {
                    *has_tombstone_filter = tombstone_check_filter(f);
                }
            }

            if (0 == strcasecmp(f->f_avtype, "nsuniqueid")) {
                /*
                 * Check if it's a RUV filter.
                 * We need to do it once per filter, so if flag is already set,
                 * don't bother doing it
                 */
                if (!(*has_ruv_filter)) {
                    *has_ruv_filter = ruv_check_filter(f);
                }
            }

            *fstr = filter_escape_filter_value(f, FILTER_EQ_FMT, FILTER_EQ_LEN);
        }
        break;

    case LDAP_FILTER_SUBSTRINGS:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "SUBSTRINGS\n");
        err = get_substring_filter(conn, ber, f, fstr);
        break;

    case LDAP_FILTER_GE:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "GE\n");
        if ((err = get_ava(ber, &f->f_ava)) == 0) {
            *fstr = filter_escape_filter_value(f, FILTER_GE_FMT, FILTER_GE_LEN);
        }
        break;

    case LDAP_FILTER_LE:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "LE\n");
        if ((err = get_ava(ber, &f->f_ava)) == 0) {
            *fstr = filter_escape_filter_value(f, FILTER_LE_FMT, FILTER_LE_LEN);
        }
        break;

    case LDAP_FILTER_PRESENT:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "PRESENT\n");
        if (ber_scanf(ber, "a", &type) == LBER_ERROR) {
            slapi_ch_free_string(&type);
            err = LDAP_PROTOCOL_ERROR;
        } else {
            err = LDAP_SUCCESS;
            f->f_type = slapi_attr_syntax_normalize(type);
            slapi_ch_free_string(&type);
            filter_compute_hash(f);
            *fstr = slapi_ch_smprintf("(%s=*)", f->f_type);
        }
        break;

    case LDAP_FILTER_APPROX:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "APPROX\n");
        if ((err = get_ava(ber, &f->f_ava)) == 0) {
            *fstr = filter_escape_filter_value(f, FILTER_APROX_FMT, FILTER_APROX_LEN);
        }
        break;

    case LDAP_FILTER_EXTENDED:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "EXTENDED\n");
        if (conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "get_filter_internal",
                          "NULL param: conn (0x%p)\n", conn);
            err = LDAP_OPERATIONS_ERROR;
        } else if (conn->c_ldapversion < 3) {
            slapi_log_err(SLAPI_LOG_ERR, "get_filter_internal",
                          "Extensible filter received from v2 client\n");
            err = LDAP_PROTOCOL_ERROR;
        } else if ((err = get_extensible_filter(ber, &f->f_mr)) == LDAP_SUCCESS) {
            *fstr = filter_escape_filter_value_extended(f);
            slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "%s\n", *fstr);
            if (f->f_mr_oid == NULL) {
                /*
            * We accept:
            * A) attr ":=" value
            * B) attr ":dn" ":=" value
                */
                err = LDAP_SUCCESS;
            } else {
                err = plugin_mr_filter_create(&f->f_mr);
            }
        }
        break;

    case LDAP_FILTER_AND:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "AND\n");
        if ((err = get_filter_list(conn, ber, &f->f_and, &ftmp, maxdepth,
                                   curdepth, subentry_dont_rewrite,
                                   has_tombstone_filter, has_ruv_filter)) == 0) {
            filter_compute_hash(f);
            *fstr = slapi_ch_smprintf("(&%s)", ftmp);
            slapi_ch_free((void **)&ftmp);
        }
        break;

    case LDAP_FILTER_OR:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "OR\n");
        if ((err = get_filter_list(conn, ber, &f->f_or, &ftmp, maxdepth,
                                   curdepth, subentry_dont_rewrite,
                                   has_tombstone_filter, has_ruv_filter)) == 0) {
            filter_compute_hash(f);
            *fstr = slapi_ch_smprintf("(|%s)", ftmp);
            slapi_ch_free((void **)&ftmp);
        }
        break;

    case LDAP_FILTER_NOT:
        slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "NOT\n");
        (void)ber_skip_tag(ber, &len);
        if ((err = get_filter_internal(conn, ber, &f->f_not, &ftmp, maxdepth,
                                       curdepth, subentry_dont_rewrite,
                                       has_tombstone_filter, has_ruv_filter)) == 0) {
            filter_compute_hash(f);
            *fstr = slapi_ch_smprintf("(!%s)", ftmp);
            slapi_ch_free((void **)&ftmp);
        }
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, "get_filter_internal",
                      "Unknown type 0x%lX\n", f->f_choice);
        err = LDAP_PROTOCOL_ERROR;
        break;
    }

    if (err != 0) {
        slapi_filter_free(f, 1);
        f = NULL;
        slapi_ch_free((void **)fstr);
    }
    *filt = f;
    slapi_log_err(SLAPI_LOG_FILTER, "get_filter_internal", "<= %d\n", err);
    return (err);
}

static int
get_filter_list(Connection *conn, BerElement *ber, struct slapi_filter **f, char **fstr, int maxdepth, int curdepth, int *subentry_dont_rewrite, int *has_tombstone_filter, int *has_ruv_filter)
{
    struct slapi_filter **new;
    int err;
    ber_tag_t tag;
    ber_len_t len = LBER_ERROR;
    char *last;

    slapi_log_err(SLAPI_LOG_FILTER, "get_filter_list", "=>\n");

    *fstr = NULL;
    new = f;
    for (tag = ber_first_element(ber, &len, &last);
         tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
         tag = ber_next_element(ber, &len, last)) {
        char *ftmp;
        if ((err = get_filter_internal(conn, ber, new, &ftmp, maxdepth,
                                       curdepth, subentry_dont_rewrite,
                                       has_tombstone_filter, has_ruv_filter)) != 0) {
            if (*fstr != NULL) {
                slapi_ch_free((void **)fstr);
            }
            return (err);
        }
        if (*fstr == NULL) {
            *fstr = ftmp;
        } else {
            *fstr = slapi_ch_realloc(*fstr, strlen(*fstr) +
                                                strlen(ftmp) + 1);
            strcat(*fstr, ftmp);
            slapi_ch_free((void **)&ftmp);
        }
        new = &(*new)->f_next;
        len = -1;
    }
    *new = NULL;

    /* openldap does not return LBER_END_OF_SEQORSET -
       so check for len == -1 - openldap ber_next_element will not set
       len if it has reached the end, and -1 is not a valid value
       for a real len */
    if ((tag != LBER_END_OF_SEQORSET) && (len != -1) && (*fstr != NULL)) {
        slapi_log_err(SLAPI_LOG_ERR, "get_filter_list", "Error parsing filter list\n");
        slapi_ch_free((void **)fstr);
    }

    slapi_log_err(SLAPI_LOG_FILTER, "get_filter_list", "<=\n");
    return ((*fstr == NULL) ? LDAP_PROTOCOL_ERROR : 0);
}

static int
get_substring_filter(
    Connection *conn __attribute__((unused)),
    BerElement *ber,
    struct slapi_filter *f,
    char **fstr)
{
    ber_tag_t tag, rc;
    ber_len_t len = -1;
    char *val, *eval, *last, *type = NULL;
    size_t fstr_len;

    slapi_log_err(SLAPI_LOG_FILTER, "get_substring_filter", "=>\n");

    if (ber_scanf(ber, "{a", &type) == LBER_ERROR) {
        slapi_ch_free_string(&type);
        return (LDAP_PROTOCOL_ERROR);
    }
    f->f_sub_type = slapi_attr_syntax_normalize(type);
    slapi_ch_free_string(&type);
    f->f_sub_initial = NULL;
    f->f_sub_any = NULL;
    f->f_sub_final = NULL;

    /* borrowing the handy macro: 256 */
    fstr_len = strlen(f->f_sub_type) + SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH;
    *fstr = slapi_ch_malloc(fstr_len);
    sprintf(*fstr, "(%s=", f->f_sub_type);
    for (tag = ber_first_element(ber, &len, &last);
         tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
         tag = ber_next_element(ber, &len, last)) {
        len = -1; /* reset - not used in loop */
        val = NULL;
        rc = ber_scanf(ber, "a", &val);
        if (rc == LBER_ERROR) {
            slapi_ch_free_string(&val);
            return (LDAP_PROTOCOL_ERROR);
        }
        if (val == NULL || *val == '\0') {
            if (val != NULL) {
                slapi_ch_free_string(&val);
            }
            return (LDAP_INVALID_SYNTAX);
        }

        switch (tag) {
        case LDAP_SUBSTRING_INITIAL:
            slapi_log_err(SLAPI_LOG_FILTER, "get_substring_filter", "INITIAL\n");
            if (f->f_sub_initial != NULL) {
                return (LDAP_PROTOCOL_ERROR);
            }
            f->f_sub_initial = val;
            eval = (char *)slapi_escape_filter_value(val, -1);
            if (eval) {
                if (fstr_len <= strlen(*fstr) + strlen(eval) + 1) {
                    fstr_len += (strlen(eval) + 1) * 2;
                    *fstr = slapi_ch_realloc(*fstr, fstr_len);
                }
                strcat(*fstr, eval);
                slapi_ch_free_string(&eval);
            }
            break;

        case LDAP_SUBSTRING_ANY:
            slapi_log_err(SLAPI_LOG_FILTER, "get_substring_filter", "ANY\n");
            charray_add(&f->f_sub_any, val);
            eval = (char *)slapi_escape_filter_value(val, -1);
            if (eval) {
                if (fstr_len <= strlen(*fstr) + strlen(eval) + 1) {
                    fstr_len += (strlen(eval) + 1) * 2;
                    *fstr = slapi_ch_realloc(*fstr, fstr_len);
                }
                strcat(*fstr, "*");
                strcat(*fstr, eval);
                slapi_ch_free_string(&eval);
            }
            break;

        case LDAP_SUBSTRING_FINAL:
            slapi_log_err(SLAPI_LOG_FILTER, "get_substring_filter", "FINAL\n");
            if (f->f_sub_final != NULL) {
                return (LDAP_PROTOCOL_ERROR);
            }
            f->f_sub_final = val;
            eval = (char *)slapi_escape_filter_value(val, -1);
            if (eval) {
                if (fstr_len <= strlen(*fstr) + strlen(eval) + 1) {
                    fstr_len += (strlen(eval) + 1) * 2;
                    *fstr = slapi_ch_realloc(*fstr, fstr_len);
                }
                strcat(*fstr, "*");
                strcat(*fstr, eval);
                slapi_ch_free_string(&eval);
            }
            break;

        default:
            slapi_log_err(SLAPI_LOG_FILTER, "get_substring_filter", "Unknown tag 0x%lX\n", tag);
            return (LDAP_PROTOCOL_ERROR);
        }
    }

    if ((tag != LBER_END_OF_SEQORSET) && (len != -1)) {
        slapi_log_err(SLAPI_LOG_ERR, "get_substring_filter", "Error reading substring filter\n");
        return (LDAP_PROTOCOL_ERROR);
    }
    if (f->f_sub_initial == NULL && f->f_sub_any == NULL &&
        f->f_sub_final == NULL) {
        return (LDAP_PROTOCOL_ERROR);
    }

    filter_compute_hash(f);
    if (fstr_len <= strlen(*fstr) + 3) {
        fstr_len += 3;
        *fstr = slapi_ch_realloc(*fstr, fstr_len);
    }
    if (f->f_sub_final == NULL) {
        strcat(*fstr, "*");
    }
    strcat(*fstr, ")");

    slapi_log_err(SLAPI_LOG_FILTER, "get_substring_filter", "<=\n");
    return (0);
}

static int
get_extensible_filter(BerElement *ber, mr_filter_t *mrf)
{
    int gotelem, gotoid, gotvalue;
    ber_tag_t tag;
    ber_len_t len = -1;
    char *last;
    int rc = LDAP_SUCCESS;

    slapi_log_err(SLAPI_LOG_FILTER, "get_extensible_filter", "=>\n");
    memset(mrf, 0, sizeof(mr_filter_t));

    gotelem = gotoid = gotvalue = 0;
    for (tag = ber_first_element(ber, &len, &last);
         tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
         tag = ber_next_element(ber, &len, last)) {
        /*
         * order of elements goes like this:
         *
         *    [oid][type]value[dnattr]
         *
         * where either oid or type is required.
         */
        len = -1; /* reset - not used in loop */
        switch (tag) {
        case LDAP_TAG_MRA_OID:
            if (gotelem != 0) {
                goto parsing_error;
            }
            if (ber_scanf(ber, "a", &mrf->mrf_oid) == LBER_ERROR) {
                rc = LDAP_PROTOCOL_ERROR;
            }
            gotoid = 1;
            gotelem++;
            break;
        case LDAP_TAG_MRA_TYPE:
            if (gotelem != 0) {
                if (gotelem != 1 || gotoid != 1) {
                    goto parsing_error;
                }
            }
            {
                char *type = NULL;
                if (ber_scanf(ber, "a", &type) == LBER_ERROR) {
                    slapi_ch_free_string(&type);
                    rc = LDAP_PROTOCOL_ERROR;
                } else {
                    mrf->mrf_type = slapi_attr_syntax_normalize(type);
                    slapi_ch_free_string(&type);
                }
            }
            gotelem++;
            break;
        case LDAP_TAG_MRA_VALUE:
            if (gotelem != 1 && gotelem != 2) {
                goto parsing_error;
            }
            if (ber_scanf(ber, "o", &mrf->mrf_value) == LBER_ERROR) {
                rc = LDAP_PROTOCOL_ERROR;
            }
            gotvalue = 1;
            gotelem++;
            break;
        case LDAP_TAG_MRA_DNATTRS:
            if (gotvalue != 1) {
                goto parsing_error;
            }
            if (ber_scanf(ber, "b", &mrf->mrf_dnAttrs) == LBER_ERROR) {
                rc = LDAP_PROTOCOL_ERROR;
            }
            gotelem++;
            break;
        default:
            goto parsing_error;
        }
        if (rc != LDAP_SUCCESS) {
            goto parsing_error;
        }
    }

    if (tag == LBER_ERROR) {
        if (len == -1) {
            /* means that the ber sequence ended without  LBER_END_OF_SEQORSET tag
             * and it is considered as valid to ensure compatibility with open ldap.
             */
        } else {
            goto parsing_error;
        }
    }

    slapi_log_err(SLAPI_LOG_FILTER, "get_extensible_filter", "<= %i\n", rc);
    return rc;

parsing_error:;
    slapi_log_err(SLAPI_LOG_ERR, "get_extensible_filter", "Error parsing extensible filter\n");
    return (LDAP_PROTOCOL_ERROR);
}


Slapi_Filter *
slapi_filter_dup(Slapi_Filter *f)
{
    Slapi_Filter *out = 0;
    struct slapi_filter *fl = 0;
    struct slapi_filter **outl = 0;
    struct slapi_filter *lastout = 0;

    if (f == NULL) {
        return NULL;
    }

    out = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));
    if (out == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_filter_dup", "Memory allocation error\n");
        return NULL;
    }

    out->f_choice = f->f_choice;
    out->f_hash = f->f_hash;
    out->f_flags = f->f_flags;

    slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_dup", "type 0x%lX\n", f->f_choice);
    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        out->f_ava.ava_type = slapi_ch_strdup(f->f_ava.ava_type);
        slapi_ber_bvcpy(&out->f_ava.ava_value, &f->f_ava.ava_value);
        break;

    case LDAP_FILTER_SUBSTRINGS:

        out->f_sub_type = slapi_ch_strdup(f->f_sub_type);
        out->f_sub_initial = slapi_ch_strdup(f->f_sub_initial);
        out->f_sub_any = charray_dup(f->f_sub_any);
        out->f_sub_final = slapi_ch_strdup(f->f_sub_final);
        break;

    case LDAP_FILTER_PRESENT:
        out->f_type = slapi_ch_strdup(f->f_type);
        break;

    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        outl = &out->f_list;
        for (fl = f->f_list; fl != NULL; fl = fl->f_next) {
            (*outl) = slapi_filter_dup(fl);
            if (*outl){
                (*outl)->f_next = 0;
                if (lastout)
                    lastout->f_next = *outl;
                lastout = *outl;
                outl = &((*outl)->f_next);
            }
        }
        break;

    case LDAP_FILTER_EXTENDED:
        out->f_mr_oid = slapi_ch_strdup(f->f_mr_oid);
        out->f_mr_type = slapi_ch_strdup(f->f_mr_type);
        slapi_ber_bvcpy(&out->f_mr_value, &f->f_mr_value);
        out->f_mr_dnAttrs = f->f_mr_dnAttrs;
        if (f->f_mr.mrf_match) {
            int rc = plugin_mr_filter_create(&out->f_mr);
            slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_dup", "plugin_mr_filter_create returned %d\n", rc);
        }
        break;

    default:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_dup", "Unknown type 0x%lX\n", f->f_choice);
        break;
    }

    return out;
}

void
slapi_filter_free(struct slapi_filter *f, int recurse)
{
    if (f == NULL) {
        return;
    }

    slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_free", "type 0x%lX\n", f->f_choice);
    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        ava_done(&f->f_ava);
        break;

    case LDAP_FILTER_SUBSTRINGS:
        slapi_ch_free((void **)&f->f_sub_type);
        slapi_ch_free((void **)&f->f_sub_initial);
        charray_free(f->f_sub_any);
        slapi_ch_free((void **)&f->f_sub_final);
        break;

    case LDAP_FILTER_PRESENT:
        slapi_ch_free((void **)&f->f_type);
        break;

    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        if (recurse) {
            struct slapi_filter *fl, *next;

            for (fl = f->f_list; fl != NULL; fl = next) {
                next = fl->f_next;
                fl->f_next = NULL;
                slapi_filter_free(fl, recurse);
                fl = next;
            }
        }
        break;

    case LDAP_FILTER_EXTENDED:
        slapi_ch_free((void **)&f->f_mr_oid);
        slapi_ch_free((void **)&f->f_mr_type);
        slapi_ber_bvdone(&f->f_mr_value);
        if (f->f_mr.mrf_destroy != NULL) {
            Slapi_PBlock *pb = slapi_pblock_new();
            if (!slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, f->f_mr.mrf_object)) {
                f->f_mr.mrf_destroy(pb);
            }
            slapi_pblock_destroy(pb);
        }
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_filter_free", "Unknown type 0x%lX\n",
                      f->f_choice);
        break;
    }
    slapi_ch_free((void **)&f);
}

struct slapi_filter *
slapi_filter_join(int ftype, struct slapi_filter *f1, struct slapi_filter *f2)
{
    return slapi_filter_join_ex(ftype, f1, f2, 1);
}


struct slapi_filter *
slapi_filter_join_ex(int ftype, struct slapi_filter *f1, struct slapi_filter *f2, int recurse_always)
{
    struct slapi_filter *fjoin;
    struct slapi_filter *add_to;
    struct slapi_filter *add_this;
    struct slapi_filter *return_this = NULL;
    int insert = 0;

    if ((NULL == f1) || (NULL == f2)) {
        switch (ftype) {
        case LDAP_FILTER_AND:
            return NULL;
        case LDAP_FILTER_OR:
            return f1 ? f1 : f2;
        default:
            if (NULL == f1) {
                if (NULL == f2) {
                    return NULL;
                } else {
                    add_this = f2;
                }
            } else {
                add_this = f1;
            }
            fjoin = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));
            fjoin->f_choice = ftype;
            fjoin->f_list = add_this;
            filter_compute_hash(fjoin);
            return fjoin;
        }
    }

    if (!recurse_always) {
        /* try to optimise the filter join */
        switch (ftype) {
        case LDAP_FILTER_AND:
        case LDAP_FILTER_OR:
            if (ftype == (int)f1->f_choice) {
                add_to = f1;
                add_this = f2;
                insert = 1;
            } else if (ftype == (int)f2->f_choice) {
                add_to = f2;
                add_this = f1;
                insert = 1;
            }

        default:
            break;
        }
    }

    if (insert) {
        /* try to avoid ! filters as the first arg */
        if (add_to->f_list->f_choice == LDAP_FILTER_NOT) {
            add_this->f_next = add_to->f_list;
            add_to->f_list = add_this;
        } else {
            /* find end of list, add the filter */
            for (fjoin = add_to->f_list; fjoin != NULL; fjoin = fjoin->f_next) {
                if (fjoin->f_next == NULL) {
                    fjoin->f_next = add_this;
                    break;
                }
            }
        }
        /*
         * Make sure we sync the filter flags. The origin filters may have flags
         * we still need on the outer layer!
         */
        add_to->f_flags |= add_this->f_flags;
        filter_compute_hash(add_to);
        return_this = add_to;
    } else {
        fjoin = (struct slapi_filter *)slapi_ch_calloc(1, sizeof(struct slapi_filter));
        fjoin->f_choice = ftype;
        fjoin->f_next = NULL;
        /* try to ensure ! filters dont cause allid search */
        if (f1->f_choice == LDAP_FILTER_NOT && f2) {
            fjoin->f_list = f2;
            f2->f_next = f1;
        } else {
            fjoin->f_list = f1;
            f1->f_next = f2;
        }
        /* Make sure any flags that were set move to the outer parent */
        fjoin->f_flags |= f1->f_flags | f2->f_flags;
        filter_compute_hash(fjoin);
        return_this = fjoin;
    }

    return (return_this);
}

int
slapi_filter_get_choice(struct slapi_filter *f)
{
    return (f->f_choice);
}

int
slapi_filter_get_ava(struct slapi_filter *f, char **type, struct berval **bval)
{
    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        break;
    default:
        *type = NULL;
        *bval = NULL;
        return (-1);
    }
    *type = f->f_avtype;
    *bval = &f->f_avvalue;
    return (0);
}

/* Deprecated--use slapi_filter_get_attribute_type() now */

SLAPI_DEPRECATED int
slapi_filter_get_type(struct slapi_filter *f, char **type)
{
    if (f->f_choice != LDAP_FILTER_PRESENT) {
        return (-1);
    }
    *type = f->f_type;

    return (0);
}

/*
 * Return the attribute type for all simple filter choices into type.
 * ie. for all except LDAP_FILTER_AND, LDAP_FILTER_OR and LDAP_FILTER_NOT.
 *
 * The returned type is "as is" and so may not be normalized.
 *  Returns 0 for success, -1 otherwise.
*/

int
slapi_filter_get_attribute_type(Slapi_Filter *f, char **type)
{

    if (f == NULL) {
        return -1;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
    case LDAP_FILTER_EQUALITY:
        *type = f->f_ava.ava_type;
        break;
    case LDAP_FILTER_SUBSTRINGS:
        *type = f->f_sub_type;
        break;
    case LDAP_FILTER_PRESENT:
        *type = f->f_type;
        break;
    case LDAP_FILTER_EXTENDED:
        *type = f->f_mr_type;
        break;
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        return (-1);
    default:
        /* Unknown filter choice */
        return -1;
    }

    /* success */
    return (0);
}

struct slapi_filter *
slapi_filter_list_first(struct slapi_filter *f)
{
    if (f->f_choice != LDAP_FILTER_AND && f->f_choice != LDAP_FILTER_OR && f->f_choice != LDAP_FILTER_NOT) {
        return (NULL);
    }
    return (f->f_list);
}

struct slapi_filter *
slapi_filter_list_next(struct slapi_filter *f __attribute__((unused)), struct slapi_filter *fprev)
{
    return (fprev->f_next);
}

int
slapi_filter_get_subfilt(
    struct slapi_filter *f,
    char **type,
    char **initial,
    char ***any,
    char ** final)
{
    if (f->f_choice != LDAP_FILTER_SUBSTRINGS) {
        return (-1);
    }
    *type = f->f_sub_type;
    *initial = f->f_sub_initial;
    *any = f->f_sub_any;
    *final = f->f_sub_final;

    return (0);
}

static void
filter_normalize_ava(struct slapi_filter *f, PRBool norm_values)
{
    char *tmp;
    struct ava *ava;

    if (f == NULL) {
        return;
    }

    ava = &f->f_ava;
    tmp = ava->ava_type;
    ava->ava_type = slapi_attr_syntax_normalize(tmp);
    slapi_ch_free((void **)&tmp);
    f->f_flags |= SLAPI_FILTER_NORMALIZED_TYPE;
    if (norm_values) {
        char *newval = NULL;
        /* NOTE: assumes ava->ava_value.bv_val is NULL terminated - get_ava/ber_scanf 'o'
           will NULL terminate the string by default */
        slapi_attr_value_normalize_ext(NULL, NULL, ava->ava_type,
                                       ava->ava_value.bv_val, 1, &newval, f->f_choice);
        if (newval && (newval != ava->ava_value.bv_val)) {
            slapi_ch_free_string(&ava->ava_value.bv_val);
            ava->ava_value.bv_val = newval;
            ava->ava_value.bv_len = strlen(newval);
        }
        f->f_flags |= SLAPI_FILTER_NORMALIZED_VALUE;
    }
}

static void
filter_normalize_subfilt(struct slapi_filter *f, PRBool norm_values)
{
    struct subfilt *sf;

    if (f == NULL) {
        return;
    }

    sf = &f->f_sub;
    char *tmp = sf->sf_type;
    sf->sf_type = slapi_attr_syntax_normalize(tmp);
    slapi_ch_free((void **)&tmp);
    f->f_flags |= SLAPI_FILTER_NORMALIZED_TYPE;
    if (norm_values) {
        char *newval = NULL;
        Slapi_Attr attr;
        int ii;

        slapi_attr_init(&attr, sf->sf_type);
        slapi_attr_value_normalize_ext(NULL, &attr, NULL, sf->sf_initial, 1, &newval, f->f_choice);
        if (newval && (newval != sf->sf_initial)) {
            slapi_ch_free_string(&sf->sf_initial);
            sf->sf_initial = newval;
        }
        for (ii = 0; sf->sf_any && sf->sf_any[ii]; ++ii) {
            newval = NULL;
            /* do not trim spaces of sf_any values - see string_filter_sub() */
            slapi_attr_value_normalize_ext(NULL, &attr, NULL, sf->sf_any[ii], 0, &newval, f->f_choice);
            if (newval && (newval != sf->sf_any[ii])) {
                slapi_ch_free_string(&sf->sf_any[ii]);
                sf->sf_any[ii] = newval;
            }
        }
        newval = NULL;
        /* do not trim spaces of sf_final values - see string_filter_sub() */
        slapi_attr_value_normalize_ext(NULL, &attr, NULL, sf->sf_final, 0, &newval, f->f_choice);
        if (newval && (newval != sf->sf_final)) {
            slapi_ch_free_string(&sf->sf_final);
            sf->sf_final = newval;
        }
        attr_done(&attr);
        f->f_flags |= SLAPI_FILTER_NORMALIZED_VALUE;
    }
}

void filter_normalize_ext(struct slapi_filter *f, PRBool norm_values);

static void
filter_normalize_list(struct slapi_filter *flist, PRBool norm_values)
{
    struct slapi_filter *f;

    for (f = flist; f != NULL; f = f->f_next) {
        filter_normalize_ext(f, norm_values);
    }
}

/*
 * Normalize all values and types in a filter.  This isn't necessary
 * when we've read the slapi_filter off the wire, but if we've hand-constructed
 * a filter inside slapd (e.g. when calling the routines in wrapper.c),
 * we've called slapi_str2filter on something which *didn't* come over the wire,
 * so the attribute names and filters in the filter struct aren't
 * normalized.
 */
void
filter_normalize_ext(struct slapi_filter *f, PRBool norm_values)
{
    char *tmp;

    if (f == NULL) {
        return;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
    case LDAP_FILTER_EQUALITY:
        filter_normalize_ava(f, norm_values);
        break;
    case LDAP_FILTER_SUBSTRINGS:
        filter_normalize_subfilt(f, norm_values);
        break;
    case LDAP_FILTER_PRESENT:
        tmp = f->f_type;
        f->f_type = slapi_attr_syntax_normalize(tmp);
        slapi_ch_free((void **)&tmp);
        f->f_flags |= SLAPI_FILTER_NORMALIZED_TYPE;
        break;
    case LDAP_FILTER_EXTENDED:
        tmp = f->f_mr_type;
        f->f_mr_type = slapi_attr_syntax_normalize(tmp);
        slapi_ch_free((void **)&tmp);
        f->f_flags |= SLAPI_FILTER_NORMALIZED_TYPE;
        break;
    case LDAP_FILTER_AND:
        filter_normalize_list(f->f_and, norm_values);
        break;
    case LDAP_FILTER_OR:
        filter_normalize_list(f->f_or, norm_values);
        break;
    case LDAP_FILTER_NOT:
        filter_normalize_list(f->f_not, norm_values);
        break;
    default:
        return;
    }
}

void
filter_normalize(struct slapi_filter *f)
{
    filter_normalize_ext(f, PR_FALSE);
}

void
slapi_filter_normalize(struct slapi_filter *f, PRBool norm_values)
{
    filter_normalize_ext(f, norm_values);
}

void
filter_print(struct slapi_filter *f)
{
    int i;
    struct slapi_filter *p;

    if (f == NULL) {
        printf("NULL");
        return;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
        printf("(%s=%s)", f->f_ava.ava_type,
               f->f_ava.ava_value.bv_val);
        break;

    case LDAP_FILTER_GE:
        printf("(%s>=%s)", f->f_ava.ava_type,
               f->f_ava.ava_value.bv_val);
        break;

    case LDAP_FILTER_LE:
        printf("(%s<=%s)", f->f_ava.ava_type,
               f->f_ava.ava_value.bv_val);
        break;

    case LDAP_FILTER_APPROX:
        printf("(%s~=%s)", f->f_ava.ava_type,
               f->f_ava.ava_value.bv_val);
        break;

    case LDAP_FILTER_SUBSTRINGS:
        printf("(%s=", f->f_sub_type);
        if (f->f_sub_initial != NULL) {
            printf("%s", f->f_sub_initial);
        }
        if (f->f_sub_any != NULL) {
            for (i = 0; f->f_sub_any[i] != NULL; i++) {
                printf("*%s", f->f_sub_any[i]);
            }
        }
        if (f->f_sub_final != NULL) {
            printf("*%s", f->f_sub_final);
        }
        printf(")");
        break;

    case LDAP_FILTER_PRESENT:
        printf("(%s=*)", f->f_type);
        break;

    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        printf("(%c", f->f_choice == LDAP_FILTER_AND ? '&' : f->f_choice == LDAP_FILTER_OR ? '|' : '!');
        for (p = f->f_list; p != NULL; p = p->f_next) {
            filter_print(p);
        }
        printf(")");
        break;

    default:
        printf("unknown type 0x%lX", f->f_choice);
        break;
    }
    fflush(stdout);
}

/* filter_to_string
 * ----------------
 * translates the supplied filter to
 * the string representation and places
 * the result in buf
 *
 * NOTE: intended for debug purposes, buffer must be
 * large enough to contain filter string
 */

char *
slapi_filter_to_string_internal(const struct slapi_filter *f, char *buf, size_t *bufsize)
{
    int i;
    char *return_buf = buf;
    struct slapi_filter *p;
    size_t size;
    char *operator= ""; /* for comparison operators */

    if (buf == NULL)
        return 0;
    else
        *buf = 0; /* make sure buf is null terminated */

    if (f == NULL) {
        sprintf(buf, "NULL");
        return 0;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
        operator= "=";
        break;

    case LDAP_FILTER_GE:
        operator= ">=";
        break;

    case LDAP_FILTER_LE:
        operator= "<=";
        break;

    case LDAP_FILTER_APPROX:
        operator= "~=";
        break;

    case LDAP_FILTER_EXTENDED:
        operator= ":=";
        break;

    default:
        break;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        /* +3 -> 1 for (, 1 for ), and one for the trailing null */
        size = strlen(f->f_ava.ava_type) + f->f_ava.ava_value.bv_len + strlen(operator) + 3;

        if (size < *bufsize) {
            /* bv_val may not be null terminated, so use the max field width
               specifier .* with the bv_len as the length to avoid reading
               past bv_len in bv_val */
            sprintf(buf, "(%s%s%.*s)", f->f_ava.ava_type, operator,(int) f->f_ava.ava_value.bv_len,
                    f->f_ava.ava_value.bv_val);
            *bufsize -= size;
        }
        break;

    case LDAP_FILTER_SUBSTRINGS:
        size = strlen(f->f_sub_type) + 2;

        if (size < *bufsize) {
            sprintf(buf, "(%s=", f->f_sub_type);
            *bufsize -= size;

            if (f->f_sub_initial != NULL) {
                size = strlen(f->f_sub_initial);

                if (size < *bufsize) {
                    buf += strlen(buf);
                    sprintf(buf, "%s", f->f_sub_initial);
                    *bufsize -= size;
                }
            }
            if (f->f_sub_any != NULL) {
                for (i = 0; f->f_sub_any[i] != NULL; i++) {
                    size = strlen(f->f_sub_any[i]) + 1;

                    if (size < *bufsize) {
                        buf += strlen(buf);
                        sprintf(buf, "*%s", f->f_sub_any[i]);
                        *bufsize -= size;
                    }
                }
            }
            if (f->f_sub_final != NULL) {
                size = strlen(f->f_sub_final) + 1;

                if (size < *bufsize) {
                    buf += strlen(buf);
                    sprintf(buf, "*%s", f->f_sub_final);
                    *bufsize -= size;
                }
            }
            buf += strlen(buf);

            if (1 < *bufsize) {
                sprintf(buf, ")");
                (*bufsize)--;
            }
        }
        break;

    case LDAP_FILTER_PRESENT:
        size = strlen(f->f_type) + 4;

        if (size < *bufsize) {
            sprintf(buf, "(%s=*)", f->f_type);
            *bufsize -= size;
        }
        break;

    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        if (2 < *bufsize) {
            sprintf(buf, "(%c", f->f_choice == LDAP_FILTER_AND ? '&' : f->f_choice == LDAP_FILTER_OR ? '|' : '!');
            *bufsize -= 2;

            for (p = f->f_list; p != NULL; p = p->f_next) {
                buf += strlen(buf);
                slapi_filter_to_string_internal(p, buf, bufsize);
            }
            buf += strlen(buf);

            if (1 < *bufsize) {
                assert(buf);  /* For gcc analyzer */
                sprintf(buf, ")");
                (*bufsize)--;
            }
        }
        break;

    case LDAP_FILTER_EXTENDED:
        size = strlen(f->f_mr_type) + f->f_mr_value.bv_len + strlen(operator) +
               (f->f_mr_dnAttrs ? sizeof(":dn") - 1 : 0) +
               (f->f_mr_oid ? strlen(f->f_mr_oid) + 1 /* : */ : 0) + 3;
        if (size < *bufsize) {
            sprintf(buf, "(%s%s%s%s%s%.*s)", f->f_mr_type, f->f_mr_dnAttrs ? ":dn" : "",
                    f->f_mr_oid ? ":" : "", f->f_mr_oid ? f->f_mr_oid : "",
                    operator,(int) f->f_mr_value.bv_len, f->f_mr_value.bv_val);
            *bufsize -= size;
        }
        break;

    default:
        size = 25;

        if (size < *bufsize) {
            sprintf(buf, "unsupported type 0x%lX", f->f_choice);
            *bufsize -= 25;
        }
        break;
    }

    return return_buf;
}

char *
slapi_filter_to_string(const struct slapi_filter *f, char *buf, size_t bufsize)
{
    size_t size = bufsize;

    return slapi_filter_to_string_internal(f, buf, &size);
}

/* rbyrne */

static int
filter_apply_list(struct slapi_filter *flist, FILTER_APPLY_FN fn, caddr_t arg, int *error_code)
{
    struct slapi_filter *f;
    int rc;

    for (f = flist; f != NULL; f = f->f_next) {
        rc = slapi_filter_apply(f, fn, arg, error_code);
        if (rc == SLAPI_FILTER_SCAN_STOP || rc == SLAPI_FILTER_SCAN_ERROR) {
            return (rc);
        }
    }

    /* If we get here we've applied the whole list sucessfully so return 0 */

    return (SLAPI_FILTER_SCAN_NOMORE);
}

/*
  *
  * The idea here is to apply, fn() to each "simple filter" in f as follows:
  * fn( Slapi_Filter *simple_filter, caddr_t arg).
  *
  * A 'simple filter' is anything other than AND, OR or NOT.
  *
  * If fn() wants the seasrch to abort it returns FILTER_SCAN_STOP.
  * In this case, FILTER_SCAN_STOP is returned by slapi_filter_apply().
  * Otherwise fn() should return FILTER_SCAN_CONTINUE.
  *
  * If the whole filter is traversed, FILTER_SCAN_NO_MORE is returned.
  * If an error occurred during the traverse, the scan is aborted and
  * FILTER_SCAN_ERROR is returned, and in this case error_code can be checked
  * for more details--right now the only error is
  * SLAPI_FILTER_UNKNOWN_FILTER_TYPE.
  *
  *
 */
int
slapi_filter_apply(struct slapi_filter *f, FILTER_APPLY_FN fn, void *arg, int *error_code)
{
    int rc = SLAPI_FILTER_SCAN_ERROR;

    if (f == NULL) {
        return SLAPI_FILTER_SCAN_NOMORE;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
    case LDAP_FILTER_EQUALITY:
        rc = (*fn)(f, arg);
        break;
    case LDAP_FILTER_SUBSTRINGS:
        rc = (*fn)(f, arg);
        /* value will be normalized later */
        break;
    case LDAP_FILTER_PRESENT:
        rc = (*fn)(f, arg);
        break;
    case LDAP_FILTER_EXTENDED:
        rc = (*fn)(f, arg);
        break;
    case LDAP_FILTER_AND:
        rc = filter_apply_list(f->f_and, fn, arg, error_code);
        break;
    case LDAP_FILTER_OR:
        rc = filter_apply_list(f->f_or, fn, arg, error_code);
        break;
    case LDAP_FILTER_NOT:
        rc = filter_apply_list(f->f_not, fn, arg, error_code);
        break;
    default:
        /* Unknown filter choice */
        *error_code = SLAPI_FILTER_UNKNOWN_FILTER_TYPE;
        rc = SLAPI_FILTER_SCAN_ERROR;
    }

    /*
     * We propagate back FILTER_SCAN_ERROR and
     * FILTER_SCAN_STOP, anything else is success.
    */

    if (rc != SLAPI_FILTER_SCAN_ERROR && rc != SLAPI_FILTER_SCAN_STOP) {
        rc = SLAPI_FILTER_SCAN_NOMORE;
    }

    return (rc);
}


int
filter_flag_is_set(const Slapi_Filter *f, unsigned char flag)
{
    return (f->f_flags & flag);
}


static int
tombstone_check_filter(Slapi_Filter *f)
{
    if (0 == strcasecmp(f->f_avvalue.bv_val, SLAPI_ATTR_VALUE_TOMBSTONE)) {
        return 1; /* Contains a nsTombstone filter */
    }
    return 0; /* Not nsTombstone filter */
}


static int
ruv_check_filter(Slapi_Filter *f)
{
    if (0 == strcasecmp(f->f_avvalue.bv_val, "ffffffff-ffffffff-ffffffff-ffffffff")) {
        return 1; /* Contains a RUV filter */
    }
    return 0; /* Not a RUV filter */
}

/*
 * To help filter optimise we break out the list manipulation
 * code.
 */

static void
filter_prioritise_element(Slapi_Filter **list, Slapi_Filter **head, Slapi_Filter **tail, Slapi_Filter **f_prev, Slapi_Filter **f_cur) {
    if (*f_prev != NULL) {
        (*f_prev)->f_next = (*f_cur)->f_next;
    } else if (*list == *f_cur) {
        *list = (*f_cur)->f_next;
    }

    if (*head == NULL) {
        *head = *f_cur;
        *tail = *f_cur;
        (*f_cur)->f_next = NULL;
    } else {
        (*f_cur)->f_next = *head;
        *head = *f_cur;
    }
}

static void
filter_merge_subfilter(Slapi_Filter **list, Slapi_Filter **f_prev, Slapi_Filter **f_cur, Slapi_Filter **f_next)   {

    /* First, graft in the new item between f_cur and f_cur -> f_next */
    Slapi_Filter *remainder = (*f_cur)->f_next;
    (*f_cur)->f_next = (*f_cur)->f_list;
    /* Go to the end of the newly grafted list, and put in our remainder. */
    Slapi_Filter *f_cur_tail = *f_cur;
    while (f_cur_tail->f_next != NULL) {
        f_cur_tail = f_cur_tail->f_next;
    }
    f_cur_tail->f_next = remainder;

    /* Now indicate to the caller what the next element is. */
    *f_next = (*f_cur)->f_next;

    /* Now that we have grafted our list in, cut out f_cur */
    if (*f_prev != NULL) {
        (*f_prev)->f_next = *f_next;
    } else if (*list == *f_cur) {
        *list = *f_next;
    }

    /* Finally free the f_cur (and/or) */
    slapi_filter_free(*f_cur, 0);
}

/* slapi_filter_optimise
 * ---------------
 * takes a filter and optimises it for fast evaluation
 *
 * Optimisations are:
 * * In OR conditions move substrings early to promote fail-fast of unindexed types
 * * In AND conditions move eq types (that are not objectClass) early to promote triggering threshold shortcut
 * * In OR conditions, merge all direct child OR conditions into the list. (|(|(a)(b))) == (|(a)(b))
 * * in AND conditions, merge all direct child AND conditions into the list. (&(&(a)(b))) == (&(a)(b))
 *
 * In the case of the OR and AND merges, we remove the inner filter because the outer one may have flags set.
 *
 * In the future this could be backend dependent.
 */
void
slapi_filter_optimise(Slapi_Filter *f)
{
    /*
     * Today tombstone searches RELY on filter ordering
     * and a filter test threshold quirk. We need to avoid
     * touching these cases!!!
     */
    if (f == NULL || (f->f_flags & SLAPI_FILTER_TOMBSTONE) != 0) {
        return;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_AND:
        /* Move all equality searches to the head. */
        /* Merge any direct descendant AND queries into us */
        {
            Slapi_Filter *f_prev = NULL;
            Slapi_Filter *f_cur = NULL;
            Slapi_Filter *f_next = NULL;

            Slapi_Filter *f_op_head = NULL;
            Slapi_Filter *f_op_tail = NULL;

            f_cur = f->f_list;
            while(f_cur != NULL) {

                switch(f_cur->f_choice) {
                case LDAP_FILTER_AND:
                    filter_merge_subfilter(&(f->f_list), &f_prev, &f_cur, &f_next);
                    f_cur = f_next;
                    break;
                case LDAP_FILTER_EQUALITY:
                    if (strcasecmp(f_cur->f_avtype, "objectclass") != 0) {
                        f_next = f_cur->f_next;
                        /* Cut it out */
                        filter_prioritise_element(&(f->f_list), &f_op_head, &f_op_tail, &f_prev, &f_cur);
                        /* Don't change previous, because we remove this f_cur */
                        f_cur = f_next;
                        break;
                    } else {
                        /* Move along */
                        f_prev = f_cur;
                        f_cur = f_cur->f_next;
                    }
                    break;
                default:
                    /* Move along */
                    f_prev = f_cur;
                    f_cur = f_cur->f_next;
                    break;
                }
            }

            if (f_op_head != NULL) {
                f_op_tail->f_next = f->f_list;
                f->f_list = f_op_head;
            }
        }
        /* finally optimize children */
        slapi_filter_optimise(f->f_list);

        break;

    case LDAP_FILTER_OR:
        /* Move all substring searches to the head. */
        {
            Slapi_Filter *f_prev = NULL;
            Slapi_Filter *f_cur = NULL;
            Slapi_Filter *f_next = NULL;

            Slapi_Filter *f_op_head = NULL;
            Slapi_Filter *f_op_tail = NULL;

            f_cur = f->f_list;
            while(f_cur != NULL) {

                switch(f_cur->f_choice) {
                case LDAP_FILTER_OR:
                    filter_merge_subfilter(&(f->f_list), &f_prev, &f_cur, &f_next);
                    f_cur = f_next;
                    break;
                case LDAP_FILTER_APPROX:
                case LDAP_FILTER_GE:
                case LDAP_FILTER_LE:
                case LDAP_FILTER_SUBSTRINGS:
                    f_next = f_cur->f_next;
                    /* Cut it out */
                    filter_prioritise_element(&(f->f_list), &f_op_head, &f_op_tail, &f_prev, &f_cur);
                    /* Don't change previous, because we remove this f_cur */
                    f_cur = f_next;
                    break;
                default:
                    /* Move along */
                    f_prev = f_cur;
                    f_cur = f_cur->f_next;
                    break;
                }
            }
            if (f_op_head != NULL) {
                f_op_tail->f_next = f->f_list;
                f->f_list = f_op_head;
            }
        }
        /* finally optimize children */
        slapi_filter_optimise(f->f_list);

        break;

    default:
        slapi_filter_optimise(f->f_next);
        break;
    }
}


/* slapi_filter_changetype
 * ------------------------
 * changes the type used in equality/>/</approx filters
 * handy for features that do type mapping
 */
int
slapi_filter_changetype(Slapi_Filter *f, const char *newtype)
{
    char **target = 0;

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        target = &f->f_ava.ava_type;
        break;

    case LDAP_FILTER_SUBSTRINGS:
        target = &f->f_sub_type;
        break;

    case LDAP_FILTER_PRESENT:
        target = &f->f_type;
        break;

    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
    default:
        goto bail;
        break;
    }

    slapi_ch_free_string(target);
    *target = slapi_ch_strdup(newtype);

bail:
    return (!target);
}
