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

/* search.c - ldbm backend search function */
/* view with ts=4 */

#include <ldap.h>

#include "back-ldbm.h"
#include "vlv_srch.h"

/* prototypes */
static int build_candidate_list( Slapi_PBlock *pb, backend *be,
        struct backentry *e, const char * base, int scope,
        int *lookup_returned_allidsp, IDList** candidates);
static IDList *base_candidates( Slapi_PBlock *pb, struct backentry *e );
static IDList *onelevel_candidates( Slapi_PBlock *pb, backend *be, const char *base, struct backentry *e, Slapi_Filter *filter, int managedsait, int *lookup_returned_allidsp, int *err );
static back_search_result_set* new_search_result_set(IDList* idl,int vlv, int lookthroughlimit);
static void delete_search_result_set(Slapi_PBlock *pb, back_search_result_set **sr);
static int can_skip_filter_test( Slapi_PBlock *pb, struct slapi_filter *f,
        int scope, IDList *idl );

/* This is for performance testing, allows us to disable ACL checking altogether */
#if defined(DISABLE_ACL_CHECK)
#define ACL_CHECK_FLAG 0
#else
#define ACL_CHECK_FLAG 1
#endif

#define ISLEGACY(be) (be?(be->be_instance_info?(((ldbm_instance *)be->be_instance_info)->inst_li?(((ldbm_instance *)be->be_instance_info)->inst_li->li_legacy_errcode):0):0):0)

int
compute_lookthrough_limit( Slapi_PBlock *pb, struct ldbminfo *li )
{
    Slapi_Connection    *conn = NULL;
    int                 limit;
    Slapi_Operation *op;

    slapi_pblock_get( pb, SLAPI_CONNECTION, &conn);
    slapi_pblock_get( pb, SLAPI_OPERATION, &op);

    if ( slapi_reslimit_get_integer_limit( conn,
            li->li_reslimit_lookthrough_handle, &limit )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
        /*
         * no limit associated with binder/connection or some other error
         * occurred.  use the default.
          */
        int    isroot = 0;

        slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
        if (isroot) {
            limit = -1;
        } else {
            PR_Lock(li->li_config_mutex);
            limit = li->li_lookthroughlimit;
            PR_Unlock(li->li_config_mutex);
        }
    }

    if (op_is_pagedresults(op)) {
        if ( slapi_reslimit_get_integer_limit( conn,
                li->li_reslimit_pagedlookthrough_handle, &limit )
                != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
            PR_Lock(li->li_config_mutex);
            if (li->li_pagedlookthroughlimit) {
                limit = li->li_pagedlookthroughlimit;
            }
            /* else set above */
            PR_Unlock(li->li_config_mutex);
        }
    }
    return( limit );
}

int
compute_allids_limit( Slapi_PBlock *pb, struct ldbminfo *li )
{
    Slapi_Connection    *conn = NULL;
    int                 limit;
    Slapi_Operation *op;

    slapi_pblock_get( pb, SLAPI_CONNECTION, &conn);
    slapi_pblock_get( pb, SLAPI_OPERATION, &op);

    if ( slapi_reslimit_get_integer_limit( conn,
            li->li_reslimit_allids_handle, &limit )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
        PR_Lock(li->li_config_mutex);
        limit = li->li_allidsthreshold;
        PR_Unlock(li->li_config_mutex);
    }
    if (op_is_pagedresults(op)) {
        if ( slapi_reslimit_get_integer_limit( conn,
                li->li_reslimit_pagedallids_handle, &limit )
             != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
            PR_Lock(li->li_config_mutex);
            if (li->li_pagedallidsthreshold) {
                limit = li->li_pagedallidsthreshold;
            }
            PR_Unlock(li->li_config_mutex);
        }
    }
    return( limit );
}


/* don't free the berval, just clean it */
static void
berval_done(struct berval *val)
{
    slapi_ch_free_string(&val->bv_val);
}

/*
 * We call this function as we exit ldbm_back_search
 */
static int
ldbm_back_search_cleanup(Slapi_PBlock *pb,
                         struct ldbminfo *li,
                         sort_spec_thing *sort_control,
                         int ldap_result,
                         char* ldap_result_description,
                         int function_result,
                         struct vlv_request *vlv_request_control,
                         struct backentry *e)
{
    int estimate = 0; /* estimated search result count */
    backend *be;
    ldbm_instance *inst;
    back_search_result_set *sr = NULL;

    slapi_pblock_get( pb, SLAPI_BACKEND, &be );
    inst = (ldbm_instance *) be->be_instance_info;
    CACHE_RETURN(&inst->inst_cache, &e); /* NULL e is handled correctly */
    if (inst->inst_ref_count) {
        slapi_counter_decrement(inst->inst_ref_count);
    }

    if(sort_control!=NULL)
    {
        sort_spec_free(sort_control);
    }
    if(ldap_result>=LDAP_SUCCESS)
    {
        slapi_send_ldap_result( pb, ldap_result, NULL, ldap_result_description, 0, NULL );
    }
    /* code to free the result set if we don't need it */
    /* We get it and check to see if the structure was ever used */
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);
    if (sr) {
        if (function_result) {
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
            delete_search_result_set(pb, &sr);
        }
    }
    if (vlv_request_control)
    {
        berval_done(&vlv_request_control->value);
    }
    return function_result;
}

static int
ldbm_search_compile_filter(Slapi_Filter *f, void *arg)
{
    int rc = SLAPI_FILTER_SCAN_CONTINUE;
    if (f->f_choice == LDAP_FILTER_SUBSTRINGS) {
        char pat[BUFSIZ];
        char *p, *end, *bigpat = NULL;
        size_t size = 0;
        Slapi_Regex *re = NULL;
        const char *re_result = NULL;
        int i = 0;

        PR_ASSERT(NULL == f->f_un.f_un_sub.sf_private);
        /*
         * construct a regular expression corresponding to the filter
         */
        pat[0] = '\0';
        p = pat;
        end = pat + sizeof(pat) - 2; /* leave room for null */

        if (f->f_sub_initial != NULL) {
            size = strlen(f->f_sub_initial) + 1; /* add 1 for "^" */
        }

        while (f->f_sub_any && f->f_sub_any[i]) {
            size += strlen(f->f_sub_any[i++]) + 2; /* add 2 for ".*" */
        }

        if (f->f_sub_final != NULL) {
            size += strlen(f->f_sub_final) + 3; /* add 3 for ".*" and "$" */
        }

        size *= 2; /* doubled in case all filter chars need escaping (regex special chars) */
        size++; /* add 1 for null */

        if (p + size > end) {
            bigpat = slapi_ch_malloc(size);
            p = bigpat;
        }
        if (f->f_sub_initial != NULL) {
            *p++ = '^';
            p = filter_strcpy_special_ext(p, f->f_sub_initial, FILTER_STRCPY_ESCAPE_RECHARS);
        }
        for (i = 0; f->f_sub_any && f->f_sub_any[i]; i++) {
            /* ".*" + value */
            *p++ = '.';
            *p++ = '*';
            p = filter_strcpy_special_ext(p, f->f_sub_any[i], FILTER_STRCPY_ESCAPE_RECHARS);
        }
        if (f->f_sub_final != NULL) {
            /* ".*" + value */
            *p++ = '.';
            *p++ = '*';
            p = filter_strcpy_special_ext(p, f->f_sub_final, FILTER_STRCPY_ESCAPE_RECHARS);
            strcat(p, "$");
        }

        /* compile the regex */
        p = bigpat ? bigpat : pat;
        re = slapi_re_comp(p, &re_result);
        if (NULL == re) {
            LDAPDebug(LDAP_DEBUG_ANY, "ldbm_search_compile_filter: re_comp (%s) failed (%s): %s\n",
                      pat, p, re_result?re_result:"unknown" );
            rc = SLAPI_FILTER_SCAN_ERROR;
        } else {
            char ebuf[BUFSIZ];
            LDAPDebug(LDAP_DEBUG_TRACE, "ldbm_search_compile_filter: re_comp (%s)\n",
                      escape_string(p, ebuf), 0, 0);
            f->f_un.f_un_sub.sf_private = (void *)re;
        }
    } else if (f->f_choice == LDAP_FILTER_EQUALITY) {
        /* store the flags in the ava_private - should be ok - points
           to itself - no dangling references */
        f->f_un.f_un_ava.ava_private = &f->f_flags;
    }
    return rc;
}

