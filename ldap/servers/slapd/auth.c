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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h> /* getenv */
#include <string.h> /* memcpy */
#include <ldaputil/ldaputil.h> /* LDAPU_SUCCESS, ldapu_VTable_set */
#include <ldaputil/init.h> /* ldaputil_init */
#include <ldaputil/certmap.h> /* ldapu_cert_to_ldap_entry */
#ifndef _WIN32
#include <sys/param.h>	/* MAXPATHLEN */
#endif
#include "slap.h" /* slapi_ch_malloc */
#include "fe.h"

char* client_auth_config_file = NULL;

/* forward declarations */

static void generate_id();
static Slapi_ComponentId * auth_get_component_id();

#define internal_ld NULL

static int LDAP_CALL LDAP_CALLBACK
slapu_msgfree( LDAP* ld, LDAPMessage* msg )
{
    Slapi_PBlock* pb = (Slapi_PBlock*)msg;
    if (ld != internal_ld) {
	return ldap_msgfree (msg);
    }
    if (pb) {
	slapi_free_search_results_internal (pb);
	slapi_pblock_destroy (pb);
    }
    return LDAP_SUCCESS;
}

static int LDAP_CALL LDAP_CALLBACK
slapu_search_s( LDAP* ld, const char* rawbaseDN, int scope, const char* filter, 
	char** attrs, int attrsonly, LDAPMessage** result )
{
    int err = LDAP_NO_SUCH_OBJECT;
    Slapi_PBlock* pb = NULL;
    LDAPControl **ctrls;
    Slapi_DN *sdn = slapi_sdn_new_dn_byval(rawbaseDN);
    const char *baseDN = slapi_sdn_get_dn(sdn);

    if (ld != internal_ld) {
        err = ldap_search_ext_s(ld, baseDN, scope, filter, attrs, attrsonly,
                                NULL, NULL, NULL, -1, result);
        slapi_sdn_free(&sdn);
        return err;
    }
    LDAPDebug (LDAP_DEBUG_TRACE, "=> slapu_search_s (\"%s\", %i, %s)\n",
               baseDN, scope, filter);
    if (filter == NULL) filter = "objectclass=*";

    /* use new internal search API */
    pb=slapi_pblock_new();
    /* we need to provide managedsait control to avoid returning continuation references */
    ctrls = (LDAPControl **)slapi_ch_calloc (2, sizeof (LDAPControl *));
    ctrls[0] = (LDAPControl*)slapi_ch_malloc (sizeof (LDAPControl));
    ctrls[0]->ldctl_oid = slapi_ch_strdup (LDAP_CONTROL_MANAGEDSAIT);
    ctrls[0]->ldctl_value.bv_val = NULL;
    ctrls[0]->ldctl_value.bv_len = 0;
    ctrls[0]->ldctl_iscritical = '\0';
    slapi_search_internal_set_pb(pb, baseDN, scope, (char *)filter, attrs,
                                 attrsonly, ctrls, NULL,
                                 auth_get_component_id(), 0 /* actions */);
    slapi_search_internal_pb(pb);

    if (pb != NULL) {
        if (slapi_pblock_get (pb, SLAPI_PLUGIN_INTOP_RESULT, &err)) {
            err = LDAP_LOCAL_ERROR;
        }
        if (err != LDAP_SUCCESS) {
            slapu_msgfree (ld, (LDAPMessage*)pb);
            pb = NULL;
            if (scope == LDAP_SCOPE_SUBTREE) {
                char fbuf[ BUFSIZ ];
                LDAPDebug (LDAP_DEBUG_ANY, "slapi_search_internal (\"%s\", subtree, %s) err %i\n",
                           baseDN, escape_string( (char*)filter, fbuf ), err);
            }
        }
    } else {
        char fbuf[ BUFSIZ ];
        LDAPDebug (LDAP_DEBUG_ANY, "slapi_search_internal (\"%s\", %i, %s) NULL\n",
                   baseDN, scope, escape_string( (char*)filter, fbuf ));
    }
    slapi_sdn_free(&sdn);
    *result = (LDAPMessage*)pb;
    LDAPDebug (LDAP_DEBUG_TRACE, "<= slapu_search_s %i\n", err, 0, 0);
    return err;
}

