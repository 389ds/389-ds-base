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
/* schemaparse.c - routines to support objectclass definitions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"


/* global_oc and global_schema_csn are both protected by oc locks */
struct objclass		*global_oc;
CSN *global_schema_csn = NULL; /* Timestamp for last update CSN. NULL = epoch */

static int      is_duplicate( char *target, char **list, int list_max );
static void     normalize_list( char **list );



/* R/W lock used to protect the global objclass linked list. */
static PRRWLock	*oc_lock = NULL;

/*
 * The oc_init_lock_callonce structure is used by NSPR to ensure
 * that oc_init_lock() is called at most once.
 */
static PRCallOnceType oc_init_lock_callonce = { 0, 0, 0 };


/* Create the objectclass read/write lock.  Returns PRSuccess if successful */
static PRStatus
oc_init_lock( void )
{
	if ( NULL == ( oc_lock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,
				"objectclass rwlock" ))) {
		slapi_log_error( SLAPI_LOG_FATAL, "oc_init_lock",
				"PR_NewRWLock() for objectclass lock failed\n" );
		return PR_FAILURE;
	}

	return PR_SUCCESS;
}


void
oc_lock_read( void )
{
	if ( NULL != oc_lock ||
			PR_SUCCESS == PR_CallOnce( &oc_init_lock_callonce, oc_init_lock )) {
		PR_RWLock_Rlock( oc_lock );
	}
}


void
oc_lock_write( void )
{
	if ( NULL != oc_lock ||
			PR_SUCCESS == PR_CallOnce( &oc_init_lock_callonce, oc_init_lock )) {
		PR_RWLock_Wlock( oc_lock );
	}
}


void
oc_unlock( void )
{
	if ( oc_lock != NULL ) {
		PR_RWLock_Unlock( oc_lock );
	}
}


/*
 * Note: callers of g_get_global_oc_nolock() must hold a read or write lock
 */
struct objclass* g_get_global_oc_nolock()
{
	return global_oc;
}

/*
 * Note: callers of g_set_global_oc_nolock() must hold a write lock
 */
void
g_set_global_oc_nolock( struct objclass *newglobaloc )
{
  global_oc = newglobaloc;
}

/*
 * Note: callers of g_get_global_schema_csn() must hold a read lock
 */
const CSN *
g_get_global_schema_csn()
{
  return global_schema_csn;
}

/*
 * Note: callers of g_set_global_schema_csn() must hold a write lock.
 * csn is consumed.
 */
void
g_set_global_schema_csn(CSN *csn)
{
	CSN *tmp = NULL;
	if (NULL != global_schema_csn)
	{
		tmp = global_schema_csn;
	}
	global_schema_csn = csn;
	if (NULL != tmp)
	{
		csn_free(&tmp);
	}
}

/*
 * There are two kinds of objectclasses: 
 * Standard Objectclasses and User Defined Objectclasses
 * 
 * Standard Objectclasses are the objectclasses which come with the Directory Server.
 * These objectclasses are always expected to be there and shouldn't be accidentally
 * changed by the end user. We dont' allow these objectclasses to be deleted, and the 
 * admin CGIs will not allow the end user to change their definitions. However, we 
 * will allow these objectclasses to be redefined via ldap_modify, by doing an LDAP_MOD_ADD.
 * The new definition will override the previous definition. The updated objectclass
 * will be written out the 00user.ldif and the original definition will stay
 * whereever it was originally defined. At startup, slapd will use the last definition
 * read as the real definition of an objectclass.
 *
 * User Defined ObjectClasses are objectclasses which were added to the Directory Server 
 * by the end user. These objectclasses are also kept in 99user.ldif. These objectclasses
 * can be deleted by the end user.
 *
 * Every objectclass contains an array of attributes called oc_orig_required,
 * which are the required attributes for that objectclass which were not inherited from
 * any other objectclass. Likewise, there's also an array called oc_orig_allowed which
 * contains the allowed attributes which were not inherited from any other objectclass.
 *
 * The arrays oc_required and oc_allowed contain all the required and allowed attributes for
 * that objectclass, including the ones inherited from its parent and also the ones in
 * oc_orig_required and oc_orig_allowed. 
 *
 * When an oc is updated, we go through the global list of objectclasses and see if
 * any ocs inherited from it. If so, we delete its oc_required and oc_allowed arrays,
 * copy the oc_orig_required and oc_orig_allowed arrays to oc_required and oc_allowed, 
 * and then merge the parent's oc_required and oc_allowed onto oc_required and oc_allowed.
 *
 *
 */