static int
ldbm_search_free_compiled_filter(Slapi_Filter *f, void *arg)
{
    int rc = SLAPI_FILTER_SCAN_CONTINUE;
    if ((f->f_choice == LDAP_FILTER_SUBSTRINGS) &&
        (f->f_un.f_un_sub.sf_private)) {
        slapi_re_free((Slapi_Regex *)f->f_un.f_un_sub.sf_private);
        f->f_un.f_un_sub.sf_private = NULL;
    } else if (f->f_choice == LDAP_FILTER_EQUALITY) {
        /* clear the flags in the ava_private */
        f->f_un.f_un_ava.ava_private = NULL;
    }
    return rc;
}

/*
 * Return values from ldbm_back_search are:
 *
 *  0: Success.  A result set is in the pblock.  No results have been
 *     sent to the client.
 *  1: Success.  The result has already been sent to the client.
 * -1: An error occurred, and results have been sent to the client.
 * -2: Disk Full.  Abandon ship!
 */
int
ldbm_back_search( Slapi_PBlock *pb )
{
    /* Search stuff */
    backend *be;
    ldbm_instance *inst;
    struct ldbminfo *li;
    struct backentry *e;
    IDList *candidates= NULL;
    const char *base;
    Slapi_DN *basesdn = NULL;
    int scope;
    LDAPControl **controls = NULL;
    Slapi_Operation *operation;
    entry_address *addr;
    int estimate = 0; /* estimated search result set size */

    /* SORT control stuff */
    int sort = 0;
    int vlv = 0;
    struct berval *sort_spec = NULL;
    int is_sorting_critical = 0;
    int is_sorting_critical_orig = 0;
    sort_spec_thing *sort_control = NULL;

    /* VLV control stuff */
    int virtual_list_view = 0;
    struct berval *vlv_spec = NULL;
    int is_vlv_critical = 0;
    struct vlv_request vlv_request_control;
    back_search_result_set *sr = NULL;

    /* Fix for bugid #394184, SD, 20 Jul 00 */
    int tmp_err = -1;  /* must be lower than LDAP_SUCCESS */
    char * tmp_desc = NULL;
    /* end Fix for defect #394184 */

    int lookup_returned_allids = 0;
    int backend_count = 1;
    static int print_once = 1;
    back_txn txn = {NULL};
    int rc = 0;

    slapi_pblock_get( pb, SLAPI_BACKEND, &be );
    slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_SEARCH_TARGET_SDN, &basesdn );
    slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr);
    slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
    slapi_pblock_get( pb, SLAPI_REQCONTROLS, &controls );
    slapi_pblock_get( pb, SLAPI_BACKEND_COUNT, &backend_count );
    slapi_pblock_get( pb, SLAPI_TXN, &txn.back_txn_txn );

    if ( !txn.back_txn_txn ) {
        dblayer_txn_init( li, &txn );
        slapi_pblock_set( pb, SLAPI_TXN, txn.back_txn_txn );
    }

    if (NULL == basesdn) {
        slapi_send_ldap_result( pb, LDAP_INVALID_DN_SYNTAX, NULL,
                               "Null target DN", 0, NULL );
        return( -1 );
    }
    inst = (ldbm_instance *) be->be_instance_info;
    if (inst && inst->inst_ref_count) {
        slapi_counter_increment(inst->inst_ref_count);
    } else {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "ldbm_search: instance \"%s\" does not exist.\n",
                      inst ? inst->inst_name : "null instance");
        return( -1 );
    }
    base = slapi_sdn_get_dn(basesdn);

    /* Initialize the result set structure here because we need to use it during search processing */
    /* Beware that if we exit this routine sideways, we might leak this structure */
    sr = new_search_result_set( NULL, 0, 
                                compute_lookthrough_limit( pb, li ));
    slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET, sr );
    slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );
    
    /* clear this out so we can free it later */
    memset(&vlv_request_control, 0, sizeof(vlv_request_control));
    if ( NULL != controls )
    {
        /* Are we being asked to sort the results ? */
        sort = slapi_control_present(controls, LDAP_CONTROL_SORTREQUEST, &sort_spec, &is_sorting_critical_orig);        
        if(sort)
        {
            rc = parse_sort_spec(sort_spec, &sort_control);
            if (rc) {
                /* Badly formed SORT control */
                if (is_sorting_critical_orig) {
                    /* RFC 4511 4.1.11 the server must not process the operation
                     * and return LDAP_UNAVAILABLE_CRITICAL_EXTENSION
                     */
                    return ldbm_back_search_cleanup(pb, li, sort_control, 
                                LDAP_UNAVAILABLE_CRITICAL_EXTENSION, "Sort Control", 
                                SLAPI_FAIL_GENERAL, NULL, NULL);
                } else {
                    PRUint64 conn_id;
                    int op_id;

                    /* Just ignore the control */
                    sort = 0;
                    slapi_pblock_get(pb, SLAPI_CONN_ID, &conn_id);
                    slapi_pblock_get(pb, SLAPI_OPERATION_ID, &op_id);

                    LDAPDebug(LDAP_DEBUG_ANY,
                            "Warning: Sort control ignored for conn=%d op=%d\n",
                            conn_id, op_id, 0);                    
                }
            } else {
                /* set this operation includes the server side sorting */
                operation->o_flags |= OP_FLAG_SERVER_SIDE_SORTING;
            }
        }
        is_sorting_critical = is_sorting_critical_orig;

        /* Are we to provide a virtual view of the list? */
        if ((vlv = slapi_control_present( controls, LDAP_CONTROL_VLVREQUEST, &vlv_spec, &is_vlv_critical)))
        {
            if(sort)
            {
                rc = vlv_parse_request_control( be, vlv_spec, &vlv_request_control );
                if (rc != LDAP_SUCCESS) {
                    /* Badly formed VLV control */
                    if (is_vlv_critical) {
                        /* RFC 4511 4.1.11 the server must not process the operation
                         * and return LDAP_UNAVAILABLE_CRITICAL_EXTENSION
                         */
                        return ldbm_back_search_cleanup(pb, li, sort_control,
                                LDAP_UNAVAILABLE_CRITICAL_EXTENSION, "VLV Control", SLAPI_FAIL_GENERAL,
                                &vlv_request_control, NULL);
                    } else {
                        PRUint64 conn_id;
                        int op_id;

                        /* Just ignore the control */
                        virtual_list_view = 0;
                        slapi_pblock_get(pb, SLAPI_CONN_ID, &conn_id);
                        slapi_pblock_get(pb, SLAPI_OPERATION_ID, &op_id);

                        LDAPDebug(LDAP_DEBUG_ANY,
                                "Warning: VLV control ignored for conn=%d op=%d\n",
                                conn_id, op_id, 0);             
                    }

                } else {
                    {
                        /* Access Control Check to see if the client is allowed to use the VLV Control. */
                        Slapi_Entry *feature;
                        char dn[128];
                        char *dummyAttr = "dummy#attr";
                        char *dummyAttrs[2] = {NULL, NULL};

                        dummyAttrs[0] = dummyAttr;

                        /* This dn is normalized. */
                        PR_snprintf(dn, sizeof (dn), "dn: oid=%s,cn=features,cn=config", LDAP_CONTROL_VLVREQUEST);
                        feature = slapi_str2entry(dn, 0);
                        rc = plugin_call_acl_plugin(pb, feature, dummyAttrs, NULL, SLAPI_ACL_READ, ACLPLUGIN_ACCESS_DEFAULT, NULL);
                        slapi_entry_free(feature);
                        if (rc != LDAP_SUCCESS) {
                            /* Client isn't allowed to do this. */
                            return ldbm_back_search_cleanup(pb, li, sort_control,
                                    rc, "VLV Control", SLAPI_FAIL_GENERAL,
                                    &vlv_request_control, NULL);
                        }
                    }
                    /*
                     * Sorting must always be critical for VLV; Force it be so.
                     */
                    is_sorting_critical = 1;
                    virtual_list_view = 1;
                }
            }
            else
            {
                /* Can't have a VLV control without a SORT control */
                return ldbm_back_search_cleanup(pb, li, sort_control, 
                                LDAP_SORT_CONTROL_MISSING, "VLV Control", 
                                SLAPI_FAIL_GENERAL, &vlv_request_control, NULL);
            }
        }
    }
    if ((virtual_list_view || sort) && backend_count > 0)
    {
        char *ctrlstr = NULL;
        struct vlv_response vlv_response = {0};
        if (virtual_list_view)
        {
            if (sort)
            {
                ctrlstr = "The VLV and sort controls cannot be processed";
            }
            else
            {
                ctrlstr = "The VLV control cannot be processed";
            }
        }
        else
        {
            if (sort)
            {
                ctrlstr = "The sort control cannot be processed";
            }
        }

        PR_ASSERT(NULL != ctrlstr);

        if (print_once)
        {
            LDAPDebug(LDAP_DEBUG_ANY,
                    "ERROR: %s "
                    "when more than one backend is involved. "
                    "VLV indexes that will never be used should be removed.\n",
                    ctrlstr, 0, 0);
            print_once = 0;
        }

        /* 402380: mapping tree must refuse VLV and SORT control
         * when several backends are impacted by a search */
        if (0 != is_vlv_critical) 
        {
            vlv_response.result = LDAP_UNWILLING_TO_PERFORM;
            vlv_make_response_control(pb, &vlv_response);
            if (sort)
            {
                sort_make_sort_response_control(pb, LDAP_UNWILLING_TO_PERFORM, NULL);
            }
            if (ISLEGACY(be))
            {
                return ldbm_back_search_cleanup(pb, li, sort_control,
                            LDAP_UNWILLING_TO_PERFORM, ctrlstr,
                            SLAPI_FAIL_GENERAL, &vlv_request_control, NULL);
            }
            else
            {
                return ldbm_back_search_cleanup(pb, li, sort_control,
                            LDAP_VIRTUAL_LIST_VIEW_ERROR, ctrlstr,
                            SLAPI_FAIL_GENERAL, &vlv_request_control, NULL);
            }
        }
        else
        {
            if (0 != is_sorting_critical_orig)
            {
                if (virtual_list_view)
                {
                    vlv_response.result = LDAP_UNWILLING_TO_PERFORM;
                    vlv_make_response_control(pb, &vlv_response);
                }
                sort_make_sort_response_control(pb, LDAP_UNWILLING_TO_PERFORM, NULL);
                return ldbm_back_search_cleanup(pb, li, sort_control,
                            LDAP_UNAVAILABLE_CRITICAL_EXTENSION, ctrlstr,
                            SLAPI_FAIL_GENERAL, &vlv_request_control, NULL);
            }
            else /* vlv and sorting are not critical, so ignore the control */
            {
                if (virtual_list_view)
                {
                    vlv_response.result = LDAP_UNWILLING_TO_PERFORM;
                    vlv_make_response_control(pb, &vlv_response);
                }
                if (sort)
                {
                    sort_make_sort_response_control(pb, LDAP_UNWILLING_TO_PERFORM, NULL);
                }
                sort = 0;
                virtual_list_view = 0;
            }
        }
    }

    /*
     * Get the base object for the search.
     * The entry "" will never be contained in the database,
     * so treat it as a special case.
     */
    if ( *base == '\0' )
    {
        e = NULL;
    }
    else
    {
        if ( ( e = find_entry( pb, be, addr, &txn )) == NULL )
        {
            /* error or referral sent by find_entry */
            return ldbm_back_search_cleanup(pb, li, sort_control, 
                            -1, NULL, 1, &vlv_request_control, NULL);
        }
    }

    /*
     * If this is a persistent search then the client is only
     * interested in entries that change, so we skip building
     * a candidate list.
     */
    if (operation_is_flag_set( operation, OP_FLAG_PS_CHANGESONLY ))
    {
        candidates = NULL;
    }
    else
    {
        time_t time_up= 0;
        int lookthrough_limit = 0;
        struct vlv_response vlv_response_control;
        int abandoned= 0;
        int vlv_rc;
        /*
         * Build a list of IDs for this entry and scope
         */    
        if ((NULL != controls) && (sort) && (vlv)) {
            /* This candidate list is for vlv, no need for sort only. */
            switch (vlv_search_build_candidate_list(pb, basesdn, &vlv_rc,
                                                    sort_control,
                                                    (vlv ? &vlv_request_control : NULL),
                                                    &candidates, &vlv_response_control)) {
            case VLV_ACCESS_DENIED:
                return ldbm_back_search_cleanup(pb, li, sort_control,
                                                vlv_rc, "VLV Control",
                                                SLAPI_FAIL_GENERAL, 
                                                &vlv_request_control, e);
            case VLV_BLD_LIST_FAILED:
                return ldbm_back_search_cleanup(pb, li, sort_control,
                                                vlv_response_control.result,
                                                NULL, SLAPI_FAIL_GENERAL,
                                                &vlv_request_control, e);
                
            case LDAP_SUCCESS:
                /* Log to the access log the particulars of this sort request */
                /* Log message looks like this: SORT <key list useful for input
                 * to ldapsearch> <#candidates> | <unsortable> */
                sort_log_access(pb,sort_control,NULL);
                /* Since a pre-computed index was found for the VLV Search then
                 * the candidate list now contains exactly what should be 
                 * returned.
                 * There's no need to sort or trim the candidate list.
                 *
                 * However, the client will be expecting a Sort Response control
                 */
                if (LDAP_SUCCESS !=
                    sort_make_sort_response_control( pb, 0, NULL ) )
                {
                    return ldbm_back_search_cleanup(pb, li, sort_control,
                                                    LDAP_OPERATIONS_ERROR,
                                                    "Sort Response Control",
                                                    SLAPI_FAIL_GENERAL,
                                                    &vlv_request_control, e);
                }
            }
        }
        if (candidates == NULL)
        {
            int rc = build_candidate_list(pb, be, e, base, scope,
                                          &lookup_returned_allids, &candidates);
            if (rc)
            {
                /* Error result sent by build_candidate_list */
                return ldbm_back_search_cleanup(pb, li, sort_control, -1,
                                                NULL, rc, 
                                                &vlv_request_control, e);
            }
            /*
             * If we're sorting then we must check what administrative
             * limits should be imposed.  Work out at what time to give
             * up, and how many entries we should sift through.
             */
            if (sort && (NULL != candidates))
            {
                time_t optime = 0;
                int tlimit = 0;

                slapi_pblock_get( pb, SLAPI_SEARCH_TIMELIMIT, &tlimit );
                slapi_pblock_get( pb, SLAPI_OPINITIATED_TIME, &optime );
                /* 
                 * (tlimit==-1) means no time limit
                 */
                time_up = (tlimit==-1 ? -1 : optime + tlimit);

                lookthrough_limit = compute_lookthrough_limit( pb, li );
            }

            /*
             * If we're presenting a virtual list view, then apply the
             * search filter before sorting.
             */
            if (virtual_list_view && (NULL != candidates))
            {
                IDList *idl = NULL;
                Slapi_Filter *filter = NULL;
                slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter );
                rc = vlv_filter_candidates(be, pb, candidates, basesdn,
                                          scope, filter, &idl,
                                          lookthrough_limit, time_up);
                if (rc == 0) {
                    idl_free(candidates);
                    candidates= idl;
                }
                else
                {
                    return ldbm_back_search_cleanup(pb, li, sort_control,
                                                    rc, NULL, -1, 
                                                    &vlv_request_control, e);
                }
            }
            /*
             * Client wants the server to sort the results.
             */
            if (sort)
            {
              if (NULL == candidates)
              {
                /* Even if candidates is NULL, we have to return a sort 
                 * response control with the LDAP_SUCCESS return code. */
                if (LDAP_SUCCESS != 
                    sort_make_sort_response_control( pb, LDAP_SUCCESS, NULL ))
                {
                    return ldbm_back_search_cleanup(pb, li, sort_control,
                                             LDAP_PROTOCOL_ERROR,
                                             "Sort Response Control", -1,
                                             &vlv_request_control, e);
                }
              }
              else
              {
                /* Before we haste off to sort the candidates, we need to 
                 * prepare some information for the purpose of imposing the
                 * administrative limits.
                 * We figure out the time when the time limit will be up.
                 * We can't use the size limit because we might be sorting
                 * a candidate list larger than the result set.
                 * But, we can use the lookthrough limit---we count each 
                 * time we access an entry as one look and act accordingly.
                 */

                char *sort_error_type = NULL;
                int sort_return_value  = 0;

                /* Don't log internal operations */
                if (!operation_is_flag_set(operation, OP_FLAG_INTERNAL)) {
                    /* Log to the access log the particulars of this
                     * sort request */
                    /* Log message looks like this: SORT <key list useful for 
                     * input to ldapsearch> <#candidates> | <unsortable> */
                    sort_log_access(pb,sort_control,candidates);
                }
                sort_return_value = sort_candidates( be, lookthrough_limit,
                                                     time_up, pb, candidates,
                                                     sort_control,
                                                     &sort_error_type );
                /* Fix for bugid # 394184, SD, 20 Jul 00 */
                /* replace the hard coded return value by the appropriate 
                 * LDAP error code */
                switch (sort_return_value) {
                   case LDAP_SUCCESS:  /* Everything OK */
                    vlv_response_control.result= LDAP_SUCCESS;
                    break;
                case LDAP_PROTOCOL_ERROR:  /* A protocol error */
                    return ldbm_back_search_cleanup(pb, li, sort_control,
                                                    LDAP_PROTOCOL_ERROR,
                                                    "Sort Control", -1,
                                                    &vlv_request_control, e);
                case LDAP_UNWILLING_TO_PERFORM:  /* Too hard */
                case LDAP_OPERATIONS_ERROR:  /* Operation error */
                case LDAP_TIMELIMIT_EXCEEDED:  /* Timeout */
                    vlv_response_control.result= LDAP_TIMELIMIT_EXCEEDED;
                    break;
                case LDAP_ADMINLIMIT_EXCEEDED:  /* Admin limit exceeded */
                    vlv_response_control.result = LDAP_ADMINLIMIT_EXCEEDED;
                    break;
                case LDAP_OTHER:  /* Abandoned */
                    abandoned = 1; /* So that we don't return a result code */
                    is_sorting_critical = 1;  /* In order to have the results
                                                 discarded */
                    break;
                default:  /* Should never get here */
                    break;
                }
                /* End fix for bug # 394184 */
                /*
                 * If the sort control was marked as critical, and there was
                 * an error in sorting, don't return any entries, and return 
                 * unavailableCriticalExtension in the searchResultDone message.
                 */
                /* Fix for bugid #394184, SD, 05 Jul 00 */
                /* we were not actually returning unavailableCriticalExtension;
                 now fixed (hopefully !) */
                if (is_sorting_critical && (0 != sort_return_value))
                {
                    idl_free(candidates);
                    candidates = idl_alloc(0);
                    tmp_err = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
                    tmp_desc = "Sort Response Control";
                }
                /* end Fix for bugid #394184 */
                /* Generate the control returned to the client to indicate 
                 * sort result */
                if (LDAP_SUCCESS != sort_make_sort_response_control( pb,
                                          sort_return_value, sort_error_type ) )
                {
                    return ldbm_back_search_cleanup(pb, li, sort_control,
                                             (abandoned?-1:LDAP_PROTOCOL_ERROR),
                                             "Sort Response Control", -1,
                                             &vlv_request_control, e);
                }
              }
            }
            /*
             * If we're presenting a virtual list view, then the candidate list
             * must be trimmed down to just the range of entries requested.
             */
            if (virtual_list_view)
            {
                if (NULL != candidates && candidates->b_nids>0)
                {
                    IDList *idl= NULL;
                    back_txn txn = {NULL};
                    slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
                    vlv_response_control.result =
                        vlv_trim_candidates_txn(be, candidates, sort_control,
                        &vlv_request_control, &idl, &vlv_response_control, &txn);
                    if(vlv_response_control.result==0)
                    {
                        idl_free(candidates);
                        candidates = idl;
                    }
                    else
                    {
                        return ldbm_back_search_cleanup(pb, li, sort_control,
                                                    vlv_response_control.result,
                                                    NULL, -1, 
                                                    &vlv_request_control, e);
                    }
                }
                else
                {
                    vlv_response_control.targetPosition = 0;
                    vlv_response_control.contentCount = 0;
                    vlv_response_control.result = LDAP_SUCCESS;
                }
            }
        }
        if (virtual_list_view)
        {
            if(LDAP_SUCCESS != 
                         vlv_make_response_control( pb, &vlv_response_control ))
            {
                return ldbm_back_search_cleanup(pb, li, sort_control,
                                             (abandoned?-1:LDAP_PROTOCOL_ERROR),
                                             "VLV Response Control", -1,
                                             &vlv_request_control, e);
            }
            /* Log the VLV operation */
            vlv_print_access_log(pb,&vlv_request_control,&vlv_response_control);
        }
    }

    CACHE_RETURN( &inst->inst_cache, &e );

    /*
     * if the candidate list is an allids list, arrange for access log
     * to record that fact.
     */
    if ( NULL != candidates && ALLIDS( candidates )) {
        unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
        int ri = 0;
        int pr_idx = -1;

        /*
         * Return error if nsslapd-require-index is set and
         * this is not an internal operation.
         * We hope the plugins know what they are doing!
         */
        if (!operation_is_flag_set(operation, OP_FLAG_INTERNAL)) {

            PR_Lock(inst->inst_config_mutex);
            ri = inst->require_index;
            PR_Unlock(inst->inst_config_mutex);

            if (ri) {
                idl_free(candidates);
                candidates = idl_alloc(0);
                tmp_err = LDAP_UNWILLING_TO_PERFORM;
                tmp_desc = "Search is not indexed";
            }
        }

        slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
        slapi_pblock_get( pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx );
        pagedresults_set_unindexed( pb->pb_conn, pb->pb_op, pr_idx );
    }

    sr->sr_candidates = candidates;
    sr->sr_virtuallistview = virtual_list_view;

    /* Set the estimated search result count for simple paged results */
    if (sr->sr_candidates && !ALLIDS(sr->sr_candidates)) {
        estimate = IDL_NIDS(sr->sr_candidates);
    }
    slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );

    /* check to see if we can skip the filter test */
    if ( li->li_filter_bypass && NULL != candidates && !virtual_list_view
                && !lookup_returned_allids ) {
        Slapi_Filter *filter = NULL;

        slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter );
        if ( can_skip_filter_test( pb, filter, scope, candidates)) {
            sr->sr_flags |= SR_FLAG_CAN_SKIP_FILTER_TEST;
        }
    }

    /* if we need to perform the filter test, pre-digest the filter to
       speed up the filter test */
    if ( !(sr->sr_flags & SR_FLAG_CAN_SKIP_FILTER_TEST) ||
         li->li_filter_bypass_check ) {
        int rc = 0, filt_errs = 0;
        Slapi_Filter *filter= NULL;

        slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &filter);
        slapi_filter_free(sr->sr_norm_filter, 1);
        sr->sr_norm_filter = slapi_filter_dup(filter);
        /* step 1 - normalize all of the values used in the search filter */
        slapi_filter_normalize(sr->sr_norm_filter, PR_TRUE /* normalize values too */);
        /* step 2 - pre-compile the substr regex and the equality flags */
        rc = slapi_filter_apply(sr->sr_norm_filter, ldbm_search_compile_filter,
                                NULL, &filt_errs);
        if (rc != SLAPI_FILTER_SCAN_NOMORE) {
            LDAPDebug2Args(LDAP_DEBUG_ANY,
                           "ERROR: could not pre-compile the search filter - error %d %d\n",
                           rc, filt_errs);
            if (rc == SLAPI_FILTER_SCAN_ERROR) {
                tmp_err = LDAP_OPERATIONS_ERROR;
                tmp_desc = "Could not compile regex for filter matching";
            }
        }
    }

    /* Fix for bugid #394184, SD, 05 Jul 00 */
    /* tmp_err == -1: no error */
    return ldbm_back_search_cleanup(pb, li, sort_control, tmp_err, tmp_desc,
                                    (tmp_err  == -1 ? 0 : -1), 
                                    &vlv_request_control, NULL);
    /* end Fix for bugid #394184 */
}

