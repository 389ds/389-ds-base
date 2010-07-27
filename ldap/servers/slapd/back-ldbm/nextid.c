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

/* id.c - keep track of the next id to be given out */

#include "back-ldbm.h"

ID
next_id(backend *be)
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
    ID id;

	/*Lock*/
	PR_Lock( inst->inst_nextid_mutex );

	/*Test if nextid hasn't been initialized. */
	if (inst->inst_nextid < 1) {
	  LDAPDebug( LDAP_DEBUG_ANY, 
		    "ldbm backend instance: nextid not initialized... exiting.\n", 0,0,0);
	  exit(1);
	}

	/*Increment the in-memory nextid*/
	inst->inst_nextid++;

	id = inst->inst_nextid - 1;
	
	/*unlock*/
	PR_Unlock( inst->inst_nextid_mutex );

	/* if ID is above the threshold, the database may need rebuilding soon */
	if (id >= ID_WARNING_THRESHOLD) {
	  if ( id >= MAXID ) {
	    LDAPDebug( LDAP_DEBUG_ANY,
		       "ldbm backend instance: FATAL ERROR: backend '%s' has no"
		       "IDs left. DATABASE MUST BE REBUILT.\n", be->be_name, 0,
		       0);
	    id = MAXID;
	  } else {
	    LDAPDebug( LDAP_DEBUG_ANY,
		       "ldbm backend instance: WARNING: backend '%s' may run out "
		       "of IDs. Please, rebuild database.\n", be->be_name, 0, 0);
	  }
	}
	return( id );
}

void
next_id_return( backend *be, ID id )
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;

	/*Lock*/
	PR_Lock( inst->inst_nextid_mutex );

	/*Test if nextid hasn't been initialized. */
	if (inst->inst_nextid < 1) {
	  LDAPDebug( LDAP_DEBUG_ANY, 
		    "ldbm backend instance: nextid not initialized... exiting\n", 0,0,0);
	  exit(1);
	}

	if ( id != inst->inst_nextid - 1 ) {
		PR_Unlock( inst->inst_nextid_mutex );
		return;
	}

	/*decrement the in-memory version*/
	inst->inst_nextid--;

	/*unlock this bad boy*/
	PR_Unlock( inst->inst_nextid_mutex );
}

ID
next_id_get( backend *be )
{
  ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
  ID id;
  
  /*lock*/
  PR_Lock( inst->inst_nextid_mutex );

  /*Test if nextid hasn't been initialized.*/
  if (inst->inst_nextid < 1) {
    LDAPDebug( LDAP_DEBUG_ANY, 
	      "ldbm backend instance: nextid not initialized... exiting\n", 0,0,0);
    exit(1);
  }
  
  id = inst->inst_nextid;
  PR_Unlock( inst->inst_nextid_mutex );
  
  return( id );
}

/*
 *  Function: get_ids_from_disk
 *
 *  Returns: squat
 *  
 *  Description: Opend the id2entry file and obtains the largest
 *               ID in use, and sets li->li_nextid.  If no IDs
 *               could be read from id2entry, li->li_nextid
 *               is set to 1.
 */
void
get_ids_from_disk(backend *be)
{
  ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
  DB *id2entrydb;          /*the id2entry database*/
  int return_value = -1;

  /*For the nextid, we go directly to the id2entry database,
   and grab the max ID*/
  
  /*Get a copy of the id2entry database*/
	if ( (return_value = dblayer_get_id2entry( be, &id2entrydb )) != 0 ) {
		id2entrydb = NULL;
	}

  /* lock the nextid mutex*/
  PR_Lock( inst->inst_nextid_mutex );

  /*
   * If there is no id2entry database, then we can assume that there
   * are no entries, and that nextid should be 1
   */
  if (id2entrydb == NULL) {	 
    inst->inst_nextid = 1;

    /* unlock */
    PR_Unlock( inst->inst_nextid_mutex );
    return;

  } else {
    
    /*Get the last key*/
	DBC *dbc = NULL;
	DBT key = {0};              /*For the nextid*/
	DBT Value = {0};

	Value.flags = DB_DBT_MALLOC;
	key.flags = DB_DBT_MALLOC;
	return_value = id2entrydb->cursor(id2entrydb,NULL,&dbc,0);
	if (0 == return_value) {
		return_value = dbc->c_get(dbc,&key,&Value,DB_LAST);
		if ( (0 == return_value) && (NULL != key.dptr) ) {
			inst->inst_nextid = id_stored_to_internal(key.dptr) + 1;
		} else {
			inst->inst_nextid = 1;	/* error case: set 1 */
		}
		slapi_ch_free(&(key.data));
		slapi_ch_free(&(Value.data));
		dbc->c_close(dbc);
	} else {
      inst->inst_nextid = 1;	/* when there is no id2entry, start from id 1 */
	}

  }
  
  /*close the cache*/
  dblayer_release_id2entry( be, id2entrydb );

  /* unlock */
  PR_Unlock( inst->inst_nextid_mutex );

  return;
}


/* routines to turn an internal machine-representation ID into the one we store (big-endian) */

void id_internal_to_stored(ID i,char *b)
{
	if ( sizeof(ID) > 4 ) {
		(void)memset (b+4, 0, sizeof(ID)-4);
	}

	b[0] = (char)(i >> 24);
	b[1] = (char)(i >> 16);
	b[2] = (char)(i >> 8);
	b[3] = (char)i;
}

ID id_stored_to_internal(char* b)
{
	ID i;
	i = (ID)b[3] & 0x000000ff;
	i |= (((ID)b[2]) << 8) & 0x0000ff00;
	i |= (((ID)b[1]) << 16) & 0x00ff0000;
	i |= ((ID)b[0]) << 24;
	return i;
}

void sizeushort_internal_to_stored(size_t i,char *b)
{
	PRUint16 ui = (PRUint16)(i & 0xffff);
	b[0] = (char)(ui >> 8);
	b[1] = (char)ui;
}

size_t sizeushort_stored_to_internal(char* b)
{
	size_t i;
	i = (PRUint16)b[1] & 0x000000ff;
	i |= (((PRUint16)b[0]) << 8) & 0x0000ff00;
	return i;
}
