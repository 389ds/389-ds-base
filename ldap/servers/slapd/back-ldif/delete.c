/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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




