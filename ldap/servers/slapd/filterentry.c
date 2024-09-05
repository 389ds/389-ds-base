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

/* filterentry.c - apply a filter to an entry */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

static int test_filter_list(Slapi_PBlock *pb, Slapi_Entry *e, struct slapi_filter *flist, int ftype, int verify_access, int only_check_access, int *access_check_done);
static int test_extensible_filter(Slapi_PBlock *callers_pb, Slapi_Entry *e, mr_filter_t *mrf, int verify_access, int only_check_access, int *access_check_done);

static int vattr_test_filter_list_and(Slapi_PBlock *pb, Slapi_Entry *e, struct slapi_filter *flist, int ftype, int verify_access, int only_check_access, int *access_check_done);
static int vattr_test_filter_list_or(Slapi_PBlock *pb, Slapi_Entry *e, struct slapi_filter *flist, int ftype, int verify_access, int only_check_access, int *access_check_done);

static int test_filter_access(Slapi_PBlock *pb, Slapi_Entry *e, char *attr_type, struct berval *attr_val);
static int slapi_vattr_filter_test_ext_internal(Slapi_PBlock *pb, Slapi_Entry *e, struct slapi_filter *f, int verify_access, int only_check_access, int *access_check_done);

static char *opt_str = 0;
static int opt = 0;

static int
optimise_filter_acl_tests(void)
{
    if (!opt_str) {
        opt_str = getenv("NS_DS_OPT_FILT_ACL_EVAL");
        if (opt_str)
            opt = !strcasecmp(opt_str, "false");
        else
            opt = 0;
        if (!opt_str)
            opt_str = "dummy";
    }

    return opt;
}

/*
 * slapi_filter_test - test a filter against a single entry.
 * returns    0    filter matched
 *            -1    filter did not match
 *            >0    an ldap error code
 */

int
slapi_filter_test(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access)
{
    return slapi_filter_test_ext(pb, e, f, verify_access, 0);
}

/*
 * slapi_filter_test_simple - test without checking access control
 *
 * returns    0    filter matched
 *            -1    filter did not match
 *            >0    an ldap error code
 */
int
slapi_filter_test_simple(
    Slapi_Entry *e,
    struct slapi_filter *f)
{
    return slapi_vattr_filter_test_ext(NULL, e, f, 0, 0);
}

/*
 * slapi_filter_test_ext - full-feature filter test function
 *
 * returns    0    filter matched
 *            -1    filter did not match
 *            >0    an ldap error code
 */

int
slapi_filter_test_ext_internal(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int rc;

    slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "=>\n");

    /*
     * RJP: Not sure if this is semantically right, but we have to
     * return something if f is NULL. If there is no filter,
     * then we say that it did match and return 0.
     */
    if (f == NULL) {
        return (0);
    }

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "EQUALITY\n");
        rc = test_ava_filter(pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_EQUALITY,
                             verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_SUBSTRINGS:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "SUBSTRINGS\n");
        rc = test_substring_filter(pb, e, f, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_GE:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "GE\n");
        rc = test_ava_filter(pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_GE,
                             verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_LE:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "LE\n");
        rc = test_ava_filter(pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_LE,
                             verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_PRESENT:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "PRESENT\n");
        rc = test_presence_filter(pb, e, f->f_type, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_APPROX:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "APPROX\n");
        rc = test_ava_filter(pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_APPROX,
                             verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_EXTENDED:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "EXTENDED\n");
        rc = test_extensible_filter(pb, e, &f->f_mr, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_AND:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "AND\n");
        rc = test_filter_list(pb, e, f->f_and,
                              LDAP_FILTER_AND, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_OR:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "OR\n");
        rc = test_filter_list(pb, e, f->f_or,
                              LDAP_FILTER_OR, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_NOT:
        slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "NOT\n");
        rc = slapi_filter_test_ext_internal(pb, e, f->f_not, verify_access, only_check_access, access_check_done);
        if (!(verify_access && only_check_access)) /* dont play with access control return codes */
        {
            if (verify_access && !rc && !(*access_check_done)) {
                /* the filter failed so access control was not checked
                 * for NOT filters this is significant so we must ensure
                 * access control is checked
                 */
                /* check access control only */
                rc = slapi_filter_test_ext_internal(pb, e, f->f_not, verify_access, -1 /*only_check_access*/, access_check_done);
                /* preserve error code if any */
                if (!rc)
                    rc = !rc;
            } else
                rc = !rc;
        }

        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_filter_test_ext_internal", "Unknown filter type 0x%lX\n",
                      f->f_choice);
        rc = -1;
    }

    slapi_log_err(SLAPI_LOG_FILTER, "slapi_filter_test_ext_internal", "<= %d\n", rc);
    return (rc);
}


