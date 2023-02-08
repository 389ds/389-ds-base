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

/* filterindex.c - generate the list of candidate entries from a filter */

#include "back-ldbm.h"

extern const char *indextype_PRESENCE;
extern const char *indextype_EQUALITY;
extern const char *indextype_APPROX;
extern const char *indextype_SUB;

static IDList *ava_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int ftype, Slapi_Filter *nextf, int range, int *err, int allidslimit);
static IDList *presence_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int *err, int allidslimit);
static IDList *extensible_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int *err, int allidslimit);
static IDList *list_candidates(Slapi_PBlock *pb, backend *be, const char *base, Slapi_Filter *flist, int ftype, int *err, int allidslimit);
static IDList *substring_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int *err, int allidslimit);
static IDList *range_candidates(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    struct berval *low_val,
    struct berval *high_val,
    int *err,
    const Slapi_Attr *sattr,
    int allidslimit);
static IDList *
keys2idl(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    const char *indextype,
    Slapi_Value **ivals,
    int *err,
    int *unindexed,
    back_txn *txn,
    int allidslimit);

IDList *
filter_candidates_ext(
    Slapi_PBlock *pb,
    backend *be,
    const char *base,
    Slapi_Filter *f,
    Slapi_Filter *nextf,
    int range,
    int *err,
    int allidslimit)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    IDList *result;
    int ftype;

    slapi_log_err(SLAPI_LOG_TRACE, "filter_candidates_ext", "=> \n");

    if (!allidslimit) {
        allidslimit = compute_allids_limit(pb, li);
    }

    if (li->li_use_vlv) {
        back_txn txn = {NULL};
        /* first, check to see if this particular filter node matches any
         * vlv indexes we're keeping.  if so, we can use that index
         * instead.
         */
        slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
        result = vlv_find_index_by_filter_txn(be, base, f, &txn);
        if (result) {
            slapi_log_err(SLAPI_LOG_TRACE, "filter_candidates_ext", "<= %lu (vlv)\n",
                          (u_long)IDL_NIDS(result));
            return result;
        }
    }

    result = NULL;
    switch ((ftype = slapi_filter_get_choice(f))) {
    case LDAP_FILTER_EQUALITY:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tEQUALITY\n");
        result = ava_candidates(pb, be, f, LDAP_FILTER_EQUALITY, nextf, range, err, allidslimit);
        break;

    case LDAP_FILTER_SUBSTRINGS:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tSUBSTRINGS\n");
        result = substring_candidates(pb, be, f, err, allidslimit);
        break;

    case LDAP_FILTER_GE:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tGE\n");
        result = ava_candidates(pb, be, f, LDAP_FILTER_GE, nextf, range,
                                err, allidslimit);
        break;

    case LDAP_FILTER_LE:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tLE\n");
        result = ava_candidates(pb, be, f, LDAP_FILTER_LE, nextf, range,
                                err, allidslimit);
        break;

    case LDAP_FILTER_PRESENT:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tPRESENT\n");
        result = presence_candidates(pb, be, f, err, allidslimit);
        break;

    case LDAP_FILTER_APPROX:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tAPPROX\n");
        result = ava_candidates(pb, be, f, LDAP_FILTER_APPROX, nextf,
                                range, err, allidslimit);
        break;

    case LDAP_FILTER_EXTENDED:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tEXTENSIBLE\n");
        result = extensible_candidates(pb, be, f, err, allidslimit);
        break;

    case LDAP_FILTER_AND:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tAND\n");
        result = list_candidates(pb, be, base, f, LDAP_FILTER_AND, err, allidslimit);
        break;

    case LDAP_FILTER_OR:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tOR\n");
        result = list_candidates(pb, be, base, f, LDAP_FILTER_OR, err, allidslimit);
        break;

    case LDAP_FILTER_NOT:
        slapi_log_err(SLAPI_LOG_FILTER, "filter_candidates_ext", "\tNOT\n");
        result = idl_allids(be);
        break;

    default:
        slapi_log_err(SLAPI_LOG_FILTER,
                      "filter_candidates_ext", "unknown type 0x%X\n",
                      ftype);
        break;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "filter_candidates_ext", "<= %lu\n",
                  (u_long)IDL_NIDS(result));
    return (result);
}

IDList *
filter_candidates(
    Slapi_PBlock *pb,
    backend *be,
    const char *base,
    Slapi_Filter *f,
    Slapi_Filter *nextf,
    int range,
    int *err)
{
    return filter_candidates_ext(pb, be, base, f, nextf, range, err, 0);
}

