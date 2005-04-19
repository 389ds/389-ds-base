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

#include <string.h>
#include <malloc.h>

/* removed for ns security integration
#include <sec.h>
*/
#include "prmem.h"
#include "key.h"
#include "cert.h"
#include <ldaputil/certmap.h>
#include <ldaputil/errors.h>
#include <ldaputil/cert.h>
#include "ldaputili.h"

NSAPI_PUBLIC int ldapu_get_cert (void *SSLendpoint, void **cert)
{
    /* TEMPORARY  -- not implemented yet*/
    return LDAPU_FAILED;
}


NSAPI_PUBLIC int ldapu_get_cert_subject_dn (void *cert_in, char **subjectDN)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    char *cert_subject = CERT_NameToAscii(&cert->subject);

    if (cert_subject != NULL) 
    	*subjectDN = strdup(cert_subject);
    else
        *subjectDN=NULL;

    PR_Free(cert_subject);
    return *subjectDN ? LDAPU_SUCCESS : LDAPU_ERR_EXTRACT_SUBJECTDN_FAILED;
}

NSAPI_PUBLIC int ldapu_get_cert_issuer_dn (void *cert_in, char **issuerDN)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    char *cert_issuer = CERT_NameToAscii(&cert->issuer);

    *issuerDN = strdup(cert_issuer);
    PR_Free(cert_issuer);

    return *issuerDN ? LDAPU_SUCCESS : LDAPU_ERR_EXTRACT_ISSUERDN_FAILED;
}

NSAPI_PUBLIC int ldapu_get_cert_der (void *cert_in, unsigned char **der,
				     unsigned int *len)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    SECItem derCert = ((CERTCertificate*)cert)->derCert;
    unsigned char *data = derCert.data;

    *len = derCert.len;
    *der = (unsigned char *)malloc(*len);

    if (!*der) return LDAPU_ERR_OUT_OF_MEMORY;

    memcpy(*der, data, *len);

    return *len ? LDAPU_SUCCESS : LDAPU_ERR_EXTRACT_DERCERT_FAILED;
}

static int certmap_name_to_secoid (const char *str)
{
    if (!ldapu_strcasecmp(str, "c")) return SEC_OID_AVA_COUNTRY_NAME;
    if (!ldapu_strcasecmp(str, "o")) return SEC_OID_AVA_ORGANIZATION_NAME;
    if (!ldapu_strcasecmp(str, "cn")) return SEC_OID_AVA_COMMON_NAME;
    if (!ldapu_strcasecmp(str, "l")) return SEC_OID_AVA_LOCALITY;
    if (!ldapu_strcasecmp(str, "st")) return SEC_OID_AVA_STATE_OR_PROVINCE;
    if (!ldapu_strcasecmp(str, "ou")) return SEC_OID_AVA_ORGANIZATIONAL_UNIT_NAME;
    if (!ldapu_strcasecmp(str, "uid")) return SEC_OID_RFC1274_UID;
    if (!ldapu_strcasecmp(str, "e")) return SEC_OID_PKCS9_EMAIL_ADDRESS;
    if (!ldapu_strcasecmp(str, "mail")) return SEC_OID_RFC1274_MAIL;
    if (!ldapu_strcasecmp(str, "dc")) return SEC_OID_AVA_DC;

    return SEC_OID_AVA_UNKNOWN;	/* return invalid OID */
}

NSAPI_PUBLIC int ldapu_get_cert_ava_val (void *cert_in, int which_dn,
					 const char *attr, char ***val_out)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    CERTName *cert_dn;
    CERTRDN **rdns;
    CERTRDN **rdn;
    CERTAVA **avas;
    CERTAVA *ava;
    int attr_tag = certmap_name_to_secoid(attr);
    char **val;
    char **ptr;
    int rv;

    *val_out = 0;

    if (attr_tag == SEC_OID_AVA_UNKNOWN) {
	return LDAPU_ERR_INVALID_ARGUMENT;
    }

    if (which_dn == LDAPU_SUBJECT_DN)
	cert_dn = &cert->subject;
    else if (which_dn == LDAPU_ISSUER_DN)
	cert_dn = &cert->issuer;
    else
	return LDAPU_ERR_INVALID_ARGUMENT;

    val = (char **)malloc(32*sizeof(char *));

    if (!val) return LDAPU_ERR_OUT_OF_MEMORY;

    ptr = val;

    rdns = cert_dn->rdns;

    if (rdns) {
	for (rdn = rdns; *rdn; rdn++) {
	    avas = (*rdn)->avas;
	    while ((ava = *avas++) != NULL) {
		int tag = CERT_GetAVATag(ava);

		if (tag == attr_tag) {
		    char buf[BIG_LINE];
		    int lenLen;
		    int vallen;
		    /* Found it */

		    /* Copied from ns/lib/libsec ...
		     * XXX this code is incorrect in general
		     * -- should use a DER template.
		     */
		    lenLen = 2;
		    if (ava->value.len >= 128) lenLen = 3;
		    vallen = ava->value.len - lenLen;

		    rv = CERT_RFC1485_EscapeAndQuote(buf,
						    BIG_LINE,
						    (char*) ava->value.data + lenLen,
						    vallen);

		    if (rv == SECSuccess) {
			*ptr++ = strdup(buf);
		    }
		    break;
		}
	    }
	}
    }

    *ptr = 0;

    if (*val) {
	/* At least one value found */
	*val_out = val;
	rv = LDAPU_SUCCESS;
    }
    else {
	free(val);
	rv = LDAPU_FAILED;
    }

    return rv;
}

