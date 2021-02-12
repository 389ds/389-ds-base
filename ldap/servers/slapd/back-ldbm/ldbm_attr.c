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

/* attr.c - backend routines for dealing with attributes */

#include "back-ldbm.h"

static void
attr_index_idlistsize_done(struct index_idlistsizeinfo *idlinfo)
{
    if (idlinfo) {
        slapi_valueset_free(idlinfo->ai_values);
        idlinfo->ai_values = NULL;
    }
}

static void
attr_index_idlistsize_free(struct index_idlistsizeinfo **idlinfo)
{
    attr_index_idlistsize_done(*idlinfo);
    slapi_ch_free((void **)idlinfo);
}

struct attrinfo *
attrinfo_new()
{
    struct attrinfo *p = (struct attrinfo *)slapi_ch_calloc(1, sizeof(struct attrinfo));
    return p;
}

void
attrinfo_delete_idlistinfo(DataList **idlinfo_dl)
{
    if (idlinfo_dl && *idlinfo_dl) {
        dl_cleanup(*idlinfo_dl, (FREEFN)attr_index_idlistsize_free);
        dl_free(idlinfo_dl);
    }
}

void
attrinfo_delete(struct attrinfo **pp)
{
    if (pp != NULL && *pp != NULL) {
        idl_release_private(*pp);
        (*pp)->ai_key_cmp_fn = NULL;
        slapi_ch_free((void **)&((*pp)->ai_type));
        slapi_ch_free((void **)(*pp)->ai_index_rules);
        slapi_ch_free((void **)&((*pp)->ai_attrcrypt));
        attr_done(&((*pp)->ai_sattr));
        attrinfo_delete_idlistinfo(&(*pp)->ai_idlistinfo);
        if ((*pp)->ai_dblayer) {
            /* attriinfo is deleted.  Cleaning up the backpointer at the same time. */
            ((dblayer_handle *)((*pp)->ai_dblayer))->dblayer_handle_ai_backpointer = NULL;
        }
        slapi_ch_free((void **)pp);
        *pp = NULL;
    }
}

static int
attrinfo_internal_delete(caddr_t data, caddr_t arg __attribute__((unused)))
{
    struct attrinfo *n = (struct attrinfo *)data;
    attrinfo_delete(&n);
    return 0;
}

void
attrinfo_deletetree(ldbm_instance *inst)
{
    avl_free(inst->inst_attrs, attrinfo_internal_delete);
}


static int
ainfo_type_cmp(
    char *type,
    struct attrinfo *a)
{
    return (strcasecmp(type, a->ai_type));
}

static int
ainfo_cmp(
    struct attrinfo *a,
    struct attrinfo *b)
{
    return (strcasecmp(a->ai_type, b->ai_type));
}

void
attrinfo_delete_from_tree(backend *be, struct attrinfo *ai)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    avl_delete(&inst->inst_attrs, ai, ainfo_cmp);
}

/*
 * Called when a duplicate "index" line is encountered.
 *
 * returns 1 => original from init code, indexmask updated
 *       2 => original not from init code, warn the user
 *
 * Hard coded to return a 1 always...
 *
 */

static int
ainfo_dup(
    struct attrinfo *a,
    struct attrinfo *b)
{
    /* merge duplicate indexing information */
    if (b->ai_indexmask == 0 || b->ai_indexmask == INDEX_OFFLINE) {
        a->ai_indexmask = INDEX_OFFLINE; /* turns off all indexes */
        charray_free(a->ai_index_rules);
        a->ai_index_rules = NULL;
    }
    a->ai_indexmask |= b->ai_indexmask;
    if (b->ai_indexmask & INDEX_RULES) {
        charray_merge(&a->ai_index_rules, b->ai_index_rules, 1);
    }
    /* free the old idlistinfo from a - transfer the list from b to a */
    attrinfo_delete_idlistinfo(&a->ai_idlistinfo);
    a->ai_idlistinfo = b->ai_idlistinfo;
    b->ai_idlistinfo = NULL;

    /* copy cmp functions and substr lengths */
    a->ai_key_cmp_fn = b->ai_key_cmp_fn;
    a->ai_dup_cmp_fn = b->ai_dup_cmp_fn;
    if (b->ai_substr_lens) {
        size_t substrlen = sizeof(int) * INDEX_SUBSTRLEN;
        a->ai_substr_lens = (int *)slapi_ch_calloc(1, substrlen);
        memcpy(a->ai_substr_lens, b->ai_substr_lens, substrlen);
    }

    return (1);
}

void
ainfo_get(
    backend *be,
    char *type,
    struct attrinfo **at)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    if ((*at = (struct attrinfo *)avl_find(inst->inst_attrs, type,
                                           ainfo_type_cmp)) == NULL) {
        if ((*at = (struct attrinfo *)avl_find(inst->inst_attrs,
                                               LDBM_PSEUDO_ATTR_DEFAULT, ainfo_type_cmp)) == NULL) {
            return;
        }
    }
}

void
_set_attr_substrlen(int index, char *str, int **substrlens)
{
    char *p = NULL;
    /* nsSubStrXxx=<VAL> is passed;
       set it to the attribute's plugin */
    p = strchr(str, '=');
    if (NULL != p) {
        long sublen = strtol(++p, (char **)NULL, 10);
        if (sublen > 0) { /* 0 is not acceptable */
            if (NULL == *substrlens) {
                *substrlens = (int *)slapi_ch_calloc(1,
                                                     sizeof(int) * INDEX_SUBSTRLEN);
            }
            (*substrlens)[index] = sublen;
        }
    }
}

#define NS_INDEX_IDLISTSCANLIMIT "nsIndexIDListScanLimit"
#define LIMIT_KW "limit="
#define LIMIT_LEN sizeof(LIMIT_KW) - 1
#define TYPE_KW "type="
#define TYPE_LEN sizeof(TYPE_KW) - 1
#define FLAGS_KW "flags="
#define FLAGS_LEN sizeof(FLAGS_KW) - 1
#define VALUES_KW "values="
#define VALUES_LEN sizeof(VALUES_KW) - 1
#define FLAGS_AND_KW "AND"
#define FLAGS_AND_LEN sizeof(FLAGS_AND_KW) - 1

