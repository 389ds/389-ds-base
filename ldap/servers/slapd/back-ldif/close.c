/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: close.c
 *
 *  Functions:
 * 
 *      ldif_back_close() - ldap ldif back-end close routine
 *
 */

#include "back-ldif.h"

/*
 *  Function: ldif_free_db
 *
 *  Returns: void
 *  
 *  Description: frees up the ldif database
 */
void
ldif_free_db(LDIF *db)
{
  ldif_Entry *cur;  /*Used for walking down the list*/

  /*If db is null, there is nothing to do*/
  if (db == NULL) {
    return;
  }

  /*Walk down the list, freeing up the ldif_entries*/
  for (cur = db->ldif_entries; cur != NULL; cur = cur->next){
    ldifentry_free(cur);
  }
  
  /*Free the ldif_file string, and then the db itself*/
  free ((void *)db->ldif_file);
  free((void *) db);
}



/*
 *  Function: ldif_back_close
 *
 *  Returns: void 
 *  
 *  Description: closes the ldif backend, frees up the database
 */
void
ldif_back_close( Slapi_PBlock *pb )
{
  LDIF   *db;
  
  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend syncing\n", 0, 0, 0 );
  slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db );
  ldif_free_db(db);
  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend done syncing\n", 0, 0, 0 );
}

/*
 *  Function: ldif_back_flush
 *
 *  Returns: void
 *  
 *  Description: does nothing
 */
void
ldif_back_flush( Slapi_PBlock *pb )
{
  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend flushing\n", 0, 0, 0 );
  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend done flushing\n", 0, 0, 0 );
  return;
}


