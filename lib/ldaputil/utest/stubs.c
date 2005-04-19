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
#include <ctype.h> /* isspace */
#include <string.h>
#include <stdio.h> /* sprintf */
#include <stdlib.h> /* malloc */

#include <ldap.h>
#include <ldaputil/certmap.h>
#include <ldaputil/cert.h>
#include <ldaputil/errors.h>

#define BIG_LINE 1024

NSAPI_PUBLIC int ldapu_get_cert_subject_dn (void *cert_in, char **subjectDN)
{
    char *cert = (char *)cert_in;

    *subjectDN = strdup((char *)cert);
    return *subjectDN ? LDAPU_SUCCESS : LDAPU_FAILED;
}

NSAPI_PUBLIC int ldapu_get_cert_issuer_dn (void *cert, char **issuerDN)
{
    extern char *global_issuer_dn;
    /* TEMPORARY  -- not implemented yet*/
    *issuerDN = global_issuer_dn ? strdup(global_issuer_dn) : 0;
    return LDAPU_SUCCESS;
}

/* A stub to remove link errors -- ignore SSL */
LDAP *ldapssl_init (const char *host, int port, int secure)
{
    LDAP *ld = 0;

    if ((ld = ldap_init(host, port)) == NULL) {
	fprintf(stderr, "ldap_init: Failed to initialize connection");
	return(0);
    }

    return ld;
}

NSAPI_PUBLIC int ldapu_get_cert_ava_val (void *cert_in, int which_dn,
					 const char *attr, char ***val_out)
{
    int rv;
    char *cert_dn;
    char **ptr;
    char **val;
    char *dnptr;
    char attr_eq1[BIG_LINE];
    char attr_eq2[BIG_LINE];
    char *comma;

    *val_out = 0;

    if (which_dn == LDAPU_SUBJECT_DN)
	rv = ldapu_get_cert_subject_dn(cert_in, &cert_dn);
    else if (which_dn == LDAPU_ISSUER_DN)
	rv = ldapu_get_cert_issuer_dn(cert_in, &cert_dn);
    else
	return LDAPU_ERR_INVALID_ARGUMENT;

    if (rv != LDAPU_SUCCESS) return rv;

    val = (char **)malloc(32*sizeof(char *));

    if (!val) return LDAPU_ERR_OUT_OF_MEMORY;

    ptr = val;
    sprintf(attr_eq1, "%s =", attr);
    sprintf(attr_eq2, "%s=", attr);

    while(cert_dn &&
	  ((dnptr = strstr(cert_dn, attr_eq1)) ||
	   (dnptr = strstr(cert_dn, attr_eq2))))
    {
	dnptr = strchr(dnptr, '=');
	dnptr++;
	while(isspace(*dnptr)) dnptr++;
	comma = strchr(dnptr, ',');

	if (comma) {
	    *ptr = (char *)malloc((comma-dnptr+1)*sizeof(char));
	    strncpy(*ptr, dnptr, (comma-dnptr));
	    (*ptr++)[comma-dnptr] = 0;
	}
	else {
	    *ptr++ = strdup(dnptr);
	}
	cert_dn = comma;
    }

    *ptr = 0;
    *val_out = val;
    return LDAPU_SUCCESS;
}

NSAPI_PUBLIC int ldapu_get_cert_der (void *cert_in, unsigned char **der,
				     unsigned int *len)
{
    return LDAPU_FAILED;
}
