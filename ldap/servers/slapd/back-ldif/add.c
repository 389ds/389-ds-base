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
 *  File: add.c
 *
 *  Functions:
 * 
 *      ldif_back_add() - ldap ldif back-end add routine 
 *      ldifentry_init() - takes an Entry and makes an ldif_Entry
 *
 */

#include "back-ldif.h"
ldif_Entry * ldifentry_init(Slapi_Entry *);

/*
 *  Function: ldif_back_add
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: For changetype: add, this function adds the entry
 */
int
ldif_back_add( Slapi_PBlock *pb )
{
  LDIF                  *db;          /*Stores the ldif database*/
  char			*dn = NULL, *parentdn = NULL;
  Slapi_Entry		*e;           /*The new entry to add*/
  ldif_Entry    	*new, *old;   /*Used for various accounting purposes*/
  ldif_Entry            *prev;        /*Used to add new ldif_Entry to db*/
  ldif_Entry            *tprev;       /*Dummy pointer for traversing the list*/
  char			*errbuf = NULL;

  prev = NULL;
  tprev = NULL;

  /*Turn on tracing to see this printed out*/
  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_add\n", 0, 0, 0 );

  /*Get the database, the dn and the entry to add*/
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_ADD_TARGET, &dn ) < 0 ||
      slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &e ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }

  /*Check to make sure the entry passes the schema check*/
  if ( slapi_entry_schema_check( pb, e ) != 0 ) {
    LDAPDebug( LDAP_DEBUG_TRACE, "entry failed schema check\n", 0, 0, 0 );
    slapi_send_ldap_result( pb, LDAP_OBJECT_CLASS_VIOLATION, NULL, NULL, 0, NULL );
    return( -1 );
  }

  prev = NULL;
  
  /*Lock the database*/
  PR_Lock( db->ldif_lock );
  
  /*
   * Attempt to find this dn in db. If there is no such dn,
   * ldif_find_entry should return NULL, and prev should point
   * to the last element in the list.
   */
  if ((old = (ldif_Entry *)ldif_find_entry(pb, db, dn, &prev)) != NULL) {
   
    /*
     * If we've reached this code, there is an entry in db
     * whose dn matches dn, so release the db lock, 
     * tell the user and return
     */
    PR_Unlock( db->ldif_lock );
    slapi_send_ldap_result( pb, LDAP_ALREADY_EXISTS, NULL, NULL, 0, NULL );
    return( -1 );
  }
  

  /*
   * Get the parent dn and see if the corresponding entry exists.
   * If the parent does not exist, only allow the "root" user to
   * add the entry.
   */
  if ( (parentdn = slapi_dn_beparent( pb, dn )) != NULL ) {
    int rc;
    if ((old = (ldif_Entry *)ldif_find_entry( pb, db, parentdn, &tprev)) == NULL ) {
      slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
      LDAPDebug( LDAP_DEBUG_TRACE, "parent does not exist\n", 0, 0, 0 );
      goto error_return;
    }
    rc= slapi_access_allowed( pb, e, NULL, NULL, SLAPI_ACL_ADD );
    if ( rc!=LDAP_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_TRACE, "no access to parent\n", 0,0, 0 );
      slapi_send_ldap_result( pb, rc, NULL, NULL, 0, NULL );
      goto error_return;
    }
  } else {	/* no parent */
    int isroot;
	slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
    if ( !isroot ) {
      LDAPDebug( LDAP_DEBUG_TRACE, "no parent & not root\n", 0, 0, 0 );
      slapi_send_ldap_result( pb, LDAP_INSUFFICIENT_ACCESS, NULL, NULL, 0, NULL );
      goto error_return;
    }

  }
	
  /*
   * Before we add the entry, find out if the syntax of the aci
   * aci attribute values are correct or not. We don't want to add
   * the entry if the syntax is incorrect.
   */
   if ( slapi_acl_verify_aci_syntax(pb, e, &errbuf) != 0 ) {
     slapi_send_ldap_result( pb, LDAP_INVALID_SYNTAX, NULL, errbuf, 0, NULL );
     if (errbuf) free(errbuf);
     goto error_return;
   }

  /*Make a new element for the linked list*/
  if ( (new = (ldif_Entry *)ldifentry_init( e )) == NULL){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    goto error_return;

  }
  
  /*Add the new element to the end of the list of entries in db*/
  if ( update_db(pb, db, new, prev, LDIF_DB_ADD) != 0)
  {
    ldifentry_free( new ); 
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    goto error_return;

  }
  
  /*We have been sucessful. Tell the user*/  
  slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

  /*Release the database lock*/
  PR_Unlock( db->ldif_lock );

  /*Free the parentdn, and return*/
  if ( parentdn != NULL ){
    free( (void *)parentdn );
  }
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_add\n", 0, 0, 0 );
  return( 0 );
  
 error_return:;
  if ( parentdn != NULL ){
    free( (void *)parentdn );
  }

  PR_Unlock( db->ldif_lock );
  return( -1 );
}

/*
 *  Function: ldifentry_init
 *
 *  Returns: a pointer to an ldif_Entry, or NULL
 *  
 *  Description: Takes a pointer to an Entry, and sticks
 *               it into an ldif_Entry structure.
 *               Note, uses Malloc.
 */
ldif_Entry *
ldifentry_init(Slapi_Entry *e)
{
  ldif_Entry *new;
  
  /*Alloc a new ldif_entry*/
  new =  (ldif_Entry *) malloc(sizeof(ldif_Entry));

  /*Did it work? if not, return NULL*/
  if (new == NULL) {
    return (NULL);
  }
  /*If it did work, then fill it*/
  new->lde_e = e;
  new->next = NULL;

  /*Send it*/
  return (new);
}



