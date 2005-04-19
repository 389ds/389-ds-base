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
#include <string.h>
#include <ctype.h>

#include "certmap.h"		/* Public Certmap API */
#include "plugin.h"		/* must define extern "C" functions */

#ifdef WIN32
CertmapDLLInitFnTbl		/* Initialize Certmap Function Table */
#endif

CertSearchFn_t default_searchfn = 0;


/* plugin_ereport -
   This function prints an error message to stderr.  It prints the issuerDN
   and subjectDN alongwith the given message.
   */
static void plugin_ereport (const char *msg, void *cert)
{
    int rv;
    char *subjectDN;
    char *issuerDN;
    char *default_subjectDN = "Failed to get the subject DN";
    char *default_issuerDN = "Failed to get the issuer DN";

    rv = ldapu_get_cert_subject_dn(cert, &subjectDN);

    if (rv != LDAPU_SUCCESS || !subjectDN) {
	subjectDN = default_subjectDN;
    }

    rv = ldapu_get_cert_issuer_dn(cert, &issuerDN);

    if (rv != LDAPU_SUCCESS || !issuerDN) {
	issuerDN = default_issuerDN;
    }

    fprintf(stderr, "%s. Issuer: %s, Subject: %s\n", msg, issuerDN,
	    subjectDN);

    if (default_subjectDN != subjectDN) ldapu_free(subjectDN);
    if (default_issuerDN != issuerDN) ldapu_free(issuerDN);
}


/* plugin_mapping_fn -
   This mapping function extracts "CN", "O" and "C" attributes from the
   subject DN to form ldapDN.  It inserts "ou=<defaultOU>" between the
   "CN" and the "O" attr-value pair.  The <defaultOU> can be configured in
   the certmap.conf config file.
   If the "C" attr is absent, it defaults to "US".
   It extracts the "E" attribute to form the filter.
   */
int plugin_mapping_fn (void *cert, LDAP *ld, void *certmap_info,
		       char **ldapDN, char **filter)
{
    char **cn_val;		/* get this from the cert */
    char **o_val;		/* get this from the cert */
    char **c_val;		/* get this from the cert */
    char **e_val;		/* get this from the cert */
    char *ou_val;		/* get this from the config file */
    int len;
    int rv;

    fprintf(stderr, "plugin_mapping_fn called.\n");

    rv = ldapu_get_cert_ava_val(cert, LDAPU_SUBJECT_DN, "CN", &cn_val);

    if (rv != LDAPU_SUCCESS || !cn_val) {
	plugin_ereport("plugin_mapping_fn: Failed to extract \"CN\" from the cert", cert);
	return LDAPU_CERT_MAP_FUNCTION_FAILED;
    }

    rv = ldapu_get_cert_ava_val(cert, LDAPU_SUBJECT_DN, "O", &o_val);

    if (rv != LDAPU_SUCCESS || !o_val) {
	plugin_ereport("plugin_mapping_fn: Failed to extract \"O\" from the cert", cert);
	return LDAPU_CERT_MAP_FUNCTION_FAILED;
    }

    rv = ldapu_get_cert_ava_val(cert, LDAPU_SUBJECT_DN, "C", &c_val);

    if (rv != LDAPU_SUCCESS || !c_val) {
	plugin_ereport("plugin_mapping_fn: Failed to extract \"C\" from the cert", cert);
    }

    rv = ldapu_get_cert_ava_val(cert, LDAPU_SUBJECT_DN, "E", &e_val);

    if (rv != LDAPU_SUCCESS || !e_val) {
	/* Don't return error -- just print the warning */
	plugin_ereport("plugin_mapping_fn: Failed to extract \"E\" from the cert", cert);
    }

    /* Get the "OU" from the "defaultOU" property from the config file */
    rv = ldapu_certmap_info_attrval(certmap_info, "defaultOU", &ou_val);

    if (rv != LDAPU_SUCCESS || !ou_val) {
	plugin_ereport("plugin_mapping_fn: Failed to get \"defaultOU\" from the configuration", cert);
	return LDAPU_CERT_MAP_FUNCTION_FAILED;
    }

    len = strlen("cn=, ou=, o=, c=") + strlen(cn_val[0]) + strlen(ou_val) +
	strlen(o_val[0]) + (c_val ? strlen(c_val[0]) : strlen("US")) + 1;
    *ldapDN = (char *)ldapu_malloc(len);

    if (!*ldapDN) {
	plugin_ereport("plugin_mapping_fn: Ran out of memory", cert);
	return LDAPU_CERT_MAP_FUNCTION_FAILED;
    }

    if (e_val) {
	len = strlen("mail=") + strlen(e_val[0]) + 1;
	*filter = (char *)ldapu_malloc(len);

	if (!*filter) {
	    free(*ldapDN);
	    plugin_ereport("plugin_mapping_fn: Ran out of memory", cert);
	    return LDAPU_CERT_MAP_FUNCTION_FAILED;
	}
	sprintf(*filter, "mail=%s", e_val[0]);
    }
    else {
	*filter = 0;
    }

    sprintf(*ldapDN, "cn=%s, ou=%s, o=%s, c=%s", cn_val[0], ou_val,
	    o_val[0], c_val ? c_val[0] : "US");

    ldapu_free_cert_ava_val(cn_val);
    ldapu_free_cert_ava_val(o_val);
    ldapu_free_cert_ava_val(c_val);
    ldapu_free_cert_ava_val(e_val);
    ldapu_free(ou_val);

    fprintf(stderr, "plugin_mapping_fn Returned:\n\tldapDN: \"%s\"\n\tfilter: \"%s\"\n",
	    *ldapDN, *filter ? *filter : "<NULL>");

    return LDAPU_SUCCESS;
}