static IDList *
ava_candidates(
    Slapi_PBlock *pb,
    backend *be,
    Slapi_Filter *f,
    int ftype,
    Slapi_Filter *nextf __attribute__((unused)),
    int range __attribute__((unused)),
    int *err,
    int allidslimit)
{
    char *type, *indextype = NULL;
    Slapi_Value sv;
    struct berval *bval;
    Slapi_Value **ivals = NULL;
    IDList *idl = NULL;
    int unindexed = 0;
    Slapi_Attr sattr;
    back_txn txn = {NULL};
    int pr_idx = -1;
    Operation *pb_op;
    Connection *pb_conn;

    slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "=>\n");

    if (slapi_filter_get_ava(f, &type, &bval) != 0) {
        slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "slapi_filter_get_ava failed\n");
        return (NULL);
    }

    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_attr_init(&sattr, type);

    if (slapi_is_loglevel_set(SLAPI_LOG_FILTER)) {
        char *op = NULL;
        char buf[BUFSIZ];

        switch (ftype) {
        case LDAP_FILTER_GE:
            op = ">=";
            break;
        case LDAP_FILTER_LE:
            op = "<=";
            break;
        case LDAP_FILTER_EQUALITY:
            op = "=";
            break;
        case LDAP_FILTER_APPROX:
            op = "~=";
            break;
        }
        slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "   %s%s%s\n", type, op, encode(bval, buf));
    }

    switch (ftype) {
    case LDAP_FILTER_GE:
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
            /*
             * REMEMBER: this flag is only set on WARN levels. If the filter verify
             * is on strict, we reject in search.c, if we ar off, the flag will NOT
             * be set on the filter at all!
             */
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "WARNING - filter contains an INVALID attribute!\n");
            slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
        }
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "REJECTING invalid filter per policy!\n");
            idl = idl_alloc(0);
        } else {
            idl = range_candidates(pb, be, type, bval, NULL, err, &sattr, allidslimit);
        }
        slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "<= idl len %lu\n",
                      (u_long)IDL_NIDS(idl));
        goto done;
        break;
    case LDAP_FILTER_LE:
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
            /*
             * REMEMBER: this flag is only set on WARN levels. If the filter verify
             * is on strict, we reject in search.c, if we ar off, the flag will NOT
             * be set on the filter at all!
             */
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "WARNING - filter contains an INVALID attribute!\n");
            slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
        }
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "REJECTING invalid filter per policy!\n");
            idl = idl_alloc(0);
        } else {
            idl = range_candidates(pb, be, type, NULL, bval, err, &sattr, allidslimit);
        }
        slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "<= idl len %lu\n",
                      (u_long)IDL_NIDS(idl));
        goto done;
        break;
    case LDAP_FILTER_EQUALITY:
        indextype = (char *)indextype_EQUALITY;
        break;
    case LDAP_FILTER_APPROX:
        indextype = (char *)indextype_APPROX;
        break;
    default:
        slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "<= invalid filter\n");
        goto done;
        break;
    }

    /* This code is result of performance anlysis; we are trying to
     * optimize our equality filter processing -- mainly by limiting
     * malloc/free calls.
     *
     * When the filter type is LDAP_FILTER_EQUALITY_FAST, the
     * syntax_assertion2keys functions are passed a stack-based
     * destination Slapi_Value array (ivals) that contains room
     * for one key value with a fixed size buffer (also stack-based).
     * If the buffer provided is not large enough, the
     * syntax_assertion2keys function can alloc a new buffer (and
     * reset ivals[0]->bv.bv_val) or alloc an entirely new ivals array.
     */

    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
    if (ftype == LDAP_FILTER_EQUALITY) {
        Slapi_Value tmp, *ptr[2], fake;
        char buf[1024];

        tmp.bv = *bval;
        tmp.v_csnset = NULL;
        tmp.v_flags = 0;
        fake.bv.bv_val = buf;
        fake.bv.bv_len = sizeof(buf);
        ptr[0] = &fake;
        ptr[1] = NULL;
        ivals = ptr;

        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
            /*
             * REMEMBER: this flag is only set on WARN levels. If the filter verify
             * is on strict, we reject in search.c, if we ar off, the flag will NOT
             * be set on the filter at all!
             */
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "WARNING - filter contains an INVALID attribute!\n");
            slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
        }
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "REJECTING invalid filter per policy!\n");
            idl = idl_alloc(0);
        } else {
            slapi_attr_assertion2keys_ava_sv(&sattr, &tmp, (Slapi_Value ***)&ivals, LDAP_FILTER_EQUALITY_FAST);
            idl = keys2idl(pb, be, type, indextype, ivals, err, &unindexed, &txn, allidslimit);
        }

        if (unindexed) {
            slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);
            pagedresults_set_unindexed(pb_conn, pb_op, pr_idx);
        }

        /* We don't use valuearray_free here since the valueset, berval
         * and value was all allocated at once in one big chunk for
         * performance reasons
         */
        if (fake.bv.bv_val != buf) {
            slapi_ch_free((void **)&fake.bv.bv_val);
        }

        /* Some syntax_assertion2keys functions may allocate a whole new
         * ivals array. Free it if so.
         */
        if (ivals != ptr) {
            slapi_ch_free((void **)&ivals);
        }
    } else {
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
            /*
             * REMEMBER: this flag is only set on WARN levels. If the filter verify
             * is on strict, we reject in search.c, if we ar off, the flag will NOT
             * be set on the filter at all!
             */
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "WARNING - filter contains an INVALID attribute!\n");
            slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
        }
        if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
            slapi_log_err(SLAPI_LOG_FILTER, "ava_candidates", "REJECTING invalid filter per policy!\n");
            idl = idl_alloc(0);
        } else {
            slapi_value_init_berval(&sv, bval);
            ivals = NULL;
            slapi_attr_assertion2keys_ava_sv(&sattr, &sv, &ivals, ftype);
            value_done(&sv);
            if (ivals == NULL || *ivals == NULL) {
                slapi_log_err(SLAPI_LOG_TRACE, "ava_candidates",
                              "<= ALLIDS (no keys)\n");
                idl = idl_allids(be);
                goto done;
            }
            idl = keys2idl(pb, be, type, indextype, ivals, err, &unindexed, &txn, allidslimit);
        }

        if (unindexed) {
            slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);
            pagedresults_set_unindexed(pb_conn, pb_op, pr_idx);
        }
        valuearray_free(&ivals);
        slapi_log_err(SLAPI_LOG_TRACE, "ava_candidates", "<= %lu\n",
                      (u_long)IDL_NIDS(idl));
    }
