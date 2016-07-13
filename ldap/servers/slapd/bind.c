/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #193297
 *     Bugfix for bug #201275
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* bind.c - decode an ldap bind operation and pass it to a backend db */

/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"
#include "fe.h"
#include "pratom.h"
#include <sasl.h>

static void log_bind_access(
    Slapi_PBlock *pb, 
    const char* dn, 
    ber_tag_t method, 
    int version,
    const char *saslmech,
    const char *msg
);


/* 
 * Function: is_root_dn_pw
 * 
 * Returns: 1 if the password for the root dn is correct.
 *          0 otherwise.
 * dn must be normalized
 *
 */
static int
is_root_dn_pw( const char *dn, const Slapi_Value *cred )
{
	int rv= 0;
	char *rootpw = config_get_rootpw();
	if ( rootpw == NULL || !slapi_dn_isroot( dn ) )
	{
	       rv = 0;
	}
	else
	{
		Slapi_Value rdnpwbv;
		Slapi_Value *rdnpwvals[2];
		slapi_value_init_string(&rdnpwbv,rootpw);
		rdnpwvals[ 0 ] = &rdnpwbv;
		rdnpwvals[ 1 ] = NULL;
		rv = slapi_pw_find_sv( rdnpwvals, cred ) == 0;
		value_done(&rdnpwbv);
	}
	slapi_ch_free_string( &rootpw );
	return rv;
}

