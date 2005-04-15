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
 *  File: init.c
 *
 *  Functions:
 * 
 *      ldif_back_init() - ldap ldif back-end initialize routine 
 *
 */

#include "back-ldif.h"

static Slapi_PluginDesc pdesc = { "ldif-backend", "Netscape", PRODUCTTEXT,
	"LDIF backend database plugin" };

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/*
 *  Function: ldif_back_init
 *
 *  Returns: returns 0 if good, -1 else.
 *  
 *  Description: Allocates a database for filling by ldif_back_config
 */
int
ldif_back_init( Slapi_PBlock *pb )
{
  LDIF *db;    /*This will hold the ldif file in memory*/
  int rc;
  
  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_init\n", 0, 0, 0 );
  
  /*
   * Allocate and initialize db with everything we
   * need to keep track of in this backend. In ldif_back_config(),
   * we will fill in db with things like the name
   * of the ldif file containing the database, and any other
   * options we allow people to set through the config file.
   */
  
  /*Allocate memory for our database and check if success*/
  db = (LDIF *) malloc( sizeof(LDIF) );
  if (db == NULL) {
    LDAPDebug(LDAP_DEBUG_ANY, "Ldif Backend: unable to initialize; out of memory\n", 0, 0, 0);
    return(-1);
  }
  
  /*Fill with initial values, including the mutex*/
  db->ldif_n = 0;
  db->ldif_entries = NULL;
  db->ldif_tries = 0;
  db->ldif_hits = 0;
  db->ldif_file = NULL;
  db->ldif_lock = PR_NewLock();
  if (&db->ldif_lock == NULL) {
    LDAPDebug(LDAP_DEBUG_ANY, "Ldif Backend: Lock creation failed\n", 0, 0, 0);
    return(-1);
  }

  
  /*
   * set SLAPI_PLUGIN_PRIVATE field in pb, so it's available
   * later in ldif_back_config(), ldif_back_search(), etc.
   */
  rc = slapi_pblock_set( pb, SLAPI_PLUGIN_PRIVATE, (void *) db );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *) &pdesc );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
      (void *) SLAPI_PLUGIN_VERSION_01 );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_BIND_FN,
      (void *) ldif_back_bind );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_UNBIND_FN,
      (void *) ldif_back_unbind );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_SEARCH_FN,
      (void *) ldif_back_search );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_COMPARE_FN,
      (void *) ldif_back_compare );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_MODIFY_FN,
      (void *) ldif_back_modify );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_MODRDN_FN,
      (void *) ldif_back_modrdn );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ADD_FN,
      (void *) ldif_back_add );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_DELETE_FN,
      (void *) ldif_back_delete );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_CONFIG_FN,
      (void *) ldif_back_config );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN,
      (void *) ldif_back_close );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_FLUSH_FN,
      (void *) ldif_back_flush );
  rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
      (void *) ldif_back_start );
  if (rc != 0) {
    LDAPDebug(LDAP_DEBUG_ANY, "Ldif Backend: unable to pass database information to front end\n",0 ,0 ,0);
    return(-1);
  }
  
  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_init\n", 0, 0, 0 );
  
  return( 0 );
}