static void
_rdns_free (char*** rdns)
{
    auto char*** rdn;
    for (rdn = rdns; *rdn; ++rdn) {
	ldap_value_free (*rdn);
    }
    free (rdns);
}

static char***
_explode_dn (const char* dn)
{
    auto char*** exp = NULL;
    if (dn && *dn) {
	auto char** rdns = ldap_explode_dn (dn, 0);
	if (rdns) {
	    auto size_t expLen = 0;
	    auto char** rdn;
	    for (rdn = rdns; *rdn; ++rdn) {
		auto char** avas = ldap_explode_rdn (*rdn, 0);
		if (avas && *avas) {
		    exp = (char***) ldapu_realloc (exp, sizeof(char**) * (expLen + 2));
		    if (exp) {
			exp[expLen++] = avas;
		    } else {
			ldap_value_free (avas);
			break;
		    }
		} else { /* parse error */
		    if (avas) {
			ldap_value_free (avas);
		    }
		    if (exp) {
			exp[expLen] = NULL;
			_rdns_free (exp);
			exp = NULL;
		    }
		    break;
		}
	    }
	    if (exp) {
		exp[expLen] = NULL;
	    }
	    ldap_value_free (rdns);
	}
    }
    return exp;
}

static size_t
_rdns_count (char*** rdns)
{
    auto size_t count = 0;
    auto char*** rdn;
    for (rdn = rdns; *rdns; ++rdns) {
	auto char** ava;
	for (ava = *rdns; *ava; ++ava) {
	    ++count;
	}
    }
    return count;
}

static int
_replaceAVA (char* attr, char** avas)
{
    if (attr && avas) {
	for (; *avas; ++avas) {
	    if (!ldapu_strcasecmp (*avas, attr)) {
		*avas = attr;
		return 1;
	    }
	}
    }
    return 0;
}

struct _attr_getter_pair {
    char* (*getter) (CERTName* dn);
    const char* name1;
    const char* name2;
} _attr_getter_table[] =
{
    {NULL, "OU", "organizationalUnitName"},
    {CERT_GetOrgName, "O", "organizationName"},
    {CERT_GetCommonName, "CN", "commonName"},
    {CERT_GetCertEmailAddress, "E", NULL},
    {CERT_GetCertEmailAddress, "MAIL", "rfc822mailbox"},
    {CERT_GetCertUid, "uid", NULL},
    {CERT_GetCountryName, "C", "country"},
    {CERT_GetStateName, "ST", "state"},
    {CERT_GetLocalityName, "L", "localityName"},
	{CERT_GetDomainComponentName, "DC", "dc"},
    {NULL, NULL, NULL}
};

static int
_is_OU (const char* attr)
{
    auto struct _attr_getter_pair* descAttr;
    for (descAttr = _attr_getter_table; descAttr->name1; ++descAttr) {
	if (descAttr->getter == NULL) { /* OU attribute */
	    if (!ldapu_strcasecmp (attr, descAttr->name1) || (descAttr->name2 &&
		!ldapu_strcasecmp (attr, descAttr->name2))) {
		return 1;
	    }
	    break;
	}
    }
    return 0;
}

static char**
_previous_OU (char** ava, char** avas)
{
    while (ava != avas) {
	--ava;
	if (_is_OU (*ava)) {
	    return ava;
	}
    }
    return NULL;
}

static char*
_value_normalize (char* value)
    /* Remove leading and trailing spaces, and
       change consecutive spaces to a single space.
    */
{
    auto char* t;
    auto char* f;
    t = f = value;
    while (*f == ' ') ++f; /* ignore leading spaces */
    for (; *f; ++f) {
	if (*f != ' ' || t[-1] != ' ') {
	    *t++ = *f; /* no consecutive spaces */
	}
    }
    if (t > value && t[-1] == ' ') {
	--t; /* ignore trailing space */
    }
    *t = '\0';
    return value;
}

static int
_explode_AVA (char* AVA)
    /* Change an attributeTypeAndValue a la <draft-ietf-asid-ldapv3-dn>,
       to the type name, followed immediately by the attribute value,
       both normalized.
     */
{
    auto char* value = strchr (AVA, '=');
    if (!value) return LDAPU_FAILED;
    *value++ = '\0';
    _value_normalize (AVA);
    _value_normalize (value);
    {
	auto char* typeEnd = AVA + strlen (AVA);
	if ((typeEnd + 1) != value) {
	    memmove (typeEnd+1, value, strlen(value)+1);
	}
    }
    return LDAPU_SUCCESS;
}

