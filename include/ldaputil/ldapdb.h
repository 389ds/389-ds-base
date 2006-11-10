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

#ifndef _LDAPU_LDAPDB_H
#define _LDAPU_LDAPDB_H

#include <ldap.h>
/* In the past, we used CRITICAL objects from lib/base/crit.cpp.
 * Now we use PRMonitor to avoid ldapu to depend on lib/base.
 */
#include <prmon.h>

#ifndef NSAPI_PUBLIC
#ifdef XP_WIN32
#define NSAPI_PUBLIC __declspec(dllexport)
#else
#define NSAPI_PUBLIC 
#endif
#endif

#define LDAPDB_URL_PREFIX "ldapdb:"
#define LDAPDB_URL_PREFIX_LEN 7

typedef struct {
    int use_ssl;    /* Set to 0 in case of local LDAP cache */
    char *host;     /* Set to 0 in case of local LDAP cache */
    int port;	    /* Set to 0 in case of local LDAP cache */
    char *basedn;
    char *scope;
    char *filter;
    LDAP *ld;
    char *binddn;   /* Set to 0 in case of local LDAP cache */
    char *bindpw;   /* Set to 0 in case of local LDAP cache */
    int bound;	    /* If 0 then not bound with binddn & bindpw */
    PRMonitor* crit;/* to control critical sections */
} LDAPDatabase_t;

#define LDAPU_ATTR_BINDDN	"binddn"
#define LDAPU_ATTR_BINDPW	"bindpw"


#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC extern int ldapu_url_parse (const char *url, const char *binddn,
					 const char *bindpw,
					 LDAPDatabase_t **ldb);

NSAPI_PUBLIC extern int ldapu_ldapdb_url_parse (const char *url,
						LDAPDatabase_t **ldb);

NSAPI_PUBLIC extern int ldapu_is_local_db (const LDAPDatabase_t *ldb);

NSAPI_PUBLIC extern void ldapu_free_LDAPDatabase_t (LDAPDatabase_t *ldb);

NSAPI_PUBLIC extern LDAPDatabase_t *ldapu_copy_LDAPDatabase_t (const LDAPDatabase_t *ldb);

NSAPI_PUBLIC extern int ldapu_ldap_init (LDAPDatabase_t *ldb);

NSAPI_PUBLIC extern int ldapu_ldap_init_and_bind (LDAPDatabase_t *ldb);

NSAPI_PUBLIC extern int ldapu_ldap_rebind (LDAPDatabase_t *ldb);

NSAPI_PUBLIC extern int ldapu_ldap_reinit_and_rebind (LDAPDatabase_t *ldb);

#ifdef __cplusplus
}
#endif

/*
 * LDAPU_REQ --
 * 'ld' is cached in the 'ldb' structure.  If the LDAP server goes down since
 * it was cached, the ldap lookup commands fail with LDAP_SERVER_DOWN.  This
 * macro can be used to rebind to the server and retry the command once if
 * this happens.
 */
#define LDAPU_REQ(rv, ldb, cmd) \
{ \
      int numtry = 0; \
      while(1) { \
	  rv = cmd; \
	  if (rv != LDAP_SERVER_DOWN || numtry++ != 0) break; \
	  /* Server went down since our last ldap lookup ... reconnect */ \
	  rv = ldapu_ldap_reinit_and_rebind(ldb); \
	  if (rv != LDAPU_SUCCESS) break; \
      } \
}


#endif /* LDAPUTIL_LDAPDB_H */
