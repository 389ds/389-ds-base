/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _PUBLIC_CERTMAP_H
#define _PUBLIC_CERTMAP_H

#include <ldap.h>

#ifndef NSAPI_PUBLIC
#if defined( _WINDOWS ) || defined( _WIN32 ) || defined( XP_WIN32 )
#define NSAPI_PUBLIC __declspec(dllexport)
#else
#define NSAPI_PUBLIC 
#endif
#endif


#define LDAPU_ATTR_INITFN		"InitFn"
#define LDAPU_ATTR_LIBRARY		"library"
#define LDAPU_ATTR_DNCOMPS		"DNComps"
#define LDAPU_ATTR_FILTERCOMPS		"FilterComps"
#define LDAPU_ATTR_VERIFYCERT 		"VerifyCert"
#define LDAPU_ATTR_CERTMAP_LDAP_ATTR 	"CmapLdapAttr"

/* Error/Success codes */
#define LDAPU_SUCCESS			   0
#define LDAPU_FAILED			  -1
#define LDAPU_CERT_MAP_FUNCTION_FAILED    -2
#define LDAPU_CERT_SEARCH_FUNCTION_FAILED -3
#define LDAPU_CERT_VERIFY_FUNCTION_FAILED -4
#define LDAPU_CERT_MAP_INITFN_FAILED      -5


/*
 * CertMapFn_t -
 *  This is a typedef for cert mapping function.  The mapping function is
 *  called by the function ldapu_cert_to_ldap_entry.
 * Parameters:
 *  cert         -  cert to be mapped.  You can pass this to
 *		    functions ldapu_get_cert_XYZ.
 *  ld		 -  Handle to the connection to the directory server.
 *  certmap_info -  This structure contains information about the 
 *		    configuration parameters for the cert's issuer (CA).
 *		    This structure can be passed to the function
 *		    ldapu_certmap_info_attrval to get value for a particular
 *		    configuration attribute (or a property).
 *  ldapdn	 -  The mapping function should allocate memory for ldapdn
 *		    using malloc and set this variable using the 'cert' and
 *		    'certmap_info'.  This DN will be used for ldap lookup.
 *  filter	 -  The mapping function should allocate memory for filter
 *		    using malloc and set this variable using the 'cert' and
 *		    'certmap_info'.  This will be used as ldap filter for ldap
 *		    lookup of the ldapdn.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion (cert is mapped)
 *  return LDAPU_FAILED there is no unexpected error but cert could not
 *		    mapped (probably because ldap entry doesn't exist).
 *  otherwise return LDAPU_CERT_MAP_FUNCTION_FAILED.
 */
typedef int (*CertMapFn_t)(void *cert, LDAP *ld, void *certmap_info,
			   char **ldapdn, char **filter);


/*
 * CertSearchFn_t -
 *  This is a typedef for cert search function.  The search function is
 *  called by the function ldapu_cert_to_ldap_entry after calling the mapping
 *  function.  The candidate 'dn' and 'filter' returned by the mapping
 *  function is passed to this function.
 *  The default search function works as follows:
 *	1.  If the 'filter' is NULL, default it to 'objectclass=*'.
 *	2.  If the 'dn' is non-NULL, do a base level search with the 'dn' and
 *	    'filter'.  If it succeeds, we are done.  If there is no serious
 *	    error (LDAP_NO_SUCH_OBJECT is not serious error yet), continue.
 *	3.  If the 'dn' is NULL, default it to 'basedn'.
 *	4.  Perform a 'subtree' search in LDAP for the 'dn' and the 'filter'.
 *	5.  Return the results of the last search.
 * Parameters:
 *  cert         -  cert to be mapped.  You can pass this to
 *		    functions ldapu_get_cert_XYZ.
 *  ld		 -  Handle to the connection to the directory server.
 *  certmap_info -  This structure contains information about the 
 *		    configuration parameters for the cert's issuer (CA).
 *		    This structure can be passed to the function
 *		    ldapu_certmap_info_attrval to get value for a particular
 *		    configuration attribute (or a property).
 *  suffix	 -  If the ldapdn is empty then use this DN to begin the
 *		    search.  This is the DN of the root object in LDAP
 *		    Directory.
 *  ldapdn	 -  candidate 'dn' returned by the mapping function.
 *  filter	 -  returned by the mapping function.
 *  attrs	 -  list of attributes to return from the search.  If this is
 *		    NULL, all attributes are returned.
 *  res		 -  result of the search which is passed to the verify
 *		    function.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  return LDAPU_FAILED there is no unexpected error but entries matching the
 *  'dn' and 'filter' doesn't exist.
 *  otherwise return LDAPU_CERT_SEARCH_FUNCTION_FAILED.
 */
