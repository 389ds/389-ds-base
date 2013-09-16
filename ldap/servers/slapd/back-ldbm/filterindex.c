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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* filterindex.c - generate the list of candidate entries from a filter */

#include "back-ldbm.h"
#include "../index_subsys.h"

extern const char *indextype_PRESENCE;
extern const char *indextype_EQUALITY;
extern const char *indextype_APPROX;
extern const char *indextype_SUB;

static IDList    *ava_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int ftype, Slapi_Filter *nextf, int range, int *err, int allidslimit);
static IDList    *presence_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int *err, int allidslimit);
static IDList    *extensible_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int *err, int allidslimit);
static IDList    *list_candidates(Slapi_PBlock *pb, backend *be, const char *base, Slapi_Filter *flist, int ftype, int *err, int allidslimit);
static IDList    *substring_candidates(Slapi_PBlock *pb, backend *be, Slapi_Filter *f, int *err, int allidslimit);
static IDList * range_candidates(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    struct berval *low_val,
    struct berval *high_val,
    int *err,
    const Slapi_Attr *sattr,
    int allidslimit
);
static IDList *
keys2idl(
    Slapi_PBlock *pb,
    backend     *be,
    char        *type,
    const char  *indextype,
    Slapi_Value **ivals,
    int         *err,
    int         *unindexed,
    back_txn    *txn,
    int allidslimit
);

IDList *
filter_candidates_ext(
    Slapi_PBlock *pb,
    backend      *be,
    const char   *base,
    Slapi_Filter *f,
    Slapi_Filter *nextf,
    int          range,
    int          *err,
    int          allidslimit
)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    IDList          *result;
    int             ftype;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> filter_candidates\n", 0, 0, 0 );

    if (!allidslimit) {
        allidslimit = compute_allids_limit(pb, li);
    }

    /* check if this is to be serviced by a virtual index */
    if(INDEX_FILTER_EVALUTED == index_subsys_evaluate_filter(f, (Slapi_DN*)slapi_be_getsuffix(be, 0), (IndexEntryList**)&result))
    {
        LDAPDebug( LDAP_DEBUG_TRACE, "<= filter_candidates %lu (vattr)\n",
               (u_long)IDL_NIDS(result), 0, 0 );
        return result;
    }

    if (li->li_use_vlv) {
        back_txn      txn = {NULL};
        /* first, check to see if this particular filter node matches any
         * vlv indexes we're keeping.  if so, we can use that index
         * instead.
         */
        slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
        result = vlv_find_index_by_filter_txn(be, base, f, &txn);
        if (result) {
            LDAPDebug( LDAP_DEBUG_TRACE, "<= filter_candidates %lu (vlv)\n",
                    (u_long)IDL_NIDS(result), 0, 0 );
            return result;
        }
    }

    result = NULL;
    switch ( (ftype = slapi_filter_get_choice( f )) ) {
    case LDAP_FILTER_EQUALITY:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tEQUALITY\n", 0, 0, 0 );
        result = ava_candidates( pb, be, f, LDAP_FILTER_EQUALITY, nextf, range, err, allidslimit );
        break;

    case LDAP_FILTER_SUBSTRINGS:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tSUBSTRINGS\n", 0, 0, 0 );
        result = substring_candidates( pb, be, f, err, allidslimit );
        break;

    case LDAP_FILTER_GE:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tGE\n", 0, 0, 0 );
        result = ava_candidates( pb, be, f, LDAP_FILTER_GE, nextf, range,
            err, allidslimit );
        break;

    case LDAP_FILTER_LE:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tLE\n", 0, 0, 0 );
        result = ava_candidates( pb, be, f, LDAP_FILTER_LE, nextf, range,
            err, allidslimit );
        break;

    case LDAP_FILTER_PRESENT:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tPRESENT\n", 0, 0, 0 );
        result = presence_candidates( pb, be, f, err, allidslimit );
        break;

    case LDAP_FILTER_APPROX:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tAPPROX\n", 0, 0, 0 );
        result = ava_candidates( pb, be, f, LDAP_FILTER_APPROX, nextf,
            range, err, allidslimit );
        break;

    case LDAP_FILTER_EXTENDED:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tEXTENSIBLE\n", 0, 0, 0 );
        result = extensible_candidates( pb, be, f, err, allidslimit );
        break;

    case LDAP_FILTER_AND:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tAND\n", 0, 0, 0 );
        result = list_candidates( pb, be, base, f, LDAP_FILTER_AND, err, allidslimit );
        break;

    case LDAP_FILTER_OR:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tOR\n", 0, 0, 0 );
        result = list_candidates( pb, be, base, f, LDAP_FILTER_OR, err, allidslimit );
        break;

    case LDAP_FILTER_NOT:
        LDAPDebug( LDAP_DEBUG_FILTER, "\tNOT\n", 0, 0, 0 );
        result = idl_allids( be );
        break;

    default:
        LDAPDebug( LDAP_DEBUG_FILTER,
            "filter_candidates: unknown type 0x%X\n",
            ftype, 0, 0 );
        break;
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= filter_candidates %lu\n",
                   (u_long)IDL_NIDS(result), 0, 0 );
    return( result );
}

