/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/************************************************************

 testbind.c

 This source file provides an example of a pre-operation plug-in
 function that handles authentication.

 Note that the Directory Server front-end handles bind
 operations requested by the root DN. The server does not
 invoke your plug-in function if the client is authenicating 
 as the root DN.

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

 dn: cn=Test Bind,cn=plugins,cn=config
 objectClass: top
 objectClass: nsSlapdPlugin
 objectClass: extensibleObject
 cn: Test Bind
 nsslapd-pluginPath: <server_root>/plugins/slapd/slapi/examples/libtest-plugin.so
 nsslapd-pluginInitfunc: testbind_init
 nsslapd-pluginType: preoperation
 nsslapd-pluginEnabled: on
 nsslapd-plugin-depends-on-type: database
 nsslapd-pluginId: test-bind

 ************************************************************/
#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

Slapi_PluginDesc bindpdesc = { "test-bind", "Netscape", "0.5",
	"sample bind pre-operation plugin" };

static Slapi_ComponentId *plugin_id = NULL;




/* Pre-operation plug-in function */
int
test_bind( Slapi_PBlock *pb )
{
	char			*dn, *attrs[2] = { SLAPI_USERPWD_ATTR, NULL };
	int			method, rc = LDAP_SUCCESS;
	struct berval		*credentials;
	struct berval		**pwvals;
    	Slapi_DN		*sdn = NULL;
	Slapi_Entry		*e = NULL;
	Slapi_Attr		*attr = NULL;

	/* Log a message to the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind", 
		"Pre-operation bind function called.\n" );

	/* Gets parameters available when processing an LDAP bind
	   operation. */
	if ( slapi_pblock_get( pb, SLAPI_BIND_TARGET, &dn ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &credentials ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
			"Could not get parameters for bind operation\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, 
			NULL, NULL, 0, NULL );
		return( 1 );
	}

	/* Check the authentication method */
	switch( method ) {
	case LDAP_AUTH_SIMPLE:
		/* First, get the entry specified by the DN. */
		sdn = slapi_sdn_new_dn_byref( dn );
		rc = slapi_search_internal_get_entry( sdn, attrs, &e,
			    plugin_id );
		slapi_sdn_free( &sdn );

		if ( rc != LDAP_SUCCESS ) {
			slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
					"Could not find entry %s (error %d)\n", 
					dn, rc );
			break;
		}

		/* Next, check credentials against the userpassword attribute
		   of that entry. */
		if ( e != NULL ) {
			Slapi_Value	*credval, **pwvals;
			int		i, hint, valcount;

			
			if ( slapi_entry_attr_find( e, SLAPI_USERPWD_ATTR,
			    &attr ) != 0 || slapi_attr_get_numvalues( attr,
			    &valcount ) != 0 ) {
				slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
					"Entry has no %s attribute values\n",
					SLAPI_USERPWD_ATTR );
				rc = LDAP_INAPPROPRIATE_AUTH;
				break;
			}

			credval = slapi_value_new_berval( credentials );
			pwvals = (Slapi_Value **)slapi_ch_calloc( valcount,
			    sizeof( Slapi_Value * ));
			i = 0;
			for ( hint = slapi_attr_first_value( attr, &pwvals[i] );
			    hint != -1; hint = slapi_attr_next_value( attr,
			    hint, &pwvals[i] )) {
				++i;
			}

			if ( slapi_pw_find_sv( pwvals, credval ) != 0 ) {
				slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
					"Credentials are not correct\n" );
				rc = LDAP_INVALID_CREDENTIALS;
			}

			slapi_value_free( &credval );
			slapi_ch_free( (void **)&pwvals );

			if ( LDAP_SUCCESS != rc ) {
			    break;
			}
		} else {
			/* This should not happen. The previous section of code 
			   already checks for this case. */
			slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
				"Could find entry for %s\n", dn );
			rc = LDAP_NO_SUCH_OBJECT;
			break;
		}

		/* Set the DN and authentication method for the connection. */
		if ( slapi_pblock_set( pb, SLAPI_CONN_DN, 
			slapi_ch_strdup( dn ) ) != 0 ||
		     slapi_pblock_set( pb, SLAPI_CONN_AUTHMETHOD, 
			SLAPD_AUTH_SIMPLE ) != 0 ) {

			slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
				"Failed to set DN and method for connection\n" );
			rc = LDAP_OPERATIONS_ERROR;
			break;
		}

		/* Send a "success" result code back to the client. */
		slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind", 
			"Authenticated: %s\n", dn );
		rc = LDAP_SUCCESS;
		break;

	/* If NONE is specified, the client is requesting to bind anonymously.
	   Normally, this case should be handled by the server's front-end
	   before it calls this plug-in function.  Just in case this does
	   get through to the plug-in function, you can handle this by
	   sending a successful result code back to the client and returning
	   1.
	 */
	case LDAP_AUTH_NONE:
		slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind", 
			"Authenticating anonymously\n" );
		rc = LDAP_SUCCESS;
		break;

	/* This plug-in does not support any other method of authentication */
	case LDAP_AUTH_SASL:
	default:
		slapi_log_error( SLAPI_LOG_PLUGIN, "test_bind",
			"Unsupported authentication method requested: %d\n",
			method );
		rc = LDAP_AUTH_METHOD_NOT_SUPPORTED;
		break;
	}

	slapi_send_ldap_result( pb, rc, NULL, NULL, 0, NULL );
	return( 1 );
}

/* Pre-operation plug-in function */
int
test_search( Slapi_PBlock *pb )
{
	char		*reqdn;

	/* Log a message to the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "test_search", 
		"Pre-operation search function called.\n" );

	/* Get requestor of search operation.  This is not critical
	   to performing the search (this plug-in just serves as 
	   confirmation that the bind plug-in works), so return 0 
	   if this fails. */
	if ( slapi_pblock_get( pb, SLAPI_REQUESTOR_DN, &reqdn ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "test_search",
		"Could not get requestor parameter for search operation\n" );
		return( 0 );
	}

	/* Indicate who is requesting the search */
	if ( reqdn != NULL && *reqdn != '\0' ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "test_search",
			"Search requested by %s\n", reqdn );
	} else {
		slapi_log_error( SLAPI_LOG_PLUGIN, "test_search",
			"Search requested by anonymous client\n" );
	}
	return( 0 );
}

/* Initialization function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testbind_init( Slapi_PBlock *pb )
{

	/* Retrieve and save the plugin identity to later pass to
	   internal operations */
	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_IDENTITY, &plugin_id ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testbind_init",
			"Failed to retrieve SLAPI_PLUGIN_IDENTITY\n" );
		return( -1 );
	}

	/* Register the pre-operation bind function and specify
	   the server plug-in version. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, 
		SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, 
		(void *)&bindpdesc ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN, 
		(void *) test_bind ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_SEARCH_FN, 
		(void *) test_search ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "testbind_init",
			"Failed to set version and functions\n" );
		return( -1 );
	}

	return( 0 );
}