/*
 * Build a candidate list for this backentry and scope.
 * Could be a BASE, ONELEVEL, or SUBTREE search.
 *
 * Returns:
 *   0  - success
 *   <0 - fail
 *   
 */
static int
build_candidate_list( Slapi_PBlock *pb, backend *be, struct backentry *e,
    const char * base, int scope, int *lookup_returned_allidsp,
    IDList** candidates)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    int managedsait= 0;
    Slapi_Filter *filter= NULL;
    int err= 0;
    int r= 0;

    slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter );
    slapi_pblock_get( pb, SLAPI_MANAGEDSAIT, &managedsait );

    switch ( scope ) {
    case LDAP_SCOPE_BASE:
        *candidates = base_candidates( pb, e );
        break;

    case LDAP_SCOPE_ONELEVEL:
        *candidates = onelevel_candidates( pb, be, base, e, filter, managedsait, 
                lookup_returned_allidsp, &err );
        break;

    case LDAP_SCOPE_SUBTREE:
        *candidates = subtree_candidates(pb, be, base, e, filter, managedsait,
                lookup_returned_allidsp, &err);
        break;

    default:
        slapi_send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, "Bad scope", 0, NULL );
        r = SLAPI_FAIL_GENERAL;
    }
    if ( 0 != err && DB_NOTFOUND != err ) {
        LDAPDebug( LDAP_DEBUG_ANY, "database error %d\n", err, 0, 0 );
        slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL,
                    0, NULL );
        if (LDBM_OS_ERR_IS_DISKFULL(err)) r = return_on_disk_full(li);
        else r = SLAPI_FAIL_GENERAL;
    }

    /*
     * If requested, set a flag to indicate whether the indexed
     * lookup returned an ALLIDs block.  Note that this is taken care of
     * above already for subtree searches.
     */
    if ( NULL != lookup_returned_allidsp ) {
        if ( 0 == err || DB_NOTFOUND == err ) {
            if ( !(*lookup_returned_allidsp) && LDAP_SCOPE_SUBTREE != scope ) {
                *lookup_returned_allidsp =
                        ( NULL != *candidates && ALLIDS( *candidates ));
            }
        } else {
            *lookup_returned_allidsp = 0;
        }
    }

    LDAPDebug(LDAP_DEBUG_TRACE, "candidate list has %lu ids\n",
                  *candidates ? (*candidates)->b_nids : 0L, 0, 0);

    return r;
}