static int
attr_index_parse_idlistsize_values(Slapi_Attr *attr, struct index_idlistsizeinfo *idlinfo, char *values, const char *strval, char *returntext)
{
    int rc = 0;
    /* if we are here, values is non-NULL and not an empty string - parse it */
    char *ptr = NULL;
    char *lasts = NULL;
    char *val;
    int syntaxcheck = config_get_syntaxcheck();
    IFP syntax_validate_fn = syntaxcheck ? attr->a_plugin->plg_syntax_validate : NULL;
    char staticfiltstrbuf[1024];                     /* for small filter strings */
    char *filtstrbuf = staticfiltstrbuf;             /* default if not malloc'd */
    size_t filtstrbuflen = sizeof(staticfiltstrbuf); /* default if not malloc'd */
    Slapi_Filter *filt = NULL;                       /* for filter converting/unescaping config values */

    /* caller should have already checked that values is valid and contains a "=" */
    PR_ASSERT(values);
    ptr = PL_strchr(values, '=');
    PR_ASSERT(ptr);
    ++ptr;
    for (val = ldap_utf8strtok_r(ptr, ",", &lasts); val;
         val = ldap_utf8strtok_r(NULL, ",", &lasts)) {
        Slapi_Value **ivals = NULL; /* for config values converted to keys */
        int ii;
#define FILT_TEMPL_BEGIN "(a="
#define FILT_TEMPL_END ")"
        size_t filttemplen = sizeof(FILT_TEMPL_BEGIN) - 1 + sizeof(FILT_TEMPL_END) - 1;
        size_t vallen = strlen(val);

        if ((vallen + filttemplen + 1) > filtstrbuflen) {
            filtstrbuflen = vallen + filttemplen + 1;
            if (filtstrbuf == staticfiltstrbuf) {
                filtstrbuf = (char *)slapi_ch_malloc(sizeof(char) * filtstrbuflen);
            } else {
                filtstrbuf = (char *)slapi_ch_realloc(filtstrbuf, sizeof(char) * filtstrbuflen);
            }
        }
        /* each value is a value from a filter which should be escaped like a filter value
         * for each value, create a dummy filter string, then parse and unescape it just
         * like a filter
         */
        PR_snprintf(filtstrbuf, filtstrbuflen, FILT_TEMPL_BEGIN "%s" FILT_TEMPL_END, val);
        filt = slapi_str2filter(filtstrbuf);
        if (!filt) {
            rc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attr_index_parse_idlistsize: invalid value %s in %s",
                        val, strval);
            break;
        }

        if (idlinfo->ai_indextype == INDEX_SUB) {
            if (syntax_validate_fn) {
                /* see if the values match the syntax, but only if checking is enabled */
                char **subany = filt->f_sub_any;
                struct berval bv;

                if (filt->f_sub_initial && *filt->f_sub_initial) {
                    bv.bv_val = filt->f_sub_initial;
                    bv.bv_len = strlen(bv.bv_val);
                    if ((rc = syntax_validate_fn(&bv))) {
                        rc = LDAP_UNWILLING_TO_PERFORM;
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                    "attr_index_parse_idlistsize: initial substring value %s "
                                    "in value %s violates syntax for attribute %s",
                                    bv.bv_val, val, attr->a_type);
                        break;
                    }
                }
                for (; !rc && subany && *subany; ++subany) {
                    char *subval = *subany;
                    if (*subval) {
                        bv.bv_val = subval;
                        bv.bv_len = strlen(bv.bv_val);
                        if ((rc = syntax_validate_fn(&bv))) {
                            rc = LDAP_UNWILLING_TO_PERFORM;
                            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                        "attr_index_parse_idlistsize: initial substring value %s in "
                                        "value %s violates syntax for attribute %s",
                                        bv.bv_val, val, attr->a_type);
                            break;
                        }
                    }
                }
                if (rc) {
                    break;
                }
                if (filt->f_sub_final) {
                    bv.bv_val = filt->f_sub_final;
                    bv.bv_len = strlen(bv.bv_val);
                    if ((rc = syntax_validate_fn(&bv))) {
                        rc = LDAP_UNWILLING_TO_PERFORM;
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                    "attr_index_parse_idlistsize: final substring value %s in value "
                                    "%s violates syntax for attribute %s",
                                    bv.bv_val, val, attr->a_type);
                        break;
                    }
                }
            }
            /* if we are here, values passed syntax or no checking */
            /* generate index keys */
            (void)slapi_attr_assertion2keys_sub_sv(attr, filt->f_sub_initial, filt->f_sub_any, filt->f_sub_final, &ivals);

        } else if (idlinfo->ai_indextype == INDEX_EQUALITY) {
            Slapi_Value sval;
            /* see if the value matches the syntax, but only if checking is enabled */
            if (syntax_validate_fn && ((rc = syntax_validate_fn(&filt->f_avvalue)))) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: value %s violates syntax for attribute %s",
                            val, attr->a_type);
                break;
            }

            sval.bv.bv_val = filt->f_avvalue.bv_val;
            sval.bv.bv_len = filt->f_avvalue.bv_len;
            sval.v_flags = 0;
            sval.v_csnset = NULL;
            (void)slapi_attr_assertion2keys_ava_sv(attr, &sval, (Slapi_Value ***)&ivals, LDAP_FILTER_EQUALITY);
        }
        /* don't need filter any more */
        slapi_filter_free(filt, 1);
        filt = NULL;

        /* add value(s) in ivals to our value set - disallow duplicates with error */
        for (ii = 0; !rc && ivals && ivals[ii]; ++ii) {
            if (idlinfo->ai_values &&
                slapi_valueset_find(attr, idlinfo->ai_values, ivals[ii])) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: duplicate value %s in %s",
                            slapi_value_get_string(ivals[ii]), val);
                slapi_value_free(&ivals[ii]);
            } else {
                if (!idlinfo->ai_values) {
                    idlinfo->ai_values = slapi_valueset_new();
                }
                slapi_valueset_add_value_ext(idlinfo->ai_values, ivals[ii], SLAPI_VALUE_FLAG_PASSIN);
            }
        }
        /* only free members of ivals that were not moved to ai_values */
        valuearray_free_ext(&ivals, ii);
        ivals = NULL;
    }

    slapi_filter_free(filt, 1);

    if (filtstrbuf != staticfiltstrbuf) {
        slapi_ch_free_string(&filtstrbuf);
    }

    return rc;
}

