/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _LDAPU_LDAPDB_H
#define _LDAPU_LDAPDB_H

#include <ldap.h>
/* removed for LDAPSDK31 integration
#include <lcache.h>
*/
#ifdef LDAPDB_THREAD_SAFE
/* In the past, we used CRITICAL objects from lib/base/crit.cpp.
 * Now we use PRMonitor to avoid ldapu to depend on lib/base.
 */
#include <prmon.h>
#else
#define PRMonitor void
#endif /* LDAPDB_THREAD_SAFE */

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