IDList *
filter_candidates(
    Slapi_PBlock *pb,
    backend      *be,
    const char   *base,
    Slapi_Filter *f,
    Slapi_Filter *nextf,
    int          range,
    int          *err
)
{
    return filter_candidates_ext(pb, be, base, f, nextf, range, err, 0);
}

static IDList *
ava_candidates(
    Slapi_PBlock *pb,
    backend      *be,
    Slapi_Filter *f,
    int             ftype,
    Slapi_Filter *nextf,
    int             range,
    int             *err,
    int             allidslimit
)
{
    char          *type, *indextype = NULL;
    Slapi_Value   sv;
    struct berval *bval;
    Slapi_Value   **ivals;
    IDList        *idl = NULL;
    int           unindexed = 0;
    Slapi_Attr    sattr;
    back_txn      txn = {NULL};
    int           pr_idx = -1;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> ava_candidates\n", 0, 0, 0 );

    if ( slapi_filter_get_ava( f, &type, &bval ) != 0 ) {
        LDAPDebug( LDAP_DEBUG_TRACE, "  slapi_filter_get_ava failed\n",
            0, 0, 0 );
        return( NULL );
    }

    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
    slapi_attr_init(&sattr, type);

#ifdef LDAP_DEBUG
    if ( LDAPDebugLevelIsSet( LDAP_DEBUG_TRACE )) {
        char    *op = NULL;
        char    buf[BUFSIZ];

        switch ( ftype ) {
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
        LDAPDebug( LDAP_DEBUG_TRACE, "   %s%s%s\n", type, op,
            encode( bval, buf ) );
    }
#endif

    switch ( ftype ) {
        case LDAP_FILTER_GE:
            idl = range_candidates(pb, be, type, bval, NULL, err, &sattr, allidslimit);
            LDAPDebug( LDAP_DEBUG_TRACE, "<= ava_candidates %lu\n",
                       (u_long)IDL_NIDS(idl), 0, 0 );
            goto done;
            break;
        case LDAP_FILTER_LE:
            idl = range_candidates(pb, be, type, NULL, bval, err, &sattr, allidslimit);
            LDAPDebug( LDAP_DEBUG_TRACE, "<= ava_candidates %lu\n",
                       (u_long)IDL_NIDS(idl), 0, 0 );
            goto done;
            break;
        case LDAP_FILTER_EQUALITY:
            indextype = (char*)indextype_EQUALITY;
            break;
        case LDAP_FILTER_APPROX:
            indextype = (char*)indextype_APPROX;
            break;
        default:
            LDAPDebug( LDAP_DEBUG_TRACE, "<= ava_candidates invalid filter\n", 0, 0, 0 );
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
    if(ftype==LDAP_FILTER_EQUALITY) {
        Slapi_Value tmp, *ptr[2], fake;
        char buf[1024];

        tmp.bv = *bval;
        tmp.v_csnset=NULL;
        tmp.v_flags = 0;
        fake.bv.bv_val=buf;
        fake.bv.bv_len=sizeof(buf);
        ptr[0]=&fake;
        ptr[1]=NULL;
        ivals=ptr;

        slapi_attr_assertion2keys_ava_sv( &sattr, &tmp, (Slapi_Value ***)&ivals, LDAP_FILTER_EQUALITY_FAST);
        idl = keys2idl( pb, be, type, indextype, ivals, err, &unindexed, &txn, allidslimit );
        if ( unindexed ) {
            unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
            slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
            pagedresults_set_unindexed( pb->pb_conn, pb->pb_op, pr_idx );
        }

        /* We don't use valuearray_free here since the valueset, berval
         * and value was all allocated at once in one big chunk for
         * performance reasons
         */
        if (fake.bv.bv_val != buf) {
            slapi_ch_free((void**)&fake.bv.bv_val);
        }

        /* Some syntax_assertion2keys functions may allocate a whole new
         * ivals array. Free it if so.
         */
        if (ivals != ptr) {
            slapi_ch_free((void**)&ivals);
        }
    } else {
        slapi_value_init_berval(&sv, bval);
        ivals=NULL;
        slapi_attr_assertion2keys_ava_sv( &sattr, &sv, &ivals, ftype );
        value_done(&sv);
        if ( ivals == NULL || *ivals == NULL ) {
            LDAPDebug( LDAP_DEBUG_TRACE,
                "<= ava_candidates ALLIDS (no keys)\n", 0, 0, 0 );
            idl = idl_allids( be );
            goto done;
        }
        idl = keys2idl( pb, be, type, indextype, ivals, err, &unindexed, &txn, allidslimit );
        if ( unindexed ) {
            unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
            slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
            pagedresults_set_unindexed( pb->pb_conn, pb->pb_op, pr_idx );
        }
         valuearray_free( &ivals );
         LDAPDebug( LDAP_DEBUG_TRACE, "<= ava_candidates %lu\n",
                   (u_long)IDL_NIDS(idl), 0, 0 );
    }
done:
    attr_done(&sattr);
    return( idl );
}

static IDList *
presence_candidates(
    Slapi_PBlock    *pb,
    backend *be,
    Slapi_Filter    *f,
    int            *err,
    int            allidslimit
)
{
    char    *type;
    IDList  *idl;
    int     unindexed = 0;
    back_txn      txn = {NULL};

    LDAPDebug( LDAP_DEBUG_TRACE, "=> presence_candidates\n", 0, 0, 0 );

    if ( slapi_filter_get_type( f, &type ) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY, "   slapi_filter_get_type failed\n",
            0, 0, 0 );
        return( NULL );
    }
    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
    idl = index_read_ext_allids( pb, be, type, indextype_PRESENCE,
                                 NULL, &txn, err, &unindexed, allidslimit );

    if ( unindexed ) {
        int pr_idx = -1;
        unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
        slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
        slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
        pagedresults_set_unindexed(pb->pb_conn, pb->pb_op, pr_idx);
    }

    if (idl != NULL && ALLIDS(idl) && strcasecmp(type, "nscpentrydn") == 0) {
        /* try the equality index instead */
        LDAPDebug(LDAP_DEBUG_TRACE, 
                      "fallback to eq index as pres index gave allids\n", 
                      0, 0, 0);
        idl_free(idl);
        idl = index_range_read_ext(pb, be, type, indextype_EQUALITY,
                                   SLAPI_OP_GREATER_OR_EQUAL,
                                   NULL, NULL, 0, &txn, err, allidslimit);
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= presence_candidates %lu\n",
                   (u_long)IDL_NIDS(idl), 0, 0 );
    return( idl );
}

static IDList *
extensible_candidates(
    Slapi_PBlock *glob_pb, 
    backend       *be,
    Slapi_Filter  *f,
    int           *err,
    int           allidslimit
)
{
    IDList* idl = NULL;
    Slapi_PBlock* pb = slapi_pblock_new();
    int mrOP = 0;
    Slapi_Operation *op = NULL;
    back_txn txn = {NULL};
    LDAPDebug (LDAP_DEBUG_TRACE, "=> extensible_candidates\n", 0, 0, 0);
    slapi_pblock_get(glob_pb, SLAPI_TXN, &txn.back_txn_txn);
    if ( ! slapi_mr_filter_index (f, pb) &&    !slapi_pblock_get (pb, SLAPI_PLUGIN_MR_QUERY_OPERATOR, &mrOP))
    {
        switch (mrOP)
        {
        case SLAPI_OP_LESS:
        case SLAPI_OP_LESS_OR_EQUAL:
        case SLAPI_OP_EQUAL:
        case SLAPI_OP_GREATER_OR_EQUAL:
        case SLAPI_OP_GREATER:
            {
            IFP mrINDEX = NULL;
            void* mrOBJECT = NULL;
            struct berval** mrVALUES = NULL;
            char* mrOID = NULL;
            char* mrTYPE = NULL;

            /* set the pb->pb_op to glob_pb->pb_op to catch the abandon req.
             * in case the operation is interrupted. */
            slapi_pblock_get (glob_pb, SLAPI_OPERATION, &op);
            slapi_pblock_set (pb, SLAPI_OPERATION, op);

            slapi_pblock_get (pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);
            slapi_pblock_get (pb, SLAPI_PLUGIN_OBJECT, &mrOBJECT);
            slapi_pblock_get (pb, SLAPI_PLUGIN_MR_VALUES, &mrVALUES);
            slapi_pblock_get (pb, SLAPI_PLUGIN_MR_OID, &mrOID);
            slapi_pblock_get (pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE);

            if (mrVALUES != NULL && *mrVALUES != NULL)
            {
                /*
                 * Compute keys for each of the values, individually.
                 * Search the index, for the computed keys.
                 * Collect the resulting IDs in idl.
                 */
                size_t n;
                struct berval** val;
                mrTYPE = slapi_attr_basetype (mrTYPE, NULL, 0);
                for (n=0,val=mrVALUES; *val; ++n,++val)
                {
                    struct berval** keys = NULL;
                    /* keys = mrINDEX (*val), conceptually.  In detail: */
                    struct berval* bvec[2];
                    bvec[0] = *val;
                    bvec[1] = NULL;
                    if (slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, mrOBJECT) ||
                        slapi_pblock_set (pb, SLAPI_PLUGIN_MR_VALUES, bvec) ||
                        mrINDEX (pb) ||
                        slapi_pblock_get (pb, SLAPI_PLUGIN_MR_KEYS, &keys))
                    {
                        /* something went wrong.  bail. */
                        break;
                    }
                    else if (keys == NULL || keys[0] == NULL)
                    {
                        /* no keys */
                        idl_free (idl);
                        idl = idl_allids (be);
                    }
                    else
                    {
                        IDList* idl2= NULL;
                        struct berval** key;
                        for (key = keys; *key != NULL; ++key)
                        {
                            int unindexed = 0;
                            IDList* idl3 = (mrOP == SLAPI_OP_EQUAL) ?
                                index_read_ext_allids(pb, be, mrTYPE, mrOID, *key, &txn,
                                                      err, &unindexed, allidslimit) :
                                index_range_read_ext(pb, be, mrTYPE, mrOID, mrOP,
                                                     *key, NULL, 0, &txn, err, allidslimit);
                            if ( unindexed ) {
                                int pr_idx = -1;
                                unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
                                slapi_pblock_set( glob_pb,
                                            SLAPI_OPERATION_NOTES, &opnote );
                                slapi_pblock_get( glob_pb,
                                                  SLAPI_PAGED_RESULTS_INDEX,
                                                  &pr_idx );
                                pagedresults_set_unindexed( glob_pb->pb_conn,
                                                            glob_pb->pb_op,
                                                            pr_idx );
                            }
                            if (idl2 == NULL)
                            {
                                /* first iteration */
                                idl2 = idl3;
                            }
                            else
                            {
                                IDList* tmp = idl_intersection (be, idl2, idl3);
                                idl_free (idl2);
                                idl_free (idl3);
                                idl2 = tmp;
                            }
                            if (idl2 == NULL) break; /* look no further */
                        }
                        if (idl == NULL)
                        {
                            idl = idl2;
                        }
                        else if (idl2 != NULL)
                        {
                            IDList* tmp = idl_union (be, idl, idl2);
                            idl_free (idl);
                            idl_free (idl2);
                            idl = tmp;
                        }
                    }
                }
                slapi_ch_free((void**)&mrTYPE);
                goto return_idl; /* possibly no matches */
            }
            }
            break;
        default:
            /* unsupported query operator */
            break;
        }
    }
    if (idl == NULL)
    {
        /* this filter isn't indexed */
        idl = idl_allids (be); /* all entries are candidates */
    }
return_idl:
    op = NULL;
    slapi_pblock_set (pb, SLAPI_OPERATION, op);
    slapi_pblock_destroy (pb);
    LDAPDebug (LDAP_DEBUG_TRACE, "<= extensible_candidates %lu\n", 
                   (u_long)IDL_NIDS(idl), 0, 0);
    return idl;
}