static int
attr_index_parse_idlistsize_limit(char *ptr, struct index_idlistsizeinfo *idlinfo, char *returntext)
{
    int rc = 0;
    char *endptr;

    PR_ASSERT(ptr && (*ptr == '='));
    ptr++;
    idlinfo->ai_idlistsizelimit = strtol(ptr, &endptr, 10);
    if (*endptr) { /* error in parsing */
        rc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "attr_index_parse_idlistsize: value %s for %s is not valid - "
                    "must be an integer >= -1",
                    ptr, LIMIT_KW);
    } else if (idlinfo->ai_idlistsizelimit < -1) {
        rc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "attr_index_parse_idlistsize: value %s for %s "
                    "must be an integer >= -1",
                    ptr, LIMIT_KW);
    }
    return rc;
}

static int
attr_index_parse_idlistsize_type(char *ptr, struct attrinfo *ai, struct index_idlistsizeinfo *idlinfo, const char *val, const char *strval, char *returntext)
{
    int rc = 0;
    char *ptr_next;
    size_t len;
    size_t preslen = strlen(indextype_PRESENCE);
    size_t eqlen = strlen(indextype_EQUALITY);
    size_t sublen = strlen(indextype_SUB);

    PR_ASSERT(ptr && (*ptr == '='));
    do {
        ++ptr;
        ptr_next = PL_strchr(ptr, ','); /* find next comma */
        if (!ptr_next) {
            ptr_next = PL_strchr(ptr, '\0'); /* find end of string */
        }
        len = ptr_next - ptr;
        if ((len == preslen) && !PL_strncmp(ptr, indextype_PRESENCE, len)) {
            if (idlinfo->ai_indextype & INDEX_PRESENCE) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: duplicate %s in value %s for %s",
                            indextype_PRESENCE, val, strval);
                break;
            }
            if (!(ai->ai_indexmask & INDEX_PRESENCE)) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: attribute %s does not have index type %s",
                            ai->ai_type, indextype_PRESENCE);
                break;
            }
            idlinfo->ai_indextype |= INDEX_PRESENCE;
        } else if ((len == eqlen) && !PL_strncmp(ptr, indextype_EQUALITY, len)) {
            if (idlinfo->ai_indextype & INDEX_EQUALITY) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: duplicate %s in value %s for %s",
                            indextype_EQUALITY, val, strval);
                break;
            }
            if (!(ai->ai_indexmask & INDEX_EQUALITY)) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: attribute %s does not have index type %s",
                            ai->ai_type, indextype_EQUALITY);
                break;
            }
            idlinfo->ai_indextype |= INDEX_EQUALITY;
        } else if ((len == sublen) && !PL_strncmp(ptr, indextype_SUB, len)) {
            if (idlinfo->ai_indextype & INDEX_SUB) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: duplicate %s in value %s for %s",
                            indextype_SUB, val, strval);
                break;
            }
            if (!(ai->ai_indexmask & INDEX_SUB)) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: attribute %s does not have index type %s",
                            ai->ai_type, indextype_SUB);
                break;
            }
            idlinfo->ai_indextype |= INDEX_SUB;
        } else {
            rc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attr_index_parse_idlistsize: unknown or unsupported index type "
                        "%s in value %s for %s",
                        ptr, val, strval);
            break;
        }
    } while ((ptr = PL_strchr(ptr, ',')));

    return rc;
}

static int
attr_index_parse_idlistsize_flags(char *ptr, struct index_idlistsizeinfo *idlinfo, const char *val, const char *strval, char *returntext)
{
    int rc = 0;
    char *ptr_next;
    size_t len;

    PR_ASSERT(ptr && (*ptr == '='));
    do {
        ++ptr;
        ptr_next = PL_strchr(ptr, ','); /* find next comma */
        if (!ptr_next) {
            ptr_next = PL_strchr(ptr, '\0'); /* find end of string */
        }
        len = ptr_next - ptr;
        if ((len == FLAGS_AND_LEN) && !PL_strncmp(ptr, FLAGS_AND_KW, len)) {
            if (idlinfo->ai_flags & INDEX_ALLIDS_FLAG_AND) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: duplicate %s in value %s for %s",
                            FLAGS_AND_KW, val, strval);
                break;
            }
            idlinfo->ai_flags |= INDEX_ALLIDS_FLAG_AND;
        } else {
            rc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attr_index_parse_idlistsize: unknown or unsupported flags %s in value %s for %s",
                        ptr, val, strval);
            break;
        }

    } while ((ptr = PL_strchr(ptr, ',')));
    return rc;
}