void
do_bind( Slapi_PBlock *pb )
{
    BerElement	*ber = pb->pb_op->o_ber;
    int		err, isroot;
    ber_tag_t 	method = LBER_DEFAULT;
    ber_int_t	version = -1;
    int		auth_response_requested = 0;
    int		pw_response_requested = 0;
    char		*rawdn = NULL;
    const char	*dn = NULL;
    char		*saslmech = NULL;
    struct berval	cred = {0};
    Slapi_Backend		*be = NULL;
    ber_tag_t ber_rc;
    int rc = 0;
    Slapi_DN *sdn = NULL;
    int bind_sdn_in_pb = 0; /* is sdn set in the pb? */
    Slapi_Entry *referral;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
    char **supported, **pmech;
    char authtypebuf[256]; /* >26 (strlen(SLAPD_AUTH_SASL)+SASL_MECHNAMEMAX+1) */
    Slapi_Entry *bind_target_entry = NULL;
    int auto_bind = 0;
    int minssf = 0;
    int minssf_exclude_rootdse = 0;
    Slapi_DN *original_sdn = NULL;

    LDAPDebug( LDAP_DEBUG_TRACE, "do_bind\n", 0, 0, 0 );

    /*
     * Parse the bind request.  It looks like this:
     *
     *	BindRequest ::= SEQUENCE {
     *		version		INTEGER,		 -- version
     *		name		DistinguishedName,	 -- dn
     *		authentication	CHOICE {
     *			simple		[0] OCTET STRING, -- passwd
     *			krbv42ldap	[1] OCTET STRING, -- not used
     *			krbv42dsa	[2] OCTET STRING, -- not used
     *			sasl		[3] SaslCredentials -- v3 only
     *		}
     *	}
     *
     *	Saslcredentials ::= SEQUENCE {
     *		mechanism	LDAPString,
     *		credentials	OCTET STRING
     *	}
     */

    ber_rc = ber_scanf( ber, "{iat", &version, &rawdn, &method );
    if ( ber_rc == LBER_ERROR ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "ber_scanf failed (op=Bind; params=Version,DN,Method)\n",
                   0, 0, 0 );
        log_bind_access (pb, "???", method, version, saslmech, "decoding error");
        send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
                          "decoding error", 0, NULL );
        slapi_ch_free_string(&rawdn);
        return;
    }
    /* Check if we should be performing strict validation. */
    if (rawdn && config_get_dn_validate_strict()) { 
        /* check that the dn is formatted correctly */
        rc = slapi_dn_syntax_check(pb, rawdn, 1);
        if (rc) { /* syntax check failed */
            op_shared_log_error_access(pb, "BIND", rawdn,
                                       "strict: invalid bind dn");
            send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
                             NULL, "invalid bind dn", 0, NULL);
            slapi_ch_free_string(&rawdn);
            return;
        }
    }
    sdn = slapi_sdn_new_dn_passin(rawdn);
    dn = slapi_sdn_get_dn(sdn);
    if (rawdn && (strlen(rawdn) > 0) && (NULL == dn)) {
        /* normalization failed */
        op_shared_log_error_access(pb, "BIND", rawdn, "invalid bind dn");
        send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                         "invalid bind dn", 0, NULL);
        slapi_sdn_free(&sdn);
        return;
    }
    LDAPDebug( LDAP_DEBUG_TRACE, "BIND dn=\"%s\" method=%" BERTAG_T " version=%d\n",
               dn?dn:"empty", method, version );

    /* target spec is used to decide which plugins are applicable for the operation */
    operation_set_target_spec (pb->pb_op, sdn);

    switch ( method ) {
    case LDAP_AUTH_SASL:
        if ( version < LDAP_VERSION3 ) {
            LDAPDebug( LDAP_DEBUG_ANY,
                       "got SASL credentials from LDAPv2 client\n",
                       0, 0, 0 );
            log_bind_access (pb, dn?dn:"empty", method, version, saslmech, "SASL credentials only in LDAPv3");
            send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
                              "SASL credentials only in LDAPv3", 0, NULL );
            goto free_and_return;
        }
        /* Get the SASL mechanism */
        ber_rc = ber_scanf( ber, "{a", &saslmech );
        /* Get the (optional) SASL credentials */
        if ( ber_rc != LBER_ERROR ) {
            /* Credentials are optional in SASL bind */
            ber_len_t clen;
            if (( ber_peek_tag( ber, &clen )) == LBER_OCTETSTRING ) {
                ber_rc = ber_scanf( ber, "o}}", &cred );
                if (cred.bv_len == 0) {
                    slapi_ch_free_string(&cred.bv_val);
                }
            } else {
                ber_rc = ber_scanf( ber, "}}" );
            }
        }
        break;
    case LDAP_AUTH_KRBV41:
        /* FALLTHROUGH */
    case LDAP_AUTH_KRBV42:
        if ( version >= LDAP_VERSION3 ) {
            static char *kmsg = 
                "LDAPv2-style kerberos authentication received "
                "on LDAPv3 connection.";
            LDAPDebug( LDAP_DEBUG_ANY, kmsg, 0, 0, 0 );
            log_bind_access (pb, dn?dn:"empty", method, version, saslmech, kmsg);
            send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
                              kmsg, 0, NULL );
            goto free_and_return;
        }
        /* FALLTHROUGH */
    case LDAP_AUTH_SIMPLE:
        ber_rc = ber_scanf( ber, "o}", &cred );
        if (cred.bv_len == 0) {
            slapi_ch_free_string(&cred.bv_val);
        }
        break;
    default:
        log_bind_access (pb, dn?dn:"empty", method, version, saslmech, "Unknown bind method");
        send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
                          "Unknown bind method", 0, NULL );
        goto free_and_return;
    }
    if ( ber_rc == LBER_ERROR ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "ber_scanf failed (op=Bind; params=Credentials)\n",
                   0, 0, 0 );
        log_bind_access (pb, dn?dn:"empty", method, version, saslmech, "decoding error");
        send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
                          "decoding error", 0, NULL );
        goto free_and_return;
    }

    /*
     * in LDAPv3 there can be optional control extensions on
     * the end of an LDAPMessage. we need to read them in and
     * pass them to the backend.
     * We also check for the presence of an "Authentication Request
     * Control" and set a flag so we know later whether we need to send
     * an "Authentication Response Control" with Success responses.
     */
    {
        LDAPControl	**reqctrls;

        if (( err = get_ldapmessage_controls( pb, ber, &reqctrls ))
            != 0 ) {
            log_bind_access (pb, dn?dn:"empty", method,
                             version, saslmech, "failed to parse LDAP controls");
            send_ldap_result( pb, err, NULL, NULL, 0, NULL );
            goto free_and_return;
        }

        auth_response_requested =  slapi_control_present( reqctrls,
                                                          LDAP_CONTROL_AUTH_REQUEST, NULL, NULL );
        slapi_pblock_get (pb, SLAPI_PWPOLICY, &pw_response_requested);
    }

    PR_EnterMonitor(pb->pb_conn->c_mutex);

    bind_credentials_clear( pb->pb_conn, PR_FALSE, /* do not lock conn */
                            PR_FALSE /* do not clear external creds. */ );