static int
is_duplicate( char *target, char **list, int list_size ) {
	  int i;
	  for ( i = 0; i < list_size; i++ ) {
		  if ( !strcasecmp( target, list[i] ) ) {
			  return 1;
		  }
	  }
	  return 0;
}

/*
 * Make normalized copies of all non-duplicate values in a list; free all old
 * values. The list is not resized.
 */
static void
normalize_list( char **list ) {
	int i, j;

	for ( i = 0, j = 0; list != NULL && list[i] != NULL; i++ ) {
		char *norm = slapi_attr_syntax_normalize( list[i] );
		char *save = list[i];
		if ( !is_duplicate( norm, list, j ) ) {
			list[j++] = norm;
		} else {
			slapi_ch_free((void **)&norm );
		}
		slapi_ch_free((void**)&save );
	}
	for ( ; j < i; j++ ) {
		list[j] = NULL;
	}
}

/*
 * normalize types contained in object class definitions. do this
 * after the whole config file is read so there is no order dependency
 * on inclusion of attributes and object classes.
 */

void
normalize_oc( void )
{
	struct objclass	*oc;

	oc_lock_write();

	for ( oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next ) {
	  LDAPDebug (LDAP_DEBUG_PARSE, 
				 "normalize_oc: normalizing '%s'\n", oc->oc_name, 0, 0);
	  /* required attributes */
	  normalize_list( oc->oc_required );
	  normalize_list( oc->oc_orig_required );
	  
	  /* optional attributes */
	  normalize_list( oc->oc_allowed );
	  normalize_list( oc->oc_orig_allowed );
	}

	oc_unlock();
}

/*
 * oc_update_inheritance_nolock: 
 * If an objectclass is redefined, we need to make sure that any objectclasses
 * which inherit from the redefined objectclass have their required and allowed
 * attributes updated.
 * 
 * Every objectclass contains an array of attributes called oc_orig_required,
 * which are the required attributes for that objectclass which were not inherited from
 * any other objectclass. Likewise, there's also an array called oc_orig_allowed which
 * contains the allowed attributes which were not inherited from any other objectclass.
 *
 * The arrays oc_required and oc_allowed contain all the required and allowed attributes for
 * that objectclass, including the ones inherited from its parent and also the ones in
 * oc_orig_required and oc_orig_allowed. 
 *
 * When an oc is updated, we go through the global list of objectclasses and see if
 * any ocs inherited from it. If so, we delete its oc_requried and oc_allowed arrays,
 * copy the oc_orig_required and oc_orig_allowed arrays to oc_required and oc_allowed, 
 * and then merge the parent's oc_required and oc_allowed onto oc_required and oc_allowed.
 */

void
oc_update_inheritance_nolock( struct objclass *psuperior_oc )
{
  struct objclass *oc;
  
  for ( oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next ) {
	if ( oc->oc_superior && 
		 (strcasecmp( oc->oc_superior, psuperior_oc->oc_name ) == 0) ) {
	  if (oc->oc_required ) {
		charray_free (oc->oc_required);
	  }
	  if (oc->oc_allowed) {
		charray_free (oc->oc_allowed);
	  }
	  oc->oc_required = charray_dup ( oc->oc_orig_required );
	  oc->oc_allowed = charray_dup ( oc->oc_orig_allowed );
	  charray_merge ( &(oc->oc_required), psuperior_oc->oc_required, 1 );
	  charray_merge ( &(oc->oc_allowed), psuperior_oc->oc_allowed, 1 );
	  oc_update_inheritance_nolock ( oc );
	}
  }
}
