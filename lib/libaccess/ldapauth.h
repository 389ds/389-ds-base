/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#ifndef LDAP_AUTH_H
#define LDAP_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ldap/ldap.h"

const int LDAP_ACL_SUCCESS = 0;
const int LDAP_ACL_FAILED = -1;

extern int ldap_auth_userdn_groupdn(LDAP *ld, char *userdn, char *groupdn);
extern int ldap_auth_uid_groupdn(LDAP *ld, char *uid, char *groupdn);
extern int ldap_auth_uid_groupid(LDAP *ld, char *uid, char *groupid);
extern int ldap_auth_userdn_groupid(LDAP *ld, char *userdn, char *groupid);
extern int ldap_auth_userdn_attrfilter(LDAP *ld, char *userdn, char *attrfilter);
extern int ldap_auth_uid_attrfilter(LDAP *ld, char *uid, char *attrfilter);
extern int ldap_auth_userdn_password(LDAP *ld, char *userdn, char *password);
extern int ldap_find_uid(LDAP *ld, char *uid, LDAPMessage **res);
extern int ldap_auth_uid_password(LDAP *ld, char *uid, char *password);
extern LDAP *init_ldap();

#ifdef __cplusplus
}
#endif

#endif /* LDAP_AUTH_H */
