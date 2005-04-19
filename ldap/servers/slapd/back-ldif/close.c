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