static int
attr_index_parse_idlistsize(struct attrinfo *ai, const char *strval, struct index_idlistsizeinfo *idlinfo, char *returntext)
{
    int rc = 0;                            /* assume success */
    char *mystr = slapi_ch_strdup(strval); /* copy for strtok */
    char *values = NULL;
    char *lasts = NULL, *val, *ptr;
    int seen_limit = 0, seen_type = 0, seen_flags = 0, seen_values = 0;
    Slapi_Attr *attr = &ai->ai_sattr;

    if (!mystr) {
        rc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "attr_index_parse_idlistsize: value is empty");
        goto done;
    }

    for (val = ldap_utf8strtok_r(mystr, " ", &lasts); val;
         val = ldap_utf8strtok_r(NULL, " ", &lasts)) {
        ptr = PL_strchr(val, '=');
        if (!ptr || !(*(ptr + 1))) {
            rc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attr_index_parse_idlistsize: invalid value %s - should be keyword=value - in %s",
                        val, strval);
            goto done;
        }
        /* ptr points at first '=' in val */
        if (!PL_strncmp(val, LIMIT_KW, LIMIT_LEN)) {
            if (seen_limit) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: can have only 1 %s in value %s",
                            LIMIT_KW, strval);
                goto done;
            }
            if ((rc = attr_index_parse_idlistsize_limit(ptr, idlinfo, returntext))) {
                goto done;
            }
            seen_limit = 1;
        } else if (!PL_strncmp(val, TYPE_KW, TYPE_LEN)) {
            if (seen_type) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: can have only 1 %s in value %s",
                            TYPE_KW, strval);
                goto done;
            }
            if ((rc = attr_index_parse_idlistsize_type(ptr, ai, idlinfo, val, strval, returntext))) {
                goto done;
            }

            seen_type = 1;
        } else if (!PL_strncmp(val, FLAGS_KW, FLAGS_LEN)) {
            if (seen_flags) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: can have only 1 %s in value %s",
                            FLAGS_KW, strval);
                goto done;
            }
            if ((rc = attr_index_parse_idlistsize_flags(ptr, idlinfo, val, strval, returntext))) {
                goto done;
            }
            seen_flags = 1;
        } else if (!PL_strncmp(val, VALUES_KW, VALUES_LEN)) {
            if (seen_values) {
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "attr_index_parse_idlistsize: can have only 1 %s in value %s",
                            VALUES_KW, strval);
                goto done;
            }
            values = val;
            seen_values = 1;
        } else {
            rc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attr_index_parse_idlistsize: unknown keyword %s in %s",
                        val, strval);
            goto done;
        }
    }

    if (!seen_limit) {
        rc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "attr_index_parse_idlistsize: no limit specified in %s",
                    strval);
        goto done;
    }

    /* parse values last
     * can only have values if type is eq or sub, and only eq by itself or sub by itself
     * eq and sub type values cannot be mixed, so error in that case
     * cannot have type pres,eq and values - pres must be by itself with no values
     */
    if (values) {
        if (idlinfo->ai_indextype == INDEX_EQUALITY) {
            ; /* ok */
        } else if (idlinfo->ai_indextype == INDEX_SUB) {
            ; /* ok */
        } else {
            rc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attr_index_parse_idlistsize: if %s is specified, the %s "
                        "must be %s or %s - not both, and not any other types",
                        VALUES_KW, TYPE_KW, indextype_PRESENCE, indextype_SUB);
            goto done;
        }
    } else {
        goto done;
    }

    /* if we are here, values contains something - parse it */
    rc = attr_index_parse_idlistsize_values(attr, idlinfo, values, strval, returntext);

done:
    slapi_ch_free_string(&mystr);
    return rc;
}

static int
attr_index_idlistsize_config(Slapi_Entry *e, struct attrinfo *ai, char *returntext)
{
    int rc = 0;
    int ii;
    Slapi_Attr *idlattr;
    Slapi_Value *sval;
    struct index_idlistsizeinfo *idlinfo;

    slapi_entry_attr_find(e, NS_INDEX_IDLISTSCANLIMIT, &idlattr);
    if (!idlattr) {
        return rc;
    }
    for (ii = slapi_attr_first_value(idlattr, &sval); !rc && (ii != -1); ii = slapi_attr_next_value(idlattr, ii, &sval)) {
        idlinfo = (struct index_idlistsizeinfo *)slapi_ch_calloc(1, sizeof(struct index_idlistsizeinfo));
        if ((rc = attr_index_parse_idlistsize(ai, slapi_value_get_string(sval), idlinfo, returntext))) {
            attr_index_idlistsize_free(&idlinfo);
            attrinfo_delete_idlistinfo(&ai->ai_idlistinfo);
        } else {
            if (!ai->ai_idlistinfo) {
                ai->ai_idlistinfo = dl_new();
                dl_init(ai->ai_idlistinfo, 1);
            }
            dl_add(ai->ai_idlistinfo, idlinfo);
        }
    }
    return rc;
}

/*
 * Function that process index attributes and modifies attrinfo structure
 *
 * Called while adding default indexes, during db2index execution and
 * when we add/modify/delete index config entry
 *
 * If char *err_buf is not NULL, it will additionally print all error messages to STDERR
 * It is used when we add/modify/delete index config entry, so the user would have a better verbose
 *
 * returns -1, 1 on a failure
 *         0 on success
 */
