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

#pragma once

/* What was extcmap.h begins ... */

#include <ldap.h>
#include <cert.h>

#ifndef NSAPI_PUBLIC
#define NSAPI_PUBLIC
#endif


#define LDAPU_ATTR_INITFN "InitFn"
#define LDAPU_ATTR_LIBRARY "library"
#define LDAPU_ATTR_DNCOMPS "DNComps"
#define LDAPU_ATTR_FILTERCOMPS "FilterComps"
#define LDAPU_ATTR_VERIFYCERT "VerifyCert"
#define LDAPU_ATTR_CERTMAP_LDAP_ATTR "CmapLdapAttr"


/*
 * CertMapFn_t -
 *  This is a typedef for cert mapping function.  The mapping function is
 *  called by the function ldapu_cert_to_ldap_entry.
 * Parameters:
 *  cert         -  cert to be mapped.  You can pass this to
 *            functions ldapu_get_cert_XYZ.
 *  ld         -  Handle to the connection to the directory server.
 *  certmap_info -  This structure contains information about the
 *            configuration parameters for the cert's issuer (CA).
 *            This structure can be passed to the function
 *            ldapu_certmap_info_attrval to get value for a particular
 *            configuration attribute (or a property).
 *  ldapdn     -  The mapping function should allocate memory for ldapdn
 *            using malloc and set this variable using the 'cert' and
 *            'certmap_info'.  This DN will be used for ldap lookup.
 *  filter     -  The mapping function should allocate memory for filter
 *            using malloc and set this variable using the 'cert' and
 *            'certmap_info'.  This will be used as ldap filter for ldap
 *            lookup of the ldapdn.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion (cert is mapped)
 *  return LDAPU_FAILED there is no unexpected error but cert could not
 *            mapped (probably because ldap entry doesn't exist).
 *  otherwise return LDAPU_CERT_MAP_FUNCTION_FAILED.
 */
typedef int (*CertMapFn_t)(void *cert, LDAP *ld, void *certmap_info, char **ldapdn, char **filter);


/*
 * CertSearchFn_t -
 *  This is a typedef for cert search function.  The search function is
 *  called by the function ldapu_cert_to_ldap_entry after calling the mapping
 *  function.  The candidate 'dn' and 'filter' returned by the mapping
 *  function is passed to this function.
 *  The default search function works as follows:
 *    1.  If the 'filter' is NULL, default it to 'objectclass=*'.
 *    2.  If the 'dn' is non-NULL, do a base level search with the 'dn' and
 *        'filter'.  If it succeeds, we are done.  If there is no serious
 *        error (LDAP_NO_SUCH_OBJECT is not serious error yet), continue.
 *    3.  If the 'dn' is NULL, default it to 'basedn'.
 *    4.  Perform a 'subtree' search in LDAP for the 'dn' and the 'filter'.
 *    5.  Return the results of the last search.
 * Parameters:
 *  cert         -  cert to be mapped.  You can pass this to
 *            functions ldapu_get_cert_XYZ.
 *  ld         -  Handle to the connection to the directory server.
 *  certmap_info -  This structure contains information about the
 *            configuration parameters for the cert's issuer (CA).
 *            This structure can be passed to the function
 *            ldapu_certmap_info_attrval to get value for a particular
 *            configuration attribute (or a property).
 *  suffix     -  If the ldapdn is empty then use this DN to begin the
 *            search.  This is the DN of the root object in LDAP
 *            Directory.
 *  ldapdn     -  candidate 'dn' returned by the mapping function.
 *  filter     -  returned by the mapping function.
 *  attrs     -  list of attributes to return from the search.  If this is
 *            NULL, all attributes are returned.
 *  res         -  result of the search which is passed to the verify
 *            function.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  return LDAPU_FAILED there is no unexpected error but entries matching the
 *  'dn' and 'filter' doesn't exist.
 *  otherwise return LDAPU_CERT_SEARCH_FUNCTION_FAILED.
 */
typedef int (*CertSearchFn_t)(void *cert, LDAP *ld, void *certmap_info, const char *suffix, const char *ldapdn, const char *filter, const char **attrs, LDAPMessage ***res);


/*
 * CertVerifyFn_t -
 *  This is a typedef for cert verify function.  The verify function is
 *  called by the function ldapu_cert_to_ldap_entry after the cert is
 *  successfully mapped to ldapdn and filter, and an entry matching that
 *  exists in the directory server.   The verify fn may get called for
 *  multiple matched entries.  This function must go through all the entries
 *  and check which one is appropriate.  The pointer to that entry must be
 *  passed back in the 'LDAPMessage **entry' parameter.
 * Parameters:
 *  cert     -  Original cert to be mapped.  You can pass this to
 *            functions ldapu_get_cert_XYZ.
 *  ld         -  Handle to the connection to the directory server.
 *  certmap_info -  This structure contains information about the
 *            configuration parameters for the cert's issuer (CA).
 *            This structure can be passed to the function
 *            ldapu_certmap_info_attrval to get value for a particular
 *            configuration attribute (or a property).
 *  res         -  cert is first mapped to ldapdn and filter.  'res' is the
 *            result of ldap search using the ldapdn and filter.
 *            'ld' and 'res' can be used in the calls to ldapsdk API.
 *  entry     -  pointer to the entry from 'res' which is the correct match
 *            according to the verify function.
 *
 * Return Values:
 *  return LDAPU_SUCCESS upon successful completion (cert is verified)
 *  return LDAPU_FAILED there is no unexpected error but cert could not
 *            verified (probably because it was revoked).
 *  otherwise return LDAPU_CERT_VERIFY_FUNCTION_FAILED.
 */
