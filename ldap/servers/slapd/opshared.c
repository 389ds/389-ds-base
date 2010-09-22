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

/* opshared.c - functions shared between regular and internal operations */

#include "slap.h"
#include "index_subsys.h"

#define PAGEDRESULTS_PAGE_END 1
#define PAGEDRESULTS_SEARCH_END 2

/* helper functions */
static void compute_limits (Slapi_PBlock *pb);

/* attributes that no clients are allowed to add or modify */
static char *protected_attrs_all [] = {    PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
                                        NULL
                                      };
static char *pwpolicy_lock_attrs_all [] = { "passwordRetryCount",
                                            "retryCountResetTime",
                                            "accountUnlockTime",
                                            NULL};
/* Forward declarations */
static void compute_limits (Slapi_PBlock *pb);
static int  send_results_ext (Slapi_PBlock *pb, int send_result, int *nentries, int pagesize, unsigned int *pr_stat);
static int process_entry(Slapi_PBlock *pb, Slapi_Entry *e, int send_result);
            
int op_shared_is_allowed_attr (const char *attr_name, int replicated_op)
{
    int                 i;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* check list of attributes that no client is allowed to specify */
    for (i = 0; protected_attrs_all[i]; i ++)
    {
        if (strcasecmp (attr_name, protected_attrs_all[i]) == 0)
        {
            /* this attribute is not allowed */
            return 0;
        }
    }

    /* ONREPL - should allow backends to plugin here to specify 
                attributes that are not allowed */

    if (!replicated_op)
    {
        /*
         * check to see if attribute is marked as one clients can't modify
         */
        struct asyntaxinfo    *asi;
        int                    no_user_mod = 0;

        asi = attr_syntax_get_by_name( attr_name );
        if ( NULL != asi &&
                0 != ( asi->asi_flags & SLAPI_ATTR_FLAG_NOUSERMOD ))
        {
            /* this attribute is not allowed */
            no_user_mod = 1;
        }
        attr_syntax_return( asi );

        if ( no_user_mod ) {
            return( 0 );
        }
    } else if (!slapdFrontendConfig->pw_is_global_policy) {
        /* check list of password policy attributes for locking accounts */
        for (i = 0; pwpolicy_lock_attrs_all[i]; i ++)
        {
            if (strcasecmp (attr_name, pwpolicy_lock_attrs_all[i]) == 0)
            {
                /* this attribute is not allowed */
                return 0;
            }
        }
    }

    /* this attribute is ok */
    return 1;
}


static ps_service_fn_ptr ps_service_fn = NULL;

void
do_ps_service(Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype, ber_int_t chgnum)
{
    if (NULL == ps_service_fn) {
        if (get_entry_point(ENTRY_POINT_PS_SERVICE, (caddr_t *)(&ps_service_fn)) < 0) {
            return;
        }
    }
    (ps_service_fn)(e, eprev, chgtype, chgnum);
}

void modify_update_last_modified_attr(Slapi_PBlock *pb, Slapi_Mods *smods)
{
    char        buf[20];
    struct berval    bv;
    struct berval    *bvals[2];
    time_t        curtime;
    struct tm    utm;
    Operation    *op;

    LDAPDebug(LDAP_DEBUG_TRACE, "modify_update_last_modified_attr\n", 0, 0, 0);

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    bvals[0] = &bv;
    bvals[1] = NULL;

    /* fill in modifiersname */
    if (slapi_sdn_isempty(&op->o_sdn)) {
        bv.bv_val = "";
        bv.bv_len = strlen(bv.bv_val);
    } else {
        bv.bv_val = (char*)slapi_sdn_get_dn(&op->o_sdn);
        bv.bv_len = strlen(bv.bv_val);
    }

    slapi_mods_add_modbvps(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, 
                           "modifiersname", bvals);    

    /* fill in modifytimestamp */
    curtime = current_time();
#ifdef _WIN32
{
    struct tm *pt;
    pt = gmtime(&curtime);
    memcpy(&utm, pt, sizeof(struct tm));
}
#else
    gmtime_r(&curtime, &utm);
#endif
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm);
    bv.bv_val = buf;
    bv.bv_len = strlen(bv.bv_val);
    slapi_mods_add_modbvps(smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES, 
                           "modifytimestamp", bvals);
}

/*
 * Returns: 0    - if the operation is successful
 *        < 0    - if operation fails. 
 * Note that an operation is considered "failed" if a result is sent 
 * directly to the client when send_result is 0.
 */