#if defined(ENABLE_AUTOBIND)
    /* LDAPI might have auto bind on, binding as anon should
       mean bind as self in this case
     */
    /* You are "bound" when the SSL connection is made, 
       but the client still passes a BIND SASL/EXTERNAL request.
     */
    if((LDAP_AUTH_SASL == method) &&
       (0 == strcasecmp (saslmech, LDAP_SASL_EXTERNAL)) &&
       (0 == dn || 0 == dn[0]) && pb->pb_conn->c_unix_local)
    {
        slapd_bind_local_user(pb->pb_conn);
        if(pb->pb_conn->c_dn)
        {
            auto_bind = 1; /* flag the bind method */
            dn = slapi_ch_strdup(pb->pb_conn->c_dn);
            slapi_sdn_free(&sdn);
            sdn = slapi_sdn_new_dn_passin(dn);
        }
    }
#endif /* ENABLE_AUTOBIND */

    /* Clear the password policy flag that forbid operation
     * other than Bind, Modify, Unbind :
     * With a new bind, the flag should be reset so that the new
     * bound user can work properly
     */
    pb->pb_conn->c_needpw = 0;
    PR_ExitMonitor(pb->pb_conn->c_mutex);

    log_bind_access(pb, dn?dn:"empty", method, version, saslmech, NULL);

    switch ( version ) {
    case LDAP_VERSION2:
        if (method == LDAP_AUTH_SIMPLE
            && (config_get_force_sasl_external() ||
                ((dn == NULL || *dn == '\0') && cred.bv_len == 0))
            && pb->pb_conn->c_external_dn != NULL) {
            /* Treat this like a SASL EXTERNAL Bind: */
            method = LDAP_AUTH_SASL;
            saslmech = slapi_ch_strdup (LDAP_SASL_EXTERNAL);
            /* This enables a client to establish an identity by sending
             * a certificate in the SSL handshake, and also use LDAPv2
             * (by sending this type of Bind request).
             */
        }
        break;
    case LDAP_VERSION3:
        if ((method == LDAP_AUTH_SIMPLE) &&
            config_get_force_sasl_external() &&
            (pb->pb_conn->c_external_dn != NULL)) {
            /* Treat this like a SASL EXTERNAL Bind: */
            method = LDAP_AUTH_SASL;
            saslmech = slapi_ch_strdup (LDAP_SASL_EXTERNAL);
            /* This enables a client to establish an identity by sending
             * a certificate in the SSL handshake, and also use LDAPv2
             * (by sending this type of Bind request).
             */
        }
        break;
    default:
        LDAPDebug( LDAP_DEBUG_TRACE, "bind: unknown LDAP protocol version %d\n",
                   version, 0, 0 );
        send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
                          "version not supported", 0, NULL );
        goto free_and_return;
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "do_bind: version %d method 0x%x dn %s\n",
               version, method, dn );
    pb->pb_conn->c_ldapversion = version;

    isroot = slapi_dn_isroot( slapi_sdn_get_ndn(sdn) );
    slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
    slapi_pblock_set( pb, SLAPI_BIND_TARGET_SDN, (void*)sdn );
    bind_sdn_in_pb = 1; /* pb now owns sdn */
    slapi_pblock_set( pb, SLAPI_BIND_METHOD, &method );
    slapi_pblock_set( pb, SLAPI_BIND_SASLMECHANISM, saslmech );
    slapi_pblock_set( pb, SLAPI_BIND_CREDENTIALS, &cred );

    if (method != LDAP_AUTH_SASL) {
        /*
         * RFC2251: client may abort a sasl bind negotiation by sending
         * an authentication choice other than sasl.
         */
        pb->pb_conn->c_flags &= ~CONN_FLAG_SASL_CONTINUE;
    }

    switch ( method ) {
    case LDAP_AUTH_SASL:
        /*
         * All SASL auth methods are categorized as strong binds,
         * although they are not necessarily stronger than simple.
         */
        slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsStrongAuthBinds);
        if ( saslmech == NULL || *saslmech == '\0' ) {
            send_ldap_result( pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                              "SASL mechanism absent", 0, NULL );
            goto free_and_return;
        }

        if (strlen(saslmech) > SASL_MECHNAMEMAX) {
            send_ldap_result( pb, LDAP_AUTH_METHOD_NOT_SUPPORTED, NULL,
                              "SASL mechanism name is too long", 0, NULL );
            goto free_and_return;
        }

        supported = slapi_get_supported_saslmechanisms_copy();
        if ( (pmech = supported) != NULL ) while (1) {
            if (*pmech == NULL) {
                /*
                 * As we call the safe function, we receive a strdup'd saslmechanisms
                 * charray. Therefore, we need to remove it instead of NULLing it
                 */
                charray_free(supported);
                pmech = supported = NULL;
                break;
            }
            if (!strcasecmp (saslmech, *pmech)) break;
            ++pmech;
        }
        if (!pmech) {
            /* now check the sasl library */
            /* ids_sasl_check_bind takes care of calling bind
             * pre-op plugins after it knows the target DN */
            ids_sasl_check_bind(pb);
            plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
            goto free_and_return;
        }
        else {
            charray_free(supported); /* Avoid leaking */
        }

        if (!strcasecmp (saslmech, LDAP_SASL_EXTERNAL)) {
            /* call preop plugins */
            if (plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_BIND_FN ) != 0){
                send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "", 0, NULL);
                goto free_and_return;
            }