int
attr_index_config(
    backend *be,
    char *fname,
    int lineno,
    Slapi_Entry *e,
    int init __attribute__((unused)),
    int indextype_none,
    char *err_buf)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int j = 0;
    struct attrinfo *a;
    int return_value = -1;
    int *substrlens = NULL;
    int need_compare_fn = 0;
    int hasIndexType = 0;
    const char *attrsyntax_oid = NULL;
    const struct berval *attrValue;
    Slapi_Value *sval;
    Slapi_Attr *attr;
    int mr_count = 0;
    char myreturntext[SLAPI_DSE_RETURNTEXT_SIZE];
    int substrval = 0;

    /* Get the cn */
    if (0 == slapi_entry_attr_find(e, "cn", &attr)) {
        slapi_attr_first_value(attr, &sval);
        attrValue = slapi_value_get_berval(sval);
    } else {
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: missing indexing arguments\n");
        slapi_log_err(SLAPI_LOG_ERR, "attr_index_config", "Missing indexing arguments\n");
        return -1;
    }

    a = attrinfo_new();
    slapi_attr_init(&a->ai_sattr, attrValue->bv_val);
    /*
     *  we can't just set a->ai_type to the type from a->ai_sattr
     *  if the type has attr options or subtypes, ai_sattr.a_type will
     *  contain them - but for the purposes of indexing, we don't want them
     */
    a->ai_type = slapi_attr_basetype(attrValue->bv_val, NULL, 0);
    attrsyntax_oid = attr_get_syntax_oid(&a->ai_sattr);
    a->ai_indexmask = 0;

    if (indextype_none) {
        /* This is the same has having none for indexType, but applies to the whole entry */
        a->ai_indexmask = INDEX_OFFLINE;
    } else {
        slapi_entry_attr_find(e, "nsIndexType", &attr);
        for (j = slapi_attr_first_value(attr, &sval); j != -1; j = slapi_attr_next_value(attr, j, &sval)) {
            hasIndexType = 1;
            attrValue = slapi_value_get_berval(sval);
            if (strcasecmp(attrValue->bv_val, "pres") == 0) {
                a->ai_indexmask |= INDEX_PRESENCE;
            } else if (strcasecmp(attrValue->bv_val, "eq") == 0) {
                a->ai_indexmask |= INDEX_EQUALITY;
            } else if (strcasecmp(attrValue->bv_val, "approx") == 0) {
                a->ai_indexmask |= INDEX_APPROX;
            } else if (strcasecmp(attrValue->bv_val, "subtree") == 0) {
                /* subtree should be located before "sub" */
                a->ai_indexmask |= INDEX_SUBTREE;
                dblayer_set_dup_cmp_fn(be, a, DBI_DUP_CMP_ENTRYRDN);
            } else if (strcasecmp(attrValue->bv_val, "sub") == 0) {
                a->ai_indexmask |= INDEX_SUB;
            } else if (strcasecmp(attrValue->bv_val, "none") == 0) {
                if (a->ai_indexmask != 0) {
                    slapi_log_err(SLAPI_LOG_WARNING,
                                  "attr_index_config", "%s: line %d: index type \"none\" cannot be combined with other types\n",
                                  fname, lineno);
                }
                a->ai_indexmask = INDEX_OFFLINE; /* note that the index isn't available */
            } else {
                slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE,
                                      "Error: %s: line %d: unknown index type \"%s\" (ignored) in entry (%s), "
                                      "valid index types are \"pres\", \"eq\", \"approx\", or \"sub\"\n",
                                      fname, lineno, attrValue->bv_val, slapi_entry_get_dn(e));
                slapi_log_err(SLAPI_LOG_ERR, "attr_index_config",
                              "%s: line %d: unknown index type \"%s\" (ignored) in entry (%s), "
                              "valid index types are \"pres\", \"eq\", \"approx\", or \"sub\"\n",
                              fname, lineno, attrValue->bv_val, slapi_entry_get_dn(e));
                attrinfo_delete(&a);
                return -1;
            }
        }
        if (hasIndexType == 0) {
            /* indexType missing, error out */
            slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: missing index type\n");
            slapi_log_err(SLAPI_LOG_ERR, "attr_index_config", "Missing index type\n");
            attrinfo_delete(&a);
            return -1;
        }
    }

    /* compute a->ai_index_rules: */
    /* for index rules there are two uses:
     * 1) a simple way to define an ordered index to support <= and >= searches
     * for those attributes which do not have an ORDERING matching rule defined
     * for them in their schema definition.  The index generated is not a :RULE:
     * index, it is a normal = EQUALITY index, with the keys ordered using the
     * comparison function provided by the syntax plugin for the attribute.  For
     * example - the uidNumber attribute has INTEGER syntax, but the standard
     * definition of the attribute does not specify an ORDERING matching rule.
     * By default, this means that you cannot perform searches like
     * (uidNumber>=501) - but many users expect to be able to perform this type of
     * search.  By specifying that you want an ordered index, using an integer
     * matching rule, you can support indexed seaches of this type.
     * 2) a RULE index - the index key prefix is :NAMEOROID: - this is used
     * to support extensible match searches like (cn:fr-CA.3:=gilles), which would
     * find the index key :fr-CA.3:gilles in the cn index.
     * We check first to see if this is a simple ordered index - user specified an
     * ordering matching rule compatible with the attribute syntax, and there is
     * a compare function.  If not, we assume it is a RULE index definition.
     */
    /*
     * nsSubStrBegin: 2
     * nsSubStrMiddle: 2
     * nsSubStrEnd: 2
     */
    substrval = slapi_entry_attr_get_int(e, INDEX_ATTR_SUBSTRBEGIN);
    if (substrval) {
        substrlens = (int *)slapi_ch_calloc(1, sizeof(int) * INDEX_SUBSTRLEN);
        substrlens[INDEX_SUBSTRBEGIN] = substrval;
    }
    substrval = slapi_entry_attr_get_int(e, INDEX_ATTR_SUBSTRMIDDLE);
    if (substrval) {
        if (!substrlens) {
            substrlens = (int *)slapi_ch_calloc(1, sizeof(int) * INDEX_SUBSTRLEN);
        }
        substrlens[INDEX_SUBSTRMIDDLE] = substrval;
    }
    substrval = slapi_entry_attr_get_int(e, INDEX_ATTR_SUBSTREND);
    if (substrval) {
        if (!substrlens) {
            substrlens = (int *)slapi_ch_calloc(1, sizeof(int) * INDEX_SUBSTRLEN);
        }
        substrlens[INDEX_SUBSTREND] = substrval;
    }
    a->ai_substr_lens = substrlens;

    if (0 == slapi_entry_attr_find(e, "nsMatchingRule", &attr)) {
        char **official_rules;
        size_t k = 0;

        /* Get a count and allocate an array for the official matching rules */
        slapi_attr_get_numvalues(attr, &mr_count);
        official_rules = (char **)slapi_ch_malloc((mr_count + 1) * sizeof(char *));

        for (j = slapi_attr_first_value(attr, &sval); j != -1; j = slapi_attr_next_value(attr, j, &sval)) {
            /* Check that index_rules[j] is an official OID */
            char *officialOID = NULL;
            IFP mrINDEX = NULL;
            Slapi_PBlock *pb = NULL;
            int do_continue = 0; /* can we skip the RULE parsing stuff? */
            attrValue = slapi_value_get_berval(sval);
            /*
             * In case nsSubstr{Begin,Middle,End}: num is not set, but set by this format:
             *   nsMatchingRule: nsSubstrBegin=2
             *   nsMatchingRule: nsSubstrMiddle=2
             *   nsMatchingRule: nsSubstrEnd=2
             */
            if (PL_strcasestr(attrValue->bv_val, INDEX_ATTR_SUBSTRBEGIN)) {
                if (!a->ai_substr_lens || !a->ai_substr_lens[INDEX_SUBSTRBEGIN]) {
                    _set_attr_substrlen(INDEX_SUBSTRBEGIN, attrValue->bv_val, &substrlens);
                }
                do_continue = 1; /* done with j - next j */
            }
            if (PL_strcasestr(attrValue->bv_val, INDEX_ATTR_SUBSTRMIDDLE)) {
                if (!a->ai_substr_lens || !a->ai_substr_lens[INDEX_SUBSTRMIDDLE]) {
                    _set_attr_substrlen(INDEX_SUBSTRMIDDLE, attrValue->bv_val, &substrlens);
                }
                do_continue = 1; /* done with j - next j */
            }
            if (PL_strcasestr(attrValue->bv_val, INDEX_ATTR_SUBSTREND)) {
                if (!a->ai_substr_lens || !a->ai_substr_lens[INDEX_SUBSTREND]) {
                    _set_attr_substrlen(INDEX_SUBSTREND, attrValue->bv_val, &substrlens);
                }
                do_continue = 1; /* done with j - next j */
            }
            /* check if this is a simple ordering specification
               for an attribute that has no ordering matching rule */
            if (slapi_matchingrule_is_ordering(attrValue->bv_val, attrsyntax_oid) &&
                slapi_matchingrule_can_use_compare_fn(attrValue->bv_val) &&
                !a->ai_sattr.a_mr_ord_plugin) { /* no ordering for this attribute */
                need_compare_fn = 1;            /* get compare func for this attr */
                do_continue = 1;                /* done with j - next j */
            }

            if (do_continue) {
                continue; /* done with index_rules[j] */
            }

            /* must be a RULE specification */
            pb = slapi_pblock_new();
            /*
             *  next check if this is a RULE type index
             *  try to actually create an indexer and see if the indexer
             *  actually has a regular INDEX_FN or an INDEX_SV_FN
             */
            if (!slapi_pblock_set(pb, SLAPI_PLUGIN_MR_OID, attrValue->bv_val) &&
                !slapi_pblock_set(pb, SLAPI_PLUGIN_MR_TYPE, a->ai_type) &&
                !slapi_mr_indexer_create(pb) &&
                ((!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX) &&
                  mrINDEX != NULL) ||
                 (!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_INDEX_SV_FN, &mrINDEX) &&
                  mrINDEX != NULL)) &&
                !slapi_pblock_get(pb, SLAPI_PLUGIN_MR_OID, &officialOID) &&
                officialOID != NULL) {
                if (!strcasecmp(attrValue->bv_val, officialOID)) {
                    official_rules[k++] = slapi_ch_strdup(officialOID);
                } else {
                    char *preamble = slapi_ch_smprintf("%s: line %d", fname, lineno);
                    slapi_log_err(SLAPI_LOG_WARNING, "attr_index_config",
                                  "%s: use \"%s\" instead of \"%s\" (ignored)\n",
                                  preamble, officialOID, attrValue->bv_val);
                    slapi_ch_free((void **)&preamble);
                }
            } else { /* we don't know what this is */
                slapi_log_err(SLAPI_LOG_WARNING, "attr_index_config", "%s: line %d: "
                                                                      "unknown or invalid matching rule \"%s\" in index configuration (ignored)\n",
                              fname, lineno, attrValue->bv_val);
            }

            { /*
                *  It would improve speed to save the indexer, for future use.
                * But, for simplicity, we destroy it now:
                */
                IFP mrDESTROY = NULL;
                if (!slapi_pblock_get(pb, SLAPI_PLUGIN_DESTROY_FN, &mrDESTROY) &&
                    mrDESTROY != NULL) {
                    mrDESTROY(pb);
                }
            }
            slapi_pblock_destroy(pb);
        }
        official_rules[k] = NULL;
        if (substrlens) {
            a->ai_substr_lens = substrlens;
        }
        if (k > 0) {
            a->ai_index_rules = official_rules;
            a->ai_indexmask |= INDEX_RULES;
        } else {
            slapi_ch_free((void **)&official_rules);
        }
    }
    if ((return_value = attr_index_idlistsize_config(e, a, myreturntext))) {
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Error: %s: Failed to parse idscanlimit info: %d:%s\n",
                              fname, return_value, myreturntext);
        slapi_log_err(SLAPI_LOG_ERR, "attr_index_config", "%s: Failed to parse idscanlimit info: %d:%s\n",
                      fname, return_value, myreturntext);
        if (err_buf != NULL) {
            /* we are inside of a callback, we shouldn't allow malformed attributes in index entries */
            attrinfo_delete(&a);
            return return_value;
        }
    }

    /* initialize the IDL code's private data */
    return_value = idl_init_private(be, a);
    if (0 != return_value) {
        /* fatal error, exit */
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Error: %s: line %d:Fatal Error: Failed to initialize attribute structure\n",
                              fname, lineno);
        slapi_log_err(SLAPI_LOG_CRIT, "attr_index_config",
                      "%s: line %d:Fatal Error: Failed to initialize attribute structure\n",
                      fname, lineno);
        exit(1);
    }

    /* if user didn't specify an ordering rule in the index config,
       see if the schema def for the attr defines one */
    if (!need_compare_fn && a->ai_sattr.a_mr_ord_plugin) {
        need_compare_fn = 1;
    }

    if (need_compare_fn) {
        int rc = attr_get_value_cmp_fn(&a->ai_sattr, &a->ai_key_cmp_fn);
        if (rc != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "attr_index_config", "The attribute [%s] does not have a valid ORDERING matching rule - error %d:%s\n",
                          a->ai_type, rc, ldap_err2string(rc));
            a->ai_key_cmp_fn = NULL;
        }
    }

    if (avl_insert(&inst->inst_attrs, a, ainfo_cmp, ainfo_dup) != 0) {
        /* duplicate - existing version updated */
        attrinfo_delete(&a);
    }

    return 0;
}