/*
 * Build a candidate list for a BASE scope search.
 */
static IDList *
base_candidates(Slapi_PBlock *pb, struct backentry *e)
{
    IDList *idl= idl_alloc( 1 );
    idl_append( idl, NULL == e ? 0 : e->ep_id );
    return( idl );
}

/*
 * Modify the filter to include entries of the referral objectclass
 *
 * make (|(originalfilter)(objectclass=referral))
 *
 * "focref, forr" are temporary filters which the caller must free
 * non-recursively when done with the returned filter.
 */
static Slapi_Filter*
create_referral_filter(Slapi_Filter* filter, Slapi_Filter** focref, Slapi_Filter** forr)
{
    char *buf = slapi_ch_strdup( "objectclass=referral" );

    *focref = slapi_str2filter( buf );
    *forr = slapi_filter_join( LDAP_FILTER_OR, filter, *focref );

    slapi_ch_free((void **)&buf);
    return *forr;
}

/*
 * Modify the filter to be a one level search.
 *
 *    (&(parentid=idofbase)(|(originalfilter)(objectclass=referral)))
 *
 * "fid2kids, focref, fand, forr" are temporary filters which the
 * caller must free'd non-recursively when done with the returned filter.
 * 
 * This function is exported for the VLV code to use.
 */