static int LDAP_CALL LDAP_CALLBACK
slapu_count_entries( LDAP* ld, LDAPMessage* msg )
{
    Slapi_Entry** entry = NULL;
    int count = 0;
    if (ld != internal_ld) {
	return ldap_count_entries (ld, msg);
    }
    if (!slapi_pblock_get ((Slapi_PBlock*)msg, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entry)
	&& entry) {
	for (; *entry; ++entry) ++count;
    }
    return count;
}

/* slapu_search_s() returns a Slapi_PBlock*, but slapu_first_entry() and
 * slapu_next_entry() return a Slapi_Entry** pointing into the same array
 * as the PBlock.  If one of the iteration (Slapi_Entry**) pointers was
 * passed to slapu_msgfree(), havoc would ensue.  ldaputil never does this.
 * But ldap_msgfree() would support it (no?); so a plugin function might.
 * Yet another way this doesn't support plugin functions.
 */

static LDAPMessage* LDAP_CALL LDAP_CALLBACK
slapu_first_entry( LDAP* ld, LDAPMessage* msg )
{
    Slapi_Entry** entry = NULL;
    if (ld != internal_ld) {
	return ldap_first_entry (ld, msg);
    }
    if (!slapi_pblock_get ((Slapi_PBlock*)msg, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entry)
	&& entry && *entry) {
	return (LDAPMessage*)entry;
    }
    return NULL;
}

static LDAPMessage* LDAP_CALL LDAP_CALLBACK
slapu_next_entry( LDAP* ld, LDAPMessage* msg )
{
    Slapi_Entry** entry = (Slapi_Entry**)msg;
    if (ld != internal_ld) {
	if (msg) {
	    return ldap_next_entry (ld, msg);
	} else {
	    return NULL;
	}
    }
    if (entry && *entry && *++entry) {
	return (LDAPMessage*)entry;
    }
    return NULL;
}

static char* LDAP_CALL LDAP_CALLBACK
slapu_get_dn( LDAP* ld, LDAPMessage* entry )
{
    if (ld != internal_ld) {
	return ldap_get_dn (ld, entry);
    }
    return slapi_ch_strdup (slapi_entry_get_dn (*(Slapi_Entry**)entry));
}

static void LDAP_CALL LDAP_CALLBACK
slapu_memfree( LDAP* ld, void* dn )
{
    if (ld != internal_ld) {
	ldap_memfree (dn);
    } else {
	free (dn);
    }
}

static char*
slapu_attr_get_desc( Slapi_Attr *attr )
{
    char* desc = NULL;
    if (slapi_attr_get_type (attr, &desc) == LDAP_SUCCESS && desc) {
	return slapi_ch_strdup (desc);
    }
    return NULL;
}

/* slapu_first_attribute and slapu_next_attribute use a Slapi_Attr*
 * as an iterator.  It is malloc'd by first() and free'd by ber_free().
 */

static char* LDAP_CALL LDAP_CALLBACK
slapu_first_attribute( LDAP* ld, LDAPMessage* entry, BerElement** iter )
{
    if (ld != internal_ld) {
	return ldap_first_attribute (ld, entry, iter);
    } else {
	Slapi_Attr** attr = (Slapi_Attr**) slapi_ch_malloc (sizeof(Slapi_Attr*));
	*iter = (BerElement*) attr;
	if (attr && slapi_entry_first_attr (*(Slapi_Entry**)entry, attr) == LDAP_SUCCESS) {
	    return slapu_attr_get_desc (*attr);
	}
    }
    return NULL;
}

static char* LDAP_CALL LDAP_CALLBACK
slapu_next_attribute( LDAP* ld, LDAPMessage* entry, BerElement* iter)
{
    Slapi_Attr** attr = (Slapi_Attr**)iter;
    if (ld != internal_ld) {
	return ldap_next_attribute (ld, entry, iter);
    }
    if (attr && slapi_entry_next_attr (*(Slapi_Entry**)entry, *attr, attr) == LDAP_SUCCESS) {
	return slapu_attr_get_desc (*attr);
    }
    return NULL;
}