int
slapi_filter_test_ext(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access,
    int only_check_access)
{
    int rc = 0; /* a no op request succeeds */
    int access_check_done = 0;

    switch (f->f_choice) {
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        /*
        * optimize acl checking by only doing it once it is
        * known that the whole filter passes and so the entry
        * is eligible to be returned.
        * then we check the filter only for access
        *
        * complex filters really benefit from
        * separate stages, filter eval, followed by acl check...
        */
        if (!only_check_access) {
            rc = slapi_filter_test_ext_internal(pb, e, f, 0, 0, &access_check_done);
        }

        if (rc == 0 && verify_access) {
            rc = slapi_filter_test_ext_internal(pb, e, f, -1, -1, &access_check_done);
        }

        break;

    default:
        /*
        * ...but simple filters are better off doing eval and
        * acl check at once
        */
        rc = slapi_filter_test_ext_internal(pb, e, f, verify_access, only_check_access, &access_check_done);
        break;
    }

    return rc;
}


static const char *
filter_type_as_string(int filter_type)
{
    switch (filter_type) {
    case LDAP_FILTER_AND:
        return "&";
    case LDAP_FILTER_OR:
        return "|";
    case LDAP_FILTER_NOT:
        return "!";
    case LDAP_FILTER_EQUALITY:
        return "=";
    case LDAP_FILTER_SUBSTRINGS:
        return "*";
    case LDAP_FILTER_GE:
        return ">=";
    case LDAP_FILTER_LE:
        return "<=";
    case LDAP_FILTER_PRESENT:
        return "=*";
    case LDAP_FILTER_APPROX:
        return "~";
    case LDAP_FILTER_EXT:
        return "EXT";
    default:
        return "?";
    }
}


