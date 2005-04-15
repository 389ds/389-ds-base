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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: compare.c
 *
 *  Functions:
 * 
 *      ldif_back_compare() - ldap ldif back-end compare routine
 *
 */

#include "back-ldif.h"

/*
 *  Function: ldif_back_compare
 *
 *  Returns: -1, 0 or 1 
 *  
 *  Description: compares entries in the ldif backend
 */
int
ldif_back_compare( Slapi_PBlock *pb )
{
  LDIF          *db;         /*The Database*/
  ldif_Entry    *e, *prev;   /*Used for searching the database*/
  char          *dn, *type;  /*The dn and the type*/
  struct berval *bval;
  Slapi_Attr	*attr;
  int rc;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_compare\n", 0, 0, 0 );
  prev = NULL;

  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_COMPARE_TARGET, &dn ) < 0 ||
      slapi_pblock_get( pb, SLAPI_COMPARE_TYPE, &type ) < 0 ||
      slapi_pblock_get( pb, SLAPI_COMPARE_VALUE, &bval ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }
  
  /*Lock the database*/
  PR_Lock( db->ldif_lock );

  
  /*Find the entry for comparison*/
  if ( (e = (ldif_Entry*) ldif_find_entry( pb, db, dn, &prev )) == NULL ) {
    slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( 1 );
  }
  
  /*Check the access*/
  rc= slapi_access_allowed( pb, e->lde_e, type, bval, SLAPI_ACL_COMPARE );
  if ( rc!=LDAP_SUCCESS  ) {
    slapi_send_ldap_result( pb, rc, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( 1 );
  }
  
  /*find the attribute*/
  if ( slapi_entry_attr_find( e->lde_e, type, &attr ) != 0 ) {
    slapi_send_ldap_result( pb, LDAP_NO_SUCH_ATTRIBUTE, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( 1 );
  }
  
  if ( slapi_attr_value_find( attr, bval ) == 0 ) {
    slapi_send_ldap_result( pb, LDAP_COMPARE_TRUE, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( 0 );
  }
  
  slapi_send_ldap_result( pb, LDAP_COMPARE_FALSE, NULL, NULL, 0, NULL );
  PR_Unlock( db->ldif_lock );
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_compare\n", 0, 0, 0 );
  return( 0 );
}