done:
    attr_done(&sattr);
    return (idl);
}

static IDList *
presence_candidates(
    Slapi_PBlock *pb,
    backend *be,
    Slapi_Filter *f,
    int *err,
    int allidslimit)
{
    char *type;
    IDList *idl;
    int unindexed = 0;
    back_txn txn = {NULL};

    slapi_log_err(SLAPI_LOG_TRACE, "presence_candidates", "=> n");

    if (slapi_filter_get_type(f, &type) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "presence_candidates", "slapi_filter_get_type failed\n");
        return (NULL);
    }
    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);

    if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
        /*
         * REMEMBER: this flag is only set on WARN levels. If the filter verify
         * is on strict, we reject in search.c, if we ar off, the flag will NOT
         * be set on the filter at all!
         */
        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
    }
    if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
        idl = idl_alloc(0);
    } else {
        idl = index_read_ext_allids(pb, be, type, indextype_PRESENCE,
                                    NULL, &txn, err, &unindexed, allidslimit);
    }

    if (unindexed) {
        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);

        Operation *pb_op;
        Connection *pb_conn;

        int pr_idx = -1;
        slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
        slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

        slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
        pagedresults_set_unindexed(pb_conn, pb_op, pr_idx);
    }

    if (idl != NULL && ALLIDS(idl) && strcasecmp(type, "nscpentrydn") == 0) {
        /* try the equality index instead */
        slapi_log_err(SLAPI_LOG_TRACE,
                      "presence_candidates", "Fallback to eq index as pres index gave allids\n");
        idl_free(&idl);
        idl = index_range_read_ext(pb, be, type, indextype_EQUALITY,
                                   SLAPI_OP_GREATER_OR_EQUAL,
                                   NULL, NULL, 0, &txn, err, allidslimit);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "presence_candidates", "<= %lu\n",
                  (u_long)IDL_NIDS(idl));
    return (idl);
}