Slapi_Filter* 
create_onelevel_filter(Slapi_Filter* filter, const struct backentry *baseEntry, int managedsait, Slapi_Filter** fid2kids, Slapi_Filter** focref, Slapi_Filter** fand, Slapi_Filter** forr)
{
    Slapi_Filter *ftop= filter;
    char buf[40];

    if ( !managedsait ) 
    {
        ftop= create_referral_filter(filter, focref, forr);
    }

    sprintf( buf, "parentid=%lu", (u_long)(baseEntry != NULL ? baseEntry->ep_id : 0) );
    *fid2kids = slapi_str2filter( buf );
    *fand = slapi_filter_join( LDAP_FILTER_AND, ftop, *fid2kids );

    return *fand;
}

/*
 * Build a candidate list for a ONELEVEL scope search.
 */
static IDList *
onelevel_candidates(
    Slapi_PBlock     *pb,
    backend          *be,
    const char       *base,
    struct backentry *e,
    Slapi_Filter     *filter,
    int              managedsait,
    int              *lookup_returned_allidsp,
    int              *err
)
{
    Slapi_Filter *fid2kids= NULL;
    Slapi_Filter *focref= NULL;
    Slapi_Filter *fand= NULL;
    Slapi_Filter *forr= NULL;
    Slapi_Filter *ftop= NULL;
    IDList *candidates;

    /*
     * modify the filter to be something like this:
     *
     *    (&(parentid=idofbase)(|(originalfilter)(objectclass=referral)))
     */

    ftop= create_onelevel_filter(filter, e, managedsait, &fid2kids, &focref, &fand, &forr);

    /* from here, it's just like subtree_candidates */
    candidates = filter_candidates( pb, be, base, ftop, NULL, 0, err );

    *lookup_returned_allidsp = slapi_be_is_flag_set(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);

    /* free up just the filter stuff we allocated above */
    slapi_filter_free( fid2kids, 0 );
    slapi_filter_free( fand, 0 );
    slapi_filter_free( forr, 0 );
    slapi_filter_free( focref, 0 );

    return( candidates );
}

/*
 * We need to modify the filter to be something like this:
 *
 *    (|(originalfilter)(objectclass=referral))
 *
 * the "objectclass=referral" part is used to select referrals to return.
 * it is only included if the managedsait service control is not set.
 *
 * This function is exported for the VLV code to use.
 */
Slapi_Filter*
create_subtree_filter(Slapi_Filter* filter, int managedsait, Slapi_Filter** focref, Slapi_Filter** forr)
{
    Slapi_Filter *ftop= filter;

    if ( !managedsait ) 
    {
        ftop= create_referral_filter(filter, focref, forr);
    }

    return ftop;
}


/*
 * Build a candidate list for a SUBTREE scope search.
 */
