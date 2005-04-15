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

