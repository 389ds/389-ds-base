/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/************************************************************

 testpreop.c

 This source file provides examples of pre-operation plug-in
 functions.  The server calls these plug-in functions before
 executing certain LDAP operations:

 * testpreop_bind (called before an LDAP bind operation)
 * testpreop_add (called before an LDAP add operation)
 * testpreop_abandon (called before an LDAP abandon operation)

 testpreop_bind logs information about the LDAP bind operation
 to the server error log.  testpreop_add prepends the name "BOB"
 to the value of the cn attribute before an entry is added.  

 Note that the Directory Server front-end handles bind
 operations requested by the root DN. The server does not
 invoke your plug-in function if the client is authenticating 
 as the root DN. 

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

 dn: cn=Test Preop,cn=plugins,cn=config
 objectClass: top
 objectClass: nsSlapdPlugin
 objectClass: extensibleObject
 cn: Test Preop
 nsslapd-pluginPath: <server_root>/plugins/slapd/slapi/examples/libtest-plugin.so
 nsslapd-pluginInitfunc: testpreop_init
 nsslapd-pluginType: preoperation
 nsslapd-pluginEnabled: on
 nsslapd-plugin-depends-on-type: database
 nsslapd-pluginId: test-preop

 ************************************************************/

#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

Slapi_PluginDesc preoppdesc = { "test-preop", "Netscape", "0.5",
	"sample pre-operation plugin" };

/* Pre-operation plug-in function */
int
testpreop_bind( Slapi_PBlock *pb )
{
	char	*dn;
	int	method;
	char	*auth;

	/* Get the DN that the client is binding as and the method
	   of authentication used. */
	if ( slapi_pblock_get( pb, SLAPI_BIND_TARGET, &dn ) != 0 || 
	     slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		"testpreop_bind", "Could not get parameters\n" );
		return( -1 );
	}

	switch( method ) {
	case LDAP_AUTH_NONE:
		auth = "No authentication";
		break;
	case LDAP_AUTH_SIMPLE:
		auth = "Simple authentication";
		break;
	case LDAP_AUTH_SASL:
		auth = "SASL authentication";
		break;
	default:
		auth = "Unknown method of authentication";
		break;
	}

	/* Log information about the bind operation to the
	   server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpreop_bind",  
		"Preoperation bind function called.\n" 
		"\tTarget DN: %s\n\tAuthentication method: %s\n",
		dn, auth );

	return( 0 );	/* allow the operation to continue */
}

/* Pre-operation plug-in function */
int
testpreop_add( Slapi_PBlock *pb )
{
	Slapi_Entry	*e;
	Slapi_Attr	*a;
	Slapi_Value	*v;
	struct berval	**bvals;
	int		i, hint;
	char		*tmp;
	const char	*s;

	/* Get the entry that is about to be added. */
	if ( slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &e ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testpreop_add", "Could not get entry\n" );
		return( -1 );
	}

	/* Prepend the name "BOB" to the value of the cn attribute
	   in the entry. */
	if ( slapi_entry_attr_find( e, "cn", &a ) == 0 ) {
		for ( hint = slapi_attr_first_value( a, &v ); hint != -1;
				hint = slapi_attr_next_value( a, hint, &v )) {
			s = slapi_value_get_string( v );
			tmp = (char *) malloc( 5 + strlen( s ));
			strcpy( tmp, "BOB " );
			strcat( tmp + 4, s );
			slapi_value_set_string( v, tmp );
			free( tmp );
		}
	}

	return( 0 );	/* allow the operation to continue */
}


/* Pre-operation plug-in function */
int
testpreop_abandon( Slapi_PBlock *pb )
{
	int	msgid;

	/* Get the LDAP message ID of the abandon target */
	if ( slapi_pblock_get( pb, SLAPI_ABANDON_MSGID, &msgid ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		"testpreop_abandon", "Could not get parameters\n" );
		return( -1 );
	}

	/* Log information about the abandon operation to the
	   server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpreop_bind",  
		"Preoperation abandon function called.\n" 
		"\tTarget MsgID: %d\n",
		msgid );

	return( 0 );	/* allow the operation to continue */
}


static void
get_plugin_config_dn_and_entry( char *msg, Slapi_PBlock *pb )
{
	char		*dn = NULL;
	Slapi_Entry	*e = NULL;
	int			loglevel = SLAPI_LOG_PLUGIN;

	if ( slapi_pblock_get( pb, SLAPI_TARGET_DN, &dn ) != 0 || dn == NULL ) {
		slapi_log_error( loglevel, msg, "failed to get plugin config DN\n" );
	} else {
		slapi_log_error( loglevel, msg, "this plugin's config DN is \"%s\"\n",
				dn );
	}

	if ( slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &e ) != 0 || e == NULL ) {
		slapi_log_error( loglevel, msg, "failed to get plugin config entry\n" );
	} else {
		char	*ldif;

		ldif = slapi_entry2str_with_options( e, NULL, 0 );
		slapi_log_error( loglevel, msg,
				"this plugin's config entry is \"\n%s\"\n", ldif );
		slapi_ch_free_string( &ldif );
	}
}

static int
testpreop_start( Slapi_PBlock *pb )
{
	get_plugin_config_dn_and_entry( "testpreop_start", pb );
	return( 0 );
}

/* Initialization function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testpreop_init( Slapi_PBlock *pb )
{
	/* Register the two pre-operation plug-in functions,
	   and specify the server plug-in version. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&preoppdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
	    (void *) testpreop_start ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN,
	    (void *) testpreop_bind ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ADD_FN,
	    (void *) testpreop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ABANDON_FN,
	    (void *) testpreop_abandon ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testpreop_init",
			"Failed to set version and function\n" );
		return( -1 );
	}

	return( 0 );
}
