/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <ctype.h> /* isspace */
#include <string.h>
#include <stdio.h> /* sprintf */
#include <stdlib.h> /* malloc */

#include <ldaputil/ldaputil.h>
#include <ldaputil/cert.h>
#include <ldaputil/errors.h>
#include "../ldaputili.h"

#define BIG_LINE 1024

NSAPI_PUBLIC int ldapu_get_cert_subject_dn (void *cert_in, char **subjectDN)
{
    char *cert = (char *)cert_in;

    *subjectDN = strdup((char *)cert);
    return *subjectDN ? LDAPU_SUCCESS : LDAPU_FAILED;
}

NSAPI_PUBLIC int ldapu_get_cert_issuer_dn (void *cert, char **issuerDN)
{
    /* TEMPORARY  -- not implemented yet*/
    *issuerDN = strdup("o=Netscape Communications, c=US");
    return *issuerDN ? LDAPU_SUCCESS : LDAPU_FAILED;
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

int
ldapu_member_certificate_match (void* cert, const char* desc)
{
    if (!strcasecmp ((char*)cert, desc)) {
	return LDAPU_SUCCESS;
    }
    return LDAPU_FAILED;
}
