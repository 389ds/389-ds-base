/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef ACL_AUTH_H
#define ACL_AUTH_H

#include <ldap.h>
#include <base/plist.h>
#include <ldaputil/ldapdb.h>
#include <libaccess/nserror.h>

NSPR_BEGIN_EXTERN_C

extern void init_ldb_rwlock ();

NSAPI_PUBLIC extern int parse_ldap_url (NSErr_t *errp, ACLDbType_t dbtype,
					const char *name, const char *url,
					PList_t plist, void **db);

extern int get_is_valid_password_basic_ldap (NSErr_t *errp,
					     PList_t subject,
					     PList_t resource,
					     PList_t auth_info,
					     PList_t global_auth,
					     void *arg);

extern int get_user_ismember_ldap (NSErr_t *errp,
				   PList_t subject,
				   PList_t resource,
				   PList_t auth_info,
				   PList_t global_auth,
				   void *arg);

extern int get_userdn_ldap (NSErr_t *errp,
			    PList_t subject,
			    PList_t resource,
			    PList_t auth_info,
			    PList_t global_auth,
			    void *arg);

extern int ACL_NeedLDAPOverSSL();

extern int acl_map_cert_to_user (NSErr_t *errp, const char *dbname,
				 LDAPDatabase_t *ldb, void *cert,
				 PList_t resource, pool_handle_t *pool,
				 char **user, char **userdn);

extern int get_user_exists_ldap (NSErr_t *errp, PList_t subject,
				 PList_t resource, PList_t auth_info,
				 PList_t global_auth, void *unused);

NSAPI_PUBLIC extern int acl_user_exists (const char *user,
					 const char *userdn,
					 const char *dbname,
					 const int logerr);

NSPR_END_EXTERN_C

#endif /* ACL_AUTH_H */