/*
 * Function that creates a new attrinfo structure and
 * inserts it into the avl tree. This is used by code
 * that wants to store attribute-level configuration data
 * e.g. attribute encryption, but where the attr_info
 * structure doesn't exist because the attribute in question
 * is not indexed.
 */
void
attr_create_empty(backend *be, char *type, struct attrinfo **ai)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    struct attrinfo *a = attrinfo_new();
    slapi_attr_init(&a->ai_sattr, type);
    a->ai_type = slapi_ch_strdup(type);
    if (avl_insert(&inst->inst_attrs, a, ainfo_cmp, ainfo_dup) != 0) {
        /* duplicate - existing version updated */
        attrinfo_delete(&a);
        ainfo_get(be, type, &a);
    }
    *ai = a;
}

/* Code for computed attributes */
extern char *hassubordinates;
extern char *numsubordinates;

static int
ldbm_compute_evaluator(computed_attr_context *c, char *type, Slapi_Entry *e, slapi_compute_output_t outputfn)
{
    int rc = 0;

    if (strcasecmp(type, numsubordinates) == 0) {
        Slapi_Attr *read_attr = NULL;
        /* Check to see whether this attribute is already present in the entry */
        if (0 != slapi_entry_attr_find(e, numsubordinates, &read_attr)) {
            /* If not, we return it as zero */
            Slapi_Attr our_attr;
            slapi_attr_init(&our_attr, numsubordinates);
            our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
            valueset_add_string(&our_attr, &our_attr.a_present_values, "0", CSN_TYPE_UNKNOWN, NULL);
            rc = (*outputfn)(c, &our_attr, e);
            attr_done(&our_attr);
            return (rc);
        }
    }
    if (strcasecmp(type, hassubordinates) == 0) {
        Slapi_Attr *read_attr = NULL;
        Slapi_Attr our_attr;
        slapi_attr_init(&our_attr, hassubordinates);
        our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
        /* This attribute is always computed */
        /* Check to see whether the subordinate count attribute is already present in the entry */
        rc = slapi_entry_attr_find(e, numsubordinates, &read_attr);
        if ((0 != rc) || slapi_entry_attr_hasvalue(e, numsubordinates, "0")) {
            /* If not, or present and zero, we return FALSE, otherwise TRUE */
            valueset_add_string(&our_attr, &our_attr.a_present_values, "FALSE", CSN_TYPE_UNKNOWN, NULL);
        } else {
            valueset_add_string(&our_attr, &our_attr.a_present_values, "TRUE", CSN_TYPE_UNKNOWN, NULL);
        }
        rc = (*outputfn)(c, &our_attr, e);
        attr_done(&our_attr);
        return (rc);
    }

    return -1; /* I see no ships */
}

