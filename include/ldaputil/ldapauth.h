/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef LDAPU_AUTH_H
#define LDAPU_AUTH_H

#include <ldap.h>

#ifndef NSAPI_PUBLIC
#ifdef XP_WIN32
#define NSAPI_PUBLIC __declspec(dllexport)
#else
#define NSAPI_PUBLIC 
#endif
#endif

typedef int (*LDAPU_GroupCmpFn_t)(const void *groupids, const char *group,
				  const int len);

#ifdef __cplusplus
extern "C" {
#endif

extern int ldapu_find (LDAP *ld, const char *base, int scope,
		       const char *filter, const char **attrs,
		       int attrsonly, LDAPMessage **res);

int ldapu_find_entire_tree (LDAP *ld, int scope,
			    const char *filter, const char **attrs,
			    int attrsonly, LDAPMessage ***res);

extern int ldapu_auth_userdn_groupdn (LDAP *ld, const char *userdn,
				      const char *groupdn,
				      const char *base);

extern int ldapu_auth_uid_groupdn (LDAP *ld, const char *uid,
				   const char *groupdn, const char *base);

extern int ldapu_auth_uid_groupid (LDAP *ld, const char *uid,
				   const char *groupid, const char *base);

extern int ldapu_auth_userdn_groupid (LDAP *ld,
				      const char *userdn, const char *groupid,
				      const char *base);

extern int ldapu_auth_userdn_groupids (LDAP *ld, const char *userdn,
				       void *groupids,
				       LDAPU_GroupCmpFn_t grpcmpfn,
				       const char *base,
				       char **group_out);

extern int ldapu_auth_userdn_attrfilter (LDAP *ld,
					 const char *userdn,
					 const char *attrfilter);

extern int ldapu_auth_uid_attrfilter (LDAP *ld, const char *uid,
				      const char *attrfilter,
				      const char *base);

extern int ldapu_auth_userdn_password (LDAP *ld,
				       const char *userdn,
				       const char *password);

extern int ldapu_find_uid_attrs (LDAP *ld, const char *uid,
				 const char *base, const char **attrs,
				 int attrsonly, LDAPMessage **res);

extern int ldapu_find_uid (LDAP *ld, const char *uid,
			   const char *base, LDAPMessage **res);

NSAPI_PUBLIC extern int ldapu_find_userdn (LDAP *ld, const char *uid,
			      const char *base, char **dn);

extern int ldapu_find_group_attrs (LDAP *ld, const char *groupid,
				   const char *base, const char **attrs,
				   int attrsonly, LDAPMessage **res);

extern int ldapu_find_group (LDAP *ld, const char *groupid,
			     const char *base, LDAPMessage **res);

extern int ldapu_find_groupdn (LDAP *ld, const char *groupid,
			       const char *base, char **dn);

extern int ldapu_auth_uid_password (LDAP *ld, const char *uid,
				    const char *password, const char *base);

#ifdef __cplusplus
}
#endif

#endif /* LDAPU_AUTH_H */
