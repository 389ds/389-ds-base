/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _CERTMAP_PLUGIN_H
#define _CERTMAP_PLUGIN_H

extern CertSearchFn_t default_searchfn;

#ifdef __cplusplus
extern "C" {
#endif

extern int plugin_mapping_fn (void *cert, LDAP *ld, void *certmap_info,
			      char **ldapDN, char **filter);

extern int plugin_search_fn (void *cert, LDAP *ld, void *certmap_info,
			     const char *basedn,
			     const char *dn, const char *filter,
			     const char **attrs, LDAPMessage **res);

extern int plugin_verify_fn (void *cert, LDAP *ld, void *certmap_info,
			     LDAPMessage *res, LDAPMessage **entry);

NSAPI_PUBLIC int plugin_init_fn (void *certmap_info, const char *issuerName,
				 const char *issuerDN, const char *dllname);

#ifdef __cplusplus
}
#endif

#endif /* _CERTMAP_PLUGIN_H */
