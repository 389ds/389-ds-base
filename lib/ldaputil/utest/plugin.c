/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <plugin.h>		/* must define extern "C" functions */
#include <certmap.h>		/* Public Certmap API */

static CertSearchFn_t default_searchfn = 0;

static int plugin_attr_val (void *cert, int which_dn, const char *attr)
{
    char **val;
    int rv = ldapu_get_cert_ava_val(cert, which_dn, attr, &val);
    char **attr_val = val;	/* preserve the pointer for free */

    if (rv != LDAPU_SUCCESS || !val) {
	fprintf(stderr, "\t%s: *** Failed ***\n", attr);
    }
    else if (!*val) {
	fprintf(stderr, "\t%s: *** Empty ***\n", attr);
    }
    else {
	fprintf(stderr, "\t%s: \"%s\"", attr, *val++);
	while(*val) {
	    fprintf(stderr, ", \"%s\"", *val++);
	}
	fprintf(stderr, "\n");
    }

    ldapu_free_cert_ava_val(attr_val);

    return LDAPU_SUCCESS;
}

static int plugin_mapping_fn (void *cert, LDAP *ld, void *certmap_info,
			      char **ldapDN, char **filter)
{
    char *subjdn;
    char *issuerDN;
    char *ptr;
    char *comma;

    fprintf(stderr, "plugin_mapping_fn called.\n");
    ldapu_get_cert_subject_dn(cert, &subjdn);
    ldapu_get_cert_issuer_dn(cert, &issuerDN);

    fprintf(stderr, "Value of attrs from subject DN & issuer DN:\n");
    fprintf(stderr, "\tCert: \"%s\"\n", (char *)cert);
    fprintf(stderr, "\tsubjdn: \"%s\"\n", subjdn);
    plugin_attr_val(cert, LDAPU_SUBJECT_DN, "cn");
    plugin_attr_val(cert, LDAPU_SUBJECT_DN, "ou");
    plugin_attr_val(cert, LDAPU_SUBJECT_DN, "o");
    plugin_attr_val(cert, LDAPU_SUBJECT_DN, "c");
    fprintf(stderr, "\tissuerDN: \"%s\"\n", issuerDN);
    plugin_attr_val(cert, LDAPU_ISSUER_DN, "cn");
    plugin_attr_val(cert, LDAPU_ISSUER_DN, "ou");
    plugin_attr_val(cert, LDAPU_ISSUER_DN, "o");
    plugin_attr_val(cert, LDAPU_ISSUER_DN, "c");

    if (subjdn && *subjdn) {
	comma = ptr = strchr(subjdn, ',');

	while(*ptr == ',' || isspace(*ptr)) ptr++;
	*ldapDN = strdup(ptr);

	/* Set filter to the first AVA in the subjdn */
	*filter = subjdn;
	*comma = 0;
    }
    else {
	*ldapDN = 0;
	*filter = 0;
    }

    return LDAPU_SUCCESS;
}

static int plugin_search_fn (void *cert, LDAP *ld, void *certmap_info,
			     const char *basedn,
			     const char *dn, const char *filter,
			     const char **attrs, LDAPMessage **res)
{
    fprintf(stderr, "plugin_search_fn called.\n");
    return (*default_searchfn)(cert, ld, certmap_info, basedn, dn, filter,
			       attrs, res);
}

static int plugin_verify_fn (void *cert, LDAP *ld, void *certmap_info,
			     LDAPMessage *res, LDAPMessage **entry)
{
    fprintf(stderr, "plugin_verify_fn called.\n");
    *entry = ldap_first_entry(ld, res);
    return LDAPU_SUCCESS;
}

int plugin_init_fn (void *certmap_info, const char *issuerName,
		    const char *issuerDN)
{
    fprintf(stderr, "plugin_init_fn called.\n");
    ldapu_set_cert_mapfn(issuerDN, plugin_mapping_fn);
    ldapu_set_cert_verifyfn(issuerDN, plugin_verify_fn);

    if (!default_searchfn) 
	default_searchfn = ldapu_get_cert_searchfn(issuerDN);

    ldapu_set_cert_searchfn(issuerDN, plugin_search_fn);
    return LDAPU_SUCCESS;
}