static IDList *
extensible_candidates(
    Slapi_PBlock *glob_pb,
    backend *be,
    Slapi_Filter *f,
    int *err,
    int allidslimit)
{
    IDList *idl = NULL;
    Slapi_PBlock *pb = slapi_pblock_new();
    int mrOP = 0;
    Slapi_Operation *op = NULL;
    back_txn txn = {NULL};
    slapi_log_err(SLAPI_LOG_TRACE, "extensible_candidates", "=> \n");
    slapi_pblock_get(glob_pb, SLAPI_TXN, &txn.back_txn_txn);
    if (!slapi_mr_filter_index(f, pb) && !slapi_pblock_get(pb, SLAPI_PLUGIN_MR_QUERY_OPERATOR, &mrOP)) {
        switch (mrOP) {
        case SLAPI_OP_LESS:
        case SLAPI_OP_LESS_OR_EQUAL:
        case SLAPI_OP_EQUAL:
        case SLAPI_OP_GREATER_OR_EQUAL:
        case SLAPI_OP_GREATER: {
            IFP mrINDEX = NULL;
            void *mrOBJECT = NULL;
            struct berval **mrVALUES = NULL;
            char *mrOID = NULL;
            char *mrTYPE = NULL;

            /* set the pb->pb_op to glob_pb->pb_op to catch the abandon req.
             * in case the operation is interrupted. */
            slapi_pblock_get(glob_pb, SLAPI_OPERATION, &op);
            /* coverity[var_deref_model] */
            slapi_pblock_set(pb, SLAPI_OPERATION, op);

            slapi_pblock_get(pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);
            slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &mrOBJECT);
            slapi_pblock_get(pb, SLAPI_PLUGIN_MR_VALUES, &mrVALUES);
            slapi_pblock_get(pb, SLAPI_PLUGIN_MR_OID, &mrOID);
            slapi_pblock_get(pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE);

            if (mrINDEX && mrVALUES && *mrVALUES && mrTYPE) {
                /*
                 * Compute keys for each of the values, individually.
                 * Search the index, for the computed keys.
                 * Collect the resulting IDs in idl.
                 */
                size_t n;
                struct berval **val;
                mrTYPE = slapi_attr_basetype(mrTYPE, NULL, 0);
                for (n = 0, val = mrVALUES; *val; ++n, ++val) {
                    struct berval **keys = NULL;
                    /* keys = mrINDEX (*val), conceptually.  In detail: */
                    struct berval *bvec[2];
                    bvec[0] = *val;
                    bvec[1] = NULL;

                    /* coverity[var_deref_model] */
                    if (slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, mrOBJECT) ||
                        slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, bvec) ||
                        mrINDEX(pb) ||
                        slapi_pblock_get(pb, SLAPI_PLUGIN_MR_KEYS, &keys)) {
                        /* something went wrong.  bail. */
                        break;
                    } else if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
                        /*
                         * REMEMBER: this flag is only set on WARN levels. If the filter verify
                         * is on strict, we reject in search.c, if we ar off, the flag will NOT
                         * be set on the filter at all!
                         */
                        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
                    }
                    if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
                        idl_free(&idl);
                        idl = idl_alloc(0);
                    } else if (keys == NULL || keys[0] == NULL) {
                        /* no keys */
                        idl_free(&idl);
                        idl = idl_allids(be);
                    } else {
                        IDList *idl2 = NULL;
                        struct berval **key;
                        for (key = keys; *key != NULL; ++key) {
                            int unindexed = 0;
                            IDList *idl3 = (mrOP == SLAPI_OP_EQUAL) ? index_read_ext_allids(pb, be, mrTYPE, mrOID, *key, &txn,
                                                                                            err, &unindexed, allidslimit)
                                                                    : index_range_read_ext(pb, be, mrTYPE, mrOID, mrOP,
                                                                                           *key, NULL, 0, &txn, err, allidslimit);
                            if (unindexed) {
                                int pr_idx = -1;
                                slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);

                                Operation *pb_op;
                                Connection *pb_conn;
                                slapi_pblock_get(glob_pb, SLAPI_OPERATION, &pb_op);
                                slapi_pblock_get(glob_pb, SLAPI_CONNECTION, &pb_conn);

                                slapi_pblock_get(glob_pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
                                pagedresults_set_unindexed(pb_conn, pb_op, pr_idx);
                            }
                            if (idl2 == NULL) {
                                /* first iteration */
                                idl2 = idl3;
                            } else {
                                IDList *tmp = idl_intersection(be, idl2, idl3);
                                idl_free(&idl2);
                                idl_free(&idl3);
                                idl2 = tmp;
                            }
                            if (idl2 == NULL)
                                break; /* look no further */
                        }
                        if (idl == NULL) {
                            idl = idl2;
                        } else if (idl2 != NULL) {
                            IDList *tmp = idl_union(be, idl, idl2);
                            idl_free(&idl);
                            idl_free(&idl2);
                            idl = tmp;
                        }
                    }
                }
                slapi_ch_free((void **)&mrTYPE);
                goto return_idl; /* possibly no matches */
            }
        } break;
        default:
            /* unsupported query operator */
            break;
        }
    }
    if (idl == NULL) {
        /* this filter isn't indexed */
        idl = idl_allids(be); /* all entries are candidates */
    }
return_idl:
    op = NULL;
    /* coverity[var_deref_model] */
    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    slapi_pblock_destroy(pb);
    slapi_log_err(SLAPI_LOG_TRACE, "extensible_candidates", "<= %lu\n",
                  (u_long)IDL_NIDS(idl));
    return idl;
}

static int
slapi_berval_reverse_cmp(const struct berval *a, const struct berval *b)
{
    return slapi_berval_cmp(b, a);
}