static int
slapi_berval_reverse_cmp(const struct berval *a, const struct berval *b)
{
    return slapi_berval_cmp(b, a);
}

static IDList *
range_candidates(
    Slapi_PBlock    *pb,
    backend *be,
    char *type,
    struct berval *low_val,
    struct berval *high_val,
    int *err,
    const Slapi_Attr *sattr,
    int allidslimit
)
{
    IDList *idl = NULL;
    struct berval *low = NULL, *high = NULL;
    struct berval **lows = NULL, **highs = NULL;
    back_txn txn = {NULL};

    LDAPDebug(LDAP_DEBUG_TRACE, "=> range_candidates attr=%s\n", type, 0, 0);

    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);

    if (low_val != NULL) {
        slapi_attr_assertion2keys_ava(sattr, low_val, &lows, LDAP_FILTER_EQUALITY);
        if (lows == NULL || *lows == NULL) {
            LDAPDebug( LDAP_DEBUG_TRACE,
                       "<= range_candidates ALLIDS (no keys)\n", 0, 0, 0 );
            idl = idl_allids( be );
            goto done;
        }
        low = attr_value_lowest(lows, slapi_berval_reverse_cmp);
    }

    if (high_val != NULL) {
        slapi_attr_assertion2keys_ava(sattr, high_val, &highs, LDAP_FILTER_EQUALITY);
        if (highs == NULL || *highs == NULL) {
            LDAPDebug( LDAP_DEBUG_TRACE,
                       "<= range_candidates ALLIDS (no keys)\n", 0, 0, 0 );
            idl = idl_allids( be );
            goto done;
        }
        high = attr_value_lowest(highs, slapi_berval_cmp);
    }

    if (low == NULL) {
        idl = index_range_read_ext(pb, be, type, (char*)indextype_EQUALITY,
                                   SLAPI_OP_LESS_OR_EQUAL,
                                   high, NULL, 0, &txn, err, allidslimit);
    } else if (high == NULL) {
        idl = index_range_read_ext(pb, be, type, (char*)indextype_EQUALITY,
                                   SLAPI_OP_GREATER_OR_EQUAL,
                                   low, NULL, 0, &txn, err, allidslimit);
    } else {
        idl = index_range_read_ext(pb, be, type, (char*)indextype_EQUALITY,
                                   SLAPI_OP_GREATER_OR_EQUAL,
                                   low, high, 1, &txn, err, allidslimit);
    }