void
op_shared_search (Slapi_PBlock *pb, int send_result)
{
  char            *base, *fstr;
  int             scope;
  Slapi_Backend   *be = NULL;
  Slapi_Backend   *be_single = NULL;
  Slapi_Backend   *be_list[BE_LIST_SIZE];
  Slapi_Entry     *referral_list[BE_LIST_SIZE];
  char            ebuf[ BUFSIZ ];
  char            attrlistbuf[ 1024 ], *attrliststr, **attrs = NULL;
  int             rc = 0;
  int             internal_op;
  Slapi_DN        sdn;
  Slapi_Operation *operation;
  Slapi_Entry     *referral = NULL;
 
  char            errorbuf[BUFSIZ];
  int             nentries,pnentries;
  int             flag_search_base_found = 0;
  int             flag_no_such_object = 0;
  int             flag_referral = 0;
  int             flag_psearch = 0;
  int             err_code = LDAP_SUCCESS;
  LDAPControl     **ctrlp;
  struct berval   *ctl_value = NULL;
  int             iscritical = 0;
  char            *be_name = NULL;
  int             index = 0;
  int             sent_result = 0;
  unsigned int    pr_stat = 0;

  ber_int_t pagesize = -1;
  int curr_search_count = 0;
  Slapi_Backend *pr_be = NULL;
  void *pr_search_result = NULL;
  int pr_search_result_count = 0;

  be_list[0] = NULL;
  referral_list[0] = NULL;

  /* get search parameters */
  slapi_pblock_get(pb, SLAPI_SEARCH_TARGET, &base);
  slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
  slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &fstr);   
  slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);   
  slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
  internal_op= operation_is_flag_set(operation, OP_FLAG_INTERNAL);
  flag_psearch = operation_is_flag_set(operation, OP_FLAG_PS);
  
  slapi_sdn_init_dn_byref(&sdn, base);
 
  if (operation_is_flag_set(operation,OP_FLAG_ACTION_LOG_ACCESS))
  {
      char *fmtstr;
        
#define SLAPD_SEARCH_FMTSTR_BASE "conn=%" NSPRIu64 " op=%d SRCH base=\"%s\" scope=%d "
#define SLAPD_SEARCH_FMTSTR_BASE_INT "conn=%s op=%d SRCH base=\"%s\" scope=%d "
#define SLAPD_SEARCH_FMTSTR_REMAINDER " attrs=%s%s\n"

      PR_ASSERT(fstr);
      if ( strlen(fstr) > 1024 )
      {
          /*
           * slapi_log_access() throws away log lines that are longer than
           * 2048 characters, so we limit the filter string to 1024 (better
           * to log something rather than nothing)
           */
          if ( !internal_op )
          {
              fmtstr = SLAPD_SEARCH_FMTSTR_BASE "filter=\"%.1024s...\"" SLAPD_SEARCH_FMTSTR_REMAINDER;
          }
          else
          {
              fmtstr = SLAPD_SEARCH_FMTSTR_BASE_INT "filter=\"%.1024s...\"" SLAPD_SEARCH_FMTSTR_REMAINDER;
          }
      } else {
          if ( !internal_op )
          {
              fmtstr = SLAPD_SEARCH_FMTSTR_BASE "filter=\"%s\"" SLAPD_SEARCH_FMTSTR_REMAINDER;
          }
          else
          {
              fmtstr = SLAPD_SEARCH_FMTSTR_BASE_INT "filter=\"%s\"" SLAPD_SEARCH_FMTSTR_REMAINDER;
          }
      }
      
      if ( NULL == attrs ) {
          attrliststr = "ALL";
      } else {
          strarray2str( attrs, attrlistbuf, sizeof( attrlistbuf ),
                        1 /* include quotes */ );
          attrliststr = attrlistbuf;
      }
      
      if ( !internal_op )
      {
          slapi_log_access(LDAP_DEBUG_STATS, fmtstr,
                           pb->pb_conn->c_connid, 
                           pb->pb_op->o_opid, 
                           escape_string(slapi_sdn_get_dn (&sdn), ebuf),
                           scope, fstr, attrliststr,
                           flag_psearch ? " options=persistent" : "");
      }
      else
      {
          slapi_log_access(LDAP_DEBUG_ARGS, fmtstr,
                           LOG_INTERNAL_OP_CON_ID,
                           LOG_INTERNAL_OP_OP_ID,
                           escape_string(slapi_sdn_get_dn (&sdn), ebuf),
                           scope, fstr, attrliststr,
                           flag_psearch ? " options=persistent" : "");
      }
  }
        
  slapi_pblock_set(pb, SLAPI_SEARCH_TARGET, (void*)slapi_sdn_get_ndn (&sdn));

  /* target spec is used to decide which plugins are applicable for the operation */
  operation_set_target_spec (pb->pb_op, &sdn);

  /* this is time to check if mapping tree specific control
   * was used to specify that we want to parse only 
   * one backend 
   */
  slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
  if (NULL != ctrlp)
  {
      if (slapi_control_present(ctrlp, MTN_CONTROL_USE_ONE_BACKEND_EXT_OID,
                &ctl_value, &iscritical))
      {
        /* this control is the smart version of MTN_CONTROL_USE_ONE_BACKEND_OID,
         * it works out for itself what back end is required (thereby relieving
         * the client of working out which backend it needs) by looking at the
         * base of the search if no value is supplied
         */
    
        if((ctl_value->bv_len != 0) && ctl_value->bv_val)
        {
            be_name = ctl_value->bv_val;
        }
        else
        {
            /* we don't need no steenkin values */
            Slapi_Backend *searchbe = slapi_be_select( &sdn );

            if(searchbe && searchbe != defbackend_get_backend())
            {
                be_name = slapi_be_get_name(searchbe);
            }
        }
      }
      else
      {
          if (slapi_control_present(ctrlp, MTN_CONTROL_USE_ONE_BACKEND_OID,
                    &ctl_value, &iscritical))
          {
              if ((ctl_value->bv_len == 0) || (ctl_value->bv_val == NULL))
              {
                  rc = -1;
                  send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
                  goto free_and_return_nolock;
              }
              else
              {
                  be_name = ctl_value->bv_val;
                  if (be_name == NULL)
                  {
                      rc = -1;
                      send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
                      goto free_and_return_nolock;
                  }
              }
          }
      }

      if ( slapi_control_present (ctrlp, LDAP_CONTROL_GET_EFFECTIVE_RIGHTS,
                                  &ctl_value, &iscritical) )
      {
          operation->o_flags |= OP_FLAG_GET_EFFECTIVE_RIGHTS;
      }

      if ( slapi_control_present (ctrlp, LDAP_CONTROL_PAGEDRESULTS,
                                  &ctl_value, &iscritical) )
      {
          rc = pagedresults_parse_control_value(ctl_value,
                                               &pagesize, &curr_search_count);
          if (LDAP_SUCCESS == rc) {
              operation->o_flags |= OP_FLAG_PAGED_RESULTS;
              pr_be = pagedresults_get_current_be(pb->pb_conn);
              pr_search_result = pagedresults_get_search_result(pb->pb_conn);
              pr_search_result_count =
                             pagedresults_get_search_result_count(pb->pb_conn);
          } else {
              /* parse paged-results-control failed */
              if (iscritical) { /* return an error since it's critical */
                  goto free_and_return;
              }
          }
      }
  }

  if (be_name == NULL)
  {
    /* no specific backend was requested, use the mapping tree
     */
    err_code = slapi_mapping_tree_select_all(pb, be_list, referral_list, errorbuf);
    if (((err_code != LDAP_SUCCESS) && (err_code != LDAP_OPERATIONS_ERROR) && (err_code != LDAP_REFERRAL))
      || ((err_code == LDAP_OPERATIONS_ERROR) && ((be_list == NULL) || be_list[0] == NULL)))
    
    {
      send_ldap_result(pb, err_code, NULL, errorbuf, 0, NULL);
      rc = -1;
      goto free_and_return;
    }
    if (be_list[0] != NULL)
    {
      index = 0;
      if (pr_be) { /* PAGED RESULT: be is found from the previous paging. */
        /* move the index in the be_list which matches pr_be */
        while (be_list[index] && be_list[index+1] && pr_be != be_list[index])
          index++;
      } else {
        while (be_list[index] && be_list[index+1])
          index++;
      }
      /* "be" is either pr_be or the last backend */
      be = be_list[index];
    }
    else
      be = pr_be?pr_be:NULL;
  }
  else
  {
      /* specific backend be_name was requested, use slapi_be_select_by_instance_name
       */
      if (pr_be) {
        be_single = be = pr_be;
      } else {
        be_single = be = slapi_be_select_by_instance_name(be_name);
      }
      if (be_single)
        slapi_be_Rlock(be_single);
      be_list[0] = NULL;
      referral_list[0] = NULL;
      referral = NULL;
  }

  slapi_pblock_set(pb, SLAPI_BACKEND_COUNT, &index);

  if (be)
  {
      slapi_pblock_set(pb, SLAPI_BACKEND, be);

      /* adjust time and size limits */
      compute_limits (pb);
    
      /* call the pre-search plugins. if they succeed, call the backend 
     search function. then call the post-search plugins. */

      /* ONREPL - should regular plugin be called for internal searches ? */
      if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_SEARCH_FN) == 0)
      {
        slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
        set_db_default_result_handlers(pb);

        /* Now would be a good time to call the search rewriters for computed attrs */
        rc = compute_rewrite_search_filter(pb);
        switch (rc) 
        {
        case 1: /* A rewriter says that we should refuse to perform this search.
               The potential exists that we will refuse to perform a search 
               which we were going to refer, perhaps to a server which would
               be willing to perform the search. That's bad. The rewriter 
               could be clever enough to spot this and do the right thing though. */
          send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Search not supported", 0, NULL);
          rc = -1;
          goto free_and_return;

        case -2: /* memory was allocated */
            /* take note of any changes */
          slapi_pblock_get(pb, SLAPI_SEARCH_TARGET, &base);
          slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);

          slapi_sdn_set_dn_byref(&sdn, base);
          break;

        case -1:
        case 0: /* OK */
          break;
        
        case 2: /* Operations error */
          send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "search rewriter", 0, NULL);
          rc = -1;
          goto free_and_return;
        }

    } else {
        /*
         * A pre-operation plugin handled this search. Grab the return code
         * (it may have been set by a plugin) and return.
         *
         * In DS 5.x, the following two lines of code did not exist, which
         * means a pre-search function might return a non-zero value (which
         * indicates that a result was returned to the client) but the code
          * below would execute the search anyway. This was a regression from
         * the documented plugin API behavior (and from DS 3.x and 4.x).
         */
        slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        goto free_and_return;
    }
  }

  /* set the timelimit to clean up the too-long-lived-paged results requests */
  if (operation->o_flags & OP_FLAG_PAGED_RESULTS) {
    time_t optime, time_up;
    int tlimit;
    slapi_pblock_get( pb, SLAPI_SEARCH_TIMELIMIT, &tlimit );
    slapi_pblock_get( pb, SLAPI_OPINITIATED_TIME, &optime );
    time_up = (tlimit==-1 ? -1 : optime + tlimit); /* -1: no time limit */
    pagedresults_set_timelimit(pb->pb_conn, time_up);
  }

  /* PAR: now filters have been rewritten, we can assign plugins to work on them */
  index_subsys_assign_filter_decoders(pb);

  nentries = 0;
  rc = -1;            /* zero backends would mean failure */
  while (be) 
  {
    const Slapi_DN * be_suffix;
    int err = 0;
    Slapi_Backend   *next_be = NULL;

    if (be->be_search == NULL)
    {
      send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Function not implemented", 0, NULL);
      rc = -1;
      goto free_and_return;
    }

    pnentries = 0;

    /* the backends returns no such object when a 
     * search is attempted in a node above their nsslapd-suffix
     * this is correct but a bit annoying when a backends
     * is below another backend because in that case the 
     * such searches should sometimes succeed 
     * To allow this we therefore have to change the 
     * SLAPI_SEARCH_TARGET parameter in the pblock
     * 
     * Also when we climb down the mapping tree we have to 
     * change ONE-LEVEL searches to BASE 
     */

    /* that's mean we only support one suffix per backend */
    be_suffix = slapi_be_getsuffix(be, 0);

    if (be_list[0] == NULL)
    {
      next_be = NULL;
    }
    else
    {
      index--;
      if (index>=0)
        next_be = be_list[index];
      else
        next_be = NULL;
    }
        
    if ((operation->o_flags & OP_FLAG_PAGED_RESULTS) && pr_search_result) {
      void *sr = NULL;
      /* PAGED RESULTS and already have the search results from the prev op */
      slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET, pr_search_result );
      rc = send_results_ext (pb, 1, &pnentries, pagesize, &pr_stat);

      /* search result could be reset in the backend/dse */
      slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);
      pagedresults_set_search_result(pb->pb_conn, sr);

      if (PAGEDRESULTS_SEARCH_END == pr_stat) {
        /* no more entries to send in the backend */
        if (NULL == next_be) {
          /* no more entries && no more backends */
          curr_search_count = -1;
        } else {
          curr_search_count = pnentries;
        }
      } else {
        curr_search_count = pnentries;
      }
      pagedresults_set_response_control(pb, 0, pagesize, curr_search_count);
      if (pagedresults_get_with_sort(pb->pb_conn)) {
        sort_make_sort_response_control(pb, CONN_GET_SORT_RESULT_CODE, NULL);
      }
      next_be = NULL; /* to break the loop */
    } else {
      /* be_suffix null means that we are searching the default backend
       * -> don't change the search parameters in pblock
       */
      if (be_suffix != NULL)
      {
        if ((be_name == NULL) && (scope == LDAP_SCOPE_ONELEVEL))
        {
                  /* one level searches 
                   * - depending on the suffix of the backend we might have to
                   *   do a one level search or a base search
                   * - we might also have to change the search target 
                   */
          if (slapi_sdn_isparent(&sdn, be_suffix)
              || (slapi_sdn_get_ndn_len(&sdn) == 0))
          {
            int tmp_scope = LDAP_SCOPE_BASE;
            slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &tmp_scope);
            slapi_pblock_set(pb, SLAPI_SEARCH_TARGET,
                     (void *)slapi_sdn_get_ndn(be_suffix));
          }
          else if (slapi_sdn_issuffix(&sdn, be_suffix))
          {
            int tmp_scope = LDAP_SCOPE_ONELEVEL;
            slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &tmp_scope);
            slapi_pblock_set(pb, SLAPI_SEARCH_TARGET,
                     (void *)slapi_sdn_get_ndn (&sdn));
          }
          else
            goto next_be;
        }
      
        /* subtree searches :
         * if the search was started above the backend suffix 
         * - temporarily set the SLAPI_SEARCH_TARGET to the 
         *   base of the node so that we don't get a NO SUCH OBJECT error
         * - do not change the scope
         */
        if (scope == LDAP_SCOPE_SUBTREE)
        {
          if (slapi_sdn_issuffix(be_suffix, &sdn))
          {
            slapi_pblock_set(pb, SLAPI_SEARCH_TARGET,
                     (void *)slapi_sdn_get_ndn(be_suffix));
          }
          else
              slapi_pblock_set(pb, SLAPI_SEARCH_TARGET, (void *)slapi_sdn_get_ndn(&sdn));
        }
      }
      
      slapi_pblock_set(pb, SLAPI_BACKEND, be);
      slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
      slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
      
      /* ONREPL - we need to be able to tell the backend not to send results directly */
      rc = (*be->be_search)(pb);
      switch (rc)
      {
      case 1:
        /* if the backend returned LDAP_NO_SUCH_OBJECT for a SEARCH request,
         * it will not have sent back a result - otherwise, it will have
         * sent a result */
        rc = SLAPI_FAIL_GENERAL;
        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &err);
        if (err == LDAP_NO_SUCH_OBJECT)
        {
            /* may be the object exist somewhere else
             * wait the end of the loop to send back this error 
             */
            flag_no_such_object = 1;
            break;
        }
        /* err something other than LDAP_NO_SUCH_OBJECT, so the backend will
         * have sent the result -
         * Set a flag here so we don't return another result. */
        sent_result = 1;
        /* fall through */
  
      case -1:    /* an error occurred */            
        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &err);
        if (err == LDAP_NO_SUCH_OBJECT)
        {
            /* may be the object exist somewhere else
             * wait the end of the loop to send back this error 
             */
            flag_no_such_object = 1;
            break;
        }
        else
        {
            /* for error other than LDAP_NO_SUCH_OBJECT
             * the error has already been sent
             * stop the search here
             */
            goto free_and_return;
        }
  
        /* when rc == SLAPI_FAIL_DISKFULL this case is executed */ 
  
      case SLAPI_FAIL_DISKFULL: 
        operation_out_of_disk_space(); 
        goto free_and_return; 
                          
      case 0:        /* search was successful and we need to send the result */
        flag_search_base_found++;
        rc = send_results_ext (pb, 1, &pnentries, pagesize, &pr_stat);
  
        /* PAGED RESULTS */
        if (operation->o_flags & OP_FLAG_PAGED_RESULTS) {
            void *sr = NULL;
            int with_sort = operation->o_flags & OP_FLAG_SERVER_SIDE_SORTING;
  
            curr_search_count = pnentries;
            if (PAGEDRESULTS_SEARCH_END == pr_stat) {
              if (NULL == next_be) {
                /* no more entries && no more backends */
                curr_search_count = -1;
              } else {
                /* no more entries, but at least another backend */
                if (pagedresults_set_current_be(pb->pb_conn, next_be) < 0) {
                  goto free_and_return;
                }
              }
            } else {
              curr_search_count = pnentries;
              slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);
              if (pagedresults_set_current_be(pb->pb_conn, be) < 0 ||
                  pagedresults_set_search_result(pb->pb_conn, sr) < 0 ||
                  pagedresults_set_search_result_count(pb->pb_conn,
                                                   curr_search_count) < 0 ||
                  pagedresults_set_with_sort(pb->pb_conn, with_sort) < 0) {
                goto free_and_return;
              }
            }
            pagedresults_set_response_control(pb, 0,
                                              pagesize, curr_search_count);
            slapi_pblock_set( pb, SLAPI_SEARCH_RESULT_SET, NULL );
            next_be = NULL; /* to break the loop */
        }
  
        /* if rc != 0 an error occurred while sending back the entries
         * to the LDAP client
         * LDAP error should already have been sent to the client
         * stop the search, free and return
         */
        if (rc != 0)     
          goto free_and_return;
        break;
      }
    }
            
    nentries += pnentries;

