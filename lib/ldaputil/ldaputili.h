/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _LDAPU_LDAPUTILI_H
#define _LDAPU_LDAPUTILI_H

#include <ldaputil/ldaputil.h>

#include <ssl.h>

#define BIG_LINE 1024

extern const int SEC_OID_AVA_UNKNOWN; /* unknown OID */

#ifdef __cplusplus
extern "C" {
#endif

SECStatus CERT_RFC1485_EscapeAndQuote (char *dst, int dstlen, char *src, int srclen);

extern void* ldapu_list_empty (LDAPUList_t* list, LDAPUListNodeFn_t free_fn, void* arg);
extern void  ldapu_list_move  (LDAPUList_t* from, LDAPUList_t* into);

extern int ldapu_get_cert_ava_val (void *cert_in, int which_dn,
				   const char *attr, char ***val_out);

extern int ldapu_member_certificate_match (void* cert, const char* desc);

/* Each of several LDAP API functions has a counterpart here.
 * They behave the same, but their implementation may be replaced
 * by calling ldapu_VTable_set(); as Directory Server does.
 */
#ifdef USE_LDAP_SSL
extern LDAP*  ldapu_ssl_init( const char *host, int port, int encrypted );
#else
extern LDAP*  ldapu_init    ( const char *host, int port );
#endif
extern int    ldapu_set_option( LDAP *ld, int opt, void *val );
extern int    ldapu_simple_bind_s( LDAP* ld, const char *username, const char *passwd );
extern int    ldapu_unbind( LDAP *ld );
extern int    ldapu_search_s( LDAP *ld, const char *base, int scope,
		              const char *filter, char **attrs, int attrsonly, LDAPMessage **res );
extern int    ldapu_count_entries( LDAP *ld, LDAPMessage *chain );
extern LDAPMessage* ldapu_first_entry( LDAP *ld, LDAPMessage *chain );
extern LDAPMessage* ldapu_next_entry( LDAP *ld, LDAPMessage *entry );
extern int    ldapu_msgfree( LDAP *ld, LDAPMessage *chain );
extern char*  ldapu_get_dn( LDAP *ld, LDAPMessage *entry );
extern void   ldapu_memfree( LDAP *ld, void *dn );
extern char*  ldapu_first_attribute( LDAP *ld, LDAPMessage *entry, BerElement **ber );
extern char*  ldapu_next_attribute( LDAP *ld, LDAPMessage *entry, BerElement *ber );
extern void   ldapu_ber_free( LDAP *ld, BerElement *ber, int freebuf );
extern char** ldapu_get_values( LDAP *ld, LDAPMessage *entry, const char *target );
extern struct berval** ldapu_get_values_len( LDAP *ld, LDAPMessage *entry, const char *target );
extern void   ldapu_value_free( LDAP *ld, char **vals );
extern void   ldapu_value_free_len( LDAP *ld, struct berval **vals );

#ifdef __cplusplus
}
#endif
#endif
