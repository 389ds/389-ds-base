/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: monitor.c
 *
 *  Functions:
 * 
 *      ldif_back_monitor_info() - ldap ldif back-end initialize routine 
 *
 *      get_monitordn() - gets the monitor dn for this backend
 *
 */

#include "back-ldif.h"

extern char Versionstr[];


/*
 *  Function: ldif_back_monitor_info
 *
 *  Returns: returns 1
 *  
 *  Description: This function wraps up backend specific monitor information
 *               and returns it to the client as an entry. This function
 *               is usually called by ldif_back_search upon receipt of 
 *               the monitor dn for this backend.
 */
int
ldif_back_monitor_info( Slapi_PBlock *pb, LDIF *db)
{
  Slapi_Entry	*e;          /*Entry*/
  char		buf[BUFSIZ]; /*Buffer for getting the attrs*/
  struct berval	val;         /*More attribute storage*/
  struct berval	*vals[2];    /*Even more*/
  char          *type;       /*Database name (type) */

  vals[0] = &val;
  vals[1] = NULL;  

  /*Alloc the entry and set the monitordn*/
  e = slapi_entry_alloc();
  slapi_entry_set_dn(e, (char *) get_monitordn(pb));
  
  /* Get the database name (be_type) */
  slapi_pblock_get( pb, SLAPI_BE_TYPE, &type);
  sprintf( buf, "%s", type );
  val.bv_val = buf;
  val.bv_len = strlen( buf );
  slapi_entry_attr_merge( e, "database", vals );

  /*Lock the database*/
  PR_Lock( db->ldif_lock );

  /*Get the number of database hits */
  sprintf( buf, "%ld",  db->ldif_hits);
  val.bv_val = buf;
  val.bv_len = strlen( buf );
  slapi_entry_attr_merge( e, "entrycachehits", vals );

  /*Get the number of database tries */
  sprintf( buf, "%ld", db->ldif_tries);
  val.bv_val = buf;
  val.bv_len = strlen( buf );
  slapi_entry_attr_merge( e, "entrycachetries", vals );

  /*Get the current size of the entrycache (db) */
  sprintf( buf, "%ld",  db->ldif_n);
  val.bv_val = buf;
  val.bv_len = strlen( buf );
  slapi_entry_attr_merge( e, "currententrycachesize", vals );


  /*
   * Get the maximum size of the entrycache (db) 
   * in this database, there is no max, so return the current size
   */
  val.bv_val = buf;
  val.bv_len = strlen( buf );
  slapi_entry_attr_merge( e, "maxentrycachesize", vals );

  /* Release the lock*/
  PR_Unlock( db->ldif_lock );

  /*Send the results back to the client*/
  slapi_send_ldap_search_entry( pb, e, NULL, NULL, 0 );
  slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 1, NULL );
  
  slapi_entry_free( e );

  return(1);


}


/*
 *  Function: get_monitordn
 *
 *  Returns: returns ptr to string if success, NULL else
 *  
 *  Description: get_monitordn takes a pblock and extracts the 
 *               monitor dn of this backend. The monitordn is a special
 *               signal to the backend to return backend specific monitor
 *               information (usually called by back_ldif_search()).
 */
char *
get_monitordn(Slapi_PBlock *pb )
{
  char *mdn;

  slapi_pblock_get( pb, SLAPI_BE_MONITORDN, &mdn );
  
  if (mdn == NULL) {
    return(NULL);
    
  }

  return(strdup(mdn));

}