#if defined(ENABLE_AUTOBIND)
            if (1 == auto_bind) {
                /* Already AUTO-BOUND */
                break;
            }
#endif
            /*
             * if this is not an SSL connection, fail and return an
             * inappropriateAuth error.
             */
            if ( 0 == ( pb->pb_conn->c_flags & CONN_FLAG_SSL )) {
                send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                                  "SASL EXTERNAL bind requires an SSL connection",
                                  0, NULL );
                /* call postop plugins */
                plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
                goto free_and_return;
            }

            /*
             * Check for the client certificate.
             */
            if( NULL == pb->pb_conn->c_client_cert){
                send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                                  "missing client certificate", 0, NULL );
                /* call postop plugins */
                plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
                goto free_and_return;
            }

            /*
             * if the client sent us a certificate but we could not map it
             * to an LDAP DN, fail and return an invalidCredentials error.
             */
            if ( NULL == pb->pb_conn->c_external_dn ) {
                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "Client certificate mapping failed");
                send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL, "", 0, NULL);
                /* call postop plugins */
                plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
                goto free_and_return;
            }

            if (!isroot) {
                /* check if the account is locked */
                bind_target_entry = get_entry(pb, pb->pb_conn->c_external_dn);
                if ( bind_target_entry && slapi_check_account_lock(pb, bind_target_entry,
                     pw_response_requested, 1 /*check password policy*/, 1 /*send ldap result*/) == 1) {
                    /* call postop plugins */
                    plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
                    goto free_and_return;
                }
            }

            /*
             * copy external credentials into connection structure
             */
            bind_credentials_set( pb->pb_conn,
                                  pb->pb_conn->c_external_authtype, 
                                  pb->pb_conn->c_external_dn,
                                  NULL, NULL, NULL , NULL);
            if ( auth_response_requested ) {
                slapi_add_auth_response_control( pb, pb->pb_conn->c_external_dn );
            }
            send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
            /* call postop plugins */
            plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
            goto free_and_return;
        }
        break;
    case LDAP_AUTH_SIMPLE:
        slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsSimpleAuthBinds);

        /* Check if the minimum SSF requirement has been met. */
        minssf = config_get_minssf();
        /* 
         * If nsslapd-minssf-exclude-rootdse is on, we have to go to the 
         * next step and check if the operation is against rootdse or not.
         * Once found it's not on rootdse, return LDAP_UNWILLING_TO_PERFORM
         * there.
         */
        minssf_exclude_rootdse = config_get_minssf_exclude_rootdse();
        if (!minssf_exclude_rootdse && (pb->pb_conn->c_sasl_ssf < minssf) &&
            (pb->pb_conn->c_ssl_ssf < minssf) &&
            (pb->pb_conn->c_local_ssf < minssf)) {
            send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                             "Minimum SSF not met.", 0, NULL);
            /* increment BindSecurityErrorcount */
            slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
            goto free_and_return;
        }

        /* accept null binds */
        if (dn == NULL || *dn == '\0') {
            slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsAnonymousBinds);
            /* by definition anonymous is also unauthenticated so increment 
               that counter */
            slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds);

            /* Refuse the operation if anonymous access is disabled.  We need to allow
             * an anonymous bind through if only root DSE anonymous access is set too. */
            if (config_get_anon_access_switch() == SLAPD_ANON_ACCESS_OFF) {
                send_ldap_result(pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                                 "Anonymous access is not allowed", 0, NULL);
                /* increment BindSecurityErrorcount */
                slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                goto free_and_return;
            }

            /* set the bind credentials so anonymous limits are set */
            bind_credentials_set( pb->pb_conn, SLAPD_AUTH_NONE,
                                      NULL, NULL, NULL, NULL , NULL);

            /* call preop plugins */
            if (plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_BIND_FN ) == 0){
                if ( auth_response_requested ) {
                    slapi_add_auth_response_control( pb, "" );
                }
                send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

                /* call postop plugins */
                plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
            } else {
                send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "", 0, NULL);
            }
            goto free_and_return;
        /* Check if unauthenticated binds are allowed. */
        } else if ( cred.bv_len == 0 ) {
            /* Increment unauthenticated bind counter */
            slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds);

            /* Refuse the operation if anonymous access is disabled. */
            if (config_get_anon_access_switch() != SLAPD_ANON_ACCESS_ON) {
                send_ldap_result(pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                                 "Anonymous access is not allowed", 0, NULL);
                /* increment BindSecurityErrorcount */
                slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                goto free_and_return;
            }

            /* Refuse the operation if unauthenticated binds are disabled. */
            if (!config_get_unauth_binds_switch()) {
                /* As stated in RFC 4513, a server SHOULD by default fail
                 * Unauthenticated Bind requests with a resultCode of
                 * unwillingToPerform. */
                send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                                 "Unauthenticated binds are not allowed", 0, NULL);
                /* increment BindSecurityErrorcount */
                slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                goto free_and_return;
            }
        /* Check if simple binds are allowed over an insecure channel.  We only check
         * this for authenticated binds. */
        } else if (config_get_require_secure_binds() == 1) {
            Connection *conn = NULL;
            int sasl_ssf = 0;
            int local_ssf = 0;

            /* Allow simple binds only for SSL/TLS established connections
             * or connections using SASL privacy layers */
            conn = pb->pb_conn;
            if ( slapi_pblock_get(pb, SLAPI_CONN_SASL_SSF, &sasl_ssf) != 0) {
                slapi_log_error( SLAPI_LOG_PLUGIN, "do_bind",
                                 "Could not get SASL SSF from connection\n" );
                sasl_ssf = 0;
            }

            if ( slapi_pblock_get(pb, SLAPI_CONN_LOCAL_SSF, &local_ssf) != 0) {
                slapi_log_error( SLAPI_LOG_PLUGIN, "do_bind",
                                 "Could not get local SSF from connection\n" );
                local_ssf = 0;
            }

            if (((conn->c_flags & CONN_FLAG_SSL) != CONN_FLAG_SSL) &&
                (sasl_ssf <= 1) && (local_ssf <= 1)) {
                send_ldap_result(pb, LDAP_CONFIDENTIALITY_REQUIRED, NULL,
                                 "Operation requires a secure connection", 0, NULL);
                slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                goto free_and_return;
            }
        }
        break;
    default:
        break;
    }

    /*
     * handle binds as the manager here, pass others to the backend
     */

    if ( isroot && method == LDAP_AUTH_SIMPLE ) {
        if (cred.bv_len != 0) {
            /* a passwd was supplied -- check it */
            Slapi_Value cv;
            slapi_value_init_berval(&cv,&cred);

            /*
             *  Call pre bind root dn plugin for checking root dn access control.
             *
             *  Do this before checking the password so that we give a consistent error,
             *  regardless if the password is correct or not.  Or else it would still be
             *  possible to brute force guess the password even though access would still
             *  be denied.
             */
            if (plugin_call_plugins(pb, SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN) != 0){
                send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                    "RootDN access control violation", 0, NULL );
                slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                value_done(&cv);
                goto free_and_return;
            }
            /*
             *  Check the dn and password
             */
            if ( is_root_dn_pw( slapi_sdn_get_ndn(sdn), &cv )) {
                /*
                 *  right dn and passwd - authorize
                 */
                bind_credentials_set( pb->pb_conn, SLAPD_AUTH_SIMPLE, slapi_ch_strdup(slapi_sdn_get_ndn(sdn)),
                                      NULL, NULL, NULL , NULL);
            } else {
                /*
                 *  right dn, wrong passwd - reject with invalid credentials
                 */
                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "Invalid credentials");
                send_ldap_result( pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL );
                /* increment BindSecurityErrorcount */
                slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                value_done(&cv);
                goto free_and_return;
            }

            value_done(&cv);
        }

        /* call preop plugin */
        if (plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_BIND_FN ) == 0){
            if ( auth_response_requested ) {
                slapi_add_auth_response_control( pb,
                                           ( cred.bv_len == 0 ) ? "" :
                                           slapi_sdn_get_ndn(sdn));
            }
            send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

            /* call postop plugins */
            plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
        } else {
            send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "", 0, NULL);
        }
        goto free_and_return;
    }

    /* We could be serving multiple database backends.  Select the appropriate one */
    if (slapi_mapping_tree_select(pb, &be, &referral, NULL, 0) != LDAP_SUCCESS) {
        send_nobackend_ldap_result( pb );
        be = NULL;
        goto free_and_return;
    }

    if (referral) {
        send_referrals_from_entry(pb,referral);
        slapi_entry_free(referral);
        goto free_and_return;
    }

    slapi_pblock_set( pb, SLAPI_BACKEND, be );

    /* not root dn - pass to the backend */
    if ( be->be_bind != NULL ) {
        original_sdn = slapi_sdn_dup(sdn);
        /*
         * call the pre-bind plugins. if they succeed, call
         * the backend bind function. then call the post-bind
         * plugins.
         */
        if ( plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_BIND_FN ) == 0 )  {
            int sdn_updated = 0;
            rc = 0;

            /* Check if a pre_bind plugin mapped the DN to another backend */
            Slapi_DN *pb_sdn;
            slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &pb_sdn);
            if (!pb_sdn) {
                slapi_create_errormsg(errorbuf, sizeof(errorbuf), "Pre-bind plug-in set NULL dn\n");
                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errorbuf);
                send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "", 0, NULL);
                goto free_and_return;
            } else if ((pb_sdn != sdn) || (sdn_updated = slapi_sdn_compare(original_sdn, pb_sdn))) {
                /*
                 * Slapi_DN set in pblock was changed by a pre bind plug-in.
                 * It is a plug-in's responsibility to free the original Slapi_DN.
                 */
                sdn = pb_sdn;
                dn = slapi_sdn_get_dn(sdn);
                if (!dn) {
                    char *udn = slapi_sdn_get_udn(sdn);
                    slapi_create_errormsg(errorbuf, sizeof(errorbuf), "Pre-bind plug-in set corrupted dn %s\n", udn?udn:"");
                    slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errorbuf);
                    send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "", 0, NULL);
                    goto free_and_return;
                }
                if (!sdn_updated) { /* pb_sdn != sdn; need to compare the dn's. */
                    sdn_updated = slapi_sdn_compare(original_sdn, sdn);
                }
                if (sdn_updated) { /* call slapi_be_select only when the DN is updated. */
                    slapi_be_Unlock(be);
                    be = slapi_be_select_exact(sdn);
                    if (be) {
                        slapi_be_Rlock(be);
                        slapi_pblock_set( pb, SLAPI_BACKEND, be );
                    } else {
                        slapi_create_errormsg(errorbuf, sizeof(errorbuf), "No matching backend for %s\n", dn);
                        slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errorbuf);
                        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, "", 0, NULL);
                        goto free_and_return;
                    }
                }
            }

            /*
             * Is this account locked ?
             *	could be locked through the account inactivation
             *	or by the password policy
             *
             * rc=0: account not locked
             * rc=1: account locked, can not bind, result has been sent
             * rc!=0 and rc!=1: error. Result was not sent, lets be_bind
             * 		deal with it.
             *
             */

            /* get the entry now, so that we can give it to slapi_check_account_lock and reslimit_update_from_dn */
            if (! slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
                bind_target_entry = get_entry(pb,  slapi_sdn_get_ndn(sdn));
                rc = slapi_check_account_lock ( pb, bind_target_entry, pw_response_requested, 1, 1);
            }

            slapi_pblock_set( pb, SLAPI_PLUGIN, be->be_database );
            set_db_default_result_handlers(pb);
            if ( (rc != 1) && 
                 (auto_bind || 
                  (((rc = (*be->be_bind)( pb )) == SLAPI_BIND_SUCCESS) ||
                   (rc == SLAPI_BIND_ANONYMOUS))) ) {
                long t;
                char* authtype = NULL;
                /* rc is SLAPI_BIND_SUCCESS or SLAPI_BIND_ANONYMOUS */
                if(auto_bind) {
                    rc = SLAPI_BIND_SUCCESS;
                }

                switch ( method ) {
                case LDAP_AUTH_SIMPLE:
                    if (cred.bv_len != 0) {
                        authtype = SLAPD_AUTH_SIMPLE;
                    }
#if defined(ENABLE_AUTOBIND)
                    else if(auto_bind) {
                        authtype = SLAPD_AUTH_OS;
                    }
#endif /* ENABLE_AUTOBIND */
                    else {
                        authtype = SLAPD_AUTH_NONE;
                    }
                    break;
                case LDAP_AUTH_SASL:
                    /* authtype = SLAPD_AUTH_SASL && saslmech: */
                    PR_snprintf(authtypebuf, sizeof(authtypebuf), "%s%s", SLAPD_AUTH_SASL, saslmech);
                    authtype = authtypebuf;
                    break;
                default:
                    break;
                }

                if ( rc == SLAPI_BIND_SUCCESS ) {
                    int myrc = 0;
                    if (!auto_bind) {
                        /* 
                         * There could be a race that bind_target_entry was not added 
                         * when bind_target_entry was retrieved before be_bind, but it
                         * was in be_bind.  Since be_bind returned SLAPI_BIND_SUCCESS,
                         * the entry is in the DS.  So, we need to retrieve it once more.
                         */
                        if (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA) && 
                            !bind_target_entry) {
                            bind_target_entry = get_entry(pb, slapi_sdn_get_ndn(sdn));
                            if (bind_target_entry) {
                                myrc = slapi_check_account_lock(pb, bind_target_entry,
                                                              pw_response_requested, 1, 1);
                                if (1 == myrc) { /* account is locked */
                                    goto account_locked;
                                }
                            } else {
                                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "No such entry");
                                send_ldap_result(pb, LDAP_INVALID_CREDENTIALS, NULL, "", 0, NULL);
                                goto free_and_return;
                            }
                        }
                        bind_credentials_set(pb->pb_conn, authtype,
                                             slapi_ch_strdup(slapi_sdn_get_ndn(sdn)),
                                             NULL, NULL, NULL, bind_target_entry);
                        if (!slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
                            /* check if need new password before sending 
                               the bind success result */
                            myrc = need_new_pw(pb, &t, bind_target_entry, pw_response_requested);
                            switch (myrc) {
                            case 1:
                                (void)slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
                                break;
                            case 2:
                                (void)slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRING, t);
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    if (auth_response_requested) {
                        slapi_add_auth_response_control(pb, slapi_sdn_get_ndn(sdn));
                    }
                    if (-1 == myrc) {
                        /* need_new_pw failed; need_new_pw already send_ldap_result in it. */
                        goto free_and_return;
                    } 
                } else {	/* anonymous */
                    /* set bind creds here so anonymous limits are set */
                    bind_credentials_set(pb->pb_conn, authtype, NULL, NULL, NULL, NULL, NULL);

                    if ( auth_response_requested ) {
                        slapi_add_auth_response_control(pb, "");
                    }
                }
            } else {
account_locked:                
                if(cred.bv_len == 0) {
                    /* its an UnAuthenticated Bind, DN specified but no pw */
                    slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds);
                }else{
                    /* password must have been invalid */
                    /* increment BindSecurityError count */
                    slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
                }
            }

            /*
             * if rc != SLAPI_BIND_SUCCESS and != SLAPI_BIND_ANONYMOUS,
             * the result has already been sent by the backend.  otherwise,
             * we assume it is success and send it here to avoid a race
             * condition where the client could be told by the
             * backend that the bind succeeded before we set the
             * c_dn field in the connection structure here in
             * the front end.
             */
            if ( rc == SLAPI_BIND_SUCCESS || rc == SLAPI_BIND_ANONYMOUS) {
                send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );
            }

            slapi_pblock_set( pb, SLAPI_PLUGIN_OPRETURN, &rc );
            plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
        } else {
            /* even though preop failed, we should still call the post-op plugins */
            plugin_call_plugins( pb, SLAPI_PLUGIN_POST_BIND_FN );
        }
    } else {
        send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                          "Function not implemented", 0, NULL );
    }

