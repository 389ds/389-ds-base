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
 *  File: bind.c
 *
 *  Functions:
 * 
 *      ldif_back_bind() - ldap ldif back-end bind routine
 *
 */

#include "back-ldif.h"

/*
 *  Function: ldif_back_bind
 *
 *  Returns: returns 0|1 if good, -1 else.
 *  
 *  Description: performs an ldap bind.
 */
int
ldif_back_bind( Slapi_PBlock *pb )
{
  char			*dn;        /*Storage for the dn*/
  int			method;     /*Storage for the bind method*/
  struct berval		*cred;      /*Storage for the bind credentials*/
  struct berval		**bvals; 
  LDIF  		*db;        /*The database*/
  ldif_Entry            *e, *prev;  /*Used for searching the db*/
  int			rc, syntax; /*Storage for error return values*/
  Slapi_Attr		*attr;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_bind\n", 0, 0, 0 );

  prev = NULL;
  
  /*Get the parameters*/
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_BIND_TARGET, &dn ) < 0 ||
      slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) < 0 ||
      slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &cred ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }

  /*Lock the database*/
  PR_Lock( db->ldif_lock );


  /*Find the entry that the person is attempting to bind as*/
  if ( (e = (ldif_Entry *)ldif_find_entry( pb, db, dn, &prev )) == NULL ) {

    /* Allow noauth binds */
    if ( method == LDAP_AUTH_SIMPLE && cred->bv_len == 0 ) {
      rc = SLAPI_BIND_ANONYMOUS;
    } else {
      slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
      rc = SLAPI_BIND_FAIL;
    }

    /*Unlock the database*/
    PR_Unlock( db->ldif_lock );
    
    return( rc );
  }
  
  switch ( method ) {
  case LDAP_AUTH_SIMPLE:
    if ( cred->bv_len == 0 ) {
      PR_Unlock( db->ldif_lock );
      return( SLAPI_BIND_ANONYMOUS );
    }
    
    if ( slapi_entry_attr_find( e->lde_e, "userpassword", &attr ) != 0 ) {
      slapi_send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL, NULL, 0, NULL );
      PR_Unlock( db->ldif_lock );
      return( SLAPI_BIND_FAIL );
    }
	/*
	 * XXXmcs: slapi_attr_get_values() is deprecated and should be avoided
	 * See XXXmcs comments in ../attr.c for detailed information.
	 */
    slapi_attr_get_values( attr, &bvals );
    
    if ( slapi_pw_find( bvals, cred ) != 0 ) {
      slapi_send_ldap_result( pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL );
      PR_Unlock( db->ldif_lock );
      return( SLAPI_BIND_FAIL );
    }
    break;
    
  default:
    slapi_send_ldap_result( pb, LDAP_STRONG_AUTH_NOT_SUPPORTED, NULL,
		     "auth method not supported", 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( SLAPI_BIND_FAIL );
  }
  
  PR_Unlock( db->ldif_lock );  

  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_bind\n", 0, 0, 0 );  

  /* success:  front end will send result */
  return( SLAPI_BIND_SUCCESS );
}