static IDList *
range_candidates(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    struct berval *low_val,
    struct berval *high_val,
    int *err,
    const Slapi_Attr *sattr,
    int allidslimit)
{
    IDList *idl = NULL;
    struct berval *low = NULL, *high = NULL;
    struct berval **lows = NULL, **highs = NULL;
    back_txn txn = {NULL};
    int operator= 0;
    Operation *op = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "range_candidates", "=> attr=%s\n", type);

    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);

    if (low_val != NULL) {
        slapi_attr_assertion2keys_ava(sattr, low_val, &lows, LDAP_FILTER_EQUALITY);
        if (lows == NULL || *lows == NULL) {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "range_candidates", "<= ALLIDS (no keys)\n");
            idl = idl_allids(be);
            goto done;
        }
        low = attr_value_lowest(lows, slapi_berval_reverse_cmp);
    }

    if (high_val != NULL) {
        slapi_attr_assertion2keys_ava(sattr, high_val, &highs, LDAP_FILTER_EQUALITY);
        if (highs == NULL || *highs == NULL) {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "range_candidates", "<= ALLIDS (no keys)\n");
            idl = idl_allids(be);
            goto done;
        }
        high = attr_value_lowest(highs, slapi_berval_cmp);
    }

    /* Check if it is for bulk import. */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (entryrdn_get_switch() && op && operation_is_flag_set(op, OP_FLAG_INTERNAL) &&
        operation_is_flag_set(op, OP_FLAG_BULK_IMPORT)) {
        /* parentid is treated specially that is needed for the bulk import. (See #48755) */
        operator= SLAPI_OP_RANGE_NO_IDL_SORT | SLAPI_OP_RANGE_NO_ALLIDS;
    }
    if (low == NULL) {
        operator|= SLAPI_OP_LESS_OR_EQUAL;
        idl = index_range_read_ext(pb, be, type, (char *)indextype_EQUALITY, operator,
                                   high, NULL, 0, &txn, err, allidslimit);
    } else if (high == NULL) {
        operator|= SLAPI_OP_GREATER_OR_EQUAL;
        idl = index_range_read_ext(pb, be, type, (char *)indextype_EQUALITY, operator,
                                   low, NULL, 0, &txn, err, allidslimit);
    } else {
        operator|= SLAPI_OP_LESS_OR_EQUAL;
        idl = index_range_read_ext(pb, be, type, (char *)indextype_EQUALITY, operator,
                                   low, high, 1, &txn, err, allidslimit);
    }

done:
    if (lows)
        ber_bvecfree(lows);
    if (highs)
        ber_bvecfree(highs);

    slapi_log_err(SLAPI_LOG_TRACE, "range_candidates", "<= %lu\n",
                  (u_long)IDL_NIDS(idl));

    return idl;
}

/*
 * If the filter type contains subtype, it returns 1; otherwise, returns 0.
 */
static int
filter_is_subtype(Slapi_Filter *f)
{
    char *p = NULL;
    size_t len = 0;
    int issubtype = 0;
    if (f) {
        p = strchr(f->f_type, ';');
        if (p) {
            len = p - f->f_type;
            if (len < strlen(f->f_type)) {
                issubtype = 1;
            }
        }
    }
    return issubtype;
}