free_and_return:;
    slapi_sdn_free(&original_sdn);
    if (be) {
        slapi_be_Unlock(be);
    }
    if (bind_sdn_in_pb) {
        slapi_pblock_get(pb, SLAPI_BIND_TARGET_SDN, &sdn);
    }
    slapi_sdn_free(&sdn);
    slapi_ch_free_string( &saslmech );
    slapi_ch_free( (void **)&cred.bv_val );
    slapi_entry_free(bind_target_entry);
}


/*
 * register all of the LDAPv3 SASL mechanisms we know about.
 */
void
init_saslmechanisms( void )
{
    ids_sasl_init();
    slapi_register_supported_saslmechanism( LDAP_SASL_EXTERNAL );
}

static void
log_bind_access (
    Slapi_PBlock *pb, 
    const char* dn, 
    ber_tag_t method, 
    int version,
    const char *saslmech,
    const char *msg
)
{
    if (method == LDAP_AUTH_SASL && saslmech && msg) {
        slapi_log_access( LDAP_DEBUG_STATS, 
                          "conn=%" NSPRIu64 " op=%d BIND dn=\"%s\" "
                          "method=sasl version=%d mech=%s, %s\n",
                          pb->pb_conn->c_connid, pb->pb_op->o_opid, dn,
                          version, saslmech, msg );
    } else if (method == LDAP_AUTH_SASL && saslmech) {
        slapi_log_access( LDAP_DEBUG_STATS, 
                          "conn=%" NSPRIu64 " op=%d BIND dn=\"%s\" "
                          "method=sasl version=%d mech=%s\n",
                          pb->pb_conn->c_connid, pb->pb_op->o_opid, dn,
                          version, saslmech );
    } else if (msg) {
        slapi_log_access( LDAP_DEBUG_STATS, 
                          "conn=%" NSPRIu64 " op=%d BIND dn=\"%s\" "
                          "method=%" BERTAG_T " version=%d, %s\n",
                          pb->pb_conn->c_connid, pb->pb_op->o_opid, dn,
                          method, version, msg );
    } else {
        slapi_log_access( LDAP_DEBUG_STATS, 
                          "conn=%" NSPRIu64 " op=%d BIND dn=\"%s\" "
                          "method=%" BERTAG_T " version=%d\n",
                          pb->pb_conn->c_connid, pb->pb_op->o_opid, dn,
                          method, version );
    }
}
