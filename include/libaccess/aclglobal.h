/** BEGIN COPYRIGHT BLOCK
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