next_be:
    be = next_be; /* this be won't be used for PAGED_RESULTS */
  }

  /* if referrals were sent back by the mapping tree
   * add them to the list of referral in the pblock instead
   * of searching the backend
   */
  index = 0;
  while ((referral = referral_list[index++]) != NULL)
  {
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    if (err_code == LDAP_REFERRAL)
    {
      send_referrals_from_entry(pb,referral);
      goto free_and_return;
    }
    else
    {
      if (process_entry(pb, referral, 1))
      {
          flag_referral++;
      }
      else
      {
          /* Manage DSA was set, referral must be sent as an entry */
          int attrsonly;
          char **attrs = NULL;    

          slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
          slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
          slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, referral);
          switch (send_ldap_search_entry(pb, referral, NULL, attrs, attrsonly)) 
          {
          case 0:
            flag_search_base_found++;
            nentries++;
            break;
          case 1:        /* entry not sent */
          case -1:       /* connection closed */
            break;
          }
      }
    }
  }

  if (flag_search_base_found || flag_referral) {
    rc = 0;
  }

  /* ONREPL - we currently call postop only if operation is successful;
     We should always send result and pass error code to the plugin */
  if (rc == 0) {
      plugin_call_plugins(pb, SLAPI_PLUGIN_POST_SEARCH_FN);
  }
  else
  {
      plugin_call_plugins(pb, SLAPI_PLUGIN_POST_SEARCH_FAIL_FN);
  }

  if (send_result) {
    if (rc == 0)
    {         
      /* at least one backend returned something and there was no critical error
       * from the LDAP client point of view the search was successful
       */
      struct berval **urls = NULL;
    
      slapi_pblock_get(pb, SLAPI_SEARCH_REFERRALS, &urls);
      send_ldap_result(pb, err_code, NULL, NULL, nentries, urls);
    }
    else if (flag_no_such_object)
    {
      /* there was at least 1 backend that was called to process 
       * the operation and all backends returned NO SUCH OBJECTS.
       * Don't send the result if it's already been handled above.
       */
      if (!sent_result) {
        slapi_send_ldap_result_from_pb(pb);
      }
    }
    else
    {
      /* No backend was found in the mapping tree to process
       * the operation : return NO SUCH OBJECT
       */
      send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL);
    }
  } else {
    /* persistent search: ignore error locating base entry */
    rc = 0;
  }

 free_and_return:
  if ((be_list[0] != NULL) || (referral_list[0] != NULL))
    slapi_mapping_tree_free_all(be_list, referral_list);
  else if (be_single)
    slapi_be_Unlock(be_single);

 free_and_return_nolock:
  slapi_pblock_set(pb, SLAPI_SEARCH_TARGET, base);
  slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
  index_subsys_filter_decoders_done(pb);
  slapi_sdn_done(&sdn);
}

