/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "acl.h"

#define BEGIN do {
#define END   } while(0);

/* ------------------------------------------------------------
 * LDAPProxyAuth
 *
 * ProxyAuthControl ::= SEQUENCE {
 *   authorizationDN LDAPDN
 * } 
 */
struct LDAPProxyAuth
{
  char *auth_dn;
};
typedef struct LDAPProxyAuth LDAPProxyAuth;

/*
 * delete_LDAPProxyAuth
 */
static void
delete_LDAPProxyAuth(LDAPProxyAuth *spec)
{
  if (!spec) return;

  slapi_ch_free((void**)&spec->auth_dn);
  slapi_ch_free((void**)&spec);
}

/*
 * parse_LDAPProxyAuth
 *
 * Parse a BER encoded value into the compoents of the LDAP ProxyAuth control.
 * The 'version' parameter should be 1 or 2.
 *
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well) and sets
 * *errtextp if appropriate.
 */
static int
parse_LDAPProxyAuth(struct berval *spec_ber, int version, char **errtextp,
		LDAPProxyAuth **out)
{
  int lderr = LDAP_OPERATIONS_ERROR;	/* pessimistic */
  LDAPProxyAuth *spec = NULL;
  BerElement *ber = NULL;
  char *errstring = "unable to parse proxied authorization control";


  BEGIN
    unsigned long tag;

	if ( version != 1 && version != 2 ) {
		break;
	}

	/* create_LDAPProxyAuth */
    spec = (LDAPProxyAuth*)slapi_ch_calloc(1,sizeof (LDAPProxyAuth));
	if (!spec) {
		break;
	}

    ber = ber_init(spec_ber);
	if (!ber) {
		break;
	}

	if ( version == 1 ) {
		tag = ber_scanf(ber, "{a}", &spec->auth_dn);
	} else {
		tag = ber_scanf(ber, "a", &spec->auth_dn);
	}
    if (tag == LBER_ERROR) {
		lderr = LDAP_PROTOCOL_ERROR;
		break;
	}

	/*
	 * In version 2 of the control, the control value is actually an
	 * authorization ID (see section 9 of RFC 2829).  We only support
	 * the "dnAuthzId" flavor, which looks like "dn:<DN>" where <DN> is
	 * an actual DN, e.g., "dn:uid=bjensen,dc=example,dc=com".  So we
	 * need to strip off the dn: if present and reject the operation if
	 * not.
	 */
	if (2 == version) {
		if ( NULL == spec->auth_dn || strlen( spec->auth_dn ) < 3 ||
				strncmp( "dn:", spec->auth_dn, 3 ) != 0 ) {
			lderr = LDAP_INSUFFICIENT_ACCESS;	/* per Proxied Auth. I-D */
			errstring = "proxied authorization id must be a DN (dn:...)";
			break;
		}
		strcpy( spec->auth_dn, spec->auth_dn + 3 );
	}

	slapi_dn_normalize(spec->auth_dn);
	lderr = LDAP_SUCCESS;	/* got it! */
  END

  /* Cleanup */
  if (ber) ber_free(ber, 0);

  if ( LDAP_SUCCESS != lderr)
  {
    if (spec) delete_LDAPProxyAuth(spec);
	spec = 0;
	if ( NULL != errtextp ) {
		*errtextp = errstring;
	}
  }

  *out = spec;

  return lderr;
}

/*
 * proxyauth_dn - find the users DN in the proxyauth control if it is
 *   present. The return value has been malloced for you.
 *
 * Returns an LDAP error code.  If anything than LDAP_SUCCESS is returned,
 * the error should be returned to the client.  LDAP_SUCCESS is always
 * returned if the proxy auth control is not present or not critical.
 */
int
acl_get_proxyauth_dn( Slapi_PBlock *pb, char **proxydnp, char **errtextp )
{
  char *dn = 0;
  LDAPProxyAuth *spec = 0;
  int rv, lderr = LDAP_SUCCESS;	/* optimistic */

  	BEGIN
		struct berval *spec_ber;
		LDAPControl **controls;
		int present;
		int critical;
		int	version = 1;

		rv = slapi_pblock_get( pb, SLAPI_REQCONTROLS, &controls );
		if (rv) break;

		present = slapi_control_present( controls, LDAP_CONTROL_PROXYAUTH,
			&spec_ber, &critical );
		if (!present) {
			present = slapi_control_present( controls, LDAP_CONTROL_PROXIEDAUTH,
				&spec_ber, &critical );
			if (!present) break;
			version = 2;
			/*
			 * Note the according to the Proxied Authorization I-D, the
			 * control is always supposed to be marked critical by the
			 * client.  If it is not, we return a protocolError.
			 */
			if ( !critical ) {
				lderr = LDAP_PROTOCOL_ERROR;
				if ( NULL != errtextp ) {
					*errtextp = "proxy control must be marked critical";
				}
				break;
			}
		}

		rv = parse_LDAPProxyAuth(spec_ber, version, errtextp, &spec);
		if (LDAP_SUCCESS != rv) {
			if ( critical ) {
				lderr = rv;
			}
			break;
		}

		dn = slapi_ch_strdup(spec->auth_dn);
		if (slapi_dn_isroot(dn) ) {
			lderr = LDAP_UNWILLING_TO_PERFORM;
			*errtextp = "Proxy dn should not be rootdn";
			break;
			
		}
	END

    if (spec) delete_LDAPProxyAuth(spec);

	if ( NULL != proxydnp ) {
		*proxydnp = dn;
	} else {
		slapi_ch_free( (void **)&dn );
	}

	return lderr;
}