static void LDAP_CALL LDAP_CALLBACK
slapu_ber_free( LDAP* ld, BerElement* iter, int freebuf )
{
    if (ld != internal_ld) {
	ber_free (iter, freebuf);
    } else {
	free ((Slapi_Attr**)iter);
    }
}

static struct berval** LDAP_CALL LDAP_CALLBACK
slapu_get_values_len( LDAP *ld, LDAPMessage *entry, const char *desc )
{
    Slapi_Attr* attr = NULL;
    if (ld != internal_ld) {
	return ldap_get_values_len (ld, entry, desc);
    }
    if (slapi_entry_attr_find (*(Slapi_Entry**)entry, desc, &attr) == LDAP_SUCCESS
	&& attr) {
	struct berval** values = NULL;
	if ( slapi_attr_get_bervals_copy (attr, &values) == 0 ) {
		return (values);
	}
    }
    return NULL;
}

static void LDAP_CALL LDAP_CALLBACK
slapu_value_free_len( LDAP* ld, struct berval **values )
{
    if (ld != internal_ld) {
	ldap_value_free_len (values);
    } else {
	ber_bvecfree (values);
    }
}

void
client_auth_init ()
{
    int err;
    if (client_auth_config_file == NULL) {
	char *confdir = config_get_configdir();
	if (NULL == confdir) {
	    LDAPDebug (LDAP_DEBUG_ANY,
		"client_auth_init: failed to get configdir\n",
		0, 0, 0);
	    return;
	}
	client_auth_config_file = PR_smprintf("%s/certmap.conf", confdir);
	if (NULL == client_auth_config_file) {
	    LDAPDebug (LDAP_DEBUG_ANY,
		"client_auth_init: failed to duplicate \"%s/certmap\"\n",
		confdir, 0, 0);
	    slapi_ch_free_string(&confdir);
	    return;
	}
	slapi_ch_free_string(&confdir);
    }
    err = ldaputil_init (client_auth_config_file, "", NULL, "slapd", NULL);
    if (err != LDAPU_SUCCESS) {
	LDAPDebug (LDAP_DEBUG_TRACE, "ldaputil_init(%s,...) %i\n",
		client_auth_config_file, err, 0);
    } else {
	LDAPUVTable_t vtable = {
	    NULL /* ssl_init */,
	    NULL /* set_option */,
	    NULL /* simple_bind_s */,
	    NULL /* unbind */,
	    slapu_search_s,
	    slapu_count_entries,
	    slapu_first_entry,
	    slapu_next_entry,
	    slapu_msgfree,
	    slapu_get_dn,
	    slapu_memfree,
	    slapu_first_attribute,
	    slapu_next_attribute,
	    slapu_ber_free,
	    NULL /* get_values */,
	    NULL /* value_free */,
	    slapu_get_values_len,
	    slapu_value_free_len};
	ldapu_VTable_set (&vtable);
    }

    /* Generate a component id for cert-based authentication */
    generate_id();
}

#include <ssl.h>
#include "slapi-plugin.h" /* SLAPI_BERVAL_EQ */
#include "slapi-private.h"	/* COMPONENT_CERT_AUTH */

static Slapi_ComponentId * auth_component_id=NULL;

static void generate_id() 
{
	if (auth_component_id == NULL ) {
	    	auth_component_id=generate_componentid (NULL /* Not a plugin */ , COMPONENT_CERT_AUTH);
	}
}

static Slapi_ComponentId * auth_get_component_id() {
	return auth_component_id;
}


static char*
subject_of (CERTCertificate* cert)
{
    char* dn = NULL;
    if (cert != NULL) {
	int err = ldapu_get_cert_subject_dn (cert, &dn);
	if (err != LDAPU_SUCCESS) {
	    LDAPDebug (LDAP_DEBUG_ANY, "ldapu_get_cert_subject_dn(%p) %i (%s)\n",
		       (void*)cert, err, ldapu_err2string (err));
	}
    }
    return dn;
}