/* Returns 1 if this processing on this entry is finished
 * and doesn't need to be sent.
 */
static int
process_entry(Slapi_PBlock *pb, Slapi_Entry *e, int send_result)
{
    int managedsait;
    Slapi_Attr *a=NULL;
    int numValues=0, i;

    if (!send_result) 
    {
        /* server requested that we don't send results to the client,
           for instance, in case of a persistent search
         */
        return 1;
    }
    
   /* ONREPL - check if the entry should be referred (because of the copyingFrom) */
    
    /*
     * If this is a referral, and the managedsait control is not present,
     * arrange for a referral to be sent.  For v2 connections,
     * the referrals are just squirreled away and sent with the
     * final result.  For v3, the referrals are sent in separate LDAP messages.
     */
    slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
    if (!managedsait && slapi_entry_attr_find(e, "ref", &a)== 0)
    {
        /* to fix 522189: when rootDSE, don't interpret attribute ref as a referral entry  */

        if ( slapi_is_rootdse(slapi_entry_get_dn_const(e)) )
            return 0;    /* more to do for this entry, e.g., send it back to the client */

        /* end fix */
        slapi_attr_get_numvalues(a, &numValues );
        if (numValues == 0) 
        {
            char ebuf[ BUFSIZ ];
            LDAPDebug(LDAP_DEBUG_ANY, "null ref in (%s)\n",
                      escape_string(slapi_entry_get_dn_const(e), ebuf), 0, 0);
        }
        else 
        {
            Slapi_Value *val=NULL;
            struct berval **refscopy=NULL;
            struct berval **urls, **tmpUrls=NULL;
            tmpUrls=(struct berval **) slapi_ch_malloc((numValues + 1) * sizeof(struct berval*));
            for ( i = slapi_attr_first_value(a, &val); i != -1;
                 i = slapi_attr_next_value(a, i, &val)) {
                tmpUrls[i]=(struct berval*)slapi_value_get_berval(val);
            }
            tmpUrls[numValues]=NULL;
            refscopy = ref_adjust(pb, tmpUrls, slapi_entry_get_sdn_const(e), 1);
            slapi_pblock_get(pb, SLAPI_SEARCH_REFERRALS, &urls);
            send_ldap_referral(pb, e, refscopy, &urls);
            slapi_pblock_set(pb, SLAPI_SEARCH_REFERRALS, urls);
            if (NULL != refscopy) 
            {
                ber_bvecfree(refscopy);
                refscopy = NULL;
            }
            if( NULL != tmpUrls) {
                slapi_ch_free( (void **)&tmpUrls );
            }
        }

        return 1;        /* done with this entry */
    } 

    return 0;
}