int
test_ava_filter(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    Slapi_Attr *a,
    struct ava *ava,
    int ftype,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int rc;

    if (slapi_is_loglevel_set(SLAPI_LOG_FILTER)) {
        char *val = slapi_berval_get_string_copy(&ava->ava_value);
        char buf[BUFSIZ];
        slapi_log_err(SLAPI_LOG_FILTER, "test_ava_filter", "=> AVA: %s%s%s\n",
                      ava->ava_type, filter_type_as_string(ftype), escape_string(val, buf));
        slapi_ch_free_string(&val);
    }

    *access_check_done = 0;

    if (optimise_filter_acl_tests()) {
        rc = 0;

        if (!only_check_access) {
            rc = -1;
            for (; a != NULL; a = a->a_next) {
                if (slapi_attr_type_cmp(ava->ava_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                    rc = plugin_call_syntax_filter_ava(a, ftype, ava);
                    if (rc == 0) {
                        break;
                    }
                }
            }
        }

        if (rc == 0 && verify_access && pb != NULL) {
            char *attrs[2] = {NULL, NULL};
            attrs[0] = ava->ava_type;
            rc = plugin_call_acl_plugin(pb, e, attrs, &ava->ava_value,
                                        SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
        }

    } else {
        if (verify_access && pb != NULL) {
            char *attrs[2] = {NULL, NULL};
            attrs[0] = ava->ava_type;
            rc = plugin_call_acl_plugin(pb, e, attrs, &ava->ava_value,
                                        SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
            if (only_check_access || rc != LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_FILTER, "test_ava_filter", "<= %d\n", rc);
                return (rc);
            }
        }

        rc = -1;
        for (; a != NULL; a = a->a_next) {
            if (slapi_attr_type_cmp(ava->ava_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                if ((ftype == LDAP_FILTER_EQUALITY) &&
                    (slapi_attr_is_dn_syntax_type(a->a_type))) {
                    /* This path is for a performance improvement */

                    /* In case of equality filter we can get benefit of the
                     * sorted valuearray (from valueset).
                     * This improvement is limited to DN syntax attributes for
                     * which the sorted valueset was designed.
                     */
                    Slapi_Value *sval = NULL;
                    sval = slapi_value_new_berval(&ava->ava_value);
                    if (slapi_valueset_find((const Slapi_Attr *)a, &a->a_present_values, sval)) {
                        rc = 0;
                    }
                    slapi_value_free(&sval);
                } else {
                    /* When sorted valuearray optimization cannot be used
                     * lets filter the value according to its syntax
                     */
                    rc = plugin_call_syntax_filter_ava(a, ftype, ava);
                }
                if (rc == 0) {
                    break;
                }
            }
        }
    }

    slapi_log_err(SLAPI_LOG_FILTER, "test_ava_filter", "<= %d\n", rc);
    return (rc);
}

int
test_presence_filter(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    char *type,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int rc;
    void *hint = NULL;

    *access_check_done = 0;

    if (optimise_filter_acl_tests()) {
        rc = 0;
        /* Use attrlist_find_ex to get subtype matching */
        if (!only_check_access) {
            rc = attrlist_find_ex(e->e_attrs, type,
                                  NULL, NULL, &hint) != NULL
                     ? 0
                     : -1;
        }

        if (rc == 0 && verify_access && pb != NULL) {
            char *attrs[2] = {NULL, NULL};
            attrs[0] = type;
            rc = plugin_call_acl_plugin(pb, e, attrs, NULL, SLAPI_ACL_SEARCH,
                                        ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
        }
    } else {
        if (verify_access && pb != NULL) {
            char *attrs[2] = {NULL, NULL};
            attrs[0] = type;
            rc = plugin_call_acl_plugin(pb, e, attrs, NULL, SLAPI_ACL_SEARCH,
                                        ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
            if (only_check_access || rc != LDAP_SUCCESS) {
                return (rc);
            }
        }

        /* Use attrlist_find_ex to get subtype matching */
        rc = attrlist_find_ex(e->e_attrs, type,
                              NULL, NULL, &hint) != NULL
                 ? 0
                 : -1;
    }

    return rc;
}

/*
 * Convert a DN into a list of attribute values.
 * The caller must free the returned attributes.
 */
static Slapi_Attr *
dn2attrs(const char *dn)
{
    int rc = 0;
    Slapi_Attr *dnAttrs = NULL;
    char **rdns = slapi_ldap_explode_dn(dn, 0);
    if (rdns) {
        char **rdn = rdns;
        for (; !rc && *rdn; ++rdn) {
            char **avas = slapi_ldap_explode_rdn(*rdn, 0);
            if (avas) {
                char **ava = avas;
                for (; !rc && *ava; ++ava) {
                    char *val = strchr(*ava, '=');
                    if (val) {
                        struct berval bv;
                        struct berval *bvec[] = {NULL, NULL};
                        size_t type_len = val - *ava;
                        char *type = slapi_ch_malloc(type_len + 1);
                        memcpy(type, *ava, type_len);
                        type[type_len] = '\0';
                        ++val; /* skip the '=' */
                        bv.bv_val = val;
                        bv.bv_len = strlen(val);
                        bvec[0] = &bv;
                        attrlist_merge(&dnAttrs, type, bvec);
                        slapi_ch_free_string(&type);
                    }
                }
                slapi_ldap_value_free(avas);
            }
        }
        slapi_ldap_value_free(rdns);
    }
    return dnAttrs;
}

static int
test_extensible_filter(
    Slapi_PBlock *callers_pb,
    Slapi_Entry *e,
    mr_filter_t *mrf,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    /*
     * The ABNF for extensible filters is
     *
     * attr [":dn"] [":" matchingrule] ":=" value
     * [":dn"] ":" matchingrule ":=" value
     *
     * So, sigh, there are six possible combinations:
     *
     * A) attr ":=" value
     * B) attr ":dn" ":=" value
     * C) attr ":" matchingrule ":=" value
     * D) attr ":dn" ":" matchingrule ":=" value
     * E) ":" matchingrule ":=" value
     * F) ":dn" ":" matchingrule ":=" value
     */
    int rc;

    slapi_log_err(SLAPI_LOG_FILTER, "test_extensible_filter", "=>\n");

    *access_check_done = 0;

    if (optimise_filter_acl_tests()) {
        rc = LDAP_SUCCESS;

        if (!only_check_access) {
            if (mrf->mrf_match == NULL) {
                /*
                 * Could be A or B
                 * No matching function. So use a regular equality filter.
                 * Check the regular attributes for the attribute value.
                 */
                struct ava a;
                a.ava_type = mrf->mrf_type;
                a.ava_value.bv_len = mrf->mrf_value.bv_len;
                a.ava_value.bv_val = mrf->mrf_value.bv_val;
                a.ava_private = NULL;
                rc = test_ava_filter(callers_pb, e, e->e_attrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */, 0 /* don't just verify access */, access_check_done);
                if (rc != LDAP_SUCCESS && mrf->mrf_dnAttrs) {
                    /* B) Also check the DN attributes for the attribute value */
                    Slapi_Attr *dnattrs = dn2attrs(slapi_entry_get_dn_const(e));
                    rc = test_ava_filter(callers_pb, e, dnattrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */, 0 /* don't just verify access */, access_check_done);
                    attrlist_free(dnattrs);
                }
            } else {
                /*
                 * Could be C, D, E, or F
                 * We have a matching rule.
                 */
                rc = mrf->mrf_match(mrf->mrf_object, e, e->e_attrs);
                if (rc != LDAP_SUCCESS && mrf->mrf_dnAttrs) {
                    /* D & F) Also check the DN attributes for the attribute value */
                    Slapi_Attr *dnattrs = dn2attrs(slapi_entry_get_dn_const(e));
                    mrf->mrf_match(mrf->mrf_object, e, dnattrs);
                    attrlist_free(dnattrs);
                }
            }
        }

        if (rc == 0 && mrf->mrf_type != NULL && verify_access) {
            char *attrs[2] = {NULL, NULL};
            /* Could be A, B, C, or D */
            /* Check we have access to this attribute on this entry */
            attrs[0] = mrf->mrf_type;
            rc = plugin_call_acl_plugin(callers_pb, e, attrs, &(mrf->mrf_value),
                                        SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
        }
    } else {
        rc = LDAP_SUCCESS;

        if (mrf->mrf_type != NULL && verify_access) {
            char *attrs[2] = {NULL, NULL};
            /* Could be A, B, C, or D */
            /* Check we have access to this attribute on this entry */
            attrs[0] = mrf->mrf_type;
            rc = plugin_call_acl_plugin(callers_pb, e, attrs, &(mrf->mrf_value),
                                        SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;

            if (only_check_access) {
                return rc;
            }
        }
        if (rc == LDAP_SUCCESS) {
            if (mrf->mrf_match == NULL) {
                /*
                 * Could be A or B
                 * No matching function. So use a regular equality filter.
                 * Check the regular attributes for the attribute value.
                 */
                struct ava a;
                a.ava_type = mrf->mrf_type;
                a.ava_value.bv_len = mrf->mrf_value.bv_len;
                a.ava_value.bv_val = mrf->mrf_value.bv_val;
                a.ava_private = NULL;
                rc = test_ava_filter(callers_pb, e, e->e_attrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */, 0 /* don't just verify access */, access_check_done);
                if (rc != LDAP_SUCCESS && mrf->mrf_dnAttrs) {
                    /* B) Also check the DN attributes for the attribute value */
                    Slapi_Attr *dnattrs = dn2attrs(slapi_entry_get_dn_const(e));
                    rc = test_ava_filter(callers_pb, e, dnattrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */, 0 /* don't just verify access */, access_check_done);
                    attrlist_free(dnattrs);
                }
            } else {
                /*
                 * Could be C, D, E, or F
                 * We have a matching rule.
                 */
                rc = mrf->mrf_match(mrf->mrf_object, e, e->e_attrs);
                if (rc != LDAP_SUCCESS && mrf->mrf_dnAttrs) {
                    /* D & F) Also check the DN attributes for the attribute value */
                    Slapi_Attr *dnattrs = dn2attrs(slapi_entry_get_dn_const(e));
                    mrf->mrf_match(mrf->mrf_object, e, dnattrs);
                    attrlist_free(dnattrs);
                }
            }
        }
    }

    slapi_log_err(SLAPI_LOG_FILTER, "test_extensible_filter", "<= %d\n", rc);
    return (rc);
}


static int
test_filter_list(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *flist,
    int ftype,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int nomatch;
    struct slapi_filter *f;
    int access_check_tmp = -1;

    slapi_log_err(SLAPI_LOG_FILTER, "test_filter_list", "=>\n");

    *access_check_done = -1;

    nomatch = 1;
    for (f = flist; f != NULL; f = f->f_next) {
        if (slapi_filter_test_ext_internal(pb, e, f, verify_access, only_check_access, &access_check_tmp) != 0) {
            /* optimize AND evaluation */
            if (ftype == LDAP_FILTER_AND) {
                /* one false is failure */
                nomatch = 1;
                break;
            }
        } else {
            nomatch = 0;

            /* optimize OR evaluation too */
            if (ftype == LDAP_FILTER_OR) {
                /* only one needs to be true */
                break;
            }
        }

        if (!access_check_tmp)
            *access_check_done = 0;
    }

    slapi_log_err(SLAPI_LOG_FILTER, "test_filter_list", "<= %d\n", nomatch);
    return (nomatch);
}

char *
filter_strcpy_special_ext(char *d, char *s, int flags)
{
    for (; *s; s++) {
        switch (*s) {
        case '.':
        case '\\':
        case '[':
        case ']':
        case '*':
        case '+':
        case '^':
        case '$':
            *d++ = '\\';
            break;
        case '(':
        case ')':
        case '}':
        case '{':
        case '|':
        case '?':
            if (flags & FILTER_STRCPY_ESCAPE_RECHARS) {
                *d++ = '\\';
            }
            break;
        default:
            break;
        }
        *d++ = *s;
    }
    *d = '\0';
    return d;
}

char *
filter_strcpy_special(char *d, char *s)
{
    return filter_strcpy_special_ext(d, s, 0);
}

int
test_substring_filter(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    Slapi_Attr *a;
    int rc;

    slapi_log_err(SLAPI_LOG_FILTER, "test_substring_filter", "<=\n");

    *access_check_done = 0;

    if (optimise_filter_acl_tests()) {
        rc = 0;

        if (!only_check_access) {
            rc = -1;
            for (a = e->e_attrs; a != NULL; a = a->a_next) {
                if (slapi_attr_type_cmp(f->f_sub_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                    /* covscan false positive: "plugin_call_syntax_filter_sub" frees "pb->pb_op". */
                    /* coverity[deref_arg] */
                    /* coverity[double_free] */
                    rc = plugin_call_syntax_filter_sub(pb, a, &f->f_sub);
                    if (rc == 0) {
                        break;
                    }
                }
            }
        }

        if (rc == 0 && verify_access && pb != NULL) {
            char *attrs[2] = {NULL, NULL};
            attrs[0] = f->f_sub_type;
            rc = plugin_call_acl_plugin(pb, e, attrs, NULL,
                                        SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
        }
    } else {
        if (verify_access && pb != NULL) {
            char *attrs[2] = {NULL, NULL};
            attrs[0] = f->f_sub_type;
            rc = plugin_call_acl_plugin(pb, e, attrs, NULL,
                                        SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);
            *access_check_done = -1;
            if (only_check_access || rc != LDAP_SUCCESS) {
                return (rc);
            }
        }


        rc = -1;
        for (a = e->e_attrs; a != NULL; a = a->a_next) {
            if (slapi_attr_type_cmp(f->f_sub_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                /* covscan false positive: "plugin_call_syntax_filter_sub" frees "pb->pb_op". */
                /* coverity[deref_arg] */
                /* coverity[double_free] */
                rc = plugin_call_syntax_filter_sub(pb, a, &f->f_sub);
                if (rc == 0 || rc == LDAP_TIMELIMIT_EXCEEDED) {
                    break;
                }
            }
        }
    }

    slapi_log_err(SLAPI_LOG_FILTER, "test_substring_filter", "<= %d\n", rc);
    return (rc);
}

/*
 * Here's a duplicate vattr filter test code modified to support vattrs.
*/

/*
 * slapi_vattr_filter_test - test a filter against a single entry.
 *
 * Supports the case where the filter mentions virtual attributes.
 * Performance for a real attr only filter is same as for slapi_filter_test()
 * No explicit support for vattrs in extended filters because:
 *     1. the matching rules must support virtual attributes themselves.
 *  2. if no matching rule is specified it defaults to equality so
 *  could just use a normal filter with equality.
 *  3. virtual naming attributes are probably too complex to support.
 *
 * returns    0    filter matched
 *            -1    filter did not match
 *            >0    an ldap error code
 */

int
slapi_vattr_filter_test(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access)
{
    return slapi_vattr_filter_test_ext(pb, e, f, verify_access, 0);
}

/*
 * vattr_filter_test_ext - full-feature filter test function
 *
 * the filter test functions can  be run in three different modes:
 * - verify that the filter matches the entry
 * - verify that the bound user has access to the attributes used in the filter
 * - verify that the filter matches and the user has access to the attributes
 *
 * A special situation is the case of OR filters, eg
 *   (|(attr1=xxx)(attr2=yyy)(attr3=zzz))
 * and the case to verify access and filter matching. An or filter is true if any
 * of the filter components in the or filter is true. So if (attr2=yyy) matches
 * and the user has access to attr2 the complete filter should match.
 * But to prevent using a mixture of matching filter components and components with
 * granted access to guess values of components without access filter evaluation needs
 * to handle access and matching synchronously.
 * It is also not sufficient to set a component without access to false because in cases
 * where this component is part of a NOT filter it would be negated to true.
 * Filter components withou access to the attribute need to be completely ignored.
 *
 * The implementation uses a three valued logic where the result of the evaluation of a
 * filter component can be:
 * - true: access to attribute and filter matches
 * - false: access to attribute and filter does not match
 * - undefined: no access to the attribute or any error during filter evaluatio
 *
 * The rules for complex filters are:
 *  (!(undefined)) --> undefined
 *  (|(undefined)(true)) --> true
 *  (|(undefined)(false)) --> false
 *  (&(undefined)(true)) --> undefined
 *  (&(undefined)(false)) --> false
 *
 * This is reflected in the return codes:
 *     0    filter matched (true)
 *    -1    filter did not match (false)
 *    >0    undefined (or an ldap error code)
 */
int
slapi_vattr_filter_test_ext(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access,
    int only_check_access)
{
    int rc = 0; /* a no op request succeeds */
    int access_check_done = 0;

    if (only_check_access != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_vattr_filter_test_ext",
            "⚠️  DANGER ⚠️  - only_check_access mode is BROKEN!!! YOU MUST CHECK ACCESS WITH FILTER MATCHING");
    }
    PR_ASSERT(only_check_access == 0);

    /* Fix for ticket 48275
     * If we want to handle or components which can contain nonmatching components without access propoerly
     * always filter verification and access check have to be done together for each component
     */
    rc = slapi_vattr_filter_test_ext_internal(pb, e, f, verify_access, only_check_access, &access_check_done);

    return rc;
}

static int
slapi_vattr_filter_test_ext_internal(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *f,
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int rc = LDAP_SUCCESS;

    /*
     * RJP: Not sure if this is semantically right, but we have to
     * return something if f is NULL. If there is no filter,
     * then we say that it did match and return 0.
     */
    if (f == NULL) {
        return (0);
    }

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
        if (verify_access) {
            rc = test_filter_access(pb, e, f->f_ava.ava_type,
                                    &f->f_ava.ava_value);
            *access_check_done = 1;
        }
        if (only_check_access || rc != LDAP_SUCCESS) {
            return (rc);
        }
        rc = vattr_test_filter(pb, e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
        break;

    case LDAP_FILTER_SUBSTRINGS:
        if (verify_access) {
            rc = test_filter_access(pb, e, f->f_sub_type, NULL);
            *access_check_done = 1;
        }
        if (only_check_access || rc != LDAP_SUCCESS) {
            return (rc);
        }
        rc = vattr_test_filter(pb, e, f, FILTER_TYPE_SUBSTRING, f->f_sub_type);
        break;

    case LDAP_FILTER_GE:
        if (verify_access) {
            rc = test_filter_access(pb, e, f->f_ava.ava_type,
                                    &f->f_ava.ava_value);
            *access_check_done = 1;
        }
        if (only_check_access || rc != LDAP_SUCCESS) {
            return (rc);
        }
        rc = vattr_test_filter(pb, e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
        break;

    case LDAP_FILTER_LE:
        if (verify_access) {
            rc = test_filter_access(pb, e, f->f_ava.ava_type,
                                    &f->f_ava.ava_value);
            *access_check_done = 1;
        }
        if (only_check_access || rc != LDAP_SUCCESS) {
            return (rc);
        }
        rc = vattr_test_filter(pb, e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
        break;

    case LDAP_FILTER_PRESENT:
        if (verify_access) {
            rc = test_filter_access(pb, e, f->f_type, NULL);
            *access_check_done = 1;
        }
        if (only_check_access || rc != LDAP_SUCCESS) {
            return (rc);
        }
        rc = vattr_test_filter(pb, e, f, FILTER_TYPE_PRES, f->f_type);
        break;

    case LDAP_FILTER_APPROX:
        if (verify_access) {
            rc = test_filter_access(pb, e, f->f_ava.ava_type,
                                    &f->f_ava.ava_value);
            *access_check_done = 1;
        }
        if (only_check_access || rc != LDAP_SUCCESS) {
            return (rc);
        }
        rc = vattr_test_filter(pb, e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
        break;

    case LDAP_FILTER_EXTENDED:
        rc = test_extensible_filter(pb, e, &f->f_mr, verify_access,
                                    only_check_access, access_check_done);
        break;

    case LDAP_FILTER_AND:
        rc = vattr_test_filter_list_and(pb, e, f->f_and,
                                        LDAP_FILTER_AND, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_OR:
        rc = vattr_test_filter_list_or(pb, e, f->f_or,
                                       LDAP_FILTER_OR, verify_access, only_check_access, access_check_done);
        break;

    case LDAP_FILTER_NOT:
        slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_NOT", "=>\n");
        rc = slapi_vattr_filter_test_ext_internal(pb, e, f->f_not, verify_access, only_check_access, access_check_done);
        if (verify_access && only_check_access) {
            /* dont play with access control return codes
             * do not negate return code */
            slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_NOT only check access", "<= %d\n", rc);
            break;
        }
        if (rc > 0) {
            /* an error occurred or access denied, don't negate */
            slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_NOT slapi_vattr_filter_test_ext_internal fails", "<= %d\n", rc);
            break;
        }
        if (verify_access) {
            int rc2;
            if (!(*access_check_done)) {
                /* the filter failed so access control was not checked
                 * for NOT filters this is significant so we must ensure
                 * access control is checked
                 */
                /* check access control only */
                rc2 = slapi_vattr_filter_test_ext_internal(pb, e, f->f_not, verify_access, -1 /*only_check_access*/, access_check_done);
                /* preserve error code if any */
                if (rc2) {
                    rc = rc2;
                } else {
                    rc = (rc == 0) ? -1 : 0;
                }
            } else {
                rc = (rc == 0) ? -1 : 0;
            }
        } else {
            /* filter verification only, no error */
            rc = (rc == 0) ? -1 : 0;
        }
        slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_NOT", "<= %d\n", rc);
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_vattr_filter_test_ext_internal",
                      "Unknown filter type 0x%lX\n", f->f_choice);
        rc = -1;
    }

    return (rc);
}

static int
test_filter_access(Slapi_PBlock *pb,
                   Slapi_Entry *e,
                   char *attr_type,
                   struct berval *attr_val)
{
    /*
     * attr_type--attr_type to test for.
     * attr_val--attr value to test for
    */
    int rc;
    char *attrs[2] = {NULL, NULL};
    attrs[0] = attr_type;

    rc = plugin_call_acl_plugin(pb, e, attrs, attr_val,
                                SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL);

    slapi_log_err(SLAPI_LOG_FILTER, "slapi_vattr_filter_test_ext_internal",
                  "acl result for %s %s = %d\n", slapi_entry_get_dn_const(e), attr_type, rc);

    return (rc);
}

static int
vattr_test_filter_list_and(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *flist,
    int ftype __attribute__((unused)),
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int nomatch = -1;
    int undefined = 0;
    int rc = 0;
    struct slapi_filter *f;

    slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_and", "=>\n");

    for (f = flist; f != NULL; f = f->f_next) {
        rc = slapi_vattr_filter_test_ext_internal(pb, e, f, verify_access, only_check_access, access_check_done);
        if (rc > 0) {
            undefined = rc;
        } else if (rc < 0) {
            undefined = 0;
            nomatch = -1;
            break;
        } else {
            if (!verify_access || (*access_check_done)) {
                nomatch = 0;
            } else {
                /* check access */
                rc = slapi_vattr_filter_test_ext_internal(pb, e, f, verify_access, 1, access_check_done);
                if (rc)
                    undefined = rc;
            }
        }
    }

    slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_and", "<= %d\n", nomatch);
    if (undefined)
        return undefined;
    return (nomatch);
}

static int
vattr_test_filter_list_or(
    Slapi_PBlock *pb,
    Slapi_Entry *e,
    struct slapi_filter *flist,
    int ftype __attribute__((unused)),
    int verify_access,
    int only_check_access,
    int *access_check_done)
{
    int nomatch = 1;
    int undefined = 0;
    int rc = 0;
    struct slapi_filter *f;

    slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_or", "=>\n");

    for (f = flist; f != NULL; f = f->f_next) {
        if (verify_access) {
            /* we do access check first */
            rc = slapi_vattr_filter_test_ext_internal(pb, e, f, verify_access, -1, access_check_done);
            if (rc != 0) {
                /* no access to this component, ignore it */
                undefined = rc;
                continue;
            }
        }
        /* we are not evaluating if the entry matches
         * but only that we have access to ALL components
         * so check the next one
         */
        if (only_check_access) {
            continue;
        }
        /* now check if filter matches */
        /*
         * We can NOT skip this because we need to know if the item we matched on
         * is the item with access denied.
         */
        undefined = 0;
        rc = slapi_vattr_filter_test_ext_internal(pb, e, f, 0, 0, access_check_done);
        if (rc == 0) {
            undefined = 0;
            nomatch = 0;
            /* We matched, and have access. we can now return */
            break;
        } else if (rc > 0) {
            undefined = rc;
        } else {
            /* filter didn't match, but we have one or component evaluated */
            nomatch = -1;
        }
    }

    slapi_log_err(SLAPI_LOG_FILTER, "vattr_test_filter_list_or", "<= %d\n", nomatch);

    if (nomatch == 1)
        return undefined;
    return (nomatch);
}
