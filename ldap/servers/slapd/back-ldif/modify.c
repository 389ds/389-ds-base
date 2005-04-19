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
/* 
 *  File: modify.c
 *
 *  Functions:
 * 
 *      ldif_back_modify() - ldif backend modify function 
 *      update_db() - updates memory and disk db to reflect changes
 *      db2disk() - writes out ldif database to disk
 *      ldifentry_free() - frees an ldif_Entry
 *      ldifentry_dup() - copies an ldif_Entry
 *      ldif_find_entry() - searches an ldif DB for a particular dn
 *      apply_mods() - applies the modifications to an Entry
 *
 */

#include "back-ldif.h"

/*Prototypes*/
void ldifentry_free(ldif_Entry *);
ldif_Entry * ldifentry_dup(ldif_Entry *);
int apply_mods( Slapi_Entry *, LDAPMod ** );
ldif_Entry * ldif_find_entry(Slapi_PBlock *, LDIF *, char *, ldif_Entry **);
int db2disk(Slapi_PBlock *, LDIF *);
int update_db(Slapi_PBlock *, LDIF *, ldif_Entry *, ldif_Entry *, int ); 

/*
 *  Function: ldif_back_modify
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: For changetype: modify, this makes the changes
 */
int
ldif_back_modify( Slapi_PBlock *pb )
{
  LDIF	     *db;                   /*The ldif file is stored here*/
  ldif_Entry *entry, *entry2,*prev; /*For db manipulation*/
  int	     err;                   /*House keeping stuff*/
  LDAPMod    **mods;                /*Used to apply the modifications*/
  char	     *dn;                   /*Storage for the dn*/
  char	     *errbuf = NULL;	    /* To get error back */
  
  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_modify\n", 0, 0, 0 );
  prev = NULL;

  /*Get the database, the dn and the mods*/
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_MODIFY_TARGET, &dn ) < 0 ||
      slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }
  /*Lock the database*/
  PR_Lock( db->ldif_lock );
 
  /* 
   * Find the entry we are about to modify.
   * prev will point to the previous element in the list,
   * NULL if there is no previous element.
   */
  if ( (entry = (ldif_Entry *)ldif_find_entry( pb, db, dn, &prev)) == NULL ) {
    slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( -1 );	
  }

  /*Check acl, note that entry is not an Entry, but a ldif_Entry*/
  if ( (err = slapi_acl_check_mods( pb, entry->lde_e, mods, &errbuf )) != LDAP_SUCCESS ) {
    slapi_send_ldap_result( pb, err, NULL, errbuf, 0, NULL );
    if (errbuf)  free (errbuf);
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }
  
  /* Create a copy of the entry and apply the changes to it */
  if ( (entry2 = (ldif_Entry *) ldifentry_dup( entry )) == NULL ) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }
  
  /*Actually apply the modifications*/
  if ( (err = apply_mods( entry2->lde_e, mods )) != 0 ) {
    slapi_send_ldap_result( pb, err, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }
  
  /* Check for abandon */
  if ( slapi_op_abandoned( pb ) ) {
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }
  
  /* Check that the entry still obeys the schema */
  if ( slapi_entry_schema_check( pb, entry2->lde_e ) != 0 ) {
    slapi_send_ldap_result( pb, LDAP_OBJECT_CLASS_VIOLATION, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }
  
  /* Check for abandon again */
  if ( slapi_op_abandoned( pb ) ) {
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }
  
  /* Change the entry itself both on disk and in the cache */
  if ( update_db(pb, db, entry2, prev, LDIF_DB_REPLACE) != 0) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    PR_Unlock( db->ldif_lock );
    goto error_return;
  }

  slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
  PR_Unlock( db->ldif_lock );
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_modify\n", 0, 0, 0 );
  return( 0 );
  
 error_return:;
  if ( entry2 != NULL ) {
    ldifentry_free( entry2 );
  }
  
  return( -1 );
}

/*
 *  Function: update_db
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: Will update the database in memory, and on disk
 *               if prev == NULL, then the element to be deleted/replaced
 *               is the first in the list.
 *               mode = LDIF_DB_ADD | LDIF_DB_REPLACE | LDIF_DB_DELETE
 *               The database should be locked when this function is called.
 *               Note that on replaces and deletes, the old ldif_Entry's
 *               are freed.
 */