#if 0
/* Loops through search entries and sends them to the client. 
 * returns -1 on error, 0 if result packet was sent or 1 if
 * result packet wasn't sent
 */
static int
iterate_with_lookahead(Slapi_PBlock *pb, Slapi_Backend *be, int send_result, int *pnentries) 
{
    int rc;
    int attrsonly;
    int done = 0;
    Slapi_Entry *e;
    void *backend_info_ptr;
    Slapi_Entry *next_e;
    void *next_backend_info_ptr;
    char **attrs = NULL;    
    int send_result_status = 0;

    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);

    /* setup for the loop */
    rc = be->be_next_search_entry_ext(pb, 1);
    if (rc < 0) 
    {
        /*
         * Some exceptional condition occurred.  Results
         * have been sent, so we're finished.
         */
        if (rc == SLAPI_FAIL_DISKFULL) 
        {
            operation_out_of_disk_space();
        }
        return -1;
    } 
        
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &next_e);
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, &next_backend_info_ptr);
    if (NULL == next_e) 
    {
        /* no entries */
        done = 1;
    }

    backend_info_ptr = NULL;

    /* Done setting up the loop, now here it comes */
    
    while (!done) 
    {
        /* Allow the backend to free the entry we just finished using */
        /* It is ok to call this when backend_info_ptr is NULL */
        be->be_entry_release(pb, backend_info_ptr);
        e = next_e;
        backend_info_ptr = next_backend_info_ptr;

        rc = be->be_next_search_entry_ext(pb, 1);
        if (rc < 0) 
        {
            /*
             * Some exceptional condition occurred.  Results
             * have been sent, so we're finished.
             */
            if (rc == SLAPI_FAIL_DISKFULL) 
            {
                operation_out_of_disk_space();
            }
            return -1;
        } 
        else 
        {
            slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &next_e);
            slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY_EXT, &next_backend_info_ptr);
            if (next_e == NULL) 
            {
                /* no more entries */
                done = 1;
            }
        }

        if (process_entry(pb, e, send_result)) 
        {
            /* shouldn't  send this entry */
            continue;
        }

        /*
         * It's a regular entry, or it's a referral and
         * managedsait control is on.  In either case, send the entry.
         */
        if (done) 
        {
            struct berval **urls = NULL;
            /* Send the entry and the result at the same time */
            slapi_pblock_get(pb, SLAPI_SEARCH_REFERRALS, &urls);
            rc = send_ldap_search_entry_ext(pb, e, NULL, attrs, attrsonly, 1, 
                                            (*pnentries)+1, urls);
            if (rc == 1) 
            {
                /* this means we didn't have access to the entry. Since the
                 * entry was not sent, we need to send the done packet.
                 */
                send_result_status = 1;
            }
        } 
        else 
        {
            /* Send the entry */
            rc = send_ldap_search_entry(pb, e, NULL, attrs, 
                        attrsonly);
        }
        switch (rc) 
        {
            case 0:        /* entry sent ok */
                (*pnentries)++;
                slapi_pblock_set(pb, SLAPI_NENTRIES, pnentries);
                break;
            case 1:        /* entry not sent */
                break;
            case -1:    /* connection closed */
                /*
                 * mark the operation as abandoned so the backend
                 * next entry function gets called again and has
                 * a chance to clean things up.
                 */
                pb->pb_op->o_status = SLAPI_OP_STATUS_ABANDONED;
                break;
        }
    }

    be->be_entry_release(pb, backend_info_ptr);
    if (*pnentries == 0 || send_result_status) 
    {
        /* We didn't send the result done message so the caller 
         * must send it */
        return 1;
    } 
    else 
    {
        /* The result message has been sent */
        return 0;
    }
}
#endif