typedef int (*CertSearchFn_t)(void *cert, LDAP *ld, void *certmap_info,
			      const char *suffix, const char *ldapdn,
			      const char *filter, const char **attrs,
			      LDAPMessage ***res);


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
 *  cert	 -  Original cert to be mapped.  You can pass this to
 *		    functions ldapu_get_cert_XYZ.
 *  ld		 -  Handle to the connection to the directory server.
 *  certmap_info -  This structure contains information about the 
 *		    configuration parameters for the cert's issuer (CA).
 *		    This structure can be passed to the function
 *		    ldapu_certmap_info_attrval to get value for a particular
 *		    configuration attribute (or a property).
 *  res		 -  cert is first mapped to ldapdn and filter.  'res' is the
 *		    result of ldap search using the ldapdn and filter.
 *		    'ld' and 'res' can be used in the calls to ldapsdk API.
 *  entry	 -  pointer to the entry from 'res' which is the correct match
 *		    according to the verify function.
 *		    
 * Return Values:
 *  return LDAPU_SUCCESS upon successful completion (cert is verified)
 *  return LDAPU_FAILED there is no unexpected error but cert could not
 *			verified (probably because it was revoked).
 *  otherwise return LDAPU_CERT_VERIFY_FUNCTION_FAILED.
 */
typedef int (*CertVerifyFn_t)(void *cert, LDAP *ld, void *certmap_info,
			      LDAPMessage *res, LDAPMessage **entry);



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
 *		    configuration parameters for the cert's issuer (CA).
 *		    This structure can be passed to the function
 *		    ldapu_certmap_info_attrval to get value for a particular
 *		    configuration attribute (or a property).
 * 
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise return LDAPU_CERT_MAP_INITFN_FAILED.  The server startup will be
 *  aborted if the return value is not LDAPU_SUCCESS.
 */
typedef int (*CertMapInitFn_t)(void *certmap_info, const char *issuerName,
			       const char *issuerDN, const char *libname);

/*
 * Refer to the description of the function ldapu_get_cert_ava_val
 */
enum {
    LDAPU_SUBJECT_DN,
    LDAPU_ISSUER_DN
};

/* ldapu_cert_to_ldap_entry */
typedef int (*t_ldapu_cert_to_ldap_entry)(void *cert, LDAP *ld,
					  const char *suffix,
					  LDAPMessage **res);

/* ldapu_set_cert_mapfn */
typedef int (*t_ldapu_set_cert_mapfn)(const char *issuerDN,
				      CertMapFn_t mapfn);

/* ldapu_get_cert_mapfn */
typedef CertMapFn_t (*t_ldapu_get_cert_mapfn) (const char *issuerDN);

/* ldapu_set_cert_searchfn */
typedef int (*t_ldapu_set_cert_searchfn) (const char *issuerDN,
					  CertSearchFn_t searchfn);

/* ldapu_get_cert_searchfn */
typedef CertSearchFn_t (*t_ldapu_get_cert_searchfn) (const char *issuerDN);

/* ldapu_set_cert_verifyfn */
typedef int (*t_ldapu_set_cert_verifyfn) (const char *issuerDN,
					  CertVerifyFn_t verifyFn);

/* ldapu_get_cert_verifyfn */
typedef CertVerifyFn_t (*t_ldapu_get_cert_verifyfn) (const char *issuerDN);

/* ldapu_get_cert_subject_dn */
typedef int (*t_ldapu_get_cert_subject_dn) (void *cert, char **subjectDN);

/* ldapu_get_cert_issuer_dn */
typedef int (*t_ldapu_get_cert_issuer_dn) (void *cert, char **issuerDN);