int plugin_cert_serial_number (void *cert)
{
    /* Just a stub function.  You can get the DER encoded cert by using the
       function ldapu_get_cert_der:
       */
    unsigned char *derCert;
    unsigned int len;
    int rv;
    int sno;

    rv = ldapu_get_cert_der(cert, &derCert, &len);

    /* extract the serial number from derCert */
    sno = 43534754;		/* a fake value for now */

    ldapu_free((char *)derCert);

    return sno;
}

/* plugin_search_fn -
   This function first does a search based on the cert's serial number.
   If that fails, it calls the default search function.
   */
int plugin_search_fn (void *cert, LDAP *ld, void *certmap_info,
		      const char *suffix,
		      const char *ldapdn, const char *filter,
		      const char **attrs, LDAPMessage **res)
{
    int rv;
    char snoFilter[256];

    fprintf(stderr, "plugin_search_fn called.\n");
    sprintf(snoFilter, "certSerialNumber=%d",
	    plugin_cert_serial_number(cert));

    /* Search the entire LDAP tree for "certSerialNumber=<serial No.>" */
    rv = ldap_search_s(ld, suffix, LDAP_SCOPE_SUBTREE, snoFilter,
		       (char **)attrs, 0, res);

    /* ldap_search_s returns LDAP_SUCCESS (rather than LDAPU_SUCCESS)
       if there is no error but there may not be any matching entries.
       */
    if (rv == LDAP_SUCCESS) {
	/* There was no error but check if any entries matched */
	int numEntries = ldap_count_entries(ld, *res);

	if (numEntries > 0) {
	    /* at least one entry matched */
	    /* change the return value to LDAPU_SUCCESS from LDAP_SUCCESS */
	    rv = LDAPU_SUCCESS;
	}
	else {
	    /* Try the default search function */
	    rv = (*default_searchfn)(cert, ld, certmap_info, suffix, ldapdn,
				     filter, attrs, res);
	}
    }

    /* It's ok to return the error code from ldap_search_s */
    return rv;
}

/*
  plugin_verify_fn -
  This function returns success if only one entry exists in 'res'.
  */
int plugin_verify_fn (void *cert, LDAP *ld, void *certmap_info,
		      LDAPMessage *res, LDAPMessage **entry)
{
    int rv;
    int numEntries;

    fprintf(stderr, "plugin_verify_fn called.\n");
    numEntries = ldap_count_entries(ld, res);

    if (numEntries == 1) {
	*entry = ldap_first_entry(ld, res);
	rv = LDAPU_SUCCESS;
    }
    else {
	plugin_ereport("plugin_verify_fn: Failing because multiple entries matched.",
		       cert);
	*entry = 0;
	rv = LDAPU_CERT_VERIFY_FUNCTION_FAILED;
    }

    return rv;
}


