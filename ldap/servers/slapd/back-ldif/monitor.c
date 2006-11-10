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
  PR_snprintf( buf, sizeof(buf), "%s", type );
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