done:
    if (lows) ber_bvecfree(lows);
    if (highs) ber_bvecfree(highs);

    LDAPDebug( LDAP_DEBUG_TRACE, "<= range_candidates %lu\n",
               (u_long)IDL_NIDS(idl), 0, 0 );

    return idl;
}

static IDList *
list_candidates(
    Slapi_PBlock  *pb,
    backend       *be,
    const char    *base,
    Slapi_Filter  *flist,
    int           ftype,
    int           *err,
    int           allidslimit
)
{
    IDList        *idl, *tmp, *tmp2;
    Slapi_Filter  *f, *nextf, *f_head;
    int           range = 0;
    int           isnot;
    int           f_count = 0, le_count = 0, ge_count = 0, is_bounded_range = 1;
    struct berval *low_val = NULL, *high_val = NULL;
    char          *t1;
    Slapi_Filter  *fpairs[2] = {NULL, NULL}; /* low, high */
    char          *tpairs[2] = {NULL, NULL};
    struct berval *vpairs[2] = {NULL, NULL};
    int is_and = 0;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> list_candidates 0x%x\n", ftype, 0, 0 );

    /* 
     * Optimize bounded range queries such as (&(cn>=A)(cn<=B)).
     * Could be better by matching pairs in a longer list
     * but for now support only a single pair.
     */
    if (ftype != LDAP_FILTER_AND) 
    {
        is_bounded_range = 0;
    }
    for ( f = slapi_filter_list_first( flist ); 
          f != NULL && is_bounded_range;
          f = slapi_filter_list_next( flist, f ) ) {
        f_count++;
        switch (slapi_filter_get_choice(f)) {
        case LDAP_FILTER_GE:
            if ( slapi_filter_get_ava(f, &t1, &low_val) != 0 ) {
                is_bounded_range = 0;
                continue;
            }
            ge_count++;
            if (NULL == fpairs[0])
            {
                fpairs[0] = f;
                tpairs[0] = slapi_ch_strdup(t1);
                vpairs[0] = slapi_ch_bvdup(low_val);
            }
            else if (NULL != fpairs[1] &&
                slapi_attr_type_cmp(tpairs[1], t1, SLAPI_TYPE_CMP_EXACT) != 0)
            {
                fpairs[0] = f;
                slapi_ch_free_string(&tpairs[0]);
                tpairs[0] = slapi_ch_strdup(t1);
                slapi_ch_bvfree(&vpairs[0]);
                vpairs[0] = slapi_ch_bvdup(low_val);
            }
            break;
        case LDAP_FILTER_LE:
            if ( slapi_filter_get_ava(f, &t1, &high_val) != 0 ) {
                is_bounded_range = 0;
                continue;
            }
            le_count++;
            if (NULL == fpairs[1])
            {
                fpairs[1] = f;
                tpairs[1] = slapi_ch_strdup(t1);
                vpairs[1] = slapi_ch_bvdup(high_val);
            }
            else if (NULL != fpairs[0] &&
                slapi_attr_type_cmp(tpairs[0], t1, SLAPI_TYPE_CMP_EXACT) != 0)
            {
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
    if (ftype == LDAP_FILTER_AND && f_count > 1)
    {
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
    if (le_count != 1 || ge_count != 1 || f_count != 2)
    {
        is_bounded_range = 0;
    }
    if (NULL == fpairs[0] || NULL == fpairs[1] ||
        0 != strcmp(tpairs[0], tpairs[1]) /* avoid "&(cn<=A)(sn>=B)" type */ )
    {
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
        LDAPDebug( LDAP_DEBUG_TRACE, "<= list_candidates %lu\n",
                   (u_long)IDL_NIDS(idl), 0, 0 );
        goto out;
    }

    idl = NULL;
    nextf = NULL;
    isnot = 0;
    for ( f_head = f = slapi_filter_list_first( flist ); f != NULL;
        f = slapi_filter_list_next( flist, f ) ) {

        /* Look for NOT foo type filter elements where foo is simple equality */
        isnot = (LDAP_FILTER_NOT == slapi_filter_get_choice( f )) &&
                (LDAP_FILTER_AND == ftype &&
                (LDAP_FILTER_EQUALITY == slapi_filter_get_choice(slapi_filter_list_first(f))));

        if (isnot) {
            /* if this is the first filter we have an allid search anyway, so bail */
            if(f == f_head)
            {
                idl = idl_allids( be );
                break;
            }

            /* Fetch the IDL for foo */
            /* Later we'll remember to call idl_notin() */
            LDAPDebug( LDAP_DEBUG_TRACE,"NOT filter\n", 0, 0, 0 );
            tmp = ava_candidates( pb, be, slapi_filter_list_first(f), LDAP_FILTER_EQUALITY, nextf, range, err, allidslimit );
        } else {
            if (fpairs[0] == f)
            {
                continue;
            }
            else if (fpairs[1] == f)
            {
                Slapi_Attr sattr;

                slapi_attr_init(&sattr, tpairs[0]);
                tmp = range_candidates(pb, be, tpairs[0],
                                       vpairs[0], vpairs[1], err, &sattr, allidslimit);
                attr_done(&sattr);
                if (tmp == NULL && ftype == LDAP_FILTER_AND)
                {
                    LDAPDebug( LDAP_DEBUG_TRACE,
                        "<= list_candidates NULL\n", 0, 0, 0 );
                    idl_free( idl );
                    idl = NULL;
                    goto out;
                }
            }
            /* Proceed as normal */
            else if ( (tmp = filter_candidates_ext( pb, be, base, f, nextf, range, err, allidslimit ))
                == NULL && ftype == LDAP_FILTER_AND ) {
                    LDAPDebug( LDAP_DEBUG_TRACE,
                        "<= list_candidates NULL\n", 0, 0, 0 );
                    idl_free( idl );
                    idl = NULL;
                    goto out;
            }
        }

        tmp2 = idl;
        if ( idl == NULL ) {
            idl = tmp;
            if ( (ftype == LDAP_FILTER_AND) && ((idl == NULL) ||
                (idl_length(idl) <= FILTER_TEST_THRESHOLD))) {
                break; /* We can exit the loop now, since the candidate list is small already */
            }
        } else if ( ftype == LDAP_FILTER_AND ) {
            if (isnot) {
                IDList *new_idl = NULL;
                int notin_result = 0;
                notin_result = idl_notin( be, idl, tmp, &new_idl );
                if (notin_result) {
                    idl_free(idl);
                    idl = new_idl;
                }
            } else {
                idl = idl_intersection(be, idl, tmp);
                idl_free( tmp2 );
            }
            idl_free( tmp );
            /* stop if the list has gotten too small */
            if ((idl == NULL) ||
                (idl_length(idl) <= FILTER_TEST_THRESHOLD))
                break;
        } else {
            Slapi_Operation *operation;
            slapi_pblock_get( pb, SLAPI_OPERATION, &operation );

            idl = idl_union( be, idl, tmp );
            idl_free( tmp );
            idl_free( tmp2 );
            /* stop if we're already committed to an exhaustive
             * search. :(
             */
            /* PAGED RESULTS: we strictly limit the idlist size by the allids (aka idlistscan) limit.
             */
            if (op_is_pagedresults(operation)) {
                int nids = IDL_NIDS(idl);
                if ( allidslimit > 0 && nids > allidslimit ) {
                    idl_free( idl );
                    idl = idl_allids( be );
                }
            }
            if (idl_is_allids(idl))
                break;
        }
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= list_candidates %lu\n",
                   (u_long)IDL_NIDS(idl), 0, 0 );
out:
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
    return( idl );
}

static IDList *
substring_candidates(
    Slapi_PBlock    *pb,
    backend *be,
    Slapi_Filter    *f,
    int            *err,
    int            allidslimit
)
{
    char         *type, *initial, *final;
    char         **any;
    IDList       *idl;
    Slapi_Value  **ivals;
    int          unindexed = 0;
    unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
    Slapi_Attr   sattr;
    back_txn     txn = {NULL};
    int          pr_idx = -1;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> sub_candidates\n", 0, 0, 0 );

    if (slapi_filter_get_subfilt( f, &type, &initial, &any, &final ) != 0) {
        LDAPDebug( LDAP_DEBUG_ANY, "  slapi_filter_get_subfilt fails\n",
            0, 0, 0 );
        return( NULL );
    }

    /*
     * get the index keys corresponding to the substring
     * assertion values
     */
    slapi_attr_init(&sattr, type);
    slapi_attr_assertion2keys_sub_sv( &sattr, initial, any, final, &ivals );
    attr_done(&sattr);
    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
    if ( ivals == NULL || *ivals == NULL ) {
        slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
        pagedresults_set_unindexed( pb->pb_conn, pb->pb_op, pr_idx );
        LDAPDebug( LDAP_DEBUG_TRACE,
            "<= sub_candidates ALLIDS (no keys)\n", 0, 0, 0 );
        return( idl_allids( be ) );
    }

    /*
     * look up each key in the index, ANDing the resulting
     * IDLists together.
     */
    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
    idl = keys2idl( pb, be, type, indextype_SUB, ivals, err, &unindexed, &txn, allidslimit );
    if ( unindexed ) {
        slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
        pagedresults_set_unindexed( pb->pb_conn, pb->pb_op, pr_idx );
    }
    valuearray_free( &ivals );

    LDAPDebug( LDAP_DEBUG_TRACE, "<= sub_candidates %lu\n",
                   (u_long)IDL_NIDS(idl), 0, 0 );
    return( idl );
}

static IDList *
keys2idl(
    Slapi_PBlock *pb,
    backend     *be,
    char        *type,
    const char  *indextype,
    Slapi_Value **ivals,
    int         *err,
    int         *unindexed,
    back_txn    *txn,
    int         allidslimit
)
{
    IDList    *idl;
    int    i;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> keys2idl type %s indextype %s\n",
        type, indextype, 0 );
    idl = NULL;
    for ( i = 0; ivals[i] != NULL; i++ ) {
        IDList    *idl2;

        idl2 = index_read_ext_allids( pb, be, type, indextype, slapi_value_get_berval(ivals[i]), txn, err, unindexed, allidslimit );

#ifdef LDAP_DEBUG
        /* XXX if ( slapd_ldap_debug & LDAP_DEBUG_TRACE ) { XXX */
        {
            char    buf[BUFSIZ];

            LDAPDebug( LDAP_DEBUG_TRACE,
                "   ival[%d] = \"%s\" => %lu IDs\n", i,
                encode( slapi_value_get_berval(ivals[i]), buf ), (u_long)IDL_NIDS(idl2) );
        }
#endif
        if ( idl2 == NULL ) {
            idl_free( idl );
            idl = NULL;
            break;
        }

        if (idl == NULL) {
            idl = idl2;
        } else {
            IDList    *tmp;

            tmp = idl;
            idl = idl_intersection(be, idl, idl2);
            idl_free( idl2 );
            idl_free( tmp );
            if ( idl == NULL ) {
                break;
            }
        }
    }

    return( idl );
}
