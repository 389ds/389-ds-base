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

#ifndef _LDAPU_LDAPUTIL_H
#define _LDAPU_LDAPUTIL_H

#include <ldaputil/dbconf.h>
#include <ldaputil/certmap.h>

typedef struct ldapu_list_node {
    void *info;				/* pointer to the corresponding info */
    struct ldapu_list_node *next;	/* pointer to the next node */
    struct ldapu_list_node *prev;	/* pointer to the prev node */
} LDAPUListNode_t;

typedef struct ldapu_list {
    LDAPUListNode_t *head;
    LDAPUListNode_t *tail;
} LDAPUList_t;

typedef struct {
    char *prop;			/* property name */
    char *val;			/* value -- only char* supported for now */
} LDAPUPropVal_t;

typedef LDAPUList_t LDAPUPropValList_t;

enum {
    COMPS_COMMENTED_OUT,
    COMPS_EMPTY,
    COMPS_HAS_ATTRS
};

typedef struct {
    char *issuerName;		  /* issuer (symbolic/short) name */
    char *issuerDN;		  /* cert issuer's DN */
    LDAPUPropValList_t *propval;  /* pointer to the prop-val pairs list */
    CertMapFn_t mapfn;		  /* cert to ldapdn & filter mapping func */
    CertVerifyFn_t verifyfn;	  /* verify cert function */
    CertSearchFn_t searchfn;	  /* search ldap entry function */
    long dncomps;		  /* bitmask: components to form ldap dn */
    long filtercomps;		  /* components used to form ldap filter */
    int verifyCert;		  /* Verify the cert? */
    char *searchAttr;		  /* LDAP attr used by the search fn */
    int dncompsState;		  /* Empty, commented out, or attr names */
    int filtercompsState;	  /* Empty, commented out, or attr names */
} LDAPUCertMapInfo_t;

typedef LDAPUList_t LDAPUCertMapListInfo_t;

typedef void * (*LDAPUListNodeFn_t)(void *info, void *arg);

#ifdef __cplusplus
extern "C" {
#endif

extern int certmap_read_default_certinfo (const char *file);

extern int certmap_read_certconfig_file (const char *file);

extern void ldapu_certinfo_free (void *certmap_info);

extern void ldapu_certmap_listinfo_free (void *certmap_listinfo);

extern void ldapu_propval_list_free (void *propval_list);

NSAPI_PUBLIC extern int ldaputil_exit ();

NSAPI_PUBLIC extern int ldapu_cert_to_user (void *cert, LDAP *ld,
					    const char *basedn,
					    LDAPMessage **res,
					    char **user);

NSAPI_PUBLIC extern int ldapu_certmap_init (const char *config_file,
					    const char *libname,
					    LDAPUCertMapListInfo_t **certmap_list,
					    LDAPUCertMapInfo_t
					    **certmap_default);

NSAPI_PUBLIC extern int ldapu_certinfo_modify (const char *issuerName,
					       const char *issuerDN,
					       const LDAPUPropValList_t *propval);

NSAPI_PUBLIC extern int ldapu_certinfo_delete (const char *issuerDN);

NSAPI_PUBLIC extern int ldapu_certinfo_save (const char *fname,
					     const char *old_fname,
					     const char *tmp_fname);

NSAPI_PUBLIC extern int ldapu_list_alloc (LDAPUList_t **list);
NSAPI_PUBLIC extern int ldapu_propval_alloc (const char *prop, const char *val,
					     LDAPUPropVal_t **propval);
NSAPI_PUBLIC extern int ldapu_list_add_info (LDAPUList_t *list, void *info);

#ifndef DONT_USE_LDAP_SSL
#define USE_LDAP_SSL
#endif

typedef struct {
#ifdef USE_LDAP_SSL
    LDAP*       (LDAP_CALL LDAP_CALLBACK *ldapuV_ssl_init)         ( const char*, int, int );
#else
    LDAP*       (LDAP_CALL LDAP_CALLBACK *ldapuV_init)             ( const char*, int );
#endif
    int         (LDAP_CALL LDAP_CALLBACK *ldapuV_set_option)       ( LDAP*, int, void* );
    int         (LDAP_CALL LDAP_CALLBACK *ldapuV_simple_bind_s)    ( LDAP*, const char*, const char* );
    int         (LDAP_CALL LDAP_CALLBACK *ldapuV_unbind)           ( LDAP* );
    int         (LDAP_CALL LDAP_CALLBACK *ldapuV_search_s)         ( LDAP*, const char*, int, const char*, char**, int, LDAPMessage** );
    int         (LDAP_CALL LDAP_CALLBACK *ldapuV_count_entries)    ( LDAP*, LDAPMessage* );
    LDAPMessage*(LDAP_CALL LDAP_CALLBACK *ldapuV_first_entry)      ( LDAP*, LDAPMessage* );
    LDAPMessage*(LDAP_CALL LDAP_CALLBACK *ldapuV_next_entry)       ( LDAP*, LDAPMessage* );
    int         (LDAP_CALL LDAP_CALLBACK *ldapuV_msgfree)          ( LDAP*, LDAPMessage* );
    char*       (LDAP_CALL LDAP_CALLBACK *ldapuV_get_dn)           ( LDAP*, LDAPMessage* );
    void        (LDAP_CALL LDAP_CALLBACK *ldapuV_memfree)          ( LDAP*, void* );
    char*       (LDAP_CALL LDAP_CALLBACK *ldapuV_first_attribute)  ( LDAP*, LDAPMessage*, BerElement** );
    char*       (LDAP_CALL LDAP_CALLBACK *ldapuV_next_attribute)   ( LDAP*, LDAPMessage*, BerElement* );
    void        (LDAP_CALL LDAP_CALLBACK *ldapuV_ber_free)         ( LDAP*, BerElement*, int );
    char**      (LDAP_CALL LDAP_CALLBACK *ldapuV_get_values)       ( LDAP*, LDAPMessage*, const char* );
    void        (LDAP_CALL LDAP_CALLBACK *ldapuV_value_free)       ( LDAP*, char** );
    struct berval**(LDAP_CALL LDAP_CALLBACK *ldapuV_get_values_len)( LDAP*, LDAPMessage*, const char* );
    void           (LDAP_CALL LDAP_CALLBACK *ldapuV_value_free_len)( LDAP*, struct berval** );
} LDAPUVTable_t;

NSAPI_PUBLIC extern void ldapu_VTable_set (LDAPUVTable_t*);

#ifdef __cplusplus
}
#endif

#endif /* _LDAPU_LDAPUTIL_H */