/* ldapu_get_cert_ava_val */
typedef int (*t_ldapu_get_cert_ava_val) (void *cert, int which_dn,
					 const char *attr, char ***val);

/* ldapu_free_cert_ava_val */
typedef int (*t_ldapu_free_cert_ava_val) (char **val);

/* ldapu_get_cert_der */
typedef int (*t_ldapu_get_cert_der) (void *cert, unsigned char **derCert,
				     unsigned int *len);

/* ldapu_issuer_certinfo */
typedef int (*t_ldapu_issuer_certinfo) (const char *issuerDN,
					void **certmap_info);

/* ldapu_certmap_info_attrval */
typedef int (*t_ldapu_certmap_info_attrval) (void *certmap_info,
					     const char *attr, char **val);

/* ldapu_err2string */
typedef char * (*t_ldapu_err2string) (int err);

/* ldapu_free */
typedef void (*t_ldapu_free_old) (char *ptr);
typedef void (*t_ldapu_free) (void *ptr);

/* ldapu_malloc */
typedef void *(*t_ldapu_malloc) (int size);

/* ldapu_strdup */
typedef char *(*t_ldapu_strdup) (const char *ptr);


typedef struct LDAPUDispatchVector LDAPUDispatchVector_t;
struct LDAPUDispatchVector {
    t_ldapu_cert_to_ldap_entry      f_ldapu_cert_to_ldap_entry;
    t_ldapu_set_cert_mapfn	    f_ldapu_set_cert_mapfn;
    t_ldapu_get_cert_mapfn	    f_ldapu_get_cert_mapfn;
    t_ldapu_set_cert_searchfn	    f_ldapu_set_cert_searchfn;
    t_ldapu_get_cert_searchfn	    f_ldapu_get_cert_searchfn;
    t_ldapu_set_cert_verifyfn	    f_ldapu_set_cert_verifyfn;
    t_ldapu_get_cert_verifyfn	    f_ldapu_get_cert_verifyfn;
    t_ldapu_get_cert_subject_dn     f_ldapu_get_cert_subject_dn;
    t_ldapu_get_cert_issuer_dn	    f_ldapu_get_cert_issuer_dn;
    t_ldapu_get_cert_ava_val	    f_ldapu_get_cert_ava_val;
    t_ldapu_free_cert_ava_val	    f_ldapu_free_cert_ava_val;
    t_ldapu_get_cert_der	    f_ldapu_get_cert_der;
    t_ldapu_issuer_certinfo	    f_ldapu_issuer_certinfo;
    t_ldapu_certmap_info_attrval    f_ldapu_certmap_info_attrval;
    t_ldapu_err2string		    f_ldapu_err2string;
    t_ldapu_free_old		    f_ldapu_free_old;
    t_ldapu_malloc		    f_ldapu_malloc;
    t_ldapu_strdup		    f_ldapu_strdup;
    t_ldapu_free		    f_ldapu_free;
};


#ifdef INTLDAPU
NSAPI_PUBLIC extern LDAPUDispatchVector_t *__ldapu_table;
#else
typedef int (*CertMapDLLInitFn_t)(LDAPUDispatchVector_t **table);

NSAPI_PUBLIC extern int CertMapDLLInitFn(LDAPUDispatchVector_t **table);

extern LDAPUDispatchVector_t *__ldapu_table;

#if defined( _WINDOWS ) || defined( _WIN32 ) || defined( XP_WIN32 )
#define CertmapDLLInitFnTbl LDAPUDispatchVector_t *__ldapu_table;
#define CertmapDLLInit(rv, libname) \
{\
	HANDLE h = LoadLibrary((libname)); \
	CertMapDLLInitFn_t init_fn; \
	if (!h) return LDAPU_CERT_MAP_INITFN_FAILED; \
	init_fn = (CertMapDLLInitFn_t)GetProcAddress(h, "CertMapDLLInitFn"); \
	rv = init_fn(&__ldapu_table); \
}
#else
#define CertmapDLLInit(rv, libname)
#define CertmapDLLInitFnTbl
#endif

#endif /* INTLDAPU */

#ifndef INTLDAPU