IDList *
subtree_candidates(
    Slapi_PBlock           *pb,
    backend                *be,
    const char             *base,
    const struct backentry *e,
    Slapi_Filter           *filter,
    int                    managedsait,
    int                    *allids_before_scopingp,
    int                    *err
)
{
    Slapi_Filter *focref= NULL;
    Slapi_Filter *forr= NULL;
    Slapi_Filter *ftop= NULL;
    IDList *candidates;
    PRBool has_tombstone_filter;
    int isroot = 0;
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    int allidslimit = compute_allids_limit(pb, li);

    /* make (|(originalfilter)(objectclass=referral)) */
    ftop= create_subtree_filter(filter, managedsait, &focref, &forr);

    /* Fetch a candidate list for the original filter */
    candidates = filter_candidates_ext( pb, be, base, ftop, NULL, 0, err, allidslimit );
    slapi_filter_free( forr, 0 );
    slapi_filter_free( focref, 0 );

    /* set 'allids before scoping' flag */
    if ( NULL != allids_before_scopingp ) {
        *allids_before_scopingp = ( NULL != candidates && ALLIDS( candidates ));
    }

    has_tombstone_filter = (filter->f_flags & SLAPI_FILTER_TOMBSTONE);
    slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );

    /*
     * Apply the DN components if the candidate list is greater than
     * our threshold, and if the filter is not "(objectclass=nstombstone)",
     * since tombstone entries are not indexed in the ancestorid index.
     * Note: they are indexed in the entryrdn index.
     */
    if(candidates!=NULL && (idl_length(candidates)>FILTER_TEST_THRESHOLD)) {
        IDList *tmp = candidates, *descendants = NULL;
        back_txn txn = {NULL};

        slapi_pblock_get(pb, SLAPI_TXN, &txn.back_txn_txn);
        if (entryrdn_get_noancestorid()) {
            /* subtree-rename: on && no ancestorid */
            *err = entryrdn_get_subordinates(be,
                                         slapi_entry_get_sdn_const(e->ep_entry),
                                         e->ep_id, &descendants, &txn);
            idl_insert(&descendants, e->ep_id);
            candidates = idl_intersection(be, candidates, descendants);
            idl_free(tmp);
            idl_free(descendants);
        } else if (!has_tombstone_filter) {
            *err = ldbm_ancestorid_read_ext(be, &txn, e->ep_id, &descendants, allidslimit);
            idl_insert(&descendants, e->ep_id);
            candidates = idl_intersection(be, candidates, descendants);
            idl_free(tmp);
            idl_free(descendants);
        } /* else == has_tombstone_filter: do nothing */
    }

    return( candidates );
}

static int grok_filter(struct slapi_filter    *f);
#if 0
/* Helper for grok_filter() */
static int 
grok_filter_list(struct slapi_filter    *flist)
{
    struct slapi_filter    *f;

    /* Scan the clauses of the AND filter, if any of them fails the grok, then we fail */
    for ( f = flist; f != NULL; f = f->f_next ) {
        if ( !grok_filter(f) ) {
                return( 0 );
        }
    }
    return( 1 );
}
#endif

/* Helper function for can_skip_filter_test() */
static int grok_filter(struct slapi_filter    *f)
{
    switch ( f->f_choice ) {
    case LDAP_FILTER_EQUALITY:
        return 1; /* If there's an ID list and an equality filter, we can skip the filter test */
    case LDAP_FILTER_SUBSTRINGS:
        return 0;

    case LDAP_FILTER_GE:
        return 1;

    case LDAP_FILTER_LE:
        return 1;

    case LDAP_FILTER_PRESENT:
        return 1; /* If there's an ID list, and a presence filter, we can skip the filter test */

    case LDAP_FILTER_APPROX:
        return 0;

    case LDAP_FILTER_EXTENDED:
        return 0;

    case LDAP_FILTER_AND:
        return 0; /* Unless we check to see whether the presence and equality branches 
                    of the search filter were all indexed, we get things wrong here,
                    so let's punt for now */
        /* return grok_filter_list(f->f_and);  AND clauses are potentially OK  */

    case LDAP_FILTER_OR:
        return 0;

    case LDAP_FILTER_NOT:
        return 0;

    default:
        return 0;
    }
}

/* Routine which says whether or not the indices produced a "correct" answer */
static int
can_skip_filter_test(
    Slapi_PBlock        *pb,
    struct slapi_filter    *f,
    int scope,
    IDList *idl
)
{
    int rc = 0;

    /* Is the ID list ALLIDS ? */
    if ( ALLIDS(idl)) {
        /* If so, then can't optimize */
        return rc;
    }

    /* Is this a base scope search? */
    if ( scope == LDAP_SCOPE_BASE ) {
        /*
         * If so, then we can't optimize.  Why not?  Because we only consult
         * the entrydn/entryrdn index in producing our 1 candidate, and that 
         * means we have not used the filter to produce the candidate list.
         */
        return rc;
    }

    /* Grok the filter and tell me if it has only equality components in it */
    rc = grok_filter(f);

    /* If we haven't determined that we can't skip the filter test already,
     * do one last check for attribute subtypes.  We don't need to worry 
     * about any complex filters here since grok_filter() will have already
     * assumed that we can't skip the filter test in those cases. */
    if (rc != 0) {
        char *type = NULL;
        char *basetype = NULL;

        /* We don't need to free type since that's taken
         * care of when the filter is free'd later.  We
         * do need to free basetype when we are done. */
        slapi_filter_get_attribute_type(f, &type);
        basetype = slapi_attr_basetype(type, NULL, 0);

        /* Is the filter using an attribute subtype? */
        if (strcasecmp(type, basetype) != 0) {
            /* If so, we can't optimize since attribute subtypes
             * are simply indexed under their basetype attribute.
             * The basetype index has no knowledge of the subtype
             * itself.  In the future, we should add support for
             * indexing the subtypes so we can optimize this type
             * of search. */
            rc = 0;
        }
        slapi_ch_free_string(&basetype);
    }

    return rc;
}



/*
 * Return the next entry in the result set.  The entry is returned
 * in the pblock.
 * Returns 0 normally.  If -1 is returned, it means that some
 * exceptional condition, e.g. timelimit exceeded has occurred, 
 * and this routine has sent a result to the client.  If zero
 * is returned and no entry is available in the PBlock, then
 * we've iterated through all the entries.
 */
int
ldbm_back_next_search_entry( Slapi_PBlock *pb )
{
    return ldbm_back_next_search_entry_ext( pb, 0 );
}

