/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/************************************************************

 testsaslbind.c

 This source file provides an example of a pre-operation plug-in
 function for SASL authentication with LDAP bind operations.
 The function demonstrates how to send credentials back to the 
 client in cases where mutual authentication is required.

 This plugin responds to SASL bind requests with a mechanism
 name of "babsmechanism".  Simple binds and other SASL mechanisms
 should not be affected by the presence of this plugin.

 Binds with our mechanism always succeed (which is not very secure!)

 Note that the Directory Server front-end handles bind
 operations requested by the root DN. The server does not
 invoke your plug-in function if the client is authenticating 
 as the root DN.

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

dn: cn=test-saslbind,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: test-saslbind
nsslapd-pluginpath: /usr/brandx/servers/plugins/slapd/slapi/examples/libtest-plugin.so
nsslapd-plugininitfunc: testsasl_init
nsslapd-plugintype: preoperation
nsslapd-pluginenabled: on
nsslapd-pluginid: test-saslbind
nsslapd-pluginversion: 5.0
nsslapd-pluginvendor: Brandx, Inc.
nsslapd-plugindescription: sample SASL bind pre-operation plugin


 ************************************************************/
#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

Slapi_PluginDesc saslpdesc = { "test-saslbind", "Brandx, Inc.", "7.1",
	"sample SASL bind pre-operation plugin" };


#define TEST_SASL_MECHANISM	"babsmechanism"
#define TEST_SASL_AUTHMETHOD	SLAPD_AUTH_SASL TEST_SASL_MECHANISM

/* Pre-operation plug-in function */
int
testsasl_bind( Slapi_PBlock *pb )
{
	char		*target;
	int		method;
	char		*mechanism;
	struct berval	*credentials;
	struct berval	svrcreds;

	/* Log a message to the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testsasl_bind", 
		"Pre-operation bind function called.\n" );

	/* Gets parameters available when processing an LDAP bind
	   operation. */
	if ( slapi_pblock_get( pb, SLAPI_BIND_TARGET, &target ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &credentials ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_SASLMECHANISM, &mechanism )
	    != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, "testsasl_bind",
			"Could not get parameters for bind operation\n" );
		return( 0 );	/* let the server try other mechanisms */
	}

	/* Check to see if the mechanism being used is ours. */
	if ( mechanism == NULL
	    || strcmp( mechanism, TEST_SASL_MECHANISM ) != 0 ) {
		return( 0 );	/* let the server try other mechanisms */
	}

	/*
	 * Set the DN and authentication method for the connection.
	 * Binds with our mechanism always succeed (which is not very secure!)
	 */
	if ( slapi_pblock_set( pb, SLAPI_CONN_DN, slapi_ch_strdup( target ) )
	    != 0 || slapi_pblock_set( pb, SLAPI_CONN_AUTHMETHOD,
	    TEST_SASL_AUTHMETHOD ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, "testsasl_bind",
		    "Failed to set DN and method for connection\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    NULL, 0, NULL );
		return( 1 );	/* done -- sent result to client */
	}

	/* Set the credentials that should be returned to the client. */
	svrcreds.bv_val = "my credentials";
	svrcreds.bv_len = sizeof("my credentials") - 1;
	if ( slapi_pblock_set( pb, SLAPI_BIND_RET_SASLCREDS, &svrcreds )
	    != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testsasl_bind", 
			"Could not set credentials to return to client\n" );
		slapi_pblock_set( pb, SLAPI_CONN_DN, NULL );
		slapi_pblock_set( pb, SLAPI_CONN_AUTHMETHOD, SLAPD_AUTH_NONE );
		return( 0 );	/* let the server try other mechanisms */
	}

	/* Send the credentials back to the client. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testsasl_bind",
	    "Authenticated: %s\n", target );
	slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

	return( 1 );	/* done -- sent result to client */
}

/* Initialization function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testsasl_init( Slapi_PBlock *pb )
{
	/* Register the pre-operation bind function and specify
	   the server plug-in version. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&saslpdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN, (void *) testsasl_bind ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testsasl_init",
			"Failed to set version and function\n" );
		return( -1 );
	}

	/* Register the SASL mechanism. */
	slapi_register_supported_saslmechanism( TEST_SASL_MECHANISM );
	return( 0 );
}