typedef int (*CertVerifyFn_t)(void *cert, LDAP *ld, void *certmap_info, LDAPMessage *res, LDAPMessage **entry);


/*
 * CertmapInitFn_t -
 *  This is a typedef for user defined init function.  An init function can be
 *  specified in the config file (<ServerRoot>/userdb/certmap.conf) per issuer
 *  of a certificate.  This init function must from the user's library, also
 *  loaded from the config file using the 'library' property.  The init
 *  function is specified in the config file using the 'InitFn' property.
 *  When the config file is loaded, any user defined init functions will be
 *  called with the certmap_info pertaining to the issuer (CA).
 * Parameters:
 *  certmap_info -  This structure contains information about the
 *            configuration parameters for the cert's issuer (CA).
 *            This structure can be passed to the function
 *            ldapu_certmap_info_attrval to get value for a particular
 *            configuration attribute (or a property).
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise return LDAPU_CERT_MAP_INITFN_FAILED.  The server startup will be
 *  aborted if the return value is not LDAPU_SUCCESS.
 */
typedef int (*CertMapInitFn_t)(void *certmap_info, const char *issuerName, const CERTName *issuerDN, const char *libname);

/*
 * Refer to the description of the function ldapu_get_cert_ava_val
 */
enum
{
    LDAPU_SUBJECT_DN,
    LDAPU_ISSUER_DN
};

/* end extcmap */

enum
{
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

#ifdef DEFINE_LDAPU_STRINGS
/* used only in certmap.c and ldaputil.c */
static char *ldapu_strings[] = {
    "objectclass=*",                                                               /* LDAPU_STR_DEFAULT */
    "uid=%s",                                                                      /* LDAPU_STR_FILTER_USER */
    "(& (cn=%s) (| (objectclass=groupofuniquenames) (objectclass=groupofnames)))", /* LDAPU_STR_FILTER_GROUP */
    "(| (uniquemember=%s) (member=%s))",                                           /* LDAPU_STR_FILTER_MEMBER */
    "(& %s (| (objectclass=groupofuniquenames) (objectclass=groupofnames))",       /* LDAPU_STR_FILTER_MEMBER_RECURSE */
    "uid",                                                                         /* LDAPU_STR_ATTR_USER */
    "userCertificate;binary",                                                      /* LDAPU_STR_ATTR_CERT */
    "userCertificate"                                                              /* LDAPU_STR_ATTR_CERT_NOSUBTYPE */
};
#endif /* DEFINE_LDAPU_STRINGS */

typedef struct
{
    char *str;
    int size;
    int len;
} LDAPUStr_t;

#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC int ldapu_cert_to_ldap_entry(void *cert, LDAP *ld, const char *basedn, LDAPMessage **res);

NSAPI_PUBLIC int ldapu_set_cert_mapfn(const CERTName *issuerDN,
                                      CertMapFn_t mapfn);


NSAPI_PUBLIC CertMapFn_t ldapu_get_cert_mapfn(const CERTName *issuerDN);

NSAPI_PUBLIC int ldapu_set_cert_searchfn(const CERTName *issuerDN,
                                         CertSearchFn_t searchfn);


NSAPI_PUBLIC CertSearchFn_t ldapu_get_cert_searchfn(const CERTName *issuerDN);

NSAPI_PUBLIC int ldapu_set_cert_verifyfn(const CERTName *issuerDN,
                                         CertVerifyFn_t verifyFn);

NSAPI_PUBLIC CertVerifyFn_t ldapu_get_cert_verifyfn(const CERTName *issuerDN);


NSAPI_PUBLIC int ldapu_get_cert_subject_dn(void *cert, char **subjectDN);


NSAPI_PUBLIC CERTName *ldapu_get_cert_issuer_dn_as_CERTName(CERTCertificate *cert);


NSAPI_PUBLIC int ldapu_get_cert_issuer_dn(void *cert, char **issuerDN);


NSAPI_PUBLIC int ldapu_get_cert_ava_val(void *cert, int which_dn, const char *attr, char ***val);


NSAPI_PUBLIC int ldapu_free_cert_ava_val(char **val);


NSAPI_PUBLIC int ldapu_get_cert_der(void *cert, unsigned char **derCert, unsigned int *len);


NSAPI_PUBLIC int ldapu_issuer_certinfo(const CERTName *issuerDN,
                                       void **certmap_info);


NSAPI_PUBLIC int ldapu_certmap_info_attrval(void *certmap_info,
                                            const char *attr,
                                            char **val);


NSAPI_PUBLIC char *ldapu_err2string(int err);

/* Keep the old fn for backward compatibility */
NSAPI_PUBLIC void ldapu_free_old(char *ptr);


NSAPI_PUBLIC void *ldapu_malloc(int size);


NSAPI_PUBLIC char *ldapu_strdup(const char *ptr);


NSAPI_PUBLIC void *ldapu_realloc(void *ptr, int size);


NSAPI_PUBLIC void ldapu_free(void *ptr);


NSAPI_PUBLIC int ldaputil_exit(void);

#ifdef __cplusplus
}
#endif