int
ldbm_back_next_search_entry_ext( Slapi_PBlock *pb, int use_extension )
{
    backend                *be;
    ldbm_instance          *inst;
    struct ldbminfo        *li;
    int                    scope;
    int                    managedsait;
    Slapi_Attr             *attr;
    Slapi_Filter           *filter;
    back_search_result_set *sr;
    ID                     id;
    struct backentry       *e;
    int                    nentries;
    time_t                 curtime, stoptime, optime;
    int                    tlimit, llimit, slimit, isroot;
    struct berval          **urls = NULL;
    int                    err;
    Slapi_DN               *basesdn = NULL;
    char                   *target_uniqueid;
    int                    rc = 0; 
    int                    estimate = 0; /* estimated search result count */
    back_txn               txn = {NULL};
    int                    pr_idx = -1;
    Slapi_Connection       *conn;
    Slapi_Operation        *op;

    slapi_pblock_get( pb, SLAPI_SEARCH_TARGET_SDN, &basesdn );
    if (NULL == basesdn) {
        slapi_send_ldap_result( pb, LDAP_INVALID_DN_SYNTAX, NULL,
                               "Null target DN", 0, NULL );
        return( -1 );
    }
    slapi_pblock_get( pb, SLAPI_SEARCH_RESULT_SET, &sr );
    if (NULL == sr) {
        goto bail;
    }
    slapi_pblock_get( pb, SLAPI_BACKEND, &be );
    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
    slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
    slapi_pblock_get( pb, SLAPI_MANAGEDSAIT, &managedsait );
    slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter );
    slapi_pblock_get( pb, SLAPI_NENTRIES, &nentries );
    slapi_pblock_get( pb, SLAPI_SEARCH_SIZELIMIT, &slimit );
    slapi_pblock_get( pb, SLAPI_SEARCH_TIMELIMIT, &tlimit );
    slapi_pblock_get( pb, SLAPI_OPINITIATED_TIME, &optime );
    slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
    slapi_pblock_get( pb, SLAPI_SEARCH_REFERRALS, &urls );
    slapi_pblock_get( pb, SLAPI_TARGET_UNIQUEID, &target_uniqueid );
    slapi_pblock_get( pb, SLAPI_TXN, &txn.back_txn_txn );
    slapi_pblock_get( pb, SLAPI_CONNECTION, &conn );
    slapi_pblock_get( pb, SLAPI_OPERATION, &op );

    if ( !txn.back_txn_txn ) {
        dblayer_txn_init( li, &txn );
        slapi_pblock_set( pb, SLAPI_TXN, txn.back_txn_txn );
    }

    if (sr->sr_norm_filter) {
        int val = 1;
        slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &val );
        filter = sr->sr_norm_filter;
    }

    if (op_is_pagedresults(op)) {
        int myslimit;
        /* On Simple Paged Results search, sizelimit is appied for each page. */
        slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
        myslimit = pagedresults_get_sizelimit(conn, op, pr_idx);
        if (myslimit >= 0) {
            slimit = myslimit;
        }
    } else if (sr->sr_current_sizelimit >= 0) {
        /* 
         * sr_current_sizelimit contains the current sizelimit.
         * In case of paged results, getting one page is one operation,
         * while the results on each page are from same back_search_result_set.
         * To maintain sizelimit beyond operations, back_search_result_set
         * holds the current sizelimit value.
         * (The current sizelimit is valid inside an operation, as well.)
         */
        slimit = sr->sr_current_sizelimit;
    }
    
    inst = (ldbm_instance *) be->be_instance_info;

    /* Return to the cache the entry we handed out last time */
    /* If we are using the extension, the front end will tell
     * us when to do this so we don't do it now */
    if (sr->sr_entry && !use_extension) {
        CACHE_RETURN( &inst->inst_cache, &(sr->sr_entry) );
        sr->sr_entry = NULL;
    }

    if(sr->sr_vlventry != NULL && !use_extension )
    {
        /* This empty entry was handed out last time because the ACL check failed on a VLV Search. */
        /* The empty entry has a pointer to the cache entry dn... make sure we don't free the dn */
        /* which belongs to the cache entry. */
        slapi_entry_free( sr->sr_vlventry );
        sr->sr_vlventry = NULL;
    }

    stoptime = optime + tlimit;
    llimit = sr->sr_lookthroughlimit;

    /* Find the next candidate entry and return it. */
    while ( 1 )
    {

        /* check for abandon */
        if ( slapi_op_abandoned( pb ) || (NULL == sr) )
        {
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );
            if ( use_extension ) {
                slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, NULL );
            }
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, NULL );
            delete_search_result_set(pb, &sr);
            rc = SLAPI_FAIL_GENERAL;
            goto bail;
        }

        /* check time limit */
        curtime = current_time();
        if ( tlimit != -1 && curtime > stoptime )
        {
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );
            if ( use_extension ) {
                slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, NULL );
            } 
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, NULL );
            delete_search_result_set(pb, &sr);
            rc = SLAPI_FAIL_GENERAL;
            slapi_send_ldap_result( pb, LDAP_TIMELIMIT_EXCEEDED, NULL, NULL, nentries, urls );
            goto bail;
        }
            
        /* check lookthrough limit */
        if ( llimit != -1 && sr->sr_lookthroughcount >= llimit )
        {
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );
            if ( use_extension ) {
                slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, NULL );
            } 
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, NULL );
            delete_search_result_set(pb, &sr);
            rc = SLAPI_FAIL_GENERAL;
            slapi_send_ldap_result( pb, LDAP_ADMINLIMIT_EXCEEDED, NULL, NULL, nentries, urls );
            goto bail;
        }
            
        /* get the entry */
        id = idl_iterator_dereference_increment(&(sr->sr_current), sr->sr_candidates);
        if ( id == NOID )
        {
            /* No more entries */
            /* destroy back_search_result_set */
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );
            if ( use_extension ) {
                slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, NULL );
            }
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, NULL );
            delete_search_result_set(pb, &sr);
            rc = 0;
            goto bail;
        }

        ++sr->sr_lookthroughcount;    /* checked above */

        /* get the entry */
        e = id2entry( be, id, &txn, &err );
        if ( e == NULL )
        {
            if ( err != 0 && err != DB_NOTFOUND )
            {
                LDAPDebug( LDAP_DEBUG_ANY, "next_search_entry db err %d\n", err, 0, 0 );
                if (LDBM_OS_ERR_IS_DISKFULL(err))
                {
                    /* disk full in the middle of returning search results
                     * is gonna be traumatic.  unavoidable.
                     */
                    slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
                    rc = return_on_disk_full(li);
                    goto bail;
                }
            }
            LDAPDebug( LDAP_DEBUG_ARGS, "candidate %lu not found\n", (u_long)id, 0, 0 );
            if ( err == DB_NOTFOUND )
            {
                /* Since we didn't really look at this entry, we should
                 * decrement the lookthrough counter (it was just incremented).  
                 * If we didn't do this, it would be possible to go over the 
                 * lookthrough limit when there are fewer entries in the database 
                 * than the lookthrough limit.  This could happen on an ALLIDS
                 * search after adding a bunch of entries and then deleting
                 * them. */
                --sr->sr_lookthroughcount;
            }
            continue;
        }
        e->ep_vlventry = NULL;
        sr->sr_entry = e;

        /*
         * If it's a referral, return it without checking the
         * filter explicitly here since it's only a candidate anyway.  Do
         * check the scope though.
         */
        if ( !managedsait && slapi_entry_attr_find( e->ep_entry, "ref", &attr ) == 0)
        {
            Slapi_Value **refs= attr_get_present_values(attr);
            if ( refs == NULL || refs[0] == NULL )
            {
                LDAPDebug( LDAP_DEBUG_ANY, "null ref in (%s)\n", backentry_get_ndn(e), 0, 0 );
            }
            else if ( slapi_sdn_scope_test( backentry_get_sdn(e), basesdn, scope ))
            {
                if ( use_extension ) {
                    slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, e );
                }
                slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, e->ep_entry );
                rc = 0;
                goto bail;
            }
        }
        else
        {
          /*
           * As per slapi_filter_test:
           * 0  filter matched
           * -1 filter did not match
           * >0 an ldap error code
           */
          int filter_test = -1;
        
          if((slapi_entry_flag_is_set(e->ep_entry,SLAPI_ENTRY_LDAPSUBENTRY) 
             && !filter_flag_is_set(filter,SLAPI_FILTER_LDAPSUBENTRY)) ||
            (slapi_entry_flag_is_set(e->ep_entry,SLAPI_ENTRY_FLAG_TOMBSTONE)
             && ((!isroot && !filter_flag_is_set(filter, SLAPI_FILTER_RUV)) ||
             !filter_flag_is_set(filter, SLAPI_FILTER_TOMBSTONE))))
          {
            /* If the entry is an LDAP subentry and filter don't filter subentries OR 
             * the entry is a TombStone and filter don't filter Tombstone 
             * don't return the entry.  We make a special case to allow a non-root user
             * to search for the RUV entry using a filter of:
             *
             *     "(&(objectclass=nstombstone)(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff))"
             *
             * For this RUV case, we let the ACL check apply.
             */
            /* ugaston - we don't want to mistake this filter failure with the one below due to ACL, 
             * because whereas the former should be read as 'no entry must be returned', the latter
             * might still lead to return an empty entry. */
             filter_test=-1;
          }
          else
          {
            /* it's a regular entry, check if it matches the filter, and passes the ACL check */
             if ( 0 != ( sr->sr_flags & SR_FLAG_CAN_SKIP_FILTER_TEST )) {
                  /* Since we do access control checking in the filter test (?Why?) we need to check access now */
                  LDAPDebug( LDAP_DEBUG_FILTER, "Bypassing filter test\n", 0, 0, 0 );
                  if ( ACL_CHECK_FLAG ) {
                      filter_test = slapi_vattr_filter_test_ext( pb, e->ep_entry, filter, ACL_CHECK_FLAG, 1 /* Only perform access checking, thank you */);
                  } else {
                      filter_test = 0;
                  }
                  if (li->li_filter_bypass_check) {
                      int    ft_rc;
  
                      LDAPDebug( LDAP_DEBUG_FILTER, "Checking bypass\n", 0, 0, 0 );
                      ft_rc = slapi_vattr_filter_test( pb, e->ep_entry, filter,
                              ACL_CHECK_FLAG );
                      if (filter_test != ft_rc) {
                          /* Oops ! This means that we thought we could bypass the filter test, but noooo... */
                          LDAPDebug( LDAP_DEBUG_ANY, "Filter bypass ERROR on entry %s\n", backentry_get_ndn(e), 0, 0 );
                          filter_test = ft_rc; /* Fix the error */
                      }
                  }
              } else {
                  /* Old-style case---we need to do a filter test */
                  filter_test = slapi_vattr_filter_test( pb, e->ep_entry, filter, ACL_CHECK_FLAG);
              }
         }
         if ( (filter_test == 0) || (sr->sr_virtuallistview && (filter_test != -1)) )
            /* ugaston - if filter failed due to subentries or tombstones (filter_test=-1),
             * just forget about it, since we don't want to return anything at all. */
         {
             if ( slapi_uniqueIDCompareString(target_uniqueid, e->ep_entry->e_uniqueid) ||
                  slapi_sdn_scope_test( backentry_get_sdn(e), basesdn, scope ))
             {
                 /* check size limit */
                 if ( slimit >= 0 )
                 {
                     if ( --slimit < 0 ) {
                         CACHE_RETURN( &inst->inst_cache, &e );
                         slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate );
                         delete_search_result_set(pb, &sr);
                         slapi_send_ldap_result( pb, LDAP_SIZELIMIT_EXCEEDED, NULL, NULL, nentries, urls );
                         rc = SLAPI_FAIL_GENERAL;
                         goto bail;
                     }
                     slapi_pblock_set( pb, SLAPI_SEARCH_SIZELIMIT, &slimit );
                     if (op_is_pagedresults(op)) {
                         /* 
                          * On Simple Paged Results search,
                          * sizelimit is appied to each page.
                          */
                         pagedresults_set_sizelimit(conn, op, slimit, pr_idx);
                     }
                     sr->sr_current_sizelimit = slimit;
                 }
                 if ( (filter_test != 0) && sr->sr_virtuallistview)
                 {
                     /* Slapi Filter Test failed.  
                      * Must be that the ACL check failed. 
                      * Send back an empty entry.
                      */
                     sr->sr_vlventry = slapi_entry_alloc();
                     slapi_entry_init(sr->sr_vlventry,slapi_ch_strdup(slapi_entry_get_dn_const(e->ep_entry)),NULL);
                     e->ep_vlventry = sr->sr_vlventry;
                     if ( use_extension ) {
                         slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, e );
                     }
                     slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, sr->sr_vlventry );
                 } else {
                     if ( use_extension ) {
                         slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, e );
                     }
                     slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_ENTRY, e->ep_entry );
                 }
                 rc = 0;
                 goto bail;
             } 
             else 
             {
                 CACHE_RETURN ( &inst->inst_cache, &(sr->sr_entry) );
                 sr->sr_entry = NULL;
             }
          }
          else
          {
              /* Failed the filter test, and this isn't a VLV Search */
              CACHE_RETURN( &inst->inst_cache, &(sr->sr_entry) );
              sr->sr_entry = NULL;
              if (LDAP_UNWILLING_TO_PERFORM == filter_test) {
                  /* Need to catch this error to detect the vattr loop */
                  slapi_send_ldap_result( pb, filter_test, NULL,
                                  "Failed the filter test", 0, NULL );
                  rc = SLAPI_FAIL_GENERAL;
                  goto bail;
              } else if (LDAP_TIMELIMIT_EXCEEDED == filter_test) {
                  slapi_send_ldap_result( pb, LDAP_TIMELIMIT_EXCEEDED, NULL, NULL, nentries, urls );
                  rc = SLAPI_FAIL_GENERAL;
                  goto bail;
              }
          }
        }
    }
