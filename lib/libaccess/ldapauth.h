/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef LDAP_AUTH_H
#define LDAP_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ldap/ldap.h"

const int LDAP_ACL_SUCCESS = 0;
const int LDAP_ACL_FAILED = -1;

extern int ldap_auth_userdn_groupdn (LDAP *ld, char *userdn,
				     char *groupdn);
extern int ldap_auth_uid_groupdn (LDAP *ld, char *uid,
				  char *groupdn);
extern int ldap_auth_uid_groupid (LDAP *ld, char *uid,
				  char *groupid);
extern int ldap_auth_userdn_groupid (LDAP *ld, char *userdn,
				     char *groupid);
extern int ldap_auth_userdn_attrfilter (LDAP *ld, char *userdn,
					char *attrfilter);
extern int ldap_auth_uid_attrfilter (LDAP *ld, char *uid,
				     char *attrfilter);
extern int ldap_auth_userdn_password (LDAP *ld, char *userdn,
				      char *password);
extern int ldap_find_uid (LDAP *ld, char *uid, LDAPMessage **res);
extern int ldap_auth_uid_password (LDAP *ld, char *uid,
				   char *password);
extern LDAP *init_ldap();

#ifdef __cplusplus
}
#endif

#endif /* LDAP_AUTH_H */