int
update_db(Slapi_PBlock *pb, LDIF *db, ldif_Entry *new, ldif_Entry *prev, int mode) 
{
  ldif_Entry *tmp;   /*Used to free the removed/replaced entries*/
  char *buf;         /*Used to convert entries to strings for output to file*/
  FILE *fp;          /*File ptr to the ldif file*/
  int  len;          /*Used by slapi_entry2str*/
  int  db_updated=0; /*Flag to designate if db in memory has been updated*/
  
  /*Make sure that the database is not null. Everything else can be, though*/
  if (db == NULL){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }
    
  /*
   * If we are adding an entry, then prev should be pointing 
   * to the last element in the list, or null if the list is empty, 
   * and new should not be null. 
   */
  if (mode == LDIF_DB_ADD) {

    /*Make sure there is something to add*/
    if ( new == NULL ) {
      slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
      return(-1);
    }
    
    /*If prev is null, then there had better be no entries in the list*/
    if (prev == NULL){
      if( db->ldif_entries != NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	return(-1);
      }
      /*There are no elements, so let's add the new one*/
      db->ldif_entries = new;
      db->ldif_n++;

      /*Set a flag*/
      db_updated = 1;

    }
    /*
     * Last error case to test for is if prev is not null, and prev->next
     * points to something. This means that we are not at the end of the list
     */
    if (prev != NULL) {
      if (prev->next != NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	return(-1);
      } 
      
      /*We're at the end of the list, so tack the new entry onto the end*/
      prev->next = new;
      db->ldif_n++;

      db_updated = 1;

    }
    
    /*If the database has been updated in memory, update the disk*/
    if (db_updated && db->ldif_file!=NULL) {

      /*Update the disk by appending to the ldif file*/
      fp = fopen(db->ldif_file, "a");
      if (fp == NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	/*This is s pretty serious problem, so we exit*/
	exit(-1);
      }
      
      /*Convert the entry to ldif format*/
      buf = slapi_entry2str(new->lde_e, &len);
      fprintf(fp, "%s\n", buf);
      free ( (void *) buf);
      fclose(fp);
      return(0);
    } else {
      slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
      fclose(fp);
      return(-1);

    }
    
  } else if (mode == LDIF_DB_DELETE){
    
    /*We're not deleting the first entry in the list*/
    if (prev != NULL){
      /*Make sure there is something to delete*/
      if (prev->next == NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	return(-1);
      }
      tmp = prev->next;
      prev->next = tmp->next;
      db->ldif_n--;
      ldifentry_free(tmp);
      
      db_updated = 1;
      
    } else { /*We are deleting the first entry in the list*/

      /*Make sure there is something to delete*/
      if (db->ldif_entries == NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	return(-1);
      }

      tmp = db->ldif_entries;
      db->ldif_entries = tmp->next;
      db->ldif_n--;

      /*Free the entry, and set the flag*/
      ldifentry_free(tmp);
      db_updated = 1;
    }

    /*
     * Update the disk by rewriting entire ldif file
     * I know, I know, but simplicity is the key here.
     */    
    if (db_updated) {
      return(db2disk(pb, db));
    } else {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
      return(-1);

    }

  } else if (mode == LDIF_DB_REPLACE) {

    /*Make sure there is something to replace with*/
    if ( new == NULL ) {
      slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
      return(-1);
    }


    /*We're not replacing the first element in the list*/
    if (prev != NULL){

      /*Make sure there is something to replace*/
      if (prev->next == NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	return(-1);
      }

      /*Splice out the old entry, and put in the new*/
      tmp = prev->next;
      prev->next = new;
      new->next = tmp->next;

      /*Free it*/
      ldifentry_free(tmp);
      db_updated = 1;
    } else { /*We are replacing the first entry in the list*/
      
      /*Make sure there is something to replace*/
      if (db->ldif_entries == NULL) {
	slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
	return(-1);
      }
      
      /*Splice out the old entry, and put in the new*/
      tmp = db->ldif_entries;
      db->ldif_entries = new;
      new->next = tmp->next;

      /*Free it*/
      ldifentry_free(tmp);
      db_updated = 1;
    }

    /*Update the disk by rewriting entire ldif file*/
    if (db_updated) {
      return(db2disk(pb, db));
    } else {
      slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
      return(-1);
    }

  }
}

/*
 *  Function: db2disk
 *
 *  Returns: returns 0 if good, exits else
 *  
 *  Description: Takes an ldif database, db, and writes it out to disk
 *               if it can't open the file, there's trouble, so we exit
 *               because this function is usually called after the db
 *               in memory has been updated.
 *               
 */
int
db2disk(Slapi_PBlock *pb, LDIF *db)
{
  ldif_Entry *cur; /*Used for walking down the list*/
  char *buf;       /*temp storage for Entry->ldif converter*/
  FILE *fp;        /*File pointer to ldif target file*/
  int len;         /*length returned by slapi_entry2str*/
   
  /*Open the file*/
  fp = fopen(db->ldif_file, "w");
  if (fp == NULL) {
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    /*This is s pretty serious problem, so we exit*/
    exit(-1);
  }
  
  /*
   * Walk down the list, converting each entry to a string,
   * writing the string out to fp
   */
  for (cur = db->ldif_entries; cur != NULL; cur = cur->next){
    buf = slapi_entry2str(cur->lde_e, &len);    
    fprintf(fp, "%s\n",buf);    
    free ( (void *) buf);
  }
  
  fclose(fp);
  return(0);

}


/*
 *  Function: ldifentry_free
 *
 *  Returns: void
 *  
 *  Description: Frees an ldif_Entry
 */
void 
ldifentry_free(ldif_Entry *e)
{

  /*Make sure that there is actually something to free*/
  if (e == NULL){
    return;
  }

  /*Free the entry*/
  slapi_entry_free(e->lde_e);

  /*Free the entire thing*/
  free ((void *) e);
}