/*
 * ldapu_cert_to_ldap_entry -
 *  This function is called to map a cert to an ldap entry.  It extracts the
 *  cert issuer information from the given cert.  The mapping function set for
 *  the issuer (if any) or the default mapping function is called to map the
 *  subject DN from the cert to a candidate ldap DN and filter for ldap
 *  search.  If the mapped ldap DN is NULL, the 'basedn' passed into this
 *  function is used as a starting place for the search.  If the mapped filter
 *  is NULL, "objectclass=*" is used as a filter.  A base level search is
 *  performed to see if the candidate DN exists in the LDAP database matching
 *  the filter.  If there is no match, a scoped search (sub-tree search) is
 *  performed.  If at least one entry matched the mapped DN and filter, the
 *  result is passed to the appropriate verify function.  The verify function
 *  is called only if 'VerifyCert' parameter has been set for the cert issuer
 *  in the certmap.conf file.
 *  If the verify function succeeds, it must return the pointer to the matched
 *  'entry'.  If at the end, there is only one matching entry, the mapping is
 *  successful.
 * Parameters:
 *  cert         -  cert to be mapped.  You can pass this to
 *		    functions ldapu_get_cert_XYZ.
 *  ld		 -  Handle to the connection to the directory server.
 *  suffix	 -  If the subject dn is mapped to empty LDAP DN then use this
 *		    DN to begin the search.  This is the DN of the root object
 *		    in LDAP Directory.
 *  res		 -  cert is first mapped to ldapdn and filter.  'res' is the
 *		    result of ldap search using the ldapdn and filter.
 *		    'ld' and 'res' can be used in the calls to ldapsdk API.
 *		    When done with 'res', free it using ldap_msgfree(res)
 * 
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_cert_to_ldap_entry (*__ldapu_table->f_ldapu_cert_to_ldap_entry)

/*
 * ldapu_set_cert_mapfn -
 *  This function can be used to set the cert mapping function for the given
 *  issuer (CA).  If the mapping information doesn't exist for the given
 *  issuer then a new one will be created and the mapping function will be
 *  set.  When creating the new mapping information, the default mapping
 *  information is copied.
 * Parameters:
 *  issuerDN	 -  DN of the cert issuer.  This mapping function will be used
 *		    for all certs issued by this issuer.  If the issuerDN is
 *		    NULL, the given 'mapfn' becomes the default mapping
 *		    function (which is used when no mapping function has been
 *		    set for the cert's issuer).
 *  mapfn	 -  the mapping function.  Look at the desciption of
 *		    CertMapFn_t to find out more about the mapping functions.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_set_cert_mapfn (*__ldapu_table->f_ldapu_set_cert_mapfn)


/*
 * ldapu_get_cert_mapfn -
 *  This function can be used to get the cert mapping function for the given
 *  issuer (CA).  This will always return a non-NULL function.
 * Parameters:
 *  issuerDN	 -  DN of the cert issuer for which the mapping function is to
 *		    be retrieved.  If this is NULL, default mapping function
 *		    is returned.
 *
 * Return Value:
 *  The mapping function set for the issuer is returned.  If the issuerDN is
 *  NULL or if no specific mapping function has been set for the issuer, the
 *  default mapping function is returned.
 */
#define ldapu_get_cert_mapfn (*__ldapu_table->f_ldapu_get_cert_mapfn)

/*
 * ldapu_set_cert_searchfn -
 *  This function can be used to set the cert search function for the given
 *  issuer (CA).
 * Parameters:
 *  issuerDN	 -  DN of the cert issuer.  This search function will be used
 *		    for all certs issued by this issuer.  If the issuerDN is
 *		    NULL, the given 'searchfn' becomes the default search
 *		    function (which is used when no search function has been
 *		    set for the cert's issuer).
 *  searchfn	 -  the search function.  Look at the desciption of
 *		    CertSearchFn_t to find out more about the search functions.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_set_cert_searchfn (*__ldapu_table->f_ldapu_set_cert_searchfn)


/*
 * ldapu_get_cert_searchfn -
 *  This function can be used to get the cert search function for the given
 *  issuer (CA).  This will always return a non-NULL function.
 * Parameters:
 *  issuerDN	 -  DN of the cert issuer for which the search function is to
 *		    be retrieved.  If this is NULL, the default search
 *		    function is returned.
 *
 * Return Value:
 *  The search function set for the issuer is returned.  If the issuerDN is
 *  NULL or if no specific search function has been set for the issuer, the
 *  default search function is returned.
 */
