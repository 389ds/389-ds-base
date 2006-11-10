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
 *  File: delete.c
 *
 *  Functions:
 * 
 *      ldif_back_delete() - ldap ldif back-end delete routine 
 *      has_children() - determines if an entry has any children
 *
 */

#include "back-ldif.h"

/*
 *  Function: ldif_back_delete
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: For changetype: delete, this function deletes the entry
 */
int
ldif_back_delete( Slapi_PBlock *pb )
{
  LDIF    		*db;         /*The database*/
  ldif_Entry      	*bye, *prev; /*"bye" is the record to be deleted*/
  char			*dn;         /*Storage for the dn*/
  int rc;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_delete\n", 0, 0, 0 );

  prev = NULL;
  
  /*Get the database and the dn to delete*/
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_DELETE_TARGET, &dn ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }
  
  /*Lock the database*/
  PR_Lock( db->ldif_lock );

  /* Find the entry we're about to delete*/
  bye = (ldif_Entry *) ldif_find_entry(pb, db, dn, &prev);
  if (bye == NULL) {
    slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
    LDAPDebug( LDAP_DEBUG_TRACE, "entry for delete does not exist\n", 0, 0, 0 );
    PR_Unlock( db->ldif_lock );
    return(-1);
  }
  
  /*Make sure that we are trying to delete a leaf.*/
  if ( has_children( db, bye ) ) {
    slapi_send_ldap_result( pb, LDAP_NOT_ALLOWED_ON_NONLEAF, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( -1 );
  }
  
  /*Check the access*/
  rc= slapi_access_allowed( pb, bye->lde_e, "entry", NULL, SLAPI_ACL_DELETE );
  if ( rc!=LDAP_SUCCESS) {
    slapi_send_ldap_result( pb, rc, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( -1 );
  }

  /* Delete from disk and database */
  if ( update_db(pb, db, NULL, prev, LDIF_DB_DELETE) != 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return(-1);
    
  }
  
  /*Success*/
  slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
  PR_Unlock( db->ldif_lock );
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_delete\n", 0, 0, 0 );
  return( 0 );
}

/*
 *  Function: has_children
 *
 *  Returns: returns 1 if the entry has kids, 0 else.
 *  
 *  Description: Determines if the entry has children
 */
int
has_children(LDIF *db, ldif_Entry *p)
{
  char *parentdn;   /*Basically the dn of p (copied)*/
  char *childdn;    /*Will be used to test if p has any children*/
  ldif_Entry *cur;  /*Used to walk down the list*/
  int  has_kid = 0; /*Flag to return*/
  
  LDAPDebug( LDAP_DEBUG_TRACE, "=> has_children\n", 0, 0, 0);

  /*If there is no p or db, then there can be no children*/
  if (p == NULL || db == NULL){
    return(0);
  }

  /*Get a copy of p's dn, and normalize it (squeeze any unneeded spaces out)*/
  parentdn = strdup( slapi_entry_get_dn(p->lde_e) );
  (void) slapi_dn_normalize( parentdn );

  /*Walk down the list, seeing if each entry has p as a parent*/
  for (cur = db->ldif_entries; cur != NULL; cur = cur->next){
    childdn = strdup(slapi_entry_get_dn(cur->lde_e));
    (void) slapi_dn_normalize(childdn);

    /*Test to see if this childdn is a child of the parentdn*/
    if (slapi_dn_issuffix( childdn, parentdn ) && strlen(childdn) > strlen(parentdn))
      {
	has_kid = 1;    
	free( (void *) childdn);
	break;
      }
    free( (void *) childdn);

  }

  free( (void *) parentdn );
    
  LDAPDebug( LDAP_DEBUG_TRACE, "<= has_children %d\n", has_kid, 0, 0);
  return( has_kid );
}




