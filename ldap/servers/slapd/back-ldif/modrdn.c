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
 *  File: modrdn.c
 *
 *  Functions:
 * 
 *      ldif_back_modrdn() - ldap ldif back-end modrdn routine 
 *      rdn2typval() - rdn to typval converter
 *      ldif_add_mod() - Adds a modification to be performed.
 *
 */

#include "back-ldif.h"
int rdn2typval(char *, char **, struct berval *);
void ldif_add_mod( LDAPMod ***, int, char *, struct berval ** );

/*
 *  Function: ldif_back_modrdn
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: For changetype: modrdn, this modifies the rdn of the entry
 */
int
ldif_back_modrdn( Slapi_PBlock *pb )
{
  LDIF          *db;                  /*ldif backend database*/
  ldif_Entry    *prev, *tprev, *entry, *entry2, *test;
  char          *pdn, *newdn;         /*Used for dn manipulation*/
  char          *dn, *newrdn, *type;  /*Used for dn manipulation*/
  int           i;                    /*A counter*/
  char          **rdns, **dns;        /*Used for dn manipulation*/
  int           deleteoldrdn;         /*Flag from user to delete old rdn*/
  struct berval bv;       
  struct berval *bvps[2];
  LDAPMod       **mods;               /*Holds the list of modifications*/
  int rc;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_modrdn\n", 0, 0, 0 );  

  prev = NULL;

  /*Get the information from the front end, including the database*/
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db )< 0 ||
  slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &dn ) < 0 ||
  slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn ) < 0 || 
  slapi_pblock_get( pb, SLAPI_MODRDN_DELOLDRDN, &deleteoldrdn ) <0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);

  }

  /*Lock the database*/
  PR_Lock( db->ldif_lock );
  
  /* 
   * Find the entry we are about to modify 
   * prev will point to the previous element in the list,
   * NULL if there is no previous element
   */
  if ( (entry = (ldif_Entry *)ldif_find_entry( pb, db, dn, &prev)) == NULL ) {
    slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( -1 );
  }

  /*Make sure that we are trying to modify the rdn of a leaf.*/
  if ( has_children( db, entry ) ) {
    slapi_send_ldap_result( pb, LDAP_NOT_ALLOWED_ON_NONLEAF, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( -1 );
  }


  /* Create a copy of the entry and apply the changes to it */
  if ( (entry2 = (ldif_Entry *)ldifentry_dup( entry )) == NULL ) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( -1 );
  }
  
  /*Check the access*/
  rc= slapi_access_allowed( pb, entry2->lde_e, NULL, NULL, SLAPI_ACL_WRITE );
  if ( rc!=LDAP_SUCCESS  ) {
    slapi_send_ldap_result( pb, rc, NULL, NULL, 0, NULL );
    ldifentry_free( entry2 );
    PR_Unlock( db->ldif_lock );
    return( -1 );
  }

  /* Construct the new dn */
  if ( (pdn = slapi_dn_beparent( pb, dn )) != NULL ) {

    /* parent + rdn + separator(s) + null */
    newdn = (char *) malloc( strlen( pdn ) + strlen( newrdn ) + 3 );
    if (newdn == NULL){
      LDAPDebug( LDAP_DEBUG_ANY,"malloc failed", 0, 0, 0 );
      exit(1);
    }

    strcpy( newdn, newrdn );
    strcat( newdn, ", " );
    strcat( newdn, pdn );
  } else {
    newdn = strdup( newrdn );
  }
  free( pdn );

  /*Normalize the newdn, that is, squeeze out all unnecessary spaces*/
  (void) slapi_dn_normalize( newdn );

  
  /* Add the new dn to our working copy of the entry */
  slapi_entry_set_dn( entry2->lde_e, newdn );


  /* See if an entry with the new name already exists */
  if ( (test = (ldif_Entry *)ldif_find_entry( pb, db, newdn, &tprev )) != NULL ) {
    slapi_send_ldap_result( pb, LDAP_ALREADY_EXISTS, NULL, NULL, 0, NULL );
    
    goto error_return;
  }
  
  
  /*
   * Delete old rdn values from the entry if deleteoldrdn is set.
   * Add new rdn values to the entry.
   */
  mods = NULL;
  bvps[0] = &bv;
  bvps[1] = NULL;
  if ( (dns = ldap_explode_dn( dn, 0 )) != NULL ) {
    if ( (rdns = ldap_explode_rdn( dns[0], 0 )) != NULL ) {
      for ( i = 0; rdns[i] != NULL; i++ ) {

	/* Delete from entry attributes */
	if ( deleteoldrdn && rdn2typval( rdns[i], &type, &bv ) == 0 ) {
	  ldif_add_mod( &mods, LDAP_MOD_DELETE, type, bvps );
	}
      }
      ldap_value_free( rdns );
    }
    ldap_value_free( dns );
  }
  if ( dns == NULL || rdns == NULL ) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    goto error_return;
  }
  /* Add new rdn values to the entry */
  if ( (rdns = ldap_explode_rdn( newrdn, 0 )) != NULL ) {
    for ( i = 0; rdns[i] != NULL; i++ ) {
      /* Add to entry */
      if ( rdn2typval( rdns[i], &type, &bv ) == 0 ) {
	ldif_add_mod( &mods, LDAP_MOD_ADD, type, bvps );
      }
    }
    ldap_value_free( rdns );
  } else {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    goto error_return;
	}
  bv.bv_val = newdn;
  bv.bv_len = strlen( newdn );
  ldif_add_mod( &mods, LDAP_MOD_REPLACE, "entrydn", bvps );
  
  /* Check for abandon */
  if ( slapi_op_abandoned( pb ) ) {
    goto error_return;
  }
  
  /* Apply the mods we built above to the copy of the entry */
  if ( apply_mods( entry2->lde_e, mods ) != 0 ) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );

    goto error_return;
  }

  /* Update the database and the disk */
  if ( update_db(pb, db, entry2, prev, LDIF_DB_REPLACE) != 0) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );

    goto error_return;
  }

  /*Unlock the database, and tell the user the good news*/
  PR_Unlock( db->ldif_lock );
  slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_modrdn\n", 0, 0, 0 );  
  return( 0 );
  