#define ldapu_get_cert_searchfn (*__ldapu_table->f_ldapu_get_cert_searchfn)

/*
 * ldapu_set_cert_verifyfn -
 *  This function can be used to set the cert verify function for the given
 *  issuer (CA).  If the mapping information doesn't exist for the given
 *  issuer then a new one will be created and the verify function will be
 *  set.  When creating the new mapping information, the default mapping
 *  information is copied.
 * Parameters:
 *  issuerDN	 -  DN of the cert issuer.  This verify function will be used
 *		    for all certs issued by this issuer.  If the issuerDN is
 *		    NULL, the given 'verifyFn' becomes the default verify
 *		    function (which is used when no verify function has been
 *		    set for the cert's issuer).
 *  verifyFn	 -  the verify function.  Look at the desciption of
 *		    CertMapFn_t to find out more about the verify functions.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_set_cert_verifyfn (*__ldapu_table->f_ldapu_set_cert_verifyfn)

/*
 * ldapu_get_cert_verifyfn -
 *  This function can be used to get the cert verify function for the given
 *  issuer (CA).  This function can return NULL when there is no applicable
 *  verify function.
 * Parameters:
 *  issuerDN	 -  DN of the cert issuer for which the verify function is to
 *		    be retrieved.  If this is NULL, default verify function
 *		    is returned.
 *
 * Return Value:
 *  The verify function set for the issuer is returned.  If the issuerDN is
 *  NULL or if no specific verify function has been set for the issuer, the
 *  default verify function is returned.  This function can return NULL when
 *  there is no applicable verify function.
 */
#define ldapu_get_cert_verifyfn (*__ldapu_table->f_ldapu_get_cert_verifyfn)


/*
 * ldapu_get_cert_subject_dn -
 *  This function can be used to get the subject DN from the cert.  Free the
 *  subjectDN using 'free' after you are done using it.
 * Parameters:
 *  cert	 -  cert from which the DN is to be extracted.
 *  subjectDN	 -  subjectDN extracted from the cert.  Free it using 'free'
 *		    after it is no longer required.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_get_cert_subject_dn (*__ldapu_table->f_ldapu_get_cert_subject_dn)


/*
 * ldapu_get_cert_issuer_dn -
 *  This function can be used to get the issuer DN from the cert.  Free the
 *  issuerDN using 'free' after you are done using it.
 * Parameters:
 *  cert	 -  cert from which the DN is to be extracted.
 *  issuerDN	 -  issuerDN extracted from the cert.  Free it using 'free'
 *		    after it is no longer required.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_get_cert_issuer_dn (*__ldapu_table->f_ldapu_get_cert_issuer_dn)


/*
 * ldapu_get_cert_ava_val -
 *  This function can be used to get value of the given attribute from either
 *  the subject DN or the issuer DN from the cert.
 * Parameters:
 *  cert	 -  cert from which the values are to be extracted.
 *  which_dn	 -  Should be either LDAPU_ISSUER_DN or LDAPU_SUBJECT_DN.
 *  attr	 -  Should be one of "CN", "OU", "O", "C", "UID", "MAIL",
 *		    "E", "L", and "ST".
 *  val		 -  An array of attribute values extracted from the cert.
 *		    There could be multiple values.  The last entry in the
 *		    array is NULL.  You must free this array of strings after
 *		    you are done with it (using the function
 *		    ldapu_free_cert_ava_val).  'val' is initialized to NULL if
 *		    there is an error.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_get_cert_ava_val (*__ldapu_table->f_ldapu_get_cert_ava_val)


/*
 * ldapu_free_cert_ava_val -
 *  This function can be used to free the array returned by the
 *  ldapu_get_cert_ava_val function.
 * Parameters:
 *  val		 -  An array of attribute values returned by
 *		    ldapu_get_cert_ava_val. 
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_free_cert_ava_val (*__ldapu_table->f_ldapu_free_cert_ava_val)


/*
 * ldapu_get_cert_der -
 *  This function can be used to get the original DER encoded cert for the
 *  given cert.
 * Parameters:
 *  cert	 -  cert from which the original DER is to be extracted.
 *  derCert	 -  the original DER encoded cert
 *  len		 -  length of derCert
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_get_cert_der (*__ldapu_table->f_ldapu_get_cert_der)


/*
 * ldapu_issuer_certinfo -
 *  This function can be used to get the handle on the internal structure for
 *  the given issuer.  This handle can be passed to ldapu_certmap_info_attrval
 *  to get configuration attribute values for the issuer.
 * Parameters:
 *  issuerDN	 -  DN of the issuer for whom the handle on internal structure
 *		    is requested.  If issuerDN is NULL, the handle to the
 *		    default configuration information is returned.
 *  certmap_info -  This structure contains information about the 
 *		    configuration parameters for the cert's issuer (CA).
 *		    This structure can be passed to the function
 *		    ldapu_certmap_info_attrval to get value for a particular
 *		    configuration attribute (or a property).
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 *  CAUTION: DON'T FREE THE 'certmap_info' STRUCTURE.
 */