static IDList *
list_candidates(
    Slapi_PBlock *pb,
    backend *be,
    const char *base,
    Slapi_Filter *flist,
    int ftype,
    int *err,
    int allidslimit)
{
    IDList *idl;
    IDList *tmp;
    Slapi_Filter *f, *nextf, *f_head;
    int range = 0;
    int isnot;
    int f_count = 0, le_count = 0, ge_count = 0, is_bounded_range = 1;
    struct berval *low_val = NULL, *high_val = NULL;
    char *t1;
    Slapi_Filter *fpairs[2] = {NULL, NULL}; /* low, high */
    char *tpairs[2] = {NULL, NULL};
    struct berval *vpairs[2] = {NULL, NULL};
    int is_and = 0;
    IDListSet *idl_set = NULL;
    back_search_result_set *sr = NULL;

    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);

    slapi_log_err(SLAPI_LOG_TRACE, "list_candidates", "=> 0x%x\n", ftype);

    /*
     * Optimize bounded range queries such as (&(cn>=A)(cn<=B)).
     * Could be better by matching pairs in a longer list
     * but for now support only a single pair.
     */
    if (ftype != LDAP_FILTER_AND) {
        is_bounded_range = 0;
    }
    for (f = slapi_filter_list_first(flist);
         f != NULL && is_bounded_range;
         f = slapi_filter_list_next(flist, f)) {
        f_count++;
        switch (slapi_filter_get_choice(f)) {
        case LDAP_FILTER_GE:
            if (slapi_filter_get_ava(f, &t1, &low_val) != 0) {
                is_bounded_range = 0;
                continue;
            }
            ge_count++;
            if (NULL == fpairs[0]) {
                fpairs[0] = f;
                tpairs[0] = slapi_ch_strdup(t1);
                vpairs[0] = slapi_ch_bvdup(low_val);
            } else if (NULL != fpairs[1] &&
                       slapi_attr_type_cmp(tpairs[1], t1, SLAPI_TYPE_CMP_EXACT) != 0) {
                fpairs[0] = f;
                slapi_ch_free_string(&tpairs[0]);
                tpairs[0] = slapi_ch_strdup(t1);
                slapi_ch_bvfree(&vpairs[0]);
                vpairs[0] = slapi_ch_bvdup(low_val);
            }
            break;
        case LDAP_FILTER_LE:
            if (slapi_filter_get_ava(f, &t1, &high_val) != 0) {
                is_bounded_range = 0;
                continue;
            }
            le_count++;
            if (NULL == fpairs[1]) {
                fpairs[1] = f;
                tpairs[1] = slapi_ch_strdup(t1);
                vpairs[1] = slapi_ch_bvdup(high_val);
            } else if (NULL != fpairs[0] &&
                       slapi_attr_type_cmp(tpairs[0], t1, SLAPI_TYPE_CMP_EXACT) != 0) {
                fpairs[1] = f;
                slapi_ch_free_string(&tpairs[1]);
                tpairs[1] = slapi_ch_strdup(t1);
                slapi_ch_bvfree(&vpairs[1]);
                vpairs[1] = slapi_ch_bvdup(high_val);
            }
            break;
        default:
            continue;
        }
    }
    if (ftype == LDAP_FILTER_AND && f_count > 1) {
        slapi_pblock_get(pb, SLAPI_SEARCH_IS_AND, &is_and);
        if (is_and) {
            /* Outer candidates function already set IS_AND.
             * So, this function does not touch it. */
            is_and = 0;
        } else {
            /* Outer candidates function hasn't set IS_AND */
            is_and = 1;
            slapi_pblock_set(pb, SLAPI_SEARCH_IS_AND, &is_and);
        }
    }
    if (le_count != 1 || ge_count != 1 || f_count != 2) {
        is_bounded_range = 0;
    }
    if (NULL == fpairs[0] || NULL == fpairs[1] ||
        0 != strcmp(tpairs[0], tpairs[1]) /* avoid "&(cn<=A)(sn>=B)" type */) {
        fpairs[0] = fpairs[1] = NULL;
        slapi_ch_free_string(&tpairs[0]);
        slapi_ch_bvfree(&vpairs[0]);
        slapi_ch_free_string(&tpairs[1]);
        slapi_ch_bvfree(&vpairs[1]);
        is_bounded_range = 0;
    }
    if (is_bounded_range) {
        Slapi_Attr sattr;

        slapi_attr_init(&sattr, tpairs[0]);
        idl = range_candidates(pb, be, tpairs[0], vpairs[0], vpairs[1], err, &sattr, allidslimit);
        attr_done(&sattr);
        slapi_log_err(SLAPI_LOG_TRACE, "list_candidates", "<= %lu\n",
                      (u_long)IDL_NIDS(idl));
        goto out;
    }

    if (ftype == LDAP_FILTER_OR || ftype == LDAP_FILTER_AND) {
        idl_set = idl_set_create();
    }

    idl = NULL;
    nextf = NULL;
    for (f_head = f = slapi_filter_list_first(flist); f != NULL;
         f = slapi_filter_list_next(flist, f)) {

        /* Look for NOT foo type filter elements where foo is simple equality */
        isnot = (LDAP_FILTER_NOT == slapi_filter_get_choice(f)) &&
                (LDAP_FILTER_AND == ftype &&
                 (LDAP_FILTER_EQUALITY == slapi_filter_get_choice(slapi_filter_list_first(f))));

        if (isnot) {
            /*
             * If this is the first filter, make sure we have something to
             * subtract from.
             */
            if (f == f_head) {
                idl = idl_allids(be);
                idl_set_insert_idl(idl_set, idl);
            }
            /*
             * Not the first filter - good! Get the indexed type out.
             * if this is unindexed, we'll get allids. During the intersection
             * in notin, we'll mark this as don't skip filter, because else we might
             * get the wrong results.
             */

            slapi_log_err(SLAPI_LOG_TRACE, "list_candidates", "NOT filter\n");
            if (filter_is_subtype(slapi_filter_list_first(f))) {
                /*
                 * If subtype is included in the filter (e.g., !(cn;fr=<CN>)),
                 * we have to give up using the index since the subtype info
                 * is not in the index.
                 */
                tmp = idl_allids(be);
            } else {
                tmp = ava_candidates(pb, be, slapi_filter_list_first(f),
                                     LDAP_FILTER_EQUALITY, nextf, range, err, allidslimit);
            }
        } else {
            if (fpairs[0] == f) {
                continue;
            } else if (fpairs[1] == f) {
                Slapi_Attr sattr;

                slapi_attr_init(&sattr, tpairs[0]);
                tmp = range_candidates(pb, be, tpairs[0],
                                       vpairs[0], vpairs[1], err, &sattr, allidslimit);
                attr_done(&sattr);
                if (tmp == NULL && ftype == LDAP_FILTER_AND) {
                    slapi_log_err(SLAPI_LOG_TRACE, "list_candidates",
                                  "<= NULL\n");
                    idl_free(&idl);
                    idl = NULL;
                    goto out;
                }
            }
            /* Proceed as normal */
            else if ((tmp = filter_candidates_ext(pb, be, base, f, nextf, range, err, allidslimit)) == NULL && ftype == LDAP_FILTER_AND) {
                slapi_log_err(SLAPI_LOG_TRACE, "list_candidates",
                              "<=  NULL 2\n");
                idl_free(&idl);
                idl = NULL;
                goto out;
            }
        }

        /*
         * The IDL for that component is NULL, so no candidate retrieved from that component. This is all normal
         * Just build a idl with an empty set
         */
        if (tmp == NULL) {
            tmp = idl_alloc(0);
        }

        /*
         * At this point we have the idl set from the subfilter. In idl_set,
         * we stash this for later ....
         */

        if (ftype == LDAP_FILTER_OR ||
            (ftype == LDAP_FILTER_AND && !isnot)) {
            idl_set_insert_idl(idl_set, tmp);
        } else if (ftype == LDAP_FILTER_AND && isnot) {
            idl_set_insert_complement_idl(idl_set, tmp);
        }

        if (ftype == LDAP_FILTER_OR && idl_set_union_shortcut(idl_set) != 0) {
            /*
             * If we encounter an allids idl, this means that union will return
             * and allids - we should not process anymore, and fallback to full
             * table scan at this point.
             */
                slapi_log_err(SLAPI_LOG_TRACE, "list_candidates", "OR shortcut condition - must apply filter test\n");
            sr->sr_flags |= SR_FLAG_MUST_APPLY_FILTER_TEST;
            goto apply_set_op;
        }

        if (ftype == LDAP_FILTER_AND && idl_set_intersection_shortcut(idl_set) != 0) {
            /*
             * If we encounter a zero length idl, we bail now because this can never
             * result in a meaningful result besides zero.
             */
            slapi_log_err(SLAPI_LOG_TRACE, "list_candidates", "AND shortcut condition - must apply filter test\n");
            sr->sr_flags |= SR_FLAG_MUST_APPLY_FILTER_TEST;
            goto apply_set_op;
        }
    }

    /*
     * Do the idl_set operation if required.
     * these are far more efficient than the iterative union and
     * intersections we previously used.
     */