error_return:;

  /* Result already sent above - just free stuff */
  PR_Unlock( db->ldif_lock );
  ldifentry_free( entry2 );

  return( -1 );
}

/*
 *  Function: rdn2typval
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: converts an rdn to a typeval
 */
int
rdn2typval(char *rdn, char **type, struct berval *bv)
{
  char    *s;
	
  if ( (s = strchr( rdn, '=' )) == NULL ) {
    return( -1 );
  }
  *s++ = '\0';
  
  *type = rdn;
  bv->bv_val = s;
  bv->bv_len = strlen( s );
  
  return( 0 );
}

/*
 *  Function: ldif_add_mod
 *
 *  Returns: void
 *  
 *  Description: Adds a modification (add, delete, etc) to the list
 *               of modifications that will eventually be made to some entry
 */
void
ldif_add_mod( LDAPMod ***modlist, int modtype, char *type, struct berval **bvps )
{
  int	i;
  
  for ( i = 0; modlist[i] != NULL; i++ ) {
    ;	/* NULL */
  }
  
  *modlist = (LDAPMod **) realloc( (char *) *modlist,
				     (i + 2) * sizeof(LDAPMod *) );

  if (*modlist == NULL){
    LDAPDebug( LDAP_DEBUG_ANY, "realloc failed", 0, 0, 0 );
    exit(1);
  }
  (*modlist)[i] = (LDAPMod *) malloc( sizeof(LDAPMod) );

  if ((*modlist)[i] == NULL){
    LDAPDebug( LDAP_DEBUG_ANY,"malloc failed", 0, 0, 0 );
    exit(1);
  }

  (*modlist)[i]->mod_type = (char *) strdup( type );
  if ((*modlist)[i]->mod_type == NULL){
    LDAPDebug( LDAP_DEBUG_ANY,"strdup failed", 0, 0, 0 );
    exit(1);
  }


  (*modlist)[i]->mod_op = modtype;
  (*modlist)[i]->mod_bvalues = (struct berval **) malloc(2*sizeof(struct berval *));
  if ((*modlist)[i]->mod_bvalues == NULL){
    LDAPDebug( LDAP_DEBUG_ANY,"malloc failed",0, 0, 0 );
    exit(1);
  }
  (*modlist)[i]->mod_bvalues[0] = ber_bvdup( bvps[0] );
  (*modlist)[i]->mod_bvalues[1] = NULL;
  (*modlist)[i+1] = NULL;
}