static char*
issuer_of (CERTCertificate* cert)
{
    char* dn = NULL;
    if (cert != NULL) {
	int err = ldapu_get_cert_issuer_dn (cert, &dn);
	if (err != LDAPU_SUCCESS) {
	    LDAPDebug (LDAP_DEBUG_ANY, "ldapu_get_cert_issuer_dn(%p) %i (%s)\n",
		       (void*)cert, err, ldapu_err2string (err));
	}
    }
    return dn;
}

/*
 * Log a certificate that was rejected because the client didn't
 * authenticate it.
 *
 * Note: handle_bad_certificate() is called via slapd_ssl_badCertHook().
 * A Connection * is passed in client data. That connection must have its
 * c_mutex locked.
 */
int
handle_bad_certificate (void* clientData, PRFileDesc *prfd)
{
    char sbuf[ BUFSIZ ], ibuf[ BUFSIZ ];
    Connection* conn = (Connection*) clientData;
    CERTCertificate* clientCert = slapd_ssl_peerCertificate (prfd);

    PRErrorCode errorCode = PR_GetError();
    char* subject = subject_of (clientCert);
    char* issuer  = issuer_of  (clientCert);
    slapi_log_access( LDAP_DEBUG_STATS,
	       "conn=%" NSPRIu64 " " SLAPI_COMPONENT_NAME_NSPR " error %i (%s); unauthenticated client %s; issuer %s\n",
	       (long long unsigned int)conn->c_connid, errorCode, slapd_pr_strerror(errorCode),
	       subject ? escape_string( subject, sbuf ) : "NULL",
	       issuer  ? escape_string( issuer,  ibuf ) : "NULL" );
    if (issuer)  free (issuer);
    if (subject) free (subject);
    if (clientCert) CERT_DestroyCertificate (clientCert);
    return -1; /* non-zero means reject this certificate */
}


/*
 * Get an identity from the client's certificate (if any was sent).
 *
 * Note: handle_handshake_done() is called via slapd_ssl_handshakeCallback().
 * A Connection * is passed in client data. That connection must have its
 * c_mutex locked.
 */