/* Loops through search entries and sends them to the client. 
 * returns -1 on error or 1 if result packet wasn't sent.
 * This function never returns 0 because it doesn't send 
 * the result packet back with the last entry like 
 * iterate_with_lookahead trys to do.
 */
static int
iterate(Slapi_PBlock *pb, Slapi_Backend *be, int send_result,
        int *pnentries, int pagesize, unsigned int *pr_statp) 
{
    int rc;
    int rval = 1; /* no error, by default */
    int attrsonly;
    int done = 0;
    Slapi_Entry *e = NULL;
    char **attrs = NULL;
    unsigned int pr_stat = 0;

    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly);

    *pnentries = 0;
    
    while (!done) 
    {
        Slapi_Entry *gerentry = NULL;
        Slapi_Operation *operation;

        rc = be->be_next_search_entry(pb);
        if (rc < 0) 
        {
            /*
             * Some exceptional condition occurred. Results have been sent,
             * so we're finished.
             */
            if (rc == SLAPI_FAIL_DISKFULL) 
            {
                operation_out_of_disk_space();
            }
            pr_stat = PAGEDRESULTS_SEARCH_END;
            pagedresults_set_timelimit(pb->pb_conn, 0);
            rval = -1;
            done = 1;
            continue;
        } 

        slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &e);

        /* Check for possible get_effective_rights control */
        slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
        if ( operation->o_flags & OP_FLAG_GET_EFFECTIVE_RIGHTS )
        {
            char *errbuf = NULL;
            char **gerattrs = NULL;
            char **gerattrsdup = NULL;
            char **gap = NULL;
            char *gapnext = NULL;

            slapi_pblock_get( pb, SLAPI_SEARCH_GERATTRS, &gerattrs );

            gerattrsdup = cool_charray_dup(gerattrs);
            gap = gerattrsdup;
            do
            {
                gapnext = NULL;
                if (gap)
                {
                    if (*gap && *(gap+1))
                    {
                        gapnext = *(gap+1);
                        *(gap+1) = NULL;
                    }
                    slapi_pblock_set( pb, SLAPI_SEARCH_GERATTRS, gap );
                    rc = plugin_call_acl_plugin (pb, e, attrs, NULL, 
                        SLAPI_ACL_ALL, ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS,
                        &errbuf);
                    if (NULL != gapnext)
                    {
                        *(gap+1) = gapnext;
                    }
                }
                else if (NULL != e)
                {
                    rc = plugin_call_acl_plugin (pb, e, attrs, NULL, 
                        SLAPI_ACL_ALL, ACLPLUGIN_ACCESS_GET_EFFECTIVE_RIGHTS,
                        &errbuf);
                }
                if (NULL == e) {
                    /* get the template entry, if any */
                    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &e);
                    if (NULL == e) {
                        /* everything is ok - don't send the result */
                        pr_stat = PAGEDRESULTS_SEARCH_END;
                        done = 1;
                        continue;
                    }
                    gerentry = e;
                }
                if ( rc != LDAP_SUCCESS ) {
                    /* Send error result and 
                       abort op if the control is critical */
                     LDAPDebug( LDAP_DEBUG_ANY,
                    "Failed to get effective rights for entry (%s), rc=%d\n",
                    slapi_entry_get_dn_const(e), rc, 0 );
                    send_ldap_result( pb, rc, NULL, errbuf, 0, NULL );
                    slapi_ch_free ( (void**)&errbuf );
                    if (gerentry)
                    {
                        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
                        slapi_entry_free(gerentry);
                        gerentry = e = NULL;
                    }
                    pr_stat = PAGEDRESULTS_SEARCH_END;
                    rval = -1;
                    done = 1;
                    continue;
                }
                slapi_ch_free ( (void**)&errbuf );
                if (process_entry(pb, e, send_result)) 
                {
                    /* shouldn't send this entry */
                    if (gerentry)
                    {
                        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
                        slapi_entry_free(gerentry);
                        gerentry = e = NULL;
                    }
                    continue;
                }

                /*
                 * It's a regular entry, or it's a referral and
                 * managedsait control is on.  In either case, send
                 * the entry.
                 */
                switch (send_ldap_search_entry(pb, e, NULL, attrs, attrsonly)) 
                {
                    case 0:        /* entry sent ok */
                        (*pnentries)++;
                        slapi_pblock_set(pb, SLAPI_NENTRIES, pnentries);
                        break;
                    case 1:        /* entry not sent */
                        break;
                    case -1:       /* connection closed */
                        /*
                         * mark the operation as abandoned so the backend
                         * next entry function gets called again and has
                         * a chance to clean things up.
                         */
                        pb->pb_op->o_status = SLAPI_OP_STATUS_ABANDONED;
                        break;
                }
                if (gerentry)
                {
                    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
                    slapi_entry_free(gerentry);
                    gerentry = e = NULL;
                }
            }
            while (gap && ++gap && *gap);
            slapi_pblock_set( pb, SLAPI_SEARCH_GERATTRS, gerattrs );
            cool_charray_free(gerattrsdup);
            if (NULL == e)
            {
                /* no more entries */
                done = 1;
                pr_stat = PAGEDRESULTS_SEARCH_END;
            }
        }
        else if (e)
        {
            if (PAGEDRESULTS_PAGE_END == pr_stat)
            {
                /* 
                 * read ahead -- there is at least more entry.
                 * undo it and return the PAGE_END
                 */
                be->be_prev_search_results(pb);
                done = 1;
                continue;
            }
            if (process_entry(pb, e, send_result)) 
            {
                /* shouldn't  send this entry */
                continue;
            }

            /*
             * It's a regular entry, or it's a referral and
             * managedsait control is on.  In either case, send
             * the entry.
             */
            switch (send_ldap_search_entry(pb, e, NULL, attrs, attrsonly)) 
            {
                case 0:        /* entry sent ok */
                    (*pnentries)++;
                    slapi_pblock_set(pb, SLAPI_NENTRIES, pnentries);
                    break;
                case 1:        /* entry not sent */
                    break;
                case -1:       /* connection closed */
                    /*
                     * mark the operation as abandoned so the backend
                     * next entry function gets called again and has
                     * a chance to clean things up.
                     */
                    pb->pb_op->o_status = SLAPI_OP_STATUS_ABANDONED;
                    break;
            }
            if (pagesize == *pnentries)
            { 
                /* PAGED RESULTS: reached the pagesize */
                /* We don't set "done = 1" here.
                 * We read ahead next entry to check whether there is
                 * more entries to return or not. */
                pr_stat = PAGEDRESULTS_PAGE_END;
            }
        }
        else
        {
            /* no more entries */
            done = 1;
            pr_stat = PAGEDRESULTS_SEARCH_END;
        }
    }

    if (pr_statp) {
        *pr_statp = pr_stat;
    }
    return rval;
}

