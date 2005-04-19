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
/* uniqueid2entry.c - given a dn return an entry */

#include "back-ldbm.h"

/*
 * uniqueid2entry - look up uniqueid in the cache/indexes and return the
 * corresponding entry. 
 */

struct backentry *
uniqueid2entry(
		backend *be, 
		const char *uniqueid, 
		back_txn *txn, 
		int *err
)
{
#ifdef UUIDCACHE_ON 
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
#endif
	struct berval		idv;
	IDList			*idl = NULL;
	struct backentry	*e = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> uniqueid2entry \"%s\"\n", uniqueid,
		   0, 0 );
#ifdef UUIDCACHE_ON 
	e = cache_find_uuid(&inst->inst_cache, uniqueid);
#endif
	if (e == NULL)	{
		/* convert dn to entry id */
		PR_ASSERT(uniqueid);
		*err = 0;
		idv.bv_val = (void*)uniqueid; 
		idv.bv_len = strlen( idv.bv_val );

		if ( (idl = index_read( be, SLAPI_ATTR_UNIQUEID, indextype_EQUALITY, &idv, txn,
		    err )) == NULL ) {
			if ( *err != 0 && *err != DB_NOTFOUND ) {
				goto ext;
			}
		} else {	
			/* convert entry id to entry */
			if ( (e = id2entry( be, idl_firstid( idl ), txn, err ))
			    != NULL ) {
				goto ext;
			} else {
				if ( *err != 0 && *err != DB_NOTFOUND ) {
					goto ext;
				}
				/*
				 * this is pretty bad anyway. the dn was in the
				 * SLAPI_ATTR_UNIQUEID index, but we could not
				 * read the entry from the id2entry index.
				 * what should we do?
				 */
			}
		}
	} else {
		goto ext;
	}

ext:
	if (NULL != idl) {
		slapi_ch_free( (void**)&idl);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= uniqueid2entry %p\n", e, 0, 0 );
	return( e );
}