/* What are we doing ?
    The back-end can't search properly for the hasSubordinates and
    numSubordinates attributes. The reason being that they're not
    always stored on entries, so filter test fails to do the correct thing.
    However, it is possible to rewrite a given search to one
    which will work, given that numSubordinates is present when non-zero,
    and we maintain a presence index for numSubordinates.
 */
/* Searches we rewrite here :
    substrings of the form
    (hassubordinates=TRUE)  to (&(numsubordinates=*)(numsubordinates>=1)) [indexed]
    (hassubordinates=FALSE) to (&(objectclass=*)(!(numsubordinates=*)))   [not indexed]
    (hassubordinates=*) to (objectclass=*)   [not indexed]
    (numsubordinates=*) to (objectclass=*)   [not indexed]
     (numsubordinates=x)  to (&(numsubordinates=*)(numsubordinates=x)) [indexed]
     (numsubordinates>=x)  to (&(numsubordinates=*)(numsubordinates>=x)) [indexed where X > 0]
     (numsubordinates<=x)  to (&(numsubordinates=*)(numsubordinates<=x)) [indexed]

    anything else involving numsubordinates and hassubordinates we flag as unwilling to perform

*/

/* Before calling this function, you must free all the parts
   which will be overwritten, this function dosn't know
   how to do that */
static int
replace_filter(Slapi_Filter *f, char *s)
{
    Slapi_Filter *newf = NULL;
    Slapi_Filter *temp = NULL;
    char *buf = slapi_ch_strdup(s);

    newf = slapi_str2filter(buf);
    slapi_ch_free((void **)&buf);

    if (NULL == newf) {
        return -1;
    }

    /* Now take the parts of newf and put them in f */
    /* An easy way to do this is to preserve the "next" ptr */
    temp = f->f_next;
    *f = *newf;
    f->f_next = temp;
    /* Free the new filter husk */
    slapi_ch_free((void **)&newf);
    return 0;
}

static void
find_our_friends(char *s, int *has, int *num)
{
    *has = (0 == strcasecmp(s, "hassubordinates"));
    if (!(*has)) {
        *num = (0 == strcasecmp(s, LDBM_NUMSUBORDINATES_STR));
    }
}