#define ldapu_issuer_certinfo (*__ldapu_table->f_ldapu_issuer_certinfo)


/*
 * ldapu_certmap_info_attrval -
 *  This function can be used to get values for the given attribute/property
 *  from the given certmap_info.  You can get handle on the certmap_info by
 *  calling the ldapu_issuer_certinfo function.  Free the 'val' using 'free'
 *  after it is no longer required.
 * Parameters:
 *  certmap_info -  This structure contains information about the 
 *		    configuration parameters for the cert's issuer (CA).
 *  attr	 -  name of the attribute/property for which the value is to
 *		    be returned.  The attribute can be one of the attributes
 *		    listed above (LDAPU_ATTR_XYZ).  User defined attributes
 *		    can also be used.
 *  val		 -  Value of the 'attr' from the 'certmap_info'.
 *
 * Return Value:
 *  return LDAPU_SUCCESS upon successful completion
 *  otherwise returns an error code that can be passed to ldapu_err2string.
 */
#define ldapu_certmap_info_attrval (*__ldapu_table->f_ldapu_certmap_info_attrval)


/*
 * ldapu_err2string -
 *  This function can be used to print any of the ldaputil or LDAP error
 *  code.
 * Parameters:
 *  err		 -  error code to be converted to printable string.
 *
 * Return Value:
 *  Printable representation of the given error code.
 */
#define ldapu_err2string (*__ldapu_table->f_ldapu_err2string)

/*
 * ldapu_free -
 *  This function should be used to free the memory allocated by
 *  ldapu_* functions if the ldapu_* function doesn't have a corresponding
 *  'free' function.  Use this function for free'ing the memory allocated by
 *  the following functions:
 *	ldapu_get_cert_subject_dn
 *	ldapu_get_cert_issuer_dn
 *	ldapu_get_cert_der
 *	ldapu_certmap_info_attrval
 *  To free memory allocated by ldapu_get_cert_ava_val, use
 *  ldapu_free_cert_ava_val.  Do not free the certmap_info pointer returned by
 *  ldapu_issuer_certinfo.
 * Parameters:
 *  ptr		 -  pointer returned by ldapu_get_cert_* functions.
 */
#define ldapu_free (*__ldapu_table->f_ldapu_free)

/*
 * ldapu_malloc -
 *  This function is a cover function for the 'malloc' system call.  On NT, it
 *  is best to alloc & free the memory in the same DLL.
 * Parameters:
 *  size	 -  size of the memory to be allocated
 * Return Value:
 *  same as 'malloc' -- pointer to the allocated memory or NULL on failure.
 */
#define ldapu_malloc (*__ldapu_table->f_ldapu_malloc)

/*
 * ldapu_strdup -
 *  This function is a cover function for the 'strdup' system call.  On NT, it
 *  is best to alloc & free the memory in the same DLL.
 * Parameters:
 *  ptr		 -  Pointer to the string to be copied
 * Return Value:
 *  same as 'strdup' -- pointer to the copied string or NULL on failure.
 */
#define ldapu_strdup (*__ldapu_table->f_ldapu_strdup)


#endif /* !INTLDAPU */

#endif /* _PUBLIC_CERTMAP_H */