/*
 *  Function: ldifentry_dup
 *
 *  Returns: a pointer to the new ldif_entry, or NULL
 *  
 *  Description: Copies and returns a pointer to a new
 *               ldif_Entry whose contents are a copy of e's contents
 *               Note: uses malloc
 */
ldif_Entry *
ldifentry_dup(ldif_Entry *e)
{
  ldif_Entry *new;

  /*Let's make sure that e is not null*/
  if (e == NULL){
    return(NULL);
  }

  /*Allocate a new ldif_entry, and return it if it is null*/
  new = (ldif_Entry *) malloc( (sizeof(ldif_Entry)));
  if (new == NULL) {
    return(new);
  }
  
  /*Copy the Entry in e*/
  new->lde_e = slapi_entry_dup(e->lde_e);
  new->next = NULL;

  return(new);
  

}

/*
 *  Function: ldif_find_entry
 *
 *  Returns: A pointer to the matched ldif_Entry, or Null
 *  
 *  Description: Goes down the list of entries in db to find the entry
 *               matching dn. Returns a pointer to the entry,
 *          	 and sets prev to point to the entry before the match.
 *               If there is no match, prev points to the last 
 *               entry in the list, and null is returned.
 *               If the first element matches, prev points to NULL
 */
ldif_Entry * 
ldif_find_entry(Slapi_PBlock *pb, LDIF *db, char *dn, ldif_Entry **prev)
{
  ldif_Entry	*cur;               /*Used for walking down the list*/
  char   	*finddn, *targetdn; /*Copies of dns for searching */
  int           found_it = 0;       /*A flag to denote a successful search*/

  /*Set cur to the start of the list*/
  cur =db->ldif_entries;
  
  /*Increase the number of accesses*/
  db->ldif_tries++;

  /*Make a copy of the target dn, and normalize it*/
  targetdn = strdup(dn);
  (void) slapi_dn_normalize(targetdn);


  /*Go down the list until we find the entry*/
  while(cur != NULL) {
    finddn = strdup(slapi_entry_get_dn(cur->lde_e));
    (void) slapi_dn_normalize(finddn);


    /*Test to see if we got the entry matching the dn*/
    if (strcasecmp(targetdn, finddn) == 0)
      {
	found_it = 1;
	free ((void *)finddn);
	db->ldif_hits++;
	break;
      }

    /*Udpate the pointers*/
    *prev = cur; 
    cur = cur->next;
    free ((void *)finddn);

  }

  free ((void *)targetdn);

  
  /*
   * If we didn't find a matching entry, we should
   * return, and let the caller handle this (possible) error
   */
  if (!found_it){
    return(NULL);
  }

  /*
   * If the first entry matches, we have to set prev to null,
   * so the caller knows.
   */
  if (*prev == cur){
    *prev = NULL;
  }
      
  return( cur );
}

/*
 *  Function: apply_mods
 *
 *  Returns: LDAP_SUCCESS if success
 *  
 *  Description: Applies the modifications specified in mods to e.
 */
int 
apply_mods( Slapi_Entry *e, LDAPMod **mods )
{
  int     err, i, j;
  
  LDAPDebug( LDAP_DEBUG_TRACE, "=> apply_mods\n", 0, 0, 0 );
  
  err = LDAP_SUCCESS;
  for ( j = 0; mods[j] != NULL; j++ ) {
    switch ( mods[j]->mod_op & ~LDAP_MOD_BVALUES ) {
    case LDAP_MOD_ADD:
      LDAPDebug( LDAP_DEBUG_ARGS, "   add: %s\n",
		mods[j]->mod_type, 0, 0 );
      err = slapi_entry_add_values( e, mods[j]->mod_type,
			     mods[j]->mod_bvalues );
      break;
      
    case LDAP_MOD_DELETE:
      LDAPDebug( LDAP_DEBUG_ARGS, "   delete: %s\n",
		mods[j]->mod_type, 0, 0 );
      err = slapi_entry_delete_values( e, mods[j]->mod_type,
				mods[j]->mod_bvalues );
      break;
      
    case LDAP_MOD_REPLACE:
      LDAPDebug( LDAP_DEBUG_ARGS, "   replace: %s\n",
		mods[j]->mod_type, 0, 0 );
      err = entry_replace_values( e, mods[j]->mod_type,
				 mods[j]->mod_bvalues );
      break;
    }
    for ( i = 0; mods[j]->mod_bvalues != NULL &&
	 mods[j]->mod_bvalues[i] != NULL; i++ ) {
      LDAPDebug( LDAP_DEBUG_ARGS, "   %s: %s\n",
		mods[j]->mod_type, mods[j]->mod_bvalues[i]->bv_val,
		0 );
    }
    LDAPDebug( LDAP_DEBUG_ARGS, "   -\n", 0, 0, 0 );
    
    if ( err != LDAP_SUCCESS ) {
      break;
    }
  }
  
  LDAPDebug( LDAP_DEBUG_TRACE, "<= apply_mods %d\n", err, 0, 0 );
  return( err );
}