apply_set_op:

    if (ftype == LDAP_FILTER_OR) {
        /* If one of the idl_set is allids, this shortcuts :) */
        idl = idl_set_union(idl_set, be);
        size_t nids = IDL_NIDS(idl);
        if (allidslimit > 0 && nids > allidslimit) {
            Slapi_Operation *operation;
            slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
            /* PAGED RESULTS: we strictly limit the idlist size by the allids (aka idlistscan) limit. */
            if (op_is_pagedresults(operation)) {
                idl_free(&idl);
                idl = idl_allids(be);
            }
        }
    } else if (ftype == LDAP_FILTER_AND) {
        idl = idl_set_intersect(idl_set, be);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "list_candidates", "<= idl len %lu\n", (u_long)IDL_NIDS(idl));
out:
    idl_set_destroy(idl_set);
    if (is_and) {
        /*
         * Sets IS_AND back to 0 only when this function set 1.
         * The info of the outer (&...) needs to be passed to the
         * descendent *_candidates functions called recursively.
         */
        is_and = 0;
        slapi_pblock_set(pb, SLAPI_SEARCH_IS_AND, &is_and);
    }
    slapi_ch_free_string(&tpairs[0]);
    slapi_ch_bvfree(&vpairs[0]);
    slapi_ch_free_string(&tpairs[1]);
    slapi_ch_bvfree(&vpairs[1]);
    return (idl);
}

static IDList *
substring_candidates(
    Slapi_PBlock *pb,
    backend *be,
    Slapi_Filter *f,
    int *err,
    int allidslimit)
{
    char *type, *initial, * final;
    char **any;
    IDList *idl;
    Slapi_Value **ivals;
    int unindexed = 0;
    Slapi_Attr sattr;
    back_txn txn = {NULL};
    int pr_idx = -1;
    struct attrinfo *ai = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "substring_candidates", "=>\n");

    if (slapi_filter_get_subfilt(f, &type, &initial, &any, & final) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "substring_candidates", "slapi_filter_get_subfilt fails\n");
        return (NULL);
    }

    /*
     * get the index keys corresponding to the substring
     * assertion values
     */
    slapi_attr_init(&sattr, type);
    ainfo_get(be, type, &ai);
    slapi_pblock_set(pb, SLAPI_SYNTAX_SUBSTRLENS, ai->ai_substr_lens);
    slapi_attr_assertion2keys_sub_sv_pb(pb, &sattr, initial, any, final, &ivals);
    attr_done(&sattr);
    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
    if (ivals == NULL || *ivals == NULL) {
        Operation *pb_op;
        Connection *pb_conn;
        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);
        slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
        slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
        pagedresults_set_unindexed(pb_conn, pb_op, pr_idx);
        slapi_log_err(SLAPI_LOG_TRACE, "substring_candidates",
                      "<= ALLIDS (no keys)\n");
        return (idl_allids(be));
    }

    /*
     * look up each key in the index, ANDing the resulting
     * IDLists together.
     */
    if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_WARN) {
        /*
         * REMEMBER: this flag is only set on WARN levels. If the filter verify
         * is on strict, we reject in search.c, if we ar off, the flag will NOT
         * be set on the filter at all!
         */
        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_FILTER_INVALID);
    }
    if (f->f_flags & SLAPI_FILTER_INVALID_ATTR_UNDEFINE) {
        idl = idl_alloc(0);
    } else {
        slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
        idl = keys2idl(pb, be, type, indextype_SUB, ivals, err, &unindexed, &txn, allidslimit);
    }
    if (unindexed) {
        Operation *pb_op;
        Connection *pb_conn;
        slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
        slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);
        pagedresults_set_unindexed(pb_conn, pb_op, pr_idx);
    }
    valuearray_free(&ivals);

    slapi_log_err(SLAPI_LOG_TRACE, "substring_candidates", "<= %lu\n",
                  (u_long)IDL_NIDS(idl));
    return (idl);
}

