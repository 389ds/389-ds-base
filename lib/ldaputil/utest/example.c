/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>

#include <certmap.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The init function must be defined extern "C" if using a C++ compiler */
int plugin_init_fn (void *certmap_info, const char *issuerName,
		    const char *issuerDN);

#ifdef __cplusplus
}
#endif


static int extract_ldapdn_and_filter (const char *subjdn, void *certmap_info,
				      char **ldapDN, char **filter)
{
    /* extract the ldapDN and filter from subjdn */
    /* You can also use the ldapu_certmap_info_attrval function to get value
       of a config file parameter for the certmap_info. */
    return LDAPU_SUCCESS;
}

static int plugin_mapping_fn (void *cert, LDAP *ld, void *certmap_info,
			      char **ldapDN, char **filter)
{
    char *subjdn;
    int rv;

    fprintf(stderr, "plugin_mapping_fn called.\n");
    rv = ldapu_get_cert_subject_dn(cert, &subjdn);

    if (rv != LDAPU_SUCCESS) return rv;

    *ldapDN = 0;
    *filter = 0;

    rv = extract_ldapdn_and_filter(subjdn, certmap_info, ldapDN, filter);

    if (rv != LDAPU_SUCCESS) {
	/* This function must return LDAPU_FAILED or
	   LDAPU_CERT_MAP_FUNCTION_FAILED on error */
	return LDAPU_CERT_MAP_FUNCTION_FAILED;
    }

    return LDAPU_SUCCESS;
}

static int plugin_cmp_certs (void *subject_cert,
			     void *entry_cert_binary,
			     unsigned long entry_cert_len)
{
    /* compare the certs */
    return LDAPU_SUCCESS;
}

static int plugin_verify_fn (void *cert, LDAP *ld, void *certmap_info,
			     LDAPMessage *res, LDAPMessage **entry_out)
{
    LDAPMessage *entry;
    struct berval **bvals;
    char *cert_attr = "userCertificate;binary";
    int i;
    int rv;

    fprintf(stderr, "plugin_verify_fn called.\n");
    *entry_out = 0;

    for (entry = ldap_first_entry(ld, res); entry != NULL;
	 entry = ldap_next_entry(ld, entry))
    {
	if ((bvals = ldap_get_values_len(ld, entry, cert_attr)) == NULL) {
	    rv = LDAPU_CERT_VERIFY_FUNCTION_FAILED;
	    /* Maybe one of the remaining entries will match */
	    continue;
	}

	for ( i = 0; bvals[i] != NULL; i++ ) {
	    rv = plugin_cmp_certs (cert,
				   bvals[i]->bv_val,
				   bvals[i]->bv_len);

	    if (rv == LDAPU_SUCCESS) {
		break;
	    }
	}

	ldap_value_free_len(bvals);

	if (rv == LDAPU_SUCCESS) {
	    *entry_out = entry;
	    break;
	}
    }

    return rv;
}

int plugin_init_fn (void *certmap_info, const char *issuerName,
		    const char *issuerDN)
{
    fprintf(stderr, "plugin_init_fn called.\n");
    ldapu_set_cert_mapfn(issuerDN, plugin_mapping_fn);
    ldapu_set_cert_verifyfn(issuerDN, plugin_verify_fn);
    return LDAPU_SUCCESS;
}