bail:
    return rc;
}

/*
 * Move back the current position in the search result set by one.
 * Paged Results needs to read ahead one entry to catch the end of the search
 * result set at the last entry not to show the prompt when there is no more
 * entries.
 */
void
ldbm_back_prev_search_results( Slapi_PBlock *pb )
{
    backend *be;
    ldbm_instance *inst;
    back_search_result_set *sr;

    slapi_pblock_get( pb, SLAPI_BACKEND, &be );
    if (!be) {
        LDAPDebug0Args(LDAP_DEBUG_ANY,
                       "ldbm_back_prev_search_results: no backend\n");
        return;
    }
    inst = (ldbm_instance *) be->be_instance_info;
    if (!inst) {
        LDAPDebug0Args(LDAP_DEBUG_ANY,
                       "ldbm_back_prev_search_results: no backend instance\n");
        return;
    }
    slapi_pblock_get( pb, SLAPI_SEARCH_RESULT_SET, &sr );
    if (sr) {
        if (sr->sr_entry) {
            /* The last entry should be returned to cache */
            LDAPDebug1Arg(LDAP_DEBUG_BACKLDBM,
                          "ldbm_back_prev_search_results: returning: %s\n",
                          slapi_entry_get_dn_const(sr->sr_entry->ep_entry));
            CACHE_RETURN (&inst->inst_cache, &(sr->sr_entry));
            sr->sr_entry = NULL;
        }
        idl_iterator_decrement(&(sr->sr_current));
    }
    return;
}

static back_search_result_set*
new_search_result_set(IDList *idl, int vlv, int lookthroughlimit)
{
    back_search_result_set *p = (back_search_result_set *)slapi_ch_calloc( 1, sizeof( back_search_result_set ));
    p->sr_candidates = idl;
    p->sr_current = idl_iterator_init(idl);
    p->sr_lookthroughlimit = lookthroughlimit;
    p->sr_virtuallistview = vlv;
    p->sr_current_sizelimit = -1;
    return p;
}

static void
delete_search_result_set( Slapi_PBlock *pb, back_search_result_set **sr )
{
    int rc = 0, filt_errs = 0;
    if ( NULL == sr || NULL == *sr )
    {
        return;
    }
    if (pb) {
        if (op_is_pagedresults(pb->pb_op)) {
            /* If the op is pagedresults, let the module clean up sr. */
            return;
        }
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
    }
    if ( NULL != (*sr)->sr_candidates )
    {
        idl_free( (*sr)->sr_candidates );
    }
    rc = slapi_filter_apply((*sr)->sr_norm_filter, ldbm_search_free_compiled_filter,
                            NULL, &filt_errs);
    if (rc != SLAPI_FILTER_SCAN_NOMORE) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ERROR: could not free the pre-compiled regexes in the search filter - error %d %d\n",
                       rc, filt_errs);
    }
    slapi_filter_free((*sr)->sr_norm_filter, 1);
    memset( *sr, 0, sizeof( back_search_result_set ) );
    slapi_ch_free( (void**)sr );
    return;
}

/*
 * This function is called from pagedresults free/cleanup functions.
 */ 
void
ldbm_back_search_results_release( void **sr )
{
    /* passing NULL pb forces to delete the search result set */
    delete_search_result_set( NULL, (back_search_result_set **)sr );
}

int
ldbm_back_entry_release( Slapi_PBlock *pb, void *backend_info_ptr ) {
    backend *be;
    ldbm_instance *inst;

    if ( backend_info_ptr == NULL )
        return 1;

    slapi_pblock_get( pb, SLAPI_BACKEND, &be );
        inst = (ldbm_instance *) be->be_instance_info;

    CACHE_RETURN( &inst->inst_cache, (struct backentry **)&backend_info_ptr );

    if( ((struct backentry *) backend_info_ptr)->ep_vlventry != NULL )
    {
        /* This entry was created during a vlv search whose acl check failed.  It needs to be
         * freed here */
        slapi_entry_free( ((struct backentry *) backend_info_ptr)->ep_vlventry );
        ((struct backentry *) backend_info_ptr)->ep_vlventry = NULL;
    }
    return 0;
}