static char*
_AVA_value (char* AVA)
{
    return (AVA + strlen (AVA) + 1);
}

static int
_value_match (char* value, char* desc)
{
    auto const int result =
      !ldapu_strcasecmp (_value_normalize(value), desc);
    return result;
}

int
ldapu_member_certificate_match (void* cert, const char* desc)
/*
 *	Return Values: (same as ldapu_find)
 *	    LDAPU_SUCCESS	cert matches desc
 *	    LDAPU_FAILED	cert doesn't match desc
 *	    <rv>		Something went wrong.
 */
{
    auto int err = LDAPU_FAILED;
    auto char*** descRDNs;
    if (!cert || !desc || desc[0] != '{') return LDAPU_FAILED;
    if (desc[1] == '\0') return LDAPU_SUCCESS; /* no AVAs */
    descRDNs = _explode_dn (desc+1);
    if (descRDNs) {
	auto char** descAVAs = (char**)ldapu_malloc(sizeof(char*) * (_rdns_count(descRDNs)+1));
	if (!descAVAs) {
	    err = LDAPU_ERR_OUT_OF_MEMORY;
	} else {
	    auto CERTName* subject = &(((CERTCertificate*)cert)->subject);
	    auto char** descAVA;

	    err = LDAPU_SUCCESS;
	    { /* extract all the AVAs, but not duplicate types, except OU */
		auto size_t descAVAsLen = 0;
		auto char*** descRDN;
		descAVAs[0] = NULL;
		for (descRDN = descRDNs; err == LDAPU_SUCCESS && *descRDN; ++descRDN) {
		    for (descAVA = *descRDN; err == LDAPU_SUCCESS && *descAVA; ++descAVA) {
			err = _explode_AVA (*descAVA);
			if (err == LDAPU_SUCCESS) {
			    if (_is_OU (*descAVA) ||
				!_replaceAVA (*descAVA, descAVAs)) {
				descAVAs[descAVAsLen++] = *descAVA;
				descAVAs[descAVAsLen] = NULL;
			    }
			}
		    }
		}
	    }

	    /* match all the attributes except OU */
	    for (descAVA = descAVAs; err == LDAPU_SUCCESS && *descAVA; ++descAVA) {
		auto struct _attr_getter_pair* descAttr;
		err = LDAPU_FAILED; /* if no match */
		for (descAttr = _attr_getter_table; descAttr->name1; ++descAttr) {
		    if (!ldapu_strcasecmp (*descAVA, descAttr->name1) || (descAttr->name2 &&
			!ldapu_strcasecmp (*descAVA, descAttr->name2))) {
			if (descAttr->getter == NULL) { /* OU attribute */
			    err = LDAPU_SUCCESS; /* for now */
			} else {
			    auto char* certVal = (*(descAttr->getter))(subject);
			    if (certVal && _value_match (certVal, _AVA_value (*descAVA))) {
				err = LDAPU_SUCCESS;
			    }
			    PR_Free (certVal);
			}
			break;
		    }
		}
	    }

	    /* match the OU attributes */
	    if (err == LDAPU_SUCCESS && descAVA != descAVAs) {
		/* Iterate over the OUs in the certificate subject */
		auto CERTRDN** certRDN = subject->rdns;
		descAVA = _previous_OU (descAVA, descAVAs);
		for (; descAVA && *certRDN; ++certRDN) {
		    auto CERTAVA** certAVA = (*certRDN)->avas;
		    for (; descAVA && *certAVA; ++certAVA) {
			auto const int tag = CERT_GetAVATag (*certAVA);
			if (tag == SEC_OID_AVA_ORGANIZATIONAL_UNIT_NAME) {
			    auto const size_t certValLen =(*certAVA)->value.len;
			    auto const size_t lenLen = (certValLen < 128) ? 2 : 3;
			    auto const size_t buflen = certValLen - lenLen;
			    auto char* buf = (char*)ldapu_malloc(buflen+1);
			    if (!buf) {
				err = LDAPU_ERR_OUT_OF_MEMORY;
				descAVA = NULL;
			    } else {
				memcpy (buf, (*certAVA)->value.data+lenLen, buflen);
				buf[buflen] = 0;
				if (_value_match (buf, _AVA_value (*descAVA))) {
				    descAVA = _previous_OU (descAVA, descAVAs);
				}
				free (buf);
			    }
			}
		    }
		}
		if (descAVA) {
		    err = LDAPU_FAILED; /* no match for descAVA in subject */
		}
	    }
	    free (descAVAs);
	}
	_rdns_free (descRDNs);
    }
    return err;
}

