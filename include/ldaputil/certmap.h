/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _LDAPU_CERTMAP_H
#define _LDAPU_CERTMAP_H

#ifndef INTLDAPU
#define INTLDAPU
#endif /* INTLDAPU */

#include "extcmap.h"

enum {
    LDAPU_STR_FILTER_DEFAULT,
    LDAPU_STR_FILTER_USER,
    LDAPU_STR_FILTER_GROUP,
    LDAPU_STR_FILTER_MEMBER,
    LDAPU_STR_FILTER_MEMBER_RECURSE,
    LDAPU_STR_ATTR_USER,
    LDAPU_STR_ATTR_CERT,
    LDAPU_STR_ATTR_CERT_NOSUBTYPE,
    LDAPU_STR_MAX_INDEX
};

static char *ldapu_strings[] = {
    "objectclass=*",		/* LDAPU_STR_DEFAULT */
    "uid=%s",			/* LDAPU_STR_FILTER_USER */
    "(& (cn=%s) (| (objectclass=groupofuniquenames) (objectclass=groupofnames)))", /* LDAPU_STR_FILTER_GROUP */
    "(| (uniquemember=%s) (member=%s))",	/* LDAPU_STR_FILTER_MEMBER */
    "(& %s (| (objectclass=groupofuniquenames) (objectclass=groupofnames))", /* LDAPU_STR_FILTER_MEMBER_RECURSE */
    "uid",			/* LDAPU_STR_ATTR_USER */
    "userCertificate;binary",	/* LDAPU_STR_ATTR_CERT */
    "userCertificate"	/* LDAPU_STR_ATTR_CERT_NOSUBTYPE */
};
    
typedef struct {
    char *str;
    int size;
    int len;
} LDAPUStr_t;

#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC int ldapu_cert_to_ldap_entry (void *cert, LDAP *ld,
					   const char *basedn,
					   LDAPMessage **res);

NSAPI_PUBLIC int ldapu_set_cert_mapfn (const char *issuerDN,
				       CertMapFn_t mapfn);


NSAPI_PUBLIC CertMapFn_t ldapu_get_cert_mapfn (const char *issuerDN);

NSAPI_PUBLIC int ldapu_set_cert_searchfn (const char *issuerDN,
					  CertSearchFn_t searchfn);


NSAPI_PUBLIC CertSearchFn_t ldapu_get_cert_searchfn (const char *issuerDN);

NSAPI_PUBLIC int ldapu_set_cert_verifyfn (const char *issuerDN,
					  CertVerifyFn_t verifyFn);

NSAPI_PUBLIC CertVerifyFn_t ldapu_get_cert_verifyfn (const char *issuerDN);


NSAPI_PUBLIC int ldapu_get_cert_subject_dn (void *cert, char **subjectDN);


NSAPI_PUBLIC int ldapu_get_cert_issuer_dn (void *cert, char **issuerDN);


NSAPI_PUBLIC int ldapu_get_cert_ava_val (void *cert, int which_dn,
					 const char *attr, char ***val);


NSAPI_PUBLIC int ldapu_free_cert_ava_val (char **val);


NSAPI_PUBLIC int ldapu_get_cert_der (void *cert, unsigned char **derCert,
				     unsigned int *len);


NSAPI_PUBLIC int ldapu_issuer_certinfo (const char *issuerDN,
					void **certmap_info);


NSAPI_PUBLIC int ldapu_certmap_info_attrval (void *certmap_info,
					     const char *attr, char **val);


NSAPI_PUBLIC char *ldapu_err2string (int err);

/* Keep the old fn for backward compatibility */
NSAPI_PUBLIC void ldapu_free_old (char *ptr);


NSAPI_PUBLIC void *ldapu_malloc (int size);


NSAPI_PUBLIC char *ldapu_strdup (const char *ptr);


NSAPI_PUBLIC void *ldapu_realloc (void *ptr, int size);


NSAPI_PUBLIC void ldapu_free (void *ptr);


NSAPI_PUBLIC int ldapu_string_set (const int type, const char *filter);


NSAPI_PUBLIC const char *ldapu_string_get (const int type);

NSAPI_PUBLIC int ldaputil_exit ();

#ifdef __cplusplus
}
#endif

#endif /* _LDAPU_CERTMAP_H */