static int        timelimit_reslimit_handle = -1;
static int        sizelimit_reslimit_handle = -1;

/*
 * Register size and time limit with the binder-based resource limits
 * subsystem. A SLAPI_RESLIMIT_STATUS_... code is returned.
 */
int
search_register_reslimits( void )
{
    int        rc1, rc2;

    rc1 = slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            "nsSizeLimit" , &sizelimit_reslimit_handle );
    rc2 = slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            "nsTimeLimit", &timelimit_reslimit_handle );

    if ( rc1 != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
        return( rc1 );
    } else {
        return( rc2 );
    }
}


/*
 * Compute size and time limits based on the connection (bind identity).
 * Binder-based resource limits get top priority, followed by those associated
 * with the backend we are using.
 *
 * If the binder is the root DN and there is nothing in the root DN's entry
 * to say otherwise, no limits are used.  Otherwise, the lower of the limit
 * that was sent in the LDAP request and that available based on the
 * connection bind identity or configured backend limit is used.
 */
static void
compute_limits (Slapi_PBlock *pb)
{
    int timelimit, sizelimit;
    int requested_timelimit, max_timelimit, requested_sizelimit, max_sizelimit;
    int isroot;
    int isCertAuth;
    Slapi_ComponentId *component_id = NULL;
    Slapi_Backend *be;

    slapi_pblock_get (pb, SLAPI_SEARCH_TIMELIMIT, &requested_timelimit);
    slapi_pblock_get (pb, SLAPI_SEARCH_SIZELIMIT, &requested_sizelimit);
    slapi_pblock_get (pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get (pb, SLAPI_BACKEND, &be);


    /* If the search belongs to the client authentication process, take the value at
     * nsslapd-timelimit as the actual time limit.
     */
    
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &component_id);
    if (component_id) {
         isCertAuth = (! strcasecmp(component_id->sci_component_name, COMPONENT_CERT_AUTH) ) ? 1 : 0;
         if (isCertAuth) {
               timelimit = config_get_timelimit();
           goto set_timelimit;
         }
    }

    /*
     * Compute the time limit.
     */
    if ( slapi_reslimit_get_integer_limit( pb->pb_conn,
            timelimit_reslimit_handle, &max_timelimit )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
        /*
         * no limit associated with binder/connection or some other error
         * occurred.  use the default maximum.
          */
        if ( isroot ) {
            max_timelimit = -1;    /* no limit */
        } else {
            max_timelimit = be->be_timelimit;
        }
    }

    if ( requested_timelimit ) {
        /* requested limit should be applied to all (including root) */
        if ( isroot ) {
            timelimit = requested_timelimit;
        } else if ( (max_timelimit == -1) ||
                    (requested_timelimit < max_timelimit) ) {
            timelimit = requested_timelimit;
        } else {
            timelimit = max_timelimit;
        }
    } else if ( isroot ) {
        timelimit = -1;    /* no limit */
    } else {
        timelimit = max_timelimit;
    }

 set_timelimit:
    slapi_pblock_set(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);


    /*
     * Compute the size limit.
     */
    if ( slapi_reslimit_get_integer_limit( pb->pb_conn,
            sizelimit_reslimit_handle, &max_sizelimit )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
        /*
         * no limit associated with binder/connection or some other error
         * occurred.  use the default maximum.
          */
        if ( isroot ) {
            max_sizelimit = -1;    /* no limit */
        } else {
            max_sizelimit = be->be_sizelimit;
        }
    }

    if ( requested_sizelimit ) {
        /* requested limit should be applied to all (including root) */
        if ( isroot ) {
            sizelimit = requested_sizelimit;
        } else if ( (max_sizelimit == -1) ||
                    (requested_sizelimit < max_sizelimit) ) {
            sizelimit = requested_sizelimit;
        } else {
            sizelimit = max_sizelimit;
        }
    } else if ( isroot ) {
        sizelimit = -1;    /* no limit */
    } else {
        sizelimit = max_sizelimit;
    }
    slapi_pblock_set(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);

    LDAPDebug( LDAP_DEBUG_TRACE,
            "=> compute_limits: sizelimit=%d, timelimit=%d\n",
            sizelimit, timelimit, 0 );
}

