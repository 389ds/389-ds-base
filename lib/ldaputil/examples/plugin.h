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

#ifndef _CERTMAP_PLUGIN_H
#define _CERTMAP_PLUGIN_H

extern CertSearchFn_t default_searchfn;

#ifdef __cplusplus
extern "C" {
#endif

extern int plugin_mapping_fn(void *cert, LDAP *ld, void *certmap_info, char **ldapDN, char **filter);

extern int plugin_search_fn(void *cert, LDAP *ld, void *certmap_info, const char *basedn, const char *dn, const char *filter, const char **attrs, LDAPMessage **res);

extern int plugin_verify_fn(void *cert, LDAP *ld, void *certmap_info, LDAPMessage *res, LDAPMessage **entry);

NSAPI_PUBLIC int plugin_init_fn(void *certmap_info, const char *issuerName, const char *issuerDN, const char *dllname);

#ifdef __cplusplus
}
#endif

#endif /* _CERTMAP_PLUGIN_H */
