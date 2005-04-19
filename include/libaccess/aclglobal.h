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
**	Header file containing global data elements.  These are duplicated
**	when a cache flush is done.
*/

#include	<libaccess/acl.h>

struct ACLGlobal_s {
	ACLListHandle_t	*masterlist;
	pool_handle_t	*pool;	/* Deallocate at the start of cache flush */
	pool_handle_t	*databasepool;
	pool_handle_t	*methodpool;
	PRHashTable	*urihash;
	PRHashTable	*urigethash;
	PRHashTable	*listhash;
	PRHashTable	*evalhash;
	PRHashTable	*flushhash;
	PRHashTable	*methodhash;
	PRHashTable	*dbtypehash;
	PRHashTable	*dbnamehash;
	PRHashTable	*attrgetterhash;
	PRHashTable	*userLdbHash; /* user's LDAP handle hash */
};

typedef struct ACLGlobal_s ACLGlobal_t;
typedef struct ACLGlobal_s *ACLGlobal_p;

#define acl_uri_hash_pool	ACLGlobal->pool
#define acl_uri_hash		ACLGlobal->urihash
#define acl_uri_get_hash	ACLGlobal->urigethash
#define ACLListHash		ACLGlobal->listhash
#define	ACLLasEvalHash		ACLGlobal->evalhash
#define ACLLasFlushHash		ACLGlobal->flushhash
#define ACLMethodHash		ACLGlobal->methodhash
#define	ACLDbTypeHash		ACLGlobal->dbtypehash
#define	ACLDbNameHash		ACLGlobal->dbnamehash
#define	ACLAttrGetterHash	ACLGlobal->attrgetterhash
#define	ACLUserLdbHash		ACLGlobal->userLdbHash
#define ACL_DATABASE_POOL	ACLGlobal->databasepool
#define ACL_METHOD_POOL		ACLGlobal->methodpool

NSPR_BEGIN_EXTERN_C

extern ACLGlobal_p ACLGlobal;
extern ACLGlobal_p oldACLGlobal;

NSPR_END_EXTERN_C
