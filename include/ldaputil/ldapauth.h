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