void
handle_handshake_done (PRFileDesc *prfd, void* clientData)
{
    Connection* conn = (Connection*) clientData;
    CERTCertificate* clientCert = slapd_ssl_peerCertificate(prfd);

    char* clientDN = NULL;
    int keySize = 0;
    char* cipher = NULL;
    char* extraErrorMsg = "";
	SSLChannelInfo channelInfo;
	SSLCipherSuiteInfo cipherInfo;
	char* subject = NULL;

	if ( (slapd_ssl_getChannelInfo (prfd, &channelInfo, sizeof(channelInfo))) != SECSuccess ) {
		PRErrorCode errorCode = PR_GetError();
		slapi_log_access (LDAP_DEBUG_STATS,
			"conn=%" NSPRIu64 " SSL failed to obtain channel info; "
			SLAPI_COMPONENT_NAME_NSPR " error %i (%s)\n",
			(long long unsigned int)conn->c_connid, errorCode, slapd_pr_strerror(errorCode));
		goto done;
	}
	if ( (slapd_ssl_getCipherSuiteInfo (channelInfo.cipherSuite, &cipherInfo, sizeof(cipherInfo)) )
			!= SECSuccess) {
		PRErrorCode errorCode = PR_GetError();
		slapi_log_access (LDAP_DEBUG_STATS,
			"conn=%" NSPRIu64 " SSL failed to obtain cipher info; "
			SLAPI_COMPONENT_NAME_NSPR " error %i (%s)\n",
			(long long unsigned int)conn->c_connid, errorCode, slapd_pr_strerror(errorCode));
		goto done;
	}

	keySize = cipherInfo.effectiveKeyBits;
	cipher = slapi_ch_strdup(cipherInfo.symCipherName);

    /* If inside an Start TLS operation, perform the privacy level discovery
     * and if the security degree achieved after the handshake is not reckoned 
     * to be enough, close the SSL connection. */
    if ( conn->c_flags & CONN_FLAG_START_TLS ) {
        if ( cipherInfo.symKeyBits == 0 ) {
	        start_tls_graceful_closure( conn, NULL, 1 );
		goto done;
	}
    }

    if (config_get_SSLclientAuth() == SLAPD_SSLCLIENTAUTH_OFF ) {
		slapi_log_access (LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " SSL %i-bit %s\n",
				(long long unsigned int)conn->c_connid, keySize, cipher ? cipher : "NULL" );
		goto done;
    } 
    if (clientCert == NULL) {
	slapi_log_access (LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " SSL %i-bit %s\n",
			(long long unsigned int)conn->c_connid, keySize, cipher ? cipher : "NULL" );
    } else {
	subject = subject_of (clientCert);
	if (!subject) {
		slapi_log_access( LDAP_DEBUG_STATS,
		       "conn=%" NSPRIu64 " SSL %i-bit %s; missing subject\n",
		       (long long unsigned int)conn->c_connid, keySize, cipher ? cipher : "NULL");
		goto done;
	}
	{
	    char* issuer  = issuer_of (clientCert);
	    char sbuf[ BUFSIZ ], ibuf[ BUFSIZ ];
	    slapi_log_access( LDAP_DEBUG_STATS,
		       "conn=%" NSPRIu64 " SSL %i-bit %s; client %s; issuer %s\n",
		       (long long unsigned int)conn->c_connid, keySize, cipher ? cipher : "NULL",
		       subject ? escape_string( subject, sbuf ) : "NULL",
		       issuer  ? escape_string( issuer,  ibuf ) : "NULL");
	    if (issuer) free (issuer);
	}
	slapi_dn_normalize (subject);
	{
	    LDAPMessage* chain = NULL;
		char *basedn = config_get_basedn();
		int err;

	    err = ldapu_cert_to_ldap_entry
	              (clientCert, internal_ld, basedn?basedn:""/*baseDN*/, &chain);
	    if (err == LDAPU_SUCCESS && chain) {
		LDAPMessage* entry = slapu_first_entry (internal_ld, chain);
		if (entry) {
		    /* clientDN is duplicated in slapu_get_dn */
		    clientDN = slapu_get_dn (internal_ld, entry);
		} else {
		  
		    extraErrorMsg = "no entry";
		    LDAPDebug (LDAP_DEBUG_TRACE, "<= ldapu_cert_to_ldap_entry() %s\n",
			       extraErrorMsg, 0, 0);
		}
	    } else {
		extraErrorMsg = ldapu_err2string(err);
	        LDAPDebug (LDAP_DEBUG_TRACE, "<= ldapu_cert_to_ldap_entry() %i (%s)%s\n",
			   err, extraErrorMsg, chain ? "" : " NULL");
	    }
		slapi_ch_free_string(&basedn);
	    slapu_msgfree (internal_ld, chain);
	}
    }

    if (clientDN != NULL) {
        Slapi_DN *sdn = NULL;
        sdn = slapi_sdn_new_dn_passin(clientDN);
        clientDN = slapi_ch_strdup(slapi_sdn_get_dn(sdn));
        slapi_sdn_free(&sdn);
        slapi_log_access (LDAP_DEBUG_STATS, 
                          "conn=%" NSPRIu64 " SSL client bound as %s\n",
                          (long long unsigned int)conn->c_connid, clientDN);
    } else if (clientCert != NULL) {
        slapi_log_access (LDAP_DEBUG_STATS,
                          "conn=%" NSPRIu64 " SSL failed to map client "
                          "certificate to LDAP DN (%s)\n",
                          (long long unsigned int)conn->c_connid, extraErrorMsg );
    }

	/*
	 * Associate the new credentials with the connection.  Note that
	 * clientDN and clientCert may be NULL.
	 */
	bind_credentials_set_nolock( conn, SLAPD_AUTH_SSL, clientDN,
			SLAPD_AUTH_SSL, clientDN, clientCert , NULL);
done:
    slapi_ch_free_string(&subject);
    slapi_ch_free_string(&cipher);
    /* clientDN and clientCert will be freed later */
}
