/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <ldaputil/errors.h>
#include <ldaputil/certmap.h>

NSAPI_PUBLIC char *ldapu_err2string(int err)
{
    char *rv;

    switch(err) {

	/* Error codes defined in certmap.h */
    case LDAPU_SUCCESS:
	rv = "success";
	break;
    case LDAPU_FAILED:
	rv = "ldap search didn't find an ldap entry";
	break;
    case LDAPU_CERT_MAP_FUNCTION_FAILED:
	rv = "Cert mapping function failed";
	break;
    case LDAPU_CERT_SEARCH_FUNCTION_FAILED:
	rv = "Cert search function failed";
	break;
    case LDAPU_CERT_VERIFY_FUNCTION_FAILED:
	rv = "Cert verify function failed";
	break;
    case LDAPU_CERT_MAP_INITFN_FAILED:
	rv = "Certmap InitFn function failed";
	break;


	/* Error codes returned by ldapdb.c */
    case LDAPU_ERR_URL_INVALID_PREFIX:
	rv = "invalid local ldap database url prefix -- must be ldapdb://";
	break;
    case LDAPU_ERR_URL_NO_BASEDN:
	rv = "base dn is missing in ldapdb url";
	break;
    case LDAPU_ERR_OUT_OF_MEMORY:
	rv = "out of memory";
	break;
    case LDAPU_ERR_LDAP_INIT_FAILED:
	rv = "Couldn't initialize connection to the ldap directory server";
	break;
    case LDAPU_ERR_LCACHE_INIT_FAILED:
	rv = "Couldn't initialize connection to the local ldap directory";
	break;
    case LDAPU_ERR_LDAP_SET_OPTION_FAILED:
	rv = "ldap_set_option failed for local ldap database";
	break;
    case LDAPU_ERR_NO_DEFAULT_CERTDB:
	rv = "default cert database not initialized when using LDAP over SSL";
	break;


	/* Errors returned by ldapauth.c */
    case LDAPU_ERR_CIRCULAR_GROUPS:
	rv = "Circular groups were detected during group membership check";
	break;
    case LDAPU_ERR_INVALID_STRING:
	rv = "Invalid string";
	break;
    case LDAPU_ERR_INVALID_STRING_INDEX:
	rv = "Invalid string index";
	break;
    case LDAPU_ERR_MISSING_ATTR_VAL:
	rv = "Missing attribute value from the search result";
	break;


	/* Errors returned by dbconf.c */
    case LDAPU_ERR_CANNOT_OPEN_FILE:
	rv = "cannot open the config file";
	break;
    case LDAPU_ERR_DBNAME_IS_MISSING:
	rv = "database name is missing";
	break;
    case LDAPU_ERR_PROP_IS_MISSING:
	rv = "database property is missing";
	break;
    case LDAPU_ERR_DIRECTIVE_IS_MISSING:
	rv = "illegal directive in the config file";
	break;
    case LDAPU_ERR_NOT_PROPVAL:
	rv = "internal error - LDAPU_ERR_NOT_PROPVAL";
	break;


	/* Error codes returned by certmap.c */
    case LDAPU_ERR_NO_ISSUERDN_IN_CERT:
	rv = "cannot extract issuer DN from the cert";
	break;
    case LDAPU_ERR_NO_ISSUERDN_IN_CONFIG_FILE:
	rv = "issuer DN missing for non-default certmap";
	break;
    case LDAPU_ERR_CERTMAP_INFO_MISSING:
	rv = "cert to ldap entry mapping information is missing";
	break;
    case LDAPU_ERR_MALFORMED_SUBJECT_DN:
	rv = "Found malformed subject DN in the certificate";
	break;
    case LDAPU_ERR_MAPPED_ENTRY_NOT_FOUND:
	rv = "Certificate couldn't be mapped to an ldap entry";
	break;
    case LDAPU_ERR_UNABLE_TO_LOAD_PLUGIN:
	rv = "Unable to load certmap plugin library";
	break;
    case LDAPU_ERR_MISSING_INIT_FN_IN_CONFIG:
	rv = "InitFn must be provided when using certmap plugin library";
	break;
    case LDAPU_ERR_MISSING_INIT_FN_IN_LIB:
	rv = "Could not find InitFn in the certmap plugin library";
	break;
    case LDAPU_ERR_CERT_VERIFY_FAILED:
	rv = "Could not matching certificate in User's LDAP entry";
	break;
    case LDAPU_ERR_CERT_VERIFY_NO_CERTS:
	rv = "User's LDAP entry doesn't have any certificates to compare";
	break;
    case LDAPU_ERR_MISSING_LIBNAME:
	rv = "Library name is missing in the config file";
	break;
    case LDAPU_ERR_MISSING_INIT_FN_NAME:
	rv = "Init function name is missing in the config file";
	break;
    case LDAPU_ERR_WRONG_ARGS:
	rv = "ldaputil API function called with wrong arguments";
	break;
    case LDAPU_ERR_RENAME_FILE_FAILED:
	rv = "Renaming of file failed";
	break;
    case LDAPU_ERR_MISSING_VERIFYCERT_VAL:
	rv = "VerifyCert property value must be on or off";
	break;
    case LDAPU_ERR_CANAME_IS_MISSING:
	rv = "Cert issuer name is missing";
	break;
    case LDAPU_ERR_CAPROP_IS_MISSING:
	rv = "property name is missing";
	break;
    case LDAPU_ERR_UNKNOWN_CERT_ATTR:
	rv = "unknown cert attribute";
	break;


    case LDAPU_ERR_EMPTY_LDAP_RESULT:
	rv = "ldap search returned empty result";
	break;
    case LDAPU_ERR_MULTIPLE_MATCHES:
	rv = "ldap search returned multiple matches when one expected";
	break;
    case LDAPU_ERR_MISSING_RES_ENTRY:
	rv = "Could not extract entry from the ldap search result";
	break;
    case LDAPU_ERR_MISSING_UID_ATTR:
	rv = "ldap entry is missing the 'uid' attribute value";
	break;
    case LDAPU_ERR_INVALID_ARGUMENT:
	rv = "invalid argument passed to the certmap API function";
	break;
    case LDAPU_ERR_INVALID_SUFFIX:
	rv = "invalid LDAP directory suffix";
	break;


	/* Error codes returned by cert.c */
    case LDAPU_ERR_EXTRACT_SUBJECTDN_FAILED:
	rv = "Couldn't extract the subject DN from the certificate";
	break;
    case LDAPU_ERR_EXTRACT_ISSUERDN_FAILED:
	rv = "Couldn't extract the issuer DN from the certificate";
	break;
    case LDAPU_ERR_EXTRACT_DERCERT_FAILED:
	rv = "Couldn't extract the original DER encoding from the certificate";
	break;


    case LDAPU_ERR_NOT_IMPLEMENTED:
	rv = "function not implemented yet";
	break;
    case LDAPU_ERR_INTERNAL:
	rv = "ldaputil internal error";
	break;

    default:
	if (err > 0) {
	    /* LDAP errors are +ve */
	    rv = ldap_err2string(err);
	}
	else {
	    rv = "internal error - unknown error code";
	}
	break;
    }

    return rv;
}
