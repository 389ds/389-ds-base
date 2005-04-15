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
 * do so, delete this exception statement from your version. 
 * 
 * 
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
