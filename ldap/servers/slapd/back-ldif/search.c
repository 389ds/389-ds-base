/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: search.c
 *
 *  Functions:
 * 
 *      ldif_back_search() - ldif backend search function 
 *  
 */

#include "back-ldif.h"

/*
 *  Function: ldif_back_search
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: Searches the database for entries satisfying the 
 *               user's criteria
 */
int
ldif_back_search( Slapi_PBlock *pb )
{
  LDIF	        *db;       /*The database*/
  char		*base;     /*Base of the search*/
  int		scope;     /*Scope of the search*/
  int		deref;     /*Should we dereference aliases?*/
  int		slimit;    /*Size limit of the search*/
  int		tlimit;    /*Time limit of the search*/ 
  Slapi_Filter        *filter;   /*The filter*/
  time_t        dummy=0;   /*Used for time()*/
  char		**attrs;   /*Attributes*/
  int		attrsonly; /*Should we just return the attributes found?*/
  time_t       	optime;    /*Time the operation started*/
  int		nentries;  /*Number of entries found thus far*/
  ldif_Entry    *cur;      /*Used for traversing the list of entries*/
  int           hitflag=0; /*Used to test if we found the entry in the db*/
  char          *freeme;   /*Tmp storage for monitordn*/
  time_t        currtime;  /*The current time*/

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_search\n", 0, 0, 0 );
  
  /* 
   * Get private information created in the init routine. 
   * Also get the parameters of the search operation. These come
   * more or less directly from the client.
   */
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_TARGET, &base ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_DEREF, &deref ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_SIZELIMIT, &slimit ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_TIMELIMIT, &tlimit ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_ATTRS, &attrs ) < 0 ||
      slapi_pblock_get( pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly ) <0 ||
      slapi_pblock_get( pb, SLAPI_OPINITIATED_TIME, &optime ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }
  
  
  /*
   * If we get a search request for the backend monitor dn,
   * call ldif_back_monitor_info(), which packages up the
   * backend database analysis info and sends it back to the
   * client
   */
  if ( scope == LDAP_SCOPE_BASE ) {
    
    /*Get the backend's monitor dn*/
    freeme = (char *) get_monitordn(pb);
    
    if (freeme != NULL){

      /*
       * Compare the monitor dn with the base,
       * if they match, call monitor_info, which
       * will return all the relevant info to the client
       */
      if ( strcasecmp( base, freeme) == 0 ) {
	ldif_back_monitor_info( pb, db );
	free ((void *) freeme);
	return(-1);
      }
      free ((void *) freeme);
    }
  }
  

  /*
   * First we lock the whole database (clumsy, inefficient and 
   * inelegant, but simple)
   */
  PR_Lock( db->ldif_lock );
  
  /*Increase the number of accesses*/
  db->ldif_tries++;

  /*
   * Look through each entry in the ldif file and see if it matches
   * the filter and scope of the search. Do this by calling the
   * slapi_filter_test() routine.
   */
  nentries = 0;
  for (cur=db->ldif_entries; cur != NULL; cur = cur->next ) {

    /*Make sure we're not exceeding our time limit...*/
    currtime = time(&dummy);
    if ((tlimit > 0) && ((currtime - optime) > tlimit)){
      slapi_send_ldap_result( pb, LDAP_TIMELIMIT_EXCEEDED, NULL, NULL, nentries, NULL);

      /*We "hit" the cache*/
      if (hitflag)
	{
	  db->ldif_hits++;
	}

      PR_Unlock( db->ldif_lock );
      return(-1);
    }

    /*...or that we haven't been abandoned*/
    if ( slapi_op_abandoned( pb ) ) {

      /*We "hit" the cache*/
      if (hitflag)
	{
	  db->ldif_hits++;
	}
      
      PR_Unlock( db->ldif_lock );
      return( -1 );
    }
    
    /*Test for exceedence of size limit*/
    if ((slimit > -1) && (nentries >= slimit)){
      slapi_send_ldap_result( pb, LDAP_SIZELIMIT_EXCEEDED, NULL, NULL, nentries, NULL);
      
      /*We hit the "cache"*/
      if (hitflag)
	{
	  db->ldif_hits++;
	}
      PR_Unlock( db->ldif_lock );
      return(-1);
    }
    
    
    
    /*Test if this entry matches the filter*/
    if ( slapi_vattr_filter_test( pb, cur->lde_e, filter, 1 /* verify access */ ) == 0 ) {

      /* Entry matches - send it */
      hitflag = 1;
      
      switch ( slapi_send_ldap_search_entry( pb, cur->lde_e, NULL, attrs,
	    attrsonly ) ) {
      case 0:	/* Entry sent ok */
	nentries++;
	break;
      case 1:	/* Entry not sent - because of acl, etc. */
	break;
      case -1:/* Connection closed */
	/* Clean up and return */
	
	/*We "hit" the cache*/
	if (hitflag)
	  {
	    db->ldif_hits++;
	  }
	PR_Unlock( db->ldif_lock );
	return( -1 );
      }
      
      
    }
  }
  
  /*If we succeeded, we should update the ldif_hits entry of db*/
  if (hitflag)
    {
      db->ldif_hits++;
    }
  
  
  /* Search is done, send LDAP_SUCCESS */
  slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, nentries, NULL );
  PR_Unlock( db->ldif_lock );
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_search\n", 0, 0, 0 );
  return( -1 );
  
}