/* Free the parts of a filter we're about to overwrite */
void
free_the_filter_bits(Slapi_Filter *f)
{
    /* We need to free: */
    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        ava_done(&f->f_ava);
        break;

    case LDAP_FILTER_PRESENT:
        if (f->f_type != NULL) {
            slapi_ch_free((void **)&(f->f_type));
        }
        break;

    default:
        break;
    }
}

static int
grok_and_rewrite_filter(Slapi_Filter *f)
{
    Slapi_Filter *p = NULL;
    int has = 0;
    int num = 0;
    char *rhs = NULL;
    struct berval rhs_berval;

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
        /* Does this involve either of our target attributes ? */
        find_our_friends(f->f_ava.ava_type, &has, &num);
        if (has || num) {
            rhs = f->f_ava.ava_value.bv_val;
            if (has) {
                if (0 == strcasecmp(rhs, "TRUE")) {
                    free_the_filter_bits(f);
                    replace_filter(f, "(&(numsubordinates=*)(numsubordinates>=1))");
                } else if (0 == strcasecmp(rhs, "FALSE")) {
                    free_the_filter_bits(f);
                    replace_filter(f, "(&(objectclass=*)(!(numsubordinates=*)))");
                } else {
                    return 1; /* Filter we can't rewrite */
                }
            }
            if (num) {
                int rhs_number = 0;

                rhs_number = atoi(rhs);
                if (rhs_number > 0) {

                    char *theType = f->f_ava.ava_type;
                    rhs_berval = f->f_ava.ava_value;
                    replace_filter(f, "(&(numsubordinates=*)(numsubordinates=x))");
                    /* Now fixup the resulting filter so that x = rhs */
                    slapi_ch_free((void **)&(f->f_and->f_next->f_ava.ava_value.bv_val));
                    /*free type also */
                    slapi_ch_free((void **)&theType);

                    f->f_and->f_next->f_ava.ava_value = rhs_berval;
                } else {
                    if (rhs_number == 0) {
                        /* This is the same as hassubordinates=FALSE */
                        free_the_filter_bits(f);
                        replace_filter(f, "(&(objectclass=*)(!(numsubordinates=*)))");
                    } else {
                        return 1;
                    }
                }
            }
            return 0;
        }
        break;

    case LDAP_FILTER_GE:
        find_our_friends(f->f_ava.ava_type, &has, &num);
        if (has) {
            return 1; /* Makes little sense for this attribute */
        }
        if (num) {
            int rhs_num = 0;
            rhs = f->f_ava.ava_value.bv_val;
            /* is the value zero ? */
            rhs_num = atoi(rhs);
            if (0 == rhs_num) {
                /* If so, rewrite to same as numsubordinates=* */
                free_the_filter_bits(f);
                replace_filter(f, "(objectclass=*)");
            } else {
                /* Rewrite to present and GE the rhs */
                char *theType = f->f_ava.ava_type;
                rhs_berval = f->f_ava.ava_value;

                replace_filter(f, "(&(numsubordinates=*)(numsubordinates>=x))");
                /* Now fixup the resulting filter so that x = rhs */
                slapi_ch_free((void **)&(f->f_and->f_next->f_ava.ava_value.bv_val));
                /*free type also */
                slapi_ch_free((void **)&theType);

                f->f_and->f_next->f_ava.ava_value = rhs_berval;
            }
            return 0;
        }
        break;

    case LDAP_FILTER_LE:
        find_our_friends(f->f_ava.ava_type, &has, &num);
        if (has) {
            return 1; /* Makes little sense for this attribute */
        }
        if (num) {
            /* One could imagine doing this one, but it's quite hard */
            return 1;
        }
        break;

    case LDAP_FILTER_APPROX:
        find_our_friends(f->f_ava.ava_type, &has, &num);
        if (has || num) {
            /* Not allowed */
            return 1;
        }
        break;

    case LDAP_FILTER_SUBSTRINGS:
        find_our_friends(f->f_sub_type, &has, &num);
        if (has || num) {
            /* Not allowed */
            return 1;
        }
        break;

    case LDAP_FILTER_PRESENT:
        find_our_friends(f->f_type, &has, &num);
        if (has || num) {
            /* we rewrite this search to (objectclass=*) */
            slapi_ch_free((void **)&(f->f_type));
            f->f_type = slapi_ch_strdup("objectclass");
            return 0;
        } /* We already weeded out the special search we use use in the console */
        break;
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        for (p = f->f_list; p != NULL; p = p->f_next) {
            grok_and_rewrite_filter(p);
        }
        break;

    default:
        return -1; /* Bad, might be an extended filter or something */
    }
    return -1;
}

static int
ldbm_compute_rewriter(Slapi_PBlock *pb)
{
    int rc = -1;
    char *fstr = NULL;

    /*
     * We need to look at the filter and see whether it might contain
     * numSubordinates or hasSubordinates. We want to do a quick check
     * before we look thoroughly.
     */
    slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &fstr);

    if (NULL != fstr) {
        if (PL_strcasestr(fstr, "subordinates")) {
            Slapi_Filter *f = NULL;
            /* Look for special filters we want to leave alone */
            if (0 == strcasecmp(fstr, "(&(numsubordinates=*)(numsubordinates>=1))")) {
                ; /* Do nothing, this one works OK */
            } else {
                /* So let's grok the filter in detail and try to rewrite it */
                slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &f);
                if (f) {
                    rc = grok_and_rewrite_filter(f);
                    if (0 == rc) {
                        /* he rewrote it ! fixup the string version */
                        /* slapi_pblock_set( pb, SLAPI_SEARCH_STRFILTER, newfstr ); */
                    }
                }
            }
        }
    }
    return rc;
}


int
ldbm_compute_init()
{
    int ret = 0;
    ret = slapi_compute_add_evaluator(ldbm_compute_evaluator);
    if (0 == ret) {
        ret = slapi_compute_add_search_rewriter(ldbm_compute_rewriter);
    }
    return ret;
}