/* Iterates through results and send them to the client.
 * Returns 0 if successful and -1 otherwise
 */
static int
send_results_ext(Slapi_PBlock *pb, int send_result, int *nentries, int pagesize, unsigned int *pr_stat)
{
    Slapi_Backend *be; 
    int rc;

    slapi_pblock_get (pb, SLAPI_BACKEND, &be);

    if (be->be_next_search_entry == NULL)
    {
        /* we need to send the result, but the function to iterate through 
           the result set is not implemented */
        /* ONREPL - log error */
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Search not supported", 0, NULL);
        return -1;
    }

    /* Iterate through the returned result set */
    if (be->be_next_search_entry_ext != NULL)
    {
        /* The iterate look ahead is causing a whole mess with the ACL.
        ** the entries are now visiting the ACL land in a random way
        ** and not the ordered way it was before. Until we figure out
        ** let's not change the behavior.
        **
        ** Don't use iterate_with_lookahead because it sends the result 
        * in the same times as the entry and this can cause failure
        * of the mapping tree scanning algorithme
        * if (getFrontendConfig()->result_tweak)
        * {
        *    rc = iterate_with_lookahead(pb, be, send_result, nentries);
        * }
        * else
        */
        rc = iterate(pb, be, send_result, nentries, pagesize, pr_stat);
    }
    else
    {
        rc = iterate(pb, be, send_result, nentries, pagesize, pr_stat);
    }

    switch(rc) 
    {
        case -1:    /* an error occured */

        case 0 :    /* everything is ok - result is sent */
                    /* If this happens we are dead but hopefully iterate
                     * never sends the result itself 
                     */
                    break;    

        case 1:        /* everything is ok - don't send the result */
                    rc = 0;
    }
    
    return rc;
}

void op_shared_log_error_access (Slapi_PBlock *pb, const char *type, const char *dn, const char *msg)
{
    char ebuf[BUFSIZ];
    slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d %s dn=\"%s\", %s\n",
                      ( pb->pb_conn ? pb->pb_conn->c_connid : 0), 
                      ( pb->pb_op ? pb->pb_op->o_opid : 0), 
                      type, 
                      escape_string( dn, ebuf ), 
                      msg ? msg : "" );

}

