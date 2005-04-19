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