static IDList *
keys2idl(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    const char *indextype,
    Slapi_Value **ivals,
    int *err,
    int *unindexed,
    back_txn *txn,
    int allidslimit)
{
    IDList *idl = NULL;
    Op_stat *op_stat = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "keys2idl", "=> type %s indextype %s\n", type, indextype);

    /* Before reading the index take the start time */
    if (LDAP_STAT_READ_INDEX & config_get_statlog_level()) {
        op_stat = op_stat_get_operation_extension(pb);
        if (op_stat->search_stat) {
            clock_gettime(CLOCK_MONOTONIC, &(op_stat->search_stat->keys_lookup_start));
        } else {
            op_stat = NULL;
        }
    }

    for (uint32_t i = 0; ivals[i] != NULL; i++) {
        IDList *idl2 = NULL;
        struct component_keys_lookup *key_stat;
        int key_len;

        idl2 = index_read_ext_allids(pb, be, type, indextype, slapi_value_get_berval(ivals[i]), txn, err, unindexed, allidslimit);
        if (op_stat) {
            /* gather the index lookup statistics */
            key_stat = (struct component_keys_lookup *) slapi_ch_calloc(1, sizeof (struct component_keys_lookup));

            /* indextype e.g. "eq" or "sub" (see index.c) */
            if (indextype) {
                key_stat->index_type = slapi_ch_strdup(indextype);
            }
            /* key value e.g. '^st' or 'smith'*/
            key_len = slapi_value_get_length(ivals[i]);
            if (key_len) {
                key_stat->key = (char *) slapi_ch_calloc(1, key_len + 1);
                memcpy(key_stat->key, slapi_value_get_string(ivals[i]), key_len);
            }

            /* attribute name e.g. 'uid' */
            if (type) {
                key_stat->attribute_type = slapi_ch_strdup(type);
            }

            /* Number of lookup IDs with the key */
            key_stat->id_lookup_cnt = idl2 ? idl2->b_nids : 0;
            if (op_stat->search_stat->keys_lookup) {
                /* it already exist key stat. add key_stat at the head */
                key_stat->next = op_stat->search_stat->keys_lookup;
            } else {
                /* this is the first key stat record */
                key_stat->next = NULL;
            }
            op_stat->search_stat->keys_lookup = key_stat;
        }
#ifdef LDAP_ERROR_LOGGING
        /* XXX if ( slapd_ldap_debug & LDAP_DEBUG_TRACE ) { XXX */
        {
            char buf[BUFSIZ];

            slapi_log_err(SLAPI_LOG_TRACE, "keys2idl",
                          "   ival[%" PRIu32 "] = \"%s\" => %" PRIu32 " IDs\n", i,
                          encode(slapi_value_get_berval(ivals[i]), buf), (uint32_t)IDL_NIDS(idl2));
        }
#endif
        if (idl2 == NULL) {
            slapi_log_err(SLAPI_LOG_WARNING, "keys2idl", "received NULL idl from index_read_ext_allids, treating as empty set\n");
            slapi_log_err(SLAPI_LOG_WARNING, "keys2idl", "this is probably a bug that should be reported\n");
            idl2 = idl_alloc(0);
        }

        /* First iteration of the ivals, stash idl2. */
        if (idl == NULL) {
            idl = idl2;
        } else {
            /*
             * second iteration of the ivals - do an intersection and free
             * the intermediates.
             */
            IDList *tmp = NULL;

            tmp = idl;
            idl = idl_intersection(be, idl, idl2);
            idl_free(&idl2);
            idl_free(&tmp);
        }
    }

    /* All the keys have been fetch, time to take the completion time */
    if (op_stat) {
        clock_gettime(CLOCK_MONOTONIC, &(op_stat->search_stat->keys_lookup_end));
    }

    return (idl);
}
